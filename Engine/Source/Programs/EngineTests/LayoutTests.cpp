// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Headless layout tests of the widget core and the layout panels.

#include "UITestHelpers.h"

#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SOverlay.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SImage.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	// A leaf with a fixed desired size that counts how often it is measured,
	// arranged and painted.
	class STestLeaf : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(STestLeaf)
			SLATE_ATTRIBUTE(FVector2, Size, (FVector2(10.0f, 10.0f)))
		SLATE_END_ARGS()

		void Construct(const FArguments& Args) { Size.Assign(Args._Size); }
		void SetSize(TAttribute<FVector2> InSize) { Size.Assign(InSize); }

		mutable int32 MeasureCount = 0;
		mutable int32 PaintCount = 0;

	protected:
		FVector2 ComputeDesiredSize(float) const override
		{
			++MeasureCount;
			return Size.Get();
		}
		int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FRect&, FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle&) const override
		{
			++PaintCount;
			Out.AddSolidBox(LayerId, Geometry.ToPaintGeometry(), FColor::White());
			return LayerId;
		}

	private:
		TWidgetAttribute<FVector2, EInvalidateWidgetReason::Layout> Size{ *this };
	};

	TSharedRef<STestLeaf> Leaf(float W, float H)
	{
		return SNew(STestLeaf).Size(FVector2(W, H));
	}

	bool RectNear(const FRect& A, const FRect& B, float Tolerance = 0.01f)
	{
		return FMath::IsNearlyEqual(A.Left, B.Left, Tolerance) && FMath::IsNearlyEqual(A.Top, B.Top, Tolerance)
			&& FMath::IsNearlyEqual(A.Right, B.Right, Tolerance) && FMath::IsNearlyEqual(A.Bottom, B.Bottom, Tolerance);
	}
}

