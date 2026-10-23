// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Framework/PropertyGrid/SPropertyGrid.h"
#include "Containers/ContainerAliases.h"
#include "Templates/SharedPointer.h"
#include "Internationalization/TextLocalization.h"

#include "Framework/Application/SlateApplication.h"
#include "Layout/LayoutUtils.h"
#include "Widgets/Input/SColorPicker.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	// A name and a value side by side; the grid decides where the divider is.
	class SPropertyRow final : public SPanel
	{
	public:
		SLATE_BEGIN_ARGS(SPropertyRow)
		SLATE_END_ARGS()

		void Construct(const FArguments&, SPropertyGrid* InGrid, const TSharedRef<SWidget>& Name,
		    const TSharedRef<SWidget>& Value, int32 InIndex)
		{
			Grid = InGrid;
			Index = InIndex;
			Children.Add(FSimpleSlot());
			Children.Add(FSimpleSlot());
			Children[0].Padding(FMargin(10.0f, 3.0f, 6.0f, 3.0f)).VAlign(VAlign_Center)[Name];
			Children[1].Padding(FMargin(6.0f, 2.0f, 8.0f, 2.0f)).VAlign(VAlign_Center)[Value];
		}

		FChildren* GetChildren() override
		{
			return &Children;
		}

	protected:
		FVector2 ComputeDesiredSize(float) const override
		{
			const FVector2 A = Children[0].GetWidget()->GetDesiredSize() + Children[0].GetPadding().GetDesiredSize();
			const FVector2 B = Children[1].GetWidget()->GetDesiredSize() + Children[1].GetPadding().GetDesiredSize();
			const float Height = FMath::Max(
			    FAppStyle::Get().GetWidgetStyle<FPropertyGridStyle>("PropertyGrid").RowHeight, FMath::Max(A.Y, B.Y));
			return FVector2(A.X + B.X, Height);
		}

		void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override
		{
			const float Split = FMath::RoundToFloat(AllottedGeometry.Size.X * Grid->GetNameColumnFraction());
			ArrangeSlotChild(AllottedGeometry, ArrangedChildren, Children[0], FVector2::Zero(),
			    FVector2(Split, AllottedGeometry.Size.Y));
			ArrangeSlotChild(AllottedGeometry, ArrangedChildren, Children[1], FVector2(Split, 0.0f),
			    FVector2(AllottedGeometry.Size.X - Split, AllottedGeometry.Size.Y));
		}

		int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect,
		    FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const override
		{
			const FPropertyGridStyle& Style = FAppStyle::Get().GetWidgetStyle<FPropertyGridStyle>("PropertyGrid");
			const FSlateBrush& Brush = IsHovered() ? Style.RowHovered : (Index % 2 ? Style.RowOdd : Style.RowEven);
			Brush.Paint(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), InWidgetStyle.ColorAndOpacityTint);
			const float Split = FMath::RoundToFloat(Geometry.Size.X * Grid->GetNameColumnFraction());
			OutDrawElements.AddSolidBox(LayerId,
			    Geometry.ToPaintGeometry(FRect(Split, 0.0f, Split + 1.0f, Geometry.Size.Y)),
			    InWidgetStyle.Tinted(Style.Divider.Resolve()));
			return PaintArrangedChildren(Args, CullingRect, OutDrawElements, LayerId + 1, InWidgetStyle);
		}

	private:
		TPanelChildren<FSimpleSlot> Children{this};
		SPropertyGrid* Grid = nullptr;
		int32 Index = 0;
	};

	// A category heading that collapses its rows.
	class SCategoryHeader final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SCategoryHeader)
		SLATE_END_ARGS()

		void Construct(const FArguments&, SPropertyGrid* InGrid, const FString& InCategory)
		{
			Grid = InGrid;
			Category = InCategory;
			const FPropertyGridStyle& Style = FAppStyle::Get().GetWidgetStyle<FPropertyGridStyle>("PropertyGrid");
			ChildSlot.Padding(
			    FMargin(22.0f, 4.0f, 8.0f, 4.0f))[SNew(STextBlock).Text(Category).TextStyle(&Style.CategoryText)];
		}

		FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event) override
		{
			if (Event.GetEffectingButton() != EMouseButton::Left)
			{
				return FReply::Unhandled();
			}
			Grid->SetCategoryExpanded(Category, !Grid->IsCategoryExpanded(Category));
			return FReply::Handled();
		}

	protected:
		int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect,
		    FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const override
		{
			const FPropertyGridStyle& Style = FAppStyle::Get().GetWidgetStyle<FPropertyGridStyle>("PropertyGrid");
			Style.CategoryHeader.Paint(
			    OutDrawElements, LayerId, Geometry.ToPaintGeometry(), InWidgetStyle.ColorAndOpacityTint);
			const FVector2 C(12.0f, Geometry.Size.Y * 0.5f);
			const TArray<FVector2> Arrow = Grid->IsCategoryExpanded(Category)
			    ? TArray<FVector2>{C + FVector2(-4.0f, -2.0f), C + FVector2(0.0f, 2.5f), C + FVector2(4.0f, -2.0f)}
			    : TArray<FVector2>{C + FVector2(-2.0f, -4.0f), C + FVector2(2.5f, 0.0f), C + FVector2(-2.0f, 4.0f)};
			OutDrawElements.AddLines(
			    LayerId + 1, Geometry.ToPaintGeometry(), Arrow, InWidgetStyle.GetForegroundColor(), 1.5f);
			return PaintArrangedChildren(Args, CullingRect, OutDrawElements, LayerId + 1, InWidgetStyle);
		}

	private:
		SPropertyGrid* Grid = nullptr;
		FString Category;
	};

	// The colored X/Y/Z letter in front of a vector component.
	TSharedRef<SWidget> AxisLabel(const char* Axis, FColor Color)
	{
		return SNew(SBorder)
		    .BorderImage(&FAppStyle::Get().GetBrush("WhiteBrush"))
		    .BorderBackgroundColor(FSlateColor(Color.WithAlpha(150)))
		    .Padding(FMargin(4.0f, 0.0f))
		    .VAlign(VAlign_Center)[SNew(STextBlock)
		            .Text(Axis)
		            .TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText"))
		            .ColorAndOpacity(FSlateColor(EStyleColor::ForegroundHover))];
	}

	float GetComponent(const FPropertyValue& Value, int32 Component)
	{
		if (const FVector2* V2 = std::get_if<FVector2>(&Value))
		{
			return Component == 0 ? V2->X : V2->Y;
		}
		if (const FPropertyVector3* V3 = std::get_if<FPropertyVector3>(&Value))
		{
			return Component == 0 ? V3->X : (Component == 1 ? V3->Y : V3->Z);
		}
		return 0.0f;
	}

	FPropertyValue SetComponent(const FPropertyValue& Value, int32 Component, float NewValue)
	{
		if (const FVector2* V2 = std::get_if<FVector2>(&Value))
		{
			FVector2 R = *V2;
			(Component == 0 ? R.X : R.Y) = NewValue;
			return R;
		}
		if (const FPropertyVector3* V3 = std::get_if<FPropertyVector3>(&Value))
		{
			FPropertyVector3 R = *V3;
			(Component == 0 ? R.X : (Component == 1 ? R.Y : R.Z)) = NewValue;
			return R;
		}
		return Value;
	}
} // namespace

