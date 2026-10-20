// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// The map editor's dockable tabs: the viewports, the outliner, the
// properties of the selection, the prefab library, the settings (with the
// key bindings) and the log.

#include "MapEditorShell.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/PropertyGrid/SPropertyGrid.h"
#include "MapCoreHeaders.h"
#include "MapObjectProperties.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SViewport.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const FTextBlockStyle& SmallText() { return FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText"); }

	const FName ViewportTabId("Viewport");

	FString ObjectLabel(Object* Obj)
	{
		const char* Name = Obj->GetObjName();
		if (Name && Name[0])
		{
			return FString::FromAnsi(Name);
		}
		if (Obj->HasEntity())
		{
			return FString::FromAnsi(Obj->GetEntityPtr()->GetClassname());
		}
		return FMapDocument::IsGroup(*Obj) ? FString("Group") : FString("Brush");
	}

	FString ObjectKind(Object* Obj)
	{
		if (FMapDocument::IsGroup(*Obj))
		{
			return FString::Format("Group of {}", Obj->GetNumChildren());
		}
		return Obj->HasEntity() ? FString("Entity") : (Obj->HasBrush() ? FString("Brush") : FString("Object"));
	}

	// Rebinds a command by pressing keys (Esc cancels, Backspace clears).
	class SChordEditor final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SChordEditor)
		SLATE_END_ARGS()

		void Construct(const FArguments&, const TSharedRef<FUICommandInfo>& InCommand, TFunction<void(const FString&)> InLog)
		{
			Command = InCommand.ToSharedPtr();
			Log = std::move(InLog);
			ChildSlot
			[
				SNew(SButton).HAlign(HAlign_Left).IsFocusable(false)
				.Text([this] { return bCapturing ? FString("Press keys (Esc cancels, Backspace clears)") : (Command->GetInputText().IsEmpty() ? FString("(none)") : Command->GetInputText()); })
				.OnClicked([this]
				{
					bCapturing = true;
					return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
				})
			];
		}

		bool SupportsKeyboardFocus() const override { return true; }

		FReply OnPreviewKeyDown(const FGeometry&, const FKeyEvent& Event) override
		{
			if (!bCapturing)
			{
				return FReply::Unhandled();
			}
			const EKey Key = Event.GetKey();
			if (Key == EKey::LeftShift || Key == EKey::RightShift || Key == EKey::LeftControl || Key == EKey::RightControl || Key == EKey::LeftAlt || Key == EKey::RightAlt)
			{
				return FReply::Handled(); // wait for the real key
			}
			bCapturing = false;
			if (Key == EKey::Escape)
			{
				return FReply::Handled().ClearUserFocus();
			}
			FInputChord Chord;
			if (Key != EKey::BackSpace)
			{
				Chord.Key = Key;
				Chord.Modifiers = (Event.IsControlDown() ? EModifierKey::Control : EModifierKey::None) | (Event.IsShiftDown() ? EModifierKey::Shift : EModifierKey::None)
					| (Event.IsAltDown() ? EModifierKey::Alt : EModifierKey::None);
			}
			const TSharedPtr<FUICommandInfo> Displaced = FInputBindingManager::Get().SetUserChord(ToSharedRef(Command), Chord);
			Log(FString::Format("Bound \"{}\" to {}{}", Command->GetLabel(), Chord.IsValid() ? Chord.ToString() : FString("nothing"),
				Displaced ? FString::Format(" (taken from \"{}\")", Displaced->GetLabel()) : FString()));
			return FReply::Handled().ClearUserFocus();
		}

		void OnFocusLost(const FFocusEvent&) override { bCapturing = false; }

	private:
		TSharedPtr<FUICommandInfo> Command;
		TFunction<void(const FString&)> Log;
		bool bCapturing = false;
	};

	// The editor's settings, for a property grid. Each viewport's own
	// settings (view mode, camera, show flags) are in its toolbar.
	class FEditorSettingsSource final : public IPropertySource
	{
	public:
		FEditorSettingsSource(FMapViewSettings& InSettings, TFunction<void(float)> InSetAppScale)
			: Settings(InSettings), SetAppScale(std::move(InSetAppScale))
		{
		}

		FString GetDisplayName() const override { return "Settings"; }

		void GetProperties(TArray<FPropertyDesc>& Out) const override
		{
			Out.push_back(FPropertyDesc("GridSize", EPropertyType::Enum, "Grid size", "Grid").SetOptions(GridOptions()).SetToolTip("Map units between grid lines"));
			Out.push_back(FPropertyDesc("Snap", EPropertyType::Bool, "Snap to the grid", "Grid"));
			Out.push_back(FPropertyDesc("ShowGrid", EPropertyType::Bool, "Show the grid", "Grid"));
			Out.push_back(FPropertyDesc("SnapRotation", EPropertyType::Bool, "Snap rotation", "Tools"));
			Out.push_back(FPropertyDesc("RotateSnap", EPropertyType::Float, "Rotation steps", "Tools").SetRange(0.0f, 90.0f).SetToolTip("Degrees; 0 turns freely"));
			Out.push_back(FPropertyDesc("MouseSensitivity", EPropertyType::Float, "Mouse sensitivity", "Views").SetRange(0.1f, 4.0f)
				.SetToolTip("How fast the perspective views turn with the mouse"));
			Out.push_back(FPropertyDesc("InvertMiddleMousePan", EPropertyType::Bool, "Invert middle mouse pan", "Views"));
			Out.push_back(FPropertyDesc("AppScale", EPropertyType::Enum, "Interface scale", "Views").SetOptions({ "100%", "125%", "150%", "200%" }));
		}

		FPropertyValue GetValue(FName Property) const override
		{
			if (Property == FName("GridSize"))
			{
				int32 Index = 0;
				for (float Size = 1.0f; Size < Settings.GridSize && Index < 8; Size *= 2.0f)
				{
					++Index;
				}
				return Index;
			}
			if (Property == FName("Snap")) return Settings.bSnapToGrid;
			if (Property == FName("ShowGrid")) return Settings.bShowGrid;
			if (Property == FName("SnapRotation")) return Settings.bSnapRotation;
			if (Property == FName("RotateSnap")) return Settings.RotateSnapDegrees;
			if (Property == FName("MouseSensitivity")) return Settings.MouseSensitivity;
			if (Property == FName("InvertMiddleMousePan")) return Settings.bInvertMiddleMousePan;
			if (Property == FName("AppScale"))
			{
				const float Scale = FSlateApplication::Get().GetApplicationScale();
				return static_cast<int32>(Scale >= 1.9f ? 3 : (Scale >= 1.4f ? 2 : (Scale >= 1.2f ? 1 : 0)));
			}
			return false;
		}

		void SetValue(FName Property, const FPropertyValue& Value) override
		{
			if (Property == FName("GridSize")) Settings.GridSize = static_cast<float>(1 << FMath::Clamp(std::get<int32>(Value), 0, 8));
			else if (Property == FName("Snap")) Settings.bSnapToGrid = std::get<bool>(Value);
			else if (Property == FName("ShowGrid")) Settings.bShowGrid = std::get<bool>(Value);
			else if (Property == FName("SnapRotation")) Settings.bSnapRotation = std::get<bool>(Value);
			else if (Property == FName("RotateSnap")) Settings.RotateSnapDegrees = std::get<float>(Value);
			else if (Property == FName("MouseSensitivity")) Settings.MouseSensitivity = std::get<float>(Value);
			else if (Property == FName("InvertMiddleMousePan")) Settings.bInvertMiddleMousePan = std::get<bool>(Value);
			else if (Property == FName("AppScale"))
			{
				static const float Scales[] = { 1.0f, 1.25f, 1.5f, 2.0f };
				SetAppScale(Scales[FMath::Clamp(std::get<int32>(Value), 0, 3)]);
			}
			Settings.Changed();
		}

	private:
		static TArray<FString> GridOptions()
		{
			TArray<FString> Options;
			for (int32 Size = 1; Size <= 256; Size *= 2)
			{
				Options.push_back(FString::Format("{}", Size));
			}
			return Options;
		}

		FMapViewSettings& Settings;
		TFunction<void(float)> SetAppScale;
	};
}

