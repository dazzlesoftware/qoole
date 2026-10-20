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
#include "MapHeightmap.h"
#include "MapLandscapeTextures.h"
#include "MapObjectProperties.h"
#include "Services/FileDialogs.h"
#include "Services/MessageDialog.h"
#include "Services/Services.h"
#include "SOutputLog.h"
#include "SPlaceActors.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SColorPicker.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Text/SEditableText.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SViewport.h"
#include "Widgets/Text/STextBlock.h"

#include <filesystem>

namespace
{
	const FTextBlockStyle& SmallText() { return FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText"); }

	const FName ViewportTabId("Viewport");
	const FName PlaceActorsTabId("PlaceActors");
	const FName BrushEditingTabId("BrushEditing");
	const FName LandscapeTabId("Landscape");
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
		if (Obj->HasLandscape())
		{
			return FString("Landscape");
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
	// Version 5: Place Actors, Landscape, content browsers and the output log.
	TSharedRef<FDockStackLayout> Bottom = FDockLayout::NewStack()->SetSizeCoefficient(0.3f)->AddTab(ContentBrowserTabId(0));
	for (int32 Index = 1; Index < MaxContentBrowsers; ++Index)
	{
		Bottom = Bottom->AddTab(ContentBrowserTabId(Index), ETabState::Closed); // opened from the Window menu, here
	}
	Bottom = Bottom->AddTab(OutputLogTabId)->SetForegroundTab(ContentBrowserTabId(0));
	return FDockLayout::New("MapEditor", 5)->AddArea(
		FDockLayout::NewPrimaryArea()->SetOrientation(EOrientation::Horizontal)
			->Split(FDockLayout::NewSplitter()->SetOrientation(EOrientation::Vertical)->SetSizeCoefficient(0.79f)
				->Split(FDockLayout::NewSplitter()->SetOrientation(EOrientation::Horizontal)->SetSizeCoefficient(0.7f)
					->Split(FDockLayout::NewStack()->SetSizeCoefficient(0.21f)->AddTab(PlaceActorsTabId)->AddTab(LandscapeTabId)->AddTab(BrushEditingTabId)->SetForegroundTab(PlaceActorsTabId))
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
	Tabs.RegisterTabSpawner(LandscapeTabId, [this](const FSpawnTabArgs&) { return MakeLandscapeTab(); })
		.SetDisplayName("Landscape").SetToolTip("The Landscape mode: new landscapes, sculpting tools and the brush");
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

// ----- Landscape -----

namespace
{
	// A header bar, as the mode panels have.
	TSharedRef<SWidget> PanelHeader(const char* Title)
	{
		return SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Header")).Padding(FMargin(8.0f, 4.0f))
			[
				SNew(STextBlock).Text(Title).TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("BoldText"))
			];
	}

	template <typename T>
	TSharedRef<SWidget> SpinRow(const char* Label, const char* Tip, TFunction<T()> Get, TFunction<void(T)> Set, T Min, T Max, int32 Digits)
	{
		return SNew(SHorizontalBox).ToolTipText(Tip)
			+ SHorizontalBox::Slot().FillWidth(0.5f).VAlign(VAlign_Center)[SNew(STextBlock).Text(Label)]
			+ SHorizontalBox::Slot().FillWidth(0.5f)
			[
				SNew(SSpinBox<T>).MinValue(Min).MaxValue(Max).FractionalDigits(Digits)
				.Value([Get] { return TOptional<T>(Get()); })
				.OnValueChanged([Set](T Value) { Set(Value); })
			];
	}

	// A button that shows when its choice is the current one.
	class SChoiceButton final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SChoiceButton)
			SLATE_ATTRIBUTE(bool, IsChosen, false)
			SLATE_EVENT(FSimpleDelegate, OnClicked)
		SLATE_END_ARGS()

		void Construct(const FArguments& Args, const FString& Label, const FString& Tip)
		{
			IsChosen.Assign(Args._IsChosen);
			const FSimpleDelegate Clicked = Args._OnClicked;
			ChildSlot[SNew(SButton).IsFocusable(false).Text(Label).ToolTipText(Tip).OnClicked([Clicked] { Clicked.ExecuteIfBound(); return FReply::Handled(); })];
		}

	protected:
		int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& Out, int32 LayerId,
			const FWidgetStyle& Style) const override
		{
			const int32 Top = SCompoundWidget::OnPaint(Args, Geometry, CullingRect, Out, LayerId, Style);
			if (IsChosen.Get())
			{
				Out.AddRoundedBox(Top + 1, Geometry.ToPaintGeometry(), FColor(0, 0, 0, 0), 4.0f, FSlateColor(EStyleColor::Primary).Resolve(), 2.0f);
			}
			return Top + 1;
		}

	private:
		TWidgetAttribute<bool, EInvalidateWidgetReason::Paint> IsChosen{ *this, false };
	};
}

TSharedRef<SDockTab> FMapEditorShell::MakeLandscapeTab()
{
	FMapLandscapeMode* Land = &LandscapeMode;
	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	const auto Pad = [&Box](const TSharedRef<SWidget>& Widget, float Top = 4.0f)
	{
		Box->AddSlot().AutoHeight().Padding(FMargin(8.0f, Top, 8.0f, 0.0f))[Widget];
	};

	// Manage: a new landscape, as Unreal's New Landscape panel.
	Box->AddSlot().AutoHeight()[PanelHeader("New Landscape")];
	Pad(SpinRow<int32>("Quads X", "Quads along X (Unreal's section size times components)", [Land] { return Land->NewQuadsX; },
		[Land](int32 V) { Land->NewQuadsX = V; }, 1, Landscape::MaxQuads, 0), 6.0f);
	Pad(SpinRow<int32>("Quads Y", "Quads along Y", [Land] { return Land->NewQuadsY; }, [Land](int32 V) { Land->NewQuadsY = V; }, 1, Landscape::MaxQuads, 0));
	Pad(SpinRow<float>("Quad Size", "Map units between heights", [Land] { return Land->NewQuadSize; }, [Land](float V) { Land->NewQuadSize = V; }, 1.0f, 1024.0f, 0));
	Pad(SNew(STextBlock).TextStyle(&SmallText()).AutoWrapText(true).Text([Land]
	{
		return FString::Format("{:.0f} x {:.0f} units, {} heights, centered on the origin.", Land->NewQuadsX * Land->NewQuadSize,
			Land->NewQuadsY * Land->NewQuadSize, (Land->NewQuadsX + 1) * (Land->NewQuadsY + 1));
	}));
	Pad(SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton).Text("Create").ToolTipText("Add a flat landscape of this size")
			.IsEnabled([this] { return Document != nullptr; })
			.OnClicked([this]
			{
				if (Object* Created = LandscapeMode.CreateLandscape(Vector3d(0.0f, 0.0f, 0.0f)))
				{
					Log(FString::Format("Created a landscape of {} x {} quads", Created->GetLandscape().GetQuadsX(), Created->GetLandscape().GetQuadsY()));
				}
				return FReply::Handled();
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))
		[
			SNew(SButton).Text("Import from File...")
			.ToolTipText("A landscape from a heightmap: one height per pixel, Quad Size apart (big images are resampled to fit)")
			.IsEnabled([this] { return Document != nullptr; })
			.OnClicked([this]
			{
				FString Path;
				if (PickLandscapeFile(false, "Import landscape from heightmap", "", Path))
				{
					FString Error;
					const Object* Created = LandscapeMode.ImportLandscape(Path, Vector3d(0.0f, 0.0f, 0.0f), Error);
					ReportLandscapeFile(Created != nullptr, Created ? FString::Format("Imported a landscape of {} x {} quads from {}",
						Created->GetLandscape().GetQuadsX(), Created->GetLandscape().GetQuadsY(), Path) : FString(), Error);
				}
				return FReply::Handled();
			})
		], 6.0f);
	// Heightmaps of the selected (or first) landscape, as Unreal's Import and
	// Export tools.
	Box->AddSlot().AutoHeight().Padding(FMargin(0.0f, 12.0f, 0.0f, 0.0f))[PanelHeader("Heightmap")];
	Pad(SpinRow<float>("Z Range", "Map units from black to white; mid-grey is height 0 (512 is Unreal's Z scale of 100)",
		[Land] { return Land->HeightmapZRange; }, [Land](float V) { Land->HeightmapZRange = V; }, 1.0f, 65536.0f, 0), 6.0f);
	Pad(SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton).Text("Import...").ToolTipText("New heights for the selected landscape (or the first) from a heightmap, stretched over it")
			.IsEnabled([Land] { return Land->GetTargetLandscape() != nullptr; })
			.OnClicked([this, Land]
			{
				FString Path;
				if (Object* Target = Land->GetTargetLandscape(); Target && PickLandscapeFile(false, "Import heightmap", "", Path))
				{
					FString Error;
					const bool bOk = Land->ImportHeightmap(*Target, Path, Error);
					ReportLandscapeFile(bOk, FString::Format("Imported heights from {}", Path), Error);
				}
				return FReply::Handled();
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))
		[
			SNew(SButton).Text("Export...").ToolTipText("The selected landscape's heights (or the first's) as a 16-bit heightmap, a pixel per height")
			.IsEnabled([Land] { return Land->GetTargetLandscape() != nullptr; })
			.OnClicked([this, Land]
			{
				FString Path;
				if (const Object* Target = Land->GetTargetLandscape(); Target && PickLandscapeFile(true, "Export heightmap", "Heightmap.png", Path))
				{
					FString Error;
					const bool bOk = Land->ExportHeightmap(*Target, Path, Error);
					ReportLandscapeFile(bOk, FString::Format("Exported heights to {}", Path), Error);
				}
				return FReply::Handled();
			})
		], 6.0f);
	Box->AddSlot().AutoHeight().Padding(FMargin(0.0f, 12.0f, 0.0f, 0.0f))[PanelHeader("Sculpt")];
	const TSharedRef<SWrapBox> Tools = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2(4.0f, 4.0f));
	const TPair<ELandscapeTool, const char*> ToolList[] = {
		{ ELandscapeTool::Sculpt, "Raise the ground under the brush (Shift lowers it)" },
		{ ELandscapeTool::Smooth, "Even out bumps under the brush" },
		{ ELandscapeTool::Flatten, "Pull the ground to the height where the stroke began" },
		{ ELandscapeTool::Noise, "Roughen the ground" },
		{ ELandscapeTool::Paint, "Paint the selected paint layer (Shift erases it)" },
	};
	for (const auto& [Tool, Tip] : ToolList)
	{
		const ELandscapeTool ThisTool = Tool;
		Tools->AddSlot()
		[
			SNew(SChoiceButton, FString(ToString(Tool)), FString(Tip))
			.IsChosen([Land, ThisTool] { return Land->Tool == ThisTool; })
			.OnClicked([this, Land, ThisTool]
			{
				Land->Tool = ThisTool;
				ViewSettings.Changed();
			})
		];
	}
	Pad(Tools, 6.0f);
	// Paint layers, as Unreal's Paint panel lists them.
	Box->AddSlot().AutoHeight().Padding(FMargin(0.0f, 12.0f, 0.0f, 0.0f))[PanelHeader("Paint Layers")];
	TSharedPtr<SBox> LayerHost;
	Pad(SAssignNew(LayerHost, SBox), 6.0f);
	LandscapeLayerHost = LayerHost;
	LandscapeLayerSignature = "(none yet)";
	LayerHost->SetContent(MakeLandscapeLayerList());
	Pad(SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton).Text("Add Layer").ToolTipText("A new paint layer on the selected landscape (or the first one)")
			.IsEnabled([Land] { return Land->GetTargetLandscape() != nullptr; })
			.OnClicked([this, Land]
			{
				if (Object* Target = Land->GetTargetLandscape(); Target && Land->AddLayer(*Target))
				{
					Land->Tool = ELandscapeTool::Paint;
					ViewSettings.Changed();
				}
				return FReply::Handled();
			})
		], 6.0f);
	// Weightmaps of the layer Paint is on, as Unreal's layer Import/Export.
	const auto HasLayer = [Land]
	{
		const Object* Target = Land->GetTargetLandscape();
		return Target && Land->SelectedLayer >= 0 && Land->SelectedLayer < Target->GetLandscape().GetNumLayers();
	};
	const auto LayerName = [Land, HasLayer]
	{
		return HasLayer() ? FString(Land->GetTargetLandscape()->GetLandscape().GetLayers()[static_cast<SIZE_T>(Land->SelectedLayer)].name) : FString();
	};
	Pad(SNew(STextBlock).TextStyle(&SmallText()).AutoWrapText(true).Text([HasLayer, LayerName]
	{
		return HasLayer() ? FString::Format("Weightmap of {} (white is all {}, black none):", LayerName(), LayerName())
			: FString("Weightmaps: add a layer first.");
	}), 8.0f);
	Pad(SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton).Text("Import...").ToolTipText("The layer's weights from a weightmap, stretched over the landscape (the other layers give way)")
			.IsEnabled(HasLayer)
			.OnClicked([this, Land, LayerName]
			{
				FString Path;
				const FString Name = LayerName();
				if (Object* Target = Land->GetTargetLandscape(); Target && PickLandscapeFile(false, FString::Format("Import {} weightmap", Name), "", Path))
				{
					FString Error;
					const bool bOk = Land->ImportWeightmap(*Target, Land->SelectedLayer, Path, Error);
					ReportLandscapeFile(bOk, FString::Format("Imported {} weights from {}", Name, Path), Error);
				}
				return FReply::Handled();
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))
		[
			SNew(SButton).Text("Export...").ToolTipText("The layer's weights as an 8-bit weightmap, a pixel per grid point")
			.IsEnabled(HasLayer)
			.OnClicked([this, Land, LayerName]
			{
				FString Path;
				const FString Name = LayerName();
				if (const Object* Target = Land->GetTargetLandscape();
					Target && PickLandscapeFile(true, FString::Format("Export {} weightmap", Name), Name + ".png", Path))
				{
					FString Error;
					const bool bOk = Land->ExportWeightmap(*Target, Land->SelectedLayer, Path, Error);
					ReportLandscapeFile(bOk, FString::Format("Exported {} weights to {}", Name, Path), Error);
				}
				return FReply::Handled();
			})
		], 4.0f);
	Box->AddSlot().AutoHeight().Padding(FMargin(0.0f, 12.0f, 0.0f, 0.0f))[PanelHeader("Brush")];
	Pad(SpinRow<float>("Brush Size", "The brush's radius in map units", [Land] { return Land->Brush.Radius; },
		[Land](float V) { Land->Brush.Radius = V; }, FLandscapeBrush::MinRadius, FLandscapeBrush::MaxRadius, 0), 6.0f);
	Pad(SpinRow<float>("Tool Strength", "How fast the tool acts", [Land] { return Land->Brush.Strength; }, [Land](float V) { Land->Brush.Strength = V; }, 0.01f, 1.0f, 2));
	Pad(SpinRow<float>("Brush Falloff", "The outer part of the radius over which the tool fades out", [Land] { return Land->Brush.Falloff; },
		[Land](float V) { Land->Brush.Falloff = V; }, 0.0f, 1.0f, 2));
	Pad(SNew(STextBlock).TextStyle(&SmallText()).AutoWrapText(true)
		.Text("Left drag over a landscape in any view to use the tool; the ring shows the brush. A stroke is one undo step."), 10.0f);
	return SNew(SDockTab).Label("Landscape")[SNew(SScrollBox) + SScrollBox::Slot()[Box]];
}

