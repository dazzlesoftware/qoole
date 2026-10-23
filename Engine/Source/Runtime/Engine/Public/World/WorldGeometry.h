// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Assets/RenderAssets.h"
class Landscape;
class Foliage;
struct FStaticMeshComponentData
{
	FString BuiltIn;
	FAssetReference Asset, MaterialOverride;
	TArray<FAssetReference> AdditionalOverrides;
	bool IsNative() const
	{
		return !Asset.IsNull();
	}
};
// Native component payloads and bulk ranges, shared by editor and runtime.
// Decode leaves output unchanged on failure; encode rolls back bulk/dependency
// appends and leaves the result unchanged. No legacy Object or game definitions.
class FWorldGeometryCodec
{
  public:
	static bool EncodeMesh(const FStaticMeshComponentData&, FJsonValue&, FString&);
	static bool DecodeMesh(const FJsonValue&, FStaticMeshComponentData&, FString&);
	static bool GetAssetReferences(const FString& ComponentType, const FJsonValue& Data, TArray<FAssetReference>&,
								   FString&);
	static bool ParseTextureToken(const FString&, FAssetReference&, FString& Error);
	static bool EncodeLandscape(const Landscape&, FAssetPackage&, FJsonValue&, FString&);
	static bool EncodeFoliage(const Foliage&, FAssetPackage&, FJsonValue&, FString&);
	static bool DecodeLandscape(const FAssetPackage&, const FJsonValue&, std::shared_ptr<const Landscape>&, FString&,
								const FAssetRegistry* Registry = nullptr);
	static bool DecodeFoliage(const FAssetPackage&, const FJsonValue&, std::shared_ptr<const Foliage>&, FString&,
							  const FAssetRegistry* Registry = nullptr);
};
