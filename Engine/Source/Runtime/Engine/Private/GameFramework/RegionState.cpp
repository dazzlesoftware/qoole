// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "GameFramework/RegionState.h"
#include "AssetPackage.h"
#include "Internationalization/TextLocalization.h"
#include <algorithm>

FJsonValue* FRegionStateStore::Edit(const FString& RegionId)
{
	if (!FAssetId::IsValid(RegionId))
		return nullptr;
	const std::string Key = RegionId.ToStdString();
	const TMap<std::string, FJsonValue>::iterator It = Regions.find(Key);
	if (It != Regions.end())
		return &It->second;
	if (Regions.size() >= static_cast<size_t>(MaxRegions))
		return nullptr;
	return &Regions.emplace(Key, FJsonValue::MakeObject()).first->second;
}

const FJsonValue* FRegionStateStore::Find(const FString& RegionId) const
{
	const TMap<std::string, FJsonValue>::const_iterator It = Regions.find(RegionId.ToStdString());
	return It == Regions.end() ? nullptr : &It->second;
}

void FRegionStateStore::Remove(const FString& RegionId)
{
	Regions.erase(RegionId.ToStdString());
}

FJsonValue FRegionStateStore::ToJson() const
{
	// In ID order, so saves are stable and comparable.
	TArray<std::string> Keys;
	for (const TPair<const std::string, FJsonValue>& Region : Regions)
		Keys.push_back(Region.first);
	std::sort(Keys.begin(), Keys.end());
	FJsonValue Json = FJsonValue::MakeObject();
	for (const std::string& Key : Keys)
		Json[Key] = Regions.at(Key);
	return Json;
}

TOptional<FRegionStateStore> FRegionStateStore::FromJson(const FJsonValue& Json, FString& Error)
{
	if (!Json.IsObject() || Json.GetKeys().size() > static_cast<size_t>(MaxRegions))
	{
		Error = GENESIS_TEXT("Region state must be an object of at most 65536 regions.");
		return {};
	}
	FRegionStateStore Result;
	for (const FString& Key : Json.GetKeys())
	{
		const FJsonValue& State = Json.Get(Key.View());
		if (!FAssetId::IsValid(Key) || !State.IsObject())
		{
			Error = FTextLocalization::Format("Invalid state for region {}.", Key);
			return {};
		}
		Result.Regions[Key.ToStdString()] = State;
	}
	Error.Empty();
	return Result;
}
