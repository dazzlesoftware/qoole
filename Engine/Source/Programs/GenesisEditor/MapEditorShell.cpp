// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "MapEditorShell.h"
#include "Containers/ContainerAliases.h"
#include "Templates/SharedPointer.h"
#include "Internationalization/TextLocalization.h"
#include "SLandscapeEditorPanel.h"
#include "SFoliageEditorPanel.h"
#include "MapAssetLibrary.h"
#include "ProjectDescriptor.h"
#include "AssetRegistry.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MenuRegistry.h"
#include "Framework/PropertyGrid/SPropertyGrid.h"
#include "MapCoreHeaders.h"
#include "Misc/App.h"
#include "HAL/PlatformMisc.h"
#include "GameFramework/GameInstance.h"
#include "GameViewportClient.h"
#include "AssetPackage.h"
#include "MapPackage.h"
#include "Widgets/SViewport.h"
#include "Serialization/Json.h"
#include "Services/FileDialogs.h"
#include "Services/MessageDialog.h"
#include "Services/Services.h"
#include "SlateRHIRenderer.h"
#include "SPlaceActors.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>

DECLARE_LOG_CATEGORY_EXTERN(LogGenesisEditor, Log);

namespace
{
	const FTextBlockStyle& SmallText()
	{
		return FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText");
	}

	const TArray<FFileTypeFilter>& MapFilters()
	{
		static const TArray<FFileTypeFilter> Filters = {
		    {"Maps and projects (*.gmap, *.qle, *.map, *.gproject)", "*.gmap;*.qle;*.map;*.gproject"},
		    {"Genesis levels (*.gmap)", "*.gmap"},
		    {"Genesis projects (*.gproject)", "*.gproject"},
		    {"Quake maps (*.map)", "*.map"},
		};
		return Filters;
	}
} // namespace

const char* ToString(EMapEditorMode Mode)
{
	switch (Mode)
	{
	case EMapEditorMode::Selection:
		return "Selection";
	case EMapEditorMode::Landscape:
		return "Landscape";
	case EMapEditorMode::Foliage:
		return "Foliage";
	case EMapEditorMode::BrushEditing:
		return "Brush Editing";
	}
	return "?";
}

FMapEditorShell::FMapEditorShell(FHost InHost) : Host(std::move(InHost))
{
	LandscapeOverlayHandle = RegisterLandscapeViewportOverlay([this] { return Document.get(); });
	Commands = std::make_shared<FUICommandList>();
	TabManager = FTabManager::Create().ToSharedPtr();
	// The mode follows its panel, as Unreal's mode tabs do: bringing the
	// Landscape tab up is Landscape mode, Place Actors is Selection.
	TabManager->OnActiveTabChanged.AddLambda([this](const TSharedPtr<SDockTab>& Tab)
	{
		if (!Tab || bShowingModeTab)
		{
			return;
		}
		const FName Id = Tab->GetTabId();
		if (Id == FName("Landscape"))
		{
			SetMode(EMapEditorMode::Landscape);
		}
		else if (Id == FName("Foliage"))
		{
			SetMode(EMapEditorMode::Foliage);
		}
		else if (Id == FName("PlaceActors"))
		{
			SetMode(EMapEditorMode::Selection);
		}
		else if (Id == FName("BrushEditing"))
		{
			SetMode(EMapEditorMode::BrushEditing);
		}
	});
	RenderResources = MakeUnique<FMapRenderResources>(*Host.Device);
	ViewSettings.OnCursorText = [this](const FString& Text) { CursorText = Text; };
	ViewSettings.OnMessage = [this](const FString& Text) { StatusText = Text; };
	// The panes of the default 2x2 layout, as in Unreal: top, perspective,
	// front and right.
	for (const EMapViewType Type :
	    {EMapViewType::Top, EMapViewType::Perspective, EMapViewType::Front, EMapViewType::Right})
	{
		Views.push_back(
		    std::make_shared<FMapViewportClient>(*Host.Device, *Host.Renderer, *RenderResources, ViewSettings, Type));
		Host.WindowRenderer->AddViewportClient(
		    ToSharedRef(StaticCastSharedPtr<FRenderTargetViewportClient>(Views.back())));
	}
	LoadEditorSettings();
	if (!Host.SettingsDir.IsEmpty())
	{
		const std::filesystem::path Folder = std::filesystem::path(Host.SettingsDir.ToWide()) / L"Prefabs";
		std::error_code Error;
		std::filesystem::create_directories(Folder, Error);
		FMapEditorEnvironment::SetUserPrefabFolder(FString::FromWide(Folder.wstring()));
	}
	RegisterCommands();
	AssetRegistry = std::make_shared<FAssetRegistry>();
	FMapAssetLibrary::SetRegistry(AssetRegistry);
	TArray<FAssetMount> Mounts;
	if (const FProjectContext* Project = FProjectContext::GetActive())
		Mounts.push_back({"/Game", Project->GetContentDir()});
	Mounts.push_back({"/Engine", FPaths::FindContentDir()});
	if (!FMapEditorEnvironment::GetUserPrefabFolder().IsEmpty())
		Mounts.push_back({"/UserPrefabs", FMapEditorEnvironment::GetUserPrefabFolder()});
	FString RegistryError;
	if (!AssetRegistry->SetMounts(std::move(Mounts), RegistryError))
		Log(RegistryError);
	else
		RefreshAssetRegistry();
	RegisterMenus();
	RegisterTabs();
}

FMapEditorShell::~FMapEditorShell()
{
	if (TSharedPtr<SLandscapeEditorPanel> Panel = LandscapePanel.lock())
		Panel->EndInteraction();
	if (TSharedPtr<SFoliageEditorPanel> Panel = FoliagePanel.lock())
		Panel->EndInteraction();
	MapSceneOverlays().Remove(LandscapeOverlayHandle);
	ViewSettings.EditorMode = nullptr;
	LandscapeMode.SetDocument(nullptr);
	FoliageMode.SetDocument(nullptr);
	for (const TSharedPtr<FMapViewportClient>& View : Views)
	{
		View->SetDocument(nullptr);
	}
	if (Document)
	{
		Document->OnChanged.Remove(DocumentHandle);
	}
	Properties.reset();
	Document.reset();
	Clipboard.clear();
}

void FMapEditorShell::Log(const FString& Line)
{
	LogLines.push_back(Line);
	StatusText = Line;
	GENESIS_LOG(LogGenesisEditor, Display, "{}", Line);
}

// ----- Settings -----

void FMapEditorShell::LoadEditorSettings()
{
	IConfigStore& Config = FServices::GetConfig();
	FString Text;
	if (Config.GetString("MapEditor", "GridSize", Text))
	{
		ViewSettings.GridSize = FMath::Clamp(static_cast<float>(std::atof(Text.ToAnsi().c_str())),
		    FMapViewSettings::MinGridSize, FMapViewSettings::MaxGridSize);
	}
	if (Config.GetString("MapEditor", "RotateSnap", Text))
	{
		ViewSettings.RotateSnapDegrees =
		    FMath::Clamp(static_cast<float>(std::atof(Text.ToAnsi().c_str())), 0.0f, 90.0f);
	}
	int32 Value = 0;
	if (Config.GetInt("MapEditor", "SnapToGrid", Value))
		ViewSettings.bSnapToGrid = Value != 0;
	if (Config.GetInt("MapEditor", "ShowGrid", Value))
		ViewSettings.bShowGrid = Value != 0;
	if (Config.GetInt("MapEditor", "SnapRotation", Value))
		ViewSettings.bSnapRotation = Value != 0;
	if (Config.GetInt("MapEditor", "InvertMiddleMousePan", Value))
		ViewSettings.bInvertMiddleMousePan = Value != 0;
	if (Config.GetString("MapEditor", "MouseSensitivity", Text))
	{
		ViewSettings.MouseSensitivity = FMath::Clamp(static_cast<float>(std::atof(Text.ToAnsi().c_str())), 0.1f, 4.0f);
	}

	for (int32 Index = 0; Index < MaxContentBrowsers; ++Index)
	{
		Config.GetString("MapEditor.ContentBrowser", FString::Format("Path{}", Index + 1), ContentBrowserPaths[Index]);
	}

	// The viewports: the layout, then each pane's type, view mode, camera and show flags.
	if (Config.GetString("MapEditor", "ViewportLayout", Text))
	{
		ViewportLayoutType = MapViewportLayoutFromString(Text).value_or(ViewportLayoutType);
	}
	for (SIZE_T Index = 0; Index < Views.size(); ++Index)
	{
		FMapViewportClient& View = *Views[Index];
		const FString Section = FString::Format("MapEditor.Viewport{}", Index);
		if (Config.GetString(Section, "Type", Text))
		{
			if (const TOptional<EMapViewType> Type = MapViewTypeFromString(Text))
			{
				View.SetViewportType(*Type);
			}
		}
		if (Config.GetInt(Section, "ViewMode", Value))
			View.SetViewMode(static_cast<EMapViewMode>(FMath::Clamp(Value, 0, 2)));
		if (Config.GetString(Section, "FOV", Text))
			View.SetViewFOV(static_cast<float>(std::atof(Text.ToAnsi().c_str())));
		if (Config.GetString(Section, "CameraSpeed", Text))
			View.SetCameraSpeed(static_cast<float>(std::atof(Text.ToAnsi().c_str())));
		if (Config.GetInt(Section, "ShowFlags", Value))
		{
			FMapShowFlags Flags;
			Flags.bGrid = (Value & 1) != 0;
			Flags.bEntities = (Value & 2) != 0;
			Flags.bViewportStats = (Value & 4) != 0;
			Flags.bAxes = (Value & 8) != 0;
			View.SetShowFlags(Flags);
		}
	}
}

void FMapEditorShell::SaveEditorSettings() const
{
	IConfigStore& Config = FServices::GetConfig();
	Config.SetString("MapEditor", "GridSize", FString::Format("{}", ViewSettings.GridSize));
	Config.SetString("MapEditor", "RotateSnap", FString::Format("{}", ViewSettings.RotateSnapDegrees));
	Config.SetInt("MapEditor", "SnapToGrid", ViewSettings.bSnapToGrid ? 1 : 0);
	Config.SetInt("MapEditor", "ShowGrid", ViewSettings.bShowGrid ? 1 : 0);
	Config.SetInt("MapEditor", "SnapRotation", ViewSettings.bSnapRotation ? 1 : 0);
	Config.SetInt("MapEditor", "InvertMiddleMousePan", ViewSettings.bInvertMiddleMousePan ? 1 : 0);
	Config.SetString("MapEditor", "MouseSensitivity", FString::Format("{}", ViewSettings.MouseSensitivity));

	for (int32 Index = 0; Index < MaxContentBrowsers; ++Index)
	{
		const TSharedPtr<SContentBrowser> Browser = ContentBrowsers[Index].lock();
		Config.SetString("MapEditor.ContentBrowser", FString::Format("Path{}", Index + 1),
		    Browser ? Browser->GetPath() : ContentBrowserPaths[Index]);
	}

	// In the layout's pane order (switching layouts can swap panes).
	const TArray<TSharedPtr<FMapViewportClient>>& Panes = ViewportLayout ? ViewportLayout->GetClients() : Views;
	Config.SetString(
	    "MapEditor", "ViewportLayout", ToString(ViewportLayout ? ViewportLayout->GetLayout() : ViewportLayoutType));
	for (SIZE_T Index = 0; Index < Panes.size(); ++Index)
	{
		if (!Panes[Index])
		{
			continue;
		}
		const FMapViewportClient& View = *Panes[Index];
		const FString Section = FString::Format("MapEditor.Viewport{}", Index);
		Config.SetString(Section, "Type", ToString(View.GetViewportType()));
		Config.SetInt(Section, "ViewMode", static_cast<int32>(View.GetPerspectiveViewMode()));
		Config.SetString(Section, "FOV", FString::Format("{}", View.GetViewFOV()));
		Config.SetString(Section, "CameraSpeed", FString::Format("{}", View.GetCameraSpeed()));
		const FMapShowFlags& Flags = View.GetShowFlags();
		Config.SetInt(Section, "ShowFlags",
		    (Flags.bGrid ? 1 : 0) | (Flags.bEntities ? 2 : 0) | (Flags.bViewportStats ? 4 : 0) | (Flags.bAxes ? 8 : 0));
	}
}

