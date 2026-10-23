// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "TestHelpers.h"
#include "Renderer/NativeMeshDrawList.h"
#include "Geometry/StaticMesh.h"
#include "Renderer/LandscapeRenderPlan.h"
#include "Renderer/FoliageRenderPlan.h"
#include "Renderer/ProjectedSceneRenderer.h"
#include "Renderer/LandscapeProjection.h"
#include "Renderer/WorldSceneDrawList.h"
#include "World/WorldGeometry.h"
#include "World/FoliageLandscapeAttachment.h"
#include "Foliage/Foliage.h"
#undef Min
#undef Max
#include <cmath>
#include <limits>
namespace
{
std::shared_ptr<FLoadedMeshAsset> CubeSnapshot(bool Transparent = false)
{
	auto Asset = std::make_shared<FLoadedMeshAsset>();
	Asset->Data.Mesh = StaticMesh::Find("Cube");
	Asset->Material = FMaterialAsset{};
	if (Transparent)
		Asset->Material->BlendMode = EMaterialBlendMode::Translucent;
	return Asset;
}
} // namespace
TEST_SUITE("NativeRenderer")
{
	TEST_CASE("Runtime draw lists own immutable assets and batch repeated opaque instances")
	{
		FString Error;
		FNativeMeshDrawList List;
		auto Asset = CubeSnapshot();
		std::weak_ptr<const FLoadedMeshAsset> Weak = Asset;
		Matrix44 View;
		View.SetTranslation(Vector3d(0, 400, 0));
		REQUIRE(List.Append(Asset, View, Error));
		REQUIRE_FALSE(List.Vertices.empty());
		REQUIRE(List.Batches.size() == 1);
		const auto Count = List.Vertices.size();
		View.SetTranslation(Vector3d(0, 700, 0));
		REQUIRE(List.Append(Asset, View, Error));
		CHECK(List.Vertices.size() == Count * 2);
		CHECK(List.Batches.size() == 1);
		CHECK(List.Batches[0].NumVertices == Count * 2);
		CHECK(List.GetDrawOrder(false).size() == 1);
		CHECK(List.GetDrawOrder(true).empty());
		Asset.reset();
		CHECK_FALSE(Weak.expired());
		List.Reset();
		CHECK(Weak.expired());
		CHECK(List.Vertices.empty());
	}
	TEST_CASE("Invalid runtime meshes roll back appended vertices and batches")
	{
		FString Error;
		FNativeMeshDrawList List;
		auto Asset = CubeSnapshot();
		Matrix44 View;
		View.SetTranslation(Vector3d(0, 400, 0));
		REQUIRE(List.Append(Asset, View, Error));
		const auto Count = List.Vertices.size(), Batches = List.Batches.size();
		const auto Tail = List.Batches.back().NumVertices;
		Matrix44 Singular;
		Singular.SetScale(Vector3d(0, 1, 1));
		CHECK_FALSE(List.Append(Asset, Singular, Error));
		auto Bad = std::make_shared<StaticMesh>("Bad");
		Bad->AddVertex(Vector3d(-1, 0, -1));
		Bad->AddVertex(Vector3d(1, 0, -1));
		Bad->AddVertex(Vector3d(0, 0, 1));
		Bad->AddFlatTriangle(0, 1, 2);
		Bad->SetTriangleMaterial(0, 3);
		Bad->Finish();
		auto Invalid = CubeSnapshot();
		Invalid->Data.Mesh = Bad;
		CHECK_FALSE(List.Append(Invalid, View, Error));
		Bad->SetTriangleMaterial(0, 0);
		const float UV[3][2] = {{0, 0}, {0, 0}, {std::numeric_limits<float>::quiet_NaN(), 0}};
		Bad->SetTriangleUVs(0, UV);
		CHECK_FALSE(List.Append(Invalid, View, Error));
		CHECK_FALSE(List.Append({}, View, Error));
		CHECK(List.Vertices.size() == Count);
		CHECK(List.Batches.size() == Batches);
		CHECK(List.Batches.back().NumVertices == Tail);
	}
	TEST_CASE("Runtime transparency sorts globally by depth and keeps equal depths stable")
	{
		FString Error;
		FNativeMeshDrawList List;
		auto Asset = CubeSnapshot(true);
		Matrix44 View;
		for (float Depth : {400.0f, 800.0f, 400.0f})
		{
			View.SetTranslation(Vector3d(0, Depth, 0));
			REQUIRE(List.Append(Asset, View, Error));
		}
		const auto Order = List.GetDrawOrder(true);
		REQUIRE(Order.size() == List.Batches.size());
		REQUIRE(Order.size() > 2);
		CHECK(List.GetDrawOrder(false).empty());
		for (size_t I = 1; I < Order.size(); ++I)
		{
			CHECK(List.Batches[Order[I - 1]].Depth >= List.Batches[Order[I]].Depth);
			if (List.Batches[Order[I - 1]].Depth == List.Batches[Order[I]].Depth)
				CHECK(Order[I - 1] < Order[I]);
		}
	}
	TEST_CASE("Runtime normal transforms preserve mirrors nonuniform scale and parent shear")
	{
		FString Error;
		FNativeMeshDrawList List;
		auto Asset = CubeSnapshot();
		Matrix44 Parent, Rotation, Child, View;
		Parent.SetScale(Vector3d(-2, 0.5f, 3));
		Rotation.SetRotateZ(0.4f);
		Parent.Multiply(Rotation);
		Child.SetRotateY(0.7f);
		View.SetTranslation(Vector3d(0, 500, 0));
		View.Multiply(Parent);
		View.Multiply(Child);
		REQUIRE(List.Append(Asset, View, Error));
		REQUIRE_FALSE(List.Vertices.empty());
		for (size_t I = 0; I < List.Vertices.size(); I += 3)
		{
			const auto& A = List.Vertices[I];
			const auto& B = List.Vertices[I + 1];
			Vector3d Edge(B.Position[0] - A.Position[0], B.Position[1] - A.Position[1], B.Position[2] - A.Position[2]);
			Edge.Normalize();
			Vector3d N(A.Normal[0], A.Normal[1], A.Normal[2]);
			CHECK(std::abs(N.DotVector(Edge)) < 0.0001f);
			CHECK(N.GetMag() == doctest::Approx(1));
		}
	}
}

