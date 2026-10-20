// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// UILayoutTest: the panels. Horizontal and vertical boxes (auto and fill
// sizes), wrap box, grid panel (with spans), overlay, canvas (anchors),
// scale box, splitters (nested), scroll box, and box size overrides.
//
// Self-test: drags a splitter, narrows the window so the wrap box flows
// onto more lines, and scrolls the page.

#include "TestApp.h"
#include "TestWidgets.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SOverlay.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	TSharedRef<SWidget> Cell(const char* Text, const char* Brush = "Recessed")
	{
		return SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush(Brush)).Padding(8.0f)[SNew(STextBlock).Text(Text)];
	}
}

class FLayoutTestApp final : public FTestApp
{
public:
	FLayoutTestApp() : FTestApp({ "UILayoutTest", "Genesis UI - Layout", FVector2(1000.0f, 760.0f) }) {}

protected:
	TSharedRef<SWidget> MakeContent() override
	{
		const TSharedRef<SWrapBox> Chips = SAssignNew(Wrap, SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2(6.0f, 6.0f));
		for (const char* Word : { "worldspawn", "func_door", "light", "info_player_start", "trigger_once", "func_wall", "path_corner", "monster_army", "item_health", "weapon_nailgun" })
		{
			Chips->AddSlot()[SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Recessed")).Padding(FMargin(8.0f, 3.0f))[SNew(STextBlock).Text(Word)]];
		}

		const TSharedRef<SGridPanel> Grid = SNew(SGridPanel);
		const FStyleColor Colors[] = { EStyleColor::Primary, EStyleColor::Success, EStyleColor::Warning, EStyleColor::Error };
		for (int32 I = 0; I < 4; ++I)
		{
			Grid->AddSlot(I % 2, I / 2).Padding(3.0f)[SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Colors[I]).Padding(FMargin(10.0f))
				[SNew(STextBlock).Text(FString::Format("Cell {},{}", I % 2, I / 2)).ColorAndOpacity(FStyleColor(EStyleColor::ForegroundInverted))]];
		}
		Grid->AddSlot(0, 2).ColumnSpan(2).Padding(3.0f)[Cell("Spanning two columns")];
		Grid->SetColumnFill(1, 1.0f);

		return TestFrame(*this, "Layout", SAssignNew(PageBox, SScrollBox)
			+ SScrollBox::Slot().Padding(FMargin(16.0f, 16.0f, 16.0f, 0.0f))
			[
				TestSection("Boxes", "Auto-sized slots take what they need, fill slots share the rest by weight; padding and alignment per slot.",
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)[Cell("auto")]
						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)[Cell("fill 1")]
						+ SHorizontalBox::Slot().FillWidth(2.0f).Padding(2.0f)[Cell("fill 2")]
						+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)[SNew(SBox).WidthOverride(TOptional<float>(120.0f))[Cell("120 wide")]]
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f).HAlign(HAlign_Left)[Cell("left")]
						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f).HAlign(HAlign_Center)[Cell("center")]
						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f).HAlign(HAlign_Right)[Cell("right")]
						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)[SNew(SBox).HeightOverride(TOptional<float>(60.0f))[SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Recessed")).VAlign(VAlign_Bottom).Padding(4.0f)[SNew(STextBlock).Text("bottom of 60")]]]
					])
			]
			+ SScrollBox::Slot().Padding(FMargin(16.0f, 16.0f, 16.0f, 0.0f))[TestSection("Wrap box", "Children flow onto new lines as the width shrinks.", Chips)]
			+ SScrollBox::Slot().Padding(FMargin(16.0f, 16.0f, 16.0f, 0.0f))
			[
				TestSection("Grid, overlay, canvas, scale box", "Panels from docs/GenesisUI.md §6.6.",
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(0.0f, 0.0f, 12.0f, 0.0f))[Grid]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(0.0f, 0.0f, 12.0f, 0.0f))
					[
						SNew(SBox).HeightOverride(TOptional<float>(140.0f))
						[
							SNew(SOverlay)
							+ SOverlay::Slot()[SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Recessed"))]
							+ SOverlay::Slot()
							[
								SNew(SCanvas)
								+ SCanvas::Slot().Anchors(FAnchors(0.0f, 0.0f)).Offset(FMargin(8.0f, 8.0f, 0.0f, 0.0f)).AutoSize(true)[SNew(STextBlock).Text("top-left")]
								+ SCanvas::Slot().Anchors(FAnchors(1.0f, 1.0f)).Alignment(FVector2(1.0f, 1.0f)).Offset(FMargin(-8.0f, -8.0f, 0.0f, 0.0f)).AutoSize(true)[SNew(STextBlock).Text("bottom-right")]
								+ SCanvas::Slot().Anchors(FAnchors(0.5f, 0.5f)).Alignment(FVector2(0.5f, 0.5f)).AutoSize(true)[SNew(SButton).Text("Centered")]
							]
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SBox).HeightOverride(TOptional<float>(140.0f))
						[
							SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
							[
								SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Panel")).Padding(FMargin(6.0f))[SNew(STextBlock).Text("Scaled to fit")]
							]
						]
					])
			]
			+ SScrollBox::Slot().Padding(FMargin(16.0f, 16.0f, 16.0f, 0.0f))
			[
				TestSection("Splitters", "Drag the handles; the inner splitter is vertical.",
					SNew(SBox).HeightOverride(TOptional<float>(180.0f))
					[
						SNew(SSplitter)
						+ SSplitter::Slot().Value(0.3f)[SAssignNew(SplitLeft, SBorder).BorderImage(&FAppStyle::Get().GetBrush("Recessed")).Padding(8.0f)[SNew(STextBlock).Text("Left")]]
						+ SSplitter::Slot().Value(0.7f)
						[
							SNew(SSplitter).Orientation(EOrientation::Vertical)
							+ SSplitter::Slot().Value(0.5f)[Cell("Top right")]
							+ SSplitter::Slot().Value(0.5f)[Cell("Bottom right")]
						]
					])
			]
			+ SScrollBox::Slot().Padding(FMargin(16.0f, 16.0f, 16.0f, 0.0f))
			[
				TestSection("Scroll box", "A scroll box inside the page: the wheel scrolls the innermost one that can still move.",
					SNew(SBox).HeightOverride(TOptional<float>(120.0f))
					[
						SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Recessed")).Padding(1.0f)
						[
							SAssignNew(InnerScroll, SScrollBox)
							+ SScrollBox::Slot().Padding(6.0f)[SNew(STextBlock).Text("Line 1")]
							+ SScrollBox::Slot().Padding(6.0f)[SNew(STextBlock).Text("Line 2")]
							+ SScrollBox::Slot().Padding(6.0f)[SNew(STextBlock).Text("Line 3")]
							+ SScrollBox::Slot().Padding(6.0f)[SNew(STextBlock).Text("Line 4")]
							+ SScrollBox::Slot().Padding(6.0f)[SNew(STextBlock).Text("Line 5")]
							+ SScrollBox::Slot().Padding(6.0f)[SNew(STextBlock).Text("Line 6")]
							+ SScrollBox::Slot().Padding(6.0f)[SNew(STextBlock).Text("Line 7")]
							+ SScrollBox::Slot().Padding(6.0f)[SNew(STextBlock).Text("Line 8")]
						]
					])
			]
			+ SScrollBox::Slot().Padding(FMargin(0.0f, 16.0f, 0.0f, 0.0f))[SNew(SSpacer)]);
	}

	void BuildSelfTest(FTestScript& Script) override
	{
		Script.Capture("start")
			// The wrap box flows onto more lines when the window narrows.
			.Do([this]
			{
				WrapHeight = Wrap->GetCachedGeometry().Size.Y;
				const FIntPoint Pos = GetMainWindow()->GetNativeWindow()->GetClientPosition();
				GetMainWindow()->GetNativeWindow()->ReshapeWindow(Pos.X, Pos.Y, FMath::RoundToInt(560.0f * GetMainWindow()->GetNativeWindow()->GetDpiScale()),
					GetMainWindow()->GetPixelSize().Y);
			})
			.Check("the wrap box is taller in a narrow window", [this] { return Wrap->GetCachedGeometry().Size.Y > WrapHeight + 1.0f; }, 10)
			.Capture("narrow")
			.Do([this]
			{
				const FIntPoint Pos = GetMainWindow()->GetNativeWindow()->GetClientPosition();
				GetMainWindow()->GetNativeWindow()->ReshapeWindow(Pos.X, Pos.Y, FMath::RoundToInt(1000.0f * GetMainWindow()->GetNativeWindow()->GetDpiScale()),
					GetMainWindow()->GetPixelSize().Y);
			})
			// Scroll the page to the splitters, then drag the outer handle right.
			.Do([this] { PageBox->ScrollDescendantIntoView(*SplitLeft); }, 10)
			.Do([this]
			{
				LeftWidth = SplitLeft->GetCachedGeometry().Size.X;
				const FRect Left = SplitLeft->GetCachedGeometry().GetLayoutBoundingRect();
				const float Scale = SplitLeft->GetCachedGeometry().Scale;
				const FVector2 Handle(Left.Right + 2.0f * Scale, Left.GetCenter().Y);
				Drag(Handle, Handle + FVector2(150.0f * Scale, 0.0f));
			}, 10)
			.Check("dragging the splitter widens the left side", [this] { return SplitLeft->GetCachedGeometry().Size.X > LeftWidth + 50.0f; })
			.Capture("splitter")
			// The inner scroll box takes the wheel.
			.Do([this] { PageBox->ScrollDescendantIntoView(*InnerScroll); })
			.Do([this] { Wheel(GetCenter(*InnerScroll), -3.0f); }, 10)
			.Check("the wheel scrolls the inner scroll box", [this] { return InnerScroll->GetScrollOffset() > 0.0f; }, 10)
			.Capture("scrolled");
	}

private:
	TSharedPtr<SScrollBox> PageBox;
	TSharedPtr<SScrollBox> InnerScroll;
	TSharedPtr<SWrapBox> Wrap;
	TSharedPtr<SBorder> SplitLeft;
	float WrapHeight = 0.0f;
	float LeftWidth = 0.0f;
};

GENESIS_TEST_APP(FLayoutTestApp)
