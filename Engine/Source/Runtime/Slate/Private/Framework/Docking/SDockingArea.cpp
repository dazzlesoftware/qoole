// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Framework/Docking/SDockingArea.h"
#include "Containers/ContainerAliases.h"
#include "Templates/SharedPointer.h"
#include "Internationalization/TextLocalization.h"

#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const FDockTabStyle& DockStyle()
	{
		return FAppStyle::Get().GetWidgetStyle<FDockTabStyle>("DockTab");
	}
} // namespace

// ---------------------------------------------------------------------------
// SDockingNode, SDockingSplitter

float SDockingNode::GetSizeCoefficient() const
{
	return ParentSplitter ? ParentSplitter->GetCoefficient(*this) : StoredCoefficient;
}

SDockingArea* SDockingNode::GetDockArea()
{
	SDockingNode* Node = this;
	while (Node->ParentSplitter)
	{
		Node = Node->ParentSplitter;
	}
	return dynamic_cast<SDockingArea*>(Node);
}

void SDockingSplitter::Construct(const FArguments& Args)
{
	Orientation = Args._Orientation;
	Rebuild();
}

int32 SDockingSplitter::IndexOf(const SDockingNode& Node) const
{
	for (int32 I = 0; I < static_cast<int32>(ChildNodes.size()); ++I)
	{
		if (&ChildNodes[I].Get() == &Node)
		{
			return I;
		}
	}
	return INDEX_NONE;
}

void SDockingSplitter::AddChildNode(const TSharedRef<SDockingNode>& Node, float Coefficient, int32 Index)
{
	if (Index < 0 || Index > static_cast<int32>(ChildNodes.size()))
	{
		Index = static_cast<int32>(ChildNodes.size());
	}
	ChildNodes.insert(ChildNodes.begin() + Index, Node);
	Coefficients.insert(Coefficients.begin() + Index, FMath::Max(0.01f, Coefficient));
	Node->ParentSplitter = this;
	Rebuild();
}

void SDockingSplitter::RemoveChildNode(const SDockingNode& Node)
{
	const int32 Index = IndexOf(Node);
	if (Index == INDEX_NONE)
	{
		return;
	}
	ChildNodes[Index]->StoredCoefficient = Coefficients[Index];
	ChildNodes[Index]->ParentSplitter = nullptr;
	ChildNodes.erase(ChildNodes.begin() + Index);
	Coefficients.erase(Coefficients.begin() + Index);
	Rebuild();
}

void SDockingSplitter::ReplaceChildNode(const SDockingNode& Old, const TSharedRef<SDockingNode>& New)
{
	const int32 Index = IndexOf(Old);
	if (Index == INDEX_NONE)
	{
		return;
	}
	ChildNodes[Index]->ParentSplitter = nullptr;
	ChildNodes[Index] = New;
	New->ParentSplitter = this;
	Rebuild();
}

float SDockingSplitter::GetCoefficient(const SDockingNode& Node) const
{
	const int32 Index = IndexOf(Node);
	if (Index == INDEX_NONE)
	{
		return 1.0f;
	}
	// The splitter's slots hold the live values (the user may have dragged).
	return Splitter && Index < Splitter->NumSlots() ? Splitter->GetSlot(Index).SizeValue : Coefficients[Index];
}

void SDockingSplitter::SetCoefficient(const SDockingNode& Node, float Coefficient)
{
	const int32 Index = IndexOf(Node);
	if (Index != INDEX_NONE)
	{
		Coefficients[Index] = FMath::Max(0.01f, Coefficient);
		if (Splitter && Index < Splitter->NumSlots())
		{
			Splitter->GetSlot(Index).Value(Coefficients[Index]);
		}
	}
}

