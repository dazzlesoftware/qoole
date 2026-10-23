/*
 * Landscape.cpp
 *
 * Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
 */

#include <math.h>
#include <functional>

#include "Landscape/Landscape.h"

Landscape::Landscape(int inQuadsX, int inQuadsY, float inQuadSize)
    : quadsX(GeometryMath::Max(1, GeometryMath::Min(inQuadsX, MaxQuads))),
      quadsY(GeometryMath::Max(1, GeometryMath::Min(inQuadsY, MaxQuads))), quadSize(GeometryMath::Max(inQuadSize, 1.0f))
{
	heights.assign((size_t)GetPointsX() * GetPointsY(), 0.0f);
	BuildShading();
}

std::shared_ptr<const Landscape> Landscape::Create(int inQuadsX, int inQuadsY, float inQuadSize,
    std::vector<float> inHeights, std::vector<LandscapeLayer> inLayers, std::vector<LandscapeSpline> inSplines)
{
	std::shared_ptr<Landscape> result = std::make_shared<Landscape>(inQuadsX, inQuadsY, inQuadSize);
	if (inHeights.size() != result->heights.size())
		return nullptr;
	for (const LandscapeLayer& layer : inLayers)
	{
		if (layer.weights.size() != result->heights.size())
			return nullptr;
	}
	result->heights = std::move(inHeights);
	result->layers = std::move(inLayers);
	result->splines = std::move(inSplines);
	result->BuildShading();
	return result;
}

float Landscape::GetWeight(int layer, int x, int y) const
{
	if (layer < 0 || layer >= (int)layers.size())
		return 0.0f;
	x = GeometryMath::Max(0, GeometryMath::Min(x, quadsX));
	y = GeometryMath::Max(0, GeometryMath::Min(y, quadsY));
	return layers[layer].weights[(size_t)y * GetPointsX() + x];
}

std::shared_ptr<const Landscape> Landscape::WithLayers(std::vector<LandscapeLayer> newLayers) const
{
	std::shared_ptr<const Landscape> result = Create(quadsX, quadsY, quadSize, heights, std::move(newLayers), splines);
	if (result)
		result->splineMeshes = splineMeshes; // the same splines
	return result;
}

std::shared_ptr<const Landscape> Landscape::WithSplines(std::vector<LandscapeSpline> newSplines) const
{
	return Create(quadsX, quadsY, quadSize, heights, layers, std::move(newSplines));
}

LandscapeLayer Landscape::MakeLayer(const char* layerName, unsigned char r, unsigned char g, unsigned char b) const
{
	LandscapeLayer layer;
	layer.name = layerName ? layerName : "";
	layer.r = r;
	layer.g = g;
	layer.b = b;
	layer.weights.assign(heights.size(), 0.0f);
	return layer;
}

float Landscape::GetHeight(int x, int y) const
{
	x = GeometryMath::Max(0, GeometryMath::Min(x, quadsX));
	y = GeometryMath::Max(0, GeometryMath::Min(y, quadsY));
	return heights[(size_t)y * GetPointsX() + x];
}

Vector3d Landscape::GetPoint(int x, int y) const
{
	return Vector3d(x * quadSize - GetSizeX() * 0.5f, y * quadSize - GetSizeY() * 0.5f, GetHeight(x, y));
}

float Landscape::SampleHeight(float localX, float localY) const
{
	const float gx = GeometryMath::Max(0.0f, GeometryMath::Min((localX + GetSizeX() * 0.5f) / quadSize, (float)quadsX));
	const float gy = GeometryMath::Max(0.0f, GeometryMath::Min((localY + GetSizeY() * 0.5f) / quadSize, (float)quadsY));
	const int x0 = GeometryMath::Min((int)gx, quadsX - 1);
	const int y0 = GeometryMath::Min((int)gy, quadsY - 1);
	const float fx = gx - x0;
	const float fy = gy - y0;
	const float h00 = GetHeight(x0, y0), h10 = GetHeight(x0 + 1, y0);
	const float h01 = GetHeight(x0, y0 + 1), h11 = GetHeight(x0 + 1, y0 + 1);
	return (h00 * (1.0f - fx) + h10 * fx) * (1.0f - fy) + (h01 * (1.0f - fx) + h11 * fx) * fy;
}

