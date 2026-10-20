// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Vulkan/VulkanRHI.h"

#include <cstring>
#include <string>

using namespace VulkanDynamicAPI;

// ---------------------------------------------------------------------------
// FVulkanBuffer

FVulkanBuffer::FVulkanBuffer(FVulkanDevice& InDevice, const FRHIBufferDesc& InDesc)
	: FRHIBuffer(InDesc)
	, Device(InDevice)
{
}

FVulkanBuffer::~FVulkanBuffer()
{
	if (Buffer)
	{
		Device.DeferDestroy([DeviceHandle = Device.GetHandle(), BufferHandle = Buffer, MemoryHandle = Memory]()
		{
			vkDestroyBuffer(DeviceHandle, BufferHandle, nullptr);
			vkFreeMemory(DeviceHandle, MemoryHandle, nullptr); // also unmaps
		});
	}
}

bool FVulkanBuffer::Initialize()
{
	// The caller's InitialData is only valid during creation.
	const void* InitialData = Desc.InitialData;
	Desc.InitialData = nullptr;

	const bool bUpload = Desc.Memory == EBufferMemory::Upload;
	if (!ensuref(Desc.Size > 0, "Vulkan: zero-sized buffer {}", Desc.DebugName ? Desc.DebugName : "")
		|| !ensuref(bUpload || InitialData, "Vulkan: Default buffers need InitialData"))
	{
		return false;
	}

	VkBufferUsageFlags Usage = Desc.Usage == EBufferUsage::Vertex ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT : VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	if (!bUpload)
	{
		Usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	}
	if (!Device.CreateBufferObject(Desc.Size, Usage, bUpload, Buffer, Memory, bUpload ? &MappedData : nullptr))
	{
		return false;
	}
	Device.SetDebugName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64>(Buffer), Desc.DebugName);

	if (bUpload)
	{
		if (InitialData)
		{
			std::memcpy(MappedData, InitialData, static_cast<SIZE_T>(Desc.Size));
		}
		return true;
	}

	// Default memory: staging copy, then make it visible to vertex input.
	FVulkanUploadContext Upload;
	if (!Device.BeginUpload(Desc.Size, Upload))
	{
		return false;
	}
	std::memcpy(Upload.StagingData, InitialData, static_cast<SIZE_T>(Desc.Size));
	VkBufferCopy Copy{ Upload.StagingOffset, 0, Desc.Size };
	vkCmdCopyBuffer(Upload.CommandBuffer, Upload.StagingBuffer, Buffer, 1, &Copy);

	VkBufferMemoryBarrier Barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
	Barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	Barrier.dstAccessMask = Desc.Usage == EBufferUsage::Vertex ? VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT : VK_ACCESS_INDEX_READ_BIT;
	Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	Barrier.buffer = Buffer;
	Barrier.size = VK_WHOLE_SIZE;
	vkCmdPipelineBarrier(Upload.CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
		0, 0, nullptr, 1, &Barrier, 0, nullptr);
	Device.EndUpload(Upload);
	return true;
}

// ---------------------------------------------------------------------------
// FVulkanTexture

FVulkanTexture::FVulkanTexture(FVulkanDevice& InDevice, const FRHITextureDesc& InDesc)
	: FRHITexture(InDesc)
	, Device(InDevice)
{
}

FVulkanTexture::~FVulkanTexture()
{
	Device.DeferDestroy([DeviceHandle = Device.GetHandle(), ImageHandle = Image, MemoryHandle = Memory,
		Sampled = SampledView, Attachment = AttachmentView, FramebufferHandle = Framebuffer, DepthFramebufferHandle = DepthFramebuffer,
		Set0 = DescriptorSets[0], Set1 = DescriptorSets[1], Pool0 = DescriptorPools[0], Pool1 = DescriptorPools[1]]()
	{
		if (Set0) vkFreeDescriptorSets(DeviceHandle, Pool0, 1, &Set0);
		if (Set1) vkFreeDescriptorSets(DeviceHandle, Pool1, 1, &Set1);
		if (FramebufferHandle) vkDestroyFramebuffer(DeviceHandle, FramebufferHandle, nullptr);
		if (DepthFramebufferHandle) vkDestroyFramebuffer(DeviceHandle, DepthFramebufferHandle, nullptr);
		if (Attachment) vkDestroyImageView(DeviceHandle, Attachment, nullptr);
		if (Sampled) vkDestroyImageView(DeviceHandle, Sampled, nullptr);
		if (ImageHandle) vkDestroyImage(DeviceHandle, ImageHandle, nullptr);
		if (MemoryHandle) vkFreeMemory(DeviceHandle, MemoryHandle, nullptr);
	});
}

