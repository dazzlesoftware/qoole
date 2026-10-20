// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Text/TextLayout.h"

namespace
{
	// Caret stops of a shaped run: the first glyph of each cluster, plus the end.
	void BuildStops(const FShapedGlyphSequence& Shaped, int32 Begin, int32 End, TArray<FTextCaretStop>& Out)
	{
		Out.clear();
		int32 Last = -1;
		for (const FShapedGlyphEntry& Glyph : Shaped.Glyphs)
		{
			const int32 Index = Begin + static_cast<int32>(Glyph.Cluster);
			if (Index > Last && Index < End)
			{
				Out.push_back({ Index, Glyph.X / Shaped.Scale });
				Last = Index;
			}
		}
		if (Out.empty() || Out.front().Index != Begin)
		{
			Out.insert(Out.begin(), FTextCaretStop{ Begin, 0.0f });
		}
		Out.push_back({ End, Shaped.Width / Shaped.Scale });
	}

	float StopX(const TArray<FTextCaretStop>& Stops, int32 Index)
	{
		float X = 0.0f;
		for (const FTextCaretStop& Stop : Stops)
		{
			if (Stop.Index > Index)
			{
				break;
			}
			X = Stop.X;
		}
		return X;
	}

	bool IsBreakSpace(char C) { return C == ' ' || C == '\t'; }
}

void FTextLayout::SetText(const FString& InText)
{
	if (!(Text == InText))
	{
		Text = InText;
		bDirty = true;
	}
}

void FTextLayout::SetFont(const FSlateFontInfo& InFont)
{
	if (Font.Font != InFont.Font || Font.Size != InFont.Size)
	{
		Font = InFont;
		bDirty = true;
	}
}

void FTextLayout::SetWrapWidth(float InWrapWidth)
{
	if (WrapWidth != InWrapWidth)
	{
		WrapWidth = InWrapWidth;
		bDirty = true;
	}
}

void FTextLayout::SetJustification(ETextJustify InJustify)
{
	Justify = InJustify;
}

void FTextLayout::SetScale(float InScale)
{
	if (Scale != InScale && InScale > 0.0f)
	{
		Scale = InScale;
		bDirty = true;
	}
}

void FTextLayout::UpdateIfNeeded(FSlateFontCache& FontCache)
{
	if (bDirty)
	{
		Layout(FontCache);
		bDirty = false;
	}
}

void FTextLayout::AddLine(FSlateFontCache& FontCache, int32 Begin, int32 End, bool bEndsParagraph, float& Y)
{
	const std::string_view View = Text.View().substr(static_cast<SIZE_T>(Begin), static_cast<SIZE_T>(End - Begin));
	const TSharedRef<const FShapedGlyphSequence> Shaped = FontCache.ShapeText(View, Font, Scale);
	FTextLine& Line = Lines.emplace_back();
	Line.Begin = Begin;
	Line.End = End;
	Line.Y = Y;
	Line.Height = Shaped->LineHeight / Scale;
	Line.Baseline = Shaped->Ascender / Scale;
	Line.bEndsParagraph = bEndsParagraph;
	BuildStops(*Shaped, Begin, End, Line.Stops);
	// Trailing spaces of a soft-wrapped line do not count towards its width.
	int32 VisibleEnd = End;
	if (!bEndsParagraph)
	{
		while (VisibleEnd > Begin && IsBreakSpace(Text[VisibleEnd - 1]))
		{
			--VisibleEnd;
		}
	}
	Line.Width = VisibleEnd == End ? Shaped->Width / Scale : StopX(Line.Stops, VisibleEnd);
	Y += Line.Height;
}

