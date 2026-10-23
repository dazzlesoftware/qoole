// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "SWorldCompositionGrid.h"
#include "Containers/ContainerAliases.h"
#include "Templates/SharedPointer.h"
#include "Internationalization/TextLocalization.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Styling/StarshipCoreStyle.h"
#include "Rendering/DrawElements.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	constexpr float MinGridLineSpacing = 8.0f;
	constexpr float MinLabelWidth = 40.0f;
	constexpr float FrameMargin = 0.1f;
	constexpr float WheelZoomStep = 1.15f;
} // namespace

void SWorldCompositionGrid::Construct(const FArguments& A)
{
	Session = A._Session;
	check(Session);
	OnChanged = A._OnChanged;
	OnSelectionChanged = A._OnSelectionChanged;
	OnError = A._OnError;
	OnOpenTile = A._OnOpenTile;
	OnNewRegion = A._OnNewRegion;
	SetClipping(EWidgetClipping::ClipToBounds);
	SetCanTick(true); // frames the tiles once its size is known
}

FVector2 SWorldCompositionGrid::CellToLocal(double CellX, double CellY) const
{
	return FVector2(
	    Pan.X + static_cast<float>(CellX) * PixelsPerCell, Pan.Y - static_cast<float>(CellY) * PixelsPerCell);
}
void SWorldCompositionGrid::LocalToCell(FVector2 Local, int32& CellX, int32& CellY) const
{
	CellX = static_cast<int32>(std::floor((Local.X - Pan.X) / PixelsPerCell));
	CellY = static_cast<int32>(std::floor((Pan.Y - Local.Y) / PixelsPerCell));
}

const FWorldCompositionTile* SWorldCompositionGrid::HitTile(FVector2 Local) const
{
	const FWorldComposition& World = Session->GetComposition();
	const double CellSize = World.GetCellSize();
	return World.FindTileAt((Local.X - Pan.X) / PixelsPerCell * CellSize, (Pan.Y - Local.Y) / PixelsPerCell * CellSize);
}

void SWorldCompositionGrid::Report(const FString& Error)
{
	SetToolTipText(Error);
	OnError.ExecuteIfBound(Error);
}

void SWorldCompositionGrid::SetSelectedTile(const FString& TileId)
{
	const FString Next = Session->GetComposition().FindTile(TileId) ? TileId : FString();
	if (Next == Selected)
		return;
	Selected = Next;
	OnSelectionChanged.ExecuteIfBound();
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SWorldCompositionGrid::DeleteSelected()
{
	if (Selected.IsEmpty())
		return;
	FString Error;
	if (!Session->RemoveTile(Selected, Error))
	{
		Report(Error);
		return;
	}
	SetSelectedTile({});
	OnChanged.ExecuteIfBound();
}

void SWorldCompositionGrid::FrameAll()
{
	const TArray<FWorldCompositionTile>& Tiles = Session->GetComposition().GetTiles();
	if (Tiles.empty())
	{
		PixelsPerCell = 48.0f;
		Pan = LastSize * 0.5f;
		Invalidate(EInvalidateWidgetReason::Paint);
		return;
	}
	int64 MinX = Tiles[0].X, MinY = Tiles[0].Y, MaxX = Tiles[0].X, MaxY = Tiles[0].Y;
	for (const FWorldCompositionTile& Tile : Tiles)
	{
		MinX = std::min<int64>(MinX, Tile.X);
		MinY = std::min<int64>(MinY, Tile.Y);
		MaxX = std::max<int64>(MaxX, static_cast<int64>(Tile.X) + Tile.SizeX);
		MaxY = std::max<int64>(MaxY, static_cast<int64>(Tile.Y) + Tile.SizeY);
	}
	const float SpanX = static_cast<float>(MaxX - MinX), SpanY = static_cast<float>(MaxY - MinY);
	const float Fit = std::min(LastSize.X / SpanX, LastSize.Y / SpanY) * (1.0f - 2.0f * FrameMargin);
	PixelsPerCell = FMath::Clamp(Fit, MinPixelsPerCell, MaxPixelsPerCell);
	const float CenterX = static_cast<float>(MinX) + SpanX * 0.5f, CenterY = static_cast<float>(MinY) + SpanY * 0.5f;
	Pan = FVector2(LastSize.X * 0.5f - CenterX * PixelsPerCell, LastSize.Y * 0.5f + CenterY * PixelsPerCell);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SWorldCompositionGrid::Tick(const FGeometry& G, double, float)
{
	LastSize = G.GetLocalSize();
	// The first tick knows the widget's size; frame the existing tiles once.
	if (!bFramed && LastSize.X > 0 && LastSize.Y > 0)
	{
		bFramed = true;
		FrameAll();
	}
}

void SWorldCompositionGrid::UpdateDragPreview()
{
	const FWorldCompositionTile* Tile = Session->GetComposition().FindTile(Selected);
	if (!Tile)
	{
		bPreviewValid = false;
		return;
	}
	FWorldCompositionTile Moved = *Tile;
	Moved.X = PreviewX;
	Moved.Y = PreviewY;
	FString Error;
	bPreviewValid = Session->GetComposition().CanUpdateTile(Moved, Error);
}

FReply SWorldCompositionGrid::OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E)
{
	Last = G.AbsoluteToLocal(E.GetAbsolutePosition());
	int32 CellX = 0, CellY = 0;
	LocalToCell(Last, CellX, CellY);
	if (E.GetEffectingButton() == EMouseButton::Middle)
	{
		bPanning = true;
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	const FWorldCompositionTile* Hit = HitTile(Last);
	SetSelectedTile(Hit ? Hit->Id : FString());
	if (E.GetEffectingButton() == EMouseButton::Right)
	{
		ShowCellMenu(E, CellX, CellY);
		return FReply::Handled();
	}
	if (E.GetEffectingButton() != EMouseButton::Left)
		return FReply::Unhandled();
	if (Hit)
	{
		bDragging = true;
		GrabCellX = CellX;
		GrabCellY = CellY;
		StartX = PreviewX = Hit->X;
		StartY = PreviewY = Hit->Y;
		bPreviewValid = true;
	}
	return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this));
}

