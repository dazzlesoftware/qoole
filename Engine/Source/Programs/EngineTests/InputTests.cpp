// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Headless input tests: routing, replies, focus, navigation, capture,
// drag and drop, tooltips, menus and modal windows.

#include "UITestHelpers.h"
#include "Templates/SharedPointer.h"

#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SOverlay.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	// Records the events it gets; optionally handles them.
	class SRecorder : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SRecorder)
		SLATE_ARGUMENT(FString, Name, "")
		SLATE_ARGUMENT(bool, HandleDown, false)
		SLATE_ARGUMENT(bool, HandlePreview, false)
		SLATE_ARGUMENT(bool, Focusable, false)
		SLATE_ARGUMENT(TArray<FString>*, Log, nullptr)
		SLATE_DEFAULT_SLOT(Content)
		SLATE_END_ARGS()

		void Construct(const FArguments& Args)
		{
			Name = Args._Name;
			bHandleDown = Args._HandleDown;
			bHandlePreview = Args._HandlePreview;
			bFocusable = Args._Focusable;
			Log = Args._Log;
			if (Args._Content.Widget)
			{
				ChildSlot[ToSharedRef(Args._Content.Widget)];
			}
		}

		bool SupportsKeyboardFocus() const override
		{
			return bFocusable;
		}
		FReply OnPreviewMouseButtonDown(const FGeometry&, const FPointerEvent&) override
		{
			Add("preview");
			return bHandlePreview ? FReply::Handled() : FReply::Unhandled();
		}
		FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override
		{
			Add("down");
			return bHandleDown ? FReply::Handled() : FReply::Unhandled();
		}
		void OnMouseEnter(const FGeometry&, const FPointerEvent&) override
		{
			Add("enter");
		}
		void OnMouseLeave(const FPointerEvent&) override
		{
			Add("leave");
		}
		FReply OnKeyDown(const FGeometry&, const FKeyEvent& Event) override
		{
			Add(FString::Format("key {}", ToString(Event.GetKey())));
			return FReply::Unhandled();
		}
		FReply OnFocusReceived(const FGeometry&, const FFocusEvent& Event) override
		{
			Add(FString::Format("focus {}", ToString(Event.GetCause())));
			return FReply::Handled();
		}
		void OnFocusLost(const FFocusEvent&) override
		{
			Add("blur");
		}

		void Add(const FString& What)
		{
			if (Log)
			{
				Log->push_back(Name + " " + What);
			}
		}

	protected:
		FVector2 ComputeDesiredSize(float Scale) const override
		{
			return FVector2::Max(SCompoundWidget::ComputeDesiredSize(Scale), FVector2(20.0f, 20.0f));
		}

	private:
		FString Name;
		bool bHandleDown = false;
		bool bHandlePreview = false;
		bool bFocusable = false;
		TArray<FString>* Log = nullptr;
	};

	class FTestDragDrop final : public FDragDropOperation
	{
	public:
		DRAG_DROP_OPERATOR_TYPE(FTestDragDrop, FDragDropOperation)
		FString Payload;
		bool bDropped = false;
		bool bDropHandled = false;
		void OnDrop(bool bHandled, const FPointerEvent&) override
		{
			bDropped = true;
			bDropHandled = bHandled;
		}
		TSharedPtr<SWidget> GetDefaultDecorator() const override
		{
			return SNew(STextBlock).Text(Payload);
		}
	};

	class SDragSource final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SDragSource)
		SLATE_END_ARGS()
		void Construct(const FArguments&) {}

		TSharedPtr<FTestDragDrop> Operation;

		FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event) override
		{
			return FReply::Handled().DetectDrag(SharedThis(this), Event.GetEffectingButton());
		}
		FReply OnDragDetected(const FGeometry&, const FPointerEvent&) override
		{
			Operation = std::make_shared<FTestDragDrop>();
			Operation->Payload = "payload";
			return FReply::Handled().BeginDragDrop(ToSharedRef(StaticCastSharedPtr<FDragDropOperation>(Operation)));
		}

	protected:
		FVector2 ComputeDesiredSize(float) const override
		{
			return FVector2(40.0f, 40.0f);
		}
		int32 OnPaint(const FPaintArgs&, const FGeometry&, const FRect&, FSlateWindowElementList&, int32 LayerId,
		    const FWidgetStyle&) const override
		{
			return LayerId;
		}
	};

	class SDropTarget final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SDropTarget)
		SLATE_END_ARGS()
		void Construct(const FArguments&) {}

		TArray<FString> Log;

		void OnDragEnter(const FGeometry&, const FDragDropEvent& Event) override
		{
			Log.push_back(Event.GetOperationAs<FTestDragDrop>() ? "enter" : "enter?");
		}
		void OnDragLeave(const FDragDropEvent&) override
		{
			Log.push_back("leave");
		}
		FReply OnDragOver(const FGeometry&, const FDragDropEvent&) override
		{
			++Overs;
			return FReply::Handled();
		}
		FReply OnDrop(const FGeometry&, const FDragDropEvent& Event) override
		{
			const TSharedPtr<FTestDragDrop> Op = Event.GetOperationAs<FTestDragDrop>();
			Log.push_back(Op ? "drop " + Op->Payload : FString("drop?"));
			return FReply::Handled();
		}
		int32 Overs = 0;

	protected:
		FVector2 ComputeDesiredSize(float) const override
		{
			return FVector2(40.0f, 40.0f);
		}
		int32 OnPaint(const FPaintArgs&, const FGeometry&, const FRect&, FSlateWindowElementList&, int32 LayerId,
		    const FWidgetStyle&) const override
		{
			return LayerId;
		}
	};

	TSharedRef<SButton> MakeButton(const char* Label, int32* Counter)
	{
		return SNew(SButton).Text(Label).OnClicked([Counter]()
		{
			++*Counter;
			return FReply::Handled();
		});
	}
} // namespace

