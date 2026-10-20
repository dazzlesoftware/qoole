// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Editable text (Engine/Documentation/Source/GenesisUI.md §8): caret, selection, word movement,
// undo/redo, clipboard, horizontal scrolling, password masking, optional
// multiple lines, and IME composition drawn inline at the caret.
//
// Keys: arrows (+Shift selects, +Ctrl by word), Home/End, Backspace/Delete
// (+Ctrl by word), Ctrl+A/C/X/V, Ctrl+Z, Ctrl+Y or Ctrl+Shift+Z, Enter
// commits (multi-line: inserts a line; Ctrl+Enter commits), Escape reverts
// when RevertTextOnEscape is set.

#pragma once

#include "Styling/SlateStyle.h"
#include "Text/TextLayout.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/WidgetDelegates.h"

class FEditableTextInputContext;

class SEditableText : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SEditableText)
		SLATE_ATTRIBUTE(FString, Text, "")
		SLATE_ATTRIBUTE(FString, HintText, "")
		SLATE_STYLE_ARGUMENT(FEditableTextStyle, Style)
		SLATE_OPTIONAL_ATTRIBUTE(FSlateFontInfo, Font)
		SLATE_ATTRIBUTE(bool, IsReadOnly, false)
		SLATE_ATTRIBUTE(bool, IsPassword, false)
		SLATE_ARGUMENT(bool, IsMultiLine, false)
		SLATE_ARGUMENT(bool, AutoWrapText, false) // multi-line only
		SLATE_ARGUMENT(bool, SelectAllTextWhenFocused, false)
		SLATE_ARGUMENT(bool, RevertTextOnEscape, false)
		SLATE_ARGUMENT(bool, ClearKeyboardFocusOnCommit, false)
		SLATE_ATTRIBUTE(float, MinDesiredWidth, 0.0f)
		SLATE_EVENT(FOnTextChanged, OnTextChanged)
		SLATE_EVENT(FOnTextCommitted, OnTextCommitted)
	SLATE_END_ARGS()

	SEditableText();
	~SEditableText() override;

	void Construct(const FArguments& Args);

	// --- Text and selection ---------------------------------------------------

	const FString& GetText() const { return EditedText; }
	void SetText(TAttribute<FString> InText);
	void SetHintText(TAttribute<FString> InHint) { HintText.Assign(InHint); }
	void SetIsReadOnly(TAttribute<bool> InReadOnly) { IsReadOnly.Assign(InReadOnly); }
	bool IsTextReadOnly() const { return IsReadOnly.Get(); }
	bool IsTextPassword() const { return IsPassword.Get(); }

	int32 GetCaretIndex() const { return CaretIndex; }
	// Selection as [Begin, End); empty when nothing is selected.
	int32 GetSelectionBegin() const { return HasSelection() ? FMath::Min(SelectionAnchor, CaretIndex) : CaretIndex; }
	int32 GetSelectionEnd() const { return HasSelection() ? FMath::Max(SelectionAnchor, CaretIndex) : CaretIndex; }
	bool HasSelection() const { return SelectionAnchor != INDEX_NONE && SelectionAnchor != CaretIndex; }
	FString GetSelectedText() const;
	void SelectAll();
	void ClearSelection();
	void SetCaret(int32 Index, bool bExtendSelection = false);

	// Editing as the keyboard would (respects read-only; records undo).
	void InsertText(const FString& Text);
	void DeleteSelection();
	bool Undo();
	bool Redo();
	void CopySelection() const;
	void CutSelection();
	void Paste();

	// --- IME ---------------------------------------------------------------------

	bool IsComposing() const { return bComposing; }
	const FString& GetCompositionText() const { return CompositionText; }
	// Window pixels.
	FIntRect GetCaretScreenRect() const;

	// --- SWidget -------------------------------------------------------------------

	bool SupportsKeyboardFocus() const override { return true; }
	FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	FReply OnKeyChar(const FGeometry& Geometry, const FCharacterEvent& Event) override;
	FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	void OnMouseCaptureLost() override { bDragSelecting = false; }
	FCursorReply OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const override;
	FReply OnFocusReceived(const FGeometry& Geometry, const FFocusEvent& Event) override;
	void OnFocusLost(const FFocusEvent& Event) override;
	EWindowZone GetWindowZoneOverride() const override { return EWindowZone::ClientArea; }