std::shared_ptr<const Landscape> Landscape::WithHeights(std::vector<float> newHeights) const
{
	std::shared_ptr<const Landscape> result = Create(quadsX, quadsY, quadSize, std::move(newHeights), layers, splines);
	if (result)
		result->splineMeshes = splineMeshes; // the same splines
	return result;
}

const std::vector<std::shared_ptr<const StaticMesh>>& Landscape::GetSplineMeshes(void) const
{
	if (!splineMeshes)
	{
		std::shared_ptr<
		    std::vector<std::shared_ptr<const StaticMesh>, std::allocator<std::shared_ptr<const StaticMesh>>>>
		    built = std::make_shared<std::vector<std::shared_ptr<const StaticMesh>>>();
		for (const LandscapeSpline& spline : splines)
			built->push_back(BuildSplineMesh(spline));
		splineMeshes = built;
	}
	return *splineMeshes;
}

std::shared_ptr<const StaticMesh> Landscape::BuildSplineMesh(const LandscapeSpline& spline)
{
	const std::shared_ptr<const StaticMesh> piece =
	    spline.mesh.empty() ? nullptr : StaticMesh::Find(spline.mesh.c_str());
	if (!piece || spline.points.size() < 2)
		return nullptr;
	Vector3d low, high;
	piece->GetBounds(low, high);
	const float pieceLength = high.GetX() - low.GetX();
	const float pieceWidth = GeometryMath::Max(high.GetY() - low.GetY(), 1e-3f);
	if (pieceLength < 1e-3f)
		return nullptr;

	// The curve by distance along it.
	std::vector<LandscapeSpline::Sample> samples;
	spline.SampleCurve(8.0f, samples);
	std::vector<float> along(samples.size(), 0.0f);
	for (size_t i = 1; i < samples.size(); i++)
	{
		Vector3d step(samples[i].position);
		step.SubVector(samples[i - 1].position);
		along[i] = along[i - 1] + step.GetMag();
	}
	const float total = along.back();
	if (total < 1.0f)
		return nullptr;
	const float scale = GeometryMath::Max(spline.meshScale, 0.01f);
	// A whole number of pieces, each stretched a little to fit.
	const int pieces = GeometryMath::Max(1, (int)floorf(total / (pieceLength * scale) + 0.5f));
	const float stretch = total / (pieces * pieceLength);
	// Where, which way and how wide the curve is at a distance along it.
	size_t cursor = 0;
	const std::function<void(float, Vector3d&, Vector3d&, float&)> at =
	    [&](float s, Vector3d& position, Vector3d& forward, float& width)
	{
		s = GeometryMath::Max(0.0f, GeometryMath::Min(s, total));
		while (cursor > 0 && along[cursor] > s)
			cursor--;
		while (cursor + 2 < samples.size() && along[cursor + 1] < s)
			cursor++;
		const size_t i = GeometryMath::Min(cursor, samples.size() - 2);
		const float span = GeometryMath::Max(along[i + 1] - along[i], 1e-6f);
		const float t = GeometryMath::Max(0.0f, GeometryMath::Min((s - along[i]) / span, 1.0f));
		const Vector3d &a = samples[i].position, &b = samples[i + 1].position;
		position.NewVector(a.GetX() + (b.GetX() - a.GetX()) * t, a.GetY() + (b.GetY() - a.GetY()) * t,
		    a.GetZ() + (b.GetZ() - a.GetZ()) * t);
		forward.SubVector(b, a);
		forward.MultVector(1.0f / GeometryMath::Max(forward.GetMag(), 1e-6f));
		width = samples[i].width + (samples[i + 1].width - samples[i].width) * t;
	};

	std::shared_ptr<StaticMesh> built = std::make_shared<StaticMesh>("SplineMesh");
	std::vector<unsigned int> colors;
	const int n = piece->GetNumVertices();
	std::vector<int> map(n);
	std::vector<Vector3d> frameX(n), frameY(n), frameZ(n);
	std::vector<float> sideScale(n);
	for (int p = 0; p < pieces; p++)
	{
		for (int v = 0; v < n; v++)
		{
			const Vector3d& local = piece->GetVertex(v);
			Vector3d position, forward;
			float width;
			at((p * pieceLength + (local.GetX() - low.GetX())) * stretch, position, forward, width);
			// The frame there: forward along the curve, left level across it,
			// up from both.
			Vector3d left(-forward.GetY(), forward.GetX(), 0.0f);
			if (left.GetMag() < 1e-6f)
				left.NewVector(0.0f, 1.0f, 0.0f);
			left.MultVector(1.0f / left.GetMag());
			Vector3d up;
			up.CrossVector(forward, left);
			const float across = spline.meshFitWidth ? width / pieceWidth : scale;
			Vector3d placed(position);
			Vector3d offset(left);
			offset.MultVector(local.GetY() * across);
			placed.AddVector(offset);
			offset = up;
			offset.MultVector(local.GetZ() * scale + spline.meshZOffset);
			placed.AddVector(offset);
			map[v] = built->AddVertex(placed);
			colors.push_back(piece->HasVertexColors() ? piece->GetVertexColor(v) : 0xFF808080u);
			frameX[v] = forward;
			frameY[v] = left;
			frameZ[v] = up;
			sideScale[v] = across;
		}
		// Normals through the frame, against the stretch.
		for (const StaticMesh::Triangle& tri : piece->GetTriangles())
		{
			Vector3d normals[3];
			for (int k = 0; k < 3; k++)
			{
				const Vector3d& nl = tri.norm[k];
				const int v = tri.v[k];
				Vector3d nx(frameX[v]), ny(frameY[v]), nz(frameZ[v]);
				nx.MultVector(nl.GetX() / (scale * stretch));
				ny.MultVector(nl.GetY() / sideScale[v]);
				nz.MultVector(nl.GetZ() / scale);
				nx.AddVector(ny).AddVector(nz);
				nx.MultVector(1.0f / GeometryMath::Max(nx.GetMag(), 1e-6f));
				normals[k] = nx;
			}
			built->AddTriangle(map[tri.v[0]], map[tri.v[1]], map[tri.v[2]], normals[0], normals[1], normals[2]);
		}
		for (const StaticMesh::Edge& edge : piece->GetEdgeList())
		{
			built->AddEdge(map[edge.GetStartPoint()], map[edge.GetEndPoint()]);
		}
	}
	built->SetVertexColors(std::move(colors));
	built->Finish();
	return built;
}

