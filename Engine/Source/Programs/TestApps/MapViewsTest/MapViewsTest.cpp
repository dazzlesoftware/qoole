// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// MapViewsTest: the map viewports and their grid. Four FMapViewportClient
// views of one map in an SMapViewportLayout (top, perspective, front and
// right, each with its toolbar, as in Unreal) drawn through FMapDraw: the
// grid (size, visibility), wireframes, Lit and Unlit faces with a depth
// buffer in the perspective view, the selection shown in every view, the
// cursor position in map units, zoom, pan and framing; viewport types, view
// modes, show flags, layouts and maximizing.
//
// Self-test: builds a scene from prefabs, zooms and pans the 2D views,
// changes the grid size and hides it, switches the perspective view between
// Lit, Unlit and wireframe, turns a pane into the other orthographic views
// with Alt shortcuts, hides entities and the grid with show flags, changes
// the layout and maximizes a pane, selects with a click and frames the
// selection, and changes the DPI.

#include "MapTestApp.h"
#include "Containers/ContainerAliases.h"
#include "Templates/SharedPointer.h"
#include "SMapViewport.h"
#include "TestWidgets.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/SViewport.h"
#include "Widgets/Text/STextBlock.h"

class FMapViewsTestApp final : public FMapTestApp
{
public:
	FMapViewsTestApp() : FMapTestApp({"MapViewsTest", "Map editor - Viewports and grid", FVector2(1280.0f, 860.0f)}) {}

protected:
	TSharedRef<SWidget> MakeContent() override
	{
		// In pane order: the 2x2 layout's top left, top right, bottom left, bottom right.
		for (const EMapViewType Type :
		    {EMapViewType::Top, EMapViewType::Perspective, EMapViewType::Front, EMapViewType::Right})
		{
			Panes.push_back(CreateView(Type).ToSharedPtr());
		}

		const TFunction<TSharedRef<SWidget>(const char*, TFunction<bool()>, TFunction<void()>)> Toggle =
		    [](const char* Label, TFunction<bool()> Get, TFunction<void()> Flip) -> TSharedRef<SWidget>
		{
			return SNew(SCheckBox)
			    .Text(Label)
			    .IsChecked([Get] {
				return Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}).OnCheckStateChanged([Flip](ECheckBoxState) { Flip(); });
		};
		return TestFrame(*this, "Viewports and grid",
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
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 16.0f, 0.0f))[Toggle("Solid 3D", [this] { return Perspective().GetViewMode() != EMapViewMode::Wireframe; }, [this] { ToggleSolid(); })]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text("Frame").ToolTipText("Frame the selection, or the whole map, in every view").OnClicked([this] { FrameAll(); return FReply::Handled(); })]
				+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right).VAlign(VAlign_Center)[SNew(STextBlock).TextStyle(&TestTextStyle("SmallText")).Text([this]
				{
					return FString::Format("{} brushes · {} entities · {} selected · cursor {} · {}", GetDocument().CountBrushes(), GetDocument().CountEntities(),
						GetDocument().GetSelection().size(), CursorText.IsEmpty() ? FString("-") : CursorText, Layout ? ToString(Layout->GetLayout()) : "");
				})]
			]
			+ SVerticalBox::Slot().FillHeight(1.0f).Padding(FMargin(8.0f, 0.0f, 8.0f, 8.0f))
			[
				SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Recessed")).Padding(1.0f)
				[
					SAssignNew(Layout, SMapViewportLayout, Panes, &ViewSettings).Layout(EMapViewportLayout::FourPanes2x2)
				]
			]);
	}

	void OnShutdown() override
	{
		Layout.reset();
		Panes.clear();
		FMapTestApp::OnShutdown();
	}

	void BuildSelfTest(FTestScript& Script) override
	{
		const FMapViewportCommands& C = FMapViewportCommands::Get();
		Script.Capture("empty")
		    .Check("four panes, each with a toolbar",
		        [this] { return Layout->GetNumVisiblePanes() == 4 && Layout->IsPaneVisible(3); })
		    .Do([this] { BuildScene(); })
		    .Check("the scene has the prefabs and entities",
		        [this] { return GetDocument().CountBrushes() >= 5 && GetDocument().CountEntities() >= 2; })
		    .Capture("scene", 20)
		    // The cursor position in map units.
		    .Do([this] { MoveMouse(MapToWindow(Top(), Vector3d(64.0f, 32.0f, 0.0f))); })
		    .Check("a 2D view reports the cursor in map units", [this] { return !CursorText.IsEmpty(); }, 5)
		    // Zoom the top view at the cursor, pan the front view.
		    .Do(
		        [this]
		{
			ZoomBefore = Top().GetZoom();
			Wheel(GetCenter(*Top().GetViewport()), 3.0f);
		})
		    .Check("the wheel zooms the top view", [this] { return Top().GetZoom() > ZoomBefore; })
		    .Do(
		        [this]
		{
			OriginBefore = MapToWindow(Front(), Vector3d(0.0f, 0.0f, 0.0f));
			const FVector2 Center = GetCenter(*Front().GetViewport());
			Drag(Center, Center + FVector2(80.0f, 40.0f), EMouseButton::Middle);
		})
		    .Check("a middle drag pans the front view",
		        [this] { return MapToWindow(Front(), Vector3d(0.0f, 0.0f, 0.0f)).X > OriginBefore.X + 40.0f; })
		    .Capture("zoomed_panned")
		    // The grid.
		    .Do([this] { SetGridSize(64.0f); })
		    .Check("the grid size is shared by the views", [this] { return ViewSettings.GridSize == 64.0f; })
		    .Capture("grid64")
		    .Do([this] { PointAtText("Show grid", true); })
		    .Check("the grid can be hidden", [this] { return !ViewSettings.bShowGrid; })
		    .Capture("no_grid")
		    .Do(
		        [this]
		{
			PointAtText("Show grid", true);
			SetGridSize(16.0f);
		})
		    // View modes of the perspective view, by its commands.
		    .Do([this, &C] { Layout->GetPane(1)->GetCommandList()->ExecuteAction(*C.UnlitMode); })
		    .Check("the perspective view draws unlit",
		        [this] { return Perspective().GetViewMode() == EMapViewMode::Unlit; })
		    .Capture("unlit_3d")
		    .Do([this] { ToggleSolid(); })
		    .Check("the perspective view draws wireframes",
		        [this] { return Perspective().GetViewMode() == EMapViewMode::Wireframe; })
		    .Capture("wireframe_3d")
		    .Do([this] { ToggleSolid(); })
		    .Check("and Lit again", [this] { return Perspective().GetViewMode() == EMapViewMode::Lit; })
		    .Check("an orthographic view cannot be lit",
		        [this, &C] { return !Layout->GetPane(0)->GetCommandList()->CanExecuteAction(*C.LitMode); })
		    // Viewport types with Unreal's shortcuts, in the focused pane.
		    .Do(
		        [this]
		{
			FSlateApplication::Get().SetKeyboardFocus(Layout->GetPane(3)->GetViewportWidget());
			SetModifierKeys(false, false, true);
			PressKey(EKey::H);
			SetModifierKeys(false, false, false);
		})
		    .Check("Alt+H turns a pane into a front view",
		        [this] { return Pane(3).GetViewportType() == EMapViewType::Front; })
		    .Do(
		        [this]
		{
			SetModifierKeys(true, false, true);
			PressKey(EKey::J);
			SetModifierKeys(false, false, false);
		})
		    .Check("Alt+Shift+J turns it into a bottom view",
		        [this] { return Pane(3).GetViewportType() == EMapViewType::Bottom && Pane(3).IsOrtho(); })
		    .Capture("bottom")
		    .Do([this, &C]
		{ Layout->GetPane(3)->GetCommandList()->ExecuteAction(*C.ViewportType(EMapViewType::Perspective)); })
		    .Check("a pane can become a second perspective view", [this] { return Pane(3).IsPerspective(); })
		    .Capture("two_perspectives")
		    .Do([this, &C]
		{ Layout->GetPane(3)->GetCommandList()->ExecuteAction(*C.ViewportType(EMapViewType::Right)); })
		    // Show flags.
		    .Do(
		        [this]
		{
			FMapShowFlags Flags = Perspective().GetShowFlags();
			Flags.bEntities = false;
			Flags.bGrid = false;
			Perspective().SetShowFlags(Flags);
		})
		    .Check("a view can hide its entities and grid",
		        [this] { return !Perspective().GetShowFlags().bEntities && !Perspective().GetShowFlags().bGrid; })
		    .Capture("show_flags")
		    .Do([this] { Perspective().SetShowFlags(FMapShowFlags()); })
		    // Select the cube with a click in the top view: every view shows it.
		    .Do([this] { FrameAll(); })
		    .Do([this] { Click(MapToWindow(Top(), Vector3d(-160.0f, -96.0f, 32.0f))); })
		    .Check("a click in the top view selects the cube",
		        [this] { return GetDocument().GetSelection().size() == 1 && GetDocument().IsSelected(Cube); })
		    .Check("the pane clicked is the active one", [this] { return Layout->GetActivePane() == 0; })
		    .Capture("selected")
		    .Do(
		        [this]
		{
			ZoomBefore = Top().GetZoom();
			FrameAll();
		})
		    .Check("framing the selection zooms in on it", [this] { return Top().GetZoom() > ZoomBefore; })
		    .Capture("framed")
		    // Layouts and maximizing.
		    .Do([this, &C] { Layout->GetPane(1)->GetCommandList()->ExecuteAction(*C.ToggleMaximize); })
		    .Check("a pane can be maximized", [this]
		{ return Layout->GetNumVisiblePanes() == 1 && Layout->IsPaneVisible(1) && !Layout->IsPaneVisible(0); }, 3)
		    .Capture("maximized")
		    .Do([this, &C] { Layout->GetPane(1)->GetCommandList()->ExecuteAction(*C.ToggleMaximize); })
		    .Check("and restored", [this] { return Layout->GetNumVisiblePanes() == 4 && Layout->IsPaneVisible(0); }, 3)
		    .Do([this] { Layout->SetLayout(EMapViewportLayout::ThreePanesLeft); })
		    .Check("three panes, the large one on the left",
		        [this] { return Layout->GetNumVisiblePanes() == 3 && !Layout->IsPaneVisible(3); }, 3)
		    .Capture("three_panes_left")
		    .Do(
		        [this]
		{
			Layout->SetActivePane(1);
			Layout->SetLayout(EMapViewportLayout::OnePane);
		})
		    .Check("one pane shows the view used last",
		        [this] { return Layout->GetNumVisiblePanes() == 1 && Layout->GetClients()[0]->IsPerspective(); }, 3)
		    .Capture("one_pane")
		    .Do([this] { Layout->SetLayout(EMapViewportLayout::FourPanes2x2); })
		    .Check("back to four panes", [this] { return Layout->GetNumVisiblePanes() == 4; }, 3)
		    // DPI.
		    .Do([this] { SimulateDpiChange(1.0f); })
		    .Capture("dpi100", 30)
		    .Do([this] { SimulateDpiChange(2.0f); })
		    .Capture("dpi200", 30)
		    .Do([this] { SimulateDpiChange(GetNativeDpiScale()); });
	}

