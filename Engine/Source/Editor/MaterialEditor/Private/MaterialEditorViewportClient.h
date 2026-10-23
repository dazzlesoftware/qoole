// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "RenderTargetViewportClient.h"
#include "Renderer/NativeMeshRenderer.h"
class FMaterialEditorViewportClient final : public FRenderTargetViewportClient
{
  public:
	FMaterialEditorViewportClient(FRHIDevice&, FSlateRHIRenderingPolicy&);
	~FMaterialEditorViewportClient() override;
	bool Update(const FMaterialAsset&, const FAssetRegistry&, FString& Error);
	void SetShape(const FString&);
	void ToggleLit();
	void SetCustomMesh(std::shared_ptr<const StaticMesh>);
	void SetRotation(float YawDegrees, float PitchDegrees);
	void SetZoom(float);
	float GetYaw() const
	{
		return Yaw;
	}
	float GetPitch() const
	{
		return Pitch;
	}
	float GetZoom() const
	{
		return Zoom;
	}

  private:
	void Draw(FRHICommandList&, FSlateRenderTargetRHI&) override;
	FNativeMeshRenderer Meshes;
	TSharedPtr<FRHITexture> Depth;
	std::shared_ptr<FLoadedMeshAsset> Asset;
	FString Shape = "Sphere";
	bool bLit = true;
	std::shared_ptr<const StaticMesh> CustomMesh;
	float Yaw = 0, Pitch = 0, Zoom = 1;
};

TSharedRef<SWidget> MakeMaterialPreviewControls(const TSharedRef<FMaterialEditorViewportClient>&,
												std::shared_ptr<FAssetRegistry>,
												std::function<void()> OnMeshChanged);