Vector3d LandscapeSpline::GetTangent(int point) const
{
	const int n = (int)points.size();
	if (point < 0 || point >= n || n < 2)
		return Vector3d(0.0f, 0.0f, 0.0f);
	if (!points[point].autoTangent)
		return points[point].tangent;
	// Catmull-Rom: half the way from the point before to the one after (the
	// ends repeat, or wrap when closed).
	const std::function<const Vector3d&(int)> at = [&](int i) -> const Vector3d&
	{
		if (closed)
			return points[((i % n) + n) % n].position;
		return points[GeometryMath::Max(0, GeometryMath::Min(i, n - 1))].position;
	};
	Vector3d tangent(at(point + 1));
	tangent.SubVector(at(point - 1));
	tangent.MultVector(0.5f);
	return tangent;
}

void LandscapeSpline::SampleCurve(float step, std::vector<Sample>& out) const
{
	out.clear();
	const int n = (int)points.size();
	if (n == 0)
		return;
	if (n == 1)
	{
		out.push_back({points[0].position, points[0].width, points[0].falloff});
		return;
	}
	const int segments = closed ? n : n - 1;
	step = GeometryMath::Max(step, 1.0f);
	for (int seg = 0; seg < segments; seg++)
	{
		const int i1 = seg, i2 = (seg + 1) % n;
		const LandscapeSplinePoint &p1 = points[i1], &p2 = points[i2];
		const Vector3d m1 = GetTangent(i1), m2 = GetTangent(i2);
		// As many steps as the chord and the tangents' pull need.
		Vector3d chord(p2.position);
		chord.SubVector(p1.position);
		const float reach = chord.GetMag() + 0.3f * (m1.GetMag() + m2.GetMag());
		const int steps = GeometryMath::Max(1, (int)ceilf(reach / step));
		for (int k = 0; k < steps; k++)
		{
			const float t = (float)k / steps, t2 = t * t, t3 = t2 * t;
			const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f, h10 = t3 - 2.0f * t2 + t, h01 = -2.0f * t3 + 3.0f * t2,
			            h11 = t3 - t2;
			Sample sample;
			sample.position.NewVector(
			    h00 * p1.position.GetX() + h10 * m1.GetX() + h01 * p2.position.GetX() + h11 * m2.GetX(),
			    h00 * p1.position.GetY() + h10 * m1.GetY() + h01 * p2.position.GetY() + h11 * m2.GetY(),
			    h00 * p1.position.GetZ() + h10 * m1.GetZ() + h01 * p2.position.GetZ() + h11 * m2.GetZ());
			sample.width = p1.width + (p2.width - p1.width) * t;
			sample.falloff = p1.falloff + (p2.falloff - p1.falloff) * t;
			out.push_back(sample);
		}
	}
	const LandscapeSplinePoint& end = closed ? points[0] : points[n - 1];
	out.push_back({end.position, end.width, end.falloff});
}

