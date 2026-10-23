// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
// This executable deliberately links no Editor, MapCore, UI or RHI libraries.
#include "Geometry/StaticMesh.h"
#include "Landscape/Landscape.h"
#include "Foliage/Foliage.h"
#include "World/World.h"
#include "Assets/RenderAssets.h"
#include <cstdio>
#include "Scripting/ScriptWorldSubsystem.h"
#include "Scripting/ScriptBindings.h"
#include "Scripting/WorldScriptBindings.h"
#include "HAL/File.h"
#include "ProjectDescriptor.h"

int wmain(int Argc, wchar_t** Argv)
{
	FString ScriptLanguage, ModulePath, ProjectPath;
	TOptional<FProjectContext> ScriptProject;
	for (int I = 1; I < Argc; ++I)
	{
		const auto A = FString::FromWide(Argv[I]);
		if (A.StartsWith("-script="))
			ScriptLanguage = A.Mid(8);
		if (A.StartsWith("-module="))
			ModulePath = A.Mid(8);
		if (A.StartsWith("-scriptproject="))
			ProjectPath = A.Mid(15);
	}
	if (!ProjectPath.IsEmpty())
	{
		FString Error;
		ScriptProject = FProjectContext::Open(ProjectPath, Error);
		if (!ScriptProject)
		{
			std::fprintf(stderr, "%s\n", *Error);
			return 19;
		}
		if (ScriptLanguage.IsEmpty())
			ScriptLanguage = ScriptProject->GetDescriptor().GameplayLanguage;
	}
	if (!ScriptLanguage.IsEmpty())
	{
		FString Error;
		FScriptRuntimeRegistry Providers;
		auto World = std::make_shared<FWorld>();
		auto Actor = FActor::New("Runtime Script Actor");
		auto Script = FActorComponent::New("Genesis.Script");
		Script.Properties["Module"] = "main";
		Actor.Components.push_back(Script);
		auto Host = std::make_shared<FScriptBindingRegistry>(
			EScriptPurpose::Gameplay, TArray<FString>{"API.Read", "Log.Write", "World.Read", "World.Write"});
		if (!RegisterBuiltinScriptProviders(Providers, Error) || !RegisterCoreScriptBindings(*Host, Error) ||
			!RegisterWorldScriptBindings(*Host, World, Error) || !World->AddActor(Actor, Error))
		{
			std::fprintf(stderr, "%s\n", *Error);
			return 20;
		}
		FScriptWorldSubsystem Scripts({ScriptLanguage, "Win64", EScriptPurpose::Gameplay}, World, Providers,
									  Host, [&](const FString& Name, TArray<uint8>& Payload, FString& E) {
										  if (ScriptProject)
											  return ScriptProject->LoadScriptModule(Name, Payload, E);
										  if (Name != "main" || !FFile::ReadAllBytes(ModulePath, Payload))
										  {
											  E = "Cannot read runtime script module.";
											  return false;
										  }
										  return true;
									  });
		if (!Scripts.BeginPlay(Error))
		{
			std::fprintf(stderr, "%s\n", *Error);
			return 21;
		}
		for (int I = 0; I < 3; ++I)
			if (!Scripts.Tick(1.0f / 60, Error))
			{
				std::fprintf(stderr, "%s\n", *Error);
				return 22;
			}
		if (World->FindActor(Actor.Id)->Name != "Script Tick 3")
			return 23;
		auto Event = FJsonValue::MakeObject();
		Event["Name"] = "Runtime event passed";
		if (!Scripts.SendEvent("Rename", Event, Error) ||
			World->FindActor(Actor.Id)->Name != "Runtime event passed" || !Scripts.EndPlay(Error))
		{
			std::fprintf(stderr, "%s\n", *Error);
			return 24;
		}
		std::printf("%s gameplay module: begin, tick, event and end passed without editor dependencies.\n",
					*ScriptLanguage);
		return 0;
	}
	Landscape Terrain(32, 32, 16);
	auto Grass = Terrain.MakeLayer("Grass", 80, 150, 50);
	Grass.weights.assign(33 * 33, 1);
	const auto Painted = Terrain.WithLayers({Grass});
	if (!Painted || Painted->GetMesh()->GetNumVertices() != 33 * 33)
		return 1;
	float Depth = 0;
	const Line Ray(Vector3d(0, 0, 100), Vector3d(0, 0, -1));
	if (!Painted->Raycast(Ray, 0, 1000, Depth))
		return 2;
	LandscapeSpline Road;
	Road.mesh = "Road";
	LandscapeSplinePoint A, B;
	A.position = Vector3d(-100, 0, 0);
	B.position = Vector3d(100, 0, 0);
	Road.points = {A, B};
	if (!Landscape::BuildSplineMesh(Road))
		return 3;
	FoliageType Tree;
	Tree.name = "Tree";
	Tree.mesh = "Tree";
	FoliageInstance Instance;
	Foliage Trees({Tree}, {Instance});
	Vector3d Low, High;
	if (!Trees.GetBounds(Low, High) || High.GetZ() <= 0)
		return 4;
	FString Error;
	FWorld World;
	auto Parent = FActor::New("Parent"), Child = FActor::New("Child");
	Child.ParentId = Parent.Id;
	Child.Root.Transform.Location = Vector3d(10, 20, 30);
	if (!World.AddActor(Parent, Error) || !World.AddActor(Child, Error))
		return 5;
	auto Back = FWorld::FromJson(World.ToJson(), Error);
	if (!Back || !Back->FindActor(Child.Id))
		return 6;
	if (Argc > 1)
	{
		FAssetPackage Package;
		if (!FAssetPackageStore::Load(FString::FromWide(Argv[1]), Package, Error) ||
			Package.Schema != "Genesis.World" || (Package.SchemaVersion < 2 || Package.SchemaVersion > 3))
		{
			std::fprintf(stderr, "Cannot load native runtime world: %s\n", *Error);
			return 7;
		}
		auto Loaded = FWorld::FromJson(Package.Exports.Get("Scene"), Error);
		if (!Loaded)
		{
			std::fprintf(stderr, "%s\n", *Error);
			return 8;
		}
		for (const auto& Actor : Loaded->GetActors())
		{
			Matrix44 Matrix;
			if (!Loaded->GetWorldTransform(Actor.Id, Matrix, Error))
				return 9;
		}
		std::printf("Native runtime scene: %zu actors loaded without editor modules.\n",
					Loaded->GetActors().size());
		if (Argc > 2)
		{
			FAssetRegistry Registry;
			if (!Registry.SetMounts({{"/Game", FString::FromWide(Argv[2])}}, Error) || !Registry.Scan())
				return 10;
			FRenderAssetLoader Loader(Registry);
			int Assets = 0;
			for (const auto& Actor : Loaded->GetActors())
				for (const auto& C : Actor.Components)
				{
					if (C.Type == "Genesis.StaticMesh" && !C.Assets.empty())
					{
						const auto& Data = C.Properties.Get("Data");
						const auto First = FAssetReference::FromJson(Data.Get("MaterialOverride"), Error);
						if (!First)
							return 11;
						TArray<FAssetReference> Additional;
						const auto& Values = Data.Get("AdditionalOverrides");
						if (!Values.IsNull() && (!Values.IsArray() || Values.Num() > 63))
							return 11;
						for (const auto& V : Values.GetItems())
						{
							auto Ref = FAssetReference::FromJson(V, Error);
							if (!Ref)
								return 11;
							Additional.push_back(*Ref);
						}
						FLoadedMeshAsset Mesh;
						if (!Loader.LoadMesh(C.Assets[0], Mesh, Error, *First, Additional))
						{
							std::fprintf(stderr, "%s\n", *Error);
							return 11;
						}
						++Assets;
					}
					else if (C.Type == "Genesis.Landscape")
						for (const auto& Ref : C.Assets)
						{
							FTextureAsset Texture;
							if (!Loader.LoadTexture(Ref, Texture, Error))
								return 12;
							++Assets;
						}
				}
			std::printf("Native mesh/material/texture dependencies: %d scene assets validated.\n", Assets);
		}
	}
	std::printf(
		"Runtime terrain, paint, raycast, spline mesh and foliage: passed. No editor dependencies.\n");
	return 0;
}
