// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// UIDragDropTest: drag and drop. Swatches start a drag (with a decorator
// following the pointer and a cursor of its own); a drop zone highlights
// while a swatch is over it and takes its color.
//
// Self-test: drags a swatch onto the zone, shows the drag in progress, and
// cancels a drag with Escape.

#include "TestApp.h"
#include "TestWidgets.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	// A drag-and-drop payload: a color swatch.
	class FSwatchDragDrop final : public FDragDropOperation
	{
	public:
		DRAG_DROP_OPERATOR_TYPE(FSwatchDragDrop, FDragDropOperation)
		FSlateColor Color;
		FString Name;

		TSharedPtr<SWidget> GetDefaultDecorator() const override
		{
			return SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("DragDecorator")).Padding(FMargin(8.0f, 4.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(TOptional<float>(14.0f)).HeightOverride(TOptional<float>(14.0f))[SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Color)]]
				+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))[SNew(STextBlock).Text(FString("Dragging ") + Name)]
			];
		}
		TOptional<EMouseCursor> GetCursor() const override { return EMouseCursor::GrabHand; }
	};

	// A swatch you can drag.
	class SSwatch final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SSwatch)
			SLATE_ARGUMENT(FSlateColor, Color, FSlateColor(FColor::White()))
			SLATE_ARGUMENT(FString, Name, "")
		SLATE_END_ARGS()

		void Construct(const FArguments& Args)
		{
			Color = Args._Color;
			Name = Args._Name;
			SetToolTipText(FString::Format("Drag {} onto the drop zone", Name));
		}

		FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event) override
		{
			return FReply::Handled().DetectDrag(SharedThis(this), Event.GetEffectingButton());
		}
		FReply OnDragDetected(const FGeometry&, const FPointerEvent&) override
		{
			auto Operation = std::make_shared<FSwatchDragDrop>();
			Operation->Color = Color;
			Operation->Name = Name;
			return FReply::Handled().BeginDragDrop(TSharedRef<FDragDropOperation>(StaticCastSharedPtr<FDragDropOperation>(Operation)));
		}
		FCursorReply OnCursorQuery(const FGeometry&, const FPointerEvent&) const override { return FCursorReply::Cursor(EMouseCursor::Hand); }

	protected:
		FVector2 ComputeDesiredSize(float) const override { return FVector2(40.0f, 40.0f); }
		int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FRect&, FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& Style) const override
		{
			Out.AddRoundedBox(LayerId, Geometry.ToPaintGeometry(), Style.Tinted(Color.Resolve()), 6.0f,
				IsHovered() ? FSlateColor(EStyleColor::ForegroundHover).Resolve() : FColor::Transparent(), 2.0f);
			return LayerId;
		}

	private:
		FSlateColor Color;
		FString Name;
	};

	// Accepts swatches and takes their color.
	class SDropZone final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SDropZone)
			SLATE_EVENT(TDelegate<void(const FString&)>, OnDropped)
		SLATE_END_ARGS()

		void Construct(const FArguments& Args) { OnDropped = Args._OnDropped; }

		bool IsHighlighted() const { return bHighlight; }

		void OnDragEnter(const FGeometry&, const FDragDropEvent& Event) override
		{
			bHighlight = Event.GetOperationAs<FSwatchDragDrop>() != nullptr;
			Invalidate(EInvalidateWidgetReason::Paint);
		}
		void OnDragLeave(const FDragDropEvent&) override
		{
			bHighlight = false;
			Invalidate(EInvalidateWidgetReason::Paint);
		}
		FReply OnDrop(const FGeometry&, const FDragDropEvent& Event) override
		{
			bHighlight = false;
			if (const TSharedPtr<FSwatchDragDrop> Op = Event.GetOperationAs<FSwatchDragDrop>())
			{
				Fill = Op->Color;
				Invalidate(EInvalidateWidgetReason::Paint);
				OnDropped.ExecuteIfBound(Op->Name);
				return FReply::Handled();
			}
			return FReply::Unhandled();
		}

	protected:
		FVector2 ComputeDesiredSize(float) const override { return FVector2(160.0f, 90.0f); }
		int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FRect&, FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& Style) const override
		{
			Out.AddRoundedBox(LayerId, Geometry.ToPaintGeometry(), Style.Tinted(Fill.Resolve()), 8.0f,
				Style.Tinted((bHighlight ? FSlateColor(EStyleColor::Primary) : FSlateColor(EStyleColor::Border)).Resolve()), bHighlight ? 3.0f : 1.5f);
			return LayerId;
		}

	private:
		TDelegate<void(const FString&)> OnDropped;
		FSlateColor Fill = FSlateColor(EStyleColor::Recessed);
		bool bHighlight = false;
	};
}

