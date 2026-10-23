// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// UIListsTest: virtualized views. A list view of 10,000 rows with multiple
// selection, and a tree view with expandable groups.
//
// Self-test: selects with clicks, Ctrl and Shift, Ctrl+A, End; checks that
// only visible rows exist as widgets; scrolls with the wheel; expands and
// collapses tree items with the keyboard.

#include "TestApp.h"
#include "Templates/SharedPointer.h"
#include "TestWidgets.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

namespace
{
	struct FTestItem
	{
		int32 Index = 0;
		FString Name;
	};

	struct FMapNode
	{
		FString Name;
		FString Kind;
		TArray<TSharedPtr<FMapNode>> Children;
	};

	TSharedPtr<FMapNode> Node(const char* Name, const char* Kind, TArray<TSharedPtr<FMapNode>> Children = {})
	{
		std::shared_ptr<FMapNode> N = std::make_shared<FMapNode>();
		N->Name = Name;
		N->Kind = Kind;
		N->Children = std::move(Children);
		return N;
	}
} // namespace

class FListsTestApp final : public FTestApp
{
public:
	FListsTestApp() : FTestApp({"UIListsTest", "Genesis UI - Lists and trees", FVector2(1000.0f, 640.0f)}) {}

protected:
	TSharedRef<SWidget> MakeContent() override
	{
		for (int32 I = 0; I < 10000; ++I)
		{
			std::shared_ptr<FTestItem> Item = std::make_shared<FTestItem>();
			Item->Index = I;
			Item->Name = FString::Format("Texture {:05}", I);
			Items.push_back(Item);
		}
		TreeRoots = {
		    Node("worldspawn", "world",
		        {
		            Node("Brushes", "group",
		                {Node("floor_01", "brush"), Node("wall_north", "brush"), Node("wall_south", "brush"),
		                    Node("pillar", "brush")}),
		            Node("Detail", "group", {Node("crate_a", "brush"), Node("crate_b", "brush")}),
		        }),
		    Node("Entities", "group",
		        {
		            Node("info_player_start", "entity"),
		            Node("light", "entity"),
		            Node("func_door", "entity", {Node("door_brush", "brush")}),
		        }),
		};

		const TSharedRef<SWidget> Content =
		    SNew(SSplitter) +
		    SSplitter::Slot().Value(
		        0.5f)[SNew(SVerticalBox) +
		              SVerticalBox::Slot().AutoHeight().Padding(FMargin(
		                  0.0f, 0.0f, 0.0f, 6.0f))[SNew(STextBlock)
		                                               .Text(
		                                                   [this]
		                                                   {
			                                                   return FString::Format(
			                                                       "10,000 items, {} selected, {} row widgets "
			                                                       "(Ctrl/Shift-click, Ctrl+A, arrows, Home/End)",
			                                                       List ? List->GetNumItemsSelected() : 0,
			                                                       List ? List->GetNumGeneratedRows() : 0);
		                                                   })] +
		              SVerticalBox::Slot().FillHeight(
		                  1.0f)[SNew(SBorder)
		                            .BorderImage(&FAppStyle::Get().GetBrush("Recessed"))
		                            .Padding(1.0f)
		                                [SAssignNew(List, SListView<TSharedPtr<FTestItem>>)
		                                     .ListItemsSource(&Items)
		                                     .SelectionMode(ESelectionMode::Multi)
		                                     .OnGenerateRow(
		                                         [](TSharedPtr<FTestItem> Item,
		                                            const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
		                                         {
			                                         return SNew(
			                                             STableRow<TSharedPtr<FTestItem>>,
			                                             Owner)[SNew(SHorizontalBox) +
			                                                    SHorizontalBox::Slot().FillWidth(
			                                                        1.0f)[SNew(STextBlock).Text(Item->Name)] +
			                                                    SHorizontalBox::Slot().AutoWidth()
			                                                        [SNew(STextBlock)
			                                                             .Text(FString::Format("{}x{}",
			                                                                                   64 << (Item->Index % 4),
			                                                                                   64 << (Item->Index % 3)))
			                                                             .TextStyle(&TestTextStyle("SmallText"))]];
		                                         })
		                                     .OnSelectionChanged(
		                                         [this](TSharedPtr<FTestItem> Item, ESelectInfo)
		                                         {
			                                         if (Item)
				                                         SetLastEvent(FString::Format("List: {}", Item->Name));
		                                         })]]] +
		    SSplitter::Slot().Value(
		        0.5f)[SNew(SVerticalBox) +
		              SVerticalBox::Slot().AutoHeight().Padding(
		                  FMargin(0.0f, 0.0f, 0.0f,
		                          6.0f))[SNew(STextBlock).Text("Map outline (Left/Right collapse and expand)")] +
		              SVerticalBox::Slot().FillHeight(
		                  1.0f)[SNew(SBorder)
		                            .BorderImage(&FAppStyle::Get().GetBrush("Recessed"))
		                            .Padding(1.0f)
		                                [SAssignNew(Tree, STreeView<TSharedPtr<FMapNode>>)
		                                     .TreeItemsSource(&TreeRoots)
		                                     .OnGetChildren([](TSharedPtr<FMapNode> N, TArray<TSharedPtr<FMapNode>>&
		                                                                                   Out) { Out = N->Children; })
		                                     .OnGenerateRow(
		                                         [](TSharedPtr<FMapNode> N, const TSharedRef<STableViewBase>& Owner)
		                                             -> TSharedRef<ITableRow>
		                                         {
			                                         return SNew(
			                                             STableRow<TSharedPtr<FMapNode>>,
			                                             Owner)[SNew(SHorizontalBox) +
			                                                    SHorizontalBox::Slot().FillWidth(
			                                                        1.0f)[SNew(STextBlock).Text(N->Name)] +
			                                                    SHorizontalBox::Slot().AutoWidth()
			                                                        [SNew(STextBlock)
			                                                             .Text(N->Kind)
			                                                             .TextStyle(&TestTextStyle("SmallText"))]];
		                                         })
		                                     .OnSelectionChanged(
		                                         [this](TSharedPtr<FMapNode> N, ESelectInfo)
		                                         {
			                                         if (N)
				                                         SetLastEvent(
				                                             FString::Format("Tree: {} ({})", N->Name, N->Kind));
		                                         })]]];
		Tree->SetItemExpansion(TreeRoots[0], true);
		Tree->SetItemExpansion(TreeRoots[0]->Children[0], true);

		return TestFrame(*this, "Lists and trees", SNew(SBox).Padding(FMargin(16.0f))[Content]);
	}

	void BuildSelfTest(FTestScript& Script) override
	{
		Script.Capture("start")
		    .Check("only the visible rows exist as widgets",
		        [this] { return List->GetNumGeneratedRows() > 5 && List->GetNumGeneratedRows() < 100; })
		    .Do([this] { PointAtText("Texture 00002", true); })
		    .Check("a click selects one row",
		        [this] { return List->GetNumItemsSelected() == 1 && List->IsItemSelected(Items[2]); })
		    .Do(
		        [this]
		{
			SetModifierKeys(true, false);
			PointAtText("Texture 00006", true);
			SetModifierKeys(false, false);
		})
		    .Check("Shift-click selects the range", [this] { return List->GetNumItemsSelected() == 5; })
		    .Do(
		        [this]
		{
			SetModifierKeys(false, true);
			PointAtText("Texture 00004", true);
			SetModifierKeys(false, false);
		})
		    .Check("Ctrl-click takes one row out",
		        [this] { return List->GetNumItemsSelected() == 4 && !List->IsItemSelected(Items[4]); })
		    .Capture("selected")
		    .Do(
		        [this]
		{
			SetModifierKeys(false, true);
			PressKey(EKey::A);
			SetModifierKeys(false, false);
		})
		    .Check("Ctrl+A selects all 10,000", [this] { return List->GetNumItemsSelected() == 10000; })
		    .Do([this] { PressKey(EKey::End); })
		    .Check("End selects the last row and scrolls to it",
		        [this]
		{
			return List->GetNumItemsSelected() == 1 && List->IsItemSelected(Items.back()) &&
			    List->GetFirstVisibleIndex() > 9900;
		}, 5)
		    .Capture("end")
		    .Do([this] { Wheel(GetCenter(*List), 5.0f); })
		    .Check("the wheel scrolls up", [this] { return List->GetFirstVisibleIndex() < 9990; }, 5)
		    // The tree: select Entities, expand it with Right, collapse worldspawn with Left.
		    .Do([this] { PointAtText("Entities", true); })
		    .Do([this] { PressKey(EKey::Right); })
		    .Check("Right expands the selected item", [this] { return Tree->IsItemExpanded(TreeRoots[1]); })
		    .Do([this] { PointAtText("worldspawn", true); })
		    .Do([this] { PressKey(EKey::Left); })
		    .Check("Left collapses the selected item", [this] {
			return !Tree->IsItemExpanded(TreeRoots[0]);
		}).Capture("tree");
	}

private:
	TArray<TSharedPtr<FTestItem>> Items;
	TArray<TSharedPtr<FMapNode>> TreeRoots;
	TSharedPtr<SListView<TSharedPtr<FTestItem>>> List;
	TSharedPtr<STreeView<TSharedPtr<FMapNode>>> Tree;
};

IMPLEMENT_TEST_APP(FListsTestApp)
