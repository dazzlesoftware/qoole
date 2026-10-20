/*
 * Landscape.cpp
 *
 * Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
 */

#include "MapCorePCH.h"

#include <math.h>

#include "Landscape.h"

Landscape::Landscape(int inQuadsX, int inQuadsY, float inQuadSize)
  : quadsX(Max(1, Min(inQuadsX, MaxQuads))), quadsY(Max(1, Min(inQuadsY, MaxQuads))),
    quadSize(Max(inQuadSize, 1.0f)) {
  heights.assign((size_t) GetPointsX() * GetPointsY(), 0.0f);
  BuildMesh();
}

std::shared_ptr<const Landscape>
Landscape::Create(int inQuadsX, int inQuadsY, float inQuadSize, std::vector<float> inHeights,
                  std::vector<LandscapeLayer> inLayers) {
  auto result = std::make_shared<Landscape>(inQuadsX, inQuadsY, inQuadSize);
  if (inHeights.size() != result->heights.size())
    return nullptr;
  for (const LandscapeLayer &layer : inLayers) {
    if (layer.weights.size() != result->heights.size())
      return nullptr;
  }
  result->heights = std::move(inHeights);
  result->layers = std::move(inLayers);
  result->BuildMesh();
  return result;
}

float
Landscape::GetWeight(int layer, int x, int y) const {
  if (layer < 0 || layer >= (int) layers.size())
    return 0.0f;
  x = Max(0, Min(x, quadsX));
  y = Max(0, Min(y, quadsY));
  return layers[layer].weights[(size_t) y * GetPointsX() + x];
}

std::shared_ptr<const Landscape>
Landscape::WithLayers(std::vector<LandscapeLayer> newLayers) const {
  return Create(quadsX, quadsY, quadSize, heights, std::move(newLayers));
}

LandscapeLayer
Landscape::MakeLayer(const char *layerName, unsigned char r, unsigned char g, unsigned char b) const {
  LandscapeLayer layer;
  layer.name = layerName ? layerName : "";
  layer.r = r;
  layer.g = g;
  layer.b = b;
  layer.weights.assign(heights.size(), 0.0f);
  return layer;
}

float
Landscape::GetHeight(int x, int y) const {
  x = Max(0, Min(x, quadsX));
  y = Max(0, Min(y, quadsY));
  return heights[(size_t) y * GetPointsX() + x];
}

Vector3d
Landscape::GetPoint(int x, int y) const {
  return Vector3d(x * quadSize - GetSizeX() * 0.5f, y * quadSize - GetSizeY() * 0.5f, GetHeight(x, y));
}

float
Landscape::SampleHeight(float localX, float localY) const {
  const float gx = Max(0.0f, Min((localX + GetSizeX() * 0.5f) / quadSize, (float) quadsX));
  const float gy = Max(0.0f, Min((localY + GetSizeY() * 0.5f) / quadSize, (float) quadsY));
  const int x0 = Min((int) gx, quadsX - 1);
  const int y0 = Min((int) gy, quadsY - 1);
  const float fx = gx - x0;
  const float fy = gy - y0;
  const float h00 = GetHeight(x0, y0), h10 = GetHeight(x0 + 1, y0);
  const float h01 = GetHeight(x0, y0 + 1), h11 = GetHeight(x0 + 1, y0 + 1);
  return (h00 * (1.0f - fx) + h10 * fx) * (1.0f - fy) + (h01 * (1.0f - fx) + h11 * fx) * fy;
}

std::shared_ptr<const Landscape>
Landscape::WithHeights(std::vector<float> newHeights) const {
  return Create(quadsX, quadsY, quadSize, std::move(newHeights), layers);
}

void
Landscape::BuildMesh(void) {
  auto built = std::make_shared<StaticMesh>("Landscape");
  const int px = GetPointsX();
  const int py = GetPointsY();
  for (int y = 0; y < py; y++)
    for (int x = 0; x < px; x++)
      built->AddVertex(GetPoint(x, y));

  // Smooth normals from the slope across each point.
  std::vector<Vector3d> normals((size_t) px * py);
  for (int y = 0; y < py; y++) {
    for (int x = 0; x < px; x++) {
      const float dx = (GetHeight(x + 1, y) - GetHeight(x - 1, y)) / ((Min(x + 1, quadsX) - Max(x - 1, 0)) * quadSize);
      const float dy = (GetHeight(x, y + 1) - GetHeight(x, y - 1)) / ((Min(y + 1, quadsY) - Max(y - 1, 0)) * quadSize);
      Vector3d n(-dx, -dy, 1.0f);
      n.MultVector(1.0f / n.GetMag());
      normals[(size_t) y * px + x] = n;
    }
  }

  const auto index = [px](int x, int y) { return y * px + x; };
  for (int y = 0; y < quadsY; y++) {
    for (int x = 0; x < quadsX; x++) {
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
  // Colors: the layers blended over the ground's own color.
  if (!layers.empty()) {
    unsigned char br, bg, bb;
    GetBaseColor(br, bg, bb);
    std::vector<unsigned int> colors((size_t) px * py);
    for (size_t i = 0; i < colors.size(); i++) {
      float sum = 0.0f, r = 0.0f, g = 0.0f, b = 0.0f;
      for (const LandscapeLayer &layer : layers) {
        const float w = layer.weights[i];
        sum += w;
        r += w * layer.r;
        g += w * layer.g;
        b += w * layer.b;
      }
      const float rest = Max(0.0f, 1.0f - sum);
      const float scale = sum > 1.0f ? 1.0f / sum : 1.0f;
      const auto channel = [](float v) { return (unsigned int) Max(0.0f, Min(v + 0.5f, 255.0f)); };
      colors[i] = channel(r * scale + rest * br) | (channel(g * scale + rest * bg) << 8) | (channel(b * scale + rest * bb) << 16) | 0xFF000000u;
    }
    built->SetVertexColors(std::move(colors));
  }
  built->Finish();
  mesh = built;
}
