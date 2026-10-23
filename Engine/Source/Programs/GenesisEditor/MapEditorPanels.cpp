// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// The map editor's dockable tabs, laid out as in Unreal: Place Actors and
// Brush Editing at the left, the viewports in the middle, the Outliner and
// Details at the right, and Content Browser 1 (2 to 4 from the Window menu)
// and the Output Log at the bottom.

#include "MapEditorShell.h"
#include "Containers/ContainerAliases.h"
#include "Templates/SharedPointer.h"
#include "Internationalization/TextLocalization.h"
#include "MaterialEditor.h"
#include "ProjectDescriptor.h"
#include "AssetRegistry.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/PropertyGrid/SPropertyGrid.h"
#include "MapCoreHeaders.h"
#include "MapEditorEnvironment.h"
#include "SLandscapeEditorPanel.h"
#include "SFoliageEditorPanel.h"
#include "MapAssetLibrary.h"
#include "MapObjectProperties.h"
#include "Services/FileDialogs.h"
#include "MeshAssetEditor.h"
#include "TextureAssetEditor.h"
#include "STextureAssetPreview.h"
#include "SlateRHIRenderer.h"
#include "Widgets/SViewport.h"
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
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#include <filesystem>

namespace
{
	const FTextBlockStyle& SmallText()
	{
		return FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText");
	}

	const FName ViewportTabId("Viewport");
	const FName PlaceActorsTabId("PlaceActors");
	const FName BrushEditingTabId("BrushEditing");
	const FName LandscapeTabId("Landscape");
	const FName FoliageTabId("Foliage");
	const FName OutlinerTabId("Outliner");
	const FName DetailsTabId("Details");
	const FName OutputLogTabId("OutputLog");

	// Unreal's names for the content browser tabs.
	FName ContentBrowserTabId(int32 Index)
	{
		return FName(FString::Format("ContentBrowserTab{}", Index + 1));
	}

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
			return FString::FromAnsi(Obj->GetMeshName());
		}
		return FMapDocument::IsGroup(*Obj) ? FString("Group") : FString("Brush");
	}

	FString ObjectKind(Object* Obj)
	{
		if (FMapDocument::IsGroup(*Obj))
		{
			return FTextLocalization::Format("Group of {}", Obj->GetNumChildren());
		}
		if (Obj->HasLandscape())
		{
			return FString("Landscape");
		}
		if (Obj->HasFoliage())
		{
			return FString::Format("Foliage, {}", Obj->GetFoliage().GetNumInstances());
		}
		if (Obj->HasMesh())
		{
			return FString("Static Mesh");
		}
		return Obj->HasEntity() ? FString("Entity") : (Obj->HasBrush() ? FString("Brush") : FString("Object"));
	}

} // namespace

TSharedRef<FDockLayout> FMapEditorShell::MakeDefaultLayout()
{
	// Unreal's level editor: the left panel and the viewports above the
	// content browser and the log, the outliner and details at the right.
	// Version 6: Place Actors, Landscape, Foliage, content browsers and the
	// output log.
	TSharedRef<FDockStackLayout> Bottom =
	    FDockLayout::NewStack()->SetSizeCoefficient(0.3f)->AddTab(ContentBrowserTabId(0));
	for (int32 Index = 1; Index < MaxContentBrowsers; ++Index)
	{
		Bottom = Bottom->AddTab(ContentBrowserTabId(Index),
		    ETabState::Closed); // opened from the Window menu, here
	}
	Bottom = Bottom->AddTab(OutputLogTabId)->SetForegroundTab(ContentBrowserTabId(0));
	return FDockLayout::New("MapEditor", 6)
	    ->AddArea(FDockLayout::NewPrimaryArea()
	            ->SetOrientation(EOrientation::Horizontal)
	            ->Split(FDockLayout::NewSplitter()
	                    ->SetOrientation(EOrientation::Vertical)
	                    ->SetSizeCoefficient(0.79f)
	                    ->Split(FDockLayout::NewSplitter()
	                            ->SetOrientation(EOrientation::Horizontal)
	                            ->SetSizeCoefficient(0.7f)
	                            ->Split(FDockLayout::NewStack()
	                                    ->SetSizeCoefficient(0.21f)
	                                    ->AddTab(PlaceActorsTabId)
	                                    ->AddTab(LandscapeTabId)
	                                    ->AddTab(FoliageTabId)
	                                    ->AddTab(BrushEditingTabId)
	                                    ->SetForegroundTab(PlaceActorsTabId))
	                            ->Split(FDockLayout::NewStack()->SetSizeCoefficient(0.79f)->AddTab(ViewportTabId)))
	                    ->Split(Bottom))
	            ->Split(FDockLayout::NewSplitter()
	                    ->SetOrientation(EOrientation::Vertical)
	                    ->SetSizeCoefficient(0.21f)
	                    ->Split(FDockLayout::NewStack()->SetSizeCoefficient(0.45f)->AddTab(OutlinerTabId))
	                    ->Split(FDockLayout::NewStack()->SetSizeCoefficient(0.55f)->AddTab(DetailsTabId))));
}

