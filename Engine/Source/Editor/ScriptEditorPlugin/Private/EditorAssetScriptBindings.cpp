// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "EditorAssetScriptBindings.h"
#include "Templates/SharedPointer.h"
#include "Internationalization/TextLocalization.h"
#include "Assets/RenderAssets.h"
#include "Geometry/StaticMesh.h"
#include <filesystem>
#include <cmath>
#include "Misc/ValidationContext.h"
namespace
{
	using J = FJsonValue;
	int Integer(FValidationContext& Validation, const J& V, int Min, int Max)
	{
		const double N = V.AsNumber();
		if (!Validation.Check(
		        V.GetType() == EJsonType::Number && std::isfinite(N) && N >= Min && N <= Max && N == std::floor(N),
		        GENESIS_TEXT("Invalid asset integer.")))
		{
			return {};
		}
		return static_cast<int>(N);
	}
	bool Existing(const FString& Path, const char* Schema, FAssetPackage& P, FString& E)
	{
		std::error_code PathError;
		const bool Exists = std::filesystem::exists(std::filesystem::path(Path.ToWide()), PathError);
		if (PathError)
		{
			E = GENESIS_TEXT("Cannot inspect asset path.");
			return false;
		}
		if (!Exists)
			return true;
		if (!FAssetPackageStore::Load(Path, P, E))
			return false;
		if (P.Schema != Schema)
		{
			E = GENESIS_TEXT("Cannot replace another asset schema.");
			return false;
		}
		return true;
	}
	bool Saved(const FString& Path, FAssetPackage& P, const std::weak_ptr<FEditorScriptContext>& Weak, J& R, FString& E)
	{
		FValidationContext Validation(E);
		TSharedPtr<FEditorScriptContext> C = Weak.lock();
		if (!Validation.Check(C != nullptr, GENESIS_TEXT("Editor asset context has expired.")))
		{
			return false;
		}

		TSharedPtr<FAssetRegistry> Registry = C->Assets.lock();
		if (!Validation.Check(Registry != nullptr, GENESIS_TEXT("Editor asset registry has expired.")))
		{
			return false;
		}

		if (!FAssetPackageStore::Save(Path, P, E))
			return false;
		Registry->Scan();
		R = J::MakeObject();
		R["Id"] = P.Id;
		R["FilePath"] = Path;
		if (const FAssetData* A = Registry->FindByFile(Path))
			R["Reference"] = A->GetReference().ToJson();
		return true;
	}
} // namespace
bool RegisterEditorAssetScriptBindings(
    FScriptBindingRegistry& Registry, std::weak_ptr<FEditorScriptContext> Weak, FString& Error)
{
	TArray<FScriptBinding> Batch;
	Batch.push_back({"Assets.CreateBasicMesh", "Editor.Files", true,
	    {{"Path", EJsonType::String}, {"Shape", EJsonType::String}, {"Material", EJsonType::Object, false}},
	    EJsonType::Object, [Weak](const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		const FString Path = A.Get("Path").AsString();
		FAssetPackage P;
		if (!Existing(Path, "Genesis.StaticMesh", P, E))
			return false;
		FStaticMeshAsset Mesh;
		Mesh.Mesh = StaticMesh::Find(*A.Get("Shape").AsString());
		if (!Validation.Check(Mesh.Mesh != nullptr, GENESIS_TEXT("Unknown built-in mesh shape.")))
		{
			return false;
		}

		if (A.Contains("Material"))
		{
			TOptional<FAssetReference> Ref = FAssetReference::FromJson(A.Get("Material"), E);
			if (!Ref)
				return false;
			Mesh.Material = *Ref;
		}
		if (!FRenderAssetCodec::EncodeMesh(Mesh, P, E))
			return false;
		return Saved(Path, P, Weak, R, E);
	}});
	Batch.push_back({"Assets.CreateTexture", "Editor.Files", true,
	    {{"Path", EJsonType::String}, {"Width", EJsonType::Number}, {"Height", EJsonType::Number},
	        {"Pixels", EJsonType::Array}, {"SRGB", EJsonType::Bool, false}},
	    EJsonType::Object, [Weak](const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		const FString Path = A.Get("Path").AsString();
		FAssetPackage P;
		if (!Existing(Path, "Genesis.Texture2D", P, E))
			return false;
		FTextureAsset T;
		T.Width = Integer(Validation, A.Get("Width"), 1, 4096);
		if (!Validation.IsValid())
		{
			return false;
		}
		T.Height = Integer(Validation, A.Get("Height"), 1, 4096);
		if (!Validation.IsValid())
		{
			return false;
		}
		if (!Validation.Check(static_cast<uint64>(T.Width) * T.Height * 4 <= 65536,
		        GENESIS_TEXT(
		            "Texture creation transfers at most 65536 RGBA bytes; use file import for larger textures.")))
		{
			return false;
		}

		if (!Validation.Check(
		        A.Get("Pixels").Num() == T.Width * T.Height * 4, GENESIS_TEXT("Texture pixel count mismatch.")))
		{
			return false;
		}

		for (const FJsonValue& V : A.Get("Pixels").GetItems())
		{
			T.Pixels.push_back(static_cast<uint8>(Integer(Validation, V, 0, 255)));
			if (!Validation.IsValid())
			{
				return false;
			}
		}
		if (!Validation.IsValid())
		{
			return false;
		}
		T.bSRGB = A.Get("SRGB").AsBool(true);
		if (!FRenderAssetCodec::EncodeTexture(T, P, E))
			return false;
		return Saved(Path, P, Weak, R, E);
	}});
	return Registry.RegisterBatch(std::move(Batch), Error);
}
