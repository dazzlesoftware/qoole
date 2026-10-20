// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// SPlaceActors: Unreal's Place Actors panel. Categories down the left
// (Recent, Basic, Lights, Shapes, Geometry, Volumes, All; those with
// nothing in them are left out), the category's items on the right with a
// search box over them. Drag an item into a viewport to place it under the
// cursor, or double-click it to place it where the active view looks.

#pragma once

#include "MapPlacement.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class SPlaceActors final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPlaceActors)
		SLATE_EVENT(TDelegate<void(const FMapPlaceable&)>, OnPlace)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

	// The placeable items (the categories follow their Category).
	void SetItems(TArray<FMapPlaceable> InItems);
	const TArray<FMapPlaceable>& GetItems() const { return Items; }
	// Puts an item first in Recent.
	void NoteRecent(const FMapPlaceable& Item);

	void SetCategory(const FString& Category);
	const FString& GetCategory() const { return Category; }
	TArray<FString> GetCategories() const;
	void SetSearchText(const FString& Text);
	// The items shown (category and search applied), for tests.
	TArray<FMapPlaceable> GetShownItems() const;

	static constexpr int32 MaxRecent = 20;

private:
	void Refresh();
	TSharedRef<SWidget> MakeCategoryBar();

	TArray<FMapPlaceable> Items;
	TArray<FMapPlaceable> Recent;
	TArray<FMapPlaceable> Shown;
	TArray<int32> ShownIndices;
	FString Category = "Basic";
	FString PreferredCategory = "Basic";
	FString SearchText;
	TDelegate<void(const FMapPlaceable&)> OnPlace;
	TSharedPtr<SListView<int32>> List;
	TSharedPtr<class SBox> CategoryHost;
	TArray<FString> BarCategories;
};
