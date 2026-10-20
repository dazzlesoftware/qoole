// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Vulkan/VulkanRHI.h"

#include "GenericPlatform/PlatformWindow.h"

using namespace GenesisVk;

FVulkanSwapChain::FVulkanSwapChain(FVulkanDevice& InDevice, IPlatformWindow& InWindow)
	: FRHISwapChain(&InWindow)
	, Device(InDevice)
{
}

FVulkanSwapChain::~FVulkanSwapChain()
{
	Device.OnSwapChainDestroyed(this);
	if (Device.GetHandle())
	{
		// Presentation may still use the images and semaphores.
		vkDeviceWaitIdle(Device.GetHandle());
		DestroyImages();
		if (Swapchain)
		{
			vkDestroySwapchainKHR(Device.GetHandle(), Swapchain, nullptr);
		}
		for (VkSemaphore Semaphore : AcquireSemaphores)
		{
			if (Semaphore)
			{
				vkDestroySemaphore(Device.GetHandle(), Semaphore, nullptr);
			}
		}
	}
	if (Surface)
	{
		vkDestroySurfaceKHR(Device.GetInstance(), Surface, nullptr);
	}
}

bool FVulkanSwapChain::Initialize()
{
	VkWin32SurfaceCreateInfoKHR SurfaceInfo{ VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
	SurfaceInfo.hinstance = GetModuleHandleW(nullptr);
	SurfaceInfo.hwnd = static_cast<HWND>(Window->GetNativeHandle());
	if (!SurfaceInfo.hwnd)
	{
		UE_LOG(LogRHI, Error, "Vulkan: cannot create a swapchain for a window without a native handle");
		return false;
	}
	if (!VulkanUtil::Check(vkCreateWin32SurfaceKHR(Device.GetInstance(), &SurfaceInfo, nullptr, &Surface), "vkCreateWin32SurfaceKHR"))
	{
		Surface = VK_NULL_HANDLE;
		return false;
	}

	VkBool32 bSupported = VK_FALSE;
	vkGetPhysicalDeviceSurfaceSupportKHR(Device.GetPhysicalDevice(), Device.GetQueueFamily(), Surface, &bSupported);
	if (!bSupported)
	{
		UE_LOG(LogRHI, Error, "Vulkan: the graphics queue cannot present to this window");
		return false;
	}

	// Non-sRGB 8-bit BGRA (the UI blends in sRGB space), else RGBA, else whatever comes first.
	uint32 Count = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(Device.GetPhysicalDevice(), Surface, &Count, nullptr);
	TArray<VkSurfaceFormatKHR> Formats(Count);
	vkGetPhysicalDeviceSurfaceFormatsKHR(Device.GetPhysicalDevice(), Surface, &Count, Formats.data());
	Formats.resize(Count);
	if (Formats.empty())
	{
		UE_LOG(LogRHI, Error, "Vulkan: the surface reports no formats");
		return false;
	}
	SurfaceFormat = Formats[0];
	PixelFormat = EPixelFormat::Unknown;
	for (VkFormat Wanted : { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM })
	{
		auto It = std::find_if(Formats.begin(), Formats.end(), [Wanted](const VkSurfaceFormatKHR& F)
		{
			return F.format == Wanted && F.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		});
		if (It != Formats.end())
		{
			SurfaceFormat = *It;
			PixelFormat = Wanted == VK_FORMAT_B8G8R8A8_UNORM ? EPixelFormat::B8G8R8A8_UNORM : EPixelFormat::R8G8B8A8_UNORM;
			break;
		}
	}
	if (PixelFormat == EPixelFormat::Unknown)
	{
		UE_LOG(LogRHI, Error, "Vulkan: the surface supports neither B8G8R8A8_UNORM nor R8G8B8A8_UNORM");
		return false;
	}

	for (VkSemaphore& Semaphore : AcquireSemaphores)
	{
		VkSemaphoreCreateInfo SemaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		if (!VulkanUtil::Check(vkCreateSemaphore(Device.GetHandle(), &SemaphoreInfo, nullptr, &Semaphore), "vkCreateSemaphore"))
		{
			Semaphore = VK_NULL_HANDLE;
			return false;
		}
	}

	const FIntPoint ClientSize = Window->GetClientSize();
	Width = static_cast<uint32>(std::max(0, ClientSize.X));
	Height = static_cast<uint32>(std::max(0, ClientSize.Y));
	bNeedsRecreate = true;
	if (IsDrawable())
	{
		Recreate(); // a failure here is retried at the next acquire
	}
	return true;
}

void FVulkanSwapChain::Resize(uint32 InWidth, uint32 InHeight)
{
	// Only flags the change: this can run inside the OS modal size loop.
	if (InWidth != Width || InHeight != Height || bZeroExtent)
	{
		Width = InWidth;
		Height = InHeight;
		bZeroExtent = false;
		bNeedsRecreate = true;
	}
}

void FVulkanSwapChain::DestroyImages()
{
	const VkDevice DeviceHandle = Device.GetHandle();
	for (FImage& Image : Images)
	{
		if (Image.Framebuffer) vkDestroyFramebuffer(DeviceHandle, Image.Framebuffer, nullptr);
		if (Image.View) vkDestroyImageView(DeviceHandle, Image.View, nullptr);
		if (Image.RenderFinished) vkDestroySemaphore(DeviceHandle, Image.RenderFinished, nullptr);
	}
	Images.clear();
}

bool FVulkanSwapChain::Recreate()
{
	check(!bAcquired);
	const VkDevice DeviceHandle = Device.GetHandle();
	const VkPhysicalDevice PhysicalDevice = Device.GetPhysicalDevice();

	VkSurfaceCapabilitiesKHR Capabilities;
	if (!VulkanUtil::Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(PhysicalDevice, Surface, &Capabilities), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))
	{
		return false;
	}
	VkExtent2D NewExtent = Capabilities.currentExtent;
	if (NewExtent.width == UINT32_MAX)
	{
		NewExtent.width = std::clamp(Width, Capabilities.minImageExtent.width, Capabilities.maxImageExtent.width);
		NewExtent.height = std::clamp(Height, Capabilities.minImageExtent.height, Capabilities.maxImageExtent.height);
	}
	if (NewExtent.width == 0 || NewExtent.height == 0)
	{
		bZeroExtent = true; // minimized; IsDrawable() is false until the next Resize
		return false;
	}

	// The GPU may still use the old images and framebuffers.
	Device.WaitIdle();
	DestroyImages();

	uint32 ImageCount = std::max(Capabilities.minImageCount + 1, 2u);
	if (Capabilities.maxImageCount > 0)
	{
		ImageCount = std::min(ImageCount, Capabilities.maxImageCount);
	}
	VkCompositeAlphaFlagBitsKHR CompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	if (!(Capabilities.supportedCompositeAlpha & CompositeAlpha))
	{
		for (VkCompositeAlphaFlagBitsKHR Candidate : { VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR })
		{
			if (Capabilities.supportedCompositeAlpha & Candidate)
			{
				CompositeAlpha = Candidate;
				break;
			}
		}
	}
	VkImageUsageFlags Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	if (Capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
	{
		Usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}

	VkSwapchainCreateInfoKHR CreateInfo{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
	CreateInfo.surface = Surface;
	CreateInfo.minImageCount = ImageCount;
	CreateInfo.imageFormat = SurfaceFormat.format;
	CreateInfo.imageColorSpace = SurfaceFormat.colorSpace;
	CreateInfo.imageExtent = NewExtent;
	CreateInfo.imageArrayLayers = 1;
	CreateInfo.imageUsage = Usage;
	CreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	CreateInfo.preTransform = (Capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : Capabilities.currentTransform;
	CreateInfo.compositeAlpha = CompositeAlpha;
	CreateInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR; // always supported
	CreateInfo.clipped = VK_TRUE;
	CreateInfo.oldSwapchain = Swapchain;

	VkSwapchainKHR NewSwapchain = VK_NULL_HANDLE;
	const VkResult Result = vkCreateSwapchainKHR(DeviceHandle, &CreateInfo, nullptr, &NewSwapchain);
	if (Swapchain)
	{
		vkDestroySwapchainKHR(DeviceHandle, Swapchain, nullptr);
		Swapchain = VK_NULL_HANDLE;
	}
	if (!VulkanUtil::Check(Result, "vkCreateSwapchainKHR"))
	{
		return false;
	}
	Swapchain = NewSwapchain;
	Extent = NewExtent;
	Width = Extent.width;
	Height = Extent.height;

	uint32 Count = 0;
	vkGetSwapchainImagesKHR(DeviceHandle, Swapchain, &Count, nullptr);
	TArray<VkImage> SwapchainImages(Count);
	vkGetSwapchainImagesKHR(DeviceHandle, Swapchain, &Count, SwapchainImages.data());

	const VkRenderPass RenderPass = Device.GetRenderPass(SurfaceFormat.format, true, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	Images.resize(Count);
	for (uint32 Index = 0; Index < Count; ++Index)
	{
		FImage& Image = Images[Index];
		Image.Image = SwapchainImages[Index];

		VkImageViewCreateInfo ViewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		ViewInfo.image = Image.Image;
		ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		ViewInfo.format = SurfaceFormat.format;
		ViewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		VkFramebufferCreateInfo FramebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
		FramebufferInfo.renderPass = RenderPass;
		FramebufferInfo.attachmentCount = 1;
		FramebufferInfo.width = Extent.width;
		FramebufferInfo.height = Extent.height;
		FramebufferInfo.layers = 1;
		VkSemaphoreCreateInfo SemaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		if (!VulkanUtil::Check(vkCreateImageView(DeviceHandle, &ViewInfo, nullptr, &Image.View), "vkCreateImageView")
			|| (FramebufferInfo.pAttachments = &Image.View, !VulkanUtil::Check(vkCreateFramebuffer(DeviceHandle, &FramebufferInfo, nullptr, &Image.Framebuffer), "vkCreateFramebuffer"))
			|| !VulkanUtil::Check(vkCreateSemaphore(DeviceHandle, &SemaphoreInfo, nullptr, &Image.RenderFinished), "vkCreateSemaphore"))
		{
			DestroyImages();
			vkDestroySwapchainKHR(DeviceHandle, Swapchain, nullptr);
			Swapchain = VK_NULL_HANDLE;
			return false;
		}
		Device.SetDebugName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64>(Image.Image), "SwapChainImage");
	}
	bNeedsRecreate = false;
	return true;
}

bool FVulkanSwapChain::AcquireNextImage(uint32 FrameIndex)
{
	if (bAcquired)
	{
		return true;
	}
	if (!IsDrawable())
	{
		return false;
	}
	if ((bNeedsRecreate || !Swapchain) && !Recreate())
	{
		return false;
	}

	for (int32 Attempt = 0; Attempt < 2; ++Attempt)
	{
		const VkResult Result = vkAcquireNextImageKHR(Device.GetHandle(), Swapchain, UINT64_MAX, AcquireSemaphores[FrameIndex], VK_NULL_HANDLE, &ImageIndex);
		if (Result == VK_SUCCESS || Result == VK_SUBOPTIMAL_KHR)
		{
			if (Result == VK_SUBOPTIMAL_KHR)
			{
				bNeedsRecreate = true; // still usable this frame
			}
			bAcquired = true;
			bRenderedThisFrame = false;
			AcquiredFrameIndex = FrameIndex;
			Device.OnSwapChainAcquired(this);
			return true;
		}
		if (Result != VK_ERROR_OUT_OF_DATE_KHR)
		{
			VulkanUtil::Check(Result, "vkAcquireNextImageKHR");
			return false;
		}
		if (!Recreate())
		{
			return false;
		}
	}
	return false;
}

void FVulkanSwapChain::Present(VkQueue Queue)
{
	check(bAcquired);
	VkPresentInfoKHR PresentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
	PresentInfo.waitSemaphoreCount = 1;
	PresentInfo.pWaitSemaphores = &Images[ImageIndex].RenderFinished;
	PresentInfo.swapchainCount = 1;
	PresentInfo.pSwapchains = &Swapchain;
	PresentInfo.pImageIndices = &ImageIndex;
	const VkResult Result = vkQueuePresentKHR(Queue, &PresentInfo);
	if (Result == VK_ERROR_OUT_OF_DATE_KHR || Result == VK_SUBOPTIMAL_KHR)
	{
		bNeedsRecreate = true;
	}
	else if (Result != VK_SUCCESS)
	{
		VulkanUtil::Check(Result, "vkQueuePresentKHR");
	}
	bAcquired = false;
	bRenderedThisFrame = false;
}
