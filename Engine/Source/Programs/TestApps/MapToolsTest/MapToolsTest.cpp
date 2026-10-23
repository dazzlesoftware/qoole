// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// MapToolsTest: editing a map with the mouse. A top, a front and a 3D view
// of one FMapDocument, the editing tools of the 2D views (select and move,
// rotate, scale, clip, vertex, edge and face), brush operations (hollow,
// subtract, group), undo and redo, and saving and reading back .qle and
// .map files.
//
// Self-test: selects and moves with the mouse and undoes, box-selects,
// rotates, scales, clips (keeping a side, then splitting), shows the vertex
// dots, hollows, subtracts, groups and ungroups, then saves and reopens.

#include "MapTestApp.h"
#include "Containers/ContainerAliases.h"
#include "Templates/SharedPointer.h"
#include "TestWidgets.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/SViewport.h"
#include "Widgets/Text/STextBlock.h"

class FMapToolsTestApp final : public FMapTestApp
{
public:
	FMapToolsTestApp() : FMapTestApp({"MapToolsTest", "Map editor - Tools and edits", FVector2(1280.0f, 820.0f)}) {}

protected:
	TSharedRef<SWidget> MakeContent() override
	{
		Top = CreateView(EMapViewType::Top).ToSharedPtr();
		Front = CreateView(EMapViewType::Front).ToSharedPtr();
		Perspective = CreateView(EMapViewType::Perspective).ToSharedPtr();
		Perspective->SetViewMode(EMapViewMode::Lit);

		const TFunction<TSharedRef<SWidget>(const TSharedPtr<FMapViewportClient>&, const char*)> Pane =
		    [this](const TSharedPtr<FMapViewportClient>& View, const char* Label) -> TSharedRef<SWidget>
		{
			return SNew(SBorder)
			    .BorderImage(&FAppStyle::Get().GetBrush("Recessed"))
			    .Padding(1.0f)[SNew(SVerticalBox) +
			        SVerticalBox::Slot().AutoHeight().Padding(
			            FMargin(6.0f, 2.0f))[SNew(STextBlock).TextStyle(&TestTextStyle("SmallText")).Text(Label)] +
			        SVerticalBox::Slot().FillHeight(1.0f)[MakeViewport(ToSharedRef(View))]];
		};
		const TSharedRef<SHorizontalBox> Tools = SNew(SHorizontalBox);
		for (const EMapTool Tool : {EMapTool::Select, EMapTool::Rotate, EMapTool::Scale, EMapTool::Clip,
		         EMapTool::Vertex, EMapTool::Edge, EMapTool::Face})
		{
			Tools->AddSlot()
			    .AutoWidth()
			    .VAlign(VAlign_Center)
			    .Padding(FMargin(0.0f, 0.0f, 10.0f, 0.0f))[SNew(SCheckBox)
			            .Text(ToString(Tool))
			            .IsChecked([this, Tool] {
				return ViewSettings.Tool == Tool ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}).OnCheckStateChanged([this, Tool](ECheckBoxState) { SetTool(Tool); })];
		}
		const TFunction<TSharedRef<SWidget>(const char*, TFunction<void()>, TFunction<bool()>)> Action =
		    [](const char* Label, TFunction<void()> Do, TFunction<bool()> CanDo) -> TSharedRef<SWidget>
		{
			return SNew(SButton)
			    .Text(Label)
			    .IsEnabled([CanDo] { return CanDo(); })
			    .OnClicked([Do]
			{
				Do();
				return FReply::Handled();
			});
		};
		const TFunction<bool()> HasSelection = [this] { return !GetDocument().GetSelection().empty(); };
		const TSharedRef<SHorizontalBox> Actions = SNew(SHorizontalBox);
		const TFunction<void(const TSharedRef<SWidget>&)> AddAction = [&Actions](const TSharedRef<SWidget>& Button)
		{ Actions->AddSlot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))[Button]; };
		AddAction(Action("Undo", [this] { GetDocument().Undo(); }, [this] { return GetDocument().CanUndo(); }));
		AddAction(Action("Redo", [this] { GetDocument().Redo(); }, [this] { return GetDocument().CanRedo(); }));
		AddAction(Action("Hollow", [this] { GetDocument().HollowObjects(GetDocument().GetSelection(), 16.0f, true); },
		    HasSelection));
		AddAction(Action("Subtract", [this] { GetDocument().SubtractSelection(); }, HasSelection));
		AddAction(Action("Group", [this] { GetDocument().GroupObjects(GetDocument().GetSelection()); }, HasSelection));
		AddAction(
		    Action("Ungroup", [this] { GetDocument().UngroupObjects(GetDocument().GetSelection()); }, HasSelection));

		return TestFrame(*this, "Tools and edits",
		    SNew(SVerticalBox) +
		        SVerticalBox::Slot().AutoHeight().Padding(FMargin(10.0f, 6.0f))[SNew(SHorizontalBox) +
		            SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 20.0f, 0.0f))[Tools] +
		            SHorizontalBox::Slot().AutoWidth()[Actions] +
		            SHorizontalBox::Slot()
		                .FillWidth(1.0f)
		                .HAlign(HAlign_Right)
		                .VAlign(VAlign_Center)[SNew(STextBlock)
		                        .TextStyle(&TestTextStyle("SmallText"))
		                        .Text(
		                            [this]
		{
			return FString::Format("{} brushes · {} entities · {} selected{}", GetDocument().CountBrushes(),
			    GetDocument().CountEntities(), GetDocument().GetSelection().size(),
			    GetDocument().CanUndo() ? FString::Format(" · undo: {}", GetDocument().GetUndoDescription())
			                            : FString());
		})]] +
		        SVerticalBox::Slot().FillHeight(1.0f).Padding(
		            FMargin(8.0f, 0.0f, 8.0f, 8.0f))[SNew(SSplitter) + SSplitter::Slot().Value(0.6f)[Pane(Top, "Top")] +
		            SSplitter::Slot().Value(0.4f)[SNew(SSplitter).Orientation(EOrientation::Vertical) +
		                SSplitter::Slot().Value(0.5f)[Pane(Front, "Front")] +
		                SSplitter::Slot().Value(0.5f)[Pane(Perspective, "3D")]]]);
	}

	void OnShutdown() override
	{
		Top.reset();
		Front.reset();
		Perspective.reset();
		FMapTestApp::OnShutdown();
	}

	void BuildSelfTest(FTestScript& Script) override
	{
		const FString QlePath = GetSelfTestDir() + "MapToolsTest.qle";
		const FString MapPath = GetSelfTestDir() + "MapToolsTest.map";
		Script.Do([this] { BuildScene(); })
		    .Capture("scene", 20)

		    // Select the cube with a click, drag it 128 right and 64 up (snapped), undo, redo.
		    .Do([this] { Click(TopPoint(-160.0f, -96.0f)); })
		    .Check("a click selects the cube",
		        [this] { return GetDocument().GetSelection().size() == 1 && GetDocument().IsSelected(Cube); })
		    .Do(
		        [this]
		{
			GetSelectionBounds(StartMin, StartMax);
			Drag(TopPoint(-160.0f, -96.0f), TopPoint(-32.0f, -32.0f));
		})
		    .Check("a drag on the selection moves it by whole grid steps",
		        [this] { return SelectionMinMovedBy(128.0f, 64.0f); })
		    .Check("the move is one undo step", [this] { return GetDocument().CanUndo(); })
		    .Capture("moved")
		    .Do([this] { PointAtText("Undo", true); })
		    .Check("Undo puts it back", [this] { return SelectionMinMovedBy(0.0f, 0.0f); })
		    .Do([this] { PointAtText("Redo", true); })
		    .Check("Redo moves it again", [this] { return SelectionMinMovedBy(128.0f, 64.0f); })

		    // Box-select across the middle of the scene in the front view.
		    .Do([this] { Drag(FrontPoint(-220.0f, 140.0f), FrontPoint(220.0f, -40.0f)); })
		    .Check("a box in the front view selects several objects",
		        [this] { return GetDocument().GetSelection().size() >= 3; })
		    .Capture("box_select")

		    // Rotate the pyramid about 45 degrees.
		    .Do(
		        [this]
		{
			GetDocument().SetSelection({Pyramid});
			GetSelectionBounds(StartMin, StartMax);
			SetTool(EMapTool::Rotate);
		})
		    .Do([this] { Drag(TopPoint(176.0f + 80.0f, -144.0f), TopPoint(176.0f + 56.0f, -144.0f + 56.0f)); })
		    .Check("the rotate tool turns the pyramid",
		        [this] { return !SelectionBoundsAre(StartMin, StartMax) && GetDocument().CanUndo(); })
		    .Capture("rotated")

		    // Scale the column: drag its corner outward.
		    .Do(
		        [this]
		{
			GetDocument().SetSelection({Column});
			GetSelectionBounds(StartMin, StartMax);
			SetTool(EMapTool::Scale);
		})
		    .Do(
		        [this]
		{
			const FVector2 Corner = TopPoint(StartMax.GetX() - 2.0f, StartMax.GetY() - 2.0f);
			Drag(Corner, TopPoint(StartMax.GetX() + 46.0f, StartMax.GetY() + 46.0f));
		})
		    .Check("the scale tool makes the column bigger",
		        [this]
		{
			Vector3d Min, Max;
			GetSelectionBounds(Min, Max);
			return Max.GetX() - Min.GetX() > StartMax.GetX() - StartMin.GetX() + 8.0f;
		})
		    .Capture("scaled")

		    // Clip the sphere along a vertical line in the front view: keep one side.
		    .Do(
		        [this]
		{
			GetDocument().SetSelection({Sphere});
			GetSelectionBounds(StartMin, StartMax);
			Brushes = GetDocument().CountBrushes();
			SetTool(EMapTool::Clip);
		})
		    .Do([this] { Drag(FrontPoint(-128.0f, 200.0f), FrontPoint(-128.0f, -80.0f)); })
		    .Capture("clip_line")
		    .Do([this] { PressKey(EKey::Enter, 13); })
		    .Check("Enter keeps one side of the clipped sphere",
		        [this]
		{
			Vector3d Min, Max;
			GetSelectionBounds(Min, Max);
			return GetDocument().CountBrushes() == Brushes &&
			    Max.GetX() - Min.GetX() < (StartMax.GetX() - StartMin.GetX()) * 0.75f;
		})
		    .Capture("clipped")
		    .Do([this] { GetDocument().Undo(); })
		    .Do([this] { Drag(FrontPoint(-128.0f, 200.0f), FrontPoint(-128.0f, -80.0f)); })
		    .Do(
		        [this]
		{
			SetModifierKeys(false, true);
			PressKey(EKey::Enter, 13);
			SetModifierKeys(false, false);
		})
		    .Check("Ctrl+Enter splits it in two", [this] { return GetDocument().CountBrushes() == Brushes + 1; })

		    // The vertex dots of the column.
		    .Do(
		        [this]
		{
			SetTool(EMapTool::Vertex);
			GetDocument().SetSelection({Column});
		})
		    .Capture("vertices")
		    .Do([this] { SetTool(EMapTool::Select); })

		    // Hollow the cube: six walls instead of one brush.
		    .Do(
		        [this]
		{
			Brushes = GetDocument().CountBrushes();
			GetDocument().SetSelection({Cube});
		})
		    .Do([this] { PointAtText("Hollow", true); })
		    .Check(
		        "Hollow turns the cube into six walls", [this] { return GetDocument().CountBrushes() == Brushes + 5; })
		    .Capture("hollowed")

		    // Subtract a cube from the plate.
		    .Do(
		        [this]
		{
			Brushes = GetDocument().CountBrushes();
			GetDocument().AddPrefab(FindPrefab("Cube"), Vector3d(0.0f, 0.0f, -8.0f));
		})
		    .Do([this] { PointAtText("Subtract", true); })
		    .Check("Subtract cuts the plate around the cube",
		        [this] { return GetDocument().CountBrushes() > Brushes + 1; })
		    .Do([this] { GetDocument().DeleteObjects(GetDocument().GetSelection()); })
		    .Capture("subtracted")

		    // Group the column and the pyramid, then ungroup.
		    .Do(
		        [this]
		{
			Objects = static_cast<int32>(GetDocument().GetObjects().size());
			GetDocument().SetSelection({Column, Pyramid});
		})
		    .Do([this] { PointAtText("Group", true); })
		    .Check("Group makes one object of two",
		        [this]
		{
			return static_cast<int32>(GetDocument().GetObjects().size()) == Objects - 1 &&
			    GetDocument().GetSelection().size() == 1;
		})
		    .Do([this] { PointAtText("Ungroup", true); })
		    .Check("Ungroup gives them back",
		        [this]
		{
			return static_cast<int32>(GetDocument().GetObjects().size()) == Objects &&
			    GetDocument().GetSelection().size() == 2;
		})

		    // Save as .qle and export .map; both read back the same.
		    .Do(
		        [this, QlePath, MapPath]
		{
			Brushes = GetDocument().CountBrushes();
			Entities = GetDocument().CountEntities();
			FString Error;
			Expect(GetDocument().Save(QlePath, Error), FString::Format("saving {} {}", QlePath, Error));
			Expect(GetDocument().Save(MapPath, Error), FString::Format("exporting {} {}", MapPath, Error));
		})
		    .Do(
		        [this, QlePath]
		{
			FString Error;
			TUniquePtr<FMapDocument> Opened = FMapDocument::Open(QlePath, Error);
			if (Expect(Opened != nullptr, FString::Format("opening {} {}", QlePath, Error)))
			{
				SetDocument(std::move(Opened));
			}
			Report("Reopened .qle");
		})
		    .Check("the .qle reads back the same",
		        [this] { return GetDocument().CountBrushes() == Brushes && GetDocument().CountEntities() == Entities; })
		    .Capture("reopened", 20)
		    .Do([this, MapPath]
		{
			FString Error;
			TUniquePtr<FMapDocument> Opened = FMapDocument::Open(MapPath, Error);
			if (Expect(Opened != nullptr, FString::Format("importing {} {}", MapPath, Error)))
			{
				SetDocument(std::move(Opened));
			}
			Report("Imported .map");
		}).Check("the .map reads back the same", [this] {
			return GetDocument().CountBrushes() == Brushes && GetDocument().CountEntities() == Entities;
		});
	}

