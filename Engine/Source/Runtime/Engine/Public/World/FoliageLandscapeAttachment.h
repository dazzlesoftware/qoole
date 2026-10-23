// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include <memory>
class Foliage;
class Landscape;
class Matrix44;

// Reproject grounded instances after a heightfield edit. Matrices map directly
// between the two local spaces. Floating/outside instances are retained. Returns
// null when nothing moved; source snapshots remain immutable. Units and the
// attachment tolerance are in heightfield-local space.
std::shared_ptr<const Foliage> SnapFoliageToHeightfield(const Foliage& Plants, const Landscape& Before,
														const Landscape& After, const Matrix44& FoliageToTerrain,
														const Matrix44& TerrainToFoliage,
														float AttachmentTolerance = 2.0f);
