// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Materials/MaterialInstance.h"
#include "Assets/RenderAssetCodecPrivate.h"
#include <set>
using namespace RenderAssetCodecPrivate;
namespace
{
void Validate(const FMaterialInstanceAsset& M)
{
	Need(!M.Parent.IsNull(), "Material instance requires a parent.");
	Reference(M.Parent.ToJson());
	Need(M.Parameters.size() + M.Textures.size() <= FMaterialGraphCompiler::MaxNodes,
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
	for (const auto& T : M.Textures)
	{
		Need(!T.Name.IsEmpty() && T.Name.Len() <= 128 && Names.insert(T.Name).second,
			 "Invalid or duplicate texture parameter name.");
		Need(!T.Texture.IsNull(), "Texture parameter requires a texture reference.");
		Reference(T.Texture.ToJson());
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
				 (P.SchemaVersion == 1 || P.SchemaVersion == 2) && P.Exports.IsObject() && P.Bulk.empty(),
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
		auto Result = Parent;
		for (const auto& P : M.Parameters)
		{
			bool Found = false;
			for (auto& N : Result.Graph->Nodes)
			{
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
			}
			Need(Found, "Instance override names a missing parent parameter.");
		}
		for (const auto& T : M.Textures)
		{
			bool Found = false;
			for (auto& N : Result.Graph->Nodes)
				if (N.Expression == EMaterialExpression::TextureSampleParameter2D &&
					N.ParameterName == T.Name)
				{
					N.Texture = T.Texture;
					Found = true;
				}
			Need(Found, "Instance texture override names a missing parent parameter.");
		}
		FMaterialGraphProgram After;
		if (!FMaterialGraphCompiler::Compile(*Result.Graph, After, Error))
			return false;
		Need(Before.PixelShaderSource == After.PixelShaderSource, "Instance override changed shader layout.");
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
