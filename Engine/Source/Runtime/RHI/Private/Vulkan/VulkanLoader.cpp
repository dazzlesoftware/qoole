// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Vulkan/VulkanLoader.h"

#include "RHI.h"

namespace GenesisVk
{
#define GENESIS_VK_DEFINE(Name) PFN_##Name Name = nullptr;
	PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
	GENESIS_VK_GLOBAL_FUNCTIONS(GENESIS_VK_DEFINE)
	GENESIS_VK_GLOBAL_OPTIONAL_FUNCTIONS(GENESIS_VK_DEFINE)
	GENESIS_VK_INSTANCE_FUNCTIONS(GENESIS_VK_DEFINE)
	GENESIS_VK_INSTANCE_OPTIONAL_FUNCTIONS(GENESIS_VK_DEFINE)
	GENESIS_VK_DEVICE_FUNCTIONS(GENESIS_VK_DEFINE)
#undef GENESIS_VK_DEFINE

	namespace
	{
		HMODULE VulkanModule = nullptr;
		int32 VulkanModuleRefCount = 0;
	}

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
			GE_LOG(LogRHI, Warning, "Vulkan: vulkan-1.dll not found (no Vulkan driver installed)");
			return false;
		}

		vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(reinterpret_cast<void*>(GetProcAddress(VulkanModule, "vkGetInstanceProcAddr")));
		if (!vkGetInstanceProcAddr)
		{
			GE_LOG(LogRHI, Warning, "Vulkan: vulkan-1.dll has no vkGetInstanceProcAddr");
			FreeLibrary(VulkanModule);
			VulkanModule = nullptr;
			return false;
		}

		bool bOk = true;
#define GENESIS_VK_LOAD_GLOBAL(Name) \
		Name = reinterpret_cast<PFN_##Name>(vkGetInstanceProcAddr(VK_NULL_HANDLE, #Name)); \
		if (!Name) { GE_LOG(LogRHI, Warning, "Vulkan: missing entry point {}", #Name); bOk = false; }
#define GENESIS_VK_LOAD_GLOBAL_OPTIONAL(Name) \
		Name = reinterpret_cast<PFN_##Name>(vkGetInstanceProcAddr(VK_NULL_HANDLE, #Name));
		GENESIS_VK_GLOBAL_FUNCTIONS(GENESIS_VK_LOAD_GLOBAL)
		GENESIS_VK_GLOBAL_OPTIONAL_FUNCTIONS(GENESIS_VK_LOAD_GLOBAL_OPTIONAL)
#undef GENESIS_VK_LOAD_GLOBAL
#undef GENESIS_VK_LOAD_GLOBAL_OPTIONAL

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
#define GENESIS_VK_RESET(Name) Name = nullptr;
		GENESIS_VK_GLOBAL_FUNCTIONS(GENESIS_VK_RESET)
		GENESIS_VK_GLOBAL_OPTIONAL_FUNCTIONS(GENESIS_VK_RESET)
		GENESIS_VK_INSTANCE_FUNCTIONS(GENESIS_VK_RESET)
		GENESIS_VK_INSTANCE_OPTIONAL_FUNCTIONS(GENESIS_VK_RESET)
		GENESIS_VK_DEVICE_FUNCTIONS(GENESIS_VK_RESET)
#undef GENESIS_VK_RESET
		vkGetInstanceProcAddr = nullptr;
		FreeLibrary(VulkanModule);
		VulkanModule = nullptr;
	}

	bool LoadInstanceFunctions(VkInstance Instance)
	{
		bool bOk = true;
#define GENESIS_VK_LOAD_INSTANCE(Name) \
		Name = reinterpret_cast<PFN_##Name>(vkGetInstanceProcAddr(Instance, #Name)); \
		if (!Name) { GE_LOG(LogRHI, Warning, "Vulkan: missing entry point {}", #Name); bOk = false; }
#define GENESIS_VK_LOAD_INSTANCE_OPTIONAL(Name) \
		Name = reinterpret_cast<PFN_##Name>(vkGetInstanceProcAddr(Instance, #Name));
		GENESIS_VK_INSTANCE_FUNCTIONS(GENESIS_VK_LOAD_INSTANCE)
		GENESIS_VK_INSTANCE_OPTIONAL_FUNCTIONS(GENESIS_VK_LOAD_INSTANCE_OPTIONAL)
#undef GENESIS_VK_LOAD_INSTANCE
#undef GENESIS_VK_LOAD_INSTANCE_OPTIONAL
		return bOk;
	}

	bool LoadDeviceFunctions(VkDevice Device)
	{
		bool bOk = true;
#define GENESIS_VK_LOAD_DEVICE(Name) \
		Name = reinterpret_cast<PFN_##Name>(vkGetDeviceProcAddr(Device, #Name)); \
		if (!Name) { GE_LOG(LogRHI, Warning, "Vulkan: missing entry point {}", #Name); bOk = false; }
		GENESIS_VK_DEVICE_FUNCTIONS(GENESIS_VK_LOAD_DEVICE)
#undef GENESIS_VK_LOAD_DEVICE
		return bOk;
	}

	const char* ResultToString(VkResult Result)
	{
		switch (Result)
		{
		case VK_SUCCESS: return "VK_SUCCESS";
		case VK_NOT_READY: return "VK_NOT_READY";
		case VK_TIMEOUT: return "VK_TIMEOUT";
		case VK_INCOMPLETE: return "VK_INCOMPLETE";
		case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
		case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
		case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
		case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
		case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
		case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
		case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
		case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
		case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
		case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
		case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
		case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
		case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
		case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
		case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
		case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
		case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
		default: break;
		}
		return "VK_ERROR_(other)";
	}
}
