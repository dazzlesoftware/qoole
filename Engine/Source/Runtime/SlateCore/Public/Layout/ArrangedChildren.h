// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// The output of SWidget::OnArrangeChildren: each visible child with the
// geometry its parent gave it, in paint order.

#pragma once

#include "Layout/Geometry.h"
#include "Types/WidgetTypes.h"

class SWidget;

struct FArrangedWidget
{
	TSharedRef<SWidget> Widget;
	FGeometry Geometry;

	FArrangedWidget(TSharedRef<SWidget> InWidget, const FGeometry& InGeometry) : Widget(std::move(InWidget)), Geometry(InGeometry) {}
};

class FArrangedChildren
{
public:
	// Adds a child unless it is collapsed (collapsed children take no space
	// and are neither painted nor hit-tested).
	void AddWidget(EVisibility ChildVisibility, FArrangedWidget Arranged)
	{
		if (IsVisibleForLayout(ChildVisibility))
		{
			Widgets.push_back(std::move(Arranged));
		}
	}

	int32 Num() const { return static_cast<int32>(Widgets.size()); }
	FArrangedWidget& operator[](int32 Index) { return Widgets[Index]; }
	const FArrangedWidget& operator[](int32 Index) const { return Widgets[Index]; }
	auto begin() { return Widgets.begin(); }
	auto end() { return Widgets.end(); }
	auto begin() const { return Widgets.begin(); }
	auto end() const { return Widgets.end(); }

	TArray<FArrangedWidget> Widgets;
};
