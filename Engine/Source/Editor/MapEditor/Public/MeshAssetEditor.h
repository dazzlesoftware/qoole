// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Assets/RenderAssets.h"

// Editor-only draft. Saving preserves identity and rejects concurrent disk changes.
class FMeshAssetEditSession
{
  public:
	static TUniquePtr<FMeshAssetEditSession> Open(const FAssetRegistry&, const FAssetReference&, FString& Error);
	const FStaticMeshAsset& GetMesh() const
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
	bool SetMaterial(int32 Slot, const FAssetReference&, const FAssetRegistry&, FString& Error);
	// Stages OBJ geometry; named slots retain their original indices/defaults.
	bool Reimport(const FString& File, FString& Error);
	bool BuildPreview(const FAssetRegistry&, FLoadedMeshAsset&, FString& Error) const;
	bool Save(const FAssetRegistry&, FString& Error);

  private:
	FStaticMeshAsset Draft;
	FAssetReference Reference;
	FString Target, Manifest, SourceFile;
	bool bDirty = false;
};