void FMapEditorShell::RegisterTabs()
{
	FTabManager& Tabs = *TabManager;
	Tabs.RegisterTabSpawner(ViewportTabId, [this](const FSpawnTabArgs&) { return MakeViewportTab(); })
	    .SetDisplayName("Viewport")
	    .SetToolTip(
	        GENESIS_TEXT("The map views: the layout and each view's type, mode and camera are in their toolbars"));
	Tabs.RegisterTabSpawner(PlaceActorsTabId, [this](const FSpawnTabArgs&) { return MakePlaceActorsTab(); })
	    .SetDisplayName("Place Actors")
	    .SetToolTip(GENESIS_TEXT("Entities and geometry to drag into the viewports"));
	Tabs.RegisterTabSpawner(LandscapeTabId, [this](const FSpawnTabArgs&) { return MakeLandscapeTab(); })
	    .SetDisplayName("Landscape")
	    .SetToolTip(GENESIS_TEXT("The Landscape mode: new landscapes, sculpting tools and the brush"));
	Tabs.RegisterTabSpawner(FoliageTabId, [this](const FSpawnTabArgs&) { return MakeFoliageTab(); })
	    .SetDisplayName("Foliage")
	    .SetToolTip(GENESIS_TEXT("The Foliage mode: foliage types and the tools that paint them"));
	Tabs.RegisterTabSpawner(BrushEditingTabId, [this](const FSpawnTabArgs&) { return MakeBrushEditingTab(); })
	    .SetDisplayName("Brush Editing")
	    .SetToolTip(GENESIS_TEXT("The Brush Editing mode's tools and brush operations"));
	Tabs.RegisterTabSpawner(OutlinerTabId, [this](const FSpawnTabArgs&) { return MakeOutlinerTab(); })
	    .SetDisplayName("Outliner")
	    .SetToolTip(GENESIS_TEXT("The map's objects and groups (double-click a group to edit it)"));
	Tabs.RegisterTabSpawner(DetailsTabId, [this](const FSpawnTabArgs&) { return MakeDetailsTab(); })
	    .SetDisplayName("Details")
	    .SetToolTip(GENESIS_TEXT("Name, position, texture and entity keys of the selection"));
	for (int32 Index = 0; Index < MaxContentBrowsers; ++Index)
	{
		Tabs.RegisterTabSpawner(ContentBrowserTabId(Index),
		        [this, Index](const FSpawnTabArgs&) { return MakeContentBrowserTab(Index); })
		    .SetDisplayName(FTextLocalization::Format("Content Browser {}", Index + 1))
		    .SetGroup("Content Browser")
		    .SetToolTip(GENESIS_TEXT("The content folders: prefabs, maps and the editor's data"));
	}
	Tabs.RegisterTabSpawner(OutputLogTabId, [this](const FSpawnTabArgs&) { return MakeOutputLogTab(); })
	    .SetDisplayName("Output Log")
	    .SetToolTip(GENESIS_TEXT("Everything the editor logs"));
}

// ----- Viewports -----

