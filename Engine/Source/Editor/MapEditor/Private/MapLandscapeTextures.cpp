// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "MapLandscapeTextures.h"
#include "MapAssetLibrary.h"

#include "HAL/File.h"
#include "MapEditorPrivate.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <thread>
#include <vector>

#if PLATFORM_WINDOWS
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace
{
	constexpr float Pi = 3.14159265f;
	constexpr int32 BuiltInSize = 256;
	std::string NativeTextureRevision(const Landscape& Land)
	{
		for(const auto& Layer:Land.GetLayers()) if(Layer.texture.rfind("asset:",0)==0) return FMapAssetLibrary::RevisionKey();
		return {};
	}

	// 0 to 1, fixed per lattice point and seed.
	float Hash01(int32 X, int32 Y, uint32 Seed)
	{
		uint32 H = static_cast<uint32>(X) * 374761393u + static_cast<uint32>(Y) * 668265263u + Seed * 2246822519u;
		H = (H ^ (H >> 13)) * 1274126177u;
		H ^= H >> 16;
		return static_cast<float>(H & 0xFFFFu) / 65535.0f;
	}

	// Smooth noise over a lattice that repeats every Period cells, so the
	// patterns tile.
	float ValueNoise(float X, float Y, int32 Period, uint32 Seed)
	{
		const int32 X0 = static_cast<int32>(std::floor(X));
		const int32 Y0 = static_cast<int32>(std::floor(Y));
		const float FX = X - static_cast<float>(X0);
		const float FY = Y - static_cast<float>(Y0);
		const auto Wrap = [Period](int32 V) { return ((V % Period) + Period) % Period; };
		const auto At = [&](int32 DX, int32 DY) { return Hash01(Wrap(X0 + DX), Wrap(Y0 + DY), Seed); };
		const float SX = FX * FX * (3.0f - 2.0f * FX);
		const float SY = FY * FY * (3.0f - 2.0f * FY);
		const float Top = At(0, 0) + (At(1, 0) - At(0, 0)) * SX;
		const float Bottom = At(0, 1) + (At(1, 1) - At(0, 1)) * SX;
		return Top + (Bottom - Top) * SY;
	}

	// Octaves of noise from Frequency cells across the tile; 0 to 1.
	float Fbm(float U, float V, int32 Frequency, int32 Octaves, uint32 Seed)
	{
		float Sum = 0.0f;
		float Amplitude = 0.5f;
		float Total = 0.0f;
		for (int32 Octave = 0; Octave < Octaves; ++Octave)
		{
			const int32 Cells = Frequency << Octave;
			Sum += ValueNoise(U * static_cast<float>(Cells), V * static_cast<float>(Cells), Cells, Seed + static_cast<uint32>(Octave) * 101u) * Amplitude;
			Total += Amplitude;
			Amplitude *= 0.5f;
		}
		return Sum / Total;
	}

	// The built-in patterns: brightness around 1 at each point of the tile.
	float Pattern(const FString& Name, float U, float V, int32 X, int32 Y)
	{
		if (Name == "Grass")
		{
			// Patches, and blades running one way.
			const float Patches = Fbm(U, V, 4, 4, 11) - 0.5f;
			const float Blades = ValueNoise(U * 128.0f, V * 32.0f, 128, 12) - 0.5f;
			const float Fine = Hash01(X, Y, 13) - 0.5f;
			return 1.0f + 0.34f * Patches + 0.3f * Blades + 0.12f * Fine;
		}
		if (Name == "Rock")
		{
			// Broad slabs with dark cracks between them.
			const float Slabs = Fbm(U, V, 4, 5, 21) - 0.5f;
			const float Ridge = 1.0f - std::fabs(2.0f * Fbm(U, V, 6, 4, 22) - 1.0f);
			const float Grain = Hash01(X, Y, 23) - 0.5f;
			return 1.0f + 0.5f * Slabs - 0.45f * std::pow(Ridge, 10.0f) + 0.1f * Grain;
		}
		if (Name == "Dirt")
		{
			// Clods, and pebbles here and there.
			const float Clods = Fbm(U, V, 8, 5, 31) - 0.5f;
			const float Pebble = ValueNoise(U * 48.0f, V * 48.0f, 48, 32);
			return (1.0f + 0.42f * Clods + 0.1f * (Hash01(X, Y, 33) - 0.5f)) * (Pebble > 0.86f ? 1.22f : 1.0f);
		}
		if (Name == "Sand")
		{
			// Fine grain over wind ripples.
			const float Warp = Fbm(U, V, 4, 3, 41);
			const float Ripples = std::sin(2.0f * Pi * (V * 12.0f + Warp * 1.5f));
			return 1.0f + 0.07f * Ripples + 0.1f * (Fbm(U, V, 32, 2, 42) - 0.5f) + 0.1f * (Hash01(X, Y, 43) - 0.5f);
		}
		// Snow: soft drifts and a little sparkle.
		const float Drifts = Fbm(U, V, 4, 4, 51) - 0.5f;
		return 1.0f + 0.12f * Drifts + (Hash01(X, Y, 52) > 0.996f ? 0.08f : 0.0f);
	}

	FLandscapeImage MakeBuiltIn(const FString& Name)
	{
		FLandscapeImage Image;
		Image.Width = BuiltInSize;
		Image.Height = BuiltInSize;
		Image.Pixels.resize(static_cast<SIZE_T>(BuiltInSize) * BuiltInSize * 4);
		for (int32 Y = 0; Y < BuiltInSize; ++Y)
		{
			for (int32 X = 0; X < BuiltInSize; ++X)
			{
				const float U = static_cast<float>(X) / BuiltInSize;
				const float V = static_cast<float>(Y) / BuiltInSize;
				const uint8 Value = static_cast<uint8>(FMath::Clamp(Pattern(Name, U, V, X, Y) * 128.0f + 0.5f, 0.0f, 255.0f));
				uint8* Texel = &Image.Pixels[(static_cast<SIZE_T>(Y) * BuiltInSize + X) * 4];
				Texel[0] = Texel[1] = Texel[2] = Value;
				Texel[3] = 255;
			}
		}
		return Image;
	}

	// Each mip half the one before (the 2 x 2 texels' average), to 1 x 1.
	void MakeMips(FLandscapeTexture& Texture)
	{
		while (Texture.Mips.back().Width > 1 || Texture.Mips.back().Height > 1)
		{
			const FLandscapeImage& From = Texture.Mips.back();
			FLandscapeImage To;
			To.Width = FMath::Max(From.Width / 2, 1);
			To.Height = FMath::Max(From.Height / 2, 1);
			To.Pixels.resize(static_cast<SIZE_T>(To.Width) * To.Height * 4);
			for (int32 Y = 0; Y < To.Height; ++Y)
			{
				for (int32 X = 0; X < To.Width; ++X)
				{
					for (int32 C = 0; C < 4; ++C)
					{
						int32 Sum = 0;
						for (int32 DY = 0; DY < 2; ++DY)
						{
							for (int32 DX = 0; DX < 2; ++DX)
							{
								const int32 SX = FMath::Min(X * 2 + DX, From.Width - 1);
								const int32 SY = FMath::Min(Y * 2 + DY, From.Height - 1);
								Sum += From.Pixels[(static_cast<SIZE_T>(SY) * From.Width + SX) * 4 + C];
							}
						}
						To.Pixels[(static_cast<SIZE_T>(Y) * To.Width + X) * 4 + C] = static_cast<uint8>((Sum + 2) / 4);
					}
				}
			}
			Texture.Mips.push_back(std::move(To));
		}
	}

	bool SameLook(const Landscape& A, const Landscape& B)
	{
		if (A.GetQuadsX() != B.GetQuadsX() || A.GetQuadsY() != B.GetQuadsY() || A.GetQuadSize() != B.GetQuadSize() || A.GetNumLayers() != B.GetNumLayers())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.GetNumLayers(); ++Index)
		{
			const LandscapeLayer& LA = A.GetLayers()[static_cast<SIZE_T>(Index)];
			const LandscapeLayer& LB = B.GetLayers()[static_cast<SIZE_T>(Index)];
			if (LA.r != LB.r || LA.g != LB.g || LA.b != LB.b || LA.texture != LB.texture || LA.tiling != LB.tiling)
			{
				return false;
			}
		}
		return true;
	}
}