TEST_SUITE("UIInput")
{
	TEST_CASE("Pointer events tunnel root to leaf, then bubble leaf to root until handled")
	{
		FUITestApp T;
		TArray<FString> Log;
		T.AddWindow(SNew(SRecorder).Name("outer").Log(&Log).HandleDown(true)[SNew(SRecorder).Name("inner").Log(&Log)]);
		Log.clear();
		T.MouseDown(T.GetApp().GetWindows()[0], FVector2(5.0f, 5.0f));
		T.Tick();
		REQUIRE(Log.size() == 4);
		CHECK(Log[0] == "outer preview");
		CHECK(Log[1] == "inner preview");
		CHECK(Log[2] == "inner down");
		CHECK(Log[3] == "outer down");
	}

	TEST_CASE("A handled preview stops the event")
	{
		FUITestApp T;
		TArray<FString> Log;
		T.AddWindow(
		    SNew(SRecorder).Name("outer").Log(&Log).HandlePreview(true)[SNew(SRecorder).Name("inner").Log(&Log)]);
		Log.clear();
		T.MouseDown(T.GetApp().GetWindows()[0], FVector2(5.0f, 5.0f));
		T.Tick();
		REQUIRE(Log.size() == 1);
		CHECK(Log[0] == "outer preview");
	}

	TEST_CASE("Hover: enter and leave come from diffing pointer paths")
	{
		FUITestApp T;
		TArray<FString> Log;
		TSharedPtr<SRecorder> A, B;
		const TSharedRef<SWindow> Window = T.AddWindow(SNew(SHorizontalBox) +
		        SHorizontalBox::Slot().FillWidth(1.0f)[SAssignNew(A, SRecorder).Name("A").Log(&Log)] +
		        SHorizontalBox::Slot().FillWidth(1.0f)[SAssignNew(B, SRecorder).Name("B").Log(&Log)],
		    FVector2(200.0f, 50.0f));
		T.MouseMove(Window, FVector2(50.0f, 25.0f));
		T.Tick();
		CHECK(A->IsHovered());
		CHECK(Log == TArray<FString>{"A enter"});
		T.MouseMove(Window, FVector2(150.0f, 25.0f));
		T.Tick();
		CHECK(!A->IsHovered());
		CHECK(B->IsHovered());
		CHECK(Log == TArray<FString>{"A enter", "A leave", "B enter"});
		T.GetPlatform().QueueMouseMove(ToSharedRef(Window->GetNativeWindow()), FVector2(150.0f, 25.0f)); // no change
		T.Tick();
		CHECK(Log.size() == 3);
	}

	TEST_CASE("Button: clicks on release inside, captures while pressed, not when released outside")
	{
		FUITestApp T;
		int32 Clicks = 0;
		TSharedPtr<SButton> Button;
		const TSharedRef<SWindow> Window = T.AddWindow(SNew(SBox)
		        .HAlign(HAlign_Left)
		        .VAlign(VAlign_Top)[SAssignNew(Button, SButton)
		                .Text("OK")
		                .OnClicked([&]
		{
			++Clicks;
			return FReply::Handled();
		})]);
		const FVector2 Center = FUITestApp::CenterOf(*Button);

		T.Click(Window, Center);
		CHECK(Clicks == 1);
		CHECK(Button->HasKeyboardFocus()); // a click focuses the button
		CHECK(T.GetApp().GetFocusCause() == EFocusCause::Mouse);

		T.MouseMove(Window, Center);
		T.MouseDown(Window, Center);
		T.Tick();
		CHECK(Button->IsPressed());
		CHECK(Button->HasMouseCapture());
		T.MouseMove(Window, FVector2(390.0f, 290.0f));
		T.MouseUp(Window, FVector2(390.0f, 290.0f));
		T.Tick();
		CHECK(Clicks == 1);
		CHECK(!Button->IsPressed());
		CHECK(!Button->HasMouseCapture());
	}

	TEST_CASE("Disabled widgets receive no clicks")
	{
		FUITestApp T;
		int32 Clicks = 0;
		TSharedPtr<SButton> Button;
		T.AddWindow(SNew(SBox)
		        .HAlign(HAlign_Left)
		        .VAlign(VAlign_Top)[SAssignNew(Button, SButton)
		                .Text("Nope")
		                .IsEnabled(false)
		                .OnClicked([&]
		{
			++Clicks;
			return FReply::Handled();
		})]);
		T.ClickWidget(*Button);
		CHECK(Clicks == 0);
		CHECK(!Button->HasKeyboardFocus());
	}

	TEST_CASE("Keyboard: Tab order, Shift+Tab, wrap, and Enter activates the focused button")
	{
		FUITestApp T;
		int32 Clicks[3] = {};
		TSharedPtr<SButton> B[3];
		T.AddWindow(SNew(SVerticalBox) +
		    SVerticalBox::Slot().AutoHeight()[SAssignNew(B[0], SButton)
		            .Text("One")
		            .OnClicked(
		                [&]
		{
			++Clicks[0];
			return FReply::Handled();
		})] +
		    SVerticalBox::Slot().AutoHeight()[SAssignNew(B[1], SButton)
		            .Text("Two")
		            .OnClicked(
		                [&]
		{
			++Clicks[1];
			return FReply::Handled();
		})] +
		    SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text("not focusable")] +
		    SVerticalBox::Slot().AutoHeight()[SAssignNew(B[2], SButton)
		            .Text("Three")
		            .OnClicked([&]
		{
			++Clicks[2];
			return FReply::Handled();
		})]);

		T.Key(EKey::Tab);
		CHECK(B[0]->HasKeyboardFocus());
		CHECK(T.GetApp().GetFocusCause() == EFocusCause::Navigation);
		T.Key(EKey::Tab);
		CHECK(B[1]->HasKeyboardFocus());
		T.Key(EKey::Tab);
		CHECK(B[2]->HasKeyboardFocus());
		T.Key(EKey::Tab);
		CHECK(B[0]->HasKeyboardFocus()); // wraps
		T.Key(EKey::Tab, true);
		CHECK(B[2]->HasKeyboardFocus());

		T.Key(EKey::Enter);
		CHECK(Clicks[2] == 1);
		T.Key(EKey::SpaceBar);
		CHECK(Clicks[2] == 2);
		CHECK(Clicks[0] == 0);
	}

	TEST_CASE("Directional navigation picks the nearest widget in that direction")
	{
		FUITestApp T;
		int32 Unused = 0;
		TSharedPtr<SButton> Cells[3][3];
		TSharedRef<SGridPanel> Grid = SNew(SGridPanel);
		for (int32 R = 0; R < 3; ++R)
		{
			for (int32 C = 0; C < 3; ++C)
			{
				Grid->AddSlot(C, R).Padding(2.0f)[SAssignNew(Cells[R][C], SButton)
				        .Text(FString::Format("{}{}", R, C))
				        .OnClicked([&]
				{
					++Unused;
					return FReply::Handled();
				})];
			}
		}
		T.AddWindow(Grid);
		T.GetApp().SetKeyboardFocus(Cells[1][1]);
		T.Key(EKey::Right);
		CHECK(Cells[1][2]->HasKeyboardFocus());
		T.Key(EKey::Down);
		CHECK(Cells[2][2]->HasKeyboardFocus());
		T.Key(EKey::Left);
		T.Key(EKey::Left);
		CHECK(Cells[2][0]->HasKeyboardFocus());
		T.Key(EKey::Left); // nothing further left: focus stays
		CHECK(Cells[2][0]->HasKeyboardFocus());
		T.Key(EKey::Up);
		T.Key(EKey::Up);
		CHECK(Cells[0][0]->HasKeyboardFocus());
	}

	TEST_CASE("Gamepad: D-pad navigates and A activates")
	{
		FUITestApp T;
		int32 Clicks = 0;
		TSharedPtr<SButton> A, B;
		T.AddWindow(SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[SAssignNew(A, SButton).Text("A")] +
		    SVerticalBox::Slot().AutoHeight()[SAssignNew(B, SButton)
		            .Text("B")
		            .OnClicked([&]
		{
			++Clicks;
			return FReply::Handled();
		})]);
		T.GetApp().SetKeyboardFocus(A);
		T.GetApp().OnGamepadButtonDown(0, EGamepadButton::DPadDown, false);
		CHECK(B->HasKeyboardFocus());
		T.GetApp().OnGamepadButtonDown(0, EGamepadButton::FaceBottom, false);
		T.GetApp().OnGamepadButtonUp(0, EGamepadButton::FaceBottom);
		CHECK(Clicks == 1);

		// A held stick navigates once, then repeats after the delay.
		T.GetApp().OnGamepadAnalog(0, EGamepadAxis::LeftY, 1.0f);
		CHECK(A->HasKeyboardFocus());
		T.GetApp().OnGamepadAnalog(0, EGamepadAxis::LeftY, 0.0f);
	}

	TEST_CASE("Focus events: lost, received and focus-changing along both paths")
	{
		FUITestApp T;
		TArray<FString> Log;
		TSharedPtr<SRecorder> A, B;
		T.AddWindow(SNew(SVerticalBox) +
		    SVerticalBox::Slot()[SAssignNew(A, SRecorder).Name("A").Log(&Log).Focusable(true)] +
		    SVerticalBox::Slot()[SAssignNew(B, SRecorder).Name("B").Log(&Log).Focusable(true)]);
		T.GetApp().SetKeyboardFocus(A);
		T.GetApp().SetKeyboardFocus(B, EFocusCause::Navigation);
		CHECK(Log == TArray<FString>{"A focus SetDirectly", "A blur", "B focus Navigation"});
		CHECK(T.GetApp().HasFocusedDescendants(T.GetApp().GetWindows()[0].ToSharedPtr().get()));

		// Hiding the focused widget moves focus away at the next frame.
		B->SetVisibility(EVisibility::Collapsed);
		T.Tick();
		CHECK(!T.GetApp().GetKeyboardFocusedWidget());
		CHECK(Log.back() == "B blur");
	}

	TEST_CASE("Clicking empty space clears focus; clicking a non-focusable handler keeps it")
	{
		FUITestApp T;
		TArray<FString> Log;
		TSharedPtr<SButton> Button;
		TSharedPtr<SRecorder> Handler;
		const TSharedRef<SWindow> Window =
		    T.AddWindow(SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[SAssignNew(Button, SButton).Text("B")] +
		        SVerticalBox::Slot().AutoHeight()[SAssignNew(Handler, SRecorder).Name("H").Log(&Log).HandleDown(true)] +
		        SVerticalBox::Slot().FillHeight(1.0f)[SNew(SBox)]);
		T.GetApp().SetKeyboardFocus(Button);
		T.ClickWidget(*Handler);
		CHECK(Button->HasKeyboardFocus());
		T.Click(Window, FVector2(200.0f, 280.0f));
		CHECK(!T.GetApp().GetKeyboardFocusedWidget());
	}

	TEST_CASE("Check box toggles by click and by Space")
	{
		FUITestApp T;
		TArray<ECheckBoxState> Changes;
		TSharedPtr<SCheckBox> Check;
		T.AddWindow(SNew(SBox)
		        .HAlign(HAlign_Left)
		        .VAlign(
		            VAlign_Top)[SAssignNew(Check, SCheckBox).Text("Option").OnCheckStateChanged([&](ECheckBoxState S) {
			Changes.push_back(S);
		})]);
		CHECK(!Check->IsChecked());
		T.ClickWidget(*Check);
		CHECK(Check->IsChecked());
		T.Key(EKey::SpaceBar);
		CHECK(!Check->IsChecked());
		CHECK(Changes == TArray<ECheckBoxState>{ECheckBoxState::Checked, ECheckBoxState::Unchecked});
	}

	TEST_CASE("Drag and drop: detect, enter, over, drop with payload, decorator follows")
	{
		FUITestApp T;
		TSharedPtr<SDragSource> Source;
		TSharedPtr<SDropTarget> Target;
		const TSharedRef<SWindow> Window =
		    T.AddWindow(SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth()[SAssignNew(Source, SDragSource)] +
		        SHorizontalBox::Slot().AutoWidth().Padding(
		            FMargin(100.0f, 0.0f, 0.0f, 0.0f))[SAssignNew(Target, SDropTarget)]);

		T.MouseMove(Window, FVector2(20.0f, 20.0f));
		T.MouseDown(Window, FVector2(20.0f, 20.0f));
		T.MouseMove(Window, FVector2(22.0f, 20.0f)); // under the drag threshold
		T.Tick();
		CHECK(!T.GetApp().IsDragDropping());
		T.MouseMove(Window, FVector2(40.0f, 20.0f));
		T.Tick();
		REQUIRE(T.GetApp().IsDragDropping());
		CHECK(Window->NumPopups() == 1); // the decorator

		T.MouseMove(Window, FVector2(160.0f, 20.0f));
		T.Tick();
		CHECK(Target->Log == TArray<FString>{"enter"});
		CHECK(Target->Overs >= 1);

		T.MouseUp(Window, FVector2(160.0f, 20.0f));
		T.Tick();
		CHECK(!T.GetApp().IsDragDropping());
		CHECK(Target->Log == TArray<FString>{"enter", "drop payload", "leave"});
		CHECK(Source->Operation->bDropped);
		CHECK(Source->Operation->bDropHandled);
		CHECK(Window->NumPopups() == 0);
	}

	TEST_CASE("Drag and drop: Escape cancels")
	{
		FUITestApp T;
		TSharedPtr<SDragSource> Source;
		const TSharedRef<SWindow> Window =
		    T.AddWindow(SNew(SBox).HAlign(HAlign_Left).VAlign(VAlign_Top)[SAssignNew(Source, SDragSource)]);
		T.MouseMove(Window, FVector2(20.0f, 20.0f));
		T.MouseDown(Window, FVector2(20.0f, 20.0f));
		T.MouseMove(Window, FVector2(60.0f, 60.0f));
		T.Tick();
		REQUIRE(T.GetApp().IsDragDropping());
		T.Key(EKey::Escape);
		CHECK(!T.GetApp().IsDragDropping());
		CHECK(Source->Operation->bDropped);
		CHECK(!Source->Operation->bDropHandled);
	}

	TEST_CASE("Tooltips open after the delay, close on click, and stay closed until the pointer leaves")
	{
		FUITestApp T;
		TSharedPtr<SButton> Button;
		const TSharedRef<SWindow> Window = T.AddWindow(SNew(SBox)
		        .HAlign(HAlign_Left)
		        .VAlign(VAlign_Top)[SAssignNew(Button, SButton).Text("Hover me").ToolTipText("Helpful text")]);
		T.HoverWidget(*Button);
		CHECK(!T.GetApp().GetActiveToolTip());
		T.Wait(0.3f);
		CHECK(!T.GetApp().GetActiveToolTip());
		T.Wait(0.3f);
		REQUIRE(T.GetApp().GetActiveToolTip());
		CHECK(Window->NumPopups() == 1);
		// Tooltips never take the pointer.
		CHECK(&Window->HitTestAt(FUITestApp::CenterOf(*Button) * Window->GetScale()).back().Get() !=
		    &T.GetApp().GetActiveToolTip()->AsWidget().Get());

		T.ClickWidget(*Button);
		CHECK(!T.GetApp().GetActiveToolTip());
		T.Wait(1.0f);
		CHECK(!T.GetApp().GetActiveToolTip());
		T.MouseMove(Window, FVector2(390.0f, 290.0f));
		T.Tick();
		T.HoverWidget(*Button);
		T.Wait(0.6f);
		CHECK(T.GetApp().GetActiveToolTip());
	}

	TEST_CASE("Menus: open from a combo button, click an entry, click outside, Escape")
	{
		FUITestApp T;
		TArray<FString> Picked;
		TSharedPtr<SComboButton> Combo;
		TSharedPtr<SMenuEntry> First;
		const TSharedRef<SWindow> Window = T.AddWindow(SNew(SBox)
		        .HAlign(HAlign_Left)
		        .VAlign(VAlign_Top)
		        .Padding(FMargin(10.0f))[SAssignNew(Combo, SComboButton)
		                .Text("Pick")
		                .OnGetMenuContent([&]() -> TSharedRef<SWidget>
		{
			return SNew(SVerticalBox) +
			    SVerticalBox::Slot().AutoHeight()
			        [SAssignNew(First, SMenuEntry).Text("First").OnSelected([&] { Picked.push_back("First"); })] +
			    SVerticalBox::Slot()
			        .AutoHeight()[SNew(SMenuEntry).Text("Second").OnSelected([&] { Picked.push_back("Second"); })];
		})]);

		T.ClickWidget(*Combo);
		REQUIRE(Combo->IsOpen());
		CHECK(T.GetApp().GetMenuCount() == 1);
		CHECK(Window->NumPopups() == 1);
		// It opens below the button.
		CHECK(FUITestApp::RectOf(*First).Top >= FUITestApp::RectOf(*Combo).Bottom - 0.01f);

		T.ClickWidget(*First);
		CHECK(Picked == TArray<FString>{"First"});
		CHECK(!Combo->IsOpen());
		CHECK(Window->NumPopups() == 0);

		// Clicking outside dismisses.
		T.ClickWidget(*Combo);
		REQUIRE(Combo->IsOpen());
		T.Click(Window, FVector2(390.0f, 290.0f));
		CHECK(!Combo->IsOpen());

		// Clicking the button again toggles it closed (and does not reopen it).
		T.ClickWidget(*Combo);
		REQUIRE(Combo->IsOpen());
		T.ClickWidget(*Combo);
		CHECK(!Combo->IsOpen());

		// Escape closes it; keyboard navigation reaches the entries.
		T.ClickWidget(*Combo);
		REQUIRE(Combo->IsOpen());
		T.Key(EKey::Down);
		CHECK(First->HasKeyboardFocus());
		T.Key(EKey::Escape);
		CHECK(!Combo->IsOpen());
		CHECK(Picked.size() == 1);
	}

	TEST_CASE("Menus: submenus open on hover and close with their parent")
	{
		FUITestApp T;
		TSharedPtr<SMenuEntry> More, Plain, Deep;
		int32 DeepPicked = 0;
		TSharedPtr<SComboButton> Combo;
		T.AddWindow(SNew(SBox)
		                .HAlign(HAlign_Left)
		                .VAlign(VAlign_Top)[SAssignNew(Combo, SComboButton)
		                        .Text("Menu")
		                        .MenuContent(SNew(SVerticalBox) +
		                            SVerticalBox::Slot().AutoHeight()[SAssignNew(Plain, SMenuEntry).Text("Plain")] +
		                            SVerticalBox::Slot().AutoHeight()[SAssignNew(More, SMenuEntry)
		                                    .Text("More")
		                                    .SubMenu(SNew(SVerticalBox) +
		                                        SVerticalBox::Slot().AutoHeight()[SAssignNew(Deep, SMenuEntry)
		                                                .Text("Deep")
		                                                .OnSelected([&] { ++DeepPicked; })])])],
		    FVector2(500.0f, 300.0f));
		T.ClickWidget(*Combo);
		T.HoverWidget(*More);
		CHECK(T.GetApp().GetMenuCount() == 2);
		// The submenu opens to the right of its entry.
		CHECK(FUITestApp::RectOf(*Deep).Left >= FUITestApp::RectOf(*More).Right - 0.01f);
		T.HoverWidget(*Plain);
		CHECK(T.GetApp().GetMenuCount() == 1);
		T.HoverWidget(*More);
		T.ClickWidget(*Deep);
		CHECK(DeepPicked == 1);
		CHECK(T.GetApp().GetMenuCount() == 0);
	}

	TEST_CASE("Mouse wheel bubbles to the nearest scroll box")
	{
		FUITestApp T;
		TSharedPtr<SScrollBox> Scroll;
		TSharedPtr<SButton> Button;
		SAssignNew(Scroll, SScrollBox);
		Scroll->AddSlot()[SAssignNew(Button, SButton).Text("Inside")];
		Scroll->AddSlot()[SNew(SBox).HeightOverride(TOptional<float>(1000.0f))];
		const TSharedRef<SWindow> Window = T.AddWindow(ToSharedRef(Scroll));
		T.Wheel(Window, FUITestApp::CenterOf(*Button), -2.0f);
		CHECK(Scroll->GetScrollOffset() == doctest::Approx(96.0f));
	}

	TEST_CASE("Borderless windows: title bar zone unless a widget claims the point")
	{
		FUITestApp T;
		TSharedPtr<SButton> Close;
		const TSharedRef<SWindow> Window = SNew(SWindow).UseOSBorder(false).TitleBarHeight(30.0f).ClientSize(FVector2(
		    300.0f, 200.0f))[SNew(SBox).HAlign(HAlign_Right).VAlign(VAlign_Top)[SAssignNew(Close, SButton).Text("X")]];
		T.GetApp().AddWindow(Window);
		T.Tick();
		CHECK(Window->GetWindowZoneForPoint(FVector2(20.0f, 10.0f)) == EWindowZone::TitleBar);
		CHECK(Window->GetWindowZoneForPoint(FUITestApp::CenterOf(*Close)) == EWindowZone::ClientArea);
		CHECK(Window->GetWindowZoneForPoint(FVector2(20.0f, 100.0f)) == EWindowZone::Unspecified);
	}

	TEST_CASE("Modal windows block other windows and run their own frames until closed")
	{
		FUITestApp T;
		int32 MainClicks = 0;
		TSharedPtr<SButton> MainButton;
		const TSharedRef<SWindow> Main = T.AddWindow(SNew(SBox)
		        .HAlign(HAlign_Left)
		        .VAlign(VAlign_Top)[SAssignNew(MainButton, SButton)
		                .Text("Main")
		                .OnClicked([&]
		{
			++MainClicks;
			return FReply::Handled();
		})]);

		bool bClosed = false;
		const TSharedRef<SWindow> Dialog =
		    SNew(SWindow)
		        .Title("Dialog")
		        .ClientSize(FVector2(200.0f, 100.0f))
		        .OnWindowClosed([&](const TSharedRef<SWindow>&) { bClosed = true; })[SNew(SBox)];
		// A timer in the dialog clicks the main window (blocked) and then closes the dialog.
		int32 Frames = 0;
		Dialog->RegisterActiveTimer(0.0f, [&](double, float)
		{
			++Frames;
			if (Frames == 2)
			{
				T.Click(Main, FUITestApp::CenterOf(*MainButton));
			}
			if (Frames == 5)
			{
				Dialog->RequestClose();
				return EActiveTimerReturn::Stop;
			}
			return EActiveTimerReturn::Continue;
		});
		T.GetApp().AddModalWindow(Dialog, Main.ToSharedPtr());
		CHECK(bClosed);
		CHECK(MainClicks == 0);
		CHECK(T.GetApp().GetWindows().size() == 1);
		CHECK(static_cast<FNullPlatformWindow&>(*Main->GetNativeWindow()).IsEnabled());
		T.ClickWidget(*MainButton);
		CHECK(MainClicks == 1);
	}

	TEST_CASE("Input processors see input first")
	{
		class FEatF5 final : public IInputProcessor
		{
		public:
			bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event) override
			{
				return Event.GetKey() == EKey::F5 && ++Count > 0;
			}
			int32 Count = 0;
		};
		FUITestApp T;
		TArray<FString> Log;
		TSharedPtr<SRecorder> R;
		T.AddWindow(SAssignNew(R, SRecorder).Name("R").Log(&Log).Focusable(true));
		T.GetApp().SetKeyboardFocus(R);
		Log.clear();
		TSharedPtr<FEatF5> Processor = std::make_shared<FEatF5>();
		T.GetApp().RegisterInputPreProcessor(
		    TSharedRef<IInputProcessor>(StaticCastSharedPtr<IInputProcessor>(Processor)));
		T.Key(EKey::F5);
		T.Key(EKey::F6);
		CHECK(Processor->Count == 1);
		CHECK(Log == TArray<FString>{"R key F6"});
	}
}
