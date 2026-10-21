/*
 * Foliage.cpp
 *
 * Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
 */


#include <math.h>

#include "Foliage/Foliage.h"

Foliage::Foliage(std::vector<FoliageType> inTypes, std::vector<FoliageInstance> inInstances)
  : types(std::move(inTypes)), instances(std::move(inInstances)) {
  Resolve();
}

void
Foliage::Resolve(void) {
  meshes.clear();
  for (const FoliageType &type : types)
    meshes.push_back(StaticMesh::Find(type.mesh.c_str()));
  // Instances of types there are not are dropped.
  std::vector<FoliageInstance> kept;
  kept.reserve(instances.size());
  for (const FoliageInstance &instance : instances) {
    if (instance.type >= 0 && instance.type < (int) types.size())
      kept.push_back(instance);
  }
  instances = std::move(kept);

  // Bounds: each instance's mesh's radius about its place.
  boundRadius = 0.0f;
  bool first = true;
  for (const FoliageInstance &instance : instances) {
    const std::shared_ptr<const StaticMesh> &mesh = meshes[instance.type];
    const float r = (mesh ? mesh->GetBoundRadius() : 0.0f) * instance.scale;
    const Vector3d &p = instance.position;
    if (first) {
      boundLow.NewVector(p.GetX() - r, p.GetY() - r, p.GetZ() - r);
      boundHigh.NewVector(p.GetX() + r, p.GetY() + r, p.GetZ() + r);
      first = false;
    }
    else {
      boundLow.NewVector(Min(boundLow.GetX(), p.GetX() - r), Min(boundLow.GetY(), p.GetY() - r), Min(boundLow.GetZ(), p.GetZ() - r));
      boundHigh.NewVector(Max(boundHigh.GetX(), p.GetX() + r), Max(boundHigh.GetY(), p.GetY() + r), Max(boundHigh.GetZ(), p.GetZ() + r));
    }
    boundRadius = Max(boundRadius, p.GetMag() + r);
  }
}

int
Foliage::CountInstances(int type) const {
  int count = 0;
  for (const FoliageInstance &instance : instances)
    count += instance.type == type ? 1 : 0;
  return count;
}

Matrix44
Foliage::GetInstanceMatrix(const FoliageInstance &instance) const {
  // Columns: the instance's own axes, scaled, then its place.
  Vector3d up(instance.up);
  const float upLen = up.GetMag();
  if (upLen < 1e-6f)
    up.NewVector(0.0f, 0.0f, 1.0f);
  else
    up.MultVector(1.0f / upLen);
  // Forward: the yaw's direction, flattened against up.
  Vector3d forward(cosf(instance.yaw), sinf(instance.yaw), 0.0f);
  Vector3d along(up);
  along.MultVector(forward.DotVector(up));
  forward.SubVector(along);
  if (forward.GetMag() < 1e-4f)
    forward.NewVector(1.0f, 0.0f, 0.0f);
  forward.MultVector(1.0f / forward.GetMag());
  // x is forward, y left (z cross x), z up: a right-handed basis.
  Vector3d left;
  left.CrossVector(up, forward);
  Matrix44 m;
  m.SetIdentity();
  const float s = instance.scale;
  const Vector3d *axes[3] = { &forward, &left, &up };
  for (int col = 0; col < 3; col++) {
    m.SetMatrix(0, col, axes[col]->GetX() * s);
    m.SetMatrix(1, col, axes[col]->GetY() * s);
    m.SetMatrix(2, col, axes[col]->GetZ() * s);
  }
  m.SetMatrix(0, 3, instance.position.GetX());
  m.SetMatrix(1, 3, instance.position.GetY());
  m.SetMatrix(2, 3, instance.position.GetZ());
  return m;
}

bool
Foliage::GetBounds(Vector3d &low, Vector3d &high) const {
  if (instances.empty())
    return false;
  low = boundLow;
  high = boundHigh;
  return true;
}

float
Foliage::GetBoundRadius(void) const {
  return boundRadius;
}

std::shared_ptr<const Foliage>
Foliage::WithTypes(std::vector<FoliageType> newTypes, const std::vector<int> &oldTypeOf) const {
  // Instances follow their type to its new place, or go with it.
  std::vector<int> newTypeOf(types.size(), -1);
  for (size_t i = 0; i < oldTypeOf.size() && i < newTypes.size(); i++) {
    if (oldTypeOf[i] >= 0 && oldTypeOf[i] < (int) types.size())
      newTypeOf[oldTypeOf[i]] = (int) i;
  }
  std::vector<FoliageInstance> kept;
  kept.reserve(instances.size());
  for (FoliageInstance instance : instances) {
    if (instance.type < 0 || instance.type >= (int) types.size() || newTypeOf[instance.type] < 0)
      continue;
    instance.type = newTypeOf[instance.type];
    kept.push_back(instance);
  }
  return std::make_shared<Foliage>(std::move(newTypes), std::move(kept));
}

std::shared_ptr<const Foliage>
Foliage::WithInstances(std::vector<FoliageInstance> newInstances) const {
  return std::make_shared<Foliage>(types, std::move(newInstances));
}
