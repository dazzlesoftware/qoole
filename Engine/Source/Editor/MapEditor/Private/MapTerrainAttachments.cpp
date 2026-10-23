// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "MapTerrainAttachments.h"
#include "MapDocument.h"
#include "MapCoreHeaders.h"
#include "World/FoliageLandscapeAttachment.h"

namespace
{
Matrix44 ToScope(const Object& Obj)
{
	Matrix44 M;
	Obj.CalTransSpaceMatrix(M.SetIdentity());
	return M;
}
Matrix44 ToLocal(const Object& Obj)
{
	Matrix44 M;
	Obj.CalInvTransSpaceMatrix(M.SetIdentity());
	return M;
}
} // namespace
void SnapFoliageToLandscape(FMapDocument& Document, const Object& Land, const Landscape& Before)
{
	if (!Land.HasLandscape())
	{
		return;
	}
	const Landscape& After = Land.GetLandscape();
	const Matrix44 LandToScope = ToScope(Land);
	const Matrix44 ScopeToLand = ToLocal(Land);
	for (Object* Obj : Document.GetObjects())
	{
		if (!Obj->HasFoliage())
		{
			continue;
		}
		const Matrix44 FoliageToScope = ToScope(*Obj);
		const Matrix44 ScopeToFoliage = ToLocal(*Obj);
		Matrix44 FoliageToLand, LandToFoliage;
		FoliageToLand.Multiply(ScopeToLand, FoliageToScope);
		LandToFoliage.Multiply(ScopeToFoliage, LandToScope);
		if (auto Updated = SnapFoliageToHeightfield(Obj->GetFoliage(), Before, After, FoliageToLand, LandToFoliage))
		{
			Document.SetFoliage(*Obj, std::move(Updated), "Sculpt Landscape");
		}
	}
}
