// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Commands (Engine/Documentation/Source/GenesisUI.md §9). A command is defined once in a binding
// context: name, label, description, icon, kind, default key chord.
// FUICommandList maps commands to actions (execute, can-execute, check
// state, visibility). Menus, toolbars and keyboard shortcuts all read the
// same command and list, so a disabled command greys out everywhere.
//
//   auto Context = MakeShared<FBindingContext>("LevelEditor", "Level editor");
//   TSharedRef<FUICommandInfo> Save = Context->AddCommand("Save", "Save", "Save the map",
//       EUserInterfaceActionType::Button, FInputChord(EKey::S, EModifierKey::Control));
//   FInputBindingManager::Get().RegisterContext(Context);
//
//   auto Commands = MakeShared<FUICommandList>();
//   Commands->MapAction(Save, [this] { SaveMap(); }, [this] { return IsDirty(); });
//   ...
//   SNew(SCommandBindingScope).CommandList(Commands)[ Content ]   // shortcuts
//
// User key bindings are kept by FInputBindingManager and saved as JSON
// (only the chords that differ from the defaults).

#pragma once

#include "Input/Events.h"
#include "Serialization/Json.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/WidgetDelegates.h"

enum class EModifierKey : uint8
{
	None = 0,
	Shift = 1 << 0,
	Control = 1 << 1,
	Alt = 1 << 2,
};

constexpr EModifierKey operator|(EModifierKey A, EModifierKey B) { return EModifierKey(uint8(A) | uint8(B)); }
constexpr bool HasModifier(EModifierKey Value, EModifierKey Flag) { return (uint8(Value) & uint8(Flag)) != 0; }

// A key plus modifiers ("Ctrl+Shift+S").
struct FInputChord
{
	EKey Key = EKey::Invalid;
	EModifierKey Modifiers = EModifierKey::None;

	FInputChord() = default;
	FInputChord(EKey InKey, EModifierKey InModifiers = EModifierKey::None) : Key(InKey), Modifiers(InModifiers) {}

	bool IsValid() const { return Key != EKey::Invalid; }
	bool Matches(const FKeyEvent& Event) const;
	FString ToString() const;
	// The inverse of ToString; nothing for text that is not a chord.
	static TOptional<FInputChord> Parse(const FString& Text);

	bool operator==(const FInputChord&) const = default;
};

enum class EUserInterfaceActionType : uint8
{
	Button,       // does something
	ToggleButton, // on/off, shown pressed in toolbars
	RadioButton,  // one of a group
	Check,        // a check mark in menus
};

class FUICommandInfo
{
public:
	FUICommandInfo(FName InContext, FName InName, FString InLabel, FString InDescription, EUserInterfaceActionType InType, FInputChord InDefaultChord, FName InIcon)
		: Context(InContext), Name(InName), Label(std::move(InLabel)), Description(std::move(InDescription)), Type(InType), DefaultChord(InDefaultChord), ActiveChord(InDefaultChord), Icon(InIcon)
	{
	}

	FName GetContext() const { return Context; }
	FName GetName() const { return Name; }
	const FString& GetLabel() const { return Label; }
	const FString& GetDescription() const { return Description; }
	EUserInterfaceActionType GetActionType() const { return Type; }
	FName GetIcon() const { return Icon; }
	const FInputChord& GetDefaultChord() const { return DefaultChord; }
	const FInputChord& GetActiveChord() const { return ActiveChord; }
	// The shortcut as menus show it ("Ctrl+S"), or empty.
	FString GetInputText() const { return ActiveChord.IsValid() ? ActiveChord.ToString() : FString(); }

private:
	friend class FInputBindingManager;

	FName Context;
	FName Name;
	FString Label;
	FString Description;
	EUserInterfaceActionType Type;
	FInputChord DefaultChord;
	FInputChord ActiveChord;
	FName Icon;
};

// A named group of commands ("LevelEditor", "TextureBrowser"). Chords only
// conflict within a context and its parents.
class FBindingContext
{
public:
	FBindingContext(FName InName, FString InDescription, FName InParent = NAME_None)
		: Name(InName), Description(std::move(InDescription)), Parent(InParent)
	{
	}

	TSharedRef<FUICommandInfo> AddCommand(FName CommandName, FString Label, FString Description,
		EUserInterfaceActionType Type = EUserInterfaceActionType::Button, FInputChord DefaultChord = FInputChord(), FName Icon = NAME_None);

	FName GetName() const { return Name; }
	const FString& GetDescription() const { return Description; }
	FName GetParent() const { return Parent; }
	TSharedPtr<FUICommandInfo> FindCommand(FName CommandName) const;
	const TArray<TSharedRef<FUICommandInfo>>& GetCommands() const { return Commands; }

private:
	FName Name;
	FString Description;
	FName Parent;
	TArray<TSharedRef<FUICommandInfo>> Commands;
};