// ----- Documents -----

void FMapEditorShell::SetDocument(TUniquePtr<FMapDocument> NewDocument)
{
	FSlateApplication::Get().DismissAllMenus();
	PlayLevelId.Empty();
	if (TSharedPtr<SLandscapeEditorPanel> Panel = LandscapePanel.lock())
		Panel->EndInteraction();
	if (TSharedPtr<SFoliageEditorPanel> Panel = FoliagePanel.lock())
		Panel->EndInteraction();
	for (const TSharedPtr<FMapViewportClient>& View : Views)
	{
		View->SetDocument(nullptr);
	}
	if (Document)
	{
		Document->OnChanged.Remove(DocumentHandle);
	}
	if (Properties)
	{
		Properties->SetObjects({});
	}
	LandscapeMode.SetDocument(nullptr);
	FoliageMode.SetDocument(nullptr);
	Document = std::move(NewDocument);
	LandscapeMode.SetDocument(Document.get());
	FoliageMode.SetDocument(Document.get());
	if (Document)
	{
		DocumentHandle = Document->OnChanged.AddLambda([this](EMapChange Change) { OnDocumentChanged(Change); });
		for (const TSharedPtr<FMapViewportClient>& View : Views)
		{
			View->SetDocument(Document.get());
		}
		for (const TSharedPtr<FMapViewportClient>& View : Views)
		{
			View->FrameSelection();
		}
	}
	RefreshOutliner();
	RefreshProperties();
	RefreshLandscapeLayers();
	RefreshFoliageTypes();
	UpdateTitle();
}

void FMapEditorShell::OnDocumentChanged(EMapChange Change)
{
	if (HasAnyChange(Change, EMapChange::Structure | EMapChange::Selection | EMapChange::Scope | EMapChange::Geometry))
	{
		RefreshOutliner();
		RefreshProperties();
		RefreshLandscapeLayers();
		RefreshFoliageTypes();
	}
	if (HasAnyChange(Change, EMapChange::File))
	{
		UpdateTitle();
	}
}

void FMapEditorShell::UpdateTitle()
{
	const TSharedPtr<SWindow> Window = MainWindow.lock();
	if (!Window)
	{
		return;
	}
	if (!Document)
	{
		Window->SetTitle("Genesis Editor");
		return;
	}
	Window->SetTitle(FTextLocalization::Format("{}{} - {}Genesis Editor", Document->GetDisplayName(),
	    Document->IsModified() ? " *" : "",
	    FProjectContext::GetActive() ? FProjectContext::GetActive()->GetDescriptor().Name + " - " : FString()));
}

bool FMapEditorShell::PlayInGame(const FString& WorldPackage)
{
	const FProjectContext* Project = FProjectContext::GetActive();
	if (!Project)
		return false;
	const FString Game = FString::FromWide(
	    (std::filesystem::path(FPlatformProcess::ExecutablePath().ToWide()).parent_path() / L"GenesisGame.exe")
	        .wstring());
	TArray<FString> Arguments = {"-project=" + Project->GetFilePath(), "-world=" + WorldPackage};
	Arguments.insert(Arguments.end(), StandaloneArguments.begin(), StandaloneArguments.end());
	if (Host.Device)
		Arguments.push_back(Host.Device->GetBackend() == ERHIBackend::Vulkan ? "-vulkan" : "-d3d12");
	FString Error;
	if (!FPlatformProcess::CreateProc(Game, Arguments, Error))
	{
		FServices::GetMessageDialog().Show(EMessageType::Error, EMessageButtons::Ok, Error);
		return false;
	}
	Log(FTextLocalization::Format("Playing {}", WorldPackage));
	return true;
}

bool FMapEditorShell::IsPlaying() const
{
	return PlayGame != nullptr;
}

bool FMapEditorShell::PlayInViewport(const FAssetReference* Composition)
{
	if (IsPlaying())
		return false;
	const TSharedPtr<SDockTab> Tab = ViewportTab.lock();
	FString Error;
	if (!Tab)
		Error = GENESIS_TEXT("Open the Viewport tab (Window > Viewport) to play in the editor.");
	else if (!AssetRegistry || !Host.Device || !Host.Renderer || !Host.WindowRenderer)
		Error = GENESIS_TEXT("Play-in-Editor needs an open project and a graphics device.");
	if (!Error.IsEmpty())
	{
		Log(FTextLocalization::Format("Could not play: {}", Error));
		FServices::GetMessageDialog().Show(EMessageType::Error, EMessageButtons::Ok, Error);
		return false;
	}
	const FProjectContext* Project = FProjectContext::GetActive();
	const std::filesystem::path Saved = Project ? std::filesystem::path(Project->GetSavedDir().ToWide())
	                                            : std::filesystem::temp_directory_path() / L"GenesisEditor";
	const FString SaveFile = FString::FromWide((Saved / L"PlayInEditor" / L"QuickSave.json").wstring());
	if (Composition)
		PlayGame = FGameInstance::Open(AssetRegistry, *Composition, SaveFile, Error);
	else if (Document)
	{
		// A copy of the level as it is in memory, saved or not, as Unreal's PIE
		// duplicates the editor world. Nothing is written to disk.
		if (!Document->GetPackageId().IsEmpty())
			PlayLevelId = Document->GetPackageId();
		else if (PlayLevelId.IsEmpty())
			PlayLevelId = FAssetId::New();
		FAssetPackage Level;
		if (FMapPackage::Build(Document->GetRoot(), PlayLevelId, Level, Error))
			PlayGame = FGameInstance::OpenLevel(AssetRegistry, Level, Document->GetDisplayName(), SaveFile, Error);
	}
	if (!PlayGame)
	{
		Log(FTextLocalization::Format("Could not play: {}", Error));
		FServices::GetMessageDialog().Show(
		    EMessageType::Error, EMessageButtons::Ok, FTextLocalization::Format("Could not play: {}", Error));
		return false;
	}
	bStopPlayRequested = false;
	PlayViewport = std::make_shared<FGameViewportClient>(*Host.Device, *Host.Renderer, *PlayGame);
	PlayViewport->OnEscape = FSimpleDelegate::CreateLambda([this] { bStopPlayRequested = true; });
	PlayViewport->OnQuit = PlayViewport->OnEscape;
	Host.WindowRenderer->AddViewportClient(ToSharedRef(PlayViewport));
	const TSharedRef<SWidget> View = FGameViewportClient::MakeGameView(ToSharedRef(PlayViewport), [this]
	{
		return PlayGame ? PlayGame->GetHudText(PlayFramesPerSecond) +
		        GENESIS_TEXT("\nClick to look around. WASD move, Shift run, Space jump, F fly, Q/E "
		                     "down/up, F5 save, F9 load. Esc (mouse free) or Stop returns to editing.")
		                : FString();
	}, PlayViewportWidget);
	PlayLastFrame = PlayFpsTime = FPlatformTime::Seconds();
	PlayFrames = 0;
	// The game advances once a frame while its view is shown.
	View->RegisterActiveTimer(0.0f, [this](double, float) -> EActiveTimerReturn
	{
		if (bStopPlayRequested)
			StopPlaying();
		if (!PlayGame)
			return EActiveTimerReturn::Stop;
		const double Now = FPlatformTime::Seconds();
		const float Delta = std::min(static_cast<float>(Now - PlayLastFrame), 0.1f);
		PlayLastFrame = Now;
		++PlayFrames;
		if (Now - PlayFpsTime >= 0.5)
		{
			PlayFramesPerSecond = static_cast<float>(PlayFrames / (Now - PlayFpsTime));
			PlayFrames = 0;
			PlayFpsTime = Now;
		}
		PlayGame->Tick(Delta);
		return EActiveTimerReturn::Continue;
	});
	Tab->SetContent(View);
	FSlateApplication::Get().SetKeyboardFocus(PlayViewportWidget);
	Log(FTextLocalization::Format("Playing {} in the viewport", PlayGame->GetWorldName()));
	return true;
}

void FMapEditorShell::StopPlaying()
{
	if (!IsPlaying())
		return;
	if (const TSharedPtr<SDockTab> Tab = ViewportTab.lock(); Tab && ViewportLayout)
		Tab->SetContent(ToSharedRef(ViewportLayout));
	if (PlayViewport && PlayViewport->IsMouseCaptured())
		PlayViewport->ReleaseMouse();
	PlayViewportWidget.reset();
	PlayViewport.reset();
	PlayGame.reset();
	bStopPlayRequested = false;
	// Editing resumes with the keyboard in the level viewports, so shortcuts such as Alt+P work.
	if (ViewportLayout)
		FSlateApplication::Get().SetKeyboardFocus(ViewportLayout);
	Log(GENESIS_TEXT("Stopped playing"));
}

bool FMapEditorShell::SetStartupWorld(const FString& WorldPackage, FString& Error)
{
	const FProjectContext* Active = FProjectContext::GetActive();
	if (!Active)
	{
		Error = GENESIS_TEXT("Open a project to set its startup world.");
		return false;
	}
	FProjectContext Project = *Active;
	if (!Project.SetDefaultMap(WorldPackage, Error))
		return false;
	FProjectContext::SetActive(std::move(Project));
	return true;
}

void FMapEditorShell::SwitchProject(const FString& Option)
{
	FString Error;
	if (!FPlatformProcess::CreateProc(FPlatformProcess::ExecutablePath(), {Option}, Error))
	{
		FServices::GetMessageDialog().Show(EMessageType::Error, EMessageButtons::Ok, Error);
		return;
	}
	Log(FTextLocalization::Format("Restarting the editor with {}", Option));
	Host.Quit();
}

bool FMapEditorShell::ConfirmDiscard()
{
	if (!Document || !Document->IsModified())
	{
		return true;
	}
	const EMessageResult Answer = FServices::GetMessageDialog().Show(EMessageType::Question,
	    EMessageButtons::YesNoCancel, FTextLocalization::Format("Save the changes to {}?", Document->GetDisplayName()));
	if (Answer == EMessageResult::Cancel)
	{
		return false;
	}
	if (Answer == EMessageResult::Yes)
	{
		FString Path = Document->GetPath();
		if (Path.IsEmpty())
		{
			FFileDialogOptions Options;
			Options.Title = "Save map";
			Options.Filters = {{"Genesis levels (*.gmap)", "*.gmap"}};
			Options.DefaultExtension = "gmap";
			Options.InitialPath = DefaultFolder();
			if (!FServices::GetFileDialogs().SaveFile(Options, Path))
			{
				return false;
			}
		}
		return SaveMap(Path);
	}
	return true;
}

