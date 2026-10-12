// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Fixed-size integer types and basic platform macros used by every module.

#pragma once

#include <cstddef>
#include <cstdint>

using int8 = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;
using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
using SIZE_T = std::size_t;

// Returned by searches that find nothing.
inline constexpr int32 INDEX_NONE = -1;

#if defined(_WIN32)
	#define GENESIS_PLATFORM_WINDOWS 1
#else
	#define GENESIS_PLATFORM_WINDOWS 0
#endif

#ifndef GENESIS_DEBUG
	#define GENESIS_DEBUG 0
#endif

#define GENESIS_LIKELY(x) (x) [[likely]]
#define GENESIS_UNLIKELY(x) (x) [[unlikely]]

// Non-copyable helper base.
class FNoncopyable
{
protected:
	FNoncopyable() = default;
	~FNoncopyable() = default;

public:
	FNoncopyable(const FNoncopyable&) = delete;
	FNoncopyable& operator=(const FNoncopyable&) = delete;
};
