// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// SSpinBox<T>: a number you drag (left/right; Shift x10, Ctrl x0.1) or type
// (click without dragging, or Tab into it). An unset value shows "Multiple
// Values", for property grids editing several objects at once.
// SNumericEntryBox<T> is the same widget.

#pragma once

#include "Framework/Application/SlateApplication.h"
#include "Widgets/Text/SEditableText.h"

#include <charconv>
#include <cstdlib>

template <typename NumericType>
	requires std::is_arithmetic_v<NumericType>
class SSpinBox : public SCompoundWidget
{
public:
	using FOnValueChanged = TDelegate<void(NumericType)>;
	using FOnValueCommitted = TDelegate<void(NumericType, ETextCommit)>;
	static constexpr bool bIsInteger = std::is_integral_v<NumericType>;

	SLATE_BEGIN_ARGS(SSpinBox)
		SLATE_STYLE_ARGUMENT(FSpinBoxStyle, Style)
		SLATE_ATTRIBUTE(TOptional<NumericType>, Value, TOptional<NumericType>(NumericType{}))
		SLATE_ARGUMENT(TOptional<NumericType>, MinValue, TOptional<NumericType>())
		SLATE_ARGUMENT(TOptional<NumericType>, MaxValue, TOptional<NumericType>())
		SLATE_ARGUMENT(float, Delta, 0.0f)            // value per logical unit dragged; 0 picks one
		SLATE_ARGUMENT(int32, FractionalDigits, 3)    // shown for floating-point values
		SLATE_ATTRIBUTE(float, MinDesiredWidth, 60.0f)
		SLATE_ARGUMENT(FString, UndeterminedText, "Multiple Values")
		SLATE_EVENT(FOnValueChanged, OnValueChanged)      // every change, including while dragging
		SLATE_EVENT(FOnValueCommitted, OnValueCommitted)  // typed and committed, or a drag ended
		SLATE_EVENT(FSimpleDelegate, OnBeginSliderMovement)
		SLATE_EVENT(FOnValueChanged, OnEndSliderMovement)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		Style = Args._Style ? Args._Style : &FAppStyle::Get().GetWidgetStyle<FSpinBoxStyle>("SpinBox");
		ValueAttribute.Assign(Args._Value);
		MinValue = Args._MinValue;
		MaxValue = Args._MaxValue;
		Delta = Args._Delta;
		if (Delta <= 0.0f)
		{
			Delta = MinValue && MaxValue ? static_cast<float>(*MaxValue - *MinValue) / 200.0f : (bIsInteger ? 0.2f : 0.05f);
		}
		FractionalDigits = Args._FractionalDigits;
		UndeterminedText = Args._UndeterminedText;
		OnValueChanged = Args._OnValueChanged;
		OnValueCommitted = Args._OnValueCommitted;
		OnBeginSliderMovement = Args._OnBeginSliderMovement;
		OnEndSliderMovement = Args._OnEndSliderMovement;

		ChildSlot.Padding(Style->Padding).VAlign(VAlign_Center)
		[
			SAssignNew(Text, SEditableText)
			.Style(&Style->Text)
			.Text([this] { return FormatValue(); })
			.SelectAllTextWhenFocused(true)
			.RevertTextOnEscape(true)
			.MinDesiredWidth(Args._MinDesiredWidth)
			.OnTextCommitted([this](const FString& Typed, ETextCommit How)
			{
				if (How == ETextCommit::OnCleared)
				{
					return;
				}
				if (const TOptional<NumericType> Parsed = ParseValue(Typed))
				{
					CommitValue(*Parsed, true, How);
				}
			})
		];
	}

	TOptional<NumericType> GetValue() const { return ValueAttribute.Get(); }
	void SetValue(TAttribute<TOptional<NumericType>> InValue) { ValueAttribute.Assign(InValue); }
	TSharedRef<SEditableText> GetEditableText() const { return ToSharedRef(Text); }
	bool IsDragging() const { return bDragging; }

	FReply OnPreviewMouseButtonDown(const FGeometry&, const FPointerEvent& Event) override
	{
		if (Event.GetEffectingButton() != EMouseButton::Left || Text->HasKeyboardFocus())
		{
			return FReply::Unhandled(); // typing: let the text handle the mouse
		}
		bPressed = true;
		bDragging = false;
		LastSetValue.reset();
		PressPosition = Event.GetAbsolutePosition();
		DragValue = static_cast<double>(ValueAttribute.Get().value_or(NumericType{}));
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		if (!bPressed)
		{
			return FReply::Unhandled();
		}
		const float Scale = FMath::Max(Geometry.Scale, 0.01f);
		const float Moved = (Event.GetAbsolutePosition().X - PressPosition.X) / Scale;
		if (!bDragging && FMath::Abs(Moved) >= 3.0f)
		{
			bDragging = true;
			LastDragX = Event.GetAbsolutePosition().X;
			OnBeginSliderMovement.ExecuteIfBound();
		}
		if (bDragging)
		{
			const float Step = (Event.GetAbsolutePosition().X - LastDragX) / Scale;
			LastDragX = Event.GetAbsolutePosition().X;
			const float Multiplier = Event.IsShiftDown() ? 10.0f : (Event.IsControlDown() ? 0.1f : 1.0f);
			DragValue += static_cast<double>(Step * Delta * Multiplier);
			DragValue = Clamp(DragValue);
			CommitValue(Convert(DragValue), false, ETextCommit::Default);
		}
		return FReply::Handled();
	}

	FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event) override
	{
		if (!bPressed || Event.GetEffectingButton() != EMouseButton::Left)
		{
			return FReply::Unhandled();
		}
		bPressed = false;
		FReply Reply = FReply::Handled().ReleaseMouseCapture();
		if (bDragging)
		{
			bDragging = false;
			// A bound value refreshes at the next frame; report what we set.
			const NumericType Final = LastSetValue.value_or(ValueAttribute.Get().value_or(NumericType{}));
			OnEndSliderMovement.ExecuteIfBound(Final);
			OnValueCommitted.ExecuteIfBound(Final, ETextCommit::Default);
		}
		else
		{
			// A click without a drag starts typing.
			Reply.SetUserFocus(ToSharedRef(StaticCastSharedPtr<SWidget>(Text)), EFocusCause::SetDirectly);
		}
		return Reply;
	}

	void OnMouseCaptureLost() override
	{
		if (bDragging)
		{
			bDragging = false;
			OnEndSliderMovement.ExecuteIfBound(ValueAttribute.Get().value_or(NumericType{}));
		}
		bPressed = false;
	}

	FCursorReply OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const override
	{
		if (Text->HasKeyboardFocus())
		{
			return SCompoundWidget::OnCursorQuery(Geometry, Event);
		}
		return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
	}

	EWindowZone GetWindowZoneOverride() const override { return EWindowZone::ClientArea; }

	// Text for a value (and back), as shown in the box.
	FString FormatValue() const
	{
		const TOptional<NumericType> Value = ValueAttribute.Get();
		if (!Value)
		{
			return UndeterminedText;
		}
		if constexpr (bIsInteger)
		{
			return FString::Format("{}", *Value);
		}
		else
		{
			std::string S = std::format("{:.{}f}", static_cast<double>(*Value), FMath::Clamp(FractionalDigits, 0, 9));
			if (S.find('.') != std::string::npos)
			{
				while (!S.empty() && S.back() == '0')
				{
					S.pop_back();
				}
				if (!S.empty() && S.back() == '.')
				{
					S.pop_back();
				}
			}
			if (S == "-0")
			{
				S = "0";
			}
			return FString(S);
		}
	}

	static TOptional<NumericType> ParseValue(const FString& Typed)
	{
		const std::string S = Typed.TrimStartAndEnd().ToStdString();
		if (S.empty())
		{
			return {};
		}
		char* End = nullptr;
		const double Parsed = std::strtod(S.c_str(), &End);
		if (End != S.c_str() + S.size())
		{
			return {};
		}
		return Convert(Parsed);
	}