bool FMapEditorShell::PickLandscapeFile(bool bSave, const FString& Title, const FString& Name, FString& OutPath)
{
	FFileDialogOptions Options;
	Options.Title = Title;
	Options.Filters = FHeightmap::FileFilters();
	FString Folder;
	if (!FServices::GetConfig().GetString("MapEditor", "LandscapeFolder", Folder) || Folder.IsEmpty())
	{
		Folder = DefaultFolder();
	}
	Options.InitialPath = Name.IsEmpty() ? Folder : FString::FromWide((std::filesystem::path(Folder.ToWide()) / Name.ToWide()).wstring());
	Options.DefaultExtension = "png";
	const bool bPicked = bSave ? FServices::GetFileDialogs().SaveFile(Options, OutPath) : FServices::GetFileDialogs().OpenFile(Options, OutPath);
	if (bPicked)
	{
		FServices::GetConfig().SetString("MapEditor", "LandscapeFolder", FString::FromWide(std::filesystem::path(OutPath.ToWide()).parent_path().wstring()));
	}
	return bPicked;
}

bool FMapEditorShell::PickLandscapeTexture(FString& OutPath)
{
	FFileDialogOptions Options;
	Options.Title = "Paint layer texture";
	Options.Filters = { { "Images (*.png, *.bmp, *.jpg, *.tif)", "*.png;*.bmp;*.jpg;*.jpeg;*.tif;*.tiff" } };
	FString Folder;
	if (!FServices::GetConfig().GetString("MapEditor", "LandscapeTextureFolder", Folder) || Folder.IsEmpty())
	{
		Folder = DefaultFolder();
	}
	Options.InitialPath = Folder;
	if (!FServices::GetFileDialogs().OpenFile(Options, OutPath))
	{
		return false;
	}
	FServices::GetConfig().SetString("MapEditor", "LandscapeTextureFolder", FString::FromWide(std::filesystem::path(OutPath.ToWide()).parent_path().wstring()));
	// Read it now, so a file that is no image says so here.
	FLandscapeImage Image;
	FString Error;
	if (!FLandscapeTextureLibrary::LoadImageFile(OutPath, Image, Error))
	{
		ReportLandscapeFile(false, FString(), Error);
		return false;
	}
	return true;
}

