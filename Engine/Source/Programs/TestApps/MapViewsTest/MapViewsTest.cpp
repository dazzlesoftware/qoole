// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// MapViewsTest: the map views and their grid. Four FMapViewportClient views
// of one map (top, front, side and 3D) drawn through FMapDraw: the grid
// (size, visibility), wireframes, solid faces with a depth buffer in the 3D
// view, the selection shown in every view, the cursor position in map
// units, zoom, pan and framing.
//
// Self-test: builds a scene from prefabs, zooms and pans the 2D views,
// changes the grid size and hides it, switches the 3D view between solid
// and wireframe, selects with a click and frames the selection, and changes
// the DPI.

#include "MapTestApp.h"
#include "TestWidgets.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SViewport.h"
#include "Widgets/Text/STextBlock.h"

class FMapViewsTestApp final : public FMapTestApp
{
public:
	FMapViewsTestApp() : FMapTestApp({ "MapViewsTest", "Map editor - Views and grid", FVector2(1200.0f, 800.0f) }) {}

protected:
	TSharedRef<SWidget> MakeContent() override
	{
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Views4[Index] = CreateView(static_cast<EMapViewType>(Index)).ToSharedPtr();
		}
		Views4[3]->SetRenderMode(EMapRenderMode::Solid);

		const auto Pane = [this](int32 Index) -> TSharedRef<SWidget>
		{
			const TSharedRef<FMapViewportClient> View = ToSharedRef(Views4[Index]);
			return SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Recessed")).Padding(1.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(6.0f, 2.0f))[SNew(STextBlock).TextStyle(&TestTextStyle("SmallText")).Text([View]
				{
					return FString::Format("{}{}", ToString(View->GetType()), View->IsPerspective() ? (View->GetRenderMode() == EMapRenderMode::Solid ? " · solid" : " · wireframe") : "");
				})]
				+ SVerticalBox::Slot().FillHeight(1.0f)[MakeViewport(View)]
			];
		};
		const TSharedRef<SGridPanel> Grid = SNew(SGridPanel);
		Grid->SetColumnFill(0, 1.0f);
		Grid->SetColumnFill(1, 1.0f);
		Grid->SetRowFill(0, 1.0f);
		Grid->SetRowFill(1, 1.0f);
		Grid->AddSlot(0, 0).Padding(2.0f)[Pane(0)];
		Grid->AddSlot(1, 0).Padding(2.0f)[Pane(3)];
		Grid->AddSlot(0, 1).Padding(2.0f)[Pane(1)];
		Grid->AddSlot(1, 1).Padding(2.0f)[Pane(2)];

		const auto Toggle = [](const char* Label, TFunction<bool()> Get, TFunction<void()> Flip) -> TSharedRef<SWidget>
		{
			return SNew(SCheckBox).Text(Label).IsChecked([Get] { return Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }).OnCheckStateChanged([Flip](ECheckBoxState) { Flip(); });
		};
		return TestFrame(*this, "Views and grid",
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(10.0f, 6.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))[SNew(STextBlock).Text("Grid size")]
				+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 16.0f, 0.0f))
				[
					SNew(SBox).WidthOverride(TOptional<float>(90.0f))
					[
						SNew(SSpinBox<float>).FractionalDigits(0).MinValue(FMapViewSettings::MinGridSize).MaxValue(FMapViewSettings::MaxGridSize)
						.Value([this] { return TOptional<float>(ViewSettings.GridSize); })
						.OnValueChanged([this](float Value) { SetGridSize(Value); })
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 16.0f, 0.0f))[Toggle("Show grid", [this] { return ViewSettings.bShowGrid; }, [this] { ViewSettings.bShowGrid = !ViewSettings.bShowGrid; ViewSettings.Changed(); })]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 16.0f, 0.0f))[Toggle("Snap", [this] { return ViewSettings.bSnapToGrid; }, [this] { ViewSettings.bSnapToGrid = !ViewSettings.bSnapToGrid; ViewSettings.Changed(); })]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 16.0f, 0.0f))[Toggle("Solid 3D", [this] { return Views4[3]->GetRenderMode() == EMapRenderMode::Solid; }, [this] { ToggleSolid(); })]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text("Frame").ToolTipText("Frame the selection, or the whole map, in every view").OnClicked([this] { FrameAll(); return FReply::Handled(); })]
				+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right).VAlign(VAlign_Center)[SNew(STextBlock).TextStyle(&TestTextStyle("SmallText")).Text([this]
				{
					return FString::Format("{} brushes · {} entities · {} selected · cursor {}", GetDocument().CountBrushes(), GetDocument().CountEntities(),
						GetDocument().GetSelection().size(), CursorText.IsEmpty() ? FString("-") : CursorText);
				})]
			]
			+ SVerticalBox::Slot().FillHeight(1.0f).Padding(FMargin(8.0f, 0.0f, 8.0f, 8.0f))[Grid]);
	}

	void OnShutdown() override
	{
		for (TSharedPtr<FMapViewportClient>& View : Views4)
		{
			View.reset();
		}
		FMapTestApp::OnShutdown();
	}

	void BuildSelfTest(FTestScript& Script) override
	{
		Script.Capture("empty")
			.Do([this] { BuildScene(); })
			.Check("the scene has the prefabs and entities", [this] { return GetDocument().CountBrushes() >= 5 && GetDocument().CountEntities() >= 2; })
			.Capture("scene", 20)
			// The cursor position in map units.
			.Do([this] { MoveMouse(MapToWindow(*Views4[0], Vector3d(64.0f, 32.0f, 0.0f))); })
			.Check("a 2D view reports the cursor in map units", [this] { return !CursorText.IsEmpty(); }, 5)
			// Zoom the top view at the cursor, pan the front view.
			.Do([this]
			{
				ZoomBefore = Views4[0]->GetZoom();
				Wheel(GetCenter(*Views4[0]->GetViewport()), 3.0f);
			})
			.Check("the wheel zooms the top view", [this] { return Views4[0]->GetZoom() > ZoomBefore; })
			.Do([this]
			{
				OriginBefore = MapToWindow(*Views4[1], Vector3d(0.0f, 0.0f, 0.0f));
				const FVector2 Center = GetCenter(*Views4[1]->GetViewport());
				Drag(Center, Center + FVector2(80.0f, 40.0f), EMouseButton::Middle);
			})
			.Check("a middle drag pans the front view", [this] { return MapToWindow(*Views4[1], Vector3d(0.0f, 0.0f, 0.0f)).X > OriginBefore.X + 40.0f; })
			.Capture("zoomed_panned")
			// The grid.
			.Do([this] { SetGridSize(64.0f); })
			.Check("the grid size is shared by the views", [this] { return ViewSettings.GridSize == 64.0f; })
			.Capture("grid64")
			.Do([this] { PointAtText("Show grid", true); })
			.Check("the grid can be hidden", [this] { return !ViewSettings.bShowGrid; })
			.Capture("no_grid")
			.Do([this] { PointAtText("Show grid", true); SetGridSize(16.0f); })
			// The 3D view.
			.Do([this] { ToggleSolid(); })
			.Check("the 3D view draws wireframes", [this] { return Views4[3]->GetRenderMode() == EMapRenderMode::Wireframe; })
			.Capture("wireframe_3d")
			.Do([this] { ToggleSolid(); })
			// Select the cube with a click in the top view: every view shows it.
			.Do([this] { FrameAll(); })
			.Do([this] { Click(MapToWindow(*Views4[0], Vector3d(-160.0f, -96.0f, 32.0f))); })
			.Check("a click in the top view selects the cube", [this] { return GetDocument().GetSelection().size() == 1 && GetDocument().IsSelected(Cube); })
			.Capture("selected")
			.Do([this]
			{
				ZoomBefore = Views4[0]->GetZoom();
				FrameAll();
			})
			.Check("framing the selection zooms in on it", [this] { return Views4[0]->GetZoom() > ZoomBefore; })
			.Capture("framed")
			// DPI.
			.Do([this] { SimulateDpiChange(1.0f); })
			.Capture("dpi100", 30)
			.Do([this] { SimulateDpiChange(2.0f); })
			.Capture("dpi200", 30)
			.Do([this] { SimulateDpiChange(GetNativeDpiScale()); });
	}

private:
	void SetGridSize(float Size)
	{
		ViewSettings.GridSize = FMath::Clamp(Size, FMapViewSettings::MinGridSize, FMapViewSettings::MaxGridSize);
		ViewSettings.Changed();
		SetLastEvent(FString::Format("Grid {:.0f}", ViewSettings.GridSize));
	}

	void ToggleSolid()
	{
		Views4[3]->SetRenderMode(Views4[3]->GetRenderMode() == EMapRenderMode::Solid ? EMapRenderMode::Wireframe : EMapRenderMode::Solid);
	}

	void FrameAll()
	{
		for (const TSharedPtr<FMapViewportClient>& View : Views4)
		{
			View->FrameSelection();
		}
	}

	TSharedPtr<FMapViewportClient> Views4[4];
	float ZoomBefore = 0.0f;
	FVector2 OriginBefore;
};

IMPLEMENT_TEST_APP(FMapViewsTestApp)
