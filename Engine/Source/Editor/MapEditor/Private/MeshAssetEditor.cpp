// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "MeshAssetEditor.h"
#include "Templates/SharedPointer.h"
#include "Internationalization/TextLocalization.h"
#include "Misc/ValidationContext.h"
#include "MapAssetImport.h"
#include "HAL/File.h"
#include "Geometry/StaticMesh.h"
#undef Min
#undef Max
#include <filesystem>

namespace
{
	bool LoadSlot(const FAssetReference& Ref, const FAssetRegistry& Registry, FLoadedMaterialSlot& Out, FString& Error)
	{
		if (Ref.IsNull())
		{
			Out = {};
			Error.Empty();
			return true;
		}
		FRenderAssetLoader Loader(Registry);
		FLoadedMaterialSlot Slot;
		FMaterialAsset M;
		if (!Loader.LoadMaterial(Ref, M, Error))
			return false;
		if (!Loader.LoadMaterialResources(M, Slot, Error))
			return false;
		Out = std::move(Slot);
		Error.Empty();
		return true;
	}
} // namespace
TUniquePtr<FMeshAssetEditSession> FMeshAssetEditSession::Open(
    const FAssetRegistry& Registry, const FAssetReference& Ref, FString& Error)
{
	const FAssetData* Data = Registry.Resolve(Ref, Error, "Genesis.StaticMesh");
	if (!Data)
		return {};
	TUniquePtr<FMeshAssetEditSession> Session = MakeUnique<FMeshAssetEditSession>();
	Session->Target = Data->FilePath;
	Session->Reference = Data->GetReference();
	FAssetPackage P;
	FString After;
	if (!FFile::ReadAllText(Session->Target, Session->Manifest) ||
	    !FAssetPackageStore::Load(Session->Target, P, Error) ||
	    !FRenderAssetCodec::DecodeMesh(P, Session->Draft, Error))
	{
		if (Error.IsEmpty())
			Error = GENESIS_TEXT("Cannot read mesh asset.");
		return {};
	}
	if (P.Id != Session->Reference.Id)
	{
		Error = GENESIS_TEXT("Mesh asset identity changed; refresh content.");
		return {};
	}
	if (!FFile::ReadAllText(Session->Target, After) || After != Session->Manifest)
	{
		Error = GENESIS_TEXT("Mesh changed while opening; try again.");
		return {};
	}
	Session->SourceFile = FMapAssetImport::GetMeshImportSource(P, Session->Target);
	Error.Empty();
	return Session;
}
bool FMeshAssetEditSession::SetMaterial(
    int32 Slot, const FAssetReference& Ref, const FAssetRegistry& Registry, FString& Error)
{
	if (Slot < 0 || Slot >= Draft.GetNumMaterials())
	{
		Error = GENESIS_TEXT("Material slot outside mesh.");
		return false;
	}
	FLoadedMaterialSlot Loaded;
	if (!LoadSlot(Ref, Registry, Loaded, Error))
		return false;
	if (Slot == 0)
		Draft.Material = Ref;
	else
		Draft.AdditionalMaterials[Slot - 1] = Ref;
	bDirty = true;
	Error.Empty();
	return true;
}
bool FMeshAssetEditSession::BuildPreview(const FAssetRegistry& Registry, FLoadedMeshAsset& Out, FString& Error) const
{
	FLoadedMeshAsset Preview;
	Preview.Source = Reference;
	Preview.Data = Draft;
	Preview.DisplayMesh = Draft.Mesh;
	Preview.AdditionalSlots.resize(Draft.AdditionalMaterials.size());
	for (int I = 0; I < Draft.GetNumMaterials(); ++I)
	{
		FLoadedMaterialSlot& Slot =
		    I == 0 ? static_cast<FLoadedMaterialSlot&>(Preview) : Preview.AdditionalSlots[I - 1];
		if (!LoadSlot(Draft.GetMaterial(I), Registry, Slot, Error))
			return false;
	}
	Out = std::move(Preview);
	Error.Empty();
	return true;
}
bool FMeshAssetEditSession::Reimport(const FString& File, FString& Error)
{
	FStaticMeshAsset Imported;
	if (!FMapAssetImport::ReadObj(File, Imported, Error))
		return false;
	FValidationContext Validation(Error);

	FStaticMeshAsset Updated = Draft;
	Updated.MaterialSlotNames.resize(Draft.GetNumMaterials());
	TArray<uint32> Remap;
	for (int I = 0; I < Imported.GetNumMaterials(); ++I)
	{
		const FString Name =
		    I < static_cast<int>(Imported.MaterialSlotNames.size()) ? Imported.MaterialSlotNames[I] : FString();
		int Match = -1;
		if (!Name.IsEmpty())
			for (size_t J = 0; J < Updated.MaterialSlotNames.size(); ++J)
				if (Updated.MaterialSlotNames[J] == Name)
				{
					if (Match != -1)
					{
						Validation.Fail(GENESIS_TEXT("Duplicate existing slot names prevent safe reimport."));
						return false;
					}
					Match = static_cast<int>(J);
				}
		// Unnamed meshes can adopt the incoming groups on their first reimport.
		if (Match < 0 && I < Draft.GetNumMaterials() && Updated.MaterialSlotNames[I].IsEmpty())
		{
			Match = I;
			Updated.MaterialSlotNames[I] = Name;
		}
		if (Match < 0)
		{
			if (Updated.GetNumMaterials() >= 64)
			{
				Validation.Fail(GENESIS_TEXT("Reimport would exceed 64 retained material slots."));
				return false;
			}
			Match = Updated.GetNumMaterials();
			Updated.AdditionalMaterials.push_back({});
			Updated.MaterialSlotNames.push_back(Name);
		}
		Remap.push_back(static_cast<uint32>(Match));
	}
	const StaticMesh& M = *Imported.Mesh;
	TSharedPtr<StaticMesh> Geometry = std::make_shared<StaticMesh>(M.GetName());
	for (int I = 0; I < M.GetNumVertices(); ++I)
		Geometry->AddVertex(M.GetVertex(I));
	for (const StaticMesh::Triangle& T : M.GetTriangles())
	{
		Geometry->AddTriangle(T.v[0], T.v[1], T.v[2], T.norm[0], T.norm[1], T.norm[2]);
		const int I = static_cast<int>(Geometry->GetTriangles().size()) - 1;
		Geometry->SetTriangleUVs(I, T.uv);
		if (!Validation.Check(T.MaterialSlot < Remap.size(), GENESIS_TEXT("Invalid imported material slot.")))
		{
			return false;
		}
		Geometry->SetTriangleMaterial(I, Remap[T.MaterialSlot]);
	}
	for (const StaticMesh::Edge& E : M.GetEdgeList())
		Geometry->AddEdge(E.start, E.end);
	Geometry->SetVertexColors(M.GetVertexColors());
	Geometry->Finish();
	Updated.Mesh = std::move(Geometry);
	std::error_code PathError;
	const std::filesystem::path Full =
	    std::filesystem::absolute(std::filesystem::path(File.ToWide()), PathError).lexically_normal();
	if (!Validation.Check(!PathError, GENESIS_TEXT("Cannot resolve mesh source path.")))
	{
		return false;
	}
	Draft = std::move(Updated);
	SourceFile = FString::FromWide(Full.wstring());
	bDirty = true;
	Error.Empty();
	return true;
}
bool FMeshAssetEditSession::Save(const FAssetRegistry& Registry, FString& Error)
{
	const FAssetData* Data = Registry.Resolve(Reference, Error, "Genesis.StaticMesh");
	if (!Data)
		return false;
	if (Data->FilePath != Target)
	{
		Error = GENESIS_TEXT("Mesh moved while editing; reopen it before saving.");
		return false;
	}
	FString Current;
	if (!FFile::ReadAllText(Target, Current) || Current != Manifest)
	{
		Error = GENESIS_TEXT("Mesh changed on disk; reopen it before saving this draft.");
		return false;
	}
	FLoadedMeshAsset Preview;
	if (!BuildPreview(Registry, Preview, Error))
		return false;
	FAssetPackage P;
	P.Id = Reference.Id;
	if (!FRenderAssetCodec::EncodeMesh(Draft, P, Error))
		return false;
	if (!SourceFile.IsEmpty())
		FMapAssetImport::SetMeshImportSource(P, Target, SourceFile);
	if (!FAssetPackageStore::Save(Target, P, Error))
		return false;
	if (!FFile::ReadAllText(Target, Manifest))
	{
		Error = GENESIS_TEXT("Mesh saved, but its manifest could not be reread; reopen it.");
		return false;
	}
	bDirty = false;
	Error.Empty();
	return true;
}
