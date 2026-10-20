// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Widgets/Text/SEditableText.h"

#include "Framework/Application/SlateApplication.h"

namespace
{
	constexpr double CaretBlinkPeriod = 0.53;
	constexpr char PasswordChar = '*';
	constexpr int32 MaxUndo = 200;

	bool IsWordByte(unsigned char C)
	{
		// ASCII letters, digits and '_'; any non-ASCII byte counts as part
		// of a word (letters in other scripts).
		return C >= 0x80 || (C >= '0' && C <= '9') || (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || C == '_';
	}

	bool IsSpaceByte(unsigned char C)
	{
		return C == ' ' || C == '\t' || C == '\n' || C == '\r';
	}

	// Byte offset of the code point starting at or after Index.
	int32 AlignToCodePoint(const FString& Text, int32 Index)
	{
		Index = FMath::Clamp(Index, 0, Text.Len());
		while (Index < Text.Len() && (static_cast<unsigned char>(Text[Index]) & 0xC0) == 0x80)
		{
			++Index;
		}
		return Index;
	}
}

// The IME's view of an editable text.
class FEditableTextInputContext final : public ITextInputMethodContext
{
public:
	explicit FEditableTextInputContext(SEditableText& InOwner) : Owner(&InOwner) {}

	void Detach() { Owner = nullptr; }

	void BeginComposition() override
	{
		if (!Owner || Owner->bComposing)
		{
			return;
		}
		Owner->BeginEdit(SEditableText::EEditKind::Other);
		Owner->DeleteSelection();
		Owner->bComposing = true;
		Owner->CompositionStart = Owner->CaretIndex;
		Owner->CompositionText.Empty();
		Owner->CompositionCaret = 0;
		Owner->Invalidate(EInvalidateWidgetReason::Layout);
	}

	void UpdateComposition(const FString& Text, int32 CaretIndex) override
	{
		if (!Owner)
		{
			return;
		}
		if (!Owner->bComposing)
		{
			BeginComposition();
		}
		Owner->CompositionText = Text;
		Owner->CompositionCaret = FMath::Clamp(CaretIndex, 0, Text.Len());
		Owner->LastCaretActivity = FSlateApplication::Get().GetTime();
		Owner->Invalidate(EInvalidateWidgetReason::Layout);
		Owner->EnsureCaretVisible();
	}

	void EndComposition() override
	{
		if (!Owner || !Owner->bComposing)
		{
			return;
		}
		Owner->bComposing = false;
		Owner->CompositionText.Empty();
		Owner->CompositionCaret = 0;
		Owner->Invalidate(EInvalidateWidgetReason::Layout);
	}

	FIntRect GetCaretRect() const override { return Owner ? Owner->GetCaretScreenRect() : FIntRect(); }

