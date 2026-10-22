// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "TextureAssetEditor.h"
#include "MapAssetImport.h"
#include "MapLandscapeTextures.h"
#include "HAL/File.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
namespace
{
float Linear(uint8 V)
{
	const float C = V / 255.0f;
	return C <= 0.04045f ? C / 12.92f : std::pow((C + 0.055f) / 1.055f, 2.4f);
}
uint8 Display(float C)
{
	C = std::clamp(C, 0.0f, 1.0f);
	return static_cast<uint8>(std::clamp(
		(C <= 0.0031308f ? 12.92f * C : 1.055f * std::pow(C, 1 / 2.4f) - 0.055f) * 255.0f + 0.5f, 0.0f, 255.0f));
}
} // namespace
bool BuildTextureAssetPreview(const FTextureAsset& In, ETexturePreviewChannel Channel, FTextureAsset& Out,
							  FString& Error)
{
	if (In.Width < 1 || In.Height < 1 || In.Width > 8192 || In.Height > 8192 ||
		In.Pixels.size() != static_cast<size_t>(In.Width) * In.Height * 4 || static_cast<int>(Channel) < 0 ||
		static_cast<int>(Channel) > 5)
	{
		Error = "Invalid texture preview data.";
		return false;
	}
	FTextureAsset Preview;
	const float Scale = std::min(1.0f, 2048.0f / static_cast<float>(std::max(In.Width, In.Height)));
	Preview.Width = std::max(1, static_cast<int>(In.Width * Scale));
	Preview.Height = std::max(1, static_cast<int>(In.Height * Scale));
	Preview.Pixels.resize(static_cast<size_t>(Preview.Width) * Preview.Height * 4);
	float Decode[256];
	uint8 Encode[256];
	for (int I = 0; I < 256; ++I)
	{
		Decode[I] = Linear(static_cast<uint8>(I));
		Encode[I] = Display(I / 255.0f);
	}
	for (int Y = 0; Y < Preview.Height; ++Y)
		for (int X = 0; X < Preview.Width; ++X)
		{
			const int SX = std::min(In.Width - 1, static_cast<int>((X + 0.5f) * In.Width / Preview.Width)),
					  SY = std::min(In.Height - 1, static_cast<int>((Y + 0.5f) * In.Height / Preview.Height));
			const auto* Pixel = In.Pixels.data() + (static_cast<size_t>(SY) * In.Width + SX) * 4;
			auto* D = Preview.Pixels.data() + (static_cast<size_t>(Y) * Preview.Width + X) * 4;
			if (static_cast<int>(Channel) >= 2)
			{
				D[0] = D[1] = D[2] = Pixel[static_cast<int>(Channel) - 2];
			}
			else
				for (int C = 0; C < 3; ++C)
				{
					if (Channel == ETexturePreviewChannel::RGB || Pixel[3] == 255)
						D[C] = In.bSRGB ? Pixel[C] : Encode[Pixel[C]];
					else
					{
						const float A = Pixel[3] / 255.0f, Background = ((X / 16 + Y / 16) & 1) ? 0.32f : 0.18f;
						D[C] = Display((In.bSRGB ? Decode[Pixel[C]] : Pixel[C] / 255.0f) * A + Background * (1 - A));
					}
				}
			D[3] = 255;
		}
	Out = std::move(Preview);
	Error.Empty();
	return true;
}
TUniquePtr<FTextureAssetEditSession> FTextureAssetEditSession::Open(const FAssetRegistry& Registry,
																	const FAssetReference& Ref, FString& Error)
{
	const auto* Data = Registry.Resolve(Ref, Error, "Genesis.Texture2D");
	if (!Data)
		return {};
	auto Session = MakeUnique<FTextureAssetEditSession>();
	Session->Reference = Data->GetReference();
	Session->Target = Data->FilePath;
	FAssetPackage P;
	FString After;
	if (!FFile::ReadAllText(Session->Target, Session->Manifest) ||
		!FAssetPackageStore::Load(Session->Target, P, Error) ||
		!FRenderAssetCodec::DecodeTexture(P, Session->Draft, Error))
	{
		if (Error.IsEmpty())
			Error = "Cannot read texture asset.";
		return {};
	}
	if (P.Id != Session->Reference.Id)
	{
		Error = "Texture identity changed; refresh content.";
		return {};
	}
	if (!FFile::ReadAllText(Session->Target, After) || After != Session->Manifest)
	{
		Error = "Texture changed while opening; try again.";
		return {};
	}
	Session->SourceFile = FMapAssetImport::GetImportSource(P, Session->Target);
	Error.Empty();
	return Session;
}
bool FTextureAssetEditSession::Reimport(const FString& File, FString& Error)
{
	FLandscapeImage Image;
	if (!FLandscapeTextureLibrary::LoadImageFile(File, Image, Error))
		return false;
	try
	{
		const auto Full = std::filesystem::absolute(std::filesystem::path(File.ToWide())).lexically_normal();
		FTextureAsset Updated;
		Updated.Width = Image.Width;
		Updated.Height = Image.Height;
		Updated.bSRGB = Draft.bSRGB;
		Updated.Pixels = std::move(Image.Pixels);
		FAssetPackage Check;
		if (!FRenderAssetCodec::EncodeTexture(Updated, Check, Error))
			return false;
		Draft = std::move(Updated);
		SourceFile = FString::FromWide(Full.wstring());
		bDirty = true;
		Error.Empty();
		return true;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		return false;
	}
}
bool FTextureAssetEditSession::Save(const FAssetRegistry& Registry, FString& Error)
{
	const auto* Data = Registry.Resolve(Reference, Error, "Genesis.Texture2D");
	if (!Data)
		return false;
	if (Data->FilePath != Target)
	{
		Error = "Texture moved while editing; reopen it before saving.";
		return false;
	}
	FString Current;
	if (!FFile::ReadAllText(Target, Current) || Current != Manifest)
	{
		Error = "Texture changed on disk; reopen it before saving this draft.";
		return false;
	}
	FAssetPackage P;
	P.Id = Reference.Id;
	if (!FRenderAssetCodec::EncodeTexture(Draft, P, Error))
		return false;
	if (!SourceFile.IsEmpty())
		FMapAssetImport::SetImportSource(P, Target, SourceFile);
	if (!FAssetPackageStore::Save(Target, P, Error))
		return false;
	if (!FFile::ReadAllText(Target, Manifest))
	{
		Error = "Texture saved, but its manifest could not be reread; reopen it.";
		return false;
	}
	bDirty = false;
	Error.Empty();
	return true;
}
