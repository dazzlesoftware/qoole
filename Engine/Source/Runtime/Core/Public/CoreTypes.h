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
#define PLATFORM_WINDOWS 1
#else
#define PLATFORM_WINDOWS 0
#endif

// Engine.props sets this; the projects that keep their own settings (MapCore,
// GenesisTests) follow the usual NDEBUG convention.
#ifndef GENESIS_BUILD_DEBUG
#ifdef NDEBUG
#define GENESIS_BUILD_DEBUG 0
#else
#define GENESIS_BUILD_DEBUG 1
#endif
#endif

#define LIKELY(x) (!!(x))
#define UNLIKELY(x) (!!(x))

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