void SDockingSplitter::Simplify()
{
	// Pull live sizes first.
	for (int32 I = 0; I < static_cast<int32>(ChildNodes.size()); ++I)
	{
		Coefficients[I] = GetCoefficient(ChildNodes[I].Get());
	}
	bool bChanged = false;
	for (int32 I = 0; I < static_cast<int32>(ChildNodes.size());)
	{
		SDockingNode& Child = ChildNodes[I].Get();
		if (Child.IsStack())
		{
			++I;
			continue;
		}
		SDockingSplitter& Sub = static_cast<SDockingSplitter&>(Child);
		Sub.Simplify();
		if (Sub.ChildNodes.empty())
		{
			Sub.ParentSplitter = nullptr;
			ChildNodes.erase(ChildNodes.begin() + I);
			Coefficients.erase(Coefficients.begin() + I);
			bChanged = true;
			continue;
		}
		if (Sub.ChildNodes.size() == 1 || Sub.Orientation == Orientation)
		{
			// Fold the sub-splitter's children into this one, sharing its size.
			const float Total = Coefficients[I];
			float SubSum = 0.0f;
			for (int32 J = 0; J < static_cast<int32>(Sub.ChildNodes.size()); ++J)
			{
				SubSum += Sub.GetCoefficient(Sub.ChildNodes[J].Get());
			}
			TArray<TSharedRef<SDockingNode>> Moved = Sub.ChildNodes;
			TArray<float> MovedCoefficients;
			for (const TSharedRef<SDockingNode>& M : Moved)
			{
				MovedCoefficients.push_back(Total * Sub.GetCoefficient(M.Get()) / FMath::Max(SubSum, 0.0001f));
			}
			Sub.ChildNodes.clear();
			Sub.Coefficients.clear();
			Sub.ParentSplitter = nullptr;
			ChildNodes.erase(ChildNodes.begin() + I);
			Coefficients.erase(Coefficients.begin() + I);
			for (SIZE_T M = 0; M < Moved.size(); ++M)
			{
				ChildNodes.insert(ChildNodes.begin() + I + static_cast<int32>(M), Moved[M]);
				Coefficients.insert(Coefficients.begin() + I + static_cast<int32>(M), MovedCoefficients[M]);
				Moved[M]->ParentSplitter = this;
			}
			bChanged = true;
			continue;
		}
		++I;
	}
	if (bChanged)
	{
		Rebuild();
	}
}

void SDockingSplitter::Rebuild()
{
	SAssignNew(Splitter, SSplitter)
	    .Orientation(Orientation)
	    .Style(&FAppStyle::Get().GetWidgetStyle<FSplitterStyle>("Splitter"));
	for (SIZE_T I = 0; I < ChildNodes.size(); ++I)
	{
		Splitter->AddSlot()
		    .Value(Coefficients[I])
		    .MinSize(60.0f)
		    .OnSlotResized(FOnFloatValueChanged::CreateLambda([this](float)
		{
			if (SDockingArea* Area = GetDockArea())
			{
				if (const TSharedPtr<FTabManager> Manager = Area->GetTabManager())
				{
					Manager->NotifyLayoutChanged();
				}
			}
		}))[ChildNodes[I]];
	}
	ChildSlot[ToSharedRef(StaticCastSharedPtr<SWidget>(Splitter))];
}

// ---------------------------------------------------------------------------
// SDockingArea

void SDockingArea::Construct(const FArguments& Args, const TSharedRef<FTabManager>& InTabManager, bool bInPrimary,
    const TSharedPtr<SWindow>& InWindow)
{
	TabManager = InTabManager.ToSharedPtr();
	bPrimary = bInPrimary;
	Window = InWindow;
	Orientation = Args._Orientation;
	Rebuild();
}

void SDockingArea::GatherStacks(TArray<TSharedRef<SDockTabStack>>& Out) const
{
	const TFunction<void(const SDockingSplitter&)> Walk = [&](const SDockingSplitter& Node)
	{
		for (const TSharedRef<SDockingNode>& Child : Node.GetChildNodes())
		{
			if (Child->IsStack())
			{
				Out.push_back(TSharedRef<SDockTabStack>(std::static_pointer_cast<SDockTabStack>(Child.ToSharedPtr())));
			}
			else
			{
				Walk(static_cast<const SDockingSplitter&>(Child.Get()));
			}
		}
	};
	Walk(*this);
}

int32 SDockingArea::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect,
    FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	if (ChildNodes.empty())
	{
		// An empty primary area says how to fill it.
		const FSlateFontInfo Font = FAppStyle::Get().GetFont("NormalFont");
		if (Font.Font && FSlateApplication::IsInitialized())
		{
			const TSharedRef<const FShapedGlyphSequence> Text = FSlateApplication::Get().GetFontCache().ShapeText(
			    "Open a panel from the Window menu", Font, Geometry.Scale);
			const FVector2 Size = Text->GetLogicalSize();
			OutDrawElements.AddText(LayerId, Geometry.ToPaintGeometry((Geometry.Size - Size) * 0.5f, Size), Text,
			    InWidgetStyle.Tinted(FSlateColor(EStyleColor::ForegroundSecondary).Resolve()));
		}
		return LayerId;
	}
	return SDockingSplitter::OnPaint(Args, Geometry, CullingRect, OutDrawElements, LayerId, InWidgetStyle);
}