bool FVulkanTexture::Initialize(const void* InitialData, uint32 InitialRowPitch)
{
	Format = VulkanUtil::ToVkFormat(Desc.Format);
	if (!ensuref(Format != VK_FORMAT_UNDEFINED && Desc.Width > 0 && Desc.Height > 0, "Vulkan: invalid texture description"))
	{
		return false;
	}
	const VkDevice DeviceHandle = Device.GetHandle();

	VkImageCreateInfo ImageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	ImageInfo.imageType = VK_IMAGE_TYPE_2D;
	ImageInfo.format = Format;
	ImageInfo.extent = { Desc.Width, Desc.Height, 1 };
	ImageInfo.mipLevels = 1;
	ImageInfo.arrayLayers = 1;
	ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	ImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	const bool bDepth = IsDepthFormat(Desc.Format);
	ImageInfo.usage = bDepth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
		: VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	if (Desc.bRenderTarget && !bDepth)
	{
		ImageInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}
	ImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if (!VulkanUtil::Check(vkCreateImage(DeviceHandle, &ImageInfo, nullptr, &Image), "vkCreateImage"))
	{
		Image = VK_NULL_HANDLE;
		return false;
	}

	VkMemoryRequirements Requirements;
	vkGetImageMemoryRequirements(DeviceHandle, Image, &Requirements);
	if (!Device.AllocateMemory(Requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, Memory)
		|| !VulkanUtil::Check(vkBindImageMemory(DeviceHandle, Image, Memory, 0), "vkBindImageMemory"))
	{
		return false;
	}

	// R8 textures sample as (1, 1, 1, R) like on D3D12, so alpha masks use
	// the same shader path as RGBA images. Attachments need the identity
	// swizzle, hence a second view for R8 render targets.
	VkImageViewCreateInfo ViewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	ViewInfo.image = Image;
	ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	ViewInfo.format = Format;
	ViewInfo.subresourceRange = { static_cast<VkImageAspectFlags>(bDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 1, 0, 1 };
	const bool bSwizzled = Desc.Format == EPixelFormat::R8_UNORM;
	if (bSwizzled)
	{
		ViewInfo.components = { VK_COMPONENT_SWIZZLE_ONE, VK_COMPONENT_SWIZZLE_ONE, VK_COMPONENT_SWIZZLE_ONE, VK_COMPONENT_SWIZZLE_R };
	}
	if (!VulkanUtil::Check(vkCreateImageView(DeviceHandle, &ViewInfo, nullptr, &SampledView), "vkCreateImageView"))
	{
		SampledView = VK_NULL_HANDLE;
		return false;
	}
	if (Desc.bRenderTarget && bSwizzled)
	{
		ViewInfo.components = {};
		if (!VulkanUtil::Check(vkCreateImageView(DeviceHandle, &ViewInfo, nullptr, &AttachmentView), "vkCreateImageView"))
		{
			AttachmentView = VK_NULL_HANDLE;
			return false;
		}
	}

	if (Desc.DebugName)
	{
		Device.SetDebugName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64>(Image), Desc.DebugName);
		Device.SetDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64>(SampledView), Desc.DebugName);
	}

	if (bDepth)
	{
		// Depth buffers live in DEPTH_STENCIL_ATTACHMENT_OPTIMAL; their first
		// pass clears them.
		FVulkanUploadContext Upload;
		if (!Device.BeginUpload(0, Upload))
		{
			return false;
		}
		VkImageMemoryBarrier Barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		Barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		Barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		Barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		Barrier.image = Image;
		Barrier.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
		vkCmdPipelineBarrier(Upload.CommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			0, 0, nullptr, 0, nullptr, 1, &Barrier);
		Device.EndUpload(Upload);
		return true;
	}

	// Initial contents: the data, or the clear color (render targets) / zero,
	// then SHADER_READ_ONLY_OPTIMAL, the layout textures live in.
	const uint32 BytesPerPixel = GetBytesPerPixel(Desc.Format);
	const uint32 RowBytes = Desc.Width * BytesPerPixel;
	const VkDeviceSize StagingSize = InitialData ? static_cast<VkDeviceSize>(RowBytes) * Desc.Height : 0;
	FVulkanUploadContext Upload;
	if (!Device.BeginUpload(StagingSize, Upload))
	{
		return false;
	}
	if (InitialData)
	{
		const uint32 SourcePitch = InitialRowPitch ? InitialRowPitch : RowBytes;
		const uint8* Source = static_cast<const uint8*>(InitialData);
		for (uint32 Row = 0; Row < Desc.Height; ++Row)
		{
			std::memcpy(Upload.StagingData + static_cast<SIZE_T>(Row) * RowBytes, Source + static_cast<SIZE_T>(Row) * SourcePitch, RowBytes);
		}
	}

	VkImageMemoryBarrier Barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	Barrier.srcAccessMask = 0;
	Barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	Barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	Barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	Barrier.image = Image;
	Barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	vkCmdPipelineBarrier(Upload.CommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 0, nullptr, 0, nullptr, 1, &Barrier);

	if (InitialData)
	{
		VkBufferImageCopy Copy{};
		Copy.bufferOffset = Upload.StagingOffset;
		Copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		Copy.imageExtent = { Desc.Width, Desc.Height, 1 };
		vkCmdCopyBufferToImage(Upload.CommandBuffer, Upload.StagingBuffer, Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Copy);
	}
	else
	{
		VkClearColorValue Clear{};
		if (Desc.bRenderTarget)
		{
			Clear.float32[0] = Desc.ClearColor.R;
			Clear.float32[1] = Desc.ClearColor.G;
			Clear.float32[2] = Desc.ClearColor.B;
			Clear.float32[3] = Desc.ClearColor.A;
		}
		const VkImageSubresourceRange Range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		vkCmdClearColorImage(Upload.CommandBuffer, Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &Clear, 1, &Range);
	}

	Barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	Barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	Barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	vkCmdPipelineBarrier(Upload.CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0, 0, nullptr, 0, nullptr, 1, &Barrier);

	Device.EndUpload(Upload);
	return true;
}