protected:
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override
	{
		const bool bActive = bDragging || Text->HasKeyboardFocus();
		const FSlateBrush& Brush = bActive ? Style->Active : (IsHovered() ? Style->Hovered : Style->Normal);
		Brush.Paint(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), InWidgetStyle.ColorAndOpacityTint);
		const TOptional<NumericType> Value = ValueAttribute.Get();
		if (MinValue && MaxValue && Value && *MaxValue > *MinValue && !Text->HasKeyboardFocus())
		{
			const float T = FMath::Clamp(static_cast<float>((static_cast<double>(*Value) - *MinValue) / (static_cast<double>(*MaxValue) - *MinValue)), 0.0f, 1.0f);
			if (T > 0.0f)
			{
				Style->Fill.Paint(OutDrawElements, LayerId + 1, Geometry.ToPaintGeometry(FRect(0.0f, 0.0f, Geometry.Size.X * T, Geometry.Size.Y)), InWidgetStyle.ColorAndOpacityTint);
			}
		}
		FWidgetStyle ChildStyle = MakeChildStyle(InWidgetStyle);
		if (!Value)
		{
			ChildStyle.BlendColorAndOpacityTint(Style->UndeterminedColor.Resolve());
		}
		return PaintArrangedChildren(Args, CullingRect, OutDrawElements, LayerId + 2, ChildStyle);
	}

private:
	double Clamp(double V) const
	{
		if (MinValue)
		{
			V = FMath::Max(V, static_cast<double>(*MinValue));
		}
		if (MaxValue)
		{
			V = FMath::Min(V, static_cast<double>(*MaxValue));
		}
		return V;
	}

	static NumericType Convert(double V)
	{
		if constexpr (bIsInteger)
		{
			return static_cast<NumericType>(std::llround(V));
		}
		else
		{
			return static_cast<NumericType>(V);
		}
	}

	void CommitValue(NumericType NewValue, bool bCommitted, ETextCommit How)
	{
		NewValue = Convert(Clamp(static_cast<double>(NewValue)));
		const TOptional<NumericType> Old = bDragging && LastSetValue ? LastSetValue : ValueAttribute.Get();
		LastSetValue = NewValue;
		if (!ValueAttribute.IsBound())
		{
			ValueAttribute.Set(NewValue);
		}
		if (!Old || *Old != NewValue)
		{
			OnValueChanged.ExecuteIfBound(NewValue);
		}
		if (bCommitted)
		{
			OnValueCommitted.ExecuteIfBound(NewValue, How);
		}
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	const FSpinBoxStyle* Style = nullptr;
	TWidgetAttribute<TOptional<NumericType>, EInvalidateWidgetReason::Paint> ValueAttribute{ *this };
	TOptional<NumericType> MinValue;
	TOptional<NumericType> MaxValue;
	float Delta = 0.1f;
	int32 FractionalDigits = 3;
	FString UndeterminedText;
	FOnValueChanged OnValueChanged;
	FOnValueCommitted OnValueCommitted;
	FSimpleDelegate OnBeginSliderMovement;
	FOnValueChanged OnEndSliderMovement;
	TSharedPtr<SEditableText> Text;
	bool bPressed = false;
	bool bDragging = false;
	FVector2 PressPosition;
	float LastDragX = 0.0f;
	double DragValue = 0.0;
	TOptional<NumericType> LastSetValue; // during a drag or commit
};

template <typename NumericType>
using SNumericEntryBox = SSpinBox<NumericType>;
