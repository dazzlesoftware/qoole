// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Vulkan/VulkanRHI.h"

#include <cstring>

using namespace GenesisVk;

void FVulkanCommandList::Begin(VkCommandBuffer InCommandBuffer)
{
	CommandBuffer = InCommandBuffer;
	bInRenderPass = false;
	bSkipPass = false;
	ResetState();
}

void FVulkanCommandList::End()
{
	if (!ensuref(!bInRenderPass, "Vulkan: EndFrame inside a render pass"))
	{
		EndRenderPass();
	}
	ResetState();
	CommandBuffer = VK_NULL_HANDLE;
}

void FVulkanCommandList::ResetState()
{
	Pipeline = nullptr;
	Texture = nullptr;
	bDescriptorDirty = true;
	bPushConstantsDirty = true; // a new command buffer starts with undefined push constants
}

void FVulkanCommandList::BeginRenderPass(FRHISwapChain& InTarget, const TOptional<FLinearColor>& ClearColor)
{
	check(CommandBuffer && !bInRenderPass);
	FVulkanSwapChain& Target = static_cast<FVulkanSwapChain&>(InTarget);
	bInRenderPass = true;
	if (!Target.AcquireNextImage(Device.GetFrameIndex()))
	{
		bSkipPass = true; // minimized or lost; drop this pass's commands
		return;
	}

	// The first pass into this frame's image discards the old contents; later
	// passes load what the earlier ones drew.
	const VkImageLayout InitialLayout = Target.bRenderedThisFrame ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
	const VkRenderPass RenderPass = Device.GetRenderPass(Target.GetVkFormat(), ClearColor.has_value(), InitialLayout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	Target.bRenderedThisFrame = true;
	const VkExtent2D Extent = Target.GetExtent();
	BeginPass(RenderPass, Target.GetCurrentFramebuffer(), Extent.width, Extent.height, ClearColor);
}

void FVulkanCommandList::BeginRenderPass(FRHITexture& InTarget, const TOptional<FLinearColor>& ClearColor)
{
	check(CommandBuffer && !bInRenderPass);
	FVulkanTexture& Target = static_cast<FVulkanTexture&>(InTarget);
	bInRenderPass = true;
	if (!ensuref(Target.GetDesc().bRenderTarget, "Vulkan: BeginRenderPass on a texture created without bRenderTarget"))
	{
		bSkipPass = true;
		return;
	}
	const VkRenderPass RenderPass = Device.GetRenderPass(Target.GetVkFormat(), ClearColor.has_value(),
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	const VkFramebuffer Framebuffer = Target.GetFramebuffer(RenderPass);
	if (!RenderPass || !Framebuffer)
	{
		bSkipPass = true;
		return;
	}
	BeginPass(RenderPass, Framebuffer, Target.GetWidth(), Target.GetHeight(), ClearColor);
}

void FVulkanCommandList::BeginRenderPass(FRHITexture& InTarget, FRHITexture& InDepthTarget, const TOptional<FLinearColor>& ClearColor, const TOptional<float>& ClearDepth)
{
	check(CommandBuffer && !bInRenderPass);
	FVulkanTexture& Target = static_cast<FVulkanTexture&>(InTarget);
	FVulkanTexture& Depth = static_cast<FVulkanTexture&>(InDepthTarget);
	bInRenderPass = true;
	if (!ensuref(Target.GetDesc().bRenderTarget && IsDepthFormat(Depth.GetFormat())
		&& Depth.GetWidth() == Target.GetWidth() && Depth.GetHeight() == Target.GetHeight(),
		"Vulkan: BeginRenderPass needs a render target and a depth buffer of the same size"))
	{
		bSkipPass = true;
		return;
	}
	const VkRenderPass RenderPass = Device.GetRenderPass(Target.GetVkFormat(), ClearColor.has_value(),
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, Depth.GetVkFormat(), ClearDepth.has_value());
	const VkFramebuffer Framebuffer = Target.GetFramebuffer(RenderPass, &Depth);
	if (!RenderPass || !Framebuffer)
	{
		bSkipPass = true;
		return;
	}
	BeginPass(RenderPass, Framebuffer, Target.GetWidth(), Target.GetHeight(), ClearColor, true, ClearDepth);
}

void FVulkanCommandList::BeginPass(VkRenderPass RenderPass, VkFramebuffer Framebuffer, uint32 Width, uint32 Height, const TOptional<FLinearColor>& ClearColor,
	bool bHasDepth, const TOptional<float>& ClearDepth)
{
	// Clear values are indexed by attachment; unused ones are ignored.
	VkClearValue ClearValues[2] = {};
	if (ClearColor)
	{
		ClearValues[0].color.float32[0] = ClearColor->R;
		ClearValues[0].color.float32[1] = ClearColor->G;
		ClearValues[0].color.float32[2] = ClearColor->B;
		ClearValues[0].color.float32[3] = ClearColor->A;
	}
	ClearValues[1].depthStencil.depth = ClearDepth.value_or(1.0f);
	const uint32 ClearCount = bHasDepth && ClearDepth ? 2u : (ClearColor ? 1u : 0u);
	VkRenderPassBeginInfo BeginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	BeginInfo.renderPass = RenderPass;
	BeginInfo.framebuffer = Framebuffer;
	BeginInfo.renderArea = { { 0, 0 }, { Width, Height } };
	BeginInfo.clearValueCount = ClearCount;
	BeginInfo.pClearValues = ClearCount ? ClearValues : nullptr;
	vkCmdBeginRenderPass(CommandBuffer, &BeginInfo, VK_SUBPASS_CONTENTS_INLINE);

	PassSize = FIntPoint(static_cast<int32>(Width), static_cast<int32>(Height));
	SetViewport(0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height));
	SetScissor(FIntRect(0, 0, PassSize.X, PassSize.Y));
}

void FVulkanCommandList::EndRenderPass()
{
	check(bInRenderPass);
	if (!bSkipPass)
	{
		vkCmdEndRenderPass(CommandBuffer);
	}
	bInRenderPass = false;
	bSkipPass = false;
}

void FVulkanCommandList::SetViewport(float X, float Y, float Width, float Height)
{
	if (bSkipPass)
	{
		return;
	}
	// Negative height (Vulkan 1.1 / VK_KHR_maintenance1) flips Y so clip space
	// matches D3D (+Y up): the same HLSL works on both backends, and vertex
	// shaders must NOT be compiled with -fvk-invert-y.
	VkViewport Viewport{};
	Viewport.x = X;
	Viewport.y = Y + Height;
	Viewport.width = Width;
	Viewport.height = -Height;
	Viewport.minDepth = 0.0f;
	Viewport.maxDepth = 1.0f;
	vkCmdSetViewport(CommandBuffer, 0, 1, &Viewport);
}

void FVulkanCommandList::SetScissor(const FIntRect& Rect)
{
	if (bSkipPass)
	{
		return;
	}
	const int32 Left = std::clamp(Rect.Left, 0, PassSize.X);
	const int32 Top = std::clamp(Rect.Top, 0, PassSize.Y);
	const int32 Right = std::clamp(Rect.Right, Left, PassSize.X);
	const int32 Bottom = std::clamp(Rect.Bottom, Top, PassSize.Y);
	VkRect2D Scissor{ { Left, Top }, { static_cast<uint32>(Right - Left), static_cast<uint32>(Bottom - Top) } };
	vkCmdSetScissor(CommandBuffer, 0, 1, &Scissor);
}

void FVulkanCommandList::SetPipeline(FRHIGraphicsPipeline& InPipeline)
{
	FVulkanGraphicsPipeline* NewPipeline = &static_cast<FVulkanGraphicsPipeline&>(InPipeline);
	if (NewPipeline == Pipeline)
	{
		return;
	}
	// The two pipeline layouts (linear / point sampler) are not compatible for
	// set 0, so switching rebinds the texture; push constants are re-pushed too.
	if (!Pipeline || Pipeline->GetDesc().SamplerFilter != NewPipeline->GetDesc().SamplerFilter)
	{
		bDescriptorDirty = true;
		bPushConstantsDirty = true;
	}
	Pipeline = NewPipeline;
	vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline->GetHandle());
}

