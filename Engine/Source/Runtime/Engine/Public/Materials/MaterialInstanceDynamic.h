// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Materials/MaterialInstance.h"
#include "Math/Color.h"
// Owns a material snapshot. Mutations are atomic and keep cooked shader bytes.
// Hosts publish a fresh loaded-material snapshot after changes; render resources
// already submitted to the GPU retain their previous values until retired.
class FMaterialInstanceDynamic
{
  public:
	static TUniquePtr<FMaterialInstanceDynamic> Create(const FMaterialAsset&, FString& Error);
	bool SetScalarParameterValue(const FString&, float, FString& Error);
	bool SetVectorParameterValue(const FString&, const FLinearColor&, FString& Error);
	bool SetTextureParameterValue(const FString&, const FAssetReference&, FString& Error);
	bool ClearParameterValue(const FString&, FString& Error);
	const FMaterialAsset& GetMaterial() const
	{
		return Resolved;
	}

  private:
	bool SetParameter(FMaterialParameterOverride, FString& Error);
	bool Commit(FMaterialInstanceAsset, FString& Error);
	FMaterialAsset Parent, Resolved;
	FMaterialInstanceAsset Overrides;
};