// ---------------------------------------------------------------------------

void SPropertyGrid::Construct(const FArguments& Args)
{
	Style = Args._Style ? Args._Style : &FAppStyle::Get().GetWidgetStyle<FPropertyGridStyle>("PropertyGrid");
	NameFraction = Args._NameColumnFraction;
	bShowSearch = Args._ShowSearch;
	OnBeginTransaction = Args._OnBeginTransaction;
	OnEndTransaction = Args._OnEndTransaction;
	OnPropertyChanged = Args._OnPropertyChanged;

	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight().Padding(FMargin(8.0f, 6.0f, 8.0f, 4.0f))[SNew(STextBlock)
	        .TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("BoldText"))
	        .Text([this]
	{
		if (Objects.empty())
		{
			return FString("Nothing selected");
		}
		return Objects.size() == 1 ? Objects[0]->GetDisplayName() : FString::Format("{} objects", Objects.size());
	})];
	if (bShowSearch)
	{
		Box->AddSlot().AutoHeight().Padding(FMargin(8.0f, 0.0f, 8.0f, 6.0f))[SAssignNew(SearchBox, SEditableTextBox)
		        .HintText(GENESIS_TEXT("Search properties"))
		        .OnTextChanged([this](const FString& Text) { SetFilterText(Text); })];
	}
	SAssignNew(Rows, SVerticalBox);
	Box->AddSlot().FillHeight(
	    1.0f)[SNew(SScrollBox) + SScrollBox::Slot()[ToSharedRef(StaticCastSharedPtr<SWidget>(Rows))]];
	ChildSlot[Box];
	Rebuild();
}

