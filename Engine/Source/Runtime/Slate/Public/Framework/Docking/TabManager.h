// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Docking (docs/GenesisUI.md §9).
//
// FTabManager owns tab spawners (tab id -> function making the tab) and the
// live dock areas. A layout is a tree of areas (one per window: the primary
// area and floating windows), splitters (orientation + size coefficients)
// and stacks of tabs; it is built with FDockLayout, restored by the tab
// manager into widgets, and persisted back (to JSON) with the user's changes.
// Tabs drag between stacks, onto the edges of a stack to split it, or out of
// every window into a new floating window.
//
//   TabManager->RegisterTabSpawner("Outliner", [](const FSpawnTabArgs&) { return SNew(SDockTab).Label("Outliner")[...]; })
//       .SetDisplayName("Outliner");
//   TSharedRef<FDockLayout> Default = FDockLayout::New("Editor", 1)->AddArea(
//       FDockLayout::NewPrimaryArea()->SetOrientation(EOrientation::Horizontal)
//           ->Split(FDockLayout::NewStack()->SetSizeCoefficient(0.25f)->AddTab("Outliner"))
//           ->Split(FDockLayout::NewStack()->AddTab("Viewport")));
//   Window->SetContent(TabManager->RestoreFrom(TabManager->LoadLayout(SavedJson, Default), Window));
//   ...
//   FString Json = TabManager->PersistLayout()->ToJson().ToString();

#pragma once

#include "Framework/MultiBox/MenuRegistry.h"
#include "Widgets/SWindow.h"

class FTabManager;
class SDockTab;
class SDockTabStack;
class SDockingSplitter;
class SDockingArea;

// --- Layout description --------------------------------------------------------

enum class ETabState : uint8
{
	Opened,
	Closed, // remembered so the tab reopens where it was
};

class FDockLayoutNode : public TSharedFromThis<FDockLayoutNode>
{
public:
	virtual ~FDockLayoutNode() = default;
	float SizeCoefficient = 1.0f;
	virtual bool IsStack() const { return false; }
};

class FDockStackLayout final : public FDockLayoutNode
{
public:
	struct FTab
	{
		FName TabId;
		ETabState State = ETabState::Opened;
	};

	TArray<FTab> Tabs;
	FName ForegroundTab;

	bool IsStack() const override { return true; }
	TSharedRef<FDockStackLayout> AddTab(FName TabId, ETabState State = ETabState::Opened);
	TSharedRef<FDockStackLayout> SetForegroundTab(FName TabId);
	TSharedRef<FDockStackLayout> SetSizeCoefficient(float Coefficient);
};

class FDockSplitterLayout : public FDockLayoutNode
{
public:
	EOrientation Orientation = EOrientation::Horizontal;
	TArray<TSharedRef<FDockLayoutNode>> Children;

	TSharedRef<FDockSplitterLayout> Split(const TSharedRef<FDockLayoutNode>& Child);
	TSharedRef<FDockSplitterLayout> SetOrientation(EOrientation InOrientation);
	TSharedRef<FDockSplitterLayout> SetSizeCoefficient(float Coefficient);
};

class FDockAreaLayout final : public FDockSplitterLayout
{
public:
	bool bPrimary = false;
	TOptional<FIntPoint> WindowPosition; // floating: client top-left, screen pixels
	FVector2 WindowSize = FVector2(600.0f, 400.0f); // floating: client size, logical units

	TSharedRef<FDockAreaLayout> Split(const TSharedRef<FDockLayoutNode>& Child);
	TSharedRef<FDockAreaLayout> SetOrientation(EOrientation InOrientation);
	TSharedRef<FDockAreaLayout> SetWindow(TOptional<FIntPoint> Position, FVector2 Size);
};

class FDockLayout : public TSharedFromThis<FDockLayout>
{
public:
	FName Name;
	int32 Version = 1; // a saved layout with another version is ignored
	TArray<TSharedRef<FDockAreaLayout>> Areas; // the primary area first

	static TSharedRef<FDockLayout> New(FName Name, int32 Version);
	static TSharedRef<FDockAreaLayout> NewPrimaryArea();
	static TSharedRef<FDockAreaLayout> NewFloatingArea();
	static TSharedRef<FDockSplitterLayout> NewSplitter();
	static TSharedRef<FDockStackLayout> NewStack();

