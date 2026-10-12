// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Starts and stops the map core (MapCore) for the editor, and manages the
// "game sets" a map is edited with: a game (entity definitions, textures)
// and the entity list file to use with it. There is one, the built-in
// Genesis game (GenesisGame), whose entity set and wireframes are in
// Content/Editor; the Qoole-era games were removed in milestone 6, and
// their maps open as Genesis maps.
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
	};

	static bool Startup(const FStartupOptions& Options);
	static void Shutdown();
	static bool IsStarted();

	static const TArray<FMapGameSet>& GetGameSets();
	static const FMapGameSet& GetBuiltInGameSet();
	// By game and entity list file (a .qle header names both); case-insensitive.
	static const FMapGameSet* FindGameSet(const FString& Game, const FString& EntFile);

	// Makes the set's game and entity list current. Fails (with a reason)
	// when the game cannot initialize.
	static bool Activate(const FMapGameSet& GameSet, FString* OutError = nullptr);
	static const FMapGameSet* GetActiveGameSet();

	// Prefabs: the built-in brushes (Content/Editor/brushes/*.qle), then the
	// user's own (*.qle in the user prefab folder), by name.
	struct FPrefab
	{
		FString Name;
		FString Path;
		bool bUser = false;
	};
	static const TArray<FPrefab>& GetPrefabs();
	// Where prefabs saved from the editor go; scanned with the built-in ones.
	static void SetUserPrefabFolder(const FString& Folder);
	static const FString& GetUserPrefabFolder();
	// Scans the prefab folders again (after a prefab was saved).
	static void RefreshPrefabs();

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
