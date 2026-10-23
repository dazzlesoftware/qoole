// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialParameterCollection.h"
#include "Assets/RenderAssetCodecPrivate.h"
#include <map>
#include <set>
#include <functional>
#include <stdexcept>
namespace
{
void Need(bool B, const char* E)
{
	if (!B)
		throw std::runtime_error(E);
}
FString Key(const FAssetReference& R)
{
	return R.Id.IsEmpty() ? R.Path : R.Id;
}
} // namespace
bool FMaterialGraphCompiler::ExpandFunctions(const FMaterialGraph& Source, FMaterialGraph& Out,
											 FString& Error)
{
	try
	{
		FMaterialGraph Flat = Source;
		Flat.Functions.clear();
		uint32 Next = 1;
		for (const auto& N : Flat.Nodes)
			Next = std::max(Next, N.Id + 1);
		std::set<FString> Stack;
		std::function<void(uint32, const FMaterialGraph&, int)> Expand;
		Expand = [&](uint32 Id, const FMaterialGraph& Owner, int Depth) {
			const auto* Found = Flat.Find(Id);
			Need(Found != nullptr, "Function call node is missing.");
			if (Found->Expression != EMaterialExpression::MaterialFunctionCall)
				return;
			const auto Call = *Found;
			Need(Depth < 16, "Material function call depth exceeds 16.");
			Need(!Call.Function.IsNull(), "Choose a material function asset.");
			FString K;
			const FMaterialGraph* Definition = nullptr;
			for (const auto& Snapshot : Owner.Functions)
				if (Call.Function.Id.IsEmpty() ? Snapshot.Reference.Path == Call.Function.Path
											   : Snapshot.Reference.Id == Call.Function.Id)
				{
					Need(Definition == nullptr, "Duplicate material function snapshot.");
					Definition = Snapshot.Graph.get();
					K = Key(Snapshot.Reference);
				}
			Need(Stack.insert(K).second, "Material function recursion detected.");
			Need(Definition && Definition->bFunction, "Material function snapshot is missing or invalid.");
			Need(Definition->Outputs[0] != 0, "Material function requires a result.");
			bool Used[8] = {};
			for (const auto& N : Definition->Nodes)
				if (N.Expression == EMaterialExpression::FunctionInput)
				{
					Need(N.InputIndex < 8, "Invalid function input index.");
					Used[N.InputIndex] = true;
				}
			for (int I = 0; I < 8; ++I)
				Need(Used[I] || Call.Inputs[I] == 0, "Material function call links an undeclared input.");
			std::map<uint32, uint32> Remap;
			for (const auto& N : Definition->Nodes)
			{
				Need(N.Id > 0 && !Remap.contains(N.Id), "Duplicate function node ID.");
				Need(Flat.Nodes.size() < MaxNodes && Next <= 1000000,
					 "Expanded material exceeds node budget.");
				Remap[N.Id] = Next++;
				Flat.Nodes.push_back(N);
				Flat.Nodes.back().Id = Remap[N.Id];
				Flat.Nodes.back().DiagnosticOwner = Call.DiagnosticOwner ? Call.DiagnosticOwner : Call.Id;
			}
			for (const auto& N : Definition->Nodes)
			{
				auto* Copy = Flat.Find(Remap.at(N.Id));
				for (int I = 0; I < 8; ++I)
					if (N.Inputs[I])
					{
						Need(Remap.contains(N.Inputs[I]), "Dangling function link.");
						Copy->Inputs[I] = Remap.at(N.Inputs[I]);
					}
				if (N.Expression == EMaterialExpression::FunctionInput)
				{
					Need(N.InputIndex < 8, "Invalid function input index.");
					if (Call.Inputs[N.InputIndex])
						Copy->Inputs[0] = Call.Inputs[N.InputIndex];
				}
			}
			Need(Remap.contains(Definition->Outputs[0]), "Function result is missing.");
			for (const auto& N : Definition->Nodes)
				if (N.Expression == EMaterialExpression::MaterialFunctionCall)
					Expand(Remap.at(N.Id), *Definition, Depth + 1);
			auto* Replacement = Flat.Find(Id);
			Replacement->Expression = EMaterialExpression::Reroute;
			Replacement->Inputs[0] = Remap.at(Definition->Outputs[0]);
			for (int I = 1; I < 8; ++I)
				Replacement->Inputs[I] = 0;
			Replacement->Function = {};
			Stack.erase(K);
		};
		TArray<uint32> Calls;
		for (const auto& N : Source.Nodes)
			if (N.Expression == EMaterialExpression::MaterialFunctionCall)
				Calls.push_back(N.Id);
		for (auto Id : Calls)
			Expand(Id, Source, 0);
		Out = std::move(Flat);
		Error.Empty();
		return true;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		return false;
	}
}