void FLandscapeTexture::Sample(float U, float V, float Footprint, float Out[3]) const
{
	// The mip whose texels are about the footprint, so that far ground
	// averages its pattern instead of picking texels from it.
	const float SourceTexels = Footprint * static_cast<float>(Mips[0].Width);
	const int32 Level = FMath::Clamp(static_cast<int32>(std::floor(std::log2(FMath::Max(SourceTexels, 1.0f)))), 0, static_cast<int32>(Mips.size()) - 1);
	const FLandscapeImage& Mip = Mips[static_cast<SIZE_T>(Level)];
	const float X = (U - std::floor(U)) * static_cast<float>(Mip.Width) - 0.5f;
	const float Y = (V - std::floor(V)) * static_cast<float>(Mip.Height) - 0.5f;
	const int32 X0 = static_cast<int32>(std::floor(X));
	const int32 Y0 = static_cast<int32>(std::floor(Y));
	const float FX = X - static_cast<float>(X0);
	const float FY = Y - static_cast<float>(Y0);
	const auto Texel = [&Mip](int32 TX, int32 TY)
	{
		TX = ((TX % Mip.Width) + Mip.Width) % Mip.Width;
		TY = ((TY % Mip.Height) + Mip.Height) % Mip.Height;
		return &Mip.Pixels[(static_cast<SIZE_T>(TY) * Mip.Width + TX) * 4];
	};
	const uint8* A = Texel(X0, Y0);
	const uint8* B = Texel(X0 + 1, Y0);
	const uint8* C = Texel(X0, Y0 + 1);
	const uint8* D = Texel(X0 + 1, Y0 + 1);
	for (int32 Channel = 0; Channel < 3; ++Channel)
	{
		const float Top = A[Channel] + (B[Channel] - A[Channel]) * FX;
		const float Bottom = C[Channel] + (D[Channel] - C[Channel]) * FX;
		Out[Channel] = Top + (Bottom - Top) * FY;
	}
}