void FMapEditorShell::NewMap(const FMapGameSet& GameSet)
{
	FString Error;
	if (FMapEditorEnvironment::GetActiveGameSet() != &GameSet && !FMapEditorEnvironment::Activate(GameSet, &Error))
	{
		FServices::GetMessageDialog().Show(EMessageType::Error, EMessageButtons::Ok, Error);
		return;
	}
	SetDocument(FMapDocument::CreateNew());
	Log("New map");
}

bool FMapEditorShell::OpenMap(const FString& Path)
{
	FString Error;
	TUniquePtr<FMapDocument> Opened = FMapDocument::Open(Path, Error);
	if (!Opened)
	{
		Log(FTextLocalization::Format("Could not open {}", Path));
		FServices::GetMessageDialog().Show(EMessageType::Error, EMessageButtons::Ok, Error);
		return false;
	}
	SetDocument(std::move(Opened));
	Log(FString::Format(
	    "Opened {}: {} brushes, {} entities", Path, Document->CountBrushes(), Document->CountEntities()));
	FServices::GetConfig().SetString(
	    "MapEditor", "LastFolder", FString::FromWide(std::filesystem::path(Path.ToWide()).parent_path().wstring()));
	FServices::GetConfig().SetString("MapEditor", "LastMap", Path);
	return true;
}

bool FMapEditorShell::SaveMap(const FString& Path)
{
	FString Error;
	if (!Document || !Document->Save(Path, Error))
	{
		FServices::GetMessageDialog().Show(EMessageType::Error, EMessageButtons::Ok, Error);
		return false;
	}
	Log(FString::Format("Saved {}", Path));
	FServices::GetConfig().SetString(
	    "MapEditor", "LastFolder", FString::FromWide(std::filesystem::path(Path.ToWide()).parent_path().wstring()));
	FServices::GetConfig().SetString("MapEditor", "LastMap", Path);
	RefreshAssetRegistry();
	return true;
}

void FMapEditorShell::RefreshAssetRegistry()
{
	if (!AssetRegistry)
		return;
	AssetRegistry->Scan();
	for (size_t I = 0; I < AssetRegistry->GetIssues().size() && I < 20; ++I)
	{
		FAssetRegistryIssue const& Issue = AssetRegistry->GetIssues()[I];
		GENESIS_LOG(LogGenesisEditor, Warning, "Content issue in {}: {}", Issue.FilePath, Issue.Message);
	}
}

bool FMapEditorShell::ExportMap(const FString& Path)
{
	FString Error;
	if (!Document || !Document->Save(Path, Error))
	{
		FServices::GetMessageDialog().Show(EMessageType::Error, EMessageButtons::Ok, Error);
		return false;
	}
	Log(FString::Format("Exported {}", Path));
	if (const int32 Meshes = Document->CountMeshes(); Meshes > 0)
	{
		Log(FTextLocalization::Format("{} static mesh actor{} left out: .map files have brushes and entities only",
		    Meshes, Meshes == 1 ? " was" : "s were"));
	}
	return true;
}

FString FMapEditorShell::GetLastMap()
{
	FString Path;
	FServices::GetConfig().GetString("MapEditor", "LastMap", Path);
	return Path;
}

FString FMapEditorShell::DefaultFolder() const
{
	if (const FProjectContext* Project = FProjectContext::GetActive();
	    Project && (!Document || Document->GetPath().IsEmpty()))
		return Project->GetContentDir();
	if (Document && !Document->GetPath().IsEmpty())
	{
		return Document->GetPath();
	}
	FString Folder;
	FServices::GetConfig().GetString("MapEditor", "LastFolder", Folder);
	return Folder;
}

// ----- Views -----

TSharedPtr<FMapViewportClient> FMapEditorShell::GetActiveView() const
{
	if (ViewportLayout && ViewportLayout->GetActiveClient())
	{
		return ViewportLayout->GetActiveClient();
	}
	return Views[0];
}

TSharedPtr<FMapViewportClient> FMapEditorShell::GetView(EMapViewType Type) const
{
	TSharedPtr<FMapViewportClient> Hidden;
	for (const TSharedPtr<FMapViewportClient>& View : Views)
	{
		if (View->GetViewportType() != Type)
		{
			continue;
		}
		if (View->GetViewport())
		{
			return View;
		}
		Hidden = Hidden ? Hidden : View;
	}
	return Hidden;
}

TSharedPtr<FMapViewportClient> FMapEditorShell::GetPerspectiveView() const
{
	const TSharedPtr<FMapViewportClient> Active = GetActiveView();
	return Active->IsPerspective() ? Active : GetView(EMapViewType::Perspective);
}

Vector3d FMapEditorShell::SnappedPlacement() const
{
	Vector3d Point = GetActiveView()->GetPlacementPoint();
	if (ViewSettings.bSnapToGrid)
	{
		const float Grid = ViewSettings.GridSize;
		Point.NewVector(std::round(Point.GetX() / Grid) * Grid, std::round(Point.GetY() / Grid) * Grid,
		    std::round(Point.GetZ() / Grid) * Grid);
	}
	return Point;
}

Vector3d FMapEditorShell::SelectionCenter() const
{
	Vector3d Min, Max;
	if (!Document || !Document->GetSelectionBounds(Min, Max))
	{
		return Vector3d(0.0f, 0.0f, 0.0f);
	}
	return Min.AddVector(Max).MultVector(0.5f);
}

Object* FMapEditorShell::AddPrefab(const FString& Path, const FString& ExpectedId)
{
	if (!Document)
	{
		return nullptr;
	}
	FString Error;
	FString Identity = ExpectedId;
	if (Identity.IsEmpty())
		if (const FAssetData* Data = AssetRegistry ? AssetRegistry->FindByFile(Path) : nullptr)
		{
			Error = AssetRegistry->GetConflict(*Data);
			if (!Error.IsEmpty())
			{
				Log(Error);
				return nullptr;
			}
			Identity = Data->Metadata.Id;
		}
	Object* Added = Document->AddPrefab(Path, SnappedPlacement(), &Error, Identity);
	if (!Added)
	{
		Log(Error);
	}
	return Added;
}

Object* FMapEditorShell::AddEntity(const FString& ClassName)
{
	return Document ? Document->AddEntity(ClassName, SnappedPlacement()) : nullptr;
}

void FMapEditorShell::SetGridSize(float Size)
{
	ViewSettings.GridSize = FMath::Clamp(Size, FMapViewSettings::MinGridSize, FMapViewSettings::MaxGridSize);
	ViewSettings.Changed();
	Log(FTextLocalization::Format("Grid size {:.0f}", ViewSettings.GridSize));
}

namespace
{
	bool IsBrushEditingTool(EMapTool Tool)
	{
		return Tool == EMapTool::Clip || Tool == EMapTool::Vertex || Tool == EMapTool::Edge || Tool == EMapTool::Face;
	}
} // namespace

void FMapEditorShell::SetTool(EMapTool Tool)
{
	ViewSettings.Tool = Tool;
	ViewSettings.Changed();
	StatusText = FString::Format("Tool: {}", ToString(Tool));
	// The mode follows the tool, as Unreal's do.
	const EMapEditorMode ToolMode = IsBrushEditingTool(Tool) ? EMapEditorMode::BrushEditing : EMapEditorMode::Selection;
	if (ToolMode != Mode)
	{
		Mode = ToolMode;
		ViewSettings.EditorMode = nullptr;
		ViewSettings.Changed();
		ShowModeTab();
	}
}

void FMapEditorShell::SetMode(EMapEditorMode NewMode)
{
	if (NewMode == Mode)
	{
		ShowModeTab();
		return;
	}
	Mode = NewMode;
	// A mode's own tools: leaving Brush Editing goes back to Select.
	if (Mode == EMapEditorMode::BrushEditing && !IsBrushEditingTool(ViewSettings.Tool))
	{
		ViewSettings.Tool = EMapTool::Vertex;
	}
	else if (Mode != EMapEditorMode::BrushEditing && IsBrushEditingTool(ViewSettings.Tool))
	{
		ViewSettings.Tool = EMapTool::Select;
	}
	// Landscape takes the views' left drags over landscapes.
	ViewSettings.EditorMode = Mode == EMapEditorMode::Landscape ? static_cast<IMapViewportMode*>(&LandscapeMode)
	    : Mode == EMapEditorMode::Foliage                       ? static_cast<IMapViewportMode*>(&FoliageMode)
	                                                            : nullptr;
	ViewSettings.Changed();
	ShowModeTab();
	Log(FString::Format("{} mode", ToString(Mode)));
}

void FMapEditorShell::ShowModeTab()
{
	bShowingModeTab = true;
	TabManager->TryInvokeTab(Mode == EMapEditorMode::BrushEditing ? FName("BrushEditing")
	        : Mode == EMapEditorMode::Landscape                   ? FName("Landscape")
	        : Mode == EMapEditorMode::Foliage                     ? FName("Foliage")
	                                                              : FName("PlaceActors"));
	bShowingModeTab = false;
}

TArray<FMapPlaceable> FMapEditorShell::GetPlaceables() const
{
	TArray<FMapPlaceable> Items;
	// Entities: lights under Lights, the rest under Basic (as Unreal's
	// Player Start and Point Light are).
	for (const FMapEditorEnvironment::FEntityClass& Class : FMapEditorEnvironment::GetPlaceableEntities())
	{
		FMapPlaceable Item;
		Item.Kind = EMapPlaceableKind::Entity;
		Item.Id = Class.ClassName;
		const bool bLight = Class.Category.Equals("Light", ESearchCase::IgnoreCase);
		Item.Category = bLight ? "Lights" : "Basic";
		Item.Label = bLight || Class.Label.Contains(Class.Category.View())
		    ? Class.Label
		    : FString::Format("{} {}", Class.Category, Class.Label);
		Item.ToolTip = FString::Format("Entity {}", Class.ClassName);
		Items.push_back(std::move(Item));
		if (bLight)
		{
			// Unreal lists Point Light under Basic too.
			FMapPlaceable Basic = Items.back();
			Basic.Category = "Basic";
			Items.push_back(std::move(Basic));
		}
	}
	// Shapes: static mesh actors.
	for (const std::string& Name : StaticMesh::GetBasicShapeNames())
	{
		FMapPlaceable Item;
		Item.Kind = EMapPlaceableKind::Mesh;
		Item.Id = FString(Name);
		Item.Category = "Shapes";
		Item.Label = FString(Name);
		Item.ToolTip =
		    FTextLocalization::Format("A static mesh actor: {}, {:.0f} units across", Name, StaticMesh::BasicShapeSize);
		Items.push_back(std::move(Item));
	}
	// Prefabs: brushes, Unreal's BSP Geometry.
	for (const FMapEditorEnvironment::FPrefab& Prefab : FMapEditorEnvironment::GetPrefabs())
	{
		FMapPlaceable Item;
		Item.Kind = EMapPlaceableKind::Prefab;
		Item.Id = Prefab.Path;
		if (const FAssetData* Data = AssetRegistry ? AssetRegistry->FindByFile(Prefab.Path) : nullptr)
		{
			if (!AssetRegistry->GetConflict(*Data).IsEmpty())
				continue;
			Item.SourceAsset = Data->GetReference();
		}
		Item.Category = "Geometry";
		Item.Label = Prefab.bUser ? FString::Format("{} (yours)", Prefab.Name) : Prefab.Name;
		Item.ToolTip = FTextLocalization::Format("Brush prefab {}", Prefab.Path);
		Items.push_back(std::move(Item));
	}
	return Items;
}

