// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Materials/MaterialInstance.h"
#include "Misc/ValidationContext.h"
#include "Materials/MaterialLimits.h"
#include "Assets/RenderAssetCodecPrivate.h"
#include <set>
#include <functional>
using namespace RenderAssetCodecPrivate;
namespace
{
	void Validate(FValidationContext& Validation, const FMaterialInstanceAsset& M, bool RequireCook = true)
	{
		if (!Validation.Check(!M.Parent.IsNull(), GENESIS_TEXT("Material instance requires a parent.")))
		{
			return;
		}

		Reference(Validation, M.Parent.ToJson());
		if (!Validation.IsValid())
		{
			return;
		}

		if (!Validation.Check(
		        M.Parameters.size() + M.Textures.size() + M.StaticParameters.size() <= FMaterialGraphCompiler::MaxNodes,
		        GENESIS_TEXT("Too many instance overrides.")))
		{
			return;
		}

		std::set<FString> Names;
		for (const FMaterialParameterOverride& P : M.Parameters)
		{
			if (!Validation.Check(!P.Name.IsEmpty() && P.Name.Len() <= MaterialLimits::MaxParameterNameLength &&
			            Names.insert(P.Name).second,
			        GENESIS_TEXT("Invalid or duplicate parameter name.")))
			{
				return;
			}

			if (!Validation.Check(P.Width == 1 || P.Width == 4, GENESIS_TEXT("Invalid parameter width.")))
			{
				return;
			}

			for (float V : P.Value)
			{
				if (!Validation.Check(std::isfinite(V) && std::abs(V) <= MaterialLimits::MaxParameterMagnitude,
				        GENESIS_TEXT("Invalid parameter value.")))
				{
					return;
				}
			}
		}
		if (!Validation.Check(M.QualityLevel <= 2, GENESIS_TEXT("Invalid instance quality level.")))
		{
			return;
		}

		for (const FMaterialStaticParameterOverride& P : M.StaticParameters)
		{
			if (!Validation.Check(!P.Name.IsEmpty() && P.Name.Len() <= MaterialLimits::MaxParameterNameLength &&
			            Names.insert(P.Name).second,
			        GENESIS_TEXT("Invalid or duplicate static parameter name.")))
			{
				return;
			}
		}
		if (RequireCook && (!M.StaticParameters.empty() || M.bOverrideQuality))
		{
			if (!Validation.Check(M.StaticPermutation.has_value(),
			        GENESIS_TEXT("Cook the static instance permutation before saving or loading.")))
			{
				return;
			}
		}
		for (const FMaterialTextureParameterOverride& T : M.Textures)
		{
			if (!Validation.Check(!T.Name.IsEmpty() && T.Name.Len() <= MaterialLimits::MaxParameterNameLength &&
			            Names.insert(T.Name).second,
			        GENESIS_TEXT("Invalid or duplicate texture parameter name.")))
			{
				return;
			}

			if (!Validation.Check(!T.Texture.IsNull(), GENESIS_TEXT("Texture parameter requires a texture reference.")))
			{
				return;
			}

			Reference(Validation, T.Texture.ToJson());
			if (!Validation.IsValid())
			{
				return;
			}
		}
	}
	void VisitGraph(FValidationContext& Validation, FMaterialGraph& G, const std::function<void(FMaterialNode&)>& Visit,
	    int Depth = 0)
	{
		if (!Validation.Check(Depth < 16, GENESIS_TEXT("Instance function nesting exceeds 16.")))
		{
			return;
		}

		for (FMaterialNode& N : G.Nodes)
		{
			Visit(N);
			if (!Validation.IsValid())
			{
				return;
			}
		}
		if (!Validation.IsValid())
		{
			return;
		}

		for (FMaterialFunctionSnapshot& F : G.Functions)
		{
			if (!Validation.Check(F.Graph != nullptr, GENESIS_TEXT("Missing function snapshot.")))
			{
				return;
			}

			FMaterialGraph Next = *F.Graph;
			VisitGraph(Validation, Next, Visit, Depth + 1);
			if (!Validation.IsValid())
			{
				return;
			}

			F.Graph = std::make_shared<const FMaterialGraph>(std::move(Next));
		}
	}
} // namespace
bool FRenderAssetCodec::EncodeMaterialInstance(const FMaterialInstanceAsset& M, FAssetPackage& Out, FString& Error)
{
	FValidationContext Validation(Error);

	Validate(Validation, M);
	if (!Validation.IsValid())
	{
		return false;
	}

	FAssetPackage P = Package(Out, "Genesis.MaterialInstance");
	P.SchemaVersion = 2;
	P.Exports["Parent"] = M.Parent.ToJson();
	Dependency(Validation, P, M.Parent);
	if (!Validation.IsValid())
	{
		return false;
	}

	P.Exports["Parameters"] = J::MakeArray();
	for (const FMaterialParameterOverride& V : M.Parameters)
	{
		FJsonValue A = J::MakeObject();
		A["Name"] = V.Name;
		A["Width"] = V.Width;
		A["Value"] = J::MakeArray();
		for (float F : V.Value)
			A["Value"].Push(F);
		P.Exports["Parameters"].Push(A);
	}
	P.Exports["Textures"] = J::MakeArray();
	for (const FMaterialTextureParameterOverride& T : M.Textures)
	{
		FJsonValue V = J::MakeObject();
		V["Name"] = T.Name;
		V["Texture"] = T.Texture.ToJson();
		P.Exports["Textures"].Push(V);
		Dependency(Validation, P, T.Texture);
		if (!Validation.IsValid())
		{
			return false;
		}
	}
	if (!M.StaticParameters.empty() || M.bOverrideQuality)
	{
		FMaterialAsset Expected;
		if (!FMaterialInstance::BuildStaticPermutation(*M.StaticPermutation, M, Expected, Error))
			return false;
		FJsonValue ExpectedJson, ActualJson;
		if (!FMaterialGraphCompiler::Encode(*Expected.Graph, ExpectedJson, Error) ||
		    !FMaterialGraphCompiler::Encode(*M.StaticPermutation->Graph, ActualJson, Error))
		{
			return false;
		}
		if (!Validation.Check(
		        ExpectedJson == ActualJson, GENESIS_TEXT("Static permutation does not match its overrides.")))
		{
			return false;
		}

		FAssetPackage Cook;
		if (!FRenderAssetCodec::EncodeMaterial(*M.StaticPermutation, Cook, Error))
			return false;
		P.SchemaVersion = 3;
		P.Exports["StaticParameters"] = J::MakeArray();
		for (const FMaterialStaticParameterOverride& S : M.StaticParameters)
		{
			FJsonValue V = J::MakeObject();
			V["Name"] = S.Name;
			V["Value"] = S.Value;
			P.Exports["StaticParameters"].Push(std::move(V));
		}
		P.Exports["OverrideQuality"] = M.bOverrideQuality;
		P.Exports["QualityLevel"] = M.QualityLevel;
		P.Exports["Permutation"] = Cook.Exports;
		P.Bulk = std::move(Cook.Bulk);
		for (const FString& D : Cook.Dependencies)
			if (std::find(P.Dependencies.begin(), P.Dependencies.end(), D) == P.Dependencies.end())
				P.Dependencies.push_back(D);
	}
	Out = std::move(P);
	Error.Empty();
	return true;
}
bool FRenderAssetCodec::DecodeMaterialInstance(const FAssetPackage& P, FMaterialInstanceAsset& Out, FString& Error)
{
	FValidationContext Validation(Error);

	if (!Validation.Check(P.Kind == EAssetPackageKind::Asset && P.Schema == "Genesis.MaterialInstance" &&
	            (P.SchemaVersion >= 1 && P.SchemaVersion <= 3) && P.Exports.IsObject() &&
	            (P.SchemaVersion == 3 || P.Bulk.empty()),
	        GENESIS_TEXT("Unsupported material instance schema.")))
	{
		return false;
	}

	FMaterialInstanceAsset M;
	M.Parent = Reference(Validation, P.Exports.Get("Parent"));
	if (!Validation.IsValid())
	{
		return false;
	}

	const FJsonValue& Values = P.Exports.Get("Parameters");
	if (!Validation.Check(Values.IsArray() && Values.Num() <= FMaterialGraphCompiler::MaxNodes,
	        GENESIS_TEXT("Invalid instance parameters.")))
	{
		return false;
	}

	for (const FJsonValue& A : Values.GetItems())
	{
		if (!Validation.Check(A.IsObject() && A.Get("Name").GetType() == EJsonType::String,
		        GENESIS_TEXT("Invalid parameter override.")))
		{
			return false;
		}

		FMaterialParameterOverride V;
		V.Name = A.Get("Name").AsString();
		V.Width = static_cast<uint8>(Count(Validation, A.Get("Width"), 4));
		if (!Validation.IsValid())
		{
			return false;
		}

		const FJsonValue& Fs = A.Get("Value");
		if (!Validation.Check(Fs.IsArray() && Fs.Num() == 4, GENESIS_TEXT("Invalid parameter values.")))
		{
			return false;
		}

		for (int I = 0; I < 4; ++I)
		{
			V.Value[I] = Number(
			    Validation, Fs[I], -MaterialLimits::MaxParameterMagnitude, MaterialLimits::MaxParameterMagnitude);
			if (!Validation.IsValid())
			{
				return {};
			}
		}
		if (!Validation.IsValid())
		{
			return false;
		}

		M.Parameters.push_back(V);
	}
	if (P.SchemaVersion >= 2)
	{
		const FJsonValue& Ts = P.Exports.Get("Textures");
		if (!Validation.Check(Ts.IsArray() && Ts.Num() <= FMaterialGraphCompiler::MaxNodes,
		        GENESIS_TEXT("Invalid texture overrides.")))
		{
			return false;
		}

		for (const FJsonValue& T : Ts.GetItems())
		{
			if (!Validation.Check(T.IsObject() && T.Get("Name").GetType() == EJsonType::String,
			        GENESIS_TEXT("Invalid texture override.")))
			{
				return false;
			}

			M.Textures.push_back({T.Get("Name").AsString(), Reference(Validation, T.Get("Texture"))});
			if (!Validation.IsValid())
			{
				return false;
			}
		}
	}
	if (P.SchemaVersion == 3)
	{
		const FJsonValue& S = P.Exports.Get("StaticParameters");
		if (!Validation.Check(S.IsArray() && S.Num() <= 256, GENESIS_TEXT("Invalid static override list.")))
		{
			return false;
		}

		for (const FJsonValue& V : S.GetItems())
		{
			if (!Validation.Check(
			        V.Get("Name").GetType() == EJsonType::String && V.Get("Value").GetType() == EJsonType::Bool,
			        GENESIS_TEXT("Invalid static override.")))
			{
				return false;
			}

			M.StaticParameters.push_back({V.Get("Name").AsString(), V.Get("Value").AsBool()});
		}
		if (!Validation.Check(P.Exports.Get("OverrideQuality").GetType() == EJsonType::Bool,
		        GENESIS_TEXT("Invalid instance quality override.")))
		{
			return false;
		}

		M.bOverrideQuality = P.Exports.Get("OverrideQuality").AsBool();
		M.QualityLevel = static_cast<uint8>(Count(Validation, P.Exports.Get("QualityLevel"), 2));
		if (!Validation.IsValid())
		{
			return false;
		}

		FAssetPackage Cook;
		Cook.Schema = "Genesis.Material";
		Cook.SchemaVersion = 4;
		Cook.Exports = P.Exports.Get("Permutation");
		Cook.Bulk = P.Bulk;
		FMaterialAsset Material;
		if (!FRenderAssetCodec::DecodeMaterial(Cook, Material, Error))
			return false;
		M.StaticPermutation = std::move(Material);
	}
	Validate(Validation, M);
	if (!Validation.IsValid())
	{
		return false;
	}

	Out = std::move(M);
	Error.Empty();
	return true;
}
bool FMaterialInstance::Resolve(
    const FMaterialAsset& Parent, const FMaterialInstanceAsset& M, FMaterialAsset& Out, FString& Error)
{
	FValidationContext Validation(Error);

	Validate(Validation, M);
	if (!Validation.IsValid())
	{
		return false;
	}

	if (!Validation.Check(Parent.Graph.has_value(), GENESIS_TEXT("Instances require a graph material parent.")))
	{
		return false;
	}

	FMaterialGraphProgram Before;
	if (!FMaterialGraphCompiler::Compile(*Parent.Graph, Before, Error))
		return false;
	if (!Validation.Check(Before.PixelShaderSource == Parent.CookedGraphSource && !Parent.GraphDXIL.empty() &&
	            !Parent.GraphSPIRV.empty(),
	        GENESIS_TEXT("Material instance parent requires a valid cook.")))
	{
		return false;
	}

	FMaterialAsset Result;
	if (!BuildStaticPermutation(Parent, M, Result, Error))
		return false;
	for (const FMaterialParameterOverride& P : M.Parameters)
	{
		bool Found = false;
		VisitGraph(Validation, *Result.Graph, [&](FMaterialNode& N)
		{
			const uint8 Width = N.Expression == EMaterialExpression::ScalarParameter ? 1
			    : N.Expression == EMaterialExpression::VectorParameter               ? 4
			                                                                         : 0;
			if (Width && N.ParameterName == P.Name)
			{
				if (!Validation.Check(Width == P.Width, GENESIS_TEXT("Instance parameter type does not match parent.")))
				{
					return;
				}

				for (int I = 0; I < 4; ++I)
					N.Value[I] = P.Value[I];
				Found = true;
			}
		});
		if (!Validation.IsValid())
		{
			return false;
		}

		if (!Validation.Check(Found, GENESIS_TEXT("Instance override names a missing parent parameter.")))
		{
			return false;
		}
	}
	for (const FMaterialTextureParameterOverride& T : M.Textures)
	{
		bool Found = false;
		VisitGraph(Validation, *Result.Graph, [&](FMaterialNode& N)
		{
			if (N.Expression == EMaterialExpression::TextureSampleParameter2D && N.ParameterName == T.Name)
			{
				N.Texture = T.Texture;
				Found = true;
			}
		});
		if (!Validation.IsValid())
		{
			return false;
		}

		if (!Validation.Check(Found, GENESIS_TEXT("Instance texture override names a missing parent parameter.")))
		{
			return false;
		}
	}
	FMaterialGraphProgram After;
	if (!FMaterialGraphCompiler::Compile(*Result.Graph, After, Error))
		return false;
	if (!M.StaticParameters.empty() || M.bOverrideQuality)
	{
		if (!Validation.Check(M.StaticPermutation->CookedGraphSource == After.PixelShaderSource,
		        GENESIS_TEXT("Static instance permutation is stale; recompile in the editor.")))
		{
			return false;
		}

		Result.GraphDXIL = M.StaticPermutation->GraphDXIL;
		Result.GraphSPIRV = M.StaticPermutation->GraphSPIRV;
		Result.CookedGraphSource = M.StaticPermutation->CookedGraphSource;
	}
	else
	{
		if (!Validation.Check(Before.PixelShaderSource == After.PixelShaderSource,
		        GENESIS_TEXT("Instance override changed shader layout.")))
		{
			return false;
		}
	}
	Out = std::move(Result);
	Error.Empty();
	return true;
}

