// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Serialization/Json.h"

enum class EAssetPackageKind { Asset, Map };

class FAssetId
{
public:
	static FString New();
	static bool IsValid(const FString& Id);
};

struct FAssetPackageMetadata
{
	FString Id;
	EAssetPackageKind Kind = EAssetPackageKind::Asset;
	FString Schema;
	int32 SchemaVersion = 1;
	TArray<FString> Dependencies;
	uint64 ExportBytes = 0;
	uint64 BulkBytes = 0;
};

// Native, versioned storage. Object serializers supply structured exports and
// little-endian bulk data; this layer never depends on a world or editor type.
struct FAssetPackage
{
	FString Id; // UUID, retained on resave; allocated on first save
	EAssetPackageKind Kind = EAssetPackageKind::Asset;
	FString Schema;
	int32 SchemaVersion = 1;
	FJsonValue Exports = FJsonValue::MakeObject();
	TArray<uint8> Bulk;
	TArray<FString> Dependencies; // logical package paths
};

class FAssetPackageStore
{
public:
	static constexpr int32 FormatVersion = 1;
	// Writes immutable generation sidecars, then atomically replaces the
	// .asset/.gmap manifest. A failed save leaves the previous package readable.
	// Older generations are retained until an explicit future cleanup pass.
	static bool Save(const FString& Path, FAssetPackage& Package, FString& Error);
	// Checks version, sizes and checksums before decoding any exports.
	// On failure the caller's package is unchanged.
	static bool Load(const FString& Path, FAssetPackage& Package, FString& Error);
	// Reads and validates only the manifest. Payload checksums are verified by
	// Load, not by discovery. On failure Metadata is unchanged.
	static bool ReadMetadata(const FString& Path, FAssetPackageMetadata& Metadata, FString& Error);
};