Object* FMapEditorShell::Place(const FMapPlaceable& Item, const Vector3d& Point, bool bOnGround)
{
	if (!Document)
	{
		return nullptr;
	}
	Vector3d At = Point;
	if (ViewSettings.bSnapToGrid)
	{
		const float Grid = ViewSettings.GridSize;
		At.NewVector(std::round(At.GetX() / Grid) * Grid, std::round(At.GetY() / Grid) * Grid,
		    std::round(At.GetZ() / Grid) * Grid);
	}
	Object* Added = nullptr;
	if (Item.Kind == EMapPlaceableKind::Entity)
	{
		Added = Document->AddEntity(Item.Id, At);
	}
	else if (Item.Kind == EMapPlaceableKind::Mesh)
	{
		if (const std::shared_ptr<const StaticMesh> Mesh = StaticMesh::Find(Item.Id.ToAnsi().c_str());
		    Mesh && bOnGround)
		{
			Vector3d Min, Max;
			Mesh->GetBounds(Min, Max);
			At.SetZ(At.GetZ() - Min.GetZ());
		}
		Added = Document->AddMesh(Item.Id, At);
	}
	else if (Item.Kind == EMapPlaceableKind::MeshAsset)
	{
		FString Error;
		Added = Document->AddMeshAsset(Item.SourceAsset, At, Error);
		if (!Added)
			Log(Error);
		else if (bOnGround)
		{
			Vector3d Min, Max;
			Added->GetMesh().GetBounds(Min, Max);
			At.SetZ(At.GetZ() - Min.GetZ());
			Added->SetPosition(At);
		}
	}
	else
	{
		FString Error;
		FString Path = Item.Id, ExpectedId;
		if (!Item.SourceAsset.IsNull())
		{
			const FAssetData* Data =
			    AssetRegistry ? AssetRegistry->Resolve(Item.SourceAsset, Error, "Genesis.Prefab") : nullptr;
			if (!Data)
			{
				Log(Error.IsEmpty() ? FString("The asset registry is unavailable.") : Error);
				return nullptr;
			}
			Path = Data->FilePath;
			ExpectedId = Data->Metadata.Id;
		}
		Added = Document->AddPrefab(Path, At, &Error, ExpectedId);
		if (!Added)
		{
			Log(Error);
		}
	}
	if (Added)
	{
		Log(FString::Format("Placed {} at {:.0f} {:.0f} {:.0f}", Item.Label, At.GetX(), At.GetY(), At.GetZ()));
	}
	return Added;
}

Object* FMapEditorShell::Place(const FMapPlaceable& Item)
{
	Object* Added = Place(Item, GetActiveView()->GetPlacementPoint());
	if (Added && PlaceActors)
	{
		PlaceActors->NoteRecent(Item);
	}
	return Added;
}

void FMapEditorShell::SaveSelectionAsPrefab()
{
	if (!Document || Document->GetSelection().empty())
	{
		return;
	}
	FFileDialogOptions Options;
	Options.Title = "Save the selection as a prefab";
	Options.Filters = {{"Genesis prefab assets (*.asset)", "*.asset"}};
	Options.DefaultExtension = "asset";
	Options.InitialPath = FProjectContext::GetActive() ? FProjectContext::GetActive()->GetContentDir()
	                                                   : FMapEditorEnvironment::GetUserPrefabFolder();
	FString Path;
	if (!FServices::GetFileDialogs().SaveFile(Options, Path))
	{
		return;
	}
	FString Error;
	if (!Document->SavePrefab(Document->GetSelection(), Path, Error))
	{
		FServices::GetMessageDialog().Show(EMessageType::Error, EMessageButtons::Ok, Error);
		return;
	}
	FMapEditorEnvironment::RefreshPrefabs();
	RefreshAssetRegistry();
	RefreshPlaceables();
	Log(FTextLocalization::Format("Saved prefab {}", Path));
}

// ----- Commands -----