VkDescriptorSet FVulkanTexture::GetDescriptorSet(ESamplerFilter Filter)
{
	const uint32 Index = static_cast<uint32>(Filter);
	if (!DescriptorSets[Index])
	{
		DescriptorSets[Index] = Device.AllocateDescriptorSet(Filter, DescriptorPools[Index]);
		if (!DescriptorSets[Index])
		{
			return VK_NULL_HANDLE;
		}
		VkDescriptorImageInfo ImageInfo{};
		ImageInfo.imageView = SampledView;
		ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		VkWriteDescriptorSet Write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		Write.dstSet = DescriptorSets[Index];
		Write.dstBinding = 0;
		Write.descriptorCount = 1;
		Write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		Write.pImageInfo = &ImageInfo;
		vkUpdateDescriptorSets(Device.GetHandle(), 1, &Write, 0, nullptr);
	}
	return DescriptorSets[Index];
}

VkFramebuffer FVulkanTexture::GetFramebuffer(VkRenderPass CompatibleRenderPass, FVulkanTexture* Depth)
{
	if (Depth)
	{
		const VkImageView DepthView = Depth->GetAttachmentView();
		if (DepthFramebuffer && DepthFramebufferView != DepthView)
		{
			Device.DeferDestroy([DeviceHandle = Device.GetHandle(), Old = DepthFramebuffer]()
			{
				vkDestroyFramebuffer(DeviceHandle, Old, nullptr);
			});
			DepthFramebuffer = VK_NULL_HANDLE;
		}
		if (!DepthFramebuffer && Desc.bRenderTarget)
		{
			const VkImageView Views[2] = { GetAttachmentView(), DepthView };
			VkFramebufferCreateInfo FramebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
			FramebufferInfo.renderPass = CompatibleRenderPass;
			FramebufferInfo.attachmentCount = 2;
			FramebufferInfo.pAttachments = Views;
			FramebufferInfo.width = Desc.Width;
			FramebufferInfo.height = Desc.Height;
			FramebufferInfo.layers = 1;
			if (VulkanUtil::Check(vkCreateFramebuffer(Device.GetHandle(), &FramebufferInfo, nullptr, &DepthFramebuffer), "vkCreateFramebuffer"))
			{
				DepthFramebufferView = DepthView;
			}
			else
			{
				DepthFramebuffer = VK_NULL_HANDLE;
			}
		}
		return DepthFramebuffer;
	}
	if (!Framebuffer && Desc.bRenderTarget)
	{
		const VkImageView View = AttachmentView ? AttachmentView : SampledView;
		VkFramebufferCreateInfo FramebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
		FramebufferInfo.renderPass = CompatibleRenderPass;
		FramebufferInfo.attachmentCount = 1;
		FramebufferInfo.pAttachments = &View;
		FramebufferInfo.width = Desc.Width;
		FramebufferInfo.height = Desc.Height;
		FramebufferInfo.layers = 1;
		if (!VulkanUtil::Check(vkCreateFramebuffer(Device.GetHandle(), &FramebufferInfo, nullptr, &Framebuffer), "vkCreateFramebuffer"))
		{
			Framebuffer = VK_NULL_HANDLE;
		}
	}
	return Framebuffer;
}

