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

// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
// Landscape tessellation extracted from the GPL MapCore view renderer.
#include "Renderer/LandscapeDrawList.h"
#include "Containers/ContainerAliases.h"
#include <algorithm>
#include <cmath>
namespace
{
	// Sutherland-Hodgman in view space. Reuses two bounded, component-local buffers.
	void ClipTriangle(const FSceneViewVolume& View, const Vector3d Input[3], std::vector<Vector3d>& Points,
	    std::vector<Vector3d>& Scratch)
	{
		Points.assign(Input, Input + 3);
		for (int Plane = 0; Plane < 6 && !Points.empty(); Plane++)
		{
			const TFunction<float(const Vector3d&)> Distance = [&](const Vector3d& P)
			{
				const float D = View.Perspective ? P.GetY() : 1.0f;
				switch (Plane)
				{
				case 0:
					return P.GetY() - View.Minimum.GetY();
				case 1:
					return View.Maximum.GetY() - P.GetY();
				case 2:
					return P.GetX() - View.Minimum.GetX() * D;
				case 3:
					return View.Maximum.GetX() * D - P.GetX();
				case 4:
					return P.GetZ() - View.Minimum.GetZ() * D;
				default:
					return View.Maximum.GetZ() * D - P.GetZ();
				}
			};
			Scratch.clear();
			Vector3d Previous = Points.back();
			float PreviousD = Distance(Previous);
			for (const Vector3d& Current : Points)
			{
				const float CurrentD = Distance(Current);
				if ((PreviousD >= 0) != (CurrentD >= 0))
				{
					const float T = PreviousD / (PreviousD - CurrentD);
					Scratch.emplace_back(Previous.GetX() + (Current.GetX() - Previous.GetX()) * T,
					    Previous.GetY() + (Current.GetY() - Previous.GetY()) * T,
					    Previous.GetZ() + (Current.GetZ() - Previous.GetZ()) * T);
				}
				if (CurrentD >= 0)
					Scratch.push_back(Current);
				Previous = Current;
				PreviousD = CurrentD;
			}
			Points.swap(Scratch);
		}
	}
} // namespace
void BuildLandscapeTriangles(const Landscape& land, const Matrix44& trans, const Matrix44& absTrans,
    const Vector3d& scale, const FSceneViewVolume& Volume, float ProjectionScale, float TargetPixels,
    ILandscapeTriangleSink& Sink, FLandscapeDrawStats& Stats)
{

	Stats = {};
	FLandscapeRenderPlan plan;
	PlanLandscape(land, trans, Volume, true, ProjectionScale, 1.0f, TargetPixels, plan);
	// Lit as static meshes are (RenderMeshSolid).
	static const Vector3d lightDir = []
	{
		Vector3d light(0.35f, -0.5f, 0.79f);
		light.MultVector(1.0f / light.GetMag());
		return light;
	}();
	Vector3d origin, viewX, viewY, viewZ, cross, viewOrigin;
	absTrans.Transform(origin, Vector3d::origVec);
	const float isx = 1.0f / std::max(scale.GetX() * scale.GetX(), 1e-8f);
	const float isy = 1.0f / std::max(scale.GetY() * scale.GetY(), 1e-8f);
	const float isz = 1.0f / std::max(scale.GetZ() * scale.GetZ(), 1e-8f);
	trans.Transform(viewOrigin, Vector3d::origVec);
	(trans.Transform(viewX, Vector3d::xAxisVec)).SubVector(viewOrigin);
	(trans.Transform(viewY, Vector3d::yAxisVec)).SubVector(viewOrigin);
	(trans.Transform(viewZ, Vector3d::zAxisVec)).SubVector(viewOrigin);
	cross.CrossVector(viewY, viewZ);
	const bool mirrored = cross.DotVector(viewX) < 0.0f;

	const bool colored = land.HasColors();
	const float invX = 1.0f / land.GetQuadsX(), invY = 1.0f / land.GetQuadsY();

	FLandscapeComponentMesh component;
	std::vector<int>&xs = component.X, &ys = component.Y;
	std::vector<Vector3d>& pts = component.Points;
	std::vector<Vector3d> Clipped, Scratch;
	Clipped.reserve(12);
	Scratch.reserve(12);
	std::vector<float> shades, gridS, gridT;
	std::vector<unsigned int> cols;
	Vector3d n, e1, e2, faceNorm;
	FLandscapeTriangleVertex zpts[3];
	const TFunction<void(const Vector3d&, FLandscapeTriangleVertex&)> project =
	    [](const Vector3d& pt, FLandscapeTriangleVertex& zp) { zp.Position = pt; };
	const TFunction<void(FLandscapeTriangleVertex*, const float*, const unsigned int*)> submit =
	    [&](FLandscapeTriangleVertex vertices[3], const float shades[3], const unsigned int* colors)
	{
		FLandscapeTriangle triangle;
		triangle.bColored = colors != nullptr;
		for (int i = 0; i < 3; i++)
		{
			triangle.Vertices[i] = vertices[i];
			triangle.Vertices[i].Shade = shades[i];
			triangle.Vertices[i].Color = colors ? colors[i] : 0xFFFFFFFFu;
		}
		Sink.OnTriangle(triangle);
	};

	for (int cy = 0; cy < plan.cys; cy++)
	{
		for (int cx = 0; cx < plan.cxs; cx++)
		{
			const int step = plan.Step(cx, cy);
			if (step == 0)
				continue;
			const bool clip = plan.bounds[(size_t)cy * plan.cxs + cx] == 0;
			BuildLandscapeComponent(land, plan, cx, cy, trans, true, component);
			const int nx = (int)xs.size(), ny = (int)ys.size();
			shades.resize(pts.size());
			gridS.resize(pts.size());
			gridT.resize(pts.size());
			cols.resize(colored ? pts.size() : 0);
			for (int j = 0; j < ny; j++)
			{
				for (int i = 0; i < nx; i++)
				{
					const int x = xs[i], y = ys[j];
					const size_t k = (size_t)j * nx + i;
					const Vector3d& local = land.GetNormal(x, y);
					absTrans.Transform(n, local.GetX() * isx, local.GetY() * isy, local.GetZ() * isz);
					n.SubVector(origin);
					const float len = n.GetMag();
					const float ndl = len > 1e-8f ? n.DotVector(lightDir) / len : 0.0f;
					const float sky = len > 1e-8f ? n.GetZ() / len : 0.0f;
					shades[k] = std::min(1.0f, 0.32f + 0.58f * std::max(ndl, 0.0f) + 0.12f * (sky * 0.5f + 0.5f));
					gridS[k] = x * invX;
					gridT[k] = y * invY;
					if (colored)
						cols[k] = land.GetColor(x, y);
				}
			}
			Stats.Components++;

			const TFunction<void(size_t, size_t, size_t)> emit = [&](size_t a, size_t b, size_t c)
			{
				const size_t idx[3] = {a, b, c};
				const Vector3d &p0 = pts[a], &p1 = pts[b], &p2 = pts[c];
				e1.SubVector(p1, p0);
				e2.SubVector(p2, p0);
				faceNorm.CrossVector(e1, e2);
				const float facing = faceNorm.DotVector(p0);
				if (mirrored ? facing <= 0.0f : facing >= 0.0f)
					return;
				float sh[3];
				unsigned int co[3] = {0, 0, 0};
				for (int q = 0; q < 3; q++)
				{
					sh[q] = shades[idx[q]];
					if (colored)
						co[q] = cols[idx[q]];
				}
				if (!clip)
				{
					for (int q = 0; q < 3; q++)
					{
						if (pts[idx[q]].GetY() <= 0.0f)
							return;
						project(pts[idx[q]], zpts[q]);
						zpts[q].U = gridS[idx[q]];
						zpts[q].V = gridT[idx[q]];
					}
					submit(zpts, sh, colored ? co : NULL);
					Stats.Triangles++;
					return;
				}
				// Cut by the view's edges: the pieces keep their place on the grid
				// and their light, from where each corner is in the triangle.
				const Vector3d Input[3] = {p0, p1, p2};
				ClipTriangle(Volume, Input, Clipped, Scratch);
				const int numPts = static_cast<int>(Clipped.size());
				if (numPts < 3)
					return;
				const Vector3d* fvPtr = Clipped.data();
				unsigned int meanColor = 0xFF000000u;
				for (int ch = 0; ch < 3; ch++)
				{
					const unsigned int sum =
					    ((co[0] >> (8 * ch)) & 0xFF) + ((co[1] >> (8 * ch)) & 0xFF) + ((co[2] >> (8 * ch)) & 0xFF);
					meanColor |= (sum / 3) << (8 * ch);
				}
				const unsigned int flatColors[3] = {meanColor, meanColor, meanColor};
				float pieceShades[3];
				const TFunction<void(const Vector3d&, FLandscapeTriangleVertex&, float&)> locate =
				    [&](const Vector3d& pt, FLandscapeTriangleVertex& zp, float& shade)
				{
					Vector3d d0, d1, d2;
					d0.SubVector(p1, p0);
					d1.SubVector(p2, p0);
					d2.SubVector(pt, p0);
					const float a00 = d0.DotVector(d0), a01 = d0.DotVector(d1), a11 = d1.DotVector(d1);
					const float b0 = d2.DotVector(d0), b1 = d2.DotVector(d1);
					const float den = a00 * a11 - a01 * a01;
					const float w1 = den != 0.0f ? (a11 * b0 - a01 * b1) / den : 0.0f;
					const float w2 = den != 0.0f ? (a00 * b1 - a01 * b0) / den : 0.0f;
					const float w0 = 1.0f - w1 - w2;
					zp.U = w0 * gridS[a] + w1 * gridS[b] + w2 * gridS[c];
					zp.V = w0 * gridT[a] + w1 * gridT[b] + w2 * gridT[c];
					shade = w0 * sh[0] + w1 * sh[1] + w2 * sh[2];
				};
				if (fvPtr[0].GetY() <= 0.0f)
					return;
				project(fvPtr[0], zpts[0]);
				locate(fvPtr[0], zpts[0], pieceShades[0]);
				for (int i = 2; i < numPts; i++)
				{
					if (fvPtr[i - 1].GetY() <= 0.0f || fvPtr[i].GetY() <= 0.0f)
						continue;
					project(fvPtr[i - 1], zpts[1]);
					project(fvPtr[i], zpts[2]);
					locate(fvPtr[i - 1], zpts[1], pieceShades[1]);
					locate(fvPtr[i], zpts[2], pieceShades[2]);
					submit(zpts, pieceShades, colored ? flatColors : NULL);
					Stats.Triangles++;
				}
			};
			for (int j = 0; j + 1 < ny; j++)
			{
				for (int i = 0; i + 1 < nx; i++)
				{
					const size_t a = (size_t)j * nx + i, b = a + 1, c = a + 1 + nx, d = a + nx;
					// Counterclockwise seen from above, as the full mesh.
					emit(a, b, c);
					emit(a, c, d);
				}
			}
		}
	}
}