FLandscapeTextureLibrary& FLandscapeTextureLibrary::Get()
{
	static FLandscapeTextureLibrary Library;
	return Library;
}

const TArray<FString>& FLandscapeTextureLibrary::GetBuiltInNames()
{
	static const TArray<FString> Names = { "Grass", "Rock", "Dirt", "Sand", "Snow" };
	return Names;
}

FString FLandscapeTextureLibrary::DisplayName(const std::string& Texture)
{
	if (Texture.empty())
	{
		return "None";
	}
	if (Texture.rfind("builtin:", 0) == 0)
	{
		return FString(Texture.substr(8));
	}
	if(Texture.rfind("asset:",0)==0) { FAssetReference Ref; FString Error; return FMapAssetLibrary::ParseTextureToken(FString(Texture),Ref,Error)?Ref.Path:FString("Invalid texture asset"); }
	return FString::FromWide(std::filesystem::path(FString(Texture).ToWide()).filename().wstring());
}

bool FLandscapeTextureLibrary::IsTextured(const Landscape& Land)
{
	for (const LandscapeLayer& Layer : Land.GetLayers())
	{
		if (!Layer.texture.empty())
		{
			return true;
		}
	}
	return false;
}

const FLandscapeTexture* FLandscapeTextureLibrary::Find(const std::string& Texture)
{
	if(Texture.rfind("asset:",0)==0) {
		const auto Revision=FMapAssetLibrary::RevisionKey();
		if(Revision!=NativeRevision) {
			std::erase_if(Textures,[](const auto& Entry) { return Entry.first.rfind("asset:",0)==0; });
			std::erase_if(Failed,[](const auto& Entry) { return Entry.first.rfind("asset:",0)==0; }); NativeRevision=Revision;
		}
	}
	if (Texture.empty() || Failed.count(Texture))
	{
		return nullptr;
	}
	if (const auto Found = Textures.find(Texture); Found != Textures.end())
	{
		return Found->second.get();
	}
	auto Made = std::make_unique<FLandscapeTexture>();
	if (Texture.rfind("builtin:", 0) == 0)
	{
		const FString Name(Texture.substr(8));
		if (std::find(GetBuiltInNames().begin(), GetBuiltInNames().end(), Name) == GetBuiltInNames().end())
		{
			UE_LOG(LogMapEditor, Warning, "No built-in landscape texture named {}", Name);
			Failed[Texture] = true;
			return nullptr;
		}
		Made->Mips.push_back(MakeBuiltIn(Name));
		Made->bTint = true;
	}
	else
	{
		FLandscapeImage Image;
		FString Error;
		bool Loaded=false;
		if(Texture.rfind("asset:",0)==0) {
			FTextureAsset Asset; Loaded=FMapAssetLibrary::LoadTexture(FString(Texture),Asset,Error);
			if(Loaded) { Image.Width=Asset.Width; Image.Height=Asset.Height; Image.Pixels=std::move(Asset.Pixels); }
		} else Loaded=LoadImageFile(FString(Texture),Image,Error);
		if (!Loaded)
		{
			UE_LOG(LogMapEditor, Warning, "Landscape texture: {}", Error);
			Failed[Texture] = true;
			return nullptr;
		}
		Made->Mips.push_back(std::move(Image));
	}
	MakeMips(*Made);
	return (Textures[Texture] = std::move(Made)).get();
}