void SPropertyGrid::SetObjects(const TArray<TSharedRef<IPropertySource>>& InObjects)
{
	if (InteractionDepth > 0)
	{
		EndInteraction();
	}
	Objects = InObjects;
	Refresh();
}

void SPropertyGrid::Refresh()
{
	// The properties every object has (same name and type), in the first
	// object's order.
	SharedProperties.clear();
	if (!Objects.empty())
	{
		Objects[0]->GetProperties(SharedProperties);
		for (SIZE_T I = 1; I < Objects.size(); ++I)
		{
			TArray<FPropertyDesc> Others;
			Objects[I]->GetProperties(Others);
			std::erase_if(SharedProperties, [&Others](const FPropertyDesc& P)
			{
				return std::none_of(Others.begin(), Others.end(),
				    [&P](const FPropertyDesc& O) { return O.Name == P.Name && O.Type == P.Type; });
			});
		}
	}
	Rebuild();
}

void SPropertyGrid::SetFilterText(const FString& Text)
{
	Filter = Text.TrimStartAndEnd();
	Rebuild();
}

void SPropertyGrid::SetCategoryExpanded(const FString& Category, bool bExpanded)
{
	if (bExpanded)
	{
		CollapsedCategories.erase(Category);
	}
	else
	{
		CollapsedCategories.insert(Category);
	}
	Rebuild();
}

void SPropertyGrid::SetNameColumnFraction(float Fraction)
{
	NameFraction = FMath::Clamp(Fraction, 0.1f, 0.9f);
	Rows->Invalidate(EInvalidateWidgetReason::Layout);
	for (int32 I = 0; I < Rows->NumSlots(); ++I)
	{
		Rows->GetSlot(I).GetWidget()->Invalidate(EInvalidateWidgetReason::Layout);
	}
}

void SPropertyGrid::OpenColorPicker(FName Property, const FGeometry& SwatchGeometry)
{
	FSlateApplication& App = FSlateApplication::Get();
	const TSharedPtr<SWindow> Window = App.FindWindowForWidget(*this);
	if (!Window)
	{
		return;
	}
	// With different colors, start from the first object's.
	TOptional<FPropertyValue> Value = GetSharedValue(Property);
	if (!Value && !Objects.empty())
	{
		Value = Objects[0]->GetValue(Property);
	}
	const TSharedRef<SWidget> Picker = SNew(SBox).Padding(6.0f)[SNew(SColorPicker)
	        .TargetColor(Value && std::holds_alternative<FColor>(*Value) ? std::get<FColor>(*Value) : FColor::White())
	        .OnInteractionBegin([this, Property] { BeginInteraction(Property); })
	        .OnInteractionEnd([this] {
		EndInteraction();
	}).OnColorChanged([this, Property](FColor Color) { SetPropertyValue(Property, Color); })];
	const FRect Swatch = SwatchGeometry.GetLayoutBoundingRect().Scale(1.0f / Window->GetScale());
	App.PushMenu(SharedThis(this), Picker, FVector2(Swatch.Left, Swatch.Bottom + 2.0f));
}