	TSharedPtr<IPlatformWindow> GetWindow() const override
	{
		if (!Owner || !FSlateApplication::IsInitialized())
		{
			return nullptr;
		}
		const TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWindowForWidget(*Owner);
		return Window ? Window->GetNativeWindow() : nullptr;
	}

private:
	SEditableText* Owner;
};

// ---------------------------------------------------------------------------
// Construction and state

SEditableText::SEditableText() = default;

SEditableText::~SEditableText()
{
	if (InputContext)
	{
		InputContext->Detach();
	}
}

void SEditableText::Construct(const FArguments& Args)
{
	Style = Args._Style ? Args._Style : &FAppStyle::Get().GetWidgetStyle<FEditableTextStyle>("EditableText");
	if (Args._Font.IsSet())
	{
		FontOverride = Args._Font.Get();
	}
	TextAttribute.Assign(Args._Text);
	EditedText = TextAttribute.Get();
	CaretIndex = EditedText.Len();
	HintText.Assign(Args._HintText);
	IsReadOnly.Assign(Args._IsReadOnly);
	IsPassword.Assign(Args._IsPassword);
	MinDesiredWidth.Assign(Args._MinDesiredWidth);
	bMultiLine = Args._IsMultiLine;
	bAutoWrap = Args._AutoWrapText && bMultiLine;
	bSelectAllWhenFocused = Args._SelectAllTextWhenFocused;
	bRevertOnEscape = Args._RevertTextOnEscape;
	bClearFocusOnCommit = Args._ClearKeyboardFocusOnCommit;
	OnTextChanged = Args._OnTextChanged;
	OnTextCommitted = Args._OnTextCommitted;
	SetClipping(EWidgetClipping::ClipToBounds);
	InputContext = std::make_shared<FEditableTextInputContext>(*this);
}

void SEditableText::SetText(TAttribute<FString> InText)
{
	TextAttribute.Assign(InText);
	if (!(EditedText == TextAttribute.Get()))
	{
		EditedText = TextAttribute.Get();
		CaretIndex = FMath::Min(CaretIndex, EditedText.Len());
		SelectionAnchor = INDEX_NONE;
		UndoStack.clear();
		RedoStack.clear();
		Invalidate(EInvalidateWidgetReason::Layout);
	}
}

FSlateFontInfo SEditableText::GetFont() const
{
	return FontOverride ? *FontOverride : Style->Font;
}

FString SEditableText::GetSelectedText() const
{
	return HasSelection() ? EditedText.Mid(GetSelectionBegin(), GetSelectionEnd() - GetSelectionBegin()) : FString();
}

void SEditableText::SelectAll()
{
	SelectionAnchor = 0;
	CaretIndex = EditedText.Len();
	OnCaretMoved();
}

void SEditableText::ClearSelection()
{
	if (SelectionAnchor != INDEX_NONE)
	{
		SelectionAnchor = INDEX_NONE;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SEditableText::SetCaret(int32 Index, bool bExtendSelection)
{
	Index = AlignToCodePoint(EditedText, Index);
	if (bExtendSelection)
	{
		if (SelectionAnchor == INDEX_NONE)
		{
			SelectionAnchor = CaretIndex;
		}
	}
	else
	{
		SelectionAnchor = INDEX_NONE;
	}
	CaretIndex = Index;
	LastEdit = EEditKind::Other; // typing after a caret move is a new undo step
	OnCaretMoved();
}

void SEditableText::OnCaretMoved()
{
	LastCaretActivity = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetTime() : 0.0;
	Invalidate(EInvalidateWidgetReason::Paint);
	EnsureCaretVisible();
	if (InputContext && FSlateApplication::IsInitialized() && HasKeyboardFocus())
	{
		if (ITextInputMethodSystem* Ime = FSlateApplication::Get().GetPlatformApplication().GetTextInputMethodSystem())
		{
			Ime->UpdateContextLayout(TSharedRef<ITextInputMethodContext>(StaticCastSharedPtr<ITextInputMethodContext>(InputContext)));
		}
	}
}

// ---------------------------------------------------------------------------
// Display text and layout

FString SEditableText::MakeDisplayText() const
{
	if (IsPassword.Get())
	{
		return FString(std::string(static_cast<SIZE_T>(FUtf8::CountCodePoints(EditedText.View())), PasswordChar));
	}
	if (bComposing)
	{
		return EditedText.Left(CompositionStart) + CompositionText + EditedText.Mid(CompositionStart);
	}
	return EditedText;
}

int32 SEditableText::ToDisplayIndex(int32 TextIndex) const
{
	if (IsPassword.Get())
	{
		return FUtf8::CountCodePoints(EditedText.View().substr(0, static_cast<SIZE_T>(FMath::Clamp(TextIndex, 0, EditedText.Len()))));
	}
	if (bComposing && TextIndex >= CompositionStart)
	{
		return TextIndex + CompositionText.Len();
	}
	return TextIndex;
}

int32 SEditableText::ToTextIndex(int32 DisplayIndex) const
{
	if (IsPassword.Get())
	{
		const char* Cursor = *EditedText;
		const char* const End = Cursor + EditedText.Len();
		for (int32 I = 0; I < DisplayIndex && Cursor < End; ++I)
		{
			FUtf8::Decode(Cursor, End);
		}
		return static_cast<int32>(Cursor - *EditedText);
	}
	if (bComposing && DisplayIndex > CompositionStart)
	{
		return DisplayIndex < CompositionStart + CompositionText.Len() ? CompositionStart : DisplayIndex - CompositionText.Len();
	}
	return DisplayIndex;
}

void SEditableText::UpdateLayout() const
{
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}
	// A bound Text follows its source while the user is not editing.
	if (!bComposing && !HasKeyboardFocus() && TextAttribute.IsBound() && !(TextAttribute.Get() == EditedText))
	{
		EditedText = TextAttribute.Get();
	}
	Layout.SetFont(GetFont());
	Layout.SetText(MakeDisplayText());
	Layout.SetWrapWidth(bAutoWrap && ArrangedWidth > 0.0f ? ArrangedWidth : 0.0f);
	Layout.UpdateIfNeeded(FSlateApplication::Get().GetFontCache());
}

FVector2 SEditableText::ComputeDesiredSize(float LayoutScale) const
{
	Layout.SetScale(LayoutScale);
	UpdateLayout();
	FVector2 Size = Layout.GetSize();
	// Room for the caret, and one line's height even when empty.
	Size.X += Style->CaretWidth + 1.0f;
	if (!Layout.GetLines().empty())
	{
		Size.Y = FMath::Max(Size.Y, Layout.GetLines()[0].Height);
	}
	Size.X = FMath::Max(Size.X, MinDesiredWidth.Get());
	return Size;
}

void SEditableText::OnPreArrange(const FGeometry& AllottedGeometry)
{
	if (bAutoWrap && !FMath::IsNearlyEqual(ArrangedWidth, AllottedGeometry.Size.X, 0.01f))
	{
		ArrangedWidth = AllottedGeometry.Size.X;
		Invalidate(EInvalidateWidgetReason::Layout);
	}
	EnsureCaretVisible();
}

void SEditableText::EnsureCaretVisible()
{
	const FVector2 View = GetCachedGeometry().Size;
	if (View.X <= 0.0f || Layout.GetLines().empty())
	{
		return;
	}
	const int32 DisplayCaret = bComposing ? CompositionStart + CompositionCaret : ToDisplayIndex(CaretIndex);
	const FRect Caret = Layout.GetCaretRect(DisplayCaret, View.X, Style->CaretWidth);
	FVector2 NewScroll = ScrollOffset;
	if (!bAutoWrap)
	{
		const float Margin = FMath::Min(20.0f, View.X * 0.25f);
		if (Caret.Left - NewScroll.X < 0.0f)
		{
			NewScroll.X = FMath::Max(0.0f, Caret.Left - Margin);
		}
		else if (Caret.Right - NewScroll.X > View.X)
		{
			NewScroll.X = Caret.Right - View.X + Margin;
		}
		NewScroll.X = FMath::Clamp(NewScroll.X, 0.0f, FMath::Max(0.0f, Layout.GetSize().X + Style->CaretWidth - View.X));
	}
	if (bMultiLine)
	{
		if (Caret.Top - NewScroll.Y < 0.0f)
		{
			NewScroll.Y = Caret.Top;
		}
		else if (Caret.Bottom - NewScroll.Y > View.Y)
		{
			NewScroll.Y = Caret.Bottom - View.Y;
		}
		NewScroll.Y = FMath::Clamp(NewScroll.Y, 0.0f, FMath::Max(0.0f, Layout.GetSize().Y - View.Y));
	}
	if (!(NewScroll == ScrollOffset))
	{
		ScrollOffset = NewScroll;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FIntRect SEditableText::GetCaretScreenRect() const
{
	const FGeometry& Geometry = GetCachedGeometry();
	const int32 DisplayCaret = bComposing ? CompositionStart + CompositionCaret : ToDisplayIndex(CaretIndex);
	const FRect Caret = Layout.GetCaretRect(DisplayCaret, Geometry.Size.X, Style->CaretWidth).Offset(-ScrollOffset);
	const FVector2 TopLeft = Geometry.LocalToAbsolute(Caret.GetTopLeft());
	const FVector2 BottomRight = Geometry.LocalToAbsolute(Caret.GetBottomRight());
	return FIntRect(FMath::FloorToInt(TopLeft.X), FMath::FloorToInt(TopLeft.Y), FMath::CeilToInt(BottomRight.X), FMath::CeilToInt(BottomRight.Y));
}

// ---------------------------------------------------------------------------
// Caret movement

int32 SEditableText::NextCaret(int32 Index) const
{
	UpdateLayout();
	return FMath::Min(EditedText.Len(), ToTextIndex(Layout.GetNextCaretIndex(ToDisplayIndex(Index))));
}

int32 SEditableText::PreviousCaret(int32 Index) const
{
	UpdateLayout();
	return FMath::Max(0, ToTextIndex(Layout.GetPreviousCaretIndex(ToDisplayIndex(Index))));
}

int32 SEditableText::NextWordBoundary(int32 Index) const
{
	if (IsPassword.Get())
	{
		return EditedText.Len();
	}
	const int32 Len = EditedText.Len();
	// Over the rest of this word (or this run of punctuation), then the spaces.
	if (Index < Len && IsWordByte(static_cast<unsigned char>(EditedText[Index])))
	{
		while (Index < Len && IsWordByte(static_cast<unsigned char>(EditedText[Index]))) ++Index;
	}
	else if (Index < Len && !IsSpaceByte(static_cast<unsigned char>(EditedText[Index])))
	{
		while (Index < Len && !IsWordByte(static_cast<unsigned char>(EditedText[Index])) && !IsSpaceByte(static_cast<unsigned char>(EditedText[Index]))) ++Index;
	}
	while (Index < Len && IsSpaceByte(static_cast<unsigned char>(EditedText[Index])) && EditedText[Index] != '\n') ++Index;
	return Index;
}

int32 SEditableText::PreviousWordBoundary(int32 Index) const
{
	if (IsPassword.Get())
	{
		return 0;
	}
	while (Index > 0 && IsSpaceByte(static_cast<unsigned char>(EditedText[Index - 1])) && EditedText[Index - 1] != '\n') --Index;
	if (Index > 0 && IsWordByte(static_cast<unsigned char>(EditedText[Index - 1])))
	{
		while (Index > 0 && IsWordByte(static_cast<unsigned char>(EditedText[Index - 1]))) --Index;
	}
	else if (Index > 0 && !IsSpaceByte(static_cast<unsigned char>(EditedText[Index - 1])))
	{
		while (Index > 0 && !IsWordByte(static_cast<unsigned char>(EditedText[Index - 1])) && !IsSpaceByte(static_cast<unsigned char>(EditedText[Index - 1]))) --Index;
	}
	else if (Index > 0)
	{
		--Index; // a line break
	}
	return Index;
}

TPair<int32, int32> SEditableText::WordAt(int32 Index) const
{
	if (IsPassword.Get())
	{
		return { 0, EditedText.Len() };
	}
	const int32 Len = EditedText.Len();
	Index = FMath::Clamp(Index, 0, Len);
	const auto Kind = [this](int32 I) { const unsigned char C = static_cast<unsigned char>(EditedText[I]); return IsWordByte(C) ? 0 : (IsSpaceByte(C) ? 1 : 2); };
	if (Len == 0)
	{
		return { 0, 0 };
	}
	const int32 Probe = Index < Len ? Index : Len - 1;
	const int32 K = Kind(Probe);
	int32 Begin = Probe, End = Probe;
	while (Begin > 0 && Kind(Begin - 1) == K) --Begin;
	while (End < Len && Kind(End) == K) ++End;
	return { Begin, End };
}

int32 SEditableText::LineStart(int32 Index) const
{
	UpdateLayout();
	const int32 Line = Layout.GetLineIndex(ToDisplayIndex(Index));
	return Line == INDEX_NONE ? 0 : ToTextIndex(Layout.GetLines()[Line].Begin);
}

int32 SEditableText::LineEnd(int32 Index) const
{
	UpdateLayout();
	const int32 Line = Layout.GetLineIndex(ToDisplayIndex(Index));
	return Line == INDEX_NONE ? EditedText.Len() : ToTextIndex(Layout.GetLines()[Line].End);
}

int32 SEditableText::VerticalMove(int32 Index, int32 Lines) const
{
	UpdateLayout();
	const TArray<FTextLine>& AllLines = Layout.GetLines();
	const int32 Line = Layout.GetLineIndex(ToDisplayIndex(Index));
	const int32 Target = Line + Lines;
	if (Line == INDEX_NONE || Target < 0)
	{
		return 0;
	}
	if (Target >= static_cast<int32>(AllLines.size()))
	{
		return EditedText.Len();
	}
	const float Width = GetCachedGeometry().Size.X;
	const float X = PreferredCaretX >= 0.0f ? PreferredCaretX : Layout.GetCaretX(ToDisplayIndex(Index), Width);
	const FTextLine& TargetLine = AllLines[Target];
	return ToTextIndex(Layout.HitTest(FVector2(X, TargetLine.Y + TargetLine.Height * 0.5f), Width));
}

int32 SEditableText::HitTestText(const FGeometry& Geometry, FVector2 AbsolutePosition) const
{
	UpdateLayout();
	const FVector2 Local = Geometry.AbsoluteToLocal(AbsolutePosition) + ScrollOffset;
	return ToTextIndex(Layout.HitTest(Local, Geometry.Size.X));
}

// ---------------------------------------------------------------------------
// Editing

void SEditableText::BeginEdit(EEditKind Kind)
{
	// Consecutive typing (or deleting) is one undo step.
	if (Kind == EEditKind::Other || Kind != LastEdit || UndoStack.empty())
	{
		UndoStack.push_back({ EditedText, CaretIndex, SelectionAnchor });
		if (static_cast<int32>(UndoStack.size()) > MaxUndo)
		{
			UndoStack.erase(UndoStack.begin());
		}
	}
	RedoStack.clear();
	LastEdit = Kind;
}

void SEditableText::ReplaceRange(int32 Begin, int32 End, const FString& With)
{
	EditedText = EditedText.Left(Begin) + With + EditedText.Mid(End);
	CaretIndex = Begin + With.Len();
	SelectionAnchor = INDEX_NONE;
	TextChanged();
}

void SEditableText::TextChanged()
{
	Invalidate(EInvalidateWidgetReason::Layout);
	OnCaretMoved();
	OnTextChanged.ExecuteIfBound(EditedText);
}

void SEditableText::InsertText(const FString& Text)
{
	if (IsReadOnly.Get())
	{
		return;
	}
	FString Clean = Text;
	if (!bMultiLine)
	{
		// One line: line breaks become spaces.
		std::string S = Clean.ToStdString();
		std::erase(S, '\r');
		std::replace(S.begin(), S.end(), '\n', ' ');
		Clean = FString(S);
	}
	// A single typed character joins the previous typing in one undo step.
	BeginEdit(FUtf8::CountCodePoints(Text.View()) == 1 ? EEditKind::Typing : EEditKind::Other);
	ReplaceRange(GetSelectionBegin(), GetSelectionEnd(), Clean);
}

void SEditableText::DeleteSelection()
{
	if (HasSelection() && !IsReadOnly.Get())
	{
		ReplaceRange(GetSelectionBegin(), GetSelectionEnd(), FString());
	}
}

bool SEditableText::Undo()
{
	if (UndoStack.empty() || IsReadOnly.Get())
	{
		return false;
	}
	RedoStack.push_back({ EditedText, CaretIndex, SelectionAnchor });
	const FUndoState State = UndoStack.back();
	UndoStack.pop_back();
	EditedText = State.Text;
	CaretIndex = State.Caret;
	SelectionAnchor = State.Anchor;
	LastEdit = EEditKind::Other;
	TextChanged();
	return true;
}

bool SEditableText::Redo()
{
	if (RedoStack.empty() || IsReadOnly.Get())
	{
		return false;
	}
	UndoStack.push_back({ EditedText, CaretIndex, SelectionAnchor });
	const FUndoState State = RedoStack.back();
	RedoStack.pop_back();
	EditedText = State.Text;
	CaretIndex = State.Caret;
	SelectionAnchor = State.Anchor;
	LastEdit = EEditKind::Other;
	TextChanged();
	return true;
}

void SEditableText::CopySelection() const
{
	if (HasSelection() && !IsPassword.Get())
	{
		FSlateApplication::Get().GetPlatformApplication().ClipboardCopy(GetSelectedText());
	}
}

void SEditableText::CutSelection()
{
	if (HasSelection() && !IsPassword.Get() && !IsReadOnly.Get())
	{
		CopySelection();
		BeginEdit(EEditKind::Other);
		DeleteSelection();
	}
}

void SEditableText::Paste()
{
	const FString Text = FSlateApplication::Get().GetPlatformApplication().ClipboardPaste();
	if (!Text.IsEmpty())
	{
		InsertText(Text);
	}
}

void SEditableText::Commit(ETextCommit How)
{
	if (!TextAttribute.IsBound())
	{
		TextAttribute.Set(EditedText);
	}
	OnTextCommitted.ExecuteIfBound(EditedText, How);
}

// ---------------------------------------------------------------------------
// Input

FReply SEditableText::OnKeyDown(const FGeometry&, const FKeyEvent& Event)
{
	if (bComposing || Event.IsGamepad())
	{
		return FReply::Unhandled();
	}
	const bool bShift = Event.IsShiftDown();
	const bool bControl = Event.IsControlDown() && !Event.IsAltDown();
	const EKey Key = Event.GetKey();
	const bool bReadOnly = IsReadOnly.Get();
	if (Key != EKey::Up && Key != EKey::Down && Key != EKey::PageUp && Key != EKey::PageDown)
	{
		PreferredCaretX = -1.0f;
	}

	switch (Key)
	{
	case EKey::Left:
		if (HasSelection() && !bShift)
		{
			SetCaret(GetSelectionBegin());
		}
		else
		{
			SetCaret(bControl ? PreviousWordBoundary(CaretIndex) : PreviousCaret(CaretIndex), bShift);
		}
		return FReply::Handled();
	case EKey::Right:
		if (HasSelection() && !bShift)
		{
			SetCaret(GetSelectionEnd());
		}
		else
		{
			SetCaret(bControl ? NextWordBoundary(CaretIndex) : NextCaret(CaretIndex), bShift);
		}
		return FReply::Handled();
	case EKey::Home:
		SetCaret(bControl || !bMultiLine ? 0 : LineStart(CaretIndex), bShift);
		return FReply::Handled();
	case EKey::End:
		SetCaret(bControl || !bMultiLine ? EditedText.Len() : LineEnd(CaretIndex), bShift);
		return FReply::Handled();
	case EKey::Up:
	case EKey::Down:
	case EKey::PageUp:
	case EKey::PageDown:
	{
		if (!bMultiLine)
		{
			return FReply::Unhandled(); // navigation
		}
		if (PreferredCaretX < 0.0f)
		{
			PreferredCaretX = Layout.GetCaretX(ToDisplayIndex(CaretIndex), GetCachedGeometry().Size.X);
		}
		const int32 Page = Layout.GetLines().empty() ? 1 : FMath::Max(1, FMath::FloorToInt(GetCachedGeometry().Size.Y / Layout.GetLines()[0].Height));
		const int32 Lines = Key == EKey::Up ? -1 : (Key == EKey::Down ? 1 : (Key == EKey::PageUp ? -Page : Page));
		const float KeepX = PreferredCaretX;
		SetCaret(VerticalMove(CaretIndex, Lines), bShift);
		PreferredCaretX = KeepX;
		return FReply::Handled();
	}
	case EKey::BackSpace:
		if (bReadOnly)
		{
			return FReply::Handled();
		}
		if (HasSelection())
		{
			BeginEdit(EEditKind::Other);
			DeleteSelection();
		}
		else if (CaretIndex > 0)
		{
			BeginEdit(bControl ? EEditKind::Other : EEditKind::Deleting);
			ReplaceRange(bControl ? PreviousWordBoundary(CaretIndex) : PreviousCaret(CaretIndex), CaretIndex, FString());
		}
		return FReply::Handled();
	case EKey::Delete:
		if (bReadOnly)
		{
			return FReply::Handled();
		}
		if (HasSelection())
		{
			BeginEdit(EEditKind::Other);
			DeleteSelection();
		}
		else if (CaretIndex < EditedText.Len())
		{
			BeginEdit(bControl ? EEditKind::Other : EEditKind::Deleting);
			ReplaceRange(CaretIndex, bControl ? NextWordBoundary(CaretIndex) : NextCaret(CaretIndex), FString());
		}
		return FReply::Handled();
	case EKey::Enter:
		if (bMultiLine && !bControl)
		{
			if (!bReadOnly)
			{
				BeginEdit(EEditKind::Other);
				ReplaceRange(GetSelectionBegin(), GetSelectionEnd(), FString("\n"));
			}
			return FReply::Handled();
		}
		Commit(ETextCommit::OnEnter);
		TextWhenFocused = EditedText;
		if (bClearFocusOnCommit)
		{
			return FReply::Handled().ClearUserFocus();
		}
		if (bSelectAllWhenFocused)
		{
			SelectAll();
		}
		return FReply::Handled();
	case EKey::Escape:
		if (bRevertOnEscape && !(EditedText == TextWhenFocused))
		{
			BeginEdit(EEditKind::Other);
			EditedText = TextWhenFocused;
			CaretIndex = EditedText.Len();
			SelectionAnchor = INDEX_NONE;
			TextChanged();
			Commit(ETextCommit::OnCleared);
			return FReply::Handled();
		}
		return FReply::Unhandled();
	default:
		break;
	}

	if (bControl)
	{
		switch (Key)
		{
		case EKey::A: SelectAll(); return FReply::Handled();
		case EKey::C: CopySelection(); return FReply::Handled();
		case EKey::X: CutSelection(); return FReply::Handled();
		case EKey::V:
			if (!bReadOnly)
			{
				Paste();
			}
			return FReply::Handled();
		case EKey::Z:
			if (bShift)
			{
				Redo();
			}
			else
			{
				Undo();
			}
			return FReply::Handled();
		case EKey::Y: Redo(); return FReply::Handled();
		case EKey::Insert: CopySelection(); return FReply::Handled();
		default: break;
		}
	}
	if (bShift && Key == EKey::Insert && !bReadOnly)
	{
		Paste();
		return FReply::Handled();
	}
	if (bShift && Key == EKey::Delete && !bReadOnly)
	{
		CutSelection();
		return FReply::Handled();
	}
	// Printable keys arrive as OnKeyChar; claim them so they do not trigger
	// shortcuts elsewhere while typing.
	const uint32 Code = static_cast<uint32>(Key);
	const bool bPrintable = (Code >= '0' && Code <= '9') || (Code >= 'A' && Code <= 'Z') || Key == EKey::SpaceBar
		|| (Code >= static_cast<uint32>(EKey::Semicolon) && Code <= static_cast<uint32>(EKey::Tilde))
		|| (Code >= static_cast<uint32>(EKey::LeftBracket) && Code <= static_cast<uint32>(EKey::Apostrophe))
		|| (Code >= static_cast<uint32>(EKey::NumPad0) && Code <= static_cast<uint32>(EKey::Divide));
	return bPrintable && !bControl && !Event.IsAltDown() ? FReply::Handled() : FReply::Unhandled();
}

FReply SEditableText::OnKeyChar(const FGeometry&, const FCharacterEvent& Event)
{
	const char32_t C = Event.GetCharacter();
	// Control characters come as key presses; Ctrl+letter is a shortcut (but
	// AltGr, reported as Ctrl+Alt, types).
	if (C < 0x20 || C == 0x7F || (Event.IsControlDown() && !Event.IsAltDown()))
	{
		return FReply::Unhandled();
	}
	if (IsReadOnly.Get())
	{
		return FReply::Handled();
	}
	std::string Utf8;
	FUtf8::Encode(C, Utf8);
	InsertText(FString(Utf8));
	return FReply::Handled();
}

FReply SEditableText::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EMouseButton::Left)
	{
		return FReply::Unhandled();
	}
	const bool bHadFocus = HasKeyboardFocus();
	bSelectAllOnMouseUp = !bHadFocus && bSelectAllWhenFocused;
	bHasDragged = false;
	PreferredCaretX = -1.0f;
	if (!bComposing)
	{
		SetCaret(HitTestText(Geometry, Event.GetAbsolutePosition()), Event.IsShiftDown() && bHadFocus);
	}
	bDragSelecting = true;
	FReply Reply = FReply::Handled().CaptureMouse(SharedThis(this));
	if (!bHadFocus)
	{
		Reply.SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	}
	return Reply;
}

FReply SEditableText::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!bDragSelecting || !HasMouseCapture() || bComposing)
	{
		return FReply::Unhandled();
	}
	const int32 Index = HitTestText(Geometry, Event.GetAbsolutePosition());
	if (Index != CaretIndex)
	{
		bHasDragged = true;
		SetCaret(Index, true);
	}
	return FReply::Handled();
}