// ---------------------------------------------------------------------------
// Tab drag and drop

TSharedPtr<SWidget> FDockTabDragDrop::GetDefaultDecorator() const
{
	const FDockTabStyle& Style = DockStyle();
	return SNew(SBorder)
	    .BorderImage(&FAppStyle::Get().GetBrush("DragDecorator"))
	    .Padding(Style.TabPadding)[SNew(STextBlock).Text(Tab ? Tab->GetLabel() : FString()).TextStyle(&Style.Text)];
}

void FDockTabDragDrop::OnDrop(bool bDropWasHandled, const FPointerEvent&)
{
	if (bDropWasHandled || bCancelled || !Tab)
	{
		return;
	}
	// Dropped outside every dock target: a new floating window there.
	if (const TSharedPtr<FTabManager> Manager = Tab->GetTabManager())
	{
		Manager->MoveTabToNewWindow(ToSharedRef(Tab),
		    FIntPoint(FMath::RoundToInt(ScreenPosition.X) - 20, FMath::RoundToInt(ScreenPosition.Y) - 12), SourceSize);
	}
}

// ---------------------------------------------------------------------------
// Tab buttons in the well

namespace
{
	class SDockTabButton final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SDockTabButton)
		SLATE_END_ARGS()

		void Construct(const FArguments&, const TSharedRef<SDockTab>& InTab, SDockTabStack* InStack)
		{
			Tab = InTab.ToSharedPtr();
			Stack = InStack;
			const FDockTabStyle& Style = DockStyle();
			const TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox) +
			    SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock)
			            .Text([this]
			{
				const TSharedPtr<SDockTab> T = Tab.lock();
				return T ? T->GetLabel() : FString();
			}).TextStyle(&Style.Text)];
			if (InTab->IsClosable())
			{
				Row->AddSlot()
				    .AutoWidth()
				    .VAlign(VAlign_Center)
				    .Padding(FMargin(6.0f, 0.0f, -4.0f, 0.0f))[SNew(SButton)
				            .Text(GENESIS_TEXT("×"))
				            .ButtonStyle(&Style.CloseButton)
				            .IsFocusable(false)
				            .ToolTipText(GENESIS_TEXT("Close"))
				            .Visibility([this]
				{ return IsHovered() || IsForeground() ? EVisibility::Visible : EVisibility::Hidden; })
				            .OnClicked([this]
				{
					if (const TSharedPtr<SDockTab> T = Tab.lock())
					{
						T->RequestCloseTab();
					}
					return FReply::Handled();
				})];
			}
			ChildSlot.Padding(Style.TabPadding)[Row];
		}

		FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event) override
		{
			const TSharedPtr<SDockTab> T = Tab.lock();
			if (!T)
			{
				return FReply::Unhandled();
			}
			if (Event.GetEffectingButton() == EMouseButton::Middle)
			{
				T->RequestCloseTab();
				return FReply::Handled();
			}
			if (Event.GetEffectingButton() != EMouseButton::Left)
			{
				return FReply::Unhandled();
			}
			T->ActivateInParent();
			return FReply::Handled().DetectDrag(SharedThis(this), EMouseButton::Left);
		}

		FReply OnDragDetected(const FGeometry&, const FPointerEvent&) override
		{
			const TSharedPtr<SDockTab> T = Tab.lock();
			if (!T)
			{
				return FReply::Unhandled();
			}
			TSharedPtr<FDockTabDragDrop> Operation = std::make_shared<FDockTabDragDrop>();
			Operation->Tab = T;
			if (const TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWindowForWidget(*Stack))
			{
				Operation->SourceSize =
				    Stack->GetCachedGeometry().GetLayoutBoundingRect().GetSize() / Window->GetScale();
			}
			return FReply::Handled().BeginDragDrop(
			    TSharedRef<FDragDropOperation>(StaticCastSharedPtr<FDragDropOperation>(Operation)));
		}

		EWindowZone GetWindowZoneOverride() const override
		{
			return EWindowZone::ClientArea;
		}

	protected:
		int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect,
		    FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const override
		{
			const FDockTabStyle& Style = DockStyle();
			const bool bForeground = IsForeground();
			const bool bActiveStack = Stack->IsActiveStack();
			const FSlateBrush& Brush = bForeground ? (bActiveStack ? Style.Active : Style.Foreground)
			                                       : (IsHovered() ? Style.Hovered : Style.Normal);
			Brush.Paint(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), InWidgetStyle.ColorAndOpacityTint);
			if (bForeground && bActiveStack)
			{
				Style.ActiveIndicator.Paint(OutDrawElements, LayerId + 1,
				    Geometry.ToPaintGeometry(FRect(0.0f, 0.0f, Geometry.Size.X, 2.0f)),
				    InWidgetStyle.ColorAndOpacityTint);
			}
			FWidgetStyle ChildStyle = MakeChildStyle(InWidgetStyle);
			ChildStyle.SetForegroundColor(
			    FSlateColor(bForeground ? EStyleColor::ForegroundHover : EStyleColor::ForegroundSecondary).Resolve());
			return PaintArrangedChildren(Args, CullingRect, OutDrawElements, LayerId + 1, ChildStyle);
		}

	private:
		bool IsForeground() const
		{
			const TSharedPtr<SDockTab> T = Tab.lock();
			return T && T->IsForeground();
		}

		TWeakPtr<SDockTab> Tab;
		SDockTabStack* Stack = nullptr;
	};
} // namespace