bool FMaterialInstance::BuildStaticPermutation(
    const FMaterialAsset& Parent, const FMaterialInstanceAsset& Instance, FMaterialAsset& Out, FString& Error)
{
	FValidationContext Validation(Error);

	Validate(Validation, Instance, false);
	if (!Validation.IsValid())
	{
		return false;
	}

	if (!Validation.Check(Parent.Graph.has_value(), GENESIS_TEXT("Instance needs a graph parent.")))
	{
		return false;
	}

	FMaterialAsset Result = Parent;
	for (const FMaterialStaticParameterOverride& Override : Instance.StaticParameters)
	{
		bool Found = false;
		VisitGraph(Validation, *Result.Graph, [&](FMaterialNode& N)
		{
			if (N.Expression == EMaterialExpression::StaticSwitchParameter && N.ParameterName == Override.Name)
			{
				N.Value[2] = Override.Value ? 1.f : 0.f;
				Found = true;
			}
		});
		if (!Validation.IsValid())
		{
			return false;
		}

		if (!Validation.Check(Found, GENESIS_TEXT("Static override names a missing parent parameter.")))
		{
			return false;
		}
	}
	if (Instance.bOverrideQuality)
		Result.Graph->QualityLevel = Instance.QualityLevel;
	Out = std::move(Result);
	Error.Empty();
	return true;
}
