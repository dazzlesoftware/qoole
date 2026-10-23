// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "World/WorldComposition.h"
#include "Misc/ConfigFile.h"

// Region files, as OpenSim's Regions/*.ini: one [Region Name] section per
// region a server hosts.
//
//   [Hills]
//   RegionUUID = 7c1e0c8a-...     required; stored lowercase
//   Location = 1000,1000          required; grid cell of the -X/-Y corner
//   SizeX = 1                     cells, default 1 (OpenSim counts metres)
//   SizeY = 1
//   Map = /Game/Regions/Hills     required; the region's Genesis.World map
//   ExternalHostName = 127.0.0.1  with InternalPort, the region server's address;
//   InternalPort = 9000           both empty for a region this process loads
//
// Other keys are reported as warnings and ignored, so OpenSim files load.
struct FRegionConfig
{
	FWorldCompositionTile Tile; // the map reference holds the path only
	FString SourceFile;
	int32 Line = 0;
	TArray<FString> Warnings;
};

struct FRegionConfigIssue
{
	FString SourceFile;
	FString Message;
};

class FRegionConfigReader
{
public:
	// All regions of one file; on failure Out is unchanged.
	static bool Read(const FConfigFile& File, TArray<FRegionConfig>& Out, FString& Error);
	// Every *.ini in the folder, in name order. A file that fails is reported
	// and skipped; the others still load. Returns false if the folder is
	// unreadable or any file failed.
	static bool ReadDirectory(const FString& Directory, TArray<FRegionConfig>& Out, TArray<FRegionConfigIssue>& Issues);
};