TSharedRef<FDockLayout> FMapEditorShell::MakeDefaultLayout()
{
	// The viewports (one tab holding up to four panes, as in Unreal); the
	// outliner and prefabs, the properties, and the log and settings at the
	// right. Version 3: the four view tabs became one viewport tab.
	return FDockLayout::New("MapEditor", 3)->AddArea(
		FDockLayout::NewPrimaryArea()->SetOrientation(EOrientation::Horizontal)
			->Split(FDockLayout::NewStack()->SetSizeCoefficient(0.77f)->AddTab(ViewportTabId))
			->Split(FDockLayout::NewSplitter()->SetOrientation(EOrientation::Vertical)->SetSizeCoefficient(0.23f)
				->Split(FDockLayout::NewStack()->SetSizeCoefficient(0.4f)->AddTab("Outliner")->AddTab("Prefabs")->SetForegroundTab("Outliner"))
				->Split(FDockLayout::NewStack()->SetSizeCoefficient(0.4f)->AddTab("Properties"))
				->Split(FDockLayout::NewStack()->SetSizeCoefficient(0.2f)->AddTab("Log")->AddTab("Settings")->SetForegroundTab("Log"))));
}

void FMapEditorShell::RegisterTabs()
{
	FTabManager& Tabs = *TabManager;
	Tabs.RegisterTabSpawner(ViewportTabId, [this](const FSpawnTabArgs&) { return MakeViewportTab(); })
		.SetDisplayName("Viewport").SetToolTip("The map views: the layout and each view's type, mode and camera are in their toolbars");

	Tabs.RegisterTabSpawner("Outliner", [this](const FSpawnTabArgs&) { return MakeOutlinerTab(); })
		.SetDisplayName("Outliner").SetToolTip("The map's objects and groups (double-click a group to edit it)");
	Tabs.RegisterTabSpawner("Properties", [this](const FSpawnTabArgs&) { return MakePropertiesTab(); })
		.SetDisplayName("Properties").SetToolTip("Name, position, texture and entity keys of the selection");
	Tabs.RegisterTabSpawner("Prefabs", [this](const FSpawnTabArgs&) { return MakePrefabsTab(); })
		.SetDisplayName("Prefabs").SetToolTip("Ready-made brushes and the ones you saved");
	Tabs.RegisterTabSpawner("Settings", [this](const FSpawnTabArgs&) { return MakeSettingsTab(); })
		.SetDisplayName("Settings").SetToolTip("Grid, tools, views and key bindings");
	Tabs.RegisterTabSpawner("Log", [this](const FSpawnTabArgs&) { return MakeLogTab(); })
		.SetDisplayName("Log").SetToolTip("What the editor did");
}

