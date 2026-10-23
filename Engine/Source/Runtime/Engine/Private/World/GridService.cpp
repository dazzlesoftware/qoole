// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "World/GridService.h"
#include "Internationalization/TextLocalization.h"

namespace
{
	constexpr std::string_view RegionPrefix = "Region_";

	struct FFlagName
	{
		std::string_view Name;
		ERegionFlags Flag;
	};
	constexpr FFlagName FlagNames[] = {
	    {"DefaultRegion", ERegionFlags::DefaultRegion},
	    {"FallbackRegion", ERegionFlags::FallbackRegion},
	    {"NoDirectLogin", ERegionFlags::NoDirectLogin},
	    {"Persistent", ERegionFlags::Persistent},
	    {"LockedOut", ERegionFlags::LockedOut},
	    {"NoMove", ERegionFlags::NoMove},
	};

	bool ParseBool(const FConfigEntry& Entry, bool& Out, FString& Error)
	{
		if (Entry.Value.Equals("true", ESearchCase::IgnoreCase))
			Out = true;
		else if (Entry.Value.Equals("false", ESearchCase::IgnoreCase))
			Out = false;
		else
		{
			Error = FTextLocalization::Format("Line {}: {} must be true or false.", Entry.Line, Entry.Key);
			return false;
		}
		return true;
	}
} // namespace

bool FGridServiceSettings::FromConfig(const FConfigSection& Section, FGridServiceSettings& Out, FString& Error)
{
	FGridServiceSettings Result;
	for (const FConfigEntry& Entry : Section.Entries)
	{
		if (Entry.Key.Equals("AutoRegistration", ESearchCase::IgnoreCase))
		{
			if (!ParseBool(Entry, Result.bAutoRegistration, Error))
				return false;
			continue;
		}
		if (Entry.Key.Equals("DeleteOnUnregister", ESearchCase::IgnoreCase))
		{
			if (!ParseBool(Entry, Result.bDeleteOnUnregister, Error))
				return false;
			continue;
		}
		if (!Entry.Key.View().starts_with(RegionPrefix))
			continue; // other services' keys share the section in OpenSim files
		const FString Target(Entry.Key.View().substr(RegionPrefix.size()));
		if (Target.IsEmpty())
		{
			Error = FTextLocalization::Format("Line {}: Region_ needs a region name or RegionUUID.", Entry.Line);
			return false;
		}
		ERegionFlags Flags = ERegionFlags::None;
		for (const FString& Part : Entry.Value.ParseIntoArray(","))
		{
			const FString Name = Part.TrimStartAndEnd();
			bool bFound = false;
			for (const FFlagName& Known : FlagNames)
				if (Name.Equals(Known.Name, ESearchCase::IgnoreCase))
				{
					Flags = Flags | Known.Flag;
					bFound = true;
				}
			if (bFound || Name.IsEmpty())
				continue;
			Error = Name.Equals("Reservation", ESearchCase::IgnoreCase) ||
			        Name.Equals("Authenticate", ESearchCase::IgnoreCase)
			    ? FTextLocalization::Format(
			          "Line {}: {} needs region server authentication, which is not supported yet.", Entry.Line, Name)
			    : FTextLocalization::Format("Line {}: unknown region flag {}.", Entry.Line, Name);
			return false;
		}
		const FString Lower = Target.ToLower();
		if (FAssetId::IsValid(Lower))
			Result.FlagsById[Lower.ToStdString()] = Flags;
		else
			Result.FlagsByName[Target.ToStdString()] = Flags;
	}
	Out = std::move(Result);
	Error.Empty();
	return true;
}

ERegionFlags FGridServiceSettings::GetFlags(const FWorldCompositionTile& Tile) const
{
	ERegionFlags Flags = ERegionFlags::None;
	if (const TMap<std::string, ERegionFlags>::const_iterator It = FlagsById.find(Tile.Id.ToStdString());
	    It != FlagsById.end())
		Flags = Flags | It->second;
	if (const TMap<std::string, ERegionFlags>::const_iterator It = FlagsByName.find(Tile.Name.ToStdString());
	    It != FlagsByName.end())
		Flags = Flags | It->second;
	return Flags;
}

FGridService::FGridService(FWorldComposition InWorld, FGridServiceSettings InSettings)
    : World(std::move(InWorld)), Settings(std::move(InSettings))
{
}

bool FGridService::RegisterRegion(const FWorldCompositionTile& Tile, FString& Error)
{
	if (Tile.Map.Id.IsEmpty())
	{
		Error = FTextLocalization::Format("Region {} must name its map by identity.", Tile.Name);
		return false;
	}
	const FWorldCompositionTile* Known = World.FindTile(Tile.Id);
	const ERegionFlags Flags = Settings.GetFlags(Tile) | (Known ? Settings.GetFlags(*Known) : ERegionFlags::None);
	if (HasFlag(Flags, ERegionFlags::LockedOut))
	{
		Error = FTextLocalization::Format("Region {} is locked out of the grid.", Tile.Name);
		return false;
	}
	FWorldComposition Next = World;
	if (Known)
	{
		const bool bMoved =
		    Known->X != Tile.X || Known->Y != Tile.Y || Known->SizeX != Tile.SizeX || Known->SizeY != Tile.SizeY;
		if (bMoved && HasFlag(Flags, ERegionFlags::NoMove))
		{
			Error = FTextLocalization::Format("Region {} may not move from cell {},{}.", Tile.Name, Known->X, Known->Y);
			return false;
		}
		const bool bChanged = Known->ToJson() != Tile.ToJson();
		if (bChanged && !Next.UpdateTile(Tile, Error))
			return false;
		bModified = bModified || bChanged;
	}
	else
	{
		if (!Settings.bAutoRegistration)
		{
			Error = FTextLocalization::Format(
			    "Region {} is not on the world map and automatic registration is off.", Tile.Name);
			return false;
		}
		if (!Next.AddTile(Tile, Error))
			return false;
		AutoRegistered.insert(Tile.Id.ToStdString());
		bModified = true;
	}
	World = std::move(Next);
	Online.insert(Tile.Id.ToStdString());
	Error.Empty();
	return true;
}

bool FGridService::DeregisterRegion(const FString& Id, FString& Error)
{
	const FWorldCompositionTile* Tile = World.FindTile(Id);
	if (!Tile || !Online.contains(Id.ToStdString()))
	{
		Error = GENESIS_TEXT("That region is not online.");
		return false;
	}
	Online.erase(Id.ToStdString());
	const bool bRemove = Settings.bDeleteOnUnregister && AutoRegistered.contains(Id.ToStdString()) &&
	    !HasFlag(Settings.GetFlags(*Tile), ERegionFlags::Persistent);
	if (bRemove)
	{
		if (!World.RemoveTile(Id, Error))
			return false;
		AutoRegistered.erase(Id.ToStdString());
		bModified = true;
	}
	Error.Empty();
	return true;
}

bool FGridService::IsOnline(const FString& Id) const
{
	return Online.contains(Id.ToStdString());
}

ERegionFlags FGridService::GetFlags(const FString& Id) const
{
	const FWorldCompositionTile* Tile = World.FindTile(Id);
	return Tile ? Settings.GetFlags(*Tile) : ERegionFlags::None;
}

void FGridService::GetRegionsWithFlag(ERegionFlags Flag, TArray<const FWorldCompositionTile*>& Out) const
{
	Out.clear();
	for (const FWorldCompositionTile& Tile : World.GetTiles())
		if (IsOnline(Tile.Id) && HasFlag(Settings.GetFlags(Tile), Flag))
			Out.push_back(&Tile);
}
