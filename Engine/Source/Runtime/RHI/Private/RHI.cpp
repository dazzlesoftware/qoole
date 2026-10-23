// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "RHI.h"
#include "Containers/ContainerAliases.h"

#include "RHIPrivate.h"

DEFINE_LOG_CATEGORY(LogRHI, Log);

const char* ToString(ERHIBackend Backend)
{
	switch (Backend)
	{
	case ERHIBackend::D3D12:
		return "D3D12";
	case ERHIBackend::Vulkan:
		return "Vulkan";
	}
	return "?";
}

uint32 GetBytesPerPixel(EPixelFormat Format)
{
	switch (Format)
	{
	case EPixelFormat::R8_UNORM:
		return 1;
	case EPixelFormat::R8G8B8A8_UNORM:
	case EPixelFormat::R8G8B8A8_SRGB:
	case EPixelFormat::B8G8R8A8_UNORM:
	case EPixelFormat::D32_FLOAT:
		return 4;
	case EPixelFormat::Unknown:
		break;
	}
	return 0;
}

uint32 GetFullMipLevels(uint32 Width, uint32 Height)
{
	uint32 Levels = 1;
	while ((Width >> Levels) > 0 || (Height >> Levels) > 0)
	{
		++Levels;
	}
	return Levels;
}

SIZE_T GetMipChainBytes(EPixelFormat Format, uint32 Width, uint32 Height, uint32 Levels)
{
	SIZE_T Bytes = 0;
	for (uint32 Level = 0; Level < Levels; ++Level)
	{
		Bytes += SIZE_T(GetMipSize(Width, Level)) * GetMipSize(Height, Level) * GetBytesPerPixel(Format);
	}
	return Bytes;
}

TUniquePtr<FRHIDevice> FRHIDevice::Create(const FRHIDeviceDesc& Desc)
{
	TUniquePtr<FRHIDevice> Device;
	switch (Desc.Backend)
	{
	case ERHIBackend::D3D12:
		Device = CreateD3D12Device(Desc);
		break;
	case ERHIBackend::Vulkan:
		Device = CreateVulkanDevice(Desc);
		break;
	}
	if (Device)
	{
		GENESIS_LOG(LogRHI, Display, "{} device on \"{}\"", ToString(Desc.Backend), Device->GetAdapterName());
	}
	return Device;
}

ERHIBackend ParseBackendFromCommandLine(std::string_view CommandLine, ERHIBackend Default)
{
	const TFunction<bool(std::string_view)> HasFlag = [CommandLine](std::string_view Flag)
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