// ----- Viewports -----

TSharedRef<SDockTab> FMapEditorShell::MakeViewportTab()
{
	// The tools' buttons in the viewport toolbars run the editor's commands,
	// so their tooltips show the shortcuts.
	TArray<TSharedPtr<FUICommandInfo>> ToolCommands = { Cmd.ToolSelect, Cmd.ToolRotate, Cmd.ToolScale, Cmd.ToolClip, Cmd.ToolVertex, Cmd.ToolEdge, Cmd.ToolFace };
	const TSharedRef<SDockTab> Tab = SNew(SDockTab).Label("Viewport").Role(ETabRole::Document)
	[
		SAssignNew(ViewportLayout, SMapViewportLayout, Views, &ViewSettings)
		.Layout(ViewportLayoutType)
		.ToolCommands(ToolCommands)
		.ToolCommandList(Commands)
	];
	Tab->SetOnTabClosed(FOnTabClosed::CreateLambda([this](const TSharedRef<SDockTab>&)
	{
		// Reopened, the tab shows the same panes in the same layout.
		if (ViewportLayout)
		{
			ViewportLayoutType = ViewportLayout->GetLayout();
			Views = ViewportLayout->GetClients();
		}
		ViewportLayout.reset();
	}));
	return Tab;
}

// ----- Outliner -----

