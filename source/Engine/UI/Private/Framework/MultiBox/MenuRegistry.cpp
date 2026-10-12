// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Framework/MultiBox/MenuRegistry.h"

#include "Application/UIApplication.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/SImage.h"
#include "Widgets/Text/STextBlock.h"

// ---------------------------------------------------------------------------
// Context and entries

TSharedPtr<FUICommandList> FMenuContext::FindListFor(const FUICommandInfo& Command) const
{
	for (const TSharedRef<FUICommandList>& List : CommandLists)
	{
		if (List->FindAction(Command))
		{
			return List.ToSharedPtr();
		}
	}
	return nullptr;
}

namespace
{
	TSharedPtr<FUICommandList> ListFor(const FMenuEntry& Entry, const FMenuContext& Context)
	{
		if (Entry.CommandList && Entry.Command && Entry.CommandList->FindAction(*Entry.Command))
		{
			return Entry.CommandList;
		}
		return Entry.Command ? Context.FindListFor(*Entry.Command) : nullptr;
	}
}

bool FMenuEntry::IsEnabled(const FMenuContext& Context) const
{
	if (Kind == EMenuEntryKind::Command)
	{
		const TSharedPtr<FUICommandList> List = ListFor(*this, Context);
		return List && List->CanExecuteAction(*Command);
	}
	if (Kind == EMenuEntryKind::Action)
	{
		return Action.CanRun();
	}
	return true;
}

bool FMenuEntry::IsVisible(const FMenuContext& Context) const
{
	if (Kind == EMenuEntryKind::Command)
	{
		const TSharedPtr<FUICommandList> List = ListFor(*this, Context);
		return List && List->IsVisible(*Command); // unmapped commands are hidden
	}
	if (Kind == EMenuEntryKind::Action)
	{
		return Action.Visible();
	}
	return true;
}

ECheckBoxState FMenuEntry::GetCheckState(const FMenuContext& Context) const
{
	if (Kind == EMenuEntryKind::Command)
	{
		const TSharedPtr<FUICommandList> List = ListFor(*this, Context);
		return List ? List->GetCheckState(*Command) : ECheckBoxState::Unchecked;
	}
	return Action.CheckState();
}

void FMenuEntry::Run(const FMenuContext& Context) const
{
	if (Kind == EMenuEntryKind::Command)
	{
		if (const TSharedPtr<FUICommandList> List = ListFor(*this, Context))
		{
			List->ExecuteAction(*Command);
		}
	}
	else if (Kind == EMenuEntryKind::Action)
	{
		Action.Run();
	}
}

// ---------------------------------------------------------------------------
// Sections and definitions

FMenuEntry& FMenuSection::AddEntryInternal(FMenuEntry Entry)
{
	if (!Entry.Name.IsNone())
	{
		for (FMenuEntry& Existing : Entries)
		{
			if (Existing.Name == Entry.Name)
			{
				Existing = std::move(Entry); // re-adding replaces
				return Existing;
			}
		}
	}
	Entries.push_back(std::move(Entry));
	return Entries.back();
}

FMenuEntry& FMenuSection::AddCommand(const TSharedPtr<FUICommandInfo>& Command, FString LabelOverride, TSharedPtr<FUICommandList> CommandList)
{
	check(Command);
	FMenuEntry Entry;
	Entry.Name = Command->GetName();
	Entry.Kind = EMenuEntryKind::Command;
	Entry.Command = Command;
	Entry.CommandList = std::move(CommandList);
	Entry.Label = std::move(LabelOverride);
	return AddEntryInternal(std::move(Entry));
}

FMenuEntry& FMenuSection::AddEntry(FName EntryName, FString EntryLabel, FString ToolTip, FUIAction Action, EUserInterfaceActionType Type)
{
	FMenuEntry Entry;
	Entry.Name = EntryName;
	Entry.Kind = EMenuEntryKind::Action;
	Entry.Label = std::move(EntryLabel);
	Entry.ToolTip = std::move(ToolTip);
	Entry.Action = std::move(Action);
	Entry.ActionType = Type;
	return AddEntryInternal(std::move(Entry));
}

