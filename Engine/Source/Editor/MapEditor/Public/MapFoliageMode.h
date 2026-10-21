// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// FMapFoliageMode: Unreal's Foliage mode for the map views. Set it as
// FMapViewSettings::EditorMode and a left drag in any view paints the
// active foliage types onto whatever is under the brush (landscapes,
// brushes, meshes), as Unreal's tools do:
//
//   Paint    adds instances until each active type has its density under
//            the brush (Shift erases them)
//   Erase    takes the active types' instances away under the brush
//   Single   one instance of the first active type where it is clicked
//
// Instances keep their type's spacing (Radius) and grow only on the slopes
// and heights it allows, scaled and turned at random, upright or along the
// ground. The foliage lives in the scope's foliage object (made when first
// painted). A stroke is one undo step. Types are edited here too (their
// mesh, density and the rest): each change an undo step.

#pragma once

#include "MapDocument.h"
#include "MapLandscapeMode.h"
#include "MapViewportClient.h"

enum class EFoliageTool : uint8
{
	Paint,
	Erase,
	Single,
};

const char* ToString(EFoliageTool Tool);

// After a landscape's heights changed (from Before): foliage standing on it
// moves up or down with the ground, as in Unreal. Inside the change's
// transaction, if one is open.
void SnapFoliageToLandscape(FMapDocument& Document, const Object& Land, const Landscape& Before);

class FMapFoliageMode final : public IMapViewportMode
{
public:
	void SetDocument(FMapDocument* InDocument);

	EFoliageTool Tool = EFoliageTool::Paint;
	// Radius and Strength (Unreal's Paint Density, a share of each type's
	// density); Falloff thins the edge.
	FLandscapeBrush Brush;

	// The scope's foliage object (the first there is); with bCreate one is
	// made (an undo step of its own outside a transaction).
	Object* GetFoliageObject(bool bCreate = false);

	// Foliage types (Unreal's foliage type assets): the next of the usual
	// ones (Grass, Bush, Tree, Pine, Rock), or one of them by name. Types
	// are painted when active (the panel's check boxes).
	static const TArray<FString>& GetPresetNames();
	static FoliageType MakePreset(const FString& Name);
	bool AddType(const FString& Preset = FString());
	bool RemoveType(int32 Type);
	bool SetType(int32 Type, const FoliageType& NewType, const FString& Description = "Change Foliage Type");
	bool IsActive(int32 Type) const;
	void SetActive(int32 Type, bool bActive);
	// Removes every instance of a type.
	bool ClearType(int32 Type);

	// Where a ray (scope space) first meets something foliage grows on.
	bool TraceSurface(const Vector3d& Origin, const Vector3d& Direction, Vector3d& OutHit) const;
	// One step of the tool at a point on a surface (scope space) over
	// DeltaSeconds, inside the stroke's transaction if one is open.
	bool ApplyAt(const Vector3d& ScopePoint, float DeltaSeconds, bool bErase);
	// One instance of a type where it can stand at a point (Single).
	bool PlaceAt(int32 Type, const Vector3d& ScopePoint);

	// A stroke from code (tests): what a press, ticks and a release do.
	bool BeginStroke(const Vector3d& ScopePoint, bool bErase);
	void EndStroke();
	bool IsStroking() const { return bStroking; }

	// IMapViewportMode
	bool InputPress(FMapViewportClient& View, FVector2 Local, const FPointerEvent& Event) override;
	void InputRelease(FMapViewportClient& View) override;
	void MouseMove(FMapViewportClient& View, FVector2 Local, const FPointerEvent& Event) override;
	void MouseLeave(FMapViewportClient& View) override;
	void Tick(FMapViewportClient& View, float DeltaSeconds) override;
	void DrawOverlay(const FMapViewportClient& View, FSlateWindowElementList& Out, float DpiScale) const override;
	FString GetHint() const override;

private:
	bool UpdateHover(FMapViewportClient& View, FVector2 Local);
	// The ground below a point (scope space), and its normal.
	bool GroundAt(float X, float Y, Vector3d& OutPoint, Vector3d& OutNormal) const;
	// An instance of a type at a ground point, if the type grows there and
	// keeps its spacing from Existing (foliage space); false if not.
	bool MakeInstance(const Foliage& Current, int32 Type, const Vector3d& Ground, const Vector3d& Normal,
		const TArray<FoliageInstance>& Existing, FoliageInstance& Out);
	float Random();

	FMapDocument* Document = nullptr;
	TArray<bool> Active;
	const FMapViewportClient* HoverView = nullptr;
	bool bHover = false;
	Vector3d HoverPoint;
	bool bHeld = false;
	bool bStroking = false;
	bool bErase = false;
	Vector3d StrokePoint;
	uint32 Seed = 12345;
};