void FMapEditorShell::RefreshLandscapeLayersNow()
{
	LandscapeLayerSignature = "(changed)";
	RefreshLandscapeLayers();
}

void FMapEditorShell::ReportLandscapeFile(bool bOk, const FString& Done, const FString& Error)
{
	if (bOk)
	{
		Log(Done);
		return;
	}
	Log(Error);
	FServices::GetMessageDialog().Show(EMessageType::Error, EMessageButtons::Ok, Error);
}

void FMapEditorShell::RefreshLandscapeLayers()
{
	const TSharedPtr<SBox> LayerHost = LandscapeLayerHost.lock();
	if (!LayerHost || bLandscapeLayersQueued)
	{
		return;
	}
	// Only when the list would change: painting changes weights, not layers.
	FString Signature;
	if (const Object* Target = LandscapeMode.GetTargetLandscape())
	{
		Signature = FString::Format("{}", static_cast<const void*>(Target));
		for (const LandscapeLayer& Layer : Target->GetLandscape().GetLayers())
		{
			Signature += FString::Format("|{} {} {} {} {}", Layer.name, Layer.r, Layer.g, Layer.b, Layer.texture);
		}
	}
	if (Signature == LandscapeLayerSignature)
	{
		return;
	}
	LandscapeLayerSignature = Signature;
	// Next frame: the change may come from a widget in the list.
	bLandscapeLayersQueued = true;
	LayerHost->RegisterActiveTimer(0.0f, [this, WeakHost = LandscapeLayerHost](double, float)
	{
		bLandscapeLayersQueued = false;
		if (const TSharedPtr<SBox> Pinned = WeakHost.lock())
		{
			Pinned->SetContent(MakeLandscapeLayerList());
		}
		return EActiveTimerReturn::Stop;
	});
}

