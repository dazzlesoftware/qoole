// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Widgets/SWidget.h"

#include "Layout/Children.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

#include <typeinfo>

namespace
{
	// Widgets with bound attributes, evaluated once per frame. Raw pointers:
	// a widget removes itself in its destructor.
	TSet<SWidget*>& WidgetsWithBindings()
	{
		static TSet<SWidget*> Set;
		return Set;
	}
}

const char* ToString(EVisibility Visibility)
{
	switch (Visibility)
	{
	case EVisibility::Visible: return "Visible";
	case EVisibility::Collapsed: return "Collapsed";
	case EVisibility::Hidden: return "Hidden";
	case EVisibility::HitTestInvisible: return "HitTestInvisible";
	case EVisibility::SelfHitTestInvisible: return "SelfHitTestInvisible";
	}
	return "?";
}

const char* ToString(EFocusCause Cause)
{
	switch (Cause)
	{
	case EFocusCause::Mouse: return "Mouse";
	case EFocusCause::Navigation: return "Navigation";
	case EFocusCause::SetDirectly: return "SetDirectly";
	case EFocusCause::Cleared: return "Cleared";
	case EFocusCause::OtherWidgetLostFocus: return "OtherWidgetLostFocus";
	case EFocusCause::WindowActivate: return "WindowActivate";
	}
	return "?";
}

const char* ToString(EUINavigation Navigation)
{
	switch (Navigation)
	{
	case EUINavigation::Left: return "Left";
	case EUINavigation::Right: return "Right";
	case EUINavigation::Up: return "Up";
	case EUINavigation::Down: return "Down";
	case EUINavigation::Next: return "Next";
	case EUINavigation::Previous: return "Previous";
	case EUINavigation::Invalid: return "Invalid";
	}
	return "?";
}

SWidget::SWidget() = default;

SWidget::~SWidget()
{
	if (!BoundAttributes.empty())
	{
		WidgetsWithBindings().erase(this);
	}
	// Children that outlive us stop pointing back when our slots are destroyed.
}

void SWidget::SWidgetConstruct(const FWidgetCommonArgs& Args)
{
	Visibility.Assign(Args._Visibility);
	EnabledState.Assign(Args._IsEnabled);
	CursorAttribute.Assign(Args._Cursor);
	RenderTransform.Assign(Args._RenderTransform);
	RenderTransformPivot = Args._RenderTransformPivot;
	Clipping = Args._Clipping;
	Tag = Args._Tag;
	if (Args._RenderOpacity)
	{
		RenderOpacity = *Args._RenderOpacity;
	}
	if (Args._ToolTip)
	{
		ToolTip = Args._ToolTip;
	}
	else if (Args._ToolTipText.IsSet())
	{
		ToolTipText = Args._ToolTipText;
	}
	if (Args._ForceVolatile)
	{
		SetVolatile(true);
	}
}

// ---------------------------------------------------------------------------
// Layout

void SWidget::Measure(float LayoutScale)
{
	if (!bNeedsMeasure && LayoutScale == MeasuredScale)
	{
		return;
	}
	if (FChildren* Children = GetChildren())
	{
		for (int32 I = 0; I < Children->Num(); ++I)
		{
			const TSharedRef<SWidget>& Child = Children->GetChildAt(I);
			if (IsVisibleForLayout(Child->GetVisibility()))
			{
				Child->Measure(LayoutScale * GetRelativeLayoutScale(I, LayoutScale));
			}
		}
	}
	DesiredSize = ComputeDesiredSize(LayoutScale);
	MeasuredScale = LayoutScale;
	bNeedsMeasure = false;
}

FVector2 SWidget::GetDesiredSize() const
{
	return IsVisibleForLayout(GetVisibility()) ? DesiredSize : FVector2::Zero();
}

