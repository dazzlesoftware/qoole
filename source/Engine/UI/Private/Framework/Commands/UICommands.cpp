// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Framework/Commands/UICommands.h"

#include "Application/UIApplication.h"

// ---------------------------------------------------------------------------
// FInputChord

namespace
{
	// Names used in chord text; letters and digits are themselves.
	struct FKeyName
	{
		EKey Key;
		const char* Name;
	};
	constexpr FKeyName KeyNames[] = {
		{ EKey::BackSpace, "Backspace" }, { EKey::Tab, "Tab" }, { EKey::Enter, "Enter" }, { EKey::Escape, "Esc" }, { EKey::SpaceBar, "Space" },
		{ EKey::PageUp, "PageUp" }, { EKey::PageDown, "PageDown" }, { EKey::End, "End" }, { EKey::Home, "Home" },
		{ EKey::Left, "Left" }, { EKey::Up, "Up" }, { EKey::Right, "Right" }, { EKey::Down, "Down" },
		{ EKey::Insert, "Insert" }, { EKey::Delete, "Delete" },
		{ EKey::Semicolon, ";" }, { EKey::Equals, "=" }, { EKey::Comma, "," }, { EKey::Hyphen, "-" }, { EKey::Period, "." },
		{ EKey::Slash, "/" }, { EKey::Tilde, "`" }, { EKey::LeftBracket, "[" }, { EKey::Backslash, "\\" }, { EKey::RightBracket, "]" },
		{ EKey::Apostrophe, "'" }, { EKey::Add, "Num+" }, { EKey::Subtract, "Num-" }, { EKey::Multiply, "Num*" }, { EKey::Divide, "Num/" },
	};

	FString KeyToText(EKey Key)
	{
		const uint32 Code = static_cast<uint32>(Key);
		if ((Code >= 'A' && Code <= 'Z') || (Code >= '0' && Code <= '9'))
		{
			return FString(std::string(1, static_cast<char>(Code)));
		}
		if (Key >= EKey::F1 && Key <= EKey::F24)
		{
			return FString::Format("F{}", Code - static_cast<uint32>(EKey::F1) + 1);
		}
		if (Key >= EKey::NumPad0 && Key <= EKey::NumPad9)
		{
			return FString::Format("Num{}", Code - static_cast<uint32>(EKey::NumPad0));
		}
		for (const FKeyName& Entry : KeyNames)
		{
			if (Entry.Key == Key)
			{
				return Entry.Name;
			}
		}
		return ::ToString(Key);
	}

