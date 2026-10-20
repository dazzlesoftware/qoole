// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Widgets/Input/SColorPicker.h"

#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

// ---------------------------------------------------------------------------
// SColorBlock

void SColorBlock::Construct(const FArguments& Args)
{
	Color.Assign(Args._Color);
	IsUndetermined.Assign(Args._IsUndetermined);
	Size = Args._Size;
	CornerRadius = Args._CornerRadius;
	OnClick = Args._OnMouseButtonDown;
}

FReply SColorBlock::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	return OnClick.IsBound() ? OnClick.Execute(Geometry, Event) : FReply::Unhandled();
}

int32 SColorBlock::OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FRect&, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	const FVector2 S = Geometry.Size;
	const FColor Border = InWidgetStyle.Tinted(FSlateColor(EStyleColor::Border).Resolve());
	if (IsUndetermined.Get())
	{
		// Diagonal stripes of grey: "several values".
		OutDrawElements.AddRoundedBox(LayerId, Geometry.ToPaintGeometry(), InWidgetStyle.Tinted(FSlateColor(EStyleColor::Input).Resolve()), CornerRadius, Border, 1.0f);
		OutDrawElements.AddLines(LayerId + 1, Geometry.ToPaintGeometry(), { FVector2(4.0f, S.Y - 3.0f), FVector2(S.X - 4.0f, 3.0f) },
			InWidgetStyle.Tinted(FSlateColor(EStyleColor::ForegroundSecondary).Resolve()), 1.5f);
		return LayerId + 1;
	}
	const FColor C = Color.Get();
	// Left half opaque; right half with alpha over a checkerboard.
	const float Half = FMath::FloorToFloat(S.X * 0.5f);
	OutDrawElements.AddSolidBox(LayerId, Geometry.ToPaintGeometry(FRect(0.0f, 0.0f, Half, S.Y)), InWidgetStyle.Tinted(C.WithAlpha(255)));
	const float Cell = FMath::Max(4.0f, S.Y * 0.5f);
	for (float Y = 0.0f; Y < S.Y; Y += Cell)
	{
		for (float X = Half; X < S.X; X += Cell)
		{
			const bool bDark = static_cast<int32>((X - Half) / Cell + Y / Cell) % 2 == 0;
			OutDrawElements.AddSolidBox(LayerId, Geometry.ToPaintGeometry(FRect(X, Y, FMath::Min(X + Cell, S.X), FMath::Min(Y + Cell, S.Y))),
				InWidgetStyle.Tinted(bDark ? FColor(90, 90, 90) : FColor(160, 160, 160)));
		}
	}
	OutDrawElements.AddSolidBox(LayerId + 1, Geometry.ToPaintGeometry(FRect(Half, 0.0f, S.X, S.Y)), InWidgetStyle.Tinted(C));
	OutDrawElements.AddRoundedBox(LayerId + 2, Geometry.ToPaintGeometry(), FColor::Transparent(), CornerRadius, Border, 1.0f);
	return LayerId + 2;
}

// ---------------------------------------------------------------------------
// SColorPicker

FString SColorPicker::ToHex(FColor C, bool bWithAlpha)
{
	return bWithAlpha ? FString::Format("{:02X}{:02X}{:02X}{:02X}", C.R, C.G, C.B, C.A) : FString::Format("{:02X}{:02X}{:02X}", C.R, C.G, C.B);
}

TOptional<FColor> SColorPicker::FromHex(const FString& Hex)
{
	std::string S = Hex.TrimStartAndEnd().ToStdString();
	if (!S.empty() && S[0] == '#')
	{
		S.erase(0, 1);
	}
	if (S.size() != 6 && S.size() != 8)
	{
		return {};
	}
	uint32 Value = 0;
	for (const char C : S)
	{
		Value <<= 4;
		if (C >= '0' && C <= '9') Value |= static_cast<uint32>(C - '0');
		else if (C >= 'a' && C <= 'f') Value |= static_cast<uint32>(C - 'a' + 10);
		else if (C >= 'A' && C <= 'F') Value |= static_cast<uint32>(C - 'A' + 10);
		else return {};
	}
	return S.size() == 6 ? FColor::FromHex(Value) : FColor::FromHexRGBA(Value);
}