void FTextLayout::Layout(FSlateFontCache& FontCache)
{
	Lines.clear();
	Size = FVector2::Zero();
	if (!Font.Font || Font.Font->Faces.empty())
	{
		return;
	}

	const std::string_view All = Text.View();
	float Y = 0.0f;
	int32 ParagraphBegin = 0;
	while (true)
	{
		SIZE_T NewLine = All.find('\n', static_cast<SIZE_T>(ParagraphBegin));
		const bool bLast = NewLine == std::string_view::npos;
		int32 ParagraphEnd = bLast ? static_cast<int32>(All.size()) : static_cast<int32>(NewLine);
		int32 ContentEnd = ParagraphEnd;
		if (ContentEnd > ParagraphBegin && All[ContentEnd - 1] == '\r')
		{
			--ContentEnd;
		}

		if (WrapWidth <= 0.0f || ContentEnd == ParagraphBegin)
		{
			AddLine(FontCache, ParagraphBegin, ContentEnd, true, Y);
		}
		else
		{
			// Shape the paragraph once and break it greedily at spaces using
			// its caret stops; each resulting line is then shaped on its own.
			const TSharedRef<const FShapedGlyphSequence> Shaped = FontCache.ShapeText(All.substr(ParagraphBegin, ContentEnd - ParagraphBegin), Font, Scale);
			TArray<FTextCaretStop> Stops;
			BuildStops(*Shaped, ParagraphBegin, ContentEnd, Stops);

			int32 LineBegin = ParagraphBegin;
			float LineStartX = 0.0f;
			int32 LastBreak = INDEX_NONE; // index after a run of spaces
			for (SIZE_T S = 1; S < Stops.size(); ++S)
			{
				// Stop S ends the cluster that starts at stop S - 1.
				const int32 Index = Stops[S].Index;
				const int32 PrevIndex = Stops[S - 1].Index;
				if (!IsBreakSpace(All[PrevIndex]) && Stops[S].X - LineStartX > WrapWidth && PrevIndex > LineBegin)
				{
					// Wrap at the last space, or inside the word when it alone
					// is too long.
					const int32 BreakAt = LastBreak != INDEX_NONE ? LastBreak : PrevIndex;
					AddLine(FontCache, LineBegin, BreakAt, false, Y);
					LineBegin = BreakAt;
					LineStartX = StopX(Stops, BreakAt);
					LastBreak = INDEX_NONE;
				}
				if (Index < ContentEnd && Index > LineBegin && IsBreakSpace(All[Index - 1]) && !IsBreakSpace(All[Index]))
				{
					LastBreak = Index;
				}
			}
			AddLine(FontCache, LineBegin, ContentEnd, true, Y);
		}

		if (bLast)
		{
			break;
		}
		ParagraphBegin = ParagraphEnd + 1;
	}

	for (const FTextLine& Line : Lines)
	{
		Size.X = FMath::Max(Size.X, Line.Width);
	}
	Size.Y = Y;
}

float FTextLayout::GetLineOffsetX(const FTextLine& Line, float AreaWidth) const
{
	switch (Justify)
	{
	case ETextJustify::Center: return FMath::Max(0.0f, (AreaWidth - Line.Width) * 0.5f);
	case ETextJustify::Right: return FMath::Max(0.0f, AreaWidth - Line.Width);
	default: return 0.0f;
	}
}

const FTextLine* FTextLayout::FindLine(int32 Index) const
{
	const int32 LineIndex = GetLineIndex(Index);
	return LineIndex == INDEX_NONE ? nullptr : &Lines[LineIndex];
}

int32 FTextLayout::GetLineIndex(int32 Index) const
{
	if (Lines.empty())
	{
		return INDEX_NONE;
	}
	// The last line starting at or before Index. A caret at the end of a
	// soft-wrapped line belongs to the next line (where typing continues).
	int32 Result = 0;
	for (int32 I = 0; I < static_cast<int32>(Lines.size()); ++I)
	{
		if (Lines[I].Begin <= Index)
		{
			Result = I;
		}
	}
	return Result;
}

int32 FTextLayout::HitTest(FVector2 LocalPoint, float AreaWidth) const
{
	if (Lines.empty())
	{
		return 0;
	}
	int32 LineIndex = static_cast<int32>(Lines.size()) - 1;
	for (int32 I = 0; I < static_cast<int32>(Lines.size()); ++I)
	{
		if (LocalPoint.Y < Lines[I].Y + Lines[I].Height)
		{
			LineIndex = I;
			break;
		}
	}
	const FTextLine& Line = Lines[LineIndex];
	const float X = LocalPoint.X - GetLineOffsetX(Line, AreaWidth);
	const TArray<FTextCaretStop>& Stops = Line.Stops;
	// A soft-wrapped line's final stop is the next line's first; do not
	// return it so clicks past the end stay on this line.
	const SIZE_T Count = Line.bEndsParagraph ? Stops.size() : Stops.size() - 1;
	if (Count == 0 || X <= Stops[0].X)
	{
		return Stops.empty() ? Line.Begin : Stops[0].Index;
	}
	for (SIZE_T S = 1; S < Count; ++S)
	{
		if (X < Stops[S].X)
		{
			return (X - Stops[S - 1].X) < (Stops[S].X - X) ? Stops[S - 1].Index : Stops[S].Index;
		}
	}
	return Stops[Count - 1].Index;
}

