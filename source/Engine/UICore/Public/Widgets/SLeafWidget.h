// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#pragma once

#include "Widgets/DeclarativeSyntax.h"

// A widget without children (text, images, separators).
class SLeafWidget : public SWidget
{
public:
	FChildren* GetChildren() override;

protected:
	void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override
	{
		(void)AllottedGeometry;
		(void)ArrangedChildren;
	}
};