TSharedRef<SWidget> FMapEditorShell::MakeLandscapeLayerList()
{
	FMapLandscapeMode* Land = &LandscapeMode;
	Object* Target = Land->GetTargetLandscape();
	if (!Target)
	{
		return SNew(STextBlock).TextStyle(&SmallText()).AutoWrapText(true).Text("Create a landscape (or select one) to paint it.");
	}
	const TArray<FString> Names = [Target]
	{
		TArray<FString> Out;
		for (const LandscapeLayer& Layer : Target->GetLandscape().GetLayers())
		{
			Out.push_back(FString(Layer.name));
		}
		return Out;
	}();
	if (Names.empty())
	{
		return SNew(STextBlock).TextStyle(&SmallText()).AutoWrapText(true).Text("No paint layers yet: Add Layer makes one (Grass first).");
	}
	// Each row: the layer's color (click to change it), its name, Paint to
	// paint with it, and x to remove it.
	const auto ColorOf = [Land](int32 Index)
	{
		const Object* Current = Land->GetTargetLandscape();
		if (!Current || Index >= Current->GetLandscape().GetNumLayers())
		{
			return FColor::White();
		}
		const LandscapeLayer& Layer = Current->GetLandscape().GetLayers()[static_cast<SIZE_T>(Index)];
		return FColor(Layer.r, Layer.g, Layer.b);
	};
	const TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (int32 Index = 0; Index < static_cast<int32>(Names.size()); ++Index)
	{
		List->AddSlot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 4.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
			[
				SNew(SComboButton).HasDownArrow(false).IsFocusable(false).ToolTipText("The layer's color")
				.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("FlatButton"))
				.ButtonContent()[SNew(SColorBlock).Size(FVector2(24.0f, 16.0f)).Color([ColorOf, Index] { return ColorOf(Index); })]
				.OnGetMenuContent([this, Land, ColorOf, Index]() -> TSharedRef<SWidget>
				{
					return SNew(SBox).WidthOverride(280.0f).Padding(FMargin(8.0f))
					[
						SNew(SColorPicker).UseAlpha(false)
						.TargetColor([ColorOf, Index] { return ColorOf(Index); })
						.OnInteractionBegin([this] { if (Document && !Document->IsInTransaction()) Document->BeginTransaction("Change Paint Layer Color"); })
						.OnInteractionEnd([this] { if (Document && Document->IsInTransaction()) Document->EndTransaction(); })
						.OnColorChanged([Land, Index](FColor Color)
						{
							if (Object* Current = Land->GetTargetLandscape())
							{
								Land->SetLayerColor(*Current, Index, Color);
							}
						})
					];
				})
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
			[
				SNew(SEditableTextBox).Text(Names[static_cast<SIZE_T>(Index)]).ToolTipText("The layer's name")
				.OnTextCommitted([Land, Index](const FString& Name, ETextCommit)
				{
					if (Object* Current = Land->GetTargetLandscape(); Current && !Name.TrimStartAndEnd().IsEmpty())
					{
						Land->RenameLayer(*Current, Index, Name.TrimStartAndEnd());
					}
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 4.0f, 0.0f))
			[
				SNew(SChoiceButton, FString("Paint"), FString("Paint this layer"))
				.IsChosen([Land, Index] { return Land->Tool == ELandscapeTool::Paint && Land->SelectedLayer == Index; })
				.OnClicked([this, Land, Index]
				{
					Land->SelectedLayer = Index;
					Land->Tool = ELandscapeTool::Paint;
					ViewSettings.Changed();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton).Text("x").IsFocusable(false).ToolTipText("Remove this layer (its paint goes with it)").OnClicked([Land, Index]
				{
					if (Object* Current = Land->GetTargetLandscape())
					{
						Land->RemoveLayer(*Current, Index);
					}
					return FReply::Handled();
				})
			]
		];
		// Under it, its texture (Unreal's layer material) and how far it
		// repeats.
		const std::string Current = Target->GetLandscape().GetLayers()[static_cast<SIZE_T>(Index)].texture;
		const auto Choices = std::make_shared<TArray<FString>>();
		Choices->push_back("None");
		for (const FString& Name : FLandscapeTextureLibrary::GetBuiltInNames())
		{
			Choices->push_back(Name);
		}
		const bool bFile = !Current.empty() && Current.rfind("builtin:", 0) != 0;
		if (bFile)
		{
			Choices->push_back(FLandscapeTextureLibrary::DisplayName(Current));
		}
		Choices->push_back("From File...");
		const FString Shown = FLandscapeTextureLibrary::DisplayName(Current);
		List->AddSlot().AutoHeight().Padding(FMargin(30.0f, 0.0f, 0.0f, 8.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
			[
				SNew(SComboBox<FString>).OptionsSource(Choices.get()).InitiallySelectedItem(TOptional<FString>(Shown))
				.ToolTipText("The layer's texture: one of the built-in ones (tinted with its color), an image file, or none (its plain color)")
				.OnGenerateWidget([](FString Choice) -> TSharedRef<SWidget> { return SNew(STextBlock).Text(Choice); })
				.OnSelectionChanged([this, Land, Index, Choices, Current, bFile](FString Choice, ESelectInfo)
				{
					Object* Layered = Land->GetTargetLandscape();
					if (!Layered)
					{
						return;
					}
					FString Texture;
					if (Choice == "From File...")
					{
						FString Path;
						if (!PickLandscapeTexture(Path))
						{
							RefreshLandscapeLayersNow();
							return;
						}
						Texture = Path;
					}
					else if (bFile && Choice == FLandscapeTextureLibrary::DisplayName(Current))
					{
						return;
					}
					else if (Choice != "None")
					{
						Texture = FLandscapeTextureLibrary::BuiltIn(Choice);
					}
					if (Land->SetLayerTexture(*Layered, Index, Texture))
					{
						Log(FString::Format("Paint layer texture: {}", Texture.IsEmpty() ? FString("none") : Texture));
					}
				})
				[SNew(STextBlock).Text(Shown)]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 4.0f, 0.0f))
			[
				SNew(STextBlock).TextStyle(&SmallText()).Text("Tiling")
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(64.0f)
				[
					SNew(SSpinBox<float>).MinValue(1.0f).MaxValue(65536.0f).FractionalDigits(0)
					.ToolTipText("Map units the texture repeats over")
					.Value([Land, Index]() -> TOptional<float>
					{
						const Object* Layered = Land->GetTargetLandscape();
						if (!Layered || Index >= Layered->GetLandscape().GetNumLayers())
						{
							return {};
						}
						return Layered->GetLandscape().GetLayers()[static_cast<SIZE_T>(Index)].tiling;
					})
					.OnBeginSliderMovement([this] { if (Document && !Document->IsInTransaction()) Document->BeginTransaction("Change Paint Layer Tiling"); })
					.OnEndSliderMovement([this](float) { if (Document && Document->IsInTransaction()) Document->EndTransaction(); })
					.OnValueChanged([Land, Index](float Tiling)
					{
						if (Object* Layered = Land->GetTargetLandscape())
						{
							Land->SetLayerTiling(*Layered, Index, Tiling);
						}
					})
				]
			]
		];
	}
	return List;
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
