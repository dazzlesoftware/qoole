// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "World/WorldComposition.h"
#include "Misc/ConfigFile.h"

// Per-region policy flags, as OpenSim's grid service Region_<Name> entries.
enum class ERegionFlags : uint32
{
	None = 0,
	DefaultRegion = 1 << 0,  // where logins go when the requested region is unavailable
	FallbackRegion = 1 << 1, // tried, in registry order, when the default regions are not online
	NoDirectLogin = 1 << 2,  // reachable only by travelling from another region
	Persistent = 1 << 3,     // stays on the map when its server unregisters
	LockedOut = 1 << 4,      // may not register
	NoMove = 1 << 5,         // may not register at another location or size
};
inline ERegionFlags operator|(ERegionFlags A, ERegionFlags B)
{
	return static_cast<ERegionFlags>(static_cast<uint32>(A) | static_cast<uint32>(B));
}
inline bool HasFlag(ERegionFlags Flags, ERegionFlags Flag)
{
	return (static_cast<uint32>(Flags) & static_cast<uint32>(Flag)) != 0;
}

struct FGridServiceSettings
{
	// Regions the world registry does not list may register themselves.
	bool bAutoRegistration = true;
	// Auto-registered regions leave the map when their server unregisters.
	bool bDeleteOnUnregister = true;
	// Region_<Name or RegionUUID> = "DefaultRegion, FallbackRegion"
	TMap<std::string, ERegionFlags> FlagsByName, FlagsById;
	// Reads a [GridService] section. Reservation and Authenticate are rejected
	// until region servers authenticate.
	static bool FromConfig(const FConfigSection& Section, FGridServiceSettings& Out, FString& Error);
	ERegionFlags GetFlags(const FWorldCompositionTile& Tile) const;
};

// The world map's registration service, as OpenSim's grid service. It owns
// the world composition: regions in the loaded registry are known and stay on
// the map; region servers bring them online and may register new ones.
// Every rejected call leaves the map unchanged. No networking; the server
// programs call it.
class FGridService
{
public:
	FGridService(FWorldComposition InWorld, FGridServiceSettings InSettings);
	const FWorldComposition& GetWorld() const
	{
		return World;
	}
	const FGridServiceSettings& GetSettings() const
	{
		return Settings;
	}
	// A region server announces a region it hosts. A known ID may move or be
	// renamed (unless NoMove) and keeps its identity; an unknown one is added
	// when auto-registration is on. The map reference must carry an ID.
	bool RegisterRegion(const FWorldCompositionTile& Tile, FString& Error);
	bool DeregisterRegion(const FString& Id, FString& Error);
	bool IsOnline(const FString& Id) const;
	ERegionFlags GetFlags(const FString& Id) const;
	// Online regions with the flag, in registry order.
	void GetRegionsWithFlag(ERegionFlags Flag, TArray<const FWorldCompositionTile*>& Out) const;
	// Whether the map differs from the registry it was loaded from.
	bool IsModified() const
	{
		return bModified;
	}

private:
	FWorldComposition World;
	FGridServiceSettings Settings;
	TSet<std::string> Online, AutoRegistered;
	bool bModified = false;
};
