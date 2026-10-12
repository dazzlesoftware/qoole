// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Starts and stops the map core (MapCore) for an editor that is not the MFC
// one, and manages the "game sets" a map is edited with: a game (entity
// definitions, textures, palette) and the entity list file to use with it.
//
//   - The built-in Genesis game (GenesisGame) needs no setup and is always
//     available. Its entity set and wireframes are in Content/Editor.
//   - The legacy games (Quake, Quake 2, Hexen 2, Half-Life, Sin, Heretic 2)
//     come with GenesisEditor.pak, whose gamesets.txt lists their sets.
//     Activating one asks for the game's folder the first time (through
//     FServices::GetFileDialogs()), as the MFC editor does.
//
// The map core keeps one current game and entity list (Game::Get(),
// EntList::Get()), so one game set is active at a time, and every map that
// is open must use it.
//
// Settings go through FServices::GetConfig(): set the config store before
// Startup and keep it alive until after Shutdown.

#pragma once

#include "CoreMinimal.h"

struct FMapGameSet
{
	FString Name;     // as shown, e.g. "Quake Standard"
	FString Game;     // Game::GetName(), e.g. "Quake"
	FString EntFile;  // e.g. "quake.ent"
	FString Palette;  // may be empty

	bool IsBuiltIn() const { return Game == "Genesis"; }
};

class FMapEditorEnvironment
{
public:
	struct FStartupOptions
	{
		FString EditorContentDir; // Content/Editor (genesis.ent, gems, prefab brushes)
		FString PakPath;          // GenesisEditor.pak; optional
		bool bLegacyGames = true; // register the legacy games (they need the pak)
	};

	static bool Startup(const FStartupOptions& Options);
	static void Shutdown();
	static bool IsStarted();

	// The built-in set first, then gamesets.txt from the pak.
	static const TArray<FMapGameSet>& GetGameSets();
	static const FMapGameSet& GetBuiltInGameSet();
	// By game and entity list file (a .qle header names both); case-insensitive.
	static const FMapGameSet* FindGameSet(const FString& Game, const FString& EntFile);

	// Makes the set's game and entity list current. Fails (with a reason)
	// when the game cannot initialize, e.g. its folder was not given.
	static bool Activate(const FMapGameSet& GameSet, FString* OutError = nullptr);
	static const FMapGameSet* GetActiveGameSet();

	// Prefab brushes (Content/Editor/brushes/*.qle): display name and path.
	struct FPrefab
	{
		FString Name;
		FString Path;
	};
	static const TArray<FPrefab>& GetPrefabs();

	// Entity classes of the active entity list that can be placed (point
	// entities: not worldspawn, not brush entities), by category.
	struct FEntityClass
	{
		FString ClassName; // e.g. "light"
		FString Category;  // e.g. "Light"
		FString Label;     // e.g. "Point Light"
	};
	static TArray<FEntityClass> GetPlaceableEntities();
};