std::shared_ptr<const Landscape> Landscape::WithSplinesApplied(void) const
{
	if (splines.empty())
		return nullptr;
	std::vector<float> newHeights = heights;
	std::vector<LandscapeLayer> newLayers = layers;
	const int px = GetPointsX();
	const float halfX = GetSizeX() * 0.5f, halfY = GetSizeY() * 0.5f;
	std::vector<LandscapeSpline::Sample> samples;
	// Per point the strongest pull so far, so where splines cross the nearer
	// one wins (and each point moves once).
	std::vector<float> pull(heights.size(), 0.0f), target(heights.size(), 0.0f);
	std::vector<float> paint(heights.size(), 0.0f);
	std::vector<int> paintLayer(heights.size(), -1);
	for (const LandscapeSpline& spline : splines)
	{
		spline.SampleCurve(quadSize * 0.5f, samples);
		int layerIndex = -1;
		for (int l = 0; l < (int)newLayers.size(); l++)
		{
			if (!spline.layer.empty() && newLayers[l].name == spline.layer)
				layerIndex = l;
		}
		for (size_t i = 0; i + 1 < samples.size() || (samples.size() == 1 && i == 0); i++)
		{
			const LandscapeSpline::Sample& a = samples[i];
			const LandscapeSpline::Sample& b = samples.size() == 1 ? samples[i] : samples[i + 1];
			const float reach = GeometryMath::Max(a.width, b.width) * 0.5f + GeometryMath::Max(a.falloff, b.falloff);
			const float minX = GeometryMath::Min(a.position.GetX(), b.position.GetX()) - reach,
			            maxX = GeometryMath::Max(a.position.GetX(), b.position.GetX()) + reach;
			const float minY = GeometryMath::Min(a.position.GetY(), b.position.GetY()) - reach,
			            maxY = GeometryMath::Max(a.position.GetY(), b.position.GetY()) + reach;
			const int x0 = GeometryMath::Max(0, (int)floorf((minX + halfX) / quadSize)),
			          x1 = GeometryMath::Min(quadsX, (int)ceilf((maxX + halfX) / quadSize));
			const int y0 = GeometryMath::Max(0, (int)floorf((minY + halfY) / quadSize)),
			          y1 = GeometryMath::Min(quadsY, (int)ceilf((maxY + halfY) / quadSize));
			const float dx = b.position.GetX() - a.position.GetX(), dy = b.position.GetY() - a.position.GetY();
			const float len2 = dx * dx + dy * dy;
			for (int y = y0; y <= y1; y++)
			{
				for (int x = x0; x <= x1; x++)
				{
					const float gx = x * quadSize - halfX, gy = y * quadSize - halfY;
					// Nearest place on the piece of curve, and how far to its side.
					float t =
					    len2 > 1e-6f ? ((gx - a.position.GetX()) * dx + (gy - a.position.GetY()) * dy) / len2 : 0.0f;
					t = GeometryMath::Max(0.0f, GeometryMath::Min(t, 1.0f));
					const float sx = gx - (a.position.GetX() + dx * t), sy = gy - (a.position.GetY() + dy * t);
					const float side = sqrtf(sx * sx + sy * sy);
					const float half = (a.width + (b.width - a.width) * t) * 0.5f;
					const float fall = GeometryMath::Max(a.falloff + (b.falloff - a.falloff) * t, 1e-3f);
					float w;
					if (side <= half)
						w = 1.0f;
					else if (side >= half + fall)
						continue;
					else
					{
						const float u = 1.0f - (side - half) / fall;
						w = u * u * (3.0f - 2.0f * u);
					}
					const size_t index = (size_t)y * px + x;
					const float h = a.position.GetZ() + (b.position.GetZ() - a.position.GetZ()) * t;
					const bool up = h > heights[index], down = h < heights[index];
					if (w > pull[index] &&
					    ((up && spline.raiseTerrain) || (down && spline.lowerTerrain) || (!up && !down)))
					{
						pull[index] = w;
						target[index] = h;
					}
					if (layerIndex >= 0 && w > paint[index])
					{
						paint[index] = w;
						paintLayer[index] = layerIndex;
					}
				}
			}
		}
	}
	for (size_t i = 0; i < newHeights.size(); i++)
	{
		if (pull[i] > 0.0f)
			newHeights[i] += (target[i] - heights[i]) * pull[i];
		// The layer at least as strong as the spline there; the others give way.
		const int l = paintLayer[i];
		if (l < 0 || newLayers[l].weights[i] >= paint[i])
			continue;
		newLayers[l].weights[i] = paint[i];
		float others = 0.0f;
		for (int o = 0; o < (int)newLayers.size(); o++)
			others += o == l ? 0.0f : newLayers[o].weights[i];
		const float room = 1.0f - paint[i];
		if (others > room && others > 0.0f)
		{
			for (int o = 0; o < (int)newLayers.size(); o++)
			{
				if (o != l)
					newLayers[o].weights[i] *= room / others;
			}
		}
	}
	return Create(quadsX, quadsY, quadSize, std::move(newHeights), std::move(newLayers), splines);
}

