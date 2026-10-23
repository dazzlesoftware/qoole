// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Virtualized list and tree views (Engine/Documentation/Source/GenesisUI.md §6.6).
//
//   TArray<FItemPtr> Items;
//   SNew(SListView<FItemPtr>)
//       .ListItemsSource(&Items)
//       .OnGenerateRow([](FItemPtr Item, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
//       {
//           return SNew(STableRow<FItemPtr>, Owner)[ SNew(STextBlock).Text(Item->Name) ];
//       })
//       .OnSelectionChanged(...)
//
// Items are values that identify a row (usually shared pointers); they must
// be hashable and comparable. Only visible rows exist as widgets; rows are
// reused while their item stays visible. Call RequestListRefresh after
// changing an item's content so its row is generated again.
//
// STreeView adds a hierarchy: OnGetChildren lists an item's children, and
// rows of expanded items are followed by their children, indented.

#pragma once

#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Views/STableViewBase.h"

#include <unordered_set>

template <typename ItemType> class ITypedTableView
{
public:
	virtual ~ITypedTableView() = default;
	virtual void Private_OnRowClicked(const ItemType& Item, const FPointerEvent& Event) = 0;
	virtual void Private_OnRowDoubleClicked(const ItemType& Item) = 0;
	virtual bool Private_IsItemSelected(const ItemType& Item) const = 0;
	virtual bool Private_IsViewFocused() const = 0;
	virtual int32 Private_GetNestingDepth(int32 IndexInList) const = 0;
	virtual bool Private_DoesItemHaveChildren(int32 IndexInList) const = 0;
	virtual bool Private_IsItemExpanded(const ItemType& Item) const = 0;
	virtual void Private_SetItemExpansion(const ItemType& Item, bool bExpanded) = 0;
	virtual const FTableRowStyle& Private_GetRowStyle() const = 0;
};

using FOnDragRowDetected = TDelegate<FReply(const FGeometry&, const FPointerEvent&)>;
using FOnRowDrop = TDelegate<FReply(const FDragDropEvent&)>;
using FOnCanAcceptRowDrop = TDelegate<bool(const FDragDropEvent&)>;

// The expander triangle of tree rows.
class SExpanderArrow : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SExpanderArrow)
	SLATE_END_ARGS()

	void Construct(
	    const FArguments&, TFunction<bool()> InIsExpanded, TFunction<bool()> InHasChildren, TFunction<void()> InToggle)
	{
		IsExpanded = std::move(InIsExpanded);
		HasChildren = std::move(InHasChildren);
		Toggle = std::move(InToggle);
	}

	FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event) override
	{
		if (Event.GetEffectingButton() == EMouseButton::Left && HasChildren())
		{
			Toggle();
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

protected:
	FVector2 ComputeDesiredSize(float) const override
	{
		return FVector2(16.0f, 16.0f);
	}
	int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FRect&, FSlateWindowElementList& Out,
	    int32 LayerId, const FWidgetStyle& Style) const override
	{
		if (!HasChildren())
		{
			return LayerId;
		}
		const FVector2 C = Geometry.Size * 0.5f;
		const FColor Color = IsHovered() ? Style.Tinted(FSlateColor(EStyleColor::ForegroundHover).Resolve())
		                                 : Style.GetForegroundColor();
		const TArray<FVector2> Points = IsExpanded()
		    ? TArray<FVector2>{C + FVector2(-4.0f, -2.0f), C + FVector2(0.0f, 2.5f), C + FVector2(4.0f, -2.0f)}
		    : TArray<FVector2>{C + FVector2(-2.0f, -4.0f), C + FVector2(2.5f, 0.0f), C + FVector2(-2.0f, 4.0f)};
		Out.AddLines(LayerId, Geometry.ToPaintGeometry(), Points, Color, 1.5f);
		return LayerId;
	}

private:
	TFunction<bool()> IsExpanded;
	TFunction<bool()> HasChildren;
	TFunction<void()> Toggle;
};

