/*
 * StaticMesh.cpp
 *
 * Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
 */

#include "MapCorePCH.h"

#include <math.h>
#include <string.h>

#include "StaticMesh.h"

namespace {
  const float Pi = 3.14159265358979f;
  const int Segments = 24;       // around curved shapes
  const int SphereRings = 16;    // pole to pole

  Vector3d FaceNormal(const Vector3d &a, const Vector3d &b, const Vector3d &c) {
    Vector3d e1, e2, n;
    e1.SubVector(b, a);
    e2.SubVector(c, a);
    n.CrossVector(e1, e2);
    const float len = n.GetMag();
    if (len > 1e-8f)
      n.MultVector(1.0f / len);
    return n;
  }

  Vector3d Unit(float x, float y, float z) {
    Vector3d v(x, y, z);
    const float len = v.GetMag();
    if (len > 1e-8f)
      v.MultVector(1.0f / len);
    return v;
  }

  std::shared_ptr<StaticMesh> MakeCube(void) {
    auto mesh = std::make_shared<StaticMesh>("Cube");
    const float h = StaticMesh::BasicShapeSize * 0.5f;
    int v[8];
    for (int i = 0; i < 8; i++)
      v[i] = mesh->AddVertex(Vector3d((i & 1) ? h : -h, (i & 2) ? h : -h, (i & 4) ? h : -h));
    // Faces as quads, counterclockwise from outside.
    const int quads[6][4] = {
      { 0, 2, 3, 1 }, // -Z
      { 4, 5, 7, 6 }, // +Z
      { 0, 1, 5, 4 }, // -Y
      { 2, 6, 7, 3 }, // +Y
      { 0, 4, 6, 2 }, // -X
      { 1, 3, 7, 5 }, // +X
    };
    for (const auto &q : quads) {
      mesh->AddFlatTriangle(v[q[0]], v[q[1]], v[q[2]]);
      mesh->AddFlatTriangle(v[q[0]], v[q[2]], v[q[3]]);
    }
    const int edges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 },
                               { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
    for (const auto &e : edges)
      mesh->AddEdge(v[e[0]], v[e[1]]);
    mesh->Finish();
    return mesh;
  }

  std::shared_ptr<StaticMesh> MakePlane(void) {
    auto mesh = std::make_shared<StaticMesh>("Plane");
    const float h = StaticMesh::BasicShapeSize * 0.5f;
    const int a = mesh->AddVertex(Vector3d(-h, -h, 0.0f));
    const int b = mesh->AddVertex(Vector3d(h, -h, 0.0f));
    const int c = mesh->AddVertex(Vector3d(h, h, 0.0f));
    const int d = mesh->AddVertex(Vector3d(-h, h, 0.0f));
    mesh->AddFlatTriangle(a, b, c);
    mesh->AddFlatTriangle(a, c, d);
    mesh->AddEdge(a, b);
    mesh->AddEdge(b, c);
    mesh->AddEdge(c, d);
    mesh->AddEdge(d, a);
    mesh->Finish();
    return mesh;
  }

  std::shared_ptr<StaticMesh> MakeSphere(void) {
    auto mesh = std::make_shared<StaticMesh>("Sphere");
    const float r = StaticMesh::BasicShapeSize * 0.5f;
    // Rings from the top pole (ring 0) to the bottom one (ring SphereRings).
    std::vector<std::vector<int>> ring(SphereRings + 1);
    for (int i = 0; i <= SphereRings; i++) {
      const float phi = Pi * (float) i / SphereRings;
      const int count = (i == 0 || i == SphereRings) ? 1 : Segments;
      for (int j = 0; j < count; j++) {
        const float theta = 2.0f * Pi * (float) j / Segments;
        ring[i].push_back(mesh->AddVertex(Vector3d(r * sinf(phi) * cosf(theta), r * sinf(phi) * sinf(theta), r * cosf(phi))));
      }
    }
    const auto normalAt = [&](int vi) { const Vector3d &p = mesh->GetVertex(vi); return Unit(p.GetX(), p.GetY(), p.GetZ()); };
    for (int i = 0; i < SphereRings; i++) {
      for (int j = 0; j < Segments; j++) {
        const int j1 = (j + 1) % Segments;
        if (i == 0) {
          const int a = ring[0][0], b = ring[1][j], c = ring[1][j1];
          mesh->AddTriangle(a, b, c, normalAt(a), normalAt(b), normalAt(c));
        }
        else if (i == SphereRings - 1) {
          const int a = ring[i][j], b = ring[i + 1][0], c = ring[i][j1];
          mesh->AddTriangle(a, b, c, normalAt(a), normalAt(b), normalAt(c));
        }
        else {
          const int a = ring[i][j], b = ring[i + 1][j], c = ring[i + 1][j1], d = ring[i][j1];
          mesh->AddTriangle(a, b, c, normalAt(a), normalAt(b), normalAt(c));
          mesh->AddTriangle(a, c, d, normalAt(a), normalAt(c), normalAt(d));
        }
      }
    }
    // Meridians and parallels.
    for (int j = 0; j < Segments; j++) {
      for (int i = 0; i < SphereRings; i++) {
        const int a = (i == 0) ? ring[0][0] : ring[i][j];
        const int b = (i + 1 == SphereRings) ? ring[SphereRings][0] : ring[i + 1][j];
        mesh->AddEdge(a, b);
      }
    }
    for (int i = 1; i < SphereRings; i++)
      for (int j = 0; j < Segments; j++)
        mesh->AddEdge(ring[i][j], ring[i][(j + 1) % Segments]);
    mesh->Finish();
    return mesh;
  }