// ---------------------------------------------------------------------------
// SDockTabStack

void SDockTabStack::Construct(const FArguments&, const TSharedRef<FTabManager>& InTabManager)
{
	TabManager = InTabManager.ToSharedPtr();
	Style = &DockStyle();
	SAssignNew(Well, SHorizontalBox);
	const TSharedRef<SWidget> WellWidget = SNew(SBorder)
	                                           .BorderImage(&Style->WellBackground)
	                                           .Padding(0.0f)[
	                                               // Clipped: tabs that do not fit must not draw over the next stack.
	                                               SNew(SBox)
	                                                   .HeightOverride(TOptional<float>(Style->WellHeight))
	                                                   .HAlign(HAlign_Left)
	                                                   .Clipping(EWidgetClipping::ClipToBounds)[ToSharedRef(Well)]];
	WellArea = WellWidget.ToSharedPtr();
	ChildSlot[SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[WellWidget] +
	    SVerticalBox::Slot().FillHeight(
	        1.0f)[SNew(SBorder).BorderImage(&Style->ContentBackground).Padding(0.0f)[SAssignNew(ContentBox, SBox)]]];
}

TSharedPtr<SDockTab> SDockTabStack::GetForegroundTab() const
{
	return Foreground >= 0 && Foreground < NumTabs() ? Tabs[Foreground].ToSharedPtr() : nullptr;
}

int32 SDockTabStack::IndexOfTab(const SDockTab& Tab) const
{
	for (int32 I = 0; I < NumTabs(); ++I)
	{
		if (&Tabs[I].Get() == &Tab)
		{
			return I;
		}
	}
	return INDEX_NONE;
}

bool SDockTabStack::IsActiveStack() const
{
	const TSharedPtr<FTabManager> Manager = TabManager.lock();
	const TSharedPtr<SDockTab> Active = Manager ? Manager->GetActiveTab() : nullptr;
	return Active && Active->GetParentStack().get() == this;
}

void SDockTabStack::AddTab(const TSharedRef<SDockTab>& Tab, int32 Index, bool bMakeForeground)
{
	if (Index < 0 || Index > NumTabs())
	{
		Index = NumTabs();
	}
	Tabs.insert(Tabs.begin() + Index, Tab);
	Tab->SetParentStack(StaticCastSharedPtr<SDockTabStack>(shared_from_this()));
	ArrayUtil::Remove(ClosedTabIds, Tab->GetTabId());
	if (bMakeForeground || Foreground == INDEX_NONE)
	{
		Foreground = Index;
	}
	else if (Foreground >= Index)
	{
		++Foreground;
	}
	RebuildWell();
	UpdateContent();
}

void SDockTabStack::RemoveTab(const SDockTab& Tab)
{
	const int32 Index = IndexOfTab(Tab);
	if (Index == INDEX_NONE)
	{
		return;
	}
	Tabs.erase(Tabs.begin() + Index);
	if (Foreground >= NumTabs() || Foreground > Index)
	{
		Foreground = FMath::Max(NumTabs() > 0 ? 0 : INDEX_NONE, Foreground - 1);
	}
	if (NumTabs() == 0)
	{
		Foreground = INDEX_NONE;
	}
	RebuildWell();
	UpdateContent();
}