FMenuEntry& FMenuSection::AddSubMenu(FName EntryName, FString EntryLabel, FString ToolTip, FMenuSectionBuilder Builder)
{
	FMenuEntry Entry;
	Entry.Name = EntryName;
	Entry.Kind = EMenuEntryKind::SubMenu;
	Entry.Label = std::move(EntryLabel);
	Entry.ToolTip = std::move(ToolTip);
	Entry.SubMenuBuilder = std::move(Builder);
	return AddEntryInternal(std::move(Entry));
}

FMenuEntry& FMenuSection::AddSubMenu(FName EntryName, FString EntryLabel, FString ToolTip, FName RegisteredMenu)
{
	FMenuEntry Entry;
	Entry.Name = EntryName;
	Entry.Kind = EMenuEntryKind::SubMenu;
	Entry.Label = std::move(EntryLabel);
	Entry.ToolTip = std::move(ToolTip);
	Entry.SubMenuName = RegisteredMenu;
	return AddEntryInternal(std::move(Entry));
}

FMenuEntry& FMenuSection::AddSeparator(FName EntryName)
{
	FMenuEntry Entry;
	Entry.Name = EntryName;
	Entry.Kind = EMenuEntryKind::Separator;
	return AddEntryInternal(std::move(Entry));
}

FMenuEntry& FMenuSection::AddWidget(FName EntryName, TFunction<TSharedRef<SWidget>(const FMenuContext&)> MakeWidget)
{
	FMenuEntry Entry;
	Entry.Name = EntryName;
	Entry.Kind = EMenuEntryKind::Widget;
	Entry.MakeWidget = std::move(MakeWidget);
	return AddEntryInternal(std::move(Entry));
}

FMenuEntry* FMenuSection::FindEntry(FName EntryName)
{
	for (FMenuEntry& Entry : Entries)
	{
		if (Entry.Name == EntryName)
		{
			return &Entry;
		}
	}
	return nullptr;
}

FMenuSection& FMenuDefinition::AddSection(FName SectionName, FString Label, FMenuInsert Insert)
{
	if (FMenuSection* Existing = FindSection(SectionName))
	{
		if (!Label.IsEmpty())
		{
			Existing->Label = std::move(Label);
		}
		if (Insert.Position != EMenuInsertPosition::Default)
		{
			Existing->Insert = Insert;
		}
		return *Existing;
	}
	FMenuSection& Section = Sections.emplace_back(SectionName, std::move(Label));
	Section.Insert = Insert;
	return Section;
}

FMenuSection& FMenuDefinition::AddDynamicSection(FName SectionName, FMenuSectionBuilder Builder, FMenuInsert Insert)
{
	FMenuSection& Section = AddSection(SectionName, FString(), Insert);
	Section.Dynamic = std::move(Builder);
	return Section;
}

FMenuSection* FMenuDefinition::FindSection(FName SectionName)
{
	for (FMenuSection& Section : Sections)
	{
		if (Section.Name == SectionName)
		{
			return &Section;
		}
	}
	return nullptr;
}

FMenuSection& FMenuDefinition::FindOrAddSection(FName SectionName)
{
	return AddSection(SectionName);
}

// ---------------------------------------------------------------------------
// Registry

FMenuRegistry& FMenuRegistry::Get()
{
	static FMenuRegistry Registry;
	return Registry;
}

FMenuDefinition& FMenuRegistry::RegisterMenu(FName MenuName, FName Parent)
{
	TSharedPtr<FMenuDefinition>& Menu = Menus[MenuName];
	if (!Menu)
	{
		Menu = std::make_shared<FMenuDefinition>();
		Menu->Name = MenuName;
	}
	Menu->Parent = Parent;
	Menu->bRegistered = true;
	return *Menu;
}