class FDragDropTestApp final : public FTestApp
{
public:
	FDragDropTestApp() : FTestApp({ "UIDragDropTest", "Genesis UI - Drag and drop", FVector2(800.0f, 420.0f) }) {}

protected:
	TSharedRef<SWidget> MakeContent() override
	{
		const TSharedRef<SHorizontalBox> Swatches = SNew(SHorizontalBox);
		const TPair<const char*, EStyleColor> SwatchColors[] = { { "blue", EStyleColor::Primary }, { "green", EStyleColor::Success }, { "orange", EStyleColor::Warning }, { "red", EStyleColor::Error } };
		for (const auto& [Name, Color] : SwatchColors)
		{
			TSharedPtr<SSwatch> Swatch;
			Swatches->AddSlot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 8.0f, 0.0f))[SAssignNew(Swatch, SSwatch).Name(Name).Color(FSlateColor(Color))];
			SwatchWidgets.push_back(Swatch);
		}
		return TestFrame(*this, "Drag and drop", TestPage({
			TestSection("Drag and drop", "Drag a swatch onto the zone; Escape cancels a drag.",
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Swatches]
				+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(24.0f, 0.0f, 0.0f, 0.0f))
				[
					SAssignNew(Zone, SDropZone).OnDropped([this](const FString& Name) { ++Drops; SetLastEvent(FString::Format("Dropped {}", Name)); })
				]),
		}));
	}

	void BuildSelfTest(FTestScript& Script) override
	{
		Script.Capture("start")
			// Green onto the zone, in steps so the drag in progress can be seen.
			.Do([this] { MouseDown(GetCenter(*SwatchWidgets[1])); })
			.Do([this] { MoveMouse(GetCenter(*SwatchWidgets[1]) + FVector2(30.0f, 10.0f)); })
			.Do([this] { MoveMouse(GetCenter(*Zone)); })
			.Check("a drag is in progress", [this] { return GetApp().IsDragDropping(); })
			.Check("the zone highlights under a swatch", [this] { return Zone->IsHighlighted(); })
			.Capture("dragging")
			.Do([this] { MouseUp(GetCenter(*Zone)); })
			.Check("dropping green on the zone", [this] { return !GetApp().IsDragDropping() && Drops == 1 && GetLastEvent() == "Dropped green"; })
			.Capture("dropped")
			// Red, cancelled with Escape over the zone.
			.Do([this] { MouseDown(GetCenter(*SwatchWidgets[3])); })
			.Do([this] { MoveMouse(GetCenter(*SwatchWidgets[3]) + FVector2(30.0f, 10.0f)); MoveMouse(GetCenter(*Zone)); })
			.Do([this] { PressKey(EKey::Escape); })
			.Check("Escape cancels the drag", [this] { return !GetApp().IsDragDropping(); })
			.Do([this] { MouseUp(GetCenter(*Zone)); })
			.Check("a cancelled drag drops nothing", [this] { return Drops == 1 && !Zone->IsHighlighted(); });
	}

private:
	TArray<TSharedPtr<SSwatch>> SwatchWidgets;
	TSharedPtr<SDropZone> Zone;
	int32 Drops = 0;
};

GENESIS_TEST_APP(FDragDropTestApp)
