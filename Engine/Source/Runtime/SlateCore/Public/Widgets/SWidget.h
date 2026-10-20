// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// SWidget: the base of every widget (Engine/Documentation/Source/GenesisUI.md §6).
//
// Layout and paint are retained (§6.4):
//
//   Measure(LayoutScale)      bottom-up; only widgets whose layout is dirty
//                             recompute their desired size.
//   ArrangeTree(Geometry)     top-down; only dirty widgets and widgets whose
//                             allotted geometry changed re-arrange. Each widget
//                             stores its geometry and arranged children.
//   Paint(...)                walks stored geometry; a window is repainted
//                             only when something in it invalidated paint.
//   HitTest(...)              walks stored geometry.
//
// Subclasses implement ComputeDesiredSize, OnArrangeChildren, GetChildren and
// OnPaint, plus whichever input handlers they need. Most widgets derive from
// SLeafWidget, SCompoundWidget or SPanel rather than from SWidget directly.

#pragma once

#include "Application/SlateApplicationBase.h"
#include "Input/Reply.h"
#include "Layout/ArrangedChildren.h"
#include "Widgets/Attribute.h"
#include "Widgets/IToolTip.h"

class FChildren;
class SWidget;

// Inherited paint state: tint and the foreground (text/icon) color.
struct FWidgetStyle
{
	FColor ColorAndOpacityTint = FColor::White();
	FColor ForegroundColor = FColor::White();

	static FColor Multiply(FColor A, FColor B)
	{
		return FColor(uint8((A.R * B.R + 127) / 255), uint8((A.G * B.G + 127) / 255), uint8((A.B * B.B + 127) / 255), uint8((A.A * B.A + 127) / 255));
	}

	FWidgetStyle& BlendColorAndOpacityTint(FColor Tint)
	{
		ColorAndOpacityTint = Multiply(ColorAndOpacityTint, Tint);
		return *this;
	}
	FWidgetStyle& SetForegroundColor(FColor Color)
	{
		ForegroundColor = Color;
		return *this;
	}
	// A color as it should be drawn under this style's tint.
	FColor Tinted(FColor Color) const { return Multiply(Color, ColorAndOpacityTint); }
	FColor GetForegroundColor() const { return Tinted(ForegroundColor); }
};

struct FPaintArgs
{
	double CurrentTime = 0.0;
	float DeltaTime = 0.0f;
	bool bEnabled = true; // this widget and all its ancestors are enabled
};

// Base of TWidgetAttribute, so SWidget can update bindings without knowing T.
class FWidgetAttributeBase
{
public:
	virtual ~FWidgetAttributeBase() = default;
	// Evaluates a binding; returns what changed (None when unchanged).
	virtual EInvalidateWidgetReason UpdateBinding() = 0;
};

// A widget member backed by a TAttribute (§6.3). It caches the current value;
// setting a different value, or a binding returning a different value when
// evaluated once per frame, invalidates the owner with Reason.
template <typename T, EInvalidateWidgetReason Reason>
class TWidgetAttribute final : public FWidgetAttributeBase
{
public:
	explicit TWidgetAttribute(SWidget& InOwner, T InDefault = T{}) : Owner(InOwner), Value(std::move(InDefault)) {}
	~TWidgetAttribute() override;
	TWidgetAttribute(const TWidgetAttribute&) = delete;
	TWidgetAttribute& operator=(const TWidgetAttribute&) = delete;

	const T& Get() const { return Value; }
	bool IsBound() const { return Binding.IsBound(); }

	// Takes the attribute's value or binding; an unset attribute keeps the
	// current value.
	void Assign(const TAttribute<T>& Attribute);
	void Set(T NewValue);

	EInvalidateWidgetReason UpdateBinding() override
	{
		if (!Binding.IsBound())
		{
			return EInvalidateWidgetReason::None;
		}
		T NewValue = Binding.Get();
		if (IsSame(NewValue))
		{
			return EInvalidateWidgetReason::None;
		}
		Value = std::move(NewValue);
		return Reason;
	}

private:
	bool IsSame(const T& NewValue) const
	{
		if constexpr (std::equality_comparable<T>)
		{
			return NewValue == Value;
		}
		else
		{
			return false;
		}
	}
	void SetValue(T NewValue);

	SWidget& Owner;
	T Value;
	TAttribute<T> Binding;
};

// Common construction arguments every widget accepts.
struct FWidgetCommonArgs
{
	TAttribute<EVisibility> _Visibility = EVisibility::Visible;
	TAttribute<bool> _IsEnabled = true;
	TAttribute<FString> _ToolTipText;
	TSharedPtr<IToolTip> _ToolTip;
	TAttribute<TOptional<EMouseCursor>> _Cursor;
	EWidgetClipping _Clipping = EWidgetClipping::Inherit;
	TAttribute<TOptional<FTransform2D>> _RenderTransform;
	FVector2 _RenderTransformPivot = FVector2(0.5f, 0.5f);
	FName _Tag;
	bool _ForceVolatile = false;
	TOptional<float> _RenderOpacity;
};

