// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Headless tests of the virtualized list and tree views.

#include "UITestHelpers.h"

#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

namespace
{
	struct FNode
	{
		FString Name;
		TArray<TSharedPtr<FNode>> Children;
	};
	using FNodePtr = TSharedPtr<FNode>;

	FNodePtr MakeNode(const char* Name, TArray<FNodePtr> Children = {})
	{
		auto Node = std::make_shared<FNode>();
		Node->Name = Name;
		Node->Children = std::move(Children);
		return Node;
	}

	struct FListFixture
	{
		FUITestApp T;
		TArray<int32> Items;
		TSharedPtr<SListView<int32>> List;
		int32 Generated = 0;
		TArray<int32> SelectionEvents;

		explicit FListFixture(int32 Count, ESelectionMode Mode = ESelectionMode::Single)
		{
			for (int32 I = 0; I < Count; ++I)
			{
				Items.push_back(I);
			}
			T.AddWindow(SAssignNew(List, SListView<int32>)
				.ListItemsSource(&Items)
				.SelectionMode(Mode)
				.OnGenerateRow([this](int32 Item, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
				{
					++Generated;
					return SNew(STableRow<int32>, Owner).Padding(FMargin(0.0f))[SNew(SBox).HeightOverride(TOptional<float>(20.0f))[SNew(STextBlock).Text(FString::Format("Row {}", Item))]];
				})
				.OnSelectionChanged([this](int32 Item, ESelectInfo) { SelectionEvents.push_back(Item); }),
				FVector2(200.0f, 100.0f));
		}

		TSharedRef<SWidget> Row(int32 Item) const
		{
			const TSharedPtr<ITableRow> R = List->WidgetFromItem(Item);
			REQUIRE(R);
			return R->AsWidget();
		}
	};
}

TEST_SUITE("UIViews")
{
	TEST_CASE("List view generates only the visible rows")
	{
		FListFixture F(10000);
		CHECK(F.List->GetNumGeneratedRows() == 5); // 100 / 20
		CHECK(F.Generated == 5);
		CHECK(FUITestApp::RectOf(*F.Row(0)).Top == doctest::Approx(0.0f));
		CHECK(FUITestApp::RectOf(*F.Row(4)).Bottom == doctest::Approx(100.0f));
		CHECK(!F.List->WidgetFromItem(5));
	}

	TEST_CASE("Scrolling reuses visible rows and generates new ones")
	{
		FListFixture F(100);
		const TSharedRef<SWindow> Window = F.T.GetApp().GetWindows()[0];
		const TSharedRef<SWidget> Row3 = F.Row(3);
		F.T.Wheel(Window, FVector2(50.0f, 50.0f), -1.0f); // 48 units = 2.4 rows
		CHECK(F.List->GetScrollOffset() == doctest::Approx(2.4));
		CHECK(F.Row(3) == Row3); // still visible: same widget
		CHECK(FUITestApp::RectOf(*F.Row(2)).Top == doctest::Approx(-8.0f));
		CHECK(!F.List->WidgetFromItem(0));
		CHECK(F.List->GetNumGeneratedRows() == 6); // partial rows at both ends

		F.List->ScrollToBottom();
		F.T.Tick();
		CHECK(F.List->GetScrollOffset() == doctest::Approx(95.0));
		CHECK(FUITestApp::RectOf(*F.Row(99)).Bottom == doctest::Approx(100.0f));
	}

	TEST_CASE("Clicking selects; keyboard moves the selection and scrolls it into view")
	{
		FListFixture F(50);
		F.T.ClickWidget(*F.Row(2));
		CHECK(F.List->IsItemSelected(2));
		CHECK(F.List->HasKeyboardFocus());
		for (int32 I = 0; I < 5; ++I)
		{
			F.T.Key(EKey::Down);
		}
		CHECK(F.List->GetSelectedItems() == TArray<int32>{ 7 });
		CHECK(FUITestApp::RectOf(*F.Row(7)).Bottom <= 100.01f);
		CHECK(FUITestApp::RectOf(*F.Row(7)).Top >= 0.0f);
		F.T.Key(EKey::End);
		CHECK(F.List->IsItemSelected(49));
		CHECK(FUITestApp::RectOf(*F.Row(49)).Bottom == doctest::Approx(100.0f));
		F.T.Key(EKey::Home);
		CHECK(F.List->GetScrollOffset() == doctest::Approx(0.0));
		CHECK(F.SelectionEvents.back() == 0);
	}

	TEST_CASE("Multi-selection with Ctrl and Shift clicks")
	{
		FListFixture F(20, ESelectionMode::Multi);
		F.T.ClickWidget(*F.Row(1));
		F.T.SetModifiers(false, true);
		F.T.ClickWidget(*F.Row(3));
		F.T.SetModifiers(true, false);
		F.T.ClickWidget(*F.Row(4));
		F.T.SetModifiers(false, false);
		CHECK(F.List->GetSelectedItems() == TArray<int32>{ 3, 4 }); // Shift extends from the last Ctrl-click
		F.T.Key(EKey::A, false, true);
		CHECK(F.List->GetNumItemsSelected() == 20);
	}

	TEST_CASE("Tree view: expansion by keyboard and expander, indentation, linear order")
	{
		FUITestApp T;
		TArray<FNodePtr> Roots{
			MakeNode("A", { MakeNode("A1"), MakeNode("A2", { MakeNode("A2a") }) }),
			MakeNode("B"),
		};
		TSharedPtr<STreeView<FNodePtr>> Tree;
		TArray<TPair<FString, bool>> Expansions;
		T.AddWindow(SAssignNew(Tree, STreeView<FNodePtr>)
			.TreeItemsSource(&Roots)
			.OnGetChildren([](FNodePtr Node, TArray<FNodePtr>& Out) { Out = Node->Children; })
			.OnExpansionChanged([&](FNodePtr Node, bool bExpanded) { Expansions.push_back({ Node->Name, bExpanded }); })
			.OnGenerateRow([](FNodePtr Node, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
			{
				return SNew(STableRow<FNodePtr>, Owner)[SNew(STextBlock).Text(Node->Name)];
			}),
			FVector2(200.0f, 300.0f));

		const auto Names = [&Tree]()
		{
			TArray<FString> Result;
			for (const FNodePtr& Node : Tree->GetLinearizedItems())
			{
				Result.push_back(Node->Name);
			}
			return Result;
		};
		CHECK(Names() == TArray<FString>{ "A", "B" });

		Tree->SetSelection(Roots[0]);
		T.GetApp().SetKeyboardFocus(Tree);
		T.Key(EKey::Right); // expand A
		CHECK(Names() == TArray<FString>{ "A", "A1", "A2", "B" });
		T.Key(EKey::Right); // to A1
		CHECK(Tree->GetSelectedItems()[0]->Name == "A1");
		T.Key(EKey::Down);
		T.Key(EKey::Right); // expand A2
		CHECK(Names() == TArray<FString>{ "A", "A1", "A2", "A2a", "B" });

		// Children are indented.
		const TSharedPtr<ITableRow> RowA = Tree->WidgetFromItem(Roots[0]);
		const TSharedPtr<ITableRow> RowA2a = Tree->WidgetFromItem(Roots[0]->Children[1]->Children[0]);
		REQUIRE(RowA);
		REQUIRE(RowA2a);
		const TSharedRef<SWidget> TextA = RowA->AsWidget()->GetArrangedChildren()[0].Widget->GetArrangedChildren().back().Widget;
		const TSharedRef<SWidget> TextA2a = RowA2a->AsWidget()->GetArrangedChildren()[0].Widget->GetArrangedChildren().back().Widget;
		CHECK(FUITestApp::RectOf(*TextA2a).Left == doctest::Approx(FUITestApp::RectOf(*TextA).Left + 28.0f));

		// Left on a child goes to its parent, Left again collapses it.
		T.Key(EKey::Down); // A2a
		T.Key(EKey::Left);
		CHECK(Tree->GetSelectedItems()[0]->Name == "A2");
		T.Key(EKey::Left);
		CHECK(Names() == TArray<FString>{ "A", "A1", "A2", "B" });

		// Clicking the expander of A collapses it.
		const TSharedRef<SWidget> ExpanderA = RowA->AsWidget()->GetArrangedChildren()[0].Widget->GetArrangedChildren()[1].Widget;
		T.ClickWidget(*ExpanderA);
		CHECK(Names() == TArray<FString>{ "A", "B" });
		CHECK(Expansions.back() == TPair<FString, bool>{ "A", false });
	}

	TEST_CASE("Rows can be dragged and dropped onto other rows")
	{
		class FRowDrag final : public FDragDropOperation
		{
		public:
			GE_DRAG_DROP_OPERATION_TYPE(FRowDrag, FDragDropOperation)
			int32 Item = 0;
		};

		FUITestApp T;
		TArray<int32> Items{ 0, 1, 2, 3 };
		TArray<TPair<int32, int32>> Drops;
		TSharedPtr<SListView<int32>> List;
		const TSharedRef<SWindow> Window = T.AddWindow(SAssignNew(List, SListView<int32>)
			.ListItemsSource(&Items)
			.OnGenerateRow([&](int32 Item, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
			{
				return SNew(STableRow<int32>, Owner)
					.OnDragDetected([Item](const FGeometry&, const FPointerEvent&)
					{
						auto Op = std::make_shared<FRowDrag>();
						Op->Item = Item;
						return FReply::Handled().BeginDragDrop(TSharedRef<FDragDropOperation>(StaticCastSharedPtr<FDragDropOperation>(Op)));
					})
					.OnDrop([&Drops, Item](const FDragDropEvent& Event)
					{
						if (const TSharedPtr<FRowDrag> Op = Event.GetOperationAs<FRowDrag>())
						{
							Drops.push_back({ Op->Item, Item });
							return FReply::Handled();
						}
						return FReply::Unhandled();
					})
					[SNew(STextBlock).Text(FString::Format("Item {}", Item))];
			}));

		const FVector2 From = FUITestApp::CenterOf(*List->WidgetFromItem(0)->AsWidget());
		const FVector2 To = FUITestApp::CenterOf(*List->WidgetFromItem(2)->AsWidget());
		T.MouseMove(Window, From);
		T.MouseDown(Window, From);
		T.MouseMove(Window, From + FVector2(0.0f, 10.0f));
		T.Tick();
		REQUIRE(T.GetApp().IsDragDropping());
		T.MouseMove(Window, To);
		T.MouseUp(Window, To);
		T.Tick();
		CHECK(Drops == TArray<TPair<int32, int32>>{ { 0, 2 } });
	}
}
