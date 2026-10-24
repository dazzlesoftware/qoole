// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "AssetRegistry.h"
#include "GameFramework/RegionState.h"
#include "World/WorldLocation.h"

class FGameWorld;

// A saved game, as Unreal's USaveGame. The player is stored by region and
// local position, so the save survives regions being moved on the grid and
// any origin rebasing. The absolute position is kept as a fallback for a
// region that has since been removed. Files are JSON in the project's
// Saved/SaveGames folder and are replaced atomically.
struct FSaveGame
{
	static constexpr int32 Version = 1;
	static constexpr int32 MaxBytes = 16 * 1024 * 1024;
	FAssetReference World;
	FWorldLocation Player;
	double AbsoluteX = 0, AbsoluteY = 0, AbsoluteZ = 0;
	float Yaw = 0, Pitch = 0; // degrees
	FRegionStateStore Regions;
	FJsonValue Game = FJsonValue::MakeObject(); // game-wide state

	FJsonValue ToJson() const;
	static TOptional<FSaveGame> FromJson(const FJsonValue& Json, FString& Error);
	bool Save(const FString& File, FString& Error) const;
	static TOptional<FSaveGame> Load(const FString& File, FString& Error);
	// Where the player should be in this world: the saved region when it still
	// exists, else the saved absolute position.
	bool ResolvePlayer(const FGameWorld& InWorld, FWorldLocation& Out) const;
};