void SDockTabStack::BringToFront(const SDockTab& Tab)
{
	const int32 Index = IndexOfTab(Tab);
	if (Index != INDEX_NONE && Index != Foreground)
	{
		Foreground = Index;
		UpdateContent();
		Invalidate(EInvalidateWidgetReason::Paint);
		if (const TSharedPtr<FTabManager> Manager = TabManager.lock())
		{
			Manager->NotifyLayoutChanged();
		}
	}
}

void SDockTabStack::RebuildWell()
{
	Well->ClearChildren();
	for (const TSharedRef<SDockTab>& Tab : Tabs)
	{
		Well->AddSlot().AutoWidth()[SNew(SDockTabButton, Tab, this)];
	}
}

void SDockTabStack::UpdateContent()
{
	const TSharedPtr<SDockTab> Tab = GetForegroundTab();
	ContentBox->SetContent(Tab ? ToSharedRef(StaticCastSharedPtr<SWidget>(Tab)) : SNullWidget::NullWidget());
}

FReply SDockTabStack::OnPreviewMouseButtonDown(const FGeometry&, const FPointerEvent&)
{
	// Clicking anywhere in a stack makes its tab the active one.
	if (const TSharedPtr<SDockTab> Tab = GetForegroundTab())
	{
		if (const TSharedPtr<FTabManager> Manager = TabManager.lock())
		{
			Manager->SetActiveTab(Tab);
		}
	}
	return FReply::Unhandled();
}

SDockTabStack::FDropTarget SDockTabStack::ComputeDropTarget(
    const FGeometry& Geometry, FVector2 AbsolutePosition, const SDockTab& Dragged) const
{
	FDropTarget Target;
	const FVector2 Local = Geometry.AbsoluteToLocal(AbsolutePosition);
	const bool bOwnOnly = Dragged.GetParentStack().get() == this && NumTabs() == 1;
	const float WellHeight = WellArea ? WellArea->GetCachedGeometry().Size.Y : Style->WellHeight;

	if (Local.Y < WellHeight)
	{
		// In the well: between tabs.
		Target.bInWell = true;
		const TArray<FArrangedWidget>& Buttons = Well->GetArrangedChildren();
		TArray<float> Lefts, Rights;
		for (const FArrangedWidget& Button : Buttons)
		{
			const FRect R = Button.Geometry.GetLayoutBoundingRect();
			Lefts.push_back(Geometry.AbsoluteToLocal(R.GetTopLeft()).X);
			Rights.push_back(Geometry.AbsoluteToLocal(R.GetBottomRight()).X);
		}
		Target.WellIndex = 0;
		for (SIZE_T I = 0; I < Lefts.size(); ++I)
		{
			if (Local.X > (Lefts[I] + Rights[I]) * 0.5f)
			{
				Target.WellIndex = static_cast<int32>(I) + 1;
			}
		}
		const float InsertX = Target.WellIndex < static_cast<int32>(Lefts.size())
		    ? Lefts[Target.WellIndex]
		    : (Rights.empty() ? 0.0f : Rights.back());
		Target.bValid = !bOwnOnly;
		Target.PreviewRect = FRect(InsertX - 1.5f, 2.0f, InsertX + 1.5f, WellHeight - 2.0f);
		return Target;
	}

	// On the content: the outer quarter of each side splits, the middle adds.
	const FRect Content(0.0f, WellHeight, Geometry.Size.X, Geometry.Size.Y);
	const float EdgeX = FMath::Min(Content.Width() * 0.25f, 120.0f);
	const float EdgeY = FMath::Min(Content.Height() * 0.25f, 120.0f);
	const FVector2 P = Local;
	if (P.X < Content.Left + EdgeX)
		Target.Side = EUINavigation::Left;
	else if (P.X > Content.Right - EdgeX)
		Target.Side = EUINavigation::Right;
	else if (P.Y < Content.Top + EdgeY)
		Target.Side = EUINavigation::Up;
	else if (P.Y > Content.Bottom - EdgeY)
		Target.Side = EUINavigation::Down;
	const FVector2 Mid = Content.GetCenter();
	switch (Target.Side)
	{
	case EUINavigation::Left:
		Target.PreviewRect = FRect(Content.Left, Content.Top, Mid.X, Content.Bottom);
		break;
	case EUINavigation::Right:
		Target.PreviewRect = FRect(Mid.X, Content.Top, Content.Right, Content.Bottom);
		break;
	case EUINavigation::Up:
		Target.PreviewRect = FRect(Content.Left, Content.Top, Content.Right, Mid.Y);
		break;
	case EUINavigation::Down:
		Target.PreviewRect = FRect(Content.Left, Mid.Y, Content.Right, Content.Bottom);
		break;
	default:
		Target.PreviewRect = Content;
		break;
	}
	const bool bAlreadyHere = Dragged.GetParentStack().get() == this;
	Target.bValid = Target.Side == EUINavigation::Invalid ? !bAlreadyHere : !bOwnOnly;
	return Target;
}

