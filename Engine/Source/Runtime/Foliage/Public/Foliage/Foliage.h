/*
 * Foliage.h
 *
 * Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
 *
 * Foliage, as Unreal's: an object (Unreal's instanced foliage actor) holding
 * foliage types (a static mesh and how it is painted: density, spacing,
 * scale, alignment, the slopes and heights it grows on, how far it is
 * drawn) and instances of them, each a place, an up direction, a turn
 * about it and a size. Like a landscape it never changes once made: a
 * stroke makes a new one (WithInstances), so undo keeps the old one.
 *
 * Instances are drawn (View) but not picked in views; the foliage object is
 * selected in the Outliner. It takes no part in CSG, clipping or .map
 * files.
 */

#ifndef _FOLIAGE_H_
#define _FOLIAGE_H_

#include <memory>
#include <string>
#include <vector>

#include "Geometry/StaticMesh.h"

struct FoliageType {
  std::string name;
  std::string mesh;               // StaticMesh::Find
  float density = 100.0f;         // instances on 1000 x 1000 units of ground (Unreal's Density / 1Kuu)
  float radius = 0.0f;            // no two of it closer than this
  float scaleMin = 0.8f, scaleMax = 1.2f;
  bool alignToNormal = false;     // stand along the ground's normal, else upright
  float alignMaxAngle = 30.0f;    // degrees it leans at most when aligned
  bool randomYaw = true;
  float slopeMin = 0.0f, slopeMax = 45.0f;            // degrees of ground it grows on
  float heightMin = -65536.0f, heightMax = 65536.0f;  // heights it grows between
  float zOffset = 0.0f;           // up from the ground
  float cullDistance = 0.0f;      // not drawn farther than this (0: always)

  bool operator==(const FoliageType &) const = default;
};

struct FoliageInstance {
  int type = 0;
  Vector3d position;              // in the foliage object's space
  Vector3d up = Vector3d(0.0f, 0.0f, 1.0f);
  float yaw = 0.0f;               // radians about up
  float scale = 1.0f;
};

class Foliage {
 public:
  Foliage(void) {}
  Foliage(std::vector<FoliageType> types, std::vector<FoliageInstance> instances);

  const std::vector<FoliageType> &GetTypes(void) const { return types; }
  int GetNumTypes(void) const { return (int) types.size(); }
  const std::vector<FoliageInstance> &GetInstances(void) const { return instances; }
  int GetNumInstances(void) const { return (int) instances.size(); }
  int CountInstances(int type) const;
  // A type's mesh (null if it names none there is).
  const std::shared_ptr<const StaticMesh> &GetMesh(int type) const { return meshes[type]; }

  // Where an instance's mesh goes: its pivot at the position, turned by
  // yaw about up, scaled.
  Matrix44 GetInstanceMatrix(const FoliageInstance &instance) const;
  // The box the instances fill (their meshes' bounds), in its own space;
  // false if there are none.
  bool GetBounds(Vector3d &low, Vector3d &high) const;
  float GetBoundRadius(void) const;

  // Copies with other types (instances of removed types go with them:
  // types[i] keeps instances of the old type map[i], -1 for none) or other
  // instances.
  std::shared_ptr<const Foliage> WithTypes(std::vector<FoliageType> newTypes, const std::vector<int> &oldTypeOf) const;
  std::shared_ptr<const Foliage> WithInstances(std::vector<FoliageInstance> newInstances) const;

 private:
  void Resolve(void);

  std::vector<FoliageType> types;
  std::vector<FoliageInstance> instances;
  std::vector<std::shared_ptr<const StaticMesh>> meshes;
  Vector3d boundLow, boundHigh;
  float boundRadius = 0.0f;
};

#endif // _FOLIAGE_H_