TEST_SUITE("UILayout")
{
	TEST_CASE("Vertical box: auto slots take their size, fill slots share the rest by weight")
	{
		FUITestApp T;
		TSharedPtr<STestLeaf> A, B, C;
		const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(5.0f)[SAssignNew(A, STestLeaf).Size(FVector2(50.0f, 20.0f))]
			+ SVerticalBox::Slot().FillHeight(1.0f)[SAssignNew(B, STestLeaf)]
			+ SVerticalBox::Slot().FillHeight(3.0f)[SAssignNew(C, STestLeaf)];
		T.AddWindow(Box, FVector2(200.0f, 330.0f));

		// A: 20 tall + 10 padding; 300 left for B and C at 1:3.
		CHECK(RectNear(FUITestApp::RectOf(*A), FRect(5.0f, 5.0f, 195.0f, 25.0f)));
		CHECK(RectNear(FUITestApp::RectOf(*B), FRect(0.0f, 30.0f, 200.0f, 105.0f)));
		CHECK(RectNear(FUITestApp::RectOf(*C), FRect(0.0f, 105.0f, 200.0f, 330.0f)));
		CHECK(Box->GetDesiredSize() == FVector2(60.0f, 50.0f));
	}

	TEST_CASE("Horizontal box: alignment inside the cell, max width")
	{
		FUITestApp T;
		TSharedPtr<STestLeaf> A, B;
		T.AddWindow(SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SAssignNew(A, STestLeaf).Size(FVector2(40.0f, 20.0f))]
			+ SHorizontalBox::Slot().FillWidth(1.0f).MaxWidth(100.0f).HAlign(HAlign_Right).VAlign(VAlign_Bottom)[SAssignNew(B, STestLeaf).Size(FVector2(30.0f, 10.0f))],
			FVector2(400.0f, 100.0f));
		CHECK(RectNear(FUITestApp::RectOf(*A), FRect(0.0f, 40.0f, 40.0f, 60.0f)));
		CHECK(RectNear(FUITestApp::RectOf(*B), FRect(110.0f, 90.0f, 140.0f, 100.0f)));
	}

	TEST_CASE("Overlay stacks children and paints later ones on higher layers")
	{
		FUITestApp T;
		TSharedPtr<STestLeaf> Back, Front;
		const TSharedRef<SWindow> Window = T.AddWindow(SNew(SOverlay)
			+ SOverlay::Slot()[SAssignNew(Back, STestLeaf)]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[SAssignNew(Front, STestLeaf).Size(FVector2(20.0f, 10.0f))],
			FVector2(100.0f, 100.0f));
		CHECK(RectNear(FUITestApp::RectOf(*Back), FRect(0.0f, 0.0f, 100.0f, 100.0f)));
		CHECK(RectNear(FUITestApp::RectOf(*Front), FRect(40.0f, 45.0f, 60.0f, 55.0f)));

		// The front child wins hit-testing and draws above the back one.
		const TArray<TSharedRef<SWidget>> Path = Window->HitTestAt(FVector2(50.0f, 50.0f));
		REQUIRE(!Path.empty());
		CHECK(&Path.back().Get() == Front.get());
		const TArray<FSlateDrawElement>& Elements = Window->GetDrawElements().GetElements();
		REQUIRE(Elements.size() == 3); // background + two leaves
		CHECK(Elements[2].Layer > Elements[1].Layer);
	}

	TEST_CASE("Grid panel: tracks size to their content, spans, fill columns")
	{
		FUITestApp T;
		TSharedPtr<SGridPanel> Grid;
		TSharedPtr<STestLeaf> A, B, Wide;
		SAssignNew(Grid, SGridPanel)
			+ SGridPanel::Slot(0, 0)[SAssignNew(A, STestLeaf).Size(FVector2(30.0f, 10.0f))]
			+ SGridPanel::Slot(1, 0)[SAssignNew(B, STestLeaf).Size(FVector2(50.0f, 20.0f))]
			+ SGridPanel::Slot(0, 1).ColumnSpan(2)[SAssignNew(Wide, STestLeaf).Size(FVector2(100.0f, 10.0f))];
		Grid->SetColumnFill(1, 1.0f);
		T.AddWindow(ToSharedRef(Grid), FVector2(200.0f, 100.0f));

		// Column 0 is 30 wide; the spanning child would need 100, so each of
		// the two columns grows by 10; column 1 then fills the window.
		CHECK(FUITestApp::RectOf(*A).Width() == doctest::Approx(40.0f));
		CHECK(FUITestApp::RectOf(*B).Left == doctest::Approx(40.0f));
		CHECK(FUITestApp::RectOf(*B).Right == doctest::Approx(200.0f));
		CHECK(RectNear(FUITestApp::RectOf(*Wide), FRect(0.0f, 20.0f, 200.0f, 30.0f)));
	}

	TEST_CASE("Wrap box moves children to the next line")
	{
		FUITestApp T;
		TSharedPtr<STestLeaf> L[4];
		T.AddWindow(SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2(5.0f, 5.0f))
			+ SWrapBox::Slot()[SAssignNew(L[0], STestLeaf).Size(FVector2(40.0f, 10.0f))]
			+ SWrapBox::Slot()[SAssignNew(L[1], STestLeaf).Size(FVector2(40.0f, 20.0f))]
			+ SWrapBox::Slot()[SAssignNew(L[2], STestLeaf).Size(FVector2(40.0f, 10.0f))]
			+ SWrapBox::Slot()[SAssignNew(L[3], STestLeaf).Size(FVector2(40.0f, 10.0f))],
			FVector2(100.0f, 100.0f));
		CHECK(FUITestApp::RectOf(*L[0]).GetTopLeft() == FVector2(0.0f, 0.0f));
		CHECK(FUITestApp::RectOf(*L[1]).GetTopLeft() == FVector2(45.0f, 0.0f));
		CHECK(FUITestApp::RectOf(*L[2]).GetTopLeft() == FVector2(0.0f, 25.0f)); // line height 20 + 5
		CHECK(FUITestApp::RectOf(*L[3]).GetTopLeft() == FVector2(45.0f, 25.0f));
	}

	TEST_CASE("Canvas places children by anchors and offsets")
	{
		FUITestApp T;
		TSharedPtr<STestLeaf> Point, Stretched, Centered;
		T.AddWindow(SNew(SCanvas)
			+ SCanvas::Slot().Offset(FMargin(10.0f, 20.0f, 30.0f, 40.0f))[SAssignNew(Point, STestLeaf)]
			+ SCanvas::Slot().Anchors(FAnchors(0.0f, 1.0f, 1.0f, 1.0f)).Offset(FMargin(5.0f, -30.0f, 5.0f, 20.0f))[SAssignNew(Stretched, STestLeaf)]
			+ SCanvas::Slot().Anchors(FAnchors(0.5f)).Alignment(FVector2(0.5f, 0.5f)).AutoSize(true)[SAssignNew(Centered, STestLeaf).Size(FVector2(20.0f, 10.0f))],
			FVector2(200.0f, 100.0f));
		CHECK(RectNear(FUITestApp::RectOf(*Point), FRect(10.0f, 20.0f, 40.0f, 60.0f)));
		CHECK(RectNear(FUITestApp::RectOf(*Stretched), FRect(5.0f, 70.0f, 195.0f, 90.0f)));
		CHECK(RectNear(FUITestApp::RectOf(*Centered), FRect(90.0f, 45.0f, 110.0f, 55.0f)));
	}

	TEST_CASE("Scale box scales its child to fit")
	{
		FUITestApp T;
		TSharedPtr<STestLeaf> Child;
		TSharedPtr<SScaleBox> Scale;
		T.AddWindow(SAssignNew(Scale, SScaleBox).Stretch(EStretch::ScaleToFit)[SAssignNew(Child, STestLeaf).Size(FVector2(50.0f, 25.0f))], FVector2(200.0f, 200.0f));
		CHECK(Scale->GetContentScale() == doctest::Approx(4.0f));
		CHECK(RectNear(FUITestApp::RectOf(*Child), FRect(0.0f, 50.0f, 200.0f, 150.0f)));
		// The child's local space is scaled: local size stays 50x25.
		CHECK(Child->GetCachedGeometry().Size == FVector2(50.0f, 25.0f));
	}

	TEST_CASE("Box size overrides and limits")
	{
		FUITestApp T;
		TSharedPtr<SBox> Fixed, Limited;
		T.AddWindow(SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)[SAssignNew(Fixed, SBox).WidthOverride(TOptional<float>(80.0f)).HeightOverride(TOptional<float>(30.0f))[Leaf(10.0f, 10.0f)]]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)[SAssignNew(Limited, SBox).MinDesiredWidth(TOptional<float>(60.0f)).MaxDesiredHeight(TOptional<float>(5.0f))[Leaf(10.0f, 10.0f)]]);
		CHECK(FUITestApp::RectOf(*Fixed).GetSize() == FVector2(80.0f, 30.0f));
		CHECK(FUITestApp::RectOf(*Limited).GetSize() == FVector2(60.0f, 5.0f));
	}

	TEST_CASE("Retained layout: only the invalidated branch is measured again")
	{
		FUITestApp T;
		TSharedPtr<STestLeaf> A, B;
		T.AddWindow(SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox)[SAssignNew(A, STestLeaf)]]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox)[SAssignNew(B, STestLeaf)]]);
		const int32 MeasuredA = A->MeasureCount;
		const int32 MeasuredB = B->MeasureCount;
		const int32 PaintedB = B->PaintCount;

		// Idle frames do no layout and no painting.
		T.Tick();
		T.Tick();
		CHECK(A->MeasureCount == MeasuredA);
		CHECK(B->PaintCount == PaintedB);

		A->SetSize(FVector2(25.0f, 10.0f));
		T.Tick();
		CHECK(A->MeasureCount == MeasuredA + 1);
		CHECK(B->MeasureCount == MeasuredB);         // sibling branch untouched
		CHECK(B->PaintCount == PaintedB + 1);        // the window repainted
		CHECK(FUITestApp::RectOf(*B).Left == doctest::Approx(25.0f)); // and B moved
	}

	TEST_CASE("Bound attributes are evaluated each frame and invalidate only on change")
	{
		FUITestApp T;
		float Width = 10.0f;
		TSharedPtr<STestLeaf> A;
		T.AddWindow(SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth()[SAssignNew(A, STestLeaf).Size([&Width] { return FVector2(Width, 10.0f); })]);
		const int32 Measured = A->MeasureCount;
		T.Tick();
		CHECK(A->MeasureCount == Measured);
		Width = 70.0f;
		T.Tick();
		CHECK(A->MeasureCount == Measured + 1);
		CHECK(FUITestApp::RectOf(*A).Width() == doctest::Approx(70.0f));
	}

	TEST_CASE("Collapsed children take no space; hidden ones do")
	{
		FUITestApp T;
		TSharedPtr<STestLeaf> A, B;
		T.AddWindow(SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[SAssignNew(A, STestLeaf).Size(FVector2(30.0f, 10.0f))]
			+ SHorizontalBox::Slot().AutoWidth()[SAssignNew(B, STestLeaf).Size(FVector2(30.0f, 10.0f))]);
		A->SetVisibility(EVisibility::Hidden);
		T.Tick();
		CHECK(FUITestApp::RectOf(*B).Left == doctest::Approx(30.0f));
		A->SetVisibility(EVisibility::Collapsed);
		T.Tick();
		CHECK(FUITestApp::RectOf(*B).Left == doctest::Approx(0.0f));
	}

	TEST_CASE("DPI changes rescale geometry but not logical layout")
	{
		FUITestApp T(1.0f);
		TSharedPtr<STestLeaf> A;
		const TSharedRef<SWindow> Window = T.AddWindow(SNew(SBox).Padding(FMargin(10.0f))[SAssignNew(A, STestLeaf)], FVector2(100.0f, 100.0f));
		CHECK(A->GetCachedGeometry().GetLayoutBoundingRect() == FRect(10.0f, 10.0f, 90.0f, 90.0f));
		T.Native(Window).SimulateDpiChange(2.0f);
		T.Tick();
		CHECK(Window->GetScale() == 2.0f);
		CHECK(A->GetCachedGeometry().GetLayoutBoundingRect() == FRect(20.0f, 20.0f, 180.0f, 180.0f));
		CHECK(A->GetCachedGeometry().Size == FVector2(80.0f, 80.0f));
	}

	TEST_CASE("Text block measures its text and wraps to the arranged width")
	{
		FUITestApp T;
		TSharedPtr<STextBlock> Text;
		T.AddWindow(SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[SAssignNew(Text, STextBlock)
			.Text("The quick brown fox jumps over the lazy dog and keeps running far away").AutoWrapText(true)], FVector2(150.0f, 200.0f));
		const FTextLayout& Layout = Text->GetTextLayout();
		CHECK(Layout.GetLines().size() >= 3);
		for (const FTextLine& Line : Layout.GetLines())
		{
			CHECK(Line.Width <= 150.0f + 0.01f);
		}
		// The box gave it the height of all lines.
		CHECK(FUITestApp::RectOf(*Text).Height() == doctest::Approx(Layout.GetSize().Y));
		// Lines break at spaces and cover the text without gaps.
		for (SIZE_T I = 1; I < Layout.GetLines().size(); ++I)
		{
			CHECK(Layout.GetLines()[I].Begin == Layout.GetLines()[I - 1].End);
			CHECK(Text->GetText()[Layout.GetLines()[I].Begin - 1] == ' ');
		}
	}

	TEST_CASE("Text layout: hard line breaks, caret rectangles and hit-testing agree")
	{
		FUITestStyle::EnsureInitialized();
		FSlateFontCache& Fonts = *FUITestStyle::GetFontCache();
		FTextLayout Layout;
		Layout.SetFont(FAppStyle::Get().GetFont("NormalFont"));
		Layout.SetText("Hello\nWorld wide");
		Layout.UpdateIfNeeded(Fonts);
		REQUIRE(Layout.GetLines().size() == 2);
		CHECK(Layout.GetLines()[1].Begin == 6);
		CHECK(Layout.GetLineIndex(8) == 1);
		for (int32 Index : { 0, 2, 5, 6, 9, 16 })
		{
			const FRect Caret = Layout.GetCaretRect(Index, 500.0f);
			CHECK(Layout.HitTest(FVector2(Caret.Left + 0.1f, Caret.GetCenter().Y), 500.0f) == Index);
		}
		CHECK(Layout.GetNextCaretIndex(5) == 6); // across the line break
		CHECK(Layout.GetPreviousCaretIndex(6) == 5);
		TArray<FRect> Rects;
		Layout.GetSelectionRects(3, 8, 500.0f, Rects);
		CHECK(Rects.size() == 2);
	}

	TEST_CASE("Scroll box: scroll bar appears when needed, wheel scrolls, focus scrolls into view")
	{
		FUITestApp T;
		TSharedPtr<SScrollBox> Scroll;
		TArray<TSharedPtr<STestLeaf>> Items(20);
		SAssignNew(Scroll, SScrollBox);
		for (TSharedPtr<STestLeaf>& Item : Items)
		{
			Scroll->AddSlot()[SAssignNew(Item, STestLeaf).Size(FVector2(50.0f, 20.0f))];
		}
		const TSharedRef<SWindow> Window = T.AddWindow(ToSharedRef(Scroll), FVector2(200.0f, 100.0f));
		CHECK(Scroll->GetContentSize() == doctest::Approx(400.0f));
		CHECK(Scroll->GetViewportSize() == doctest::Approx(100.0f));
		// Items are narrower than the window by the scroll bar.
		CHECK(FUITestApp::RectOf(*Items[0]).Width() < 200.0f);

		T.Wheel(Window, FVector2(50.0f, 50.0f), -1.0f);
		CHECK(Scroll->GetScrollOffset() == doctest::Approx(48.0f));
		CHECK(FUITestApp::RectOf(*Items[0]).Top == doctest::Approx(-48.0f));

		Scroll->ScrollToEnd();
		T.Tick();
		CHECK(Scroll->GetScrollOffset() == doctest::Approx(300.0f));
		CHECK(FUITestApp::RectOf(*Items[19]).Bottom == doctest::Approx(100.0f));

		CHECK(Scroll->ScrollDescendantIntoView(*Items[2]));
		T.Tick();
		CHECK(FUITestApp::RectOf(*Items[2]).Top == doctest::Approx(0.0f));
	}

	TEST_CASE("Splitter: dragging a handle moves the space between two slots")
	{
		FUITestApp T;
		TSharedPtr<STestLeaf> A, B;
		const TSharedRef<SWindow> Window = T.AddWindow(SNew(SSplitter)
			+ SSplitter::Slot().Value(1.0f)[SAssignNew(A, STestLeaf)]
			+ SSplitter::Slot().Value(1.0f)[SAssignNew(B, STestLeaf)],
			FVector2(205.0f, 100.0f));
		CHECK(FUITestApp::RectOf(*A).Width() == doctest::Approx(100.0f));
		CHECK(FUITestApp::RectOf(*B).Left == doctest::Approx(105.0f));

		T.MouseMove(Window, FVector2(102.0f, 50.0f));
		T.MouseDown(Window, FVector2(102.0f, 50.0f));
		T.Tick();
		CHECK(T.GetApp().GetMouseCaptor().get() != nullptr);
		T.MouseMove(Window, FVector2(62.5f, 50.0f));
		T.MouseUp(Window, FVector2(62.5f, 50.0f));
		T.Tick();
		CHECK(FUITestApp::RectOf(*A).Width() == doctest::Approx(60.0f));
		CHECK(FUITestApp::RectOf(*B).Width() == doctest::Approx(140.0f));
		CHECK(T.GetApp().GetMouseCaptor() == nullptr);
	}

	TEST_CASE("Widget switcher lays out and hit-tests only the active child")
	{
		FUITestApp T;
		TSharedPtr<SWidgetSwitcher> Switcher;
		TSharedPtr<STestLeaf> A, B;
		const TSharedRef<SWindow> Window = T.AddWindow(SAssignNew(Switcher, SWidgetSwitcher)
			+ SWidgetSwitcher::Slot()[SAssignNew(A, STestLeaf)]
			+ SWidgetSwitcher::Slot()[SAssignNew(B, STestLeaf)]);
		CHECK(&Window->HitTestAt(FVector2(10.0f, 10.0f)).back().Get() == A.get());
		Switcher->SetActiveWidgetIndex(1);
		T.Tick();
		CHECK(&Window->HitTestAt(FVector2(10.0f, 10.0f)).back().Get() == B.get());
		CHECK(B->PaintCount > 0);
	}
}
