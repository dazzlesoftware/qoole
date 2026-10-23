// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Assets/RenderAssets.h"
#include "RenderAssetCodecPrivate.h"
#include "Geometry/StaticMesh.h"
#include <bit>
#include <cmath>
#include <stdexcept>

namespace
{
using namespace RenderAssetCodecPrivate;
using J = FJsonValue;
void FloatOK(float V)
{
	Need(std::isfinite(V) && std::abs(V) <= 1.0e9f, "Invalid mesh coordinate or normal.");
}
void Word(FAssetPackage& P, uint32 V)
{
	for (int B = 0; B < 4; ++B)
		P.Bulk.push_back(static_cast<uint8>(V >> (B * 8)));
}
void Float(FAssetPackage& P, float V)
{
	FloatOK(V);
	Word(P, std::bit_cast<uint32>(V));
}
void Vector(FAssetPackage& P, const Vector3d& V)
{
	Float(P, V.GetX());
	Float(P, V.GetY());
	Float(P, V.GetZ());
}
struct FReader
{
	const FAssetPackage& P;
	size_t Offset = 0;
	uint32 Word()
	{
		Need(Offset <= P.Bulk.size() && P.Bulk.size() - Offset >= 4, "Truncated mesh bulk data.");
		uint32 V = 0;
		for (int B = 0; B < 4; ++B)
			V |= static_cast<uint32>(P.Bulk[Offset++]) << (B * 8);
		return V;
	}
	float Float()
	{
		const auto V = std::bit_cast<float>(Word());
		FloatOK(V);
		return V;
	}
	Vector3d Vector()
	{
		const float X = Float(), Y = Float(), Z = Float();
		return Vector3d(X, Y, Z);
	}
};
void Schema(const FAssetPackage& P, const char* Type)
{
	Need(P.Kind == EAssetPackageKind::Asset && P.Schema == Type && P.SchemaVersion == 1 &&
			 P.Exports.IsObject(),
		 "Unsupported render asset schema.");
}
std::shared_ptr<const StaticMesh> Tint(const StaticMesh& Mesh, const FMaterialAsset& Material)
{
	auto Out = std::make_shared<StaticMesh>(Mesh.GetName());
	TArray<uint32> Colors;
	for (int I = 0; I < Mesh.GetNumVertices(); ++I)
	{
		Out->AddVertex(Mesh.GetVertex(I));
		uint32 Color = 0;
		const auto Base = Mesh.HasVertexColors() ? Mesh.GetVertexColor(I) : 0xffffffffu;
		for (int C = 0; C < 4; ++C)
			Color |= static_cast<uint32>(((Base >> (C * 8)) & 255) * Material.BaseColor[C] + 0.5f) << (C * 8);
		Colors.push_back(Color);
	}
	for (const auto& T : Mesh.GetTriangles())
	{
		Out->AddTriangle(T.v[0], T.v[1], T.v[2], T.norm[0], T.norm[1], T.norm[2]);
		Out->SetTriangleUVs(static_cast<int>(Out->GetTriangles().size()) - 1, T.uv);
		Out->SetTriangleMaterial(static_cast<int>(Out->GetTriangles().size()) - 1, T.MaterialSlot);
	}
	for (const auto& E : Mesh.GetEdgeList())
		Out->AddEdge(E.start, E.end);
	Out->SetVertexColors(std::move(Colors));
	Out->Finish();
	return Out;
}
} // namespace