TSharedRef<SDockTab> FMapEditorShell::MakeOutlinerTab()
{
	const TSharedRef<SDockTab> Tab = SNew(SDockTab).Label("Outliner")
	[
		SAssignNew(Outliner, STreeView<Object*>)
		.TreeItemsSource(&OutlinerRoots)
		.SelectionMode(ESelectionMode::Multi)
		.OnGetChildren([](Object* Obj, TArray<Object*>& OutChildren)
		{
			if (FMapDocument::IsGroup(*Obj))
			{
				for (int Index = 0; Index < Obj->GetNumChildren(); ++Index)
				{
					OutChildren.push_back(&Obj->GetChild(Index));
				}
			}
		})
		.OnGenerateRow([](Object* Obj, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
		{
			// Texts are read once: an object can be freed (a brush cut away)
			// before the tree is rebuilt, so rows must not look at it later.
			return SNew(STableRow<Object*>, Owner)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(STextBlock).Clipping(EWidgetClipping::ClipToBounds).Text(ObjectLabel(Obj))]
				+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))[SNew(STextBlock).TextStyle(&SmallText()).Text(ObjectKind(Obj))]
			];
		})
		.OnSelectionChanged([this](Object*, ESelectInfo)
		{
			if (!Outliner || !Document || bSyncingOutliner)
			{
				return;
			}
			// Objects of one group at a time: the first one's group becomes
			// the scope when it is not already.
			const TArray<Object*> Picked = Outliner->GetSelectedItems();
			if (Picked.empty())
			{
				Document->ClearSelection();
				return;
			}
			Object* Parent = Picked.front()->GetParentPtr();
			TArray<Object*> Siblings;
			for (Object* Obj : Picked)
			{
				if (Obj->GetParentPtr() == Parent)
				{
					Siblings.push_back(Obj);
				}
			}
			if (Parent && Parent != &Document->GetScope())
			{
				Document->SetScope(*Parent);
			}
			Document->SetSelection(Siblings);
		})
		.OnMouseButtonDoubleClick([this](Object* Obj)
		{
			if (Document && FMapDocument::IsGroup(*Obj))
			{
				Document->SetScope(*Obj);
				return;
			}
			for (const TSharedPtr<FMapViewportClient>& View : Views)
			{
				View->FrameSelection();
			}
		})
	];
	RefreshOutliner();
	Tab->SetOnTabClosed(FOnTabClosed::CreateLambda([this](const TSharedRef<SDockTab>&) { Outliner.reset(); }));
	return Tab;
}

void FMapEditorShell::RefreshOutliner()
{
	OutlinerRoots.clear();
	if (Document)
	{
		Object& Root = Document->GetRoot();
		for (int Index = 0; Index < Root.GetNumChildren(); ++Index)
		{
			OutlinerRoots.push_back(&Root.GetChild(Index));
		}
	}
	if (!Outliner)
	{
		return;
	}
	bSyncingOutliner = true;
	if (Document)
	{
		// Open the groups down to the one being edited.
		for (Object* Group = &Document->GetScope(); Group && Group != &Document->GetRoot(); Group = Group->GetParentPtr())
		{
			Outliner->SetItemExpansion(Group, true);
		}
	}
	Outliner->RequestTreeRefresh();
	Outliner->ClearSelection();
	if (Document)
	{
		for (Object* Obj : Document->GetSelection())
		{
			Outliner->SetItemSelection(Obj, true);
		}
	}
	bSyncingOutliner = false;
}

// ----- Properties -----

TSharedRef<SDockTab> FMapEditorShell::MakePropertiesTab()
{
	const TSharedRef<SDockTab> Tab = SNew(SDockTab).Label("Properties")
	[
		SAssignNew(Properties, SPropertyGrid)
		.OnBeginTransaction([this](const FString& Description) { if (Document) Document->BeginTransaction(Description); })
		.OnEndTransaction([this] { if (Document && Document->IsInTransaction()) Document->EndTransaction(); })
	];
	RefreshProperties();
	Tab->SetOnTabClosed(FOnTabClosed::CreateLambda([this](const TSharedRef<SDockTab>&) { Properties.reset(); }));
	return Tab;
}

