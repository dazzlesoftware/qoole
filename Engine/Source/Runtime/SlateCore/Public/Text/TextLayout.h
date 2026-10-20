// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// FTextLayout (docs/GenesisUI.md §8): text -> lines, with word wrapping,
// justification and hit-testing (point -> caret index, caret index ->
// rectangle, selection rectangles). Each line is shaped by the FFontCache.
//
// Indices are byte offsets into the UTF-8 text. Caret stops are at shaping
// cluster boundaries, so a caret never lands inside a conjunct or a base +
// combining-mark pair. All geometry is in logical units relative to the
// layout's top-left corner. Left-to-right text only for now.

#pragma once

#include "Fonts/FontCache.h"
#include "Layout/Geometry.h"
#include "Types/WidgetTypes.h"

struct FTextCaretStop
{
	int32 Index = 0; // byte offset into the full text
	float X = 0.0f;  // logical, from the line start
};

struct FTextLine
{
	int32 Begin = 0;  // byte range in the text; End excludes the line break
	int32 End = 0;
	float Y = 0.0f;   // top of the line
	float Width = 0.0f;
	float Height = 0.0f;
	float Baseline = 0.0f; // from the top of the line
	bool bEndsParagraph = true; // false for soft (wrapped) line ends
	TArray<FTextCaretStop> Stops; // ascending; the last one is End
};

class FTextLayout
{
public:
	void SetText(const FString& InText);
	void SetFont(const FFontInfo& InFont);
	// Lines wrap at word boundaries to fit this width; <= 0 disables wrapping.
	void SetWrapWidth(float InWrapWidth);
	void SetJustification(ETextJustify InJustify);
	// The DPI/layout scale text is shaped at (metrics depend on it).
	void SetScale(float InScale);

	const FString& GetText() const { return Text; }
	const FFontInfo& GetFont() const { return Font; }
	float GetWrapWidth() const { return WrapWidth; }
	float GetScale() const { return Scale; }

	// Lays the text out again if anything changed.
	void UpdateIfNeeded(FFontCache& FontCache);
	bool IsDirty() const { return bDirty; }

	// Size of the laid-out text (widest line x total height).
	FVector2 GetSize() const { return Size; }
	const TArray<FTextLine>& GetLines() const { return Lines; }
	// Horizontal offset of a line inside an area of AreaWidth (justification).
	float GetLineOffsetX(const FTextLine& Line, float AreaWidth) const;

	// Caret index nearest to a local point.
	int32 HitTest(FVector2 LocalPoint, float AreaWidth) const;
	// The line a caret index is on.
	int32 GetLineIndex(int32 Index) const;
	// Caret rectangle (CaretWidth wide, line height tall).
	FRect GetCaretRect(int32 Index, float AreaWidth, float CaretWidth = 1.0f) const;
	// X of a caret index on its line (local, including justification).
	float GetCaretX(int32 Index, float AreaWidth) const;
	// Rectangles covering [Begin, End).
	void GetSelectionRects(int32 Begin, int32 End, float AreaWidth, TArray<FRect>& OutRects) const;

	// Neighbouring caret stops (cluster boundaries), across lines.
	int32 GetNextCaretIndex(int32 Index) const;
	int32 GetPreviousCaretIndex(int32 Index) const;
	// Snaps to the closest caret stop at or before Index.
	int32 SnapToCaretStop(int32 Index) const;

	// Draws the lines with their top-left at Offset in Geometry's local space.
	// Text is shaped at the geometry's paint scale.
	void Paint(FFontCache& FontCache, FDrawElementList& Out, int32 Layer, const FGeometry& Geometry, FVector2 Offset, float AreaWidth, FColor Color) const;

private:
	void Layout(FFontCache& FontCache);
	void AddLine(FFontCache& FontCache, int32 Begin, int32 End, bool bEndsParagraph, float& Y);
	const FTextLine* FindLine(int32 Index) const;

	FString Text;
	FFontInfo Font;
	float WrapWidth = 0.0f;
	float Scale = 1.0f;
	ETextJustify Justify = ETextJustify::Left;
	bool bDirty = true;

	TArray<FTextLine> Lines;
	FVector2 Size;
};
