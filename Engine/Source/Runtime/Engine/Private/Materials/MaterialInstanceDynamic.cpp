// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Materials/MaterialInstanceDynamic.h"
#include <algorithm>
TUniquePtr<FMaterialInstanceDynamic> FMaterialInstanceDynamic::Create(const FMaterialAsset& Parent,
																	  FString& Error)
{
	if (!Parent.Graph)
	{
		Error = "Dynamic material instances require graph materials.";
		return {};
	}
	FMaterialGraphProgram Program;
	if (!FMaterialGraphCompiler::Compile(*Parent.Graph, Program, Error))
		return {};
	if (Program.PixelShaderSource != Parent.CookedGraphSource || Parent.GraphDXIL.empty() ||
		Parent.GraphSPIRV.empty())
	{
		Error = "Dynamic material instance parent requires a valid cook.";
		return {};
	}
	auto Result = MakeUnique<FMaterialInstanceDynamic>();
	Result->Parent = Parent;
	Result->Resolved = Parent;
	Result->Overrides.Parent.Path = "/Engine/Transient";
	Error.Empty();
	return Result;
}
bool FMaterialInstanceDynamic::Commit(FMaterialInstanceAsset Candidate, FString& Error)
{
	FMaterialAsset Material;
	if (!FMaterialInstance::Resolve(Parent, Candidate, Material, Error))
		return false;
	Overrides = std::move(Candidate);
	Resolved = std::move(Material);
	return true;
}
bool FMaterialInstanceDynamic::SetParameter(FMaterialParameterOverride Value, FString& Error)
{
	auto Candidate = Overrides;
	auto It = std::find_if(Candidate.Parameters.begin(), Candidate.Parameters.end(),
						   [&](const auto& P) { return P.Name == Value.Name; });
	if (It == Candidate.Parameters.end())
		Candidate.Parameters.push_back(std::move(Value));
	else
		*It = std::move(Value);
	return Commit(std::move(Candidate), Error);
}
bool FMaterialInstanceDynamic::SetScalarParameterValue(const FString& Name, float Value, FString& Error)
{
	return SetParameter({Name, 1, {Value, 0, 0, 0}}, Error);
}
bool FMaterialInstanceDynamic::SetVectorParameterValue(const FString& Name, const FLinearColor& Value,
													   FString& Error)
{
	return SetParameter({Name, 4, {Value.R, Value.G, Value.B, Value.A}}, Error);
}
bool FMaterialInstanceDynamic::SetTextureParameterValue(const FString& Name, const FAssetReference& Texture,
														FString& Error)
{
	auto Candidate = Overrides;
	auto It = std::find_if(Candidate.Textures.begin(), Candidate.Textures.end(),
						   [&](const auto& P) { return P.Name == Name; });
	if (It == Candidate.Textures.end())
		Candidate.Textures.push_back({Name, Texture});
	else
		It->Texture = Texture;
	return Commit(std::move(Candidate), Error);
}
bool FMaterialInstanceDynamic::ClearParameterValue(const FString& Name, FString& Error)
{
	auto Candidate = Overrides;
	std::erase_if(Candidate.Parameters, [&](const auto& P) { return P.Name == Name; });
	std::erase_if(Candidate.Textures, [&](const auto& P) { return P.Name == Name; });
	return Commit(std::move(Candidate), Error);
}
