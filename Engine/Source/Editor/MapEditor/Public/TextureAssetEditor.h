// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Assets/RenderAssets.h"
enum class ETexturePreviewChannel
{
	RGBA,
	RGB,
	Red,
	Green,
	Blue,
	Alpha
};
// Opaque sRGB display pixels, alpha composited in linear light over a checkerboard.
// Source pixels are never changed. Large images use a bounded preview.
bool BuildTextureAssetPreview(const FTextureAsset&, ETexturePreviewChannel, FTextureAsset&, FString& Error);
class FTextureAssetEditSession
{
  public:
	static TUniquePtr<FTextureAssetEditSession> Open(const FAssetRegistry&, const FAssetReference&, FString& Error);
	const FTextureAsset& GetTexture() const
	{
		return Draft;
	}
	const FAssetReference& GetReference() const
	{
		return Reference;
	}
	const FString& GetSourceFile() const
	{
		return SourceFile;
	}
	bool IsDirty() const
	{
		return bDirty;
	}
	void SetSRGB(bool Value)
	{
		if (Draft.bSRGB != Value)
		{
			Draft.bSRGB = Value;
			bDirty = true;
		}
	}
	bool Reimport(const FString& File, FString& Error);
	bool Save(const FAssetRegistry&, FString& Error);

  private:
	FTextureAsset Draft;
	FAssetReference Reference;
	FString Target, Manifest, SourceFile;
	bool bDirty = false;
};
