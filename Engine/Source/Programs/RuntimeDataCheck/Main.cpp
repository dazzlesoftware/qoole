// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
// This executable deliberately links no Editor, MapCore, UI or RHI libraries.
#include "Geometry/StaticMesh.h"
#include "Landscape/Landscape.h"
#include "Foliage/Foliage.h"
#include "World/World.h"
#include "Assets/RenderAssets.h"
#include <cstdio>

int wmain(int Argc, wchar_t** Argv)
{
	Landscape Terrain(32, 32, 16);
	auto Grass = Terrain.MakeLayer("Grass", 80, 150, 50);
	Grass.weights.assign(33 * 33, 1);
	const auto Painted = Terrain.WithLayers({ Grass });
	if (!Painted || Painted->GetMesh()->GetNumVertices() != 33 * 33) return 1;
	float Depth = 0;
	const Line Ray(Vector3d(0, 0, 100), Vector3d(0, 0, -1));
	if (!Painted->Raycast(Ray, 0, 1000, Depth)) return 2;
	LandscapeSpline Road;
	Road.mesh = "Road";
	LandscapeSplinePoint A, B;
	A.position = Vector3d(-100, 0, 0);
	B.position = Vector3d(100, 0, 0);
	Road.points = { A, B };
	if (!Landscape::BuildSplineMesh(Road)) return 3;
	FoliageType Tree;
	Tree.name = "Tree"; Tree.mesh = "Tree";
	FoliageInstance Instance;
	Foliage Trees({ Tree }, { Instance });
	Vector3d Low, High;
	if (!Trees.GetBounds(Low, High) || High.GetZ() <= 0) return 4;
	FString Error;
	FWorld World; auto Parent = FActor::New("Parent"), Child = FActor::New("Child");
	Child.ParentId = Parent.Id; Child.Root.Transform.Location = Vector3d(10,20,30);
	if (!World.AddActor(Parent, Error) || !World.AddActor(Child, Error)) return 5;
	auto Back = FWorld::FromJson(World.ToJson(), Error);
	if (!Back || !Back->FindActor(Child.Id)) return 6;
	if (Argc > 1)
	{
		FAssetPackage Package;
		if (!FAssetPackageStore::Load(FString::FromWide(Argv[1]), Package, Error) || Package.Schema != "Genesis.World" || Package.SchemaVersion != 2)
		{ std::fprintf(stderr, "Cannot load native runtime world: %s\n", *Error); return 7; }
		auto Loaded = FWorld::FromJson(Package.Exports.Get("Scene"), Error);
		if (!Loaded) { std::fprintf(stderr, "%s\n", *Error); return 8; }
		for (const auto& Actor : Loaded->GetActors())
		{ Matrix44 Matrix; if (!Loaded->GetWorldTransform(Actor.Id, Matrix, Error)) return 9; }
		std::printf("Native runtime scene: %zu actors loaded without editor modules.\n", Loaded->GetActors().size());
		if(Argc>2) {
			FAssetRegistry Registry; if(!Registry.SetMounts({{"/Game",FString::FromWide(Argv[2])}},Error) || !Registry.Scan()) return 10;
			FRenderAssetLoader Loader(Registry); int Assets=0;
			for(const auto& Actor:Loaded->GetActors()) for(const auto& C:Actor.Components) {
				if(C.Type=="Genesis.StaticMesh" && !C.Assets.empty()) {
					FLoadedMeshAsset Mesh; if(!Loader.LoadMesh(C.Assets[0],Mesh,Error,C.Assets.size()>1?C.Assets[1]:FAssetReference{})) { std::fprintf(stderr,"%s\n",*Error); return 11; } ++Assets;
				} else if(C.Type=="Genesis.Landscape") for(const auto& Ref:C.Assets) { FTextureAsset Texture; if(!Loader.LoadTexture(Ref,Texture,Error)) return 12; ++Assets; }
			}
			std::printf("Native mesh/material/texture dependencies: %d scene assets validated.\n",Assets);
		}
	}
	std::printf("Runtime terrain, paint, raycast, spline mesh and foliage: passed. No editor dependencies.\n");
	return 0;
}
