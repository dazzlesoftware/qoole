// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "World/WorldSceneSnapshot.h"
#include "Misc/ValidationContext.h"
#include "Templates/SharedPointer.h"
#include "Internationalization/TextLocalization.h"
#include "World/WorldGeometry.h"
#include "Landscape/Landscape.h"
#include "Foliage/Foliage.h"

#include <atomic>
namespace
{
	std::atomic<uint64> NextRenderKey{1};

} // namespace
bool FWorldSceneSnapshot::Load(
    const FAssetPackage& Package, const FAssetRegistry* Registry, FWorldSceneSnapshot& Result, FString& Error)
{
	FValidationContext Validation(Error);

	if (!Validation.Check((Package.Schema == "Genesis.World" && Package.Kind == EAssetPackageKind::Map) ||
	            (Package.Schema == "Genesis.Prefab" && Package.Kind == EAssetPackageKind::Asset),
	        GENESIS_TEXT("Not a native world or prefab package.")))
	{
		return false;
	}

	if (!Validation.Check(Package.SchemaVersion >= 2 && Package.SchemaVersion <= 3,
	        GENESIS_TEXT("Unsupported native scene version; upgrade it through the editor.")))
	{
		return false;
	}

	TOptional<FWorld> World = FWorld::FromJson(Package.Exports.Get("Scene"), Error);
	if (!World)
		return false;
	FWorldSceneSnapshot Out;
	Out.Scene = std::move(*World);
	TMap<std::string, std::shared_ptr<const FLoadedMeshAsset>> Meshes;
	for (const FActor& Actor : Out.Scene.GetActors())
	{
		Matrix44 ToWorld;
		if (!Out.Scene.GetWorldTransform(Actor.Id, ToWorld, Error))
			return false;
		for (const FActorComponent& C : Actor.Components)
		{
			const FJsonValue& Data = C.Properties.Get("Data");
			// Point entities (worldspawn, player starts, lights) are gameplay data, not drawn.
			if (C.Type == "Genesis.LegacyEntity")
			{
				++Out.MetadataComponents;
				continue;
			}
			if (C.Type != "Genesis.StaticMesh" && C.Type != "Genesis.Landscape" && C.Type != "Genesis.Foliage")
			{
				Error = FString::Format(
				    "Runtime rendering does not support component {} ({}) on actor {}.", C.Type, C.Id, Actor.Name);
				return false;
			}
			TArray<FAssetReference> Expected;
			if (!FWorldGeometryCodec::GetAssetReferences(C.Type, Data, Expected, Error))
				return false;
			if (!Validation.Check(
			        Expected.size() == C.Assets.size(), GENESIS_TEXT("Component assets disagree with its payload.")))
			{
				return false;
			}

			for (size_t I = 0; I < Expected.size(); I++)
			{
				if (!Validation.Check(Expected[I].ToJson() == C.Assets[I].ToJson(),
				        GENESIS_TEXT("Component asset identity disagrees with its payload.")))
				{
					return false;
				}
			}
			FWorldSceneItem Item;
			Item.ActorId = Actor.Id;
			Item.ComponentId = C.Id;
			Item.ToWorld = ToWorld;
			Item.RenderKey = NextRenderKey.fetch_add(1, std::memory_order_relaxed);
			if (C.Type == "Genesis.StaticMesh")
			{
				const std::string Key = Data.ToString(false).ToStdString();
				if (const TMap<std::string, std::shared_ptr<const FLoadedMeshAsset>>::iterator It = Meshes.find(Key);
				    It != Meshes.end())
					Item.Mesh = It->second;
				else
				{
					TSharedPtr<FLoadedMeshAsset> Asset = std::make_shared<FLoadedMeshAsset>();
					FStaticMeshComponentData Definition;
					if (!FWorldGeometryCodec::DecodeMesh(Data, Definition, Error))
						return false;
					if (!Definition.IsNative())
					{
						Asset->Data.Mesh = StaticMesh::Find(Definition.BuiltIn.ToAnsi().c_str());
						Asset->DisplayMesh = Asset->Data.Mesh;
					}
					else
					{
						if (!Validation.Check(Registry != nullptr,
						        GENESIS_TEXT("A native mesh component needs a valid asset registry.")))
						{
							return false;
						}

						if (!FRenderAssetLoader(*Registry).LoadMesh(Definition.Asset, *Asset, Error,
						        Definition.MaterialOverride, Definition.AdditionalOverrides))
							return false;
					}
					Item.Mesh = Asset;
					Meshes[Key] = Asset;
				}
			}
			else if (C.Type == "Genesis.Landscape")
			{
				if (!FWorldGeometryCodec::DecodeLandscape(Package, Data, Item.Terrain, Error, Registry))
					return false;
				for (const std::shared_ptr<const StaticMesh>& Spline : Item.Terrain->GetSplineMeshes())
					if (Spline)
					{
						TSharedPtr<FLoadedMeshAsset> Asset = std::make_shared<FLoadedMeshAsset>();
						Asset->Data.Mesh = Spline;
						Asset->DisplayMesh = Spline;
						Item.SplineMeshes.push_back(Asset);
					}
			}
			else
			{
				if (!FWorldGeometryCodec::DecodeFoliage(Package, Data, Item.Plants, Error))
					return false;
				for (const FoliageInstance& I : Item.Plants->GetInstances())
				{
					if (!Validation.Check(
					        Item.Plants->GetMesh(I.type) != nullptr, GENESIS_TEXT("Unknown foliage mesh.")))
					{
						return false;
					}
				}
			}
			Out.Items.push_back(std::move(Item));
		}
	}
	Result = std::move(Out);
	Error.Empty();
	return true;
}
