// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Widgets/SWindow.h"

#include "Framework/Application/SlateApplication.h"
#include "Layout/LayoutUtils.h"

void SWindow::Construct(const FArguments& Args)
{
	Title = Args._Title;
	InitialClientSize = Args._ClientSize;
	InitialScreenPosition = Args._ScreenPosition;
	Type = Args._Type;
	bUseOSBorder = Args._UseOSBorder;
	bSizable = Args._Sizable;
	bSupportsMinimize = Args._SupportsMinimize;
	bSupportsMaximize = Args._SupportsMaximize;
	bIsTopmost = Args._IsTopmost;
	bAppearsInTaskbar = Args._AppearsInTaskbar;
	bAcceptsFileDrops = Args._AcceptsFileDrops;
	TitleBarHeight = Args._TitleBarHeight;
	Style = Args._Style ? Args._Style : &FAppStyle::Get().GetWidgetStyle<FWindowStyle>("Window");
	if (Args._BackgroundColor.IsSet())
	{
		BackgroundColor = Args._BackgroundColor.Get();
	}
	OnWindowClosed = Args._OnWindowClosed;
	OnRequestClose = Args._OnRequestClose;

	Children.Add(FWindowSlot());
	if (Args._Content.Widget)
	{
		Children[0][ToSharedRef(Args._Content.Widget)];
	}
}

void SWindow::SetContent(const TSharedRef<SWidget>& Content)
{
	Children[0][Content];
}

void SWindow::AddPopup(const TSharedRef<SWidget>& Widget, FVector2 Position, EPopupLayer Layer)
{
	FWindowSlot Slot;
	Slot.bIsPopup = true;
	Slot.Layer = Layer;
	Slot.Position = Position;
	Slot[Widget];
	// Keep layers in order: menus, then tooltips, then the drag decorator.
	int32 Index = Children.Num();
	while (Index > 1 && Children[Index - 1].Layer > Layer)
	{
		--Index;
	}
	Children.Insert(std::move(Slot), Index);
}

void SWindow::MovePopup(const SWidget& Widget, FVector2 Position)
{
	for (int32 I = 1; I < Children.Num(); ++I)
	{
		if (&Children[I].GetWidget().Get() == &Widget)
		{
			if (!(Children[I].Position == Position))
			{
				Children[I].Position = Position;
				Invalidate(EInvalidateWidgetReason::Layout);
			}
			return;
		}
	}
}

void SWindow::RemovePopup(const SWidget& Widget)
{
	for (int32 I = 1; I < Children.Num(); ++I)
	{
		if (&Children[I].GetWidget().Get() == &Widget)
		{
			Children.RemoveAt(I);
			return;
		}
	}
}

bool SWindow::HasPopup(const SWidget& Widget) const
{
	for (int32 I = 1; I < Children.Num(); ++I)
	{
		if (&Children[I].GetWidget().Get() == &Widget)
		{
			return true;
		}
	}
	return false;
}

FWindowDesc SWindow::MakeWindowDesc() const
{
	FWindowDesc Desc;
	Desc.Title = Title;
	Desc.Type = Type;
	Desc.Size = InitialClientSize;
	Desc.Position = InitialScreenPosition;
	Desc.bUseOSBorder = bUseOSBorder;
	Desc.bSizable = bSizable;
	Desc.bHasMinimizeButton = bSupportsMinimize;
	Desc.bHasMaximizeButton = bSupportsMaximize;
	Desc.bTopmost = bIsTopmost;
	Desc.bAppearsInTaskbar = bAppearsInTaskbar;
	Desc.bAcceptsFileDrops = bAcceptsFileDrops;
	Desc.bShowImmediately = false;
	return Desc;
}

void SWindow::SetNativeWindow(const TSharedRef<IPlatformWindow>& Native)
{
	NativeWindow = Native.ToSharedPtr();
	PixelSize = Native->GetClientSize();
}

void SWindow::SetTitle(const FString& InTitle)
{
	Title = InTitle;
	if (NativeWindow)
	{
		NativeWindow->SetTitle(Title);
	}
}

void SWindow::SetScale(float InScale)
{
	if (InScale > 0.0f && InScale != Scale)
	{
		Scale = InScale;
		Invalidate(EInvalidateWidgetReason::Layout);
	}
}

void SWindow::SetPixelSize(FIntPoint InSize)
{
	if (!(InSize == PixelSize))
	{
		PixelSize = InSize;
		Invalidate(EInvalidateWidgetReason::Layout);
	}
}

