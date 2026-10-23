// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Geometry/Math3D.h"
// View-space bounds: X/Z slopes for perspective, absolute bounds for orthographic.
struct FSceneViewVolume
{
	Vector3d Minimum, Maximum;
	bool Perspective = true;
	bool IsValid() const;
	int BoundBox(const Vector3d corners[8]) const;
};