TEST_SUITE("NativeRenderer")
{
	TEST_CASE("Runtime landscape plans cull components and reduce distant terrain")
	{
		Landscape Land(512, 512, 32);
		FSceneViewVolume View{Vector3d(-1, 1, -1), Vector3d(1, 65536, 1), true};
		Matrix44 Transform;
		Transform.SetIdentity();
		Transform.SetTranslation(Vector3d(0, 20000, 0));
		FLandscapeRenderPlan Far;
		PlanLandscape(Land, Transform, View, true, 256, 1, 8, Far);
		REQUIRE(Far.steps.size() == 256);
		CHECK(Far.Step(8, 8) > 1);
		CHECK(Far.Step(-1, 0) == 0);
		Transform.SetTranslation(Vector3d(0, 10000, 0));
		FLandscapeRenderPlan Near;
		PlanLandscape(Land, Transform, View, true, 256, 1, 8, Near);
		CHECK(Near.Step(8, 8) <= Far.Step(8, 8));
		Transform.SetTranslation(Vector3d(0, -20000, 0));
		PlanLandscape(Land, Transform, View, true, 256, 1, 8, Near);
		for (int Step : Near.steps)
			CHECK(Step == 0);
	}
	TEST_CASE("Runtime landscape orthographic visibility and zoom use screen dimensions")
	{
		Landscape Land(64, 32, 1);
		FSceneViewVolume View{Vector3d(-40, -100, -1), Vector3d(40, 100, 1), false};
		Matrix44 Transform;
		Transform.SetIdentity();
		FLandscapeRenderPlan Plan;
		PlanLandscape(Land, Transform, View, false, 256, 1, 8, Plan);
		CHECK(Plan.Step(0, 0) == 8);
		CHECK(Plan.Step(1, 0) == 8);
		PlanLandscape(Land, Transform, View, false, 256, 8, 8, Plan);
		CHECK(Plan.Step(0, 0) == 1);
		View.Minimum.SetX(1);
		PlanLandscape(Land, Transform, View, false, 256, 1, 8, Plan);
		CHECK(Plan.Step(0, 0) == 0);
		CHECK(Plan.Step(1, 0) > 0);
	}
	TEST_CASE("Runtime landscape components stitch fine edges to coarse neighbors")
	{
		std::vector<float> Heights(65 * 33);
		Heights[1 * 65 + 32] = 20;
		auto Land = Landscape::Create(64, 32, 1, Heights);
		REQUIRE(Land);
		FLandscapeRenderPlan Plan;
		Plan.cxs = 2;
		Plan.cys = 1;
		Plan.steps = {1, 4};
		Plan.bounds = {1, 1};
		Matrix44 Transform;
		Transform.SetIdentity();
		FLandscapeComponentMesh Fine, Coarse;
		BuildLandscapeComponent(*Land, Plan, 0, 0, Transform, true, Fine);
		BuildLandscapeComponent(*Land, Plan, 1, 0, Transform, true, Coarse);
		REQUIRE(Fine.Points.size() == 33 * 33);
		REQUIRE(Coarse.Points.size() == 9 * 9);
		CHECK(Fine.Points[1 * 33 + 32].GetZ() == doctest::Approx(0));
		for (int Row = 0; Row < 9; Row++)
		{
			const auto& A = Fine.Points[Row * 4 * 33 + 32];
			const auto& B = Coarse.Points[Row * 9];
			CHECK(A.GetX() == doctest::Approx(B.GetX()));
			CHECK(A.GetY() == doctest::Approx(B.GetY()));
			CHECK(A.GetZ() == doctest::Approx(B.GetZ()));
		}
		BuildLandscapeComponent(*Land, Plan, 0, 0, Transform, false, Fine);
		CHECK(Fine.Points[1 * 33 + 32].GetZ() == doctest::Approx(20));
		Plan.steps[0] = 0;
		BuildLandscapeComponent(*Land, Plan, 0, 0, Transform, true, Fine);
		CHECK(Fine.Points.empty());
		CHECK(Fine.X.empty());
	}
	TEST_CASE("Runtime landscape components retain partial boundary samples and transforms")
	{
		Landscape Land(35, 9, 2);
		FLandscapeRenderPlan Plan;
		Plan.cxs = 2;
		Plan.cys = 1;
		Plan.steps = {8, 8};
		Plan.bounds = {1, 1};
		Matrix44 Transform;
		Transform.SetIdentity();
		Transform.SetTranslation(Vector3d(10, 20, 30));
		FLandscapeComponentMesh Component;
		BuildLandscapeComponent(Land, Plan, 1, 0, Transform, true, Component);
		REQUIRE(Component.X.size() == 2);
		REQUIRE(Component.Y.size() == 3);
		CHECK(Component.X.back() == 35);
		CHECK(Component.Y.back() == 9);
		CHECK(Component.Points.back().GetX() == doctest::Approx(45));
		CHECK(Component.Points.back().GetY() == doctest::Approx(29));
		CHECK(Component.Points.back().GetZ() == doctest::Approx(30));
	}
}
TEST_SUITE("NativeRenderer")
{
	TEST_CASE("Runtime instance batching retains snapshots and rejects invalid transforms atomically")
	{
		TArray<FMeshInstanceBatch> Batches;
		auto Mesh = std::make_shared<StaticMesh>("Transient triangle");
		Mesh->AddVertex(Vector3d(0, 0, 0));
		Mesh->AddVertex(Vector3d(1, 0, 0));
		Mesh->AddVertex(Vector3d(0, 1, 0));
		Mesh->AddFlatTriangle(0, 1, 2);
		std::weak_ptr<const StaticMesh> Weak = Mesh;
		Matrix44 T;
		T.SetTranslation(Vector3d(0, 100, 0));
		REQUIRE(AppendMeshInstance(Batches, Mesh, T));
		REQUIRE(AppendMeshInstance(Batches, Mesh, T));
		REQUIRE(Batches.size() == 1);
		CHECK(Batches[0].Count() == 2);
		T.SetMatrix(0, 0, std::numeric_limits<float>::quiet_NaN());
		CHECK_FALSE(AppendMeshInstance(Batches, Mesh, T));
		T.SetScale(Vector3d(0, 1, 1));
		CHECK_FALSE(AppendMeshInstance(Batches, Mesh, T));
		CHECK(Batches[0].Count() == 2);
		T.SetScale(Vector3d(-2, 1, 1));
		REQUIRE(AppendMeshInstance(Batches, Mesh, T));
		REQUIRE(Batches.size() == 2);
		CHECK(Batches[1].bMirrored);
		CHECK_FALSE(Batches[0].bMirrored);

		Mesh.reset();
		CHECK_FALSE(Weak.expired());
		Batches.clear();
		CHECK(Weak.expired());
	}
	TEST_CASE("Runtime foliage culls by camera volume and type distance and emits shared mesh batches")
	{
		FoliageType Type;
		Type.mesh = "Cube";
		Type.cullDistance = 1000;
		FoliageInstance Near;
		Near.position = Vector3d(0, 400, 0);
		FoliageInstance Far = Near;
		Far.position = Vector3d(0, 3000, 0);
		FoliageInstance Behind = Near;
		Behind.position = Vector3d(0, -400, 0);
		FoliageInstance Side = Near;
		Side.position = Vector3d(4000, 400, 0);
		Foliage Data({Type}, {Near, Far, Behind, Side, Near});
		Matrix44 T;
		T.SetIdentity();
		FSceneViewVolume View{Vector3d(-1, 1, -1), Vector3d(1, 65536, 1), true};
		TArray<FMeshInstanceBatch> Batches;
		CHECK(BuildFoliageDrawList(Data, T, 1, View, Batches) == 2);
		REQUIRE(Batches.size() == 1);
		CHECK(Batches[0].Count() == 2);
		CHECK(Batches[0].Rows[7] == doctest::Approx(400));
		Matrix44 Placed;
		bool Clip = false;
		FoliageInstance Invalid = Near;
		Invalid.type = 5;
		CHECK_FALSE(IsFoliageInstanceVisible(Data, Invalid, T, 1, View, Placed, Clip));
		Invalid = Near;
		Invalid.scale = 0;
		CHECK_FALSE(IsFoliageInstanceVisible(Data, Invalid, T, 1, View, Placed, Clip));
		View.Perspective = false;
		View.Minimum = Vector3d(-100, -65536, -100);
		View.Maximum = Vector3d(100, 65536, 100);
		Batches.clear();
		// Orthographic editors show the entire depth range, without type distance culling.
		CHECK(BuildFoliageDrawList(Data, T, 1, View, Batches) == 4);
	}
	TEST_CASE("Runtime projected scenes reject malformed input and keep frames through cache eviction")
	{
		FRHIDeviceDesc Desc;
		Desc.Backend = ERHIBackend::D3D12;
		Desc.bSoftwareAdapter = true;
		auto Device = FRHIDevice::Create(Desc);
		REQUIRE(Device);
		{
			FProjectedSceneRenderer Renderer(*Device);
			FString Error;
			TArray<FProjectedVertex> V = {
				{0, 0, .01f, 0, 0, 0xFFFFFFFF}, {20, 0, .01f, 1, 0, 0xFFFFFFFF}, {0, 20, .01f, 0, 1, 0xFFFFFFFF}};
			TArray<FProjectedTriangleBatch> B = {{0, nullptr, 0, 3}};
			B[0].NumVertices = 4;
			CHECK_FALSE(Renderer.Prepare(V, B, {}, EPixelFormat::R8G8B8A8_UNORM, Error));
			CHECK_FALSE(Error.IsEmpty());
			B[0].NumVertices = 3;
			V[0].Q = 0;
			CHECK_FALSE(Renderer.Prepare(V, B, {}, EPixelFormat::R8G8B8A8_UNORM, Error));
			V[0].Q = .01f;
			FMeshInstanceBatch Invalid;
			Invalid.Mesh = StaticMesh::Find("Cube");
			Invalid.Rows = {1};
			CHECK_FALSE(Renderer.Prepare(V, B, {Invalid}, EPixelFormat::R8G8B8A8_UNORM, Error));
			Landscape Ground(4, 4, 32);
			auto Layer = Ground.MakeLayer("Grass", 50, 180, 40);
			Layer.texture = "builtin:Grass";
			auto Land = Ground.WithLayers({Layer});
			B[0].Terrain = Land;
			B[0].LandscapeKey = 9;
			Device->BeginFrame();
			auto Prepared = Renderer.Prepare(V, B, {}, EPixelFormat::R8G8B8A8_UNORM, Error);
			REQUIRE(Prepared);
			CHECK(Error.IsEmpty());
			CHECK(Renderer.GetNumLandscapeTextures() == 1);
			for (int I = 0; I < 302; I++)
				Renderer.Trim();
			CHECK(Renderer.GetNumLandscapeTextures() == 0);
			Device->EndFrame();
			Prepared.reset();
		}
		Device->WaitIdle();
	}
}
TEST_SUITE("NativeRenderer")
{
	TEST_CASE("Runtime landscape triangles retain valid UVs and light across view clipping")
	{
		struct FSink final : ILandscapeTriangleSink
		{
			std::vector<FLandscapeTriangle> Triangles;
			void OnTriangle(const FLandscapeTriangle& T) override
			{
				Triangles.push_back(T);
			}
		} Sink;
		Landscape Land(4, 4, 32);
		Matrix44 ToView;
		ToView.SetIdentity();
		ToView.SetMatrix(1, 1, 0);
		ToView.SetMatrix(1, 2, -1);
		ToView.SetMatrix(2, 1, 1);
		ToView.SetMatrix(2, 2, 0);
		ToView.SetMatrix(1, 3, 40);
		Matrix44 World;
		World.SetIdentity();
		FSceneViewVolume Volume{Vector3d(-1, 1, -1), Vector3d(1, 100, 1), true};
		FLandscapeDrawStats Stats;
		BuildLandscapeTriangles(Land, ToView, World, Vector3d(1, 1, 1), Volume, 256, 1, Sink, Stats);
		REQUIRE(Stats.Triangles > 0);
		REQUIRE(Stats.Components == 1);
		CHECK(Stats.Triangles == static_cast<int>(Sink.Triangles.size()));
		bool FoundClippedUV = false;
		for (const auto& T : Sink.Triangles)
			for (const auto& V : T.Vertices)
			{
				CHECK(V.Position.GetY() >= 1);
				CHECK(std::abs(V.Position.GetX()) <= V.Position.GetY() + .001f);
				CHECK(std::abs(V.Position.GetZ()) <= V.Position.GetY() + .001f);
				CHECK(V.U >= 0);
				CHECK(V.U <= 1);
				CHECK(V.V >= 0);
				CHECK(V.V <= 1);
				CHECK(std::isfinite(V.Shade));
				CHECK(V.Shade >= 0);
				CHECK(V.Shade <= 1);
				if (std::abs(V.U - .1875f) < .001f || std::abs(V.U - .8125f) < .001f)
					FoundClippedUV = true;
			}
		CHECK(FoundClippedUV);
		FProjectedSceneView View;
		View.Width = View.Height = 256;
		View.CenterX = View.CenterY = 128;
		View.ProjectionScale = 128;
		View.bLit = false;
		TArray<FProjectedVertex> Vertices;
		TArray<FProjectedTriangleBatch> Batches;
		auto Layer = Land.MakeLayer("Rock", 180, 160, 140);
		Layer.texture = "builtin:Rock";
		auto Painted = Land.WithLayers({Layer});
		AppendProjectedLandscape(Painted, 12, ToView, World, Vector3d(1, 1, 1), Volume, View, 1, Vertices, Batches,
								 Stats);
		REQUIRE(Batches.size() == 1);
		CHECK(Batches[0].Terrain == Painted);
		CHECK(Batches[0].NumVertices == Vertices.size());
		REQUIRE_FALSE(Vertices.empty());
		CHECK(Vertices[0].Color == 0xFFDBDBDB);
	}
}
TEST_SUITE("WorldGeometry")
{
	TEST_CASE("Native terrain and foliage payload codecs share bulk data without legacy objects")
	{
		FAssetPackage Package;
		FString Error;
		FJsonValue Data;
		Landscape Terrain(8, 4, 32);
		auto Layer = Terrain.MakeLayer("Grass", 50, 160, 80);
		Layer.texture = "builtin:Grass";
		Layer.weights[7] = .75f;
		LandscapeSpline Spline;
		LandscapeSplinePoint A, B;
		A.position = Vector3d(-10, 0, 0);
		B.position = Vector3d(10, 0, 0);
		A.autoTangent = false;
		A.tangent = Vector3d(2, 3, 4);
		A.joint = 3;
		Spline.points = {A, B};
		Spline.mesh = "Road";
		auto Painted = Terrain.WithLayers({Layer})->WithSplines({Spline});
		REQUIRE(FWorldGeometryCodec::EncodeLandscape(*Painted, Package, Data, Error));
		std::shared_ptr<const Landscape> Back;
		REQUIRE(FWorldGeometryCodec::DecodeLandscape(Package, Data, Back, Error));
		CHECK(Back->GetQuadsX() == 8);
		CHECK(Back->GetWeight(0, 7, 0) == doctest::Approx(.75));
		REQUIRE(Back->GetSplines().size() == 1);
		CHECK(Back->GetSplines()[0].points[0].joint == 3);
		CHECK(Back->GetSplines()[0].points[0].tangent.GetZ() == 4);
		const auto Previous = Back;
		auto Bad = Data;
		Bad["Heights"]["Offset"] = static_cast<double>(Package.Bulk.size());
		CHECK_FALSE(FWorldGeometryCodec::DecodeLandscape(Package, Bad, Back, Error));
		CHECK(Back == Previous);
		const auto Size = Package.Bulk.size();
		const auto OriginalData = Data;
		auto BadLayer = Layer;
		BadLayer.texture = "asset:invalid";
		auto Invalid = Terrain.WithLayers({BadLayer});
		CHECK_FALSE(FWorldGeometryCodec::EncodeLandscape(*Invalid, Package, Data, Error));
		CHECK(Package.Bulk.size() == Size);
		CHECK(Data == OriginalData);
		FoliageType Type;
		Type.mesh = "Cube";
		Type.name = "Cube plants";
		FoliageInstance I;
		I.position = Vector3d(1, 2, 3);
		I.scale = 2;
		Foliage Plants({Type}, {I});
		REQUIRE(FWorldGeometryCodec::EncodeFoliage(Plants, Package, Data, Error));
		std::shared_ptr<const Foliage> PlantsBack;
		REQUIRE(FWorldGeometryCodec::DecodeFoliage(Package, Data, PlantsBack, Error));
		REQUIRE(PlantsBack->GetNumInstances() == 1);
		CHECK(PlantsBack->GetInstances()[0].position.GetZ() == 3);
		CHECK(PlantsBack->GetInstances()[0].scale == 2);
	}
	TEST_CASE("Native world snapshots own render payloads, retain hierarchy and reject unsupported data atomically")
	{
		FAssetPackage Package;
		Package.Kind = EAssetPackageKind::Map;
		Package.Schema = "Genesis.World";
		Package.SchemaVersion = 3;
		FWorld Scene;
		FString Error;
		auto Parent = FActor::New("Parent");
		Parent.Root.Transform.Location = Vector3d(10, 20, 0);
		auto Metadata = FActorComponent::New("Genesis.LegacyEntity");
		Metadata.Properties["Data"]["Class"] = "worldspawn";
		Parent.Components.push_back(Metadata);
		REQUIRE(Scene.AddActor(Parent, Error));
		auto Cube = FActor::New("Cube");
		Cube.ParentId = Parent.Id;
		auto Mesh = FActorComponent::New("Genesis.StaticMesh");
		Mesh.Properties["Data"] = "Cube";
		Cube.Components.push_back(Mesh);
		REQUIRE(Scene.AddActor(Cube, Error));
		auto TerrainActor = FActor::New("Ground");
		Landscape Ground(8, 8, 32);
		auto Terrain = FActorComponent::New("Genesis.Landscape");
		FJsonValue Data;
		REQUIRE(FWorldGeometryCodec::EncodeLandscape(Ground, Package, Data, Error));
		Terrain.Properties["Data"] = Data;
		TerrainActor.Components.push_back(Terrain);
		REQUIRE(Scene.AddActor(TerrainActor, Error));
		FoliageType Type;
		Type.mesh = "Cube";
		FoliageInstance I;
		I.position = Vector3d(100, 0, 0);
		Foliage Plants({Type}, {I});
		auto PlantsActor = FActor::New("Plants");
		auto PlantsComponent = FActorComponent::New("Genesis.Foliage");
		REQUIRE(FWorldGeometryCodec::EncodeFoliage(Plants, Package, Data, Error));
		PlantsComponent.Properties["Data"] = Data;
		PlantsActor.Components.push_back(PlantsComponent);
		REQUIRE(Scene.AddActor(PlantsActor, Error));
		Package.Exports["Scene"] = Scene.ToJson();
		FWorldSceneSnapshot Snapshot;
		REQUIRE(FWorldSceneSnapshot::Load(Package, nullptr, Snapshot, Error));
		REQUIRE(Snapshot.GetItems().size() == 3);
		CHECK(Snapshot.GetMetadataComponents() == 1);
		CHECK(Snapshot.GetItems()[0].ToWorld.GetMatrix(0, 3) == 10);
		CHECK(Snapshot.GetItems()[0].ToWorld.GetMatrix(1, 3) == 20);
		CHECK(Snapshot.GetItems()[0].RenderKey != Snapshot.GetItems()[1].RenderKey);
		FProjectedSceneView View;
		View.Width = View.Height = 256;
		View.CenterX = View.CenterY = 128;
		View.ProjectionScale = 128;
		Matrix44 ToView;
		ToView.SetIdentity();
		ToView.SetMatrix(1, 1, 0);
		ToView.SetMatrix(1, 2, -1);
		ToView.SetMatrix(2, 1, 1);
		ToView.SetMatrix(2, 2, 0);
		ToView.SetMatrix(1, 3, 400);
		FSceneViewVolume Volume{Vector3d(-1, 1, -1), Vector3d(1, 2000, 1), true};
		FWorldSceneDrawList List;
		REQUIRE(List.Build(Snapshot, ToView, Volume, View, 8, Error));
		CHECK(List.TerrainTriangles > 0);
		CHECK(List.FoliageInstances == 1);
		CHECK_FALSE(List.Meshes.Vertices.empty());
		const auto PreviousVertices = List.Vertices.size();
		View.Width = 0;
		CHECK_FALSE(List.Build(Snapshot, ToView, Volume, View, 8, Error));
		CHECK(List.Vertices.size() == PreviousVertices);
		auto Unsupported = FActor::New("Brush");
		Unsupported.Components.push_back(FActorComponent::New("Genesis.LegacyBrush"));
		REQUIRE(Scene.AddActor(Unsupported, Error));
		Package.Exports["Scene"] = Scene.ToJson();
		CHECK_FALSE(FWorldSceneSnapshot::Load(Package, nullptr, Snapshot, Error));
		CHECK(Snapshot.GetItems().size() == 3);
		CHECK(Error.ToStdString().find("Genesis.LegacyBrush") != std::string::npos);
	}
}
TEST_SUITE("WorldGeometry")
{
	TEST_CASE("Native mesh component schema preserves sparse material override indices")
	{
		FString Error;
		FJsonValue Data;
		FStaticMeshComponentData BuiltIn;
		BuiltIn.BuiltIn = "Cube";
		REQUIRE(FWorldGeometryCodec::EncodeMesh(BuiltIn, Data, Error));
		CHECK(Data.AsString() == "Cube");
		FStaticMeshComponentData Back;
		REQUIRE(FWorldGeometryCodec::DecodeMesh(Data, Back, Error));
		CHECK_FALSE(Back.IsNative());
		FStaticMeshComponentData Native;
		Native.Asset = {FAssetId::New(), "/Game/TestMesh"};
		Native.MaterialOverride = {};
		Native.AdditionalOverrides = {{}, {FAssetId::New(), "/Game/SecondMaterial"}};
		REQUIRE(FWorldGeometryCodec::EncodeMesh(Native, Data, Error));
		REQUIRE(FWorldGeometryCodec::DecodeMesh(Data, Back, Error));
		REQUIRE(Back.AdditionalOverrides.size() == 2);
		CHECK(Back.AdditionalOverrides[0].IsNull());
		CHECK(Back.AdditionalOverrides[1].Id == Native.AdditionalOverrides[1].Id);
		TArray<FAssetReference> References;
		REQUIRE(FWorldGeometryCodec::GetAssetReferences("Genesis.StaticMesh", Data, References, Error));
		REQUIRE(References.size() == 2);
		CHECK(References[0].Id == Native.Asset.Id);
		CHECK(References[1].Id == Native.AdditionalOverrides[1].Id);
		auto Bad = Data;
		Bad["AdditionalOverrides"] = FJsonValue::MakeArray();
		Bad["AdditionalOverrides"].Push("invalid");
		CHECK_FALSE(FWorldGeometryCodec::DecodeMesh(Bad, Back, Error));
		CHECK(Back.Asset.Id == Native.Asset.Id);
		CHECK(Back.AdditionalOverrides.size() == 2);
		Native.AdditionalOverrides.resize(64);
		const auto Before = Data;
		CHECK_FALSE(FWorldGeometryCodec::EncodeMesh(Native, Data, Error));
		CHECK(Data == Before);
		BuiltIn.MaterialOverride = {FAssetId::New(), "/Game/Material"};
		CHECK_FALSE(FWorldGeometryCodec::EncodeMesh(BuiltIn, Data, Error));
		CHECK(Data == Before);
	}
}
TEST_SUITE("NativeRenderer")
{
	TEST_CASE("Native material preparation rejects invalid public draw data before upload")
	{
		FRHIDeviceDesc Desc;
		Desc.Backend = ERHIBackend::D3D12;
		Desc.bSoftwareAdapter = true;
		auto Device = FRHIDevice::Create(Desc);
		REQUIRE(Device);
		FNativeMeshRenderer Renderer(*Device);
		FNativeMeshDrawList List;
		FString Error;
		Matrix44 T;
		T.SetTranslation(Vector3d(0, 400, 0));
		REQUIRE(List.Append(CubeSnapshot(), T, Error));
		REQUIRE_FALSE(List.Vertices.empty());
		REQUIRE_FALSE(List.Batches.empty());
		const auto Original = List.Vertices[0];
		const auto Batch = List.Batches[0];
		List.Vertices[0].UV[0] = std::numeric_limits<float>::quiet_NaN();
		CHECK_FALSE(Renderer.Prepare(List, EPixelFormat::R8G8B8A8_UNORM, Error));
		CHECK_FALSE(Error.IsEmpty());
		List.Vertices[0] = Original;
		List.Vertices[0].Position[0] = std::numeric_limits<float>::infinity();
		CHECK_FALSE(Renderer.Prepare(List, EPixelFormat::R8G8B8A8_UNORM, Error));
		List.Vertices[0] = Original;
		List.Vertices[0].Normal[0] = std::numeric_limits<float>::quiet_NaN();
		CHECK_FALSE(Renderer.Prepare(List, EPixelFormat::R8G8B8A8_UNORM, Error));
		List.Vertices[0] = Original;
		List.Batches[0].Depth = std::numeric_limits<float>::quiet_NaN();
		CHECK_FALSE(Renderer.Prepare(List, EPixelFormat::R8G8B8A8_UNORM, Error));
		List.Batches[0] = Batch;
		List.Batches[0].NumVertices = std::numeric_limits<uint32>::max();
		CHECK_FALSE(Renderer.Prepare(List, EPixelFormat::R8G8B8A8_UNORM, Error));
		Device->WaitIdle();
	}
}