FReply SWorldCompositionGrid::OnMouseMove(const FGeometry& G, const FPointerEvent& E)
{
	const FVector2 Cursor = G.AbsoluteToLocal(E.GetAbsolutePosition());
	const FVector2 Delta = Cursor - Last;
	Last = Cursor;
	if (bPanning)
	{
		Pan += Delta;
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}
	if (bDragging)
	{
		int32 CellX = 0, CellY = 0;
		LocalToCell(Cursor, CellX, CellY);
		const int32 NextX = static_cast<int32>(FMath::Clamp<int64>(static_cast<int64>(StartX) + CellX - GrabCellX,
		    std::numeric_limits<int32>::min(), std::numeric_limits<int32>::max()));
		const int32 NextY = static_cast<int32>(FMath::Clamp<int64>(static_cast<int64>(StartY) + CellY - GrabCellY,
		    std::numeric_limits<int32>::min(), std::numeric_limits<int32>::max()));
		if (NextX != PreviewX || NextY != PreviewY)
		{
			PreviewX = NextX;
			PreviewY = NextY;
			UpdateDragPreview();
			Invalidate(EInvalidateWidgetReason::Paint);
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SWorldCompositionGrid::OnMouseButtonUp(const FGeometry&, const FPointerEvent& E)
{
	if (E.GetEffectingButton() != EMouseButton::Left && E.GetEffectingButton() != EMouseButton::Middle)
		return FReply::Unhandled();
	if (bDragging && (PreviewX != StartX || PreviewY != StartY))
	{
		FString Error;
		if (Session->MoveTile(Selected, PreviewX, PreviewY, Error))
			OnChanged.ExecuteIfBound();
		else
			Report(Error);
	}
	OnMouseCaptureLost();
	return FReply::Handled().ReleaseMouseCapture();
}

void SWorldCompositionGrid::OnMouseCaptureLost()
{
	bPanning = bDragging = false;
	bPreviewValid = true;
	Invalidate(EInvalidateWidgetReason::Paint);
}

FReply SWorldCompositionGrid::OnMouseButtonDoubleClick(const FGeometry& G, const FPointerEvent& E)
{
	if (E.GetEffectingButton() != EMouseButton::Left)
		return FReply::Unhandled();
	const FWorldCompositionTile* Hit = HitTile(G.AbsoluteToLocal(E.GetAbsolutePosition()));
	if (!Hit)
		return FReply::Unhandled();
	SetSelectedTile(Hit->Id);
	OnOpenTile.ExecuteIfBound(Hit->Id);
	return FReply::Handled();
}

FReply SWorldCompositionGrid::OnMouseWheel(const FGeometry& G, const FPointerEvent& E)
{
	const FVector2 P = G.AbsoluteToLocal(E.GetAbsolutePosition());
	const FVector2 Before = (P - Pan) / PixelsPerCell;
	PixelsPerCell =
	    FMath::Clamp(PixelsPerCell * std::pow(WheelZoomStep, E.GetWheelDelta()), MinPixelsPerCell, MaxPixelsPerCell);
	Pan = P - Before * PixelsPerCell;
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled();
}

FReply SWorldCompositionGrid::OnKeyDown(const FGeometry&, const FKeyEvent& E)
{
	if (E.GetKey() == EKey::Delete)
	{
		DeleteSelected();
		return FReply::Handled();
	}
	if (E.GetKey() == EKey::F)
	{
		FrameAll();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SWorldCompositionGrid::OnDragOver(const FGeometry&, const FDragDropEvent& Event)
{
	const TSharedPtr<FAssetDragDropOp> Op = Event.GetOperationAs<FAssetDragDropOp>();
	return Op && Op->Schema == "Genesis.World" ? FReply::Handled() : FReply::Unhandled();
}

FReply SWorldCompositionGrid::OnDrop(const FGeometry& G, const FDragDropEvent& Event)
{
	const TSharedPtr<FAssetDragDropOp> Op = Event.GetOperationAs<FAssetDragDropOp>();
	if (!Op || Op->Schema != "Genesis.World")
		return FReply::Unhandled();
	int32 CellX = 0, CellY = 0;
	LocalToCell(G.AbsoluteToLocal(Event.GetAbsolutePosition()), CellX, CellY);
	FString Error, TileId;
	if (!Session->RegisterMap(Op->Asset, CellX, CellY, TileId, Error))
	{
		Report(Error);
		return FReply::Unhandled();
	}
	SetSelectedTile(TileId);
	OnChanged.ExecuteIfBound();
	return FReply::Handled();
}

void SWorldCompositionGrid::ShowCellMenu(const FPointerEvent& Event, int32 CellX, int32 CellY)
{
	const bool bHasTile = !Selected.IsEmpty();
	const TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox) +
	    SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry)
	            .Text(GENESIS_TEXT("New Region Here"))
	            .IsEnabled(!bHasTile && OnNewRegion.IsBound())
	            .OnSelected([this, CellX, CellY] { OnNewRegion.ExecuteIfBound(CellX, CellY); })] +
	    SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry)
	            .Text(GENESIS_TEXT("Open Region Map"))
	            .IsEnabled(bHasTile)
	            .OnSelected([this] { OnOpenTile.ExecuteIfBound(Selected); })] +
	    SVerticalBox::Slot()
	        .AutoHeight()[SNew(SMenuEntry).Text(GENESIS_TEXT("Remove Region")).IsEnabled(bHasTile).OnSelected([this] {
		DeleteSelected();
	})];
	FSlateApplication& App = FSlateApplication::Get();
	const TSharedPtr<SWindow> Window = App.FindWindowForWidget(*this);
	App.PushMenu(SharedThis(this), Menu, Event.GetAbsolutePosition() / (Window ? Window->GetScale() : 1.0f));
}

