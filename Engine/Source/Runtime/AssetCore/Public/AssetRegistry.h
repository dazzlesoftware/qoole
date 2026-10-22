// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "AssetPackage.h"

struct FAssetMount
{
	FString Root;      // /Game, /Engine, or a future plugin content root
	FString Directory;
};

struct FAssetReference
{
	FString Id;
	FString Path; // logical path hint, without extension
	bool IsNull() const { return Id.IsEmpty() && Path.IsEmpty(); }
	FJsonValue ToJson() const;
	static TOptional<FAssetReference> FromJson(const FJsonValue& Json, FString& Error);
};

struct FAssetData
{
	FString PackagePath;
	FString FilePath;
	FAssetPackageMetadata Metadata;
	FAssetReference GetReference() const { return { Metadata.Id, PackagePath }; }
};

struct FAssetRegistryIssue
{
	FString FilePath;
	FString Message;
};

// Explicit, synchronous discovery. No payload loading, filesystem watcher, UI,
// world model or editor dependencies. Readers use a snapshot until Scan runs.
class FAssetRegistry
{
public:
	// Invalid mount changes leave the existing snapshot intact. Roots and
	// directories must be unique; overlapping directories are rejected.
	bool SetMounts(TArray<FAssetMount> Mounts, FString& Error);
	// Replaces the snapshot. Returns false when there are issues, while keeping
	// other discovered packages available. Never follows directory symlinks.
	bool Scan();
	uint64 GetRevision() const { return Revision; }
	const TArray<FAssetMount>& GetMounts() const { return Mounts; }
	const TArray<FAssetData>& GetAssets() const { return Assets; }
	const TArray<FAssetRegistryIssue>& GetIssues() const { return Issues; }
	// Pointers remain valid until the next successful SetMounts or Scan.
	const FAssetData* FindById(const FString& Id) const;
	const FAssetData* FindByPath(const FString& PackagePath) const;
	const FAssetData* FindByFile(const FString& FilePath) const;
	FString GetConflict(const FAssetData& Asset) const;
	// An ID is authoritative. A stale/missing ID never falls back to a path
	// which could now refer to a different asset. Path-only references work too.
	const FAssetData* Resolve(const FAssetReference& Reference, FString& Error, std::string_view ExpectedSchema = {}) const;
	bool Load(const FAssetReference& Reference, FAssetPackage& Package, FString& Error, std::string_view ExpectedSchema = {}) const;
	static bool IsPackagePath(const FString& Path);
private:
	TArray<FAssetMount> Mounts;
	TArray<FAssetData> Assets;
	TArray<FAssetRegistryIssue> Issues;
	TMap<std::string, TArray<size_t>> ById, ByPath;
	TMap<std::string, size_t> ByFile;
	uint64 Revision = 0;
};
