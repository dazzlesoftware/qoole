// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// FMapLandscapeMode: Unreal's Landscape mode for the map views. Set it as
// FMapViewSettings::EditorMode and a left drag over a landscape in any view
// sculpts it under a brush (drawn as a ring on the ground), with one of the
// tools of Unreal's Sculpt panel:
//
//   Sculpt   raises the ground (Shift lowers it)
//   Smooth   evens out bumps
//   Flatten  pulls the ground to the height where the stroke began, or to
//            a set height; only up or only down if asked (FlattenMode)
//   Ramp     a drag from one point to another lays a slope between them,
//            the brush's radius wide, when the button comes up
//   Erosion  wears steep slopes down onto the ground below them (thermal
//            erosion: earth slides where a slope is steeper than ErosionTalus)
//   Noise    roughens it
//   Splines  edits the landscape's splines (Unreal's Manage > Splines):
//            Ctrl+click adds a control point after the selected one (or
//            starts a spline); Ctrl+click on another point joins the
//            selected one to it (a junction) or branches from it; a click
//            selects a point and a drag moves it (a junction's points
//            together); the selected point's tangent handles bend the
//            curve there; Apply Splines shapes the ground to them
//   Paint    paints the selected paint layer toward PaintTarget (Shift
//            erases it); the other layers give way so the weights at a
//            point add up to 1 at most
//
// Auto Paint lays a layer where the ground's slope and height are in a
// range, all over the landscape at once (Unreal's auto material, done once).
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
	Ramp,
	Erosion,
	Noise,
	Splines,
	Paint,
};

const char* ToString(ELandscapeTool Tool);

// Which way Flatten may move the ground (Unreal's Flatten Mode).
enum class ELandscapeFlattenMode : uint8
{
	Both,
	Raise,
	Lower,
};

const char* ToString(ELandscapeFlattenMode Mode);

struct FLandscapeBrush
{
	float Radius = 256.0f;
	float Strength = 0.4f;
	float Falloff = 0.5f;

	static constexpr float MinRadius = 8.0f;
	static constexpr float MaxRadius = 8192.0f;

	// Unreal's [ and ] (a step of a quarter), and Ctrl with them for strength.
	void Grow(int32 Steps);
	void Strengthen(int32 Steps);
};

// Where Auto Paint lays a layer: slopes in degrees, heights in the
// landscape's units; the edges fade over SlopeFade degrees and HeightFade units.
struct FLandscapeAutoPaint
{
	float MinSlope = 0.0f;
	float MaxSlope = 90.0f;
	float SlopeFade = 5.0f;
	float MinHeight = -65536.0f;
	float MaxHeight = 65536.0f;
	float HeightFade = 32.0f;

	// 0 to 1 at a point of the landscape.
	float WeightAt(const Landscape& Land, int32 X, int32 Y) const;
};

// Every landscape's splines over a view (all modes): the curves and their
// control points, the selected one (Selected* of the Landscape mode, if
// any) bigger.
void DrawLandscapeSplines(const FMapDocument& Document, const FMapViewportClient& View, FSlateWindowElementList& Out, float DpiScale,
	const Object* SelectedLand = nullptr, int32 SelectedSpline = -1, int32 SelectedPoint = -1);

class FMapLandscapeMode final : public IMapViewportMode
{
public:
	void SetDocument(FMapDocument* InDocument);

	ELandscapeTool Tool = ELandscapeTool::Sculpt;
	FLandscapeBrush Brush;

	// Tool settings (Unreal's Tool Settings, per tool).
	ELandscapeFlattenMode FlattenMode = ELandscapeFlattenMode::Both;
	bool bFlattenToTarget = false; // to FlattenTarget, not where the stroke began
	float FlattenTarget = 0.0f;    // a height in the landscape's units
	float ErosionTalus = 30.0f;    // degrees: steeper slopes slide
	float PaintTarget = 1.0f;      // the weight painting moves toward

	// A slope from From to To (scope space, on the landscape) the brush's
	// radius wide, its edges fading over the falloff; one undo step.
	bool AddRamp(Object& Land, const Vector3d& From, const Vector3d& To);

	// Splines: the selected control point (on SplineLand; -1 for none).
	Object* SplineLand = nullptr;
	int32 SelectedSpline = -1;
	int32 SelectedPoint = -1;
	bool HasSelectedSplinePoint() const;
	const LandscapeSpline* GetSelectedSpline() const;
	// A control point where the ground is (scope space): after the
	// selected one of its spline (or before it, at its start), else a new
	// spline's first; it becomes the selected one. One undo step.
	bool AddSplinePoint(Object& Land, const Vector3d& ScopePoint);
	// Moves a point, and every point of its junction with it.
	bool MoveSplinePoint(Object& Land, int32 Spline, int32 Point, const Vector3d& ScopePoint);
	// A segment from the selected point to another (a junction there): from
	// an end of its spline, the spline goes on to it; else a new spline
	// runs between them. With nothing selected, a new spline branches from
	// it. The new end is selected. One undo step.
	bool ConnectSplinePoint(Object& Land, int32 Spline, int32 Point);
	// The point's tangent by hand (landscape space), or automatic again.
	bool SetSplineTangent(Object& Land, int32 Spline, int32 Point, const Vector3d& Tangent);
	bool SetSplineAutoTangent(Object& Land, int32 Spline, int32 Point);
	// The selected point no longer in a junction.
	bool BreakSelectedJunction();
	// Where the selected point's tangent handles are (scope space): the
	// curve's Bezier controls, a third of the tangent either way.
	bool GetTangentHandles(Vector3d& OutBefore, Vector3d& OutAfter) const;
	bool DeleteSelectedSplinePoint();
	bool DeleteSelectedSpline();
	bool SetSpline(Object& Land, int32 Spline, const LandscapeSpline& NewSpline, const FString& Description = "Change Spline");
	// The ground shaped to every spline of it, their layers painted.
	bool ApplySplines(Object& Land);
	// The control point within Pixels of a view point, nearest first.
	bool PickSplinePoint(const FMapViewportClient& View, FVector2 Local, float Pixels, Object*& OutLand, int32& OutSpline, int32& OutPoint) const;

	FLandscapeAutoPaint AutoPaint;
	// The layer where AutoPaint says, the others giving way; one undo step.
	bool ApplyAutoPaint(Object& Land, int32 Layer);

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
	// A ramp being dragged out: from where, on which landscape.
	Object* RampLand = nullptr;
	Vector3d RampFrom;
	bool bInvert = false;
	bool bHeld = false; // the left button, pressed in a view
	bool bDraggingPoint = false;
	int32 DraggingHandle = 0; // -1 the handle before the point, +1 after
	uint32 NoiseSeed = 1;
};
