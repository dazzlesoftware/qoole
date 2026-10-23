// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "MaterialEditorViewportClient.h"
#include "Templates/SharedPointer.h"
#include "Internationalization/TextLocalization.h"
#include "Geometry/StaticMesh.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Text/STextBlock.h"
#include <cmath>

FMaterialEditorViewportClient::FMaterialEditorViewportClient(FRHIDevice& D, FSlateRHIRenderingPolicy& R)
    : FRenderTargetViewportClient(D, R), Meshes(D)
{
}

FMaterialEditorViewportClient::~FMaterialEditorViewportClient()
{
	if (Depth)
		Device.DeferRelease(Depth);
}

bool FMaterialEditorViewportClient::Update(const FMaterialAsset& M, const FAssetRegistry& Registry, FString& Error)
{
	TSharedPtr<FLoadedMeshAsset> Next = std::make_shared<FLoadedMeshAsset>();
	Next->Data.Mesh = CustomMesh ? CustomMesh : StaticMesh::Find(Shape.ToStdString().c_str());
	if (!Next->Data.Mesh)
	{
		Error = GENESIS_TEXT("Missing preview mesh.");
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
	CustomMesh.reset();
	Pitch = S == "Plane" ? 90.f : 0.f;
	Yaw = 0;
}

void FMaterialEditorViewportClient::ToggleLit()
{
	bLit = !bLit;
	RequestRedraw();
}

void FMaterialEditorViewportClient::Draw(FRHICommandList& Cmd, FSlateRenderTargetRHI& RenderTarget)
{
	const uint32 W = RenderTarget.GetTexture().GetWidth(), H = RenderTarget.GetTexture().GetHeight();
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
	T.SetIdentity();
	float Scale = 1;
	Vector3d Center;
	if (Asset && Asset->Data.Mesh)
	{
		Vector3d Low, High;
		Asset->Data.Mesh->GetBounds(Low, High);
		Center = Low;
		Center.AddVector(High);
		Center.MultVector(.5f);
		Scale = 128.f /
		    std::max(
		        1.f, std::max(High.GetX() - Low.GetX(), std::max(High.GetY() - Low.GetY(), High.GetZ() - Low.GetZ())));
	}
	const float Y = Yaw * .01745329252f, P = Pitch * .01745329252f, CY = std::cos(Y), SY = std::sin(Y),
	            CP = std::cos(P), SP = std::sin(P);
	T.SetMatrix(0, 0, Scale * CY);
	T.SetMatrix(0, 1, -Scale * SY * CP);
	T.SetMatrix(0, 2, Scale * SY * SP);
	T.SetMatrix(1, 0, Scale * SY);
	T.SetMatrix(1, 1, Scale * CY * CP);
	T.SetMatrix(1, 2, -Scale * CY * SP);
	T.SetMatrix(2, 0, 0);
	T.SetMatrix(2, 1, Scale * SP);
	T.SetMatrix(2, 2, Scale * CP);
	Vector3d Offset;
	T.Transform(Offset, Center);
	T.SetMatrix(0, 3, -Offset.GetX());
	T.SetMatrix(1, 3, 220.f / Zoom - Offset.GetY());
	T.SetMatrix(2, 3, -Offset.GetZ());
	if (Asset)
		List.Append(Asset, T, Error);
	TUniquePtr<FNativeMeshFrame> Frame = Meshes.Prepare(List, RenderTarget.GetTexture().GetFormat(), Error);
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

void FMaterialEditorViewportClient::SetCustomMesh(std::shared_ptr<const StaticMesh> Mesh)
{
	CustomMesh = std::move(Mesh);
	Pitch = Yaw = 0;
}
void FMaterialEditorViewportClient::SetRotation(float InYaw, float InPitch)
{
	Yaw = InYaw;
	Pitch = InPitch;
	RequestRedraw();
}
void FMaterialEditorViewportClient::SetZoom(float V)
{
	Zoom = std::clamp(V, .25f, 2.f);
	RequestRedraw();
}
TSharedRef<SWidget> MakeMaterialPreviewControls(const TSharedRef<FMaterialEditorViewportClient>& Preview,
    std::shared_ptr<FAssetRegistry> Registry, std::function<void()> Changed)
{
	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	const TSharedRef<SHorizontalBox> Shapes = SNew(SHorizontalBox);
	for (const FString Shape : {FString("Sphere"), FString("Cube"), FString("Plane")})
		Shapes->AddSlot().AutoWidth()[SNew(SButton).Text(Shape).OnClicked([Preview, Changed, Shape]
		{
			Preview->SetShape(Shape);
			Changed();
			return FReply::Handled();
		})];
	Shapes->AddSlot().AutoWidth()[SNew(SButton)
	        .Text(GENESIS_TEXT("Lit / Unlit"))
	        .OnClicked([Preview]
	{
		Preview->ToggleLit();
		return FReply::Handled();
	})];
	Box->AddSlot().AutoHeight()[Shapes];
	Box->AddSlot().AutoHeight()[SNew(SComboButton)
	        .Text(GENESIS_TEXT("Custom Preview Mesh"))
	        .OnGetMenuContent([Preview, Registry, Changed]
	{
		const TSharedRef<SVerticalBox> Choices = SNew(SVerticalBox);
		for (const FAssetData& A : Registry->GetAssets())
			if (A.Metadata.Schema == "Genesis.StaticMesh")
			{
				const FAssetReference Ref = A.GetReference();
				Choices->AddSlot().AutoHeight()[SNew(SMenuEntry)
				        .Text(A.PackagePath)
				        .OnSelected([Preview, Registry, Changed, Ref]
				{
					FAssetPackage P;
					FStaticMeshAsset Mesh;
					FString Error;
					if (Registry->Load(Ref, P, Error, "Genesis.StaticMesh") &&
					    FRenderAssetCodec::DecodeMesh(P, Mesh, Error))
					{
						Preview->SetCustomMesh(Mesh.Mesh);
						Changed();
					}
				})];
			}
		return Choices;
	})];
	const TSharedRef<SHorizontalBox> Orbit = SNew(SHorizontalBox);
	Orbit->AddSlot().AutoWidth()[SNew(STextBlock).Text(GENESIS_TEXT("Yaw / Pitch / Zoom"))];
	for (int I = 0; I < 2; ++I)
		Orbit->AddSlot().FillWidth(1)[SNew(SSpinBox<float>)
		        .MinValue(I == 0 ? -180.f : -90.f)
		        .MaxValue(I == 0 ? 180.f : 90.f)
		        .Value([Preview, I] {
			return I == 0 ? Preview->GetYaw() : Preview->GetPitch();
		}).OnValueChanged([Preview, I](float V) {
			Preview->SetRotation(I == 0 ? V : Preview->GetYaw(), I == 1 ? V : Preview->GetPitch());
		})];
	Orbit->AddSlot().FillWidth(
	    1)[SNew(SSpinBox<float>).MinValue(.25f).MaxValue(2.f).Value([Preview] {
		return Preview->GetZoom();
	}).OnValueChanged([Preview](float V) { Preview->SetZoom(V); })];
	Box->AddSlot().AutoHeight()[Orbit];
	return Box;
}
