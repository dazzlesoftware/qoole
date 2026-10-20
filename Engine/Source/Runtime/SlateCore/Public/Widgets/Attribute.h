// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// TAttribute<T> holds either a value or a binding: a function returning T
// (docs/GenesisUI.md §6.3).
//
//   TAttribute<FString> A = "Fixed";
//   TAttribute<FString> B = [this] { return Name; };          // binding
//   TAttribute<FString> C(this, &SMyWidget::GetLabel);        // binding to a const member
//
// Widgets do not store TAttributes directly. They store TWidgetAttribute
// (Widgets/SWidget.h), which caches the value, evaluates bindings once per
// frame and invalidates the owner with a declared reason when the result
// changes.

#pragma once

#include "CoreMinimal.h"

#include <concepts>

template <typename T>
class TAttribute
{
public:
	using FGetter = TFunction<T()>;

	// An unset attribute (IsSet() is false; Get() returns T{}).
	TAttribute() = default;

	TAttribute(const T& InValue) : Value(InValue), bIsSet(true) {}
	TAttribute(T&& InValue) : Value(std::move(InValue)), bIsSet(true) {}

	// Anything T can be constructed from ("text" for FString, a float for a
	// margin...).
	template <typename U>
		requires(!std::same_as<std::remove_cvref_t<U>, TAttribute> && !std::same_as<std::remove_cvref_t<U>, T>
			&& std::constructible_from<T, U&&> && !std::invocable<U>)
	TAttribute(U&& InValue) : Value(T(std::forward<U>(InValue))), bIsSet(true)
	{
	}

	// A binding: a callable returning something convertible to T.
	template <typename F>
		requires(!std::same_as<std::remove_cvref_t<F>, TAttribute> && std::invocable<F>
			&& std::convertible_to<std::invoke_result_t<F>, T>)
	TAttribute(F&& Functor) : Getter(std::forward<F>(Functor)), bIsSet(true)
	{
	}

	// A binding to a const member function of a raw object.
	template <typename O>
	TAttribute(const O* Object, T (O::*Method)() const)
		: Getter([Object, Method]() { return (Object->*Method)(); })
		, bIsSet(true)
	{
	}

	// A binding to a member of a shared object; once it dies the attribute
	// returns the value it last produced.
	template <typename O>
	static TAttribute CreateSP(const TSharedPtr<O>& Object, T (O::*Method)() const)
	{
		TWeakPtr<O> Weak = Object;
		auto Last = std::make_shared<T>();
		return TAttribute(FGetter([Weak, Method, Last]() {
			if (const TSharedPtr<O> Pinned = Weak.lock())
			{
				*Last = ((*Pinned).*Method)();
			}
			return *Last;
		}));
	}

	static TAttribute Create(FGetter InGetter)
	{
		TAttribute A;
		A.Getter = std::move(InGetter);
		A.bIsSet = true;
		return A;
	}

	bool IsSet() const { return bIsSet; }
	bool IsBound() const { return static_cast<bool>(Getter); }

	T Get() const { return Getter ? Getter() : Value; }
	T Get(const T& DefaultValue) const { return bIsSet ? Get() : DefaultValue; }

	void Set(const T& InValue)
	{
		Getter = nullptr;
		Value = InValue;
		bIsSet = true;
	}

	void Bind(FGetter InGetter)
	{
		Getter = std::move(InGetter);
		bIsSet = true;
	}

private:
	T Value{};
	FGetter Getter;
	bool bIsSet = false;
};
