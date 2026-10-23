// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "SPlaceActors.h"
#include "Containers/ContainerAliases.h"
#include "Templates/SharedPointer.h"
#include "Internationalization/TextLocalization.h"

#include "MapEditorIcons.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Text/SEditableText.h"
#include "Widgets/Text/STextBlock.h"

using namespace MapEditorPrivate;

// ---------------------------------------------------------------------------
// The drag

namespace
{
	class SPlaceDecorator final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SPlaceDecorator)
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FMapPlaceable& Item)
		{
			ChildSlot[SNew(SBorder)
			        .BorderImage(&FAppStyle::Get().GetBrush("DragDecorator"))
			        .Padding(FMargin(8.0f, 4.0f))[SNew(STextBlock).Text(FString::Format("Place {}", Item.Label))]];
		}
	};
} // namespace

TSharedRef<FDragDropOperation> FMapPlaceDragDrop::New(FMapPlaceable Item)
{
	return TSharedRef<FDragDropOperation>(
	    StaticCastSharedPtr<FDragDropOperation>(std::make_shared<FMapPlaceDragDrop>(std::move(Item))));
}

TSharedPtr<SWidget> FMapPlaceDragDrop::GetDefaultDecorator() const
{
	return SNew(SPlaceDecorator, Item);
}

// ---------------------------------------------------------------------------
// SPlaceActors

namespace
{
	// Unreal's categories, in its order; others go before All.
	const char* const StandardCategories[] = {"Recent", "Basic", "Lights", "Shapes", "Geometry", "Volumes"};

	EIcon CategoryIcon(const FString& Category)
	{
		if (Category == "Recent")
			return EIcon::Recent;
		if (Category == "Basic")
			return EIcon::Basic;
		if (Category == "Lights")
			return EIcon::Light;
		if (Category == "Shapes" || Category == "Geometry")
			return EIcon::Geometry;
		if (Category == "Volumes")
			return EIcon::Ortho;
		if (Category == "All")
			return EIcon::All;
		return EIcon::Entity;
	}

	EIcon ItemIcon(const FMapPlaceable& Item)
	{
		if (Item.Kind == EMapPlaceableKind::Prefab)
		{
			return EIcon::Geometry;
		}
		if (Item.Kind == EMapPlaceableKind::Mesh)
		{
			return EIcon::Lit;
		}
		return Item.Category == "Lights" ? EIcon::Light : EIcon::Basic;
	}

	// A category button: icon above a small label, highlighted when chosen.
	class SCategoryButton final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SCategoryButton)
		SLATE_ATTRIBUTE(bool, IsChosen, false)
		SLATE_EVENT(FSimpleDelegate, OnClicked)
		SLATE_END_ARGS()

		void Construct(const FArguments& Args, const FString& Category)
		{
			IsChosen.Assign(Args._IsChosen);
			const FSimpleDelegate Clicked = Args._OnClicked;
			ChildSlot[SNew(SButton)
			        .ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("FlatButton"))
			        .IsFocusable(false)
			        .ContentPadding(FMargin(2.0f, 6.0f))
			        .ToolTipText(Category)
			        .OnClicked(
			            [Clicked]
			{
				Clicked.ExecuteIfBound();
				return FReply::Handled();
			})[SNew(SVerticalBox) +
			            SVerticalBox::Slot().AutoHeight().HAlign(
			                HAlign_Center)[SNew(SMapViewportIcon).Icon(CategoryIcon(Category)).Size(18.0f)] +
			            SVerticalBox::Slot()
			                .AutoHeight()
			                .HAlign(HAlign_Center)
			                .Padding(FMargin(0.0f, 3.0f, 0.0f, 0.0f))[SNew(STextBlock)
			                        .TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText"))
			                        .Text(Category)
			                        .ColorAndOpacity([this]
			{ return FSlateColor(IsChosen.Get() ? EStyleColor::White : EStyleColor::ForegroundSecondary); })]]];
		}

	protected:
		int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect,
		    FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& Style) const override
		{
			if (IsChosen.Get())
			{
				Out.AddRoundedBox(
				    LayerId, Geometry.ToPaintGeometry(), FSlateColor(EStyleColor::Primary).Resolve(), 4.0f);
			}
			return SCompoundWidget::OnPaint(Args, Geometry, CullingRect, Out, LayerId + 1, Style);
		}

	private:
		TWidgetAttribute<bool, EInvalidateWidgetReason::Paint> IsChosen{*this, false};
	};
} // namespace

