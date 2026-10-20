// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "RHI.h"

#include "RHIPrivate.h"

DEFINE_LOG_CATEGORY(LogRHI, Log);

const char* ToString(ERHIBackend Backend)
{
	switch (Backend)
	{
	case ERHIBackend::D3D12: return "D3D12";
	case ERHIBackend::Vulkan: return "Vulkan";
	}
	return "?";
}

uint32 GetBytesPerPixel(EPixelFormat Format)
{
	switch (Format)
	{
	case EPixelFormat::R8_UNORM: return 1;
	case EPixelFormat::R8G8B8A8_UNORM:
	case EPixelFormat::B8G8R8A8_UNORM:
	case EPixelFormat::D32_FLOAT: return 4;
	case EPixelFormat::Unknown: break;
	}
	return 0;
}

TUniquePtr<FRHIDevice> FRHIDevice::Create(const FRHIDeviceDesc& Desc)
{
	TUniquePtr<FRHIDevice> Device;
	switch (Desc.Backend)
	{
	case ERHIBackend::D3D12: Device = CreateD3D12Device(Desc); break;
	case ERHIBackend::Vulkan: Device = CreateVulkanDevice(Desc); break;
	}
	if (Device)
	{
		UE_LOG(LogRHI, Display, "{} device on \"{}\"", ToString(Desc.Backend), Device->GetAdapterName());
	}
	return Device;
}

ERHIBackend ParseBackendFromCommandLine(std::string_view CommandLine, ERHIBackend Default)
{
	const auto HasFlag = [CommandLine](std::string_view Flag)
	{
		for (SIZE_T Pos = CommandLine.find(Flag); Pos != std::string_view::npos; Pos = CommandLine.find(Flag, Pos + 1))
		{
			const bool bStart = Pos == 0 || CommandLine[Pos - 1] == ' ' || CommandLine[Pos - 1] == '"';
			const SIZE_T End = Pos + Flag.size();
			const bool bEnd = End == CommandLine.size() || CommandLine[End] == ' ' || CommandLine[End] == '"';
			if (bStart && bEnd)
			{
				return true;
			}
		}
		return false;
	};
	if (HasFlag("-vulkan") || HasFlag("--vulkan") || HasFlag("-vk"))
	{
		return ERHIBackend::Vulkan;
	}
	if (HasFlag("-d3d12") || HasFlag("--d3d12") || HasFlag("-dx12"))
	{
		return ERHIBackend::D3D12;
	}
	return Default;
}
