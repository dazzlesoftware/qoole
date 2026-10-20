// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "RHI.h"

// Backend factories (D3D12/D3D12Device.cpp, Vulkan/VulkanDevice.cpp).
TUniquePtr<FRHIDevice> CreateD3D12Device(const FRHIDeviceDesc& Desc);
TUniquePtr<FRHIDevice> CreateVulkanDevice(const FRHIDeviceDesc& Desc);