class SWidget : public TSharedFromThis<SWidget>, private FNoncopyable
{
public:
	SWidget();
	virtual ~SWidget();

	// Applies the common arguments; SNew calls it before Construct.
	void SWidgetConstruct(const FWidgetCommonArgs& Args);

	// --- Layout ---------------------------------------------------------

	// Recomputes desired sizes where layout is dirty (or the scale changed).
	void Measure(float LayoutScale);
	// Desired size from the last Measure; zero while collapsed.
	FVector2 GetDesiredSize() const;
	// Stores the geometry and arranges children where needed.
	void ArrangeTree(const FGeometry& AllottedGeometry);
	// Geometry from the last arrange (window pixels, see FGeometry).
	const FGeometry& GetCachedGeometry() const { return CachedGeometry; }
	const TArray<FArrangedWidget>& GetArrangedChildren() const { return ArrangedChildrenCache; }
	// Whether this widget or anything under it needs Measure/ArrangeTree.
	bool NeedsLayout() const { return bNeedsMeasure || bNeedsArrange || bDescendantNeedsArrange; }
	bool NeedsRepaint() const { return bNeedsRepaint; }
	void ClearNeedsRepaint() { bNeedsRepaint = false; }
	// The scale passed to the last Measure.
	float GetMeasuredLayoutScale() const { return MeasuredScale; }

	// Scale of a child's local space relative to this widget (SScaleBox).
	virtual float GetRelativeLayoutScale(int32 ChildIndex, float LayoutScale) const { (void)ChildIndex; (void)LayoutScale; return 1.0f; }

	virtual FChildren* GetChildren() = 0;
	const FChildren* GetChildren() const { return const_cast<SWidget*>(this)->GetChildren(); }

	// --- Paint and hit-testing -------------------------------------------