FMenuDefinition& FMenuRegistry::ExtendMenu(FName MenuName)
{
	TSharedPtr<FMenuDefinition>& Menu = Menus[MenuName];
	if (!Menu)
	{
		Menu = std::make_shared<FMenuDefinition>();
		Menu->Name = MenuName;
	}
	return *Menu;
}

bool FMenuRegistry::IsMenuRegistered(FName MenuName) const
{
	auto It = Menus.find(MenuName);
	return It != Menus.end() && It->second->bRegistered;
}

void FMenuRegistry::RemoveMenu(FName MenuName)
{
	Menus.erase(MenuName);
}

void FMenuRegistry::RemoveAll()
{
	Menus.clear();
}

namespace
{
	// Orders items: default ones in add order, then each positioned item
	// placed relative to the item it names (or at the end if missing).
	template <typename T>
	TArray<T> ApplyInsertOrder(const TArray<T>& Items)
	{
		TArray<T> Result;
		for (const T& Item : Items)
		{
			if (Item.Insert.Position == EMenuInsertPosition::Default)
			{
				Result.push_back(Item);
			}
		}
		for (const T& Item : Items)
		{
			const FMenuInsert& Insert = Item.Insert;
			if (Insert.Position == EMenuInsertPosition::Default)
			{
				continue;
			}
			if (Insert.Position == EMenuInsertPosition::First)
			{
				Result.insert(Result.begin(), Item);
				continue;
			}
			auto It = std::find_if(Result.begin(), Result.end(), [&Insert](const T& Other) { return Other.Name == Insert.Name; });
			if (It == Result.end())
			{
				Result.push_back(Item);
			}
			else
			{
				Result.insert(Insert.Position == EMenuInsertPosition::Before ? It : It + 1, Item);
			}
		}
		return Result;
	}

	TArray<FMenuSection> ResolveSubMenuSections(const FMenuEntry& Entry, const FMenuContext& Context)
	{
		if (!Entry.SubMenuName.IsNone())
		{
			return FMenuRegistry::Get().ResolveMenu(Entry.SubMenuName, Context);
		}
		FMenuSection Section;
		if (Entry.SubMenuBuilder)
		{
			Entry.SubMenuBuilder(Section, Context);
		}
		return { Section };
	}
}

TArray<FMenuSection> FMenuRegistry::ResolveMenu(FName MenuName, const FMenuContext& Context) const
{
	auto It = Menus.find(MenuName);
	if (It == Menus.end())
	{
		return {};
	}
	const FMenuDefinition& Menu = *It->second;

	TArray<FMenuSection> Sections;
	if (!Menu.Parent.IsNone() && Menu.Parent != MenuName)
	{
		Sections = ResolveMenu(Menu.Parent, Context);
	}
	for (const FMenuSection& Own : Menu.Sections)
	{
		auto Existing = std::find_if(Sections.begin(), Sections.end(), [&Own](const FMenuSection& S) { return S.Name == Own.Name; });
		if (Existing == Sections.end())
		{
			Sections.push_back(Own);
		}
		else
		{
			Existing->Entries.insert(Existing->Entries.end(), Own.Entries.begin(), Own.Entries.end());
			if (Own.Dynamic)
			{
				Existing->Dynamic = Own.Dynamic;
			}
		}
	}
	Sections = ApplyInsertOrder(Sections);
	for (FMenuSection& Section : Sections)
	{
		if (Section.Dynamic)
		{
			Section.Dynamic(Section, Context);
		}
		Section.Entries = ApplyInsertOrder(Section.Entries);
		std::erase_if(Section.Entries, [&Context](const FMenuEntry& Entry) { return !Entry.IsVisible(Context); });
		// No separators at the ends or twice in a row.
		TArray<FMenuEntry> Clean;
		for (const FMenuEntry& Entry : Section.Entries)
		{
			const bool bSeparator = Entry.Kind == EMenuEntryKind::Separator;
			if (bSeparator && (Clean.empty() || Clean.back().Kind == EMenuEntryKind::Separator))
			{
				continue;
			}
			Clean.push_back(Entry);
		}
		while (!Clean.empty() && Clean.back().Kind == EMenuEntryKind::Separator)
		{
			Clean.pop_back();
		}
		Section.Entries = std::move(Clean);
	}
	std::erase_if(Sections, [](const FMenuSection& S) { return S.Entries.empty(); });
	return Sections;
}

