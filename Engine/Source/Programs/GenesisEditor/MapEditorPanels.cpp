// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// The map editor's dockable tabs, laid out as in Unreal: Place Actors and
// Brush Editing at the left, the viewports in the middle, the Outliner and
// Details at the right, and Content Browser 1 (2 to 4 from the Window menu)
// and the Output Log at the bottom.

#include "MapEditorShell.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/PropertyGrid/SPropertyGrid.h"
#include "MapCoreHeaders.h"
#include "MapEditorEnvironment.h"
#include "MapObjectProperties.h"
#include "SOutputLog.h"
#include "SPlaceActors.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SViewport.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const FTextBlockStyle& SmallText() { return FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText"); }

	const FName ViewportTabId("Viewport");
	const FName PlaceActorsTabId("PlaceActors");
	const FName BrushEditingTabId("BrushEditing");
	const FName OutlinerTabId("Outliner");
	const FName DetailsTabId("Details");
	const FName OutputLogTabId("OutputLog");

	// Unreal's names for the content browser tabs.
	FName ContentBrowserTabId(int32 Index) { return FName(FString::Format("ContentBrowserTab{}", Index + 1)); }

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
		if (Obj->HasMesh())
		{
			return FString::FromAnsi(Obj->GetMesh().GetName());
		}
		return FMapDocument::IsGroup(*Obj) ? FString("Group") : FString("Brush");
	}

	FString ObjectKind(Object* Obj)
	{
		if (FMapDocument::IsGroup(*Obj))
		{
			return FString::Format("Group of {}", Obj->GetNumChildren());
		}
		if (Obj->HasMesh())
		{
			return FString("Static Mesh");
		}
		return Obj->HasEntity() ? FString("Entity") : (Obj->HasBrush() ? FString("Brush") : FString("Object"));
	}

}

TSharedRef<FDockLayout> FMapEditorShell::MakeDefaultLayout()
{
	// Unreal's level editor: the left panel and the viewports above the
	// content browser and the log, the outliner and details at the right.
	// Version 4: Place Actors, content browsers and the output log.
	TSharedRef<FDockStackLayout> Bottom = FDockLayout::NewStack()->SetSizeCoefficient(0.3f)->AddTab(ContentBrowserTabId(0));
	for (int32 Index = 1; Index < MaxContentBrowsers; ++Index)
	{
		Bottom = Bottom->AddTab(ContentBrowserTabId(Index), ETabState::Closed); // opened from the Window menu, here
	}
	Bottom = Bottom->AddTab(OutputLogTabId)->SetForegroundTab(ContentBrowserTabId(0));
	return FDockLayout::New("MapEditor", 4)->AddArea(
		FDockLayout::NewPrimaryArea()->SetOrientation(EOrientation::Horizontal)
			->Split(FDockLayout::NewSplitter()->SetOrientation(EOrientation::Vertical)->SetSizeCoefficient(0.79f)
				->Split(FDockLayout::NewSplitter()->SetOrientation(EOrientation::Horizontal)->SetSizeCoefficient(0.7f)
					->Split(FDockLayout::NewStack()->SetSizeCoefficient(0.21f)->AddTab(PlaceActorsTabId)->AddTab(BrushEditingTabId)->SetForegroundTab(PlaceActorsTabId))
					->Split(FDockLayout::NewStack()->SetSizeCoefficient(0.79f)->AddTab(ViewportTabId)))
				->Split(Bottom))
			->Split(FDockLayout::NewSplitter()->SetOrientation(EOrientation::Vertical)->SetSizeCoefficient(0.21f)
				->Split(FDockLayout::NewStack()->SetSizeCoefficient(0.45f)->AddTab(OutlinerTabId))
				->Split(FDockLayout::NewStack()->SetSizeCoefficient(0.55f)->AddTab(DetailsTabId))));
}