bool SWindow::UpdateLayout()
{
	if (!NeedsLayout() || PixelSize.X <= 0 || PixelSize.Y <= 0)
	{
		return false;
	}
	// Widgets that adapt to their arranged size (wrapping text, virtual
	// lists, scale boxes) may invalidate layout while being arranged; repeat
	// a few times so the frame shows the settled result.
	constexpr int32 MaxPasses = 4;
	for (int32 Pass = 0; Pass < MaxPasses && NeedsLayout(); ++Pass)
	{
		Measure(Scale);
		ArrangeTree(FGeometry::MakeRoot(GetLogicalSize(), Scale));
	}
	if (NeedsLayout())
	{
		GENESIS_LOG(LogSlate, Verbose, "Window \"{}\" layout did not settle in {} passes", Title, MaxPasses);
	}
	return true;
}

void SWindow::PaintWindow(const FPaintArgs& Args)
{
	Elements.Reset();
	Elements.SetViewportSize(PixelSize);
	FWidgetStyle WidgetStyle;
	WidgetStyle.ForegroundColor = FSlateColor(EStyleColor::Foreground).Resolve();
	const FRect Culling(0.0f, 0.0f, static_cast<float>(PixelSize.X), static_cast<float>(PixelSize.Y));
	Paint(Args, Culling, Elements, 0, WidgetStyle);
	ClearNeedsRepaint();
}

FColor SWindow::GetBackgroundColor() const
{
	return (BackgroundColor ? *BackgroundColor : Style->Background).Resolve();
}

FVector2 SWindow::ComputeDesiredSize(float) const
{
	return Children[0].GetWidget()->GetDesiredSize();
}

void SWindow::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	const FVector2 Size = AllottedGeometry.Size;
	ArrangeSlotChild(AllottedGeometry, ArrangedChildren, Children[0], FVector2::Zero(), Size);
	for (int32 I = 1; I < Children.Num(); ++I)
	{
		const FWindowSlot& Slot = Children[I];
		const TSharedRef<SWidget>& Widget = Slot.GetWidget();
		const FVector2 Desired = FVector2::Min(Widget->GetDesiredSize(), Size);
		// Keep popups inside the window.
		const FVector2 Position(FMath::Clamp(Slot.Position.X, 0.0f, FMath::Max(0.0f, Size.X - Desired.X)),
		    FMath::Clamp(Slot.Position.Y, 0.0f, FMath::Max(0.0f, Size.Y - Desired.Y)));
		ArrangedChildren.AddWidget(
		    Widget->GetVisibility(), FArrangedWidget(Widget, AllottedGeometry.MakeChild(Position, Desired)));
	}
}

int32 SWindow::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect,
    FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	OutDrawElements.AddSolidBox(LayerId, Geometry.ToPaintGeometry(), GetBackgroundColor());
	int32 MaxLayer = LayerId;
	for (const FArrangedWidget& Child : GetArrangedChildren())
	{
		// Each popup draws above everything before it.
		MaxLayer = Child.Widget->Paint(Args, CullingRect, OutDrawElements, MaxLayer + 1, InWidgetStyle);
	}
	if (!bUseOSBorder && Type == EWindowType::Normal && Style->Outline.IsVisible())
	{
		Style->Outline.Paint(
		    OutDrawElements, ++MaxLayer, Geometry.ToPaintGeometry(), InWidgetStyle.ColorAndOpacityTint);
	}
	return MaxLayer;
}

TArray<TSharedRef<SWidget>> SWindow::HitTestAt(FVector2 PixelPosition)
{
	TArray<TSharedRef<SWidget>> Path;
	HitTest(PixelPosition, Path);
	return Path;
}

EWindowZone SWindow::GetWindowZoneForPoint(FVector2 PixelPosition)
{
	const TArray<TSharedRef<SWidget>> Path = HitTestAt(PixelPosition);
	for (TArray<TSharedRef<SWidget>>::const_reverse_iterator It = Path.rbegin(); It != Path.rend(); ++It)
	{
		const EWindowZone Zone = (*It)->GetWindowZoneOverride();
		if (Zone != EWindowZone::Unspecified)
		{
			return Zone;
		}
	}
	if (!bUseOSBorder && TitleBarHeight > 0.0f && PixelPosition.Y < TitleBarHeight * Scale)
	{
		return EWindowZone::TitleBar;
	}
	return EWindowZone::Unspecified;
}

void SWindow::RequestClose()
{
	if (bClosed)
	{
		return;
	}
	if (OnRequestClose.IsBound() && !OnRequestClose.Execute())
	{
		return;
	}
	FSlateApplication::Get().RequestDestroyWindow(SharedThis(this));
}

void SWindow::NotifyWindowClosed()
{
	if (!bClosed)
	{
		bClosed = true;
		OnWindowClosed.ExecuteIfBound(SharedThis(this));
	}
}
