// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// FMapViewportClient: one map view in an SViewport, Unreal's
// FLevelEditorViewportClient. Its viewport type (perspective, or top,
// bottom, left, right, front or back) can change at any time. It owns a map
// core View (the camera, projection and picking) and draws through FMapDraw
// into its render target: wireframes and the grid with the UI renderer,
// solid faces of a perspective view (Lit or Unlit) with a depth buffer.
//
// Mouse, in every view:
//   left click          select the object under the cursor (again: the next
//                       one under it; Alt: the previous one)
//   Ctrl + left click   add or remove the object under the cursor
//   left drag           box select (Shift: add to the selection)
// 2D views: middle or right drag pans, the wheel zooms at the cursor, and a
//   left drag uses the current tool (FMapViewSettings::Tool), snapped to the
//   grid unless Alt is held:
//     Select   a drag that starts on the selection moves it
//     Rotate   turns the selection about its center (in GetRotateSnap() steps)
//     Scale    stretches the selection from its opposite corner (Shift: evenly)
//     Clip     draws the clipping line; Enter keeps the marked side, Ctrl+Enter
//              splits, Tab changes sides, Esc drops the line
//     Vertex, Edge, Face
//              drag the dots of the one selected brush
// Perspective views: right drag looks around (the cursor hides; W/A/S/D/Q/E
//   fly while it is held, Shift faster, and the wheel changes the camera
//   speed), middle drag pans, the wheel moves forward. Alt + left drag
//   orbits about the point looked at, Alt + right drag dollies, Alt +
//   middle drag pans.
//
// Every view of a document shares one FMapViewSettings (grid, snapping,
// tool) and redraws when the document changes. View mode, field of view,
// camera speed and show flags belong to each view. SMapViewport puts a
// toolbar over it.

#pragma once

#include "CoreMinimal.h"
#include "MapDocument.h"
#include "MapLandscapeTextures.h"
#include "RenderTargetViewportClient.h"

#include "MapCoreHeaders.h"

#include <map>

class FMapDraw;
class FMapViewportClient;
class View;

// What an editor mode does with the viewports (Unreal's FEdMode, for
// input): the Landscape mode's sculpting. While FMapViewSettings::EditorMode
// is set, a left press it takes (InputPress) is a drag of its own, ticked
// every frame while it is held; it sees the cursor move and draws over the
// views.
class IMapViewportMode
{
public:
	virtual ~IMapViewportMode() = default;
	virtual bool InputPress(FMapViewportClient& View, FVector2 Local, const FPointerEvent& Event) = 0;
	virtual void InputRelease(FMapViewportClient& View) { (void)View; }
	virtual void MouseMove(FMapViewportClient& View, FVector2 Local, const FPointerEvent& Event) { (void)View; (void)Local; (void)Event; }
	virtual void MouseLeave(FMapViewportClient& View) { (void)View; }
	virtual void Tick(FMapViewportClient& View, float DeltaSeconds) { (void)View; (void)DeltaSeconds; }
	// Logical units over the view (its DPI scale given).
	virtual void DrawOverlay(const FMapViewportClient& View, FSlateWindowElementList& Out, float DpiScale) const { (void)View; (void)Out; (void)DpiScale; }
	// For the view's stats label.
	virtual FString GetHint() const { return FString(); }
};

// Unreal's ELevelViewportType. The first four are the panes of the default
// four-pane layout, in order.
enum class EMapViewType : uint8
{
	Top,         // looking down
	Front,       // looking along +Y
	Right,       // looking along -X
	Perspective,
	Bottom,      // looking up
	Back,        // looking along -Y
	Left,        // looking along +X
	Count
};

const char* ToString(EMapViewType Type);
TOptional<EMapViewType> MapViewTypeFromString(const FString& Name);

// What a left drag does in a 2D view.
enum class EMapTool : uint8
{
	Select,
	Rotate,
	Scale,
	Clip,
	Vertex,
	Edge,
	Face,
};

const char* ToString(EMapTool Tool);

// Unreal's EViewModeIndex, for the modes a map view has. Orthographic views
// always draw wireframes.
enum class EMapViewMode : uint8
{
	Wireframe,
	Unlit, // solid faces in their flat colors
	Lit,   // solid faces shaded by the way they face
};

const char* ToString(EMapViewMode Mode);

// What a view draws besides the map (Unreal's FEngineShowFlags, for this editor).
struct FMapShowFlags
{
	bool bGrid = true;
	bool bEntities = true;
	bool bViewportStats = true; // the label at the top left
	bool bAxes = true;          // the axis indicator at the bottom left

	bool operator==(const FMapShowFlags&) const = default;
};

