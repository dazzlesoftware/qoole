// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Vulkan/VulkanLoader.h"

#include "RHI.h"

namespace VulkanDynamicAPI
{
#define DEFINE_VK_ENTRYPOINTS(Name) PFN_##Name Name = nullptr;
	PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
	ENUM_VK_ENTRYPOINTS_BASE(DEFINE_VK_ENTRYPOINTS)
	ENUM_VK_ENTRYPOINTS_OPTIONAL_BASE(DEFINE_VK_ENTRYPOINTS)
	ENUM_VK_ENTRYPOINTS_INSTANCE(DEFINE_VK_ENTRYPOINTS)
	ENUM_VK_ENTRYPOINTS_OPTIONAL_INSTANCE(DEFINE_VK_ENTRYPOINTS)
	ENUM_VK_ENTRYPOINTS_DEVICE(DEFINE_VK_ENTRYPOINTS)
#undef DEFINE_VK_ENTRYPOINTS

	namespace
	{
		HMODULE VulkanModule = nullptr;
		int32 VulkanModuleRefCount = 0;
	} // namespace

	bool LoadVulkanLibrary()
	{
		if (VulkanModuleRefCount > 0)
		{
			++VulkanModuleRefCount;
			return true;
		}

		VulkanModule = LoadLibraryW(L"vulkan-1.dll");
		if (!VulkanModule)
		{
			GENESIS_LOG(LogRHI, Warning, "Vulkan: vulkan-1.dll not found (no Vulkan driver installed)");
			return false;
		}

		vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
		    reinterpret_cast<void*>(GetProcAddress(VulkanModule, "vkGetInstanceProcAddr")));
		if (!vkGetInstanceProcAddr)
		{
			GENESIS_LOG(LogRHI, Warning, "Vulkan: vulkan-1.dll has no vkGetInstanceProcAddr");
			FreeLibrary(VulkanModule);
			VulkanModule = nullptr;
			return false;
		}

		bool bOk = true;
#define GET_VK_ENTRYPOINTS(Name)                                                                                       \
	Name = reinterpret_cast<PFN_##Name>(vkGetInstanceProcAddr(VK_NULL_HANDLE, #Name));                                 \
	if (!Name)                                                                                                         \
	{                                                                                                                  \
		GENESIS_LOG(LogRHI, Warning, "Vulkan: missing entry point {}", #Name);                                         \
		bOk = false;                                                                                                   \
	}
#define GET_VK_ENTRYPOINTS_OPTIONAL(Name)                                                                              \
	Name = reinterpret_cast<PFN_##Name>(vkGetInstanceProcAddr(VK_NULL_HANDLE, #Name));
		ENUM_VK_ENTRYPOINTS_BASE(GET_VK_ENTRYPOINTS)
		ENUM_VK_ENTRYPOINTS_OPTIONAL_BASE(GET_VK_ENTRYPOINTS_OPTIONAL)
#undef GET_VK_ENTRYPOINTS
#undef GET_VK_ENTRYPOINTS_OPTIONAL

		if (!bOk)
		{
			FreeLibrary(VulkanModule);
			VulkanModule = nullptr;
			return false;
		}
		VulkanModuleRefCount = 1;
		return true;
	}

	void UnloadVulkanLibrary()
	{
		if (VulkanModuleRefCount <= 0 || --VulkanModuleRefCount > 0)
		{
			return;
		}
#define CLEAR_VK_ENTRYPOINTS(Name) Name = nullptr;
		ENUM_VK_ENTRYPOINTS_BASE(CLEAR_VK_ENTRYPOINTS)
		ENUM_VK_ENTRYPOINTS_OPTIONAL_BASE(CLEAR_VK_ENTRYPOINTS)
		ENUM_VK_ENTRYPOINTS_INSTANCE(CLEAR_VK_ENTRYPOINTS)
		ENUM_VK_ENTRYPOINTS_OPTIONAL_INSTANCE(CLEAR_VK_ENTRYPOINTS)
		ENUM_VK_ENTRYPOINTS_DEVICE(CLEAR_VK_ENTRYPOINTS)
#undef CLEAR_VK_ENTRYPOINTS
		vkGetInstanceProcAddr = nullptr;
		FreeLibrary(VulkanModule);
		VulkanModule = nullptr;
	}