private:
	FMapViewportClient& Pane(int32 Index) const
	{
		return *Panes[static_cast<SIZE_T>(Index)];
	}
	FMapViewportClient& Top() const
	{
		return Pane(0);
	}
	FMapViewportClient& Perspective() const
	{
		return Pane(1);
	}
	FMapViewportClient& Front() const
	{
		return Pane(2);
	}

	void SetGridSize(float Size)
	{
		ViewSettings.GridSize = FMath::Clamp(Size, FMapViewSettings::MinGridSize, FMapViewSettings::MaxGridSize);
		ViewSettings.Changed();
		SetLastEvent(FString::Format("Grid {:.0f}", ViewSettings.GridSize));
	}

	void ToggleSolid()
	{
		Perspective().SetViewMode(
		    Perspective().GetViewMode() == EMapViewMode::Wireframe ? EMapViewMode::Lit : EMapViewMode::Wireframe);
	}

	void FrameAll()
	{
		for (const TSharedPtr<FMapViewportClient>& View : Panes)
		{
			View->FrameSelection();
		}
	}

	TArray<TSharedPtr<FMapViewportClient>> Panes;
	TSharedPtr<SMapViewportLayout> Layout;
	float ZoomBefore = 0.0f;
	FVector2 OriginBefore;
};

IMPLEMENT_TEST_APP(FMapViewsTestApp)