// Settings shared by the views of an editor.
struct FMapViewSettings
{
	float GridSize = 16.0f; // map units; snapping and the minor grid lines
	bool bSnapToGrid = true;
	bool bShowGrid = true; // every view; each also has its own Grid show flag
	EMapTool Tool = EMapTool::Select;
	bool bSnapRotation = true;
	float RotateSnapDegrees = 15.0f;
	float MouseSensitivity = 1.0f; // looking around, times the default speed
	IMapViewportMode* EditorMode = nullptr; // takes left drags (the Landscape mode)
	bool bInvertMiddleMousePan = false;

	// Called with the map position under the cursor of a 2D view ("x y z").
	TFunction<void(const FString&)> OnCursorText;
	// Called with what a tool did or needs (for the status bar and log).
	TFunction<void(const FString&)> OnMessage;
	// Fired when a setting changes; views redraw.
	TMulticastDelegate<void()> OnChanged;
	void Changed() { OnChanged.Broadcast(); }

	// The rotation step tools turn by: 0 when rotation snapping is off.
	float GetRotateSnap() const { return bSnapRotation ? RotateSnapDegrees : 0.0f; }

	static constexpr float MinGridSize = 1.0f;
	static constexpr float MaxGridSize = 256.0f;
};

// GPU objects shared by all map views of a device.
class FMapRenderResources
{
public:
	explicit FMapRenderResources(FRHIDevice& InDevice) : Device(InDevice) {}
	~FMapRenderResources();
	FRHIGraphicsPipeline* GetSolidPipeline(EPixelFormat TargetFormat);
	FRHIGraphicsPipeline* GetMaterialPipeline(EPixelFormat TargetFormat, bool Translucent = false);
	bool bMeshMaterials = true;
	bool GetMaterialTextures(const std::shared_ptr<const struct FLoadedMeshAsset>&, FRHITexture*& Color, FRHITexture*& Normal);
	// Textured landscapes, their layers blended per pixel (PSLandscape).
	FRHIGraphicsPipeline* GetLandscapePipeline(EPixelFormat TargetFormat);
	// Meshes drawn instanced (foliage: VSFoliage), and a mesh's triangles
	// for it (made once a mesh: positions, normals and colors a corner).
	FRHIGraphicsPipeline* GetInstancedPipeline(EPixelFormat TargetFormat);
	struct FMeshBuffers
	{
		TSharedPtr<FRHIBuffer> Vertices;
		TSharedPtr<FRHIBuffer> Indices;
		uint32 IndexCount = 0;
	};
	const FMeshBuffers* GetMeshBuffers(const StaticMesh& Mesh);
	FRHITexture& GetWhiteTexture();
	// A landscape's layers for the GPU to blend (FLandscapeLayerImages):
	// its weights and its layers' textures, brought up to date (a stroke
	// copies only the weights it changed). False if it has more layers than
	// the GPU blends, or none textured: then it is drawn baked
	// (GetLandscapeTexture). Call outside render passes.
	bool GetLandscapeLayers(const Object& Land, FRHITexture*& OutWeights, FRHITexture*& OutLayers);
	// Whether landscapes blend their layers on the GPU (true) or are drawn
	// with a baked image (tests compare the two).
	bool bBlendLandscapeLayers = true;
	// Whether foliage is drawn instanced on the GPU (true) or as triangles
	// View makes (tests compare the two).
	bool bInstanceMeshes = true;
	// A textured landscape's baked image (FLandscapeBake), brought up to
	// date: only what a stroke changed is baked and copied again. Null if
	// the object is no textured landscape. Call outside render passes.
	FRHITexture* GetLandscapeTexture(const Object& Land);
	// Once a frame: images no view has drawn for a while are let go.
	void Trim();
	int32 GetNumLandscapeTextures() const { return static_cast<int32>(Landscapes.size()); }
	// The bake of a landscape drawn lately (tests); null if none.
	const FLandscapeBake* FindLandscapeBake(const Object& Land) const;

private:
	struct FMaterialEntry
	{
		std::shared_ptr<const struct FLoadedMeshAsset> Asset;
		TSharedPtr<FRHITexture> Color, Normal;
		uint64 LastUsed = 0;
	};
	std::map<const struct FLoadedMeshAsset*, FMaterialEntry> Materials;
	struct FLandscapeEntry
	{
		FLandscapeBake Bake;
		TSharedPtr<FRHITexture> Texture;
		FLandscapeLayerImages Layers;
		TSharedPtr<FRHITexture> WeightTexture;
		TSharedPtr<FRHITexture> LayerTexture;
		uint64 LastUsed = 0;
	};
	FLandscapeEntry& FindEntry(const Object& Land);
	void Release(FLandscapeEntry& Entry);

