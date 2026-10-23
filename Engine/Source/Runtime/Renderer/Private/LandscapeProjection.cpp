// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Renderer/LandscapeProjection.h"
#include <algorithm>
#include <cmath>
void AppendProjectedLandscape(const std::shared_ptr<const Landscape>& Land, uint64 Key, const Matrix44& MeshToView,
    const Matrix44& MeshToWorld, const Vector3d& Scale, const FSceneViewVolume& Volume, const FProjectedSceneView& View,
    float TargetPixels, TArray<FProjectedVertex>& Vertices, TArray<FProjectedTriangleBatch>& Batches,
    FLandscapeDrawStats& Stats)
{
	Stats = {};
	if (!Land)
		return;
	class FSink final : public ILandscapeTriangleSink
	{
	public:
		FSink(const FProjectedSceneView& InView, bool Texture, TArray<FProjectedVertex>& V)
		    : View(InView), Textured(Texture), Vertices(V)
		{
		}
		void OnTriangle(const FLandscapeTriangle& T) override
		{
			for (const FLandscapeTriangleVertex& P : T.Vertices)
			{
				const float Shade = View.bLit ? std::clamp(P.Shade * 1.1f, 0.0f, 1.0f) : 0.86f;
				uint32 Color = 0xFF000000;
				const float Base[3] = {158.1f, 168.3f, 147.9f};
				for (int C = 0; C < 3; C++)
				{
					const float Value = Textured ? 255.0f : T.bColored ? float((P.Color >> (8 * C)) & 255) : Base[C];
					Color |= static_cast<uint32>(std::clamp(Value * Shade + 0.5f, 0.0f, 255.0f)) << (8 * C);
				}
				const float Q = 1.0f / std::max(P.Position.GetY(), 1e-4f);
				Vertices.push_back({View.CenterX + P.Position.GetX() * View.ProjectionScale * Q,
				    View.CenterY - P.Position.GetZ() * View.ProjectionScale * Q, Q, Textured ? P.U : 0.0f,
				    Textured ? P.V : 0.0f, Color});
			}
		}

	private:
		const FProjectedSceneView& View;
		bool Textured;
		TArray<FProjectedVertex>& Vertices;
	} Sink(View, FLandscapeTextureLibrary::IsTextured(*Land), Vertices);
	const uint32 First = static_cast<uint32>(Vertices.size());
	BuildLandscapeTriangles(
	    *Land, MeshToView, MeshToWorld, Scale, Volume, View.ProjectionScale, TargetPixels, Sink, Stats);
	const uint32 Count = static_cast<uint32>(Vertices.size()) - First;
	if (Count)
		Batches.push_back({Key, FLandscapeTextureLibrary::IsTextured(*Land) ? Land : nullptr, First, Count});
}
