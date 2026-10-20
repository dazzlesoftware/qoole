/*
 * Landscape.h
 *
 * Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
 *
 * A heightfield, as Unreal's landscape: a grid of (QuadsX + 1) x (QuadsY + 1)
 * heights, QuadSize apart, centered on the object's position in X and Y
 * (Object::SetLandscape). A landscape never changes once built: sculpting
 * makes a new one (WithHeights), so undo keeps the old one as it was.
 *
 * Paint layers (Unreal's weight-blended landscape layers) color it: each
 * has a name, a color, a texture (which the editor draws) and a weight per
 * grid point. At a point the weights
 * add up to 1 at most; what is left shows the ground's own color.
 *
 * It builds the triangle mesh the object is drawn and picked with (smooth
 * normals from the heights, colors from the layers, the grid's lines as its
 * wireframe), so a
 * landscape is a static mesh actor in every other way: it keeps its
 * transform and takes no part in CSG, clipping or .map files.
 */

#ifndef _LANDSCAPE_H_
#define _LANDSCAPE_H_

#include <memory>
#include <string>
#include <vector>

#include "StaticMesh.h"

struct LandscapeLayer {
  std::string name;
  unsigned char r = 255, g = 255, b = 255;
  std::vector<float> weights; // one per grid point, 0 to 1
  // What it looks like: empty for its plain color, "builtin:<Name>" for one
  // of the editor's own patterns (tinted with the color), or an image
  // file's path. It repeats every `tiling` units of the landscape.
  std::string texture;
  float tiling = 512.0f;
};

class Landscape {
 public:
  // Flat at height 0. Sizes are clamped to 1..MaxQuads quads and a quad
  // size of at least 1 unit.
  Landscape(int quadsX, int quadsY, float quadSize);
  // With these heights (one per grid point, row by row from -Y); null if
  // the count is wrong.
  static std::shared_ptr<const Landscape> Create(int quadsX, int quadsY, float quadSize, std::vector<float> heights,
                                                 std::vector<LandscapeLayer> layers = {});

  int GetQuadsX(void) const { return quadsX; }
  int GetQuadsY(void) const { return quadsY; }
  int GetPointsX(void) const { return quadsX + 1; }
  int GetPointsY(void) const { return quadsY + 1; }
  float GetQuadSize(void) const { return quadSize; }
  float GetSizeX(void) const { return quadsX * quadSize; }
  float GetSizeY(void) const { return quadsY * quadSize; }

  const std::vector<float> &GetHeights(void) const { return heights; }
  const std::vector<LandscapeLayer> &GetLayers(void) const { return layers; }
  int GetNumLayers(void) const { return (int) layers.size(); }
  float GetWeight(int layer, int x, int y) const;
  // The ground's own color, where no layer is painted.
  static void GetBaseColor(unsigned char &r, unsigned char &g, unsigned char &b) { r = 158; g = 168; b = 148; }
  float GetHeight(int x, int y) const;
  // The grid point's position in the landscape's own space.
  Vector3d GetPoint(int x, int y) const;
  // Interpolated over the grid at a point in the landscape's space (edges
  // clamp).
  float SampleHeight(float localX, float localY) const;

  const std::shared_ptr<const StaticMesh> &GetMesh(void) const { return mesh; }

  // A copy with other heights, or other layers (same size; null if the
  // counts are wrong).
  std::shared_ptr<const Landscape> WithHeights(std::vector<float> newHeights) const;
  std::shared_ptr<const Landscape> WithLayers(std::vector<LandscapeLayer> newLayers) const;
  // A new layer, its weights all 0.
  LandscapeLayer MakeLayer(const char *layerName, unsigned char r, unsigned char g, unsigned char b) const;

  static constexpr int MaxQuads = 512;

 private:
  void BuildMesh(void);

  int quadsX, quadsY;
  float quadSize;
  std::vector<float> heights;
  std::vector<LandscapeLayer> layers;
  std::shared_ptr<const StaticMesh> mesh;
};

#endif // _LANDSCAPE_H_