bool FMaterialFunctionCodec::Encode(const FMaterialGraph& Graph, FAssetPackage& Out, FString& Error)
{
	try
	{
		Need(Graph.bFunction, "Material function requires a function graph.");
		Need(Graph.Outputs[0] != 0, "Material function needs a result.");
		for (int I = 1; I < 6; ++I)
			Need(Graph.Outputs[I] == 0, "A material function has one result.");
		FMaterialGraphProgram Program;
		if (!FMaterialGraphCompiler::Compile(Graph, Program, Error))
			return false;
		auto P = RenderAssetCodecPrivate::Package(Out, "Genesis.MaterialFunction");
		P.SchemaVersion = 1;
		P.Exports["Graph"] = FMaterialGraphCompiler::Encode(Graph);
		std::function<void(const FMaterialGraph&)> Dependencies = [&](const auto& G) {
			for (const auto& N : G.Nodes)
			{
				RenderAssetCodecPrivate::Dependency(P, N.Texture);
				RenderAssetCodecPrivate::Dependency(P, N.Function);
				RenderAssetCodecPrivate::Dependency(P, N.Collection);
			}
			for (const auto& F : G.Functions)
				if (F.Graph)
					Dependencies(*F.Graph);
		};
		Dependencies(Graph);
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
bool FMaterialFunctionCodec::Decode(const FAssetPackage& P, FMaterialGraph& Out, FString& Error)
{
	if (P.Kind != EAssetPackageKind::Asset || P.Schema != "Genesis.MaterialFunction" ||
		P.SchemaVersion != 1 || !P.Bulk.empty())
	{
		Error = "Unsupported material function schema.";
		return false;
	}
	FMaterialGraph G;
	if (!FMaterialGraphCompiler::Decode(P.Exports.Get("Graph"), G, Error))
		return false;
	for (int I = 1; I < 6; ++I)
		if (G.Outputs[I] != 0)
		{
			Error = "A material function has one result.";
			return false;
		}
	if (!G.bFunction || G.Outputs[0] == 0)
	{
		Error = "Material function needs a function graph and result.";
		return false;
	}
	Out = std::move(G);
	Error.Empty();
	return true;
}
bool FMaterialFunctionCodec::Refresh(FMaterialGraph& Graph, const FAssetRegistry& Registry, bool& Changed,
									 FString& Error, const FString& OwnerId)
{
	try
	{
		auto Candidate = Graph;
		std::set<FString> Active;
		if (!OwnerId.IsEmpty())
			Active.insert(OwnerId);
		int Count = 0;
		std::function<void(FMaterialGraph&, int)> Resolve = [&](auto& G, int Depth) {
			Need(Depth < 16, "Material function dependency depth exceeds 16.");
			G.Functions.clear();
			std::set<FString> Added;
			for (auto& Node : G.Nodes)
				if (Node.Expression == EMaterialExpression::MaterialFunctionCall)
				{
					FString E;
					const auto* Asset = Registry.Resolve(Node.Function, E, "Genesis.MaterialFunction");
					Need(Asset != nullptr, *E);
					const auto Reference = Asset->GetReference();
					Node.Function = Reference;
					if (!Added.insert(Reference.Id).second)
						continue;
					Need(Active.insert(Reference.Id).second,
						 "Material function dependency recursion detected.");
					Need(++Count <= 64, "Material function dependency budget exceeds 64.");
					FAssetPackage Package;
					Need(Registry.Load(Reference, Package, E, "Genesis.MaterialFunction"), *E);
					FMaterialGraph Definition;
					Need(Decode(Package, Definition, E), *E);
					Resolve(Definition, Depth + 1);
					Active.erase(Reference.Id);
					G.Functions.push_back(
						{Reference, std::make_shared<const FMaterialGraph>(std::move(Definition))});
				}
		};
		Resolve(Candidate, 0);
		bool CollectionChanged = false;
		Need(FMaterialParameterCollectionCodec::Refresh(Candidate, Registry, CollectionChanged, Error),
			 *Error);
		FMaterialGraphProgram Program;
		if (!FMaterialGraphCompiler::Compile(Candidate, Program, Error))
			return false;
		Changed = !(FMaterialGraphCompiler::Encode(Candidate) == FMaterialGraphCompiler::Encode(Graph));
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

namespace
{
const FMaterialGraph* FunctionDefinition(const FMaterialGraph& G, const FMaterialNode& N)
{
	for (const auto& F : G.Functions)
		if (N.Function.Id.IsEmpty() ? F.Reference.Path == N.Function.Path : F.Reference.Id == N.Function.Id)
			return F.Graph.get();
	return nullptr;
}
} // namespace
int MaterialNodeInputCount(const FMaterialGraph& G, const FMaterialNode& N)
{
	if (N.Expression == EMaterialExpression::MaterialFunctionCall)
		if (const auto* F = FunctionDefinition(G, N))
		{
			int Count = 0;
			for (const auto& Input : F->Nodes)
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
		if (const auto* F = FunctionDefinition(G, N))
		{
			for (const auto& Input : F->Nodes)
				if (Input.Expression == EMaterialExpression::FunctionInput && Input.InputIndex == I)
					return true;
			return false;
		}
	return true;
}
FString MaterialNodeInputName(const FMaterialGraph& G, const FMaterialNode& N, int I)
{
	if (N.Expression == EMaterialExpression::MaterialFunctionCall)
		if (const auto* F = FunctionDefinition(G, N))
			for (const auto& Input : F->Nodes)
				if (Input.Expression == EMaterialExpression::FunctionInput && Input.InputIndex == I &&
					!Input.ParameterName.IsEmpty())
					return Input.ParameterName;
	return MaterialInputName(N.Expression, I);
}
