// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "GameFramework/GameInstance.h"
#include "Internationalization/TextLocalization.h"

DEFINE_LOG_CATEGORY(LogGame, Log);

namespace
{
	// Regions load within one cell of the player and unload past one and a half.
	constexpr double LoadCells = 1.0, UnloadCells = 1.5;
} // namespace

TUniquePtr<FGameInstance> FGameInstance::Open(
    std::shared_ptr<FAssetRegistry> Registry, const FAssetReference& World, const FString& SaveFile, FString& Error)
{
	TUniquePtr<FGameWorld> Opened = FGameWorld::Open(Registry, World, Error);
	if (!Opened)
		return {};
	const FAssetData* Data = Registry->Resolve(World, Error);
	TUniquePtr<FGameInstance> Result(new FGameInstance());
	if (!Result->Start(std::move(Registry), std::move(Opened), Data ? Data->PackagePath : World.Path, SaveFile, Error))
		return {};
	return Result;
}

TUniquePtr<FGameInstance> FGameInstance::OpenLevel(std::shared_ptr<FAssetRegistry> Registry, const FAssetPackage& Level,
    const FString& Name, const FString& SaveFile, FString& Error)
{
	TUniquePtr<FGameWorld> Opened = FGameWorld::OpenLevel(Registry, Level, Name, Error);
	if (!Opened)
		return {};
	TUniquePtr<FGameInstance> Result(new FGameInstance());
	if (!Result->Start(std::move(Registry), std::move(Opened), Name, SaveFile, Error))
		return {};
	return Result;
}

bool FGameInstance::Start(std::shared_ptr<FAssetRegistry> InRegistry, TUniquePtr<FGameWorld> InWorld,
    const FString& Name, const FString& InSaveFile, FString& Error)
{
	Registry = std::move(InRegistry);
	World = std::move(InWorld);
	WorldName = Name;
	SaveFile = InSaveFile;
	FWorldCompositionStreamingSettings Streaming;
	Streaming.LoadDistance = World->GetComposition().GetCellSize() * LoadCells;
	Streaming.UnloadDistance = World->GetComposition().GetCellSize() * UnloadCells;
	if (!World->SetStreamingSettings(Streaming, Error))
		return false;
	// Stream in around the first region, then find the start among what loaded.
	World->FindPlayerStart(Player.Location);
	World->Update(Player.Location);
	World->FindPlayerStart(Player.Location);
	World->Update(Player.Location);
	CurrentRegion = Player.Location.RegionId;
	Error.Empty();
	return true;
}

void FGameInstance::Tick(float DeltaSeconds)
{
	++Ticks;
	Player.Update(*World, Input, DeltaSeconds);
	FGameWorldChanges Changes;
	World->Update(Player.Location, &Changes);
	const FWorldComposition& Composition = World->GetComposition();
	for (const FString& Id : Changes.Loaded)
		if (const FWorldCompositionTile* Tile = Composition.FindTile(Id))
			GENESIS_LOG(LogGame, Display, "Loaded region {}", Tile->Name);
	for (const FString& Id : Changes.Unloaded)
		if (const FWorldCompositionTile* Tile = Composition.FindTile(Id))
			GENESIS_LOG(LogGame, Display, "Unloaded region {}", Tile->Name);
	for (const FString& Id : Changes.Failed)
		if (const FWorldCompositionTile* Tile = Composition.FindTile(Id))
			GENESIS_LOG(LogGame, Warning, "Region {} failed to load", Tile->Name);
	if (Changes.bRebased)
	{
		++Rebases;
		GENESIS_LOG(
		    LogGame, Display, "Rebased the origin to cell {},{}", World->GetOriginCellX(), World->GetOriginCellY());
	}
	if (Player.Location.RegionId != CurrentRegion)
	{
		CurrentRegion = Player.Location.RegionId;
		++RegionsEntered;
		if (const FWorldCompositionTile* Tile = Composition.FindTile(CurrentRegion))
			GENESIS_LOG(LogGame, Display, "Entered region {}", Tile->Name);
	}
}

void FGameInstance::ToggleFlying()
{
	Player.bFlying = !Player.bFlying;
	Status = Player.bFlying ? GENESIS_TEXT("Flying") : GENESIS_TEXT("Walking");
}

bool FGameInstance::QuickSave(FString& Error)
{
	FSaveGame Save;
	Save.World = World->GetReference();
	Save.Player = Player.Location;
	World->ToAbsolute(Player.Location, Save.AbsoluteX, Save.AbsoluteY, Save.AbsoluteZ);
	Save.Yaw = Player.Yaw;
	Save.Pitch = Player.Pitch;
	Save.Regions = RegionState;
	Save.Game["Flying"] = Player.bFlying;
	if (!Save.Save(SaveFile, Error))
	{
		Status = Error;
		return false;
	}
	Status = GENESIS_TEXT("Saved");
	GENESIS_LOG(LogGame, Display, "Saved {} at {}", SaveFile, DescribeLocation());
	return true;
}

bool FGameInstance::QuickLoad(FString& Error)
{
	TOptional<FSaveGame> Save = FSaveGame::Load(SaveFile, Error);
	FWorldLocation Location;
	if (Save && Save->World.Id != World->GetReference().Id)
		Error = GENESIS_TEXT("The saved game belongs to another world.");
	else if (Save && !Save->ResolvePlayer(*World, Location))
		Error = GENESIS_TEXT("The saved position is not in this world.");
	if (!Save || !Error.IsEmpty())
	{
		Status = Error;
		return false;
	}
	Player.Location = Location;
	Player.Yaw = Save->Yaw;
	Player.Pitch = Save->Pitch;
	Player.bFlying = Save->Game.Get("Flying").AsBool();
	Player.VerticalSpeed = 0;
	RegionState = std::move(Save->Regions);
	World->Update(Player.Location);
	Status = GENESIS_TEXT("Loaded");
	GENESIS_LOG(LogGame, Display, "Loaded {} at {}", SaveFile, DescribeLocation());
	return true;
}

FString FGameInstance::DescribeLocation() const
{
	const FWorldCompositionTile* Tile = World->GetComposition().FindTile(Player.Location.RegionId);
	return FTextLocalization::Format("{} ({:.0f}, {:.0f}, {:.0f})", Tile ? Tile->Name : FString("?"),
	    Player.Location.Local.GetX(), Player.Location.Local.GetY(), Player.Location.Local.GetZ());
}

FString FGameInstance::GetHudText(float FramesPerSecond) const
{
	TArray<FGameRegion> Loaded;
	World->GetLoadedRegions(Loaded);
	double X = 0, Y = 0, Z = 0;
	World->ToAbsolute(Player.Location, X, Y, Z);
	FString Text = FTextLocalization::Format(
	    "{}\nRegion: {}\nWorld: {:.0f}, {:.0f}, {:.0f}\nOrigin cell: {}, {}   Regions loaded: {}   {:.0f} fps\n{}",
	    WorldName, DescribeLocation(), X, Y, Z, World->GetOriginCellX(), World->GetOriginCellY(), Loaded.size(),
	    FramesPerSecond, Player.bFlying ? GENESIS_TEXT("Flying") : GENESIS_TEXT("Walking"));
	if (!Status.IsEmpty())
		Text += "\n" + Status;
	return Text;
}