	TOptional<EKey> TextToKey(const FString& Text)
	{
		if (Text.Len() == 1)
		{
			const char C = Text[0];
			if (C >= 'a' && C <= 'z') return static_cast<EKey>(C - 'a' + 'A');
			if ((C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9')) return static_cast<EKey>(C);
		}
		if (Text.Len() >= 2 && (Text[0] == 'F' || Text[0] == 'f'))
		{
			const int32 N = std::atoi(*Text.Mid(1));
			if (N >= 1 && N <= 24)
			{
				return static_cast<EKey>(static_cast<uint32>(EKey::F1) + static_cast<uint32>(N - 1));
			}
		}
		if (Text.StartsWith("Num", ESearchCase::IgnoreCase) && Text.Len() == 4 && Text[3] >= '0' && Text[3] <= '9')
		{
			return static_cast<EKey>(static_cast<uint32>(EKey::NumPad0) + static_cast<uint32>(Text[3] - '0'));
		}
		for (const FKeyName& Entry : KeyNames)
		{
			if (Text.Equals(Entry.Name, ESearchCase::IgnoreCase))
			{
				return Entry.Key;
			}
		}
		return {};
	}
}

bool FInputChord::Matches(const FKeyEvent& Event) const
{
	if (!IsValid() || Event.IsGamepad() || Event.GetKey() != Key)
	{
		return false;
	}
	return Event.IsShiftDown() == HasModifier(Modifiers, EModifierKey::Shift)
		&& Event.IsControlDown() == HasModifier(Modifiers, EModifierKey::Control)
		&& Event.IsAltDown() == HasModifier(Modifiers, EModifierKey::Alt);
}

FString FInputChord::ToString() const
{
	if (!IsValid())
	{
		return FString();
	}
	FString Text;
	if (HasModifier(Modifiers, EModifierKey::Control)) Text += "Ctrl+";
	if (HasModifier(Modifiers, EModifierKey::Shift)) Text += "Shift+";
	if (HasModifier(Modifiers, EModifierKey::Alt)) Text += "Alt+";
	return Text + KeyToText(Key);
}

TOptional<FInputChord> FInputChord::Parse(const FString& Text)
{
	FInputChord Chord;
	// "+" separates parts, but the key itself may be "+"-free punctuation only.
	const TArray<FString> Parts = Text.ParseIntoArray("+", true);
	if (Parts.empty())
	{
		return {};
	}
	for (SIZE_T I = 0; I + 1 < Parts.size(); ++I)
	{
		const FString Part = Parts[I].TrimStartAndEnd();
		if (Part.Equals("Ctrl", ESearchCase::IgnoreCase) || Part.Equals("Control", ESearchCase::IgnoreCase)) Chord.Modifiers = Chord.Modifiers | EModifierKey::Control;
		else if (Part.Equals("Shift", ESearchCase::IgnoreCase)) Chord.Modifiers = Chord.Modifiers | EModifierKey::Shift;
		else if (Part.Equals("Alt", ESearchCase::IgnoreCase)) Chord.Modifiers = Chord.Modifiers | EModifierKey::Alt;
		else return {};
	}
	const TOptional<EKey> Key = TextToKey(Parts.back().TrimStartAndEnd());
	if (!Key)
	{
		return {};
	}
	Chord.Key = *Key;
	return Chord;
}

// ---------------------------------------------------------------------------
// FBindingContext, FInputBindingManager

TSharedRef<FUICommandInfo> FBindingContext::AddCommand(FName CommandName, FString Label, FString CommandDescription, EUserInterfaceActionType Type, FInputChord DefaultChord, FName Icon)
{
	checkf(!FindCommand(CommandName), "Command {} added twice to {}", CommandName, Name);
	auto Command = MakeShared<FUICommandInfo>(Name, CommandName, std::move(Label), std::move(CommandDescription), Type, DefaultChord, Icon);
	Commands.push_back(Command);
	return Command;
}

TSharedPtr<FUICommandInfo> FBindingContext::FindCommand(FName CommandName) const
{
	for (const TSharedRef<FUICommandInfo>& Command : Commands)
	{
		if (Command->GetName() == CommandName)
		{
			return Command.ToSharedPtr();
		}
	}
	return nullptr;
}

FInputBindingManager& FInputBindingManager::Get()
{
	static FInputBindingManager Manager;
	return Manager;
}

void FInputBindingManager::RegisterContext(const TSharedRef<FBindingContext>& Context)
{
	UnregisterContext(Context->GetName());
	Contexts.push_back(Context);
	OnBindingsChanged.Broadcast();
}

void FInputBindingManager::UnregisterContext(FName ContextName)
{
	std::erase_if(Contexts, [ContextName](const TSharedRef<FBindingContext>& C) { return C->GetName() == ContextName; });
}

TSharedPtr<FBindingContext> FInputBindingManager::FindContext(FName ContextName) const
{
	for (const TSharedRef<FBindingContext>& Context : Contexts)
	{
		if (Context->GetName() == ContextName)
		{
			return Context.ToSharedPtr();
		}
	}
	return nullptr;
}

bool FInputBindingManager::IsInFamily(FName Context, FName Other) const
{
	// Other is Context or one of its parents, or the other way around.
	const auto IsAncestor = [this](FName Child, FName Ancestor)
	{
		for (FName C = Child; !C.IsNone();)
		{
			if (C == Ancestor)
			{
				return true;
			}
			const TSharedPtr<FBindingContext> Found = FindContext(C);
			C = Found ? Found->GetParent() : NAME_None;
		}
		return false;
	};
	return IsAncestor(Context, Other) || IsAncestor(Other, Context);
}

TSharedPtr<FUICommandInfo> FInputBindingManager::FindCommandForChord(FName Context, const FInputChord& Chord) const
{
	for (FName C = Context; !C.IsNone();)
	{
		const TSharedPtr<FBindingContext> Found = FindContext(C);
		if (!Found)
		{
			break;
		}
		for (const TSharedRef<FUICommandInfo>& Command : Found->GetCommands())
		{
			if (Command->GetActiveChord().IsValid() && Command->GetActiveChord() == Chord)
			{
				return Command.ToSharedPtr();
			}
		}
		C = Found->GetParent();
	}
	return nullptr;
}

TSharedPtr<FUICommandInfo> FInputBindingManager::SetUserChord(const TSharedRef<FUICommandInfo>& Command, const FInputChord& Chord)
{
	TSharedPtr<FUICommandInfo> Displaced;
	if (Chord.IsValid())
	{
		for (const TSharedRef<FBindingContext>& Context : Contexts)
		{
			if (!IsInFamily(Command->GetContext(), Context->GetName()))
			{
				continue;
			}
			for (const TSharedRef<FUICommandInfo>& Other : Context->GetCommands())
			{
				if (Other != Command && Other->ActiveChord == Chord)
				{
					Other->ActiveChord = FInputChord();
					Displaced = Other.ToSharedPtr();
				}
			}
		}
	}
	Command->ActiveChord = Chord;
	OnBindingsChanged.Broadcast();
	return Displaced;
}

void FInputBindingManager::ResetToDefaults()
{
	for (const TSharedRef<FBindingContext>& Context : Contexts)
	{
		for (const TSharedRef<FUICommandInfo>& Command : Context->GetCommands())
		{
			Command->ActiveChord = Command->DefaultChord;
		}
	}
	OnBindingsChanged.Broadcast();
}

FJsonValue FInputBindingManager::SaveUserBindings() const
{
	FJsonValue Root = FJsonValue::MakeObject();
	for (const TSharedRef<FBindingContext>& Context : Contexts)
	{
		for (const TSharedRef<FUICommandInfo>& Command : Context->GetCommands())
		{
			if (!(Command->ActiveChord == Command->DefaultChord))
			{
				Root[Context->GetName().View()][Command->GetName().View()] = Command->ActiveChord.ToString();
			}
		}
	}
	return Root;
}

void FInputBindingManager::LoadUserBindings(const FJsonValue& Bindings)
{
	for (const TSharedRef<FBindingContext>& Context : Contexts)
	{
		const FJsonValue& ContextBindings = Bindings.Get(Context->GetName().View());
		for (const TSharedRef<FUICommandInfo>& Command : Context->GetCommands())
		{
			const FJsonValue* Value = ContextBindings.Find(Command->GetName().View());
			if (!Value)
			{
				Command->ActiveChord = Command->DefaultChord;
				continue;
			}
			const FString Text = Value->AsString();
			const TOptional<FInputChord> Chord = FInputChord::Parse(Text);
			Command->ActiveChord = Chord ? *Chord : FInputChord();
		}
	}
	OnBindingsChanged.Broadcast();
}

// ---------------------------------------------------------------------------
// FUICommandList

void FUICommandList::MapAction(const TSharedRef<FUICommandInfo>& Command, FUIAction Action)
{
	UnmapAction(Command);
	Mappings.push_back({ Command, std::move(Action) });
}

void FUICommandList::MapAction(const TSharedRef<FUICommandInfo>& Command, TFunction<void()> Execute, TFunction<bool()> CanExecute, TFunction<bool()> IsChecked)
{
	FUIAction Action;
	Action.Execute = FSimpleDelegate::CreateLambda(std::move(Execute));
	if (CanExecute)
	{
		Action.CanExecute = TDelegate<bool()>::CreateLambda(std::move(CanExecute));
	}
	if (IsChecked)
	{
		Action.GetCheckState = TDelegate<ECheckBoxState()>::CreateLambda([IsChecked]() { return IsChecked() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; });
	}
	MapAction(Command, std::move(Action));
}

void FUICommandList::UnmapAction(const TSharedRef<FUICommandInfo>& Command)
{
	std::erase_if(Mappings, [&Command](const FMapping& M) { return M.Command == Command; });
}

void FUICommandList::Append(const TSharedRef<FUICommandList>& Child)
{
	check(&Child.Get() != this);
	Children.push_back(Child);
}

const FUIAction* FUICommandList::FindAction(const FUICommandInfo& Command) const
{
	for (const FMapping& Mapping : Mappings)
	{
		if (&Mapping.Command.Get() == &Command)
		{
			return &Mapping.Action;
		}
	}
	for (const TSharedRef<FUICommandList>& Child : Children)
	{
		if (const FUIAction* Action = Child->FindAction(Command))
		{
			return Action;
		}
	}
	return nullptr;
}

bool FUICommandList::ExecuteAction(const FUICommandInfo& Command) const
{
	const FUIAction* Action = FindAction(Command);
	return Action && Action->Run();
}

bool FUICommandList::CanExecuteAction(const FUICommandInfo& Command) const
{
	const FUIAction* Action = FindAction(Command);
	return Action && Action->CanRun();
}

ECheckBoxState FUICommandList::GetCheckState(const FUICommandInfo& Command) const
{
	const FUIAction* Action = FindAction(Command);
	return Action ? Action->CheckState() : ECheckBoxState::Unchecked;
}

bool FUICommandList::IsVisible(const FUICommandInfo& Command) const
{
	const FUIAction* Action = FindAction(Command);
	return Action && Action->Visible();
}

bool FUICommandList::ProcessCommandBindings(const FKeyEvent& Event) const
{
	if (Event.IsRepeat())
	{
		return false;
	}
	for (const FMapping& Mapping : Mappings)
	{
		if (Mapping.Command->GetActiveChord().Matches(Event))
		{
			// A matching but disabled command still eats the key.
			Mapping.Action.Run();
			return true;
		}
	}
	for (const TSharedRef<FUICommandList>& Child : Children)
	{
		if (Child->ProcessCommandBindings(Event))
		{
			return true;
		}
	}
	return false;
}