void FMapEditorShell::RegisterCommands()
{
	TSharedRef<FBindingContext> Context = MakeShared<FBindingContext>(FName("MapEditor"), "Map editor");
	using T = EUserInterfaceActionType;
	using M = EModifierKey;
	struct FAddBuilder
	{
		const TSharedRef<FBindingContext>& Context;
		TSharedPtr<FUICommandInfo> operator()(const char* Name, const char* Label, const char* Tip, T Type = T::Button,
		    FInputChord Chord = FInputChord()) const
		{
			return Context->AddCommand(Name, Label, Tip, Type, Chord).ToSharedPtr();
		}
	};
	const FAddBuilder Add{Context};
	Cmd.NewMap = Add("NewMap", "New", "Start a new map", T::Button, FInputChord(EKey::N, M::Control));
	Cmd.OpenMap = Add(
	    "OpenMap", "Open...", "Open a .gmap level or import a legacy map", T::Button, FInputChord(EKey::O, M::Control));
	Cmd.SaveMap = Add("SaveMap", "Save", "Save the map", T::Button, FInputChord(EKey::S, M::Control));
	Cmd.SaveMapAs = Add("SaveMapAs", "Save As...", "Save the map under another name", T::Button,
	    FInputChord(EKey::S, M::Control | M::Shift));
	Cmd.ExportMap = Add("ExportMap", "Export .map...", "Write the map as a Quake .map file", T::Button,
	    FInputChord(EKey::E, M::Control));
	Cmd.Exit = Add("Exit", "Exit", "Close the editor");
	Cmd.NewProject = Add("NewProject", "New Project...", "Create a project and restart the editor on it");
	Cmd.OpenProject = Add("OpenProject", "Open Project...", "Restart the editor on another .gproject");
	Cmd.SetStartupWorld = Add("SetStartupWorld", "Set as Startup World",
	    "Make the current level the world the project's game and editor open first");
	Cmd.Play = Add("Play", "Play", "Play the current level (or the project's startup world) in the viewport", T::Button,
	    FInputChord(EKey::P, M::Alt));
	Cmd.StopPlay = Add("StopPlay", "Stop", "Stop playing and return to editing");
	Cmd.PlayStandalone = Add("PlayStandalone", "Standalone Game",
	    "Play the current level (or the project's startup world) in a separate game window");

	Cmd.Undo = Add("Undo", "Undo", "Undo the last change", T::Button, FInputChord(EKey::Z, M::Control));
	Cmd.Redo = Add("Redo", "Redo", "Redo the change undone last", T::Button, FInputChord(EKey::Y, M::Control));
	Cmd.Cut = Add("Cut", "Cut", "Move the selection to the clipboard", T::Button, FInputChord(EKey::X, M::Control));
	Cmd.Copy = Add("Copy", "Copy", "Copy the selection to the clipboard", T::Button, FInputChord(EKey::C, M::Control));
	Cmd.Paste = Add("Paste", "Paste", "Add the clipboard's objects, one grid step away from where they were", T::Button,
	    FInputChord(EKey::V, M::Control));
	Cmd.Duplicate = Add("Duplicate", "Duplicate", "Copy the selection, one grid step away", T::Button,
	    FInputChord(EKey::D, M::Control));
	Cmd.Delete = Add("Delete", "Delete", "Delete the selection", T::Button, FInputChord(EKey::Delete));
	Cmd.SelectAll = Add("SelectAll", "Select All", "Select every object of the group being edited", T::Button,
	    FInputChord(EKey::A, M::Control));
	Cmd.SelectNone = Add("SelectNone", "Select None", "Clear the selection", T::Button, FInputChord(EKey::Escape));

	Cmd.FrameSelection = Add("FrameSelection", "Frame Selection",
	    "Fit every view to the selection (or the whole map); a view's camera menu frames only that view", T::Button,
	    FInputChord(EKey::L, M::Control));
	Cmd.ResetViews =
	    Add("ResetViews", "Reset Views", "Put every view back at the origin", T::Button, FInputChord(EKey::Home));
	Cmd.ShowGrid = Add("ShowGrid", "Grid", "Show the grid in every view (each view's Show menu can hide it there)",
	    T::ToggleButton, FInputChord(EKey::G, M::Shift));
	Cmd.SnapToGrid = Add("SnapToGrid", "Snap", "Snap to the grid (hold Alt while dragging to move freely)",
	    T::ToggleButton, FInputChord(EKey::N));
	Cmd.GridSmaller =
	    Add("GridSmaller", "Smaller Grid", "Halve the grid size", T::Button, FInputChord(EKey::LeftBracket));
	Cmd.GridLarger =
	    Add("GridLarger", "Larger Grid", "Double the grid size", T::Button, FInputChord(EKey::RightBracket));
	// In Landscape mode [ and ] size the brush instead, as in Unreal; with Ctrl, its strength.
	Cmd.BrushWeaker = Add("BrushWeaker", "Weaker Brush", "Lower the landscape tool's strength", T::Button,
	    FInputChord(EKey::LeftBracket, M::Control));
	Cmd.BrushStronger = Add("BrushStronger", "Stronger Brush", "Raise the landscape tool's strength", T::Button,
	    FInputChord(EKey::RightBracket, M::Control));
	Cmd.SolidView =
	    Add("SolidView", "Solid 3D", "Draw the perspective view lit, with solid faces, instead of wireframes",
	        T::ToggleButton, FInputChord(EKey::F3));

	Cmd.ToolSelect = Add(
	    "ToolSelect", "Select", "Select, and move the selection by dragging it", T::RadioButton, FInputChord(EKey::M));
	Cmd.ToolRotate = Add("ToolRotate", "Rotate", "Turn the selection about its center by dragging", T::RadioButton,
	    FInputChord(EKey::R));
	Cmd.ToolScale = Add("ToolScale", "Scale", "Stretch the selection by dragging (Shift: evenly)", T::RadioButton,
	    FInputChord(EKey::S));
	Cmd.ToolClip =
	    Add("ToolClip", "Clip", "Draw a line to clip the selected brushes", T::RadioButton, FInputChord(EKey::C));
	Cmd.ToolVertex =
	    Add("ToolVertex", "Vertices", "Drag the corners of the selected brush", T::RadioButton, FInputChord(EKey::V));
	Cmd.ToolEdge =
	    Add("ToolEdge", "Edges", "Drag the edges of the selected brush", T::RadioButton, FInputChord(EKey::G));
	Cmd.ToolFace =
	    Add("ToolFace", "Faces", "Drag the faces of the selected brush", T::RadioButton, FInputChord(EKey::F));

	Cmd.AddCube = Add("AddCube", "Add Cube", "Add a cube brush where the view looks", T::Button, FInputChord(EKey::B));
	Cmd.Group = Add("Group", "Group", "Make the selection one group", T::Button, FInputChord(EKey::G, M::Control));
	Cmd.Ungroup =
	    Add("Ungroup", "Ungroup", "Break the selected groups up", T::Button, FInputChord(EKey::U, M::Control));
	Cmd.EnterGroup = Add("EnterGroup", "Edit Group", "Edit the inside of the selected group", T::Button,
	    FInputChord(EKey::PageDown, M::Control));
	Cmd.ScopeUp = Add("ScopeUp", "Leave Group", "Go back to the group around this one", T::Button,
	    FInputChord(EKey::PageUp, M::Control));
	Cmd.HollowIn = Add("HollowIn", "Hollow", "Make the selected brushes rooms: walls inside their outline", T::Button,
	    FInputChord(EKey::Insert, M::Control));
	Cmd.HollowOut = Add("HollowOut", "Hollow Outward", "Make the selected brushes rooms: walls around their outline",
	    T::Button, FInputChord(EKey::Delete, M::Control));
	Cmd.Subtract = Add("Subtract", "Subtract", "Cut the selected brushes out of the brushes they touch", T::Button,
	    FInputChord(EKey::Subtract, M::Control));
	Cmd.Intersect = Add("Intersect", "Intersect", "Replace the selected brushes by their common volume", T::Button,
	    FInputChord(EKey::Add, M::Control));
	Cmd.FlipX = Add("FlipX", "Flip X", "Mirror the selection across its center, along X");
	Cmd.FlipY = Add("FlipY", "Flip Y", "Mirror the selection across its center, along Y");
	Cmd.FlipZ = Add("FlipZ", "Flip Z", "Mirror the selection across its center, along Z");
	Cmd.RotateLeft = Add("RotateLeft", "Rotate 90° Left",
	    "Turn the selection a quarter counterclockwise, as seen in the last 2D view used", T::Button,
	    FInputChord(EKey::Left, M::Control));
	Cmd.RotateRight = Add("RotateRight", "Rotate 90° Right",
	    "Turn the selection a quarter clockwise, as seen in the last 2D view used", T::Button,
	    FInputChord(EKey::Right, M::Control));
	Cmd.SavePrefab = Add("SavePrefab", "Save as Prefab...", "Save the selection as a prefab for the Prefabs panel");

	Cmd.SelectionMode = Add("SelectionMode", "Selection Mode", "Select, move, turn and scale; place actors",
	    T::RadioButton, FInputChord(EKey::One, M::Shift));
	Cmd.LandscapeMode = Add("LandscapeMode", "Landscape Mode", "Make landscapes and sculpt them", T::RadioButton,
	    FInputChord(EKey::Two, M::Shift));
	Cmd.FoliageMode = Add("FoliageMode", "Foliage Mode", "Paint grass, bushes, trees and rocks onto the ground",
	    T::RadioButton, FInputChord(EKey::Three, M::Shift));
	Cmd.BrushEditingMode = Add("BrushEditingMode", "Brush Editing Mode",
	    "Clip brushes and drag their corners, edges and faces", T::RadioButton, FInputChord(EKey::Six, M::Shift));
	Cmd.EditorPreferences = Add("EditorPreferences", "Editor Preferences...",
	    "Graphics, appearance, keyboard shortcuts, viewports, grid and snapping");
	Cmd.ResetLayout = Add("ResetLayout", "Reset Layout", "Go back to the default window layout");
	Cmd.About = Add("About", "About Genesis Editor", "Version and license");
	FInputBindingManager::Get().RegisterContext(Context);
	// The viewports' own commands, before the saved bindings are loaded.
	FMapViewportCommands::Register();

	FUICommandList& List = *Commands;
	const TFunction<bool()> HasDocument = [this] { return Document != nullptr; };
	const TFunction<bool()> HasSelection = [this] { return Document && !Document->GetSelection().empty(); };
	const TFunction<bool()> HasTwoSelected = [this] { return Document && Document->GetSelection().size() >= 2; };

	// File.
	List.MapAction(ToSharedRef(Cmd.NewMap), [this]
	{
		if (ConfirmDiscard())
		{
			NewMap(FMapEditorEnvironment::GetBuiltInGameSet());
		}
	});
	List.MapAction(ToSharedRef(Cmd.OpenMap), [this]
	{
		if (!ConfirmDiscard())
		{
			return;
		}
		FFileDialogOptions Options;
		Options.Title = "Open map";
		Options.Filters = MapFilters();
		Options.InitialPath = DefaultFolder();
		FString Path;
		if (!FServices::GetFileDialogs().OpenFile(Options, Path))
			return;
		if (Path.EndsWith(".gproject", ESearchCase::IgnoreCase))
			SwitchProject("-project=" + Path);
		else
			OpenMap(Path);
	});
	// The current level as a /Game package, when it is saved in the active project.
	const TFunction<FString()> CurrentLevelPackage = [this]() -> FString
	{
		if (!Document || Document->GetPath().IsEmpty() || !AssetRegistry)
			return {};
		const FAssetData* Level = AssetRegistry->FindByFile(Document->GetPath());
		return Level && Level->Metadata.Schema == "Genesis.World" ? Level->PackagePath : FString();
	};
	List.MapAction(ToSharedRef(Cmd.Play), [this] { PlayInViewport(); }, [this] { return !IsPlaying() && Document; });
	List.MapAction(ToSharedRef(Cmd.StopPlay), [this] { StopPlaying(); }, [this] { return IsPlaying(); });
	List.MapAction(ToSharedRef(Cmd.PlayStandalone), [this, CurrentLevelPackage]
	{
		const FProjectContext* Project = FProjectContext::GetActive();
		if (!Project)
		{
			Log(GENESIS_TEXT("Open a project to play its worlds."));
			return;
		}
		if (!ConfirmDiscard())
			return;
		FString World = CurrentLevelPackage();
		if (World.IsEmpty())
			World = Project->GetDescriptor().DefaultMap;
		if (World.IsEmpty())
		{
			FServices::GetMessageDialog().Show(EMessageType::Info, EMessageButtons::Ok,
			    GENESIS_TEXT("Save the level in the project's Content folder, or set a startup world, to play it."));
			return;
		}
		PlayInGame(World);
	});
	List.MapAction(ToSharedRef(Cmd.SetStartupWorld), [this, CurrentLevelPackage]
	{
		const FString Level = CurrentLevelPackage();
		FString Error;
		if (Level.IsEmpty())
			Log(GENESIS_TEXT("Save the level in the project's Content folder first."));
		else if (SetStartupWorld(Level, Error))
			Log(FTextLocalization::Format("{} is now the startup world.", Level));
		else
			Log(Error);
	}, [CurrentLevelPackage] { return FProjectContext::GetActive() && !CurrentLevelPackage().IsEmpty(); });
	List.MapAction(ToSharedRef(Cmd.OpenProject), [this]
	{
		if (!ConfirmDiscard())
			return;
		FFileDialogOptions Options;
		Options.Title = GENESIS_TEXT("Open project");
		Options.Filters = {{GENESIS_TEXT("Genesis projects (*.gproject)"), "*.gproject"}};
		Options.InitialPath = DefaultFolder();
		FString Path;
		if (FServices::GetFileDialogs().OpenFile(Options, Path))
			SwitchProject("-project=" + Path);
	});
	List.MapAction(ToSharedRef(Cmd.NewProject), [this]
	{
		if (!ConfirmDiscard())
			return;
		FFileDialogOptions Options;
		Options.Title = GENESIS_TEXT("New project");
		Options.Filters = {{GENESIS_TEXT("Genesis projects (*.gproject)"), "*.gproject"}};
		Options.DefaultExtension = "gproject";
		Options.InitialPath = DefaultFolder();
		FString Path;
		if (!FServices::GetFileDialogs().SaveFile(Options, Path))
			return;
		// Choosing an existing project opens it rather than failing at startup.
		SwitchProject((std::filesystem::exists(Path.ToWide()) ? "-project=" : "-newproject=") + Path);
	});
	List.MapAction(ToSharedRef(Cmd.SaveMap), [this]
	{
		if (Document->GetPath().IsEmpty())
		{
			Commands->ExecuteAction(*Cmd.SaveMapAs);
			return;
		}
		SaveMap(Document->GetPath());
	}, HasDocument);
	List.MapAction(ToSharedRef(Cmd.SaveMapAs), [this]
	{
		FFileDialogOptions Options;
		Options.Title = "Save map as";
		Options.Filters = {{"Genesis levels (*.gmap)", "*.gmap"}};
		Options.DefaultExtension = "gmap";
		Options.InitialPath = DefaultFolder();
		FString Path;
		if (FServices::GetFileDialogs().SaveFile(Options, Path))
		{
			SaveMap(Path);
		}
	}, HasDocument);
	List.MapAction(ToSharedRef(Cmd.ExportMap), [this]
	{
		FFileDialogOptions Options;
		Options.Title = "Export .map";
		Options.Filters = {{"Quake maps (*.map)", "*.map"}};
		Options.DefaultExtension = "map";
		const FString Base = DefaultFolder();
		Options.InitialPath = Base.EndsWith(".gmap", ESearchCase::IgnoreCase) ? Base.Left(Base.Len() - 5) + ".map"
		    : Base.EndsWith(".qle", ESearchCase::IgnoreCase)                  ? Base.Left(Base.Len() - 4) + ".map"
		                                                                      : Base;
		FString Path;
		if (FServices::GetFileDialogs().SaveFile(Options, Path))
		{
			ExportMap(Path);
		}
	}, HasDocument);
	List.MapAction(ToSharedRef(Cmd.Exit), [this]
	{
		if (ConfirmDiscard())
			Host.Quit();
	});

	// Edit.
	List.MapAction(ToSharedRef(Cmd.Undo), [this]
	{
		Log(FString::Format("Undo {}", Document->GetUndoDescription()));
		Document->Undo();
	}, [this] { return Document && Document->CanUndo(); });
	List.MapAction(ToSharedRef(Cmd.Redo), [this]
	{
		Log(FString::Format("Redo {}", Document->GetRedoDescription()));
		Document->Redo();
	}, [this] { return Document && Document->CanRedo(); });
	List.MapAction(ToSharedRef(Cmd.Copy), [this]
	{
		Clipboard.clear();
		for (Object* Obj : Document->GetSelection())
		{
			Clipboard.push_back(MakeUnique<Object>(*Obj));
		}
		Log(FString::Format("Copied {} object(s)", Clipboard.size()));
	}, HasSelection);
	List.MapAction(ToSharedRef(Cmd.Cut), [this]
	{
		Commands->ExecuteAction(*Cmd.Copy);
		Document->BeginTransaction("Cut");
		Document->DeleteObjects(Document->GetSelection());
		Document->EndTransaction();
	}, HasSelection);
	List.MapAction(ToSharedRef(Cmd.Paste), [this]
	{
		TArray<Object*> Copies;
		const float Step = ViewSettings.GridSize;
		for (const TUniquePtr<Object>& Obj : Clipboard)
		{
			Object* Copy = new Object(*Obj);
			Copy->RegenerateIdentity();
			Vector3d Position;
			Copy->GetPosition(Position).AddVector(Vector3d(Step, Step, 0.0f));
			Copy->SetPosition(Position);
			Copies.push_back(Copy);
		}
		Document->BeginTransaction("Paste");
		Document->AddObjects(Copies);
		Document->EndTransaction();
	}, [this] { return Document && !Clipboard.empty(); });
	List.MapAction(ToSharedRef(Cmd.Duplicate), [this]
	{
		const float Step = ViewSettings.GridSize;
		Document->DuplicateObjects(Document->GetSelection(), Vector3d(Step, Step, 0.0f));
	}, HasSelection);
	// With the landscape's Splines tool, Delete takes the selected spline point.
	const TFunction<bool()> DeletesSplinePoint = [this]
	{
		return Mode == EMapEditorMode::Landscape && LandscapeMode.Tool == ELandscapeTool::Splines &&
		    LandscapeMode.HasSelectedSplinePoint();
	};
	List.MapAction(ToSharedRef(Cmd.Delete), [this, DeletesSplinePoint]
	{
		if (DeletesSplinePoint())
		{
			LandscapeMode.DeleteSelectedSplinePoint();
			return;
		}
		Document->DeleteObjects(Document->GetSelection());
	}, [HasSelection, DeletesSplinePoint] { return DeletesSplinePoint() || HasSelection(); });
	List.MapAction(ToSharedRef(Cmd.SelectAll), [this] { Document->SelectAll(); }, HasDocument);
	List.MapAction(ToSharedRef(Cmd.SelectNone), [this] { Document->ClearSelection(); }, HasSelection);

	// View.
	List.MapAction(ToSharedRef(Cmd.FrameSelection), [this]
	{
		for (const TSharedPtr<FMapViewportClient>& View : Views)
		{
			View->FrameSelection();
		}
	}, HasDocument);
	List.MapAction(ToSharedRef(Cmd.ResetViews), [this]
	{
		for (const TSharedPtr<FMapViewportClient>& View : Views)
		{
			View->ResetCamera();
		}
	}, HasDocument);
	List.MapAction(ToSharedRef(Cmd.ShowGrid), [this]
	{
		ViewSettings.bShowGrid = !ViewSettings.bShowGrid;
		ViewSettings.Changed();
	}, nullptr, [this] { return ViewSettings.bShowGrid; });
	List.MapAction(ToSharedRef(Cmd.SnapToGrid), [this]
	{
		ViewSettings.bSnapToGrid = !ViewSettings.bSnapToGrid;
		ViewSettings.Changed();
	}, nullptr, [this] { return ViewSettings.bSnapToGrid; });
	const TFunction<void(int32, int32)> ChangeBrush = [this](int32 Size, int32 Strength)
	{
		FLandscapeBrush& Brush = Mode == EMapEditorMode::Foliage ? FoliageMode.Brush : LandscapeMode.Brush;
		Brush.Grow(Size);
		Brush.Strengthen(Strength);
		StatusText = FTextLocalization::Format("Brush size {:.0f}, strength {:.2f}", Brush.Radius, Brush.Strength);
		ViewSettings.Changed();
	};
	List.MapAction(ToSharedRef(Cmd.GridSmaller), [this, ChangeBrush]
	{
		if (HasBrushMode())
		{
			ChangeBrush(-1, 0);
			return;
		}
		SetGridSize(ViewSettings.GridSize * 0.5f);
	}, [this] { return HasBrushMode() || ViewSettings.GridSize > FMapViewSettings::MinGridSize; });
	List.MapAction(ToSharedRef(Cmd.GridLarger), [this, ChangeBrush]
	{
		if (HasBrushMode())
		{
			ChangeBrush(1, 0);
			return;
		}
		SetGridSize(ViewSettings.GridSize * 2.0f);
	}, [this] { return HasBrushMode() || ViewSettings.GridSize < FMapViewSettings::MaxGridSize; });
	List.MapAction(
	    ToSharedRef(Cmd.BrushWeaker), [ChangeBrush] { ChangeBrush(0, -1); }, [this] { return HasBrushMode(); });
	List.MapAction(
	    ToSharedRef(Cmd.BrushStronger), [ChangeBrush] { ChangeBrush(0, 1); }, [this] { return HasBrushMode(); });
	List.MapAction(ToSharedRef(Cmd.SolidView),
	    [this]
	{
		const TSharedPtr<FMapViewportClient> View = GetPerspectiveView();
		View->SetViewMode(View->GetViewMode() == EMapViewMode::Wireframe ? EMapViewMode::Lit : EMapViewMode::Wireframe);
	}, [this] { return GetPerspectiveView() != nullptr; }, [this]
	{
		const TSharedPtr<FMapViewportClient> View = GetPerspectiveView();
		return View && View->GetViewMode() != EMapViewMode::Wireframe;
	});

	// Tools.
	const TPair<TSharedPtr<FUICommandInfo>, EMapTool> Tools[] = {
	    {Cmd.ToolSelect, EMapTool::Select},
	    {Cmd.ToolRotate, EMapTool::Rotate},
	    {Cmd.ToolScale, EMapTool::Scale},
	    {Cmd.ToolClip, EMapTool::Clip},
	    {Cmd.ToolVertex, EMapTool::Vertex},
	    {Cmd.ToolEdge, EMapTool::Edge},
	    {Cmd.ToolFace, EMapTool::Face},
	};
	for (const std::pair<std::shared_ptr<FUICommandInfo>, EMapTool>& CommandBinding : Tools)
	{
		const std::shared_ptr<FUICommandInfo>& Command = CommandBinding.first;
		const EMapTool& Tool = CommandBinding.second;

		const EMapTool ThisTool = Tool;
		// Not in Landscape mode, whose left drags sculpt and paint (as Unreal
		// greys the transform tools there): S, R and the others are also
		// keys one flies with.
		List.MapAction(ToSharedRef(Command), [this, ThisTool] { SetTool(ThisTool); },
		    [this] { return !HasBrushMode(); }, [this, ThisTool] { return ViewSettings.Tool == ThisTool; });
	}

	// Object.
	List.MapAction(ToSharedRef(Cmd.AddCube), [this]
	{
		for (const FMapEditorEnvironment::FPrefab& Prefab : FMapEditorEnvironment::GetPrefabs())
		{
			if (Prefab.Name == "Cube" && !Prefab.bUser)
			{
				AddPrefab(Prefab.Path);
				return;
			}
		}
	}, HasDocument);
	List.MapAction(ToSharedRef(Cmd.Group), [this] { Document->GroupObjects(Document->GetSelection()); }, HasSelection);
	List.MapAction(ToSharedRef(Cmd.Ungroup), [this] { Document->UngroupObjects(Document->GetSelection()); }, [this]
	{
		if (!Document)
		{
			return false;
		}
		for (Object* Obj : Document->GetSelection())
		{
			if (FMapDocument::IsGroup(*Obj))
			{
				return true;
			}
		}
		return false;
	});
	List.MapAction(ToSharedRef(Cmd.EnterGroup), [this] { Document->SetScope(*Document->GetSelection()[0]); }, [this] {
		return Document && Document->GetSelection().size() == 1 && FMapDocument::IsGroup(*Document->GetSelection()[0]);
	});
	List.MapAction(ToSharedRef(Cmd.ScopeUp), [this] { Document->ScopeUp(); },
	    [this] { return Document && Document->CanScopeUp(); });
	List.MapAction(ToSharedRef(Cmd.HollowIn), [this]
	{
		if (!Document->HollowObjects(Document->GetSelection(), ViewSettings.GridSize, true))
		{
			Log("Hollow: the brushes are too small for walls as thick as the grid");
		}
	}, HasSelection);
	List.MapAction(ToSharedRef(Cmd.HollowOut), [this]
	{
		if (!Document->HollowObjects(Document->GetSelection(), ViewSettings.GridSize, false))
		{
			Log("Hollow: nothing to hollow in the selection");
		}
	}, HasSelection);
	List.MapAction(ToSharedRef(Cmd.Subtract), [this]
	{
		if (!Document->SubtractSelection())
		{
			Log("Subtract: the selection does not touch other brushes");
		}
	}, HasSelection);
	List.MapAction(ToSharedRef(Cmd.Intersect), [this]
	{
		if (!Document->IntersectSelection())
		{
			Log("Intersect: the selected brushes have nothing in common");
		}
	}, HasTwoSelected);
	const TPair<TSharedPtr<FUICommandInfo>, int32> Flips[] = {{Cmd.FlipX, 0}, {Cmd.FlipY, 1}, {Cmd.FlipZ, 2}};
	for (const std::pair<std::shared_ptr<FUICommandInfo>, int>& CommandBinding : Flips)
	{
		const std::shared_ptr<FUICommandInfo>& Command = CommandBinding.first;
		const int& Axis = CommandBinding.second;

		const int32 ThisAxis = Axis;
		List.MapAction(ToSharedRef(Command), [this, ThisAxis]
		{ Document->FlipObjects(Document->GetSelection(), ThisAxis, SelectionCenter()); }, HasSelection);
	}
	const TFunction<void(float)> RotateQuarter = [this](float Degrees)
	{
		// About the depth axis of the 2D view used last (Z from a perspective view).
		const int32 Axis = GetActiveView()->GetDepthAxis();
		Document->RotateObjects(Document->GetSelection(), Axis, Degrees, SelectionCenter());
	};
	List.MapAction(ToSharedRef(Cmd.RotateLeft), [RotateQuarter] { RotateQuarter(90.0f); }, HasSelection);
	List.MapAction(ToSharedRef(Cmd.RotateRight), [RotateQuarter] { RotateQuarter(-90.0f); }, HasSelection);
	List.MapAction(ToSharedRef(Cmd.SavePrefab), [this] { SaveSelectionAsPrefab(); }, HasSelection);

	// Modes.
	List.MapAction(ToSharedRef(Cmd.SelectionMode), [this] { SetMode(EMapEditorMode::Selection); }, nullptr,
	    [this] { return Mode == EMapEditorMode::Selection; });
	List.MapAction(ToSharedRef(Cmd.LandscapeMode), [this] { SetMode(EMapEditorMode::Landscape); }, nullptr,
	    [this] { return Mode == EMapEditorMode::Landscape; });
	List.MapAction(ToSharedRef(Cmd.FoliageMode), [this] { SetMode(EMapEditorMode::Foliage); }, nullptr,
	    [this] { return Mode == EMapEditorMode::Foliage; });
	List.MapAction(ToSharedRef(Cmd.BrushEditingMode), [this] { SetMode(EMapEditorMode::BrushEditing); }, nullptr,
	    [this] { return Mode == EMapEditorMode::BrushEditing; });

	// Window, Help.
	List.MapAction(ToSharedRef(Cmd.EditorPreferences), [this] { OpenPreferences(); });
	List.MapAction(ToSharedRef(Cmd.ResetLayout), [this] { ResetLayout(); });
	List.MapAction(ToSharedRef(Cmd.About), [this] { ShowAbout(); });

	if (!Host.SettingsDir.IsEmpty())
	{
		FString Text;
		if (FFileHelper::LoadFileToString(Text, Host.SettingsDir + "Bindings.json"))
		{
			if (const TOptional<FJsonValue> Json = FJsonValue::Parse(Text.View()))
			{
				FInputBindingManager::Get().LoadUserBindings(*Json);
			}
		}
	}
}

