// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "CoreMinimal.h"
class Object;
struct FAssetPackage;

// Temporary adapter between the legacy editor model and native packages.
// The package layer and on-disk schema have no dependency on QLE syntax.
class FMapPackage
{
public:
	static bool Save(const FString& Path, Object& Root, FString& Id, FString& Error, bool bPrefab = false);
	// Builds the package in memory without writing it, as Play-in-Editor copies the level.
	static bool Build(Object& Root, const FString& Id, FAssetPackage& Package, FString& Error, bool bPrefab = false);
	static TUniquePtr<Object> Load(const FString& Path, FString& Id, FString& Error, bool bPrefab = false, const FString& ExpectedId = {}, bool* bUpgraded = nullptr);
};