FReply SEditableText::OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EMouseButton::Left || !bDragSelecting)
	{
		return FReply::Unhandled();
	}
	bDragSelecting = false;
	if (bSelectAllOnMouseUp && !bHasDragged)
	{
		SelectAll();
	}
	bSelectAllOnMouseUp = false;
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SEditableText::OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EMouseButton::Left || bComposing)
	{
		return FReply::Unhandled();
	}
	const auto [Begin, End] = WordAt(HitTestText(Geometry, Event.GetAbsolutePosition()));
	SelectionAnchor = Begin;
	CaretIndex = End;
	OnCaretMoved();
	bDragSelecting = false;
	bSelectAllOnMouseUp = false;
	return FReply::Handled();
}

FCursorReply SEditableText::OnCursorQuery(const FGeometry&, const FPointerEvent&) const
{
	return FCursorReply::Cursor(EMouseCursor::TextEditBeam);
}

FReply SEditableText::OnFocusReceived(const FGeometry&, const FFocusEvent& Event)
{
	TextWhenFocused = EditedText;
	if (bSelectAllWhenFocused && Event.GetCause() != EFocusCause::Mouse)
	{
		SelectAll();
	}
	StartBlinking();
	if (!IsPassword.Get() && !IsReadOnly.Get())
	{
		if (ITextInputMethodSystem* Ime = FSlateApplication::Get().GetPlatformApplication().GetTextInputMethodSystem())
		{
			Ime->ActivateContext(TSharedRef<ITextInputMethodContext>(StaticCastSharedPtr<ITextInputMethodContext>(InputContext)));
		}
	}
	return FReply::Handled();
}

