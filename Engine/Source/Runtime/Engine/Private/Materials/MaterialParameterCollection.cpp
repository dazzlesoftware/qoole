// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Materials/MaterialParameterCollection.h"
#include "Misc/ValidationContext.h"
#include "Materials/MaterialLimits.h"
#include "Internationalization/TextLocalization.h"
#include "Assets/RenderAssetCodecPrivate.h"
#include <set>
#include <functional>
using namespace RenderAssetCodecPrivate;
bool FMaterialParameterCollectionCodec::Encode(
    const FMaterialParameterCollection& Collection, FAssetPackage& Out, FString& Error)
{
	if (Collection.Parameters.size() > MaterialLimits::MaxCollectionParameters)
	{
		Error = GENESIS_TEXT("Too many collection parameters.");
		return false;
	}

	std::set<FString> Names;
	FAssetPackage PackageData = Package(Out, "Genesis.MaterialParameterCollection");
	PackageData.SchemaVersion = 1;
	PackageData.Exports["Parameters"] = J::MakeArray();
	for (const FMaterialCollectionParameter& Parameter : Collection.Parameters)
	{
		if (Parameter.Name.IsEmpty() || Parameter.Name.Len() > MaterialLimits::MaxParameterNameLength ||
		    !Names.insert(Parameter.Name).second)
		{
			Error = GENESIS_TEXT("Invalid or duplicate collection parameter name.");
			return false;
		}
		if (Parameter.Width != 1 && Parameter.Width != 4)
		{
			Error = GENESIS_TEXT("Collection parameters must be scalar or float4.");
			return false;
		}

		J Entry = J::MakeObject();
		Entry["Name"] = Parameter.Name;
		Entry["Width"] = Parameter.Width;
		Entry["Value"] = J::MakeArray();
		for (float Value : Parameter.Value)
		{
			if (!std::isfinite(Value) || std::abs(Value) > MaterialLimits::MaxParameterMagnitude)
			{
				Error = GENESIS_TEXT("Invalid collection value.");
				return false;
			}
			Entry["Value"].Push(Value);
		}
		PackageData.Exports["Parameters"].Push(std::move(Entry));
	}

	Out = std::move(PackageData);
	Error.Empty();
	return true;
}

bool FMaterialParameterCollectionCodec::Decode(
    const FAssetPackage& PackageData, FMaterialParameterCollection& Out, FString& Error)
{
	if (PackageData.Kind != EAssetPackageKind::Asset || PackageData.Schema != "Genesis.MaterialParameterCollection" ||
	    PackageData.SchemaVersion != 1 || !PackageData.Bulk.empty())
	{
		Error = GENESIS_TEXT("Unsupported material parameter collection schema.");
		return false;
	}

	const J& Parameters = PackageData.Exports.Get("Parameters");
	if (!Parameters.IsArray() || Parameters.Num() > MaterialLimits::MaxCollectionParameters)
	{
		Error = GENESIS_TEXT("Invalid collection parameter list.");
		return false;
	}

	FMaterialParameterCollection Collection;
	for (const J& Entry : Parameters.GetItems())
	{
		const J& Name = Entry.Get("Name");
		const J& Width = Entry.Get("Width");
		const J& Values = Entry.Get("Value");
		if (Name.GetType() != EJsonType::String)
		{
			Error = GENESIS_TEXT("Invalid collection name.");
			return false;
		}
		if (Width.GetType() != EJsonType::Number || (Width.AsNumber() != 1 && Width.AsNumber() != 4))
		{
			Error = GENESIS_TEXT("Collection parameters must be scalar or float4.");
			return false;
		}
		if (!Values.IsArray() || Values.Num() != 4)
		{
			Error = GENESIS_TEXT("Invalid collection value.");
			return false;
		}

		FMaterialCollectionParameter Parameter;
		Parameter.Name = Name.AsString();
		Parameter.Width = static_cast<uint8>(Width.AsNumber());
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const J& Value = Values[Index];
			if (Value.GetType() != EJsonType::Number || !std::isfinite(Value.AsNumber()) ||
			    std::abs(Value.AsNumber()) > MaterialLimits::MaxParameterMagnitude)
			{
				Error = GENESIS_TEXT("Invalid collection value.");
				return false;
			}
			Parameter.Value[Index] = Value.AsFloat();
		}
		Collection.Parameters.push_back(std::move(Parameter));
	}

	FAssetPackage Validation;
	if (!Encode(Collection, Validation, Error))
	{
		return false;
	}
	Out = std::move(Collection);
	Error.Empty();
	return true;
}