TSharedRef<SDockTab> FMapEditorShell::MakeViewportTab()
{
	// The tools' buttons in the viewport toolbars run the editor's commands,
	// so their tooltips show the shortcuts.
	TArray<TSharedPtr<FUICommandInfo>> ToolCommands = {
	    Cmd.ToolSelect, Cmd.ToolRotate, Cmd.ToolScale, Cmd.ToolClip, Cmd.ToolVertex, Cmd.ToolEdge, Cmd.ToolFace};
	const TSharedRef<SDockTab> Tab =
	    SNew(SDockTab)
	        .Label("Viewport")
	        .Role(ETabRole::Document)[SAssignNew(ViewportLayout, SMapViewportLayout, Views, &ViewSettings)
	                .Layout(ViewportLayoutType)
	                .ToolCommands(ToolCommands)
	                .ToolCommandList(Commands)
	                .OnPlace([this](const FMapPlaceable& Item, FMapViewportClient& View, const Vector3d& Point)
	{
		if (Place(Item, Point, View.IsPerspective()) && PlaceActors)
		{
			PlaceActors->NoteRecent(Item);
		}
	})];
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
	const TSharedRef<SDockTab> Tab = SNew(SDockTab).Label("Outliner")[SAssignNew(Outliner, STreeView<Object*>)
	        .TreeItemsSource(&OutlinerRoots)
	        .SelectionMode(ESelectionMode::Multi)
	        .OnGetChildren(
	            [](Object* Obj, TArray<Object*>& OutChildren)
	{
		if (FMapDocument::IsGroup(*Obj))
		{
			for (int Index = 0; Index < Obj->GetNumChildren(); ++Index)
			{
				OutChildren.push_back(&Obj->GetChild(Index));
			}
		}
	})
	        .OnGenerateRow(
	            [](Object* Obj, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
	{
		// Texts are read once: an object can be freed (a brush cut away)
		// before the tree is rebuilt, so rows must not look at it later.
		return SNew(STableRow<Object*>, Owner)[SNew(SHorizontalBox) +
		    SHorizontalBox::Slot().FillWidth(
		        1.0f)[SNew(STextBlock).Clipping(EWidgetClipping::ClipToBounds).Text(ObjectLabel(Obj))] +
		    SHorizontalBox::Slot().AutoWidth().Padding(
		        FMargin(8.0f, 0.0f, 0.0f, 0.0f))[SNew(STextBlock).TextStyle(&SmallText()).Text(ObjectKind(Obj))]];
	})
	        .OnSelectionChanged(
	            [this](Object*, ESelectInfo)
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
	})];
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
		for (Object* Group = &Document->GetScope(); Group && Group != &Document->GetRoot();
		    Group = Group->GetParentPtr())
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
	const TSharedRef<SDockTab> Tab = SNew(SDockTab).Label("Details")[SAssignNew(Properties, SPropertyGrid)
	        .OnBeginTransaction(
	            [this](const FString& Description)
	{
		if (Document)
			Document->BeginTransaction(Description);
	})
	        .OnEndTransaction([this]
	{
		if (Document && Document->IsInTransaction())
			Document->EndTransaction();
	})];
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
	const TSharedRef<SDockTab> Tab = SNew(SDockTab).Label(
	    "Place Actors")[SAssignNew(PlaceActors, SPlaceActors).OnPlace([this](const FMapPlaceable& Item) {
		Place(Item);
	})];
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
			Browser->Refresh(false);
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

		void Construct(
		    const FArguments&, const TSharedPtr<FUICommandInfo>& InCommand, const TSharedPtr<FUICommandList>& InList)
		{
			Command = InCommand;
			List = InList;
			ChildSlot[SNew(SButton)
			        .IsFocusable(false)
			        .Text(Command->GetLabel())
			        .ToolTipText(
			            [this]
			{
				const FString Keys = Command->GetInputText();
				return FString::Format("{}{}\n{}", Command->GetLabel(),
				    Keys.IsEmpty() ? FString() : FString::Format(" ({})", Keys), Command->GetDescription());
			})
			        .IsEnabled([this] { return List->CanExecuteAction(*Command); })
			        .OnClicked([this]
			{
				List->ExecuteAction(*Command);
				return FReply::Handled();
			})];
		}

	protected:
		int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect,
		    FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& Style) const override
		{
			const int32 Top = SCompoundWidget::OnPaint(Args, Geometry, CullingRect, Out, LayerId, Style);
			if (Command->GetActionType() != EUserInterfaceActionType::Button &&
			    List->GetCheckState(*Command) == ECheckBoxState::Checked)
			{
				Out.AddRoundedBox(Top + 1, Geometry.ToPaintGeometry(), FColor(0, 0, 0, 0), 4.0f,
				    FSlateColor(EStyleColor::Primary).Resolve(), 2.0f);
			}
			return Top + 1;
		}

	private:
		TSharedPtr<FUICommandInfo> Command;
		TSharedPtr<FUICommandList> List;
	};
} // namespace

TSharedRef<SDockTab> FMapEditorShell::MakeBrushEditingTab()
{
	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	const TFunction<void(const char*, const char*, std::initializer_list<TSharedPtr<FUICommandInfo>>)> Group =
	    [this, &Box](const char* Title, const char* Note, std::initializer_list<TSharedPtr<FUICommandInfo>> Buttons)
	{
		Box->AddSlot().AutoHeight()[SNew(SBorder)
		        .BorderImage(&FAppStyle::Get().GetBrush("Header"))
		        .Padding(FMargin(8.0f, 4.0f))[SNew(STextBlock)
		                .Text(Title)
		                .TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("BoldText"))]];
		if (Note[0])
		{
			Box->AddSlot().AutoHeight().Padding(FMargin(
			    8.0f, 4.0f, 8.0f, 0.0f))[SNew(STextBlock).TextStyle(&SmallText()).AutoWrapText(true).Text(Note)];
		}
		const TSharedRef<SWrapBox> Row = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2(4.0f, 4.0f));
		for (const TSharedPtr<FUICommandInfo>& Command : Buttons)
		{
			Row->AddSlot()[SNew(SModeCommandButton, Command, Commands)];
		}
		Box->AddSlot().AutoHeight().Padding(FMargin(8.0f, 6.0f, 8.0f, 10.0f))[Row];
	};
	Group("Tools",
	    "A left drag in the 2D views: Clip draws the cutting line; the others drag the selected brush's "
	    "corners, edges or faces.",
	    {Cmd.ToolClip, Cmd.ToolVertex, Cmd.ToolEdge, Cmd.ToolFace});
	Group("Brush Operations", "", {Cmd.HollowIn, Cmd.HollowOut, Cmd.Subtract, Cmd.Intersect});
	Group("Transform", "", {Cmd.RotateLeft, Cmd.RotateRight, Cmd.FlipX, Cmd.FlipY, Cmd.FlipZ});
	Group("Groups", "", {Cmd.Group, Cmd.Ungroup, Cmd.EnterGroup, Cmd.ScopeUp});
	return SNew(SDockTab).Label("Brush Editing")[SNew(SScrollBox) + SScrollBox::Slot()[Box]];
}

// ----- Landscape -----

