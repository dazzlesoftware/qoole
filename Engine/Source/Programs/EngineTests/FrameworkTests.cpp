// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Headless tests of the editor framework (milestone 4): JSON, commands and
// key bindings, the menu registry, menu bars and toolbars, docking, the
// property grid, the value editors and the viewport widget.

#include "UITestHelpers.h"
#include "Containers/ContainerAliases.h"
#include "Templates/SharedPointer.h"

#include "Framework/Docking/SDockingArea.h"
#include "Framework/MultiBox/MenuRegistry.h"
#include "Framework/PropertyGrid/SPropertyGrid.h"
#include "Serialization/Json.h"
#include "Widgets/Input/SColorPicker.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/SViewport.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	// Finds widgets of a type under Root (arranged children only).
	template <typename T> void FindWidgets(const SWidget& Root, TArray<T*>& Out)
	{
		if (T* Typed = dynamic_cast<T*>(const_cast<SWidget*>(&Root)))
		{
			Out.push_back(Typed);
		}
		for (const FArrangedWidget& Child : Root.GetArrangedChildren())
		{
			FindWidgets(Child.Widget.Get(), Out);
		}
	}

	// Text of every STextBlock under Root, in order.
	TArray<FString> TextsUnder(const SWidget& Root)
	{
		TArray<STextBlock*> Blocks;
		FindWidgets(Root, Blocks);
		TArray<FString> Texts;
		for (STextBlock* Block : Blocks)
		{
			Texts.push_back(Block->GetText());
		}
		return Texts;
	}

	STextBlock* FindText(const SWidget& Root, std::string_view Text)
	{
		TArray<STextBlock*> Blocks;
		FindWidgets(Root, Blocks);
		for (STextBlock* Block : Blocks)
		{
			if (Block->GetText().View() == Text)
			{
				return Block;
			}
		}
		return nullptr;
	}

	struct FCommandsFixture
	{
		TSharedRef<FBindingContext> Context = MakeShared<FBindingContext>(FName("TestEditor"), "Test editor");
		TSharedRef<FUICommandInfo> Save = Context->AddCommand("Save", "Save", "Save the map",
		    EUserInterfaceActionType::Button, FInputChord(EKey::S, EModifierKey::Control));
		TSharedRef<FUICommandInfo> Grid = Context->AddCommand(
		    "Grid", "Show grid", "Toggle the grid", EUserInterfaceActionType::ToggleButton, FInputChord(EKey::G));
		TSharedRef<FUICommandInfo> Delete = Context->AddCommand(
		    "Delete", "Delete", "Delete the selection", EUserInterfaceActionType::Button, FInputChord(EKey::Delete));
		TSharedRef<FUICommandList> List = MakeShared<FUICommandList>();
		int32 Saves = 0;
		bool bGrid = false;
		bool bCanDelete = false;
		int32 Deletes = 0;

		FCommandsFixture()
		{
			FInputBindingManager::Get().RegisterContext(Context);
			List->MapAction(Save, [this] { ++Saves; });
			List->MapAction(Grid, [this] { bGrid = !bGrid; }, nullptr, [this] { return bGrid; });
			List->MapAction(Delete, [this] { ++Deletes; }, [this] { return bCanDelete; });
		}
		~FCommandsFixture()
		{
			FInputBindingManager::Get().UnregisterContext("TestEditor");
			FMenuRegistry::Get().RemoveAll();
		}
	};

	class FTestSource final : public IPropertySource
	{
	public:
		explicit FTestSource(FString InName, bool bExtra = false) : Name(std::move(InName)), bHasExtra(bExtra) {}

		FString GetDisplayName() const override
		{
			return Name;
		}
		void GetProperties(TArray<FPropertyDesc>& Out) const override
		{
			Out.push_back(FPropertyDesc("Visible", EPropertyType::Bool, "Visible", "Display"));
			Out.push_back(
			    FPropertyDesc("Brightness", EPropertyType::Float, "Brightness", "Light").SetRange(0.0f, 10.0f));
			Out.push_back(FPropertyDesc("Style", EPropertyType::Enum, "Style", "Light")
			        .SetOptions({"Steady", "Flicker", "Pulse"}));
			Out.push_back(FPropertyDesc("Color", EPropertyType::Color, "Color", "Light"));
			Out.push_back(FPropertyDesc("Origin", EPropertyType::Vector3, "Origin", "Transform"));
			Out.push_back(FPropertyDesc("Target", EPropertyType::String, "Target name", "Logic"));
			if (bHasExtra)
			{
				Out.push_back(FPropertyDesc("Only", EPropertyType::Int, "Only on one", "Logic"));
			}
		}
		FPropertyValue GetValue(FName Property) const override
		{
			TMap<FName, FPropertyValue>::const_iterator It = Values.find(Property);
			return It == Values.end() ? FPropertyValue(false) : It->second;
		}
		void SetValue(FName Property, const FPropertyValue& Value) override
		{
			Values[Property] = Value;
		}

		FString Name;
		bool bHasExtra = false;
		TMap<FName, FPropertyValue> Values{
		    {FName("Visible"), FPropertyValue(true)},
		    {FName("Brightness"), FPropertyValue(1.0f)},
		    {FName("Style"), FPropertyValue(int32(0))},
		    {FName("Color"), FPropertyValue(FColor(255, 200, 100))},
		    {FName("Origin"), FPropertyValue(FPropertyVector3{0.0f, 0.0f, 64.0f})},
		    {FName("Target"), FPropertyValue(FString("door1"))},
		    {FName("Only"), FPropertyValue(int32(7))},
		};
	};

	class FTestViewportClient final : public IViewportClient
	{
	public:
		TSharedPtr<ISlateRenderTarget> GetRenderTarget() const override
		{
			return nullptr;
		}
		void OnResize(FIntPoint Size) override
		{
			Sizes.push_back(Size);
		}
		FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event) override
		{
			Log.push_back("down");
			if (Event.GetEffectingButton() == EMouseButton::Right && Viewport)
			{
				return FReply::Handled().UseHighPrecisionMouseMovement(
				    ToSharedRef(StaticCastSharedPtr<SWidget>(Viewport)));
			}
			return FReply::Handled();
		}
		FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override
		{
			Log.push_back("up");
			return FReply::Handled().ReleaseMouseCapture();
		}
		FReply OnRawMouseMove(FVector2 Delta) override
		{
			RawTotal += Delta;
			return FReply::Handled();
		}
		FReply OnKeyDown(const FGeometry&, const FKeyEvent& Event) override
		{
			Log.push_back(FString("key ") + ToString(Event.GetKey()));
			return FReply::Handled();
		}
		void SetViewport(const TSharedPtr<SViewport>& InViewport) override
		{
			Viewport = InViewport;
		}

		TSharedPtr<SViewport> Viewport;
		TArray<FIntPoint> Sizes;
		TArray<FString> Log;
		FVector2 RawTotal;
	};
} // namespace

