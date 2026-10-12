// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Runtime loading of vulkan-1.dll. Nothing links against vulkan-1.lib: the
// headers are used with VK_NO_PROTOTYPES and every entry point is a function
// pointer in the GenesisVk namespace, fetched with vkGetInstanceProcAddr /
// vkGetDeviceProcAddr. Device-level pointers come from the one live device
// (the Vulkan RHI supports a single device at a time).

#pragma once

#include "CoreMinimal.h"

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#define GENESIS_VK_GLOBAL_FUNCTIONS(X) \
	X(vkCreateInstance) \
	X(vkEnumerateInstanceExtensionProperties) \
	X(vkEnumerateInstanceLayerProperties)

// May be missing (Vulkan 1.0 loader).
#define GENESIS_VK_GLOBAL_OPTIONAL_FUNCTIONS(X) \
	X(vkEnumerateInstanceVersion)

#define GENESIS_VK_INSTANCE_FUNCTIONS(X) \
	X(vkDestroyInstance) \
	X(vkEnumeratePhysicalDevices) \
	X(vkGetPhysicalDeviceProperties) \
	X(vkGetPhysicalDeviceQueueFamilyProperties) \
	X(vkGetPhysicalDeviceMemoryProperties) \
	X(vkCreateDevice) \
	X(vkGetDeviceProcAddr) \
	X(vkEnumerateDeviceExtensionProperties) \
	X(vkDestroySurfaceKHR) \
	X(vkGetPhysicalDeviceSurfaceSupportKHR) \
	X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
	X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
	X(vkGetPhysicalDeviceSurfacePresentModesKHR) \
	X(vkCreateWin32SurfaceKHR) \
	X(vkGetPhysicalDeviceWin32PresentationSupportKHR)

// VK_EXT_debug_utils; null unless the extension was enabled.
#define GENESIS_VK_INSTANCE_OPTIONAL_FUNCTIONS(X) \
	X(vkCreateDebugUtilsMessengerEXT) \
	X(vkDestroyDebugUtilsMessengerEXT) \
	X(vkSetDebugUtilsObjectNameEXT)

#define GENESIS_VK_DEVICE_FUNCTIONS(X) \
	X(vkDestroyDevice) \
	X(vkGetDeviceQueue) \
	X(vkDeviceWaitIdle) \
	X(vkQueueSubmit) \
	X(vkQueueWaitIdle) \
	X(vkQueuePresentKHR) \
	X(vkCreateSwapchainKHR) \
	X(vkDestroySwapchainKHR) \
	X(vkGetSwapchainImagesKHR) \
	X(vkAcquireNextImageKHR) \
	X(vkCreateImage) \
	X(vkDestroyImage) \
	X(vkCreateImageView) \
	X(vkDestroyImageView) \
	X(vkGetImageMemoryRequirements) \
	X(vkBindImageMemory) \
	X(vkCreateBuffer) \
	X(vkDestroyBuffer) \
	X(vkGetBufferMemoryRequirements) \
	X(vkBindBufferMemory) \
	X(vkAllocateMemory) \
	X(vkFreeMemory) \
	X(vkMapMemory) \
	X(vkUnmapMemory) \
	X(vkCreateSampler) \
	X(vkDestroySampler) \
	X(vkCreateDescriptorSetLayout) \
	X(vkDestroyDescriptorSetLayout) \
	X(vkCreatePipelineLayout) \
	X(vkDestroyPipelineLayout) \
	X(vkCreateDescriptorPool) \
	X(vkDestroyDescriptorPool) \
	X(vkAllocateDescriptorSets) \
	X(vkFreeDescriptorSets) \
	X(vkUpdateDescriptorSets) \
	X(vkCreateShaderModule) \
	X(vkDestroyShaderModule) \
	X(vkCreateGraphicsPipelines) \
	X(vkDestroyPipeline) \
	X(vkCreateRenderPass) \
	X(vkDestroyRenderPass) \
	X(vkCreateFramebuffer) \
	X(vkDestroyFramebuffer) \
	X(vkCreateCommandPool) \
	X(vkDestroyCommandPool) \
	X(vkResetCommandPool) \
	X(vkAllocateCommandBuffers) \
	X(vkBeginCommandBuffer) \
	X(vkEndCommandBuffer) \
	X(vkCreateFence) \
	X(vkDestroyFence) \
	X(vkWaitForFences) \
	X(vkResetFences) \
	X(vkCreateSemaphore) \
	X(vkDestroySemaphore) \
	X(vkCmdBeginRenderPass) \
	X(vkCmdEndRenderPass) \
	X(vkCmdBindPipeline) \
	X(vkCmdBindDescriptorSets) \
	X(vkCmdBindVertexBuffers) \
	X(vkCmdBindIndexBuffer) \
	X(vkCmdSetViewport) \
	X(vkCmdSetScissor) \
	X(vkCmdPushConstants) \
	X(vkCmdDrawIndexed) \
	X(vkCmdPipelineBarrier) \
	X(vkCmdCopyBuffer) \
	X(vkCmdCopyBufferToImage) \
	X(vkCmdCopyImageToBuffer) \
	X(vkCmdClearColorImage)

namespace GenesisVk
{
#define GENESIS_VK_DECLARE(Name) extern PFN_##Name Name;
	extern PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
	GENESIS_VK_GLOBAL_FUNCTIONS(GENESIS_VK_DECLARE)
	GENESIS_VK_GLOBAL_OPTIONAL_FUNCTIONS(GENESIS_VK_DECLARE)
	GENESIS_VK_INSTANCE_FUNCTIONS(GENESIS_VK_DECLARE)
	GENESIS_VK_INSTANCE_OPTIONAL_FUNCTIONS(GENESIS_VK_DECLARE)
	GENESIS_VK_DEVICE_FUNCTIONS(GENESIS_VK_DECLARE)
#undef GENESIS_VK_DECLARE

	// Loads vulkan-1.dll and the global entry points. Reference counted.
	bool LoadVulkanLibrary();
	void UnloadVulkanLibrary();

	// False (and logs) when a required entry point is missing.
	bool LoadInstanceFunctions(VkInstance Instance);
	bool LoadDeviceFunctions(VkDevice Device);

	const char* ResultToString(VkResult Result);
}