void FMapEditorShell::RegisterMenus()
{
	FMenuRegistry& Registry = FMenuRegistry::Get();
	FMenuSection& Bar = Registry.RegisterMenu("MapEditor.MainMenu").AddSection("Menus");
	Bar.AddSubMenu("File", "File", "", FName("MapEditor.MainMenu.File"));
	Bar.AddSubMenu("Edit", "Edit", "", FName("MapEditor.MainMenu.Edit"));
	Bar.AddSubMenu("View", "View", "", FName("MapEditor.MainMenu.View"));
	Bar.AddSubMenu("Tools", "Tools", "", FName("MapEditor.MainMenu.Tools"));
	Bar.AddSubMenu("Object", "Object", "", FName("MapEditor.MainMenu.Object"));
	Bar.AddSubMenu("Window", "Window", "", FName("MapEditor.MainMenu.Window"));
	Bar.AddSubMenu("Help", "Help", "", FName("MapEditor.MainMenu.Help"));

	FMenuDefinition& File = Registry.RegisterMenu("MapEditor.MainMenu.File");
	FMenuSection& Open = File.AddSection("Open");
	Open.AddCommand(Cmd.NewMap);
	Open.AddCommand(Cmd.OpenMap);
	FMenuSection& ProjectSection = File.AddSection("Project");
	ProjectSection.AddCommand(Cmd.NewProject);
	ProjectSection.AddCommand(Cmd.OpenProject);
	ProjectSection.AddCommand(Cmd.SetStartupWorld);
	FMenuSection& Save = File.AddSection("Save");
	Save.AddCommand(Cmd.SaveMap);
	Save.AddCommand(Cmd.SaveMapAs);
	Save.AddCommand(Cmd.ExportMap);
	File.AddSection("Exit").AddCommand(Cmd.Exit);

	// Undo and redo name what they would do, so the section is built on open.
	FMenuDefinition& Edit = Registry.RegisterMenu("MapEditor.MainMenu.Edit");
	Edit.AddDynamicSection("History", [this](FMenuSection& Section, const FMenuContext&)
	{
		const bool bUndo = Document && Document->CanUndo();
		const bool bRedo = Document && Document->CanRedo();
		Section.AddCommand(
		    Cmd.Undo, bUndo ? FString::Format("Undo {}", Document->GetUndoDescription()) : FString("Undo"));
		Section.AddCommand(
		    Cmd.Redo, bRedo ? FString::Format("Redo {}", Document->GetRedoDescription()) : FString("Redo"));
	});
	FMenuSection& Clip = Edit.AddSection("Clipboard");
	Clip.AddCommand(Cmd.Cut);
	Clip.AddCommand(Cmd.Copy);
	Clip.AddCommand(Cmd.Paste);
	Clip.AddCommand(Cmd.Duplicate);
	Clip.AddCommand(Cmd.Delete);
	FMenuSection& Selection = Edit.AddSection("Selection");
	Selection.AddCommand(Cmd.SelectAll);
	Selection.AddCommand(Cmd.SelectNone);
	Edit.AddSection("Configuration", "Configuration").AddCommand(Cmd.EditorPreferences);

	FMenuDefinition& View = Registry.RegisterMenu("MapEditor.MainMenu.View");
	FMenuSection& Camera = View.AddSection("Camera");
	Camera.AddCommand(Cmd.FrameSelection);
	Camera.AddCommand(Cmd.ResetViews);
	Camera.AddCommand(Cmd.SolidView);
	FMenuSection& Grid = View.AddSection("Grid", "Grid");
	Grid.AddCommand(Cmd.ShowGrid);
	Grid.AddCommand(Cmd.SnapToGrid);
	Grid.AddCommand(Cmd.GridSmaller);
	Grid.AddCommand(Cmd.GridLarger);
	Grid.AddSubMenu("GridSize", "Grid Size", "Map units between grid lines",
	    [this](FMenuSection& Section, const FMenuContext&)
	{
		for (const float Size : {1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f, 128.0f, 256.0f})
		{
			FUIAction Action;
			Action.Execute = FSimpleDelegate::CreateLambda([this, Size] { SetGridSize(Size); });
			Action.GetCheckState = TDelegate<ECheckBoxState()>::CreateLambda([this, Size]
			{ return ViewSettings.GridSize == Size ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; });
			Section.AddEntry(FName(FString::Format("Grid{}", Size)), FString::Format("{:.0f}", Size), "", Action,
			    EUserInterfaceActionType::RadioButton);
		}
	});
	View.AddSection("Scale").AddSubMenu("AppScale", "Interface Scale",
	    "Scale the interface on top of the monitor's DPI", [this](FMenuSection& Section, const FMenuContext&)
	{
		for (const float Scale : {1.0f, 1.25f, 1.5f, 2.0f})
		{
			FUIAction Action;
			Action.Execute = FSimpleDelegate::CreateLambda([this, Scale] { Host.SetAppScale(Scale); });
			Action.GetCheckState = TDelegate<ECheckBoxState()>::CreateLambda([Scale]
			{
				return FMath::IsNearlyEqual(FSlateApplication::Get().GetApplicationScale(), Scale, 0.01f)
				    ? ECheckBoxState::Checked
				    : ECheckBoxState::Unchecked;
			});
			Section.AddEntry(FName(FString::Format("Scale{}", Scale * 100.0f)),
			    FString::Format("{:.0f}%", Scale * 100.0f), "", Action, EUserInterfaceActionType::RadioButton);
		}
	});

	FMenuDefinition& ToolsMenu = Registry.RegisterMenu("MapEditor.MainMenu.Tools");
	FMenuSection& Edit2D = ToolsMenu.AddSection("Tools", "In the 2D views");
	for (const TSharedPtr<FUICommandInfo>& Command : {Cmd.ToolSelect, Cmd.ToolRotate, Cmd.ToolScale, Cmd.ToolClip})
	{
		Edit2D.AddCommand(Command);
	}
	FMenuSection& BrushTools = ToolsMenu.AddSection("Brush", "One brush");
	for (const TSharedPtr<FUICommandInfo>& Command : {Cmd.ToolVertex, Cmd.ToolEdge, Cmd.ToolFace})
	{
		BrushTools.AddCommand(Command);
	}

	// Prefabs and the entity classes of the current game set.
	FMenuDefinition& ObjectMenu = Registry.RegisterMenu("MapEditor.MainMenu.Object");
	FMenuSection& Add = ObjectMenu.AddSection("Add", "Add");
	Add.AddCommand(Cmd.AddCube);
	Add.AddSubMenu("AddBrush", "Add Prefab", "Add a prefab where the view looks",
	    [this](FMenuSection& Section, const FMenuContext&)
	{
		for (const FMapEditorEnvironment::FPrefab& Prefab : FMapEditorEnvironment::GetPrefabs())
		{
			const FString Path = Prefab.Path;
			FUIAction Action;
			Action.Execute = FSimpleDelegate::CreateLambda([this, Path] { AddPrefab(Path); });
			Action.CanExecute = TDelegate<bool()>::CreateLambda([this] { return Document != nullptr; });
			Section.AddEntry(
			    FName(Path), Prefab.bUser ? FString::Format("{} (yours)", Prefab.Name) : Prefab.Name, "", Action);
		}
	});
	Add.AddSubMenu("AddEntity", "Add Entity", "Add an entity where the view looks",
	    [this](FMenuSection& Section, const FMenuContext&)
	{
		TArray<FString> Categories;
		const TArray<FMapEditorEnvironment::FEntityClass> Classes = FMapEditorEnvironment::GetPlaceableEntities();
		for (const FMapEditorEnvironment::FEntityClass& Class : Classes)
		{
			if (std::find(Categories.begin(), Categories.end(), Class.Category) == Categories.end())
			{
				Categories.push_back(Class.Category);
			}
		}
		for (const FString& Category : Categories)
		{
			Section.AddSubMenu(FName(Category), Category, "",
			    [this, Category, Classes](FMenuSection& Sub, const FMenuContext&)
			{
				for (const FMapEditorEnvironment::FEntityClass& Class : Classes)
				{
					if (Class.Category != Category)
					{
						continue;
					}
					const FString ClassName = Class.ClassName;
					FUIAction Action;
					Action.Execute = FSimpleDelegate::CreateLambda([this, ClassName] { AddEntity(ClassName); });
					Action.CanExecute = TDelegate<bool()>::CreateLambda([this] { return Document != nullptr; });
					Sub.AddEntry(FName(ClassName), Class.Label, ClassName, Action);
				}
			});
		}
	});
	FMenuSection& Groups = ObjectMenu.AddSection("Groups", "Groups");
	Groups.AddCommand(Cmd.Group);
	Groups.AddCommand(Cmd.Ungroup);
	Groups.AddCommand(Cmd.EnterGroup);
	Groups.AddCommand(Cmd.ScopeUp);
	FMenuSection& Brushes = ObjectMenu.AddSection("Brushes", "Brushes");
	Brushes.AddCommand(Cmd.HollowIn);
	Brushes.AddCommand(Cmd.HollowOut);
	Brushes.AddCommand(Cmd.Subtract);
	Brushes.AddCommand(Cmd.Intersect);
	FMenuSection& Transform = ObjectMenu.AddSection("Transform", "Transform");
	Transform.AddCommand(Cmd.RotateLeft);
	Transform.AddCommand(Cmd.RotateRight);
	Transform.AddCommand(Cmd.FlipX);
	Transform.AddCommand(Cmd.FlipY);
	Transform.AddCommand(Cmd.FlipZ);
	ObjectMenu.AddSection("Prefab").AddCommand(Cmd.SavePrefab);

	FMenuDefinition& Window = Registry.RegisterMenu("MapEditor.MainMenu.Window");
	Window.AddDynamicSection(
	    "Tabs", [this](FMenuSection& Section, const FMenuContext&) { TabManager->PopulateTabSpawnerMenu(Section); });
	// The project's worlds, as Unreal's Window > World Composition (Levels).
	Window.AddDynamicSection("World", [this](FMenuSection& Section, const FMenuContext&)
	{
		Section.AddSubMenu("WorldComposition", GENESIS_TEXT("World Composition"),
		    GENESIS_TEXT("Arrange the regions of a world made of separately saved levels"),
		    [this](FMenuSection& Sub, const FMenuContext&)
		{
			if (AssetRegistry)
				for (const FAssetData& Asset : AssetRegistry->GetAssets())
				{
					if (Asset.Metadata.Schema != "Genesis.WorldComposition")
						continue;
					const FAssetReference Reference = Asset.GetReference();
					const FString Name =
					    FString::FromWide(std::filesystem::path(Asset.FilePath.ToWide()).stem().wstring());
					FUIAction Open;
					Open.Execute = FSimpleDelegate::CreateLambda(
					    [this, Reference, Name] { OpenWorldComposition(Reference, Name); });
					Sub.AddEntry(
					    FName(Asset.PackagePath), Asset.PackagePath, GENESIS_TEXT("Open this world's regions"), Open);
				}
			FUIAction Create;
			Create.Execute = FSimpleDelegate::CreateLambda([this] { NewWorldComposition(); });
			Create.CanExecute = TDelegate<bool()>::CreateLambda([] { return FProjectContext::GetActive() != nullptr; });
			Sub.AddEntry("NewWorldComposition", GENESIS_TEXT("New World Composition"),
			    GENESIS_TEXT("Make an empty world in the project's Worlds folder and open it"), Create);
		});
	});
	Window.AddSection("Layout").AddCommand(Cmd.ResetLayout);

	Registry.RegisterMenu("MapEditor.MainMenu.Help").AddSection("About").AddCommand(Cmd.About);

	// The modes and Add, as in Unreal's main toolbar.
	FMenuSection& ModeList = Registry.RegisterMenu("MapEditor.Modes").AddSection("Modes", "Editor Modes");
	ModeList.AddCommand(Cmd.SelectionMode);
	ModeList.AddCommand(Cmd.LandscapeMode);
	ModeList.AddCommand(Cmd.FoliageMode);
	ModeList.AddCommand(Cmd.BrushEditingMode);
	FMenuDefinition& QuickAdd = Registry.RegisterMenu("MapEditor.QuickAdd");
	for (const char* Category : {"Basic", "Lights", "Shapes", "Geometry"})
	{
		const FString Name = Category;
		QuickAdd.AddDynamicSection(FName(Name), [this, Name](FMenuSection& Section, const FMenuContext&)
		{
			Section.Label = Name;
			for (const FMapPlaceable& Item : GetPlaceables())
			{
				if (Item.Category != Name)
				{
					continue;
				}
				FUIAction Action;
				Action.Execute = FSimpleDelegate::CreateLambda([this, Item] { Place(Item); });
				Action.CanExecute = TDelegate<bool()>::CreateLambda([this] { return Document != nullptr; });
				Section.AddEntry(FName(Item.Id), Item.Label, Item.ToolTip, Action);
			}
		});
	}

	FMenuDefinition& Bars = Registry.RegisterMenu("MapEditor.ToolBar");
	Bars.AddSection("File").AddCommand(Cmd.SaveMap);
	FMenuSection& Modes = Bars.AddSection("Modes");
	Modes.AddWidget("Mode", [this](const FMenuContext& Context) -> TSharedRef<SWidget>
	{
		return SNew(SComboButton)
		    .ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FToolBarStyle>("ToolBar").Button)
		    .IsFocusable(false)
		    .ToolTipText(
		        GENESIS_TEXT("The editor mode: what the tools and the left panel are for (Shift+1, Shift+2, Shift+6)"))
		    .OnGetMenuContent([Context] {
			return FMenuRegistry::Get().GenerateMenuWidget("MapEditor.Modes", Context);
		}).ButtonContent()[SNew(STextBlock).Text([this] { return FString::Format("{} Mode", ToString(Mode)); })];
	});
	Modes.AddSubMenu(
	    "QuickAdd", "Add", "Add an entity or a brush where the active view looks", FName("MapEditor.QuickAdd"));
	FMenuSection& History = Bars.AddSection("History");
	History.AddCommand(Cmd.Undo);
	History.AddCommand(Cmd.Redo);
	FMenuSection& PlaySection = Bars.AddSection("Play");
	PlaySection.AddCommand(Cmd.Play);
	PlaySection.AddCommand(Cmd.StopPlay);
	PlaySection.AddCommand(Cmd.PlayStandalone);
}

