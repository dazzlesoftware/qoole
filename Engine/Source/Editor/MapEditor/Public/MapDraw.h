// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// FMapDraw: the map core's drawing interface (FDraw, which View renders
// through) collected into lists the GPU can draw.
//
//   - Lines (wireframes, the grid, selection) become thin quads in one
//     custom-vertex element per batch, drawn by the UI renderer.
//   - Triangles (View::RenderSolid) become vertices in window pixels with
//     q = 1/depth, drawn with a depth buffer by FMapViewportClient's map
//     pipeline (perspective-correct, like the old GPU renderer plugins).
//
// Colors: View draws with palette indices (VRC_* and the grid's 252-254),
// which SetColor fills. Solid faces get a color per object and, when shaded
// (the Lit view mode), a shade per facing direction, since the built-in game
// has no textures. Landscapes with textured layers are the exception: their
// triangles carry where they are on the grid (U, V) and are drawn with the
// landscape's baked image (FLandscapeBake), in batches of their own.
//
// Coordinates are physical pixels of the target.

#pragma once
#include "Renderer/NativeMeshDrawList.h"
#include "Renderer/ProjectedSceneRenderer.h"

#include "CoreMinimal.h"
#include "Rendering/DrawElements.h"

#include "MapCoreHeaders.h"

using FMapVertex=FProjectedVertex;

// Runtime triangle batches retain terrain snapshots; untextured faces use white.
using FMapTriangleBatch=FProjectedTriangleBatch;

// Copies of one mesh drawn on the GPU (foliage): each copy's
// object-to-view matrix, its first three rows (12 floats a copy).
using FMapInstanceBatch=FMeshInstanceBatch;

using FMapMaterialVertex = FNativeMeshVertex;
using FMapMaterialBatch = FNativeMeshBatch;

class FMapDraw final : public FDraw
{
public:
	FMapDraw();

	void Size(int Cx, int Cy) override;
	int GetWidth() const { return width; }
	int GetHeight() const { return height; }
	// Line thickness in pixels (the DPI scale, so lines stay one logical pixel).
	void SetLineThickness(float Thickness) { LineThickness = Thickness; }
	// Lit: faces are shaded by the way they face; Unlit: flat colors.
	void SetShaded(bool bInShaded) { bShaded = bInShaded; }

	// A line among the triangles, so the depth buffer hides it behind faces
	// (window pixels, Q = 1 / view depth at each end). It loses to a face at
	// the same depth.
	void DepthLine(float X1, float Y1, float Q1, float X2, float Y2, float Q2);

	// Empties the line and triangle lists (the palette stays).
	void Reset();
	// Moves the lines drawn since the last call into Out as one element.
	void FlushLines(FSlateWindowElementList& Out, int32 Layer);
	const TArray<FMapVertex>& GetTriangles() const { return Triangles; }
	// The triangles in drawing order, by texture.
	const TArray<FMapTriangleBatch>& GetBatches() const { return Batches; }
	// Meshes whose copies the GPU draws (ZBufMeshInstance), and how to
	// light and project them: the light and the sky in view space, the
	// projection's width, and the window's center (all set by View).
	bool bInstancing = true;
	bool bMaterials = true;
	const FNativeMeshDrawList& GetMaterialDrawList() const { return MaterialDraws; }
	const TArray<FMapMaterialVertex>& GetMaterialVertices() const { return MaterialDraws.Vertices; }
	const TArray<FMapMaterialBatch>& GetMaterialBatches() const { return MaterialDraws.Batches; }
	const TArray<FMapInstanceBatch>& GetInstanceBatches() const { return InstanceBatches; }
	const Vector3d& GetInstanceLight() const { return InstanceLight; }
	const Vector3d& GetInstanceUp() const { return InstanceUp; }
	float GetInstanceWinWidth() const { return InstanceWinWidth; }
	FVector2 GetOrigin() const { return FVector2(static_cast<float>(orgX), static_cast<float>(orgY)); }
	bool IsShaded() const { return bShaded; }

	FColor GetPaletteColor(int Index) const;

	// FDraw
	void Clear() override { Reset(); }
	void Color(int NewColor) override { CurrentColor = NewColor; }
	void Begin() override {}
	void Line(int X1, int Y1, int X2, int Y2) override;
	void CopyBuf(int) override {}
	void RealizePal() override {}
	void ZBufInit() override {}
	void ZBufClear() override { Triangles.clear(); }
	void ZBufTriangle(ZPoint Points[3]) override;
	void ZBufShadedTriangle(ZPoint Points[3], const float Shades[3]) override;
	void ZBufColoredTriangle(ZPoint Points[3], const float Shades[3], const unsigned int Colors[3]) override;
	void ZBufLandscapeTriangle(ZPoint Points[3], const float Shades[3], const unsigned int Colors[3]) override;
	void ZBufInstanceLight(const Vector3d& Light, const Vector3d& Up, float WinWidth) override;
	bool ZBufMeshInstance(const std::shared_ptr<const StaticMesh>& Mesh, const Matrix44& ToView) override;
	bool ZBufMaterialMesh(const Object& Object, const Matrix44& ToView) override;
	void ZBufRender() override {}

private:
	// The color of the object being drawn (FDraw::ZBufSetup set it), shaded.
	FColor ObjectColor(float Shade) const;
	void PushTriangle(const ZPoint Points[3], const uint32 Colors[3], const Object* Textured = nullptr);
	void AddVertex(const FMapVertex& Vertex, const Object* Textured);
	float LitShade(float Shade) const { return bShaded ? FMath::Clamp(Shade * 1.1f, 0.0f, 1.0f) : 0.86f; }

	TArray<FSlateVertex2D> LineVertices;
	TArray<uint32> LineIndices;
	TArray<FMapVertex> Triangles;
	TArray<FMapTriangleBatch> Batches;
	TArray<FMapInstanceBatch> InstanceBatches;
	FNativeMeshDrawList MaterialDraws;
	Vector3d InstanceLight;
	Vector3d InstanceUp;
	float InstanceWinWidth = 1.0f;
	int CurrentColor = 0;
	float LineThickness = 1.0f;
	bool bShaded = true;
};
