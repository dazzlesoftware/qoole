// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// UIDockingTest: docking. A tab manager with tab spawners, a dock area of
// splitters and tab stacks (document and panel tabs), tabs dragged by their
// titles onto another stack, a stack's edge or outside the window (a
// floating window), the Window menu that reopens closed tabs, and the
// layout saved and restored between runs.
//
// Self-test: drags a tab onto another stack with the mouse, tears a tab off
// into a window and docks it back, closes and reopens a tab, saves the
// layout and restores it, and resets to the default.

#include "Framework/Docking/SDockingArea.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MenuRegistry.h"
#include "TestApp.h"
#include "TestWidgets.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Text/SEditableText.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

class FDockingTestApp final : public FTestApp
{
public:
	FDockingTestApp() : FTestApp({ "UIDockingTest", "Genesis UI - Docking", FVector2(1100.0f, 720.0f), true }) {}

protected:
	bool StartServices() override
	{
		Tabs = FTabManager::Create().ToSharedPtr();
		RegisterTabs();
		FMenuRegistry& Registry = FMenuRegistry::Get();
		FMenuSection& Bar = Registry.RegisterMenu("DockingTest.MainMenu").AddSection("Menus");
		Bar.AddSubMenu("Window", "Window", "", FName("DockingTest.MainMenu.Window"));
		FMenuDefinition& Window = Registry.RegisterMenu("DockingTest.MainMenu.Window");
		Window.AddDynamicSection("Tabs", [this](FMenuSection& Section, const FMenuContext&) { Tabs->PopulateTabSpawnerMenu(Section); });
		FUIAction Reset;
		Reset.Execute = FSimpleDelegate::CreateLambda([this] { ResetLayout(); });
		Window.AddSection("Layout").AddEntry("ResetLayout", "Reset layout", "Go back to the default layout", Reset, EUserInterfaceActionType::Button);
		return true;
	}

	TSharedRef<SWidget> MakeContent() override
	{
		FString Saved;
		if (!GetSettingsDir().IsEmpty())
		{
			FFileHelper::LoadFileToString(Saved, GetSettingsDir() + "Layout.json");
		}
		DockArea = Tabs->RestoreFrom(FTabManager::LoadLayout(Saved, MakeDefaultLayout()), GetMainWindow()).ToSharedPtr();
		const FMenuContext Context(ToSharedRef(Commands));
		return TestFrame(*this, "Docking",
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Header")).Padding(FMargin(6.0f, 0.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[FMenuRegistry::Get().GenerateMenuBar("DockingTest.MainMenu", Context)]
					+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right).VAlign(VAlign_Center)[SNew(STextBlock).TextStyle(&TestTextStyle("SmallText")).Text([this]
					{
						const TSharedPtr<SDockTab> Active = Tabs->GetActiveTab();
						return FString::Format("Active tab: {}   ·   {} dock areas", Active ? Active->GetLabel() : FString("none"), Tabs->GetAreas().size());
					})]
				]
			]
			+ SVerticalBox::Slot().FillHeight(1.0f)[SAssignNew(DockHost, SBox)[ToSharedRef(DockArea)]]);
	}

	void OnShutdown() override
	{
		if (!GetSettingsDir().IsEmpty() && Tabs)
		{
			SaveLayout(GetSettingsDir() + "Layout.json");
		}
		DockArea.reset();
		DockHost.reset();
		Tabs.reset();
	}

	void BuildSelfTest(FTestScript& Script) override
	{
		Script.Capture("default")
			// Drag the Log tab by its title onto the stack of the Viewport tab.
			.Do([this] { PointAtText("Log", true); })
			.Do([this]
			{
				const TSharedPtr<SDockTab> Viewport = Tabs->FindExistingLiveTab("Viewport");
				const FVector2 Target = GetCenter(*Viewport->GetParentStack());
				MouseDown(LabelCenter("Log"));
				for (int32 Step = 1; Step <= 10; ++Step)
				{
					MoveMouse(LabelCenter("Log") + (Target - LabelCenter("Log")) * (static_cast<float>(Step) / 10.0f));
				}
			})
			.Capture("dragging_tab", 5)
			.Do([this] { MouseUp(GetCenter(*Tabs->FindExistingLiveTab("Viewport")->GetParentStack())); })
			.Check("dropping a tab on a stack moves it there", [this] { return SameStack("Log", "Viewport"); }, 5)
			.Capture("log_moved")
			// Tear Details off into a window, then dock it back right of the viewport.
			.Do([this]
			{
				const FIntPoint Pos = GetMainWindow()->GetNativeWindow()->GetClientPosition();
				Tabs->MoveTabToNewWindow(ToSharedRef(Tabs->FindExistingLiveTab("Details")), FIntPoint(Pos.X + 200, Pos.Y + 160), FVector2(380.0f, 420.0f));
			})
			.Check("a torn-off tab gets a window of its own", [this] { return GetApp().GetWindows().size() == 2 && Tabs->GetAreas().size() == 2; }, 10)
			.Capture("torn_off", 15)
			.Do([this]
			{
				Tabs->MoveTabToNewStack(ToSharedRef(Tabs->FindExistingLiveTab("Details")), ToSharedRef(Tabs->FindExistingLiveTab("Viewport")->GetParentStack()), EUINavigation::Right);
			})
			.Check("docking it back closes the window", [this] { return GetApp().GetWindows().size() == 1; }, 10)
			// Close Notes; the Window menu reopens it.
			.Do([this] { Tabs->FindExistingLiveTab("Notes")->RequestCloseTab(); })
			.Check("a closed tab is gone", [this] { return !Tabs->FindExistingLiveTab("Notes"); }, 5)
			.Do([this] { PointAtText("Window", true); })
			.Capture("window_menu", 10)
			.Do([this] { PointAtText("Notes", true); })
			.Check("the Window menu reopens it", [this] { return Tabs->FindExistingLiveTab("Notes") != nullptr; }, 5)
			// Save, reset, restore.
			.Do([this]
			{
				SaveLayout(GetSelfTestDir() + "Layout.json");
				ResetLayout();
			})
			.Check("Reset layout puts Log back with Outliner", [this] { return SameStack("Log", "Outliner") && !SameStack("Log", "Viewport"); }, 5)
			.Capture("reset")
			.Do([this]
			{
				FString Saved;
				FFileHelper::LoadFileToString(Saved, GetSelfTestDir() + "Layout.json");
				RestoreLayout(FTabManager::LoadLayout(Saved, MakeDefaultLayout()));
			})
			.Check("restoring the saved layout brings the moved tabs back", [this] { return SameStack("Log", "Viewport") && Tabs->FindExistingLiveTab("Details") != nullptr; }, 5)
			.Capture("restored");
	}