	bool LoadInstanceFunctions(VkInstance Instance)
	{
		bool bOk = true;
#define GET_INSTANCE_VK_ENTRYPOINTS(Name)                                                                              \
	Name = reinterpret_cast<PFN_##Name>(vkGetInstanceProcAddr(Instance, #Name));                                       \
	if (!Name)                                                                                                         \
	{                                                                                                                  \
		GENESIS_LOG(LogRHI, Warning, "Vulkan: missing entry point {}", #Name);                                         \
		bOk = false;                                                                                                   \
	}
#define GET_INSTANCE_VK_ENTRYPOINTS_OPTIONAL(Name)                                                                     \
	Name = reinterpret_cast<PFN_##Name>(vkGetInstanceProcAddr(Instance, #Name));
		ENUM_VK_ENTRYPOINTS_INSTANCE(GET_INSTANCE_VK_ENTRYPOINTS)
		ENUM_VK_ENTRYPOINTS_OPTIONAL_INSTANCE(GET_INSTANCE_VK_ENTRYPOINTS_OPTIONAL)
#undef GET_INSTANCE_VK_ENTRYPOINTS
#undef GET_INSTANCE_VK_ENTRYPOINTS_OPTIONAL
		return bOk;
	}

	bool LoadDeviceFunctions(VkDevice Device)
	{
		bool bOk = true;
#define GET_DEVICE_VK_ENTRYPOINTS(Name)                                                                                \
	Name = reinterpret_cast<PFN_##Name>(vkGetDeviceProcAddr(Device, #Name));                                           \
	if (!Name)                                                                                                         \
	{                                                                                                                  \
		GENESIS_LOG(LogRHI, Warning, "Vulkan: missing entry point {}", #Name);                                         \
		bOk = false;                                                                                                   \
	}
		ENUM_VK_ENTRYPOINTS_DEVICE(GET_DEVICE_VK_ENTRYPOINTS)
#undef GET_DEVICE_VK_ENTRYPOINTS
		return bOk;
	}

	const char* ResultToString(VkResult Result)
	{
		switch (Result)
		{
		case VK_SUCCESS:
			return "VK_SUCCESS";
		case VK_NOT_READY:
			return "VK_NOT_READY";
		case VK_TIMEOUT:
			return "VK_TIMEOUT";
		case VK_INCOMPLETE:
			return "VK_INCOMPLETE";
		case VK_SUBOPTIMAL_KHR:
			return "VK_SUBOPTIMAL_KHR";
		case VK_ERROR_OUT_OF_HOST_MEMORY:
			return "VK_ERROR_OUT_OF_HOST_MEMORY";
		case VK_ERROR_OUT_OF_DEVICE_MEMORY:
			return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
		case VK_ERROR_INITIALIZATION_FAILED:
			return "VK_ERROR_INITIALIZATION_FAILED";
		case VK_ERROR_DEVICE_LOST:
			return "VK_ERROR_DEVICE_LOST";
		case VK_ERROR_MEMORY_MAP_FAILED:
			return "VK_ERROR_MEMORY_MAP_FAILED";
		case VK_ERROR_LAYER_NOT_PRESENT:
			return "VK_ERROR_LAYER_NOT_PRESENT";
		case VK_ERROR_EXTENSION_NOT_PRESENT:
			return "VK_ERROR_EXTENSION_NOT_PRESENT";
		case VK_ERROR_FEATURE_NOT_PRESENT:
			return "VK_ERROR_FEATURE_NOT_PRESENT";
		case VK_ERROR_INCOMPATIBLE_DRIVER:
			return "VK_ERROR_INCOMPATIBLE_DRIVER";
		case VK_ERROR_TOO_MANY_OBJECTS:
			return "VK_ERROR_TOO_MANY_OBJECTS";
		case VK_ERROR_FORMAT_NOT_SUPPORTED:
			return "VK_ERROR_FORMAT_NOT_SUPPORTED";
		case VK_ERROR_FRAGMENTED_POOL:
			return "VK_ERROR_FRAGMENTED_POOL";
		case VK_ERROR_OUT_OF_POOL_MEMORY:
			return "VK_ERROR_OUT_OF_POOL_MEMORY";
		case VK_ERROR_SURFACE_LOST_KHR:
			return "VK_ERROR_SURFACE_LOST_KHR";
		case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
			return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
		case VK_ERROR_OUT_OF_DATE_KHR:
			return "VK_ERROR_OUT_OF_DATE_KHR";
		default:
			break;
		}
		return "VK_ERROR_(other)";
	}
} // namespace VulkanDynamicAPI