bool FMaterialParameterCollectionCodec::Refresh(
    FMaterialGraph& Graph, const FAssetRegistry& Registry, bool& Changed, FString& Error)
{
	FValidationContext Validation(Error);

	FMaterialGraph Candidate = Graph;
	bool CandidateChanged = false;
	std::function<void(FMaterialGraph&, int)> Visit = [&](FMaterialGraph& G, int Depth)
	{
		if (!Validation.Check(Depth < 16, GENESIS_TEXT("Collection dependency depth exceeds 16.")))
		{
			return;
		}

		for (FMaterialNode& N : G.Nodes)
			if (N.Expression == EMaterialExpression::ScalarCollectionParameter ||
			    N.Expression == EMaterialExpression::VectorCollectionParameter)
			{
				FAssetPackage P;
				FString E;

				if (!Registry.Load(N.Collection, P, E, "Genesis.MaterialParameterCollection"))
				{
					Validation.Fail(E);
					return;
				}

				FMaterialParameterCollection C;

				if (!Decode(P, C, E))
				{
					Validation.Fail(E);
					return;
				}

				const FAssetData* Asset = Registry.FindById(P.Id);
				if (!Validation.Check(Asset != nullptr, GENESIS_TEXT("Collection registry entry is missing.")))
				{
					return;
				}

				const FAssetReference R = Asset->GetReference();
				CandidateChanged |= N.Collection.Id != R.Id || N.Collection.Path != R.Path;
				N.Collection = R;
				bool Found = false;
				for (const FMaterialCollectionParameter& Parameter : C.Parameters)
					if (Parameter.Name == N.ParameterName)
					{
						if (!Validation.Check(Parameter.Width ==
						            (N.Expression == EMaterialExpression::ScalarCollectionParameter ? 1 : 4),
						        GENESIS_TEXT("Collection parameter type changed.")))
						{
							return;
						}

						for (int I = 0; I < 4; ++I)
						{
							CandidateChanged |= N.Value[I] != Parameter.Value[I];
							N.Value[I] = Parameter.Value[I];
						}
						Found = true;
						break;
					}
				if (!Validation.Check(Found, GENESIS_TEXT("Collection parameter is missing.")))
				{
					return;
				}
			}
		for (FMaterialFunctionSnapshot& F : G.Functions)
		{
			if (!Validation.Check(F.Graph != nullptr, GENESIS_TEXT("Missing function snapshot.")))
			{
				return;
			}

			FMaterialGraph Definition = *F.Graph;
			Visit(Definition, Depth + 1);
			if (!Validation.IsValid())
			{
				return;
			}

			F.Graph = std::make_shared<const FMaterialGraph>(std::move(Definition));
		}
	};
	Visit(Candidate, 0);
	if (!Validation.IsValid())
	{
		return false;
	}

	Graph = std::move(Candidate);
	Changed = CandidateChanged;
	Error.Empty();
	return true;
}
FMaterialParameterCollectionInstance::FMaterialParameterCollectionInstance(
    FAssetReference R, FMaterialParameterCollection C)
    : Reference(std::move(R)), Values(std::move(C))
{
}
std::shared_ptr<FMaterialParameterCollectionInstance> FMaterialParameterCollectionInstance::Create(
    FAssetReference Reference, FMaterialParameterCollection Values, FString& Error)
{
	if (!FAssetReference::FromJson(Reference.ToJson(), Error))
	{
		return {};
	}
	if (Reference.Id.IsEmpty())
	{
		Error = GENESIS_TEXT("Collection instance requires a UUID-backed asset reference.");
		return {};
	}
	FAssetPackage Package;
	if (!FMaterialParameterCollectionCodec::Encode(Values, Package, Error))
	{
		return {};
	}
	return std::shared_ptr<FMaterialParameterCollectionInstance>(
	    new FMaterialParameterCollectionInstance(std::move(Reference), std::move(Values)));
}
bool FMaterialParameterCollectionInstance::SetScalar(const FString& Name, float V, FString& E)
{
	if (!std::isfinite(V) || std::abs(V) > MaterialLimits::MaxParameterMagnitude)
	{
		E = GENESIS_TEXT("Invalid collection scalar.");
		return false;
	}
	for (FMaterialCollectionParameter& P : Values.Parameters)
		if (P.Name == Name && P.Width == 1)
		{
			P.Value[0] = V;
			E.Empty();
			return true;
		}
	E = GENESIS_TEXT("Collection scalar is missing.");
	return false;
}
bool FMaterialParameterCollectionInstance::SetVector(const FString& Name, const float (&V)[4], FString& E)
{
	for (float F : V)
		if (!std::isfinite(F) || std::abs(F) > MaterialLimits::MaxParameterMagnitude)
		{
			E = GENESIS_TEXT("Invalid collection vector.");
			return false;
		}
	for (FMaterialCollectionParameter& P : Values.Parameters)
		if (P.Name == Name && P.Width == 4)
		{
			std::copy(std::begin(V), std::end(V), P.Value);
			E.Empty();
			return true;
		}
	E = GENESIS_TEXT("Collection vector is missing.");
	return false;
}
bool FMaterialParameterCollectionInstance::GetValue(const FString& Name, uint8 Width, float (&Out)[4], FString& E) const
{
	for (const FMaterialCollectionParameter& P : Values.Parameters)
		if (P.Name == Name && P.Width == Width)
		{
			std::copy(std::begin(P.Value), std::end(P.Value), Out);
			E.Empty();
			return true;
		}
	E = GENESIS_TEXT("Collection parameter is missing or has changed type.");
	return false;
}
