// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "WorldCompositionEditing.h"
#include "Widgets/WidgetDelegates.h"
class SWindow;

// The application supplies map creation and opening. WorldBrowser owns
// registering, arranging and saving regions, with no MapEditor dependency.
struct FWorldCompositionEditorArgs
{
	std::shared_ptr<FAssetRegistry> Registry;
	FAssetReference Composition;
	FString Name;
	FCreateRegionMap OnCreateRegionMap;
	TDelegate<void(const FAssetData&)> OnOpenRegion;
	FSimpleDelegate OnSaved;
	TDelegate<void(const FString&)> OnLog;
};
class FWorldCompositionEditor
{
public:
	static TSharedPtr<SWindow> Open(const FWorldCompositionEditorArgs&, FString& Error);
};
