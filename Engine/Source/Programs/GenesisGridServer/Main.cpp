// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// GenesisGridServer: the world map's grid server, as OpenSim's ROBUST grid
// service. Stage 1 runs once: it reads its configuration, registers every
// region file on the world map, reports each result and exits. Region
// servers connecting over the network arrive in a later stage.
//
// Command line:
//   GenesisGridServer.exe -config=<GridServer.ini> [-save]
//
//   [GridServer]
//   Project = ../MyGame.gproject   relative to this file
//   World = /Game/Worlds/Main      the Genesis.WorldComposition registry
//   Regions = Regions              folder of region files, relative to this file
//   SaveWorld = false              write registrations back (or pass -save)
//
//   [GridService]
//   AutoRegistration = true
//   DeleteOnUnregister = true
//   Region_Hills = "DefaultRegion, FallbackRegion"
//
// Exit status: 0 when every region registered, 1 when any region file or
// region was rejected, 2 when the configuration, project or world is unusable.
//
// This executable links no Editor, MapCore, UI or RHI libraries.
#include "World/GridService.h"
#include "World/RegionConfig.h"
#include "HAL/PlatformMisc.h"
#include "Internationalization/TextLocalization.h"
#include "ProjectDescriptor.h"
#include <cstdio>
#include <filesystem>

namespace
{
	constexpr int ExitRegionsRejected = 1;
	constexpr int ExitUnusable = 2;

	void Print(const FString& Line)
	{
		std::printf("%s\n", *Line);
	}
	void PrintError(const FString& Line)
	{
		std::fflush(stdout); // keep the two streams in order when both are redirected
		std::fprintf(stderr, "%s\n", *Line);
	}
	FString Relative(const FString& ConfigFile, const FString& Path)
	{
		const std::filesystem::path Value(Path.ToWide());
		return FString::FromWide(
		    (Value.is_absolute() ? Value : std::filesystem::path(ConfigFile.ToWide()).parent_path() / Value)
		        .lexically_normal()
		        .wstring());
	}
	FString FlagText(ERegionFlags Flags)
	{
		FString Text;
		for (const TPair<ERegionFlags, const char*> Flag :
		    {TPair<ERegionFlags, const char*>(ERegionFlags::DefaultRegion, "DefaultRegion"),
		        TPair<ERegionFlags, const char*>(ERegionFlags::FallbackRegion, "FallbackRegion"),
		        TPair<ERegionFlags, const char*>(ERegionFlags::NoDirectLogin, "NoDirectLogin"),
		        TPair<ERegionFlags, const char*>(ERegionFlags::Persistent, "Persistent"),
		        TPair<ERegionFlags, const char*>(ERegionFlags::LockedOut, "LockedOut"),
		        TPair<ERegionFlags, const char*>(ERegionFlags::NoMove, "NoMove")})
			if (HasFlag(Flags, Flag.first))
				Text += Text.IsEmpty() ? FString(Flag.second) : FString::Format(", {}", Flag.second);
		return Text;
	}
} // namespace

