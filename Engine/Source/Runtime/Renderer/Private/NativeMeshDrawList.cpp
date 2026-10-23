// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Renderer/NativeMeshDrawList.h"
#include "Misc/ValidationContext.h"
#include "Misc/ScopeExit.h"
#include "Geometry/StaticMesh.h"
#include <algorithm>
#include <cmath>

bool FNativeMeshDrawList::Append(
    const std::shared_ptr<const FLoadedMeshAsset>& Asset, const Matrix44& MeshToView, FString& Error)
{
	const unsigned long long VertexCount = Vertices.size(), BatchCount = Batches.size();
	const uint32 Tail = BatchCount ? Batches.back().NumVertices : 0;
	FValidationContext Validation(Error);
	FScopeExit Rollback([&]
	{
		Vertices.resize(VertexCount);
		Batches.resize(BatchCount);
		if (BatchCount)
			Batches.back().NumVertices = Tail;
	});

	if (!Asset || !Asset->Data.Mesh || Asset->Data.GetNumMaterials() > 64)
	{
		Validation.Fail(GENESIS_TEXT("Missing or invalid mesh snapshot."));
		return false;
	}
	const StaticMesh& Mesh = *Asset->Data.Mesh;
	Vector3d Origin, Axis[3], Cofactor[3];
	MeshToView.Transform(Origin, Vector3d());
	for (int I = 0; I < 3; ++I)
	{
		Vector3d Unit(I == 0 ? 1.0f : 0.0f, I == 1 ? 1.0f : 0.0f, I == 2 ? 1.0f : 0.0f);
		MeshToView.Transform(Axis[I], Unit);
		Axis[I].SubVector(Origin);
	}
	Cofactor[0].CrossVector(Axis[1], Axis[2]);
	Cofactor[1].CrossVector(Axis[2], Axis[0]);
	Cofactor[2].CrossVector(Axis[0], Axis[1]);
	const float Det = Axis[0].DotVector(Cofactor[0]);
	if (!std::isfinite(Det) || std::abs(Det) < 1e-30f)
	{
		Validation.Fail(GENESIS_TEXT("Mesh transform is singular or nonfinite."));
		return false;
	}
	for (const StaticMesh::Triangle& T : Mesh.GetTriangles())
	{
		if (T.MaterialSlot >= static_cast<uint32>(Asset->Data.GetNumMaterials()) ||
		    (T.MaterialSlot > 0 && T.MaterialSlot > Asset->AdditionalSlots.size()))
		{
			Validation.Fail(GENESIS_TEXT("Invalid prepared material slot."));
			return false;
		}
		for (int Index : T.v)
			if (Index < 0 || Index >= Mesh.GetNumVertices())
			{
				Validation.Fail(GENESIS_TEXT("Invalid mesh vertex index."));
				return false;
			}
		const FLoadedMaterialSlot& Slot = Asset->GetSlot(T.MaterialSlot);
		const bool Translucent = Slot.Material && Slot.Material->BlendMode == EMaterialBlendMode::Translucent;
		Vector3d P[3], A, B, Face;
		for (int K = 0; K < 3; ++K)
		{
			MeshToView.Transform(P[K], Mesh.GetVertex(T.v[K]));
			if (!std::isfinite(P[K].GetX()) || !std::isfinite(P[K].GetY()) || !std::isfinite(P[K].GetZ()))
			{
				Validation.Fail(GENESIS_TEXT("Nonfinite transformed mesh position."));
				return false;
			}
		}
		A.SubVector(P[1], P[0]);
		B.SubVector(P[2], P[0]);
		Face.CrossVector(A, B);
		const float Facing = Face.DotVector(P[0]);
		if (Det < 0 ? Facing <= 0 : Facing >= 0)
			continue;
		const uint32 TriangleStart = static_cast<uint32>(Vertices.size());
		for (int K = 0; K < 3; ++K)
		{
			const Vector3d& N = T.norm[K];
			Vector3d Normal;
			Normal.NewVector(
			    (Cofactor[0].GetX() * N.GetX() + Cofactor[1].GetX() * N.GetY() + Cofactor[2].GetX() * N.GetZ()) / Det,
			    (Cofactor[0].GetY() * N.GetX() + Cofactor[1].GetY() * N.GetY() + Cofactor[2].GetY() * N.GetZ()) / Det,
			    (Cofactor[0].GetZ() * N.GetX() + Cofactor[1].GetZ() * N.GetY() + Cofactor[2].GetZ() * N.GetZ()) / Det);
			const float Length = Normal.GetMag();
			if (!std::isfinite(Length) || !std::isfinite(T.uv[K][0]) || !std::isfinite(T.uv[K][1]))
			{
				Validation.Fail(GENESIS_TEXT("Nonfinite mesh normal or UV."));
				return false;
			}
			if (Length > 1e-20f)
				Normal.MultVector(1 / Length);
			FNativeMeshVertex V = {{P[K].GetX(), P[K].GetY(), P[K].GetZ()},
			    {Normal.GetX(), Normal.GetY(), Normal.GetZ()}, {T.uv[K][0], T.uv[K][1]},
			    Mesh.HasVertexColors() ? Mesh.GetVertexColor(T.v[K]) : 0xffffffffu};
			Vertices.push_back(V);
		}
		if (!Translucent && !Batches.empty() && Batches.back().Asset == Asset &&
		    Batches.back().Slot == T.MaterialSlot &&
		    Batches.back().FirstVertex + Batches.back().NumVertices == TriangleStart)
			Batches.back().NumVertices += 3;
		else
			Batches.push_back({Asset, TriangleStart, 3, (P[0].GetY() + P[1].GetY() + P[2].GetY()) / 3, T.MaterialSlot});
	}
	Error.Empty();
	Rollback.Release();
	return true;
}
TArray<size_t> FNativeMeshDrawList::GetDrawOrder(bool Transparent) const
{
	TArray<size_t> Order;
	for (size_t I = 0; I < Batches.size(); ++I)
	{
		const TOptional<FMaterialAsset>& Material = Batches[I].Asset->GetSlot(Batches[I].Slot).Material;
		if ((Material && Material->BlendMode == EMaterialBlendMode::Translucent) == Transparent)
			Order.push_back(I);
	}
	if (Transparent)
		std::stable_sort(
		    Order.begin(), Order.end(), [&](size_t A, size_t B) { return Batches[A].Depth > Batches[B].Depth; });
	return Order;
}
