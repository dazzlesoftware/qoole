// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// UIPropertyGridTest: SPropertyGrid over IPropertySource. A list of the
// demo scene's objects on the left (multiple selection), their properties
// on the right: categories, every property type (string, enum, bool,
// vector, color, float with a range, read-only int), mixed values when the
// selected objects differ, a search box, and undo of every edit.
//
// Self-test: edits a name, edits the brightness of two lights at once,
// undoes and redoes, shows mixed values, filters, and opens the color
// picker from a swatch.

#include "DemoScene.h"
#include "Framework/PropertyGrid/SPropertyGrid.h"
#include "TestApp.h"
#include "TestWidgets.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SColorPicker.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

class FPropertyGridTestApp final : public FTestApp
{
public:
	FPropertyGridTestApp() : FTestApp({ "UIPropertyGridTest", "Genesis UI - Property grid", FVector2(1000.0f, 700.0f) }) {}

protected:
	TSharedRef<SWidget> MakeContent() override
	{
		Scene.OnLog.AddLambda([this](const FString& Line) { SetLastEvent(Line); });
		Scene.OnSelectionChanged.AddLambda([this] { SyncList(); UpdateGrid(); });
		Scene.OnChanged.AddLambda([this]
		{
			SyncList();
			// The kinds of the selected objects decide which properties exist
			// (lights have a brightness): rebuild the rows when they change,
			// next frame (the change may come from a widget in the grid).
			if (SelectedKinds() != Kinds && Grid)
			{
				Kinds = SelectedKinds();
				const TWeakPtr<SPropertyGrid> WeakGrid = Grid;
				Grid->RegisterActiveTimer(0.0f, [WeakGrid](double, float)
				{
					if (const TSharedPtr<SPropertyGrid> Pinned = WeakGrid.lock())
					{
						Pinned->Refresh();
					}
					return EActiveTimerReturn::Stop;
				});
			}
		});

		const TSharedRef<SWidget> Content = SNew(SSplitter)
			+ SSplitter::Slot().Value(0.35f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text("Objects (Ctrl/Shift-click)")]
					+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(4.0f, 0.0f))[SNew(SButton).Text("Undo").IsEnabled([this] { return Scene.CanUndo(); }).OnClicked([this] { Scene.Undo(); return FReply::Handled(); })]
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text("Redo").IsEnabled([this] { return Scene.CanRedo(); }).OnClicked([this] { Scene.Redo(); return FReply::Handled(); })]
				]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Recessed")).Padding(1.0f)
					[
						SAssignNew(List, SListView<int32>)
						.ListItemsSource(&Items)
						.SelectionMode(ESelectionMode::Multi)
						.OnGenerateRow([this](int32 Id, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
						{
							return SNew(STableRow<int32>, Owner)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(STextBlock).Text([this, Id] { const FDemoEntity* E = Scene.Find(Id); return E ? E->Name : FString(); })]
								+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).TextStyle(&TestTextStyle("SmallText")).Text([this, Id] { const FDemoEntity* E = Scene.Find(Id); return E ? FString(GetEntityKindName(E->Kind)) : FString(); })]
							];
						})
						.OnSelectionChanged([this](int32, ESelectInfo)
						{
							if (!bSyncing)
							{
								Scene.SetSelection(List->GetSelectedItems());
							}
						})
					]
				]
			]
			+ SSplitter::Slot().Value(0.65f)
			[
				SAssignNew(Grid, SPropertyGrid)
				.OnBeginTransaction([this](const FString& Description) { Scene.BeginTransaction(Description); })
				.OnEndTransaction([this] { Scene.EndTransaction(); })
			];
		SyncList();
		return TestFrame(*this, "Property grid", SNew(SBox).Padding(FMargin(16.0f))[Content]);
	}

	void OnShutdown() override
	{
		Grid.reset();
		List.reset();
	}

	void BuildSelfTest(FTestScript& Script) override
	{
		Script.Capture("empty")
			.Do([this] { PointAtText("pillar", true); })
			.Check("selecting an object shows its properties", [this] { return Grid->GetObjects().size() == 1 && HasProperty("Name") && !HasProperty("Brightness"); })
			.Capture("pillar")
			.Do([this] { Grid->SetPropertyValue("Name", FString("pillar_big")); })
			.Check("editing a property changes the object", [this] { return Scene.Find(PillarId())->Name == "pillar_big"; })
			.Check("an edit is one undo step", [this] { return Scene.CanUndo(); })
			.Do([this] { PointAtText("Undo", true); })
			.Check("Undo reverts the edit", [this] { return Scene.Find(PillarId())->Name == "pillar"; })
			.Do([this] { PointAtText("Redo", true); })
			.Check("Redo applies it again", [this] { return Scene.Find(PillarId())->Name == "pillar_big"; })
			// Both lights: brightness is shared, and their values differ.
			.Do([this] { Scene.SetSelection(IdsOfKind(1)); })
			.Check("two lights selected, the grid shows their shared properties", [this] { return Grid->GetObjects().size() == 2 && HasProperty("Brightness"); }, 5)
			.Capture("mixed")
			.Do([this] { Grid->SetPropertyValue("Brightness", 4.0f); })
			.Check("one edit changes both lights", [this]
			{
				for (const int32 Id : IdsOfKind(1))
				{
					if (Scene.Find(Id)->Brightness != 4.0f)
					{
						return false;
					}
				}
				return true;
			})
			.Do([this] { Scene.Undo(); })
			.Check("one undo reverts both", [this] { return Scene.Find(IdsOfKind(1)[0])->Brightness == 1.0f && Scene.Find(IdsOfKind(1)[1])->Brightness == 2.5f; })
			// A brush and a light: no brightness.
			.Do([this] { Scene.SetSelection({ PillarId(), IdsOfKind(1)[0] }); })
			.Check("a brush and a light share no brightness", [this] { return !HasProperty("Brightness") && HasProperty("Color"); }, 5)
			// The search box.
			.Do([this] { Grid->SetFilterText("pos"); })
			.Capture("filtered", 5)
			.Do([this] { Grid->SetFilterText(""); })
			// The color picker from a swatch.
			.Do([this]
			{
				Scene.SetSelection({ PillarId() });
			})
			.Do([this]
			{
				if (const SColorBlock* Swatch = FindWidget<SColorBlock>(*Grid); Expect(Swatch != nullptr, "the grid shows a color swatch"))
				{
					Click(GetCenter(*Swatch));
				}
			}, 5)
			.Check("a click on the swatch opens the color picker", [this] { return GetApp().GetMenuCount() == 1; }, 5)
			.Capture("color_picker")
			.Do([this] { PressKey(EKey::Escape); });
	}

