/*
 * StaticMesh.h
 *
 * Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
 *
 * A triangle mesh that map objects place with their transform, as Unreal's
 * static mesh actors do (Object::SetMesh): unlike a brush it keeps its
 * location, rotation and scale, need not be convex, and takes no part in
 * CSG, clipping or .map files.
 *
 * Meshes are shared and never change once built. The library holds
 * Unreal's basic shapes, centered on their pivot and 128 units across:
 * Cube, Sphere, Cylinder, Cone and Plane (flat, facing up).
 *
 * Wireframes draw the mesh's own edge list (the cube's 12 edges, the
 * sphere's meridians and parallels), not every triangle edge. Each
 * triangle corner has a normal, so curved sides shade smoothly.
 */

#ifndef _STATICMESH_H_
#define _STATICMESH_H_

#include <memory>
#include <string>
#include <vector>

#include "list.h"
#include "math3d.h"
#include "geometry.h"

class StaticMesh {
 public:
  struct Triangle {
    int v[3];          // vertex indices, counterclockwise seen from outside
    Vector3d norm[3];  // corner normals (unit)
  };

  explicit StaticMesh(const char *meshName);
  ~StaticMesh(void);
  StaticMesh(const StaticMesh &) = delete;
  StaticMesh &operator=(const StaticMesh &) = delete;

  const char *GetName(void) const { return name.c_str(); }

  int GetNumVertices(void) const { return (int) vertices.size(); }
  const Vector3d &GetVertex(int index) const { return vertices[index]; }
  const std::vector<Triangle> &GetTriangles(void) const { return triangles; }
  const LinkList<Edge3d> &GetEdgeList(void) const { return edges; }

  // Building (before the mesh is shared).
  int AddVertex(const Vector3d &pos);
  void AddTriangle(int a, int b, int c, const Vector3d &na, const Vector3d &nb, const Vector3d &nc);
  void AddFlatTriangle(int a, int b, int c); // the face's own normal at every corner
  void AddEdge(int a, int b);
  void Finish(void); // bounds

  //===== Bounds and hits (in the mesh's own space) =====
  float GetBoundRadius(void) const { return boundRadius; } // from the pivot
  void GetBounds(Vector3d &minVec, Vector3d &maxVec) const { minVec = boundMin; maxVec = boundMax; }
  void FindTransBoundBox(const Matrix44 &trans, Vector3d &minVec, Vector3d &maxVec) const;
  // Like Geometry::HitGeometry: depth is the signed distance from the
  // ray's point along its slope. Triangles are hit from both sides.
  bool HitMesh(const Line &ray, float rangeBegin, float rangeEnd, float &depth) const;
  Points3d &Transform(const Matrix44 &transMatrix, Points3d &vBuffer) const;

  //===== The library =====
  // A mesh by name (case-insensitive); null if there is none.
  static std::shared_ptr<const StaticMesh> Find(const char *meshName);
  // The basic shapes, in Unreal's order.
  static const std::vector<std::string> &GetBasicShapeNames(void);

  static constexpr float BasicShapeSize = 128.0f;

 private:
  std::string name;
  std::vector<Vector3d> vertices;
  std::vector<Triangle> triangles;
  LinkList<Edge3d> edges;
  float boundRadius;
  Vector3d boundMin, boundMax;
};

#endif // _STATICMESH_H_
