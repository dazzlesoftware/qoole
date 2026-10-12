// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Simple delegates.
//
// TDelegate<R(Args...)> holds one callable: a lambda, a raw object + member
// function, or a shared object + member function (which unbinds itself when
// the object dies).
//
// TMulticastDelegate<void(Args...)> holds many; Add* returns a handle for
// Remove. Broadcast tolerates handlers adding or removing handlers.

#pragma once

#include "Containers/ContainerAliases.h"
#include "Misc/AssertionMacros.h"

#include <memory>

class FDelegateHandle
{
public:
	FDelegateHandle() = default;
	bool IsValid() const { return Id != 0; }
	void Reset() { Id = 0; }

	friend bool operator==(FDelegateHandle A, FDelegateHandle B) { return A.Id == B.Id; }

private:
	template <typename>
	friend class TMulticastDelegate;

	explicit FDelegateHandle(uint64 InId) : Id(InId) {}
	uint64 Id = 0;
};

template <typename Signature>
class TDelegate;

template <typename R, typename... Args>
class TDelegate<R(Args...)>
{
public:
	TDelegate() = default;

	template <typename F>
	static TDelegate CreateLambda(F&& Functor)
	{
		TDelegate D;
		D.BindLambda(std::forward<F>(Functor));
		return D;
	}

	template <typename T>
	static TDelegate CreateRaw(T* Object, R (T::*Method)(Args...))
	{
		TDelegate D;
		D.BindRaw(Object, Method);
		return D;
	}

	template <typename T>
	static TDelegate CreateRaw(const T* Object, R (T::*Method)(Args...) const)
	{
		TDelegate D;
		D.BindRaw(Object, Method);
		return D;
	}

	template <typename T, typename M>
	static TDelegate CreateSP(const std::shared_ptr<T>& Object, M Method)
	{
		TDelegate D;
		D.BindSP(Object, Method);
		return D;
	}

	template <typename F>
	void BindLambda(F&& Functor)
	{
		Function = std::forward<F>(Functor);
		Owner.reset();
		bHasOwner = false;
	}

	template <typename T>
	void BindRaw(T* Object, R (T::*Method)(Args...))
	{
		check(Object);
		Function = [Object, Method](Args... InArgs) -> R { return (Object->*Method)(std::forward<Args>(InArgs)...); };
		Owner.reset();
		bHasOwner = false;
	}

	template <typename T>
	void BindRaw(const T* Object, R (T::*Method)(Args...) const)
	{
		check(Object);
		Function = [Object, Method](Args... InArgs) -> R { return (Object->*Method)(std::forward<Args>(InArgs)...); };
		Owner.reset();
		bHasOwner = false;
	}

	// The object is held weakly; the delegate reports unbound once it dies.
	template <typename T, typename M>
	void BindSP(const std::shared_ptr<T>& Object, M Method)
	{
		check(Object);
		T* Raw = Object.get();
		Function = [Raw, Method](Args... InArgs) -> R { return (Raw->*Method)(std::forward<Args>(InArgs)...); };
		Owner = Object;
		bHasOwner = true;
	}

	void Unbind()
	{
		Function = nullptr;
		Owner.reset();
		bHasOwner = false;
	}

	bool IsBound() const { return Function && (!bHasOwner || !Owner.expired()); }
	explicit operator bool() const { return IsBound(); }

	R Execute(Args... InArgs) const
	{
		check(IsBound());
		// Keep a shared owner alive for the duration of the call.
		std::shared_ptr<const void> Pin = bHasOwner ? Owner.lock() : nullptr;
		return Function(std::forward<Args>(InArgs)...);
	}

	// Calls the delegate if bound; returns whether it was called.
	bool ExecuteIfBound(Args... InArgs) const
		requires std::is_void_v<R>
	{
		std::shared_ptr<const void> Pin = bHasOwner ? Owner.lock() : nullptr;
		if (!Function || (bHasOwner && !Pin))
		{
			return false;
		}
		Function(std::forward<Args>(InArgs)...);
		return true;
	}

private:
	std::function<R(Args...)> Function;
	std::weak_ptr<const void> Owner;
	bool bHasOwner = false;
};

template <typename Signature>
class TMulticastDelegate;

template <typename... Args>
class TMulticastDelegate<void(Args...)>
{
public:
	using FDelegate = TDelegate<void(Args...)>;

	FDelegateHandle Add(FDelegate Delegate)
	{
		const FDelegateHandle Handle(++LastId);
		Entries.push_back({ Handle, std::make_shared<FDelegate>(std::move(Delegate)) });
		return Handle;
	}

	template <typename F>
	FDelegateHandle AddLambda(F&& Functor)
	{
		return Add(FDelegate::CreateLambda(std::forward<F>(Functor)));
	}

	template <typename T, typename M>
	FDelegateHandle AddRaw(T* Object, M Method)
	{
		return Add(FDelegate::CreateRaw(Object, Method));
	}

	template <typename T, typename M>
	FDelegateHandle AddSP(const std::shared_ptr<T>& Object, M Method)
	{
		return Add(FDelegate::CreateSP(Object, Method));
	}

	bool Remove(FDelegateHandle Handle)
	{
		auto It = std::find_if(Entries.begin(), Entries.end(), [Handle](const FEntry& E) { return E.Handle == Handle; });
		if (It == Entries.end())
		{
			return false;
		}
		Entries.erase(It);
		return true;
	}

	void Clear() { Entries.clear(); }
	bool IsBound() const { return !Entries.empty(); }
	int32 Num() const { return static_cast<int32>(Entries.size()); }

	void Broadcast(Args... InArgs) const
	{
		// Iterate a snapshot so handlers may add/remove handlers. A handler
		// removed during the broadcast may still receive this one call.
		const TArray<FEntry> Snapshot = Entries;
		for (const FEntry& Entry : Snapshot)
		{
			Entry.Delegate->ExecuteIfBound(InArgs...);
		}
	}

private:
	struct FEntry
	{
		FDelegateHandle Handle;
		std::shared_ptr<FDelegate> Delegate;
	};

	TArray<FEntry> Entries;
	uint64 LastId = 0;
};