TEST_SUITE("UIFramework")
{
	// --- JSON -------------------------------------------------------------------

	TEST_CASE("JSON: build, write, parse back")
	{
		FJsonValue Root = FJsonValue::MakeObject();
		Root["Name"] = "Editor";
		Root["Version"] = 3;
		Root["Ratio"] = 0.25;
		Root["Flags"].Push(true).Push(nullptr);
		Root["Flags"].Push("tab \"quoted\"\né");
		Root["Nested"]["Deep"] = FJsonValue::MakeArray();
		for (const bool bPretty : {true, false})
		{
			const FString Text = Root.ToString(bPretty);
			const TOptional<FJsonValue> Back = FJsonValue::Parse(Text.View());
			REQUIRE(Back);
			CHECK(*Back == Root);
			CHECK(Back->GetKeys() == TArray<FString>{"Name", "Version", "Ratio", "Flags", "Nested"}); // order kept
		}
		CHECK(Root.Get("Version").AsInt() == 3);
		CHECK(Root.Get("Missing").IsNull());
	}

	TEST_CASE("JSON: escapes and errors")
	{
		const TOptional<FJsonValue> V = FJsonValue::Parse(R"({"a": "é😀\t", "n": -1.5e2, "e": []})");
		REQUIRE(V);
		CHECK(V->Get("a").AsString() == "\xC3\xA9\xF0\x9F\x98\x80\t");
		CHECK(V->Get("n").AsNumber() == -150.0);
		FString Error;
		CHECK(!FJsonValue::Parse("{\n  \"a\": 1,\n  \"b\": tru\n}", &Error));
		CHECK(Error.Contains("line 3"));
		CHECK(!FJsonValue::Parse("[1, 2,]"));
		CHECK(!FJsonValue::Parse("{} extra"));
	}

	// --- Commands -----------------------------------------------------------------

	TEST_CASE("Input chords: text round trip")
	{
		const FInputChord Chord(EKey::S, EModifierKey::Control | EModifierKey::Shift);
		CHECK(Chord.ToString() == "Ctrl+Shift+S");
		CHECK(FInputChord::Parse("Ctrl+Shift+S") == Chord);
		CHECK(FInputChord::Parse("ctrl+shift+s") == Chord);
		CHECK(FInputChord::Parse("F5") == FInputChord(EKey::F5));
		CHECK(FInputChord::Parse("Alt+Delete") == FInputChord(EKey::Delete, EModifierKey::Alt));
		CHECK(FInputChord(EKey::Comma, EModifierKey::Control).ToString() == "Ctrl+,");
		CHECK(!FInputChord::Parse("Hyper+X"));
	}

	TEST_CASE("Commands: shortcuts run through the scope; focused text keeps its keys; disabled commands do not run")
	{
		FCommandsFixture C;
		FUITestApp T;
		TSharedPtr<SEditableTextBox> Text;
		T.AddWindow(SNew(SCommandBindingScope)
		        .CommandList(C.List.ToSharedPtr())[SNew(SVerticalBox) +
		            SVerticalBox::Slot().AutoHeight()[SAssignNew(Text, SEditableTextBox)]]);
		T.Key(EKey::S, false, true); // nothing focused: the window content's scope sees it
		CHECK(C.Saves == 1);
		T.Key(EKey::G);
		CHECK(C.bGrid);
		T.Key(EKey::Delete);
		CHECK(C.Deletes == 0); // can't execute
		C.bCanDelete = true;
		T.Key(EKey::Delete);
		CHECK(C.Deletes == 1);

		// A focused text box takes G and Delete itself.
		T.GetApp().SetKeyboardFocus(Text->GetEditableText());
		T.Type("g");
		T.Key(EKey::G);
		CHECK(C.bGrid);
		CHECK(Text->GetText() == "g");
		T.Key(EKey::S, false, true); // the text box does not use Ctrl+S: it bubbles
		CHECK(C.Saves == 2);
	}

	TEST_CASE("Key bindings: rebinding displaces conflicts, saves and loads, resets")
	{
		FCommandsFixture C;
		TSharedRef<FBindingContext> Child =
		    MakeShared<FBindingContext>(FName("TestViewport"), "Viewport", FName("TestEditor"));
		const TSharedRef<FUICommandInfo> Focus =
		    Child->AddCommand("Focus", "Focus", "", EUserInterfaceActionType::Button, FInputChord(EKey::F));
		FInputBindingManager& Manager = FInputBindingManager::Get();
		Manager.RegisterContext(Child);

		CHECK(Manager.FindCommandForChord("TestViewport", FInputChord(EKey::S, EModifierKey::Control)) ==
		    C.Save.ToSharedPtr()); // through the parent
		const TSharedPtr<FUICommandInfo> Displaced = Manager.SetUserChord(Focus, FInputChord(EKey::G));
		CHECK(Displaced == C.Grid.ToSharedPtr());
		CHECK(!C.Grid->GetActiveChord().IsValid());
		CHECK(Focus->GetInputText() == "G");

		const FJsonValue Saved = Manager.SaveUserBindings();
		CHECK(Saved.Get("TestViewport").Get("Focus").AsString() == "G");
		CHECK(Saved.Get("TestEditor").Get("Grid").AsString() == "");
		CHECK(!Saved.Get("TestEditor").Contains("Save")); // unchanged

		Manager.ResetToDefaults();
		CHECK(C.Grid->GetInputText() == "G");
		CHECK(Focus->GetInputText() == "F");
		Manager.LoadUserBindings(Saved);
		CHECK(Focus->GetInputText() == "G");
		CHECK(!C.Grid->GetActiveChord().IsValid());
		Manager.ResetToDefaults();
		Manager.UnregisterContext("TestViewport");
	}

	// --- Menus ---------------------------------------------------------------------

	TEST_CASE("Menu registry: sections, extensions before registration, insert order, dynamic sections, parents")
	{
		FCommandsFixture C;
		FMenuRegistry& Registry = FMenuRegistry::Get();
		// A plug-in extends the menu before it exists.
		Registry.ExtendMenu("Test.File")
		    .FindOrAddSection("IO")
		    .AddEntry("Export", "Export...", "", FUIAction(FSimpleDelegate::CreateLambda([] {
		}))).InsertAfter("Save");
		Registry.ExtendMenu("Test.File")
		    .AddSection("Plugin", "Plug-in", FMenuInsert{"IO", EMenuInsertPosition::Before})
		    .AddEntry("Hello", "Hello", "", FUIAction(FSimpleDelegate::CreateLambda([] {})));

		FMenuDefinition& File = Registry.RegisterMenu("Test.File");
		FMenuSection& IO = File.AddSection("IO", "File");
		IO.AddCommand(C.Save);
		IO.AddSeparator();
		IO.AddCommand(C.Delete);
		IO.AddEntry("First", "First", "", FUIAction(FSimpleDelegate::CreateLambda([] {}))).InsertFirst();
		File.AddDynamicSection("Recent", [](FMenuSection& Section, const FMenuContext&)
		{
			for (const char* Map : {"a.map", "b.map"})
			{
				Section.AddEntry(Map, Map, "", FUIAction(FSimpleDelegate::CreateLambda([] {})));
			}
		});
		Registry.RegisterMenu("Test.FileExtra", "Test.File")
		    .AddSection("Extra")
		    .AddEntry("More", "More", "", FUIAction(FSimpleDelegate::CreateLambda([] {})));

		const FMenuContext Context(C.List);
		const TArray<FMenuSection> Sections = Registry.ResolveMenu("Test.File", Context);
		REQUIRE(Sections.size() == 3);
		CHECK(Sections[0].Name == FName("Plugin"));
		CHECK(Sections[1].Name == FName("IO"));
		TArray<FName> Names;
		for (const FMenuEntry& Entry : Sections[1].Entries)
		{
			Names.push_back(Entry.Name);
		}
		CHECK(Names == TArray<FName>{"First", "Save", "Export", FName(), "Delete"});
		CHECK(Sections[2].Entries.size() == 2);
		CHECK(Registry.ResolveMenu("Test.FileExtra", Context).size() == 4);

		// Commands nobody mapped are hidden.
		const FMenuContext Empty;
		const TArray<FMenuSection> Unmapped = Registry.ResolveMenu("Test.File", Empty);
		for (const FMenuSection& Section : Unmapped)
		{
			for (const FMenuEntry& Entry : Section.Entries)
			{
				CHECK(Entry.Kind != EMenuEntryKind::Command);
			}
		}
	}

	TEST_CASE("Menu bar and toolbar: commands run, show shortcuts, check and disabled states")
	{
		FCommandsFixture C;
		FMenuRegistry& Registry = FMenuRegistry::Get();
		Registry.RegisterMenu("Test.Main").AddSection("Menus").AddSubMenu("File", "File", "", FName("Test.Main.File"));
		Registry.ExtendMenu("Test.Main")
		    .FindOrAddSection("Menus")
		    .AddSubMenu("View", "View", "", FName("Test.Main.View"));
		Registry.RegisterMenu("Test.Main.File").AddSection("IO").AddCommand(C.Save);
		Registry.RegisterMenu("Test.Main.View").AddSection("Show").AddCommand(C.Grid);
		FMenuSection& Tools = Registry.RegisterMenu("Test.Tools").AddSection("Edit");
		Tools.AddCommand(C.Save);
		Tools.AddCommand(C.Grid);
		Tools.AddCommand(C.Delete);

		FUITestApp T;
		const FMenuContext Context(C.List);
		const TSharedRef<SWindow> Window = T.AddWindow(SNew(SVerticalBox) +
		        SVerticalBox::Slot().AutoHeight()[Registry.GenerateMenuBar("Test.Main", Context)] +
		        SVerticalBox::Slot().AutoHeight()[Registry.GenerateToolBar("Test.Tools", Context)] +
		        SVerticalBox::Slot().FillHeight(1.0f)[SNew(SBox)],
		    FVector2(500.0f, 300.0f));

		// The menu bar: open File, the entry shows the shortcut, hover View switches.
		STextBlock* File = FindText(*Window, "File");
		REQUIRE(File);
		T.ClickWidget(*File);
		CHECK(T.GetApp().GetMenuCount() == 1);
		STextBlock* Gesture = FindText(*Window, "Ctrl+S");
		CHECK(Gesture != nullptr);
		STextBlock* View = FindText(*Window, "View");
		REQUIRE(View);
		T.HoverWidget(*View);
		CHECK(T.GetApp().GetMenuCount() == 1);
		STextBlock* ShowGrid = FindText(*Window, "Show grid");
		REQUIRE(ShowGrid);
		T.ClickWidget(*ShowGrid);
		CHECK(C.bGrid);
		CHECK(T.GetApp().GetMenuCount() == 0);

		// The toolbar: Save runs, Delete is disabled until it can run.
		TArray<SButton*> Buttons;
		FindWidgets(*Window, Buttons);
		SButton* SaveButton = nullptr;
		SButton* DeleteButton = nullptr;
		for (SButton* Button : Buttons)
		{
			const TArray<FString> Texts = TextsUnder(*Button);
			if (!Texts.empty() && Texts[0] == "Save")
				SaveButton = Button;
			if (!Texts.empty() && Texts[0] == "Delete")
				DeleteButton = Button;
		}
		REQUIRE(SaveButton);
		REQUIRE(DeleteButton);
		T.ClickWidget(*SaveButton);
		CHECK(C.Saves == 1);
		CHECK(!DeleteButton->IsEnabledInHierarchy());
		T.ClickWidget(*DeleteButton);
		CHECK(C.Deletes == 0);
		C.bCanDelete = true;
		T.Tick();
		CHECK(DeleteButton->IsEnabledInHierarchy());
		T.ClickWidget(*DeleteButton);
		CHECK(C.Deletes == 1);
	}

	// --- Docking -------------------------------------------------------------------

	TEST_CASE("Docking: restore, invoke closed tabs where they were, persist and restore again")
	{
		FUITestApp T;
		const TSharedRef<FTabManager> Tabs = FTabManager::Create();
		for (const char* Id : {"Outliner", "Details", "Viewport", "Log"})
		{
			Tabs->RegisterTabSpawner(Id, [Id](const FSpawnTabArgs&)
			{ return SNew(SDockTab).Label(Id)[SNew(STextBlock).Text(FString(Id) + " content")]; });
		}
		const TSharedRef<FDockLayout> Default =
		    FDockLayout::New("TestLayout", 2)
		        ->AddArea(FDockLayout::NewPrimaryArea()
		                ->SetOrientation(EOrientation::Horizontal)
		                ->Split(FDockLayout::NewStack()
		                        ->SetSizeCoefficient(0.25f)
		                        ->AddTab("Outliner")
		                        ->AddTab("Log", ETabState::Closed))
		                ->Split(FDockLayout::NewSplitter()
		                        ->SetOrientation(EOrientation::Vertical)
		                        ->SetSizeCoefficient(0.75f)
		                        ->Split(FDockLayout::NewStack()->AddTab("Viewport"))
		                        ->Split(FDockLayout::NewStack()->AddTab("Details"))));

		const TSharedRef<SWindow> Window = SNew(SWindow).ClientSize(FVector2(800.0f, 600.0f));
		T.GetApp().AddWindow(Window);
		Window->SetContent(Tabs->RestoreFrom(Default, Window.ToSharedPtr()));
		T.Tick();
		CHECK(Tabs->GetLiveTabs().size() == 3);
		CHECK(!Tabs->FindExistingLiveTab("Log"));
		// Sizes follow the coefficients.
		const TSharedPtr<SDockTab> Outliner = Tabs->FindExistingLiveTab("Outliner");
		REQUIRE(Outliner);
		CHECK(FUITestApp::RectOf(*Outliner->GetParentStack()).Width() == doctest::Approx(197.5f).epsilon(0.02));
		CHECK(FindText(*Window, "Viewport content") != nullptr);

		// Log reopens in the Outliner's stack (where it was closed).
		const TSharedPtr<SDockTab> Log = Tabs->TryInvokeTab("Log");
		REQUIRE(Log);
		CHECK(Log->GetParentStack() == Outliner->GetParentStack());
		CHECK(Log->IsForeground());
		CHECK(Tabs->GetActiveTab() == Log);

		// Close Details: its stack goes away, and it remembers where it was.
		const TSharedPtr<SDockTab> Details = Tabs->FindExistingLiveTab("Details");
		REQUIRE(Details);
		CHECK(Details->RequestCloseTab());
		T.Tick();
		CHECK(!Tabs->FindExistingLiveTab("Details"));

		// Persist -> JSON -> load -> restore gives the same tabs.
		const FString Json = Tabs->PersistLayout()->ToJson().ToString();
		Tabs->CloseAllAreas();
		CHECK(Tabs->GetLiveTabs().empty());
		const TSharedRef<FDockLayout> Loaded = FTabManager::LoadLayout(Json, Default);
		CHECK(Loaded != Default);
		Window->SetContent(Tabs->RestoreFrom(Loaded, Window.ToSharedPtr()));
		T.Tick();
		TArray<FString> Ids;
		for (const TSharedRef<SDockTab>& Tab : Tabs->GetLiveTabs())
		{
			Ids.push_back(Tab->GetTabId().ToString());
		}
		std::sort(Ids.begin(), Ids.end());
		CHECK(Ids == TArray<FString>{"Log", "Outliner", "Viewport"});
		CHECK(Tabs->FindExistingLiveTab("Log")->IsForeground());
		CHECK(Tabs->TryInvokeTab("Details") != nullptr);

		// Another version is ignored.
		const TSharedRef<FDockLayout> NewerDefault = FDockLayout::New("TestLayout", 3);
		CHECK(FTabManager::LoadLayout(Json, NewerDefault) == NewerDefault);
		CHECK(FTabManager::LoadLayout("{ not json", Default) == Default);
	}

	TEST_CASE("Docking: drag tabs to reorder, to split a stack, out to a floating window and back")
	{
		FUITestApp T;
		const TSharedRef<FTabManager> Tabs = FTabManager::Create();
		for (const char* Id : {"A", "B", "C"})
		{
			Tabs->RegisterTabSpawner(
			    Id, [Id](const FSpawnTabArgs&) { return SNew(SDockTab).Label(FString("Tab ") + Id)[SNew(SBox)]; });
		}
		const TSharedRef<FDockLayout> Layout = FDockLayout::New("Drag", 1)->AddArea(
		    FDockLayout::NewPrimaryArea()->Split(FDockLayout::NewStack()->AddTab("A")->AddTab("B")->AddTab("C")));
		const TSharedRef<SWindow> Window = SNew(SWindow).ClientSize(FVector2(600.0f, 400.0f));
		T.GetApp().AddWindow(Window);
		Window->SetContent(Tabs->RestoreFrom(Layout, Window.ToSharedPtr()));
		T.Tick();

		const TFunction<TArray<FString>()> Order = [&Tabs]()
		{
			TArray<FString> Ids;
			for (const TSharedRef<SDockTab>& Tab : Tabs->FindExistingLiveTab("A")->GetParentStack()->GetTabs())
			{
				Ids.push_back(Tab->GetTabId().ToString());
			}
			return Ids;
		};
		const TFunction<void(const SWidget&, FVector2)> Drag = [&](const SWidget& From, FVector2 To)
		{
			const FVector2 Start = FUITestApp::CenterOf(From);
			T.MouseMove(Window, Start);
			T.MouseDown(Window, Start);
			T.MouseMove(Window, Start + FVector2(12.0f, 0.0f));
			T.Tick();
			REQUIRE(T.GetApp().IsDragDropping());
			T.MouseMove(Window, To);
			T.Tick();
			T.MouseUp(Window, To);
			T.Tick();
			CHECK(!T.GetApp().IsDragDropping());
		};

		// Reorder: drag A's tab past C's.
		STextBlock* TabA = FindText(*Window, "Tab A");
		STextBlock* TabC = FindText(*Window, "Tab C");
		REQUIRE(TabA);
		REQUIRE(TabC);
		Drag(*TabA, FUITestApp::RectOf(*TabC).GetCenter() + FVector2(40.0f, 0.0f));
		CHECK(Order() == TArray<FString>{"B", "C", "A"});

		// Split: drag B onto the right edge of the content.
		STextBlock* TabB = FindText(*Window, "Tab B");
		REQUIRE(TabB);
		Drag(*TabB, FVector2(590.0f, 200.0f));
		const TSharedPtr<SDockTab> B = Tabs->FindExistingLiveTab("B");
		CHECK(B->GetParentStack() != Tabs->FindExistingLiveTab("A")->GetParentStack());
		CHECK(FUITestApp::RectOf(*B->GetParentStack()).Left > 250.0f);
		TArray<TSharedRef<SDockTabStack>> Stacks;
		Tabs->GetPrimaryArea()->GatherStacks(Stacks);
		CHECK(Stacks.size() == 2);

		// Tear off: drop B outside the window -> a floating window.
		T.Tick();
		TabB = FindText(*Window, "Tab B");
		REQUIRE(TabB);
		Drag(*TabB, FVector2(900.0f, 100.0f));
		CHECK(T.GetApp().GetWindows().size() == 2);
		CHECK(Tabs->GetAreas().size() == 2);
		const TSharedPtr<SWindow> Floating = T.GetApp().GetWindows()[1].ToSharedPtr();
		CHECK(Tabs->FindExistingLiveTab("B")->GetParentStack()->GetDockArea()->GetWindow() == Floating);
		CHECK(Floating->GetNativeWindow()->GetClientPosition().X > 600);
		Stacks.clear();
		Tabs->GetPrimaryArea()->GatherStacks(Stacks);
		CHECK(Stacks.size() == 1); // the emptied split went away

		// The layout remembers the floating window.
		const TSharedRef<FDockLayout> Persisted = Tabs->PersistLayout();
		CHECK(Persisted->Areas.size() == 2);
		CHECK(Persisted->Areas[1]->WindowPosition.has_value());

		// Dock it back: drag B from the floating window into the main stack's well.
		T.Tick();
		STextBlock* FloatingTab = FindText(*Floating, "Tab B");
		REQUIRE(FloatingTab);
		const FVector2 Start = FUITestApp::CenterOf(*FloatingTab);
		T.MouseMove(ToSharedRef(Floating), Start);
		T.MouseDown(ToSharedRef(Floating), Start);
		T.MouseMove(ToSharedRef(Floating), Start + FVector2(12.0f, 0.0f));
		T.Tick();
		REQUIRE(T.GetApp().IsDragDropping());
		// Positions are relative to the floating window (it holds the capture).
		const FVector2 Offset =
		    (Window->GetNativeWindow()->GetClientPosition() - Floating->GetNativeWindow()->GetClientPosition())
		        .ToVector2();
		const FVector2 WellPoint = FUITestApp::RectOf(*FindText(*Window, "Tab A")).GetCenter() + FVector2(40.0f, 0.0f);
		T.MouseMove(ToSharedRef(Floating), WellPoint + Offset);
		T.Tick();
		T.MouseUp(ToSharedRef(Floating), WellPoint + Offset);
		T.Tick();
		T.Tick();
		CHECK(Order() == TArray<FString>{"C", "A", "B"});
		CHECK(T.GetApp().GetWindows().size() == 1);
		CHECK(Tabs->GetAreas().size() == 1);
	}

	TEST_CASE("Docking: the Window menu opens and closes tabs")
	{
		FUITestApp T;
		const TSharedRef<FTabManager> Tabs = FTabManager::Create();
		Tabs->RegisterTabSpawner("One", [](const FSpawnTabArgs&) {
			return SNew(SDockTab)[SNew(SBox)];
		}).SetDisplayName("Panel one");
		Tabs->RegisterTabSpawner("Two", [](const FSpawnTabArgs&) {
			return SNew(SDockTab)[SNew(SBox)];
		}).SetDisplayName("Panel two");
		const TSharedRef<SWindow> Window = SNew(SWindow);
		T.GetApp().AddWindow(Window);
		Window->SetContent(Tabs->RestoreFrom(FDockLayout::New("W", 1)->AddArea(FDockLayout::NewPrimaryArea()->Split(
		                                         FDockLayout::NewStack()->AddTab("One"))),
		    Window.ToSharedPtr()));
		FMenuSection Section;
		Tabs->PopulateTabSpawnerMenu(Section);
		REQUIRE(Section.Entries.size() == 2);
		const FMenuContext Context;
		CHECK(Section.Entries[0].GetCheckState(Context) == ECheckBoxState::Checked);
		CHECK(Section.Entries[1].GetCheckState(Context) == ECheckBoxState::Unchecked);
		CHECK(Section.Entries[1].GetLabel() == "Panel two");
		Section.Entries[1].Run(Context);
		CHECK(Tabs->FindExistingLiveTab("Two") != nullptr);
		CHECK(Tabs->FindExistingLiveTab("Two")->GetLabel() == "Panel two"); // the spawner's name when the tab has none
		Section.Entries[0].Run(Context);
		CHECK(!Tabs->FindExistingLiveTab("One"));
	}

	// --- Property grid ---------------------------------------------------------------

	TEST_CASE("Property grid: shared properties, multiple values, editing all, transactions")
	{
		FUITestApp T;
		std::shared_ptr<FTestSource> A = std::make_shared<FTestSource>("light_01", true);
		std::shared_ptr<FTestSource> B = std::make_shared<FTestSource>("light_02");
		B->Values[FName("Brightness")] = 3.0f;
		TArray<FString> Transactions;
		int32 Open = 0;
		TSharedPtr<SPropertyGrid> Grid;
		T.AddWindow(SAssignNew(Grid, SPropertyGrid)
		                .OnBeginTransaction(
		                    [&](const FString& Description)
		{
			Transactions.push_back(Description);
			++Open;
		}).OnEndTransaction([&] { --Open; }),
		    FVector2(420.0f, 600.0f));

		Grid->SetObjects({TSharedRef<IPropertySource>(A), TSharedRef<IPropertySource>(B)});
		T.Tick();
		CHECK(Grid->GetSharedProperties().size() == 6); // "Only" is not shared
		CHECK(FindText(*Grid, "2 objects") != nullptr);
		CHECK(!Grid->GetSharedValue("Brightness")); // differs
		CHECK(Grid->GetSharedValue("Target"));

		// The brightness spin box shows "Multiple Values"; typing sets both.
		TArray<SSpinBox<float>*> Floats;
		FindWidgets(*Grid, Floats);
		REQUIRE(Floats.size() >= 1);
		SSpinBox<float>* Brightness = Floats[0];
		CHECK(Brightness->FormatValue() == "Multiple Values");
		T.ClickWidget(*Brightness);
		CHECK(Brightness->GetEditableText()->HasKeyboardFocus());
		T.Type("5");
		T.Key(EKey::Enter);
		CHECK(std::get<float>(A->Values[FName("Brightness")]) == 5.0f);
		CHECK(std::get<float>(B->Values[FName("Brightness")]) == 5.0f);
		CHECK(Transactions.size() == 1);
		CHECK(Transactions[0] == "Change Brightness");
		CHECK(Open == 0);

		// A drag is one transaction, however many changes it makes.
		const FVector2 Center = FUITestApp::CenterOf(*Brightness);
		const TSharedRef<SWindow> Window = T.GetApp().GetWindows()[0];
		T.GetApp().ClearKeyboardFocus();
		T.MouseMove(Window, Center);
		T.MouseDown(Window, Center);
		for (int32 I = 1; I <= 5; ++I)
		{
			T.MouseMove(Window, Center + FVector2(10.0f * I, 0.0f));
			T.Tick();
		}
		CHECK(Open == 1);
		T.MouseUp(Window, Center + FVector2(50.0f, 0.0f));
		T.Tick();
		CHECK(Open == 0);
		CHECK(Transactions.size() == 2);
		CHECK(std::get<float>(A->Values[FName("Brightness")]) > 5.0f);
		CHECK(std::get<float>(A->Values[FName("Brightness")]) <= 10.0f); // clamped to the range

		// A vector component sets just that component on each object.
		A->Values[FName("Origin")] = FPropertyVector3{1.0f, 2.0f, 3.0f};
		Grid->ModifyPropertyValue("Origin", [](const FPropertyValue& Old)
		{
			FPropertyVector3 V = std::get<FPropertyVector3>(Old);
			V.Z = 9.0f;
			return FPropertyValue(V);
		});
		CHECK(std::get<FPropertyVector3>(A->Values[FName("Origin")]) == FPropertyVector3{1.0f, 2.0f, 9.0f});
		CHECK(std::get<FPropertyVector3>(B->Values[FName("Origin")]) == FPropertyVector3{0.0f, 0.0f, 9.0f});

		// Categories collapse; the search filters rows.
		CHECK(FindText(*Grid, "Origin") != nullptr);
		Grid->SetCategoryExpanded("Transform", false);
		T.Tick();
		CHECK(FindText(*Grid, "Origin") == nullptr);
		Grid->SetFilterText("bright");
		T.Tick();
		CHECK(FindText(*Grid, "Brightness") != nullptr);
		CHECK(FindText(*Grid, "Visible") == nullptr);

		// A single object shows everything, including "Only".
		Grid->SetFilterText("");
		Grid->SetObjects({TSharedRef<IPropertySource>(A)});
		T.Tick();
		CHECK(Grid->GetSharedProperties().size() == 7);
		CHECK(FindText(*Grid, "light_01") != nullptr);
	}

	TEST_CASE("Property grid: check box, enum combo box and the color picker edit values")
	{
		FUITestApp T;
		std::shared_ptr<FTestSource> A = std::make_shared<FTestSource>("a");
		int32 Transactions = 0;
		TSharedPtr<SPropertyGrid> Grid;
		T.AddWindow(SAssignNew(Grid, SPropertyGrid).OnBeginTransaction([&](const FString&) { ++Transactions; }),
		    FVector2(420.0f, 600.0f));
		Grid->SetObjects({TSharedRef<IPropertySource>(A)});
		T.Tick();

		TArray<SCheckBox*> Checks;
		FindWidgets(*Grid, Checks);
		REQUIRE(Checks.size() == 1);
		T.ClickWidget(*Checks[0]);
		CHECK(std::get<bool>(A->Values[FName("Visible")]) == false);

		TArray<SComboBox<int32>*> Combos;
		FindWidgets(*Grid, Combos);
		REQUIRE(Combos.size() == 1);
		CHECK(FindText(*Combos[0], "Steady") != nullptr);
		T.ClickWidget(*Combos[0]);
		REQUIRE(Combos[0]->IsOpen());
		T.Tick();
		const TSharedPtr<SListView<int32>> List = Combos[0]->GetList();
		REQUIRE(List);
		const TSharedPtr<ITableRow> Pulse = List->WidgetFromItem(2);
		REQUIRE(Pulse);
		T.ClickWidget(*Pulse->AsWidget());
		CHECK(std::get<int32>(A->Values[FName("Style")]) == 2);
		CHECK(!Combos[0]->IsOpen());

		// The color swatch opens a picker; its hex field sets the color.
		TArray<SColorBlock*> Swatches;
		FindWidgets(*Grid, Swatches);
		REQUIRE(Swatches.size() == 1);
		T.ClickWidget(*Swatches[0]);
		REQUIRE(T.GetApp().GetMenuCount() == 1);
		TArray<SColorPicker*> Pickers;
		FindWidgets(*T.GetApp().GetWindows()[0], Pickers);
		REQUIRE(Pickers.size() == 1);
		TArray<SEditableText*> Fields;
		FindWidgets(*Pickers[0], Fields);
		SEditableText* Hex = Fields.back();
		CHECK(Hex->GetText() == "FFC864FF");
		T.GetApp().SetKeyboardFocus(StaticCastSharedPtr<SWidget>(Hex->shared_from_this()));
		T.Key(EKey::A, false, true);
		T.Type("336699");
		T.Key(EKey::Enter);
		CHECK(std::get<FColor>(A->Values[FName("Color")]) == FColor(0x33, 0x66, 0x99, 255));
		CHECK(Transactions == 3);
	}

	// --- Value editors ------------------------------------------------------------------

	TEST_CASE("Spin box: drag changes the value, a click types, clamps and rounds")
	{
		FUITestApp T;
		int32 Value = 10;
		TArray<int32> Committed;
		TSharedPtr<SSpinBox<int32>> Spin;
		const TSharedRef<SWindow> Window = T.AddWindow(SNew(SBox)
		        .HAlign(HAlign_Left)
		        .VAlign(VAlign_Top)[SAssignNew(Spin, SSpinBox<int32>)
		                .Value([&Value] { return TOptional<int32>(Value); })
		                .MinValue(TOptional<int32>(0))
		                .MaxValue(TOptional<int32>(20))
		                .Delta(0.5f)
		                .OnValueChanged([&Value](int32 V)
		{ Value = V; }).OnValueCommitted([&Committed](int32 V, ETextCommit) { Committed.push_back(V); })]);
		const FVector2 Center = FUITestApp::CenterOf(*Spin);
		T.MouseMove(Window, Center);
		T.MouseDown(Window, Center);
		T.MouseMove(Window, Center + FVector2(20.0f, 0.0f));
		T.Tick();
		CHECK(Spin->IsDragging());
		T.MouseMove(Window, Center + FVector2(60.0f, 0.0f));
		T.MouseUp(Window, Center + FVector2(60.0f, 0.0f));
		T.Tick();
		CHECK(Value == 20); // 10 + ~0.5 * 57, clamped
		CHECK(Committed == TArray<int32>{20});

		T.ClickWidget(*Spin);
		REQUIRE(Spin->GetEditableText()->HasKeyboardFocus());
		T.Type("7");
		T.Key(EKey::Enter);
		CHECK(Value == 7);
		T.Key(EKey::A, false, true);
		T.Type("-4");
		T.Key(EKey::Enter);
		CHECK(Value == 0);
		CHECK(SSpinBox<float>::ParseValue("2.5") == 2.5f);
		CHECK(!SSpinBox<float>::ParseValue("abc"));
	}

	TEST_CASE("Slider: click, drag and arrow keys")
	{
		FUITestApp T;
		float Value = 0.0f;
		int32 Begins = 0, Ends = 0;
		TSharedPtr<SSlider> Slider;
		const TSharedRef<SWindow> Window = T.AddWindow(SNew(SVerticalBox) +
		        SVerticalBox::Slot().AutoHeight()[SAssignNew(Slider, SSlider)
		                .MinValue(0.0f)
		                .MaxValue(100.0f)
		                .StepSize(5.0f)
		                .Value([&Value] { return Value; })
		                .OnValueChanged([&Value](float V) { Value = V; })
		                .OnMouseCaptureBegin([&Begins]
		{ ++Begins; }).OnMouseCaptureEnd([&Ends] { ++Ends; })],
		    FVector2(214.0f, 100.0f));
		const FRect R = FUITestApp::RectOf(*Slider);
		T.Click(Window, FVector2(R.GetCenter().X, R.GetCenter().Y));
		CHECK(Value == doctest::Approx(50.0f).epsilon(0.02));
		CHECK(Begins == 1);
		CHECK(Ends == 1);
		T.Key(EKey::Right);
		CHECK(Value == doctest::Approx(55.0f).epsilon(0.02));
		CHECK(Slider->HasKeyboardFocus()); // Right adjusted instead of navigating
	}

	TEST_CASE("Combo box: keyboard picks with arrows and Enter")
	{
		FUITestApp T;
		TArray<FString> Options{"Small", "Medium", "Large"};
		TArray<FString> Picked;
		TSharedPtr<SComboBox<FString>> Combo;
		T.AddWindow(SNew(SBox)
		        .HAlign(HAlign_Left)
		        .VAlign(VAlign_Top)[SAssignNew(Combo, SComboBox<FString>)
		                .OptionsSource(&Options)
		                .InitiallySelectedItem(TOptional<FString>(FString("Small")))
		                .OnGenerateWidget([](FString S) -> TSharedRef<SWidget> {
			return SNew(STextBlock).Text(S);
		}).OnSelectionChanged([&Picked](FString S, ESelectInfo) {
			Picked.push_back(S);
		})[SNew(STextBlock).Text([&Combo] {
			return Combo && Combo->GetSelectedItem() ? *Combo->GetSelectedItem() : FString();
		})]]);
		T.ClickWidget(*Combo);
		T.Tick();
		REQUIRE(Combo->IsOpen());
		CHECK(Combo->GetList()->HasKeyboardFocus());
		T.Key(EKey::Down);
		T.Key(EKey::Down);
		T.Key(EKey::Enter);
		CHECK(!Combo->IsOpen());
		CHECK(Picked == TArray<FString>{"Large"});
		CHECK(*Combo->GetSelectedItem() == "Large");
	}

	// --- Viewport --------------------------------------------------------------------

	TEST_CASE("Viewport: resizes its client, forwards input and focus, raw mouse while looking")
	{
		FUITestApp T(1.5f);
		std::shared_ptr<FTestViewportClient> Client = std::make_shared<FTestViewportClient>();
		TSharedPtr<SViewport> Viewport;
		const TSharedRef<SWindow> Window =
		    T.AddWindow(SAssignNew(Viewport, SViewport).ViewportClient(Client), FVector2(200.0f, 100.0f));
		REQUIRE(!Client->Sizes.empty());
		CHECK(Client->Sizes.back() == FIntPoint(300, 150)); // pixels at 150%

		T.Click(Window, FVector2(50.0f, 50.0f));
		CHECK(Viewport->HasKeyboardFocus());
		T.Key(EKey::W);
		CHECK(Client->Log == TArray<FString>{"down", "up", "key W"});

		// Right button: high-precision mouse (raw input, hidden cursor).
		FNullPlatformApplication& Platform = T.GetPlatform();
		Platform.GetCursor()->SetPosition(123, 45);
		T.MouseDown(Window, FVector2(50.0f, 50.0f), EMouseButton::Right);
		T.Tick();
		CHECK(Platform.IsRawMouseEnabled());
		CHECK(Viewport->HasMouseCapture());
		Platform.QueueRawMouseMove(5, -3);
		Platform.QueueRawMouseMove(2, 1);
		T.Tick();
		CHECK(Client->RawTotal == FVector2(7.0f, -2.0f));
		T.MouseUp(Window, FVector2(50.0f, 50.0f), EMouseButton::Right);
		T.Tick();
		CHECK(!Platform.IsRawMouseEnabled());
		CHECK(Platform.GetCursor()->GetPosition() == FVector2(123.0f, 45.0f)); // put back
	}
}
