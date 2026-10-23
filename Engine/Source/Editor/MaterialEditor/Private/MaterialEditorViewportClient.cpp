// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "MaterialEditorViewportClient.h"
#include "Geometry/StaticMesh.h"

FMaterialEditorViewportClient::FMaterialEditorViewportClient(FRHIDevice& D, FSlateRHIRenderingPolicy& R)
	: FRenderTargetViewportClient(D, R), Meshes(D)
{
}

FMaterialEditorViewportClient::~FMaterialEditorViewportClient()
{
	if (Depth)
		Device.DeferRelease(Depth);
}

bool FMaterialEditorViewportClient::Update(const FMaterialAsset& M, const FAssetRegistry& Registry,
										   FString& Error)
{
	auto Next = std::make_shared<FLoadedMeshAsset>();
	Next->Data.Mesh = StaticMesh::Find(Shape.ToStdString().c_str());
	if (!Next->Data.Mesh)
	{
		Error = "Missing preview mesh.";
		return false;
	}
	if (!FRenderAssetLoader(Registry).LoadMaterialResources(M, *Next, Error))
		return false;
	Asset = Next;
	RequestRedraw();
	return true;
}

void FMaterialEditorViewportClient::SetShape(const FString& S)
{
	Shape = S;
}

void FMaterialEditorViewportClient::ToggleLit()
{
	bLit = !bLit;
	RequestRedraw();
}

void FMaterialEditorViewportClient::Draw(FRHICommandList& Cmd, FSlateRenderTargetRHI& RenderTarget)
{
	const auto W = RenderTarget.GetTexture().GetWidth(), H = RenderTarget.GetTexture().GetHeight();
	if (!Depth || Depth->GetWidth() != W || Depth->GetHeight() != H)
	{
		if (Depth)
			Device.DeferRelease(Depth);
		FRHITextureDesc TD;
		TD.Width = W;
		TD.Height = H;
		TD.Format = EPixelFormat::D32_FLOAT;
		TD.bRenderTarget = true;
		Depth = Device.CreateTexture(TD);
	}
	if (!Depth)
		return;
	FString Error;
	FNativeMeshDrawList List;
	Matrix44 T;
	T.SetTranslation(Vector3d(0, 220, 0));
	if (Asset)
		List.Append(Asset, T, Error);
	auto Frame = Meshes.Prepare(List, RenderTarget.GetTexture().GetFormat(), Error);
	Cmd.BeginRenderPass(RenderTarget.GetTexture(), *Depth, FLinearColor(.035f, .04f, .05f, 1), 1.f);
	if (Frame)
	{
		FNativeMeshView V;
		V.Width = W;
		V.Height = H;
		V.ProjectionScale = std::min(W, H) * .8f;
		V.bLit = bLit;
		Meshes.Draw(Cmd, *Frame, V, false);
		Meshes.Draw(Cmd, *Frame, V, true);
	}
	Cmd.EndRenderPass();
	Meshes.Trim();
}
