// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "MaterialScriptBindings.h"
#include "Templates/SharedPointer.h"
#include "Internationalization/TextLocalization.h"
#include "MaterialEditingLibrary.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialParameterCollection.h"
#include <cmath>
#include <filesystem>
#include "Misc/ValidationContext.h"
namespace
{
	using J = FJsonValue;
	J Settings(const FMaterialAsset& M)
	{
		FJsonValue R = J::MakeObject();
		R["BaseColor"] = J::MakeArray();
		for (float V : M.BaseColor)
			R["BaseColor"].Push(V);
		R["Roughness"] = M.Roughness;
		R["Metallic"] = M.Metallic;
		R["BlendMode"] = M.BlendMode == EMaterialBlendMode::Opaque ? "Opaque"
		    : M.BlendMode == EMaterialBlendMode::Masked            ? "Masked"
		                                                           : "Translucent";
		R["OpacityMaskClip"] = M.OpacityMaskClip;
		R["BaseColorTexture"] = M.BaseColorTexture.ToJson();
		R["NormalTexture"] = M.NormalTexture.ToJson();
		return R;
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
	bool Saved(const FString& Path, FAssetPackage& P, std::weak_ptr<FAssetRegistry> Assets, J& R, FString& E,
	    bool Dependents = false)
	{
		if (!FAssetPackageStore::Save(Path, P, E))
			return false;
		R = J::MakeObject();
		R["Id"] = P.Id;
		R["FilePath"] = Path;
		if (TSharedPtr<FAssetRegistry> Registry = Assets.lock())
		{
			Registry->Scan();
			if (const FAssetData* A = Registry->FindByFile(Path))
			{
				const FAssetReference Ref = A->GetReference();
				R["Reference"] = Ref.ToJson();
				if (Dependents)
				{
					int32 Count = 0;
					const bool Success = FMaterialEditingLibrary::RecompileDependents(*Registry, Ref, Count, E);
					R["UpdatedDependents"] = Count;
					R["DependencyError"] = Success ? FString() : E;
					E.Empty();
				}
			}
		}
		return true;
	}
	bool ApplySettings(const J& A, FMaterialAsset& M, FString& E)
	{
		FValidationContext Validation(E);
		if (!A.IsObject())
		{
			E = GENESIS_TEXT("Material settings must be an object.");
			return false;
		}
		J Candidate = Settings(M);
		for (const FString& Key : A.GetKeys())
		{
			if (!Candidate.Contains(Key.View()))
			{
				E = GENESIS_TEXT("Unknown material setting: ") + Key;
				return false;
			}
			Candidate[Key.View()] = A.Get(Key.View());
		}
		const FJsonValue& Color = Candidate.Get("BaseColor");
		if (!Validation.Check(
		        Color.IsArray() && Color.Num() == 4, GENESIS_TEXT("Material BaseColor must contain four numbers.")))
		{
			return false;
		}

		for (int I = 0; I < 4; ++I)
		{
			if (!Validation.Check(
			        Color[I].GetType() == EJsonType::Number, GENESIS_TEXT("Material BaseColor must contain numbers.")))
			{
				return false;
			}

			M.BaseColor[I] = Color[I].AsFloat();
		}
		for (const char* Key : {"Roughness", "Metallic", "OpacityMaskClip"})
		{
			if (!Validation.Check(Candidate.Get(Key).GetType() == EJsonType::Number,
			        GENESIS_TEXT("Material setting must be a number.")))
			{
				return false;
			}
		}
		M.Roughness = Candidate.Get("Roughness").AsFloat();
		M.Metallic = Candidate.Get("Metallic").AsFloat();
		M.OpacityMaskClip = Candidate.Get("OpacityMaskClip").AsFloat();
		const FString Blend = Candidate.Get("BlendMode").AsString();
		if (!Validation.Check(
		        Blend == "Opaque" || Blend == "Masked" || Blend == "Translucent", GENESIS_TEXT("Unknown blend mode.")))
		{
			return false;
		}

		M.BlendMode = Blend == "Opaque" ? EMaterialBlendMode::Opaque
		    : Blend == "Masked"         ? EMaterialBlendMode::Masked
		                                : EMaterialBlendMode::Translucent;
		TOptional<FAssetReference> ColorRef = FAssetReference::FromJson(Candidate.Get("BaseColorTexture"), E);
		if (!ColorRef)
			return false;
		TOptional<FAssetReference> NormalRef = FAssetReference::FromJson(Candidate.Get("NormalTexture"), E);
		if (!NormalRef)
			return false;
		M.BaseColorTexture = *ColorRef;
		M.NormalTexture = *NormalRef;
		FAssetPackage ValidatedPackage;
		return FRenderAssetCodec::EncodeMaterial(M, ValidatedPackage, E);
	}
} // namespace
bool RegisterMaterialScriptBindings(
    FScriptBindingRegistry& Registry, FString& Error, std::weak_ptr<FAssetRegistry> Assets)
{
	TArray<FScriptBinding> Batch;
	Batch.push_back(
	    {"Material.Describe", "Material.Read", true, {}, EJsonType::Object, [](const J&, J& R, FString&) -> bool
	{
		R = J::MakeObject();
		R["Expressions"] = J::MakeArray();
		R["Outputs"] = J::MakeArray();
		for (int I = 0; I < static_cast<int>(EMaterialExpression::Count); ++I)
		{
			const EMaterialExpression Type = static_cast<EMaterialExpression>(I);
			FJsonValue V = J::MakeObject();
			V["Id"] = I;
			V["Name"] = MaterialExpressionName(Type);
			V["Inputs"] = J::MakeArray();
			for (int Input = 0; Input < MaterialInputCount(Type); ++Input)
				V["Inputs"].Push(MaterialInputName(Type, Input));
			R["Expressions"].Push(std::move(V));
		}
		for (int I = 0; I < static_cast<int>(EMaterialOutput::Count); ++I)
		{
			FJsonValue V = J::MakeObject();
			V["Id"] = I;
			V["Name"] = MaterialOutputName(static_cast<EMaterialOutput>(I));
			R["Outputs"].Push(std::move(V));
		}
		return true;
	}});
	Batch.push_back({"Material.CreateGraph", "Material.Read", true, {{"Settings", EJsonType::Object, false}},
	    EJsonType::Object, [](const J& A, J& R, FString& E) -> bool
	{
		FMaterialAsset M;
		if (A.Contains("Settings") && !ApplySettings(A.Get("Settings"), M, E))
			return false;
		R = J::MakeObject();
		R["Settings"] = Settings(M);
		R["Graph"] = FMaterialGraphCompiler::Encode(FMaterialEditingLibrary::CreateGraph(M));
		return true;
	}});
	Batch.push_back({"Material.Translate", "Material.Read", true, {{"Graph", EJsonType::Object}}, EJsonType::Object,
	    [](const J& A, J& R, FString& E) -> bool
	{
		FMaterialGraph G;
		FMaterialGraphProgram Program;
		if (!FMaterialGraphCompiler::Decode(A.Get("Graph"), G, E) || !FMaterialGraphCompiler::Compile(G, Program, E))
			return false;
		R = J::MakeObject();
		R["PixelShaderSource"] = Program.PixelShaderSource;
		R["Textures"] = J::MakeArray();
		R["Parameters"] = J::MakeArray();
		for (const FMaterialGraphTexture& T : Program.Textures)
		{
			FJsonValue V = J::MakeObject();
			V["Texture"] = T.Reference.ToJson();
			V["Normal"] = T.bNormal;
			V["ParameterName"] = T.ParameterName;
			R["Textures"].Push(std::move(V));
		}
		for (const FMaterialGraphParameter& P : Program.Parameters)
		{
			FJsonValue V = J::MakeObject();
			V["Name"] = P.Name;
			V["Width"] = P.Width;
			V["Value"] = J::MakeArray();
			for (float F : P.Value)
				V["Value"].Push(F);
			R["Parameters"].Push(std::move(V));
		}
		return true;
	}});
	Batch.push_back({"Material.LoadGraph", "Editor.Files", true, {{"Path", EJsonType::String}}, EJsonType::Object,
	    [](const J& A, J& R, FString& E) -> bool
	{
		FAssetPackage P;
		FMaterialAsset M;
		if (!FAssetPackageStore::Load(A.Get("Path").AsString(), P, E) || !FRenderAssetCodec::DecodeMaterial(P, M, E))
			return false;
		R = J::MakeObject();
		R["Id"] = P.Id;
		R["Settings"] = Settings(M);
		R["Graph"] = FMaterialGraphCompiler::Encode(FMaterialEditingLibrary::CreateGraph(M));
		return true;
	}});
	Batch.push_back({"Material.CookGraph", "Editor.Files", true,
	    {{"Path", EJsonType::String}, {"Graph", EJsonType::Object}, {"Settings", EJsonType::Object, false}},
	    EJsonType::Object, [Assets](const J& A, J& R, FString& E) -> bool
	{
		const FString Path = A.Get("Path").AsString();
		FAssetPackage Package;
		FMaterialAsset M;
		std::error_code PathError;
		const bool Exists = std::filesystem::exists(std::filesystem::path(Path.ToWide()), PathError);
		if (PathError)
		{
			E = GENESIS_TEXT("Cannot inspect asset path.");
			return false;
		}
		if (Exists)
		{
			if (!FAssetPackageStore::Load(Path, Package, E) || !FRenderAssetCodec::DecodeMaterial(Package, M, E))
				return false;
		}
		if (A.Contains("Settings") && !ApplySettings(A.Get("Settings"), M, E))
			return false;
		FMaterialGraph G;
		if (!FMaterialGraphCompiler::Decode(A.Get("Graph"), G, E, false))
			return false;
		if (TSharedPtr<FAssetRegistry> Registry = Assets.lock())
		{
			bool Changed = false;
			if (!FMaterialFunctionCodec::Refresh(G, *Registry, Changed, E, Package.Id))
				return false;
		}
		M.Graph = std::move(G);
		if (!FMaterialEditingLibrary::CompileMaterial(M, E) || !FRenderAssetCodec::EncodeMaterial(M, Package, E) ||
		    !Saved(Path, Package, Assets, R, E, true))
			return false;
		R["DXILBytes"] = static_cast<int>(M.GraphDXIL.size());
		R["SPIRVBytes"] = static_cast<int>(M.GraphSPIRV.size());
		return true;
	}});
	Batch.push_back({"Material.CreateFunctionGraph", "Material.Read", true, {}, EJsonType::Object,
	    [](const J&, J& R, FString&) -> bool
	{
		FMaterialGraph G;
		G.bFunction = true;
		G.Outputs[0] = G.Add(EMaterialExpression::FunctionInput);
		R = FMaterialGraphCompiler::Encode(G);
		return true;
	}});
	Batch.push_back({"Material.LoadFunction", "Editor.Files", true, {{"Path", EJsonType::String}}, EJsonType::Object,
	    [](const J& A, J& R, FString& E) -> bool
	{
		FAssetPackage P;
		FMaterialGraph G;
		if (!FAssetPackageStore::Load(A.Get("Path").AsString(), P, E) || !FMaterialFunctionCodec::Decode(P, G, E))
			return false;
		R = J::MakeObject();
		R["Id"] = P.Id;
		R["Graph"] = FMaterialGraphCompiler::Encode(G);
		return true;
	}});
	Batch.push_back(
	    {"Material.SaveFunction", "Editor.Files", true, {{"Path", EJsonType::String}, {"Graph", EJsonType::Object}},
	        EJsonType::Object, [Assets](const J& A, J& R, FString& E) -> bool
	{
		const FString Path = A.Get("Path").AsString();
		FAssetPackage P;
		FMaterialGraph G;
		if (!Existing(Path, "Genesis.MaterialFunction", P, E) ||
		    !FMaterialGraphCompiler::Decode(A.Get("Graph"), G, E, false))
			return false;
		if (TSharedPtr<FAssetRegistry> Registry = Assets.lock())
		{
			bool Changed = false;
			if (!FMaterialFunctionCodec::Refresh(G, *Registry, Changed, E, P.Id))
				return false;
		}
		if (!FMaterialFunctionCodec::Encode(G, P, E))
			return false;
		return Saved(Path, P, Assets, R, E, true);
	}});
	Batch.push_back({"Material.LoadCollection", "Editor.Files", true, {{"Path", EJsonType::String}}, EJsonType::Object,
	    [](const J& A, J& R, FString& E) -> bool
	{
		FAssetPackage P;
		FMaterialParameterCollection C;
		if (!FAssetPackageStore::Load(A.Get("Path").AsString(), P, E) ||
		    !FMaterialParameterCollectionCodec::Decode(P, C, E))
			return false;
		R = J::MakeObject();
		R["Id"] = P.Id;
		R["Parameters"] = P.Exports.Get("Parameters");
		return true;
	}});
	Batch.push_back({"Material.SaveCollection", "Editor.Files", true,
	    {{"Path", EJsonType::String}, {"Parameters", EJsonType::Array}}, EJsonType::Object,
	    [Assets](const J& A, J& R, FString& E) -> bool
	{
		const FString Path = A.Get("Path").AsString();
		FAssetPackage P;
		if (!Existing(Path, "Genesis.MaterialParameterCollection", P, E))
			return false;
		FAssetPackage Candidate = P;
		Candidate.Schema = "Genesis.MaterialParameterCollection";
		Candidate.SchemaVersion = 1;
		Candidate.Exports = J::MakeObject();
		Candidate.Exports["Parameters"] = A.Get("Parameters");
		Candidate.Bulk.clear();
		FMaterialParameterCollection C;
		if (!FMaterialParameterCollectionCodec::Decode(Candidate, C, E) ||
		    !FMaterialParameterCollectionCodec::Encode(C, P, E))
			return false;
		return Saved(Path, P, Assets, R, E);
	}});
	Batch.push_back({"Material.LoadInstance", "Editor.Files", true, {{"Path", EJsonType::String}}, EJsonType::Object,
	    [](const J& A, J& R, FString& E) -> bool
	{
		FAssetPackage P;
		FMaterialInstanceAsset M;
		if (!FAssetPackageStore::Load(A.Get("Path").AsString(), P, E) ||
		    !FRenderAssetCodec::DecodeMaterialInstance(P, M, E))
			return false;
		R = J::MakeObject();
		R["Id"] = P.Id;
		R["Parent"] = M.Parent.ToJson();
		R["Parameters"] = P.Exports.Get("Parameters");
		R["Textures"] = P.Exports.Get("Textures");
		R["StaticParameters"] = J::MakeArray();
		for (const FMaterialStaticParameterOverride& S : M.StaticParameters)
		{
			FJsonValue V = J::MakeObject();
			V["Name"] = S.Name;
			V["Value"] = S.Value;
			R["StaticParameters"].Push(std::move(V));
		}
		R["OverrideQuality"] = M.bOverrideQuality;
		R["QualityLevel"] = M.QualityLevel;
		return true;
	}});
	Batch.push_back({"Material.SaveInstance", "Editor.Files", true,
	    {{"Path", EJsonType::String}, {"Parent", EJsonType::Object}, {"Parameters", EJsonType::Array, false},
	        {"Textures", EJsonType::Array, false}, {"StaticParameters", EJsonType::Array, false},
	        {"QualityLevel", EJsonType::Number, false}},
	    EJsonType::Object, [Assets](const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		TSharedPtr<FAssetRegistry> Registry = Assets.lock();
		if (!Validation.Check(Registry != nullptr, GENESIS_TEXT("Instance authoring requires an asset registry.")))
		{
			return false;
		}

		const FString Path = A.Get("Path").AsString();
		FAssetPackage P;
		if (!Existing(Path, "Genesis.MaterialInstance", P, E))
			return false;
		FAssetPackage Draft;
		Draft.Schema = "Genesis.MaterialInstance";
		Draft.SchemaVersion = 2;
		Draft.Exports["Parent"] = A.Get("Parent");
		Draft.Exports["Parameters"] = A.Contains("Parameters") ? A.Get("Parameters") : J::MakeArray();
		Draft.Exports["Textures"] = A.Contains("Textures") ? A.Get("Textures") : J::MakeArray();
		FMaterialInstanceAsset M;
		if (!FRenderAssetCodec::DecodeMaterialInstance(Draft, M, E))
			return false;
		if (A.Contains("StaticParameters"))
		{
			if (!Validation.Check(A.Get("StaticParameters").Num() <= 256, GENESIS_TEXT("Too many static overrides.")))
			{
				return false;
			}

			for (const FJsonValue& S : A.Get("StaticParameters").GetItems())
			{
				if (!Validation.Check(
				        S.Get("Name").GetType() == EJsonType::String && S.Get("Value").GetType() == EJsonType::Bool,
				        GENESIS_TEXT("Invalid static override.")))
				{
					return false;
				}

				M.StaticParameters.push_back({S.Get("Name").AsString(), S.Get("Value").AsBool()});
			}
		}
		if (A.Contains("QualityLevel"))
		{
			const double Q = A.Get("QualityLevel").AsNumber();
			if (!Validation.Check(
			        std::isfinite(Q) && Q >= 0 && Q <= 2 && std::floor(Q) == Q, GENESIS_TEXT("Invalid quality level.")))
			{
				return false;
			}

			M.bOverrideQuality = true;
			M.QualityLevel = static_cast<uint8>(Q);
		}
		FMaterialAsset Parent, Resolved;
		if (!FRenderAssetLoader(*Registry).LoadMaterial(M.Parent, Parent, E) ||
		    !FMaterialEditingLibrary::CompileMaterialInstance(Parent, M, E) ||
		    !FMaterialInstance::Resolve(Parent, M, Resolved, E) || !FRenderAssetCodec::EncodeMaterialInstance(M, P, E))
			return false;
		return Saved(Path, P, Assets, R, E, true);
	}});
	return Registry.RegisterBatch(std::move(Batch), Error);
}