// One row of a list or tree: background by state, the content, and for
// trees the indentation and expander.
template <typename ItemType> class STableRow : public SCompoundWidget, public ITableRow
{
public:
	SLATE_BEGIN_ARGS(STableRow)
	SLATE_STYLE_ARGUMENT(FTableRowStyle, Style)
	SLATE_ARGUMENT(FMargin, Padding, FMargin(4.0f, 2.0f))
	SLATE_EVENT(FOnDragRowDetected, OnDragDetected)
	SLATE_EVENT(FOnRowDrop, OnDrop)
	SLATE_EVENT(FOnCanAcceptRowDrop, OnCanAcceptDrop)
	SLATE_DEFAULT_SLOT(Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args, const TSharedRef<STableViewBase>& InOwner)
	{
		Owner = InOwner.ToSharedPtr();
		Style = Args._Style;
		OnDragDetectedDelegate = Args._OnDragDetected;
		OnDropDelegate = Args._OnDrop;
		OnCanAcceptDrop = Args._OnCanAcceptDrop;
		Content = Args._Content.Widget ? Args._Content.Widget : SNullWidget::NullWidget().ToSharedPtr();
		Padding = Args._Padding;
		bBuilt = false;
	}

	// Called by the view when the row is (re)used for an item.
	void SetItem(const ItemType& InItem, int32 Index)
	{
		Item = InItem;
		SetIndexInList(Index);
		if (!bBuilt)
		{
			BuildContent();
		}
	}
	const ItemType& GetItem() const
	{
		return Item;
	}

	// ITableRow
	TSharedRef<SWidget> AsWidget() override
	{
		return SharedThis(this);
	}
	void SetIndexInList(int32 Index) override
	{
		if (IndexInList != Index)
		{
			IndexInList = Index;
			Invalidate(EInvalidateWidgetReason::Layout); // indentation may change
		}
	}
	int32 GetIndexInList() const override
	{
		return IndexInList;
	}

	FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event) override
	{
		if (ITypedTableView<ItemType>* View = GetView())
		{
			View->Private_OnRowClicked(Item, Event);
		}
		FReply Reply = FReply::Handled();
		if (OnDragDetectedDelegate.IsBound() && Event.GetEffectingButton() == EMouseButton::Left)
		{
			Reply.DetectDrag(SharedThis(this), EMouseButton::Left);
		}
		if (const TSharedPtr<STableViewBase> Pinned = Owner.lock())
		{
			Reply.SetUserFocus(ToSharedRef(StaticCastSharedPtr<SWidget>(Pinned)), EFocusCause::Mouse);
		}
		return Reply;
	}

	FReply OnMouseButtonDoubleClick(const FGeometry&, const FPointerEvent& Event) override
	{
		if (Event.GetEffectingButton() != EMouseButton::Left)
		{
			return FReply::Unhandled();
		}
		if (ITypedTableView<ItemType>* View = GetView())
		{
			View->Private_OnRowDoubleClicked(Item);
		}
		return FReply::Handled();
	}

	FReply OnDragDetected(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		return OnDragDetectedDelegate.IsBound() ? OnDragDetectedDelegate.Execute(Geometry, Event) : FReply::Unhandled();
	}

	void OnDragEnter(const FGeometry&, const FDragDropEvent& Event) override
	{
		bDropTarget = OnCanAcceptDrop.IsBound() ? OnCanAcceptDrop.Execute(Event) : OnDropDelegate.IsBound();
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	void OnDragLeave(const FDragDropEvent&) override
	{
		bDropTarget = false;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	FReply OnDragOver(const FGeometry&, const FDragDropEvent&) override
	{
		return bDropTarget ? FReply::Handled() : FReply::Unhandled();
	}
	FReply OnDrop(const FGeometry&, const FDragDropEvent& Event) override
	{
		bDropTarget = false;
		Invalidate(EInvalidateWidgetReason::Paint);
		return OnDropDelegate.IsBound() ? OnDropDelegate.Execute(Event) : FReply::Unhandled();
	}

protected:
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect,
	    FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const override
	{
		const ITypedTableView<ItemType>* View = GetView();
		const FTableRowStyle& RowStyle = Style
		    ? *Style
		    : (View ? View->Private_GetRowStyle() : FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("TableRow"));
		const bool bSelected = View && View->Private_IsItemSelected(Item);
		const bool bFocused = View && View->Private_IsViewFocused();
		const FSlateBrush& Brush = bSelected
		    ? (IsHovered() ? RowStyle.SelectedHovered
		                   : (bFocused ? RowStyle.ActiveSelected : RowStyle.InactiveSelected))
		    : (IsHovered() ? RowStyle.Hovered : (IndexInList % 2 ? RowStyle.Odd : RowStyle.Even));
		Brush.Paint(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), InWidgetStyle.ColorAndOpacityTint);

		FWidgetStyle ChildStyle = MakeChildStyle(InWidgetStyle);
		ChildStyle.SetForegroundColor((bSelected ? RowStyle.SelectedText : RowStyle.Text).Resolve());
		int32 MaxLayer = PaintArrangedChildren(Args, CullingRect, OutDrawElements, LayerId + 1, ChildStyle);
		if (bDropTarget)
		{
			OutDrawElements.AddRoundedBox(++MaxLayer, Geometry.ToPaintGeometry(), FColor::Transparent(), 2.0f,
			    InWidgetStyle.Tinted(RowStyle.DropIndicator.Resolve()), 1.5f);
		}
		return MaxLayer;
	}

private:
	ITypedTableView<ItemType>* GetView() const
	{
		const TSharedPtr<STableViewBase> Pinned = Owner.lock();
		return Pinned ? dynamic_cast<ITypedTableView<ItemType>*>(Pinned.get()) : nullptr;
	}

	void BuildContent()
	{
		bBuilt = true;
		ITypedTableView<ItemType>* View = GetView();
		const bool bTree = View && View->Private_GetNestingDepth(IndexInList) >= 0;
		if (!bTree)
		{
			ChildSlot.Padding(Padding)[ToSharedRef(Content)];
			return;
		}
		// Tree rows: indentation and the expander before the content.
		const TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		Row->AddSlot().AutoWidth()[SAssignNew(Indent, SSpacer)
		        .Size([this]
		{
			const ITypedTableView<ItemType>* V = GetView();
			const float Step = V ? V->Private_GetRowStyle().IndentSize : 14.0f;
			return FVector2(V ? Step * static_cast<float>(V->Private_GetNestingDepth(IndexInList)) : 0.0f, 0.0f);
		})];
		TFunction<bool()> IsExpanded = [this]
		{
			const ITypedTableView<ItemType>* V = GetView();
			return V && V->Private_IsItemExpanded(Item);
		};
		TFunction<bool()> HasKids = [this]
		{
			const ITypedTableView<ItemType>* V = GetView();
			return V && V->Private_DoesItemHaveChildren(IndexInList);
		};
		TFunction<void()> Toggle = [this]
		{
			if (ITypedTableView<ItemType>* V = GetView())
				V->Private_SetItemExpansion(Item, !V->Private_IsItemExpanded(Item));
		};
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center)[SNew(SExpanderArrow, IsExpanded, HasKids, Toggle)];
		Row->AddSlot().FillWidth(1.0f).VAlign(VAlign_Center)[ToSharedRef(Content)];
		ChildSlot.Padding(Padding)[Row];
	}

	TWeakPtr<STableViewBase> Owner;
	const FTableRowStyle* Style = nullptr;
	FOnDragRowDetected OnDragDetectedDelegate;
	FOnRowDrop OnDropDelegate;
	FOnCanAcceptRowDrop OnCanAcceptDrop;
	TSharedPtr<SWidget> Content;
	TSharedPtr<SSpacer> Indent;
	FMargin Padding;
	ItemType Item{};
	int32 IndexInList = INDEX_NONE;
	bool bBuilt = false;
	bool bDropTarget = false;
};