void FMapEditorShell::RefreshProperties()
{
	if (!Properties || bPropertiesRefreshQueued)
	{
		return;
	}
	// Next frame: the change may come from a widget in the grid, which the
	// rebuild would destroy under its own feet.
	bPropertiesRefreshQueued = true;
	const TWeakPtr<SPropertyGrid> WeakGrid = Properties;
	Properties->RegisterActiveTimer(0.0f, [this, WeakGrid](double, float)
	{
		bPropertiesRefreshQueued = false;
		const TSharedPtr<SPropertyGrid> Grid = WeakGrid.lock();
		if (!Grid)
		{
			return EActiveTimerReturn::Stop;
		}
		TArray<TSharedRef<IPropertySource>> Sources;
		if (Document && !Document->IsInTransaction())
		{
			for (Object* Obj : Document->GetSelection())
			{
				Sources.push_back(TSharedRef<IPropertySource>(std::make_shared<FMapObjectProperties>(*Document, *Obj)));
			}
		}
		if (Document && Document->IsInTransaction())
		{
			RefreshProperties(); // after the drag or edit ends
			return EActiveTimerReturn::Stop;
		}
		Grid->SetObjects(Sources);
		return EActiveTimerReturn::Stop;
	});
}

// ----- Prefabs -----

TSharedRef<SDockTab> FMapEditorShell::MakePrefabsTab()
{
	const TSharedRef<SDockTab> Tab = SNew(SDockTab).Label("Prefabs")
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SAssignNew(PrefabList, SListView<int32>)
			.ListItemsSource(&PrefabItems)
			.SelectionMode(ESelectionMode::Single)
			.OnGenerateRow([](int32 Index, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
			{
				const TArray<FMapEditorEnvironment::FPrefab>& Prefabs = FMapEditorEnvironment::GetPrefabs();
				const bool bValid = Index >= 0 && Index < static_cast<int32>(Prefabs.size());
				return SNew(STableRow<int32>, Owner)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(STextBlock).Text(bValid ? Prefabs[static_cast<SIZE_T>(Index)].Name : FString())]
					+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).TextStyle(&SmallText()).Text(bValid && Prefabs[static_cast<SIZE_T>(Index)].bUser ? FString("yours") : FString())]
				];
			})
			.OnMouseButtonDoubleClick([this](int32 Index)
			{
				const TArray<FMapEditorEnvironment::FPrefab>& Prefabs = FMapEditorEnvironment::GetPrefabs();
				if (Index >= 0 && Index < static_cast<int32>(Prefabs.size()))
				{
					AddPrefab(Prefabs[static_cast<SIZE_T>(Index)].Path);
				}
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(6.0f, 4.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
			[
				SNew(SButton).Text("Add").IsEnabled([this] { return Document && PrefabList && !PrefabList->GetSelectedItems().empty(); }).OnClicked([this]
				{
					const TArray<FMapEditorEnvironment::FPrefab>& Prefabs = FMapEditorEnvironment::GetPrefabs();
					for (const int32 Index : PrefabList->GetSelectedItems())
					{
						if (Index >= 0 && Index < static_cast<int32>(Prefabs.size()))
						{
							AddPrefab(Prefabs[static_cast<SIZE_T>(Index)].Path);
						}
					}
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton).Text("Save Selection...").IsEnabled([this] { return Document && !Document->GetSelection().empty(); }).OnClicked([this]
				{
					SaveSelectionAsPrefab();
					return FReply::Handled();
				})
			]
		]
	];
	RefreshPrefabList();
	Tab->SetOnTabClosed(FOnTabClosed::CreateLambda([this](const TSharedRef<SDockTab>&) { PrefabList.reset(); }));
	return Tab;
}

void FMapEditorShell::RefreshPrefabList()
{
	PrefabItems.clear();
	for (SIZE_T Index = 0; Index < FMapEditorEnvironment::GetPrefabs().size(); ++Index)
	{
		PrefabItems.push_back(static_cast<int32>(Index));
	}
	if (PrefabList)
	{
		PrefabList->RequestListRefresh();
	}
}

// ----- Settings -----

TSharedRef<SDockTab> FMapEditorShell::MakeSettingsTab()
{
	auto Items = std::make_shared<TArray<TSharedPtr<FUICommandInfo>>>();
	for (const char* ContextName : { "MapEditor", "MapViewport" })
	{
		if (const TSharedPtr<FBindingContext> Context = FInputBindingManager::Get().FindContext(ContextName))
		{
			for (const TSharedRef<FUICommandInfo>& Command : Context->GetCommands())
			{
				Items->push_back(Command.ToSharedPtr());
			}
		}
	}
	TSharedPtr<SPropertyGrid> Grid;
	const TSharedRef<SDockTab> Tab = SNew(SDockTab).Label("Settings")
	[
		// Settings above, key bindings below; each half clips, so a short
		// panel never draws one over the other.
		SNew(SSplitter).Orientation(EOrientation::Vertical)
		+ SSplitter::Slot().Value(0.45f).MinSize(60.0f)
		[
			SNew(SBox).Clipping(EWidgetClipping::ClipToBounds)[SAssignNew(Grid, SPropertyGrid)]
		]
		+ SSplitter::Slot().Value(0.55f).MinSize(60.0f)
		[
		SNew(SBox).Clipping(EWidgetClipping::ClipToBounds)
		[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(8.0f, 6.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).TextStyle(&SmallText()).AutoWrapText(true).Text("Key bindings: click a shortcut, then press the new keys.")
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton).Text("Reset All").OnClicked([this]
				{
					FInputBindingManager::Get().ResetToDefaults();
					Log("Key bindings reset");
					return FReply::Handled();
				})
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SListView<TSharedPtr<FUICommandInfo>>)
			.ListItemsSource(Items.get())
			.SelectionMode(ESelectionMode::None)
			.OnGenerateRow([this, Items](TSharedPtr<FUICommandInfo> Command, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
			{
				return SNew(STableRow<TSharedPtr<FUICommandInfo>>, Owner).Padding(FMargin(8.0f, 2.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(0.5f).VAlign(VAlign_Center)[SNew(STextBlock).Text(Command->GetLabel()).ToolTipText(Command->GetDescription())]
					+ SHorizontalBox::Slot().FillWidth(0.5f)[SNew(SChordEditor, ToSharedRef(Command), [this](const FString& Line) { Log(Line); })]
				];
			})
		]
		]
		]
	];
	Grid->SetObjects({ TSharedRef<IPropertySource>(std::make_shared<FEditorSettingsSource>(ViewSettings, [this](float Scale) { Host.SetAppScale(Scale); })) });
	return Tab;
}