void Landscape::BuildShading(void)
{
	const int px = GetPointsX();
	const int py = GetPointsY();

	// Smooth normals from the slope across each point.
	normals.resize((size_t)px * py);
	for (int y = 0; y < py; y++)
	{
		for (int x = 0; x < px; x++)
		{
			const float dx = (GetHeight(x + 1, y) - GetHeight(x - 1, y)) /
			    ((GeometryMath::Min(x + 1, quadsX) - GeometryMath::Max(x - 1, 0)) * quadSize);
			const float dy = (GetHeight(x, y + 1) - GetHeight(x, y - 1)) /
			    ((GeometryMath::Min(y + 1, quadsY) - GeometryMath::Max(y - 1, 0)) * quadSize);
			Vector3d n(-dx, -dy, 1.0f);
			n.MultVector(1.0f / n.GetMag());
			normals[(size_t)y * px + x] = n;
		}
	}

	// Colors: the layers blended over the ground's own color.
	colors.clear();
	if (!layers.empty())
	{
		unsigned char br, bg, bb;
		GetBaseColor(br, bg, bb);
		colors.resize((size_t)px * py);
		for (size_t i = 0; i < colors.size(); i++)
		{
			float sum = 0.0f, r = 0.0f, g = 0.0f, b = 0.0f;
			for (const LandscapeLayer& layer : layers)
			{
				const float w = layer.weights[i];
				sum += w;
				r += w * layer.r;
				g += w * layer.g;
				b += w * layer.b;
			}
			const float rest = GeometryMath::Max(0.0f, 1.0f - sum);
			const float scale = sum > 1.0f ? 1.0f / sum : 1.0f;
			const std::function<unsigned int(float)> channel = [](float v)
			{ return (unsigned int)GeometryMath::Max(0.0f, GeometryMath::Min(v + 0.5f, 255.0f)); };
			colors[i] = channel(r * scale + rest * br) | (channel(g * scale + rest * bg) << 8) |
			    (channel(b * scale + rest * bb) << 16) | 0xFF000000u;
		}
	}

	// Each component's lowest and highest heights (its edges included).
	const int cxs = GetComponentsX(), cys = GetComponentsY();
	componentLow.assign((size_t)cxs * cys, 0.0f);
	componentHigh.assign((size_t)cxs * cys, 0.0f);
	minHeight = maxHeight = heights[0];
	for (int cy = 0; cy < cys; cy++)
	{
		for (int cx = 0; cx < cxs; cx++)
		{
			float lo = GetHeight(cx * ComponentQuads, cy * ComponentQuads), hi = lo;
			for (int y = cy * ComponentQuads; y <= GeometryMath::Min((cy + 1) * ComponentQuads, quadsY); y++)
			{
				for (int x = cx * ComponentQuads; x <= GeometryMath::Min((cx + 1) * ComponentQuads, quadsX); x++)
				{
					const float h = heights[(size_t)y * px + x];
					lo = GeometryMath::Min(lo, h);
					hi = GeometryMath::Max(hi, h);
				}
			}
			componentLow[(size_t)cy * cxs + cx] = lo;
			componentHigh[(size_t)cy * cxs + cx] = hi;
			minHeight = GeometryMath::Min(minHeight, lo);
			maxHeight = GeometryMath::Max(maxHeight, hi);
		}
	}
	mesh.reset();
}