private:
	void SetTool(EMapTool Tool)
	{
		ViewSettings.Tool = Tool;
		ViewSettings.Changed();
		SetLastEvent(FString::Format("Tool: {}", ToString(Tool)));
	}

	FVector2 TopPoint(float X, float Y) const
	{
		return MapToWindow(*Top, Vector3d(X, Y, 32.0f));
	}
	FVector2 FrontPoint(float X, float Z) const
	{
		return MapToWindow(*Front, Vector3d(X, 0.0f, Z));
	}

	bool SelectionMinMovedBy(float X, float Y) const
	{
		Vector3d Min, Max;
		GetSelectionBounds(Min, Max);
		return FMath::Abs(Min.GetX() - (StartMin.GetX() + X)) < 0.5f &&
		    FMath::Abs(Min.GetY() - (StartMin.GetY() + Y)) < 0.5f;
	}

	bool SelectionBoundsAre(const Vector3d& ExpectedMin, const Vector3d& ExpectedMax) const
	{
		Vector3d Min, Max;
		GetSelectionBounds(Min, Max);
		const TFunction<bool(const Vector3d&, const Vector3d&)> Near = [](const Vector3d& A, const Vector3d& B)
		{
			return FMath::Abs(A.GetX() - B.GetX()) < 0.5f && FMath::Abs(A.GetY() - B.GetY()) < 0.5f &&
			    FMath::Abs(A.GetZ() - B.GetZ()) < 0.5f;
		};
		return Near(Min, ExpectedMin) && Near(Max, ExpectedMax);
	}

	TSharedPtr<FMapViewportClient> Top;
	TSharedPtr<FMapViewportClient> Front;
	TSharedPtr<FMapViewportClient> Perspective;
	Vector3d StartMin, StartMax;
	int32 Brushes = 0;
	int32 Entities = 0;
	int32 Objects = 0;
};

IMPLEMENT_TEST_APP(FMapToolsTestApp)