// ---------------------------------------------------------------------------
// Widgets

namespace
{
	TSharedRef<SWidget> MakeEntryWidget(const FMenuEntry& Entry, const FMenuContext& Context, bool bShowCheck)
	{
		switch (Entry.Kind)
		{
		case EMenuEntryKind::Separator:
			return SNew(SSeparator);
		case EMenuEntryKind::Widget:
			return SNew(SBox).Padding(FMargin(10.0f, 3.0f))[Entry.MakeWidget ? Entry.MakeWidget(Context) : SNullWidget::NullWidget()];
		case EMenuEntryKind::SubMenu:
			return SNew(SMenuEntry).Text(Entry.GetLabel()).ToolTipText(Entry.GetToolTip()).ShowCheck(bShowCheck)
				.OnGetSubMenu([Entry, Context]() { return FMenuRegistry::Get().GenerateMenuWidget(ResolveSubMenuSections(Entry, Context), Context); });
		default:
			break;
		}
		TAttribute<FString> Gesture;
		if (Entry.Command)
		{
			const TSharedPtr<FUICommandInfo> Command = Entry.Command;
			Gesture = TAttribute<FString>([Command] { return Command->GetInputText(); });
		}
		return SNew(SMenuEntry)
			.Text(Entry.GetLabel())
			.ToolTipText(Entry.GetToolTip())
			.InputGesture(Gesture.IsSet() ? Gesture : TAttribute<FString>(""))
			.ShowCheck(bShowCheck)
			.CheckState([Entry, Context] { return Entry.GetCheckState(Context); })
			.IsEnabled([Entry, Context] { return Entry.IsEnabled(Context); })
			.OnSelected([Entry, Context] { Entry.Run(Context); });
	}
}

TSharedRef<SWidget> FMenuRegistry::GenerateMenuWidget(FName MenuName, const FMenuContext& Context) const
{
	return GenerateMenuWidget(ResolveMenu(MenuName, Context), Context);
}

