// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Materials/MaterialFunction.h"
#include "Misc/ValidationContext.h"
#include "Internationalization/TextLocalization.h"
#include "Materials/MaterialParameterCollection.h"
#include "Assets/RenderAssetCodecPrivate.h"
#include <map>
#include <set>
#include <functional>

namespace
{

	FString Key(const FAssetReference& R)
	{
		return R.Id.IsEmpty() ? R.Path : R.Id;
	}
} // namespace
bool FMaterialGraphCompiler::ExpandFunctions(const FMaterialGraph& Source, FMaterialGraph& Out, FString& Error)
{
	FValidationContext Validation(Error);

	FMaterialGraph Flat = Source;
	Flat.Functions.clear();
	uint32 Next = 1;
	for (const FMaterialNode& N : Flat.Nodes)
		Next = std::max(Next, N.Id + 1);
	std::set<FString> Stack;
	std::function<void(uint32, const FMaterialGraph&, int)> Expand;
	Expand = [&](uint32 Id, const FMaterialGraph& Owner, int Depth)
	{
		const FMaterialNode* Found = Flat.Find(Id);
		if (!Validation.Check(Found != nullptr, GENESIS_TEXT("Function call node is missing.")))
		{
			return;
		}

		if (Found->Expression != EMaterialExpression::MaterialFunctionCall)
			return;
		const FMaterialNode Call = *Found;
		if (!Validation.Check(Depth < 16, GENESIS_TEXT("Material function call depth exceeds 16.")))
		{
			return;
		}

		if (!Validation.Check(!Call.Function.IsNull(), GENESIS_TEXT("Choose a material function asset.")))
		{
			return;
		}

		FString K;
		const FMaterialGraph* Definition = nullptr;
		for (const FMaterialFunctionSnapshot& Snapshot : Owner.Functions)
			if (Call.Function.Id.IsEmpty() ? Snapshot.Reference.Path == Call.Function.Path
			                               : Snapshot.Reference.Id == Call.Function.Id)
			{
				if (!Validation.Check(Definition == nullptr, GENESIS_TEXT("Duplicate material function snapshot.")))
				{
					return;
				}

				Definition = Snapshot.Graph.get();
				K = Key(Snapshot.Reference);
			}
		if (!Validation.Check(Stack.insert(K).second, GENESIS_TEXT("Material function recursion detected.")))
		{
			return;
		}

		if (!Validation.Check(
		        Definition && Definition->bFunction, GENESIS_TEXT("Material function snapshot is missing or invalid.")))
		{
			return;
		}

		if (!Validation.Check(Definition->Outputs[0] != 0, GENESIS_TEXT("Material function requires a result.")))
		{
			return;
		}

		bool Used[8] = {};
		for (const FMaterialNode& N : Definition->Nodes)
			if (N.Expression == EMaterialExpression::FunctionInput)
			{
				if (!Validation.Check(N.InputIndex < 8, GENESIS_TEXT("Invalid function input index.")))
				{
					return;
				}

				Used[N.InputIndex] = true;
			}
		for (int I = 0; I < 8; ++I)
		{
			if (!Validation.Check(
			        Used[I] || Call.Inputs[I] == 0, GENESIS_TEXT("Material function call links an undeclared input.")))
			{
				return;
			}
		}
		std::map<uint32, uint32> Remap;
		for (const FMaterialNode& N : Definition->Nodes)
		{
			if (!Validation.Check(N.Id > 0 && !Remap.contains(N.Id), GENESIS_TEXT("Duplicate function node ID.")))
			{
				return;
			}

			if (!Validation.Check(Flat.Nodes.size() < MaxNodes && Next <= 1000000,
			        GENESIS_TEXT("Expanded material exceeds node budget.")))
			{
				return;
			}

			Remap[N.Id] = Next++;
			Flat.Nodes.push_back(N);
			Flat.Nodes.back().Id = Remap[N.Id];
			Flat.Nodes.back().DiagnosticOwner = Call.DiagnosticOwner ? Call.DiagnosticOwner : Call.Id;
		}
		for (const FMaterialNode& N : Definition->Nodes)
		{
			FMaterialNode* Copy = Flat.Find(Remap.at(N.Id));
			for (int I = 0; I < 8; ++I)
				if (N.Inputs[I])
				{
					if (!Validation.Check(Remap.contains(N.Inputs[I]), GENESIS_TEXT("Dangling function link.")))
					{
						return;
					}

					Copy->Inputs[I] = Remap.at(N.Inputs[I]);
				}
			if (N.Expression == EMaterialExpression::FunctionInput)
			{
				if (!Validation.Check(N.InputIndex < 8, GENESIS_TEXT("Invalid function input index.")))
				{
					return;
				}

				if (Call.Inputs[N.InputIndex])
					Copy->Inputs[0] = Call.Inputs[N.InputIndex];
			}
		}
		if (!Validation.Check(Remap.contains(Definition->Outputs[0]), GENESIS_TEXT("Function result is missing.")))
		{
			return;
		}

		for (const FMaterialNode& N : Definition->Nodes)
		{
			if (N.Expression == EMaterialExpression::MaterialFunctionCall)
				Expand(Remap.at(N.Id), *Definition, Depth + 1);
			if (!Validation.IsValid())
			{
				return;
			}
		}
		if (!Validation.IsValid())
		{
			return;
		}

		FMaterialNode* Replacement = Flat.Find(Id);
		Replacement->Expression = EMaterialExpression::Reroute;
		Replacement->Inputs[0] = Remap.at(Definition->Outputs[0]);
		for (int I = 1; I < 8; ++I)
			Replacement->Inputs[I] = 0;
		Replacement->Function = {};
		Stack.erase(K);
	};
	TArray<uint32> Calls;
	for (const FMaterialNode& N : Source.Nodes)
		if (N.Expression == EMaterialExpression::MaterialFunctionCall)
			Calls.push_back(N.Id);
	for (unsigned int Id : Calls)
	{
		Expand(Id, Source, 0);
		if (!Validation.IsValid())
		{
			return {};
		}
	}
	if (!Validation.IsValid())
	{
		return false;
	}

	Out = std::move(Flat);
	Error.Empty();
	return true;
}