void SPlaceActors::Construct(const FArguments& Args)
{
	OnPlace = Args._OnPlace;
	ChildSlot
	    [SNew(SHorizontalBox) +
	     SHorizontalBox::Slot()
	         .AutoWidth()[SNew(SBorder)
	                          .BorderImage(&FAppStyle::Get().GetBrush("Header"))
	                          .Padding(FMargin(3.0f, 4.0f))[SAssignNew(CategoryHost, SBox).WidthOverride(60.0f)]] +
	     SHorizontalBox::Slot().FillWidth(1.0f)
	         [SNew(SVerticalBox) +
	          SVerticalBox::Slot().AutoHeight().Padding(FMargin(
	              6.0f, 6.0f, 6.0f, 4.0f))[SNew(SEditableTextBox)
	                                           .HintText(GENESIS_TEXT("Search Classes"))
	                                           .OnTextChanged([this](const FString& Text) { SetSearchText(Text); })] +
	          SVerticalBox::Slot().AutoHeight()
	              [SNew(SBorder)
	                   .BorderImage(&FAppStyle::Get().GetBrush("Header"))
	                   .Padding(FMargin(8.0f, 4.0f))
	                       [SNew(STextBlock)
	                            .TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("BoldText"))
	                            .Text(
	                                [this]
	                                {
		                                return SearchText.IsEmpty() ? Category
		                                                            : FString::Format("Search: {}", SearchText);
	                                })]] +
	          SVerticalBox::Slot().FillHeight(1.0f)
	              [SAssignNew(List, SListView<int32>)
	                   .ListItemsSource(&ShownIndices)
	                   .SelectionMode(ESelectionMode::Single)
	                   .OnGenerateRow(
	                       [this](int32 Index, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
	                       {
		                       const FMapPlaceable Item = Index >= 0 && Index < static_cast<int32>(Shown.size())
		                                                      ? Shown[static_cast<SIZE_T>(Index)]
		                                                      : FMapPlaceable();
		                       return SNew(STableRow<int32>, Owner)
		                           .Padding(FMargin(8.0f, 4.0f))
		                           .OnDragDetected(
		                               [Item](const FGeometry&, const FPointerEvent&)
		                               {
			                               return FReply::Handled().BeginDragDrop(FMapPlaceDragDrop::New(Item));
		                               })[SNew(SHorizontalBox)
		                                      .ToolTipText(Item.ToolTip.IsEmpty()
		                                                       ? Item.Label
		                                                       : FString::Format("{}\n{}", Item.Label, Item.ToolTip)) +
		                                  SHorizontalBox::Slot()
		                                      .AutoWidth()
		                                      .VAlign(VAlign_Center)
		                                      .Padding(FMargin(0.0f, 0.0f, 8.0f,
		                                                       0.0f))[SNew(SMapViewportIcon).Icon(ItemIcon(Item))] +
		                                  SHorizontalBox::Slot().FillWidth(1.0f).VAlign(
		                                      VAlign_Center)[SNew(STextBlock).Text(Item.Label)]];
	                       })
	                   .OnMouseButtonDoubleClick(
	                       [this](int32 Index)
	                       {
		                       if (Index >= 0 && Index < static_cast<int32>(Shown.size()))
		                       {
			                       const FMapPlaceable Item = Shown[static_cast<SIZE_T>(Index)];
			                       NoteRecent(Item);
			                       OnPlace.ExecuteIfBound(Item);
		                       }
	                       })]]];
	Refresh();
}

