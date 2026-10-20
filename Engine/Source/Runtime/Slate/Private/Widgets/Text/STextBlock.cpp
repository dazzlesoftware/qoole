// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Widgets/Text/STextBlock.h"

void STextBlock::Construct(const FArguments& Args)
{
	bExplicitStyle = Args._TextStyle != nullptr;
	TextStyle = Args._TextStyle ? Args._TextStyle : &FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");
	Text.Assign(Args._Text);
	if (Args._Font.IsSet())
	{
		FontOverride = Args._Font.Get();
	}
	if (Args._ColorAndOpacity.IsSet())
	{
		const TAttribute<FSlateColor> Color = Args._ColorAndOpacity;
		ColorAndOpacity.Assign(Color.IsBound() ? TAttribute<TOptional<FSlateColor>>([Color] { return TOptional<FSlateColor>(Color.Get()); })
			: TAttribute<TOptional<FSlateColor>>(TOptional<FSlateColor>(Color.Get())));
	}
	Justification = Args._Justification;
	bAutoWrap = Args._AutoWrapText;
	WrapTextAt.Assign(Args._WrapTextAt);
	MinDesiredWidth.Assign(Args._MinDesiredWidth);
	Margin = Args._Margin;
	Layout.SetJustification(Justification);
}

void STextBlock::SetFont(const FSlateFontInfo& InFont)
{
	FontOverride = InFont;
	Invalidate(EInvalidateWidgetReason::Layout);
}

void STextBlock::SetAutoWrapText(bool bWrap)
{
	if (bAutoWrap != bWrap)
	{
		bAutoWrap = bWrap;
		Invalidate(EInvalidateWidgetReason::Layout);
	}
}

FSlateFontInfo STextBlock::GetEffectiveFont() const
{
	return FontOverride ? *FontOverride : TextStyle->Font;
}

float STextBlock::GetEffectiveWrapWidth() const
{
	if (WrapTextAt.Get() > 0.0f)
	{
		return WrapTextAt.Get();
	}
	if (bAutoWrap && ArrangedWidth > 0.0f)
	{
		return FMath::Max(1.0f, ArrangedWidth - Margin.Left - Margin.Right);
	}
	return 0.0f;
}

FVector2 STextBlock::ComputeDesiredSize(float LayoutScale) const
{
	const FSlateFontInfo Font = GetEffectiveFont();
	if (!Font.Font || !FSlateApplicationBase::IsInitialized())
	{
		return Margin.GetDesiredSize();
	}
	Layout.SetText(Text.Get());
	Layout.SetFont(Font);
	Layout.SetScale(LayoutScale);
	Layout.SetWrapWidth(GetEffectiveWrapWidth());
	Layout.UpdateIfNeeded(FSlateApplicationBase::Get().GetFontCache());
	FVector2 Size = Layout.GetSize() + Margin.GetDesiredSize();
	Size.X = FMath::Max(Size.X, MinDesiredWidth.Get());
	return Size;
}

void STextBlock::OnPreArrange(const FGeometry& AllottedGeometry)
{
	if (bAutoWrap && WrapTextAt.Get() <= 0.0f && !FMath::IsNearlyEqual(ArrangedWidth, AllottedGeometry.Size.X, 0.01f))
	{
		ArrangedWidth = AllottedGeometry.Size.X;
		// Wrapping at the new width changes the desired height.
		if (!FMath::IsNearlyEqual(Layout.GetWrapWidth(), GetEffectiveWrapWidth(), 0.01f))
		{
			Invalidate(EInvalidateWidgetReason::Layout);
		}
	}
}

int32 STextBlock::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect&, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	if (!FSlateApplicationBase::IsInitialized() || Layout.GetLines().empty())
	{
		return LayerId;
	}
	FColor Color;
	if (!Args.bEnabled)
	{
		Color = InWidgetStyle.Tinted(FSlateColor(EStyleColor::ForegroundDisabled).Resolve());
	}
	else if (const TOptional<FSlateColor>& Explicit = ColorAndOpacity.Get())
	{
		Color = InWidgetStyle.Tinted(Explicit->Resolve());
	}
	else if (bExplicitStyle)
	{
		Color = InWidgetStyle.Tinted(TextStyle->ColorAndOpacity.Resolve());
	}
	else
	{
		Color = InWidgetStyle.GetForegroundColor();
	}

	FSlateFontCache& FontCache = FSlateApplicationBase::Get().GetFontCache();
	const float AreaWidth = Geometry.Size.X - Margin.Left - Margin.Right;
	const FVector2 Offset(Margin.Left, Margin.Top);
	if (TextStyle->ShadowOffset.X != 0.0f || TextStyle->ShadowOffset.Y != 0.0f)
	{
		Layout.Paint(FontCache, OutDrawElements, LayerId, Geometry, Offset + TextStyle->ShadowOffset, AreaWidth, InWidgetStyle.Tinted(TextStyle->ShadowColor.Resolve()));
	}
	Layout.Paint(FontCache, OutDrawElements, LayerId + 1, Geometry, Offset, AreaWidth, Color);
	return LayerId + 1;
}
