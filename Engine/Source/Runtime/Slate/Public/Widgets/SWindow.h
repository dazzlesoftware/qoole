// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// SWindow: the root of a widget tree (Engine/Documentation/Source/GenesisUI.md §6.6). It owns an
// IPlatformWindow and draws its content plus popup layers on top of it
// (menus, tooltips, the drag-and-drop decorator), which live inside the
// window's client area.
//
// Logical units: the window's scale is the monitor DPI scale times the
// application scale; the root geometry maps logical units to pixels with it.

#pragma once

#include "GenericPlatform/PlatformWindow.h"
#include "Styling/SlateStyle.h"
#include "Widgets/SPanel.h"
#include "Widgets/WidgetDelegates.h"

class SWindow;

using FOnWindowClosed = TDelegate<void(const TSharedRef<SWindow>&)>;
using FRequestWindowClose = TDelegate<bool()>; // return false to keep the window open

enum class EPopupLayer : uint8
{
	Menu,
	ToolTip,
	DragDecorator,
};

class SWindow : public SPanel
{
public:
	SLATE_BEGIN_ARGS(SWindow)
		SLATE_ARGUMENT(FString, Title, "Genesis")
		SLATE_ARGUMENT(FVector2, ClientSize, (FVector2(800.0f, 600.0f)))
		SLATE_ARGUMENT(TOptional<FIntPoint>, ScreenPosition, TOptional<FIntPoint>())
		SLATE_ARGUMENT(EWindowType, Type, EWindowType::Normal)
		SLATE_ARGUMENT(bool, UseOSBorder, true)
		SLATE_ARGUMENT(bool, Sizable, true)
		SLATE_ARGUMENT(bool, SupportsMinimize, true)
		SLATE_ARGUMENT(bool, SupportsMaximize, true)
		SLATE_ARGUMENT(bool, IsTopmost, false)
		SLATE_ARGUMENT(bool, AppearsInTaskbar, true)
		SLATE_ARGUMENT(bool, AcceptsFileDrops, false)
		// Borderless windows: the height of the UI-drawn title bar, which the
		// OS treats as the caption (drag to move) unless a widget there
		// claims the point (buttons do).
		SLATE_ARGUMENT(float, TitleBarHeight, 0.0f)
		SLATE_STYLE_ARGUMENT(FWindowStyle, Style)
		SLATE_OPTIONAL_ATTRIBUTE(FSlateColor, BackgroundColor)
		SLATE_EVENT(FOnWindowClosed, OnWindowClosed)
		SLATE_EVENT(FRequestWindowClose, OnRequestClose)
		SLATE_DEFAULT_SLOT(Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

	FChildren* GetChildren() override { return &Children; }

	// --- Content and popups ------------------------------------------------

	void SetContent(const TSharedRef<SWidget>& Content);
	TSharedRef<SWidget> GetContent() const { return Children[0].GetWidget(); }

	// Adds a popup at a logical position in the window (moved to fit inside).
	void AddPopup(const TSharedRef<SWidget>& Widget, FVector2 Position, EPopupLayer Layer);
	void MovePopup(const SWidget& Widget, FVector2 Position);
	void RemovePopup(const SWidget& Widget);
	bool HasPopup(const SWidget& Widget) const;
	int32 NumPopups() const { return Children.Num() - 1; }

	// --- Native window -------------------------------------------------------

	FWindowDesc MakeWindowDesc() const;
	void SetNativeWindow(const TSharedRef<IPlatformWindow>& Native);
	const TSharedPtr<IPlatformWindow>& GetNativeWindow() const { return NativeWindow; }
	bool IsWindowMinimized() const { return NativeWindow && NativeWindow->IsMinimized(); }

	const FString& GetTitle() const { return Title; }
	void SetTitle(const FString& InTitle);
	EWindowType GetType() const { return Type; }
	bool IsPopupWindow() const { return Type != EWindowType::Normal; }

	// Pixels per logical unit (DPI x application scale).
	float GetScale() const { return Scale; }
	void SetScale(float InScale);
	FIntPoint GetPixelSize() const { return PixelSize; }
	void SetPixelSize(FIntPoint InSize);
	FVector2 GetLogicalSize() const { return PixelSize.ToVector2() / Scale; }

	// Lays out the tree (measure + arrange) if anything changed. Returns true
	// if layout ran.
	bool UpdateLayout();
	// Paints the whole window into its element list.
	void PaintWindow(const FPaintArgs& Args);
	const FSlateWindowElementList& GetDrawElements() const { return Elements; }
	FColor GetBackgroundColor() const;

	// Hit-tests popups first, then the content (window pixels).
	TArray<TSharedRef<SWidget>> HitTestAt(FVector2 PixelPosition);
	EWindowZone GetWindowZoneForPoint(FVector2 PixelPosition);

	// --- Closing ---------------------------------------------------------------

	// Asks OnRequestClose and destroys the window through the application.
	void RequestClose();
	void SetOnRequestClose(FRequestWindowClose Delegate) { OnRequestClose = std::move(Delegate); }
	// Called by the application when the window is destroyed.
	void NotifyWindowClosed();
	bool IsClosed() const { return bClosed; }

	// Modal windows disable their parent while open.
	bool IsModal() const { return bIsModal; }
	void SetIsModal(bool bModal) { bIsModal = bModal; }

	// The widget that had focus when the window was last deactivated.
	TWeakPtr<SWidget> WidgetToFocusOnActivate;

protected:
	FVector2 ComputeDesiredSize(float LayoutScale) const override;
	void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	class FWindowSlot : public TSlotBase<FWindowSlot>, public TPaddingMixin<FWindowSlot>, public TAlignmentMixin<FWindowSlot>
	{
	public:
		bool bIsPopup = false;
		EPopupLayer Layer = EPopupLayer::Menu;
		FVector2 Position;
	};

	TPanelChildren<FWindowSlot> Children{ this };
	TSharedPtr<IPlatformWindow> NativeWindow;
	const FWindowStyle* Style = nullptr;
	TOptional<FSlateColor> BackgroundColor;
	FString Title;
	FVector2 InitialClientSize;
	TOptional<FIntPoint> InitialScreenPosition;
	EWindowType Type = EWindowType::Normal;
	bool bUseOSBorder = true;
	bool bSizable = true;
	bool bSupportsMinimize = true;
	bool bSupportsMaximize = true;
	bool bIsTopmost = false;
	bool bAppearsInTaskbar = true;
	bool bAcceptsFileDrops = false;
	float TitleBarHeight = 0.0f;
	FOnWindowClosed OnWindowClosed;
	FRequestWindowClose OnRequestClose;

	float Scale = 1.0f;
	FIntPoint PixelSize{ 0, 0 };
	FSlateWindowElementList Elements;
	bool bClosed = false;
	bool bIsModal = false;
};
