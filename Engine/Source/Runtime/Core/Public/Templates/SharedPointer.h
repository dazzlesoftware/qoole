// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Shared pointers. TSharedPtr/TWeakPtr are std::shared_ptr/std::weak_ptr;
// TSharedRef is a shared pointer that is never null (checked on creation),
// which lets APIs say "this always exists" in their signature.

#pragma once

#include "Misc/AssertionMacros.h"

#include <memory>

template <typename T>
using TSharedPtr = std::shared_ptr<T>;

template <typename T>
using TWeakPtr = std::weak_ptr<T>;

template <typename T>
using TSharedFromThis = std::enable_shared_from_this<T>;

template <typename T>
class TSharedRef
{
public:
	// Takes ownership of a new object.
	explicit TSharedRef(T* InObject)
		: Ptr(InObject)
	{
		check(Ptr);
	}

	// From a shared pointer that must not be null.
	explicit TSharedRef(std::shared_ptr<T> InPtr)
		: Ptr(std::move(InPtr))
	{
		check(Ptr);
	}

	template <typename U>
		requires std::is_convertible_v<U*, T*>
	TSharedRef(const TSharedRef<U>& Other)
		: Ptr(Other.ToSharedPtr())
	{
	}

	T& Get() const { return *Ptr; }
	T* operator->() const { return Ptr.get(); }
	T& operator*() const { return *Ptr; }

	const std::shared_ptr<T>& ToSharedPtr() const { return Ptr; }
	// To a shared pointer of this type or a base.
	template <typename U>
		requires std::is_convertible_v<T*, U*>
	operator std::shared_ptr<U>() const
	{
		return Ptr;
	}

	template <typename U>
		requires std::is_convertible_v<T*, U*>
	operator std::weak_ptr<U>() const
	{
		return Ptr;
	}

	friend bool operator==(const TSharedRef& A, const TSharedRef& B) { return A.Ptr == B.Ptr; }

private:
	std::shared_ptr<T> Ptr;
};

template <typename T, typename... Args>
TSharedRef<T> MakeShared(Args&&... InArgs)
{
	return TSharedRef<T>(std::make_shared<T>(std::forward<Args>(InArgs)...));
}

// Converts a non-null shared pointer to a reference (checked).
template <typename T>
TSharedRef<T> ToSharedRef(const TSharedPtr<T>& Ptr)
{
	return TSharedRef<T>(Ptr);
}

template <typename To, typename From>
TSharedPtr<To> StaticCastSharedPtr(const TSharedPtr<From>& Ptr)
{
	return std::static_pointer_cast<To>(Ptr);
}
