// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
// No Editor, MapCore, Slate or window/application modules.
#include "Renderer/NativeMeshRenderer.h"
#include "Renderer/ProjectedSceneRenderer.h"
#include "Renderer/LandscapeProjection.h"
#include "Renderer/WorldSceneDrawList.h"
#include "Foliage/Foliage.h"
#include "HAL/File.h"
#include "Geometry/StaticMesh.h"
#include "Renderer/LandscapeRenderPlan.h"
#undef Min
#undef Max
#include <cstdio>
#include <cmath>
#include <string_view>
#include <limits>
#include <algorithm>
int wmain(int Argc, wchar_t** Argv)
{
	const auto Usage = [] {
		std::printf("Usage: RuntimeRenderCheck.exe [-d3d12|-vulkan] [World.gmap ContentDirectory [Capture.bmp]]\n");
	};
	if (Argc > 1)
	{
		const std::wstring_view Option(Argv[1]);
		if (Option == L"--help" || Option == L"-h")
		{
			Usage();
			return 0;
		}
		if (Option != L"-d3d12" && Option != L"-vulkan")
		{
			Usage();
			return 1;
		}
	}
	if (Argc != 1 && Argc != 2 && Argc != 4 && Argc != 5)
	{
		Usage();
		return 1;
	}

	// Prove terrain preparation is usable with runtime dependencies alone.
	Landscape Terrain(512, 512, 32);
	Matrix44 TerrainToView;
	TerrainToView.SetTranslation(Vector3d(0, 20000, 0));
	FSceneViewVolume TerrainView{Vector3d(-1, 1, -1), Vector3d(1, 65536, 1), true};
	FLandscapeRenderPlan TerrainPlan;
	PlanLandscape(Terrain, TerrainToView, TerrainView, true, 256, 1, 8, TerrainPlan);
	FLandscapeComponentMesh Component;
	BuildLandscapeComponent(Terrain, TerrainPlan, 8, 8, TerrainToView, true, Component);
	if (Component.Points.empty() || Component.Points.size() > 1089 || TerrainPlan.Step(8, 8) <= 1)
	{
		std::fprintf(stderr, "Runtime landscape preparation failed.\n");
		return 7;
	}
	std::printf("Runtime landscape visibility, LOD and component preparation passed.\n");

	const auto Backend =
		Argc > 1 && std::wstring_view(Argv[1]) == L"-vulkan" ? ERHIBackend::Vulkan : ERHIBackend::D3D12;
	FRHIDeviceDesc DeviceDesc;
	DeviceDesc.Backend = Backend;
	DeviceDesc.bSoftwareAdapter = Backend == ERHIBackend::D3D12;
	auto Device = FRHIDevice::Create(DeviceDesc);
	if (!Device)
	{
		std::fprintf(stderr, "Runtime renderer backend unavailable.\n");
		return 1;
	}
	FNativeMeshRenderer Renderer(*Device);
	FString Error;
	FRHITextureDesc Desc;
	Desc.Width = Desc.Height = 256;
	Desc.bRenderTarget = true;
	auto Target = Device->CreateTexture(Desc);
	Desc.Format = EPixelFormat::D32_FLOAT;
	auto Depth = Device->CreateTexture(Desc);
	if (!Target || !Depth)
		return 2;
	for (auto Mode : {EMaterialBlendMode::Opaque, EMaterialBlendMode::Masked, EMaterialBlendMode::Translucent})
	{
		auto Asset = std::make_shared<FLoadedMeshAsset>();
		Asset->Data.Mesh = StaticMesh::Find("Cube");
		Asset->DisplayMesh = Asset->Data.Mesh;
		Asset->Material = FMaterialAsset{};
		Asset->Material->BaseColor[1] = Asset->Material->BaseColor[2] = 0;
		Asset->Material->BaseColor[3] = 0.5f;
		Asset->Material->BlendMode = Mode;
		Asset->Material->OpacityMaskClip = 0.6f;
		FNativeMeshDrawList List;
		Matrix44 Transform;
		Transform.SetTranslation(Vector3d(0, 400, 0));
		if (!List.Append(Asset, Transform, Error))
		{
			std::fprintf(stderr, "%s\n", *Error);
			return 3;
		}
		auto& Cmd = Device->BeginFrame();
		auto Prepared = Renderer.Prepare(List, Target->GetFormat(), Error);
		if (!Prepared)
		{
			std::fprintf(stderr, "%s\n", *Error);
			Device->EndFrame();
			return 4;
		}
		FNativeMeshView View;
		View.Width = View.Height = 256;
		View.ProjectionScale = 256;
		View.bLit = false;
		Cmd.BeginRenderPass(*Target, *Depth, FLinearColor(0, 0, 1, 1), 1.0f);
		Cmd.SetViewport(0, 0, 256, 256);
		Cmd.SetScissor(FIntRect(0, 0, 256, 256));
		Renderer.Draw(Cmd, *Prepared, View, false);
		Renderer.Draw(Cmd, *Prepared, View, true);
		Cmd.EndRenderPass();
		Device->EndFrame();
		TArray<uint8> Pixels;
		if (!Device->ReadbackTexture(*Target, Pixels) || Pixels.size() != 256 * 256 * 4)
			return 5;
		const size_t I = (128 * 256 + 128) * 4;
		const int Red = Pixels[I], Blue = Pixels[I + 2];
		const bool Good = Mode == EMaterialBlendMode::Opaque   ? (Red > 250 && Blue < 4)
						  : Mode == EMaterialBlendMode::Masked ? (Red < 4 && Blue > 250)
															   : (std::abs(Red - 128) < 4 && std::abs(Blue - 128) < 4);
		if (!Good)
		{
			std::fprintf(stderr, "Bad runtime pixel for mode %d: %d %d\n", static_cast<int>(Mode), Red, Blue);
			return 6;
		}
		Renderer.Trim();
	}
	FProjectedSceneRenderer Scene(*Device);
	Landscape Ground(4, 4, 32);
	auto Layer = Ground.MakeLayer("Test", 240, 30, 20);
	Layer.texture = "builtin:Snow";
	for (float& W : Layer.weights)
		W = 1;
	auto Painted = Ground.WithLayers({Layer});
	const TArray<FProjectedVertex> Quad = {{64, 64, .01f, 0, 0, 0xFFFFFFFF},   {192, 64, .01f, 1, 0, 0xFFFFFFFF},
										   {192, 192, .01f, 1, 1, 0xFFFFFFFF}, {64, 64, .01f, 0, 0, 0xFFFFFFFF},
										   {192, 192, .01f, 1, 1, 0xFFFFFFFF}, {64, 192, .01f, 0, 1, 0xFFFFFFFF}};
	FProjectedSceneView SceneView;
	SceneView.Width = SceneView.Height = 256;
	SceneView.CenterX = SceneView.CenterY = 128;
	SceneView.ProjectionScale = 256;
	SceneView.bLit = false;
	for (int Fixture = 0; Fixture < 7; Fixture++)
	{
		Scene.bBlendLandscapeLayers = Fixture != 1;
		FProjectedSceneView DrawView = SceneView;
		const bool IsInstance = Fixture == 2 || Fixture >= 5;
		TArray<FMeshInstanceBatch> Instances;
		TArray<FProjectedTriangleBatch> Batches;
		TArray<FProjectedVertex> FixtureVertices = IsInstance ? TArray<FProjectedVertex>{} : Quad;
		if (IsInstance)
		{

			std::shared_ptr<const StaticMesh> Mesh = StaticMesh::Find("Cube");
			Matrix44 T;
			T.SetTranslation(Vector3d(0, 400, 0));
			if (Fixture >= 5)
			{
				auto Triangle = std::make_shared<StaticMesh>("Scaled normal test");
				Triangle->AddVertex(Vector3d(-64, 0, -64));
				Triangle->AddVertex(Vector3d(64, 0, -64));
				Triangle->AddVertex(Vector3d(0, 0, 64));
				Vector3d N(1, -1, 0);
				N.MultVector(1.0f / N.GetMag());
				Triangle->AddTriangle(0, 1, 2, N, N, N);
				Mesh = Triangle;
				T.SetScale(Vector3d(Fixture == 5 ? -2.0f : 2.0f, 1, 1));
				T.SetMatrix(1, 3, 400);
				DrawView.bLit = true;
				DrawView.Light[0] = Fixture == 5 ? -1.0f : 1.0f;
				DrawView.Light[1] = DrawView.Light[2] = 0;
				DrawView.Up[0] = DrawView.Up[1] = 0;
				DrawView.Up[2] = 1;
			}
			if (!AppendMeshInstance(Instances, Mesh, T))
				return 20;
		}
		else
		{
			if (Fixture == 3)
			{
				Layer.r = 20;
				Layer.g = 240;
				Painted = Ground.WithLayers({Layer});
			}
			if (Fixture == 4)
			{
				Layer.r = 240;
				Layer.g = 30;
				Painted = Ground.WithLayers({Layer});
				Matrix44 ToView;
				ToView.SetIdentity();
				ToView.SetMatrix(1, 1, 0);
				ToView.SetMatrix(1, 2, -1);
				ToView.SetMatrix(2, 1, 1);
				ToView.SetMatrix(2, 2, 0);
				ToView.SetMatrix(1, 3, 200);
				Matrix44 ToWorld;
				ToWorld.SetIdentity();
				FixtureVertices.clear();
				FSceneViewVolume Volume{Vector3d(-1, 1, -1), Vector3d(1, 65536, 1), true};
				FLandscapeDrawStats Stats;
				AppendProjectedLandscape(Painted, 1, ToView, ToWorld, Vector3d(1, 1, 1), Volume, SceneView, 8,
										 FixtureVertices, Batches, Stats);
				if (Stats.Triangles == 0)
					return 11;
			}
			else
				Batches.push_back({1, Painted, 0, 6});
		}
		auto& Cmd = Device->BeginFrame();
		auto Prepared = Scene.Prepare(FixtureVertices, Batches, Instances, Target->GetFormat(), Error);
		if (!Prepared)
		{
			std::fprintf(stderr, "%s\n", *Error);
			Device->EndFrame();
			return 8;
		}
		// Frames must keep resources alive through cache eviction and snapshot destruction.
		if (Fixture == 0 || Fixture == 2)
			for (int I = 0; I < 302; I++)
				Scene.Trim();
		Cmd.BeginRenderPass(*Target, *Depth, FLinearColor(0, 0, 1, 1), 1.0f);
		Cmd.SetViewport(0, 0, 256, 256);
		Cmd.SetScissor(FIntRect(0, 0, 256, 256));
		Scene.DrawGeometry(Cmd, *Prepared, DrawView);
		Scene.DrawInstances(Cmd, *Prepared, DrawView);
		Cmd.EndRenderPass();
		Device->EndFrame();
		TArray<uint8> Pixels;
		if (!Device->ReadbackTexture(*Target, Pixels))
			return 9;
		const size_t I = (128 * 256 + 128) * 4;
		const int R = Pixels[I], G = Pixels[I + 1], B = Pixels[I + 2];
		const bool Good = Fixture >= 5	 ? (std::abs(R - 179) < 4 && std::abs(G - 179) < 4 && std::abs(B - 179) < 4)
						  : Fixture == 2 ? (std::abs(R - 219) < 4 && std::abs(G - 219) < 4 && std::abs(B - 219) < 4)
						  : Fixture == 3 ? (G > 180 && R < 40 && B < 40)
										 : (R > 180 && G < 40 && B < 40);
		if (!Good)
		{
			std::fprintf(stderr, "Bad scene fixture %d pixel: %d %d %d\n", Fixture, R, G, B);
			return 10;
		}
	}
	std::printf("%s: GPU landscape layers, baked fallback, layer refresh, instanced meshes and mirrored/nonuniform "
				"normals passed runtime-only readback.\n",
				ToString(Backend));

	if (Argc > 2)
	{
		auto Registry = std::make_shared<FAssetRegistry>();
		if (Argc < 4 || !Registry->SetMounts({{"/Game", FString::FromWide(Argv[3])}}, Error) || !Registry->Scan())
		{
			std::fprintf(stderr, "Supply a map and Content directory. %s\n", *Error);
			return 12;
		}
		FAssetPackage Package;
		FWorldSceneSnapshot World;
		if (!FAssetPackageStore::Load(FString::FromWide(Argv[2]), Package, Error) ||
			!FWorldSceneSnapshot::Load(Package, Registry.get(), World, Error))
		{
			std::fprintf(stderr, "Cannot construct runtime scene: %s\n", *Error);
			return 13;
		}
		FLandscapeTextureLibrary::SetAssetRegistry(Registry);
		Vector3d Low, High;
		bool First = true;
		for (const auto& Item : World.GetItems())
		{
			Vector3d A, B;
			bool HasBounds = true;
			if (Item.Mesh)
				Item.Mesh->Data.Mesh->GetBounds(A, B);
			else if (Item.Terrain)
				Item.Terrain->GetBounds(A, B);
			else if (Item.Plants)
				HasBounds = Item.Plants->GetBounds(A, B);
			else
				HasBounds = false;
			if (!HasBounds)
				continue;
			for (int I = 0; I < 8; I++)
			{
				Vector3d P;
				Item.ToWorld.Transform(P, Vector3d((I & 1) ? B.GetX() : A.GetX(), (I & 2) ? B.GetY() : A.GetY(),
												   (I & 4) ? B.GetZ() : A.GetZ()));
				if (First)
				{
					Low = High = P;
					First = false;
				}
				else
				{
					Low.NewVector(std::min(Low.GetX(), P.GetX()), std::min(Low.GetY(), P.GetY()),
								  std::min(Low.GetZ(), P.GetZ()));
					High.NewVector(std::max(High.GetX(), P.GetX()), std::max(High.GetY(), P.GetY()),
								   std::max(High.GetZ(), P.GetZ()));
				}
			}
		}
		if (First)
		{
			std::fprintf(stderr, "Scene has no renderable bounds.\n");
			return 14;
		}
		const float Span =
			std::max({High.GetX() - Low.GetX(), High.GetY() - Low.GetY(), High.GetZ() - Low.GetZ(), 128.0f});
		Matrix44 WorldToView;
		WorldToView.SetIdentity();
		WorldToView.SetMatrix(1, 1, 0);
		WorldToView.SetMatrix(1, 2, -1);
		WorldToView.SetMatrix(2, 1, 1);
		WorldToView.SetMatrix(2, 2, 0);
		WorldToView.SetMatrix(0, 3, -(High.GetX() + Low.GetX()) * .5f);
		WorldToView.SetMatrix(1, 3, High.GetZ() + Span * 1.3f);
		WorldToView.SetMatrix(2, 3, -(High.GetY() + Low.GetY()) * .5f);
		FSceneViewVolume Volume{Vector3d(-1, 1, -1), Vector3d(1, Span * 8, 1), true};
		FProjectedSceneView View = SceneView;
		View.bLit = true;
		View.Light[0] = .35f;
		View.Light[1] = -.79f;
		View.Light[2] = -.5f;
		View.Up[0] = 0;
		View.Up[1] = -1;
		View.Up[2] = 0;
		FWorldSceneDrawList List;
		if (!List.Build(World, WorldToView, Volume, View, 8, Error))
		{
			std::fprintf(stderr, "%s\n", *Error);
			return 15;
		}
		auto& Cmd = Device->BeginFrame();
		auto Geometry = Scene.Prepare(List.Vertices, List.Batches, List.Instances, Target->GetFormat(), Error);
		if (!Geometry)
		{
			std::fprintf(stderr, "%s\n", *Error);
			Device->EndFrame();
			return 16;
		}
		auto Materials = Renderer.Prepare(List.Meshes, Target->GetFormat(), Error);
		if (!Materials && !List.Meshes.Vertices.empty())
		{
			std::fprintf(stderr, "%s\n", *Error);
			Device->EndFrame();
			return 16;
		}
		Cmd.BeginRenderPass(*Target, *Depth, FLinearColor(0, 0, 0, 1), 1.0f);
		Cmd.SetViewport(0, 0, 256, 256);
		Cmd.SetScissor(FIntRect(0, 0, 256, 256));
		Scene.DrawGeometry(Cmd, *Geometry, View);
		if (Materials)
			Renderer.Draw(Cmd, *Materials, View, false);
		Scene.DrawInstances(Cmd, *Geometry, View);
		if (Materials)
			Renderer.Draw(Cmd, *Materials, View, true);
		Cmd.EndRenderPass();
		Device->EndFrame();
		TArray<uint8> Pixels;
		if (!Device->ReadbackTexture(*Target, Pixels))
			return 17;
		size_t Visible = 0;
		for (size_t I = 0; I < Pixels.size(); I += 4)
			if (Pixels[I] + Pixels[I + 1] + Pixels[I + 2] > 20)
				++Visible;
		if (Visible < 16)
		{
			std::fprintf(stderr, "Native scene readback is blank.\n");
			return 18;
		}
		if (Argc > 4)
		{
			// Top-down 32-bit BMP for a portable, dependency-free diagnostic artifact.
			TArray<uint8> BMP(54 + Pixels.size(), 0);
			const auto Put = [&](size_t At, uint32 Value, int Bytes) {
				for (int I = 0; I < Bytes; I++)
					BMP[At + I] = static_cast<uint8>(Value >> (8 * I));
			};
			BMP[0] = 'B';
			BMP[1] = 'M';
			Put(2, static_cast<uint32>(BMP.size()), 4);
			Put(10, 54, 4);
			Put(14, 40, 4);
			Put(18, 256, 4);
			Put(22, static_cast<uint32>(-256), 4);
			Put(26, 1, 2);
			Put(28, 32, 2);
			for (size_t I = 0; I < Pixels.size(); I += 4)
			{
				BMP[54 + I] = Pixels[I + 2];
				BMP[55 + I] = Pixels[I + 1];
				BMP[56 + I] = Pixels[I];
				BMP[57 + I] = 255;
			}
			FFile File;
			if (!File.Open(FString::FromWide(Argv[4]), EFileOpenMode::Write) ||
				File.Write(BMP.data(), static_cast<int64>(BMP.size())) != static_cast<int64>(BMP.size()))
				return 19;
		}
		std::printf("%s: native .gmap rendered: %zu scene items, %d terrain triangles, %u foliage instances, %zu "
					"visible pixels; %d worldspawn metadata components.\n",
					ToString(Backend), World.GetItems().size(), List.TerrainTriangles, List.FoliageInstances, Visible,
					World.GetMetadataComponents());
		FLandscapeTextureLibrary::SetAssetRegistry({});
	}

	Device->WaitIdle();
	std::printf("%s: native opaque, masked and translucent meshes rendered without editor/UI modules.\n",
				ToString(Backend));
	return 0;
}
