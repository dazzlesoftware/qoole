// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Renderer/SceneViewVolume.h"
#include <cmath>
int FSceneViewVolume::BoundBox(const Vector3d corners[8]) const
{
	unsigned char all = 0xFF, any = 0;
	for (int i = 0; i < 8; i++)
	{
		const Vector3d& p = corners[i];
		const float depth = Perspective ? p.GetY() : 1.0f;
		unsigned char code = 0;
		if (p.GetX() < Minimum.GetX() * depth)
			code |= 1;
		if (p.GetX() > Maximum.GetX() * depth)
			code |= 2;
		if (p.GetZ() < Minimum.GetZ() * depth)
			code |= 4;
		if (p.GetZ() > Maximum.GetZ() * depth)
			code |= 8;
		if (p.GetY() < Minimum.GetY())
			code |= 16;
		if (p.GetY() > Maximum.GetY())
			code |= 32;
		all &= code;
		any |= code;
	}
	return all ? -1 : (any ? 0 : 1);
}

bool FSceneViewVolume::IsValid() const
{
	for (float V : {Minimum.GetX(), Minimum.GetY(), Minimum.GetZ(), Maximum.GetX(), Maximum.GetY(), Maximum.GetZ()})
		if (!std::isfinite(V))
			return false;
	return Minimum.GetX() < Maximum.GetX() && Minimum.GetY() < Maximum.GetY() && Minimum.GetZ() < Maximum.GetZ() &&
	    (!Perspective || Minimum.GetY() > 0);
}