void FMapEditorShell::RegisterTabs()
{
	FTabManager& Tabs = *TabManager;
	Tabs.RegisterTabSpawner(ViewportTabId, [this](const FSpawnTabArgs&) { return MakeViewportTab(); })
		.SetDisplayName("Viewport").SetToolTip("The map views: the layout and each view's type, mode and camera are in their toolbars");
	Tabs.RegisterTabSpawner(PlaceActorsTabId, [this](const FSpawnTabArgs&) { return MakePlaceActorsTab(); })
		.SetDisplayName("Place Actors").SetToolTip("Entities and geometry to drag into the viewports");
	Tabs.RegisterTabSpawner(BrushEditingTabId, [this](const FSpawnTabArgs&) { return MakeBrushEditingTab(); })
		.SetDisplayName("Brush Editing").SetToolTip("The Brush Editing mode's tools and brush operations");
	Tabs.RegisterTabSpawner(OutlinerTabId, [this](const FSpawnTabArgs&) { return MakeOutlinerTab(); })
		.SetDisplayName("Outliner").SetToolTip("The map's objects and groups (double-click a group to edit it)");
	Tabs.RegisterTabSpawner(DetailsTabId, [this](const FSpawnTabArgs&) { return MakeDetailsTab(); })
		.SetDisplayName("Details").SetToolTip("Name, position, texture and entity keys of the selection");
	for (int32 Index = 0; Index < MaxContentBrowsers; ++Index)
	{
		Tabs.RegisterTabSpawner(ContentBrowserTabId(Index), [this, Index](const FSpawnTabArgs&) { return MakeContentBrowserTab(Index); })
			.SetDisplayName(FString::Format("Content Browser {}", Index + 1)).SetGroup("Content Browser")
			.SetToolTip("The content folders: prefabs, maps and the editor's data");
	}
	Tabs.RegisterTabSpawner(OutputLogTabId, [this](const FSpawnTabArgs&) { return MakeOutputLogTab(); })
		.SetDisplayName("Output Log").SetToolTip("Everything the editor logs");
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
		.OnPlace([this](const FMapPlaceable& Item, FMapViewportClient& View, const Vector3d& Point)
		{
			if (Place(Item, Point, View.IsPerspective()) && PlaceActors)
			{
				PlaceActors->NoteRecent(Item);
			}
		})
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

// ----- Details -----

TSharedRef<SDockTab> FMapEditorShell::MakeDetailsTab()
{
	const TSharedRef<SDockTab> Tab = SNew(SDockTab).Label("Details")
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


// ----- Place Actors -----

TSharedRef<SDockTab> FMapEditorShell::MakePlaceActorsTab()
{
	const TSharedRef<SDockTab> Tab = SNew(SDockTab).Label("Place Actors")
	[
		SAssignNew(PlaceActors, SPlaceActors).OnPlace([this](const FMapPlaceable& Item) { Place(Item); })
	];
	PlaceActors->SetItems(GetPlaceables());
	Tab->SetOnTabClosed(FOnTabClosed::CreateLambda([this](const TSharedRef<SDockTab>&) { PlaceActors.reset(); }));
	return Tab;
}

void FMapEditorShell::RefreshPlaceables()
{
	if (PlaceActors)
	{
		PlaceActors->SetItems(GetPlaceables());
	}
	for (const TWeakPtr<SContentBrowser>& Weak : ContentBrowsers)
	{
		if (const TSharedPtr<SContentBrowser> Browser = Weak.lock())
		{
			Browser->Refresh();
		}
	}
}

// ----- Brush Editing -----

namespace
{
	// A command as a button in a mode panel: its label, its shortcut in the
	// tooltip, highlighted while it is on.
	class SModeCommandButton final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SModeCommandButton)
		SLATE_END_ARGS()

		void Construct(const FArguments&, const TSharedPtr<FUICommandInfo>& InCommand, const TSharedPtr<FUICommandList>& InList)
		{
			Command = InCommand;
			List = InList;
			ChildSlot
			[
				SNew(SButton)
				.IsFocusable(false)
				.Text(Command->GetLabel())
				.ToolTipText([this]
				{
					const FString Keys = Command->GetInputText();
					return FString::Format("{}{}\n{}", Command->GetLabel(), Keys.IsEmpty() ? FString() : FString::Format(" ({})", Keys), Command->GetDescription());
				})
				.IsEnabled([this] { return List->CanExecuteAction(*Command); })
				.OnClicked([this] { List->ExecuteAction(*Command); return FReply::Handled(); })
			];
		}

	protected:
		int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& Out, int32 LayerId,
			const FWidgetStyle& Style) const override
		{
			const int32 Top = SCompoundWidget::OnPaint(Args, Geometry, CullingRect, Out, LayerId, Style);
			if (Command->GetActionType() != EUserInterfaceActionType::Button && List->GetCheckState(*Command) == ECheckBoxState::Checked)
			{
				Out.AddRoundedBox(Top + 1, Geometry.ToPaintGeometry(), FColor(0, 0, 0, 0), 4.0f, FSlateColor(EStyleColor::Primary).Resolve(), 2.0f);
			}
			return Top + 1;
		}

	private:
		TSharedPtr<FUICommandInfo> Command;
		TSharedPtr<FUICommandList> List;
	};
}