// ----- About -----

void FMapEditorShell::ShowAbout()
{
	const TSharedPtr<SWindow> Parent = MainWindow.lock();
	// The OK button closes the window it is in, which does not exist yet
	// when the button is made.
	TSharedPtr<std::weak_ptr<SWindow>> Self = std::make_shared<TWeakPtr<SWindow>>();
	const TSharedRef<SWindow> About =
	    SNew(SWindow)
	        .Title(GENESIS_TEXT("About Genesis Editor"))
	        .ClientSize(FVector2(460.0f, 220.0f))[SNew(SBorder)
	                .BorderImage(&FAppStyle::Get().GetBrush("Panel"))
	                .Padding(FMargin(20.0f, 16.0f))[SNew(SVerticalBox) +
	                    SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
	                            .Text(GENESIS_TEXT("Genesis Editor"))
	                            .TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("BoldText"))] +
	                    SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))[SNew(STextBlock)
	                            .AutoWrapText(true)
	                            .Text(GENESIS_TEXT("The map editor for Genesis Engine, built on Genesis UI. "
	                                               "It descends from Qoole 99 (GX Media, Inc.) and is free "
	                                               "software under the GNU GPL, version 2 or later."))] +
	                    SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))[SNew(STextBlock)
	                            .TextStyle(&SmallText())
	                            .Text(GENESIS_TEXT("Copyright (C) Dazzle Software, LLC.  Fonts: Noto Sans "
	                                               "(SIL Open Font License)."))] +
	                    SVerticalBox::Slot().FillHeight(1.0f) +
	                    SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)[SNew(SButton)
	                            .Text(GENESIS_TEXT("OK"))
	                            .OnClicked([Self]
	{
		if (const TSharedPtr<SWindow> Window = Self->lock())
		{
			FSlateApplication::Get().RequestDestroyWindow(ToSharedRef(Window));
		}
		return FReply::Handled();
	})]]];
	*Self = About.ToSharedPtr();
	FSlateApplication::Get().AddModalWindow(About, Parent);
}