	FRHIDevice& Device;
	TSharedPtr<FRHITexture> WhiteTexture;
	TSharedPtr<FRHIGraphicsPipeline> SolidPipeline;
	TSharedPtr<FRHIGraphicsPipeline> MaterialPipeline;
	TSharedPtr<FRHIGraphicsPipeline> TranslucentMaterialPipeline;
	EPixelFormat MaterialFormat = EPixelFormat::Unknown;
	EPixelFormat SolidFormat = EPixelFormat::Unknown;
	TSharedPtr<FRHIGraphicsPipeline> LandscapePipeline;
	EPixelFormat LandscapeFormat = EPixelFormat::Unknown;
	TSharedPtr<FRHIGraphicsPipeline> InstancedPipeline;
	EPixelFormat InstancedFormat = EPixelFormat::Unknown;
	std::map<const StaticMesh*, FMeshBuffers> MeshBuffers;
	std::map<const Object*, std::unique_ptr<FLandscapeEntry>> Landscapes;
	uint64 Frame = 0;
};

class FMapViewportClient final : public FRenderTargetViewportClient
{
public:
	FMapViewportClient(FRHIDevice& InDevice, FSlateRHIRenderingPolicy& InRenderer, FMapRenderResources& InResources, FMapViewSettings& InSettings, EMapViewType InType);
	~FMapViewportClient() override;

	EMapViewType GetViewportType() const { return Type; }
	// Looks the other way: the camera keeps what it was looking at (2D
	// views keep their zoom).
	void SetViewportType(EMapViewType InType);
	bool IsPerspective() const { return Type == EMapViewType::Perspective; }
	bool IsOrtho() const { return !IsPerspective(); }

	// The document shown (may be null); the view resets its camera.
	void SetDocument(FMapDocument* InDocument);
	FMapDocument* GetDocument() const { return Document; }

	// The mode drawn: always Wireframe in orthographic views.
	EMapViewMode GetViewMode() const { return IsPerspective() ? ViewMode : EMapViewMode::Wireframe; }
	// The mode the view has when it is a perspective view (what SetViewMode sets).
	EMapViewMode GetPerspectiveViewMode() const { return ViewMode; }
	void SetViewMode(EMapViewMode Mode);
	bool SupportsViewMode(EMapViewMode Mode) const { return IsPerspective() || Mode == EMapViewMode::Wireframe; }

	const FMapShowFlags& GetShowFlags() const { return ShowFlags; }
	void SetShowFlags(const FMapShowFlags& Flags);

	// Perspective views: horizontal field of view in degrees (the map core
	// clips at 90 degrees, so that is the widest), and the flying speed.
	float GetViewFOV() const { return ViewFOV; }
	void SetViewFOV(float Degrees);
	float GetCameraSpeed() const { return CameraSpeed; }
	void SetCameraSpeed(float Speed);
	static constexpr float MinViewFOV = 20.0f;
	static constexpr float MaxViewFOV = 90.0f;
	static constexpr float MinCameraSpeed = 0.1f;
	static constexpr float MaxCameraSpeed = 32.0f;

	// Fired when the type, view mode, field of view, camera speed or show
	// flags change (toolbars and saved settings follow it).
	TMulticastDelegate<void()> OnViewChanged;

	// Camera.
	void ResetCamera();
	void FrameSelection();   // the selection, or the whole map when nothing is selected
	void ZoomBy(float Factor);
	float GetZoom() const { return Zoom; }

	// Where new objects go: the view's center (2D, at the camera depth) or a
	// point in front of the camera (3D), in scope space.
	Vector3d GetPlacementPoint() const;
	// Where something dropped at a viewport point goes, in scope space: on
	// the plane through the camera (2D), or where the ray hits the ground
	// (z = 0) in front of the camera, else a point ahead along it (3D).
	Vector3d GetDropPoint(FVector2 LocalPosition) const;
	// The ray under a viewport point in scope space: from the camera (3D) or
	// from the camera's plane along the view (2D). The direction is a unit vector.
	bool GetCursorRay(FVector2 LocalPosition, Vector3d& OutOrigin, Vector3d& OutDirection) const;
	// The scope axis a 2D view looks along (0 X, 1 Y, 2 Z); Z for 3D views.
	int32 GetDepthAxis() const;

	// Converts a point in the viewport (logical units) to the map, on the
	// plane through the camera (2D views only).
	bool ScreenToMap(FVector2 LocalPosition, Vector3d& OutPoint) const;
	// The other way: where a map point is in the viewport (logical units);
	// false behind the camera of the 3D view.
	bool MapToScreen(const Vector3d& Point, FVector2& OutLocal) const;

	// IViewportClient
	bool IsRealtime() const override { return bLooking || Drag == EDrag::Mode; }
	FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override;
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
	void Draw(FRHICommandList& CommandList, FSlateRenderTargetRHI& Target) override;

private:
	enum class EDrag : uint8
	{
		None,
		Pending,   // left button down, not moved yet
		BoxSelect,
		Move,
		Pan,
		Look,
		Mode,      // the editor mode's (Settings.EditorMode)
		Orbit,     // Alt + left drag (3D)
		Dolly,     // Alt + right drag (3D)
		Rotate,
		Scale,
		ClipLine,
		BrushEdit,
	};

