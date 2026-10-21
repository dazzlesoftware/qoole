// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Serialization/Json.h"

// Project data has no dependency on the editor or the legacy map model.
struct FProjectDescriptor
{
	static constexpr int32 CurrentVersion = 1;
	FString Name;
	FString EngineAssociation = "Genesis";
	FString Description;
	// A logical package path, e.g. /Game/Maps/Main; empty opens a new level.
	FString DefaultMap;
	// Unknown fields survive a load/save, including future plugin settings.
	FJsonValue Extensions = FJsonValue::MakeObject();
	FJsonValue ToJson() const;
	static TOptional<FProjectDescriptor> FromJson(const FJsonValue& Json, FString& Error);
};

class FProjectContext
{
public:
	static TOptional<FProjectContext> Open(const FString& Path, FString& Error);
	// Creates an empty project. Never overwrites an existing descriptor.
	static TOptional<FProjectContext> Create(const FString& Path, const FString& Name, FString& Error);
	const FProjectDescriptor& GetDescriptor() const { return Descriptor; }
	const FString& GetFilePath() const { return FilePath; }
	FString GetRootDir() const;
	FString GetContentDir() const;
	FString GetSavedDir() const;
	// Validates a /Game path and resolves it below this project's Content.
	bool ResolvePackage(const FString& Package, bool bMap, FString& File, FString& Error) const;
	// The application owns project lifetime; tools can query its context.
	static const FProjectContext* GetActive();
	static void SetActive(TOptional<FProjectContext> Project);
private:
	FString FilePath;
	FProjectDescriptor Descriptor;
};