bool FRenderAssetCodec::EncodeTexture(const FTextureAsset& In, FAssetPackage& Out, FString& Error)
{
	try
	{
		Need(In.Width > 0 && In.Height > 0 && In.Width <= 8192 && In.Height <= 8192 &&
				 In.Pixels.size() == static_cast<size_t>(In.Width) * In.Height * 4,
			 "Invalid RGBA8 texture dimensions or byte count.");
		auto P = Package(Out, "Genesis.Texture2D");
		P.Exports["Width"] = In.Width;
		P.Exports["Height"] = In.Height;
		P.Exports["SRGB"] = In.bSRGB;
		P.Exports["Format"] = "RGBA8";
		P.Bulk = In.Pixels;
		Out = std::move(P);
		Error.Empty();
		return true;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		return false;
	}
}
bool FRenderAssetCodec::DecodeTexture(const FAssetPackage& P, FTextureAsset& Out, FString& Error)
{
	try
	{
		Schema(P, "Genesis.Texture2D");
		FTextureAsset T;
		T.Width = Count(P.Exports.Get("Width"), 8192);
		T.Height = Count(P.Exports.Get("Height"), 8192);
		Need(T.Width > 0 && T.Height > 0 && P.Exports.Get("Format").AsString() == "RGBA8" &&
				 P.Exports.Get("SRGB").GetType() == EJsonType::Bool &&
				 P.Bulk.size() == static_cast<size_t>(T.Width) * T.Height * 4,
			 "Invalid RGBA8 texture payload.");
		T.bSRGB = P.Exports.Get("SRGB").AsBool();
		T.Pixels = P.Bulk;
		Out = std::move(T);
		Error.Empty();
		return true;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		return false;
	}
}
bool FRenderAssetCodec::EncodeMesh(const FStaticMeshAsset& In, FAssetPackage& Out, FString& Error)
{
	try
	{
		Need(In.Mesh != nullptr, "Missing mesh.");
		const auto& M = *In.Mesh;
		const int V = M.GetNumVertices();
		Need(V > 0 && V <= 1000000 && !M.GetTriangles().empty() && M.GetTriangles().size() <= 2000000 &&
				 M.GetEdgeList().size() <= 3000000,
			 "Mesh exceeds topology limits or is empty.");
		Need(M.GetVertexColors().empty() || M.GetVertexColors().size() == static_cast<size_t>(V),
			 "Invalid mesh vertex color count.");
		auto P = Package(Out, "Genesis.StaticMesh");
		P.SchemaVersion = 2;
		Need(In.GetNumMaterials() <= 64, "Mesh supports at most 64 material slots.");
		P.Exports["Name"] = FString::FromAnsi(M.GetName());
		P.Exports["Vertices"] = V;
		P.Exports["Triangles"] = static_cast<int>(M.GetTriangles().size());
		P.Exports["Edges"] = static_cast<int>(M.GetEdgeList().size());
		P.Exports["Colors"] = M.HasVertexColors();
		Need(In.MaterialSlotNames.empty() ||
				 In.MaterialSlotNames.size() == static_cast<size_t>(In.GetNumMaterials()),
			 "Invalid material slot names.");
		P.Exports["MaterialSlotNames"] = J::MakeArray();
		for (const auto& Name : In.MaterialSlotNames)
		{
			Need(Name.Len() <= 256 && Name.View().find('\0') == std::string_view::npos,
				 "Invalid material slot name.");
			P.Exports["MaterialSlotNames"].Push(Name);
		}
		P.Exports["Materials"] = J::MakeArray();
		for (int Slot = 0; Slot < In.GetNumMaterials(); ++Slot)
		{
			const auto& Ref = In.GetMaterial(Slot);
			P.Exports["Materials"].Push(Ref.ToJson());
			Dependency(P, Ref);
		}
		for (int I = 0; I < V; ++I)
			Vector(P, M.GetVertex(I));
		for (const auto& T : M.GetTriangles())
		{
			for (int I : T.v)
			{
				Need(I >= 0 && I < V, "Invalid mesh index.");
				Word(P, I);
			}
			for (const auto& N : T.norm)
			{
				const auto Len = N.GetX() * N.GetX() + N.GetY() * N.GetY() + N.GetZ() * N.GetZ();
				Need(std::isfinite(Len) && Len > 0.5f && Len < 1.5f, "Mesh normals must have unit length.");
				Vector(P, N);
			}
			for (const auto& UV : T.uv)
				for (float Value : UV)
					Float(P, Value);
			Need(T.MaterialSlot < static_cast<uint32>(In.GetNumMaterials()),
				 "Triangle material slot is outside the mesh material list.");
			Word(P, T.MaterialSlot);
		}
		for (const auto& E : M.GetEdgeList())
		{
			Need(E.start >= 0 && E.start < V && E.end >= 0 && E.end < V, "Invalid mesh edge.");
			Word(P, E.start);
			Word(P, E.end);
		}
		for (auto Color : M.GetVertexColors())
			Word(P, Color);
		Out = std::move(P);
		Error.Empty();
		return true;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		return false;
	}
}
bool FRenderAssetCodec::DecodeMesh(const FAssetPackage& P, FStaticMeshAsset& Out, FString& Error)
{
	try
	{
		Need(P.Kind == EAssetPackageKind::Asset && P.Schema == "Genesis.StaticMesh" &&
				 (P.SchemaVersion == 1 || P.SchemaVersion == 2) && P.Exports.IsObject(),
			 "Unsupported mesh schema.");
		const auto& E = P.Exports;
		const int V = Count(E.Get("Vertices"), 1000000), T = Count(E.Get("Triangles"), 2000000),
				  Edges = Count(E.Get("Edges"), 3000000);
		Need(V > 0 && T > 0 && E.Get("Colors").GetType() == EJsonType::Bool,
			 "Empty mesh or missing color flag.");
		const bool Colors = E.Get("Colors").AsBool();
		const uint64 Bytes = static_cast<uint64>(V) * 12 +
							 static_cast<uint64>(T) * (P.SchemaVersion == 1 ? 72 : 76) +
							 static_cast<uint64>(Edges) * 8 + (Colors ? static_cast<uint64>(V) * 4 : 0);
		Need(Bytes == P.Bulk.size(), "Invalid mesh bulk byte count.");
		const auto Name = E.Get("Name").AsString();
		Need(E.Get("Name").GetType() == EJsonType::String && Name.Len() <= 65536 &&
				 Name.View().find('\0') == std::string_view::npos,
			 "Invalid mesh name.");
		FStaticMeshAsset Asset;
		if (P.SchemaVersion == 1)
			Asset.Material = Reference(E.Get("Material"));
		else
		{
			const auto& Slots = E.Get("Materials");
			Need(Slots.IsArray() && Slots.Num() >= 1 && Slots.Num() <= 64,
				 "Invalid mesh material slot count.");
			Asset.Material = Reference(Slots[0]);
			for (int I = 1; I < Slots.Num(); ++I)
				Asset.AdditionalMaterials.push_back(Reference(Slots[I]));
		}
		const auto& Names = E.Get("MaterialSlotNames");
		Need(Names.IsNull() ||
				 (Names.IsArray() && (Names.Num() == 0 || Names.Num() == Asset.GetNumMaterials())),
			 "Invalid material slot names.");
		for (const auto& N : Names.GetItems())
		{
			const auto Text = N.AsString();
			Need(N.GetType() == EJsonType::String && Text.Len() <= 256 &&
					 Text.View().find('\0') == std::string_view::npos,
				 "Invalid material slot name.");
			Asset.MaterialSlotNames.push_back(Text);
		}
		auto M = std::make_shared<StaticMesh>(Name.ToAnsi().c_str());
		FReader R{P};
		for (int I = 0; I < V; ++I)
			M->AddVertex(R.Vector());
		for (int I = 0; I < T; ++I)
		{
			const auto A = R.Word(), B = R.Word(), C = R.Word();
			Need(A < static_cast<uint32>(V) && B < static_cast<uint32>(V) && C < static_cast<uint32>(V),
				 "Invalid mesh index.");
			Vector3d N[3];
			for (auto& Normal : N)
			{
				Normal = R.Vector();
				const auto L = Normal.GetX() * Normal.GetX() + Normal.GetY() * Normal.GetY() +
							   Normal.GetZ() * Normal.GetZ();
				Need(L > 0.5f && L < 1.5f, "Invalid mesh normal.");
			}
			M->AddTriangle(A, B, C, N[0], N[1], N[2]);
			float UV[3][2];
			for (auto& Pair : UV)
				for (auto& Value : Pair)
					Value = R.Float();
			M->SetTriangleUVs(I, UV);
			const auto Slot = P.SchemaVersion == 1 ? 0u : R.Word();
			Need(Slot < static_cast<uint32>(Asset.GetNumMaterials()), "Invalid triangle material slot.");
			M->SetTriangleMaterial(I, Slot);
		}
		for (int I = 0; I < Edges; ++I)
		{
			const auto A = R.Word(), B = R.Word();
			Need(A < static_cast<uint32>(V) && B < static_cast<uint32>(V), "Invalid mesh edge.");
			M->AddEdge(A, B);
		}
		if (Colors)
		{
			TArray<uint32> C;
			C.reserve(V);
			for (int I = 0; I < V; ++I)
				C.push_back(R.Word());
			M->SetVertexColors(std::move(C));
		}
		M->Finish();
		Asset.Mesh = std::move(M);
		Out = std::move(Asset);
		Error.Empty();
		return true;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		return false;
	}
}