private:
	TArray<int32> SelectedKinds() const
	{
		TArray<int32> Result;
		for (const int32 Id : Scene.Selection)
		{
			const FDemoEntity* E = Scene.Find(Id);
			Result.push_back(E ? E->Kind : -1);
		}
		return Result;
	}

	TArray<int32> IdsOfKind(int32 Kind) const
	{
		TArray<int32> Ids;
		for (const FDemoEntity& E : Scene.Entities)
		{
			if (E.Kind == Kind)
			{
				Ids.push_back(E.Id);
			}
		}
		return Ids;
	}

	int32 PillarId() const { return 5; }

	bool HasProperty(const char* Name) const
	{
		for (const FPropertyDesc& Desc : Grid->GetSharedProperties())
		{
			if (Desc.Name.View() == Name)
			{
				return true;
			}
		}
		return false;
	}

	void UpdateGrid()
	{
		if (!Grid)
		{
			return;
		}
		TArray<TSharedRef<IPropertySource>> Sources;
		for (const int32 Id : Scene.Selection)
		{
			Sources.push_back(TSharedRef<IPropertySource>(std::make_shared<FDemoEntitySource>(Scene, Id)));
		}
		Kinds = SelectedKinds();
		Grid->SetObjects(Sources);
	}

	// The list follows the scene and its selection.
	void SyncList()
	{
		if (!List)
		{
			return;
		}
		bSyncing = true;
		TArray<int32> Ids;
		for (const FDemoEntity& E : Scene.Entities)
		{
			Ids.push_back(E.Id);
		}
		if (Ids != Items)
		{
			Items = Ids;
			List->RequestListRefresh();
		}
		List->ClearSelection();
		for (const int32 Id : Scene.Selection)
		{
			List->SetItemSelection(Id, true);
		}
		bSyncing = false;
	}

	FDemoScene Scene;
	TArray<int32> Items;
	TArray<int32> Kinds;
	TSharedPtr<SListView<int32>> List;
	TSharedPtr<SPropertyGrid> Grid;
	bool bSyncing = false;
};

GENESIS_TEST_APP(FPropertyGridTestApp)