void SWidget::ArrangeTree(const FGeometry& AllottedGeometry)
{
	if (!bNeedsArrange && bEverArranged && AllottedGeometry == AllottedGeometryCache)
	{
		if (bDescendantNeedsArrange)
		{
			bDescendantNeedsArrange = false;
			for (const FArrangedWidget& Child : TArray<FArrangedWidget>(ArrangedChildrenCache))
			{
				Child.Widget->ArrangeTree(Child.Geometry);
			}
		}
		return;
	}

	AllottedGeometryCache = AllottedGeometry;
	const TOptional<FTransform2D>& Transform = RenderTransform.Get();
	CachedGeometry = Transform ? AllottedGeometry.WithRenderTransform(*Transform, RenderTransformPivot) : AllottedGeometry;
	bNeedsArrange = false;
	bDescendantNeedsArrange = false;
	bEverArranged = true;

	OnPreArrange(CachedGeometry);

	FArrangedChildren Arranged;
	OnArrangeChildren(CachedGeometry, Arranged);
	ArrangedChildrenCache = std::move(Arranged.Widgets);
	// Arrange from a copy: a child's OnPreArrange may change our children.
	for (const FArrangedWidget& Child : TArray<FArrangedWidget>(ArrangedChildrenCache))
	{
		Child.Widget->ArrangeTree(Child.Geometry);
	}
}

// ---------------------------------------------------------------------------
// Paint and hit-testing

int32 SWidget::Paint(const FPaintArgs& Args, const FRect& CullingRect, FDrawElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	const EVisibility V = GetVisibility();
	if (!IsPainted(V) || !bEverArranged || RenderOpacity <= 0.0f)
	{
		return LayerId;
	}

	const FGeometry& Geometry = CachedGeometry;
	const FRect Bounds = Geometry.GetRenderBoundingRect();
	const bool bClips = Clipping == EWidgetClipping::ClipToBounds && Geometry.RenderTransform.IsAxisAligned();
	const FChildren* Children = GetChildren();
	const bool bIsLeaf = !Children || Children->Num() == 0;
	// Children of a non-clipping widget may draw outside it, so only leaves
	// and clipping widgets are culled by their own bounds.
	if ((bClips || bIsLeaf) && !Bounds.Intersects(CullingRect))
	{
		return LayerId;
	}

	FPaintArgs MyArgs = Args;
	MyArgs.bEnabled = Args.bEnabled && IsEnabled();

	FWidgetStyle MyStyle = InWidgetStyle;
	if (RenderOpacity < 1.0f)
	{
		MyStyle.BlendColorAndOpacityTint(FColor(255, 255, 255, uint8(FMath::RoundToInt(RenderOpacity * 255.0f))));
	}

	FRect MyCulling = CullingRect;
	if (bClips)
	{
		OutDrawElements.PushClip(Geometry.ToPaintGeometry());
		MyCulling = CullingRect.IntersectionWith(Bounds);
	}
	const int32 MaxLayer = OnPaint(MyArgs, Geometry, MyCulling, OutDrawElements, LayerId, MyStyle);
	if (bClips)
	{
		OutDrawElements.PopClip();
	}
	return FMath::Max(MaxLayer, LayerId);
}

int32 SWidget::PaintArrangedChildren(const FPaintArgs& Args, const FRect& CullingRect, FDrawElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	int32 MaxLayer = LayerId;
	for (const FArrangedWidget& Child : ArrangedChildrenCache)
	{
		MaxLayer = FMath::Max(MaxLayer, Child.Widget->Paint(Args, CullingRect, OutDrawElements, LayerId, InWidgetStyle));
	}
	return MaxLayer;
}

bool SWidget::HitTest(FVector2 Position, TArray<TSharedRef<SWidget>>& OutPath)
{
	const EVisibility V = GetVisibility();
	if (!IsPainted(V) || !bEverArranged || V == EVisibility::HitTestInvisible)
	{
		return false;
	}
	const bool bInside = CachedGeometry.IsUnderLocation(Position);
	if (!bInside && Clipping == EWidgetClipping::ClipToBounds)
	{
		return false;
	}

	OutPath.push_back(TSharedRef<SWidget>(shared_from_this()));
	if (AreChildrenHitTestable(V))
	{
		// Topmost (last painted) first.
		for (int32 I = static_cast<int32>(ArrangedChildrenCache.size()) - 1; I >= 0; --I)
		{
			const TSharedRef<SWidget> Child = ArrangedChildrenCache[I].Widget;
			if (Child->HitTest(Position, OutPath))
			{
				return true;
			}
		}
	}
	if (bInside && IsSelfHitTestable(V))
	{
		return true;
	}
	OutPath.pop_back();
	return false;
}

// ---------------------------------------------------------------------------
// Invalidation

