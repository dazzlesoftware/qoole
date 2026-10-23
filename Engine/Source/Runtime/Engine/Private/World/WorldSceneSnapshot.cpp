// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "World/WorldSceneSnapshot.h"
#include "World/WorldGeometry.h"
#include "Landscape/Landscape.h"
#include "Foliage/Foliage.h"
#include <stdexcept>
#include <atomic>
namespace
{
std::atomic<uint64> NextRenderKey{1};
void Require(bool B, const char* Error)
{
	if (!B)
		throw std::runtime_error(Error);
}
} // namespace
bool FWorldSceneSnapshot::Load(const FAssetPackage& Package, const FAssetRegistry* Registry,
							   FWorldSceneSnapshot& Result, FString& Error)
{
	try
	{
		Require((Package.Schema == "Genesis.World" && Package.Kind == EAssetPackageKind::Map) ||
					(Package.Schema == "Genesis.Prefab" && Package.Kind == EAssetPackageKind::Asset),
				"Not a native world or prefab package.");
		Require(Package.SchemaVersion >= 2 && Package.SchemaVersion <= 3,
				"Unsupported native scene version; upgrade it through the editor.");
		auto World = FWorld::FromJson(Package.Exports.Get("Scene"), Error);
		if (!World)
			return false;
		FWorldSceneSnapshot Out;
		Out.Scene = std::move(*World);
		TMap<std::string, std::shared_ptr<const FLoadedMeshAsset>> Meshes;
		for (const auto& Actor : Out.Scene.GetActors())
		{
			Matrix44 ToWorld;
			if (!Out.Scene.GetWorldTransform(Actor.Id, ToWorld, Error))
				return false;
			for (const auto& C : Actor.Components)
			{
				const auto& Data = C.Properties.Get("Data");
				if (C.Type == "Genesis.LegacyEntity" && Data.Get("Class").AsString() == "worldspawn")
				{
					++Out.MetadataComponents;
					continue;
				}
				if (C.Type != "Genesis.StaticMesh" && C.Type != "Genesis.Landscape" && C.Type != "Genesis.Foliage")
				{
					Error = FString::Format("Runtime rendering does not support component {} ({}) on actor {}.", C.Type,
											C.Id, Actor.Name);
					return false;
				}
				TArray<FAssetReference> Expected;
				if (!FWorldGeometryCodec::GetAssetReferences(C.Type, Data, Expected, Error))
					return false;
				Require(Expected.size() == C.Assets.size(), "Component assets disagree with its payload.");
				for (size_t I = 0; I < Expected.size(); I++)
					Require(Expected[I].ToJson() == C.Assets[I].ToJson(),
							"Component asset identity disagrees with its payload.");
				FWorldSceneItem Item;
				Item.ActorId = Actor.Id;
				Item.ComponentId = C.Id;
				Item.ToWorld = ToWorld;
				Item.RenderKey = NextRenderKey.fetch_add(1, std::memory_order_relaxed);
				if (C.Type == "Genesis.StaticMesh")
				{
					const auto Key = Data.ToString(false).ToStdString();
					if (const auto It = Meshes.find(Key); It != Meshes.end())
						Item.Mesh = It->second;
					else
					{
						auto Asset = std::make_shared<FLoadedMeshAsset>();
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
							Require(Registry != nullptr, "A native mesh component needs a valid asset registry.");
							if (!FRenderAssetLoader(*Registry).LoadMesh(Definition.Asset, *Asset, Error,
																		Definition.MaterialOverride,
																		Definition.AdditionalOverrides))
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
					for (const auto& Spline : Item.Terrain->GetSplineMeshes())
						if (Spline)
						{
							auto Asset = std::make_shared<FLoadedMeshAsset>();
							Asset->Data.Mesh = Spline;
							Asset->DisplayMesh = Spline;
							Item.SplineMeshes.push_back(Asset);
						}
				}
				else
				{
					if (!FWorldGeometryCodec::DecodeFoliage(Package, Data, Item.Plants, Error))
						return false;
					for (const auto& I : Item.Plants->GetInstances())
						Require(Item.Plants->GetMesh(I.type) != nullptr, "Unknown foliage mesh.");
				}
				Out.Items.push_back(std::move(Item));
			}
		}
		Result = std::move(Out);
		Error.Empty();
		return true;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		return false;
	}
}