TSharedRef<SWidget> FMenuRegistry::GenerateMenuWidget(const TArray<FMenuSection>& Sections, const FMenuContext& Context) const
{
	// A check column for everyone if anyone needs one, so labels line up.
	bool bShowCheck = false;
	for (const FMenuSection& Section : Sections)
	{
		for (const FMenuEntry& Entry : Section.Entries)
		{
			const EUserInterfaceActionType Type = Entry.GetActionType();
			bShowCheck |= (Entry.Kind == EMenuEntryKind::Command || Entry.Kind == EMenuEntryKind::Action) && Type != EUserInterfaceActionType::Button;
		}
	}

	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	for (SIZE_T S = 0; S < Sections.size(); ++S)
	{
		const FMenuSection& Section = Sections[S];
		if (S > 0)
		{
			Box->AddSlot().AutoHeight()[SNew(SSeparator)];
		}
		if (!Section.Label.IsEmpty())
		{
			Box->AddSlot().AutoHeight().Padding(FMargin(10.0f, 4.0f, 10.0f, 2.0f))
				[SNew(STextBlock).Text(Section.Label).TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText"))];
		}
		for (const FMenuEntry& Entry : Section.Entries)
		{
			Box->AddSlot().AutoHeight()[MakeEntryWidget(Entry, Context, bShowCheck)];
		}
	}
	if (Sections.empty())
	{
		Box->AddSlot().AutoHeight().Padding(FMargin(10.0f, 4.0f))[SNew(STextBlock).Text("(empty)").TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText"))];
	}
	return Box;
}

// --- Menu bar ------------------------------------------------------------------

namespace
{
	class SMenuBar;

	// One menu of the bar; hovering it while another bar menu is open
	// switches to it.
	class SMenuBarItem final : public SCompoundWidget
	{
	public:
		GE_BEGIN_ARGS(SMenuBarItem)
		GE_END_ARGS()

		void Construct(const FArguments&, SMenuBar* InBar, const FMenuEntry& Entry, const FMenuContext& Context);
		void OnMouseEnter(const FGeometry& Geometry, const FPointerEvent& Event) override;

		TSharedPtr<SComboButton> Combo;
		SMenuBar* Bar = nullptr;
	};

	class SMenuBar final : public SCompoundWidget
	{
	public:
		GE_BEGIN_ARGS(SMenuBar)
		GE_END_ARGS()

		void Construct(const FArguments&, const TArray<FMenuSection>& Sections, const FMenuContext& Context)
		{
			const TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
			for (const FMenuSection& Section : Sections)
			{
				for (const FMenuEntry& Entry : Section.Entries)
				{
					if (Entry.Kind != EMenuEntryKind::SubMenu)
					{
						continue;
					}
					TSharedPtr<SMenuBarItem> Item;
					Row->AddSlot().AutoWidth()[SAssignNew(Item, SMenuBarItem, this, Entry, Context)];
					Items.push_back(Item);
				}
			}
			ChildSlot[Row];
		}

		bool IsAnyOpen() const
		{
			return std::any_of(Items.begin(), Items.end(), [](const TSharedPtr<SMenuBarItem>& I) { return I->Combo->IsOpen(); });
		}

		TArray<TSharedPtr<SMenuBarItem>> Items;
	};

	void SMenuBarItem::Construct(const FArguments&, SMenuBar* InBar, const FMenuEntry& Entry, const FMenuContext& Context)
	{
		Bar = InBar;
		ChildSlot
		[
			SAssignNew(Combo, SComboButton)
			.Text(Entry.GetLabel())
			.HasDownArrow(false)
			.IsFocusable(false)
			.Placement(EMenuPlacement::BelowAnchor)
			.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("FlatButton"))
			.OnGetMenuContent([Entry, Context]() { return FMenuRegistry::Get().GenerateMenuWidget(ResolveSubMenuSections(Entry, Context), Context); })
		];
	}

	void SMenuBarItem::OnMouseEnter(const FGeometry&, const FPointerEvent&)
	{
		if (!Combo->IsOpen() && Bar->IsAnyOpen())
		{
			Combo->SetIsOpen(true);
		}
	}

	// --- Tool bar --------------------------------------------------------------

	// A toolbar button that shows its toggle state.
	class SToolBarButton final : public SCompoundWidget
	{
	public:
		GE_BEGIN_ARGS(SToolBarButton)
		GE_END_ARGS()

		void Construct(const FArguments&, const FMenuEntry& InEntry, const FMenuContext& InContext)
		{
			Entry = InEntry;
			Context = InContext;
			Style = &FAppStyle::Get().GetWidgetStyle<FToolBarStyle>("ToolBar");
			FString Tip = Entry.GetLabel();
			if (Entry.Command && !Entry.Command->GetInputText().IsEmpty())
			{
				Tip += FString::Format(" ({})", Entry.Command->GetInputText());
			}
			if (!Entry.GetToolTip().IsEmpty())
			{
				Tip += "\n" + Entry.GetToolTip();
			}
			const FStyleBrush* Icon = Entry.Command && !Entry.Command->GetIcon().IsNone() ? FAppStyle::Get().Find<FStyleBrush>(Entry.Command->GetIcon())
				: (!Entry.Icon.IsNone() ? FAppStyle::Get().Find<FStyleBrush>(Entry.Icon) : nullptr);
			const TSharedRef<SHorizontalBox> Content = SNew(SHorizontalBox);
			if (Icon)
			{
				Content->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 5.0f, 0.0f))[SNew(SImage).Image(Icon)];
			}
			Content->AddSlot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(Entry.GetLabel()).TextStyle(&Style->LabelText)];
			ChildSlot
			[
				SAssignNew(Button, SButton)
				.ButtonStyle(&Style->Button)
				.IsFocusable(false)
				.ToolTipText(Tip)
				.IsEnabled([this] { return Entry.IsEnabled(Context); })
				.OnClicked([this] { Entry.Run(Context); return FReply::Handled(); })
				[Content]
			];
		}

	protected:
		int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FDrawElementList& OutDrawElements,
			int32 LayerId, const FWidgetStyle& InWidgetStyle) const override
		{
			const bool bCheckable = Entry.GetActionType() != EUserInterfaceActionType::Button;
			if (bCheckable && Entry.GetCheckState(Context) == ECheckBoxState::Checked)
			{
				const FStyleBrush& Brush = IsHovered() ? Style->ToggleButtonChecked.Hovered : Style->ToggleButtonChecked.Normal;
				Brush.Paint(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), InWidgetStyle.ColorAndOpacityTint);
			}
			return PaintArrangedChildren(Args, CullingRect, OutDrawElements, LayerId + 1, MakeChildStyle(InWidgetStyle));
		}

	private:
		FMenuEntry Entry;
		FMenuContext Context;
		const FToolBarStyle* Style = nullptr;
		TSharedPtr<SButton> Button;
	};
}

