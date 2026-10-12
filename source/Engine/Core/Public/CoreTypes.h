/*
Copyright (C) 2026 Dazzle Software, LLC

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#pragma once

// Fixed-size integer types used across Genesis modules.

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

#if defined(_MSC_VER)
#define GENESIS_DEBUG_BREAK() __debugbreak()
#else
#define GENESIS_DEBUG_BREAK() ((void)0)
#endif

// Checks are compiled in for debug builds, like MFC's ASSERT was.
#ifndef GENESIS_DO_CHECK
#ifdef NDEBUG
#define GENESIS_DO_CHECK 0
#else
#define GENESIS_DO_CHECK 1
#endif
#endif
