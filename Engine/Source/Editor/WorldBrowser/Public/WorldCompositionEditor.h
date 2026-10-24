// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "WorldCompositionEditing.h"
#include "Widgets/WidgetDelegates.h"
class SWindow;

// A region's map and where its origin lies in the world, for a combined preview.
struct FWorldPreviewRegion
{
	FString Name;
	FAssetData Map;
	Vector3d Origin;
};

// The application supplies map creation and opening. WorldBrowser owns
// registering, arranging and saving regions, with no MapEditor dependency.
struct FWorldCompositionEditorArgs
{
	std::shared_ptr<FAssetRegistry> Registry;
	FAssetReference Composition;
	FString Name;
	FCreateRegionMap OnCreateRegionMap;
	TDelegate<void(const FAssetData&)> OnOpenRegion;
	// Shows every region's level together, each at its place on the grid.
	TDelegate<void(const TArray<FWorldPreviewRegion>&)> OnPreviewWorld;
	// Plays this world in the game (as saved).
	TDelegate<void(const FAssetReference&)> OnPlayWorld;
	// Makes this world the project's startup world.
	TDelegate<bool(const FAssetReference&, FString&)> OnSetStartupWorld;
	FSimpleDelegate OnSaved;
	TDelegate<void(const FString&)> OnLog;
};
class FWorldCompositionEditor
{
public:
	static TSharedPtr<SWindow> Open(const FWorldCompositionEditorArgs&, FString& Error);
};