void SWidget::Invalidate(EInvalidateWidgetReason Reason)
{
	if (Reason == EInvalidateWidgetReason::None || bIsNullWidget)
	{
		return;
	}
	const bool bLayout = HasAnyFlags(Reason, EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Visibility | EInvalidateWidgetReason::ChildOrder);
	if (bLayout)
	{
		bNeedsMeasure = true;
		bNeedsArrange = true;
	}
	else if (HasAnyFlags(Reason, EInvalidateWidgetReason::RenderTransform))
	{
		bNeedsArrange = true;
	}

	SWidget* Widget = this;
	for (SWidget* Ancestor = ParentPtr; Ancestor; Ancestor = Ancestor->ParentPtr)
	{
		if (bLayout)
		{
			Ancestor->bNeedsMeasure = true;
			Ancestor->bNeedsArrange = true;
		}
		else if (HasAnyFlags(Reason, EInvalidateWidgetReason::RenderTransform))
		{
			Ancestor->bDescendantNeedsArrange = true;
		}
		Widget = Ancestor;
	}
	// Every change repaints the window it is in.
	Widget->bNeedsRepaint = true;
}

void SWidget::SetVolatile(bool bInVolatile)
{
	if (bVolatile == bInVolatile)
	{
		return;
	}
	bVolatile = bInVolatile;
	if (bVolatile && FUIApplicationBase::IsInitialized())
	{
		FUIApplicationBase::Get().RegisterVolatileWidget(TSharedRef<SWidget>(shared_from_this()));
	}
}

// ---------------------------------------------------------------------------
// Tree

SWidget* SWidget::GetRootWidget()
{
	SWidget* Widget = this;
	while (Widget->ParentPtr)
	{
		Widget = Widget->ParentPtr;
	}
	return Widget;
}

bool SWidget::IsAncestorOf(const SWidget* Other) const
{
	for (const SWidget* W = Other ? Other->ParentPtr : nullptr; W; W = W->ParentPtr)
	{
		if (W == this)
		{
			return true;
		}
	}
	return false;
}

void SWidget::SetParentWidget(SWidget* NewParent)
{
	if (bIsNullWidget || ParentPtr == NewParent)
	{
		return;
	}
	ParentPtr = NewParent;
	// New surroundings: lay out again (the new parent invalidates itself).
	bNeedsMeasure = true;
	bNeedsArrange = true;
	bEverArranged = false;
}

// ---------------------------------------------------------------------------
// State

bool SWidget::IsVisibleInHierarchy() const
{
	for (const SWidget* W = this; W; W = W->ParentPtr)
	{
		if (!IsPainted(W->GetVisibility()))
		{
			return false;
		}
	}
	return true;
}

bool SWidget::IsEnabledInHierarchy() const
{
	for (const SWidget* W = this; W; W = W->ParentPtr)
	{
		if (!W->IsEnabled())
		{
			return false;
		}
	}
	return true;
}

