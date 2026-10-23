// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Assets/RenderAssets.h"
#include "Misc/ValidationContext.h"
#include "Templates/SharedPointer.h"
#include "Internationalization/TextLocalization.h"
#include "RenderAssetCodecPrivate.h"
#include "Geometry/StaticMesh.h"
#include <bit>
#include <cmath>

namespace
{
	using namespace RenderAssetCodecPrivate;
	using J = FJsonValue;
	void FloatOK(FValidationContext& Validation, float V)
	{
		if (!Validation.Check(
		        std::isfinite(V) && std::abs(V) <= 1.0e9f, GENESIS_TEXT("Invalid mesh coordinate or normal.")))
		{
			return;
		}
	}
	void Word(FValidationContext&, FAssetPackage& P, uint32 V)
	{
		for (int B = 0; B < 4; ++B)
			P.Bulk.push_back(static_cast<uint8>(V >> (B * 8)));
	}
	void Float(FValidationContext& Validation, FAssetPackage& P, float V)
	{
		FloatOK(Validation, V);
		if (!Validation.IsValid())
		{
			return;
		}

		Word(Validation, P, std::bit_cast<uint32>(V));
		if (!Validation.IsValid())
		{
			return;
		}
	}
	void Vector(FValidationContext& Validation, FAssetPackage& P, const Vector3d& V)
	{
		Float(Validation, P, V.GetX());
		if (!Validation.IsValid())
		{
			return;
		}

		Float(Validation, P, V.GetY());
		if (!Validation.IsValid())
		{
			return;
		}

		Float(Validation, P, V.GetZ());
		if (!Validation.IsValid())
		{
			return;
		}
	}
	struct FReader
	{
		const FAssetPackage& P;
		size_t Offset = 0;
		uint32 Word(FValidationContext& Validation)
		{
			if (!Validation.Check(
			        Offset <= P.Bulk.size() && P.Bulk.size() - Offset >= 4, GENESIS_TEXT("Truncated mesh bulk data.")))
			{
				return {};
			}

			uint32 V = 0;
			for (int B = 0; B < 4; ++B)
				V |= static_cast<uint32>(P.Bulk[Offset++]) << (B * 8);
			return V;
		}
		float Float(FValidationContext& Validation)
		{
			float const V = std::bit_cast<float>(Word(Validation));
			if (!Validation.IsValid())
			{
				return {};
			}

			FloatOK(Validation, V);
			if (!Validation.IsValid())
			{
				return {};
			}
			return V;
		}
		Vector3d Vector(FValidationContext& Validation)
		{
			const float X = Float(Validation), Y = Float(Validation), Z = Float(Validation);
			if (!Validation.IsValid())
			{
				return {};
			}
			return Vector3d(X, Y, Z);
		}
	};
	void Schema(FValidationContext& Validation, const FAssetPackage& P, const char* Type)
	{
		if (!Validation.Check(
		        P.Kind == EAssetPackageKind::Asset && P.Schema == Type && P.SchemaVersion == 1 && P.Exports.IsObject(),
		        GENESIS_TEXT("Unsupported render asset schema.")))
		{
			return;
		}
	}
	std::shared_ptr<const StaticMesh> Tint(const StaticMesh& Mesh, const FMaterialAsset& Material)
	{
		TSharedPtr<StaticMesh> Out = std::make_shared<StaticMesh>(Mesh.GetName());
		TArray<uint32> Colors;
		for (int I = 0; I < Mesh.GetNumVertices(); ++I)
		{
			Out->AddVertex(Mesh.GetVertex(I));
			uint32 Color = 0;
			const unsigned int Base = Mesh.HasVertexColors() ? Mesh.GetVertexColor(I) : 0xffffffffu;
			for (int C = 0; C < 4; ++C)
				Color |= static_cast<uint32>(((Base >> (C * 8)) & 255) * Material.BaseColor[C] + 0.5f) << (C * 8);
			Colors.push_back(Color);
		}
		for (const StaticMesh::Triangle& T : Mesh.GetTriangles())
		{
			Out->AddTriangle(T.v[0], T.v[1], T.v[2], T.norm[0], T.norm[1], T.norm[2]);
			Out->SetTriangleUVs(static_cast<int>(Out->GetTriangles().size()) - 1, T.uv);
			Out->SetTriangleMaterial(static_cast<int>(Out->GetTriangles().size()) - 1, T.MaterialSlot);
		}
		for (const StaticMesh::Edge& E : Mesh.GetEdgeList())
			Out->AddEdge(E.start, E.end);
		Out->SetVertexColors(std::move(Colors));
		Out->Finish();
		return Out;
	}
} // namespace