void SPropertyGrid::Rebuild()
{
	Rows->ClearChildren();
	EnumOptionIndices.clear();
	TArray<FString> Categories;
	for (const FPropertyDesc& Property : SharedProperties)
	{
		if (!ArrayUtil::Contains(Categories, Property.Category))
		{
			Categories.push_back(Property.Category);
		}
	}
	int32 RowIndex = 0;
	for (const FString& Category : Categories)
	{
		TArray<const FPropertyDesc*> Visible;
		for (const FPropertyDesc& Property : SharedProperties)
		{
			const FString Label = Property.DisplayName.IsEmpty() ? Property.Name.ToString() : Property.DisplayName;
			if (Property.Category == Category &&
			    (Filter.IsEmpty() || Label.ToLower().Contains(Filter.ToLower().View())))
			{
				Visible.push_back(&Property);
			}
		}
		if (Visible.empty())
		{
			continue;
		}
		Rows->AddSlot().AutoHeight()[SNew(SCategoryHeader, this, Category)];
		if (!Filter.IsEmpty() || IsCategoryExpanded(Category))
		{
			for (const FPropertyDesc* Property : Visible)
			{
				const TSharedRef<SWidget> Name =
				    SNew(STextBlock)
				        .Text(Property->DisplayName.IsEmpty() ? Property->Name.ToString() : Property->DisplayName)
				        .TextStyle(&Style->NameText)
				        .ToolTipText(Property->ToolTip);
				Rows->AddSlot().AutoHeight()[SNew(SPropertyRow, this, Name, MakeValueWidget(*Property), RowIndex++)];
			}
		}
	}
	if (Objects.empty())
	{
		Rows->AddSlot().AutoHeight().Padding(FMargin(10.0f, 8.0f))[SNew(STextBlock)
		        .Text(GENESIS_TEXT("Select something to see its properties."))
		        .TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText"))];
	}
}

TOptional<FPropertyValue> SPropertyGrid::GetSharedValue(FName Property) const
{
	if (Objects.empty())
	{
		return {};
	}
	// Rows of objects shown before SetObjects can still ask until the grid
	// is rebuilt: a property the objects lack, or a value of another type,
	// reads as unset.
	const FPropertyDesc* Desc = nullptr;
	for (const FPropertyDesc& Shared : SharedProperties)
	{
		if (Shared.Name == Property)
		{
			Desc = &Shared;
		}
	}
	if (!Desc)
	{
		return {};
	}
	FPropertyValue First = Objects[0]->GetValue(Property);
	static constexpr SIZE_T Alternatives[] = {0, 1, 2, 3, 4, 1, 5,
	    6}; // by EPropertyType: bool, int32, float, FString, FColor, int32, FVector2, FPropertyVector3
	if (First.index() != Alternatives[static_cast<SIZE_T>(Desc->Type)])
	{
		return {};
	}
	for (SIZE_T I = 1; I < Objects.size(); ++I)
	{
		if (!(Objects[I]->GetValue(Property) == First))
		{
			return {};
		}
	}
	return First;
}

FString SPropertyGrid::GetPropertyLabel(FName Property) const
{
	for (const FPropertyDesc& Desc : SharedProperties)
	{
		if (Desc.Name == Property)
		{
			return Desc.DisplayName.IsEmpty() ? Desc.Name.ToString() : Desc.DisplayName;
		}
	}
	return Property.ToString();
}

void SPropertyGrid::BeginInteraction(FName Property)
{
	if (InteractionDepth++ == 0)
	{
		OnBeginTransaction.ExecuteIfBound(FString::Format("Change {}", GetPropertyLabel(Property)));
	}
}

void SPropertyGrid::EndInteraction()
{
	if (InteractionDepth > 0 && --InteractionDepth == 0)
	{
		OnEndTransaction.ExecuteIfBound();
	}
}

void SPropertyGrid::ModifyPropertyValue(FName Property, const TFunction<FPropertyValue(const FPropertyValue&)>& Modify)
{
	BeginInteraction(Property);
	for (const TSharedRef<IPropertySource>& Object : Objects)
	{
		Object->SetValue(Property, Modify(Object->GetValue(Property)));
	}
	EndInteraction();
	OnPropertyChanged.ExecuteIfBound();
}

void SPropertyGrid::SetPropertyValue(FName Property, const FPropertyValue& Value)
{
	ModifyPropertyValue(Property, [&Value](const FPropertyValue&) { return Value; });
}

TSharedRef<SWidget> SPropertyGrid::MakeValueWidget(const FPropertyDesc& Property)
{
	const FName Name = Property.Name;
	const bool bEnabled = !Property.bReadOnly;
	switch (Property.Type)
	{
	case EPropertyType::Bool:
		return SNew(SBox).HAlign(HAlign_Left)[SNew(SCheckBox)
		        .IsEnabled(bEnabled)
		        .IsChecked([this, Name]
		{
			const TOptional<FPropertyValue> V = GetSharedValue(Name);
			if (!V)
			{
				return ECheckBoxState::Undetermined;
			}
			return std::get<bool>(*V) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		}).OnCheckStateChanged([this, Name](ECheckBoxState State) {
			SetPropertyValue(Name, State == ECheckBoxState::Checked);
		})];

	case EPropertyType::Int:
		return SNew(SSpinBox<int32>)
		    .IsEnabled(bEnabled)
		    .MinValue(Property.Min ? TOptional<int32>(FMath::RoundToInt(*Property.Min)) : TOptional<int32>())
		    .MaxValue(Property.Max ? TOptional<int32>(FMath::RoundToInt(*Property.Max)) : TOptional<int32>())
		    .Value([this, Name]
		{
			const TOptional<FPropertyValue> V = GetSharedValue(Name);
			return V ? TOptional<int32>(std::get<int32>(*V)) : TOptional<int32>();
		}).OnBeginSliderMovement([this, Name] {
			BeginInteraction(Name);
		}).OnEndSliderMovement([this](int32) {
			EndInteraction();
		}).OnValueChanged([this, Name](int32 V) { SetPropertyValue(Name, V); });

	case EPropertyType::Float:
		return SNew(SSpinBox<float>)
		    .IsEnabled(bEnabled)
		    .MinValue(Property.Min)
		    .MaxValue(Property.Max)
		    .Value([this, Name]
		{
			const TOptional<FPropertyValue> V = GetSharedValue(Name);
			return V ? TOptional<float>(std::get<float>(*V)) : TOptional<float>();
		}).OnBeginSliderMovement([this, Name] {
			BeginInteraction(Name);
		}).OnEndSliderMovement([this](float) {
			EndInteraction();
		}).OnValueChanged([this, Name](float V) { SetPropertyValue(Name, V); });

	case EPropertyType::String:
		return SNew(SEditableTextBox)
		    .IsReadOnly(!bEnabled)
		    .RevertTextOnEscape(true)
		    .SelectAllTextWhenFocused(true)
		    .Text(
		        [this, Name]
		{
			const TOptional<FPropertyValue> V = GetSharedValue(Name);
			return V ? std::get<FString>(*V) : FString();
		})
		    .HintText([this, Name] { return GetSharedValue(Name) ? FString() : FString("Multiple Values"); })
		    .OnTextCommitted([this, Name](const FString& Text, ETextCommit How)
		{
			const TOptional<FPropertyValue> V = GetSharedValue(Name);
			if (How != ETextCommit::OnCleared && (!V || !(std::get<FString>(*V) == Text)) && !(Text.IsEmpty() && !V))
			{
				SetPropertyValue(Name, Text);
			}
		});

	case EPropertyType::Color:
		return SNew(SBox).HAlign(HAlign_Left)[SNew(SColorBlock)
		        .Size(FVector2(64.0f, 18.0f))
		        .IsEnabled(bEnabled)
		        .Color(
		            [this, Name]
		{
			const TOptional<FPropertyValue> V = GetSharedValue(Name);
			return V ? std::get<FColor>(*V) : FColor::White();
		})
		        .IsUndetermined([this, Name] { return !GetSharedValue(Name).has_value(); })
		        .OnMouseButtonDown([this, Name](const FGeometry& Geometry, const FPointerEvent& Event)
		{
			if (Event.GetEffectingButton() != EMouseButton::Left)
			{
				return FReply::Unhandled();
			}
			OpenColorPicker(Name, Geometry);
			return FReply::Handled();
		})];

	case EPropertyType::Enum:
	{
		const TSharedRef<TArray<int32>> Indices = MakeShared<TArray<int32>>();
		for (int32 I = 0; I < static_cast<int32>(Property.EnumOptions.size()); ++I)
		{
			Indices->push_back(I);
		}
		EnumOptionIndices.push_back(Indices.ToSharedPtr()); // the combo box reads it
		const TArray<FString> Options = Property.EnumOptions;
		return SNew(SBox)
		    .HAlign(HAlign_Left)
		    .MinDesiredWidth(TOptional<float>(140.0f))[SNew(SComboBox<int32>)
		            .IsEnabled(bEnabled)
		            .OptionsSource(&Indices.Get())
		            .OnGenerateWidget([Options](int32 Index) -> TSharedRef<SWidget>
		{ return SNew(STextBlock).Text(Index < static_cast<int32>(Options.size()) ? Options[Index] : FString("?")); })
		            .OnSelectionChanged(
		                [this, Name](int32 Index, ESelectInfo) { SetPropertyValue(Name, Index); })[SNew(STextBlock)
		                    .Text([this, Name, Options]
		{
			const TOptional<FPropertyValue> V = GetSharedValue(Name);
			if (!V)
			{
				return FString("Multiple Values");
			}
			const int32 Index = std::get<int32>(*V);
			return Index >= 0 && Index < static_cast<int32>(Options.size()) ? Options[Index] : FString("?");
		})]];
	}

	case EPropertyType::Vector2:
	case EPropertyType::Vector3:
	{
		const int32 Count = Property.Type == EPropertyType::Vector2 ? 2 : 3;
		const char* Axes[] = {"X", "Y", "Z"};
		const FColor AxisColors[] = {FColor(200, 60, 60), FColor(70, 160, 70), FColor(60, 100, 210)};
		const TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		for (int32 C = 0; C < Count; ++C)
		{
			Row->AddSlot().FillWidth(1.0f).Padding(FMargin(C > 0 ? 4.0f : 0.0f, 0.0f, 0.0f,
			    0.0f))[SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth()[AxisLabel(Axes[C], AxisColors[C])] +
			    SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SSpinBox<float>)
			            .IsEnabled(bEnabled)
			            .MinDesiredWidth(30.0f)
			            .UndeterminedText("—")
			            .MinValue(Property.Min)
			            .MaxValue(Property.Max)
			            .Value([this, Name, C]() -> TOptional<float>
			{
				if (Objects.empty())
				{
					return {};
				}
				const float First = GetComponent(Objects[0]->GetValue(Name), C);
				for (const TSharedRef<IPropertySource>& Object : Objects)
				{
					if (GetComponent(Object->GetValue(Name), C) != First)
					{
						return {};
					}
				}
				return First;
			}).OnBeginSliderMovement([this, Name] {
				BeginInteraction(Name);
			}).OnEndSliderMovement([this](float) {
				EndInteraction();
			}).OnValueChanged([this, Name, C](float V) {
				ModifyPropertyValue(Name, [C, V](const FPropertyValue& Old) { return SetComponent(Old, C, V); });
			})]];
		}
		return Row;
	}
	}
	return SNullWidget::NullWidget();
}