bool FLandscapeTextureLibrary::LoadImageFile(const FString& Path, FLandscapeImage& Out, FString& Error)
{
	Out = FLandscapeImage();
	if (!FFile::Exists(Path))
	{
		Error = FString::Format("{} doesn't exist", Path);
		return false;
	}
#if PLATFORM_WINDOWS
	using Microsoft::WRL::ComPtr;
	const bool bCom = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
	bool bOk = false;
	{
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
		}
		else if (Width < 1 || Height < 1 || Width > 8192 || Height > 8192)
		{
			Error = FString::Format("A landscape texture must be 1 to 8192 pixels each way ({} is {} x {})", Path, Width, Height);
		}
		else if (FAILED(Factory->CreateFormatConverter(&Converter))
			|| FAILED(Converter->Initialize(Frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
		{
			Error = FString::Format("Can't read {} as RGBA", Path);
		}
		else
		{
			Out.Width = static_cast<int32>(Width);
			Out.Height = static_cast<int32>(Height);
			Out.Pixels.resize(static_cast<SIZE_T>(Width) * Height * 4);
			bOk = SUCCEEDED(Converter->CopyPixels(nullptr, Width * 4, static_cast<UINT>(Out.Pixels.size()), Out.Pixels.data()));
			if (!bOk)
			{
				Error = FString::Format("Can't read the pixels of {}", Path);
				Out = FLandscapeImage();
			}
		}
	}
	if (bCom)
	{
		CoUninitialize();
	}
	return bOk;
#else
	Error = FString::Format("Images can't be read here ({})", Path);
	return false;
#endif
}

// ---------------------------------------------------------------------------

int32 FLandscapeBake::GetTexelsPerQuad(const Landscape& Land)
{
	return FMath::Clamp(MaxSize / FMath::Max(Land.GetQuadsX(), Land.GetQuadsY()), 1, 16);
}

FIntRect FLandscapeBake::Update(const std::shared_ptr<const Landscape>& Land)
{
	if (!Land)
	{
		return FIntRect();
	}
	const auto Revision=NativeTextureRevision(*Land);
	if(Revision!=NativeRevision) { Baked.reset(); NativeRevision=Revision; }
	if (Land == Baked)
	{
		return FIntRect();
	}
	const int32 PerQuad = GetTexelsPerQuad(*Land);
	const FIntRect All(0, 0, Land->GetQuadsX() * PerQuad, Land->GetQuadsY() * PerQuad);
	if (!Baked || !SameLook(*Baked, *Land))
	{
		Image.Width = All.Width();
		Image.Height = All.Height();
		Image.Pixels.assign(static_cast<SIZE_T>(Image.Width) * Image.Height * 4, 255);
		Bake(*Land, All);
		Baked = Land;
		return All;
	}

	// Where the weights changed (a stroke, an import): those grid points'
	// quads, and their neighbours', which blend toward them.
	const int32 PointsX = Land->GetPointsX();
	int32 MinX = INT32_MAX, MinY = INT32_MAX, MaxX = -1, MaxY = -1;
	for (int32 Layer = 0; Layer < Land->GetNumLayers(); ++Layer)
	{
		const std::vector<float>& New = Land->GetLayers()[static_cast<SIZE_T>(Layer)].weights;
		const std::vector<float>& Old = Baked->GetLayers()[static_cast<SIZE_T>(Layer)].weights;
		for (SIZE_T Index = 0; Index < New.size(); ++Index)
		{
			if (New[Index] != Old[Index])
			{
				const int32 X = static_cast<int32>(Index % PointsX);
				const int32 Y = static_cast<int32>(Index / PointsX);
				MinX = FMath::Min(MinX, X);
				MinY = FMath::Min(MinY, Y);
				MaxX = FMath::Max(MaxX, X);
				MaxY = FMath::Max(MaxY, Y);
			}
		}
	}
	Baked = Land;
	if (MaxX < 0)
	{
		return FIntRect();
	}
	const FIntRect Dirty(FMath::Max(MinX - 1, 0) * PerQuad, FMath::Max(MinY - 1, 0) * PerQuad,
		FMath::Min(MaxX + 1, Land->GetQuadsX()) * PerQuad, FMath::Min(MaxY + 1, Land->GetQuadsY()) * PerQuad);
	Bake(*Land, Dirty);
	return Dirty;
}

FColor FLandscapeBake::GetTexel(int32 X, int32 Y) const
{
	const uint8* Texel = &Image.Pixels[(static_cast<SIZE_T>(Y) * Image.Width + X) * 4];
	return FColor(Texel[0], Texel[1], Texel[2], Texel[3]);
}

void FLandscapeBake::Bake(const Landscape& Land, const FIntRect& Texels)
{
	const int32 PerQuad = GetTexelsPerQuad(Land);
	const int32 QuadsX = Land.GetQuadsX();
	const int32 QuadsY = Land.GetQuadsY();
	const int32 PointsX = Land.GetPointsX();
	unsigned char BaseR, BaseG, BaseB;
	Landscape::GetBaseColor(BaseR, BaseG, BaseB);
	const float Base[3] = { static_cast<float>(BaseR), static_cast<float>(BaseG), static_cast<float>(BaseB) };

	// What each layer looks like, found before the work is shared out.
	struct FLook
	{
		const std::vector<float>* Weights;
		const FLandscapeTexture* Texture;
		float Color[3];
		float Repeat;    // repeats per landscape unit
		float Footprint; // repeats per baked texel
	};
	std::vector<FLook> Looks;
	for (const LandscapeLayer& Layer : Land.GetLayers())
	{
		FLook Look;
		Look.Weights = &Layer.weights;
		Look.Texture = FLandscapeTextureLibrary::Get().Find(Layer.texture);
		Look.Color[0] = Layer.r;
		Look.Color[1] = Layer.g;
		Look.Color[2] = Layer.b;
		Look.Repeat = 1.0f / FMath::Max(Layer.tiling, 1.0f);
		Look.Footprint = Land.GetQuadSize() / static_cast<float>(PerQuad) * Look.Repeat;
		Looks.push_back(Look);
	}

	const auto BakeRows = [&](int32 FirstRow, int32 EndRow)
	{
		float Sampled[3];
		for (int32 Y = FirstRow; Y < EndRow; ++Y)
		{
			const float GY = (static_cast<float>(Y) + 0.5f) / static_cast<float>(PerQuad);
			const int32 Y0 = FMath::Min(static_cast<int32>(GY), QuadsY - 1);
			const float FY = GY - static_cast<float>(Y0);
			for (int32 X = Texels.Left; X < Texels.Right; ++X)
			{
				const float GX = (static_cast<float>(X) + 0.5f) / static_cast<float>(PerQuad);
				const int32 X0 = FMath::Min(static_cast<int32>(GX), QuadsX - 1);
				const float FX = GX - static_cast<float>(X0);
				const SIZE_T I00 = static_cast<SIZE_T>(Y0) * PointsX + X0;
				const SIZE_T I01 = I00 + static_cast<SIZE_T>(PointsX);
				float Color[3] = { 0.0f, 0.0f, 0.0f };
				float Total = 0.0f;
				for (const FLook& Look : Looks)
				{
					const std::vector<float>& W = *Look.Weights;
					const float Weight = (W[I00] * (1.0f - FX) + W[I00 + 1] * FX) * (1.0f - FY) + (W[I01] * (1.0f - FX) + W[I01 + 1] * FX) * FY;
					if (Weight <= 0.0f)
					{
						continue;
					}
					const float* Own = Look.Color;
					if (Look.Texture)
					{
						Look.Texture->Sample(GX * Land.GetQuadSize() * Look.Repeat, GY * Land.GetQuadSize() * Look.Repeat, Look.Footprint, Sampled);
						if (Look.Texture->bTint)
						{
							for (int32 C = 0; C < 3; ++C)
							{
								Sampled[C] = Look.Color[C] * Sampled[C] / 128.0f;
							}
						}
						Own = Sampled;
					}
					for (int32 C = 0; C < 3; ++C)
					{
						Color[C] += Own[C] * Weight;
					}
					Total += Weight;
				}
				// What the layers leave shows the ground's own color.
				const float Rest = FMath::Max(1.0f - Total, 0.0f);
				const float Scale = Total > 1.0f ? 1.0f / Total : 1.0f;
				uint8* Texel = &Image.Pixels[(static_cast<SIZE_T>(Y) * Image.Width + X) * 4];
				for (int32 C = 0; C < 3; ++C)
				{
					Texel[C] = static_cast<uint8>(FMath::Clamp(Color[C] * Scale + Base[C] * Rest + 0.5f, 0.0f, 255.0f));
				}
				Texel[3] = 255;
			}
		}
	};

	// A whole landscape is millions of texels: shared among the cores.
	const int32 Rows = Texels.Height();
	const int64 Work = static_cast<int64>(Rows) * Texels.Width() * FMath::Max(static_cast<int32>(Looks.size()), 1);
	const int32 Threads = Work < 262144 ? 1 : FMath::Clamp(static_cast<int32>(std::thread::hardware_concurrency()), 1, 16);
	if (Threads == 1)
	{
		BakeRows(Texels.Top, Texels.Bottom);
		return;
	}
	std::vector<std::thread> Workers;
	const int32 Chunk = (Rows + Threads - 1) / Threads;
	for (int32 First = Texels.Top; First < Texels.Bottom; First += Chunk)
	{
		Workers.emplace_back(BakeRows, First, FMath::Min(First + Chunk, Texels.Bottom));
	}
	for (std::thread& Worker : Workers)
	{
		Worker.join();
	}
}

// ---------------------------------------------------------------------------

bool FLandscapeLayerImages::CanBlend(const Landscape& Land)
{
	return Land.GetNumLayers() <= MaxLayers && FLandscapeTextureLibrary::IsTextured(Land);
}

FIntRect FLandscapeLayerImages::UpdateWeights(const std::shared_ptr<const Landscape>& Land)
{
	if (!Land || Land == WeightsOf)
	{
		return FIntRect();
	}
	const int32 NewX = Land->GetPointsX();
	const int32 NewY = Land->GetPointsY();
	const int32 NewLayers = FMath::Min(Land->GetNumLayers(), MaxLayers);
	FIntRect Dirty(0, 0, NewX, NewY);
	if (WeightsOf && NewX == PointsX && NewY == PointsY && NewLayers == Layers)
	{
		// Only the points whose weights changed.
		int32 MinX = INT32_MAX, MinY = INT32_MAX, MaxX = -1, MaxY = -1;
		for (int32 Layer = 0; Layer < NewLayers; ++Layer)
		{
			const std::vector<float>& New = Land->GetLayers()[static_cast<SIZE_T>(Layer)].weights;
			const std::vector<float>& Old = WeightsOf->GetLayers()[static_cast<SIZE_T>(Layer)].weights;
			for (SIZE_T Index = 0; Index < New.size(); ++Index)
			{
				if (New[Index] != Old[Index])
				{
					const int32 X = static_cast<int32>(Index % NewX);
					const int32 Y = static_cast<int32>(Index / NewX);
					MinX = FMath::Min(MinX, X);
					MinY = FMath::Min(MinY, Y);
					MaxX = FMath::Max(MaxX, X);
					MaxY = FMath::Max(MaxY, Y);
				}
			}
		}
		WeightsOf = Land;
		if (MaxX < 0)
		{
			return FIntRect();
		}
		Dirty = FIntRect(MinX, MinY, MaxX + 1, MaxY + 1);
	}
	else
	{
		PointsX = NewX;
		PointsY = NewY;
		Layers = NewLayers;
		Weights.assign(static_cast<SIZE_T>(PointsX) * PointsY * 4 * 2, 0);
		WeightsOf = Land;
	}
	const SIZE_T SliceBytes = static_cast<SIZE_T>(PointsX) * PointsY * 4;
	for (int32 Layer = 0; Layer < Layers; ++Layer)
	{
		const std::vector<float>& W = Land->GetLayers()[static_cast<SIZE_T>(Layer)].weights;
		uint8* Slice = Weights.data() + SliceBytes * static_cast<SIZE_T>(Layer / 4);
		for (int32 Y = Dirty.Top; Y < Dirty.Bottom; ++Y)
		{
			for (int32 X = Dirty.Left; X < Dirty.Right; ++X)
			{
				const SIZE_T Point = static_cast<SIZE_T>(Y) * PointsX + X;
				Slice[Point * 4 + static_cast<SIZE_T>(Layer % 4)] = static_cast<uint8>(FMath::Clamp(W[Point], 0.0f, 1.0f) * 255.0f + 0.5f);
			}
		}
	}
	return Dirty;
}

bool FLandscapeLayerImages::UpdateTextures(const Landscape& Land)
{
	FLandscapeTextureLibrary& Library = FLandscapeTextureLibrary::Get();
	const int32 Count = FMath::Max(FMath::Min(Land.GetNumLayers(), MaxLayers), 1);
	std::string NewLook=NativeTextureRevision(Land);
	TArray<const FLandscapeTexture*> Sources;
	int32 Size = 16;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const LandscapeLayer* Layer = Index < Land.GetNumLayers() ? &Land.GetLayers()[static_cast<SIZE_T>(Index)] : nullptr;
		const FLandscapeTexture* Source = Layer ? Library.Find(Layer->texture) : nullptr;
		Sources.push_back(Source);
		if (Layer)
		{
			NewLook += Layer->texture + "|" + std::to_string(Layer->r) + "," + std::to_string(Layer->g) + "," + std::to_string(Layer->b)
				+ (Source ? "+" : "-") + ";";
		}
		if (Source)
		{
			Size = FMath::Max(Size, FMath::Max(Source->Mips[0].Width, Source->Mips[0].Height));
		}
	}
	if (NewLook == Look && !Textures.empty())
	{
		return false;
	}
	Look = NewLook;
	// One size for all: the biggest texture's, a power of two.
	TextureSize = 16;
	while (TextureSize < FMath::Min(Size, MaxTextureSize))
	{
		TextureSize *= 2;
	}
	Slices = Count;
	Textures.clear();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const LandscapeLayer* Layer = Index < Land.GetNumLayers() ? &Land.GetLayers()[static_cast<SIZE_T>(Index)] : nullptr;
		const FLandscapeTexture* Source = Sources[static_cast<SIZE_T>(Index)];
		const float Color[3] = { Layer ? static_cast<float>(Layer->r) : 255.0f, Layer ? static_cast<float>(Layer->g) : 255.0f,
			Layer ? static_cast<float>(Layer->b) : 255.0f };
		FLandscapeTexture Slice;
		FLandscapeImage Top;
		Top.Width = Top.Height = TextureSize;
		Top.Pixels.resize(static_cast<SIZE_T>(TextureSize) * TextureSize * 4);
		float Sampled[3];
		for (int32 Y = 0; Y < TextureSize; ++Y)
		{
			for (int32 X = 0; X < TextureSize; ++X)
			{
				uint8* Texel = &Top.Pixels[(static_cast<SIZE_T>(Y) * TextureSize + X) * 4];
				const float* Own = Color;
				if (Source)
				{
					// The texture over one repeat, from the level nearest this size.
					Source->Sample((X + 0.5f) / TextureSize, (Y + 0.5f) / TextureSize, 1.0f / TextureSize, Sampled);
					if (Source->bTint)
					{
						for (int32 C = 0; C < 3; ++C)
						{
							Sampled[C] = Color[C] * Sampled[C] / 128.0f;
						}
					}
					Own = Sampled;
				}
				for (int32 C = 0; C < 3; ++C)
				{
					Texel[C] = static_cast<uint8>(FMath::Clamp(Own[C] + 0.5f, 0.0f, 255.0f));
				}
				Texel[3] = 255;
			}
		}
		Slice.Mips.push_back(std::move(Top));
		MakeMips(Slice);
		MipLevels = static_cast<int32>(Slice.Mips.size());
		for (const FLandscapeImage& Mip : Slice.Mips)
		{
			Textures.insert(Textures.end(), Mip.Pixels.begin(), Mip.Pixels.end());
		}
	}
	return true;
}

void FLandscapeLayerImages::GetConstants(const Landscape& Land, float Out[12])
{
	Out[0] = Land.GetSizeX();
	Out[1] = Land.GetSizeY();
	Out[2] = static_cast<float>(FMath::Min(Land.GetNumLayers(), MaxLayers));
	Out[3] = 0.0f;
	for (int32 Index = 0; Index < MaxLayers; ++Index)
	{
		Out[4 + Index] = Index < Land.GetNumLayers() ? 1.0f / FMath::Max(Land.GetLayers()[static_cast<SIZE_T>(Index)].tiling, 1.0f) : 0.0f;
	}
}