TEST_CASE("Runtime terrain attachment preserves floating foliage and local coordinate transforms")
{
	Landscape Before(4, 4, 16);
	const auto After = Before.WithHeights(std::vector<float>(25, 20.0f));
	REQUIRE(After);
	FoliageType Type;
	Type.mesh = "Grass";
	Type.zOffset = 3.0f;
	FoliageInstance Grounded, Floating, Outside;
	Grounded.position = Vector3d(0, 0, 3);
	Grounded.yaw = 0.75f;
	Grounded.scale = 1.5f;
	Floating.position = Vector3d(0, 0, 12);
	Outside.position = Vector3d(100, 0, 3);
	Foliage Plants({ Type }, { Grounded, Floating, Outside });
	Matrix44 ToTerrain, ToPlants;
	ToTerrain.SetScale(Vector3d(2, 1, 2));
	ToTerrain.SetMatrix(2, 3, -3);
	ToPlants.SetScale(Vector3d(0.5f, 1, 0.5f));
	ToPlants.SetMatrix(2, 3, 1.5f);
	const auto Snapped = SnapFoliageToHeightfield(Plants, Before, *After, ToTerrain, ToPlants);
	REQUIRE(Snapped);
	REQUIRE(Snapped->GetNumInstances() == 3);
	CHECK(Snapped->GetInstances()[0].position.GetZ() == doctest::Approx(13));
	CHECK(Snapped->GetInstances()[0].yaw == Grounded.yaw);
	CHECK(Snapped->GetInstances()[0].scale == Grounded.scale);
	CHECK(Snapped->GetInstances()[1].position.GetZ() == Floating.position.GetZ());
	CHECK(Snapped->GetInstances()[2].position.GetX() == Outside.position.GetX());
	CHECK(Snapped->GetInstances()[2].position.GetZ() == Outside.position.GetZ());
	CHECK(Plants.GetInstances()[0].position.GetZ() == Grounded.position.GetZ());
	CHECK_FALSE(SnapFoliageToHeightfield(Plants, Before, Before, ToTerrain, ToPlants));
	CHECK_FALSE(SnapFoliageToHeightfield(Plants, Before, *After, ToTerrain, ToPlants, -1));
	ToTerrain.SetMatrix(0, 0, std::numeric_limits<float>::quiet_NaN());
	CHECK_FALSE(SnapFoliageToHeightfield(Plants, Before, *After, ToTerrain, ToPlants));
}