	void CreateView();
	// Puts the 2D camera at Center, or the 3D camera Distance from it
	// looking at it from above.
	void PlaceCamera(const Vector3d& Center, float Distance);
	// What the camera looks at: the center of a 2D view, or a point in front
	// of the 3D camera (at the selection's depth when it is ahead).
	Vector3d GetFocusPoint(float* OutDistance = nullptr) const;
	float GetWindowWidth() const; // the map core's projection scale (pixels)
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
	void Orbit(FVector2 Delta);
	void Dolly(float Delta);
	void DrawGrid(FMapDraw& MapDraw);
	void DrawPerspectiveGrid(FMapDraw& MapDraw, bool bDepthTested);
	void DrawOverlay(FSlateWindowElementList& Elements, FIntPoint PixelSize);
	void DrawAxes(FSlateWindowElementList& Elements, FIntPoint PixelSize);
	void DrawScaleBar(FSlateWindowElementList& Elements, FIntPoint PixelSize);
	void ViewChanged();
	void OnDocumentChanged(EMapChange Change);
	void FollowScope();
	void Message(const FString& Text) const;

	// Tools (MapViewportTools.cpp).
	// The axes of a 2D view in scope space: across, up the screen, and depth.
	void GetViewAxes(int32& OutRight, float& OutRightSign, int32& OutUp, float& OutUpSign, int32& OutDepth) const;
	Object* GetSingleBrush() const;
	// Starts a tool drag once a left press has moved; false if the tool does not apply.
	bool BeginToolDrag(FVector2 Local, const FPointerEvent& Event);
	void UpdateToolDrag(FVector2 Local, const FPointerEvent& Event);
	void EndToolDrag();
	void CancelToolDrag();
	// Whether a left press starts dragging a brush dot (Vertex, Edge, Face tools).
	bool PickBrushDot(FVector2 Local);
	FReply ToolKeyDown(const FKeyEvent& Event);
	void ApplyClip(EMapClip Mode);
	void DrawTools(FMapDraw& MapDraw);
	FString GetToolHint() const;
	void UpdateDotDisplay();

	FMapRenderResources& Resources;
	FMapViewSettings& Settings;
	EMapViewType Type;
	FMapDocument* Document = nullptr;
	FDelegateHandle DocumentHandle;
	FDelegateHandle SettingsHandle;

	TUniquePtr<FMapDraw> MapDraw;
	View* MapView = nullptr;
	EMapViewMode ViewMode = EMapViewMode::Lit;
	FMapShowFlags ShowFlags;
	float ViewFOV = 90.0f;
	float CameraSpeed = 1.0f;
	float Zoom = 0.5f;      // 2D: logical pixels per map unit
	float DrawZoom = 1.0f;  // what FDraw scales by (it keeps a pointer)
	float Yaw = 0.0f;       // 3D camera, radians
	float Pitch = 0.0f;

	EDrag Drag = EDrag::None;
	EMouseButton DragButton = EMouseButton::Left;
	FVector2 DragStart;      // logical, in the viewport
	FVector2 DragLast;
	bool bDragAdd = false;   // box select adds (Shift)
	bool bAltDrag = false;   // pressed with Alt: a 3D left drag orbits
	TArray<Object*> SelectionBeforeBox;
	TArray<Object*> MovedObjects;
	Vector3d MoveReference;  // selection bounds min at drag start
	Vector3d MoveApplied;    // total applied so far
	FVector2 MoveTotal;      // logical pixels since the drag started

	// Orbit: the point turned about and the camera's distance from it.
	Vector3d OrbitPivot;
	float OrbitDistance = 0.0f;

	// Rotate and scale.
	Vector3d Pivot;          // scope space
	float RotateApplied = 0.0f; // degrees
	Vector3d ScaleApplied;   // factors per scope axis
	Vector3d ScaleStartSize; // selection size at the start

	// Clip: the line drawn (scope space), kept after the drag until applied.
	bool bHasClipLine = false;
	bool bClipFlipped = false;
	Vector3d ClipA, ClipB;

	// Brush editing: the dot grabbed.
	enum class EDot : uint8 { None, Vertex, Edge, Face } GrabbedDot = EDot::None;
	int32 GrabbedVertex = -1;
	const Edge3d* GrabbedEdge = nullptr;
	const GPolygon* GrabbedFace = nullptr;

	bool bLooking = false;
	double LastModeTime = 0.0;
	TSet<EKey> FlyKeys;
	double LastFlyTime = 0.0;
	FVector2 CursorLocal{ -1.0f, -1.0f };

	TSharedPtr<FRHITexture> DepthBuffer;
	FSlateWindowElementList Elements;
};