	TSharedRef<FDockLayout> AddArea(const TSharedRef<FDockAreaLayout>& Area);
	TSharedPtr<FDockAreaLayout> GetPrimaryArea() const;

	FJsonValue ToJson() const;
	// Nothing if the JSON is not a layout.
	static TSharedPtr<FDockLayout> FromJson(const FJsonValue& Json);
};

// --- Tabs ------------------------------------------------------------------------

enum class ETabRole : uint8
{
	Panel,    // tools: outliner, details, log
	Document, // what is edited: viewports, documents
};

using FOnTabClosed = TDelegate<void(const TSharedRef<SDockTab>&)>;
using FCanCloseTab = TDelegate<bool()>;

// One tab: its label and content. Spawned by a tab spawner and placed in a
// stack by the tab manager.
class SDockTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDockTab)
		SLATE_ATTRIBUTE(FString, Label, "")
		SLATE_ARGUMENT(ETabRole, Role, ETabRole::Panel)
		SLATE_ARGUMENT(bool, Closable, true)
		SLATE_EVENT(FOnTabClosed, OnTabClosed)
		SLATE_EVENT(FCanCloseTab, CanClose)
		SLATE_DEFAULT_SLOT(Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

	FName GetTabId() const { return TabId; }
	FString GetLabel() const { return Label.Get(); }
	void SetLabel(TAttribute<FString> InLabel) { Label.Assign(InLabel); }
	void SetOnTabClosed(FOnTabClosed Delegate) { OnTabClosed = std::move(Delegate); }
	ETabRole GetRole() const { return Role; }
	bool IsClosable() const { return bClosable; }
	TSharedRef<SWidget> GetContent() const { return ChildSlot.GetWidget(); }
	void SetContent(const TSharedRef<SWidget>& Content) { ChildSlot[Content]; }

	TSharedPtr<SDockTabStack> GetParentStack() const { return ParentStack.lock(); }
	bool IsForeground() const;
	bool IsActive() const; // the foreground tab of the active stack

	// Brings the tab to the front of its stack and makes it the active tab.
	void ActivateInParent();
	// Closes the tab unless CanClose says no. Returns whether it closed.
	bool RequestCloseTab();

	// Used by the tab manager.
	void SetTabId(FName InTabId) { TabId = InTabId; }
	void SetParentStack(const TSharedPtr<SDockTabStack>& Stack) { ParentStack = Stack; }
	void SetTabManager(const TSharedPtr<FTabManager>& Manager) { TabManager = Manager; }
	TSharedPtr<FTabManager> GetTabManager() const { return TabManager.lock(); }
	void NotifyClosed();

private:
	FName TabId;
	TWidgetAttribute<FString, EInvalidateWidgetReason::Layout> Label{ *this };
	ETabRole Role = ETabRole::Panel;
	bool bClosable = true;
	FOnTabClosed OnTabClosed;
	FCanCloseTab CanClose;
	TWeakPtr<SDockTabStack> ParentStack;
	TWeakPtr<FTabManager> TabManager;
};

// --- Tab manager -------------------------------------------------------------------

struct FSpawnTabArgs
{
	FName TabId;
	TSharedPtr<SWindow> OwnerWindow;
};

using FOnSpawnTab = TFunction<TSharedRef<SDockTab>(const FSpawnTabArgs&)>;

class FTabSpawnerEntry
{
public:
	FTabSpawnerEntry(FName InTabId, FOnSpawnTab InSpawn) : TabId(InTabId), Spawn(std::move(InSpawn)), DisplayName(InTabId.ToString()) {}

	FTabSpawnerEntry& SetDisplayName(FString Name) { DisplayName = std::move(Name); return *this; }
	FTabSpawnerEntry& SetToolTip(FString Text) { ToolTip = std::move(Text); return *this; }
	// The Window menu groups spawners by this name.
	FTabSpawnerEntry& SetGroup(FString Group) { GroupName = std::move(Group); return *this; }
	// Hidden from the Window menu (documents opened another way).
	FTabSpawnerEntry& SetMenuHidden(bool bHidden) { bMenuHidden = bHidden; return *this; }

	FName TabId;
	FOnSpawnTab Spawn;
	FString DisplayName;
	FString ToolTip;
	FString GroupName;
	bool bMenuHidden = false;
};