bool FRenderAssetLoader::LoadTexture(const FAssetReference& R, FTextureAsset& Out, FString& Error) const
{
	FAssetPackage P;
	return Registry.Load(R, P, Error, "Genesis.Texture2D") && FRenderAssetCodec::DecodeTexture(P, Out, Error);
}
bool FRenderAssetLoader::LoadMesh(const FAssetReference& R, FLoadedMeshAsset& Out, FString& Error,
								  const FAssetReference& Override,
								  const TArray<FAssetReference>& AdditionalOverrides) const
{
	FLoadedMeshAsset Asset;
	FAssetPackage P;
	if (!Registry.Load(R, P, Error, "Genesis.StaticMesh") ||
		!FRenderAssetCodec::DecodeMesh(P, Asset.Data, Error))
		return false;
	const auto* Metadata = Registry.FindById(P.Id);
	Asset.Source = {P.Id, Metadata ? Metadata->PackagePath : R.Path};
	Asset.MaterialOverride = Override;
	if (AdditionalOverrides.size() > Asset.Data.AdditionalMaterials.size())
	{
		Error = "Material override slot outside mesh.";
		return false;
	}
	Asset.AdditionalOverrides = AdditionalOverrides;
	Asset.AdditionalSlots.resize(Asset.Data.AdditionalMaterials.size());
	for (int I = 0; I < Asset.Data.GetNumMaterials(); ++I)
	{
		const auto Ref = I == 0
							 ? Override
							 : (I <= static_cast<int>(AdditionalOverrides.size()) ? AdditionalOverrides[I - 1]
																				  : FAssetReference{});
		if (!FAssetReference::FromJson(Ref.ToJson(), Error))
			return false;
		const auto& Material = Ref.IsNull() ? Asset.Data.GetMaterial(I) : Ref;
		auto& Slot = I == 0 ? static_cast<FLoadedMaterialSlot&>(Asset) : Asset.AdditionalSlots[I - 1];
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