TSharedRef<SWidget> FMapEditorShell::MakeContent(const TSharedRef<SWindow>& Window)
{
	MainWindow = Window.ToSharedPtr();
	FString Saved;
	if (!Host.SettingsDir.IsEmpty())
	{
		FFileHelper::LoadFileToString(Saved, Host.SettingsDir + "Layout.json");
	}
	const TSharedRef<FDockLayout> Layout = FTabManager::LoadLayout(Saved, MakeDefaultLayout());
	DockArea = TabManager->RestoreFrom(Layout, Window.ToSharedPtr()).ToSharedPtr();
	Window->SetOnRequestClose(FRequestWindowClose::CreateLambda([this] { return ConfirmDiscard(); }));
	UpdateTitle();

	const FMenuContext Context(ToSharedRef(Commands));
	FMenuRegistry& Registry = FMenuRegistry::Get();
	return SNew(SCommandBindingScope)
	    .CommandList(Commands)
	        [SNew(SVerticalBox) +
	         SVerticalBox::Slot().AutoHeight()
	             [SNew(SBorder)
	                  .BorderImage(&FAppStyle::Get().GetBrush("Header"))
	                  .Padding(FMargin(10.0f, 2.0f))
	                      [SNew(SHorizontalBox) +
	                       SHorizontalBox::Slot()
	                           .AutoWidth()
	                           .VAlign(VAlign_Center)
	                           .Padding(FMargin(0.0f, 0.0f, 12.0f, 0.0f))
	                               [SNew(STextBlock)
	                                    .Text(GENESIS_TEXT("Genesis"))
	                                    .TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("BoldText"))] +
	                       SHorizontalBox::Slot().AutoWidth()[Registry.GenerateMenuBar("MapEditor.MainMenu", Context)] +
	                       SHorizontalBox::Slot()
	                           .FillWidth(1.0f)
	                           .HAlign(HAlign_Right)
	                           .VAlign(VAlign_Center)[SNew(STextBlock)
	                                                      .TextStyle(&SmallText())
	                                                      .Text(
	                                                          [this]
	                                                          {
		                                                          if (!Document)
		                                                          {
			                                                          return FString();
		                                                          }
		                                                          FString Text = Document->GetDisplayName();
		                                                          if (&Document->GetScope() != &Document->GetRoot())
		                                                          {
			                                                          const char* Name =
			                                                              Document->GetScope().GetObjName();
			                                                          Text += FString::Format(
			                                                              "  ·  editing {}",
			                                                              Name && Name[0] ? FString::FromAnsi(Name)
			                                                                              : FString("a group"));
		                                                          }
		                                                          return Text;
	                                                          })]]] +
	         SVerticalBox::Slot().AutoHeight()[Registry.GenerateToolBar("MapEditor.ToolBar", Context)] +
	         SVerticalBox::Slot().FillHeight(1.0f)[SAssignNew(DockHost, SBox)[ToSharedRef(DockArea)]] +
	         SVerticalBox::Slot()
	             .AutoHeight()
	                 [SNew(SBorder)
	                      .BorderImage(&FAppStyle::Get().GetBrush("Header"))
	                      .Padding(FMargin(10.0f, 3.0f))
	                          [SNew(SHorizontalBox) +
	                           SHorizontalBox::Slot().FillWidth(1.0f)[SNew(STextBlock)
	                                                                      .TextStyle(&SmallText())
	                                                                      .Clipping(EWidgetClipping::ClipToBounds)
	                                                                      .Text([this] { return StatusText; })] +
	                           SHorizontalBox::Slot().AutoWidth().Padding(FMargin(16.0f, 0.0f))
	                               [SNew(STextBlock).TextStyle(&SmallText()).Text([this] { return CursorText; })] +
	                           SHorizontalBox::Slot().AutoWidth()
	                               [SNew(STextBlock)
	                                    .TextStyle(&SmallText())
	                                    .Text(
	                                        [this]
	                                        {
		                                        if (!Document)
		                                        {
			                                        return FString();
		                                        }
		                                        const TFunction<FString(SIZE_T, const char*, const char*)> Count =
		                                            [](SIZE_T N, const char* One, const char* Many)
		                                        { return FString::Format("{} {}", N, N == 1 ? One : Many); };
		                                        return FString::Format(
		                                            "{}  ·  {}  ·  {}  ·  {}  ·  {} selected  ·  grid {:.0f}{}",
		                                            ToString(ViewSettings.Tool),
		                                            Count(static_cast<SIZE_T>(Document->CountBrushes()), "brush",
		                                                  "brushes"),
		                                            Count(static_cast<SIZE_T>(Document->CountMeshes()), "mesh",
		                                                  "meshes"),
		                                            Count(static_cast<SIZE_T>(Document->CountEntities()), "entity",
		                                                  "entities"),
		                                            Document->GetSelection().size(), ViewSettings.GridSize,
		                                            ViewSettings.bSnapToGrid ? " (snap)" : "");
	                                        })]]]];
}

void FMapEditorShell::ResetLayout()
{
	const TSharedPtr<SWindow> Window = MainWindow.lock();
	if (!Window || !DockHost)
	{
		return;
	}
	TabManager->CloseAllAreas();
	Outliner.reset();
	Properties.reset();
	PlaceActors.reset();
	ViewportLayout.reset();
	DockArea = TabManager->RestoreFrom(MakeDefaultLayout(), Window).ToSharedPtr();
	DockHost->SetContent(ToSharedRef(DockArea));
	Log("Layout reset");
}

void FMapEditorShell::SaveSettings()
{
	SaveEditorSettings();
	if (Host.SettingsDir.IsEmpty())
	{
		return;
	}
	const FString Layout = TabManager->PersistLayout()->ToJson().ToString();
	FFileHelper::SaveArrayToFile(
	    TArrayView<const uint8>(reinterpret_cast<const uint8*>(*Layout), static_cast<SIZE_T>(Layout.Len())),
	    Host.SettingsDir + "Layout.json");
	const FString Bindings = FInputBindingManager::Get().SaveUserBindings().ToString();
	FFileHelper::SaveArrayToFile(
	    TArrayView<const uint8>(reinterpret_cast<const uint8*>(*Bindings), static_cast<SIZE_T>(Bindings.Len())),
	    Host.SettingsDir + "Bindings.json");
}
