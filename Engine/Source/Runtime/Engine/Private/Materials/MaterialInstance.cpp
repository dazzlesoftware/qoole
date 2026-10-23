// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Materials/MaterialInstance.h"
#include "Assets/RenderAssetCodecPrivate.h"
#include <set>
#include <functional>
using namespace RenderAssetCodecPrivate;
namespace
{
void Validate(const FMaterialInstanceAsset& M, bool RequireCook = true)
{
	Need(!M.Parent.IsNull(), "Material instance requires a parent.");
	Reference(M.Parent.ToJson());
	Need(M.Parameters.size() + M.Textures.size() + M.StaticParameters.size() <=
			 FMaterialGraphCompiler::MaxNodes,
		 "Too many instance overrides.");
	std::set<FString> Names;
	for (const auto& P : M.Parameters)
	{
		Need(!P.Name.IsEmpty() && P.Name.Len() <= 128 && Names.insert(P.Name).second,
			 "Invalid or duplicate parameter name.");
		Need(P.Width == 1 || P.Width == 4, "Invalid parameter width.");
		for (float V : P.Value)
			Need(std::isfinite(V) && std::abs(V) <= 1e6f, "Invalid parameter value.");
	}
	Need(M.QualityLevel <= 2, "Invalid instance quality level.");
	for (const auto& P : M.StaticParameters)
		Need(!P.Name.IsEmpty() && P.Name.Len() <= 128 && Names.insert(P.Name).second,
			 "Invalid or duplicate static parameter name.");
	if (RequireCook && (!M.StaticParameters.empty() || M.bOverrideQuality))
		Need(M.StaticPermutation.has_value(),
			 "Cook the static instance permutation before saving or loading.");
	for (const auto& T : M.Textures)
	{
		Need(!T.Name.IsEmpty() && T.Name.Len() <= 128 && Names.insert(T.Name).second,
			 "Invalid or duplicate texture parameter name.");
		Need(!T.Texture.IsNull(), "Texture parameter requires a texture reference.");
		Reference(T.Texture.ToJson());
	}
}
void VisitGraph(FMaterialGraph& G, const std::function<void(FMaterialNode&)>& Visit, int Depth = 0)
{
	Need(Depth < 16, "Instance function nesting exceeds 16.");
	for (auto& N : G.Nodes)
		Visit(N);
	for (auto& F : G.Functions)
	{
		Need(F.Graph != nullptr, "Missing function snapshot.");
		auto Next = *F.Graph;
		VisitGraph(Next, Visit, Depth + 1);
		F.Graph = std::make_shared<const FMaterialGraph>(std::move(Next));
	}
}
} // namespace
bool FRenderAssetCodec::EncodeMaterialInstance(const FMaterialInstanceAsset& M, FAssetPackage& Out,
											   FString& Error)
{
	try
	{
		Validate(M);
		auto P = Package(Out, "Genesis.MaterialInstance");
		P.SchemaVersion = 2;
		P.Exports["Parent"] = M.Parent.ToJson();
		Dependency(P, M.Parent);
		P.Exports["Parameters"] = J::MakeArray();
		for (const auto& V : M.Parameters)
		{
			auto A = J::MakeObject();
			A["Name"] = V.Name;
			A["Width"] = V.Width;
			A["Value"] = J::MakeArray();
			for (float F : V.Value)
				A["Value"].Push(F);
			P.Exports["Parameters"].Push(A);
		}
		P.Exports["Textures"] = J::MakeArray();
		for (const auto& T : M.Textures)
		{
			auto V = J::MakeObject();
			V["Name"] = T.Name;
			V["Texture"] = T.Texture.ToJson();
			P.Exports["Textures"].Push(V);
			Dependency(P, T.Texture);
		}
		if (!M.StaticParameters.empty() || M.bOverrideQuality)
		{
			FMaterialAsset Expected;
			if (!FMaterialInstance::BuildStaticPermutation(*M.StaticPermutation, M, Expected, Error))
				return false;
			Need(FMaterialGraphCompiler::Encode(*Expected.Graph) ==
					 FMaterialGraphCompiler::Encode(*M.StaticPermutation->Graph),
				 "Static permutation does not match its overrides.");
			FAssetPackage Cook;
			if (!FRenderAssetCodec::EncodeMaterial(*M.StaticPermutation, Cook, Error))
				return false;
			P.SchemaVersion = 3;
			P.Exports["StaticParameters"] = J::MakeArray();
			for (const auto& S : M.StaticParameters)
			{
				auto V = J::MakeObject();
				V["Name"] = S.Name;
				V["Value"] = S.Value;
				P.Exports["StaticParameters"].Push(std::move(V));
			}
			P.Exports["OverrideQuality"] = M.bOverrideQuality;
			P.Exports["QualityLevel"] = M.QualityLevel;
			P.Exports["Permutation"] = Cook.Exports;
			P.Bulk = std::move(Cook.Bulk);
			for (const auto& D : Cook.Dependencies)
				if (std::find(P.Dependencies.begin(), P.Dependencies.end(), D) == P.Dependencies.end())
					P.Dependencies.push_back(D);
		}
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
bool FRenderAssetCodec::DecodeMaterialInstance(const FAssetPackage& P, FMaterialInstanceAsset& Out,
											   FString& Error)
{
	try
	{
		Need(P.Kind == EAssetPackageKind::Asset && P.Schema == "Genesis.MaterialInstance" &&
				 (P.SchemaVersion >= 1 && P.SchemaVersion <= 3) && P.Exports.IsObject() &&
				 (P.SchemaVersion == 3 || P.Bulk.empty()),
			 "Unsupported material instance schema.");
		FMaterialInstanceAsset M;
		M.Parent = Reference(P.Exports.Get("Parent"));
		const auto& Values = P.Exports.Get("Parameters");
		Need(Values.IsArray() && Values.Num() <= FMaterialGraphCompiler::MaxNodes,
			 "Invalid instance parameters.");
		for (const auto& A : Values.GetItems())
		{
			Need(A.IsObject() && A.Get("Name").GetType() == EJsonType::String, "Invalid parameter override.");
			FMaterialParameterOverride V;
			V.Name = A.Get("Name").AsString();
			V.Width = static_cast<uint8>(Count(A.Get("Width"), 4));
			const auto& Fs = A.Get("Value");
			Need(Fs.IsArray() && Fs.Num() == 4, "Invalid parameter values.");
			for (int I = 0; I < 4; ++I)
				V.Value[I] = Number(Fs[I], -1e6f, 1e6f);
			M.Parameters.push_back(V);
		}
		if (P.SchemaVersion >= 2)
		{
			const auto& Ts = P.Exports.Get("Textures");
			Need(Ts.IsArray() && Ts.Num() <= FMaterialGraphCompiler::MaxNodes, "Invalid texture overrides.");
			for (const auto& T : Ts.GetItems())
			{
				Need(T.IsObject() && T.Get("Name").GetType() == EJsonType::String,
					 "Invalid texture override.");
				M.Textures.push_back({T.Get("Name").AsString(), Reference(T.Get("Texture"))});
			}
		}
		if (P.SchemaVersion == 3)
		{
			const auto& S = P.Exports.Get("StaticParameters");
			Need(S.IsArray() && S.Num() <= 256, "Invalid static override list.");
			for (const auto& V : S.GetItems())
			{
				Need(V.Get("Name").GetType() == EJsonType::String &&
						 V.Get("Value").GetType() == EJsonType::Bool,
					 "Invalid static override.");
				M.StaticParameters.push_back({V.Get("Name").AsString(), V.Get("Value").AsBool()});
			}
			Need(P.Exports.Get("OverrideQuality").GetType() == EJsonType::Bool,
				 "Invalid instance quality override.");
			M.bOverrideQuality = P.Exports.Get("OverrideQuality").AsBool();
			M.QualityLevel = static_cast<uint8>(Count(P.Exports.Get("QualityLevel"), 2));
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
		Validate(M);
		Out = std::move(M);
		Error.Empty();
		return true;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		return false;
	}
}
bool FMaterialInstance::Resolve(const FMaterialAsset& Parent, const FMaterialInstanceAsset& M,
								FMaterialAsset& Out, FString& Error)
{
	try
	{
		Validate(M);
		Need(Parent.Graph.has_value(), "Instances require a graph material parent.");
		FMaterialGraphProgram Before;
		if (!FMaterialGraphCompiler::Compile(*Parent.Graph, Before, Error))
			return false;
		Need(Before.PixelShaderSource == Parent.CookedGraphSource && !Parent.GraphDXIL.empty() &&
				 !Parent.GraphSPIRV.empty(),
			 "Material instance parent requires a valid cook.");
		FMaterialAsset Result;
		if (!BuildStaticPermutation(Parent, M, Result, Error))
			return false;
		for (const auto& P : M.Parameters)
		{
			bool Found = false;
			VisitGraph(*Result.Graph, [&](auto& N) {
				const uint8 Width = N.Expression == EMaterialExpression::ScalarParameter   ? 1
									: N.Expression == EMaterialExpression::VectorParameter ? 4
																						   : 0;
				if (Width && N.ParameterName == P.Name)
				{
					Need(Width == P.Width, "Instance parameter type does not match parent.");
					for (int I = 0; I < 4; ++I)
						N.Value[I] = P.Value[I];
					Found = true;
				}
			});
			Need(Found, "Instance override names a missing parent parameter.");
		}
		for (const auto& T : M.Textures)
		{
			bool Found = false;
			VisitGraph(*Result.Graph, [&](auto& N) {
				if (N.Expression == EMaterialExpression::TextureSampleParameter2D &&
					N.ParameterName == T.Name)
				{
					N.Texture = T.Texture;
					Found = true;
				}
			});
			Need(Found, "Instance texture override names a missing parent parameter.");
		}
		FMaterialGraphProgram After;
		if (!FMaterialGraphCompiler::Compile(*Result.Graph, After, Error))
			return false;
		if (!M.StaticParameters.empty() || M.bOverrideQuality)
		{
			Need(M.StaticPermutation->CookedGraphSource == After.PixelShaderSource,
				 "Static instance permutation is stale; recompile in the editor.");
			Result.GraphDXIL = M.StaticPermutation->GraphDXIL;
			Result.GraphSPIRV = M.StaticPermutation->GraphSPIRV;
			Result.CookedGraphSource = M.StaticPermutation->CookedGraphSource;
		}
		else
			Need(Before.PixelShaderSource == After.PixelShaderSource,
				 "Instance override changed shader layout.");
		Out = std::move(Result);
		Error.Empty();
		return true;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		return false;
	}
}

bool FMaterialInstance::BuildStaticPermutation(const FMaterialAsset& Parent,
											   const FMaterialInstanceAsset& Instance, FMaterialAsset& Out,
											   FString& Error)
{
	try
	{
		Validate(Instance, false);
		Need(Parent.Graph.has_value(), "Instance needs a graph parent.");
		auto Result = Parent;
		for (const auto& Override : Instance.StaticParameters)
		{
			bool Found = false;
			VisitGraph(*Result.Graph, [&](auto& N) {
				if (N.Expression == EMaterialExpression::StaticSwitchParameter &&
					N.ParameterName == Override.Name)
				{
					N.Value[2] = Override.Value ? 1.f : 0.f;
					Found = true;
				}
			});
			Need(Found, "Static override names a missing parent parameter.");
		}
		if (Instance.bOverrideQuality)
			Result.Graph->QualityLevel = Instance.QualityLevel;
		Out = std::move(Result);
		Error.Empty();
		return true;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		return false;
	}
}
