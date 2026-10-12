// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Unreal-style names for the standard containers (docs/GenesisUI.md §2).
// They are plain aliases so the standard algorithms and range-for work as
// usual; a custom implementation can replace one later without touching call
// sites.

#pragma once

#include "CoreTypes.h"

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

template <typename T>
using TArray = std::vector<T>;

template <typename T, SIZE_T N>
using TStaticArray = std::array<T, N>;

template <typename T>
using TArrayView = std::span<T>;

template <typename K, typename V, typename Hash = std::hash<K>>
using TMap = std::unordered_map<K, V, Hash>;

template <typename T, typename Hash = std::hash<T>>
using TSet = std::unordered_set<T, Hash>;

template <typename A, typename B>
using TPair = std::pair<A, B>;

template <typename T>
using TOptional = std::optional<T>;

template <typename Signature>
using TFunction = std::function<Signature>;

template <typename T, typename D = std::default_delete<T>>
using TUniquePtr = std::unique_ptr<T, D>;

template <typename T, typename... Args>
TUniquePtr<T> MakeUnique(Args&&... InArgs)
{
	return std::make_unique<T>(std::forward<Args>(InArgs)...);
}

// Small helpers for the most common TArray chores.
namespace ArrayUtil
{
	template <typename T>
	int32 Num(const TArray<T>& Array)
	{
		return static_cast<int32>(Array.size());
	}

	template <typename T, typename U>
	int32 Find(const TArray<T>& Array, const U& Value)
	{
		auto It = std::find(Array.begin(), Array.end(), Value);
		return It == Array.end() ? INDEX_NONE : static_cast<int32>(It - Array.begin());
	}

	template <typename T, typename U>
	bool Contains(const TArray<T>& Array, const U& Value)
	{
		return std::find(Array.begin(), Array.end(), Value) != Array.end();
	}

	// Removes every element equal to Value; returns how many were removed.
	template <typename T, typename U>
	int32 Remove(TArray<T>& Array, const U& Value)
	{
		const SIZE_T Before = Array.size();
		std::erase(Array, Value);
		return static_cast<int32>(Before - Array.size());
	}

	// Removes by swapping with the last element (does not keep order).
	template <typename T>
	void RemoveAtSwap(TArray<T>& Array, int32 Index)
	{
		if (Index != static_cast<int32>(Array.size()) - 1)
		{
			Array[Index] = std::move(Array.back());
		}
		Array.pop_back();
	}
}