	// Paints this widget and its children at their stored geometry. Returns
	// the highest layer used.
	int32 Paint(const FPaintArgs& Args, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const;

	// Finds the widgets under Position (window pixels), root to leaf.
	bool HitTest(FVector2 Position, TArray<TSharedRef<SWidget>>& OutPath);

	// --- Invalidation ----------------------------------------------------

	void Invalidate(EInvalidateWidgetReason Reason);

	// Volatile widgets repaint every frame (animations, viewports).
	void SetVolatile(bool bInVolatile);
	bool IsVolatile() const { return bVolatile; }

	// --- Tree -------------------------------------------------------------

	SWidget* GetParentWidget() const { return ParentPtr; }
	TSharedPtr<SWidget> GetParentWidgetShared() const { return ParentPtr ? ParentPtr->shared_from_this() : nullptr; }
	// The topmost ancestor (the window, once attached to one).
	SWidget* GetRootWidget();
	bool IsAncestorOf(const SWidget* Other) const;
	bool IsNullWidget() const { return bIsNullWidget; }
	// Called by slots; do not use directly.
	void SetParentWidget(SWidget* NewParent);

	// --- State ------------------------------------------------------------

	EVisibility GetVisibility() const { return Visibility.Get(); }
	void SetVisibility(TAttribute<EVisibility> InVisibility) { Visibility.Assign(InVisibility); }
	// Visible to the user: this widget and every ancestor is painted.
	bool IsVisibleInHierarchy() const;

	bool IsEnabled() const { return EnabledState.Get(); }
	void SetEnabled(TAttribute<bool> InEnabled) { EnabledState.Assign(InEnabled); }
	bool IsEnabledInHierarchy() const;

	EWidgetClipping GetClipping() const { return Clipping; }
	void SetClipping(EWidgetClipping InClipping);

	const TOptional<FTransform2D>& GetRenderTransform() const { return RenderTransform.Get(); }
	void SetRenderTransform(TAttribute<TOptional<FTransform2D>> InTransform) { RenderTransform.Assign(InTransform); }
	void SetRenderTransformPivot(FVector2 Pivot);
	float GetRenderOpacity() const { return RenderOpacity; }
	void SetRenderOpacity(float Opacity);

	FName GetTag() const { return Tag; }
	void SetTag(FName InTag) { Tag = InTag; }
	// Readable type name for logs and tests ("SButton").
	FString GetTypeName() const;

	TSharedPtr<IToolTip> GetToolTip();
	void SetToolTip(TSharedPtr<IToolTip> InToolTip) { ToolTip = std::move(InToolTip); ToolTipText = {}; }
	void SetToolTipText(TAttribute<FString> Text);

	TOptional<EMouseCursor> GetCursor() const { return CursorAttribute.Get(); }
	void SetCursor(TAttribute<TOptional<EMouseCursor>> InCursor) { CursorAttribute.Assign(InCursor); }

	bool IsHovered() const { return bIsHovered; }
	bool IsDirectlyHovered() const { return bIsDirectlyHovered; }
	// Set by the application as the pointer moves.
	void SetHovered(bool bHovered, bool bDirectly);

	bool HasKeyboardFocus() const;
	bool HasFocusedDescendants() const;
	bool HasMouseCapture() const;

	// Widgets that can hold keyboard focus override this.
	virtual bool SupportsKeyboardFocus() const { return false; }

	// --- Timers and ticking -------------------------------------------------

	// Calls Delegate every Period seconds (0: every frame) until it returns
	// Stop or is unregistered. Active timers keep the frame loop running.
	TSharedRef<FActiveTimerHandle> RegisterActiveTimer(float Period, FWidgetActiveTimerDelegate Delegate);
	void UnRegisterActiveTimer(const TSharedRef<FActiveTimerHandle>& Handle);

	// Ticking widgets get Tick every frame while they are in a visible window.
	void SetCanTick(bool bInCanTick);
	bool GetCanTick() const { return bCanTick; }
	virtual void Tick(const FGeometry& AllottedGeometry, double CurrentTime, float DeltaTime) { (void)AllottedGeometry; (void)CurrentTime; (void)DeltaTime; }

	// Evaluates bound attributes (once per frame, by the application).
	static void UpdateAllBoundAttributes();
	void RegisterBoundAttribute(FWidgetAttributeBase* Attribute);
	void UnregisterBoundAttribute(FWidgetAttributeBase* Attribute);

	// --- Input (Engine/Documentation/Source/GenesisUI.md §7) -------------------------------------

	// Pointer events tunnel root -> leaf (preview) and then bubble leaf -> root.
	virtual FReply OnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) { (void)Geometry; (void)Event; return FReply::Unhandled(); }
	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) { (void)Geometry; (void)Event; return FReply::Unhandled(); }
	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) { (void)Geometry; (void)Event; return FReply::Unhandled(); }
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) { (void)Geometry; (void)Event; return FReply::Unhandled(); }
	virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) { (void)Geometry; (void)Event; return FReply::Unhandled(); }
	virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) { (void)Geometry; (void)Event; return FReply::Unhandled(); }
	virtual void OnMouseEnter(const FGeometry& Geometry, const FPointerEvent& Event) { (void)Geometry; (void)Event; }
	virtual void OnMouseLeave(const FPointerEvent& Event) { (void)Event; }
	virtual void OnMouseCaptureLost() {}
	// Unaccelerated mouse motion (device units) while this widget holds the
	// capture with high-precision movement (FReply::UseHighPrecisionMouseMovement).
	virtual FReply OnRawMouseMove(FVector2 Delta) { (void)Delta; return FReply::Unhandled(); }
	virtual FCursorReply OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const;

	// Key events route along the focus path: preview root -> leaf, then leaf -> root.
	virtual FReply OnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) { (void)Geometry; (void)Event; return FReply::Unhandled(); }
	virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) { (void)Geometry; (void)Event; return FReply::Unhandled(); }
	virtual FReply OnKeyUp(const FGeometry& Geometry, const FKeyEvent& Event) { (void)Geometry; (void)Event; return FReply::Unhandled(); }
	virtual FReply OnKeyChar(const FGeometry& Geometry, const FCharacterEvent& Event) { (void)Geometry; (void)Event; return FReply::Unhandled(); }

	virtual FReply OnFocusReceived(const FGeometry& Geometry, const FFocusEvent& Event) { (void)Geometry; (void)Event; return FReply::Unhandled(); }
	virtual void OnFocusLost(const FFocusEvent& Event) { (void)Event; }
	// Sent to every widget on the old and the new focus path.
	virtual void OnFocusChanging(const SWidget* PreviousFocus, const SWidget* NewFocus, const FFocusEvent& Event) { (void)PreviousFocus; (void)NewFocus; (void)Event; }

	// How navigation leaves this widget (default: ask the parent).
	virtual FNavigationReply OnNavigation(const FGeometry& Geometry, const FNavigationEvent& Event) { (void)Geometry; (void)Event; return FNavigationReply::Escape(); }

	virtual FReply OnDragDetected(const FGeometry& Geometry, const FPointerEvent& Event) { (void)Geometry; (void)Event; return FReply::Unhandled(); }
	virtual void OnDragEnter(const FGeometry& Geometry, const FDragDropEvent& Event) { (void)Geometry; (void)Event; }
	virtual void OnDragLeave(const FDragDropEvent& Event) { (void)Event; }
	virtual FReply OnDragOver(const FGeometry& Geometry, const FDragDropEvent& Event) { (void)Geometry; (void)Event; return FReply::Unhandled(); }
	virtual FReply OnDrop(const FGeometry& Geometry, const FDragDropEvent& Event) { (void)Geometry; (void)Event; return FReply::Unhandled(); }

	// Borderless windows ask the widgets under the pointer what part of the
	// window they are (title bar, client area...); Unspecified defers to the parent.
	virtual EWindowZone GetWindowZoneOverride() const { return EWindowZone::Unspecified; }

	// Scrollable containers scroll so Descendant is visible; return true if
	// this widget scrolled.
	virtual bool ScrollDescendantIntoView(const SWidget& Descendant) { (void)Descendant; return false; }

