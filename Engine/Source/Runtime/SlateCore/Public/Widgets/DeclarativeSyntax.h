// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Declarative widget construction (docs/GenesisUI.md §6.5).
//
//   SNew(SButton).Text("OK").OnClicked(this, &SDialog::OnOk)
//   SAssignNew(Button, SButton).Text("OK")
//   SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[ ... ]
//   SNew(STableRow<FItemPtr>, OwnerTable).Padding(2)[ ... ]   // extra Construct arguments
//
// SNew(T, Extra...) expands to a builder followed by "<<= T::FArguments()".
// Everything chained after it (setters, slot "+", content "[ ]") binds tighter
// than <<=, so the arguments are complete when the builder creates the
// widget: it calls SWidgetConstruct with the common arguments and then
// T::Construct(Args, Extra...).
//
// Widgets declare their arguments between GE_BEGIN_ARGS and GE_END_ARGS:
//
//   GE_BEGIN_ARGS(SButton)
//       GE_ATTRIBUTE(FString, Text, "")          // value or binding
//       GE_ARGUMENT(bool, IsFocusable, true)      // plain value
//       GE_STYLE_ARGUMENT(FButtonStyle, ButtonStyle)
//       GE_EVENT(FOnClicked, OnClicked)           // delegate
//       GE_DEFAULT_SLOT(Content)                  // SNew(SButton)[ ... ]
//       GE_NAMED_SLOT(Header)                     // .Header()[ ... ]
//       GE_SLOTS(FSlot, Slots)                    // + T::Slot()[ ... ]
//   GE_END_ARGS()

#pragma once

#include "Layout/Children.h"

#include <tuple>

template <typename WidgetType>
using TWidgetArguments = typename WidgetType::FArguments;

// Common setters (visibility, tooltip, cursor...) on every FArguments.
template <typename Derived>
struct TWidgetArgsBase : public FWidgetCommonArgs
{
	Derived& Visibility(TAttribute<EVisibility> In) { _Visibility = std::move(In); return Self(); }
	Derived& IsEnabled(TAttribute<bool> In) { _IsEnabled = std::move(In); return Self(); }
	Derived& ToolTipText(TAttribute<FString> In) { _ToolTipText = std::move(In); return Self(); }
	Derived& ToolTip(TSharedPtr<IToolTip> In) { _ToolTip = std::move(In); return Self(); }
	Derived& Cursor(TAttribute<TOptional<EMouseCursor>> In) { _Cursor = std::move(In); return Self(); }
	Derived& Cursor(EMouseCursor In) { _Cursor = TOptional<EMouseCursor>(In); return Self(); }
	Derived& Clipping(EWidgetClipping In) { _Clipping = In; return Self(); }
	Derived& RenderTransform(TAttribute<TOptional<FTransform2D>> In) { _RenderTransform = std::move(In); return Self(); }
	Derived& RenderTransformPivot(FVector2 In) { _RenderTransformPivot = In; return Self(); }
	Derived& RenderOpacity(float In) { _RenderOpacity = In; return Self(); }
	Derived& Tag(FName In) { _Tag = In; return Self(); }
	Derived& ForceVolatile(bool In) { _ForceVolatile = In; return Self(); }

protected:
	Derived& Self() { return static_cast<Derived&>(*this); }
};

// Content given with [ ] or a named slot.
struct FWidgetSlotArg
{
	TSharedPtr<SWidget> Widget;
};

template <typename WidgetType, typename... ExtraArgs>
class TWidgetBuilder
{
public:
	TWidgetBuilder(TSharedPtr<WidgetType>* InAssignTo, ExtraArgs... InExtra) : AssignTo(InAssignTo), Extra(std::move(InExtra)...) {}

	TSharedRef<WidgetType> operator<<=(TWidgetArguments<WidgetType>& Args) { return Build(Args); }
	TSharedRef<WidgetType> operator<<=(TWidgetArguments<WidgetType>&& Args) { return Build(Args); }

private:
	TSharedRef<WidgetType> Build(TWidgetArguments<WidgetType>& Args)
	{
		TSharedRef<WidgetType> Widget(std::make_shared<WidgetType>());
		Widget->SWidgetConstruct(Args);
		std::apply([&](auto&... E) { Widget->Construct(Args, E...); }, Extra);
		if (AssignTo)
		{
			*AssignTo = Widget.ToSharedPtr();
		}
		return Widget;
	}

	TSharedPtr<WidgetType>* AssignTo;
	std::tuple<ExtraArgs...> Extra;
};

template <typename WidgetType, typename... ExtraArgs>
TWidgetBuilder<WidgetType, std::decay_t<ExtraArgs>...> MakeWidgetBuilder(TSharedPtr<WidgetType>* AssignTo, ExtraArgs&&... Extra)
{
	return TWidgetBuilder<WidgetType, std::decay_t<ExtraArgs>...>(AssignTo, std::forward<ExtraArgs>(Extra)...);
}

#define SNew(WidgetType, ...) \
	::MakeWidgetBuilder<WidgetType>(static_cast<TSharedPtr<WidgetType>*>(nullptr) __VA_OPT__(,) __VA_ARGS__) <<= ::TWidgetArguments<WidgetType>()

