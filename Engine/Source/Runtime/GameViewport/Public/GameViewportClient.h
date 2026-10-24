// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "RenderTargetViewportClient.h"
#include "Renderer/NativeMeshRenderer.h"
#include "Renderer/ProjectedSceneRenderer.h"
#include "GameFramework/GameInstance.h"

// The game's view, as Unreal's game viewport client: every loaded region
// drawn through its region-to-render transform from the player's eye, and the
// player's keyboard and mouse. Shared by the standalone game and
// Play-in-Editor, which only differ in what Esc and F10 do (OnEscape, OnQuit).
//
// Click to capture the mouse and look around; Esc releases it. WASD or the
// arrows move, Shift runs, Space jumps, F flies, Q/E go down/up while flying,
// F5/F9 quick save/load. Movement keys are always handled here, so a host's
// own shortcuts (the editor's) do not fire while playing.
class FGameViewportClient final : public FRenderTargetViewportClient
{
public:
	static constexpr float FieldOfViewDegrees = 75.0f;
	static constexpr float NearDistance = 10.0f;
	static constexpr float TerrainDetailPixels = 8.0f;
	static constexpr float FarCells = 2.5f; // drawing distance, in grid cells

	FGameViewportClient(FRHIDevice& InDevice, FSlateRHIRenderingPolicy& InRenderer, FGameInstance& InGame);
	~FGameViewportClient() override;
	bool IsRealtime() const override
	{
		return true;
	}
	FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnRawMouseMove(FVector2 Delta) override;
	void OnMouseCaptureLost() override
	{
		bMouseCaptured = false;
	}
	FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	FReply OnKeyUp(const FGeometry& Geometry, const FKeyEvent& Event) override;
	void OnFocusLost(const FFocusEvent& Event) override;
	void ReleaseMouse();
	bool IsMouseCaptured() const
	{
		return bMouseCaptured;
	}

	// Esc with the mouse free (Play-in-Editor stops), and F10.
	FSimpleDelegate OnEscape, OnQuit;
	// From the last frame drawn.
	int32 GetRegionsDrawn() const
	{
		return RegionsDrawn;
	}
	int32 GetTerrainTriangles() const
	{
		return TerrainTriangles;
	}
	int64 GetFramesDrawn() const
	{
		return FramesDrawn;
	}
	const FString& GetLastError() const
	{
		return LastError;
	}

	// The game view: the viewport with the HUD panel over its top-left corner.
	static TSharedRef<SWidget> MakeGameView(const TSharedRef<FGameViewportClient>& Client, TFunction<FString()> HudText,
	    TSharedPtr<SViewport>& OutViewport);

protected:
	void Draw(FRHICommandList& CommandList, FSlateRenderTargetRHI& Target) override;

private:
	void UpdateInput();
	FGameInstance& Game;
	FProjectedSceneRenderer Scene;
	FNativeMeshRenderer Meshes;
	TSharedPtr<FRHITexture> Depth;
	TSet<uint16> HeldKeys;
	int32 RegionsDrawn = 0, TerrainTriangles = 0;
	int64 FramesDrawn = 0;
	FString LastError;
	bool bMouseCaptured = false;
};
