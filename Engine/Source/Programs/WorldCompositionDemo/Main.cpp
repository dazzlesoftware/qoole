// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// WorldCompositionDemo: builds a demo world of eight regions on a 4x2 grid,
// each its own level with a different heightmap, plus the grid server files
// that register them.
//
// Command line:
//   WorldCompositionDemo.exe -project=<folder>/WorldDemo.gproject
//
// The project is created when it does not exist. The tool writes:
//   Content/Worlds/WorldDemo.gmap            the world registry
//   Content/Worlds/WorldDemoRegions/*.gmap   one level per region
//   GridServer/GridServer.ini, GridServer/Regions/*.ini
// It stops without changing anything if the demo world already exists.
//
// Open the project in GenesisEditor and double-click Worlds/WorldDemo in the
// content browser; Preview World there shows all eight regions together.
#include "CoreMinimal.h"
#include "HAL/File.h"
#include "HAL/PlatformMisc.h"
#include "Internationalization/TextLocalization.h"
#include "MapDocument.h"
#include "MapEditorEnvironment.h"
#include "ProjectDescriptor.h"
#include "World/WorldComposition.h"
#include "World/WorldCompositionLimits.h"
#include "Landscape/Landscape.h"
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace
{
	constexpr int32 RegionQuads = 128;
	constexpr double CellSize = WorldCompositionLimits::DefaultCellSize;
	// Heights blend to 0 over this share of the region's half-width, so every
	// region meets its neighbours at the same level.
	constexpr float EdgeBand = 0.12f;
	constexpr float SnowHeight = 2600.0f;
	constexpr float RockSlope = 0.55f; // rise per unit run
	constexpr float SandHeight = 60.0f;
	constexpr float Pi = 3.14159265f;

	// Deterministic value noise in [0, 1].
	float Hash(int32 X, int32 Y, uint32 Seed)
	{
		uint32 H = static_cast<uint32>(X) * 374761393u + static_cast<uint32>(Y) * 668265263u + Seed * 2246822519u;
		H = (H ^ (H >> 13)) * 1274126177u;
		return static_cast<float>((H ^ (H >> 16)) & 0xffffff) / static_cast<float>(0xffffff);
	}
	float Smooth(float T)
	{
		return T * T * (3.0f - 2.0f * T);
	}
	float ValueNoise(float X, float Y, uint32 Seed)
	{
		const int32 X0 = static_cast<int32>(std::floor(X)), Y0 = static_cast<int32>(std::floor(Y));
		const float TX = Smooth(X - X0), TY = Smooth(Y - Y0);
		const float A = Hash(X0, Y0, Seed), B = Hash(X0 + 1, Y0, Seed), C = Hash(X0, Y0 + 1, Seed),
		            D = Hash(X0 + 1, Y0 + 1, Seed);
		return (A + (B - A) * TX) + ((C + (D - C) * TX) - (A + (B - A) * TX)) * TY;
	}
	// Fractal noise in about [0, 1].
	float Fractal(float X, float Y, uint32 Seed, int32 Octaves)
	{
		float Sum = 0, Amplitude = 0.5f, Total = 0;
		for (int32 I = 0; I < Octaves; ++I)
		{
			Sum += ValueNoise(X, Y, Seed + I) * Amplitude;
			Total += Amplitude;
			X *= 2.03f;
			Y *= 2.03f;
			Amplitude *= 0.5f;
		}
		return Sum / Total;
	}

	// U, V in [0, 1] across the region; heights in world units.
	using FHeightField = float (*)(float U, float V);

	float RollingHills(float U, float V)
	{
		return 450.0f * (std::sin(U * 2 * Pi * 2.0f) * std::cos(V * 2 * Pi * 1.5f) + 1.0f) +
		    500.0f * Fractal(U * 4, V * 4, 11, 4);
	}
	float Mountains(float U, float V)
	{
		float Ridge = 0, Amplitude = 1, Total = 0, X = U * 3, Y = V * 3;
		for (int32 I = 0; I < 5; ++I)
		{
			Ridge += (1.0f - std::abs(ValueNoise(X, Y, 23 + I) * 2.0f - 1.0f)) * Amplitude;
			Total += Amplitude;
			X *= 2.1f;
			Y *= 2.1f;
			Amplitude *= 0.5f;
		}
		const float Peak = std::pow(Ridge / Total, 2.2f);
		return 4200.0f * Peak;
	}
	float Canyon(float U, float V)
	{
		const float River = 0.5f + 0.18f * std::sin(V * 2 * Pi * 1.3f) + 0.05f * std::sin(V * 2 * Pi * 4.0f);
		const float Distance = std::abs(U - River);
		const float Cut = Distance < 0.04f ? 0.0f : Distance < 0.12f ? Smooth((Distance - 0.04f) / 0.08f) : 1.0f;
		return -500.0f + 1800.0f * Cut + 150.0f * Fractal(U * 6, V * 6, 37, 3);
	}
	float Mesa(float U, float V)
	{
		const float Base = Fractal(U * 2.5f, V * 2.5f, 41, 3);
		const float Steps = 4.0f, Level = Base * Steps, Floor = std::floor(Level);
		const float Terraced = (Floor + Smooth(std::pow(Level - Floor, 6.0f))) / Steps;
		return 2200.0f * std::max(0.0f, Terraced - 0.25f) / 0.75f;
	}
	float Dunes(float U, float V)
	{
		const float Wave = U * 9.0f + 0.6f * std::sin(V * 2 * Pi * 1.7f) + 0.4f * Fractal(U * 3, V * 3, 53, 2);
		const float Phase = Wave - std::floor(Wave);
		// Long windward slope, short steep lee face.
		const float Dune = Phase < 0.8f ? Phase / 0.8f : (1.0f - Phase) / 0.2f;
		return 380.0f * Dune * (0.6f + 0.4f * ValueNoise(U * 3, V * 3, 59));
	}
	float Lake(float U, float V)
	{
		const float DX = U - 0.5f, DY = (V - 0.5f) * 1.3f;
		const float R = std::sqrt(DX * DX + DY * DY) + 0.06f * (Fractal(U * 5, V * 5, 61, 3) - 0.5f);
		const float Basin = R < 0.22f ? -600.0f * Smooth(1.0f - R / 0.22f) : 0.0f;
		const float Shore = R >= 0.22f ? 700.0f * Smooth(std::min(1.0f, (R - 0.22f) / 0.2f)) : 0.0f;
		return Basin + Shore * (0.7f + 0.3f * Fractal(U * 4, V * 4, 67, 3));
	}
	float Crater(float U, float V)
	{
		const float DX = U - 0.5f, DY = V - 0.5f, R = std::sqrt(DX * DX + DY * DY);
		const float Rim = 1600.0f * std::exp(-std::pow((R - 0.28f) / 0.06f, 2.0f));
		const float Bowl = R < 0.28f ? -450.0f * Smooth(1.0f - R / 0.28f) : 0.0f;
		const float Peak = 900.0f * std::exp(-std::pow(R / 0.05f, 2.0f));
		return Rim + Bowl + Peak + 120.0f * Fractal(U * 7, V * 7, 71, 3);
	}
	float Terraces(float U, float V)
	{
		const float DX = U - 0.42f, DY = V - 0.55f;
		const float Hill = std::max(0.0f, 1.0f - std::sqrt(DX * DX + DY * DY) / 0.42f);
		const float Level = Hill * 9.0f, Floor = std::floor(Level);
		return 2000.0f * (Floor + Smooth(std::min(1.0f, (Level - Floor) * 4.0f))) / 9.0f;
	}

	struct FRegionPlan
	{
		const char* Name;
		int32 X, Y;
		FHeightField Height;
		const char* Flags; // grid server Region_<Name> flags, or null
	};
	constexpr FRegionPlan Plans[] = {
	    {"Rolling Hills", 0, 1, RollingHills, "DefaultRegion"},
	    {"Mountains", 1, 1, Mountains, nullptr},
	    {"Canyon", 2, 1, Canyon, nullptr},
	    {"Mesa", 3, 1, Mesa, nullptr},
	    {"Dunes", 0, 0, Dunes, nullptr},
	    {"Lake", 1, 0, Lake, "FallbackRegion"},
	    {"Crater", 2, 0, Crater, nullptr},
	    {"Terraces", 3, 0, Terraces, nullptr},
	};

	std::shared_ptr<const Landscape> BuildRegion(FHeightField Height)
	{
		const int32 Points = RegionQuads + 1;
		const float QuadSize = static_cast<float>(CellSize / RegionQuads);
		TArray<float> Heights(static_cast<size_t>(Points) * Points);
		for (int32 Y = 0; Y < Points; ++Y)
			for (int32 X = 0; X < Points; ++X)
			{
				const float U = static_cast<float>(X) / RegionQuads, V = static_cast<float>(Y) / RegionQuads;
				const float Edge = std::min(std::min(U, 1.0f - U), std::min(V, 1.0f - V)) * 2.0f;
				Heights[static_cast<size_t>(Y) * Points + X] = Height(U, V) * Smooth(std::min(1.0f, Edge / EdgeBand));
			}
		const std::shared_ptr<const Landscape> Bare = Landscape::Create(RegionQuads, RegionQuads, QuadSize, Heights);
		if (!Bare)
			return nullptr;
		// Paint by height and slope, so the terrain types read at a glance.
		LandscapeLayer Sand = Bare->MakeLayer("Sand", 214, 196, 140), Rock = Bare->MakeLayer("Rock", 120, 112, 104),
		               Snow = Bare->MakeLayer("Snow", 240, 244, 248), Water = Bare->MakeLayer("Lakebed", 70, 110, 150);
		for (int32 Y = 0; Y < Points; ++Y)
			for (int32 X = 0; X < Points; ++X)
			{
				const size_t I = static_cast<size_t>(Y) * Points + X;
				const float H = Heights[I];
				const int32 XA = std::max(0, X - 1), XB = std::min(RegionQuads, X + 1), YA = std::max(0, Y - 1),
				            YB = std::min(RegionQuads, Y + 1);
				const float SlopeX = (Bare->GetHeight(XB, Y) - Bare->GetHeight(XA, Y)) / ((XB - XA) * QuadSize);
				const float SlopeY = (Bare->GetHeight(X, YB) - Bare->GetHeight(X, YA)) / ((YB - YA) * QuadSize);
				const float Slope = std::sqrt(SlopeX * SlopeX + SlopeY * SlopeY);
				if (H < -SandHeight)
					Water.weights[I] = 1.0f;
				else if (H > SnowHeight)
					Snow.weights[I] = std::min(1.0f, (H - SnowHeight) / 400.0f);
				else if (Slope > RockSlope)
					Rock.weights[I] = std::min(1.0f, (Slope - RockSlope) * 4.0f);
				else if (H < SandHeight)
					Sand.weights[I] = 0.8f;
			}
		return Bare->WithLayers({Sand, Rock, Snow, Water});
	}

	FString PathIn(const std::filesystem::path& Folder, const FString& Name)
	{
		return FString::FromWide((Folder / Name.ToWide()).wstring());
	}
	int Fail(const FString& Message)
	{
		std::fprintf(stderr, "%s\n", *Message);
		return 1;
	}
} // namespace