void SEditableText::OnFocusLost(const FFocusEvent&)
{
	if (ITextInputMethodSystem* Ime = FSlateApplication::Get().GetPlatformApplication().GetTextInputMethodSystem())
	{
		Ime->DeactivateContext(TSharedRef<ITextInputMethodContext>(StaticCastSharedPtr<ITextInputMethodContext>(InputContext)));
	}
	if (bComposing)
	{
		InputContext->EndComposition();
	}
	StopBlinking();
	bDragSelecting = false;
	Commit(ETextCommit::OnUserMovedFocus);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SEditableText::StartBlinking()
{
	LastCaretActivity = FSlateApplication::Get().GetTime();
	if (!BlinkTimer)
	{
		BlinkTimer = RegisterActiveTimer(static_cast<float>(CaretBlinkPeriod), [this](double, float)
		{
			Invalidate(EInvalidateWidgetReason::Paint);
			return EActiveTimerReturn::Continue;
		}).ToSharedPtr();
	}
}

void SEditableText::StopBlinking()
{
	if (BlinkTimer)
	{
		UnRegisterActiveTimer(ToSharedRef(BlinkTimer));
		BlinkTimer.reset();
	}
}

bool SEditableText::IsCaretVisibleNow() const
{
	if (!FSlateApplication::IsInitialized())
	{
		return true;
	}
	const double Since = FSlateApplication::Get().GetTime() - LastCaretActivity;
	return FMath::Fmod(static_cast<float>(Since), static_cast<float>(CaretBlinkPeriod * 2.0)) < static_cast<float>(CaretBlinkPeriod);
}

// ---------------------------------------------------------------------------
// Paint

int32 SEditableText::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect&, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	if (!FSlateApplication::IsInitialized())
	{
		return LayerId;
	}
	UpdateLayout();
	FSlateFontCache& FontCache = FSlateApplication::Get().GetFontCache();
	const float Width = Geometry.Size.X;
	const FVector2 Offset = -ScrollOffset;
	const bool bFocused = HasKeyboardFocus();

	// Selection.
	if (HasSelection())
	{
		TArray<FRect> Rects;
		Layout.GetSelectionRects(ToDisplayIndex(GetSelectionBegin()), ToDisplayIndex(GetSelectionEnd()), Width, Rects);
		const FColor Color = InWidgetStyle.Tinted((bFocused ? Style->SelectionColor : Style->InactiveSelectionColor).Resolve());
		for (const FRect& Rect : Rects)
		{
			OutDrawElements.AddSolidBox(LayerId, Geometry.ToPaintGeometry(Rect.Offset(Offset)), Color);
		}
	}

	// Text, or the hint when empty.
	if (EditedText.IsEmpty() && !bComposing)
	{
		const FString& Hint = HintText.Get();
		if (!Hint.IsEmpty())
		{
			FTextLayout HintLayout;
			HintLayout.SetFont(GetFont());
			HintLayout.SetText(Hint);
			HintLayout.SetScale(Geometry.ToPaintGeometry().GetScale());
			HintLayout.UpdateIfNeeded(FontCache);
			HintLayout.Paint(FontCache, OutDrawElements, LayerId + 1, Geometry, Offset, Width, InWidgetStyle.Tinted(Style->HintColor.Resolve()));
		}
	}
	else
	{
		const FColor Color = Args.bEnabled ? InWidgetStyle.Tinted(Style->Color.Resolve()) : InWidgetStyle.Tinted(FSlateColor(EStyleColor::ForegroundDisabled).Resolve());
		Layout.Paint(FontCache, OutDrawElements, LayerId + 1, Geometry, Offset, Width, Color);
	}

	// Composition underline.
	if (bComposing && !CompositionText.IsEmpty())
	{
		TArray<FRect> Rects;
		Layout.GetSelectionRects(CompositionStart, CompositionStart + CompositionText.Len(), Width, Rects);
		const FColor Color = InWidgetStyle.Tinted(Style->CompositionUnderline.Resolve());
		for (const FRect& Rect : Rects)
		{
			OutDrawElements.AddSolidBox(LayerId + 1, Geometry.ToPaintGeometry(FRect(Rect.Left, Rect.Bottom - 2.0f, Rect.Right, Rect.Bottom - 1.0f).Offset(Offset)), Color);
		}
	}

	// Caret.
	if (bFocused && Args.bEnabled && IsCaretVisibleNow())
	{
		const int32 DisplayCaret = bComposing ? CompositionStart + CompositionCaret : ToDisplayIndex(CaretIndex);
		const FRect Caret = Layout.GetCaretRect(DisplayCaret, Width, Style->CaretWidth);
		OutDrawElements.AddSolidBox(LayerId + 2, Geometry.ToPaintGeometry(Caret.Offset(Offset)), InWidgetStyle.Tinted(Style->CaretColor.Resolve()));
	}
	return LayerId + 2;
}