void SDockTabStack::OnDragEnter(const FGeometry&, const FDragDropEvent&) {}

void SDockTabStack::OnDragLeave(const FDragDropEvent&)
{
	if (DragTarget)
	{
		DragTarget.reset();
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FReply SDockTabStack::OnDragOver(const FGeometry& Geometry, const FDragDropEvent& Event)
{
	const TSharedPtr<FDockTabDragDrop> Op = Event.GetOperationAs<FDockTabDragDrop>();
	if (!Op || !Op->Tab)
	{
		return FReply::Unhandled();
	}
	const FDropTarget Target = ComputeDropTarget(Geometry, Event.GetAbsolutePosition(), *Op->Tab);
	const bool bChanged =
	    !DragTarget || DragTarget->bValid != Target.bValid || !(DragTarget->PreviewRect == Target.PreviewRect);
	DragTarget = Target;
	if (bChanged)
	{
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	return FReply::Handled();
}

FReply SDockTabStack::OnDrop(const FGeometry& Geometry, const FDragDropEvent& Event)
{
	const TSharedPtr<FDockTabDragDrop> Op = Event.GetOperationAs<FDockTabDragDrop>();
	const TSharedPtr<FTabManager> Manager = TabManager.lock();
	DragTarget.reset();
	Invalidate(EInvalidateWidgetReason::Paint);
	if (!Op || !Op->Tab || !Manager)
	{
		return FReply::Unhandled();
	}
	const FDropTarget Target = ComputeDropTarget(Geometry, Event.GetAbsolutePosition(), *Op->Tab);
	if (!Target.bValid)
	{
		// Over a stack but nowhere useful: the tab stays where it was.
		return FReply::Handled();
	}
	const TSharedRef<SDockTabStack> Self(std::static_pointer_cast<SDockTabStack>(shared_from_this()));
	if (Target.bInWell)
	{
		Manager->MoveTabToStack(ToSharedRef(Op->Tab), Self, Target.WellIndex);
	}
	else if (Target.Side == EUINavigation::Invalid)
	{
		Manager->MoveTabToStack(ToSharedRef(Op->Tab), Self, INDEX_NONE);
	}
	else
	{
		Manager->MoveTabToNewStack(ToSharedRef(Op->Tab), Self, Target.Side);
	}
	return FReply::Handled();
}

int32 SDockTabStack::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect,
    FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	int32 MaxLayer = SDockingNode::OnPaint(Args, Geometry, CullingRect, OutDrawElements, LayerId, InWidgetStyle);
	if (DragTarget && DragTarget->bValid)
	{
		Style->DropTarget.Paint(OutDrawElements, ++MaxLayer, Geometry.ToPaintGeometry(DragTarget->PreviewRect),
		    InWidgetStyle.ColorAndOpacityTint);
		if (!DragTarget->bInWell)
		{
			// The five targets in the middle of the content.
			const float WellHeight = WellArea ? WellArea->GetCachedGeometry().Size.Y : Style->WellHeight;
			const FVector2 C(Geometry.Size.X * 0.5f, (Geometry.Size.Y + WellHeight) * 0.5f);
			const float S = 22.0f, Gap = 26.0f;
			const FVector2 Offsets[] = {FVector2(0.0f, 0.0f), FVector2(-Gap, 0.0f), FVector2(Gap, 0.0f),
			    FVector2(0.0f, -Gap), FVector2(0.0f, Gap)};
			++MaxLayer;
			for (const FVector2& O : Offsets)
			{
				Style->DropZoneIcon.Paint(OutDrawElements, MaxLayer,
				    Geometry.ToPaintGeometry(C + O - FVector2(S * 0.5f), FVector2(S)),
				    InWidgetStyle.ColorAndOpacityTint);
			}
		}
	}
	return MaxLayer;
}