  // A cylinder, or a cone when the top radius is 0.
  std::shared_ptr<StaticMesh> MakeRound(const char *meshName, float topRadius) {
    auto mesh = std::make_shared<StaticMesh>(meshName);
    const float r = StaticMesh::BasicShapeSize * 0.5f;
    const float h = StaticMesh::BasicShapeSize * 0.5f;
    const bool cone = topRadius <= 0.0f;
    std::vector<int> bottom, top;
    for (int j = 0; j < Segments; j++) {
      const float theta = 2.0f * Pi * (float) j / Segments;
      bottom.push_back(mesh->AddVertex(Vector3d(r * cosf(theta), r * sinf(theta), -h)));
      if (!cone)
        top.push_back(mesh->AddVertex(Vector3d(topRadius * cosf(theta), topRadius * sinf(theta), h)));
    }
    const int apex = cone ? mesh->AddVertex(Vector3d(0.0f, 0.0f, h)) : -1;
    const int bottomCenter = mesh->AddVertex(Vector3d(0.0f, 0.0f, -h));
    const int topCenter = cone ? -1 : mesh->AddVertex(Vector3d(0.0f, 0.0f, h));
    // The side's normal leans up by the slope (r - topRadius) / height.
    const float lean = (r - topRadius) / (2.0f * h);
    const auto sideNormal = [&](float theta) { return Unit(cosf(theta), sinf(theta), lean); };
    for (int j = 0; j < Segments; j++) {
      const int j1 = (j + 1) % Segments;
      const float t0 = 2.0f * Pi * (float) j / Segments;
      const float t1 = 2.0f * Pi * (float) (j + 1) / Segments;
      if (cone) {
        const float tm = 0.5f * (t0 + t1);
        mesh->AddTriangle(bottom[j], bottom[j1], apex, sideNormal(t0), sideNormal(t1), sideNormal(tm));
      }
      else {
        mesh->AddTriangle(bottom[j], bottom[j1], top[j1], sideNormal(t0), sideNormal(t1), sideNormal(t1));
        mesh->AddTriangle(bottom[j], top[j1], top[j], sideNormal(t0), sideNormal(t1), sideNormal(t0));
        mesh->AddFlatTriangle(topCenter, top[j], top[j1]);
      }
      mesh->AddFlatTriangle(bottomCenter, bottom[j1], bottom[j]);
      mesh->AddEdge(bottom[j], bottom[j1]);
      if (cone) {
        mesh->AddEdge(bottom[j], apex);
      }
      else {
        mesh->AddEdge(top[j], top[j1]);
        mesh->AddEdge(bottom[j], top[j]);
      }
    }
    mesh->Finish();
    return mesh;
  }

  struct Library {
    std::vector<std::shared_ptr<const StaticMesh>> meshes;
    std::vector<std::string> names;
    Library(void) {
      meshes.push_back(MakeCube());
      meshes.push_back(MakeSphere());
      meshes.push_back(MakeRound("Cylinder", StaticMesh::BasicShapeSize * 0.5f));
      meshes.push_back(MakeRound("Cone", 0.0f));
      meshes.push_back(MakePlane());
      for (const auto &mesh : meshes)
        names.push_back(mesh->GetName());
    }
  };

  const Library &GetLibrary(void) {
    static const Library library;
    return library;
  }
}

StaticMesh::StaticMesh(const char *meshName)
  : name(meshName ? meshName : ""), boundRadius(0.0f),
    boundMin(0.0f, 0.0f, 0.0f), boundMax(0.0f, 0.0f, 0.0f) {
}

StaticMesh::~StaticMesh(void) {
  while (edges.NumOfElm() > 0)
    delete &(edges.RemoveNode(0));
}

int
StaticMesh::AddVertex(const Vector3d &pos) {
  vertices.push_back(pos);
  return (int) vertices.size() - 1;
}

void
StaticMesh::AddTriangle(int a, int b, int c, const Vector3d &na, const Vector3d &nb, const Vector3d &nc) {
  Triangle tri;
  tri.v[0] = a;
  tri.v[1] = b;
  tri.v[2] = c;
  tri.norm[0] = na;
  tri.norm[1] = nb;
  tri.norm[2] = nc;
  triangles.push_back(tri);
}

