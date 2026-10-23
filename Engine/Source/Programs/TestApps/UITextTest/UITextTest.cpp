// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// UITextTest: text blocks (wrapping, fallback fonts, justification) and
// text boxes (hint, password, read-only, select-all, multi-line).
//
// Self-test: types into the boxes and commits with Enter, reverts with
// Escape, checks that read-only text cannot change and that the password
// box keeps what was typed, and types several lines.

#include "TestApp.h"
#include "Containers/ContainerAliases.h"
#include "Templates/SharedPointer.h"
#include "TestWidgets.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Text/SEditableText.h"
#include "Widgets/Text/STextBlock.h"

class FTextTestApp final : public FTestApp
{
public:
	FTextTestApp() : FTestApp({"UITextTest", "Genesis UI - Text", FVector2(900.0f, 760.0f)}) {}

protected:
	TSharedRef<SWidget> MakeContent() override
	{
		const TSharedRef<SGridPanel> Form = SNew(SGridPanel);
		Form->SetColumnFill(1, 1.0f);
		const TFunction<void(int32, const char*, const TSharedRef<SWidget>&)> Row =
		    [&Form](int32 Index, const char* Label, const TSharedRef<SWidget>& Field)
		{
			Form->AddSlot(0, Index)
			    .VAlign(VAlign_Center)
			    .Padding(FMargin(0.0f, 4.0f, 12.0f, 4.0f))[SNew(STextBlock).Text(Label)];
			Form->AddSlot(1, Index).Padding(FMargin(0.0f, 4.0f))[Field];
		};
		const TFunction<void(const FString&, ETextCommit)> OnCommit = [this](const FString& Text, ETextCommit How)
		{
			const char* HowText = How == ETextCommit::OnEnter
			    ? "Enter"
			    : (How == ETextCommit::OnUserMovedFocus
			              ? "focus moved"
			              : (How == ETextCommit::OnCleared ? "Escape (reverted)" : "code"));
			LastCommit = FString::Format("Committed \"{}\" ({})", Text, HowText);
			SetLastEvent(LastCommit);
		};
		Row(0, "Name",
		    SAssignNew(NameBox, SEditableTextBox)
		        .HintText("Type a name")
		        .OnTextCommitted(OnCommit)
		        .RevertTextOnEscape(true));
		Row(1, "Password",
		    SAssignNew(PasswordBox, SEditableTextBox)
		        .IsPassword(true)
		        .HintText("Masked; cannot be copied")
		        .OnTextCommitted(OnCommit));
		Row(2, "Read-only",
		    SAssignNew(ReadOnlyBox, SEditableTextBox).Text("Select and copy me, but no editing").IsReadOnly(true));
		Row(3, "Select all",
		    SNew(SEditableTextBox)
		        .Text("Focus selects everything")
		        .SelectAllTextWhenFocused(true)
		        .OnTextCommitted(OnCommit));
		Row(4, "Notes",
		    SNew(SBox).HeightOverride(TOptional<float>(110.0f))[SAssignNew(NotesBox, SEditableTextBox)
		            .IsMultiLine(true)
		            .AutoWrapText(true)
		            .Text("Multiple lines: Enter adds a line, Ctrl+Enter commits.\nUp and "
		                  "Down keep the column; Ctrl+arrows move by word.")
		            .OnTextCommitted(OnCommit)]);

		return TestFrame(*this, "Text",
		    TestPage({
		        TestSection("Text blocks", "Wrapped to the available width; resize the window.",
		            SNew(SVerticalBox) +
		                SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
		                        .AutoWrapText(true)
		                        .Text("Genesis UI lays out text with FTextLayout: lines are shaped by HarfBuzz, "
		                              "rasterized by FreeType at the final pixel size, and wrapped at word "
		                              "boundaries. Layout runs only when something changes, so an idle editor redraws "
		                              "nothing.")] +
		                SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))[SNew(STextBlock)
		                        .Text("Fallback fonts: नमस्ते दुनिया — Devanagari, shaped")
		                        .TextStyle(&TestTextStyle("BoldText"))] +
		                SVerticalBox::Slot().AutoHeight().Padding(FMargin(
		                    0.0f, 4.0f, 0.0f, 0.0f))[SNew(STextBlock).Text("Ελληνικά · Русский · Café · fi ligature")] +
		                SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 4.0f, 0.0f,
		                    0.0f))[SNew(STextBlock).Text("Centered text").Justification(ETextJustify::Center)] +
		                SVerticalBox::Slot().AutoHeight()
		                    [SNew(STextBlock).Text("Right-aligned text").Justification(ETextJustify::Right)] +
		                SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 4.0f, 0.0f,
		                    0.0f))[SNew(STextBlock).Text("Small text style").TextStyle(&TestTextStyle("SmallText"))] +
		                SVerticalBox::Slot().AutoHeight()
		                    [SNew(STextBlock).Text("Heading text style").TextStyle(&TestTextStyle("HeadingText"))]),
		        TestSection("Text boxes",
		            "Caret, selection (mouse drag, double-click, Shift+arrows), undo/redo, clipboard, IME composition.",
		            SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[Form] +
		                SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 10.0f, 0.0f, 0.0f))[SNew(STextBlock)
		                        .TextStyle(&TestTextStyle("SmallText"))
		                        .Text([this] { return LastCommit; })]),
		    }));
	}

	void BuildSelfTest(FTestScript& Script) override
	{
		Script.Capture("start")
		    .Do([this] { Click(GetCenter(*NameBox)); })
		    .Do([this] { TypeText("brush_01"); })
		    .Do([this] { PressKey(EKey::Enter); })
		    .Check("Enter commits the name",
		        [this] { return NameBox->GetText() == "brush_01" && LastCommit == "Committed \"brush_01\" (Enter)"; })
		    .Do([this] { TypeText("_extra"); })
		    .Check("typing changes the text", [this] { return NameBox->GetText() == "brush_01_extra"; })
		    .Do([this] { PressKey(EKey::Escape); })
		    .Check("Escape reverts to the committed text", [this] { return NameBox->GetText() == "brush_01"; })
		    .Do([this] { Click(GetCenter(*PasswordBox)); })
		    .Do([this] { TypeText("secret"); })
		    .Check("the password box keeps what was typed", [this] { return PasswordBox->GetText() == "secret"; })
		    .Do(
		        [this]
		{
			ReadOnlyText = ReadOnlyBox->GetText();
			Click(GetCenter(*ReadOnlyBox));
		})
		    .Do(
		        [this]
		{
			TypeText("xyz");
			PressKey(EKey::BackSpace);
		})
		    .Check("read-only text does not change", [this] { return ReadOnlyBox->GetText() == ReadOnlyText; })
		    .Do([this] { Click(GetCenter(*NotesBox)); })
		    .Do(
		        [this]
		{
			PressKey(EKey::End);
			PressKey(EKey::Enter);
			TypeText("A third line");
		})
		    .Check("Enter adds a line in a multi-line box", [this] {
			return NotesBox->GetText().EndsWith("\nA third line");
		}).Capture("typed");
	}

private:
	TSharedPtr<SEditableTextBox> NameBox;
	TSharedPtr<SEditableTextBox> PasswordBox;
	TSharedPtr<SEditableTextBox> ReadOnlyBox;
	TSharedPtr<SEditableTextBox> NotesBox;
	FString ReadOnlyText;
	FString LastCommit = "(nothing committed yet)";
};

IMPLEMENT_TEST_APP(FTextTestApp)