protected:
	// Desired size in local units, given the children's desired sizes.
	virtual FVector2 ComputeDesiredSize(float LayoutScale) const = 0;
	// Gives each visible child its geometry.
	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const = 0;
	// Draws the widget; returns the highest layer used.
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const = 0;
	// Called during ArrangeTree before OnArrangeChildren, once the widget's
	// geometry is known: virtualizing panels create the children they need
	// here (and Measure them), text wraps to the new width.
	virtual void OnPreArrange(const FGeometry& AllottedGeometry) { (void)AllottedGeometry; }

	// Paints the arranged children at LayerId; returns the highest layer used.
	int32 PaintArrangedChildren(const FPaintArgs& Args, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const;

	template <typename T>
	TSharedRef<T> SharedThis(T* This) { return TSharedRef<T>(StaticCastSharedPtr<T>(This->shared_from_this())); }
	template <typename T>
	TSharedRef<const T> SharedThis(const T* This) const { return TSharedRef<const T>(std::static_pointer_cast<const T>(This->shared_from_this())); }

	bool bIsNullWidget = false;

private:
	friend class SNullWidgetImpl;

	SWidget* ParentPtr = nullptr;
	// Before the attributes below: they unregister from it when destroyed.
	TArray<FWidgetAttributeBase*> BoundAttributes;

	TWidgetAttribute<EVisibility, EInvalidateWidgetReason::Visibility> Visibility{ *this, EVisibility::Visible };
	TWidgetAttribute<bool, EInvalidateWidgetReason::Paint> EnabledState{ *this, true };
	TWidgetAttribute<TOptional<EMouseCursor>, EInvalidateWidgetReason::None> CursorAttribute{ *this };
	TWidgetAttribute<TOptional<FTransform2D>, EInvalidateWidgetReason::RenderTransform> RenderTransform{ *this };
	FVector2 RenderTransformPivot = FVector2(0.5f, 0.5f);
	float RenderOpacity = 1.0f;
	EWidgetClipping Clipping = EWidgetClipping::Inherit;
	FName Tag;

	TSharedPtr<IToolTip> ToolTip;
	TAttribute<FString> ToolTipText;

	FVector2 DesiredSize;
	float MeasuredScale = 0.0f;
	FGeometry AllottedGeometryCache;
	FGeometry CachedGeometry;
	TArray<FArrangedWidget> ArrangedChildrenCache;

	bool bNeedsMeasure = true;
	bool bNeedsArrange = true;
	bool bDescendantNeedsArrange = false;
	bool bNeedsRepaint = true;
	bool bVolatile = false;
	bool bCanTick = false;
	bool bIsHovered = false;
	bool bIsDirectlyHovered = false;
	bool bEverArranged = false;
};

template <typename T, EInvalidateWidgetReason Reason>
TWidgetAttribute<T, Reason>::~TWidgetAttribute()
{
	if (Binding.IsBound())
	{
		Owner.UnregisterBoundAttribute(this);
	}
}

template <typename T, EInvalidateWidgetReason Reason>
void TWidgetAttribute<T, Reason>::Assign(const TAttribute<T>& Attribute)
{
	if (!Attribute.IsSet())
	{
		return;
	}
	const bool bWasBound = Binding.IsBound();
	if (Attribute.IsBound())
	{
		Binding = Attribute;
		if (!bWasBound)
		{
			Owner.RegisterBoundAttribute(this);
		}
	}
	else
	{
		Binding = TAttribute<T>();
		if (bWasBound)
		{
			Owner.UnregisterBoundAttribute(this);
		}
	}
	SetValue(Attribute.Get());
}

template <typename T, EInvalidateWidgetReason Reason>
void TWidgetAttribute<T, Reason>::Set(T NewValue)
{
	if (Binding.IsBound())
	{
		Binding = TAttribute<T>();
		Owner.UnregisterBoundAttribute(this);
	}
	SetValue(std::move(NewValue));
}

template <typename T, EInvalidateWidgetReason Reason>
void TWidgetAttribute<T, Reason>::SetValue(T NewValue)
{
	if (IsSame(NewValue))
	{
		return;
	}
	Value = std::move(NewValue);
	Owner.Invalidate(Reason);
}
