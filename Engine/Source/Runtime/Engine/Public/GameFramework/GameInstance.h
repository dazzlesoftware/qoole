// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "GameFramework/CharacterMovement.h"
#include "GameFramework/SaveGame.h"
#include "Logging/Log.h"
#include "World/GameWorld.h"

DECLARE_LOG_CATEGORY_EXTERN(LogGame, Log);

// A running game, as Unreal's UGameInstance: the world, the player, their
// input, quick save and load, and the HUD's text. It has no rendering or
// windowing, so the standalone game and Play-in-Editor share it, and tests
// drive it directly.
class FGameInstance
{
public:
	// Opens a startup world (a level or a world composition) and places the
	// player at its player start. Quick saves go to SaveFile.
	static TUniquePtr<FGameInstance> Open(std::shared_ptr<FAssetRegistry> Registry, const FAssetReference& World,
	    const FString& SaveFile, FString& Error);
	// Plays a level that need not be saved, such as the one open in the editor.
	static TUniquePtr<FGameInstance> OpenLevel(std::shared_ptr<FAssetRegistry> Registry, const FAssetPackage& Level,
	    const FString& Name, const FString& SaveFile, FString& Error);

	// Moves the player by this frame's input, then streams and rebases the world.
	void Tick(float DeltaSeconds);
	FCharacterInput Input; // this frame's movement input
	void ToggleFlying();
	bool QuickSave(FString& Error);
	bool QuickLoad(FString& Error);

	const FGameWorld& GetWorld() const
	{
		return *World;
	}
	FGameWorld& GetWorld()
	{
		return *World;
	}
	FCharacterMovement Player;
	FRegionStateStore RegionState;
	const FString& GetWorldName() const
	{
		return WorldName;
	}
	const FString& GetSaveFile() const
	{
		return SaveFile;
	}
	int32 GetRebases() const
	{
		return Rebases;
	}
	int32 GetRegionsEntered() const
	{
		return RegionsEntered;
	}
	int64 GetTicks() const
	{
		return Ticks;
	}
	// The last action's result ("Saved", an error), shown on the HUD.
	const FString& GetStatus() const
	{
		return Status;
	}
	void SetStatus(const FString& Message)
	{
		Status = Message;
	}
	FString DescribeLocation() const;
	FString GetHudText(float FramesPerSecond) const;

private:
	FGameInstance() = default;
	bool Start(std::shared_ptr<FAssetRegistry> InRegistry, TUniquePtr<FGameWorld> InWorld, const FString& Name,
	    const FString& InSaveFile, FString& Error);
	std::shared_ptr<FAssetRegistry> Registry;
	TUniquePtr<FGameWorld> World;
	FString WorldName, SaveFile, Status, CurrentRegion;
	int32 Rebases = 0, RegionsEntered = 0;
	int64 Ticks = 0;
};
