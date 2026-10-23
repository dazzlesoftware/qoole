// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Materials/MaterialParameterCollection.h"
#include "Assets/RenderAssetCodecPrivate.h"
#include <set>
#include <functional>
using namespace RenderAssetCodecPrivate;
bool FMaterialParameterCollectionCodec::Encode(const FMaterialParameterCollection& C, FAssetPackage& Out,
											   FString& Error)
{
	try
	{
		Need(C.Parameters.size() <= 256, "Too many collection parameters.");
		std::set<FString> Names;
		auto P = Package(Out, "Genesis.MaterialParameterCollection");
		P.SchemaVersion = 1;
		P.Exports["Parameters"] = J::MakeArray();
		for (const auto& V : C.Parameters)
		{
			Need(!V.Name.IsEmpty() && V.Name.Len() <= 128 && Names.insert(V.Name).second,
				 "Invalid or duplicate collection parameter name.");
			Need(V.Width == 1 || V.Width == 4, "Collection parameters must be scalar or float4.");
			auto A = J::MakeObject();
			A["Name"] = V.Name;
			A["Width"] = V.Width;
			A["Value"] = J::MakeArray();
			for (float F : V.Value)
			{
				Need(std::isfinite(F) && std::abs(F) <= 1e6f, "Invalid collection value.");
				A["Value"].Push(F);
			}
			P.Exports["Parameters"].Push(std::move(A));
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
bool FMaterialParameterCollectionCodec::Decode(const FAssetPackage& P, FMaterialParameterCollection& Out,
											   FString& Error)
{
	try
	{
		Need(P.Kind == EAssetPackageKind::Asset && P.Schema == "Genesis.MaterialParameterCollection" &&
				 P.SchemaVersion == 1 && P.Bulk.empty(),
			 "Unsupported material parameter collection schema.");
		const auto& A = P.Exports.Get("Parameters");
		Need(A.IsArray() && A.Num() <= 256, "Invalid collection parameter list.");
		FMaterialParameterCollection C;
		for (const auto& V : A.GetItems())
		{
			Need(V.Get("Name").GetType() == EJsonType::String, "Invalid collection name.");
			FMaterialCollectionParameter Parameter;
			Parameter.Name = V.Get("Name").AsString();
			Parameter.Width = static_cast<uint8>(Count(V.Get("Width"), 4));
			const auto& Values = V.Get("Value");
			Need(Values.IsArray() && Values.Num() == 4, "Invalid collection value.");
			for (int I = 0; I < 4; ++I)
				Parameter.Value[I] = Number(Values[I], -1e6f, 1e6f);
			C.Parameters.push_back(Parameter);
		}
		FAssetPackage Check;
		if (!Encode(C, Check, Error))
			return false;
		Out = std::move(C);
		Error.Empty();
		return true;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		return false;
	}
}
bool FMaterialParameterCollectionCodec::Refresh(FMaterialGraph& Graph, const FAssetRegistry& Registry,
												bool& Changed, FString& Error)
{
	try
	{
		auto Candidate = Graph;
		Changed = false;
		std::function<void(FMaterialGraph&, int)> Visit = [&](auto& G, int Depth) {
			Need(Depth < 16, "Collection dependency depth exceeds 16.");
			for (auto& N : G.Nodes)
				if (N.Expression == EMaterialExpression::ScalarCollectionParameter ||
					N.Expression == EMaterialExpression::VectorCollectionParameter)
				{
					FAssetPackage P;
					FString E;
					Need(Registry.Load(N.Collection, P, E, "Genesis.MaterialParameterCollection"), *E);
					FMaterialParameterCollection C;
					Need(Decode(P, C, E), *E);
					const auto* Asset = Registry.FindById(P.Id);
					Need(Asset != nullptr, "Collection registry entry is missing.");
					const auto R = Asset->GetReference();
					Changed |= N.Collection.Id != R.Id || N.Collection.Path != R.Path;
					N.Collection = R;
					bool Found = false;
					for (const auto& Parameter : C.Parameters)
						if (Parameter.Name == N.ParameterName)
						{
							Need(Parameter.Width ==
									 (N.Expression == EMaterialExpression::ScalarCollectionParameter ? 1 : 4),
								 "Collection parameter type changed.");
							for (int I = 0; I < 4; ++I)
							{
								Changed |= N.Value[I] != Parameter.Value[I];
								N.Value[I] = Parameter.Value[I];
							}
							Found = true;
							break;
						}
					Need(Found, "Collection parameter is missing.");
				}
			for (auto& F : G.Functions)
			{
				Need(F.Graph != nullptr, "Missing function snapshot.");
				auto Definition = *F.Graph;
				Visit(Definition, Depth + 1);
				F.Graph = std::make_shared<const FMaterialGraph>(std::move(Definition));
			}
		};
		Visit(Candidate, 0);
		Graph = std::move(Candidate);
		Error.Empty();
		return true;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		return false;
	}
}
FMaterialParameterCollectionInstance::FMaterialParameterCollectionInstance(FAssetReference R,
																		   FMaterialParameterCollection C)
	: Reference(std::move(R)), Values(std::move(C))
{
	RenderAssetCodecPrivate::Reference(Reference.ToJson());
	Need(!Reference.Id.IsEmpty(), "Collection instance requires a UUID-backed asset reference.");
	FAssetPackage Validation;
	FString Error;
	Need(FMaterialParameterCollectionCodec::Encode(Values, Validation, Error), *Error);
}
bool FMaterialParameterCollectionInstance::SetScalar(const FString& Name, float V, FString& E)
{
	if (!std::isfinite(V) || std::abs(V) > 1e6f)
	{
		E = "Invalid collection scalar.";
		return false;
	}
	for (auto& P : Values.Parameters)
		if (P.Name == Name && P.Width == 1)
		{
			P.Value[0] = V;
			E.Empty();
			return true;
		}
	E = "Collection scalar is missing.";
	return false;
}
bool FMaterialParameterCollectionInstance::SetVector(const FString& Name, const float (&V)[4], FString& E)
{
	for (float F : V)
		if (!std::isfinite(F) || std::abs(F) > 1e6f)
		{
			E = "Invalid collection vector.";
			return false;
		}
	for (auto& P : Values.Parameters)
		if (P.Name == Name && P.Width == 4)
		{
			std::copy(std::begin(V), std::end(V), P.Value);
			E.Empty();
			return true;
		}
	E = "Collection vector is missing.";
	return false;
}
bool FMaterialParameterCollectionInstance::GetValue(const FString& Name, uint8 Width, float (&Out)[4],
													FString& E) const
{
	for (const auto& P : Values.Parameters)
		if (P.Name == Name && P.Width == Width)
		{
			std::copy(std::begin(P.Value), std::end(P.Value), Out);
			E.Empty();
			return true;
		}
	E = "Collection parameter is missing or has changed type.";
	return false;
}
