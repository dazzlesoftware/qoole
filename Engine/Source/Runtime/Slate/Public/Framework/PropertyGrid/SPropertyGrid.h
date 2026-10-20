// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// SPropertyGrid: a details view over one or more IPropertySources
// (docs/GenesisUI.md §9). Rows are grouped by category (collapsible), with a
// draggable name/value divider and a search box. With several objects
// selected it shows the properties they share; values that differ show as
// "Multiple Values" and editing sets them all.
//
// Undo: every change is wrapped in OnBeginTransaction / OnEndTransaction.
// A drag (spin box, slider, color picker) is one transaction; anything
// else (a typed value, a click) is a transaction of its own.

#pragma once

#include "Framework/PropertyGrid/PropertySource.h"
#include "Styling/SlateStyle.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/WidgetDelegates.h"

class SVerticalBox;
class SEditableTextBox;

using FOnPropertyTransaction = TDelegate<void(const FString& Description)>;

class SPropertyGrid : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPropertyGrid)
		SLATE_STYLE_ARGUMENT(FPropertyGridStyle, Style)
		SLATE_ARGUMENT(float, NameColumnFraction, 0.4f)
		SLATE_ARGUMENT(bool, ShowSearch, true)
		SLATE_EVENT(FOnPropertyTransaction, OnBeginTransaction)
		SLATE_EVENT(FSimpleDelegate, OnEndTransaction)
		SLATE_EVENT(FSimpleDelegate, OnPropertyChanged) // after every change
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

	// The objects to edit; the rows are rebuilt.
	void SetObjects(const TArray<TSharedRef<IPropertySource>>& InObjects);
	const TArray<TSharedRef<IPropertySource>>& GetObjects() const { return Objects; }
	// Rebuilds the rows (when the set of properties changed; values update by themselves).
	void Refresh();
	void SetFilterText(const FString& Text);
	void SetCategoryExpanded(const FString& Category, bool bExpanded);
	bool IsCategoryExpanded(const FString& Category) const { return !CollapsedCategories.contains(Category); }

	float GetNameColumnFraction() const { return NameFraction; }
	void SetNameColumnFraction(float Fraction);

	// The shared value, or nothing when the objects differ (or none are selected).
	TOptional<FPropertyValue> GetSharedValue(FName Property) const;
	// Sets the value on every object as one change (opens a transaction
	// unless an interaction holds one).
	void SetPropertyValue(FName Property, const FPropertyValue& Value);
	// Changes every object's value with a function of its current value
	// (one component of a vector, say).
	void ModifyPropertyValue(FName Property, const TFunction<FPropertyValue(const FPropertyValue&)>& Modify);
	// Bracket a drag: one transaction until EndInteraction.
	void BeginInteraction(FName Property);
	void EndInteraction();

	// The properties shown (shared by every object), in order.
	const TArray<FPropertyDesc>& GetSharedProperties() const { return SharedProperties; }

	FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FCursorReply OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const override;
	void OnMouseCaptureLost() override { bDraggingDivider = false; }

private:
	void Rebuild();
	TSharedRef<SWidget> MakeValueWidget(const FPropertyDesc& Property);
	void OpenColorPicker(FName Property, const FGeometry& SwatchGeometry);
	FString GetPropertyLabel(FName Property) const;
	bool IsOverDivider(const FGeometry& Geometry, FVector2 AbsolutePosition) const;

	const FPropertyGridStyle* Style = nullptr;
	TArray<TSharedRef<IPropertySource>> Objects;
	TArray<FPropertyDesc> SharedProperties;
	TSet<FString> CollapsedCategories;
	FString Filter;
	float NameFraction = 0.4f;
	bool bShowSearch = true;
	bool bDraggingDivider = false;
	int32 InteractionDepth = 0;
	FOnPropertyTransaction OnBeginTransaction;
	FSimpleDelegate OnEndTransaction;
	FSimpleDelegate OnPropertyChanged;
	TSharedPtr<SVerticalBox> Rows;
	TSharedPtr<SEditableTextBox> SearchBox;
	TArray<TSharedPtr<TArray<int32>>> EnumOptionIndices; // option lists of the enum rows
};
