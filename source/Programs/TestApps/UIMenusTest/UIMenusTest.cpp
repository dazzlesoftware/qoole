// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// UIMenusTest: popups. Drop-down menus from combo buttons (a menu bar, a
// submenu, separators, shortcuts shown), a context menu, and tooltips
// (text and custom widgets).
//
// Self-test: opens a menu and its submenu and picks an entry, opens a
// context menu and closes it with Escape, and shows both tooltips.

#include "TestApp.h"
#include "TestWidgets.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/SImage.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"

class FMenusTestApp final : public FTestApp
{
public:
	FMenusTestApp() : FTestApp({ "UIMenusTest", "Genesis UI - Menus and tooltips", FVector2(900.0f, 640.0f) }) {}

protected:
	TSharedRef<SWidget> MakeMenuContent(const FString& MenuName)
	{
		const auto Picked = [this](const char* What) { return [this, What]() { SetLastEvent(FString::Format("Menu: {}", What)); }; };
		if (MenuName == "File")
		{
			return SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry).Text("New map").InputGesture("Ctrl+N").OnSelected(Picked("New map"))]
				+ SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry).Text("Open...").InputGesture("Ctrl+O").OnSelected(Picked("Open"))]
				+ SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry).Text("Open recent")
					.SubMenu(SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry).Text("castle.map").OnSelected(Picked("castle.map"))]
						+ SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry).Text("dungeon.map").OnSelected(Picked("dungeon.map"))]
						+ SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry).Text("arena.map").OnSelected(Picked("arena.map"))])]
				+ SVerticalBox::Slot().AutoHeight()[SNew(SSeparator)]
				+ SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry).Text("Save").InputGesture("Ctrl+S").OnSelected(Picked("Save"))]
				+ SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry).Text("Disabled entry").IsEnabled(false)]
				+ SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry).Text("Exit").OnSelected([this] { GetApp().RequestQuit(); })];
		}
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry).Text("Undo").InputGesture("Ctrl+Z").OnSelected(Picked("Undo"))]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry).Text("Redo").InputGesture("Ctrl+Y").OnSelected(Picked("Redo"))]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SSeparator)]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry).Text("Cut").InputGesture("Ctrl+X").OnSelected(Picked("Cut"))]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry).Text("Copy").InputGesture("Ctrl+C").OnSelected(Picked("Copy"))]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry).Text("Paste").InputGesture("Ctrl+V").OnSelected(Picked("Paste"))];
	}

	TSharedRef<SWidget> MakeContent() override
	{
		const auto MenuButton = [this](const char* Name) -> TSharedRef<SComboButton>
		{
			const FString MenuName = Name;
			return SNew(SComboButton).Text(Name).HasDownArrow(false).Placement(EMenuPlacement::BelowAnchor).ButtonStyle(&TestButtonStyle("FlatButton"))
				.OnGetMenuContent([this, MenuName] { return MakeMenuContent(MenuName); });
		};

		const TSharedRef<SWidget> Page = TestPage({
			TestSection("Menu bar", "Combo buttons styled as a menu bar; once one is open, moving to the next opens it.",
				SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Header")).Padding(FMargin(6.0f, 2.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[MenuButton("File")]
					+ SHorizontalBox::Slot().AutoWidth()[MenuButton("Edit")]
				]),
			TestSection("Drop-down menus", "A menu with a submenu, shortcuts and a disabled entry. Arrows move, Enter picks, Escape closes.",
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 8.0f, 0.0f))[SAssignNew(MapMenu, SComboButton).Text("Choose a map").OnGetMenuContent([this] { return MakeMenuContent("File"); })]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SComboButton).Text("Edit").OnGetMenuContent([this] { return MakeMenuContent("Edit"); })]),
			TestSection("Context menu", "Right-click the area.",
				SAssignNew(ContextArea, SBorder).BorderImage(&FAppStyle::Get().GetBrush("Recessed")).Padding(FMargin(16.0f, 28.0f)).HAlign(HAlign_Center)
				.OnMouseButtonDown([this](const FGeometry&, const FPointerEvent& Event)
				{
					if (Event.GetEffectingButton() != EMouseButton::Right)
					{
						return FReply::Unhandled();
					}
					FUIApplication& UIApp = FUIApplication::Get();
					const TSharedPtr<SWindow> Window = UIApp.FindWindowForWidget(*ContextArea);
					const FVector2 Position = Event.GetAbsolutePosition() / (Window ? Window->GetScale() : 1.0f);
					UIApp.PushMenu(ToSharedRef(StaticCastSharedPtr<SWidget>(ContextArea)), MakeMenuContent("Edit"), Position);
					SetLastEvent("Context menu");
					return FReply::Handled();
				})
				[
					SNew(STextBlock).Text("Right-click here").TextStyle(&TestTextStyle("SmallText"))
				]),
			TestSection("Tooltips", "Rest the pointer on a widget. They close on a click and stay closed until the pointer moves on.",
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 8.0f, 0.0f))[SAssignNew(TextTipButton, SButton).Text("Text tooltip").ToolTipText("A plain text tooltip, wrapped when it gets long enough to need more than one line of text.")]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SAssignNew(CustomTipButton, SButton).Text("Custom tooltip")
					.ToolTip(SNew(SToolTip)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text("Any widget can be a tooltip").TextStyle(&TestTextStyle("BoldText"))]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))[SNew(SProgressBar)]
						])
				]),
		});
		return TestFrame(*this, "Menus and tooltips", Page);
	}

	void BuildSelfTest(FTestScript& Script) override
	{
		const double ToolTipWait = GetApp().ToolTipDelay + 0.3;
		Script.Capture("start")
			.Do([this] { Click(GetCenter(*MapMenu)); })
			.Check("the menu opens", [this] { return GetApp().GetMenuCount() == 1; }, 5)
			.Do([this] { PointAtText("Open recent", false); })
			.Check("resting on Open recent opens the submenu", [this] { return GetApp().GetMenuCount() == 2; }, 30)
			.Capture("submenu")
			.Do([this] { PointAtText("castle.map", true); })
			.Check("picking an entry runs it and closes the menus", [this] { return GetLastEvent() == "Menu: castle.map" && GetApp().GetMenuCount() == 0; })
			// The menu bar's File menu.
			.Do([this] { PointAtText("File", true); })
			.Check("the menu bar opens File", [this] { return GetApp().GetMenuCount() == 1 && IsTextShown("New map") && !IsTextShown("Undo"); }, 5)
			.Capture("menu_bar")
			.Do([this] { PressKey(EKey::Escape); })
			.Check("Escape closes the menu", [this] { return GetApp().GetMenuCount() == 0; })
			// The context menu.
			.Do([this] { Click(GetCenter(*ContextArea), EMouseButton::Right); })
			.Check("right-click opens the context menu", [this] { return GetApp().GetMenuCount() == 1 && GetLastEvent() == "Context menu"; })
			.Capture("context_menu")
			.Do([this] { PointAtText("Copy", true); })
			.Check("the context menu's entry runs", [this] { return GetLastEvent() == "Menu: Copy"; })
			// Tooltips.
			.Do([this] { MoveMouse(GetCenter(*TextTipButton)); })
			.WaitSeconds(ToolTipWait)
			.Check("a text tooltip shows after the delay", [this] { return GetApp().GetActiveToolTip() != nullptr; }, 5)
			.Capture("tooltip_text")
			.Do([this] { MoveMouse(GetCenter(*CustomTipButton)); })
			.WaitSeconds(ToolTipWait)
			.Check("a custom tooltip shows", [this] { return GetApp().GetActiveToolTip() != nullptr && IsTextShown("Any widget can be a tooltip"); }, 5)
			.Capture("tooltip_custom")
			.Do([this] { Click(GetCenter(*CustomTipButton)); })
			.Check("a click closes the tooltip", [this] { return GetApp().GetActiveToolTip() == nullptr; }, 5);
	}

private:
	// Whether a text is shown in any window (menus and tooltips included).
	bool IsTextShown(std::string_view Text) const
	{
		for (const TSharedRef<SWindow>& Window : GetApp().GetWindows())
		{
			if (FindWidget<STextBlock>(*Window, [Text](STextBlock& Block) { return Block.GetText().View() == Text; }))
			{
				return true;
			}
		}
		return false;
	}

	TSharedPtr<SComboButton> MapMenu;
	TSharedPtr<SBorder> ContextArea;
	TSharedPtr<SButton> TextTipButton;
	TSharedPtr<SButton> CustomTipButton;
};

GENESIS_TEST_APP(FMenusTestApp)