template <typename ItemType> class SListView : public STableViewBase, public ITypedTableView<ItemType>
{
public:
	using FOnGenerateRow = TDelegate<TSharedRef<ITableRow>(ItemType, const TSharedRef<STableViewBase>&)>;
	using FOnSelectionChanged = TDelegate<void(ItemType, ESelectInfo)>;
	using FOnMouseButtonDoubleClick = TDelegate<void(ItemType)>;

	SLATE_BEGIN_ARGS(SListView)
	SLATE_ARGUMENT(const TArray<ItemType>*, ListItemsSource, nullptr)
	SLATE_EVENT(FOnGenerateRow, OnGenerateRow)
	SLATE_EVENT(FOnSelectionChanged, OnSelectionChanged)
	SLATE_EVENT(FOnMouseButtonDoubleClick, OnMouseButtonDoubleClick)
	SLATE_ARGUMENT(ESelectionMode, SelectionMode, ESelectionMode::Single)
	SLATE_STYLE_ARGUMENT(FTableRowStyle, RowStyle)
	SLATE_STYLE_ARGUMENT(FScrollBarStyle, ScrollBarStyle)
	SLATE_ARGUMENT(EVisibility, ScrollBarVisibility, EVisibility::Visible)
	SLATE_ARGUMENT(float, WheelScrollAmount, 48.0f)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		ItemsSource = Args._ListItemsSource;
		ConstructCommon(Args._OnGenerateRow, Args._OnSelectionChanged, Args._OnMouseButtonDoubleClick,
		    Args._SelectionMode, Args._RowStyle, Args._ScrollBarStyle, Args._WheelScrollAmount,
		    Args._ScrollBarVisibility);
	}

	// --- Items -------------------------------------------------------------------

	void SetItemsSource(const TArray<ItemType>* InItems)
	{
		ItemsSource = InItems;
		RequestListRefresh();
	}
	// Regenerates every row at the next layout.
	void RequestListRefresh()
	{
		RebuildAllRows();
	}
	int32 GetNumItems() const override
	{
		return GetItems() ? static_cast<int32>(GetItems()->size()) : 0;
	}
	// The row widget of an item, if it is currently generated.
	TSharedPtr<ITableRow> WidgetFromItem(const ItemType& Item) const
	{
		typename std::unordered_map<ItemType, TSharedRef<ITableRow>>::const_iterator It = Rows.find(Item);
		return It == Rows.end() ? nullptr : It->second.ToSharedPtr();
	}

	// --- Selection -------------------------------------------------------------------

	TArray<ItemType> GetSelectedItems() const
	{
		TArray<ItemType> Result;
		if (const TArray<ItemType>* Items = GetItems())
		{
			for (const ItemType& Item : *Items)
			{
				if (Selected.contains(Item))
				{
					Result.push_back(Item);
				}
			}
		}
		return Result;
	}
	int32 GetNumItemsSelected() const
	{
		return static_cast<int32>(Selected.size());
	}
	bool IsItemSelected(const ItemType& Item) const
	{
		return Selected.contains(Item);
	}
	void SetSelection(const ItemType& Item, ESelectInfo Info = ESelectInfo::Direct)
	{
		Selected.clear();
		Selected.insert(Item);
		SelectorItem = Item;
		RangeStart = Item;
		SelectionChanged(Item, Info);
	}
	void SetItemSelection(const ItemType& Item, bool bSelected, ESelectInfo Info = ESelectInfo::Direct)
	{
		if (bSelected)
		{
			if (SelectionMode != ESelectionMode::Multi)
			{
				Selected.clear();
			}
			Selected.insert(Item);
		}
		else
		{
			Selected.erase(Item);
		}
		SelectionChanged(Item, Info);
	}
	void ClearSelection()
	{
		if (!Selected.empty())
		{
			Selected.clear();
			SelectionChanged(ItemType{}, ESelectInfo::Direct);
		}
	}
	void RequestScrollIntoView(const ItemType& Item)
	{
		const int32 Index = IndexOf(Item);
		if (Index != INDEX_NONE)
		{
			RequestScrollIntoViewIndex(Index);
		}
	}

	// --- Input ---------------------------------------------------------------------------

	FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override
	{
		const int32 Count = GetNumItems();
		if (Count == 0 || Event.IsGamepad())
		{
			return STableViewBase::OnKeyDown(Geometry, Event);
		}
		const int32 Current = IndexOf(SelectorItem);
		const int32 Page = FMath::Max(1, FMath::FloorToInt(Geometry.Size.Y / FMath::Max(1.0f, AverageRowHeight)) - 1);
		int32 Target = INDEX_NONE;
		switch (Event.GetKey())
		{
		case EKey::Up:
			Target = Current == INDEX_NONE ? 0 : FMath::Max(0, Current - 1);
			break;
		case EKey::Down:
			Target = Current == INDEX_NONE ? 0 : FMath::Min(Count - 1, Current + 1);
			break;
		case EKey::Home:
			Target = 0;
			break;
		case EKey::End:
			Target = Count - 1;
			break;
		case EKey::PageUp:
			Target = FMath::Max(0, (Current == INDEX_NONE ? 0 : Current) - Page);
			break;
		case EKey::PageDown:
			Target = FMath::Min(Count - 1, (Current == INDEX_NONE ? 0 : Current) + Page);
			break;
		case EKey::A:
			if (Event.IsControlDown() && SelectionMode == ESelectionMode::Multi)
			{
				for (const ItemType& Item : *GetItems())
				{
					Selected.insert(Item);
				}
				SelectionChanged(SelectorItem, ESelectInfo::OnKeyPress);
				return FReply::Handled();
			}
			break;
		default:
			break;
		}
		if (Target == INDEX_NONE)
		{
			return OnTreeKeyDown(Event, Current);
		}
		const ItemType& Item = (*GetItems())[Target];
		if (SelectionMode == ESelectionMode::Multi && Event.IsShiftDown())
		{
			SelectRange(RangeStart, Item);
			SelectorItem = Item;
			SelectionChanged(Item, ESelectInfo::OnNavigation);
		}
		else if (SelectionMode != ESelectionMode::None)
		{
			SetSelection(Item, ESelectInfo::OnNavigation);
		}
		else
		{
			SelectorItem = Item;
		}
		RequestScrollIntoViewIndex(Target);
		return FReply::Handled();
	}

	// --- ITypedTableView ---------------------------------------------------------------------

	void Private_OnRowClicked(const ItemType& Item, const FPointerEvent& Event) override
	{
		if (SelectionMode == ESelectionMode::None)
		{
			return;
		}
		if (Event.GetEffectingButton() == EMouseButton::Right && Selected.contains(Item))
		{
			return; // keep a multi-selection for context menus
		}
		if (SelectionMode == ESelectionMode::Multi && Event.IsControlDown())
		{
			SelectorItem = Item;
			RangeStart = Item;
			SetItemSelection(Item, !Selected.contains(Item), ESelectInfo::OnMouseClick);
		}
		else if (SelectionMode == ESelectionMode::Multi && Event.IsShiftDown())
		{
			SelectRange(RangeStart, Item);
			SelectorItem = Item;
			SelectionChanged(Item, ESelectInfo::OnMouseClick);
		}
		else if (SelectionMode == ESelectionMode::SingleToggle && Selected.contains(Item))
		{
			ClearSelection();
		}
		else
		{
			SetSelection(Item, ESelectInfo::OnMouseClick);
		}
	}
	void Private_OnRowDoubleClicked(const ItemType& Item) override
	{
		OnMouseButtonDoubleClick.ExecuteIfBound(Item);
	}
	bool Private_IsItemSelected(const ItemType& Item) const override
	{
		return Selected.contains(Item);
	}
	bool Private_IsViewFocused() const override
	{
		return HasKeyboardFocus() || HasFocusedDescendants();
	}
	int32 Private_GetNestingDepth(int32) const override
	{
		return -1;
	}
	bool Private_DoesItemHaveChildren(int32) const override
	{
		return false;
	}
	bool Private_IsItemExpanded(const ItemType&) const override
	{
		return false;
	}
	void Private_SetItemExpansion(const ItemType&, bool) override {}
	const FTableRowStyle& Private_GetRowStyle() const override
	{
		return *RowStyle;
	}