// ---------------------------------------------------------------------------
// FVulkanGraphicsPipeline

namespace
{
	// The entry point name of the given execution model (0 = Vertex,
	// 4 = Fragment) in a SPIR-V module; DXC keeps the HLSL entry name.
	std::string FindSpirvEntryPoint(const TArray<uint32>& Code, uint32 ExecutionModel)
	{
		const uint32* Words = Code.data();
		const SIZE_T WordCount = Code.size();
		if (WordCount < 5 || Words[0] != 0x07230203u)
		{
			return "main";
		}
		constexpr uint32 OpEntryPoint = 15;
		for (SIZE_T Index = 5; Index < WordCount;)
		{
			const uint32 Opcode = Words[Index] & 0xFFFFu;
			const uint32 Length = Words[Index] >> 16;
			if (Length == 0 || Index + Length > WordCount)
			{
				break;
			}
			if (Opcode == OpEntryPoint && Length >= 4 && Words[Index + 1] == ExecutionModel)
			{
				const char* Name = reinterpret_cast<const char*>(&Words[Index + 3]);
				const SIZE_T MaxBytes = (Length - 3) * 4;
				return std::string(Name, strnlen(Name, MaxBytes));
			}
			Index += Length;
		}
		return "main";
	}

	VkFormat ToVkVertexFormat(EVertexFormat Format)
	{
		switch (Format)
		{
		case EVertexFormat::Float2: return VK_FORMAT_R32G32_SFLOAT;
		case EVertexFormat::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
		case EVertexFormat::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
		case EVertexFormat::UByte4N: return VK_FORMAT_R8G8B8A8_UNORM;
		}
		return VK_FORMAT_UNDEFINED;
	}
}

FVulkanGraphicsPipeline::FVulkanGraphicsPipeline(FVulkanDevice& InDevice, const FRHIGraphicsPipelineDesc& InDesc)
	: FRHIGraphicsPipeline(InDesc)
	, Device(InDevice)
{
}

FVulkanGraphicsPipeline::~FVulkanGraphicsPipeline()
{
	if (Pipeline)
	{
		Device.DeferDestroy([DeviceHandle = Device.GetHandle(), PipelineHandle = Pipeline]()
		{
			vkDestroyPipeline(DeviceHandle, PipelineHandle, nullptr);
		});
	}
}

