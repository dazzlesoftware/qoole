// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "MapHeightmap.h"

#include "HAL/File.h"
#include "MapEditorPrivate.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if PLATFORM_WINDOWS
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace
{
	bool IsRaw(const FString& Path)
	{
		return Path.EndsWith(".r16", ESearchCase::IgnoreCase) || Path.EndsWith(".r8", ESearchCase::IgnoreCase)
			|| Path.EndsWith(".raw", ESearchCase::IgnoreCase);
	}

	// The side of a square of Count samples, or 0.
	int32 SquareSide(SIZE_T Count)
	{
		const int32 Side = static_cast<int32>(std::lround(std::sqrt(static_cast<double>(Count))));
		return Side >= 2 && static_cast<SIZE_T>(Side) * Side == Count ? Side : 0;
	}

	uint8 To8(uint16 Sample)
	{
		return static_cast<uint8>((Sample + 128) / 257);
	}

	bool LoadRaw(const FString& Path, FHeightmap& Out, FString& Error)
	{
		TArray<uint8> Bytes;
		if (!FFile::ReadAllBytes(Path, Bytes))
		{
			Error = FString::Format("Can't read {}", Path);
			return false;
		}
		// No header: the size is the square the samples make. 8 bits (as
		// Unreal's weightmaps) when the bytes make a square, 16 when their
		// pairs do (never both, as 2 n^2 is no square).
		const bool b16 = Path.EndsWith(".r16", ESearchCase::IgnoreCase)
			|| (!Path.EndsWith(".r8", ESearchCase::IgnoreCase) && Bytes.size() % 2 == 0 && SquareSide(Bytes.size() / 2) != 0);
		const SIZE_T Count = b16 ? Bytes.size() / 2 : Bytes.size();
		const int32 Side = (b16 && Bytes.size() % 2 != 0) ? 0 : SquareSide(Count);
		if (Side == 0)
		{
			Error = FString::Format("A RAW file must be a square of {} samples ({})", b16 ? "16-bit" : "8- or 16-bit", Path);
			return false;
		}
		Out.Width = Side;
		Out.Height = Side;
		Out.Samples.resize(Count);
		for (SIZE_T Index = 0; Index < Count; ++Index)
		{
			Out.Samples[Index] = b16 ? static_cast<uint16>(Bytes[Index * 2] | (Bytes[Index * 2 + 1] << 8)) : static_cast<uint16>(Bytes[Index] * 257);
		}
		return true;
	}

	bool SaveRaw(const FString& Path, const FHeightmap& Map, int32 Bits, FString& Error)
	{
		const SIZE_T SampleBytes = Bits == 8 ? 1 : 2;
		TArray<uint8> Bytes(Map.Samples.size() * SampleBytes);
		for (SIZE_T Index = 0; Index < Map.Samples.size(); ++Index)
		{
			if (Bits == 8)
			{
				Bytes[Index] = To8(Map.Samples[Index]);
				continue;
			}
			Bytes[Index * 2] = static_cast<uint8>(Map.Samples[Index] & 0xFF);
			Bytes[Index * 2 + 1] = static_cast<uint8>(Map.Samples[Index] >> 8);
		}
		FFile File;
		if (!File.Open(Path, EFileOpenMode::Write) || File.Write(Bytes.data(), static_cast<int64>(Bytes.size())) != static_cast<int64>(Bytes.size()))
		{
			Error = FString::Format("Can't write {}", Path);
			return false;
		}
		return true;
	}

#if PLATFORM_WINDOWS
	using Microsoft::WRL::ComPtr;

	// COM for the calling thread, if it has none yet (tests, worker threads).
	struct FComScope
	{
		bool bOwned = false;
		FComScope() { bOwned = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)); }
		~FComScope() { if (bOwned) CoUninitialize(); }
	};

	bool DecodeImage(const FString& Path, FHeightmap& Out, FString& Error)
	{
		const FComScope Com;
		ComPtr<IWICImagingFactory> Factory;
		ComPtr<IWICBitmapDecoder> Decoder;
		ComPtr<IWICBitmapFrameDecode> Frame;
		ComPtr<IWICFormatConverter> Converter;
		UINT Width = 0;
		UINT Height = 0;
		if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Factory)))
			|| FAILED(Factory->CreateDecoderFromFilename(Path.ToWide().c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &Decoder))
			|| FAILED(Decoder->GetFrame(0, &Frame))
			|| FAILED(Frame->GetSize(&Width, &Height)))
		{
			Error = FString::Format("Can't read {} as an image", Path);
			return false;
		}
		if (Width < 2 || Height < 2 || Width > 16384 || Height > 16384)
		{
			Error = FString::Format("A heightmap or weightmap must be 2 to 16384 pixels each way ({} is {} x {})", Path, Width, Height);
			return false;
		}
		// Whatever the file holds, as 16-bit gray (8 bits scale up evenly).
		if (FAILED(Factory->CreateFormatConverter(&Converter))
			|| FAILED(Converter->Initialize(Frame.Get(), GUID_WICPixelFormat16bppGray, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
		{
			Error = FString::Format("Can't read {} as grayscale", Path);
			return false;
		}
		Out.Width = static_cast<int32>(Width);
		Out.Height = static_cast<int32>(Height);
		Out.Samples.resize(static_cast<SIZE_T>(Width) * Height);
		if (FAILED(Converter->CopyPixels(nullptr, Width * 2, static_cast<UINT>(Out.Samples.size() * 2), reinterpret_cast<BYTE*>(Out.Samples.data()))))
		{
			Error = FString::Format("Can't read the pixels of {}", Path);
			return false;
		}
		return true;
	}

	bool EncodePng(const FString& Path, const FHeightmap& Map, int32 Bits, FString& Error)
	{
		const FComScope Com;
		ComPtr<IWICImagingFactory> Factory;
		ComPtr<IWICStream> Stream;
		ComPtr<IWICBitmapEncoder> Encoder;
		ComPtr<IWICBitmapFrameEncode> Frame;
		const WICPixelFormatGUID Wanted = Bits == 8 ? GUID_WICPixelFormat8bppGray : GUID_WICPixelFormat16bppGray;
		WICPixelFormatGUID Format = Wanted;
		// Rows padded to 4 bytes, as WIC likes them.
		const UINT RowBytes = static_cast<UINT>(Map.Width) * (Bits == 8 ? 1 : 2);
		const UINT Stride = (RowBytes + 3) & ~3u;
		TArray<uint8> Pixels(static_cast<SIZE_T>(Stride) * Map.Height, 0);
		for (int32 Y = 0; Y < Map.Height; ++Y)
		{
			uint8* Row = &Pixels[static_cast<SIZE_T>(Y) * Stride];
			if (Bits != 8)
			{
				std::memcpy(Row, &Map.Samples[static_cast<SIZE_T>(Y) * Map.Width], RowBytes);
				continue;
			}
			for (int32 X = 0; X < Map.Width; ++X)
			{
				Row[X] = To8(Map.At(X, Y));
			}
		}
		if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Factory)))
			|| FAILED(Factory->CreateStream(&Stream))
			|| FAILED(Stream->InitializeFromFilename(Path.ToWide().c_str(), GENERIC_WRITE))
			|| FAILED(Factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &Encoder))
			|| FAILED(Encoder->Initialize(Stream.Get(), WICBitmapEncoderNoCache))
			|| FAILED(Encoder->CreateNewFrame(&Frame, nullptr))
			|| FAILED(Frame->Initialize(nullptr))
			|| FAILED(Frame->SetSize(static_cast<UINT>(Map.Width), static_cast<UINT>(Map.Height)))
			|| FAILED(Frame->SetPixelFormat(&Format)) || Format != Wanted
			|| FAILED(Frame->WritePixels(static_cast<UINT>(Map.Height), Stride, static_cast<UINT>(Pixels.size()), Pixels.data()))
			|| FAILED(Frame->Commit()) || FAILED(Encoder->Commit()))
		{
			Error = FString::Format("Can't write {}", Path);
			return false;
		}
		return true;
	}