void SWidget::SetClipping(EWidgetClipping InClipping)
{
	if (Clipping != InClipping)
	{
		Clipping = InClipping;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SWidget::SetRenderTransformPivot(FVector2 Pivot)
{
	if (!(RenderTransformPivot == Pivot))
	{
		RenderTransformPivot = Pivot;
		Invalidate(EInvalidateWidgetReason::RenderTransform);
	}
}

void SWidget::SetRenderOpacity(float Opacity)
{
	if (RenderOpacity != Opacity)
	{
		RenderOpacity = Opacity;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FString SWidget::GetTypeName() const
{
	FString Name(typeid(*this).name());
	// MSVC: "class SButton" or "class SListView<int>".
	if (Name.StartsWith("class "))
	{
		Name = Name.Mid(6);
	}
	return Name;
}

TSharedPtr<IToolTip> SWidget::GetToolTip()
{
	if (!ToolTip && ToolTipText.IsSet() && FUIApplicationBase::IsInitialized())
	{
		ToolTip = FUIApplicationBase::Get().MakeToolTip(ToolTipText).ToSharedPtr();
	}
	return ToolTip;
}

void SWidget::SetToolTipText(TAttribute<FString> Text)
{
	ToolTip.reset();
	ToolTipText = std::move(Text);
}

void SWidget::SetHovered(bool bHovered, bool bDirectly)
{
	bIsDirectlyHovered = bHovered && bDirectly;
	if (bIsHovered != bHovered)
	{
		bIsHovered = bHovered;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

bool SWidget::HasKeyboardFocus() const
{
	return FUIApplicationBase::IsInitialized() && FUIApplicationBase::Get().HasFocus(this);
}

bool SWidget::HasFocusedDescendants() const
{
	return FUIApplicationBase::IsInitialized() && FUIApplicationBase::Get().HasFocusedDescendants(this);
}

bool SWidget::HasMouseCapture() const
{
	return FUIApplicationBase::IsInitialized() && FUIApplicationBase::Get().HasMouseCapture(this);
}

FCursorReply SWidget::OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const
{
	(void)Geometry;
	(void)Event;
	const TOptional<EMouseCursor>& Value = CursorAttribute.Get();
	return Value ? FCursorReply::Cursor(*Value) : FCursorReply::Unhandled();
}

// ---------------------------------------------------------------------------
// Timers, ticking, bound attributes

TSharedRef<FActiveTimerHandle> SWidget::RegisterActiveTimer(float Period, FWidgetActiveTimerDelegate Delegate)
{
	const double Now = FUIApplicationBase::IsInitialized() ? FUIApplicationBase::Get().GetTime() : 0.0;
	auto Handle = MakeShared<FActiveTimerHandle>(Period, std::move(Delegate), Now + Period);
	if (FUIApplicationBase::IsInitialized())
	{
		FUIApplicationBase::Get().RegisterActiveTimer(TSharedRef<SWidget>(shared_from_this()), Handle);
	}
	return Handle;
}

void SWidget::UnRegisterActiveTimer(const TSharedRef<FActiveTimerHandle>& Handle)
{
	Handle->bPendingRemoval = true;
	if (FUIApplicationBase::IsInitialized())
	{
		FUIApplicationBase::Get().UnRegisterActiveTimer(TSharedRef<SWidget>(shared_from_this()), Handle);
	}
}

void SWidget::SetCanTick(bool bInCanTick)
{
	if (bCanTick == bInCanTick)
	{
		return;
	}
	bCanTick = bInCanTick;
	if (bCanTick && FUIApplicationBase::IsInitialized())
	{
		FUIApplicationBase::Get().RegisterTickingWidget(TSharedRef<SWidget>(shared_from_this()));
	}
}

void SWidget::RegisterBoundAttribute(FWidgetAttributeBase* Attribute)
{
	if (BoundAttributes.empty())
	{
		WidgetsWithBindings().insert(this);
	}
	BoundAttributes.push_back(Attribute);
}

void SWidget::UnregisterBoundAttribute(FWidgetAttributeBase* Attribute)
{
	ArrayUtil::Remove(BoundAttributes, Attribute);
	if (BoundAttributes.empty())
	{
		WidgetsWithBindings().erase(this);
	}
}

void SWidget::UpdateAllBoundAttributes()
{
	TSet<SWidget*>& Set = WidgetsWithBindings();
	const TArray<SWidget*> Snapshot(Set.begin(), Set.end());
	for (SWidget* Widget : Snapshot)
	{
		// A binding may destroy widgets; skip those that are gone.
		if (!Set.contains(Widget))
		{
			continue;
		}
		EInvalidateWidgetReason Changed = EInvalidateWidgetReason::None;
		for (FWidgetAttributeBase* Attribute : TArray<FWidgetAttributeBase*>(Widget->BoundAttributes))
		{
			Changed |= Attribute->UpdateBinding();
		}
		Widget->Invalidate(Changed);
	}
}

// ---------------------------------------------------------------------------
// SLeafWidget, SCompoundWidget, SNullWidget, slots

FChildren* SLeafWidget::GetChildren()
{
	static FNoChildren NoChildren;
	return &NoChildren;
}

FVector2 SCompoundWidget::ComputeDesiredSize(float LayoutScale) const
{
	(void)LayoutScale;
	const TSharedRef<SWidget>& Child = ChildSlot.GetWidget();
	if (!IsVisibleForLayout(Child->GetVisibility()))
	{
		return ChildSlot.GetPadding().GetDesiredSize();
	}
	return Child->GetDesiredSize() + ChildSlot.GetPadding().GetDesiredSize();
}

void SCompoundWidget::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	const FMargin& Padding = ChildSlot.GetPadding();
	const TSharedRef<SWidget>& Child = ChildSlot.GetWidget();
	const FVector2 Desired = Child->GetDesiredSize();
	const float AvailX = FMath::Max(0.0f, AllottedGeometry.Size.X - Padding.Left - Padding.Right);
	const float AvailY = FMath::Max(0.0f, AllottedGeometry.Size.Y - Padding.Top - Padding.Bottom);
	const auto Align = [](float Avail, float Want, int32 Mode) -> FVector2 // (offset, size); Mode 0 fill, 1 start, 2 center, 3 end
	{
		const float Size = Mode == 0 ? Avail : FMath::Min(Want, Avail);
		const float Offset = Mode == 2 ? (Avail - Size) * 0.5f : (Mode == 3 ? Avail - Size : 0.0f);
		return { Offset, Size };
	};
	const FVector2 X = Align(AvailX, Desired.X, static_cast<int32>(ChildSlot.GetHAlign()));
	const FVector2 Y = Align(AvailY, Desired.Y, static_cast<int32>(ChildSlot.GetVAlign()));
	ArrangedChildren.AddWidget(Child->GetVisibility(),
		FArrangedWidget(Child, AllottedGeometry.MakeChild(FVector2(Padding.Left + X.X, Padding.Top + Y.X), FVector2(X.Y, Y.Y))));
}

FWidgetStyle SCompoundWidget::MakeChildStyle(const FWidgetStyle& InWidgetStyle) const
{
	FWidgetStyle Style = InWidgetStyle;
	Style.BlendColorAndOpacityTint(ColorAndOpacity.Get());
	if (const TOptional<FStyleColor>& Foreground = ForegroundColor.Get())
	{
		Style.SetForegroundColor(Foreground->Resolve());
	}
	return Style;
}

int32 SCompoundWidget::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FDrawElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	(void)Geometry;
	return PaintArrangedChildren(Args, CullingRect, OutDrawElements, LayerId, MakeChildStyle(InWidgetStyle));
}

class SNullWidgetImpl final : public SLeafWidget
{
public:
	SNullWidgetImpl()
	{
		bIsNullWidget = true;
		Visibility.Set(EVisibility::SelfHitTestInvisible);
	}

protected:
	FVector2 ComputeDesiredSize(float) const override { return FVector2::Zero(); }
	int32 OnPaint(const FPaintArgs&, const FGeometry&, const FRect&, FDrawElementList&, int32 LayerId, const FWidgetStyle&) const override { return LayerId; }
};

TSharedRef<SWidget> SNullWidget::NullWidget()
{
	static const TSharedRef<SWidget> Instance(std::make_shared<SNullWidgetImpl>());
	return Instance;
}

FSlotBase::FSlotBase()
	: Widget(SNullWidget::NullWidget())
{
}

FSlotBase::FSlotBase(FSlotBase&& Other) noexcept
	: Owner(nullptr)
	, Widget(Other.Widget)
{
	check(!Other.Owner); // only free-standing slots move
}

FSlotBase& FSlotBase::operator=(FSlotBase&& Other) noexcept
{
	check(!Owner && !Other.Owner);
	Widget = Other.Widget;
	return *this;
}

FSlotBase::~FSlotBase()
{
	if (Owner && Widget->GetParentWidget() == Owner)
	{
		Widget->SetParentWidget(nullptr);
	}
}

void FSlotBase::AttachWidget(TSharedRef<SWidget> InWidget)
{
	if (Owner && Widget->GetParentWidget() == Owner)
	{
		Widget->SetParentWidget(nullptr);
	}
	Widget = std::move(InWidget);
	if (Owner)
	{
		Widget->SetParentWidget(Owner);
		Owner->Invalidate(EInvalidateWidgetReason::ChildOrder);
	}
}

TSharedRef<SWidget> FSlotBase::DetachWidget()
{
	TSharedRef<SWidget> Old = Widget;
	AttachWidget(SNullWidget::NullWidget());
	return Old;
}

void FSlotBase::SetOwner(SWidget* InOwner)
{
	if (Owner && Widget->GetParentWidget() == Owner)
	{
		Widget->SetParentWidget(nullptr);
	}
	Owner = InOwner;
	if (Owner)
	{
		Widget->SetParentWidget(Owner);
	}
}

void FSlotBase::InvalidateSlot(EInvalidateWidgetReason Reason) const
{
	if (Owner)
	{
		Owner->Invalidate(Reason);
	}
}

const TSharedRef<SWidget>& FNoChildren::GetChildAt(int32) const
{
	check(false); // has no children
	static const TSharedRef<SWidget> Null = SNullWidget::NullWidget();
	return Null;
}

const FSlotBase& FNoChildren::GetSlotAt(int32) const
{
	check(false); // has no children
	static const FSimpleSlot Slot;
	return Slot;
}
