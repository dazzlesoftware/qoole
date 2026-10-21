// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "geometry.h"
#include "Geometry/StaticMesh.h"
#include <cstdlib>

// Adapter for the legacy viewport's point buffer. Runtime meshes have no
// dependency on this editor-only allocation/container type.
class FMeshVertexBufferAdapter
{
public:
  static Points3d &Transform(const StaticMesh &mesh, const Matrix44 &matrix, Points3d &buffer) {
    const int needed = mesh.GetNumVertices();
    if (buffer.numElm == 0) {
      for (int i = 0; i < buffer.arraySize; ++i) buffer.array[i] = new Point3d;
      buffer.numElm = buffer.arraySize;
    }
    check(buffer.numElm == buffer.arraySize);
    if (buffer.arraySize < needed) {
      const int old = buffer.arraySize;
      buffer.arraySize = needed;
      buffer.array = static_cast<Point3d **>(std::realloc(buffer.array, sizeof(Point3d *) * buffer.arraySize));
      for (int i = old; i < buffer.arraySize; ++i) buffer.array[i] = new Point3d;
      buffer.numElm = buffer.arraySize;
    }
    for (int i = 0; i < needed; ++i) matrix.Transform(*buffer.array[i], mesh.GetVertex(i));
    return buffer;
  }
};
