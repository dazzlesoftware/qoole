// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "SLandscapeEditorPanel.h"
#include "MapFeaturePanelWidgets.h"
#include "MapCoreHeaders.h"
#include "MapDocument.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SColorPicker.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Text/SEditableText.h"
#include "Services/MessageDialog.h"
#include "Services/Services.h"
#include "Services/FileDialogs.h"
#include "MapLandscapeTextures.h"
#include <filesystem>
using namespace MapFeaturePanelWidgets;
namespace
{
const FTextBlockStyle& SmallText()
{
	return FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText");
}
} // namespace
void SLandscapeEditorPanel::Construct(const FArguments&, FMapLandscapeMode* InMode, FMapFeaturePanelHost InHost)
{
	check(InMode);
	Mode = InMode;
	Host = std::move(InHost);
	ChildSlot[MakePanel()];
}

TSharedRef<SWidget> SLandscapeEditorPanel::MakePanel()
{
	FMapLandscapeMode* Land = Mode;
	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	const auto Pad = [&Box](const TSharedRef<SWidget>& Widget, float Top = 4.0f) {
		Box->AddSlot().AutoHeight().Padding(FMargin(8.0f, Top, 8.0f, 0.0f))[Widget];
	};

	// Manage: a new landscape, as Unreal's New Landscape panel.
	Box->AddSlot().AutoHeight()[PanelHeader("New Landscape")];
	Pad(SpinRow<int32>(
			"Quads X", "Quads along X (Unreal's section size times components)", [Land] { return Land->NewQuadsX; },
			[Land](int32 V) { Land->NewQuadsX = V; }, 1, Landscape::MaxQuads, 0),
		6.0f);
	Pad(SpinRow<int32>(
		"Quads Y", "Quads along Y", [Land] { return Land->NewQuadsY; }, [Land](int32 V) { Land->NewQuadsY = V; }, 1,
		Landscape::MaxQuads, 0));
	Pad(SpinRow<float>(
		"Quad Size", "Map units between heights", [Land] { return Land->NewQuadSize; },
		[Land](float V) { Land->NewQuadSize = V; }, 1.0f, 1024.0f, 0));
	Pad(SNew(STextBlock).TextStyle(&SmallText()).AutoWrapText(true).Text([Land] {
		return FString::Format("{:.0f} x {:.0f} units, {} heights, centered on the origin.",
							   Land->NewQuadsX * Land->NewQuadSize, Land->NewQuadsY * Land->NewQuadSize,
							   (Land->NewQuadsX + 1) * (Land->NewQuadsY + 1));
	}));
	Pad(SNew(SHorizontalBox) +
			SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
												   .Text("Create")
												   .ToolTipText("Add a flat landscape of this size")
												   .IsEnabled([this] { return GetDocument() != nullptr; })
												   .OnClicked([this] {
													   if (Object* Created =
															   Mode->CreateLandscape(Vector3d(0.0f, 0.0f, 0.0f)))
													   {
														   Log(FString::Format("Created a landscape of {} x {} quads",
																			   Created->GetLandscape().GetQuadsX(),
																			   Created->GetLandscape().GetQuadsY()));
													   }
													   return FReply::Handled();
												   })] +
			SHorizontalBox::Slot().AutoWidth().Padding(FMargin(
				6.0f, 0.0f, 0.0f,
				0.0f))[SNew(SButton)
						   .Text("Import from File...")
						   .ToolTipText("A landscape from a heightmap: one height per pixel, Quad Size apart (big "
										"images are resampled to fit)")
						   .IsEnabled([this] { return GetDocument() != nullptr; })
						   .OnClicked([this] {
							   FString Path;
							   if (PickLandscapeFile(false, "Import landscape from heightmap", "", Path))
							   {
								   FString Error;
								   const Object* Created =
									   Mode->ImportLandscape(Path, Vector3d(0.0f, 0.0f, 0.0f), Error);
								   ReportLandscapeFile(
									   Created != nullptr,
									   Created ? FString::Format("Imported a landscape of {} x {} quads from {}",
																 Created->GetLandscape().GetQuadsX(),
																 Created->GetLandscape().GetQuadsY(), Path)
											   : FString(),
									   Error);
							   }
							   return FReply::Handled();
						   })],
		6.0f);
	// Heightmaps of the selected (or first) landscape, as Unreal's Import and
	// Export tools.
	Box->AddSlot().AutoHeight().Padding(FMargin(0.0f, 12.0f, 0.0f, 0.0f))[PanelHeader("Heightmap")];
	Pad(SpinRow<float>(
			"Z Range", "Map units from black to white; mid-grey is height 0 (512 is Unreal's Z scale of 100)",
			[Land] { return Land->HeightmapZRange; }, [Land](float V) { Land->HeightmapZRange = V; }, 1.0f, 65536.0f,
			0),
		6.0f);
	Pad(SNew(SHorizontalBox) +
			SHorizontalBox::Slot().AutoWidth()
				[SNew(SButton)
					 .Text("Import...")
					 .ToolTipText(
						 "New heights for the selected landscape (or the first) from a heightmap, stretched over it")
					 .IsEnabled([Land] { return Land->GetTargetLandscape() != nullptr; })
					 .OnClicked([this, Land] {
						 FString Path;
						 if (Object* Target = Land->GetTargetLandscape();
							 Target && PickLandscapeFile(false, "Import heightmap", "", Path))
						 {
							 FString Error;
							 const bool bOk = Land->ImportHeightmap(*Target, Path, Error);
							 ReportLandscapeFile(bOk, FString::Format("Imported heights from {}", Path), Error);
						 }
						 return FReply::Handled();
					 })] +
			SHorizontalBox::Slot().AutoWidth().Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))
				[SNew(SButton)
					 .Text("Export...")
					 .ToolTipText(
						 "The selected landscape's heights (or the first's) as a 16-bit heightmap, a pixel per height")
					 .IsEnabled([Land] { return Land->GetTargetLandscape() != nullptr; })
					 .OnClicked([this, Land] {
						 FString Path;
						 if (const Object* Target = Land->GetTargetLandscape();
							 Target && PickLandscapeFile(true, "Export heightmap", "Heightmap.png", Path))
						 {
							 FString Error;
							 const bool bOk = Land->ExportHeightmap(*Target, Path, Error);
							 ReportLandscapeFile(bOk, FString::Format("Exported heights to {}", Path), Error);
						 }
						 return FReply::Handled();
					 })],
		6.0f);
	Box->AddSlot().AutoHeight().Padding(FMargin(0.0f, 12.0f, 0.0f, 0.0f))[PanelHeader("Sculpt")];
	const TSharedRef<SWrapBox> Tools = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2(4.0f, 4.0f));
	const TPair<ELandscapeTool, const char*> ToolList[] = {
		{ELandscapeTool::Sculpt, "Raise the ground under the brush (Shift lowers it)"},
		{ELandscapeTool::Smooth, "Even out bumps under the brush"},
		{ELandscapeTool::Flatten, "Pull the ground to the height where the stroke began, or to a set height"},
		{ELandscapeTool::Ramp, "Drag from one point to another for a slope between them, the brush's size wide"},
		{ELandscapeTool::Erosion, "Wear steep slopes down onto the ground below them"},
		{ELandscapeTool::Noise, "Roughen the ground"},
		{ELandscapeTool::Splines,
		 "Edit splines: Ctrl+click adds a point, drag moves one, Delete removes it; Apply shapes the ground to them"},
		{ELandscapeTool::Paint, "Paint the selected paint layer (Shift erases it)"},
	};
	for (const auto& [Tool, Tip] : ToolList)
	{
		const ELandscapeTool ThisTool = Tool;
		Tools->AddSlot()[SNew(SChoiceButton, FString(ToString(Tool)), FString(Tip))
							 .IsChosen([Land, ThisTool] { return Land->Tool == ThisTool; })
							 .OnClicked([this, Land, ThisTool] {
								 Land->Tool = ThisTool;
								 InvalidateViews();
							 })];
	}
	Pad(Tools, 6.0f);
	// The chosen tool's own settings, as Unreal's Tool Settings.
	const auto ForTool = [Land](ELandscapeTool Tool) {
		return TAttribute<EVisibility>(
			[Land, Tool] { return Land->Tool == Tool ? EVisibility::Visible : EVisibility::Collapsed; });
	};
	{
		const TSharedRef<SHorizontalBox> Modes =
			SNew(SHorizontalBox) + SHorizontalBox::Slot()
									   .AutoWidth()
									   .VAlign(VAlign_Center)
									   .Padding(FMargin(0.0f, 0.0f, 8.0f, 0.0f))[SNew(STextBlock).Text("Mode")];
		for (const ELandscapeFlattenMode FlattenMode :
			 {ELandscapeFlattenMode::Both, ELandscapeFlattenMode::Raise, ELandscapeFlattenMode::Lower})
		{
			Modes->AddSlot().AutoWidth().Padding(
				FMargin(0.0f, 0.0f, 4.0f,
						0.0f))[SNew(SChoiceButton, FString(ToString(FlattenMode)),
									FString(FlattenMode == ELandscapeFlattenMode::Both	  ? "Raise and lower the ground"
											: FlattenMode == ELandscapeFlattenMode::Raise ? "Only raise the ground"
																						  : "Only lower the ground"))
								   .IsChosen([Land, FlattenMode] { return Land->FlattenMode == FlattenMode; })
								   .OnClicked([Land, FlattenMode] { Land->FlattenMode = FlattenMode; })];
		}
		Pad(SNew(SBox).Visibility(ForTool(ELandscapeTool::Flatten))
				[SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[Modes] +
				 SVerticalBox::Slot().AutoHeight().Padding(FMargin(
					 0.0f, 4.0f, 0.0f,
					 0.0f))[SNew(SCheckBox)
								.Text(FString("Use Target Height"))
								.ToolTipText("Flatten to the height below, not to where the stroke began")
								.IsChecked([Land] {
									return Land->bFlattenToTarget ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
								})
								.OnCheckStateChanged([this, Land](ECheckBoxState State) {
									Land->bFlattenToTarget = State == ECheckBoxState::Checked;
									InvalidateViews();
								})] +
				 SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))[SpinRow<float>(
					 "Target Height", "The height Flatten pulls to, in the landscape's units",
					 [Land] { return Land->FlattenTarget; }, [Land](float V) { Land->FlattenTarget = V; }, -65536.0f,
					 65536.0f, 0)]],
			6.0f);
	}
	Pad(SNew(SBox).Visibility(ForTool(ELandscapeTool::Erosion))[SpinRow<float>(
			"Talus Angle", "Slopes steeper than this, in degrees, wear down", [Land] { return Land->ErosionTalus; },
			[Land](float V) { Land->ErosionTalus = V; }, 1.0f, 89.0f, 0)],
		6.0f);
	Pad(SNew(SBox).Visibility(ForTool(ELandscapeTool::Paint))[SpinRow<float>(
			"Target Value", "The weight painting moves toward (Unreal's Tool Target Value)",
			[Land] { return Land->PaintTarget; }, [Land](float V) { Land->PaintTarget = V; }, 0.0f, 1.0f, 2)],
		6.0f);
	{
		TSharedPtr<SBox> SplineHost;
		Pad(SAssignNew(SplineHost, SBox).Visibility(ForTool(ELandscapeTool::Splines)), 6.0f);
		LandscapeSplineHost = SplineHost;
		SplineHost->SetContent(MakeLandscapeSplineSettings());
	}
	Pad(SNew(SBox).Visibility(
			ForTool(ELandscapeTool::Ramp))[SNew(STextBlock)
											   .TextStyle(&SmallText())
											   .AutoWrapText(true)
											   .Text("Drag from where the ramp starts to where it ends; it is laid "
													 "when the button comes up, the brush's size wide.")],
		6.0f);
	// Paint layers, as Unreal's Paint panel lists them.
	Box->AddSlot().AutoHeight().Padding(FMargin(0.0f, 12.0f, 0.0f, 0.0f))[PanelHeader("Paint Layers")];
	TSharedPtr<SBox> LayerHost;
	Pad(SAssignNew(LayerHost, SBox), 6.0f);
	LandscapeLayerHost = LayerHost;
	LandscapeLayerSignature = "(none yet)";
	LayerHost->SetContent(MakeLandscapeLayerList());
	Pad(SNew(SHorizontalBox) +
			SHorizontalBox::Slot()
				.AutoWidth()[SNew(SButton)
								 .Text("Add Layer")
								 .ToolTipText("A new paint layer on the selected landscape (or the first one)")
								 .IsEnabled([Land] { return Land->GetTargetLandscape() != nullptr; })
								 .OnClicked([this, Land] {
									 if (Object* Target = Land->GetTargetLandscape(); Target && Land->AddLayer(*Target))
									 {
										 Land->Tool = ELandscapeTool::Paint;
										 InvalidateViews();
									 }
									 return FReply::Handled();
								 })],
		6.0f);
	// Weightmaps of the layer Paint is on, as Unreal's layer Import/Export.
	const auto HasLayer = [Land] {
		const Object* Target = Land->GetTargetLandscape();
		return Target && Land->SelectedLayer >= 0 && Land->SelectedLayer < Target->GetLandscape().GetNumLayers();
	};
	const auto LayerName = [Land, HasLayer] {
		return HasLayer() ? FString(Land->GetTargetLandscape()
										->GetLandscape()
										.GetLayers()[static_cast<SIZE_T>(Land->SelectedLayer)]
										.name)
						  : FString();
	};
	Pad(SNew(STextBlock).TextStyle(&SmallText()).AutoWrapText(true).Text([HasLayer, LayerName] {
		return HasLayer() ? FString::Format("Weightmap of {} (white is all {}, black none):", LayerName(), LayerName())
						  : FString("Weightmaps: add a layer first.");
	}),
		8.0f);
	Pad(SNew(SHorizontalBox) +
			SHorizontalBox::Slot()
				.AutoWidth()[SNew(SButton)
								 .Text("Import...")
								 .ToolTipText("The layer's weights from a weightmap, stretched over the landscape (the "
											  "other layers give way)")
								 .IsEnabled(HasLayer)
								 .OnClicked([this, Land, LayerName] {
									 FString Path;
									 const FString Name = LayerName();
									 if (Object* Target = Land->GetTargetLandscape();
										 Target && PickLandscapeFile(
													   false, FString::Format("Import {} weightmap", Name), "", Path))
									 {
										 FString Error;
										 const bool bOk =
											 Land->ImportWeightmap(*Target, Land->SelectedLayer, Path, Error);
										 ReportLandscapeFile(
											 bOk, FString::Format("Imported {} weights from {}", Name, Path), Error);
									 }
									 return FReply::Handled();
								 })] +
			SHorizontalBox::Slot().AutoWidth().Padding(FMargin(
				6.0f, 0.0f, 0.0f,
				0.0f))[SNew(SButton)
						   .Text("Export...")
						   .ToolTipText("The layer's weights as an 8-bit weightmap, a pixel per grid point")
						   .IsEnabled(HasLayer)
						   .OnClicked([this, Land, LayerName] {
							   FString Path;
							   const FString Name = LayerName();
							   if (const Object* Target = Land->GetTargetLandscape();
								   Target && PickLandscapeFile(true, FString::Format("Export {} weightmap", Name),
															   Name + ".png", Path))
							   {
								   FString Error;
								   const bool bOk = Land->ExportWeightmap(*Target, Land->SelectedLayer, Path, Error);
								   ReportLandscapeFile(bOk, FString::Format("Exported {} weights to {}", Name, Path),
													   Error);
							   }
							   return FReply::Handled();
						   })],
		4.0f);
	// Auto Paint: the layer where the ground's slope and height are right.
	Box->AddSlot().AutoHeight().Padding(FMargin(0.0f, 12.0f, 0.0f, 0.0f))[PanelHeader("Auto Paint")];
	Pad(SpinRow<float>(
			"Min Slope", "Degrees: the layer goes on slopes at least this steep",
			[Land] { return Land->AutoPaint.MinSlope; }, [Land](float V) { Land->AutoPaint.MinSlope = V; }, 0.0f, 90.0f,
			0),
		6.0f);
	Pad(SpinRow<float>(
		"Max Slope", "Degrees: and at most this steep", [Land] { return Land->AutoPaint.MaxSlope; },
		[Land](float V) { Land->AutoPaint.MaxSlope = V; }, 0.0f, 90.0f, 0));
	Pad(SpinRow<float>(
		"Min Height", "The lowest ground it goes on, in the landscape's units",
		[Land] { return Land->AutoPaint.MinHeight; }, [Land](float V) { Land->AutoPaint.MinHeight = V; }, -65536.0f,
		65536.0f, 0));
	Pad(SpinRow<float>(
		"Max Height", "The highest", [Land] { return Land->AutoPaint.MaxHeight; },
		[Land](float V) { Land->AutoPaint.MaxHeight = V; }, -65536.0f, 65536.0f, 0));
	Pad(SNew(SHorizontalBox) +
			SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
												   .ToolTipText("Lay the selected layer wherever the slope and height "
																"are in range, all over the landscape (one undo step)")
												   .Text([HasLayer, LayerName] {
													   return HasLayer() ? FString::Format("Auto Paint {}", LayerName())
																		 : FString("Auto Paint");
												   })
												   .IsEnabled(HasLayer)
												   .OnClicked([this, Land, LayerName] {
													   if (Object* Target = Land->GetTargetLandscape();
														   Target && Land->ApplyAutoPaint(*Target, Land->SelectedLayer))
													   {
														   Log(FString::Format("Auto painted {}", LayerName()));
													   }
													   return FReply::Handled();
												   })],
		6.0f);
	Box->AddSlot().AutoHeight().Padding(FMargin(0.0f, 12.0f, 0.0f, 0.0f))[PanelHeader("Brush")];
	Pad(SpinRow<float>(
			"Brush Size", "The brush's radius in map units", [Land] { return Land->Brush.Radius; },
			[Land](float V) { Land->Brush.Radius = V; }, FLandscapeBrush::MinRadius, FLandscapeBrush::MaxRadius, 0),
		6.0f);
	Pad(SpinRow<float>(
		"Tool Strength", "How fast the tool acts", [Land] { return Land->Brush.Strength; },
		[Land](float V) { Land->Brush.Strength = V; }, 0.01f, 1.0f, 2));
	Pad(SpinRow<float>(
		"Brush Falloff", "The outer part of the radius over which the tool fades out",
		[Land] { return Land->Brush.Falloff; }, [Land](float V) { Land->Brush.Falloff = V; }, 0.0f, 1.0f, 2));
	Pad(SNew(STextBlock)
			.TextStyle(&SmallText())
			.AutoWrapText(true)
			.Text("Left drag over a landscape in any view to use the tool; the ring shows the brush. A stroke is one "
				  "undo step. "
				  "[ and ] size the brush, Ctrl+[ and Ctrl+] its strength."),
		10.0f);
	return SNew(SScrollBox) + SScrollBox::Slot()[Box];
}