int wmain(int Argc, wchar_t** Argv)
{
	FString ConfigPath;
	bool bSave = false;
	for (int I = 1; I < Argc; ++I)
	{
		const FString Arg = FString::FromWide(Argv[I]);
		if (Arg.StartsWith("-config="))
			ConfigPath = Arg.Mid(8);
		else if (Arg.Equals("-save", ESearchCase::IgnoreCase))
			bSave = true;
	}
	if (ConfigPath.IsEmpty())
	{
		PrintError(GENESIS_TEXT("Usage: GenesisGridServer -config=<GridServer.ini> [-save]"));
		return ExitUnusable;
	}
	ConfigPath = FString::FromWide(std::filesystem::absolute(std::filesystem::path(ConfigPath.ToWide())).wstring());

	FString Error;
	FConfigFile Config;
	if (!FConfigFile::Load(ConfigPath, Config, Error))
	{
		PrintError(Error);
		return ExitUnusable;
	}
	const FConfigSection* Server = Config.FindSection("GridServer");
	const FConfigEntry* ProjectEntry = Server ? Server->Find("Project") : nullptr;
	const FConfigEntry* WorldEntry = Server ? Server->Find("World") : nullptr;
	if (!ProjectEntry || !WorldEntry)
	{
		PrintError(FTextLocalization::Format("{}: [GridServer] needs Project and World.", ConfigPath));
		return ExitUnusable;
	}
	const FConfigEntry* RegionsEntry = Server->Find("Regions");
	const FString RegionsDir = Relative(ConfigPath, RegionsEntry ? RegionsEntry->Value : FString("Regions"));
	if (const FConfigEntry* SaveEntry = Server->Find("SaveWorld"))
		bSave = bSave || SaveEntry->Value.Equals("true", ESearchCase::IgnoreCase);
	FGridServiceSettings Settings;
	if (const FConfigSection* Service = Config.FindSection("GridService"))
		if (!FGridServiceSettings::FromConfig(*Service, Settings, Error))
		{
			PrintError(FTextLocalization::Format("{}: {}", ConfigPath, Error));
			return ExitUnusable;
		}

	TOptional<FProjectContext> Project = FProjectContext::Open(Relative(ConfigPath, ProjectEntry->Value), Error);
	if (!Project)
	{
		PrintError(Error);
		return ExitUnusable;
	}
	FAssetRegistry Registry;
	if (!Registry.SetMounts({{"/Game", Project->GetContentDir()}, {"/Engine", FPaths::FindContentDir()}}, Error))
	{
		PrintError(Error);
		return ExitUnusable;
	}
	Registry.Scan();
	const FAssetReference WorldReference{{}, WorldEntry->Value};
	const FAssetData* WorldAsset = Registry.Resolve(WorldReference, Error, "Genesis.WorldComposition");
	FAssetPackage WorldPackage;
	FWorldComposition World;
	if (!WorldAsset || !Registry.Load(WorldReference, WorldPackage, Error, "Genesis.WorldComposition") ||
	    !FWorldComposition::FromPackage(WorldPackage, World, Error))
	{
		PrintError(FTextLocalization::Format("World {}: {}", WorldEntry->Value, Error));
		return ExitUnusable;
	}
	const FString WorldFile = WorldAsset->FilePath;
	FGridService Grid(std::move(World), std::move(Settings));
	Print(FTextLocalization::Format("Grid server: world {} ({} regions on a {}-unit grid), region files in {}",
	    WorldEntry->Value, Grid.GetWorld().GetTiles().size(), Grid.GetWorld().GetCellSize(), RegionsDir));

	int Result = 0;
	TArray<FRegionConfig> Regions;
	TArray<FRegionConfigIssue> Issues;
	if (!FRegionConfigReader::ReadDirectory(RegionsDir, Regions, Issues))
		Result = ExitRegionsRejected;
	for (const FRegionConfigIssue& Issue : Issues)
		PrintError(Issue.Message);
	for (FRegionConfig& Region : Regions)
	{
		for (const FString& Warning : Region.Warnings)
			Print(FTextLocalization::Format("Warning: {}", Warning));
		const FAssetData* Map = Registry.Resolve(Region.Tile.Map, Error, "Genesis.World");
		if (Map && Map->Metadata.Kind != EAssetPackageKind::Map)
		{
			Map = nullptr;
			Error = GENESIS_TEXT("Only world maps can be regions.");
		}
		if (Map)
			Region.Tile.Map = Map->GetReference();
		else
			Error = FTextLocalization::Format("Map {}: {}", Region.Tile.Map.Path, Error);
		if (!Map || !Grid.RegisterRegion(Region.Tile, Error))
		{
			PrintError(FTextLocalization::Format(
			    "Rejected {} ({}({})): {}", Region.Tile.Name, Region.SourceFile, Region.Line, Error));
			Result = ExitRegionsRejected;
			continue;
		}
		Print(FTextLocalization::Format("Registered {} at {},{}", Region.Tile.Name, Region.Tile.X, Region.Tile.Y));
	}

	Print(GENESIS_TEXT("World map:"));
	for (const FWorldCompositionTile& Tile : Grid.GetWorld().GetTiles())
	{
		const FString Flags = FlagText(Grid.GetFlags(Tile.Id));
		Print(FTextLocalization::Format("  {} at {},{} ({}x{} cells) {}{}{}", Tile.Name, Tile.X, Tile.Y, Tile.SizeX,
		    Tile.SizeY, Grid.IsOnline(Tile.Id) ? GENESIS_TEXT("online") : GENESIS_TEXT("offline"),
		    Tile.Host.IsEmpty() ? FString() : FString::Format(" on {}", Tile.Host),
		    Flags.IsEmpty() ? FString() : FString::Format(" [{}]", Flags)));
	}

	if (bSave && Grid.IsModified())
	{
		Grid.GetWorld().ToPackage(WorldPackage);
		if (!FAssetPackageStore::Save(WorldFile, WorldPackage, Error))
		{
			PrintError(FTextLocalization::Format("Could not save the world map: {}", Error));
			return ExitUnusable;
		}
		Print(FTextLocalization::Format("Saved the world map to {}", WorldFile));
	}
	return Result;
}
