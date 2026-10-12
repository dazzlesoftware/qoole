// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// FMapViewportClient: one map view (top, front, side or 3D) in an SViewport.
// It owns a map core View (the camera, projection and picking) and draws
// through FMapDraw into its render target: wireframes and the grid with the
// UI renderer, solid faces of the 3D view with a depth buffer.
//
// Mouse, in every view:
//   left click          select the object under the cursor (again: the next
//                       one under it; Alt: the previous one)
//   Ctrl + left click   add or remove the object under the cursor
//   left drag           box select (Shift: add to the selection); in the
//                       2D views, a drag that starts on the selection moves
//                       it (snapped to the grid unless Alt is held)
// 2D views: middle or right drag pans, the wheel zooms at the cursor.
// 3D view: right drag looks around (the cursor hides; W/A/S/D/Q/E fly while
//   it is held, Shift faster), middle drag pans, the wheel moves forward.
//
// Every view of a document shares one FMapViewSettings (grid, snapping) and
// redraws when the document changes.

#pragma once

#include "CoreMinimal.h"
#include "MapDocument.h"
#include "RenderTargetViewportClient.h"

#include "MapCoreHeaders.h"

class FMapDraw;
class View;

enum class EMapViewType : uint8
{
	Top,
	Front,
	Side,
	Perspective,
};

const char* ToString(EMapViewType Type);

enum class EMapRenderMode : uint8
{
	Wireframe,
	Solid,
};

// Settings shared by the views of an editor.
struct FMapViewSettings
{
	float GridSize = 16.0f; // map units; snapping and the minor grid lines
	bool bSnapToGrid = true;
	bool bShowGrid = true;

	// Called with the map position under the cursor of a 2D view ("x y z").
	TFunction<void(const FString&)> OnCursorText;
	// Fired when a setting changes; views redraw.
	TMulticastDelegate<void()> OnChanged;
	void Changed() { OnChanged.Broadcast(); }

	static constexpr float MinGridSize = 1.0f;
	static constexpr float MaxGridSize = 256.0f;
};

// GPU objects shared by all map views of a device.
class FMapRenderResources
{
public:
	explicit FMapRenderResources(FRHIDevice& InDevice) : Device(InDevice) {}
	FRHIGraphicsPipeline* GetSolidPipeline(EPixelFormat TargetFormat);
	FRHITexture& GetWhiteTexture();

private:
	FRHIDevice& Device;
	TSharedPtr<FRHITexture> WhiteTexture;
	TSharedPtr<FRHIGraphicsPipeline> SolidPipeline;
	EPixelFormat SolidFormat = EPixelFormat::Unknown;
};

class FMapViewportClient final : public FRenderTargetViewportClient
{
public:
	FMapViewportClient(FRHIDevice& InDevice, FUIRenderer& InRenderer, FMapRenderResources& InResources, FMapViewSettings& InSettings, EMapViewType InType);
	~FMapViewportClient() override;

	EMapViewType GetType() const { return Type; }
	bool IsPerspective() const { return Type == EMapViewType::Perspective; }

	// The document shown (may be null); the view resets its camera.
	void SetDocument(FMapDocument* InDocument);
	FMapDocument* GetDocument() const { return Document; }

	EMapRenderMode GetRenderMode() const { return RenderMode; }
	void SetRenderMode(EMapRenderMode Mode);

	// Camera.
	void ResetCamera();
	void FrameSelection();   // the selection, or the whole map when nothing is selected
	void ZoomBy(float Factor);
	float GetZoom() const { return Zoom; }

	// Where new objects go: the view's center (2D, at the camera depth) or a
	// point in front of the camera (3D), in scope space.
	Vector3d GetPlacementPoint() const;

	// Converts a point in the viewport (logical units) to the map, on the
	// plane through the camera (2D views only).
	bool ScreenToMap(FVector2 LocalPosition, Vector3d& OutPoint) const;
	// The other way: where a map point is in the viewport (logical units);
	// false behind the camera of the 3D view.
	bool MapToScreen(const Vector3d& Point, FVector2& OutLocal) const;

	// IViewportClient
	bool IsRealtime() const override { return bLooking; }
	FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnRawMouseMove(FVector2 Delta) override;
	void OnMouseLeave(const FPointerEvent& Event) override;
	void OnMouseCaptureLost() override;
	FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	FReply OnKeyUp(const FGeometry& Geometry, const FKeyEvent& Event) override;
	void OnFocusLost(const FFocusEvent& Event) override;
	FCursorReply OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const override;

protected:
	void Draw(FRHICommandList& CommandList, FUIRenderTarget& Target) override;

private:
	enum class EDrag : uint8
	{
		None,
		Pending,   // left button down, not moved yet
		BoxSelect,
		Move,
		Pan,
		Look,
	};

	void CreateView();
	void UpdateProjection();
	float GetDpiScale() const;
	float GetDrawZoom() const; // pixels per map unit (2D) or 1 (3D)
	// Viewport position (logical) to the map core's screen coordinates.
	void ToScreen(FVector2 Local, float& OutX, float& OutY) const;
	Vector3d ViewToScope(const Vector3d& ViewVector) const; // a direction
	Vector3d ScopeToView(const Vector3d& Point) const;      // a point
	void PanBy(FVector2 LocalDelta);
	void SelectAt(FVector2 Local, const FPointerEvent& Event);
	void UpdateBoxSelection(bool bFinal);
	void UpdateMove(FVector2 Local, bool bSnap);
	void ReportCursor(FVector2 Local);
	void Fly(float DeltaSeconds);
	void DrawGrid(FMapDraw& MapDraw);
	void DrawOverlay(FDrawElementList& Elements, FIntPoint PixelSize);
	void OnDocumentChanged(EMapChange Change);

	FMapRenderResources& Resources;
	FMapViewSettings& Settings;
	const EMapViewType Type;
	FMapDocument* Document = nullptr;
	FDelegateHandle DocumentHandle;
	FDelegateHandle SettingsHandle;

	TUniquePtr<FMapDraw> MapDraw;
	View* MapView = nullptr;
	EMapRenderMode RenderMode = EMapRenderMode::Wireframe;
	float Zoom = 0.5f;      // 2D: logical pixels per map unit
	float DrawZoom = 1.0f;  // what FDraw scales by (it keeps a pointer)
	float Yaw = 0.0f;       // 3D camera, radians
	float Pitch = 0.0f;

	EDrag Drag = EDrag::None;
	EMouseButton DragButton = EMouseButton::Left;
	FVector2 DragStart;      // logical, in the viewport
	FVector2 DragLast;
	bool bDragAdd = false;   // box select adds (Shift)
	TArray<Object*> SelectionBeforeBox;
	TArray<Object*> MovedObjects;
	Vector3d MoveReference;  // selection bounds min at drag start
	Vector3d MoveApplied;    // total applied so far
	FVector2 MoveTotal;      // logical pixels since the drag started

	bool bLooking = false;
	TSet<EKey> FlyKeys;
	double LastFlyTime = 0.0;
	FVector2 CursorLocal{ -1.0f, -1.0f };

	TSharedPtr<FRHITexture> DepthBuffer;
	FDrawElementList Elements;
};
