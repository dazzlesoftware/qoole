// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// FGeometry: where a widget is (Engine/Documentation/Source/GenesisUI.md §6.4).
//
// Layout happens in logical units. A geometry is the widget's local size plus
// the transform from its local space to window pixels ("absolute" space in
// this module), which includes the window's DPI and application scale. The
// layout transform has scale and translation only; render transforms
// (any affine) are kept separately so layout never sees them.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/DrawElements.h"

struct FGeometry
{
	FVector2 Size;                 // local units
	float Scale = 1.0f;            // window pixels per local unit (layout)
	FVector2 AbsolutePosition;     // window pixels of the local origin (layout)
	FTransform2D RenderTransform;  // local -> window pixels, including render transforms

	FGeometry() = default;

	// The root geometry of a window.
	static FGeometry MakeRoot(FVector2 LocalSize, float InScale)
	{
		FGeometry G;
		G.Size = LocalSize;
		G.Scale = InScale;
		G.RenderTransform = FTransform2D(InScale, {});
		return G;
	}

	// A child at Offset (local units) with its own size; ChildScale scales the
	// child's local space (SScaleBox and friends).
	FGeometry MakeChild(FVector2 Offset, FVector2 ChildSize, float ChildScale = 1.0f) const
	{
		FGeometry G;
		G.Size = ChildSize;
		G.Scale = Scale * ChildScale;
		G.AbsolutePosition = AbsolutePosition + Offset * Scale;
		G.RenderTransform = FTransform2D(ChildScale, Offset).Concatenate(RenderTransform);
		return G;
	}

	// Applies a render transform around a pivot (normalized, 0..1 of the size).
	FGeometry WithRenderTransform(const FTransform2D& Transform, FVector2 Pivot) const
	{
		FGeometry G = *this;
		const FVector2 PivotLocal = Size * Pivot;
		const FTransform2D Local = FTransform2D::FromTranslation(-PivotLocal).Concatenate(Transform).Concatenate(FTransform2D::FromTranslation(PivotLocal));
		G.RenderTransform = Local.Concatenate(RenderTransform);
		return G;
	}

	FVector2 GetAbsoluteSize() const { return Size * Scale; }
	FVector2 GetLocalSize() const { return Size; }

	// Layout bounds in window pixels (ignores render transforms).
	FRect GetLayoutBoundingRect() const { return FRect::FromPosSize(AbsolutePosition, Size * Scale); }
	// Bounds in window pixels including render transforms.
	FRect GetRenderBoundingRect() const { return FPaintGeometry(RenderTransform, Size).GetPixelBounds(); }

	FVector2 LocalToAbsolute(FVector2 Local) const { return RenderTransform.TransformPoint(Local); }
	FVector2 AbsoluteToLocal(FVector2 Absolute) const { return RenderTransform.Inverse().TransformPoint(Absolute); }
	FVector2 AbsoluteToLocalVector(FVector2 AbsoluteVector) const { return RenderTransform.Inverse().TransformVector(AbsoluteVector); }

	bool IsUnderLocation(FVector2 Absolute) const
	{
		const FVector2 L = AbsoluteToLocal(Absolute);
		return L.X >= 0.0f && L.Y >= 0.0f && L.X < Size.X && L.Y < Size.Y;
	}

	FPaintGeometry ToPaintGeometry() const { return FPaintGeometry(RenderTransform, Size); }
	FPaintGeometry ToPaintGeometry(FVector2 Offset, FVector2 LocalSize) const
	{
		return FPaintGeometry(FTransform2D::FromTranslation(Offset).Concatenate(RenderTransform), LocalSize);
	}
	FPaintGeometry ToPaintGeometry(const FRect& LocalRect) const { return ToPaintGeometry(LocalRect.GetTopLeft(), LocalRect.GetSize()); }
	// Inset by a margin (local units).
	FPaintGeometry ToInflatedPaintGeometry(const FMargin& Inset) const
	{
		return ToPaintGeometry(FVector2(Inset.Left, Inset.Top), Size - Inset.GetDesiredSize());
	}

	bool operator==(const FGeometry&) const = default;
};
