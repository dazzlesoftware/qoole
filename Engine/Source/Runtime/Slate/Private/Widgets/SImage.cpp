// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Widgets/SImage.h"

// ---------------------------------------------------------------------------
// SImage

void SImage::Construct(const FArguments& Args)
{
	Image = Args._Image;
	ColorAndOpacity.Assign(Args._ColorAndOpacity);
	if (Args._DesiredSizeOverride.IsSet())
	{
		DesiredSizeOverride = Args._DesiredSizeOverride.Get();
	}
}

void SImage::SetImage(const FStyleBrush* InImage)
{
	if (Image != InImage)
	{
		Image = InImage;
		Invalidate(EInvalidateWidgetReason::Layout);
	}
}

FVector2 SImage::ComputeDesiredSize(float) const
{
	if (DesiredSizeOverride)
	{
		return *DesiredSizeOverride;
	}
	return Image ? Image->GetImageSize() : FVector2::Zero();
}

int32 SImage::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect&, FDrawElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	if (Image && Image->IsVisible())
	{
		FColor Tint = InWidgetStyle.Tinted(ColorAndOpacity.Get().Resolve());
		if (!Args.bEnabled)
		{
			Tint.A = uint8(Tint.A / 2);
		}
		Image->Paint(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), Tint);
	}
	return LayerId;
}

// ---------------------------------------------------------------------------
// SSeparator

void SSeparator::Construct(const FArguments& Args)
{
	Orientation = Args._Orientation;
	Style = Args._Style ? Args._Style : &FAppStyle::Get().GetWidgetStyle<FSeparatorStyle>("Separator");
	Padding = Args._Padding;
}

FVector2 SSeparator::ComputeDesiredSize(float) const
{
	const float Cross = Style->Thickness + Padding * 2.0f;
	return Orientation == EOrientation::Horizontal ? FVector2(0.0f, Cross) : FVector2(Cross, 0.0f);
}

int32 SSeparator::OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FRect&, FDrawElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	const FRect Line = Orientation == EOrientation::Horizontal
		? FRect(0.0f, (Geometry.Size.Y - Style->Thickness) * 0.5f, Geometry.Size.X, (Geometry.Size.Y + Style->Thickness) * 0.5f)
		: FRect((Geometry.Size.X - Style->Thickness) * 0.5f, 0.0f, (Geometry.Size.X + Style->Thickness) * 0.5f, Geometry.Size.Y);
	OutDrawElements.AddSolidBox(LayerId, Geometry.ToPaintGeometry(Line), InWidgetStyle.Tinted(Style->Color.Resolve()));
	return LayerId;
}

// ---------------------------------------------------------------------------
// SProgressBar

void SProgressBar::Construct(const FArguments& Args)
{
	Style = Args._Style ? Args._Style : &FAppStyle::Get().GetWidgetStyle<FProgressBarStyle>("ProgressBar");
	if (Args._FillColor.IsSet())
	{
		FillColor = Args._FillColor.Get();
	}
	SetPercent(Args._Percent);
}

void SProgressBar::SetPercent(TAttribute<TOptional<float>> InPercent)
{
	Percent.Assign(InPercent);
	// The busy animation repaints every frame.
	SetVolatile(!Percent.Get().has_value() || Percent.IsBound());
}

FVector2 SProgressBar::ComputeDesiredSize(float) const
{
	return FVector2(Style->Thickness * 4.0f, Style->Thickness);
}

int32 SProgressBar::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect&, FDrawElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	Style->Background.Paint(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), InWidgetStyle.ColorAndOpacityTint);
	const float Width = Geometry.Size.X;
	FRect Fill;
	if (const TOptional<float> Value = Percent.Get())
	{
		Fill = FRect(0.0f, 0.0f, Width * FMath::Clamp(*Value, 0.0f, 1.0f), Geometry.Size.Y);
	}
	else
	{
		// A segment a quarter of the width sweeping back and forth.
		const float Segment = Width * 0.25f;
		const float T = FMath::Fmod(static_cast<float>(Args.CurrentTime) * 0.6f, 2.0f);
		const float Phase = T < 1.0f ? T : 2.0f - T;
		const float Start = (Width - Segment) * Phase;
		Fill = FRect(Start, 0.0f, Start + Segment, Geometry.Size.Y);
	}
	if (Fill.Width() > 0.0f)
	{
		FColor Tint = InWidgetStyle.ColorAndOpacityTint;
		if (FillColor)
		{
			Tint = FWidgetStyle::Multiply(Tint, FillColor->Resolve());
		}
		Style->Fill.Paint(OutDrawElements, LayerId + 1, Geometry.ToPaintGeometry(Fill), Tint);
	}
	return LayerId + 1;
}

// ---------------------------------------------------------------------------
// SThrobber

void SThrobber::Construct(const FArguments& Args)
{
	NumPieces = FMath::Max(1, Args._NumPieces);
	PieceSize = Args._PieceSize;
	Color = Args._Color;
	SetVolatile(true);
}

FVector2 SThrobber::ComputeDesiredSize(float) const
{
	return FVector2(PieceSize * (NumPieces * 2 - 1), PieceSize);
}

int32 SThrobber::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect&, FDrawElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	const FColor Base = InWidgetStyle.Tinted(Color.Resolve());
	for (int32 I = 0; I < NumPieces; ++I)
	{
		const float Phase = static_cast<float>(Args.CurrentTime) * 4.0f - static_cast<float>(I) * 0.7f;
		const float Pulse = 0.5f + 0.5f * FMath::Sin(Phase);
		const float Size = PieceSize * (0.55f + 0.45f * Pulse);
		const FVector2 Center(PieceSize * (I * 2.0f + 0.5f), Geometry.Size.Y * 0.5f);
		FColor C = Base;
		C.A = uint8(Base.A * (0.35f + 0.65f * Pulse));
		OutDrawElements.AddRoundedBox(LayerId, Geometry.ToPaintGeometry(Center - FVector2(Size * 0.5f), FVector2(Size)), C, Size * 0.5f);
	}
	return LayerId;
}