protected:
	void ConstructCommon(const FOnGenerateRow& InOnGenerateRow, const FOnSelectionChanged& InOnSelectionChanged,
	    const FOnMouseButtonDoubleClick& InOnDoubleClick, ESelectionMode InSelectionMode,
	    const FTableRowStyle* InRowStyle, const FScrollBarStyle* InScrollBarStyle, float InWheelScrollAmount,
	    EVisibility InScrollBarVisibility)
	{
		OnGenerateRow = InOnGenerateRow;
		OnSelectionChanged = InOnSelectionChanged;
		OnMouseButtonDoubleClick = InOnDoubleClick;
		SelectionMode = InSelectionMode;
		RowStyle = InRowStyle ? InRowStyle : &FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("TableRow");
		ConstructTableView(InScrollBarStyle, InWheelScrollAmount, InScrollBarVisibility);
	}

	// The items in display order (a tree's flattened, visible items).
	virtual const TArray<ItemType>* GetItems() const
	{
		return ItemsSource;
	}
	virtual FReply OnTreeKeyDown(const FKeyEvent& Event, int32 Current)
	{
		(void)Event;
		(void)Current;
		return FReply::Unhandled();
	}

	int32 IndexOf(const ItemType& Item) const
	{
		const TArray<ItemType>* Items = GetItems();
		if (!Items)
		{
			return INDEX_NONE;
		}
		const typename TArray<ItemType>::const_iterator It = std::find(Items->begin(), Items->end(), Item);
		return It == Items->end() ? INDEX_NONE : static_cast<int32>(It - Items->begin());
	}

	void SelectRange(const ItemType& From, const ItemType& To)
	{
		int32 A = IndexOf(From), B = IndexOf(To);
		if (A == INDEX_NONE)
		{
			A = B;
		}
		if (A > B)
		{
			std::swap(A, B);
		}
		Selected.clear();
		for (int32 I = A; I <= B && I >= 0; ++I)
		{
			Selected.insert((*GetItems())[I]);
		}
	}

	void SelectionChanged(const ItemType& Item, ESelectInfo Info)
	{
		Invalidate(EInvalidateWidgetReason::Paint);
		OnSelectionChanged.ExecuteIfBound(Item, Info);
	}

	// STableViewBase
	TSharedRef<SWidget> GetRowWidgetForIndex(int32 Index) override
	{
		const ItemType& Item = (*GetItems())[Index];
		typename std::unordered_map<ItemType, TSharedRef<ITableRow>>::const_iterator It = Rows.find(Item);
		TSharedPtr<ITableRow> Row;
		if (It != Rows.end())
		{
			Row = It->second.ToSharedPtr();
		}
		else
		{
			check(OnGenerateRow.IsBound());
			Row = OnGenerateRow.Execute(Item, SharedThis(static_cast<STableViewBase*>(this))).ToSharedPtr();
			Rows.emplace(Item, ToSharedRef(Row));
		}
		if (STableRow<ItemType>* TypedRow = dynamic_cast<STableRow<ItemType>*>(Row.get()))
		{
			TypedRow->SetItem(Item, Index);
		}
		else
		{
			Row->SetIndexInList(Index);
		}
		Used.insert(Item);
		return Row->AsWidget();
	}
	void BeginGeneration() override
	{
		Used.clear();
	}
	void EndGeneration() override
	{
		std::erase_if(Rows,
		    [this](const typename std::unordered_map<ItemType, TSharedRef<ITableRow>>::value_type& Entry)
		{ return !Used.contains(Entry.first); });
	}

	const TArray<ItemType>* ItemsSource = nullptr;
	FOnGenerateRow OnGenerateRow;
	FOnSelectionChanged OnSelectionChanged;
	FOnMouseButtonDoubleClick OnMouseButtonDoubleClick;
	ESelectionMode SelectionMode = ESelectionMode::Single;
	const FTableRowStyle* RowStyle = nullptr;
	std::unordered_set<ItemType> Selected;
	ItemType SelectorItem{};
	ItemType RangeStart{};
	std::unordered_map<ItemType, TSharedRef<ITableRow>> Rows;
	std::unordered_set<ItemType> Used;
};