bool FMaterialFunctionCodec::Encode(const FMaterialGraph& Graph, FAssetPackage& Out, FString& Error)
{
	FValidationContext Validation(Error);

	if (!Validation.Check(Graph.bFunction, GENESIS_TEXT("Material function requires a function graph.")))
	{
		return false;
	}

	if (!Validation.Check(Graph.Outputs[0] != 0, GENESIS_TEXT("Material function needs a result.")))
	{
		return false;
	}

	for (int I = 1; I < 6; ++I)
	{
		if (!Validation.Check(Graph.Outputs[I] == 0, GENESIS_TEXT("A material function has one result.")))
		{
			return false;
		}
	}
	FMaterialGraphProgram Program;
	if (!FMaterialGraphCompiler::Compile(Graph, Program, Error))
		return false;
	FAssetPackage P = RenderAssetCodecPrivate::Package(Out, "Genesis.MaterialFunction");
	P.SchemaVersion = 1;
	if (!FMaterialGraphCompiler::Encode(Graph, P.Exports["Graph"], Error))
	{
		return false;
	}
	std::function<void(const FMaterialGraph&)> Dependencies = [&](const FMaterialGraph& G)
	{
		for (const FMaterialNode& N : G.Nodes)
		{
			RenderAssetCodecPrivate::Dependency(Validation, P, N.Texture);
			if (!Validation.IsValid())
			{
				return;
			}

			RenderAssetCodecPrivate::Dependency(Validation, P, N.Function);
			if (!Validation.IsValid())
			{
				return;
			}

			RenderAssetCodecPrivate::Dependency(Validation, P, N.Collection);
			if (!Validation.IsValid())
			{
				return;
			}
		}
		for (const FMaterialFunctionSnapshot& F : G.Functions)
		{
			if (F.Graph)
				Dependencies(*F.Graph);
			if (!Validation.IsValid())
			{
				return;
			}
		}
		if (!Validation.IsValid())
		{
			return;
		}
	};
	Dependencies(Graph);
	if (!Validation.IsValid())
	{
		return false;
	}

	Out = std::move(P);
	Error.Empty();
	return true;
}
bool FMaterialFunctionCodec::Decode(const FAssetPackage& P, FMaterialGraph& Out, FString& Error)
{
	if (P.Kind != EAssetPackageKind::Asset || P.Schema != "Genesis.MaterialFunction" || P.SchemaVersion != 1 ||
	    !P.Bulk.empty())
	{
		Error = GENESIS_TEXT("Unsupported material function schema.");
		return false;
	}
	FMaterialGraph G;
	if (!FMaterialGraphCompiler::Decode(P.Exports.Get("Graph"), G, Error))
		return false;
	for (int I = 1; I < 6; ++I)
		if (G.Outputs[I] != 0)
		{
			Error = GENESIS_TEXT("A material function has one result.");
			return false;
		}
	if (!G.bFunction || G.Outputs[0] == 0)
	{
		Error = GENESIS_TEXT("Material function needs a function graph and result.");
		return false;
	}
	Out = std::move(G);
	Error.Empty();
	return true;
}
bool FMaterialFunctionCodec::Refresh(
    FMaterialGraph& Graph, const FAssetRegistry& Registry, bool& Changed, FString& Error, const FString& OwnerId)
{
	FValidationContext Validation(Error);

	FMaterialGraph Candidate = Graph;
	std::set<FString> Active;
	if (!OwnerId.IsEmpty())
		Active.insert(OwnerId);
	int Count = 0;
	std::function<void(FMaterialGraph&, int)> Resolve = [&](FMaterialGraph& G, int Depth)
	{
		if (!Validation.Check(Depth < 16, GENESIS_TEXT("Material function dependency depth exceeds 16.")))
		{
			return;
		}

		G.Functions.clear();
		std::set<FString> Added;
		for (FMaterialNode& Node : G.Nodes)
			if (Node.Expression == EMaterialExpression::MaterialFunctionCall)
			{
				FString E;
				const FAssetData* Asset = Registry.Resolve(Node.Function, E, "Genesis.MaterialFunction");
				if (!Validation.IsValid())
				{
					return;
				}

				if (Asset == nullptr)
				{
					Validation.Fail(E);
					return;
				}

				const FAssetReference Reference = Asset->GetReference();
				Node.Function = Reference;
				if (!Added.insert(Reference.Id).second)
					continue;
				if (!Validation.Check(Active.insert(Reference.Id).second,
				        GENESIS_TEXT("Material function dependency recursion detected.")))
				{
					return;
				}

				if (!Validation.Check(++Count <= 64, GENESIS_TEXT("Material function dependency budget exceeds 64.")))
				{
					return;
				}

				FAssetPackage Package;

				if (!Registry.Load(Reference, Package, E, "Genesis.MaterialFunction"))
				{
					Validation.Fail(E);
					return;
				}

				FMaterialGraph Definition;

				if (!Decode(Package, Definition, E))
				{
					Validation.Fail(E);
					return;
				}

				Resolve(Definition, Depth + 1);
				if (!Validation.IsValid())
				{
					return;
				}

				Active.erase(Reference.Id);
				G.Functions.push_back({Reference, std::make_shared<const FMaterialGraph>(std::move(Definition))});
			}
	};
	Resolve(Candidate, 0);
	if (!Validation.IsValid())
	{
		return false;
	}

	bool CollectionChanged = false;

	if (!FMaterialParameterCollectionCodec::Refresh(Candidate, Registry, CollectionChanged, Error))
	{
		Validation.Fail(Error);
		return false;
	}

	FMaterialGraphProgram Program;
	if (!FMaterialGraphCompiler::Compile(Candidate, Program, Error))
		return false;
	FJsonValue CandidateJson, PreviousJson;
	if (!FMaterialGraphCompiler::Encode(Candidate, CandidateJson, Error) ||
	    !FMaterialGraphCompiler::Encode(Graph, PreviousJson, Error))
	{
		return false;
	}
	Changed = CandidateJson != PreviousJson;
	Graph = std::move(Candidate);
	Error.Empty();
	return true;
}