FMapFeaturePanelHost FMapEditorShell::MakeFeaturePanelHost()
{
	FMapFeaturePanelHost HostServices;
	HostServices.GetDocument = [this] { return Document.get(); };
	HostServices.InvalidateViews = [this] { ViewSettings.Changed(); };
	HostServices.Log = [this](const FString& Message) { Log(Message); };
	HostServices.GetDefaultFolder = [this] { return DefaultFolder(); };
	return HostServices;
}

TSharedRef<SDockTab> FMapEditorShell::MakeLandscapeTab()
{
	TSharedPtr<SLandscapeEditorPanel> Panel;
	const TSharedRef<SLandscapeEditorPanel> Widget =
	    SAssignNew(Panel, SLandscapeEditorPanel, &LandscapeMode, MakeFeaturePanelHost());
	LandscapePanel = Panel;
	return SNew(SDockTab).Label("Landscape")[Widget];
}

TSharedRef<SDockTab> FMapEditorShell::MakeFoliageTab()
{
	TSharedPtr<SFoliageEditorPanel> Panel;
	const TSharedRef<SFoliageEditorPanel> Widget =
	    SAssignNew(Panel, SFoliageEditorPanel, &FoliageMode, MakeFeaturePanelHost());
	FoliagePanel = Panel;
	return SNew(SDockTab).Label("Foliage")[Widget];
}

void FMapEditorShell::RefreshLandscapeLayers()
{
	if (TSharedPtr<SLandscapeEditorPanel> Panel = LandscapePanel.lock())
		Panel->RefreshLandscapeLayers();
}

void FMapEditorShell::RefreshFoliageTypes()
{
	if (TSharedPtr<SFoliageEditorPanel> Panel = FoliagePanel.lock())
		Panel->RefreshFoliageTypes();
}

TSharedRef<SDockTab> FMapEditorShell::MakeContentBrowserTab(int32 Index)
{
	TArray<FContentBrowserRoot> Roots;
	if (const FProjectContext* Project = FProjectContext::GetActive())
		Roots.push_back({"Content", Project->GetContentDir()});
	Roots.push_back({"Engine Content", FPaths::FindContentDir()});
	TArray<FString> PrefabFolders = {FPaths::Combine(FPaths::Combine(FPaths::FindContentDir(), "Editor"), "brushes")};
	const FString& UserPrefabs = FMapEditorEnvironment::GetUserPrefabFolder();
	if (!UserPrefabs.IsEmpty())
	{
		Roots.push_back({"Prefabs", UserPrefabs});
		PrefabFolders.push_back(UserPrefabs);
	}
	TSharedPtr<SContentBrowser> Browser;
	const TSharedRef<SDockTab> Tab = SNew(SDockTab).Label(
	    FTextLocalization::Format("Content Browser {}", Index + 1))[SAssignNew(Browser, SContentBrowser)
	        .Roots(Roots)
	        .Registry(AssetRegistry)
	        .PrefabFolders(PrefabFolders)
	        .InitialPath(ContentBrowserPaths[Index])
	        .OnAssetActivated([this](const FContentBrowserItem& Item) { OnAssetActivated(Item); })
	        .OnSaveAll([this] { Commands->ExecuteAction(*Cmd.SaveMap); })
	        .OnCreateMesh([this](const FString& File, FString& Error)
	{
		const TArray<Object*> Selected = Document->GetSelection();
		if (Selected.size() != 1 || !Selected[0]->HasMesh() || Selected[0]->HasLandscape())
		{
			Error = GENESIS_TEXT("Select one static mesh actor to create a mesh asset.");
			return false;
		}
		FStaticMeshAsset Asset;
		if (const std::shared_ptr<const FLoadedMeshAsset> Existing = Selected[0]->GetMeshAsset())
		{
			Asset = Existing->Data;
			if (!Existing->MaterialOverride.IsNull())
				Asset.Material = Existing->MaterialOverride;
			for (size_t I = 0; I < Existing->AdditionalOverrides.size(); ++I)
				if (!Existing->AdditionalOverrides[I].IsNull())
					Asset.AdditionalMaterials[I] = Existing->AdditionalOverrides[I];
		}
		else
			Asset.Mesh = Selected[0]->GetMeshPtr();
		FAssetPackage P;
		return FRenderAssetCodec::EncodeMesh(Asset, P, Error) && FAssetPackageStore::Save(File, P, Error);
	})];
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
	case EContentAssetType::StaticMesh:
	{
		OpenMeshAsset(Item);
		break;
	}
	case EContentAssetType::Material:
	case EContentAssetType::MaterialFunction:
	case EContentAssetType::MaterialParameterCollection:
		OpenMaterialAsset(Item);
		break;
	case EContentAssetType::Texture:
		if (Item.Schema == "Genesis.Texture2D")
			OpenTextureAsset(Item);
		else
			Log("Import this image to create a native texture asset first.");
		break;
	case EContentAssetType::Prefab:
		if (AssetRegistry && !Item.PackagePath.IsEmpty())
		{
			FString Error;
			const FAssetData* Data =
			    AssetRegistry->Resolve({Item.PackageId, Item.PackagePath}, Error, "Genesis.Prefab");
			if (Data)
				AddPrefab(Data->FilePath, Data->Metadata.Id);
			else
				Log(Error);
		}
		else
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
		Log(Item.Schema.IsEmpty()
		        ? FTextLocalization::Format("Nothing in the editor opens {} files ({})", ToString(Item.Type), Item.Name)
		        : FTextLocalization::Format("No asset editor is registered for {} ({})", Item.Schema, Item.Name));
		break;
	}
}