bool FRenderAssetCodec::EncodeTexture(const FTextureAsset& In, FAssetPackage& Out, FString& Error)
{
	FValidationContext Validation(Error);

	if (!Validation.Check(In.Width > 0 && In.Height > 0 && In.Width <= 8192 && In.Height <= 8192 &&
	            In.Pixels.size() == static_cast<size_t>(In.Width) * In.Height * 4,
	        GENESIS_TEXT("Invalid RGBA8 texture dimensions or byte count.")))
	{
		return false;
	}

	FAssetPackage P = Package(Out, "Genesis.Texture2D");
	P.Exports["Width"] = In.Width;
	P.Exports["Height"] = In.Height;
	P.Exports["SRGB"] = In.bSRGB;
	P.Exports["Format"] = "RGBA8";
	P.Bulk = In.Pixels;
	Out = std::move(P);
	Error.Empty();
	return true;
}
bool FRenderAssetCodec::DecodeTexture(const FAssetPackage& P, FTextureAsset& Out, FString& Error)
{
	FValidationContext Validation(Error);

	Schema(Validation, P, "Genesis.Texture2D");
	if (!Validation.IsValid())
	{
		return false;
	}

	FTextureAsset T;
	T.Width = Count(Validation, P.Exports.Get("Width"), 8192);
	if (!Validation.IsValid())
	{
		return false;
	}

	T.Height = Count(Validation, P.Exports.Get("Height"), 8192);
	if (!Validation.IsValid())
	{
		return false;
	}

	if (!Validation.Check(T.Width > 0 && T.Height > 0 && P.Exports.Get("Format").AsString() == "RGBA8" &&
	            P.Exports.Get("SRGB").GetType() == EJsonType::Bool &&
	            P.Bulk.size() == static_cast<size_t>(T.Width) * T.Height * 4,
	        GENESIS_TEXT("Invalid RGBA8 texture payload.")))
	{
		return false;
	}

	T.bSRGB = P.Exports.Get("SRGB").AsBool();
	T.Pixels = P.Bulk;
	Out = std::move(T);
	Error.Empty();
	return true;
}
bool FRenderAssetCodec::EncodeMesh(const FStaticMeshAsset& In, FAssetPackage& Out, FString& Error)
{
	FValidationContext Validation(Error);

	if (!Validation.Check(In.Mesh != nullptr, GENESIS_TEXT("Missing mesh.")))
	{
		return false;
	}

	const StaticMesh& M = *In.Mesh;
	const int V = M.GetNumVertices();
	if (!Validation.Check(V > 0 && V <= 1000000 && !M.GetTriangles().empty() && M.GetTriangles().size() <= 2000000 &&
	            M.GetEdgeList().size() <= 3000000,
	        GENESIS_TEXT("Mesh exceeds topology limits or is empty.")))
	{
		return false;
	}

	if (!Validation.Check(M.GetVertexColors().empty() || M.GetVertexColors().size() == static_cast<size_t>(V),
	        GENESIS_TEXT("Invalid mesh vertex color count.")))
	{
		return false;
	}

	FAssetPackage P = Package(Out, "Genesis.StaticMesh");
	P.SchemaVersion = 2;
	if (!Validation.Check(In.GetNumMaterials() <= 64, GENESIS_TEXT("Mesh supports at most 64 material slots.")))
	{
		return false;
	}

	P.Exports["Name"] = FString::FromAnsi(M.GetName());
	P.Exports["Vertices"] = V;
	P.Exports["Triangles"] = static_cast<int>(M.GetTriangles().size());
	P.Exports["Edges"] = static_cast<int>(M.GetEdgeList().size());
	P.Exports["Colors"] = M.HasVertexColors();
	if (!Validation.Check(
	        In.MaterialSlotNames.empty() || In.MaterialSlotNames.size() == static_cast<size_t>(In.GetNumMaterials()),
	        GENESIS_TEXT("Invalid material slot names.")))
	{
		return false;
	}

	P.Exports["MaterialSlotNames"] = J::MakeArray();
	for (const FString& Name : In.MaterialSlotNames)
	{
		if (!Validation.Check(Name.Len() <= 256 && Name.View().find('\0') == std::string_view::npos,
		        GENESIS_TEXT("Invalid material slot name.")))
		{
			return false;
		}

		P.Exports["MaterialSlotNames"].Push(Name);
	}
	P.Exports["Materials"] = J::MakeArray();
	for (int Slot = 0; Slot < In.GetNumMaterials(); ++Slot)
	{
		const FAssetReference& Ref = In.GetMaterial(Slot);
		P.Exports["Materials"].Push(Ref.ToJson());
		Dependency(Validation, P, Ref);
		if (!Validation.IsValid())
		{
			return false;
		}
	}
	for (int I = 0; I < V; ++I)
	{
		Vector(Validation, P, M.GetVertex(I));
		if (!Validation.IsValid())
		{
			return {};
		}
	}
	if (!Validation.IsValid())
	{
		return false;
	}

	for (const StaticMesh::Triangle& T : M.GetTriangles())
	{
		for (int I : T.v)
		{
			if (!Validation.Check(I >= 0 && I < V, GENESIS_TEXT("Invalid mesh index.")))
			{
				return false;
			}

			Word(Validation, P, I);
			if (!Validation.IsValid())
			{
				return false;
			}
		}
		for (const Vector3d& N : T.norm)
		{
			const float Len = N.GetX() * N.GetX() + N.GetY() * N.GetY() + N.GetZ() * N.GetZ();
			if (!Validation.Check(std::isfinite(Len) && Len > 0.5f && Len < 1.5f,
			        GENESIS_TEXT("Mesh normals must have unit length.")))
			{
				return false;
			}

			Vector(Validation, P, N);
			if (!Validation.IsValid())
			{
				return false;
			}
		}
		for (const float (&UV)[2] : T.uv)
			for (float Value : UV)
			{
				Float(Validation, P, Value);
				if (!Validation.IsValid())
				{
					return {};
				}
			}
		if (!Validation.IsValid())
		{
			return false;
		}

		if (!Validation.Check(T.MaterialSlot < static_cast<uint32>(In.GetNumMaterials()),
		        GENESIS_TEXT("Triangle material slot is outside the mesh material list.")))
		{
			return false;
		}

		Word(Validation, P, T.MaterialSlot);
		if (!Validation.IsValid())
		{
			return false;
		}
	}
	for (const StaticMesh::Edge& E : M.GetEdgeList())
	{
		if (!Validation.Check(
		        E.start >= 0 && E.start < V && E.end >= 0 && E.end < V, GENESIS_TEXT("Invalid mesh edge.")))
		{
			return false;
		}

		Word(Validation, P, E.start);
		if (!Validation.IsValid())
		{
			return false;
		}

		Word(Validation, P, E.end);
		if (!Validation.IsValid())
		{
			return false;
		}
	}
	for (unsigned int Color : M.GetVertexColors())
	{
		Word(Validation, P, Color);
		if (!Validation.IsValid())
		{
			return {};
		}
	}
	if (!Validation.IsValid())
	{
		return false;
	}

	Out = std::move(P);
	Error.Empty();
	return true;
}
bool FRenderAssetCodec::DecodeMesh(const FAssetPackage& P, FStaticMeshAsset& Out, FString& Error)
{
	FValidationContext Validation(Error);

	if (!Validation.Check(P.Kind == EAssetPackageKind::Asset && P.Schema == "Genesis.StaticMesh" &&
	            (P.SchemaVersion == 1 || P.SchemaVersion == 2) && P.Exports.IsObject(),
	        GENESIS_TEXT("Unsupported mesh schema.")))
	{
		return false;
	}

	const FJsonValue& E = P.Exports;
	const int V = Count(Validation, E.Get("Vertices"), 1000000), T = Count(Validation, E.Get("Triangles"), 2000000),
	          Edges = Count(Validation, E.Get("Edges"), 3000000);
	if (!Validation.IsValid())
	{
		return false;
	}

	if (!Validation.Check(V > 0 && T > 0 && E.Get("Colors").GetType() == EJsonType::Bool,
	        GENESIS_TEXT("Empty mesh or missing color flag.")))
	{
		return false;
	}

	const bool Colors = E.Get("Colors").AsBool();
	const uint64 Bytes = static_cast<uint64>(V) * 12 + static_cast<uint64>(T) * (P.SchemaVersion == 1 ? 72 : 76) +
	    static_cast<uint64>(Edges) * 8 + (Colors ? static_cast<uint64>(V) * 4 : 0);
	if (!Validation.Check(Bytes == P.Bulk.size(), GENESIS_TEXT("Invalid mesh bulk byte count.")))
	{
		return false;
	}

	const FString Name = E.Get("Name").AsString();
	if (!Validation.Check(E.Get("Name").GetType() == EJsonType::String && Name.Len() <= 65536 &&
	            Name.View().find('\0') == std::string_view::npos,
	        GENESIS_TEXT("Invalid mesh name.")))
	{
		return false;
	}

	FStaticMeshAsset Asset;
	if (P.SchemaVersion == 1)
		Asset.Material = Reference(Validation, E.Get("Material"));
	else
	{
		const FJsonValue& Slots = E.Get("Materials");
		if (!Validation.Check(Slots.IsArray() && Slots.Num() >= 1 && Slots.Num() <= 64,
		        GENESIS_TEXT("Invalid mesh material slot count.")))
		{
			return false;
		}

		Asset.Material = Reference(Validation, Slots[0]);
		if (!Validation.IsValid())
		{
			return false;
		}

		for (int I = 1; I < Slots.Num(); ++I)
		{
			Asset.AdditionalMaterials.push_back(Reference(Validation, Slots[I]));
			if (!Validation.IsValid())
			{
				return {};
			}
		}
		if (!Validation.IsValid())
		{
			return false;
		}
	}
	if (!Validation.IsValid())
	{
		return false;
	}

	const FJsonValue& Names = E.Get("MaterialSlotNames");
	if (!Validation.Check(
	        Names.IsNull() || (Names.IsArray() && (Names.Num() == 0 || Names.Num() == Asset.GetNumMaterials())),
	        GENESIS_TEXT("Invalid material slot names.")))
	{
		return false;
	}

	for (const FJsonValue& N : Names.GetItems())
	{
		const FString Text = N.AsString();
		if (!Validation.Check(N.GetType() == EJsonType::String && Text.Len() <= 256 &&
		            Text.View().find('\0') == std::string_view::npos,
		        GENESIS_TEXT("Invalid material slot name.")))
		{
			return false;
		}

		Asset.MaterialSlotNames.push_back(Text);
	}
	TSharedPtr<StaticMesh> M = std::make_shared<StaticMesh>(Name.ToAnsi().c_str());
	FReader R{P};
	for (int I = 0; I < V; ++I)
	{
		M->AddVertex(R.Vector(Validation));
		if (!Validation.IsValid())
		{
			return {};
		}
	}
	if (!Validation.IsValid())
	{
		return false;
	}

	for (int I = 0; I < T; ++I)
	{
		const uint32 A = R.Word(Validation), B = R.Word(Validation), C = R.Word(Validation);
		if (!Validation.IsValid())
		{
			return false;
		}

		if (!Validation.Check(A < static_cast<uint32>(V) && B < static_cast<uint32>(V) && C < static_cast<uint32>(V),
		        GENESIS_TEXT("Invalid mesh index.")))
		{
			return false;
		}

		Vector3d N[3];
		for (Vector3d& Normal : N)
		{
			Normal = R.Vector(Validation);
			if (!Validation.IsValid())
			{
				return false;
			}

			const float L =
			    Normal.GetX() * Normal.GetX() + Normal.GetY() * Normal.GetY() + Normal.GetZ() * Normal.GetZ();
			if (!Validation.Check(L > 0.5f && L < 1.5f, GENESIS_TEXT("Invalid mesh normal.")))
			{
				return false;
			}
		}
		M->AddTriangle(A, B, C, N[0], N[1], N[2]);
		float UV[3][2];
		for (float (&Pair)[2] : UV)
			for (float& Value : Pair)
			{
				Value = R.Float(Validation);
				if (!Validation.IsValid())
				{
					return {};
				}
			}
		if (!Validation.IsValid())
		{
			return false;
		}

		M->SetTriangleUVs(I, UV);
		const unsigned int Slot = P.SchemaVersion == 1 ? 0u : R.Word(Validation);
		if (!Validation.IsValid())
		{
			return false;
		}

		if (!Validation.Check(
		        Slot < static_cast<uint32>(Asset.GetNumMaterials()), GENESIS_TEXT("Invalid triangle material slot.")))
		{
			return false;
		}

		M->SetTriangleMaterial(I, Slot);
	}
	for (int I = 0; I < Edges; ++I)
	{
		const uint32 A = R.Word(Validation), B = R.Word(Validation);
		if (!Validation.IsValid())
		{
			return false;
		}

		if (!Validation.Check(
		        A < static_cast<uint32>(V) && B < static_cast<uint32>(V), GENESIS_TEXT("Invalid mesh edge.")))
		{
			return false;
		}

		M->AddEdge(A, B);
	}
	if (Colors)
	{
		TArray<uint32> C;
		C.reserve(V);
		for (int I = 0; I < V; ++I)
		{
			C.push_back(R.Word(Validation));
			if (!Validation.IsValid())
			{
				return {};
			}
		}
		if (!Validation.IsValid())
		{
			return false;
		}

		M->SetVertexColors(std::move(C));
	}
	M->Finish();
	Asset.Mesh = std::move(M);
	Out = std::move(Asset);
	Error.Empty();
	return true;
}

