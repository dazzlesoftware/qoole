// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "Framework/Application/SlateApplication.h"
#include "Widgets/SCompoundWidget.h"

// What PushMenu puts in the window: the menu background around the content.
// Focusable, so keys reach the menu as soon as it opens; navigation wraps
// inside it.
class SMenuHost final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMenuHost)
		SLATE_DEFAULT_SLOT(Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

	bool SupportsKeyboardFocus() const override { return true; }
	FNavigationReply OnNavigation(const FGeometry& Geometry, const FNavigationEvent& Event) override;
	FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	EWindowZone GetWindowZoneOverride() const override { return EWindowZone::ClientArea; }

protected:
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	const FMenuStyle* Style = nullptr;
};

class FSlateApplication::FMenu final : public IMenu, public TSharedFromThis<FSlateApplication::FMenu>
{
public:
	FMenu(FSlateApplication& InApp, const TSharedRef<SWidget>& InHost, const TSharedRef<SWidget>& InParent, const TSharedRef<SWindow>& InWindow)
		: App(InApp), Host(InHost), Parent(InParent.ToSharedPtr()), Window(InWindow.ToSharedPtr())
	{
	}

	void Dismiss() override { App.DismissMenuAndChildren(*this); }
	bool IsDismissed() const override { return bDismissed; }
	TSharedRef<SWidget> GetContent() const override { return Host; }
	TSharedPtr<SWidget> GetParentWidget() const override { return Parent.lock(); }
	TSharedPtr<SWindow> GetOwnerWindow() const override { return Window.lock(); }

	FSlateApplication& App;
	TSharedRef<SWidget> Host;
	TWeakPtr<SWidget> Parent;
	TWeakPtr<SWindow> Window;
	TWeakPtr<SWidget> PreviousFocus; // restored when the menu closes with focus inside
	bool bDismissed = false;
};

// Small helpers shared by the application files.
namespace UIApplicationUtil
{
	inline bool PathContains(const TArray<TSharedRef<SWidget>>& Path, const SWidget* Widget)
	{
		for (const TSharedRef<SWidget>& W : Path)
		{
			if (&W.Get() == Widget)
			{
				return true;
			}
		}
		return false;
	}
}