int wmain(int Argc, wchar_t** Argv)
{
	FString ProjectPath;
	for (int I = 1; I < Argc; ++I)
	{
		const FString Arg = FString::FromWide(Argv[I]);
		if (Arg.StartsWith("-project="))
			ProjectPath = Arg.Mid(9);
	}
	if (ProjectPath.IsEmpty())
		return Fail(GENESIS_TEXT("Usage: WorldCompositionDemo -project=<folder>/WorldDemo.gproject"));
	const std::filesystem::path ProjectFile = std::filesystem::absolute(std::filesystem::path(ProjectPath.ToWide()));
	FString Error;
	TOptional<FProjectContext> Project = std::filesystem::exists(ProjectFile)
	    ? FProjectContext::Open(FString::FromWide(ProjectFile.wstring()), Error)
	    : FProjectContext::Create(
	          FString::FromWide(ProjectFile.wstring()), FString::FromWide(ProjectFile.stem().wstring()), Error);
	if (!Project)
		return Fail(Error);
	const std::filesystem::path Content(Project->GetContentDir().ToWide());
	const std::filesystem::path Worlds = Content / L"Worlds", RegionFolder = Worlds / L"WorldDemoRegions";
	const FString WorldFile = PathIn(Worlds, "WorldDemo.gmap");
	if (std::filesystem::exists(WorldFile.ToWide()))
		return Fail(FTextLocalization::Format("{} already exists; delete it and {} to rebuild the demo.", WorldFile,
		    FString::FromWide(RegionFolder.wstring())));

	FMapEditorEnvironment::FStartupOptions Options;
	Options.EditorContentDir = FPaths::Combine(FPaths::FindContentDir(), "Editor");
	if (!FMapEditorEnvironment::Startup(Options) ||
	    !FMapEditorEnvironment::Activate(FMapEditorEnvironment::GetBuiltInGameSet(), &Error))
		return Fail(FTextLocalization::Format("Could not start the map environment. {}", Error));

	std::error_code FileError;
	std::filesystem::create_directories(RegionFolder, FileError);
	TArray<FString> RegionFiles;
	for (const FRegionPlan& Plan : Plans)
	{
		const std::shared_ptr<const Landscape> Terrain = BuildRegion(Plan.Height);
		TUniquePtr<FMapDocument> Level = FMapDocument::CreateNew();
		const FString File = PathIn(RegionFolder, FString::Format("{}.gmap", Plan.Name));
		if (!Terrain || !Level->AddLandscape(Terrain, Vector3d()) || !Level->Save(File, Error))
			return Fail(FTextLocalization::Format("Could not write region {}: {}", Plan.Name, Error));
		std::printf("%s\n",
		    *FTextLocalization::Format(
		        "Wrote {} ({:.0f} to {:.0f} units high)", File, Terrain->GetMinHeight(), Terrain->GetMaxHeight()));
		RegionFiles.push_back(File);
	}

	FAssetRegistry Registry;
	if (!Registry.SetMounts({{"/Game", Project->GetContentDir()}, {"/Engine", FPaths::FindContentDir()}}, Error))
		return Fail(Error);
	Registry.Scan();
	FWorldComposition World;
	if (!World.SetCellSize(CellSize, Error))
		return Fail(Error);
	for (size_t I = 0; I < std::size(Plans); ++I)
	{
		const FAssetData* Map = Registry.FindByFile(RegionFiles[I]);
		if (!Map)
			return Fail(FTextLocalization::Format("{} is not in the project's content.", RegionFiles[I]));
		if (!World.AddTile(
		        FWorldCompositionTile::New(Plans[I].Name, Map->GetReference(), Plans[I].X, Plans[I].Y), Error))
			return Fail(Error);
	}
	FAssetPackage Package;
	World.ToPackage(Package);
	if (!FAssetPackageStore::Save(WorldFile, Package, Error))
		return Fail(Error);
	std::printf("%s\n", *FTextLocalization::Format("Wrote {} with {} regions", WorldFile, World.GetTiles().size()));

	// The same regions as grid server region files, as OpenSim's Regions/*.ini.
	const std::filesystem::path ServerFolder = ProjectFile.parent_path() / L"GridServer";
	std::filesystem::create_directories(ServerFolder / L"Regions", FileError);
	FString ServerConfig = FString::Format("; Grid server for the world composition demo.\n"
	                                       "[GridServer]\nProject = ../{}\nWorld = /Game/Worlds/WorldDemo\n"
	                                       "Regions = Regions\n\n[GridService]\nAutoRegistration = true\n",
	    FString::FromWide(ProjectFile.filename().wstring()));
	for (size_t I = 0; I < std::size(Plans); ++I)
	{
		const FWorldCompositionTile& Tile = World.GetTiles()[I];
		if (Plans[I].Flags)
			ServerConfig += FString::Format("Region_{} = {}\n", Tile.Name, Plans[I].Flags);
		const FString Region = FString::Format(
		    "[{}]\nRegionUUID = {}\nLocation = {},{}\nMap = {}\n", Tile.Name, Tile.Id, Tile.X, Tile.Y, Tile.Map.Path);
		if (!FFile::WriteAllText(PathIn(ServerFolder / L"Regions", FString::Format("{}.ini", Tile.Name)), Region))
			return Fail(GENESIS_TEXT("Could not write the grid server region files."));
	}
	if (!FFile::WriteAllText(PathIn(ServerFolder, "GridServer.ini"), ServerConfig))
		return Fail(GENESIS_TEXT("Could not write the grid server configuration."));
	std::printf(
	    "%s\n", *FTextLocalization::Format("Wrote grid server files to {}", FString::FromWide(ServerFolder.wstring())));
	FMapEditorEnvironment::Shutdown();
	return 0;
}