// Like SNew; also stores the new widget in Variable (a TSharedPtr of the
// widget type or a base of it).
#define SAssignNew(Variable, WidgetType, ...) \
	::GE_AssignHelper(Variable) = SNew(WidgetType __VA_OPT__(,) __VA_ARGS__)

// Lets SAssignNew assign a TSharedRef<Derived> to a TSharedPtr<Base>.
template <typename VarType>
struct TAssignHelper
{
	TSharedPtr<VarType>& Var;
	template <typename WidgetType>
	TSharedRef<WidgetType> operator=(TSharedRef<WidgetType> Widget)
	{
		Var = Widget.ToSharedPtr();
		return Widget;
	}
};
template <typename VarType>
TAssignHelper<VarType> GE_AssignHelper(TSharedPtr<VarType>& Var) { return TAssignHelper<VarType>{ Var }; }

// --- Argument declaration macros -----------------------------------------

#define GE_BEGIN_ARGS(WidgetType) \
	public: \
	struct FArguments : public ::TWidgetArgsBase<FArguments> \
	{ \
		using WidgetArgsType = FArguments;

#define GE_END_ARGS() \
	};

// A TAttribute<Type> argument: a value, a callable, or (Object, &Class::Getter).
#define GE_ATTRIBUTE(Type, Name, Default) \
	TAttribute<Type> _##Name = TAttribute<Type>(Default); \
	WidgetArgsType& Name(TAttribute<Type> InAttribute) { _##Name = std::move(InAttribute); return *this; } \
	template <typename GE_O> \
	WidgetArgsType& Name(const GE_O* InObject, Type (GE_O::*InMethod)() const) { _##Name = TAttribute<Type>(InObject, InMethod); return *this; }

// An unset TAttribute (IsSet() false unless given); for "use the style unless overridden".
#define GE_OPTIONAL_ATTRIBUTE(Type, Name) \
	TAttribute<Type> _##Name; \
	WidgetArgsType& Name(TAttribute<Type> InAttribute) { _##Name = std::move(InAttribute); return *this; } \
	template <typename GE_O> \
	WidgetArgsType& Name(const GE_O* InObject, Type (GE_O::*InMethod)() const) { _##Name = TAttribute<Type>(InObject, InMethod); return *this; }

// A plain value.
#define GE_ARGUMENT(Type, Name, Default) \
	Type _##Name = Default; \
	WidgetArgsType& Name(Type InArg) { _##Name = std::move(InArg); return *this; }

// A style struct; nullptr means "the default style".
#define GE_STYLE_ARGUMENT(StyleType, Name) \
	const StyleType* _##Name = nullptr; \
	WidgetArgsType& Name(const StyleType* InStyle) { _##Name = InStyle; return *this; }

// A delegate: a TDelegate, a callable, or (Object, &Class::Method).
#define GE_EVENT(DelegateType, Name) \
	DelegateType _##Name; \
	WidgetArgsType& Name(const DelegateType& InDelegate) { _##Name = InDelegate; return *this; } \
	template <typename GE_F> \
		requires(!std::same_as<std::remove_cvref_t<GE_F>, DelegateType>) \
	WidgetArgsType& Name(GE_F&& InFunctor) { _##Name = DelegateType::CreateLambda(std::forward<GE_F>(InFunctor)); return *this; } \
	template <typename GE_O, typename GE_M> \
	WidgetArgsType& Name(GE_O* InObject, GE_M InMethod) { _##Name = DelegateType::CreateRaw(InObject, InMethod); return *this; }

// Content given with operator[].
#define GE_DEFAULT_SLOT(Name) \
	::FWidgetSlotArg _##Name; \
	WidgetArgsType& operator[](TSharedRef<SWidget> InChild) { _##Name.Widget = InChild.ToSharedPtr(); return *this; } \
	template <typename GE_W> \
		requires std::derived_from<GE_W, SWidget> \
	WidgetArgsType& operator[](const TSharedRef<GE_W>& InChild) { _##Name.Widget = InChild.ToSharedPtr(); return *this; }

// Content given with .Name()[ ... ].
#define GE_NAMED_SLOT(Name) \
	::FWidgetSlotArg _##Name; \
	struct F##Name##Proxy \
	{ \
		WidgetArgsType* Args; \
		WidgetArgsType& operator[](TSharedRef<SWidget> InChild) const { Args->_##Name.Widget = InChild.ToSharedPtr(); return *Args; } \
	}; \
	F##Name##Proxy Name() { return F##Name##Proxy{ this }; } \
	WidgetArgsType& Name(TSharedRef<SWidget> InChild) { _##Name.Widget = InChild.ToSharedPtr(); return *this; }

// A list of slots given with operator+.
#define GE_SLOTS(SlotType, Name) \
	TArray<SlotType> _##Name; \
	WidgetArgsType& operator+(SlotType& InSlot) { _##Name.push_back(std::move(InSlot)); return *this; } \
	WidgetArgsType& operator+(SlotType&& InSlot) { _##Name.push_back(std::move(InSlot)); return *this; }