protected:
	FVector2 ComputeDesiredSize(float LayoutScale) const override;
	void OnPreArrange(const FGeometry& AllottedGeometry) override;
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	friend class FEditableTextInputContext;

	struct FUndoState
	{
		FString Text;
		int32 Caret = 0;
		int32 Anchor = INDEX_NONE;
	};
	enum class EEditKind : uint8
	{
		Other,
		Typing,
		Deleting,
	};

	// The text as laid out: masked for passwords, with the composition inserted.
	FString MakeDisplayText() const;
	int32 ToDisplayIndex(int32 TextIndex) const;
	int32 ToTextIndex(int32 DisplayIndex) const;
	void UpdateLayout() const;
	FSlateFontInfo GetFont() const;

	int32 NextCaret(int32 Index) const;
	int32 PreviousCaret(int32 Index) const;
	int32 NextWordBoundary(int32 Index) const;
	int32 PreviousWordBoundary(int32 Index) const;
	TPair<int32, int32> WordAt(int32 Index) const;
	int32 LineStart(int32 Index) const;
	int32 LineEnd(int32 Index) const;
	int32 VerticalMove(int32 Index, int32 Lines) const;
	int32 HitTestText(const FGeometry& Geometry, FVector2 AbsolutePosition) const;

	void BeginEdit(EEditKind Kind);
	void ReplaceRange(int32 Begin, int32 End, const FString& With);
	void TextChanged();
	void Commit(ETextCommit How);
	void OnCaretMoved();
	void EnsureCaretVisible();
	bool IsCaretVisibleNow() const;
	void StartBlinking();
	void StopBlinking();

	TWidgetAttribute<FString, EInvalidateWidgetReason::Layout> TextAttribute{ *this };
	TWidgetAttribute<FString, EInvalidateWidgetReason::Paint> HintText{ *this };
	TWidgetAttribute<bool, EInvalidateWidgetReason::Paint> IsReadOnly{ *this, false };
	TWidgetAttribute<bool, EInvalidateWidgetReason::Layout> IsPassword{ *this, false };
	TWidgetAttribute<float, EInvalidateWidgetReason::Layout> MinDesiredWidth{ *this, 0.0f };
	const FEditableTextStyle* Style = nullptr;
	TOptional<FSlateFontInfo> FontOverride;
	bool bMultiLine = false;
	bool bAutoWrap = false;
	bool bSelectAllWhenFocused = false;
	bool bRevertOnEscape = false;
	bool bClearFocusOnCommit = false;
	FOnTextChanged OnTextChanged;
	FOnTextCommitted OnTextCommitted;

	mutable FString EditedText;
	FString TextWhenFocused;
	int32 CaretIndex = 0;
	int32 SelectionAnchor = INDEX_NONE;
	float PreferredCaretX = -1.0f; // keeps the column when moving up/down

	TArray<FUndoState> UndoStack;
	TArray<FUndoState> RedoStack;
	EEditKind LastEdit = EEditKind::Other;

	bool bComposing = false;
	FString CompositionText;
	int32 CompositionStart = 0;
	int32 CompositionCaret = 0; // bytes into CompositionText

	bool bDragSelecting = false;
	bool bSelectAllOnMouseUp = false;
	bool bHasDragged = false;
	FVector2 ScrollOffset;        // logical units
	double LastCaretActivity = 0.0;
	TSharedPtr<FActiveTimerHandle> BlinkTimer;
	TSharedPtr<FEditableTextInputContext> InputContext;
	float ArrangedWidth = 0.0f;

	mutable FTextLayout Layout;
};

// Editable text inside a frame that shows hover and focus.
class SEditableTextBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEditableTextBox)
		SLATE_STYLE_ARGUMENT(FEditableTextBoxStyle, Style)
		SLATE_ATTRIBUTE(FString, Text, "")
		SLATE_ATTRIBUTE(FString, HintText, "")
		SLATE_ATTRIBUTE(bool, IsReadOnly, false)
		SLATE_ATTRIBUTE(bool, IsPassword, false)
		SLATE_ARGUMENT(bool, IsMultiLine, false)
		SLATE_ARGUMENT(bool, AutoWrapText, false)
		SLATE_ARGUMENT(bool, SelectAllTextWhenFocused, false)
		SLATE_ARGUMENT(bool, RevertTextOnEscape, false)
		SLATE_ARGUMENT(bool, ClearKeyboardFocusOnCommit, false)
		SLATE_ATTRIBUTE(float, MinDesiredWidth, 0.0f)
		SLATE_OPTIONAL_ATTRIBUTE(FMargin, Padding)
		SLATE_EVENT(FOnTextChanged, OnTextChanged)
		SLATE_EVENT(FOnTextCommitted, OnTextCommitted)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

	const FString& GetText() const { return EditableText->GetText(); }
	void SetText(TAttribute<FString> InText) { EditableText->SetText(InText); }
	TSharedRef<SEditableText> GetEditableText() const { return ToSharedRef(EditableText); }

	FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FCursorReply OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const override { (void)Geometry; (void)Event; return FCursorReply::Cursor(EMouseCursor::TextEditBeam); }

protected:
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle) const override;

private:
	const FEditableTextBoxStyle* Style = nullptr;
	TSharedPtr<SEditableText> EditableText;
};