#else
	bool DecodeImage(const FString& Path, FHeightmap&, FString& Error)
	{
		Error = FString::Format("Only RAW (.r16, .r8) files can be read here ({})", Path);
		return false;
	}

	bool EncodePng(const FString& Path, const FHeightmap&, int32, FString& Error)
	{
		Error = FString::Format("Only RAW (.r16, .r8) files can be written here ({})", Path);
		return false;
	}
#endif
}

float FHeightmap::Sample(float U, float V) const
{
	const float X = FMath::Clamp(U, 0.0f, 1.0f) * static_cast<float>(Width - 1);
	const float Y = FMath::Clamp(V, 0.0f, 1.0f) * static_cast<float>(Height - 1);
	const int32 X0 = FMath::Min(static_cast<int32>(X), Width - 2);
	const int32 Y0 = FMath::Min(static_cast<int32>(Y), Height - 2);
	const float FX = X - static_cast<float>(X0);
	const float FY = Y - static_cast<float>(Y0);
	const float Top = At(X0, Y0) + (At(X0 + 1, Y0) - At(X0, Y0)) * FX;
	const float Bottom = At(X0, Y0 + 1) + (At(X0 + 1, Y0 + 1) - At(X0, Y0 + 1)) * FX;
	return Top + (Bottom - Top) * FY;
}