bool FVulkanGraphicsPipeline::Initialize()
{
	if (!ensuref(Desc.VertexShader.Data && Desc.VertexShader.Size % 4 == 0 && Desc.PixelShader.Data && Desc.PixelShader.Size % 4 == 0,
		"Vulkan: pipeline {} needs SPIR-V vertex and pixel shaders", Desc.DebugName ? Desc.DebugName : "")
		|| !ensuref(Desc.PushConstantSize <= FVulkanDevice::MaxPushConstantSize && Desc.PushConstantSize % 4 == 0,
		"Vulkan: push constants must be a multiple of 4 bytes, at most {}", FVulkanDevice::MaxPushConstantSize))
	{
		return false;
	}
	const VkDevice DeviceHandle = Device.GetHandle();

	// The bytecode may come from byte arrays (dxc -Fh headers) without 4-byte
	// alignment, so copy it into words first.
	TArray<uint32> Codes[2];
	const FRHIShaderBytecode* Sources[2] = { &Desc.VertexShader, &Desc.PixelShader };
	VkShaderModule Modules[2] = {};
	for (uint32 Index = 0; Index < 2; ++Index)
	{
		Codes[Index].resize(Sources[Index]->Size / 4);
		std::memcpy(Codes[Index].data(), Sources[Index]->Data, Sources[Index]->Size);
		VkShaderModuleCreateInfo ModuleInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		ModuleInfo.codeSize = Sources[Index]->Size;
		ModuleInfo.pCode = Codes[Index].data();
		if (!VulkanUtil::Check(vkCreateShaderModule(DeviceHandle, &ModuleInfo, nullptr, &Modules[Index]), "vkCreateShaderModule"))
		{
			if (Modules[0]) vkDestroyShaderModule(DeviceHandle, Modules[0], nullptr);
			return false;
		}
	}
	const std::string VertexEntry = FindSpirvEntryPoint(Codes[0], 0);
	const std::string PixelEntry = FindSpirvEntryPoint(Codes[1], 4);

	VkPipelineShaderStageCreateInfo Stages[2] = {};
	Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	Stages[0].module = Modules[0];
	Stages[0].pName = VertexEntry.c_str();
	Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	Stages[1].module = Modules[1];
	Stages[1].pName = PixelEntry.c_str();

	VkVertexInputBindingDescription Binding{};
	Binding.binding = 0;
	Binding.stride = Desc.VertexStride;
	Binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	TArray<VkVertexInputAttributeDescription> Attributes;
	for (SIZE_T Index = 0; Index < Desc.VertexAttributes.size(); ++Index)
	{
		const FVertexAttribute& Attribute = Desc.VertexAttributes[Index];
		Attributes.push_back({ static_cast<uint32>(Index), 0, ToVkVertexFormat(Attribute.Format), Attribute.Offset });
	}
	VkPipelineVertexInputStateCreateInfo VertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	if (!Attributes.empty())
	{
		VertexInput.vertexBindingDescriptionCount = 1;
		VertexInput.pVertexBindingDescriptions = &Binding;
		VertexInput.vertexAttributeDescriptionCount = static_cast<uint32>(Attributes.size());
		VertexInput.pVertexAttributeDescriptions = Attributes.data();
	}

	VkPipelineInputAssemblyStateCreateInfo InputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo ViewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	ViewportState.viewportCount = 1;
	ViewportState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo Rasterization{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	Rasterization.polygonMode = VK_POLYGON_MODE_FILL;
	Rasterization.cullMode = VK_CULL_MODE_NONE;
	Rasterization.frontFace = VK_FRONT_FACE_CLOCKWISE;
	Rasterization.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo Multisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineDepthStencilStateCreateInfo DepthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	const VkFormat DepthFormat = VulkanUtil::ToVkFormat(Desc.DepthFormat);
	if (DepthFormat != VK_FORMAT_UNDEFINED)
	{
		DepthStencil.depthTestEnable = VK_TRUE;
		DepthStencil.depthWriteEnable = Desc.bDepthWrite ? VK_TRUE : VK_FALSE;
		DepthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	}

	VkPipelineColorBlendAttachmentState BlendAttachment{};
	BlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	switch (Desc.BlendMode)
	{
	case EBlendMode::Opaque:
		BlendAttachment.blendEnable = VK_FALSE;
		break;
	case EBlendMode::AlphaBlend:
	case EBlendMode::PremultipliedAlpha:
		BlendAttachment.blendEnable = VK_TRUE;
		BlendAttachment.srcColorBlendFactor = Desc.BlendMode == EBlendMode::AlphaBlend ? VK_BLEND_FACTOR_SRC_ALPHA : VK_BLEND_FACTOR_ONE;
		BlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		BlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
		BlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		BlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		BlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
		break;
	}
	VkPipelineColorBlendStateCreateInfo ColorBlend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	ColorBlend.attachmentCount = 1;
	ColorBlend.pAttachments = &BlendAttachment;

	const VkDynamicState DynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo DynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	DynamicState.dynamicStateCount = 2;
	DynamicState.pDynamicStates = DynamicStates;

	// Any render pass with the same attachment format is compatible.
	const VkFormat TargetFormat = VulkanUtil::ToVkFormat(Desc.RenderTargetFormat);
	const VkRenderPass RenderPass = Device.GetRenderPass(TargetFormat, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		DepthFormat);

	VkGraphicsPipelineCreateInfo PipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	PipelineInfo.stageCount = 2;
	PipelineInfo.pStages = Stages;
	PipelineInfo.pVertexInputState = &VertexInput;
	PipelineInfo.pInputAssemblyState = &InputAssembly;
	PipelineInfo.pViewportState = &ViewportState;
	PipelineInfo.pRasterizationState = &Rasterization;
	PipelineInfo.pMultisampleState = &Multisample;
	PipelineInfo.pDepthStencilState = &DepthStencil;
	PipelineInfo.pColorBlendState = &ColorBlend;
	PipelineInfo.pDynamicState = &DynamicState;
	PipelineInfo.layout = Device.GetPipelineLayout(Desc.SamplerFilter);
	PipelineInfo.renderPass = RenderPass;
	PipelineInfo.subpass = 0;

	const bool bOk = RenderPass != VK_NULL_HANDLE
		&& VulkanUtil::Check(vkCreateGraphicsPipelines(DeviceHandle, VK_NULL_HANDLE, 1, &PipelineInfo, nullptr, &Pipeline), "vkCreateGraphicsPipelines");
	vkDestroyShaderModule(DeviceHandle, Modules[0], nullptr);
	vkDestroyShaderModule(DeviceHandle, Modules[1], nullptr);
	if (!bOk)
	{
		Pipeline = VK_NULL_HANDLE;
		return false;
	}
	Device.SetDebugName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64>(Pipeline), Desc.DebugName);
	return true;
}
