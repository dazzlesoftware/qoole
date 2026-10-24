// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Serialization/Json.h"

// Gameplay changes to each region (doors opened, items taken, actors moved),
// keyed by the region's permanent ID. The store outlives streaming, so a
// region that unloads and loads again finds its state unchanged; the game
// applies a region's state when FGameWorld reports it loaded. Saved with the game.
class FRegionStateStore
{
public:
	static constexpr int32 MaxRegions = 65536;
	// The region's state object, created empty on first use; null for an invalid ID.
	FJsonValue* Edit(const FString& RegionId);
	const FJsonValue* Find(const FString& RegionId) const;
	void Remove(const FString& RegionId);
	int32 Num() const
	{
		return static_cast<int32>(Regions.size());
	}
	FJsonValue ToJson() const;
	static TOptional<FRegionStateStore> FromJson(const FJsonValue& Json, FString& Error);

private:
	TMap<std::string, FJsonValue> Regions;
};