const std::shared_ptr<const StaticMesh>& Landscape::GetMesh(void) const
{
	if (mesh)
		return mesh;
	std::shared_ptr<StaticMesh> built = std::make_shared<StaticMesh>("Landscape");
	const int px = GetPointsX();
	const int py = GetPointsY();
	for (int y = 0; y < py; y++)
		for (int x = 0; x < px; x++)
			built->AddVertex(GetPoint(x, y));
	const std::function<int(int, int)> index = [px](int x, int y) { return y * px + x; };
	for (int y = 0; y < quadsY; y++)
	{
		for (int x = 0; x < quadsX; x++)
		{
			const int a = index(x, y), b = index(x + 1, y), c = index(x + 1, y + 1), d = index(x, y + 1);
			// Counterclockwise seen from above.
			built->AddTriangle(a, b, c, normals[a], normals[b], normals[c]);
			built->AddTriangle(a, c, d, normals[a], normals[c], normals[d]);
		}
	}
	// The grid's lines (no diagonals), as Unreal's landscape wireframe.
	for (int y = 0; y < py; y++)
		for (int x = 0; x < quadsX; x++)
			built->AddEdge(index(x, y), index(x + 1, y));
	for (int x = 0; x < px; x++)
		for (int y = 0; y < quadsY; y++)
			built->AddEdge(index(x, y), index(x, y + 1));
	if (!colors.empty())
		built->SetVertexColors(colors);
	built->Finish();
	mesh = built;
	return mesh;
}

void Landscape::GetComponentHeights(int cx, int cy, float& low, float& high) const
{
	const size_t i = (size_t)GeometryMath::Max(0, GeometryMath::Min(cy, GetComponentsY() - 1)) * GetComponentsX() +
	    GeometryMath::Max(0, GeometryMath::Min(cx, GetComponentsX() - 1));
	low = componentLow[i];
	high = componentHigh[i];
}

void Landscape::GetBounds(Vector3d& low, Vector3d& high) const
{
	low.NewVector(-GetSizeX() * 0.5f, -GetSizeY() * 0.5f, minHeight);
	high.NewVector(GetSizeX() * 0.5f, GetSizeY() * 0.5f, maxHeight);
	// And the splines' meshes.
	for (const std::shared_ptr<const StaticMesh>& splineMesh : GetSplineMeshes())
	{
		if (!splineMesh)
			continue;
		Vector3d ml, mh;
		splineMesh->GetBounds(ml, mh);
		low.NewVector(GeometryMath::Min(low.GetX(), ml.GetX()), GeometryMath::Min(low.GetY(), ml.GetY()),
		    GeometryMath::Min(low.GetZ(), ml.GetZ()));
		high.NewVector(GeometryMath::Max(high.GetX(), mh.GetX()), GeometryMath::Max(high.GetY(), mh.GetY()),
		    GeometryMath::Max(high.GetZ(), mh.GetZ()));
	}
}

float Landscape::GetBoundRadius(void) const
{
	Vector3d low, high;
	GetBounds(low, high);
	const float hx = GeometryMath::Max(fabsf(low.GetX()), fabsf(high.GetX())),
	            hy = GeometryMath::Max(fabsf(low.GetY()), fabsf(high.GetY()));
	const float hz = GeometryMath::Max(fabsf(low.GetZ()), fabsf(high.GetZ()));
	return sqrtf(hx * hx + hy * hy + hz * hz);
}

void Landscape::QuadCorners(int x, int y, Vector3d& a, Vector3d& b, Vector3d& c, Vector3d& d) const
{
	a = GetPoint(x, y);
	b = GetPoint(x + 1, y);
	c = GetPoint(x + 1, y + 1);
	d = GetPoint(x, y + 1);
}

