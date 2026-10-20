// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// UIViewportTest: an RHI viewport in the UI. SViewport shows a render
// target that a viewport client (FRenderTargetViewportClient) draws into
// when something changes: a small 2D map with a grid. Left click selects
// (Shift adds), a left drag moves the selection (snapped), the wheel zooms
// at the cursor, the middle button pans, and the right button pans with raw
// mouse input (the cursor hides).
//
// Self-test: selects with clicks, moves an object with a drag (one undo
// step), undoes it, zooms with the wheel, pans, and toggles the grid.

#include "DemoScene.h"
#include "TestApp.h"
#include "TestWidgets.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/SViewport.h"
#include "Widgets/Text/STextBlock.h"

class FViewportTestApp final : public FTestApp
{
public:
	FViewportTestApp() : FTestApp({ "UIViewportTest", "Genesis UI - RHI viewport", FVector2(1000.0f, 700.0f) }) {}

protected:
	TSharedRef<SWidget> MakeContent() override
	{
		Client = std::make_shared<FDemoViewportClient>(GetDevice(), GetRenderer(), Scene);
		GetWindowRenderer().AddViewportClient(ToSharedRef(StaticCastSharedPtr<FRenderTargetViewportClient>(Client)));
		Scene.OnChanged.AddLambda([this] { Client->RequestRedraw(); });
		Scene.OnSelectionChanged.AddLambda([this] { Client->RequestRedraw(); });
		Scene.OnLog.AddLambda([this](const FString& Line) { SetLastEvent(Line); });

		const auto Toggle = [](const char* Label, TFunction<bool()> Get, TFunction<void()> Flip) -> TSharedRef<SWidget>
		{
			return SNew(SCheckBox).Text(Label).IsChecked([Get] { return Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }).OnCheckStateChanged([Flip](ECheckBoxState) { Flip(); });
		};
		return TestFrame(*this, "RHI viewport",
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(12.0f, 8.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 16.0f, 0.0f))
					[Toggle("Grid", [this] { return Client->bShowGrid; }, [this] { Client->bShowGrid = !Client->bShowGrid; Client->RequestRedraw(); })]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 16.0f, 0.0f))
					[Toggle("Snap", [this] { return Client->bSnap; }, [this] { Client->bSnap = !Client->bSnap; })]
				+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 8.0f, 0.0f))[SNew(SButton).Text("Undo").IsEnabled([this] { return Scene.CanUndo(); }).OnClicked([this] { Scene.Undo(); return FReply::Handled(); })]
				+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 8.0f, 0.0f))[SNew(SButton).Text("Redo").IsEnabled([this] { return Scene.CanRedo(); }).OnClicked([this] { Scene.Redo(); return FReply::Handled(); })]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text("Frame selection").OnClicked([this] { Client->FrameSelection(); return FReply::Handled(); })]
				+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right).VAlign(VAlign_Center)[SNew(STextBlock).TextStyle(&TestTextStyle("SmallText")).Text([this]
				{
					return FString::Format("{} selected · zoom {:.0f}% · {} draws", Scene.Selection.size(), Client->GetZoom() * 100.0f, Client->GetDrawCount());
				})]
			]
			+ SVerticalBox::Slot().FillHeight(1.0f).Padding(FMargin(12.0f, 0.0f, 12.0f, 12.0f))
			[
				SAssignNew(Viewport, SViewport).ViewportClient(StaticCastSharedPtr<IViewportClient>(Client))
			]);
	}

	void OnShutdown() override
	{
		Viewport.reset();
		Client.reset();
	}

	void BuildSelfTest(FTestScript& Script) override
	{
		Script.Capture("start")
			.Do([this] { Click(EntityCenter("pillar")); })
			.Check("a click selects the pillar", [this] { return Scene.Selection == TArray<int32>{ Find("pillar")->Id }; })
			.Do([this]
			{
				SetModifierKeys(true, false);
				Click(EntityCenter("light_warm"));
				SetModifierKeys(false, false);
			})
			.Check("Shift-click adds the light", [this] { return Scene.Selection.size() == 2; })
			// Drag the pillar by 64 world units right: a drag that starts on the
			// selection moves all of it, snapped, as one undo step.
			.Do([this]
			{
				PillarStart = Find("pillar")->Position;
				LightStart = Find("light_warm")->Position;
				const float Pixels = 64.0f * Client->GetZoom() * Viewport->GetCachedGeometry().Scale;
				Drag(EntityCenter("pillar"), EntityCenter("pillar") + FVector2(Pixels + 3.0f, 0.0f));
			})
			.Check("the drag moves the selection 64 units, snapped", [this]
			{
				return Find("pillar")->Position == PillarStart + FVector2(64.0f, 0.0f) && Find("light_warm")->Position == LightStart + FVector2(64.0f, 0.0f);
			})
			.Check("the move is one undo step", [this] { return Scene.CanUndo() && Scene.GetUndoDescription() == "Move 2 object(s)"; })
			.Capture("moved")
			.Do([this] { PointAtText("Undo", true); })
			.Check("Undo puts it back", [this] { return Find("pillar")->Position == PillarStart && Scene.CanRedo(); })
			// Zoom at the cursor, then pan with the middle button.
			.Do([this]
			{
				ZoomBefore = Client->GetZoom();
				Wheel(GetCenter(*Viewport), 3.0f);
			})
			.Check("the wheel zooms in", [this] { return Client->GetZoom() > ZoomBefore * 1.3f; })
			.Do([this]
			{
				PanBefore = Client->WorldToLocal(FVector2::Zero());
				Drag(GetCenter(*Viewport), GetCenter(*Viewport) + FVector2(-120.0f, -80.0f), EMouseButton::Middle);
			})
			.Check("a middle drag pans", [this] { return Client->WorldToLocal(FVector2::Zero()).X < PanBefore.X - 10.0f; })
			.Capture("zoomed")
			.Do([this]
			{
				DrawsBefore = Client->GetDrawCount();
				PointAtText("Grid", true);
			})
			.Check("toggling the grid redraws the viewport", [this] { return !Client->bShowGrid && Client->GetDrawCount() > DrawsBefore; }, 5)
			.Do([this] { MoveMouse(FVector2(2.0f, 2.0f)); DrawsBefore = Client->GetDrawCount(); }, 10)
			.Check("an idle viewport does not redraw", [this] { return Client->GetDrawCount() == DrawsBefore; }, 30)
			.Capture("no_grid");
	}

private:
	const FDemoEntity* Find(const char* Name) const
	{
		for (const FDemoEntity& E : Scene.Entities)
		{
			if (E.Name == Name)
			{
				return &E;
			}
		}
		return &Scene.Entities[0];
	}

	// Where an entity's center is in the window.
	FVector2 EntityCenter(const char* Name) const
	{
		const FDemoEntity* E = Find(Name);
		return Viewport->GetCachedGeometry().LocalToAbsolute(Client->WorldToLocal(E->Position + E->Size * 0.5f));
	}

	FDemoScene Scene;
	TSharedPtr<FDemoViewportClient> Client;
	TSharedPtr<SViewport> Viewport;
	int32 DrawsBefore = 0;
	FVector2 PillarStart;
	FVector2 LightStart;
	FVector2 PanBefore;
	float ZoomBefore = 0.0f;
};

IMPLEMENT_TEST_APP(FViewportTestApp)