int32 SWorldCompositionGrid::OnPaint(const FPaintArgs&, const FGeometry& G, const FRect&, FSlateWindowElementList& Out,
    int32 L, const FWidgetStyle&) const
{
	const FVector2 Area = G.GetLocalSize();
	Out.AddSolidBox(L, G.ToPaintGeometry(), FColor(22, 24, 28));

	// Grid lines every Step cells, a power of two that keeps them apart.
	int32 Step = 1;
	while (PixelsPerCell * static_cast<float>(Step) < MinGridLineSpacing)
		Step *= 2;
	const float Spacing = PixelsPerCell * static_cast<float>(Step);
	const int64 FirstX = static_cast<int64>(std::floor(-Pan.X / Spacing)),
	            FirstY = static_cast<int64>(std::floor((Pan.Y - Area.Y) / Spacing));
	for (int64 I = FirstX; Pan.X + static_cast<float>(I) * Spacing <= Area.X; ++I)
	{
		const float X = Pan.X + static_cast<float>(I) * Spacing;
		Out.AddLines(L + 1, G.ToPaintGeometry(), {FVector2(X, 0), FVector2(X, Area.Y)},
		    I == 0 ? FColor(70, 76, 88) : FColor(34, 37, 43), I == 0 ? 2.0f : 1.0f);
	}
	for (int64 I = FirstY; Pan.Y - static_cast<float>(I) * Spacing >= 0; ++I)
	{
		const float Y = Pan.Y - static_cast<float>(I) * Spacing;
		Out.AddLines(L + 1, G.ToPaintGeometry(), {FVector2(0, Y), FVector2(Area.X, Y)},
		    I == 0 ? FColor(70, 76, 88) : FColor(34, 37, 43), I == 0 ? 2.0f : 1.0f);
	}

	FSlateFontInfo Font = FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText").Font;
	const FWorldComposition& World = Session->GetComposition();
	const TFunction<FRect(int32, int32, int32, int32)> Footprint = [this](int32 X, int32 Y, int32 SizeX, int32 SizeY)
	{
		const FVector2 TopLeft = CellToLocal(X, static_cast<double>(Y) + SizeY);
		return FRect(TopLeft.X, TopLeft.Y, TopLeft.X + SizeX * PixelsPerCell, TopLeft.Y + SizeY * PixelsPerCell);
	};
	for (const FWorldCompositionTile& Tile : World.GetTiles())
	{
		const FRect Box = Footprint(Tile.X, Tile.Y, Tile.SizeX, Tile.SizeY);
		if (Box.Right < 0 || Box.Bottom < 0 || Box.Left > Area.X || Box.Top > Area.Y)
			continue;
		const bool bSelected = Tile.Id == Selected;
		const FColor Fill = Tile.Host.IsEmpty() ? FColor(46, 84, 66) : FColor(60, 62, 104);
		Out.AddRoundedBox(L + 2,
		    G.ToPaintGeometry(FVector2(Box.Left, Box.Top), FVector2(Box.Right - Box.Left, Box.Bottom - Box.Top)),
		    bDragging && bSelected ? FColor(Fill.R, Fill.G, Fill.B, 110) : Fill, 3,
		    bSelected ? FColor(240, 170, 45) : FColor(100, 140, 120), bSelected ? 2.0f : 1.0f);
		if (Box.Right - Box.Left >= MinLabelWidth)
		{
			const TSharedRef<const FShapedGlyphSequence> Run =
			    FSlateApplication::Get().GetFontCache().ShapeText(Tile.Name.View(), Font, G.Scale);
			// Long names stop at the tile's edge.
			Out.PushClip(G.ToPaintGeometry(
			    FVector2(Box.Left + 2, Box.Top + 2), FVector2(Box.Right - Box.Left - 4, Box.Bottom - Box.Top - 4)));
			Out.AddText(L + 3,
			    G.ToPaintGeometry(FVector2(Box.Left + 6, Box.Top + 4), FVector2(Box.Right - Box.Left - 8, 20)), Run,
			    FColor(225, 230, 235));
			Out.PopClip();
		}
	}
	if (bDragging && (PreviewX != StartX || PreviewY != StartY))
		if (const FWorldCompositionTile* Tile = World.FindTile(Selected))
		{
			const FRect Box = Footprint(PreviewX, PreviewY, Tile->SizeX, Tile->SizeY);
			Out.AddRoundedBox(L + 4,
			    G.ToPaintGeometry(FVector2(Box.Left, Box.Top), FVector2(Box.Right - Box.Left, Box.Bottom - Box.Top)),
			    bPreviewValid ? FColor(70, 160, 110, 120) : FColor(170, 60, 55, 120), 3,
			    bPreviewValid ? FColor(120, 230, 160) : FColor(250, 90, 80), 2.0f);
		}
	return L + 5;
}
