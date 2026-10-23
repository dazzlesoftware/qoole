// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "CoreTypes.h"

// Shared validation budgets. Keep these stable when reading existing packages;
// increasing a budget does not change the serialized schema.
namespace MaterialLimits
{
	inline constexpr int32 MaxParentDepth = 32;
	inline constexpr int32 MaxFunctionDepth = 16;
	inline constexpr int32 MaxEmbeddedFunctionGraphs = 65;
	inline constexpr int32 MaxEmbeddedFunctionNodes = 4096;
	inline constexpr int32 MaxCollectionParameters = 256;
	inline constexpr int32 MaxParameterNameLength = 128;
	inline constexpr float MaxParameterMagnitude = 1.0e6f;
	inline constexpr int32 MaxShaderBytes = 4 * 1024 * 1024;
} // namespace MaterialLimits
