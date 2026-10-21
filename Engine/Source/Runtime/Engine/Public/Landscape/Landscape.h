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
 * Splines (Unreal's landscape splines) run over it: chains of control
 * points, each with a width and a side falloff, the curve passing smoothly
 * through them (Catmull-Rom). Applied (WithSplinesApplied), a spline
 * raises and lowers the ground to itself, fading out over the falloff, and
 * paints a layer along itself: roads, paths, rivers' beds.
 *
 * What drawing needs is made with it: a smooth normal and the layers'
 * blended color at each grid point, and the lowest and highest heights of
 * each component (ComponentQuads x ComponentQuads quads), so views cull and
 * draw each component at the detail its distance needs (View draws
 * landscapes so). Picking traces the grid (Raycast). The full triangle mesh
 * (GetMesh) is only built when something asks for it, so editing a big
 * landscape never waits for it. A landscape is a static mesh actor in every
 * other way: it keeps its transform and takes no part in CSG, clipping or
 * .map files.
 */

#ifndef _LANDSCAPE_H_
#define _LANDSCAPE_H_

#include <memory>
#include <string>
#include <vector>

#include "Geometry/StaticMesh.h"

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

struct LandscapeSplinePoint {
  Vector3d position;      // in the landscape's own space
  float width = 256.0f;   // flat across it
  float falloff = 256.0f; // fading out to either side beyond the width
  // The curve's tangent there (its direction, and how far it pulls): the
  // smooth one through its neighbours (Catmull-Rom) unless set by hand.
  bool autoTangent = true;
  Vector3d tangent;
  // A junction: points of any of the landscape's splines with the same
  // joint (0 on; -1 for none) are one place, moved together.
  int joint = -1;
};

struct LandscapeSpline {
  std::vector<LandscapeSplinePoint> points;
  bool closed = false;        // the last point joins the first
  bool raiseTerrain = true;   // applying lifts ground below it
  bool lowerTerrain = true;   // and cuts ground above it
  std::string layer;          // the paint layer it paints ("": none)
  // Its mesh (Unreal's spline meshes): a static mesh repeated along it, bent
  // to the curve, its length along the mesh's X. Fit to width stretches it
  // across to the spline's width; scale sizes the rest; it sits Z offset
  // above the curve.
  std::string mesh;           // "": none
  float meshScale = 1.0f;
  bool meshFitWidth = true;
  float meshZOffset = 0.0f;

  // The tangent the curve has at a point (its own, or the automatic one).
  Vector3d GetTangent(int point) const;
  // A point on the curve every so far along it, with the width and falloff
  // there (between control points they change evenly). Between points the
  // curve is Hermite's with their tangents.
  struct Sample {
    Vector3d position;
    float width, falloff;
  };
  void SampleCurve(float step, std::vector<Sample> &out) const;
};

class Landscape {
 public:
  // Flat at height 0. Sizes are clamped to 1..MaxQuads quads and a quad
  // size of at least 1 unit.
  Landscape(int quadsX, int quadsY, float quadSize);
  // With these heights (one per grid point, row by row from -Y); null if
  // the count is wrong.
  static std::shared_ptr<const Landscape> Create(int quadsX, int quadsY, float quadSize, std::vector<float> heights,
                                                 std::vector<LandscapeLayer> layers = {}, std::vector<LandscapeSpline> splines = {});

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

  // The whole grid as triangles, made the first time it is asked for.
  const std::shared_ptr<const StaticMesh> &GetMesh(void) const;

  // At a grid point: its smooth normal, and the layers blended over the
  // ground's color (RGBA, red in the low byte) when it has layers.
  const Vector3d &GetNormal(int x, int y) const { return normals[(size_t) y * GetPointsX() + x]; }
  bool HasColors(void) const { return !colors.empty(); }
  unsigned int GetColor(int x, int y) const { return colors[(size_t) y * GetPointsX() + x]; }

  // Components: blocks of ComponentQuads quads a side (the last ones may be
  // smaller), each with its lowest and highest height.
  static constexpr int ComponentQuads = 32;
  int GetComponentsX(void) const { return (quadsX + ComponentQuads - 1) / ComponentQuads; }
  int GetComponentsY(void) const { return (quadsY + ComponentQuads - 1) / ComponentQuads; }
  void GetComponentHeights(int cx, int cy, float &low, float &high) const;
  float GetMinHeight(void) const { return minHeight; }
  float GetMaxHeight(void) const { return maxHeight; }
  // The box it fills in its own space, and the radius about its origin.
  void GetBounds(Vector3d &low, Vector3d &high) const;
  float GetBoundRadius(void) const;

  // Where a ray (landscape space) first meets the ground, from above or
  // below, between rangeBegin and rangeEnd along it; as StaticMesh::HitMesh,
  // the same triangles, without the mesh.
  bool Raycast(const Line &ray, float rangeBegin, float rangeEnd, float &depth) const;

  // A copy with other heights, or other layers (same size; null if the
  // counts are wrong).
  std::shared_ptr<const Landscape> WithHeights(std::vector<float> newHeights) const;
  std::shared_ptr<const Landscape> WithLayers(std::vector<LandscapeLayer> newLayers) const;
  const std::vector<LandscapeSpline> &GetSplines(void) const { return splines; }
  // A mesh a spline (null for those with none), bent along it: made when
  // first asked for, and kept by copies with other heights or layers.
  const std::vector<std::shared_ptr<const StaticMesh>> &GetSplineMeshes(void) const;
  static std::shared_ptr<const StaticMesh> BuildSplineMesh(const LandscapeSpline &spline);
  std::shared_ptr<const Landscape> WithSplines(std::vector<LandscapeSpline> newSplines) const;
  // The ground shaped to the splines and their layers painted along them
  // (as Unreal's Apply Splines to Landscape); null without splines.
  std::shared_ptr<const Landscape> WithSplinesApplied(void) const;
  // A new layer, its weights all 0.
  LandscapeLayer MakeLayer(const char *layerName, unsigned char r, unsigned char g, unsigned char b) const;

  static constexpr int MaxQuads = 512;

 private:
  void BuildShading(void);
  // The triangles of the quad at x, y: a, b, c and a, c, d (counterclockwise
  // seen from above).
  void QuadCorners(int x, int y, Vector3d &a, Vector3d &b, Vector3d &c, Vector3d &d) const;

  int quadsX, quadsY;
  float quadSize;
  std::vector<float> heights;
  std::vector<LandscapeLayer> layers;
  std::vector<LandscapeSpline> splines;
  std::vector<Vector3d> normals;
  std::vector<unsigned int> colors;
  std::vector<float> componentLow, componentHigh;
  float minHeight = 0.0f, maxHeight = 0.0f;
  mutable std::shared_ptr<const StaticMesh> mesh;
  mutable std::shared_ptr<const std::vector<std::shared_ptr<const StaticMesh>>> splineMeshes;
};

#endif // _LANDSCAPE_H_