private:
	static TSharedRef<FDockLayout> MakeDefaultLayout()
	{
		return FDockLayout::New("DockingTest", 1)->AddArea(
			FDockLayout::NewPrimaryArea()->SetOrientation(EOrientation::Horizontal)
				->Split(FDockLayout::NewStack()->SetSizeCoefficient(0.22f)->AddTab("Outliner")->AddTab("Log")->SetForegroundTab("Outliner"))
				->Split(FDockLayout::NewStack()->SetSizeCoefficient(0.53f)->AddTab("Viewport")->AddTab("Notes")->SetForegroundTab("Viewport"))
				->Split(FDockLayout::NewStack()->SetSizeCoefficient(0.25f)->AddTab("Details")));
	}

	void RegisterTabs()
	{
		const auto Panel = [](const char* Text) -> TSharedRef<SWidget>
		{
			return SNew(SBox).Padding(FMargin(12.0f))[SNew(STextBlock).Text(Text).AutoWrapText(true).TextStyle(&TestTextStyle("SmallText"))];
		};
		Tabs->RegisterTabSpawner("Viewport", [](const FSpawnTabArgs&)
		{
			return SNew(SDockTab).Label("Viewport").Role(ETabRole::Document)
			[
				SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Recessed")).HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text("A document tab: drag tabs onto this stack, onto its edges, or outside the window").TextStyle(&TestTextStyle("SmallText"))
				]
			];
		}).SetDisplayName("Viewport").SetToolTip("A document tab");
		Tabs->RegisterTabSpawner("Notes", [](const FSpawnTabArgs&)
		{
			return SNew(SDockTab).Label("Notes").Role(ETabRole::Document)[SNew(SEditableTextBox).IsMultiLine(true).Text("A second document; its text survives being moved between stacks.")];
		}).SetDisplayName("Notes").SetToolTip("A document tab with text");
		Tabs->RegisterTabSpawner("Outliner", [Panel](const FSpawnTabArgs&) { return SNew(SDockTab).Label("Outliner")[Panel("A panel tab.")]; }).SetDisplayName("Outliner");
		Tabs->RegisterTabSpawner("Log", [Panel](const FSpawnTabArgs&) { return SNew(SDockTab).Label("Log")[Panel("Another panel tab.")]; }).SetDisplayName("Log");
		Tabs->RegisterTabSpawner("Details", [Panel](const FSpawnTabArgs&) { return SNew(SDockTab).Label("Details")[Panel("Tear me off: drag my title outside the window.")]; }).SetDisplayName("Details");
	}

	void ResetLayout()
	{
		RestoreLayout(MakeDefaultLayout());
		SetLastEvent("Layout reset");
	}

	void RestoreLayout(const TSharedRef<FDockLayout>& Layout)
	{
		Tabs->CloseAllAreas();
		DockArea = Tabs->RestoreFrom(Layout, GetMainWindow()).ToSharedPtr();
		DockHost->SetContent(ToSharedRef(DockArea));
	}

	void SaveLayout(const FString& Path) const
	{
		const FString Layout = Tabs->PersistLayout()->ToJson().ToString();
		FFileHelper::SaveArrayToFile(TArrayView<const uint8>(reinterpret_cast<const uint8*>(*Layout), static_cast<SIZE_T>(Layout.Len())), Path);
	}

	bool SameStack(const char* A, const char* B) const
	{
		const TSharedPtr<SDockTab> TabA = Tabs->FindExistingLiveTab(A);
		const TSharedPtr<SDockTab> TabB = Tabs->FindExistingLiveTab(B);
		return TabA && TabB && TabA->GetParentStack() == TabB->GetParentStack();
	}

	// The center of a tab's title (its label text in the stack's tab well).
	FVector2 LabelCenter(const char* Label) const
	{
		const STextBlock* Text = FindWidget<STextBlock>(*GetMainWindow(), [Label](STextBlock& Block) { return Block.GetText().View() == Label; });
		return Text ? GetCenter(*Text) : FVector2();
	}

	TSharedPtr<FTabManager> Tabs;
	TSharedPtr<FUICommandList> Commands = std::make_shared<FUICommandList>();
	TSharedPtr<SWidget> DockArea;
	TSharedPtr<SBox> DockHost;
};

GENESIS_TEST_APP(FDockingTestApp)
