/*
 * StaticMesh.cpp
 *
 * Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
 */

#include <math.h>
#include <string.h>

#include "Geometry/StaticMesh.h"

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


  // ----- Foliage meshes: low, colored per vertex, pivoted where they stand.

  unsigned int Rgb(int r, int g, int b) {
    return (unsigned int) r | ((unsigned int) g << 8) | ((unsigned int) b << 16) | 0xFF000000u;
  }

  unsigned int Mix(unsigned int a, unsigned int b, float t) {
    t = Max(0.0f, Min(t, 1.0f));
    unsigned int out = 0xFF000000u;
    for (int c = 0; c < 3; c++) {
      const float va = (float) ((a >> (8 * c)) & 0xFF), vb = (float) ((b >> (8 * c)) & 0xFF);
      out |= (unsigned int) (va + (vb - va) * t + 0.5f) << (8 * c);
    }
    return out;
  }

  // 0 to 1, fixed per (i, j, seed).
  float Jitter(int i, int j, unsigned int seed) {
    unsigned int h = (unsigned int) i * 374761393u + (unsigned int) j * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float) (h & 0xFFFFu) / 65535.0f;
  }

  struct ColoredMesh {
    std::shared_ptr<StaticMesh> mesh;
    std::vector<unsigned int> colors;
    explicit ColoredMesh(const char *meshName) : mesh(std::make_shared<StaticMesh>(meshName)) {}
    int Vertex(const Vector3d &p, unsigned int color) {
      colors.push_back(color);
      return mesh->AddVertex(p);
    }
    std::shared_ptr<StaticMesh> Done(void) {
      mesh->SetVertexColors(std::move(colors));
      mesh->Finish();
      return mesh;
    }
  };

  // A lumpy ellipsoid: smooth normals, shaded from bottom to top.
  void Blob(ColoredMesh &out, const Vector3d &center, float rx, float ry, float rz, float lump, unsigned int seed,
            unsigned int bottom, unsigned int top, int rings = 6, int segments = 9) {
    std::vector<std::vector<int>> ring(rings + 1);
    std::vector<std::vector<Vector3d>> normal(rings + 1);
    for (int i = 0; i <= rings; i++) {
      const float phi = Pi * (float) i / rings;
      const int count = (i == 0 || i == rings) ? 1 : segments;
      for (int j = 0; j < count; j++) {
        const float theta = 2.0f * Pi * ((float) j + 0.5f * (i & 1)) / segments;
        const float k = 1.0f + lump * (2.0f * Jitter(i, j, seed) - 1.0f);
        const Vector3d dir(sinf(phi) * cosf(theta), sinf(phi) * sinf(theta), cosf(phi));
        const Vector3d p(center.GetX() + dir.GetX() * rx * k, center.GetY() + dir.GetY() * ry * k, center.GetZ() + dir.GetZ() * rz * k);
        ring[i].push_back(out.Vertex(p, Mix(bottom, top, 1.0f - (float) i / rings)));
        normal[i].push_back(Unit(dir.GetX() / rx, dir.GetY() / ry, dir.GetZ() / rz));
      }
    }
    for (int i = 0; i < rings; i++) {
      for (int j = 0; j < segments; j++) {
        const int j1 = (j + 1) % segments;
        if (i == 0) {
          out.mesh->AddTriangle(ring[0][0], ring[1][j], ring[1][j1], normal[0][0], normal[1][j], normal[1][j1]);
        }
        else if (i == rings - 1) {
          out.mesh->AddTriangle(ring[i][j], ring[rings][0], ring[i][j1], normal[i][j], normal[rings][0], normal[i][j1]);
        }
        else {
          out.mesh->AddTriangle(ring[i][j], ring[i + 1][j], ring[i + 1][j1], normal[i][j], normal[i + 1][j], normal[i + 1][j1]);
          out.mesh->AddTriangle(ring[i][j], ring[i + 1][j1], ring[i][j1], normal[i][j], normal[i + 1][j1], normal[i][j1]);
        }
      }
    }
    // The wireframe: its middle ring and a few meridians.
    const int middle = rings / 2;
    for (int j = 0; j < segments; j++)
      out.mesh->AddEdge(ring[middle][j], ring[middle][(j + 1) % segments]);
    for (int j = 0; j < segments; j += 3) {
      for (int i = 0; i < rings; i++) {
        const int a = (i == 0) ? ring[0][0] : ring[i][j];
        const int b = (i + 1 == rings) ? ring[rings][0] : ring[i + 1][j];
        out.mesh->AddEdge(a, b);
      }
    }
  }

  // A trunk or a cone: radius r0 at z0 to r1 at z1 (0: a point), with a
  // floor when it is a cone (seen from below).
  void Frustum(ColoredMesh &out, float z0, float z1, float r0, float r1, unsigned int bottom, unsigned int top, int segments = 8) {
    std::vector<int> low, high;
    const float lean = (r0 - r1) / Max(z1 - z0, 1e-3f);
    for (int j = 0; j < segments; j++) {
      const float theta = 2.0f * Pi * (float) j / segments;
      low.push_back(out.Vertex(Vector3d(r0 * cosf(theta), r0 * sinf(theta), z0), bottom));
      high.push_back(r1 > 0.0f ? out.Vertex(Vector3d(r1 * cosf(theta), r1 * sinf(theta), z1), top) : -1);
    }
    const int apex = r1 > 0.0f ? -1 : out.Vertex(Vector3d(0.0f, 0.0f, z1), top);
    const auto side = [&](float theta) { return Unit(cosf(theta), sinf(theta), lean); };
    for (int j = 0; j < segments; j++) {
      const int j1 = (j + 1) % segments;
      const float t0 = 2.0f * Pi * (float) j / segments, t1 = 2.0f * Pi * (float) (j + 1) / segments;
      if (apex >= 0) {
        out.mesh->AddTriangle(low[j], low[j1], apex, side(t0), side(t1), side(0.5f * (t0 + t1)));
      }
      else {
        out.mesh->AddTriangle(low[j], low[j1], high[j1], side(t0), side(t1), side(t1));
        out.mesh->AddTriangle(low[j], high[j1], high[j], side(t0), side(t1), side(t0));
      }
      out.mesh->AddEdge(low[j], low[j1]);
      if (j % 2 == 0)
        out.mesh->AddEdge(low[j], apex >= 0 ? apex : high[j]);
    }
    if (apex >= 0) {
      const int middle = out.Vertex(Vector3d(0.0f, 0.0f, z0), bottom);
      for (int j = 0; j < segments; j++)
        out.mesh->AddFlatTriangle(middle, low[(j + 1) % segments], low[j]);
    }
  }

  std::shared_ptr<StaticMesh> MakeGrass(void) {
    // A tuft of blades, each a thin triangle seen from both sides.
    ColoredMesh out("Grass");
    const unsigned int root = Rgb(58, 96, 38), tip = Rgb(150, 182, 84);
    const int blades = 9;
    for (int b = 0; b < blades; b++) {
      const float angle = 2.0f * Pi * (b + Jitter(b, 1, 3)) / blades;
      const float at = 3.0f + 9.0f * Jitter(b, 2, 3);
      const float height = 26.0f + 18.0f * Jitter(b, 3, 3);
      const float lean = 6.0f + 10.0f * Jitter(b, 4, 3);
      const float width = 2.6f;
      const Vector3d base(at * cosf(angle), at * sinf(angle), 0.0f);
      // Across the blade, at right angles to where it leans.
      const float cx = -sinf(angle) * width, cy = cosf(angle) * width;
      const int a = out.Vertex(Vector3d(base.GetX() - cx, base.GetY() - cy, 0.0f), root);
      const int c = out.Vertex(Vector3d(base.GetX() + cx, base.GetY() + cy, 0.0f), root);
      const int t = out.Vertex(Vector3d(base.GetX() + lean * cosf(angle), base.GetY() + lean * sinf(angle), height), tip);
      out.mesh->AddFlatTriangle(a, c, t);
      out.mesh->AddFlatTriangle(c, a, t);
      out.mesh->AddEdge(a, t);
      out.mesh->AddEdge(c, t);
    }
    return out.Done();
  }

  std::shared_ptr<StaticMesh> MakeBush(void) {
    ColoredMesh out("Bush");
    Blob(out, Vector3d(0.0f, 0.0f, 30.0f), 50.0f, 46.0f, 36.0f, 0.18f, 11, Rgb(44, 82, 36), Rgb(92, 140, 60));
    Blob(out, Vector3d(22.0f, -14.0f, 46.0f), 30.0f, 28.0f, 26.0f, 0.2f, 12, Rgb(50, 92, 40), Rgb(104, 152, 66), 5, 8);
    return out.Done();
  }

  std::shared_ptr<StaticMesh> MakeTree(void) {
    ColoredMesh out("Tree");
    Frustum(out, 0.0f, 150.0f, 12.0f, 7.0f, Rgb(84, 62, 42), Rgb(110, 82, 56));
    Blob(out, Vector3d(0.0f, 0.0f, 190.0f), 82.0f, 78.0f, 70.0f, 0.16f, 21, Rgb(42, 84, 38), Rgb(88, 136, 56), 7, 11);
    Blob(out, Vector3d(34.0f, -20.0f, 236.0f), 48.0f, 46.0f, 42.0f, 0.18f, 22, Rgb(50, 96, 42), Rgb(100, 148, 62));
    return out.Done();
  }

  std::shared_ptr<StaticMesh> MakePine(void) {
    ColoredMesh out("Pine");
    Frustum(out, 0.0f, 60.0f, 10.0f, 7.0f, Rgb(80, 58, 40), Rgb(100, 74, 50));
    Frustum(out, 40.0f, 150.0f, 78.0f, 0.0f, Rgb(30, 70, 44), Rgb(54, 104, 62), 9);
    Frustum(out, 105.0f, 205.0f, 60.0f, 0.0f, Rgb(34, 76, 48), Rgb(60, 112, 66), 9);
    Frustum(out, 165.0f, 262.0f, 40.0f, 0.0f, Rgb(38, 82, 52), Rgb(68, 120, 72), 8);
    return out.Done();
  }

  std::shared_ptr<StaticMesh> MakeRock(void) {
    ColoredMesh out("Rock");
    // Half sunk: its pivot is on the ground.
    Blob(out, Vector3d(0.0f, 0.0f, 8.0f), 52.0f, 42.0f, 30.0f, 0.28f, 31, Rgb(104, 100, 94), Rgb(146, 142, 134), 6, 8);
    return out.Done();
  }

  // ----- Spline meshes: run along +X from 0 to their length, centered
  // across Y, sliced along X so that they bend along a curve.

  // A box from (x0, y0, z0) to (x1, y1, z1), sliced along x, colored.
  void SlicedBox(ColoredMesh &out, float x0, float x1, float y0, float y1, float z0, float z1, int slices, unsigned int color,
                 unsigned int top) {
    // Rings of 4 corners along x: bottom-left, bottom-right, top-right, top-left (seen along +x).
    std::vector<std::vector<int>> rings;
    for (int s = 0; s <= slices; s++) {
      const float x = x0 + (x1 - x0) * s / slices;
      rings.push_back({ out.Vertex(Vector3d(x, y1, z0), color), out.Vertex(Vector3d(x, y0, z0), color),
                        out.Vertex(Vector3d(x, y0, z1), top), out.Vertex(Vector3d(x, y1, z1), top) });
    }
    const Vector3d sideNormals[4] = { Vector3d(0.0f, 0.0f, -1.0f), Vector3d(0.0f, -1.0f, 0.0f), Vector3d(0.0f, 0.0f, 1.0f), Vector3d(0.0f, 1.0f, 0.0f) };
    for (int s = 0; s < slices; s++) {
      for (int f = 0; f < 4; f++) {
        const int a = rings[s][f], b = rings[s][(f + 1) % 4], c = rings[s + 1][(f + 1) % 4], d = rings[s + 1][f];
        const Vector3d &n = sideNormals[f];
        out.mesh->AddTriangle(a, c, b, n, n, n);
        out.mesh->AddTriangle(a, d, c, n, n, n);
      }
    }
    // The ends.
    out.mesh->AddFlatTriangle(rings[0][0], rings[0][1], rings[0][2]);
    out.mesh->AddFlatTriangle(rings[0][0], rings[0][2], rings[0][3]);
    out.mesh->AddFlatTriangle(rings[slices][0], rings[slices][2], rings[slices][1]);
    out.mesh->AddFlatTriangle(rings[slices][0], rings[slices][3], rings[slices][2]);
    for (int f = 0; f < 4; f++) {
      out.mesh->AddEdge(rings[0][f], rings[0][(f + 1) % 4]);
      out.mesh->AddEdge(rings[slices][f], rings[slices][(f + 1) % 4]);
      out.mesh->AddEdge(rings[0][f], rings[slices][f]);
    }
  }

  // A strip across Y at heights given per point of its cross-section,
  // sliced along x; colors per cross-section point.
  void Strip(ColoredMesh &out, float length, const std::vector<float> &ys, const std::vector<float> &zs, const std::vector<unsigned int> &colors,
             int slices) {
    std::vector<std::vector<int>> rows;
    for (int s = 0; s <= slices; s++) {
      const float x = length * s / slices;
      std::vector<int> row;
      for (size_t i = 0; i < ys.size(); i++)
        row.push_back(out.Vertex(Vector3d(x, ys[i], zs[i]), colors[i]));
      rows.push_back(row);
    }
    for (int s = 0; s < slices; s++) {
      for (size_t i = 0; i + 1 < ys.size(); i++) {
        // Facing up: counterclockwise seen from above.
        out.mesh->AddFlatTriangle(rows[s][i], rows[s + 1][i + 1], rows[s][i + 1]);
        out.mesh->AddFlatTriangle(rows[s][i], rows[s + 1][i], rows[s + 1][i + 1]);
      }
    }
    for (size_t i = 0; i < ys.size(); i += ys.size() - 1)
      out.mesh->AddEdge(rows[0][i], rows[slices][i]);
    out.mesh->AddEdge(rows[0][0], rows[0][ys.size() - 1]);
    out.mesh->AddEdge(rows[slices][0], rows[slices][ys.size() - 1]);
  }

  std::shared_ptr<StaticMesh> MakeRoad(void) {
    // Asphalt 256 wide with low shoulders, and a dashed line down its middle.
    ColoredMesh out("Road");
    const unsigned int shoulder = Rgb(96, 90, 80), asphalt = Rgb(58, 58, 62);
    Strip(out, 512.0f, { -128.0f, -112.0f, 112.0f, 128.0f }, { -6.0f, 3.0f, 3.0f, -6.0f }, { shoulder, asphalt, asphalt, shoulder }, 16);
    const unsigned int paint = Rgb(220, 210, 170);
    for (int dash = 0; dash < 4; dash++) {
      const float x0 = dash * 128.0f + 16.0f, x1 = x0 + 64.0f;
      const int a = out.Vertex(Vector3d(x0, -3.0f, 3.5f), paint), b = out.Vertex(Vector3d(x0, 3.0f, 3.5f), paint);
      const int c = out.Vertex(Vector3d((x0 + x1) * 0.5f, 3.0f, 3.5f), paint), d = out.Vertex(Vector3d((x0 + x1) * 0.5f, -3.0f, 3.5f), paint);
      const int e = out.Vertex(Vector3d(x1, 3.0f, 3.5f), paint), f = out.Vertex(Vector3d(x1, -3.0f, 3.5f), paint);
      out.mesh->AddFlatTriangle(a, c, b);
      out.mesh->AddFlatTriangle(a, d, c);
      out.mesh->AddFlatTriangle(d, e, c);
      out.mesh->AddFlatTriangle(d, f, e);
    }
    return out.Done();
  }

  std::shared_ptr<StaticMesh> MakeFence(void) {
    // A post at its start and two rails to the next one, 200 on.
    ColoredMesh out("Fence");
    const unsigned int wood = Rgb(112, 82, 52), light = Rgb(140, 108, 72);
    SlicedBox(out, -5.0f, 5.0f, -5.0f, 5.0f, -10.0f, 110.0f, 1, wood, light);
    SlicedBox(out, 0.0f, 200.0f, -2.5f, 2.5f, 44.0f, 56.0f, 6, wood, light);
    SlicedBox(out, 0.0f, 200.0f, -2.5f, 2.5f, 84.0f, 96.0f, 6, wood, light);
    return out.Done();
  }

  std::shared_ptr<StaticMesh> MakeWall(void) {
    // Stone: a little uneven along its top.
    ColoredMesh out("Wall");
    SlicedBox(out, 0.0f, 200.0f, -20.0f, 20.0f, -10.0f, 80.0f, 8, Rgb(112, 108, 100), Rgb(150, 146, 136));
    SlicedBox(out, 0.0f, 200.0f, -24.0f, 24.0f, 80.0f, 92.0f, 8, Rgb(126, 122, 112), Rgb(160, 156, 146));
    return out.Done();
  }

  std::shared_ptr<StaticMesh> MakeRiver(void) {
    // Water 256 wide (its banks a little lower, under the ground).
    ColoredMesh out("River");
    const unsigned int deep = Rgb(40, 84, 120), shallow = Rgb(70, 124, 150);
    Strip(out, 512.0f, { -136.0f, -100.0f, 0.0f, 100.0f, 136.0f }, { -8.0f, 0.0f, 0.0f, 0.0f, -8.0f }, { shallow, shallow, deep, shallow, shallow }, 16);
    return out.Done();
  }

  struct Library {
    std::vector<std::shared_ptr<const StaticMesh>> meshes;
    std::vector<std::string> names, foliageNames, splineNames;
    Library(void) {
      meshes.push_back(MakeCube());
      meshes.push_back(MakeSphere());
      meshes.push_back(MakeRound("Cylinder", StaticMesh::BasicShapeSize * 0.5f));
      meshes.push_back(MakeRound("Cone", 0.0f));
      meshes.push_back(MakePlane());
      for (const auto &mesh : meshes)
        names.push_back(mesh->GetName());
      for (const auto &mesh : { MakeGrass(), MakeBush(), MakeTree(), MakePine(), MakeRock() }) {
        meshes.push_back(mesh);
        foliageNames.push_back(mesh->GetName());
      }
      for (const auto &mesh : { MakeRoad(), MakeFence(), MakeWall(), MakeRiver() }) {
        meshes.push_back(mesh);
        splineNames.push_back(mesh->GetName());
      }
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
  edges.push_back({ a, b });
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

const std::vector<std::string> &
StaticMesh::GetFoliageMeshNames(void) {
  return GetLibrary().foliageNames;
}

const std::vector<std::string> &
StaticMesh::GetSplineMeshNames(void) {
  return GetLibrary().splineNames;
}