TSharedRef<SDockTab> FMapEditorShell::MakeBrushEditingTab()
{
	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	const auto Group = [this, &Box](const char* Title, const char* Note, std::initializer_list<TSharedPtr<FUICommandInfo>> Buttons)
	{
		Box->AddSlot().AutoHeight()
		[
			SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Header")).Padding(FMargin(8.0f, 4.0f))
			[
				SNew(STextBlock).Text(Title).TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("BoldText"))
			]
		];
		if (Note[0])
		{
			Box->AddSlot().AutoHeight().Padding(FMargin(8.0f, 4.0f, 8.0f, 0.0f))[SNew(STextBlock).TextStyle(&SmallText()).AutoWrapText(true).Text(Note)];
		}
		const TSharedRef<SWrapBox> Row = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2(4.0f, 4.0f));
		for (const TSharedPtr<FUICommandInfo>& Command : Buttons)
		{
			Row->AddSlot()[SNew(SModeCommandButton, Command, Commands)];
		}
		Box->AddSlot().AutoHeight().Padding(FMargin(8.0f, 6.0f, 8.0f, 10.0f))[Row];
	};
	Group("Tools", "A left drag in the 2D views: Clip draws the cutting line; the others drag the selected brush's corners, edges or faces.",
		{ Cmd.ToolClip, Cmd.ToolVertex, Cmd.ToolEdge, Cmd.ToolFace });
	Group("Brush Operations", "", { Cmd.HollowIn, Cmd.HollowOut, Cmd.Subtract, Cmd.Intersect });
	Group("Transform", "", { Cmd.RotateLeft, Cmd.RotateRight, Cmd.FlipX, Cmd.FlipY, Cmd.FlipZ });
	Group("Groups", "", { Cmd.Group, Cmd.Ungroup, Cmd.EnterGroup, Cmd.ScopeUp });
	return SNew(SDockTab).Label("Brush Editing")[SNew(SScrollBox) + SScrollBox::Slot()[Box]];
}

// ----- Content Browsers -----

TSharedRef<SDockTab> FMapEditorShell::MakeContentBrowserTab(int32 Index)
{
	TArray<FContentBrowserRoot> Roots = { { "Content", FPaths::FindContentDir() } };
	TArray<FString> PrefabFolders = { FPaths::Combine(FPaths::Combine(FPaths::FindContentDir(), "Editor"), "brushes") };
	const FString& UserPrefabs = FMapEditorEnvironment::GetUserPrefabFolder();
	if (!UserPrefabs.IsEmpty())
	{
		Roots.push_back({ "Prefabs", UserPrefabs });
		PrefabFolders.push_back(UserPrefabs);
	}
	TSharedPtr<SContentBrowser> Browser;
	const TSharedRef<SDockTab> Tab = SNew(SDockTab).Label(FString::Format("Content Browser {}", Index + 1))
	[
		SAssignNew(Browser, SContentBrowser)
		.Roots(Roots)
		.PrefabFolders(PrefabFolders)
		.InitialPath(ContentBrowserPaths[Index])
		.OnAssetActivated([this](const FContentBrowserItem& Item) { OnAssetActivated(Item); })
		.OnSaveAll([this] { Commands->ExecuteAction(*Cmd.SaveMap); })
	];
	ContentBrowsers[Index] = Browser;
	Tab->SetOnTabClosed(FOnTabClosed::CreateLambda([this, Index](const TSharedRef<SDockTab>&)
	{
		if (const TSharedPtr<SContentBrowser> Closing = ContentBrowsers[Index].lock())
		{
			ContentBrowserPaths[Index] = Closing->GetPath();
		}
	}));
	return Tab;
}

void FMapEditorShell::OnAssetActivated(const FContentBrowserItem& Item)
{
	switch (Item.Type)
	{
	case EContentAssetType::Prefab:
		AddPrefab(Item.Path);
		break;
	case EContentAssetType::Map:
	case EContentAssetType::QuakeMap:
		if (ConfirmDiscard())
		{
			OpenMap(Item.Path);
		}
		break;
	default:
		Log(FString::Format("Nothing in the editor opens {} files ({})", ToString(Item.Type), Item.Name));
		break;
	}
}

// ----- Output Log -----

TSharedRef<SDockTab> FMapEditorShell::MakeOutputLogTab()
{
	return SNew(SDockTab).Label("Output Log")[SNew(SOutputLog)];
}
