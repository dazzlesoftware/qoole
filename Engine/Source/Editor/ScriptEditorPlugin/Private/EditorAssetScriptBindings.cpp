// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "EditorAssetScriptBindings.h"
#include "Assets/RenderAssets.h"
#include "Geometry/StaticMesh.h"
#include <filesystem>
#include <cmath>
#include <stdexcept>
namespace
{
using J = FJsonValue;
void Require(bool B, const char* E)
{
	if (!B)
		throw std::runtime_error(E);
}
int Integer(const J& V, int Min, int Max)
{
	const auto N = V.AsNumber();
	Require(V.GetType() == EJsonType::Number && std::isfinite(N) && N >= Min && N <= Max &&
				N == std::floor(N),
			"Invalid asset integer.");
	return static_cast<int>(N);
}
bool Existing(const FString& Path, const char* Schema, FAssetPackage& P, FString& E)
{
	if (!std::filesystem::exists(std::filesystem::path(Path.ToWide())))
		return true;
	if (!FAssetPackageStore::Load(Path, P, E))
		return false;
	if (P.Schema != Schema)
	{
		E = "Cannot replace another asset schema.";
		return false;
	}
	return true;
}
bool Saved(const FString& Path, FAssetPackage& P, const std::weak_ptr<FEditorScriptContext>& Weak, J& R,
		   FString& E)
{
	auto C = Weak.lock();
	Require(C != nullptr, "Editor asset context has expired.");
	auto Registry = C->Assets.lock();
	Require(Registry != nullptr, "Editor asset registry has expired.");
	if (!FAssetPackageStore::Save(Path, P, E))
		return false;
	Registry->Scan();
	R = J::MakeObject();
	R["Id"] = P.Id;
	R["FilePath"] = Path;
	if (const auto* A = Registry->FindByFile(Path))
		R["Reference"] = A->GetReference().ToJson();
	return true;
}
} // namespace
bool RegisterEditorAssetScriptBindings(FScriptBindingRegistry& Registry,
									   std::weak_ptr<FEditorScriptContext> Weak, FString& Error)
{
	TArray<FScriptBinding> Batch;
	Batch.push_back(
		{"Assets.CreateBasicMesh",
		 "Editor.Files",
		 true,
		 {{"Path", EJsonType::String}, {"Shape", EJsonType::String}, {"Material", EJsonType::Object, false}},
		 EJsonType::Object,
		 [Weak](const J& A, J& R, FString& E) {
			 const auto Path = A.Get("Path").AsString();
			 FAssetPackage P;
			 if (!Existing(Path, "Genesis.StaticMesh", P, E))
				 return false;
			 FStaticMeshAsset Mesh;
			 Mesh.Mesh = StaticMesh::Find(*A.Get("Shape").AsString());
			 Require(Mesh.Mesh != nullptr, "Unknown built-in mesh shape.");
			 if (A.Contains("Material"))
			 {
				 auto Ref = FAssetReference::FromJson(A.Get("Material"), E);
				 if (!Ref)
					 return false;
				 Mesh.Material = *Ref;
			 }
			 if (!FRenderAssetCodec::EncodeMesh(Mesh, P, E))
				 return false;
			 return Saved(Path, P, Weak, R, E);
		 }});
	Batch.push_back(
		{"Assets.CreateTexture",
		 "Editor.Files",
		 true,
		 {{"Path", EJsonType::String},
		  {"Width", EJsonType::Number},
		  {"Height", EJsonType::Number},
		  {"Pixels", EJsonType::Array},
		  {"SRGB", EJsonType::Bool, false}},
		 EJsonType::Object,
		 [Weak](const J& A, J& R, FString& E) {
			 const auto Path = A.Get("Path").AsString();
			 FAssetPackage P;
			 if (!Existing(Path, "Genesis.Texture2D", P, E))
				 return false;
			 FTextureAsset T;
			 T.Width = Integer(A.Get("Width"), 1, 4096);
			 T.Height = Integer(A.Get("Height"), 1, 4096);
			 Require(
				 static_cast<uint64>(T.Width) * T.Height * 4 <= 65536,
				 "Texture creation transfers at most 65536 RGBA bytes; use file import for larger textures.");
			 Require(A.Get("Pixels").Num() == T.Width * T.Height * 4, "Texture pixel count mismatch.");
			 for (const auto& V : A.Get("Pixels").GetItems())
				 T.Pixels.push_back(static_cast<uint8>(Integer(V, 0, 255)));
			 T.bSRGB = A.Get("SRGB").AsBool(true);
			 if (!FRenderAssetCodec::EncodeTexture(T, P, E))
				 return false;
			 return Saved(Path, P, Weak, R, E);
		 }});
	return Registry.RegisterBatch(std::move(Batch), Error);
}