void SColorPicker::SetColor(FColor NewColor, bool bNotify)
{
	if (NewColor == Color)
	{
		return;
	}
	Color = NewColor;
	Invalidate(EInvalidateWidgetReason::Paint);
	if (bNotify)
	{
		OnColorChanged.ExecuteIfBound(Color);
	}
}

TSharedRef<SWidget> SColorPicker::MakeChannel(const char* Label, int32 Channel, FSlateColor BarColor)
{
	const auto Get = [this, Channel]() -> uint8
	{
		switch (Channel)
		{
		case 0: return Color.R;
		case 1: return Color.G;
		case 2: return Color.B;
		default: return Color.A;
		}
	};
	const auto Set = [this, Channel](int32 Value)
	{
		FColor C = Color;
		const uint8 V = static_cast<uint8>(FMath::Clamp(Value, 0, 255));
		(Channel == 0 ? C.R : Channel == 1 ? C.G : Channel == 2 ? C.B : C.A) = V;
		SetColor(C, true);
	};
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 8.0f, 0.0f))[SNew(SBox).WidthOverride(TOptional<float>(14.0f))[SNew(STextBlock).Text(Label)]]
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(SSlider).MinValue(0.0f).MaxValue(255.0f).StepSize(1.0f).BarColor(BarColor)
			.Value([Get] { return static_cast<float>(Get()); })
			.OnValueChanged([Set](float V) { Set(FMath::RoundToInt(V)); })
			.OnMouseCaptureBegin([this] { OnInteractionBegin.ExecuteIfBound(); })
			.OnMouseCaptureEnd([this] { OnInteractionEnd.ExecuteIfBound(); })
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))
		[
			SNew(SBox).WidthOverride(TOptional<float>(56.0f))
			[
				SNew(SSpinBox<int32>).MinValue(TOptional<int32>(0)).MaxValue(TOptional<int32>(255)).MinDesiredWidth(30.0f)
				.Value([Get] { return TOptional<int32>(Get()); })
				.OnBeginSliderMovement([this] { OnInteractionBegin.ExecuteIfBound(); })
				.OnEndSliderMovement([this](int32) { OnInteractionEnd.ExecuteIfBound(); })
				.OnValueChanged([Set](int32 V) { Set(V); })
			]
		];
}

void SColorPicker::Construct(const FArguments& Args)
{
	Color = Args._TargetColor.Get();
	bUseAlpha = Args._UseAlpha;
	OnColorChanged = Args._OnColorChanged;
	OnInteractionBegin = Args._OnInteractionBegin;
	OnInteractionEnd = Args._OnInteractionEnd;

	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 8.0f))
		[
			SNew(SColorBlock).Size(FVector2(240.0f, 36.0f)).CornerRadius(4.0f).Color([this] { return Color; })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 2.0f))[MakeChannel("R", 0, FSlateColor(FColor(235, 80, 80)))]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 2.0f))[MakeChannel("G", 1, FSlateColor(FColor(90, 210, 110)))]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 2.0f))[MakeChannel("B", 2, FSlateColor(FColor(90, 150, 255)))];
	if (bUseAlpha)
	{
		Box->AddSlot().AutoHeight().Padding(FMargin(0.0f, 2.0f))[MakeChannel("A", 3, FSlateColor(EStyleColor::ForegroundSecondary))];
	}
	Box->AddSlot().AutoHeight().Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 8.0f, 0.0f))[SNew(STextBlock).Text("Hex")]
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		[
			SNew(SEditableTextBox).Text([this] { return ToHex(Color, bUseAlpha); }).SelectAllTextWhenFocused(true)
			.OnTextCommitted([this](const FString& Text, ETextCommit How)
			{
				if (How == ETextCommit::OnCleared)
				{
					return;
				}
				if (const TOptional<FColor> Parsed = FromHex(Text))
				{
					OnInteractionBegin.ExecuteIfBound();
					SetColor(bUseAlpha ? *Parsed : Parsed->WithAlpha(Color.A), true);
					OnInteractionEnd.ExecuteIfBound();
				}
			})
		]
	];
	ChildSlot[SNew(SBox).WidthOverride(TOptional<float>(260.0f))[Box]];
}
