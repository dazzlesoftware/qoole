// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "CoreTypes.h"
namespace ScriptLimits
{
	inline constexpr int32 MaxTransferBytes = 4 * 1024 * 1024;
	inline constexpr int32 MaxJsonNodes = 100000;
	inline constexpr int32 MaxJsonDepth = 64;
	inline constexpr int32 MaxNameLength = 128;
	inline constexpr int32 MaxModules = 256;
	inline constexpr int32 MaxErrorBytes = 4096;
	inline constexpr int32 LuaStackReserve = 8;
	inline constexpr int32 JsonNodeBytes = 16;
} // namespace ScriptLimits