bool SPropertyGrid::IsOverDivider(const FGeometry& Geometry, FVector2 AbsolutePosition) const
{
	if (!Rows)
	{
		return false;
	}
	const FGeometry& RowsGeometry = Rows->GetCachedGeometry();
	const FVector2 Local = RowsGeometry.AbsoluteToLocal(AbsolutePosition);
	(void)Geometry;
	const float Split = RowsGeometry.Size.X * NameFraction;
	return Local.Y >= 0.0f && Local.Y < RowsGeometry.Size.Y && FMath::Abs(Local.X - Split) <= 3.0f;
}

FReply SPropertyGrid::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() == EMouseButton::Left && IsOverDivider(Geometry, Event.GetAbsolutePosition()))
	{
		bDraggingDivider = true;
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	return FReply::Unhandled();
}

FReply SPropertyGrid::OnMouseMove(const FGeometry&, const FPointerEvent& Event)
{
	if (!bDraggingDivider)
	{
		return FReply::Unhandled();
	}
	const FGeometry& RowsGeometry = Rows->GetCachedGeometry();
	const float X = RowsGeometry.AbsoluteToLocal(Event.GetAbsolutePosition()).X;
	SetNameColumnFraction(RowsGeometry.Size.X > 0.0f ? X / RowsGeometry.Size.X : NameFraction);
	return FReply::Handled();
}

FReply SPropertyGrid::OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event)
{
	if (bDraggingDivider && Event.GetEffectingButton() == EMouseButton::Left)
	{
		bDraggingDivider = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FCursorReply SPropertyGrid::OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const
{
	if (bDraggingDivider || IsOverDivider(Geometry, Event.GetAbsolutePosition()))
	{
		return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
	}
	return SCompoundWidget::OnCursorQuery(Geometry, Event);
}