void FVulkanCommandList::SetPushConstants(const void* Data, uint32 Size)
{
	if (!ensuref(Size <= FVulkanDevice::MaxPushConstantSize && Size % 4 == 0, "Vulkan: push constants must be a multiple of 4 bytes, at most 64"))
	{
		return;
	}
	std::memcpy(PushConstantData, Data, Size);
	bPushConstantsDirty = true;
}

void FVulkanCommandList::SetTexture(FRHITexture& InTexture)
{
	FVulkanTexture* NewTexture = &static_cast<FVulkanTexture&>(InTexture);
	if (NewTexture != Texture)
	{
		Texture = NewTexture;
		bDescriptorDirty = true;
	}
}

void FVulkanCommandList::SetVertexBuffer(FRHIBuffer& Buffer, uint64 Offset, uint32 Stride)
{
	(void)Stride; // part of the pipeline in Vulkan 1.1
	const VkBuffer Handle = static_cast<FVulkanBuffer&>(Buffer).GetHandle();
	const VkDeviceSize VkOffset = Offset;
	vkCmdBindVertexBuffers(CommandBuffer, 0, 1, &Handle, &VkOffset);
}

void FVulkanCommandList::SetIndexBuffer(FRHIBuffer& Buffer, uint64 Offset, EIndexFormat Format)
{
	vkCmdBindIndexBuffer(CommandBuffer, static_cast<FVulkanBuffer&>(Buffer).GetHandle(), Offset,
		Format == EIndexFormat::UInt16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
}

void FVulkanCommandList::DrawIndexed(uint32 IndexCount, uint32 FirstIndex, int32 BaseVertex)
{
	if (bSkipPass || IndexCount == 0)
	{
		return;
	}
	if (!ensuref(bInRenderPass && Pipeline, "Vulkan: DrawIndexed needs a render pass and a pipeline"))
	{
		return;
	}

	const ESamplerFilter Filter = Pipeline->GetDesc().SamplerFilter;
	const VkPipelineLayout Layout = Device.GetPipelineLayout(Filter);
	if (bDescriptorDirty)
	{
		FVulkanTexture& Bound = Texture ? *Texture : Device.GetWhiteTexture();
		const VkDescriptorSet Set = Bound.GetDescriptorSet(Filter);
		if (!Set)
		{
			return;
		}
		vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Layout, 0, 1, &Set, 0, nullptr);
		bDescriptorDirty = false;
	}
	if (bPushConstantsDirty)
	{
		// Always the whole range (the unused tail keeps older bytes) so no part
		// of the layout's 64-byte range is ever undefined.
		vkCmdPushConstants(CommandBuffer, Layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, FVulkanDevice::MaxPushConstantSize, PushConstantData);
		bPushConstantsDirty = false;
	}
	vkCmdDrawIndexed(CommandBuffer, IndexCount, 1, FirstIndex, BaseVertex, 0);
}
