// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "CoreTypes.h"
namespace WorldCompositionLimits
{
	inline constexpr int32 SchemaVersion = 1;
	inline constexpr int32 MaxTiles = 65536;
	// A tile spans at most this many grid cells a side. It is also the spatial
	// index bucket size, so a tile touches at most four buckets.
	inline constexpr int32 MaxTileCells = 64;
	inline constexpr int32 MaxNameLength = 256;
	inline constexpr int32 MaxHostLength = 1024;
	inline constexpr double MinCellSize = 1.0;
	inline constexpr double MaxCellSize = 1.0e6;
	// Matches the scene's coordinate bound (FTransform3D).
	inline constexpr double MaxWorldExtent = 1.0e9;
	inline constexpr double DefaultCellSize = 16384.0;
	inline constexpr double DefaultLoadDistance = 16384.0;
	inline constexpr double DefaultUnloadDistance = 24576.0;
	inline constexpr int32 DefaultMaxLoadsPerUpdate = 4;
} // namespace WorldCompositionLimits