bool FHeightmap::Load(const FString& Path, FHeightmap& Out, FString& Error)
{
	Out = FHeightmap();
	if (!FFile::Exists(Path))
	{
		Error = FString::Format("{} doesn't exist", Path);
		return false;
	}
	const bool bOk = IsRaw(Path) ? LoadRaw(Path, Out, Error) : DecodeImage(Path, Out, Error);
	if (!bOk)
	{
		Out = FHeightmap();
	}
	return bOk;
}

bool FHeightmap::Save(const FString& Path, FString& Error, int32 Bits) const
{
	if (!IsValid())
	{
		Error = "Nothing to save";
		return false;
	}
	// The extension can say: .r16 is 16 bits, .r8 8.
	Bits = Path.EndsWith(".r16", ESearchCase::IgnoreCase) ? 16 : Path.EndsWith(".r8", ESearchCase::IgnoreCase) ? 8 : (Bits == 8 ? 8 : 16);
	if (IsRaw(Path))
	{
		return SaveRaw(Path, *this, Bits, Error);
	}
	if (!Path.EndsWith(".png", ESearchCase::IgnoreCase))
	{
		Error = "Heightmaps and weightmaps are saved as .png, .raw, .r16 or .r8";
		return false;
	}
	return EncodePng(Path, *this, Bits, Error);
}

TArray<FFileTypeFilter> FHeightmap::FileFilters()
{
	return { { "Grayscale maps (*.png, *.raw, *.r16, *.r8)", "*.png;*.raw;*.r16;*.r8" },
		{ "Images (*.png, *.bmp, *.jpg, *.tif)", "*.png;*.bmp;*.jpg;*.jpeg;*.tif;*.tiff" } };
}

std::vector<float> FHeightmap::ToHeights(int32 PointsX, int32 PointsY, float ZRange) const
{
	std::vector<float> Heights(static_cast<SIZE_T>(FMath::Max(PointsX, 0)) * FMath::Max(PointsY, 0), 0.0f);
	if (!IsValid() || PointsX < 2 || PointsY < 2)
	{
		return Heights;
	}
	const float Scale = ZRange / 65536.0f;
	for (int32 Y = 0; Y < PointsY; ++Y)
	{
		// Grid rows go from -Y; the image's top is +Y.
		const float V = 1.0f - static_cast<float>(Y) / static_cast<float>(PointsY - 1);
		for (int32 X = 0; X < PointsX; ++X)
		{
			const float U = static_cast<float>(X) / static_cast<float>(PointsX - 1);
			Heights[static_cast<SIZE_T>(Y) * PointsX + X] = (Sample(U, V) - 32768.0f) * Scale;
		}
	}
	return Heights;
}

FHeightmap FHeightmap::FromLandscape(const Landscape& Land, float ZRange)
{
	FHeightmap Map;
	Map.Width = Land.GetPointsX();
	Map.Height = Land.GetPointsY();
	Map.Samples.resize(static_cast<SIZE_T>(Map.Width) * Map.Height);
	const float Scale = 65536.0f / FMath::Max(ZRange, 1e-3f);
	for (int32 Row = 0; Row < Map.Height; ++Row)
	{
		const int32 Y = Map.Height - 1 - Row;
		for (int32 X = 0; X < Map.Width; ++X)
		{
			const float Value = std::round(Land.GetHeight(X, Y) * Scale + 32768.0f);
			Map.Samples[static_cast<SIZE_T>(Row) * Map.Width + X] = static_cast<uint16>(FMath::Clamp(Value, 0.0f, 65535.0f));
		}
	}
	return Map;
}

std::vector<float> FHeightmap::ToWeights(int32 PointsX, int32 PointsY) const
{
	// The same stretch as heights, mapped so that black is 0 and white 1.
	std::vector<float> Weights = ToHeights(PointsX, PointsY, 65536.0f);
	for (float& Weight : Weights)
	{
		Weight = IsValid() ? FMath::Clamp((Weight + 32768.0f) / 65535.0f, 0.0f, 1.0f) : 0.0f;
	}
	return Weights;
}

FHeightmap FHeightmap::FromWeights(const Landscape& Land, int32 Layer)
{
	FHeightmap Map;
	if (Layer < 0 || Layer >= Land.GetNumLayers())
	{
		return Map;
	}
	Map.Width = Land.GetPointsX();
	Map.Height = Land.GetPointsY();
	Map.Samples.resize(static_cast<SIZE_T>(Map.Width) * Map.Height);
	for (int32 Row = 0; Row < Map.Height; ++Row)
	{
		for (int32 X = 0; X < Map.Width; ++X)
		{
			const float Value = std::round(FMath::Clamp(Land.GetWeight(Layer, X, Map.Height - 1 - Row), 0.0f, 1.0f) * 65535.0f);
			Map.Samples[static_cast<SIZE_T>(Row) * Map.Width + X] = static_cast<uint16>(Value);
		}
	}
	return Map;
}
