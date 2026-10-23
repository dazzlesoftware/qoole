// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "World/FoliageLandscapeAttachment.h"
#include "Landscape/Landscape.h"
#include "Foliage/Foliage.h"
#include <cmath>

std::shared_ptr<const Foliage> SnapFoliageToHeightfield(const Foliage& Plants, const Landscape& Before,
    const Landscape& After, const Matrix44& FoliageToTerrain, const Matrix44& TerrainToFoliage,
    float AttachmentTolerance)
{
	if (!std::isfinite(AttachmentTolerance) || AttachmentTolerance < 0)
		return nullptr;
	for (int R = 0; R < 4; ++R)
		for (int C = 0; C < 4; ++C)
			if (!std::isfinite(FoliageToTerrain.GetMatrix(R, C)) || !std::isfinite(TerrainToFoliage.GetMatrix(R, C)))
				return nullptr;
	std::vector<FoliageInstance> Instances = Plants.GetInstances();
	bool Moved = false;
	for (FoliageInstance& Instance : Instances)
	{
		if (Instance.type < 0 || Instance.type >= Plants.GetNumTypes())
			continue;
		FoliageType const& Type = Plants.GetTypes()[static_cast<size_t>(Instance.type)];
		Vector3d OnTerrain;
		FoliageToTerrain.Transform(OnTerrain, Instance.position);
		if (std::fabs(OnTerrain.GetX()) > After.GetSizeX() * 0.5f ||
		    std::fabs(OnTerrain.GetY()) > After.GetSizeY() * 0.5f)
			continue;
		const float Was = Before.SampleHeight(OnTerrain.GetX(), OnTerrain.GetY());
		const float Now = After.SampleHeight(OnTerrain.GetX(), OnTerrain.GetY());
		if (Was == Now || std::fabs(OnTerrain.GetZ() - Type.zOffset - Was) > AttachmentTolerance)
			continue;
		OnTerrain.NewVector(OnTerrain.GetX(), OnTerrain.GetY(), OnTerrain.GetZ() + Now - Was);
		Vector3d Position;
		TerrainToFoliage.Transform(Position, OnTerrain);
		if (!std::isfinite(Position.GetX()) || !std::isfinite(Position.GetY()) || !std::isfinite(Position.GetZ()))
			return nullptr;
		Instance.position = Position;
		Moved = true;
	}
	return Moved ? Plants.WithInstances(std::move(Instances)) : nullptr;
}