// ----- Log -----

TSharedRef<SDockTab> FMapEditorShell::MakeLogTab()
{
	auto Items = std::make_shared<TArray<int32>>();
	TSharedPtr<SListView<int32>> List;
	const TSharedRef<SDockTab> Tab = SNew(SDockTab).Label("Log")
	[
		SAssignNew(List, SListView<int32>)
		.ListItemsSource(Items.get())
		.SelectionMode(ESelectionMode::None)
		.OnGenerateRow([this](int32 Index, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
		{
			return SNew(STableRow<int32>, Owner).Padding(FMargin(8.0f, 1.0f))
			[
				SNew(STextBlock).TextStyle(&SmallText()).Text(Index < static_cast<int32>(LogLines.size()) ? LogLines[static_cast<SIZE_T>(Index)] : FString())
			];
		})
	];
	const TWeakPtr<SListView<int32>> WeakList = List;
	List->RegisterActiveTimer(0.1f, [this, Items, WeakList](double, float)
	{
		const TSharedPtr<SListView<int32>> Pinned = WeakList.lock();
		if (Pinned && Items->size() != LogLines.size())
		{
			while (Items->size() < LogLines.size())
			{
				Items->push_back(static_cast<int32>(Items->size()));
			}
			Pinned->RequestListRefresh();
			Pinned->ScrollToBottom();
		}
		return EActiveTimerReturn::Continue;
	});
	return Tab;
}
