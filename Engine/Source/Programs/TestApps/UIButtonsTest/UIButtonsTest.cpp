// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// UIButtonsTest: buttons (styles, disabled, not focusable), check boxes
// (with an undetermined parent), and progress bars and a throbber.
//
// Self-test: clicks each kind of button, presses one from the keyboard,
// toggles the check boxes and checks the parent's undetermined state.

#include "TestApp.h"
#include "TestWidgets.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SImage.h"
#include "Widgets/Text/STextBlock.h"

class FButtonsTestApp final : public FTestApp
{
public:
	FButtonsTestApp() : FTestApp({ "UIButtonsTest", "Genesis UI - Buttons and toggles", FVector2(900.0f, 640.0f) }) {}

protected:
	TSharedRef<SWidget> MakeContent() override
	{
		const auto Clicked = [this](const char* Name) { return [this, Name] { ++Clicks; SetLastEvent(FString::Format("Clicked {}", Name)); return FReply::Handled(); }; };
		return TestFrame(*this, "Buttons and toggles", TestPage({
			TestSection("Buttons", "Click, or focus with Tab and press Enter or Space. The disabled one ignores everything.",
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2(8.0f, 8.0f))
					+ SWrapBox::Slot()[SAssignNew(DefaultButton, SButton).Text("Default").ToolTipText("A default button").OnClicked(Clicked("Default"))]
					+ SWrapBox::Slot()[SNew(SButton).Text("Primary").ButtonStyle(&TestButtonStyle("PrimaryButton")).OnClicked(Clicked("Primary"))]
					+ SWrapBox::Slot()[SNew(SButton).Text("Flat").ButtonStyle(&TestButtonStyle("FlatButton")).OnClicked(Clicked("Flat"))]
					+ SWrapBox::Slot()[SNew(SButton).Text("Disabled").IsEnabled(false).OnClicked(Clicked("Disabled"))]
					+ SWrapBox::Slot()[SNew(SButton).Text("Not focusable").IsFocusable(false).ToolTipText("Clicking this keeps focus where it was").OnClicked(Clicked("Not focusable"))]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 10.0f, 0.0f, 0.0f))[SNew(STextBlock).Text([this] { return FString::Format("Buttons clicked {} times", Clicks); })]),

			TestSection("Check boxes", "\"All layers\" shows the undetermined state when only some layers are on.",
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 2.0f))[SNew(SCheckBox).Text("All layers")
					.IsChecked([this] { return GetAllLayers(); })
					.OnCheckStateChanged([this](ECheckBoxState)
					{
						const ECheckBoxState New = (LayerA == ECheckBoxState::Checked && LayerB == ECheckBoxState::Checked) ? ECheckBoxState::Unchecked : ECheckBoxState::Checked;
						LayerA = LayerB = New;
						SetLastEvent("All layers toggled");
					})]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(22.0f, 2.0f, 0.0f, 2.0f))[SNew(SCheckBox).Text("Brushes").IsChecked([this] { return LayerA; }).OnCheckStateChanged([this](ECheckBoxState S) { LayerA = S; SetLastEvent("Brushes toggled"); })]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(22.0f, 2.0f, 0.0f, 2.0f))[SNew(SCheckBox).Text("Entities").IsChecked([this] { return LayerB; }).OnCheckStateChanged([this](ECheckBoxState S) { LayerB = S; SetLastEvent("Entities toggled"); })]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 2.0f))[SNew(SCheckBox).Text("Snap to grid (keeps its own state)").IsChecked(ECheckBoxState::Checked)]),

			TestSection("Progress", "A bound fraction, a busy bar and a throbber (these repaint every frame; the rest of the window only when something changes).",
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 4.0f))[SNew(SProgressBar).Percent([] { return TOptional<float>(FMath::Fmod(static_cast<float>(FUIApplication::Get().GetTime()) * 0.2f, 1.0f)); })]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 4.0f))[SNew(SProgressBar)]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 8.0f)).HAlign(HAlign_Left)[SNew(SThrobber)]),
		}));
	}

	void BuildSelfTest(FTestScript& Script) override
	{
		Script.Capture("start")
			.Do([this] { PointAtText("Default", true); })
			.Check("Default was clicked", [this] { return GetLastEvent() == "Clicked Default" && Clicks == 1; })
			.Do([this] { PointAtText("Primary", true); })
			.Check("Primary was clicked", [this] { return GetLastEvent() == "Clicked Primary"; })
			.Do([this] { PointAtText("Disabled", true); })
			.Check("the disabled button ignores clicks", [this] { return GetLastEvent() == "Clicked Primary" && Clicks == 2; })
			// Keyboard: focus Default (a click focuses it), then Enter.
			.Do([this] { Click(GetCenter(*DefaultButton)); })
			.Do([this] { PressKey(EKey::Enter); })
			.Check("Enter presses the focused button", [this] { return Clicks == 4; })
			.Do([this] { PointAtText("Not focusable", true); })
			.Check("a button that is not focusable keeps focus where it was", [this]
			{
				return FUIApplication::Get().GetKeyboardFocusedWidget().get() == DefaultButton.get();
			})
			// Check boxes: Brushes starts on and Entities off, so the parent is
			// undetermined until both match.
			.Check("one layer on: All layers is undetermined", [this] { return GetAllLayers() == ECheckBoxState::Undetermined; })
			.Do([this] { PointAtText("Entities", true); })
			.Check("both layers on: All layers is checked", [this] { return LayerB == ECheckBoxState::Checked && GetAllLayers() == ECheckBoxState::Checked; })
			.Do([this] { PointAtText("Brushes", true); })
			.Check("Brushes off: All layers is undetermined again", [this] { return LayerA == ECheckBoxState::Unchecked && GetAllLayers() == ECheckBoxState::Undetermined; })
			.Capture("undetermined")
			.Do([this] { PointAtText("All layers", true); })
			.Check("All layers turns every layer on", [this] { return LayerA == ECheckBoxState::Checked && LayerB == ECheckBoxState::Checked; })
			.Do([this] { MoveMouse(GetCenter(*DefaultButton)); })
			.Capture("hover", 40);
	}

private:
	ECheckBoxState GetAllLayers() const { return LayerA == LayerB ? LayerA : ECheckBoxState::Undetermined; }

	TSharedPtr<SButton> DefaultButton;
	int32 Clicks = 0;
	ECheckBoxState LayerA = ECheckBoxState::Checked;
	ECheckBoxState LayerB = ECheckBoxState::Unchecked;
};

GENESIS_TEST_APP(FButtonsTestApp)
