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

  private:
	void Draw(FRHICommandList&, FSlateRenderTargetRHI&) override;
	FNativeMeshRenderer Meshes;
	TSharedPtr<FRHITexture> Depth;
	std::shared_ptr<FLoadedMeshAsset> Asset;
	FString Shape = "Sphere";
	bool bLit = true;
};