class FInputBindingManager
{
public:
	static FInputBindingManager& Get();

	void RegisterContext(const TSharedRef<FBindingContext>& Context);
	void UnregisterContext(FName ContextName);
	TSharedPtr<FBindingContext> FindContext(FName ContextName) const;
	const TArray<TSharedRef<FBindingContext>>& GetContexts() const { return Contexts; }

	// The command bound to Chord in Context or one of its parents.
	TSharedPtr<FUICommandInfo> FindCommandForChord(FName Context, const FInputChord& Chord) const;

	// Rebinds a command; a command in the same context family that had the
	// chord loses it and is returned. An invalid chord unbinds.
	TSharedPtr<FUICommandInfo> SetUserChord(const TSharedRef<FUICommandInfo>& Command, const FInputChord& Chord);
	void ResetToDefaults();

	// { "Context": { "Command": "Ctrl+S", ... }, ... } with only the chords
	// that differ from the defaults ("" for unbound).
	FJsonValue SaveUserBindings() const;
	void LoadUserBindings(const FJsonValue& Bindings);

	TMulticastDelegate<void()> OnBindingsChanged;

private:
	bool IsInFamily(FName Context, FName Other) const;

	TArray<TSharedRef<FBindingContext>> Contexts;
};

struct FUIAction
{
	FSimpleDelegate Execute;
	TDelegate<bool()> CanExecute;            // unbound: always
	TDelegate<ECheckBoxState()> GetCheckState; // unbound: unchecked
	TDelegate<bool()> IsVisible;             // unbound: always

	FUIAction() = default;
	explicit FUIAction(FSimpleDelegate InExecute, TDelegate<bool()> InCanExecute = {}, TDelegate<ECheckBoxState()> InGetCheckState = {}, TDelegate<bool()> InIsVisible = {})
		: Execute(std::move(InExecute)), CanExecute(std::move(InCanExecute)), GetCheckState(std::move(InGetCheckState)), IsVisible(std::move(InIsVisible))
	{
	}

	bool CanRun() const { return Execute.IsBound() && (!CanExecute.IsBound() || CanExecute.Execute()); }
	bool Run() const
	{
		if (!CanRun())
		{
			return false;
		}
		Execute.Execute();
		return true;
	}
	ECheckBoxState CheckState() const { return GetCheckState.IsBound() ? GetCheckState.Execute() : ECheckBoxState::Unchecked; }
	bool Visible() const { return !IsVisible.IsBound() || IsVisible.Execute(); }
};

class FUICommandList : public TSharedFromThis<FUICommandList>
{
public:
	void MapAction(const TSharedRef<FUICommandInfo>& Command, FUIAction Action);
	// Shorthand with callables.
	void MapAction(const TSharedRef<FUICommandInfo>& Command, TFunction<void()> Execute, TFunction<bool()> CanExecute = nullptr, TFunction<bool()> IsChecked = nullptr);
	void UnmapAction(const TSharedRef<FUICommandInfo>& Command);
	bool IsActionMapped(const TSharedRef<FUICommandInfo>& Command) const { return FindAction(Command.Get()) != nullptr; }
	// Commands of Child are found through this list too.
	void Append(const TSharedRef<FUICommandList>& Child);

	const FUIAction* FindAction(const FUICommandInfo& Command) const;
	bool ExecuteAction(const FUICommandInfo& Command) const;
	bool CanExecuteAction(const FUICommandInfo& Command) const;
	ECheckBoxState GetCheckState(const FUICommandInfo& Command) const;
	bool IsVisible(const FUICommandInfo& Command) const;

	// Runs the command whose active chord matches the key, if it can run.
	bool ProcessCommandBindings(const FKeyEvent& Event) const;

private:
	struct FMapping
	{
		TSharedRef<FUICommandInfo> Command;
		FUIAction Action;
	};
	TArray<FMapping> Mappings;
	TArray<TSharedRef<FUICommandList>> Children;
};

// Runs CommandList's shortcuts for keys that reach it unhandled (keys go to
// the focused widget first, so a text box keeps its own Ctrl+Z).
class SCommandBindingScope : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCommandBindingScope)
		SLATE_ARGUMENT(TSharedPtr<FUICommandList>, CommandList, nullptr)
		SLATE_DEFAULT_SLOT(Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		CommandList = Args._CommandList;
		if (Args._Content.Widget)
		{
			ChildSlot[ToSharedRef(Args._Content.Widget)];
		}
	}

	FReply OnKeyDown(const FGeometry&, const FKeyEvent& Event) override
	{
		return CommandList && CommandList->ProcessCommandBindings(Event) ? FReply::Handled() : FReply::Unhandled();
	}

private:
	TSharedPtr<FUICommandList> CommandList;
};