bool SLandscapeEditorPanel::PickLandscapeFile(bool bSave, const FString& Title, const FString& Name, FString& OutPath)
{
	FFileDialogOptions Options;
	Options.Title = Title;
	Options.Filters = FHeightmap::FileFilters();
	FString Folder;
	if (!FServices::GetConfig().GetString("MapEditor", "LandscapeFolder", Folder) || Folder.IsEmpty())
	{
		Folder = DefaultFolder();
	}
	Options.InitialPath =
		Name.IsEmpty() ? Folder : FString::FromWide((std::filesystem::path(Folder.ToWide()) / Name.ToWide()).wstring());
	Options.DefaultExtension = "png";
	const bool bPicked = bSave ? FServices::GetFileDialogs().SaveFile(Options, OutPath)
							   : FServices::GetFileDialogs().OpenFile(Options, OutPath);
	if (bPicked)
	{
		FServices::GetConfig().SetString(
			"MapEditor", "LandscapeFolder",
			FString::FromWide(std::filesystem::path(OutPath.ToWide()).parent_path().wstring()));
	}
	return bPicked;
}

bool SLandscapeEditorPanel::PickLandscapeTexture(FString& OutPath)
{
	FFileDialogOptions Options;
	Options.Title = "Paint layer texture";
	Options.Filters = {{"Images (*.png, *.bmp, *.jpg, *.tif)", "*.png;*.bmp;*.jpg;*.jpeg;*.tif;*.tiff"}};
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
	FServices::GetConfig().SetString(
		"MapEditor", "LandscapeTextureFolder",
		FString::FromWide(std::filesystem::path(OutPath.ToWide()).parent_path().wstring()));
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

void SLandscapeEditorPanel::RefreshLandscapeLayersNow()
{
	LandscapeLayerSignature = "(changed)";
	RefreshLandscapeLayers();
}

void SLandscapeEditorPanel::ReportLandscapeFile(bool bOk, const FString& Done, const FString& Error)
{
	if (bOk)
	{
		Log(Done);
		return;
	}
	Log(Error);
	FServices::GetMessageDialog().Show(EMessageType::Error, EMessageButtons::Ok, Error);
}

void SLandscapeEditorPanel::RefreshLandscapeLayers()
{
	const TSharedPtr<SBox> LayerHost = LandscapeLayerHost.lock();
	if (!LayerHost || bLandscapeLayersQueued)
	{
		return;
	}
	// Only when the list would change: painting changes weights, not layers.
	FString Signature;
	if (const Object* Target = Mode->GetTargetLandscape())
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
	RegisterActiveTimer(0.0f, [this, WeakHost = LandscapeLayerHost](double, float) {
		bLandscapeLayersQueued = false;
		if (const TSharedPtr<SBox> Pinned = WeakHost.lock())
		{
			Pinned->SetContent(MakeLandscapeLayerList());
		}
		// The splines' layer choice lists the layers too.
		if (const TSharedPtr<SBox> Pinned = LandscapeSplineHost.lock())
		{
			Pinned->SetContent(MakeLandscapeSplineSettings());
		}
		return EActiveTimerReturn::Stop;
	});
}

TSharedRef<SWidget> SLandscapeEditorPanel::MakeLandscapeSplineSettings()
{
	FMapLandscapeMode* Land = Mode;
	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	const auto Row = [&Box](const TSharedRef<SWidget>& Widget, float Top = 4.0f) {
		Box->AddSlot().AutoHeight().Padding(FMargin(0.0f, Top, 0.0f, 0.0f))[Widget];
	};
	const auto Selected = [Land] { return Land->HasSelectedSplinePoint(); };
	Row(SNew(STextBlock).TextStyle(&SmallText()).AutoWrapText(true).Text([Land] {
		const LandscapeSpline* Spline = Land->GetSelectedSpline();
		if (!Spline || !Land->HasSelectedSplinePoint())
		{
			return FString("Ctrl+click a landscape to start a spline; click a point to pick it.");
		}
		const int Joint = Spline->points[static_cast<SIZE_T>(Land->SelectedPoint)].joint;
		return FString::Format("Spline {}, point {} of {}{}.", Land->SelectedSpline + 1, Land->SelectedPoint + 1,
							   Spline->points.size(),
							   Joint >= 0 ? FString::Format(", in junction {}", Joint + 1) : FString());
	}),
		0.0f);
	// Reading and changing the selected point and its spline.
	const auto Point = [Land]() -> LandscapeSplinePoint {
		return Land->HasSelectedSplinePoint()
				   ? Land->GetSelectedSpline()->points[static_cast<SIZE_T>(Land->SelectedPoint)]
				   : LandscapeSplinePoint();
	};
	const auto EditSpline = [Land](const TFunction<void(LandscapeSpline&)>& Change, const FString& What) {
		const LandscapeSpline* Spline = Land->GetSelectedSpline();
		if (!Spline)
		{
			return;
		}
		LandscapeSpline Changed = *Spline;
		Change(Changed);
		Land->SetSpline(*Land->SplineLand, Land->SelectedSpline, Changed, What);
	};
	const auto PointSpin = [this, Point, EditSpline, Land, Selected](const char* Label, const char* Tip,
																	 float LandscapeSplinePoint::* Field) {
		return SNew(SHorizontalBox).ToolTipText(Tip).IsEnabled(Selected) +
			   SHorizontalBox::Slot().FillWidth(0.5f).VAlign(VAlign_Center)[SNew(STextBlock).Text(Label)] +
			   SHorizontalBox::Slot().FillWidth(
				   0.5f)[SNew(SSpinBox<float>)
							 .MinValue(0.0f)
							 .MaxValue(65536.0f)
							 .FractionalDigits(0)
							 .Value([Point, Field] { return TOptional<float>(Point().*Field); })
							 .OnBeginSliderMovement([this] { BeginInteraction("Change Spline"); })
							 .OnEndSliderMovement([this](float) { EndInteraction(); })
							 .OnValueChanged([EditSpline, Land, Field](float V) {
								 const int32 Index = Land->SelectedPoint;
								 EditSpline(
									 [Index, Field, V](LandscapeSpline& Spline) {
										 Spline.points[static_cast<SIZE_T>(Index)].*Field = V;
									 },
									 "Change Spline");
							 })];
	};
	Row(PointSpin("Width", "How wide the spline is flat at this point", &LandscapeSplinePoint::width), 6.0f);
	Row(PointSpin("Side Falloff", "How far beyond its width the spline fades into the ground at this point",
				  &LandscapeSplinePoint::falloff));
	Row(SNew(SCheckBox)
			.Text(FString("Auto Tangent"))
			.IsEnabled(Selected)
			.ToolTipText("The curve's direction here follows its neighbours; drag a tangent handle to set it by hand")
			.IsChecked([Point] { return Point().autoTangent ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged([Land](ECheckBoxState State) {
				if (!Land->HasSelectedSplinePoint())
				{
					return;
				}
				if (State == ECheckBoxState::Checked)
				{
					Land->SetSplineAutoTangent(*Land->SplineLand, Land->SelectedSpline, Land->SelectedPoint);
					return;
				}
				// By hand, from the tangent it has now.
				Land->SetSplineTangent(*Land->SplineLand, Land->SelectedSpline, Land->SelectedPoint,
									   Land->GetSelectedSpline()->GetTangent(Land->SelectedPoint));
			}),
		6.0f);
	const auto SplineCheck = [Land, EditSpline, Selected](const char* Label, const char* Tip,
														  bool LandscapeSpline::* Field) {
		return SNew(SCheckBox)
			.Text(FString(Label))
			.ToolTipText(Tip)
			.IsEnabled(Selected)
			.IsChecked([Land, Field] {
				const LandscapeSpline* S = Land->GetSelectedSpline();
				return S && S->*Field ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged([EditSpline, Field](ECheckBoxState State) {
				EditSpline(
					[Field, State](LandscapeSpline& Spline) { Spline.*Field = State == ECheckBoxState::Checked; },
					"Change Spline");
			});
	};
	Row(SplineCheck("Raise Terrain", "Applying lifts the ground below the spline", &LandscapeSpline::raiseTerrain),
		6.0f);
	Row(SplineCheck("Lower Terrain", "Applying cuts the ground above the spline", &LandscapeSpline::lowerTerrain));
	Row(SplineCheck("Closed Loop", "The last point joins the first", &LandscapeSpline::closed));
	// Its mesh (Unreal's spline mesh): bent along it.
	const auto Meshes = std::make_shared<TArray<FString>>();
	Meshes->push_back("None");
	for (const std::vector<std::string>* Names :
		 {&StaticMesh::GetSplineMeshNames(), &StaticMesh::GetFoliageMeshNames(), &StaticMesh::GetBasicShapeNames()})
	{
		for (const std::string& Name : *Names)
		{
			Meshes->push_back(FString(Name));
		}
	}
	Row(SNew(SHorizontalBox)
				.ToolTipText("A mesh repeated along the spline and bent to it (Road, Fence, Wall, River, or any mesh)")
				.IsEnabled(Selected) +
			SHorizontalBox::Slot().FillWidth(0.5f).VAlign(VAlign_Center)[SNew(STextBlock).Text("Mesh")] +
			SHorizontalBox::Slot().FillWidth(0.5f)[SNew(SComboBox<FString>)
													   .OptionsSource(Meshes.get())
													   .OnGenerateWidget([Meshes](FString Name) -> TSharedRef<SWidget> {
														   return SNew(STextBlock).Text(Name);
													   })
													   .OnSelectionChanged([EditSpline](FString Name, ESelectInfo) {
														   EditSpline(
															   [Name](LandscapeSpline& Spline) {
																   Spline.mesh =
																	   Name == "None" ? std::string() : Name.ToAnsi();
															   },
															   "Change Spline Mesh");
													   })[SNew(STextBlock).Text([Land] {
														   const LandscapeSpline* S = Land->GetSelectedSpline();
														   return S && !S->mesh.empty() ? FString(S->mesh)
																						: FString("None");
													   })]],
		6.0f);
	const auto SplineSpin = [this, Land, EditSpline, Selected](const char* Label, const char* Tip,
															   float LandscapeSpline::* Field, float Min, float Max,
															   int32 Digits) {
		return SNew(SHorizontalBox).ToolTipText(Tip).IsEnabled(Selected) +
			   SHorizontalBox::Slot().FillWidth(0.5f).VAlign(VAlign_Center)[SNew(STextBlock).Text(Label)] +
			   SHorizontalBox::Slot().FillWidth(
				   0.5f)[SNew(SSpinBox<float>)
							 .MinValue(Min)
							 .MaxValue(Max)
							 .FractionalDigits(Digits)
							 .Value([Land, Field] {
								 const LandscapeSpline* S = Land->GetSelectedSpline();
								 return TOptional<float>(S ? S->*Field : 0.0f);
							 })
							 .OnBeginSliderMovement([this] { BeginInteraction("Change Spline Mesh"); })
							 .OnEndSliderMovement([this](float) { EndInteraction(); })
							 .OnValueChanged([EditSpline, Field](float V) {
								 EditSpline([Field, V](LandscapeSpline& Spline) { Spline.*Field = V; },
											"Change Spline Mesh");
							 })];
	};
	Row(SplineSpin("Mesh Scale", "The mesh's size (and how long a piece is before it repeats)",
				   &LandscapeSpline::meshScale, 0.05f, 20.0f, 2));
	Row(SplineCheck("Fit Mesh to Width", "Stretch the mesh across to the spline's width",
					&LandscapeSpline::meshFitWidth));
	Row(SplineSpin("Mesh Z Offset", "Up from the curve", &LandscapeSpline::meshZOffset, -1000.0f, 1000.0f, 0));
	// The layer it paints: one of the landscape's, or none.
	const auto Layers = std::make_shared<TArray<FString>>();
	Layers->push_back("None");
	if (const Object* Target = Land->GetTargetLandscape())
	{
		for (const LandscapeLayer& Layer : Target->GetLandscape().GetLayers())
		{
			Layers->push_back(FString(Layer.name));
		}
	}
	Row(SNew(SHorizontalBox).ToolTipText("The paint layer applying paints along the spline").IsEnabled(Selected) +
			SHorizontalBox::Slot().FillWidth(0.5f).VAlign(VAlign_Center)[SNew(STextBlock).Text("Layer")] +
			SHorizontalBox::Slot().FillWidth(0.5f)[SNew(SComboBox<FString>)
													   .OptionsSource(Layers.get())
													   .OnGenerateWidget([Layers](FString Name) -> TSharedRef<SWidget> {
														   return SNew(STextBlock).Text(Name);
													   })
													   .OnSelectionChanged([EditSpline](FString Name, ESelectInfo) {
														   EditSpline(
															   [Name](LandscapeSpline& Spline) {
																   Spline.layer =
																	   Name == "None" ? std::string() : Name.ToAnsi();
															   },
															   "Change Spline");
													   })[SNew(STextBlock).Text([Land] {
														   const LandscapeSpline* S = Land->GetSelectedSpline();
														   return S && !S->layer.empty() ? FString(S->layer)
																						 : FString("None");
													   })]],
		6.0f);
	Row(SNew(SHorizontalBox) +
			SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
												   .Text("Delete Point")
												   .ToolTipText("Remove the selected point (Delete)")
												   .IsEnabled(Selected)
												   .OnClicked([Land] {
													   Land->DeleteSelectedSplinePoint();
													   return FReply::Handled();
												   })] +
			SHorizontalBox::Slot().AutoWidth().Padding(
				FMargin(6.0f, 0.0f, 0.0f, 0.0f))[SNew(SButton)
													 .Text("Delete Spline")
													 .ToolTipText("Remove the selected point's whole spline")
													 .IsEnabled(Selected)
													 .OnClicked([Land] {
														 Land->DeleteSelectedSpline();
														 return FReply::Handled();
													 })],
		8.0f);
	Row(SNew(SHorizontalBox) +
			SHorizontalBox::Slot()
				.AutoWidth()[SNew(SButton)
								 .Text("Break Junction")
								 .ToolTipText("The selected point no longer moves with the others of its junction")
								 .IsEnabled([Point, Selected] { return Selected() && Point().joint >= 0; })
								 .OnClicked([Land] {
									 Land->BreakSelectedJunction();
									 return FReply::Handled();
								 })],
		4.0f);
	Row(SNew(SHorizontalBox) +
			SHorizontalBox::Slot().AutoWidth()
				[SNew(SButton)
					 .Text("Apply Splines to Landscape")
					 .ToolTipText(
						 "Shape the ground to every spline of the landscape and paint their layers (one undo step)")
					 .IsEnabled([Land] {
						 const Object* Target = Land->GetTargetLandscape();
						 return Target && !Target->GetLandscape().GetSplines().empty();
					 })
					 .OnClicked([this, Land] {
						 if (Object* Target = Land->GetTargetLandscape(); Target && Land->ApplySplines(*Target))
						 {
							 Log(FString::Format("Applied {} splines to the landscape",
												 Target->GetLandscape().GetSplines().size()));
						 }
						 return FReply::Handled();
					 })],
		6.0f);
	return Box;
}

TSharedRef<SWidget> SLandscapeEditorPanel::MakeLandscapeLayerList()
{
	FMapLandscapeMode* Land = Mode;
	Object* Target = Land->GetTargetLandscape();
	if (!Target)
	{
		return SNew(STextBlock)
			.TextStyle(&SmallText())
			.AutoWrapText(true)
			.Text("Create a landscape (or select one) to paint it.");
	}
	const TArray<FString> Names = [Target] {
		TArray<FString> Out;
		for (const LandscapeLayer& Layer : Target->GetLandscape().GetLayers())
		{
			Out.push_back(FString(Layer.name));
		}
		return Out;
	}();
	if (Names.empty())
	{
		return SNew(STextBlock)
			.TextStyle(&SmallText())
			.AutoWrapText(true)
			.Text("No paint layers yet: Add Layer makes one (Grass first).");
	}
	// Each row: the layer's color (click to change it), its name, Paint to
	// paint with it, and x to remove it.
	const auto ColorOf = [Land](int32 Index) {
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
		List->AddSlot().AutoHeight().Padding(FMargin(
			0.0f, 0.0f, 0.0f,
			4.0f))[SNew(SHorizontalBox) +
				   SHorizontalBox::Slot()
					   .AutoWidth()
					   .VAlign(VAlign_Center)
					   .Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
						   [SNew(SComboButton)
								.HasDownArrow(false)
								.IsFocusable(false)
								.ToolTipText("The layer's color")
								.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("FlatButton"))
								.ButtonContent()[SNew(SColorBlock).Size(FVector2(24.0f, 16.0f)).Color([ColorOf, Index] {
									return ColorOf(Index);
								})]
								.OnGetMenuContent([this, Land, ColorOf, Index]() -> TSharedRef<SWidget> {
									return SNew(SBox).WidthOverride(280.0f).Padding(
										FMargin(8.0f))[SNew(SColorPicker)
														   .UseAlpha(false)
														   .TargetColor([ColorOf, Index] { return ColorOf(Index); })
														   .OnInteractionBegin(
															   [this] { BeginInteraction("Change Paint Layer Color"); })
														   .OnInteractionEnd([this] { EndInteraction(); })
														   .OnColorChanged([Land, Index](FColor Color) {
															   if (Object* Current = Land->GetTargetLandscape())
															   {
																   Land->SetLayerColor(*Current, Index, Color);
															   }
														   })];
								})] +
				   SHorizontalBox::Slot()
					   .FillWidth(1.0f)
					   .VAlign(VAlign_Center)
					   .Padding(FMargin(0.0f, 0.0f, 6.0f,
										0.0f))[SNew(SEditableTextBox)
												   .Text(Names[static_cast<SIZE_T>(Index)])
												   .ToolTipText("The layer's name")
												   .OnTextCommitted([Land, Index](const FString& Name, ETextCommit) {
													   if (Object* Current = Land->GetTargetLandscape();
														   Current && !Name.TrimStartAndEnd().IsEmpty())
													   {
														   Land->RenameLayer(*Current, Index, Name.TrimStartAndEnd());
													   }
												   })] +
				   SHorizontalBox::Slot()
					   .AutoWidth()
					   .VAlign(VAlign_Center)
					   .Padding(FMargin(0.0f, 0.0f, 4.0f,
										0.0f))[SNew(SChoiceButton, FString("Paint"), FString("Paint this layer"))
												   .IsChosen([Land, Index] {
													   return Land->Tool == ELandscapeTool::Paint &&
															  Land->SelectedLayer == Index;
												   })
												   .OnClicked([this, Land, Index] {
													   Land->SelectedLayer = Index;
													   Land->Tool = ELandscapeTool::Paint;
													   InvalidateViews();
												   })] +
				   SHorizontalBox::Slot().AutoWidth().VAlign(
					   VAlign_Center)[SNew(SButton)
										  .Text("x")
										  .IsFocusable(false)
										  .ToolTipText("Remove this layer (its paint goes with it)")
										  .OnClicked([Land, Index] {
											  if (Object* Current = Land->GetTargetLandscape())
											  {
												  Land->RemoveLayer(*Current, Index);
											  }
											  return FReply::Handled();
										  })]];
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
			[SNew(SHorizontalBox) +
			 SHorizontalBox::Slot()
				 .FillWidth(1.0f)
				 .VAlign(VAlign_Center)
				 .Padding(FMargin(0.0f, 0.0f, 6.0f,
								  0.0f))[SNew(SComboBox<FString>)
											 .OptionsSource(Choices.get())
											 .InitiallySelectedItem(TOptional<FString>(Shown))
											 .ToolTipText("The layer's texture: one of the built-in ones (tinted with "
														  "its color), an image file, or none (its plain color)")
											 .OnGenerateWidget([](FString Choice) -> TSharedRef<SWidget> {
												 return SNew(STextBlock).Text(Choice);
											 })
											 .OnSelectionChanged([this, Land, Index, Choices, Current,
																  bFile](FString Choice, ESelectInfo) {
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
												 else if (bFile &&
														  Choice == FLandscapeTextureLibrary::DisplayName(Current))
												 {
													 return;
												 }
												 else if (Choice != "None")
												 {
													 Texture = FLandscapeTextureLibrary::BuiltIn(Choice);
												 }
												 if (Land->SetLayerTexture(*Layered, Index, Texture))
												 {
													 Log(FString::Format("Paint layer texture: {}",
																		 Texture.IsEmpty() ? FString("none")
																						   : Texture));
												 }
											 })[SNew(STextBlock).Text(Shown)]] +
			 SHorizontalBox::Slot()
				 .AutoWidth()
				 .VAlign(VAlign_Center)
				 .Padding(FMargin(0.0f, 0.0f, 4.0f, 0.0f))[SNew(STextBlock).TextStyle(&SmallText()).Text("Tiling")] +
			 SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(
				 64.0f)[SNew(SSpinBox<float>)
							.MinValue(1.0f)
							.MaxValue(65536.0f)
							.FractionalDigits(0)
							.ToolTipText("Map units the texture repeats over")
							.Value([Land, Index]() -> TOptional<float> {
								const Object* Layered = Land->GetTargetLandscape();
								if (!Layered || Index >= Layered->GetLandscape().GetNumLayers())
								{
									return {};
								}
								return Layered->GetLandscape().GetLayers()[static_cast<SIZE_T>(Index)].tiling;
							})
							.OnBeginSliderMovement([this] { BeginInteraction("Change Paint Layer Tiling"); })
							.OnEndSliderMovement([this](float) { EndInteraction(); })
							.OnValueChanged([Land, Index](float Tiling) {
								if (Object* Layered = Land->GetTargetLandscape())
								{
									Land->SetLayerTiling(*Layered, Index, Tiling);
								}
							})]]];
	}
	return List;
}