float FTextLayout::GetCaretX(int32 Index, float AreaWidth) const
{
	const FTextLine* Line = FindLine(Index);
	return Line ? GetLineOffsetX(*Line, AreaWidth) + StopX(Line->Stops, Index) : 0.0f;
}

FRect FTextLayout::GetCaretRect(int32 Index, float AreaWidth, float CaretWidth) const
{
	const FTextLine* Line = FindLine(Index);
	if (!Line)
	{
		const float Height = Font.Size * 1.3f;
		return FRect(0.0f, 0.0f, CaretWidth, Height);
	}
	const float X = GetLineOffsetX(*Line, AreaWidth) + StopX(Line->Stops, Index);
	return FRect(X, Line->Y, X + CaretWidth, Line->Y + Line->Height);
}

void FTextLayout::GetSelectionRects(int32 Begin, int32 End, float AreaWidth, TArray<FRect>& OutRects) const
{
	if (Begin > End)
	{
		std::swap(Begin, End);
	}
	for (const FTextLine& Line : Lines)
	{
		if (End < Line.Begin || Begin > Line.End || (Begin == End))
		{
			continue;
		}
		const int32 A = FMath::Max(Begin, Line.Begin);
		const int32 B = FMath::Min(End, Line.End);
		const float Offset = GetLineOffsetX(Line, AreaWidth);
		float X0 = Offset + StopX(Line.Stops, A);
		float X1 = Offset + StopX(Line.Stops, B);
		// A selected line break shows as a small extra width.
		if (End > Line.End && Line.bEndsParagraph)
		{
			X1 += Font.Size * 0.3f;
		}
		if (X1 > X0)
		{
			OutRects.push_back(FRect(X0, Line.Y, X1, Line.Y + Line.Height));
		}
	}
}

int32 FTextLayout::GetNextCaretIndex(int32 Index) const
{
	for (const FTextLine& Line : Lines)
	{
		for (const FTextCaretStop& Stop : Line.Stops)
		{
			if (Stop.Index > Index)
			{
				return Stop.Index;
			}
		}
		// Crossing a hard line break.
		if (Line.bEndsParagraph && Index >= Line.End && Line.End < Text.Len())
		{
			const int32 Next = Line.End + ((Line.End < Text.Len() && Text[Line.End] == '\r') ? 2 : 1);
			if (Next > Index)
			{
				return FMath::Min(Next, Text.Len());
			}
		}
	}
	return Text.Len();
}

int32 FTextLayout::GetPreviousCaretIndex(int32 Index) const
{
	int32 Best = 0;
	for (const FTextLine& Line : Lines)
	{
		for (const FTextCaretStop& Stop : Line.Stops)
		{
			if (Stop.Index < Index)
			{
				Best = FMath::Max(Best, Stop.Index);
			}
		}
	}
	return Best;
}

int32 FTextLayout::SnapToCaretStop(int32 Index) const
{
	int32 Best = 0;
	for (const FTextLine& Line : Lines)
	{
		for (const FTextCaretStop& Stop : Line.Stops)
		{
			if (Stop.Index <= Index)
			{
				Best = FMath::Max(Best, Stop.Index);
			}
		}
	}
	return Best;
}

void FTextLayout::Paint(FSlateFontCache& FontCache, FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, FVector2 Offset, float AreaWidth, FColor Color) const
{
	if (!Font.Font || Color.A == 0)
	{
		return;
	}
	const FPaintGeometry Base = Geometry.ToPaintGeometry();
	const float PaintScale = Base.GetScale();
	for (const FTextLine& Line : Lines)
	{
		if (Line.End == Line.Begin)
		{
			continue;
		}
		const std::string_view View = Text.View().substr(static_cast<SIZE_T>(Line.Begin), static_cast<SIZE_T>(Line.End - Line.Begin));
		const TSharedRef<const FShapedGlyphSequence> Shaped = FontCache.ShapeText(View, Font, PaintScale);
		const FVector2 Position = Offset + FVector2(GetLineOffsetX(Line, AreaWidth), Line.Y);
		Out.AddText(Layer, Base.MakeChild(Position, Shaped->GetLogicalSize()), Shaped, Color);
	}
}
