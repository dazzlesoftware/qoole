// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// The widget gallery (milestone 3 of docs/GenesisUI.md): every built-in
// widget on a page of its own, plus the milestone 2 draw-element showcase.

#pragma once

#include "SandboxContent.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SOverlay.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class FSandboxApp;

// What the gallery needs from the application around it.
struct FGalleryHost
{
	FSandboxContent* Content = nullptr;
	TSharedPtr<FUIRenderTarget>* ViewportTarget = nullptr;
	TFunction<FSandboxFrameInfo()> GetFrameInfo;     // header line and the showcase
	TFunction<void(float)> SetAppScale;
	TFunction<void()> ToggleInspector;
	TFunction<void()> Quit;
};

class SWidgetGallery : public SCompoundWidget
{
public:
	GE_BEGIN_ARGS(SWidgetGallery)
	GE_END_ARGS()

	void Construct(const FArguments& Args, FGalleryHost InHost);

	int32 GetPageCount() const { return static_cast<int32>(PageNames.size()); }
	const FString& GetPageName(int32 Index) const { return PageNames[Index]; }
	void SetPage(int32 Index);
	int32 GetPage() const { return Switcher->GetActiveWidgetIndex(); }

	// Widgets the self-test drives.
	TSharedPtr<SComboButton> FileMenu;
	TSharedPtr<SWidget> ToolTipDemo;

	void SetLastEvent(const FString& Event) { LastEvent = Event; }

private:
	TSharedRef<SWidget> MakeHeader();
	TSharedRef<SWidget> MakeStatusBar();
	TSharedRef<SWidget> MakeButtonsPage();
	TSharedRef<SWidget> MakeTextPage();
	TSharedRef<SWidget> MakeLayoutPage();
	TSharedRef<SWidget> MakeViewsPage();
	TSharedRef<SWidget> MakeMenusPage();
	TSharedRef<SWidget> MakeDrawPage();
	TSharedRef<SWidget> MakeMenuContent(const FString& MenuName);

	FGalleryHost Host;
	TSharedPtr<SWidgetSwitcher> Switcher;
	TSharedPtr<SListView<int32>> Nav;
	TArray<FString> PageNames;
	TArray<int32> PageIndices;
	FString LastEvent = "Ready";
	int32 Clicks = 0;
	ECheckBoxState LayerA = ECheckBoxState::Checked;
	ECheckBoxState LayerB = ECheckBoxState::Unchecked;
	FString LastCommit = "(nothing committed yet)";

	struct FGalleryItem
	{
		int32 Index = 0;
		FString Name;
	};
	struct FMapNode
	{
		FString Name;
		FString Kind;
		TArray<TSharedPtr<FMapNode>> Children;
	};
	TArray<TSharedPtr<FGalleryItem>> ListItems;
	TArray<TSharedPtr<FMapNode>> TreeRoots;
	TFunction<int32()> GetSelectedCount;
};
