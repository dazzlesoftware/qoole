// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Materials/MaterialGraph.h"
struct FMaterialCollectionParameter
{
	FString Name;
	uint8 Width = 1;
	float Value[4] = {};
};
struct FMaterialParameterCollection
{
	TArray<FMaterialCollectionParameter> Parameters;
};
class FMaterialParameterCollectionCodec
{
  public:
	static bool Encode(const FMaterialParameterCollection&, FAssetPackage&, FString&);
	static bool Decode(const FAssetPackage&, FMaterialParameterCollection&, FString&);
	static bool Refresh(FMaterialGraph&, const FAssetRegistry&, bool& bChanged, FString&);
};
// Share one instance across renderers/materials in a world. Owner-thread updates
// become immutable per-frame uniform snapshots; shader binaries remain unchanged.
class FMaterialParameterCollectionInstance
{
  public:
	FMaterialParameterCollectionInstance(FAssetReference, FMaterialParameterCollection);
	const FAssetReference& GetReference() const
	{
		return Reference;
	}
	bool SetScalar(const FString&, float, FString&);
	bool SetVector(const FString&, const float (&)[4], FString&);
	bool GetValue(const FString&, uint8 Width, float (&)[4], FString&) const;

  private:
	FAssetReference Reference;
	FMaterialParameterCollection Values;
};
