// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Materials/Material.h"
#include "Misc/ValidationContext.h"
#include "Materials/MaterialLimits.h"
#include "Internationalization/TextLocalization.h"
#include "Materials/MaterialParameterCollection.h"
#include "Assets/RenderAssetCodecPrivate.h"
#include <set>
#include <functional>
using namespace RenderAssetCodecPrivate;

bool FRenderAssetCodec::EncodeMaterial(const FMaterialAsset& In, FAssetPackage& Out, FString& Error)
{
	FValidationContext Validation(Error);

	FAssetPackage P = Package(Out, "Genesis.Material");
	P.SchemaVersion = 3;
	P.Exports["BaseColor"] = J::MakeArray();
	for (float V : In.BaseColor)
	{
		if (!Validation.Check(
		        std::isfinite(V) && V >= 0 && V <= 1, GENESIS_TEXT("Material color must be between zero and one.")))
		{
			return false;
		}

		P.Exports["BaseColor"].Push(V);
	}
	if (!Validation.Check(std::isfinite(In.Roughness) && In.Roughness >= 0 && In.Roughness <= 1 &&
	            std::isfinite(In.Metallic) && In.Metallic >= 0 && In.Metallic <= 1,
	        GENESIS_TEXT("Invalid material surface values.")))
	{
		return false;
	}

	P.Exports["Roughness"] = In.Roughness;
	P.Exports["Metallic"] = In.Metallic;
	if (!Validation.Check(static_cast<int>(In.BlendMode) >= 0 && static_cast<int>(In.BlendMode) <= 2 &&
	            std::isfinite(In.OpacityMaskClip) && In.OpacityMaskClip >= 0 && In.OpacityMaskClip <= 1,
	        GENESIS_TEXT("Invalid material blend settings.")))
	{
		return false;
	}

	P.Exports["BlendMode"] = static_cast<int>(In.BlendMode);
	P.Exports["OpacityMaskClip"] = In.OpacityMaskClip;
	P.Exports["BaseColorTexture"] = In.BaseColorTexture.ToJson();
	if (!In.Graph)
		Dependency(Validation, P, In.BaseColorTexture);
	if (!Validation.IsValid())
	{
		return false;
	}

	P.Exports["NormalTexture"] = In.NormalTexture.ToJson();
	if (!In.Graph)
		Dependency(Validation, P, In.NormalTexture);
	if (!Validation.IsValid())
	{
		return false;
	}

	if (In.Graph)
	{
		if (!Validation.Check(!In.Graph->bFunction, GENESIS_TEXT("Save function graphs as material function assets.")))
		{
			return false;
		}

		FMaterialGraphProgram Program;
		if (!FMaterialGraphCompiler::Compile(*In.Graph, Program, Error))
			return false;
		if (!Validation.Check(In.CookedGraphSource == Program.PixelShaderSource,
		        GENESIS_TEXT("Material graph changed; compile before saving.")))
		{
			return false;
		}

		if (!Validation.Check(!In.GraphDXIL.empty() && !In.GraphSPIRV.empty(),
		        GENESIS_TEXT("Compile the material graph before saving.")))
		{
			return false;
		}

		if (!Validation.Check(In.GraphDXIL.size() <= MaterialLimits::MaxShaderBytes &&
		            In.GraphSPIRV.size() <= MaterialLimits::MaxShaderBytes && In.GraphSPIRV.size() % 4 == 0,
		        GENESIS_TEXT("Invalid cooked graph size.")))
		{
			return false;
		}

		if (!Validation.Check(In.GraphDXIL.size() >= 4 && In.GraphSPIRV.size() >= 4 && In.GraphDXIL[0] == 'D' &&
		            In.GraphDXIL[1] == 'X' && In.GraphDXIL[2] == 'B' && In.GraphDXIL[3] == 'C' &&
		            In.GraphSPIRV[0] == 3 && In.GraphSPIRV[1] == 2 && In.GraphSPIRV[2] == 35 && In.GraphSPIRV[3] == 7,
		        GENESIS_TEXT("Invalid shader container.")))
		{
			return false;
		}

		P.SchemaVersion = 4;
		if (!FMaterialGraphCompiler::Encode(*In.Graph, P.Exports["Graph"], Error))
		{
			return false;
		}
		P.Exports["GraphSource"] = Program.PixelShaderSource;
		P.Exports["GraphDXILBytes"] = static_cast<int>(In.GraphDXIL.size());
		P.Exports["GraphSPIRVBytes"] = static_cast<int>(In.GraphSPIRV.size());
		P.Bulk = In.GraphDXIL;
		P.Bulk.insert(P.Bulk.end(), In.GraphSPIRV.begin(), In.GraphSPIRV.end());
		std::function<void(const FMaterialGraph&)> FunctionDependencies = [&](const FMaterialGraph& G)
		{
			for (const FMaterialNode& N : G.Nodes)
			{
				Dependency(Validation, P, N.Function);
				if (!Validation.IsValid())
				{
					return;
				}

				Dependency(Validation, P, N.Collection);
				if (!Validation.IsValid())
				{
					return;
				}
			}
			for (const FMaterialFunctionSnapshot& F : G.Functions)
			{
				if (F.Graph)
					FunctionDependencies(*F.Graph);
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
		FunctionDependencies(*In.Graph);
		if (!Validation.IsValid())
		{
			return false;
		}

		for (const FMaterialGraphTexture& T : Program.Textures)
		{
			Dependency(Validation, P, T.Reference);
			if (!Validation.IsValid())
			{
				return {};
			}
		}
		if (!Validation.IsValid())
		{
			return false;
		}
	}
	Out = std::move(P);
	Error.Empty();
	return true;
}
bool FRenderAssetCodec::DecodeMaterial(const FAssetPackage& P, FMaterialAsset& Out, FString& Error)
{
	FValidationContext Validation(Error);

	if (!Validation.Check(P.Kind == EAssetPackageKind::Asset && P.Schema == "Genesis.Material" &&
	            (P.SchemaVersion >= 1 && P.SchemaVersion <= 4) && P.Exports.IsObject(),
	        GENESIS_TEXT("Unsupported material schema.")))
	{
		return false;
	}

	if (!Validation.Check(P.SchemaVersion == 4 || P.Bulk.empty(), GENESIS_TEXT("Unexpected material bulk payload.")))
	{
		return false;
	}

	FMaterialAsset M;
	const FJsonValue& Color = P.Exports.Get("BaseColor");
	if (!Validation.Check(Color.IsArray() && Color.Num() == 4, GENESIS_TEXT("Invalid material color.")))
	{
		return false;
	}

	for (int I = 0; I < 4; ++I)
	{
		M.BaseColor[I] = Number(Validation, Color[I]);
		if (!Validation.IsValid())
		{
			return {};
		}
	}
	if (!Validation.IsValid())
	{
		return false;
	}

	M.Roughness = Number(Validation, P.Exports.Get("Roughness"));
	if (!Validation.IsValid())
	{
		return false;
	}

	M.Metallic = Number(Validation, P.Exports.Get("Metallic"));
	if (!Validation.IsValid())
	{
		return false;
	}

	M.BaseColorTexture = Reference(Validation, P.Exports.Get("BaseColorTexture"));
	if (!Validation.IsValid())
	{
		return false;
	}

	if (P.SchemaVersion >= 2)
		M.NormalTexture = Reference(Validation, P.Exports.Get("NormalTexture"));
	if (!Validation.IsValid())
	{
		return false;
	}

	if (P.SchemaVersion >= 3)
	{
		const FJsonValue& Mode = P.Exports.Get("BlendMode");
		if (!Validation.Check(Mode.GetType() == EJsonType::Number && Mode.AsNumber() >= 0 && Mode.AsNumber() <= 2 &&
		            std::floor(Mode.AsNumber()) == Mode.AsNumber(),
		        GENESIS_TEXT("Invalid material blend mode.")))
		{
			return false;
		}

		M.BlendMode = static_cast<EMaterialBlendMode>(static_cast<int>(Mode.AsNumber()));
		M.OpacityMaskClip = Number(Validation, P.Exports.Get("OpacityMaskClip"));
		if (!Validation.IsValid())
		{
			return false;
		}
	}
	if (P.SchemaVersion == 4)
	{
		FMaterialGraph Graph;
		FMaterialGraphProgram Program;
		if (!FMaterialGraphCompiler::Decode(P.Exports.Get("Graph"), Graph, Error) ||
		    !FMaterialGraphCompiler::Compile(Graph, Program, Error))
			return false;
		if (!Validation.Check(
		        !Graph.bFunction, GENESIS_TEXT("A surface material cannot contain a function graph root.")))
		{
			return false;
		}

		if (!Validation.Check(P.Exports.Get("GraphSource").AsString() == Program.PixelShaderSource,
		        GENESIS_TEXT("Material graph cook is stale; recompile in the editor.")))
		{
			return false;
		}

		const int D = Count(Validation, P.Exports.Get("GraphDXILBytes"), MaterialLimits::MaxShaderBytes),
		          S = Count(Validation, P.Exports.Get("GraphSPIRVBytes"), MaterialLimits::MaxShaderBytes);
		if (!Validation.IsValid())
		{
			return false;
		}

		if (!Validation.Check(D > 0 && S > 0 && S % 4 == 0 && P.Bulk.size() == static_cast<size_t>(D) + S,
		        GENESIS_TEXT("Invalid graph shader payload.")))
		{
			return false;
		}

		M.Graph = std::move(Graph);
		M.GraphDXIL.assign(P.Bulk.begin(), P.Bulk.begin() + D);
		M.GraphSPIRV.assign(P.Bulk.begin() + D, P.Bulk.end());
		M.CookedGraphSource = Program.PixelShaderSource;
		if (!Validation.Check(D >= 4 && S >= 4 && M.GraphDXIL[0] == 'D' && M.GraphDXIL[1] == 'X' &&
		            M.GraphDXIL[2] == 'B' && M.GraphDXIL[3] == 'C' && M.GraphSPIRV[0] == 3 && M.GraphSPIRV[1] == 2 &&
		            M.GraphSPIRV[2] == 35 && M.GraphSPIRV[3] == 7,
		        GENESIS_TEXT("Invalid shader container.")))
		{
			return false;
		}
	}
	Out = std::move(M);
	Error.Empty();
	return true;
}
bool FRenderAssetLoader::LoadMaterial(const FAssetReference& R, FMaterialAsset& Out, FString& Error) const
{

	std::set<FString> Visited;
	std::function<bool(const FAssetReference&, FMaterialAsset&, int)> Load =
	    [&](const FAssetReference& Ref, FMaterialAsset& Result, int Depth)
	{
		if (Depth >= MaterialLimits::MaxParentDepth)
		{
			Error = GENESIS_TEXT("Material instance parent chain exceeds 32 assets.");
			return false;
		}
		FAssetPackage P;
		if (!Registry.Load(Ref, P, Error))
			return false;
		if (!Visited.insert(P.Id).second)
		{
			Error = GENESIS_TEXT("Material instance parent cycle.");
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
	if (Result.Graph)
	{
		bool Changed = false;
		if (!FMaterialParameterCollectionCodec::Refresh(*Result.Graph, Registry, Changed, Error))
			return false;
	}
	Out = std::move(Result);
	Error.Empty();
	return true;
}
bool FRenderAssetLoader::LoadMaterialResources(const FMaterialAsset& M, FLoadedMaterialSlot& Out, FString& Error) const
{
	FLoadedMaterialSlot Slot;
	Slot.Material = M;
	if (M.Graph)
	{
		FMaterialGraphProgram Program;
		if (!FMaterialGraphCompiler::Compile(*M.Graph, Program, Error))
			return false;
		for (const FMaterialGraphTexture& Binding : Program.Textures)
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
	        (M.Schema == "Genesis.MaterialInstance" && M.SchemaVersion >= 1 && M.SchemaVersion <= 3));
}