TSharedRef<SWidget> FMenuRegistry::GenerateMenuBar(FName MenuName, const FMenuContext& Context) const
{
	return SNew(SMenuBar, ResolveMenu(MenuName, Context), Context);
}

TSharedRef<SWidget> FMenuRegistry::GenerateToolBar(FName MenuName, const FMenuContext& Context) const
{
	const FToolBarStyle& Style = FAppStyle::Get().GetWidgetStyle<FToolBarStyle>("ToolBar");
	const TArray<FMenuSection> Sections = ResolveMenu(MenuName, Context);
	const TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (SIZE_T S = 0; S < Sections.size(); ++S)
	{
		if (S > 0)
		{
			Row->AddSlot().AutoWidth().Padding(FMargin(4.0f, 2.0f))[SNew(SSeparator).Orientation(EOrientation::Vertical).Padding(0.0f)];
		}
		for (const FMenuEntry& Entry : Sections[S].Entries)
		{
			TSharedPtr<SWidget> Widget;
			switch (Entry.Kind)
			{
			case EMenuEntryKind::Separator:
				Widget = SNew(SSeparator).Orientation(EOrientation::Vertical).Padding(4.0f);
				break;
			case EMenuEntryKind::Widget:
				Widget = Entry.MakeWidget ? Entry.MakeWidget(Context).ToSharedPtr() : nullptr;
				break;
			case EMenuEntryKind::SubMenu:
				Widget = SNew(SComboButton).Text(Entry.GetLabel()).ButtonStyle(&Style.Button).IsFocusable(false).ToolTipText(Entry.GetToolTip())
					.OnGetMenuContent([Entry, Context]() { return FMenuRegistry::Get().GenerateMenuWidget(ResolveSubMenuSections(Entry, Context), Context); });
				break;
			default:
				Widget = SNew(SToolBarButton, Entry, Context);
				break;
			}
			if (Widget)
			{
				Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(1.0f, 0.0f))[ToSharedRef(Widget)];
			}
		}
	}
	return SNew(SBorder).BorderImage(&Style.Background).Padding(Style.Padding)[Row];
}