bool FRenderAssetLoader::LoadTexture(const FAssetReference& R, FTextureAsset& Out, FString& Error) const
{
	FAssetPackage P;
	return Registry.Load(R, P, Error, "Genesis.Texture2D") && FRenderAssetCodec::DecodeTexture(P, Out, Error);
}
bool FRenderAssetLoader::LoadMesh(const FAssetReference& R, FLoadedMeshAsset& Out, FString& Error,
    const FAssetReference& Override, const TArray<FAssetReference>& AdditionalOverrides) const
{
	FLoadedMeshAsset Asset;
	FAssetPackage P;
	if (!Registry.Load(R, P, Error, "Genesis.StaticMesh") || !FRenderAssetCodec::DecodeMesh(P, Asset.Data, Error))
		return false;
	const FAssetData* Metadata = Registry.FindById(P.Id);
	Asset.Source = {P.Id, Metadata ? Metadata->PackagePath : R.Path};
	Asset.MaterialOverride = Override;
	if (AdditionalOverrides.size() > Asset.Data.AdditionalMaterials.size())
	{
		Error = GENESIS_TEXT("Material override slot outside mesh.");
		return false;
	}
	Asset.AdditionalOverrides = AdditionalOverrides;
	Asset.AdditionalSlots.resize(Asset.Data.AdditionalMaterials.size());
	for (int I = 0; I < Asset.Data.GetNumMaterials(); ++I)
	{
		const FAssetReference Ref = I == 0
		    ? Override
		    : (I <= static_cast<int>(AdditionalOverrides.size()) ? AdditionalOverrides[I - 1] : FAssetReference{});
		if (!FAssetReference::FromJson(Ref.ToJson(), Error))
			return false;
		const FAssetReference& Material = Ref.IsNull() ? Asset.Data.GetMaterial(I) : Ref;
		FLoadedMaterialSlot& Slot = I == 0 ? static_cast<FLoadedMaterialSlot&>(Asset) : Asset.AdditionalSlots[I - 1];
		if (Material.IsNull())
			continue;
		FMaterialAsset M;
		if (!LoadMaterial(Material, M, Error))
			return false;
		if (!LoadMaterialResources(M, Slot, Error))
			return false;
	}
	Asset.DisplayMesh = Asset.Material ? Tint(*Asset.Data.Mesh, *Asset.Material) : Asset.Data.Mesh;
	Out = std::move(Asset);
	Error.Empty();
	return true;
}
