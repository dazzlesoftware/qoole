// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// FMapLandscapeMode: Unreal's Landscape mode for the map views. Set it as
// FMapViewSettings::EditorMode and a left drag over a landscape in any view
// sculpts it under a brush (drawn as a ring on the ground), with one of the
// tools of Unreal's Sculpt panel:
//
//   Sculpt   raises the ground (Shift lowers it)
//   Smooth   evens out bumps
//   Flatten  pulls the ground to the height where the stroke began
//   Noise    roughens it
//   Paint    paints the selected paint layer (Shift erases it); the other
//            layers give way so the weights at a point add up to 1 at most
//
// The brush has a radius in map units, a strength (0 to 1) and a falloff
// (the outer part of the radius, as a fraction, over which the effect fades
// out). Tools act over time while the button is held; a stroke, from press
// to release, is one undo step. New landscapes are made with
// CreateLandscape, or imported from heightmap files (ImportLandscape).

#pragma once

#include "MapDocument.h"
#include "MapHeightmap.h"
#include "MapViewportClient.h"

enum class ELandscapeTool : uint8
{
	Sculpt,
	Smooth,
	Flatten,
	Noise,
	Paint,
};

const char* ToString(ELandscapeTool Tool);

struct FLandscapeBrush
{
	float Radius = 256.0f;
	float Strength = 0.4f;
	float Falloff = 0.5f;

	static constexpr float MinRadius = 8.0f;
	static constexpr float MaxRadius = 8192.0f;
};

class FMapLandscapeMode final : public IMapViewportMode
{
public:
	void SetDocument(FMapDocument* InDocument);

	ELandscapeTool Tool = ELandscapeTool::Sculpt;
	FLandscapeBrush Brush;

	// The size of a new landscape (Unreal's New Landscape panel).
	int32 NewQuadsX = 64;
	int32 NewQuadsY = 64;
	float NewQuadSize = 32.0f;
	// A flat landscape of the new size centered on Center (scope space).
	Object* CreateLandscape(const Vector3d& Center);

	// Heightmaps (MapHeightmap.h), as Unreal's Import from File: the map
	// units black to white span, mid-grey being height 0.
	float HeightmapZRange = FHeightmap::DefaultZRange;
	// A landscape from a heightmap file, one grid point per pixel at
	// NewQuadSize apart. An image bigger than MaxQuads + 1 pixels is
	// resampled down, with bigger quads covering the same extent.
	Object* ImportLandscape(const FString& Path, const Vector3d& Center, FString& Error);
	// New heights for a landscape from a file (Unreal's reimport): the image
	// stretched over its grid; its paint layers stay. One undo step.
	bool ImportHeightmap(Object& Land, const FString& Path, FString& Error);
	// One pixel per grid point; heights outside the range are clamped.
	bool ExportHeightmap(const Object& Land, const FString& Path, FString& Error) const;
	// A paint layer's weights from a weightmap (stretched over the grid; the
	// other layers give way where the sum would pass 1), or to one (8-bit,
	// as Unreal's). An import is one undo step.
	bool ImportWeightmap(Object& Land, int32 Layer, const FString& Path, FString& Error);
	bool ExportWeightmap(const Object& Land, int32 Layer, const FString& Path, FString& Error) const;

	// Paint layers of a landscape (each change an undo step): what the
	// panel shows is the selected landscape's, or the scope's first one's.
	Object* GetTargetLandscape() const;
	int32 SelectedLayer = 0;
	// A layer named, colored and textured after the next of Unreal's usual
	// ones (Grass, Rock, Dirt, Sand, Snow: the built-in textures); it
	// becomes the selected one.
	bool AddLayer(Object& Land);
	bool AddLayer(Object& Land, const FString& Name, FColor Color, const FString& Texture = FString());
	bool RemoveLayer(Object& Land, int32 Layer);
	bool RenameLayer(Object& Land, int32 Layer, const FString& Name);
	bool SetLayerColor(Object& Land, int32 Layer, FColor Color);
	// Its texture (MapLandscapeTextures.h: "", "builtin:<Name>" or a file)
	// and the units it repeats over.
	bool SetLayerTexture(Object& Land, int32 Layer, const FString& Texture);
	bool SetLayerTiling(Object& Land, int32 Layer, float Tiling);

	// The nearest landscape of the scope a ray hits, and where (scope space).
	Object* TraceLandscape(const Vector3d& Origin, const Vector3d& Direction, Vector3d& OutHit) const;
	// One step of the tool at a point (scope space) over DeltaSeconds, inside
	// the stroke's transaction if one is open.
	bool ApplyAt(Object& Land, const Vector3d& ScopePoint, float DeltaSeconds, bool bInvert);

	// A stroke from code (tests): what a press, ticks and a release do.
	bool BeginStroke(Object& Land, const Vector3d& ScopePoint, bool bInvert);
	void EndStroke();
	bool IsStroking() const { return StrokeTarget != nullptr; }

	// IMapViewportMode
	bool InputPress(FMapViewportClient& View, FVector2 Local, const FPointerEvent& Event) override;
	void InputRelease(FMapViewportClient& View) override;
	void MouseMove(FMapViewportClient& View, FVector2 Local, const FPointerEvent& Event) override;
	void MouseLeave(FMapViewportClient& View) override;
	void Tick(FMapViewportClient& View, float DeltaSeconds) override;
	void DrawOverlay(const FMapViewportClient& View, FSlateWindowElementList& Out, float DpiScale) const override;
	FString GetHint() const override;

private:
	// Where the brush is: over which landscape and view, at what point.
	bool UpdateHover(FMapViewportClient& View, FVector2 Local);

	FMapDocument* Document = nullptr;
	Object* HoverLand = nullptr;
	const FMapViewportClient* HoverView = nullptr;
	Vector3d HoverPoint;
	Object* StrokeTarget = nullptr;
	Vector3d StrokePoint;
	float FlattenHeight = 0.0f; // landscape space
	bool bInvert = false;
	bool bHeld = false; // the left button, pressed in a view
	uint32 NoiseSeed = 1;
};
