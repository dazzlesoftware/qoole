// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "MaterialScriptBindings.h"
#include "MaterialEditingLibrary.h"
#include <filesystem>
#include <stdexcept>
namespace
{
using J = FJsonValue;
void Require(bool B, const char* E)
{
	if (!B)
		throw std::runtime_error(E);
}
J Settings(const FMaterialAsset& M)
{
	auto R = J::MakeObject();
	R["BaseColor"] = J::MakeArray();
	for (float V : M.BaseColor)
		R["BaseColor"].Push(V);
	R["Roughness"] = M.Roughness;
	R["Metallic"] = M.Metallic;
	R["BlendMode"] = M.BlendMode == EMaterialBlendMode::Opaque	 ? "Opaque"
					 : M.BlendMode == EMaterialBlendMode::Masked ? "Masked"
																 : "Translucent";
	R["OpacityMaskClip"] = M.OpacityMaskClip;
	R["BaseColorTexture"] = M.BaseColorTexture.ToJson();
	R["NormalTexture"] = M.NormalTexture.ToJson();
	return R;
}
bool ApplySettings(const J& A, FMaterialAsset& M, FString& E)
{
	if (!A.IsObject())
	{
		E = "Material settings must be an object.";
		return false;
	}
	auto Candidate = Settings(M);
	for (const auto& Key : A.GetKeys())
	{
		if (!Candidate.Contains(Key.View()))
		{
			E = "Unknown material setting: " + Key;
			return false;
		}
		Candidate[Key.View()] = A.Get(Key.View());
	}
	const auto& Color = Candidate.Get("BaseColor");
	Require(Color.IsArray() && Color.Num() == 4, "Material BaseColor must contain four numbers.");
	for (int I = 0; I < 4; ++I)
	{
		Require(Color[I].GetType() == EJsonType::Number, "Material BaseColor must contain numbers.");
		M.BaseColor[I] = Color[I].AsFloat();
	}
	for (const char* Key : {"Roughness", "Metallic", "OpacityMaskClip"})
		Require(Candidate.Get(Key).GetType() == EJsonType::Number, "Material setting must be a number.");
	M.Roughness = Candidate.Get("Roughness").AsFloat();
	M.Metallic = Candidate.Get("Metallic").AsFloat();
	M.OpacityMaskClip = Candidate.Get("OpacityMaskClip").AsFloat();
	const auto Blend = Candidate.Get("BlendMode").AsString();
	Require(Blend == "Opaque" || Blend == "Masked" || Blend == "Translucent", "Unknown blend mode.");
	M.BlendMode = Blend == "Opaque"	  ? EMaterialBlendMode::Opaque
				  : Blend == "Masked" ? EMaterialBlendMode::Masked
									  : EMaterialBlendMode::Translucent;
	auto ColorRef = FAssetReference::FromJson(Candidate.Get("BaseColorTexture"), E);
	if (!ColorRef)
		return false;
	auto NormalRef = FAssetReference::FromJson(Candidate.Get("NormalTexture"), E);
	if (!NormalRef)
		return false;
	M.BaseColorTexture = *ColorRef;
	M.NormalTexture = *NormalRef;
	FAssetPackage Validation;
	return FRenderAssetCodec::EncodeMaterial(M, Validation, E);
}
} // namespace
bool RegisterMaterialScriptBindings(FScriptBindingRegistry& Registry, FString& Error)
{
	TArray<FScriptBinding> Batch;
	Batch.push_back(
		{"Material.Describe", "Material.Read", true, {}, EJsonType::Object, [](const J&, J& R, FString&) {
			 R = J::MakeObject();
			 R["Expressions"] = J::MakeArray();
			 R["Outputs"] = J::MakeArray();
			 for (int I = 0; I < static_cast<int>(EMaterialExpression::Count); ++I)
			 {
				 const auto Type = static_cast<EMaterialExpression>(I);
				 auto V = J::MakeObject();
				 V["Id"] = I;
				 V["Name"] = MaterialExpressionName(Type);
				 V["Inputs"] = J::MakeArray();
				 for (int Input = 0; Input < MaterialInputCount(Type); ++Input)
					 V["Inputs"].Push(MaterialInputName(Type, Input));
				 R["Expressions"].Push(std::move(V));
			 }
			 for (int I = 0; I < static_cast<int>(EMaterialOutput::Count); ++I)
			 {
				 auto V = J::MakeObject();
				 V["Id"] = I;
				 V["Name"] = MaterialOutputName(static_cast<EMaterialOutput>(I));
				 R["Outputs"].Push(std::move(V));
			 }
			 return true;
		 }});
	Batch.push_back({"Material.CreateGraph",
					 "Material.Read",
					 true,
					 {{"Settings", EJsonType::Object, false}},
					 EJsonType::Object,
					 [](const J& A, J& R, FString& E) {
						 FMaterialAsset M;
						 if (A.Contains("Settings") && !ApplySettings(A.Get("Settings"), M, E))
							 return false;
						 R = J::MakeObject();
						 R["Settings"] = Settings(M);
						 R["Graph"] = FMaterialGraphCompiler::Encode(FMaterialEditingLibrary::CreateGraph(M));
						 return true;
					 }});
	Batch.push_back({"Material.Translate",
					 "Material.Read",
					 true,
					 {{"Graph", EJsonType::Object}},
					 EJsonType::Object,
					 [](const J& A, J& R, FString& E) {
						 FMaterialGraph G;
						 FMaterialGraphProgram Program;
						 if (!FMaterialGraphCompiler::Decode(A.Get("Graph"), G, E) ||
							 !FMaterialGraphCompiler::Compile(G, Program, E))
							 return false;
						 R = J::MakeObject();
						 R["PixelShaderSource"] = Program.PixelShaderSource;
						 R["Textures"] = J::MakeArray();
						 R["Parameters"] = J::MakeArray();
						 for (const auto& T : Program.Textures)
						 {
							 auto V = J::MakeObject();
							 V["Texture"] = T.Reference.ToJson();
							 V["Normal"] = T.bNormal;
							 V["ParameterName"] = T.ParameterName;
							 R["Textures"].Push(std::move(V));
						 }
						 for (const auto& P : Program.Parameters)
						 {
							 auto V = J::MakeObject();
							 V["Name"] = P.Name;
							 V["Width"] = P.Width;
							 V["Value"] = J::MakeArray();
							 for (float F : P.Value)
								 V["Value"].Push(F);
							 R["Parameters"].Push(std::move(V));
						 }
						 return true;
					 }});
	Batch.push_back({"Material.LoadGraph",
					 "Editor.Files",
					 true,
					 {{"Path", EJsonType::String}},
					 EJsonType::Object,
					 [](const J& A, J& R, FString& E) {
						 FAssetPackage P;
						 FMaterialAsset M;
						 if (!FAssetPackageStore::Load(A.Get("Path").AsString(), P, E) ||
							 !FRenderAssetCodec::DecodeMaterial(P, M, E))
							 return false;
						 R = J::MakeObject();
						 R["Id"] = P.Id;
						 R["Settings"] = Settings(M);
						 R["Graph"] = FMaterialGraphCompiler::Encode(FMaterialEditingLibrary::CreateGraph(M));
						 return true;
					 }});
	Batch.push_back(
		{"Material.CookGraph",
		 "Editor.Files",
		 true,
		 {{"Path", EJsonType::String}, {"Graph", EJsonType::Object}, {"Settings", EJsonType::Object, false}},
		 EJsonType::Object,
		 [](const J& A, J& R, FString& E) {
			 const auto Path = A.Get("Path").AsString();
			 FAssetPackage Package;
			 FMaterialAsset M;
			 if (std::filesystem::exists(std::filesystem::path(Path.ToWide())))
			 {
				 if (!FAssetPackageStore::Load(Path, Package, E) ||
					 !FRenderAssetCodec::DecodeMaterial(Package, M, E))
					 return false;
			 }
			 if (A.Contains("Settings") && !ApplySettings(A.Get("Settings"), M, E))
				 return false;
			 FMaterialGraph G;
			 if (!FMaterialGraphCompiler::Decode(A.Get("Graph"), G, E))
				 return false;
			 M.Graph = std::move(G);
			 if (!FMaterialEditingLibrary::CompileMaterial(M, E) ||
				 !FRenderAssetCodec::EncodeMaterial(M, Package, E) ||
				 !FAssetPackageStore::Save(Path, Package, E))
				 return false;
			 R = J::MakeObject();
			 R["Id"] = Package.Id;
			 R["FilePath"] = Path;
			 R["DXILBytes"] = static_cast<int>(M.GraphDXIL.size());
			 R["SPIRVBytes"] = static_cast<int>(M.GraphSPIRV.size());
			 return true;
		 }});
	return Registry.RegisterBatch(std::move(Batch), Error);
}