template <typename ItemType> class STreeView : public SListView<ItemType>
{
public:
	using Super = SListView<ItemType>;
	using FOnGetChildren = TDelegate<void(ItemType, TArray<ItemType>&)>;
	using FOnExpansionChanged = TDelegate<void(ItemType, bool)>;
	using FOnGenerateRow = typename Super::FOnGenerateRow;
	using FOnSelectionChanged = typename Super::FOnSelectionChanged;
	using FOnMouseButtonDoubleClick = typename Super::FOnMouseButtonDoubleClick;

	SLATE_BEGIN_ARGS(STreeView)
	SLATE_ARGUMENT(const TArray<ItemType>*, TreeItemsSource, nullptr)
	SLATE_EVENT(FOnGetChildren, OnGetChildren)
	SLATE_EVENT(FOnExpansionChanged, OnExpansionChanged)
	SLATE_EVENT(FOnGenerateRow, OnGenerateRow)
	SLATE_EVENT(FOnSelectionChanged, OnSelectionChanged)
	SLATE_EVENT(FOnMouseButtonDoubleClick, OnMouseButtonDoubleClick)
	SLATE_ARGUMENT(ESelectionMode, SelectionMode, ESelectionMode::Single)
	SLATE_STYLE_ARGUMENT(FTableRowStyle, RowStyle)
	SLATE_STYLE_ARGUMENT(FScrollBarStyle, ScrollBarStyle)
	SLATE_ARGUMENT(EVisibility, ScrollBarVisibility, EVisibility::Visible)
	SLATE_ARGUMENT(float, WheelScrollAmount, 48.0f)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		RootItems = Args._TreeItemsSource;
		OnGetChildren = Args._OnGetChildren;
		OnExpansionChanged = Args._OnExpansionChanged;
		this->ConstructCommon(Args._OnGenerateRow, Args._OnSelectionChanged, Args._OnMouseButtonDoubleClick,
		    Args._SelectionMode, Args._RowStyle, Args._ScrollBarStyle, Args._WheelScrollAmount,
		    Args._ScrollBarVisibility);
		bTreeDirty = true;
	}

	void SetTreeItemsSource(const TArray<ItemType>* InRoots)
	{
		RootItems = InRoots;
		RequestTreeRefresh();
	}
	// Rebuilds the visible items (after the hierarchy changed).
	void RequestTreeRefresh()
	{
		bTreeDirty = true;
		this->RequestListRefresh();
	}
	bool IsItemExpanded(const ItemType& Item) const
	{
		return Expanded.contains(Item);
	}
	void SetItemExpansion(const ItemType& Item, bool bExpand)
	{
		if (bExpand == IsItemExpanded(Item))
		{
			return;
		}
		if (bExpand)
		{
			Expanded.insert(Item);
		}
		else
		{
			Expanded.erase(Item);
		}
		bTreeDirty = true;
		this->RefreshRows();
		OnExpansionChanged.ExecuteIfBound(Item, bExpand);
	}
	// Depth of each visible item (0 = root).
	const TArray<ItemType>& GetLinearizedItems() const
	{
		RebuildIfNeeded();
		return Linear;
	}

	// ITypedTableView
	int32 Private_GetNestingDepth(int32 Index) const override
	{
		RebuildIfNeeded();
		return Index >= 0 && Index < static_cast<int32>(Depths.size()) ? Depths[Index] : 0;
	}
	bool Private_DoesItemHaveChildren(int32 Index) const override
	{
		RebuildIfNeeded();
		return Index >= 0 && Index < static_cast<int32>(HasChildren.size()) && HasChildren[Index];
	}
	bool Private_IsItemExpanded(const ItemType& Item) const override
	{
		return IsItemExpanded(Item);
	}
	void Private_SetItemExpansion(const ItemType& Item, bool bExpanded) override
	{
		SetItemExpansion(Item, bExpanded);
	}