namespace
{
	const FMaterialGraph* FunctionDefinition(const FMaterialGraph& G, const FMaterialNode& N)
	{
		for (const FMaterialFunctionSnapshot& F : G.Functions)
			if (N.Function.Id.IsEmpty() ? F.Reference.Path == N.Function.Path : F.Reference.Id == N.Function.Id)
				return F.Graph.get();
		return nullptr;
	}
} // namespace
int MaterialNodeInputCount(const FMaterialGraph& G, const FMaterialNode& N)
{
	if (N.Expression == EMaterialExpression::MaterialFunctionCall)
		if (const FMaterialGraph* F = FunctionDefinition(G, N))
		{
			int Count = 0;
			for (const FMaterialNode& Input : F->Nodes)
				if (Input.Expression == EMaterialExpression::FunctionInput)
					Count = std::max(Count, int(Input.InputIndex) + 1);
			return Count;
		}
	return MaterialInputCount(N.Expression);
}
bool MaterialNodeHasInput(const FMaterialGraph& G, const FMaterialNode& N, int I)
{
	if (I < 0 || I >= MaterialNodeInputCount(G, N))
		return false;
	if (N.Expression == EMaterialExpression::MaterialFunctionCall)
		if (const FMaterialGraph* F = FunctionDefinition(G, N))
		{
			for (const FMaterialNode& Input : F->Nodes)
				if (Input.Expression == EMaterialExpression::FunctionInput && Input.InputIndex == I)
					return true;
			return false;
		}
	return true;
}
FString MaterialNodeInputName(const FMaterialGraph& G, const FMaterialNode& N, int I)
{
	if (N.Expression == EMaterialExpression::MaterialFunctionCall)
		if (const FMaterialGraph* F = FunctionDefinition(G, N))
			for (const FMaterialNode& Input : F->Nodes)
				if (Input.Expression == EMaterialExpression::FunctionInput && Input.InputIndex == I &&
				    !Input.ParameterName.IsEmpty())
					return Input.ParameterName;
	return MaterialInputName(N.Expression, I);
}