bool Landscape::Raycast(const Line& ray, float rangeBegin, float rangeEnd, float& depth) const
{
	Vector3d dir(ray.GetSlope());
	const float dirLen = dir.GetMag();
	if (dirLen < 1e-8f)
		return false;
	dir.MultVector(1.0f / dirLen);
	const Vector3d& origin = ray.GetPoint();

	// Into the box it fills: where the ray enters and leaves it.
	Vector3d low, high;
	GetBounds(low, high);
	float t0 = rangeBegin, t1 = rangeEnd;
	const float o[3] = {origin.GetX(), origin.GetY(), origin.GetZ()};
	const float d[3] = {dir.GetX(), dir.GetY(), dir.GetZ()};
	const float lo[3] = {low.GetX(), low.GetY(), low.GetZ() - 0.01f};
	const float hi[3] = {high.GetX(), high.GetY(), high.GetZ() + 0.01f};
	for (int axis = 0; axis < 3; axis++)
	{
		if (fabsf(d[axis]) < 1e-12f)
		{
			if (o[axis] < lo[axis] || o[axis] > hi[axis])
				return false;
			continue;
		}
		float a = (lo[axis] - o[axis]) / d[axis], b = (hi[axis] - o[axis]) / d[axis];
		if (a > b)
		{
			const float swap = a;
			a = b;
			b = swap;
		}
		t0 = GeometryMath::Max(t0, a);
		t1 = GeometryMath::Min(t1, b);
		if (t0 > t1)
			return false;
	}

	// Moller-Trumbore on a triangle, from either side.
	bool hit = false;
	const std::function<void(const Vector3d&, const Vector3d&, const Vector3d&)> triangle =
	    [&](const Vector3d& v0, const Vector3d& v1, const Vector3d& v2)
	{
		Vector3d e1, e2, p, sv, q;
		e1.SubVector(v1, v0);
		e2.SubVector(v2, v0);
		p.CrossVector(dir, e2);
		const float det = e1.DotVector(p);
		if (fabsf(det) < 1e-8f)
			return;
		const float inv = 1.0f / det;
		sv.SubVector(origin, v0);
		const float u = sv.DotVector(p) * inv;
		if (u < 0.0f || u > 1.0f)
			return;
		q.CrossVector(sv, e1);
		const float v = dir.DotVector(q) * inv;
		if (v < 0.0f || u + v > 1.0f)
			return;
		const float t = e2.DotVector(q) * inv;
		if (t < rangeBegin || t > rangeEnd)
			return;
		if (!hit || t < depth)
		{
			depth = t;
			hit = true;
		}
	};

	// Walk the cells under the ray, nearest first (Amanatides and Woo); the
	// first cell with a hit has the nearest one.
	const float gx = (o[0] + d[0] * t0 + GetSizeX() * 0.5f) / quadSize;
	const float gy = (o[1] + d[1] * t0 + GetSizeY() * 0.5f) / quadSize;
	int cx = GeometryMath::Max(0, GeometryMath::Min((int)floorf(gx), quadsX - 1));
	int cy = GeometryMath::Max(0, GeometryMath::Min((int)floorf(gy), quadsY - 1));
	const int stepX = d[0] > 0.0f ? 1 : -1;
	const int stepY = d[1] > 0.0f ? 1 : -1;
	const float cellT = quadSize;
	const float deltaX = fabsf(d[0]) > 1e-12f ? cellT / fabsf(d[0]) : 1e30f;
	const float deltaY = fabsf(d[1]) > 1e-12f ? cellT / fabsf(d[1]) : 1e30f;
	const std::function<float(float, int, int, float)> nextBoundary = [&](float g, int c, int step, float dd)
	{
		if (fabsf(dd) < 1e-12f)
			return 1e30f;
		const float edge = (float)(step > 0 ? c + 1 : c);
		return t0 + (edge - g) * quadSize / dd;
	};
	float tMaxX = nextBoundary(gx, cx, stepX, d[0]);
	float tMaxY = nextBoundary(gy, cy, stepY, d[1]);
	Vector3d a, b, c, dd;
	while (cx >= 0 && cy >= 0 && cx < quadsX && cy < quadsY)
	{
		QuadCorners(cx, cy, a, b, c, dd);
		triangle(a, b, c);
		triangle(a, c, dd);
		if (hit)
			return true;
		if (GeometryMath::Min(tMaxX, tMaxY) > t1)
			break;
		if (tMaxX < tMaxY)
		{
			cx += stepX;
			tMaxX += deltaX;
		}
		else
		{
			cy += stepY;
			tMaxY += deltaY;
		}
	}
	return false;
}