protected:
	const TArray<ItemType>* GetItems() const override
	{
		RebuildIfNeeded();
		return &Linear;
	}

	FReply OnTreeKeyDown(const FKeyEvent& Event, int32 Current) override
	{
		if (Current == INDEX_NONE)
		{
			return FReply::Unhandled();
		}
		const ItemType Item = Linear[Current];
		if (Event.GetKey() == EKey::Right)
		{
			if (HasChildren[Current] && !IsItemExpanded(Item))
			{
				SetItemExpansion(Item, true);
			}
			else if (HasChildren[Current] && Current + 1 < static_cast<int32>(Linear.size()))
			{
				this->SetSelection(Linear[Current + 1], ESelectInfo::OnNavigation);
				this->RequestScrollIntoViewIndex(Current + 1);
			}
			return FReply::Handled();
		}
		if (Event.GetKey() == EKey::Left)
		{
			if (HasChildren[Current] && IsItemExpanded(Item))
			{
				SetItemExpansion(Item, false);
			}
			else
			{
				// To the parent: the nearest earlier item one level up.
				for (int32 I = Current - 1; I >= 0; --I)
				{
					if (Depths[I] < Depths[Current])
					{
						this->SetSelection(Linear[I], ESelectInfo::OnNavigation);
						this->RequestScrollIntoViewIndex(I);
						break;
					}
				}
			}
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

private:
	void RebuildIfNeeded() const
	{
		if (!bTreeDirty)
		{
			return;
		}
		bTreeDirty = false;
		Linear.clear();
		Depths.clear();
		HasChildren.clear();
		if (!RootItems)
		{
			return;
		}
		const TFunction<void(const ItemType&, int32)> Add = [&](const ItemType& Item, int32 Depth)
		{
			TArray<ItemType> Kids;
			if (OnGetChildren.IsBound())
			{
				OnGetChildren.Execute(Item, Kids);
			}
			Linear.push_back(Item);
			Depths.push_back(Depth);
			HasChildren.push_back(!Kids.empty());
			if (!Kids.empty() && Expanded.contains(Item))
			{
				for (const ItemType& Kid : Kids)
				{
					Add(Kid, Depth + 1);
				}
			}
		};
		for (const ItemType& Root : *RootItems)
		{
			Add(Root, 0);
		}
	}

	const TArray<ItemType>* RootItems = nullptr;
	FOnGetChildren OnGetChildren;
	FOnExpansionChanged OnExpansionChanged;
	std::unordered_set<ItemType> Expanded;
	mutable TArray<ItemType> Linear;
	mutable TArray<int32> Depths;
	mutable TArray<bool> HasChildren;
	mutable bool bTreeDirty = true;
};