// ----- Output Log -----

TSharedRef<SDockTab> FMapEditorShell::MakeOutputLogTab()
{
	return SNew(SDockTab).Label("Output Log")[SNew(SOutputLog)];
}

void FMapEditorShell::OpenMeshAsset(const FContentBrowserItem& Item)
{
	FString Error;
	TUniquePtr<FMeshAssetEditSession> Session =
	    FMeshAssetEditSession::Open(*AssetRegistry, {Item.PackageId, Item.PackagePath}, Error);
	if (!Session)
	{
		Log(Error);
		return;
	}
	struct FPreviewState
	{
		TUniquePtr<FMeshAssetEditSession> Session;
		FString Status;
		FMapViewSettings Settings;
		TUniquePtr<FMapDocument> Doc;
		TSharedPtr<FMapViewportClient> Client; // destroyed before its document and settings
		Object* Mesh = nullptr;
	};
	TSharedPtr<FPreviewState> State = std::make_shared<FPreviewState>();
	State->Session = std::move(Session);
	State->Settings.bShowGrid = false;
	State->Doc = FMapDocument::CreateNew();
	State->Mesh = State->Doc->AddMesh("Cube", Vector3d());
	State->Client = std::make_shared<FMapViewportClient>(
	    *Host.Device, *Host.Renderer, *RenderResources, State->Settings, EMapViewType::Perspective);
	State->Client->SetDocument(State->Doc.get());
	State->Client->SetViewMode(EMapViewMode::Lit);
	const TFunction<void(bool)> UpdatePreview = [this, State](bool Frame)
	{
		TSharedPtr<FLoadedMeshAsset> Loaded = std::make_shared<FLoadedMeshAsset>();
		FString PreviewError;
		if (!State->Session->BuildPreview(*AssetRegistry, *Loaded, PreviewError))
		{
			State->Status = PreviewError;
			Loaded->Data = State->Session->GetMesh();
			Loaded->DisplayMesh = Loaded->Data.Mesh;
			Loaded->AdditionalSlots.resize(Loaded->Data.AdditionalMaterials.size());
		}
		State->Mesh->SetMeshAsset(Loaded);
		if (Frame)
		{
			State->Doc->SetSelection({State->Mesh});
			State->Client->FrameSelection();
			State->Doc->ClearSelection();
		}
		State->Client->RequestRedraw();
	};
	UpdatePreview(true);
	// Aliasing owners keep settings/document alive until the viewport releases its client.
	const TSharedPtr<FRenderTargetViewportClient> RenderClient(State, State->Client.get());
	Host.WindowRenderer->AddViewportClient(ToSharedRef(RenderClient));
	const TSharedPtr<IViewportClient> InputClient(State, State->Client.get());
	TSharedRef<SBox> Slots = SNew(SBox);
	TSharedPtr<std::function<void()>> Rebuild = std::make_shared<TFunction<void()>>();
	*Rebuild = [this, State, Slots, UpdatePreview]
	{
		TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
		TSharedPtr<std::vector<FString, std::allocator<FString>>> Labels = std::make_shared<TArray<FString>>();
		TSharedPtr<std::vector<FAssetReference, std::allocator<FAssetReference>>> Refs =
		    std::make_shared<TArray<FAssetReference>>();
		Labels->push_back("None");
		Refs->push_back({});
		for (const FAssetData& Asset : AssetRegistry->GetAssets())
			if (IsMaterialAsset(Asset.Metadata))
			{
				Labels->push_back(Asset.PackagePath);
				Refs->push_back(Asset.GetReference());
			}
		const FStaticMeshAsset& Data = State->Session->GetMesh();
		for (int I = 0; I < Data.GetNumMaterials(); ++I)
		{
			List->AddSlot().AutoHeight()[SNew(STextBlock).Text(Data.GetMaterialName(I))];
			List->AddSlot().AutoHeight()[SNew(SComboBox<FString>)
			        .OptionsSource(Labels.get())
			        .OnGenerateWidget(
			            [](const FString& Label) -> TSharedRef<SWidget> { return SNew(STextBlock).Text(Label); })
			        .OnSelectionChanged(
			            [this, State, Labels, Refs, I, UpdatePreview](const FString& Label, ESelectInfo)
			{
				for (size_t J = 0; J < Labels->size(); ++J)
					if ((*Labels)[J] == Label)
					{
						FString AssignError;
						if (!State->Session->SetMaterial(I, (*Refs)[J], *AssetRegistry, AssignError))
							State->Status = AssignError;
						else
						{
							State->Status = GENESIS_TEXT("Draft updated. Save to apply mesh defaults.");
							UpdatePreview(false);
						}
					}
			})[SNew(STextBlock)
			                .Text([State, I]
			{
				const FAssetReference& Ref = State->Session->GetMesh().GetMaterial(I);
				return Ref.IsNull() ? FString("None") : Ref.Path;
			})]];
		}
		Slots->SetContent(List);
	};
	(*Rebuild)();
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	Panel->AddSlot().AutoHeight()[SNew(STextBlock).Text(Item.PackagePath)];
	Panel->AddSlot().AutoHeight()[SNew(STextBlock)
	        .Text([State]
	{
		const FStaticMeshAsset& Data = State->Session->GetMesh();
		return FString::Format("{} vertices, {} triangles, {} slots{}", Data.Mesh->GetNumVertices(),
		    Data.Mesh->GetTriangles().size(), Data.GetNumMaterials(),
		    State->Session->IsDirty() ? " - unsaved draft" : "");
	}).AutoWrapText(true)];
	Panel->AddSlot().AutoHeight()[SNew(SHorizontalBox) +
	    SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
	            .Text(GENESIS_TEXT("Frame"))
	            .OnClicked(
	                [State]
	{
		State->Client->FrameSelection();
		return FReply::Handled();
	})] +
	    SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
	            .Text(GENESIS_TEXT("Lit"))
	            .OnClicked(
	                [State]
	{
		State->Client->SetViewMode(EMapViewMode::Lit);
		return FReply::Handled();
	})] +
	    SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
	            .Text(GENESIS_TEXT("Unlit"))
	            .OnClicked(
	                [State]
	{
		State->Client->SetViewMode(EMapViewMode::Unlit);
		return FReply::Handled();
	})] +
	    SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
	            .Text(GENESIS_TEXT("Wire"))
	            .OnClicked([State]
	{
		State->Client->SetViewMode(EMapViewMode::Wireframe);
		return FReply::Handled();
	})]];

	Panel->AddSlot().AutoHeight()[SNew(STextBlock).Text(GENESIS_TEXT("Default Materials"))];
	Panel->AddSlot().FillHeight(1)[SNew(SScrollBox) + SScrollBox::Slot()[Slots]];
	Panel->AddSlot().AutoHeight()[SNew(STextBlock)
	        .Text([State]
	{
		return "Source: " +
		    (State->Session->GetSourceFile().IsEmpty() ? FString("Not recorded") : State->Session->GetSourceFile());
	}).AutoWrapText(true)];
	const TFunction<void(const FString&)> Reimport = [State, Rebuild, UpdatePreview](const FString& File)
	{
		FString ImportError;
		if (!State->Session->Reimport(File, ImportError))
			State->Status = ImportError;
		else
		{
			State->Status = GENESIS_TEXT("Geometry reimported into the draft. Save to apply.");
			(*Rebuild)();
			UpdatePreview(true);
		}
	};
	Panel->AddSlot().AutoHeight()[SNew(SButton)
	        .Text(GENESIS_TEXT("Reimport Recorded Source"))
	        .IsEnabled([State] { return !State->Session->GetSourceFile().IsEmpty(); })
	        .OnClicked([State, Reimport]
	{
		Reimport(State->Session->GetSourceFile());
		return FReply::Handled();
	})];
	Panel->AddSlot().AutoHeight()[SNew(SButton)
	        .Text(GENESIS_TEXT("Reimport from OBJ..."))
	        .OnClicked([State, Reimport]
	{
		FFileDialogOptions Options;
		Options.Title = "Reimport Mesh Geometry";
		Options.Filters = {{"Wavefront OBJ", "*.obj"}};
		Options.DefaultExtension = "obj";
		Options.InitialPath = State->Session->GetSourceFile();
		FString File;
		if (FServices::GetFileDialogs().OpenFile(Options, File))
			Reimport(File);
		return FReply::Handled();
	})];
	Panel->AddSlot().AutoHeight()[SNew(SButton)
	        .Text(GENESIS_TEXT("Save Mesh Asset"))
	        .OnClicked([this, State, Rebuild]
	{
		FString SaveError;
		if (!State->Session->Save(*AssetRegistry, SaveError))
			State->Status = SaveError;
		else
		{
			RefreshAssetRegistry();
			(*Rebuild)();
			if (Document && !Document->ReloadMeshAsset(State->Session->GetReference(), SaveError))
				State->Status = GENESIS_TEXT("Saved; level refresh failed: ") + SaveError;
			else
				State->Status = GENESIS_TEXT("Saved mesh asset and refreshed level instances.");
		}
		Log(State->Status);
		return FReply::Handled();
	})];
	Panel->AddSlot().AutoHeight()[SNew(SButton)
	        .Text(GENESIS_TEXT("Place in Level"))
	        .OnClicked([this, State, Item]
	{
		if (State->Session->IsDirty())
		{
			State->Status = GENESIS_TEXT("Save the draft before placing this mesh.");
			return FReply::Handled();
		}
		FMapPlaceable Placeable;
		Placeable.Kind = EMapPlaceableKind::MeshAsset;
		Placeable.SourceAsset = State->Session->GetReference();
		Placeable.Label = Item.Name;
		Place(Placeable);
		return FReply::Handled();
	})];
	Panel->AddSlot().AutoHeight()[SNew(STextBlock).Text([State] { return State->Status; }).AutoWrapText(true)];
	TSharedRef<SHorizontalBox> Content = SNew(SHorizontalBox) +
	    SHorizontalBox::Slot().FillWidth(2)[SNew(SVerticalBox) +
	        SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
	                .Text(GENESIS_TEXT("Preview - Alt + left drag to orbit, wheel to zoom"))] +
	        SVerticalBox::Slot().FillHeight(1)[SNew(SViewport).ViewportClient(InputClient)]] +
	    SHorizontalBox::Slot().FillWidth(1)[Panel];
	FSlateApplication::Get().AddWindow(SNew(SWindow)
	        .Title("Static Mesh: " + Item.Name)
	        .BackgroundColor(FSlateColor(FColor(32, 35, 40)))
	        .ClientSize(FVector2(1000,
	            700))[SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Panel")).Padding(FMargin(12))[Content]]);
}

void FMapEditorShell::OpenTextureAsset(const FContentBrowserItem& Item)
{
	FString Error;
	TUniquePtr<FTextureAssetEditSession> Session =
	    FTextureAssetEditSession::Open(*AssetRegistry, {Item.PackageId, Item.PackagePath}, Error);
	if (!Session)
	{
		Log(Error);
		return;
	}
	struct FState
	{
		TUniquePtr<FTextureAssetEditSession> Session;
		FString Status;
		ETexturePreviewChannel Channel = ETexturePreviewChannel::RGBA;
	};
	TSharedPtr<FState> State = std::make_shared<FState>();
	State->Session = std::move(Session);
	TSharedRef<STextureAssetPreview> Preview = SNew(STextureAssetPreview);
	const TFunction<void()> Update = [State, Preview]
	{
		FString PreviewError;
		if (!Preview->Update(State->Session->GetTexture(), State->Channel, PreviewError))
			State->Status = PreviewError;
	};
	Update();
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	Panel->AddSlot().AutoHeight()[SNew(STextBlock).Text(Item.PackagePath)];
	Panel->AddSlot().AutoHeight()[SNew(STextBlock)
	        .Text([State]
	{
		const FTextureAsset& T = State->Session->GetTexture();
		return FString::Format("{} x {}, RGBA8, {} bytes{}", T.Width, T.Height, T.Pixels.size(),
		    State->Session->IsDirty() ? " - unsaved draft" : "");
	}).AutoWrapText(true)];
	Panel->AddSlot().AutoHeight()[SNew(SCheckBox)
	        .Text(GENESIS_TEXT("sRGB Color Texture"))
	        .IsChecked([State]
	{ return State->Session->GetTexture().bSRGB ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
	        .OnCheckStateChanged([State, Update](ECheckBoxState Value)
	{
		State->Session->SetSRGB(Value == ECheckBoxState::Checked);
		State->Status = GENESIS_TEXT("Draft updated. Save to apply.");
		Update();
	})];
	TSharedPtr<std::vector<FString, std::allocator<FString>>> Labels =
	    std::make_shared<TArray<FString>>(TArray<FString>{"RGBA", "RGB", "Red", "Green", "Blue", "Alpha"});
	Panel->AddSlot().AutoHeight()[SNew(STextBlock).Text(GENESIS_TEXT("Preview Channels"))];
	Panel->AddSlot().AutoHeight()[SNew(SComboBox<FString>)
	        .OptionsSource(Labels.get())
	        .OnGenerateWidget([](const FString& Label) -> TSharedRef<SWidget> { return SNew(STextBlock).Text(Label); })
	        .OnSelectionChanged([State, Labels, Update](const FString& Label, ESelectInfo)
	{
		for (size_t I = 0; I < Labels->size(); ++I)
			if ((*Labels)[I] == Label)
			{
				State->Channel = static_cast<ETexturePreviewChannel>(I);
				Update();
			}
	})[SNew(STextBlock).Text([State, Labels] { return (*Labels)[static_cast<int>(State->Channel)]; })]];
	Panel->AddSlot().AutoHeight()[SNew(SButton)
	        .Text(GENESIS_TEXT("Fit Preview"))
	        .OnClicked([Preview]
	{
		Preview->Fit();
		return FReply::Handled();
	})];
	Panel->AddSlot().AutoHeight()[SNew(STextBlock)
	        .Text(GENESIS_TEXT("Wheel: zoom. Middle drag: pan. RGB respects sRGB; individual channels "
	                           "show stored values. Preview is limited to 2048 pixels per side."))
	        .AutoWrapText(true)];
	Panel->AddSlot().AutoHeight()[SNew(STextBlock)
	        .Text(GENESIS_TEXT("Disable sRGB for linear data maps. Tangent-space normal maps are always "
	                           "sampled as linear by mesh materials."))
	        .AutoWrapText(true)];
	Panel->AddSlot().AutoHeight()[SNew(STextBlock)
	        .Text([State]
	{
		return "Source: " +
		    (State->Session->GetSourceFile().IsEmpty() ? FString("Not recorded") : State->Session->GetSourceFile());
	}).AutoWrapText(true)];
	const TFunction<void(const FString&)> Reimport = [State, Preview, Update](const FString& File)
	{
		FString ImportError;
		if (!State->Session->Reimport(File, ImportError))
			State->Status = ImportError;
		else
		{
			State->Status = GENESIS_TEXT("Reimported into draft. Save to apply.");
			Preview->Fit();
			Update();
		}
	};
	Panel->AddSlot().AutoHeight()[SNew(SButton)
	        .Text(GENESIS_TEXT("Reimport Recorded Source"))
	        .IsEnabled([State] { return !State->Session->GetSourceFile().IsEmpty(); })
	        .OnClicked([State, Reimport]
	{
		Reimport(State->Session->GetSourceFile());
		return FReply::Handled();
	})];
	Panel->AddSlot().AutoHeight()[SNew(SButton)
	        .Text(GENESIS_TEXT("Reimport Image..."))
	        .OnClicked([State, Reimport]
	{
		FFileDialogOptions Options;
		Options.Title = "Reimport Texture Image";
		Options.Filters = {{"Image files", "*.png;*.bmp;*.jpg;*.jpeg;*.tif;*.tiff"}};
		Options.InitialPath = State->Session->GetSourceFile();
		FString File;
		if (FServices::GetFileDialogs().OpenFile(Options, File))
			Reimport(File);
		return FReply::Handled();
	})];
	Panel->AddSlot().AutoHeight()[SNew(SButton)
	        .Text(GENESIS_TEXT("Save Texture Asset"))
	        .OnClicked([this, State]
	{
		FString SaveError;
		if (!State->Session->Save(*AssetRegistry, SaveError))
			State->Status = SaveError;
		else
		{
			RefreshAssetRegistry();
			ViewSettings.Changed();
			if (Document && !Document->ReloadRenderAssets(SaveError))
				State->Status = GENESIS_TEXT("Saved; level refresh failed: ") + SaveError;
			else
				State->Status = GENESIS_TEXT("Saved texture and refreshed level assets.");
		}
		Log(State->Status);
		return FReply::Handled();
	})];
	Panel->AddSlot().AutoHeight()[SNew(SButton)
	        .Text(GENESIS_TEXT("Use for Selected Landscape Layer"))
	        .OnClicked([this, State]
	{
		if (State->Session->IsDirty())
		{
			State->Status = GENESIS_TEXT("Save the draft before assigning this texture.");
			return FReply::Handled();
		}
		Object* Land = LandscapeMode.GetTargetLandscape();
		if (!Land)
			State->Status = GENESIS_TEXT("Select a landscape and paint layer first.");
		else
		{
			const FString Token = FMapAssetLibrary::TextureToken(State->Session->GetReference());
			FTextureAsset T;
			FString AssignError;
			if (!FMapAssetLibrary::LoadTexture(Token, T, AssignError))
				State->Status = AssignError;
			else if (!LandscapeMode.SetLayerTexture(*Land, LandscapeMode.SelectedLayer, Token))
				State->Status = GENESIS_TEXT("Add or select a paint layer first.");
			else
				State->Status = GENESIS_TEXT("Assigned texture to landscape paint layer.");
		}
		return FReply::Handled();
	})];
	Panel->AddSlot().AutoHeight()[SNew(STextBlock).Text([State] { return State->Status; }).AutoWrapText(true)];
	FSlateApplication::Get().AddWindow(SNew(SWindow)
	        .Title("Texture: " + Item.Name)
	        .BackgroundColor(FSlateColor(FColor(32, 35, 40)))
	        .ClientSize(FVector2(1000, 700))[SNew(SBorder)
	                .BorderImage(&FAppStyle::Get().GetBrush("Panel"))
	                .Padding(FMargin(12))[SNew(SHorizontalBox) + SHorizontalBox::Slot().FillWidth(2)[Preview] +
	                    SHorizontalBox::Slot().FillWidth(1).Padding(
	                        FMargin(8, 0, 0, 0))[SNew(SScrollBox) + SScrollBox::Slot()[Panel]]]]);
}

void FMapEditorShell::OpenMaterialAsset(const FContentBrowserItem& Item)
{
	FMaterialEditorArgs Args;
	Args.Registry = AssetRegistry;
	Args.Material = {Item.PackageId, Item.PackagePath};
	Args.Name = Item.Name;
	Args.Device = Host.Device;
	Args.Renderer = Host.Renderer;
	Args.WindowRenderer = Host.WindowRenderer;
	Args.OnSaved = FSimpleDelegate::CreateLambda([this] { RefreshAssetRegistry(); });
	Args.OnLog = TDelegate<void(const FString&)>::CreateLambda([this](const FString& Message) { Log(Message); });
	Args.OnApply = TDelegate<void(int32)>::CreateLambda([this, Ref = Args.Material](int32 Slot)
	{
		if (!Document)
			return;
		FString Error;
		Document->BeginTransaction("Assign Material");
		for (Object* Obj : Document->GetSelection())
			if (Obj->HasMesh() && !Obj->HasLandscape())
				if (!Document->SetMeshMaterial(*Obj, Ref, Error, Slot))
					Log(Error);
		Document->EndTransaction();
	});
	FString Error;
	if (!FMaterialEditor::Open(Args, Error))
		Log(Error);
}