// ---------------------------------------------------------------------------
// SEditableTextBox

void SEditableTextBox::Construct(const FArguments& Args)
{
	Style = Args._Style ? Args._Style : &FAppStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("EditableTextBox");
	ChildSlot.Padding(Args._Padding.IsSet() ? Args._Padding.Get() : Style->Padding).VAlign(Args._IsMultiLine ? VAlign_Fill : VAlign_Center)
	[
		SAssignNew(EditableText, SEditableText)
		.Style(&Style->Text)
		.Text(Args._Text)
		.HintText(Args._HintText)
		.IsReadOnly(Args._IsReadOnly)
		.IsPassword(Args._IsPassword)
		.IsMultiLine(Args._IsMultiLine)
		.AutoWrapText(Args._AutoWrapText)
		.SelectAllTextWhenFocused(Args._SelectAllTextWhenFocused)
		.RevertTextOnEscape(Args._RevertTextOnEscape)
		.ClearKeyboardFocusOnCommit(Args._ClearKeyboardFocusOnCommit)
		.MinDesiredWidth(Args._MinDesiredWidth)
		.OnTextChanged(Args._OnTextChanged)
		.OnTextCommitted(Args._OnTextCommitted)
	];
}

FReply SEditableTextBox::OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event)
{
	// Clicks on the frame's padding focus the text.
	if (Event.GetEffectingButton() == EMouseButton::Left && !EditableText->HasKeyboardFocus())
	{
		return FReply::Handled().SetUserFocus(ToSharedRef(StaticCastSharedPtr<SWidget>(EditableText)), EFocusCause::Mouse);
	}
	return FReply::Unhandled();
}

int32 SEditableTextBox::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	const FSlateBrush& Brush = EditableText->IsTextReadOnly() ? Style->ReadOnly
		: (EditableText->HasKeyboardFocus() ? Style->Focused : (IsHovered() ? Style->Hovered : Style->Normal));
	Brush.Paint(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), InWidgetStyle.ColorAndOpacityTint);
	return PaintArrangedChildren(Args, CullingRect, OutDrawElements, LayerId + 1, MakeChildStyle(InWidgetStyle));
}
