/*
Copyright (C) 1996-1997 GX Media, Inc.

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

#include "Renderer/LandscapeRenderPlan.h"
#include "Containers/ContainerAliases.h"
#include <algorithm>
#include <cmath>

// The coarsest step (a power of two, a component at most) at which a
// quad that looks quadPixels big at step 1 stays within target pixels.
static int LandscapeStep(float quadPixels, float target)
{
	int step = 1;
	while (step < Landscape::ComponentQuads && quadPixels * (float)(step * 2) <= target)
		step *= 2;
	return step;
}

// The grid lines a component is drawn with at a step: from its first
// to its last, every step (the last always).
static void LandscapeSamples(int first, int last, int step, std::vector<int>& out)
{
	out.clear();
	for (int i = first; i < last; i += step)
		out.push_back(i);
	out.push_back(last);
}

// What each component of a landscape is drawn as this frame: culled
// (step 0) or at a step, and whether it crosses the view's edges.

// Zoom: what lines are scaled by after (FDraw::GetZoom); faces are not.
void PlanLandscape(const Landscape& land, const Matrix44& trans, const FSceneViewVolume& clip, bool perspective,
    float winWidth, float zoom, float target, FLandscapeRenderPlan& plan)
{
	plan.cxs = land.GetComponentsX();
	plan.cys = land.GetComponentsY();
	plan.steps.assign((size_t)plan.cxs * plan.cys, 0);
	plan.bounds.assign((size_t)plan.cxs * plan.cys, -1);
	const float qs = land.GetQuadSize();
	// How long a quad's side is in view space.
	Vector3d o, ax, ay;
	trans.Transform(o, Vector3d::origVec);
	trans.Transform(ax, Vector3d(qs, 0.0f, 0.0f)).SubVector(o);
	trans.Transform(ay, Vector3d(0.0f, qs, 0.0f)).SubVector(o);
	// Seen from the side (an orthographic view), only what crosses the
	// screen counts: the depth axis is y.
	const float quadView = zoom *
	    (perspective ? std::max(ax.GetMag(), ay.GetMag())
	                 : std::max(sqrtf(ax.GetX() * ax.GetX() + ax.GetZ() * ax.GetZ()),
	                       sqrtf(ay.GetX() * ay.GetX() + ay.GetZ() * ay.GetZ())));
	const int orthoStep = LandscapeStep(quadView, target);
	Vector3d corners[8];
	for (int cy = 0; cy < plan.cys; cy++)
	{
		for (int cx = 0; cx < plan.cxs; cx++)
		{
			float lo, hi;
			land.GetComponentHeights(cx, cy, lo, hi);
			const int x0 = cx * Landscape::ComponentQuads,
			          x1 = std::min(x0 + Landscape::ComponentQuads, land.GetQuadsX());
			const int y0 = cy * Landscape::ComponentQuads,
			          y1 = std::min(y0 + Landscape::ComponentQuads, land.GetQuadsY());
			const float lx0 = x0 * qs - land.GetSizeX() * 0.5f, lx1 = x1 * qs - land.GetSizeX() * 0.5f;
			const float ly0 = y0 * qs - land.GetSizeY() * 0.5f, ly1 = y1 * qs - land.GetSizeY() * 0.5f;
			Vector3d nearMin, nearMax;
			for (int i = 0; i < 8; i++)
			{
				trans.Transform(
				    corners[i], Vector3d((i & 1) ? lx1 : lx0, (i & 2) ? ly1 : ly0, (i & 4) ? hi + 0.01f : lo - 0.01f));
				if (i == 0)
				{
					nearMin = nearMax = corners[0];
					continue;
				}
				nearMin.NewVector(std::min(nearMin.GetX(), corners[i].GetX()),
				    std::min(nearMin.GetY(), corners[i].GetY()), std::min(nearMin.GetZ(), corners[i].GetZ()));
				nearMax.NewVector(std::max(nearMax.GetX(), corners[i].GetX()),
				    std::max(nearMax.GetY(), corners[i].GetY()), std::max(nearMax.GetZ(), corners[i].GetZ()));
			}
			const size_t c = (size_t)cy * plan.cxs + cx;
			plan.bounds[c] = clip.BoundBox(corners);
			if (plan.bounds[c] < 0)
				continue;
			if (!perspective)
			{
				plan.steps[c] = orthoStep;
				continue;
			}
			// The camera is the origin: how far the component's box is from it.
			const float dx = std::max(std::max(nearMin.GetX(), -nearMax.GetX()), 0.0f);
			const float dy = std::max(std::max(nearMin.GetY(), -nearMax.GetY()), 0.0f);
			const float dz = std::max(std::max(nearMin.GetZ(), -nearMax.GetZ()), 0.0f);
			const float distance = std::max(sqrtf(dx * dx + dy * dy + dz * dz), 1.0f);
			plan.steps[c] = LandscapeStep(quadView * winWidth / distance, target);
		}
	}
}

// A grid point's height, or, on an edge it shares with a component drawn
// coarser, where that component's edge passes (so no crack opens).
static float LandscapeEdgeHeight(const Landscape& land, const FLandscapeRenderPlan& plan, int cx, int cy, int step,
    int x, int y, int x0, int x1, int y0, int y1)
{
	const TFunction<float(int, int, int, int, bool, int)> along =
	    [&](int i, int first, int last, int coarse, bool horizontal, int fixed)
	{
		const int a = std::min(first + ((i - first) / coarse) * coarse, last);
		const int b = std::min(a + coarse, last);
		if (b == a)
			return horizontal ? land.GetHeight(a, fixed) : land.GetHeight(fixed, a);
		const float t = (float)(i - a) / (float)(b - a);
		const float ha = horizontal ? land.GetHeight(a, fixed) : land.GetHeight(fixed, a);
		const float hb = horizontal ? land.GetHeight(b, fixed) : land.GetHeight(fixed, b);
		return ha + (hb - ha) * t;
	};
	int coarse;
	if (y == y0 && (coarse = plan.Step(cx, cy - 1)) > step)
		return along(x, x0, x1, coarse, true, y);
	if (y == y1 && (coarse = plan.Step(cx, cy + 1)) > step)
		return along(x, x0, x1, coarse, true, y);
	if (x == x0 && (coarse = plan.Step(cx - 1, cy)) > step)
		return along(y, y0, y1, coarse, false, x);
	if (x == x1 && (coarse = plan.Step(cx + 1, cy)) > step)
		return along(y, y0, y1, coarse, false, x);
	return land.GetHeight(x, y);
}
void BuildLandscapeComponent(const Landscape& land, const FLandscapeRenderPlan& plan, int cx, int cy,
    const Matrix44& transform, bool stitch, FLandscapeComponentMesh& out)
{
	out.X.clear();
	out.Y.clear();
	out.Points.clear();
	const int step = plan.Step(cx, cy);
	if (!step)
		return;
	const int x0 = cx * Landscape::ComponentQuads, x1 = std::min(x0 + Landscape::ComponentQuads, land.GetQuadsX());
	const int y0 = cy * Landscape::ComponentQuads, y1 = std::min(y0 + Landscape::ComponentQuads, land.GetQuadsY());
	LandscapeSamples(x0, x1, step, out.X);
	LandscapeSamples(y0, y1, step, out.Y);
	out.Points.resize(out.X.size() * out.Y.size());
	for (size_t j = 0; j < out.Y.size(); j++)
		for (size_t i = 0; i < out.X.size(); i++)
		{
			const int x = out.X[i], y = out.Y[j];
			const float h =
			    stitch ? LandscapeEdgeHeight(land, plan, cx, cy, step, x, y, x0, x1, y0, y1) : land.GetHeight(x, y);
			transform.Transform(out.Points[j * out.X.size() + i],
			    Vector3d(x * land.GetQuadSize() - land.GetSizeX() * 0.5f,
			        y * land.GetQuadSize() - land.GetSizeY() * 0.5f, h));
		}
}
