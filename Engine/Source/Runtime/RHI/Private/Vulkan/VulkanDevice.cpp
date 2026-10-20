// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Vulkan/VulkanRHI.h"

#include <cstring>

using namespace VulkanDynamicAPI;

namespace
{
	// One Vulkan device at a time: device-level entry points are process globals.
	bool GVulkanDeviceAlive = false;

	constexpr uint32 DescriptorSetsPerPool = 256;
	constexpr VkDeviceSize MinStagingSize = 1024 * 1024;

	VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessengerCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT Severity,
		VkDebugUtilsMessageTypeFlagsEXT Types,
		const VkDebugUtilsMessengerCallbackDataEXT* Data,
		void* UserData)
	{
		(void)UserData;
		const char* Message = Data && Data->pMessage ? Data->pMessage : "";
		if (Types == VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT && !(Severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT))
		{
			// Loader chatter (e.g. registry lookups for layers), not API misuse.
			UE_LOG(LogRHI, Log, "Vulkan loader: {}", Message);
		}
		else if (Severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
		{
			UE_LOG(LogRHI, Error, "Vulkan validation: {}", Message);
		}
		else if (Severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		{
			UE_LOG(LogRHI, Warning, "Vulkan validation: {}", Message);
		}
		else
		{
			UE_LOG(LogRHI, Verbose, "Vulkan: {}", Message);
		}
		return VK_FALSE;
	}

	bool HasExtension(const TArray<VkExtensionProperties>& Extensions, const char* Name)
	{
		for (const VkExtensionProperties& Extension : Extensions)
		{
			if (std::strcmp(Extension.extensionName, Name) == 0)
			{
				return true;
			}
		}
		return false;
	}

	VkDeviceSize AlignUp(VkDeviceSize Value, VkDeviceSize Alignment)
	{
		return (Value + Alignment - 1) / Alignment * Alignment;
	}

	uint32 LayoutKey(VkImageLayout Layout)
	{
		switch (Layout)
		{
		case VK_IMAGE_LAYOUT_UNDEFINED: return 0;
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: return 1;
		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: return 2;
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: return 3;
		default: break;
		}
		ensuref(false, "Vulkan: unexpected render pass layout {}", static_cast<int32>(Layout));
		return 7;
	}
}

namespace VulkanUtil
{
	VkFormat ToVkFormat(EPixelFormat Format)
	{
		switch (Format)
		{
		case EPixelFormat::R8_UNORM: return VK_FORMAT_R8_UNORM;
		case EPixelFormat::R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
		case EPixelFormat::B8G8R8A8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
		case EPixelFormat::D32_FLOAT: return VK_FORMAT_D32_SFLOAT;
		case EPixelFormat::Unknown: break;
		}
		return VK_FORMAT_UNDEFINED;
	}

	bool Check(VkResult Result, const char* What)
	{
		if (Result == VK_SUCCESS)
		{
			return true;
		}
		UE_LOG(LogRHI, Error, "Vulkan: {} failed: {}", What, ResultToString(Result));
		return false;
	}
}

TUniquePtr<FRHIDevice> CreateVulkanDevice(const FRHIDeviceDesc& Desc)
{
	if (GVulkanDeviceAlive)
	{
		UE_LOG(LogRHI, Warning, "Vulkan: only one Vulkan device can exist at a time");
		return nullptr;
	}
	TUniquePtr<FVulkanDevice> Device = MakeUnique<FVulkanDevice>();
	if (!Device->Initialize(Desc))
	{
		return nullptr;
	}
	return Device;
}

FVulkanDevice::FVulkanDevice()
	: FRHIDevice(ERHIBackend::Vulkan)
{
	GVulkanDeviceAlive = true;
}

FVulkanDevice::~FVulkanDevice()
{
	if (Device)
	{
		vkDeviceWaitIdle(Device);
		CommandList.reset();
		WhiteTexture.reset();

		// Everything is idle: run all garbage, including garbage that the
		// destructors running here add.
		CompletedSerial = UINT64_MAX;
		while (!Garbage.empty())
		{
			CollectGarbage();
		}

		for (FFrame& Frame : Frames)
		{
			if (Frame.StagingBuffer) vkDestroyBuffer(Device, Frame.StagingBuffer, nullptr);
			if (Frame.StagingMemory) vkFreeMemory(Device, Frame.StagingMemory, nullptr);
			if (Frame.Fence) vkDestroyFence(Device, Frame.Fence, nullptr);
			if (Frame.CommandPool) vkDestroyCommandPool(Device, Frame.CommandPool, nullptr);
		}
		if (ImmediateFence) vkDestroyFence(Device, ImmediateFence, nullptr);
		if (ImmediatePool) vkDestroyCommandPool(Device, ImmediatePool, nullptr);
		for (auto& [Key, RenderPass] : RenderPasses)
		{
			vkDestroyRenderPass(Device, RenderPass, nullptr);
		}
		for (VkDescriptorPool Pool : DescriptorPools)
		{
			vkDestroyDescriptorPool(Device, Pool, nullptr);
		}
		for (uint32 Index = 0; Index < 2; ++Index)
		{
			if (PipelineLayouts[Index]) vkDestroyPipelineLayout(Device, PipelineLayouts[Index], nullptr);
			if (SetLayouts[Index]) vkDestroyDescriptorSetLayout(Device, SetLayouts[Index], nullptr);
			if (Samplers[Index]) vkDestroySampler(Device, Samplers[Index], nullptr);
		}
		vkDestroyDevice(Device, nullptr);
	}
	if (DebugMessenger)
	{
		vkDestroyDebugUtilsMessengerEXT(Instance, DebugMessenger, nullptr);
	}
	if (Instance)
	{
		vkDestroyInstance(Instance, nullptr);
	}
	if (bLibraryLoaded)
	{
		UnloadVulkanLibrary();
	}
	GVulkanDeviceAlive = false;
}

bool FVulkanDevice::Initialize(const FRHIDeviceDesc& Desc)
{
	if (!LoadVulkanLibrary())
	{
		return false;
	}
	bLibraryLoaded = true;

	return CreateInstance(Desc.bDebugLayer)
		&& PickPhysicalDevice()
		&& CreateLogicalDevice()
		&& CreateSharedObjects();
}

bool FVulkanDevice::CreateInstance(bool bDebugLayer)
{
	uint32 LoaderVersion = VK_API_VERSION_1_0;
	if (vkEnumerateInstanceVersion)
	{
		vkEnumerateInstanceVersion(&LoaderVersion);
	}
	if (LoaderVersion < VK_API_VERSION_1_1)
	{
		UE_LOG(LogRHI, Warning, "Vulkan: the installed loader only supports Vulkan {}.{}; 1.1 is required",
			VK_API_VERSION_MAJOR(LoaderVersion), VK_API_VERSION_MINOR(LoaderVersion));
		return false;
	}

	uint32 Count = 0;
	vkEnumerateInstanceExtensionProperties(nullptr, &Count, nullptr);
	TArray<VkExtensionProperties> Extensions(Count);
	vkEnumerateInstanceExtensionProperties(nullptr, &Count, Extensions.data());
	Extensions.resize(Count);

	TArray<const char*> EnabledExtensions;
	for (const char* Required : { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME })
	{
		if (!HasExtension(Extensions, Required))
		{
			UE_LOG(LogRHI, Warning, "Vulkan: instance extension {} is not available", Required);
			return false;
		}
		EnabledExtensions.push_back(Required);
	}

	TArray<const char*> EnabledLayers;
	if (bDebugLayer)
	{
		static constexpr const char* ValidationLayer = "VK_LAYER_KHRONOS_validation";
		Count = 0;
		vkEnumerateInstanceLayerProperties(&Count, nullptr);
		TArray<VkLayerProperties> Layers(Count);
		vkEnumerateInstanceLayerProperties(&Count, Layers.data());
		Layers.resize(Count);
		const bool bHasValidation = std::any_of(Layers.begin(), Layers.end(),
			[](const VkLayerProperties& Layer) { return std::strcmp(Layer.layerName, ValidationLayer) == 0; });
		if (bHasValidation)
		{
			EnabledLayers.push_back(ValidationLayer);
			UE_LOG(LogRHI, Display, "Vulkan validation layer enabled");
		}
		else
		{
			UE_LOG(LogRHI, Display, "Vulkan: validation layer not installed; running without it");
		}

		// Debug utils (object names for tools, and validation messages).
		if (HasExtension(Extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
		{
			EnabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
			bDebugUtils = true;
		}
	}

	VkApplicationInfo AppInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
	AppInfo.pApplicationName = "Genesis";
	AppInfo.pEngineName = "Genesis";
	AppInfo.apiVersion = VK_API_VERSION_1_1;

	VkDebugUtilsMessengerCreateInfoEXT MessengerInfo{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
	MessengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	MessengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	MessengerInfo.pfnUserCallback = &DebugMessengerCallback;

	VkInstanceCreateInfo CreateInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	CreateInfo.pApplicationInfo = &AppInfo;
	CreateInfo.enabledExtensionCount = static_cast<uint32>(EnabledExtensions.size());
	CreateInfo.ppEnabledExtensionNames = EnabledExtensions.data();
	CreateInfo.enabledLayerCount = static_cast<uint32>(EnabledLayers.size());
	CreateInfo.ppEnabledLayerNames = EnabledLayers.data();
	if (bDebugUtils)
	{
		CreateInfo.pNext = &MessengerInfo; // also reports vkCreateInstance / vkDestroyInstance issues
	}

	const VkResult Result = vkCreateInstance(&CreateInfo, nullptr, &Instance);
	if (Result != VK_SUCCESS)
	{
		UE_LOG(LogRHI, Warning, "Vulkan: vkCreateInstance failed: {}", ResultToString(Result));
		Instance = VK_NULL_HANDLE;
		return false;
	}
	if (!LoadInstanceFunctions(Instance))
	{
		return false;
	}

	if (bDebugUtils && vkCreateDebugUtilsMessengerEXT)
	{
		VulkanUtil::Check(vkCreateDebugUtilsMessengerEXT(Instance, &MessengerInfo, nullptr, &DebugMessenger), "vkCreateDebugUtilsMessengerEXT");
	}
	bDebugUtils = bDebugUtils && vkSetDebugUtilsObjectNameEXT;
	return true;
}

bool FVulkanDevice::PickPhysicalDevice()
{
	uint32 Count = 0;
	vkEnumeratePhysicalDevices(Instance, &Count, nullptr);
	TArray<VkPhysicalDevice> Devices(Count);
	vkEnumeratePhysicalDevices(Instance, &Count, Devices.data());
	Devices.resize(Count);

	int32 BestScore = -1;
	for (VkPhysicalDevice Candidate : Devices)
	{
		VkPhysicalDeviceProperties Properties;
		vkGetPhysicalDeviceProperties(Candidate, &Properties);
		if (Properties.apiVersion < VK_API_VERSION_1_1)
		{
			continue;
		}

		uint32 ExtensionCount = 0;
		vkEnumerateDeviceExtensionProperties(Candidate, nullptr, &ExtensionCount, nullptr);
		TArray<VkExtensionProperties> Extensions(ExtensionCount);
		vkEnumerateDeviceExtensionProperties(Candidate, nullptr, &ExtensionCount, Extensions.data());
		Extensions.resize(ExtensionCount);
		if (!HasExtension(Extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
		{
			continue;
		}

		uint32 FamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(Candidate, &FamilyCount, nullptr);
		TArray<VkQueueFamilyProperties> Families(FamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(Candidate, &FamilyCount, Families.data());
		int32 Family = INDEX_NONE;
		for (uint32 Index = 0; Index < FamilyCount; ++Index)
		{
			if ((Families[Index].queueFlags & VK_QUEUE_GRAPHICS_BIT) && vkGetPhysicalDeviceWin32PresentationSupportKHR(Candidate, Index))
			{
				Family = static_cast<int32>(Index);
				break;
			}
		}
		if (Family == INDEX_NONE)
		{
			continue;
		}

		int32 Score = 0;
		switch (Properties.deviceType)
		{
		case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: Score = 4; break;
		case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: Score = 3; break;
		case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: Score = 2; break;
		case VK_PHYSICAL_DEVICE_TYPE_CPU: Score = 1; break;
		default: break;
		}
		if (Score > BestScore)
		{
			BestScore = Score;
			PhysicalDevice = Candidate;
			PhysicalDeviceProperties = Properties;
			QueueFamily = static_cast<uint32>(Family);
		}
	}

	if (!PhysicalDevice)
	{
		UE_LOG(LogRHI, Warning, "Vulkan: no GPU with Vulkan 1.1, graphics + presentation and VK_KHR_swapchain found");
		return false;
	}
	AdapterName = FString(PhysicalDeviceProperties.deviceName);
	vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &MemoryProperties);
	return true;
}

bool FVulkanDevice::CreateLogicalDevice()
{
	const float Priority = 1.0f;
	VkDeviceQueueCreateInfo QueueInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	QueueInfo.queueFamilyIndex = QueueFamily;
	QueueInfo.queueCount = 1;
	QueueInfo.pQueuePriorities = &Priority;

	const char* Extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	VkDeviceCreateInfo CreateInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	CreateInfo.queueCreateInfoCount = 1;
	CreateInfo.pQueueCreateInfos = &QueueInfo;
	CreateInfo.enabledExtensionCount = 1;
	CreateInfo.ppEnabledExtensionNames = Extensions;

	const VkResult Result = vkCreateDevice(PhysicalDevice, &CreateInfo, nullptr, &Device);
	if (Result != VK_SUCCESS)
	{
		UE_LOG(LogRHI, Warning, "Vulkan: vkCreateDevice failed: {}", ResultToString(Result));
		Device = VK_NULL_HANDLE;
		return false;
	}
	if (!LoadDeviceFunctions(Device))
	{
		return false;
	}
	vkGetDeviceQueue(Device, QueueFamily, 0, &Queue);
	return true;
}

bool FVulkanDevice::CreateSharedObjects()
{
	// Samplers, set layouts and pipeline layouts, one per ESamplerFilter.
	for (uint32 Index = 0; Index < 2; ++Index)
	{
		const VkFilter Filter = static_cast<ESamplerFilter>(Index) == ESamplerFilter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		VkSamplerCreateInfo SamplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		SamplerInfo.magFilter = Filter;
		SamplerInfo.minFilter = Filter;
		SamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		SamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		SamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		SamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		SamplerInfo.maxLod = 0.0f;
		if (!VulkanUtil::Check(vkCreateSampler(Device, &SamplerInfo, nullptr, &Samplers[Index]), "vkCreateSampler"))
		{
			return false;
		}

		VkDescriptorSetLayoutBinding Bindings[2] = {};
		Bindings[0].binding = 0;
		Bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		Bindings[0].descriptorCount = 1;
		Bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		Bindings[1].binding = 1;
		Bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		Bindings[1].descriptorCount = 1;
		Bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		Bindings[1].pImmutableSamplers = &Samplers[Index];

		VkDescriptorSetLayoutCreateInfo SetLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		SetLayoutInfo.bindingCount = 2;
		SetLayoutInfo.pBindings = Bindings;
		if (!VulkanUtil::Check(vkCreateDescriptorSetLayout(Device, &SetLayoutInfo, nullptr, &SetLayouts[Index]), "vkCreateDescriptorSetLayout"))
		{
			return false;
		}

		VkPushConstantRange PushRange{};
		PushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		PushRange.offset = 0;
		PushRange.size = MaxPushConstantSize;

		VkPipelineLayoutCreateInfo LayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		LayoutInfo.setLayoutCount = 1;
		LayoutInfo.pSetLayouts = &SetLayouts[Index];
		LayoutInfo.pushConstantRangeCount = 1;
		LayoutInfo.pPushConstantRanges = &PushRange;
		if (!VulkanUtil::Check(vkCreatePipelineLayout(Device, &LayoutInfo, nullptr, &PipelineLayouts[Index]), "vkCreatePipelineLayout"))
		{
			return false;
		}
	}
	SetDebugName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<uint64>(Samplers[0]), "LinearClampSampler");
	SetDebugName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<uint64>(Samplers[1]), "PointClampSampler");

	// Frames in flight.
	for (FFrame& Frame : Frames)
	{
		VkCommandPoolCreateInfo PoolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		PoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		PoolInfo.queueFamilyIndex = QueueFamily;
		if (!VulkanUtil::Check(vkCreateCommandPool(Device, &PoolInfo, nullptr, &Frame.CommandPool), "vkCreateCommandPool"))
		{
			return false;
		}
		VkCommandBuffer Buffers[2] = {};
		VkCommandBufferAllocateInfo AllocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		AllocInfo.commandPool = Frame.CommandPool;
		AllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		AllocInfo.commandBufferCount = 2;
		if (!VulkanUtil::Check(vkAllocateCommandBuffers(Device, &AllocInfo, Buffers), "vkAllocateCommandBuffers"))
		{
			return false;
		}
		Frame.CommandBuffer = Buffers[0];
		Frame.UploadCommandBuffer = Buffers[1];
		SetDebugName(VK_OBJECT_TYPE_COMMAND_BUFFER, reinterpret_cast<uint64>(Frame.CommandBuffer), "FrameCommandBuffer");
		SetDebugName(VK_OBJECT_TYPE_COMMAND_BUFFER, reinterpret_cast<uint64>(Frame.UploadCommandBuffer), "UploadCommandBuffer");

		VkFenceCreateInfo FenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		FenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		if (!VulkanUtil::Check(vkCreateFence(Device, &FenceInfo, nullptr, &Frame.Fence), "vkCreateFence"))
		{
			return false;
		}
	}

	// Immediate submissions (uploads outside frames, readbacks).
	{
		VkCommandPoolCreateInfo PoolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		PoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		PoolInfo.queueFamilyIndex = QueueFamily;
		if (!VulkanUtil::Check(vkCreateCommandPool(Device, &PoolInfo, nullptr, &ImmediatePool), "vkCreateCommandPool"))
		{
			return false;
		}
		VkCommandBufferAllocateInfo AllocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		AllocInfo.commandPool = ImmediatePool;
		AllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		AllocInfo.commandBufferCount = 1;
		if (!VulkanUtil::Check(vkAllocateCommandBuffers(Device, &AllocInfo, &ImmediateCommandBuffer), "vkAllocateCommandBuffers"))
		{
			return false;
		}
		SetDebugName(VK_OBJECT_TYPE_COMMAND_BUFFER, reinterpret_cast<uint64>(ImmediateCommandBuffer), "ImmediateCommandBuffer");
		VkFenceCreateInfo FenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		if (!VulkanUtil::Check(vkCreateFence(Device, &FenceInfo, nullptr, &ImmediateFence), "vkCreateFence"))
		{
			return false;
		}
	}

	CommandList = MakeUnique<FVulkanCommandList>(*this);

	// Bound when a draw has no texture set, so shaders always see a valid image.
	const uint32 White = 0xFFFFFFFFu;
	FRHITextureDesc WhiteDesc;
	WhiteDesc.Format = EPixelFormat::R8G8B8A8_UNORM;
	WhiteDesc.InitialData = &White;
	WhiteDesc.DebugName = "WhiteTexture";
	WhiteTexture = std::make_shared<FVulkanTexture>(*this, WhiteDesc);
	if (!WhiteTexture->Initialize(&White, 0))
	{
		WhiteTexture.reset();
		return false;
	}
	return true;
}

void FVulkanDevice::SetDebugName(VkObjectType Type, uint64 Handle, const char* Name)
{
	if (!bDebugUtils || !Name || !Handle)
	{
		return;
	}
	VkDebugUtilsObjectNameInfoEXT NameInfo{ VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
	NameInfo.objectType = Type;
	NameInfo.objectHandle = Handle;
	NameInfo.pObjectName = Name;
	vkSetDebugUtilsObjectNameEXT(Device, &NameInfo);
}

uint32 FVulkanDevice::FindMemoryType(uint32 TypeBits, VkMemoryPropertyFlags Required, VkMemoryPropertyFlags Preferred) const
{
	for (const VkMemoryPropertyFlags Wanted : { Required | Preferred, Required })
	{
		for (uint32 Index = 0; Index < MemoryProperties.memoryTypeCount; ++Index)
		{
			if ((TypeBits & (1u << Index)) && (MemoryProperties.memoryTypes[Index].propertyFlags & Wanted) == Wanted)
			{
				return Index;
			}
		}
	}
	return UINT32_MAX;
}

bool FVulkanDevice::AllocateMemory(const VkMemoryRequirements& Requirements, VkMemoryPropertyFlags Required, VkMemoryPropertyFlags Preferred, VkDeviceMemory& OutMemory)
{
	const uint32 TypeIndex = FindMemoryType(Requirements.memoryTypeBits, Required, Preferred);
	if (TypeIndex == UINT32_MAX)
	{
		UE_LOG(LogRHI, Error, "Vulkan: no suitable memory type (bits 0x{:x}, flags 0x{:x})", Requirements.memoryTypeBits, Required);
		return false;
	}
	VkMemoryAllocateInfo AllocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	AllocInfo.allocationSize = Requirements.size;
	AllocInfo.memoryTypeIndex = TypeIndex;
	return VulkanUtil::Check(vkAllocateMemory(Device, &AllocInfo, nullptr, &OutMemory), "vkAllocateMemory");
}

bool FVulkanDevice::CreateBufferObject(VkDeviceSize Size, VkBufferUsageFlags Usage, bool bHostVisible, VkBuffer& OutBuffer, VkDeviceMemory& OutMemory, void** OutMapped)
{
	OutBuffer = VK_NULL_HANDLE;
	OutMemory = VK_NULL_HANDLE;
	VkBufferCreateInfo BufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	BufferInfo.size = Size;
	BufferInfo.usage = Usage;
	BufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (!VulkanUtil::Check(vkCreateBuffer(Device, &BufferInfo, nullptr, &OutBuffer), "vkCreateBuffer"))
	{
		OutBuffer = VK_NULL_HANDLE;
		return false;
	}

	VkMemoryRequirements Requirements;
	vkGetBufferMemoryRequirements(Device, OutBuffer, &Requirements);
	const VkMemoryPropertyFlags Required = bHostVisible ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) : 0;
	const VkMemoryPropertyFlags Preferred = bHostVisible ? 0 : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	if (!AllocateMemory(Requirements, Required, Preferred, OutMemory)
		|| !VulkanUtil::Check(vkBindBufferMemory(Device, OutBuffer, OutMemory, 0), "vkBindBufferMemory")
		|| (OutMapped && !VulkanUtil::Check(vkMapMemory(Device, OutMemory, 0, VK_WHOLE_SIZE, 0, OutMapped), "vkMapMemory")))
	{
		vkDestroyBuffer(Device, OutBuffer, nullptr);
		if (OutMemory)
		{
			vkFreeMemory(Device, OutMemory, nullptr);
		}
		OutBuffer = VK_NULL_HANDLE;
		OutMemory = VK_NULL_HANDLE;
		return false;
	}
	return true;
}

VkRenderPass FVulkanDevice::GetRenderPass(VkFormat Format, bool bClear, VkImageLayout InitialLayout, VkImageLayout FinalLayout,
	VkFormat DepthFormat, bool bClearDepth)
{
	// Formats are small enum values (below 2^16).
	const uint64 Key = static_cast<uint64>(Format)
		| (static_cast<uint64>(bClear) << 32)
		| (static_cast<uint64>(LayoutKey(InitialLayout)) << 33)
		| (static_cast<uint64>(LayoutKey(FinalLayout)) << 36)
		| (static_cast<uint64>(bClearDepth) << 39)
		| (static_cast<uint64>(DepthFormat) << 40);
	if (auto It = RenderPasses.find(Key); It != RenderPasses.end())
	{
		return It->second;
	}

	VkAttachmentDescription Attachment{};
	Attachment.format = Format;
	Attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	Attachment.loadOp = bClear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
	Attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	Attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	Attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	Attachment.initialLayout = InitialLayout;
	Attachment.finalLayout = FinalLayout;

	VkAttachmentDescription Attachments[2] = { Attachment, {} };
	const bool bDepth = DepthFormat != VK_FORMAT_UNDEFINED;
	if (bDepth)
	{
		VkAttachmentDescription& DepthAttachment = Attachments[1];
		DepthAttachment.format = DepthFormat;
		DepthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		DepthAttachment.loadOp = bClearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
		DepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		DepthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		DepthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		DepthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		DepthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	}

	VkAttachmentReference ColorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	VkAttachmentReference DepthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
	VkSubpassDescription Subpass{};
	Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	Subpass.colorAttachmentCount = 1;
	Subpass.pColorAttachments = &ColorRef;
	Subpass.pDepthStencilAttachment = bDepth ? &DepthRef : nullptr;

	// In: wait for earlier sampling/copies/attachment writes of the image (and,
	// for swapchains, for the acquire semaphore wait at COLOR_ATTACHMENT_OUTPUT).
	// Out: make the attachment writes visible to later sampling and copies.
	VkSubpassDependency Dependencies[2] = {};
	Dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	Dependencies[0].dstSubpass = 0;
	Dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
	Dependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
	Dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	Dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	Dependencies[1].srcSubpass = 0;
	Dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	Dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	Dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	Dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
	Dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
	if (bDepth)
	{
		// The depth buffer is written by the previous pass that used it.
		Dependencies[0].srcStageMask |= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		Dependencies[0].srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		Dependencies[0].dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		Dependencies[0].dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	}

	VkRenderPassCreateInfo CreateInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	CreateInfo.attachmentCount = bDepth ? 2u : 1u;
	CreateInfo.pAttachments = Attachments;
	CreateInfo.subpassCount = 1;
	CreateInfo.pSubpasses = &Subpass;
	CreateInfo.dependencyCount = 2;
	CreateInfo.pDependencies = Dependencies;

	VkRenderPass RenderPass = VK_NULL_HANDLE;
	if (!VulkanUtil::Check(vkCreateRenderPass(Device, &CreateInfo, nullptr, &RenderPass), "vkCreateRenderPass"))
	{
		return VK_NULL_HANDLE;
	}
	RenderPasses.emplace(Key, RenderPass);
	return RenderPass;
}

VkDescriptorSet FVulkanDevice::AllocateDescriptorSet(ESamplerFilter Filter, VkDescriptorPool& OutPool)
{
	VkDescriptorSetAllocateInfo AllocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	AllocInfo.descriptorSetCount = 1;
	AllocInfo.pSetLayouts = &SetLayouts[static_cast<uint32>(Filter)];

	// Try the newest pool first, then create a new one.
	for (int32 Attempt = 0; Attempt < 2; ++Attempt)
	{
		if (!DescriptorPools.empty())
		{
			AllocInfo.descriptorPool = DescriptorPools.back();
			VkDescriptorSet Set = VK_NULL_HANDLE;
			const VkResult Result = vkAllocateDescriptorSets(Device, &AllocInfo, &Set);
			if (Result == VK_SUCCESS)
			{
				OutPool = AllocInfo.descriptorPool;
				return Set;
			}
			if (Result != VK_ERROR_OUT_OF_POOL_MEMORY && Result != VK_ERROR_FRAGMENTED_POOL)
			{
				VulkanUtil::Check(Result, "vkAllocateDescriptorSets");
				return VK_NULL_HANDLE;
			}
		}

		const VkDescriptorPoolSize Sizes[] = {
			{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, DescriptorSetsPerPool },
			{ VK_DESCRIPTOR_TYPE_SAMPLER, DescriptorSetsPerPool },
		};
		VkDescriptorPoolCreateInfo PoolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		PoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		PoolInfo.maxSets = DescriptorSetsPerPool;
		PoolInfo.poolSizeCount = 2;
		PoolInfo.pPoolSizes = Sizes;
		VkDescriptorPool Pool = VK_NULL_HANDLE;
		if (!VulkanUtil::Check(vkCreateDescriptorPool(Device, &PoolInfo, nullptr, &Pool), "vkCreateDescriptorPool"))
		{
			return VK_NULL_HANDLE;
		}
		DescriptorPools.push_back(Pool);
	}
	return VK_NULL_HANDLE;
}

TUniquePtr<FRHISwapChain> FVulkanDevice::CreateSwapChain(IPlatformWindow& Window)
{
	TUniquePtr<FVulkanSwapChain> SwapChain = MakeUnique<FVulkanSwapChain>(*this, Window);
	if (!SwapChain->Initialize())
	{
		return nullptr;
	}
	return SwapChain;
}

TSharedPtr<FRHIBuffer> FVulkanDevice::CreateBuffer(const FRHIBufferDesc& Desc)
{
	TSharedPtr<FVulkanBuffer> Buffer = std::make_shared<FVulkanBuffer>(*this, Desc);
	if (!Buffer->Initialize())
	{
		return nullptr;
	}
	return Buffer;
}

TSharedPtr<FRHITexture> FVulkanDevice::CreateTexture(const FRHITextureDesc& Desc)
{
	TSharedPtr<FVulkanTexture> Texture = std::make_shared<FVulkanTexture>(*this, Desc);
	if (!Texture->Initialize(Desc.InitialData, Desc.InitialRowPitch))
	{
		return nullptr;
	}
	return Texture;
}

TSharedPtr<FRHIGraphicsPipeline> FVulkanDevice::CreateGraphicsPipeline(const FRHIGraphicsPipelineDesc& Desc)
{
	TSharedPtr<FVulkanGraphicsPipeline> Pipeline = std::make_shared<FVulkanGraphicsPipeline>(*this, Desc);
	if (!Pipeline->Initialize())
	{
		return nullptr;
	}
	return Pipeline;
}

bool FVulkanDevice::BeginUpload(VkDeviceSize StagingSize, FVulkanUploadContext& OutContext)
{
	OutContext = FVulkanUploadContext();
	if (bInFrame)
	{
		// Recorded into the frame's upload command buffer, submitted before the
		// frame's main command buffer. Staging comes from the frame's ring,
		// which is reused once the GPU has finished the frame.
		FFrame& Frame = Frames[FrameIndex];
		if (StagingSize > 0)
		{
			VkDeviceSize Offset = AlignUp(Frame.StagingOffset, 16);
			if (Offset + StagingSize > Frame.StagingSize)
			{
				// Grow: the old buffer may still be read by this frame's copies.
				if (Frame.StagingBuffer)
				{
					DeferDestroy([DeviceHandle = Device, Buffer = Frame.StagingBuffer, Memory = Frame.StagingMemory]()
					{
						vkDestroyBuffer(DeviceHandle, Buffer, nullptr);
						vkFreeMemory(DeviceHandle, Memory, nullptr);
					});
					Frame.StagingBuffer = VK_NULL_HANDLE;
					Frame.StagingMemory = VK_NULL_HANDLE;
					Frame.StagingData = nullptr;
				}
				const VkDeviceSize NewSize = std::max({ MinStagingSize, Frame.StagingSize * 2, AlignUp(StagingSize, 64 * 1024) });
				void* Mapped = nullptr;
				if (!CreateBufferObject(NewSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, Frame.StagingBuffer, Frame.StagingMemory, &Mapped))
				{
					Frame.StagingSize = 0;
					Frame.StagingOffset = 0;
					return false;
				}
				SetDebugName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64>(Frame.StagingBuffer), "FrameStagingBuffer");
				Frame.StagingData = static_cast<uint8*>(Mapped);
				Frame.StagingSize = NewSize;
				Offset = 0;
			}
			OutContext.StagingBuffer = Frame.StagingBuffer;
			OutContext.StagingOffset = Offset;
			OutContext.StagingData = Frame.StagingData + Offset;
			Frame.StagingOffset = Offset + StagingSize;
		}

		if (!Frame.bUploadRecording)
		{
			VkCommandBufferBeginInfo BeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
			BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			if (!VulkanUtil::Check(vkBeginCommandBuffer(Frame.UploadCommandBuffer, &BeginInfo), "vkBeginCommandBuffer"))
			{
				return false;
			}
			Frame.bUploadRecording = true;
		}
		OutContext.CommandBuffer = Frame.UploadCommandBuffer;
		return true;
	}

	// Outside a frame: a temporary staging buffer and an immediate submit.
	OutContext.bImmediate = true;
	if (StagingSize > 0)
	{
		void* Mapped = nullptr;
		if (!CreateBufferObject(StagingSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, OutContext.StagingBuffer, OutContext.TempMemory, &Mapped))
		{
			return false;
		}
		OutContext.StagingData = static_cast<uint8*>(Mapped);
	}
	VkCommandBufferBeginInfo BeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if (!VulkanUtil::Check(vkBeginCommandBuffer(ImmediateCommandBuffer, &BeginInfo), "vkBeginCommandBuffer"))
	{
		if (OutContext.StagingBuffer)
		{
			vkDestroyBuffer(Device, OutContext.StagingBuffer, nullptr);
			vkFreeMemory(Device, OutContext.TempMemory, nullptr);
		}
		return false;
	}
	OutContext.CommandBuffer = ImmediateCommandBuffer;
	return true;
}

void FVulkanDevice::EndUpload(FVulkanUploadContext& Context)
{
	if (!Context.bImmediate)
	{
		return; // submitted with the frame
	}
	SubmitImmediate(Context.CommandBuffer);
	if (Context.StagingBuffer)
	{
		vkDestroyBuffer(Device, Context.StagingBuffer, nullptr);
		vkFreeMemory(Device, Context.TempMemory, nullptr);
	}
	Context = FVulkanUploadContext();
}

void FVulkanDevice::SubmitImmediate(VkCommandBuffer CommandBuffer)
{
	VulkanUtil::Check(vkEndCommandBuffer(CommandBuffer), "vkEndCommandBuffer");
	VkSubmitInfo SubmitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
	SubmitInfo.commandBufferCount = 1;
	SubmitInfo.pCommandBuffers = &CommandBuffer;
	if (VulkanUtil::Check(vkQueueSubmit(Queue, 1, &SubmitInfo, ImmediateFence), "vkQueueSubmit"))
	{
		vkWaitForFences(Device, 1, &ImmediateFence, VK_TRUE, UINT64_MAX);
	}
	vkResetFences(Device, 1, &ImmediateFence);
}

void FVulkanDevice::FlushPendingUploads()
{
	FFrame& Frame = Frames[FrameIndex];
	if (!bInFrame || !Frame.bUploadRecording)
	{
		return;
	}
	// The frame's upload command buffer can be re-begun later this frame
	// (the pool allows resetting individual command buffers).
	Frame.bUploadRecording = false;
	SubmitImmediate(Frame.UploadCommandBuffer);
}

void FVulkanDevice::UpdateTexture(FRHITexture& InTexture, const FIntRect& Region, const void* Data, uint32 RowPitch)
{
	FVulkanTexture& Texture = static_cast<FVulkanTexture&>(InTexture);
	if (!ensuref(Data && !Region.IsEmpty() && Region.Left >= 0 && Region.Top >= 0
		&& Region.Right <= static_cast<int32>(Texture.GetWidth()) && Region.Bottom <= static_cast<int32>(Texture.GetHeight()),
		"UpdateTexture: region ({}, {})-({}, {}) outside the {}x{} texture", Region.Left, Region.Top, Region.Right, Region.Bottom, Texture.GetWidth(), Texture.GetHeight()))
	{
		return;
	}

	const uint32 BytesPerPixel = GetBytesPerPixel(Texture.GetFormat());
	const uint32 RowBytes = static_cast<uint32>(Region.Width()) * BytesPerPixel;
	const uint32 SourcePitch = RowPitch ? RowPitch : RowBytes;
	const uint32 Rows = static_cast<uint32>(Region.Height());

	FVulkanUploadContext Upload;
	if (!BeginUpload(static_cast<VkDeviceSize>(RowBytes) * Rows, Upload))
	{
		return;
	}
	const uint8* Source = static_cast<const uint8*>(Data);
	for (uint32 Row = 0; Row < Rows; ++Row)
	{
		std::memcpy(Upload.StagingData + static_cast<SIZE_T>(Row) * RowBytes, Source + static_cast<SIZE_T>(Row) * SourcePitch, RowBytes);
	}

	VkImageMemoryBarrier Barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	// Earlier writes (render passes, uploads) were already made available by
	// their own outgoing dependencies; only an execution dependency is needed.
	Barrier.srcAccessMask = 0;
	Barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	Barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	Barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	Barrier.image = Texture.GetImage();
	Barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	vkCmdPipelineBarrier(Upload.CommandBuffer,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &Barrier);

	VkBufferImageCopy Copy{};
	Copy.bufferOffset = Upload.StagingOffset;
	Copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	Copy.imageOffset = { Region.Left, Region.Top, 0 };
	Copy.imageExtent = { static_cast<uint32>(Region.Width()), Rows, 1 };
	vkCmdCopyBufferToImage(Upload.CommandBuffer, Upload.StagingBuffer, Texture.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Copy);

	Barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	Barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	Barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	vkCmdPipelineBarrier(Upload.CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0, 0, nullptr, 0, nullptr, 1, &Barrier);

	EndUpload(Upload);
}

bool FVulkanDevice::ReadbackTexture(FRHITexture& InTexture, TArray<uint8>& OutPixels)
{
	FVulkanTexture& Texture = static_cast<FVulkanTexture&>(InTexture);
	FlushPendingUploads();

	const uint32 BytesPerPixel = GetBytesPerPixel(Texture.GetFormat());
	const VkDeviceSize Size = static_cast<VkDeviceSize>(Texture.GetWidth()) * Texture.GetHeight() * BytesPerPixel;

	VkBuffer Buffer = VK_NULL_HANDLE;
	VkDeviceMemory Memory = VK_NULL_HANDLE;
	void* Mapped = nullptr;
	if (!CreateBufferObject(Size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, Buffer, Memory, &Mapped))
	{
		return false;
	}

	VkCommandBufferBeginInfo BeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(ImmediateCommandBuffer, &BeginInfo);

	VkImageMemoryBarrier Barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	// Earlier writes (render passes, uploads) were already made available by
	// their own outgoing dependencies; only an execution dependency is needed.
	Barrier.srcAccessMask = 0;
	Barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	Barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	Barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	Barrier.image = Texture.GetImage();
	Barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	vkCmdPipelineBarrier(ImmediateCommandBuffer,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &Barrier);

	VkBufferImageCopy Copy{};
	Copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	Copy.imageExtent = { Texture.GetWidth(), Texture.GetHeight(), 1 };
	vkCmdCopyImageToBuffer(ImmediateCommandBuffer, Texture.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Buffer, 1, &Copy);

	Barrier.srcAccessMask = 0;
	Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	Barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	Barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	vkCmdPipelineBarrier(ImmediateCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0, 0, nullptr, 0, nullptr, 1, &Barrier);

	VkBufferMemoryBarrier HostBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
	HostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	HostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	HostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	HostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	HostBarrier.buffer = Buffer;
	HostBarrier.size = VK_WHOLE_SIZE;
	vkCmdPipelineBarrier(ImmediateCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
		0, 0, nullptr, 1, &HostBarrier, 0, nullptr);

	SubmitImmediate(ImmediateCommandBuffer);

	OutPixels.resize(static_cast<SIZE_T>(Size));
	std::memcpy(OutPixels.data(), Mapped, static_cast<SIZE_T>(Size));
	vkDestroyBuffer(Device, Buffer, nullptr);
	vkFreeMemory(Device, Memory, nullptr);
	return true;
}

FRHICommandList& FVulkanDevice::BeginFrame()
{
	check(!bInFrame);
	FFrame& Frame = Frames[FrameIndex];

	// Wait for the frame that last used this slot, then recycle its resources.
	vkWaitForFences(Device, 1, &Frame.Fence, VK_TRUE, UINT64_MAX);
	CompletedSerial = std::max(CompletedSerial, Frame.SubmittedSerial);
	CollectGarbage();
	vkResetFences(Device, 1, &Frame.Fence);
	vkResetCommandPool(Device, Frame.CommandPool, 0);
	Frame.StagingOffset = 0;
	Frame.bUploadRecording = false;

	VkCommandBufferBeginInfo BeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VulkanUtil::Check(vkBeginCommandBuffer(Frame.CommandBuffer, &BeginInfo), "vkBeginCommandBuffer");

	bInFrame = true;
	CommandList->Begin(Frame.CommandBuffer);
	return *CommandList;
}

void FVulkanDevice::EndFrame()
{
	check(bInFrame);
	FFrame& Frame = Frames[FrameIndex];
	CommandList->End();
	VulkanUtil::Check(vkEndCommandBuffer(Frame.CommandBuffer), "vkEndCommandBuffer");

	TArray<VkCommandBuffer> CommandBuffers;
	if (Frame.bUploadRecording)
	{
		VulkanUtil::Check(vkEndCommandBuffer(Frame.UploadCommandBuffer), "vkEndCommandBuffer");
		CommandBuffers.push_back(Frame.UploadCommandBuffer);
		Frame.bUploadRecording = false;
	}
	CommandBuffers.push_back(Frame.CommandBuffer);

	TArray<VkSemaphore> WaitSemaphores;
	TArray<VkPipelineStageFlags> WaitStages;
	TArray<VkSemaphore> SignalSemaphores;
	for (FVulkanSwapChain* SwapChain : AcquiredSwapChains)
	{
		WaitSemaphores.push_back(SwapChain->GetAcquireSemaphore());
		WaitStages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
		SignalSemaphores.push_back(SwapChain->GetRenderFinishedSemaphore());
	}

	VkSubmitInfo SubmitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
	SubmitInfo.waitSemaphoreCount = static_cast<uint32>(WaitSemaphores.size());
	SubmitInfo.pWaitSemaphores = WaitSemaphores.data();
	SubmitInfo.pWaitDstStageMask = WaitStages.data();
	SubmitInfo.commandBufferCount = static_cast<uint32>(CommandBuffers.size());
	SubmitInfo.pCommandBuffers = CommandBuffers.data();
	SubmitInfo.signalSemaphoreCount = static_cast<uint32>(SignalSemaphores.size());
	SubmitInfo.pSignalSemaphores = SignalSemaphores.data();
	VulkanUtil::Check(vkQueueSubmit(Queue, 1, &SubmitInfo, Frame.Fence), "vkQueueSubmit");

	Frame.SubmittedSerial = CurrentSerial++;

	for (FVulkanSwapChain* SwapChain : AcquiredSwapChains)
	{
		SwapChain->Present(Queue);
	}
	AcquiredSwapChains.clear();

	bInFrame = false;
	FrameIndex = (FrameIndex + 1) % MaxFramesInFlight;
}

void FVulkanDevice::WaitIdle()
{
	vkDeviceWaitIdle(Device);
	// Everything submitted is done; garbage of the frame being recorded (if
	// any) must still wait for that frame.
	CompletedSerial = CurrentSerial - 1;
	CollectGarbage();
}

void FVulkanDevice::DeferRelease(TSharedPtr<void> Resource)
{
	DeferDestroy([Held = std::move(Resource)]() mutable { Held.reset(); });
}

void FVulkanDevice::DeferDestroy(TFunction<void()> Destroy)
{
	Garbage.push_back(FGarbage{ CurrentSerial, std::move(Destroy) });
}

void FVulkanDevice::CollectGarbage()
{
	// Move the ready entries out first: running them can destroy RHI
	// resources, whose destructors add new garbage.
	TArray<TFunction<void()>> Ready;
	while (!Garbage.empty() && Garbage.front().Serial <= CompletedSerial)
	{
		Ready.push_back(std::move(Garbage.front().Destroy));
		Garbage.pop_front();
	}
	for (TFunction<void()>& Destroy : Ready)
	{
		Destroy();
	}
}

void FVulkanDevice::OnSwapChainAcquired(FVulkanSwapChain* SwapChain)
{
	if (!ArrayUtil::Contains(AcquiredSwapChains, SwapChain))
	{
		AcquiredSwapChains.push_back(SwapChain);
	}
}

void FVulkanDevice::OnSwapChainDestroyed(FVulkanSwapChain* SwapChain)
{
	if (ArrayUtil::Remove(AcquiredSwapChains, SwapChain) > 0)
	{
		UE_LOG(LogRHI, Warning, "Vulkan: swapchain destroyed in the middle of a frame that draws into it");
	}
}