void
StaticMesh::AddFlatTriangle(int a, int b, int c) {
  const Vector3d n = FaceNormal(vertices[a], vertices[b], vertices[c]);
  AddTriangle(a, b, c, n, n, n);
}

void
StaticMesh::AddEdge(int a, int b) {
  edges.AppendNode(*(new Edge3d(a, b)));
}

void
StaticMesh::Finish(void) {
  boundRadius = 0.0f;
  for (size_t i = 0; i < vertices.size(); i++) {
    const Vector3d &p = vertices[i];
    boundRadius = Max(boundRadius, p.GetMag());
    if (i == 0) {
      boundMin = boundMax = p;
      continue;
    }
    boundMin.NewVector(Min(boundMin.GetX(), p.GetX()), Min(boundMin.GetY(), p.GetY()), Min(boundMin.GetZ(), p.GetZ()));
    boundMax.NewVector(Max(boundMax.GetX(), p.GetX()), Max(boundMax.GetY(), p.GetY()), Max(boundMax.GetZ(), p.GetZ()));
  }
}

void
StaticMesh::FindTransBoundBox(const Matrix44 &trans, Vector3d &minVec, Vector3d &maxVec) const {
  Vector3d p;
  for (size_t i = 0; i < vertices.size(); i++) {
    trans.Transform(p, vertices[i]);
    if (i == 0) {
      minVec = maxVec = p;
      continue;
    }
    minVec.NewVector(Min(minVec.GetX(), p.GetX()), Min(minVec.GetY(), p.GetY()), Min(minVec.GetZ(), p.GetZ()));
    maxVec.NewVector(Max(maxVec.GetX(), p.GetX()), Max(maxVec.GetY(), p.GetY()), Max(maxVec.GetZ(), p.GetZ()));
  }
}

bool
StaticMesh::HitMesh(const Line &ray, float rangeBegin, float rangeEnd, float &depth) const {
  // Moller-Trumbore on each triangle.
  Vector3d dir(ray.GetSlope());
  const float dirLen = dir.GetMag();
  if (dirLen < 1e-8f)
    return false;
  dir.MultVector(1.0f / dirLen);
  const Vector3d &origin = ray.GetPoint();
  bool hit = false;
  Vector3d e1, e2, p, s, q;
  for (const Triangle &tri : triangles) {
    const Vector3d &v0 = vertices[tri.v[0]];
    e1.SubVector(vertices[tri.v[1]], v0);
    e2.SubVector(vertices[tri.v[2]], v0);
    p.CrossVector(dir, e2);
    const float det = e1.DotVector(p);
    if (fabsf(det) < 1e-8f)
      continue;
    const float inv = 1.0f / det;
    s.SubVector(origin, v0);
    const float u = s.DotVector(p) * inv;
    if (u < 0.0f || u > 1.0f)
      continue;
    q.CrossVector(s, e1);
    const float v = dir.DotVector(q) * inv;
    if (v < 0.0f || u + v > 1.0f)
      continue;
    const float t = e2.DotVector(q) * inv;
    if (t < rangeBegin || t > rangeEnd)
      continue;
    if (!hit || t < depth) {
      depth = t;
      hit = true;
    }
  }
  return hit;
}

Points3d &
StaticMesh::Transform(const Matrix44 &transMatrix, Points3d &vBuffer) const {
  // As Geometry::Transform does: the buffer holds a point per vertex.
  int i;
  const int needed = (int) vertices.size();
  if (vBuffer.numElm == 0) {
    for (i = 0; i < vBuffer.arraySize; i++)
      vBuffer.array[i] = new Point3d;
    vBuffer.numElm = vBuffer.arraySize;
  }
  check(vBuffer.numElm == vBuffer.arraySize);
  if (vBuffer.arraySize < needed) {
    i = vBuffer.arraySize;
    vBuffer.arraySize = needed;
    vBuffer.array = (Point3d **) realloc(vBuffer.array, sizeof(Point3d *) * vBuffer.arraySize);
    for (; i < vBuffer.arraySize; i++)
      vBuffer.array[i] = new Point3d;
    vBuffer.numElm = vBuffer.arraySize;
  }
  for (i = 0; i < needed; i++)
    transMatrix.Transform(*(vBuffer.array[i]), vertices[i]);
  return vBuffer;
}

std::shared_ptr<const StaticMesh>
StaticMesh::Find(const char *meshName) {
  if (meshName == NULL)
    return nullptr;
  for (const auto &mesh : GetLibrary().meshes) {
    if (_stricmp(mesh->GetName(), meshName) == 0)
      return mesh;
  }
  return nullptr;
}

const std::vector<std::string> &
StaticMesh::GetBasicShapeNames(void) {
  return GetLibrary().names;
}