class FTabManager : public TSharedFromThis<FTabManager>
{
public:
	static TSharedRef<FTabManager> Create();

	FTabSpawnerEntry& RegisterTabSpawner(FName TabId, FOnSpawnTab Spawn);
	void UnregisterTabSpawner(FName TabId);
	const FTabSpawnerEntry* FindSpawner(FName TabId) const;

	// Builds the layout: returns the primary area's widget (put it in the
	// main window) and opens a window per floating area.
	TSharedRef<SWidget> RestoreFrom(const TSharedRef<FDockLayout>& Layout, const TSharedPtr<SWindow>& PrimaryWindow);
	// The live layout, including tabs that were closed (to reopen in place).
	TSharedRef<FDockLayout> PersistLayout() const;
	// Saved JSON if it parses and has Default's name and version; otherwise Default.
	static TSharedRef<FDockLayout> LoadLayout(const FString& SavedJson, const TSharedRef<FDockLayout>& Default);
	// Closes every tab and floating window (before restoring another layout).
	void CloseAllAreas();

	// Opens the tab (where it was last, or in the primary area) or brings it
	// to the front if it is open.
	TSharedPtr<SDockTab> TryInvokeTab(FName TabId);
	TSharedPtr<SDockTab> FindExistingLiveTab(FName TabId) const;
	TArray<TSharedRef<SDockTab>> GetLiveTabs() const;

	TSharedPtr<SDockTab> GetActiveTab() const { return ActiveTab.lock(); }
	TSharedPtr<SDockingArea> GetPrimaryArea() const { return PrimaryArea.lock(); }
	const TArray<TWeakPtr<SDockingArea>>& GetAreas() const { return Areas; }

	// Adds a "Window" section of open/close entries, one per spawner.
	void PopulateTabSpawnerMenu(FMenuSection& Section);

	TMulticastDelegate<void(const TSharedPtr<SDockTab>&)> OnActiveTabChanged;
	// The layout changed (a tab moved, opened or closed, a split resized...).
	TMulticastDelegate<void()> OnLayoutChanged;

	// --- Used by the docking widgets ---

	void SetActiveTab(const TSharedPtr<SDockTab>& Tab);
	void OnTabClosed(const TSharedRef<SDockTab>& Tab, const TSharedRef<SDockTabStack>& Stack);
	// Moves a tab into a stack at an index.
	void MoveTabToStack(const TSharedRef<SDockTab>& Tab, const TSharedRef<SDockTabStack>& Target, int32 Index);
	// Splits a stack, putting the tab in a new stack on one side.
	void MoveTabToNewStack(const TSharedRef<SDockTab>& Tab, const TSharedRef<SDockTabStack>& Beside, EUINavigation Side);
	// Moves a tab into a new floating window at a screen position.
	void MoveTabToNewWindow(const TSharedRef<SDockTab>& Tab, FIntPoint ScreenPosition, FVector2 LogicalSize);
	// A stack lost its last tab.
	void OnStackEmptied(const TSharedRef<SDockTabStack>& Stack);
	void NotifyLayoutChanged() { OnLayoutChanged.Broadcast(); }

private:
	TSharedRef<SDockTab> SpawnTab(FName TabId, const TSharedPtr<SWindow>& Window);
	TSharedRef<SWidget> RestoreNode(const TSharedRef<FDockLayoutNode>& Node, const TSharedPtr<SWindow>& Window);
	TSharedRef<SDockingArea> RestoreArea(const TSharedRef<FDockAreaLayout>& Layout, const TSharedPtr<SWindow>& Window);
	TSharedRef<SDockingArea> OpenFloatingWindow(const TSharedRef<FDockAreaLayout>& Layout);
	void RemoveTabFromStack(const TSharedRef<SDockTab>& Tab);
	TSharedPtr<SDockTabStack> FindStackRemembering(FName TabId) const;
	TSharedPtr<SDockTabStack> FindAnyStack() const;

	TArray<FTabSpawnerEntry> Spawners;
	TArray<TWeakPtr<SDockingArea>> Areas;
	TWeakPtr<SDockingArea> PrimaryArea;
	TWeakPtr<SDockTab> ActiveTab;
	FName LayoutName;
	int32 LayoutVersion = 1;
	bool bRestoring = false;
};
