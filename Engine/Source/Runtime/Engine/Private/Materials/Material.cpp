// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Materials/Material.h"
#include "Assets/RenderAssetCodecPrivate.h"
#include <set>
#include <functional>
using namespace RenderAssetCodecPrivate;

bool FRenderAssetCodec::EncodeMaterial(const FMaterialAsset& In, FAssetPackage& Out, FString& Error)
{
	try
	{
		auto P = Package(Out, "Genesis.Material");
		P.SchemaVersion = 3;
		P.Exports["BaseColor"] = J::MakeArray();
		for (float V : In.BaseColor)
		{
			Need(std::isfinite(V) && V >= 0 && V <= 1, "Material color must be between zero and one.");
			P.Exports["BaseColor"].Push(V);
		}
		Need(std::isfinite(In.Roughness) && In.Roughness >= 0 && In.Roughness <= 1 &&
				 std::isfinite(In.Metallic) && In.Metallic >= 0 && In.Metallic <= 1,
			 "Invalid material surface values.");
		P.Exports["Roughness"] = In.Roughness;
		P.Exports["Metallic"] = In.Metallic;
		Need(static_cast<int>(In.BlendMode) >= 0 && static_cast<int>(In.BlendMode) <= 2 &&
				 std::isfinite(In.OpacityMaskClip) && In.OpacityMaskClip >= 0 && In.OpacityMaskClip <= 1,
			 "Invalid material blend settings.");
		P.Exports["BlendMode"] = static_cast<int>(In.BlendMode);
		P.Exports["OpacityMaskClip"] = In.OpacityMaskClip;
		P.Exports["BaseColorTexture"] = In.BaseColorTexture.ToJson();
		if (!In.Graph)
			Dependency(P, In.BaseColorTexture);
		P.Exports["NormalTexture"] = In.NormalTexture.ToJson();
		if (!In.Graph)
			Dependency(P, In.NormalTexture);
		if (In.Graph)
		{
			FMaterialGraphProgram Program;
			if (!FMaterialGraphCompiler::Compile(*In.Graph, Program, Error))
				return false;
			Need(In.CookedGraphSource == Program.PixelShaderSource,
				 "Material graph changed; compile before saving.");
			Need(!In.GraphDXIL.empty() && !In.GraphSPIRV.empty(),
				 "Compile the material graph before saving.");
			Need(In.GraphDXIL.size() <= 4 * 1024 * 1024 && In.GraphSPIRV.size() <= 4 * 1024 * 1024 &&
					 In.GraphSPIRV.size() % 4 == 0,
				 "Invalid cooked graph size.");
			Need(In.GraphDXIL.size() >= 4 && In.GraphSPIRV.size() >= 4 && In.GraphDXIL[0] == 'D' &&
					 In.GraphDXIL[1] == 'X' && In.GraphDXIL[2] == 'B' && In.GraphDXIL[3] == 'C' &&
					 In.GraphSPIRV[0] == 3 && In.GraphSPIRV[1] == 2 && In.GraphSPIRV[2] == 35 &&
					 In.GraphSPIRV[3] == 7,
				 "Invalid shader container.");
			P.SchemaVersion = 4;
			P.Exports["Graph"] = FMaterialGraphCompiler::Encode(*In.Graph);
			P.Exports["GraphSource"] = Program.PixelShaderSource;
			P.Exports["GraphDXILBytes"] = static_cast<int>(In.GraphDXIL.size());
			P.Exports["GraphSPIRVBytes"] = static_cast<int>(In.GraphSPIRV.size());
			P.Bulk = In.GraphDXIL;
			P.Bulk.insert(P.Bulk.end(), In.GraphSPIRV.begin(), In.GraphSPIRV.end());
			for (const auto& T : Program.Textures)
				Dependency(P, T.Reference);
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
bool FRenderAssetCodec::DecodeMaterial(const FAssetPackage& P, FMaterialAsset& Out, FString& Error)
{
	try
	{
		Need(P.Kind == EAssetPackageKind::Asset && P.Schema == "Genesis.Material" &&
				 (P.SchemaVersion >= 1 && P.SchemaVersion <= 4) && P.Exports.IsObject(),
			 "Unsupported material schema.");
		Need(P.SchemaVersion == 4 || P.Bulk.empty(), "Unexpected material bulk payload.");
		FMaterialAsset M;
		const auto& Color = P.Exports.Get("BaseColor");
		Need(Color.IsArray() && Color.Num() == 4, "Invalid material color.");
		for (int I = 0; I < 4; ++I)
			M.BaseColor[I] = Number(Color[I]);
		M.Roughness = Number(P.Exports.Get("Roughness"));
		M.Metallic = Number(P.Exports.Get("Metallic"));
		M.BaseColorTexture = Reference(P.Exports.Get("BaseColorTexture"));
		if (P.SchemaVersion >= 2)
			M.NormalTexture = Reference(P.Exports.Get("NormalTexture"));
		if (P.SchemaVersion >= 3)
		{
			const auto& Mode = P.Exports.Get("BlendMode");
			Need(Mode.GetType() == EJsonType::Number && Mode.AsNumber() >= 0 && Mode.AsNumber() <= 2 &&
					 std::floor(Mode.AsNumber()) == Mode.AsNumber(),
				 "Invalid material blend mode.");
			M.BlendMode = static_cast<EMaterialBlendMode>(static_cast<int>(Mode.AsNumber()));
			M.OpacityMaskClip = Number(P.Exports.Get("OpacityMaskClip"));
		}
		if (P.SchemaVersion == 4)
		{
			FMaterialGraph Graph;
			FMaterialGraphProgram Program;
			if (!FMaterialGraphCompiler::Decode(P.Exports.Get("Graph"), Graph, Error) ||
				!FMaterialGraphCompiler::Compile(Graph, Program, Error))
				return false;
			Need(P.Exports.Get("GraphSource").AsString() == Program.PixelShaderSource,
				 "Material graph cook is stale; recompile in the editor.");
			const auto D = Count(P.Exports.Get("GraphDXILBytes"), 4 * 1024 * 1024),
					   S = Count(P.Exports.Get("GraphSPIRVBytes"), 4 * 1024 * 1024);
			Need(D > 0 && S > 0 && S % 4 == 0 && P.Bulk.size() == static_cast<size_t>(D) + S,
				 "Invalid graph shader payload.");
			M.Graph = std::move(Graph);
			M.GraphDXIL.assign(P.Bulk.begin(), P.Bulk.begin() + D);
			M.GraphSPIRV.assign(P.Bulk.begin() + D, P.Bulk.end());
			M.CookedGraphSource = Program.PixelShaderSource;
			Need(D >= 4 && S >= 4 && M.GraphDXIL[0] == 'D' && M.GraphDXIL[1] == 'X' &&
					 M.GraphDXIL[2] == 'B' && M.GraphDXIL[3] == 'C' && M.GraphSPIRV[0] == 3 &&
					 M.GraphSPIRV[1] == 2 && M.GraphSPIRV[2] == 35 && M.GraphSPIRV[3] == 7,
				 "Invalid shader container.");
		}
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
bool FRenderAssetLoader::LoadMaterial(const FAssetReference& R, FMaterialAsset& Out, FString& Error) const
{

	std::set<FString> Visited;
	std::function<bool(const FAssetReference&, FMaterialAsset&, int)> Load =
		[&](const FAssetReference& Ref, FMaterialAsset& Result, int Depth) {
			if (Depth >= 32)
			{
				Error = "Material instance parent chain exceeds 32 assets.";
				return false;
			}
			FAssetPackage P;
			if (!Registry.Load(Ref, P, Error))
				return false;
			if (!Visited.insert(P.Id).second)
			{
				Error = "Material instance parent cycle.";
				return false;
			}
			if (P.Schema == "Genesis.Material")
				return FRenderAssetCodec::DecodeMaterial(P, Result, Error);
			FMaterialInstanceAsset Instance;
			if (!FRenderAssetCodec::DecodeMaterialInstance(P, Instance, Error))
				return false;
			FMaterialAsset Parent;
			if (!Load(Instance.Parent, Parent, Depth + 1))
				return false;
			return FMaterialInstance::Resolve(Parent, Instance, Result, Error);
		};
	FMaterialAsset Result;
	if (!Load(R, Result, 0))
		return false;
	Out = std::move(Result);
	Error.Empty();
	return true;
}
bool FRenderAssetLoader::LoadMaterialResources(const FMaterialAsset& M, FLoadedMaterialSlot& Out,
											   FString& Error) const
{
	FLoadedMaterialSlot Slot;
	Slot.Material = M;
	if (M.Graph)
	{
		FMaterialGraphProgram Program;
		if (!FMaterialGraphCompiler::Compile(*M.Graph, Program, Error))
			return false;
		for (const auto& Binding : Program.Textures)
		{
			FTextureAsset T;
			if (!LoadTexture(Binding.Reference, T, Error))
				return false;
			Slot.GraphTextures.push_back(std::move(T));
			Slot.GraphTextureNormals.push_back(Binding.bNormal);
		}
	}
	if (!M.Graph && !M.BaseColorTexture.IsNull())
	{
		FTextureAsset T;
		if (!LoadTexture(M.BaseColorTexture, T, Error))
			return false;
		Slot.Texture = std::move(T);
	}
	if (!M.Graph && !M.NormalTexture.IsNull())
	{
		FTextureAsset T;
		if (!LoadTexture(M.NormalTexture, T, Error))
			return false;
		Slot.NormalTexture = std::move(T);
	}
	Out = std::move(Slot);
	Error.Empty();
	return true;
}

bool IsMaterialAsset(const FAssetPackageMetadata& M)
{
	return M.Kind == EAssetPackageKind::Asset &&
		   ((M.Schema == "Genesis.Material" && M.SchemaVersion >= 1 && M.SchemaVersion <= 4) ||
			(M.Schema == "Genesis.MaterialInstance" && M.SchemaVersion >= 1 && M.SchemaVersion <= 2));
}