TArray<FString> SPlaceActors::GetCategories() const
{
	TArray<FString> Result;
	const TFunction<bool(const FString&)> Has = [this](const FString& Name)
	{
		return Name == "Recent" ? !Recent.empty()
		                        : std::any_of(Items.begin(), Items.end(),
		                              [&Name](const FMapPlaceable& Item) { return Item.Category == Name; });
	};
	for (const char* Name : StandardCategories)
	{
		if (Has(Name))
		{
			Result.push_back(Name);
		}
	}
	for (const FMapPlaceable& Item : Items)
	{
		if (std::find(Result.begin(), Result.end(), Item.Category) == Result.end() &&
		    std::find(std::begin(StandardCategories), std::end(StandardCategories), Item.Category) ==
		        std::end(StandardCategories))
		{
			Result.push_back(Item.Category);
		}
	}
	Result.push_back("All");
	return Result;
}

TSharedRef<SWidget> SPlaceActors::MakeCategoryBar()
{
	const TSharedRef<SVerticalBox> Bar = SNew(SVerticalBox);
	for (const FString& Name : GetCategories())
	{
		Bar->AddSlot().AutoHeight().Padding(FMargin(
		    0.0f, 0.0f, 0.0f, 2.0f))[SNew(SCategoryButton, Name).IsChosen([this, Name] {
			return Category == Name;
		}).OnClicked([this, Name] { SetCategory(Name); })];
	}
	return Bar;
}

void SPlaceActors::SetItems(TArray<FMapPlaceable> InItems)
{
	Items = std::move(InItems);
	// Recent keeps only what still exists.
	std::erase_if(Recent,
	    [this](const FMapPlaceable& Item) { return std::find(Items.begin(), Items.end(), Item) == Items.end(); });
	Refresh();
}

void SPlaceActors::NoteRecent(const FMapPlaceable& Item)
{
	std::erase(Recent, Item);
	Recent.insert(Recent.begin(), Item);
	if (Recent.size() > static_cast<SIZE_T>(MaxRecent))
	{
		Recent.resize(MaxRecent);
	}
	Refresh();
}

void SPlaceActors::SetCategory(const FString& InCategory)
{
	Category = InCategory;
	PreferredCategory = InCategory;
	Refresh();
}

void SPlaceActors::SetSearchText(const FString& Text)
{
	SearchText = Text.TrimStartAndEnd();
	Refresh();
}

TArray<FMapPlaceable> SPlaceActors::GetShownItems() const
{
	return Shown;
}

void SPlaceActors::Refresh()
{
	const TArray<FString> Categories = GetCategories();
	// The category chosen last, once it has items again (Basic at first).
	if (std::find(Categories.begin(), Categories.end(), PreferredCategory) != Categories.end())
	{
		Category = PreferredCategory;
	}
	else if (std::find(Categories.begin(), Categories.end(), Category) == Categories.end())
	{
		// The first category besides Recent (All when there is no other).
		Category = Categories.front() == "Recent" ? Categories[1] : Categories.front();
	}
	Shown.clear();
	// A search looks through everything, as in Unreal.
	const FString Needle = SearchText.ToLower();
	const TArray<FMapPlaceable>& Source = Category == "Recent" && Needle.IsEmpty() ? Recent : Items;
	for (const FMapPlaceable& Item : Source)
	{
		const bool bInCategory =
		    !Needle.IsEmpty() || Category == "All" || Category == "Recent" || Item.Category == Category;
		// An item in two categories (Point Light: Basic and Lights) shows once in All.
		const bool bShownAlready = std::find(Shown.begin(), Shown.end(), Item) != Shown.end();
		if (bInCategory && !bShownAlready &&
		    (Needle.IsEmpty() || Item.Label.ToLower().Contains(Needle.View()) ||
		        Item.Id.ToLower().Contains(Needle.View())))
		{
			Shown.push_back(Item);
		}
	}
	ShownIndices.clear();
	for (SIZE_T Index = 0; Index < Shown.size(); ++Index)
	{
		ShownIndices.push_back(static_cast<int32>(Index));
	}
	// Rebuilt only when the categories change: a category button calls this.
	if (CategoryHost && Categories != BarCategories)
	{
		BarCategories = Categories;
		CategoryHost->SetContent(MakeCategoryBar());
	}
	if (List)
	{
		List->RequestListRefresh();
	}
}
