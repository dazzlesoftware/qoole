// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// The named menu registry (Engine/Documentation/Source/GenesisUI.md §9). Menus and toolbars are
// lists of sections and entries registered by name ("MainMenu.File",
// "MapView.Context", "LevelEditor.ToolBar"), built into widgets lazily when
// they open, and extendable by name from anywhere, even before the owner
// registers them.
//
//   FMenuDefinition& File = FMenuRegistry::Get().RegisterMenu("MainMenu.File");
//   FMenuSection& Section = File.AddSection("FileIO", "File");
//   Section.AddCommand(Commands.Save);
//   Section.AddSubMenu("Recent", "Open recent", "", [](FMenuSection& Sub, const FMenuContext&) { ... });
//
//   // A plug-in elsewhere:
//   FMenuRegistry::Get().ExtendMenu("MainMenu.File").FindOrAddSection("FileIO")
//       .AddEntry("Export", "Export...", "Write the map as OBJ", FUIAction(...)).InsertAfter("Save");
//
// A menu bar is a menu whose entries are submenus ("MainMenu" with
// "MainMenu.File", "MainMenu.Edit"...). Commands read their label, tooltip,
// shortcut, enabled and check state from the command lists in the
// FMenuContext the menu is generated with.

#pragma once

#include "Framework/Commands/UICommands.h"

struct FMenuContext
{
	TArray<TSharedRef<FUICommandList>> CommandLists;
	// Anything the menu's dynamic sections need (the selection, the asset...).
	TSharedPtr<void> Payload;

	FMenuContext() = default;
	explicit FMenuContext(TSharedRef<FUICommandList> List) { CommandLists.push_back(std::move(List)); }

	// The first list that maps Command.
	TSharedPtr<FUICommandList> FindListFor(const FUICommandInfo& Command) const;
};

class FMenuSection;
using FMenuSectionBuilder = TFunction<void(FMenuSection& Section, const FMenuContext& Context)>;

enum class EMenuEntryKind : uint8
{
	Command,
	Action,
	SubMenu,
	Separator,
	Widget,
};

enum class EMenuInsertPosition : uint8
{
	Default, // at the end
	First,
	Before,
	After,
};

struct FMenuInsert
{
	FName Name;
	EMenuInsertPosition Position = EMenuInsertPosition::Default;
};

struct FMenuEntry
{
	FName Name;
	EMenuEntryKind Kind = EMenuEntryKind::Action;
	FMenuInsert Insert;

	// Command entries (label/tooltip below override the command's).
	TSharedPtr<FUICommandInfo> Command;
	TSharedPtr<FUICommandList> CommandList; // overrides the context's lists

	// Action entries.
	FString Label;
	FString ToolTip;
	FName Icon;
	FUIAction Action;
	EUserInterfaceActionType ActionType = EUserInterfaceActionType::Button;

	// Submenus: a registered menu or a builder.
	FName SubMenuName;
	FMenuSectionBuilder SubMenuBuilder;

	// Widget entries.
	TFunction<TSharedRef<SWidget>(const FMenuContext&)> MakeWidget;

	FMenuEntry& InsertBefore(FName Other) { Insert = { Other, EMenuInsertPosition::Before }; return *this; }
	FMenuEntry& InsertAfter(FName Other) { Insert = { Other, EMenuInsertPosition::After }; return *this; }
	FMenuEntry& InsertFirst() { Insert = { NAME_None, EMenuInsertPosition::First }; return *this; }

	// Resolved for display, given the menu's context.
	FString GetLabel() const { return !Label.IsEmpty() || !Command ? Label : Command->GetLabel(); }
	FString GetToolTip() const { return !ToolTip.IsEmpty() || !Command ? ToolTip : Command->GetDescription(); }
	EUserInterfaceActionType GetActionType() const { return Command ? Command->GetActionType() : ActionType; }
	bool IsEnabled(const FMenuContext& Context) const;
	bool IsVisible(const FMenuContext& Context) const;
	ECheckBoxState GetCheckState(const FMenuContext& Context) const;
	void Run(const FMenuContext& Context) const;
};

class FMenuSection
{
public:
	FMenuSection() = default;
	FMenuSection(FName InName, FString InLabel) : Name(InName), Label(std::move(InLabel)) {}

	FName Name;
	FString Label;        // shown as a heading in menus (optional)
	FMenuInsert Insert;   // where the section goes among others
	FMenuSectionBuilder Dynamic; // fills the section each time the menu opens
	TArray<FMenuEntry> Entries;

	FMenuEntry& AddCommand(const TSharedPtr<FUICommandInfo>& Command, FString LabelOverride = FString(), TSharedPtr<FUICommandList> CommandList = nullptr);
	FMenuEntry& AddEntry(FName EntryName, FString EntryLabel, FString ToolTip, FUIAction Action, EUserInterfaceActionType Type = EUserInterfaceActionType::Button);
	FMenuEntry& AddSubMenu(FName EntryName, FString EntryLabel, FString ToolTip, FMenuSectionBuilder Builder);
	FMenuEntry& AddSubMenu(FName EntryName, FString EntryLabel, FString ToolTip, FName RegisteredMenu);
	FMenuEntry& AddSeparator(FName EntryName = NAME_None);
	FMenuEntry& AddWidget(FName EntryName, TFunction<TSharedRef<SWidget>(const FMenuContext&)> MakeWidget);

	FMenuEntry* FindEntry(FName EntryName);

private:
	FMenuEntry& AddEntryInternal(FMenuEntry Entry);
};

class FMenuDefinition
{
public:
	FName Name;
	FName Parent; // sections of the parent come first
	bool bRegistered = false; // false while it only has extensions
	TArray<FMenuSection> Sections;

	FMenuSection& AddSection(FName SectionName, FString Label = FString(), FMenuInsert Insert = FMenuInsert());
	FMenuSection& AddDynamicSection(FName SectionName, FMenuSectionBuilder Builder, FMenuInsert Insert = FMenuInsert());
	FMenuSection* FindSection(FName SectionName);
	FMenuSection& FindOrAddSection(FName SectionName);
};

class FMenuRegistry
{
public:
	static FMenuRegistry& Get();

	// Registers (or re-registers, keeping extensions) a menu.
	FMenuDefinition& RegisterMenu(FName MenuName, FName Parent = NAME_None);
	// The menu to add to; created empty (and unregistered) if needed.
	FMenuDefinition& ExtendMenu(FName MenuName);
	bool IsMenuRegistered(FName MenuName) const;
	void RemoveMenu(FName MenuName);
	void RemoveAll();

	// The menu as it will be shown: parents first, sections and entries in
	// their insert order, dynamic sections filled, hidden entries dropped.
	TArray<FMenuSection> ResolveMenu(FName MenuName, const FMenuContext& Context) const;

	// Widgets.
	TSharedRef<SWidget> GenerateMenuWidget(FName MenuName, const FMenuContext& Context) const;
	TSharedRef<SWidget> GenerateMenuWidget(const TArray<FMenuSection>& Sections, const FMenuContext& Context) const;
	TSharedRef<SWidget> GenerateMenuBar(FName MenuName, const FMenuContext& Context) const;
	TSharedRef<SWidget> GenerateToolBar(FName MenuName, const FMenuContext& Context) const;

private:
	TMap<FName, TSharedPtr<FMenuDefinition>> Menus;
};
