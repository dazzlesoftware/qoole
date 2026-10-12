// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Headless tests of SEditableText / SEditableTextBox.

#include "UITestHelpers.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Text/SEditableText.h"

namespace
{
	struct FTextFixture
	{
		FUITestApp T;
		TSharedPtr<SEditableTextBox> Box;
		TSharedPtr<SEditableText> Text;
		TArray<TPair<FString, ETextCommit>> Commits;
		int32 Changes = 0;

		explicit FTextFixture(const char* Initial = "", bool bMultiLine = false, bool bPassword = false)
		{
			T.AddWindow(SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SAssignNew(Box, SEditableTextBox).Text(Initial).IsMultiLine(bMultiLine).IsPassword(bPassword)
					.RevertTextOnEscape(true)
					.OnTextChanged([this](const FString&) { ++Changes; })
					.OnTextCommitted([this](const FString& S, ETextCommit How) { Commits.push_back({ S, How }); })]
				+ SVerticalBox::Slot().AutoHeight()[SNew(SButton).Text("Other")]);
			Text = Box->GetEditableText().ToSharedPtr();
			T.GetApp().SetKeyboardFocus(Text);
			T.Tick();
		}
	};
}

TEST_SUITE("UIEditableText")
{
	TEST_CASE("Typing inserts at the caret; arrows, Home and End move it")
	{
		FTextFixture F;
		F.T.Type("Hello");
		CHECK(F.Text->GetText() == "Hello");
		CHECK(F.Text->GetCaretIndex() == 5);
		F.T.Key(EKey::Left);
		F.T.Key(EKey::Left);
		F.T.Type("p!");
		CHECK(F.Text->GetText() == "Help!lo");
		F.T.Key(EKey::Home);
		F.T.Type(">");
		F.T.Key(EKey::End);
		F.T.Type("<");
		CHECK(F.Text->GetText() == ">Help!lo<");
		CHECK(F.Changes == 9);
	}

	TEST_CASE("Backspace and Delete, by character and by word")
	{
		FTextFixture F("alpha beta gamma");
		F.T.Key(EKey::BackSpace, false, true); // Ctrl+Backspace: "gamma"
		CHECK(F.Text->GetText() == "alpha beta ");
		F.T.Key(EKey::BackSpace);
		CHECK(F.Text->GetText() == "alpha beta");
		F.T.Key(EKey::Home);
		F.T.Key(EKey::Delete, false, true); // Ctrl+Delete: "alpha "
		CHECK(F.Text->GetText() == "beta");
		F.T.Key(EKey::Delete);
		CHECK(F.Text->GetText() == "eta");
	}

	TEST_CASE("Selection with Shift and Ctrl, replace, select all")
	{
		FTextFixture F("one two three");
		F.T.Key(EKey::Home);
		F.T.Key(EKey::Right, true, true); // Ctrl+Shift+Right selects "one "
		CHECK(F.Text->GetSelectedText() == "one ");
		F.T.Type("1 ");
		CHECK(F.Text->GetText() == "1 two three");
		F.T.Key(EKey::A, false, true);
		CHECK(F.Text->GetSelectedText() == "1 two three");
		F.T.Key(EKey::Left); // collapses to the start
		CHECK(!F.Text->HasSelection());
		CHECK(F.Text->GetCaretIndex() == 0);
	}

	TEST_CASE("Undo groups typing; redo restores")
	{
		FTextFixture F;
		F.T.Type("abc");
		F.T.Key(EKey::SpaceBar); // a key press alone types nothing (chars do)
		F.T.Key(EKey::Home);
		F.T.Type("X");
		CHECK(F.Text->GetText() == "Xabc");
		F.T.Key(EKey::Z, false, true);
		CHECK(F.Text->GetText() == "abc");
		F.T.Key(EKey::Z, false, true);
		CHECK(F.Text->GetText() == "");
		F.T.Key(EKey::Y, false, true);
		CHECK(F.Text->GetText() == "abc");
		F.T.Key(EKey::Z, true, true); // Ctrl+Shift+Z redoes too
		CHECK(F.Text->GetText() == "Xabc");
	}

	TEST_CASE("Clipboard: copy, cut, paste; one line flattens line breaks")
	{
		FTextFixture F("copy me");
		F.T.Key(EKey::A, false, true);
		F.T.Key(EKey::C, false, true);
		CHECK(F.T.GetPlatform().ClipboardPaste() == "copy me");
		F.T.Key(EKey::X, false, true);
		CHECK(F.Text->GetText() == "");
		F.T.GetPlatform().ClipboardCopy("two\nlines");
		F.T.Key(EKey::V, false, true);
		CHECK(F.Text->GetText() == "two lines");
	}

	TEST_CASE("Enter commits; Escape reverts; moving focus commits")
	{
		FTextFixture F("start");
		F.T.Type("!");
		F.T.Key(EKey::Enter);
		REQUIRE(F.Commits.size() == 1);
		CHECK(F.Commits[0].first == "start!");
		CHECK(F.Commits[0].second == ETextCommit::OnEnter);
		F.T.Type("?");
		F.T.Key(EKey::Escape);
		CHECK(F.Text->GetText() == "start!");
		CHECK(F.Commits.back().second == ETextCommit::OnCleared);
		F.T.Type("#");
		F.T.Key(EKey::Tab);
		CHECK(!F.Text->HasKeyboardFocus());
		CHECK(F.Commits.back().first == "start!#");
		CHECK(F.Commits.back().second == ETextCommit::OnUserMovedFocus);
	}

	TEST_CASE("Mouse: click places the caret, drag selects, double-click selects a word")
	{
		FTextFixture F("hello wonderful world");
		F.T.GetApp().ClearKeyboardFocus();
		F.T.Tick();
		const TSharedRef<SWindow> Window = F.T.GetApp().GetWindows()[0];
		const FRect Rect = FUITestApp::RectOf(*F.Text);
		const float Y = Rect.GetCenter().Y;

		F.T.Click(Window, FVector2(Rect.Left + 1.0f, Y));
		CHECK(F.Text->HasKeyboardFocus());
		CHECK(F.Text->GetCaretIndex() == 0);

		F.T.MouseMove(Window, FVector2(Rect.Left + 1.0f, Y));
		F.T.MouseDown(Window, FVector2(Rect.Left + 1.0f, Y));
		F.T.MouseMove(Window, FVector2(Rect.Right - 1.0f, Y));
		F.T.MouseUp(Window, FVector2(Rect.Right - 1.0f, Y));
		F.T.Tick();
		CHECK(F.Text->GetSelectedText() == "hello wonderful world");

		// Double-click in the middle of "wonderful".
		F.Text->SetCaret(10);
		const float X = static_cast<float>(F.Text->GetCaretScreenRect().Left) / Window->GetScale();
		F.T.GetPlatform().QueueMouseMove(ToSharedRef(Window->GetNativeWindow()), FVector2(X, Y));
		F.T.Tick();
		F.T.GetApp().OnMouseDoubleClick(ToSharedRef(Window->GetNativeWindow()), EMouseButton::Left, FVector2(X, Y));
		F.T.GetApp().OnMouseUp(ToSharedRef(Window->GetNativeWindow()), EMouseButton::Left, FVector2(X, Y));
		CHECK(F.Text->GetSelectedText() == "wonderful");
	}

	TEST_CASE("Password text is masked and cannot be copied")
	{
		FTextFixture F("", false, true);
		F.T.Type("s3cr\xC3\xA9t"); // 6 code points, 7 bytes
		CHECK(F.Text->GetText() == "s3cr\xC3\xA9t");
		F.T.Key(EKey::A, false, true);
		F.T.Key(EKey::C, false, true);
		CHECK(F.T.GetPlatform().ClipboardPaste() == "");
		F.T.Key(EKey::End);
		F.T.Key(EKey::Left);
		F.T.Key(EKey::Left);
		CHECK(F.Text->GetCaretIndex() == 4); // stepped over the 2-byte character
	}

	TEST_CASE("Multi-line: Enter inserts a line, Up and Down keep the column")
	{
		FTextFixture F("", true);
		F.T.Type("first line");
		F.T.Key(EKey::Enter);
		F.T.Type("second");
		CHECK(F.Text->GetText() == "first line\nsecond");
		F.T.Key(EKey::Up);
		CHECK(F.Text->GetCaretIndex() >= 4); // about the same column on the first line
		CHECK(F.Text->GetCaretIndex() <= 10);
		F.T.Key(EKey::Down);
		CHECK(F.Text->GetCaretIndex() == 17);
		CHECK(F.Commits.empty());
	}

	TEST_CASE("IME: composition shows inline, committed text arrives as characters")
	{
		FTextFixture F("ab");
		F.T.Key(EKey::Left);
		FNullPlatformApplication& Platform = F.T.GetPlatform();
		CHECK(Platform.GetNullTextInputMethod().GetActiveContext() != nullptr);
		Platform.QueueCompositionBegin();
		Platform.QueueCompositionUpdate("\xE3\x81\x8B", 3); // か
		F.T.Tick();
		CHECK(F.Text->IsComposing());
		CHECK(F.Text->GetText() == "ab"); // not committed yet
		CHECK(F.Text->GetCompositionText() == "\xE3\x81\x8B");
		Platform.QueueCompositionUpdate("\xE6\xBC\xA2", 3); // 漢
		Platform.QueueCompositionEnd("\xE6\xBC\xA2");
		F.T.Tick();
		CHECK(!F.Text->IsComposing());
		CHECK(F.Text->GetText() == "a\xE6\xBC\xA2" "b");
		CHECK(F.Text->GetCaretIndex() == 4);

		// Losing focus deactivates the context.
		F.T.Key(EKey::Tab);
		CHECK(Platform.GetNullTextInputMethod().GetActiveContext() == nullptr);
	}

	TEST_CASE("Read-only text can be selected and copied but not changed")
	{
		FUITestApp T;
		TSharedPtr<SEditableTextBox> Box;
		T.AddWindow(SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[SAssignNew(Box, SEditableTextBox).Text("fixed").IsReadOnly(true)]);
		T.GetApp().SetKeyboardFocus(Box->GetEditableText());
		T.Type("x");
		T.Key(EKey::BackSpace);
		CHECK(Box->GetText() == "fixed");
		T.Key(EKey::A, false, true);
		T.Key(EKey::C, false, true);
		CHECK(T.GetPlatform().ClipboardPaste() == "fixed");
	}
}
