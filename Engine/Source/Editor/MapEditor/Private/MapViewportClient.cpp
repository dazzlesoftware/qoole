// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "MapViewportClient.h"

#include "MapDraw.h"
#include "MapEditorPrivate.h"

#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontCache.h"
#include "Styling/SlateStyle.h"

#include "MapShaders_PSColor_DXIL.h"
#include "MapShaders_VSMain_DXIL.h"
#include "Shaders/MapShaders_PSColor_SPIRV.h"
#include "Shaders/MapShaders_VSMain_SPIRV.h"

#include <cmath>
#include <cstring>

using namespace MapEditorPrivate;

const char* ToString(EMapViewType Type)
{
	switch (Type)
	{
	case EMapViewType::Top: return "Top";
	case EMapViewType::Front: return "Front";
	case EMapViewType::Side: return "Side";
	case EMapViewType::Perspective: return "3D";
	}
	return "?";
}

namespace
{
	constexpr FColor BackgroundColor(24, 26, 31);
	constexpr float ClipDepth2D = 8192.0f;
	constexpr float ClipDepth3D = 8192.0f;
	constexpr float DragThreshold = 4.0f; // logical pixels before a press becomes a drag
	constexpr float MinZoom = 0.02f;
	constexpr float MaxZoom = 16.0f;
	constexpr float LookSpeed = 0.0035f;  // radians per raw mouse count
	constexpr float FlySpeed = 640.0f;    // map units per second

	// Palette entries the editor draws with, besides View's VRC_* colors.
	constexpr int ColorGridMinor = 252;
	constexpr int ColorGridMajor = 253;
	constexpr int ColorGridAxis = 254;
	constexpr int ColorBox = 250;

	void SetPalette(FDraw& Draw)
	{
		Draw.SetColor(VRC_VIEW, BackgroundColor.R, BackgroundColor.G, BackgroundColor.B);
		Draw.SetColor(VRC_NORMAL, 196, 202, 212);
		Draw.SetColor(VRC_ENTITY, 110, 172, 255);
		Draw.SetColor(VRC_SELECT, 255, 92, 64);
		Draw.SetColor(VRC_HOTSELECT, 255, 168, 140);
		Draw.SetColor(VRC_MANIPDOT, 255, 220, 0);
		Draw.SetColor(VRC_FACESELECT, 255, 220, 0);
		Draw.SetColor(VRC_CROSSHAIR, 24, 100, 244);
		Draw.SetColor(VRC_CROSSHAIRNORTH, 4, 208, 24);
		Draw.SetColor(VRC_OPCENTER, 32, 192, 255);
		Draw.SetColor(VRC_LEAKGEOM, 255, 128, 0);
		Draw.SetColor(ColorGridMinor, 36, 40, 48);
		Draw.SetColor(ColorGridMajor, 54, 60, 72);
		Draw.SetColor(ColorGridAxis, 84, 94, 116);
		Draw.SetColor(ColorBox, 120, 170, 255);
	}

	float AxisComponent(const Vector3d& V, int Axis)
	{
		return Axis == 0 ? V.GetX() : (Axis == 1 ? V.GetY() : V.GetZ());
	}

	void SetAxisComponent(Vector3d& V, int Axis, float Value)
	{
		if (Axis == 0) V.SetX(Value);
		else if (Axis == 1) V.SetY(Value);
		else V.SetZ(Value);
	}

	// The scope axis a view direction runs along (2D views are axis-aligned),
	// and whether it runs backwards.
	int DominantAxis(const Vector3d& V, float& OutSign)
	{
		int Axis = 0;
		float Best = FMath::Abs(V.GetX());
		if (FMath::Abs(V.GetY()) > Best) { Axis = 1; Best = FMath::Abs(V.GetY()); }
		if (FMath::Abs(V.GetZ()) > Best) { Axis = 2; }
		OutSign = AxisComponent(V, Axis) < 0.0f ? -1.0f : 1.0f;
		return Axis;
	}

	const char* AxisName(int Axis)
	{
		return Axis == 0 ? "X" : (Axis == 1 ? "Y" : "Z");
	}

	// Which way SphrVector turns: does a larger yaw turn right, a larger
	// pitch look up? Measured once from the map core's own conversion.
	struct FTurnSigns
	{
		float Yaw = 1.0f;
		float Pitch = 1.0f;
		FTurnSigns()
		{
			Vector3d Front, Top;
			SphrVector::Sphr2Rect(SphrVector(0.1f, 0.0f, 0.0f), Front, Top);
			Yaw = Front.GetX() > 0.0f ? 1.0f : -1.0f;  // forward is +Y; right is +X
			SphrVector::Sphr2Rect(SphrVector(0.0f, 0.1f, 0.0f), Front, Top);
			Pitch = Front.GetZ() > 0.0f ? 1.0f : -1.0f;
		}
	};
	const FTurnSigns& GetTurnSigns()
	{
		static const FTurnSigns Signs;
		return Signs;
	}
}

// ---------------------------------------------------------------------------

FRHIGraphicsPipeline* FMapRenderResources::GetSolidPipeline(EPixelFormat TargetFormat)
{
	if (SolidPipeline && SolidFormat == TargetFormat)
	{
		return SolidPipeline.get();
	}
	FRHIGraphicsPipelineDesc Desc;
	const bool bVulkan = Device.GetBackend() == ERHIBackend::Vulkan;
	Desc.VertexShader = bVulkan ? FRHIShaderBytecode{ g_MapShaders_VSMain_SPIRV, sizeof(g_MapShaders_VSMain_SPIRV) }
		: FRHIShaderBytecode{ g_MapShaders_VSMain_DXIL, sizeof(g_MapShaders_VSMain_DXIL) };
	Desc.PixelShader = bVulkan ? FRHIShaderBytecode{ g_MapShaders_PSColor_SPIRV, sizeof(g_MapShaders_PSColor_SPIRV) }
		: FRHIShaderBytecode{ g_MapShaders_PSColor_DXIL, sizeof(g_MapShaders_PSColor_DXIL) };
	Desc.VertexAttributes = {
		{ "POSITION", 0, EVertexFormat::Float3, offsetof(FMapVertex, X) },
		{ "TEXCOORD", 0, EVertexFormat::Float2, offsetof(FMapVertex, U) },
		{ "COLOR", 0, EVertexFormat::UByte4N, offsetof(FMapVertex, Color) },
	};
	Desc.VertexStride = sizeof(FMapVertex);
	Desc.BlendMode = EBlendMode::Opaque;
	Desc.RenderTargetFormat = TargetFormat;
	Desc.DepthFormat = EPixelFormat::D32_FLOAT;
	Desc.PushConstantSize = 16;
	Desc.DebugName = "Map solid faces";
	SolidPipeline = Device.CreateGraphicsPipeline(Desc);
	SolidFormat = TargetFormat;
	return SolidPipeline.get();
}

FRHITexture& FMapRenderResources::GetWhiteTexture()
{
	if (!WhiteTexture)
	{
		// The pipelines' texture slot must hold something; the faces are not textured yet.
		const uint32 White = 0xFFFFFFFFu;
		FRHITextureDesc Desc;
		Desc.InitialData = &White;
		Desc.DebugName = "Map white";
		WhiteTexture = Device.CreateTexture(Desc);
	}
	return *WhiteTexture;
}

// ---------------------------------------------------------------------------

FMapViewportClient::FMapViewportClient(FRHIDevice& InDevice, FSlateRHIRenderingPolicy& InRenderer, FMapRenderResources& InResources,
	FMapViewSettings& InSettings, EMapViewType InType)
	: FRenderTargetViewportClient(InDevice, InRenderer)
	, Resources(InResources)
	, Settings(InSettings)
	, Type(InType)
	, MapDraw(std::make_unique<FMapDraw>())
{
	MapDraw->SetZoomPtr(&DrawZoom);
	SetPalette(*MapDraw);
	SettingsHandle = Settings.OnChanged.AddLambda([this] { RequestRedraw(); });
}

FMapViewportClient::~FMapViewportClient()
{
	Settings.OnChanged.Remove(SettingsHandle);
	SetDocument(nullptr);
}

void FMapViewportClient::SetDocument(FMapDocument* InDocument)
{
	if (Document)
	{
		Document->OnChanged.Remove(DocumentHandle);
	}
	if (Drag == EDrag::Move && Document && Document->IsInTransaction())
	{
		Document->EndTransaction();
	}
	Drag = EDrag::None;
	bHasClipLine = false;
	GrabbedDot = EDot::None;
	delete MapView;
	MapView = nullptr;
	Document = InDocument;
	if (Document)
	{
		DocumentHandle = Document->OnChanged.AddLambda([this](EMapChange Change) { OnDocumentChanged(Change); });
		CreateView();
	}
	RequestRedraw();
}

void FMapViewportClient::SetRenderMode(EMapRenderMode Mode)
{
	if (RenderMode != Mode)
	{
		RenderMode = Mode;
		RequestRedraw();
	}
}

void FMapViewportClient::OnDocumentChanged(EMapChange Change)
{
	if (HasAnyChange(Change, EMapChange::Scope))
	{
		FollowScope();
	}
	RequestRedraw();
}

void FMapViewportClient::FollowScope()
{
	if (!MapView || !Document)
	{
		return;
	}
	Object* OldScope = MapView->GetViewObjPtr();
	Object& NewScope = Document->GetScope();
	if (OldScope == &NewScope)
	{
		return;
	}
	// Keep the camera where it was in the map: move it into the new
	// scope's space (as the MFC views did), unless the old scope is gone.
	Vector3d Position = MapView->GetPosition();
	SphrVector Orientation = MapView->GetOrientation();
	const Object* Top = OldScope;
	while (Top && Top->GetParentPtr())
	{
		Top = Top->GetParentPtr();
	}
	if (Top == &Document->GetRoot())
	{
		Matrix44 Transform;
		Object::GetTransMatrix(*OldScope, NewScope, Transform);
		Vector3d Front, Up;
		MapView->GetOrientation(Front, Up);
		Front.AddVector(Position);
		Up.AddVector(Position);
		Transform.Transform(Position);
		Transform.Transform(Front).SubVector(Position);
		Transform.Transform(Up).SubVector(Position);
		Orientation.NewVector(Front, Up);
	}
	bHasClipLine = false;
	MapView->SetNewView(NewScope, Document->GetSelector(), true, &Position, &Orientation, IsPerspective(), 1,
		IsPerspective() ? ClipDepth3D : ClipDepth2D);
	UpdateProjection();
}

void FMapViewportClient::Message(const FString& Text) const
{
	if (Settings.OnMessage)
	{
		Settings.OnMessage(Text);
	}
}

void FMapViewportClient::CreateView()
{
	MapView = new View(MapDraw.get(), Document->GetScope(), Document->GetSelector());
	ResetCamera();
}

void FMapViewportClient::ResetCamera()
{
	if (!MapView)
	{
		return;
	}
	Vector3d Position(0.0f, 0.0f, 0.0f);
	SphrVector Orientation;
	switch (Type)
	{
	case EMapViewType::Top: Orientation.NewVector(0.0f, DEG2RAD(-90.0f), 0.0f); break;
	case EMapViewType::Front: Orientation.NewVector(0.0f, 0.0f, 0.0f); break;
	case EMapViewType::Side: Orientation.NewVector(DEG2RAD(-90.0f), 0.0f, 0.0f); break;
	case EMapViewType::Perspective:
	{
		const Vector3d Offset(-384.0f, -384.0f, 256.0f);
		Position = Offset;
		Vector3d Front(Offset);
		Front.MultVector(-1.0f);
		Orientation.NewVector(Front, Vector3d(0.0f, 0.0f, 1.0f));
		Yaw = Orientation.GetYaw();
		Pitch = Orientation.GetPitch();
		break;
	}
	}
	Zoom = 0.5f;
	MapView->SetNewView(Document->GetScope(), Document->GetSelector(), true, &Position, &Orientation, IsPerspective(), 1,
		IsPerspective() ? ClipDepth3D : ClipDepth2D);
	RequestRedraw();
}

float FMapViewportClient::GetDpiScale() const
{
	const TSharedPtr<SViewport> Widget = GetViewport();
	return Widget ? FMath::Max(Widget->GetCachedGeometry().Scale, 0.01f) : 1.0f;
}

float FMapViewportClient::GetDrawZoom() const
{
	return IsPerspective() ? 1.0f : Zoom * GetDpiScale();
}

void FMapViewportClient::UpdateProjection()
{
	const FIntPoint PixelSize = GetSize();
	const int Width = FMath::Max(PixelSize.X, 1);
	const int Height = FMath::Max(PixelSize.Y, 1);
	MapDraw->Size(Width, Height);
	DrawZoom = GetDrawZoom();
	if (MapView)
	{
		MapView->SetWindowWidth(FMath::Max(1, static_cast<int>(static_cast<float>(FMath::Max(Width, Height)) / (2.0f * DrawZoom))));
	}
}

void FMapViewportClient::ToScreen(FVector2 Local, float& OutX, float& OutY) const
{
	const float Scale = GetDpiScale();
	const FIntPoint PixelSize = GetSize();
	// Points outside the view (a drag that left it) are taken at its edge:
	// the map core's picking needs rays inside the view's clip volume.
	Local.X = FMath::Clamp(Local.X, 0.0f, static_cast<float>(PixelSize.X) / Scale);
	Local.Y = FMath::Clamp(Local.Y, 0.0f, static_cast<float>(PixelSize.Y) / Scale);
	const float Zoom2 = GetDrawZoom();
	OutX = (Local.X * Scale - static_cast<float>(FMath::Max(PixelSize.X, 1) / 2)) / Zoom2;
	OutY = (static_cast<float>(FMath::Max(PixelSize.Y, 1) / 2) - Local.Y * Scale) / Zoom2;
}

Vector3d FMapViewportClient::ViewToScope(const Vector3d& ViewVector) const
{
	Matrix44 Transform;
	MapView->CalTransSpaceMatrix(Transform.SetIdentity());
	Vector3d Origin(0.0f, 0.0f, 0.0f);
	Vector3d Result(ViewVector);
	Transform.Transform(Origin);
	Transform.Transform(Result);
	return Result.SubVector(Origin);
}

Vector3d FMapViewportClient::ScopeToView(const Vector3d& Point) const
{
	Matrix44 Transform;
	MapView->CalInvTransSpaceMatrix(Transform.SetIdentity());
	Vector3d Result(Point);
	Transform.Transform(Result);
	return Result;
}

bool FMapViewportClient::ScreenToMap(FVector2 LocalPosition, Vector3d& OutPoint) const
{
	if (!MapView || IsPerspective())
	{
		return false;
	}
	float X, Y;
	ToScreen(LocalPosition, X, Y);
	Matrix44 Transform;
	MapView->CalTransSpaceMatrix(Transform.SetIdentity());
	OutPoint.NewVector(X, 0.0f, Y);
	Transform.Transform(OutPoint);
	return true;
}

bool FMapViewportClient::MapToScreen(const Vector3d& Point, FVector2& OutLocal) const
{
	if (!MapView)
	{
		return false;
	}
	const Vector3d InView = ScopeToView(Point);
	const FIntPoint PixelSize = GetSize();
	const float Scale = GetDpiScale();
	float X = InView.GetX();
	float Y = InView.GetZ();
	if (IsPerspective())
	{
		if (InView.GetY() <= 1.0f)
		{
			return false;
		}
		const float WinWidth = static_cast<float>(FMath::Max(PixelSize.X, PixelSize.Y)) * 0.5f;
		X = X * WinWidth / InView.GetY();
		Y = Y * WinWidth / InView.GetY();
	}
	else
	{
		X *= GetDrawZoom();
		Y *= GetDrawZoom();
	}
	OutLocal = FVector2((static_cast<float>(FMath::Max(PixelSize.X, 1) / 2) + X) / Scale, (static_cast<float>(FMath::Max(PixelSize.Y, 1) / 2) - Y) / Scale);
	return true;
}

Vector3d FMapViewportClient::GetPlacementPoint() const
{
	if (!MapView)
	{
		return Vector3d(0.0f, 0.0f, 0.0f);
	}
	Vector3d Point(0.0f, IsPerspective() ? 256.0f : 0.0f, 0.0f);
	Matrix44 Transform;
	MapView->CalTransSpaceMatrix(Transform.SetIdentity());
	Transform.Transform(Point);
	return Point;
}

void FMapViewportClient::PanBy(FVector2 LocalDelta)
{
	if (IsPerspective())
	{
		MapView->MoveRelPosition(Vector3d(-LocalDelta.X, 0.0f, LocalDelta.Y));
	}
	else
	{
		MapView->MoveRelPosition(Vector3d(-LocalDelta.X / Zoom, 0.0f, LocalDelta.Y / Zoom));
	}
	RequestRedraw();
}

void FMapViewportClient::ZoomBy(float Factor)
{
	if (!MapView)
	{
		return;
	}
	if (IsPerspective())
	{
		MapView->MoveRelPosition(Vector3d(0.0f, Factor > 1.0f ? 128.0f : -128.0f, 0.0f));
	}
	else
	{
		Zoom = FMath::Clamp(Zoom * Factor, MinZoom, MaxZoom);
	}
	RequestRedraw();
}

void FMapViewportClient::FrameSelection()
{
	if (!MapView || !Document)
	{
		return;
	}
	Vector3d Min, Max;
	if (!Document->GetSelectionBounds(Min, Max))
	{
		// Nothing selected: the whole map.
		bool bAny = false;
		for (Object* Obj : Document->GetObjects())
		{
			Vector3d ObjMin, ObjMax;
			GetObjectBounds(*Obj, ObjMin, ObjMax);
			if (!bAny)
			{
				Min = ObjMin;
				Max = ObjMax;
				bAny = true;
				continue;
			}
			Min.NewVector(FMath::Min(Min.GetX(), ObjMin.GetX()), FMath::Min(Min.GetY(), ObjMin.GetY()), FMath::Min(Min.GetZ(), ObjMin.GetZ()));
			Max.NewVector(FMath::Max(Max.GetX(), ObjMax.GetX()), FMath::Max(Max.GetY(), ObjMax.GetY()), FMath::Max(Max.GetZ(), ObjMax.GetZ()));
		}
		if (!bAny)
		{
			ResetCamera();
			return;
		}
	}
	Vector3d Center(Min);
	Center.AddVector(Max).MultVector(0.5f);
	Vector3d Extent(Max);
	Extent.SubVector(Min);
	const float Radius = FMath::Max(Extent.GetMag() * 0.5f, 32.0f);

	if (IsPerspective())
	{
		// Back off along the view direction until the bounds fit (90 degree field of view).
		Vector3d Back = ViewToScope(Vector3d(0.0f, -1.0f, 0.0f));
		Back.MultVector(Radius * 1.6f);
		Vector3d Position(Center);
		Position.AddVector(Back);
		MapView->SetPosition(Position);
	}
	else
	{
		// Keep the camera's depth; center it and fit the bounds.
		Vector3d Position = MapView->GetPosition();
		Vector3d Offset(Center);
		Offset.SubVector(Position);
		const Vector3d ViewOffset = ScopeToView(Center);
		Position.AddVector(ViewToScope(Vector3d(ViewOffset.GetX(), 0.0f, ViewOffset.GetZ())));
		MapView->SetPosition(Position);
		const TSharedPtr<SViewport> Widget = GetViewport();
		const FVector2 LocalSize = Widget ? Widget->GetCachedGeometry().GetLocalSize() : FVector2(512.0f, 512.0f);
		const float Fit = FMath::Min(LocalSize.X, LocalSize.Y) * 0.45f / Radius;
		Zoom = FMath::Clamp(Fit, MinZoom, MaxZoom);
	}
	RequestRedraw();
}

// ----- Input -----

FReply FMapViewportClient::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	const TSharedPtr<SViewport> Widget = GetViewport();
	if (!Widget || !MapView || Drag != EDrag::None)
	{
		return FReply::Unhandled();
	}
	const TSharedRef<SWidget> Self = ToSharedRef(StaticCastSharedPtr<SWidget>(Widget));
	const FVector2 Local = Geometry.AbsoluteToLocal(Event.GetAbsolutePosition());
	DragStart = DragLast = Local;
	DragButton = Event.GetEffectingButton();
	switch (DragButton)
	{
	case EMouseButton::Left:
		Drag = EDrag::Pending;
		bDragAdd = Event.IsShiftDown();
		if (Settings.Tool == EMapTool::Vertex || Settings.Tool == EMapTool::Edge || Settings.Tool == EMapTool::Face)
		{
			PickBrushDot(Local);
		}
		return FReply::Handled().CaptureMouse(Self);
	case EMouseButton::Middle:
		Drag = EDrag::Pan;
		return FReply::Handled().CaptureMouse(Self);
	case EMouseButton::Right:
		if (IsPerspective())
		{
			Drag = EDrag::Look;
			bLooking = true;
			LastFlyTime = FPlatformTime::Seconds();
			RequestRedraw();
			return FReply::Handled().UseHighPrecisionMouseMovement(Self);
		}
		Drag = EDrag::Pan;
		return FReply::Handled().CaptureMouse(Self);
	default:
		break;
	}
	return FReply::Unhandled();
}

FReply FMapViewportClient::OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Drag == EDrag::None || Event.GetEffectingButton() != DragButton)
	{
		return FReply::Unhandled();
	}
	const FVector2 Local = Geometry.AbsoluteToLocal(Event.GetAbsolutePosition());
	switch (Drag)
	{
	case EDrag::Pending:
		SelectAt(Local, Event);
		break;
	case EDrag::BoxSelect:
		UpdateBoxSelection(true);
		break;
	case EDrag::Move:
		Document->EndTransaction();
		MovedObjects.clear();
		break;
	case EDrag::Look:
		bLooking = false;
		FlyKeys.clear();
		break;
	case EDrag::Rotate:
	case EDrag::Scale:
	case EDrag::ClipLine:
	case EDrag::BrushEdit:
		EndToolDrag();
		break;
	default:
		break;
	}
	GrabbedDot = EDot::None;
	Drag = EDrag::None;
	RequestRedraw();
	return FReply::Handled().ReleaseMouseCapture();
}

FReply FMapViewportClient::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	const FVector2 Local = Geometry.AbsoluteToLocal(Event.GetAbsolutePosition());
	CursorLocal = Local;
	ReportCursor(Local);
	if (!MapView)
	{
		return FReply::Unhandled();
	}
	const FVector2 Delta = Local - DragLast;
	DragLast = Local;
	switch (Drag)
	{
	case EDrag::Pending:
	{
		if ((Local - DragStart).Size() < DragThreshold)
		{
			return FReply::Handled();
		}
		UpdateProjection();
		if (BeginToolDrag(Local, Event))
		{
			return FReply::Handled();
		}
		float X, Y;
		ToScreen(DragStart, X, Y);
		const bool bOnSelection = !IsPerspective() && !Event.IsShiftDown() && !Event.IsControlDown()
			&& Document->GetSelector().GetNumMSelectedObjects() > 0 && MapView->IsMultiSelectionHit(X, Y);
		if (bOnSelection)
		{
			Drag = EDrag::Move;
			MovedObjects = Document->GetSelection();
			Vector3d Max;
			Document->GetSelectionBounds(MoveReference, Max);
			MoveApplied.NewVector(0.0f, 0.0f, 0.0f);
			Document->BeginTransaction(MovedObjects.size() == 1 ? "Move Object" : FString::Format("Move {} Objects", MovedObjects.size()));
			UpdateMove(Local, Settings.bSnapToGrid && !Event.IsAltDown());
		}
		else
		{
			Drag = EDrag::BoxSelect;
			SelectionBeforeBox = Document->GetSelection();
			UpdateBoxSelection(false);
		}
		return FReply::Handled();
	}
	case EDrag::BoxSelect:
		UpdateBoxSelection(false);
		return FReply::Handled();
	case EDrag::Move:
		UpdateMove(Local, Settings.bSnapToGrid && !Event.IsAltDown());
		return FReply::Handled();
	case EDrag::Pan:
		PanBy(Delta);
		return FReply::Handled();
	case EDrag::Rotate:
	case EDrag::Scale:
	case EDrag::ClipLine:
	case EDrag::BrushEdit:
		UpdateToolDrag(Local, Event);
		return FReply::Handled();
	default:
		break;
	}
	return FReply::Unhandled();
}

FReply FMapViewportClient::OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!MapView)
	{
		return FReply::Unhandled();
	}
	const float Wheel = Event.GetWheelDelta();
	if (IsPerspective())
	{
		MapView->MoveRelPosition(Vector3d(0.0f, Wheel * 96.0f, 0.0f));
		RequestRedraw();
		return FReply::Handled();
	}
	// Zoom about the cursor: the map point under it stays put.
	const FVector2 Local = Geometry.AbsoluteToLocal(Event.GetAbsolutePosition());
	const FVector2 Center = Geometry.GetLocalSize() * 0.5f;
	const FVector2 Offset = Local - Center;
	const float OldZoom = Zoom;
	Zoom = FMath::Clamp(Zoom * std::pow(1.2f, Wheel), MinZoom, MaxZoom);
	const float Shift = 1.0f / OldZoom - 1.0f / Zoom;
	MapView->MoveRelPosition(Vector3d(Offset.X * Shift, 0.0f, -Offset.Y * Shift));
	ReportCursor(Local);
	RequestRedraw();
	return FReply::Handled();
}

FReply FMapViewportClient::OnRawMouseMove(FVector2 Delta)
{
	if (Drag != EDrag::Look || !MapView)
	{
		return FReply::Unhandled();
	}
	const FTurnSigns& Signs = GetTurnSigns();
	Yaw += Signs.Yaw * Delta.X * LookSpeed;
	Pitch = FMath::Clamp(Pitch - Signs.Pitch * Delta.Y * LookSpeed, DEG2RAD(-89.0f), DEG2RAD(89.0f));
	MapView->SetOrientation(SphrVector(Yaw, Pitch, 0.0f));
	RequestRedraw();
	return FReply::Handled();
}

void FMapViewportClient::OnMouseLeave(const FPointerEvent&)
{
	CursorLocal = FVector2(-1.0f, -1.0f);
	if (Settings.OnCursorText)
	{
		Settings.OnCursorText(FString());
	}
}

void FMapViewportClient::OnMouseCaptureLost()
{
	if (Drag == EDrag::Move && Document && Document->IsInTransaction())
	{
		Document->EndTransaction();
	}
	if (Drag == EDrag::BoxSelect && Document)
	{
		UpdateBoxSelection(true);
	}
	if (Document && (Drag == EDrag::Rotate || Drag == EDrag::Scale || Drag == EDrag::BrushEdit))
	{
		CancelToolDrag();
	}
	GrabbedDot = EDot::None;
	Drag = EDrag::None;
	bLooking = false;
	FlyKeys.clear();
	MovedObjects.clear();
	RequestRedraw();
}

FReply FMapViewportClient::OnKeyDown(const FGeometry&, const FKeyEvent& Event)
{
	if (ToolKeyDown(Event).IsEventHandled())
	{
		return FReply::Handled();
	}
	// Flying keys only count while looking around (right button held), so
	// the same letters stay shortcuts otherwise.
	if (bLooking)
	{
		switch (Event.GetKey())
		{
		case EKey::W: case EKey::A: case EKey::S: case EKey::D: case EKey::Q: case EKey::E:
		case EKey::Up: case EKey::Down: case EKey::Left: case EKey::Right:
		case EKey::LeftShift: case EKey::RightShift:
			FlyKeys.insert(Event.GetKey());
			return FReply::Handled();
		default:
			break;
		}
	}
	return FReply::Unhandled();
}

FReply FMapViewportClient::OnKeyUp(const FGeometry&, const FKeyEvent& Event)
{
	if (FlyKeys.erase(Event.GetKey()) > 0)
	{
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void FMapViewportClient::OnFocusLost(const FFocusEvent&)
{
	FlyKeys.clear();
}

FCursorReply FMapViewportClient::OnCursorQuery(const FGeometry&, const FPointerEvent&) const
{
	switch (Drag)
	{
	case EDrag::Move: return FCursorReply::Cursor(EMouseCursor::CardinalCross);
	case EDrag::Pan: return FCursorReply::Cursor(EMouseCursor::GrabHand);
	case EDrag::Look: return FCursorReply::Cursor(EMouseCursor::None);
	default: break;
	}
	return FCursorReply::Cursor(EMouseCursor::Default);
}

void FMapViewportClient::SelectAt(FVector2 Local, const FPointerEvent& Event)
{
	UpdateProjection(); // the view may have zoomed since it last drew
	float X, Y;
	ToScreen(Local, X, Y);
	if (Event.IsControlDown() || Event.IsShiftDown())
	{
		if (Object* Hit = MapView->SingleForwardSelect(X, Y))
		{
			Document->ToggleSelection(Hit);
		}
		return;
	}
	// Clicking again cycles through the objects under the cursor.
	Object* Hit = Event.IsAltDown() ? MapView->MultiReverseSelect(X, Y) : MapView->MultiForwardSelect(X, Y);
	Document->SetSelection(Hit ? TArray<Object*>{ Hit } : TArray<Object*>{});
}

void FMapViewportClient::UpdateBoxSelection(bool bFinal)
{
	UpdateProjection(); // the view may have zoomed since it last drew
	float X1, Y1, X2, Y2;
	ToScreen(DragStart, X1, Y1);
	ToScreen(DragLast, X2, Y2);
	LinkList<ObjectPtr> Found;
	MapView->DragSelectMultiObjects(FMath::Min(X1, X2), FMath::Min(Y1, Y2), FMath::Max(X1, X2), FMath::Max(Y1, Y2), Found);
	TArray<Object*> Selection = bDragAdd ? SelectionBeforeBox : TArray<Object*>{};
	for (Object* Obj : ToArray(Found))
	{
		Selection.push_back(Obj);
	}
	Document->SetSelection(Selection);
	if (bFinal)
	{
		SelectionBeforeBox.clear();
	}
	RequestRedraw();
}

void FMapViewportClient::UpdateMove(FVector2 Local, bool bSnap)
{
	const FVector2 Total = Local - DragStart;
	const Vector3d ScopeDelta = ViewToScope(Vector3d(Total.X / Zoom, 0.0f, -Total.Y / Zoom));
	Vector3d Goal(MoveReference);
	Goal.AddVector(ScopeDelta);
	{
		// Moves stay in the view's plane (rounding would drift the depth axis).
		float Sign;
		const int DepthAxis = DominantAxis(ViewToScope(Vector3d(0.0f, 1.0f, 0.0f)), Sign);
		SetAxisComponent(Goal, DepthAxis, AxisComponent(MoveReference, DepthAxis));
	}
	if (bSnap)
	{
		// Snap the selection's corner to the grid on the view's two axes.
		float Sign;
		const int RightAxis = DominantAxis(ViewToScope(Vector3d(1.0f, 0.0f, 0.0f)), Sign);
		const int UpAxis = DominantAxis(ViewToScope(Vector3d(0.0f, 0.0f, 1.0f)), Sign);
		for (const int Axis : { RightAxis, UpAxis })
		{
			const float Grid = Settings.GridSize;
			SetAxisComponent(Goal, Axis, std::round(AxisComponent(Goal, Axis) / Grid) * Grid);
		}
	}
	Vector3d Wanted(Goal);
	Wanted.SubVector(MoveReference);
	Vector3d Step(Wanted);
	Step.SubVector(MoveApplied);
	if (!(Step == Vector3d(0.0f, 0.0f, 0.0f)))
	{
		Document->MoveObjects(MovedObjects, Step);
		MoveApplied = Wanted;
	}
}

void FMapViewportClient::ReportCursor(FVector2 Local)
{
	if (!Settings.OnCursorText)
	{
		return;
	}
	Vector3d Point;
	if (!ScreenToMap(Local, Point))
	{
		Settings.OnCursorText(FString());
		return;
	}
	float Sign;
	const int RightAxis = DominantAxis(ViewToScope(Vector3d(1.0f, 0.0f, 0.0f)), Sign);
	const int UpAxis = DominantAxis(ViewToScope(Vector3d(0.0f, 0.0f, 1.0f)), Sign);
	Settings.OnCursorText(FString::Format("{} {:.0f}   {} {:.0f}", AxisName(RightAxis), AxisComponent(Point, RightAxis), AxisName(UpAxis),
		AxisComponent(Point, UpAxis)));
}

void FMapViewportClient::Fly(float DeltaSeconds)
{
	if (!MapView)
	{
		return;
	}
	const auto Held = [this](EKey A, EKey B) { return FlyKeys.contains(A) || FlyKeys.contains(B); };
	Vector3d Move(0.0f, 0.0f, 0.0f);
	if (Held(EKey::W, EKey::Up)) Move.SetY(Move.GetY() + 1.0f);
	if (Held(EKey::S, EKey::Down)) Move.SetY(Move.GetY() - 1.0f);
	if (Held(EKey::D, EKey::Right)) Move.SetX(Move.GetX() + 1.0f);
	if (Held(EKey::A, EKey::Left)) Move.SetX(Move.GetX() - 1.0f);
	if (Held(EKey::E, EKey::E)) Move.SetZ(Move.GetZ() + 1.0f);
	if (Held(EKey::Q, EKey::Q)) Move.SetZ(Move.GetZ() - 1.0f);
	const bool bFast = Held(EKey::LeftShift, EKey::RightShift);
	Move.MultVector(FlySpeed * (bFast ? 4.0f : 1.0f) * FMath::Min(DeltaSeconds, 0.1f));
	MapView->MoveRelPosition(Move);
}

// ----- Drawing -----

void FMapViewportClient::DrawGrid(FMapDraw& Draw)
{
	const FIntPoint PixelSize = GetSize();
	const float Width = static_cast<float>(PixelSize.X);
	const float Height = static_cast<float>(PixelSize.Y);
	const float OrgX = static_cast<float>(PixelSize.X / 2);
	const float OrgY = static_cast<float>(PixelSize.Y / 2);
	const float Scale = GetDpiScale();

	float RightSign, UpSign;
	const int RightAxis = DominantAxis(ViewToScope(Vector3d(1.0f, 0.0f, 0.0f)), RightSign);
	const int UpAxis = DominantAxis(ViewToScope(Vector3d(0.0f, 0.0f, 1.0f)), UpSign);
	const Vector3d& Camera = MapView->GetPosition();
	const float CameraRight = AxisComponent(Camera, RightAxis);
	const float CameraUp = AxisComponent(Camera, UpAxis);

	// Minor lines at the grid size (doubled until they are far enough apart),
	// major lines every 64 units or more.
	float Minor = FMath::Max(Settings.GridSize, 1.0f);
	while (Minor * DrawZoom < 7.0f * Scale)
	{
		Minor *= 2.0f;
	}
	float Major = 64.0f;
	while (Major < Minor * 4.0f)
	{
		Major *= 2.0f;
	}

	const auto ScreenX = [&](float Value) { return OrgX + RightSign * (Value - CameraRight) * DrawZoom; };
	const auto ScreenY = [&](float Value) { return OrgY - UpSign * (Value - CameraUp) * DrawZoom; };
	const float HalfW = OrgX / DrawZoom + Minor;
	const float HalfH = OrgY / DrawZoom + Minor;

	for (const int Pass : { 0, 1 })
	{
		const float Step = Pass == 0 ? Minor : Major;
		Draw.Color(Pass == 0 ? ColorGridMinor : ColorGridMajor);
		for (float V = std::floor((CameraRight - HalfW) / Step) * Step; V <= CameraRight + HalfW; V += Step)
		{
			if (Pass == 0 && std::fmod(std::fabs(V), Major) < 0.01f)
			{
				continue; // drawn as a major line
			}
			const int X = static_cast<int>(std::floor(ScreenX(V)));
			Draw.Line(X, 0, X, static_cast<int>(Height));
		}
		for (float V = std::floor((CameraUp - HalfH) / Step) * Step; V <= CameraUp + HalfH; V += Step)
		{
			if (Pass == 0 && std::fmod(std::fabs(V), Major) < 0.01f)
			{
				continue;
			}
			const int Y = static_cast<int>(std::floor(ScreenY(V)));
			Draw.Line(0, Y, static_cast<int>(Width), Y);
		}
	}
	Draw.Color(ColorGridAxis);
	const int AxisX = static_cast<int>(std::floor(ScreenX(0.0f)));
	const int AxisY = static_cast<int>(std::floor(ScreenY(0.0f)));
	Draw.Line(AxisX, 0, AxisX, static_cast<int>(Height));
	Draw.Line(0, AxisY, static_cast<int>(Width), AxisY);
}

void FMapViewportClient::DrawOverlay(FSlateWindowElementList& Out, FIntPoint PixelSize)
{
	const float Scale = GetDpiScale();
	const auto Geo = [Scale](FVector2 Position, FVector2 Extent) { return FPaintGeometry::Make(Position, Extent, Scale); };

	if (Drag == EDrag::BoxSelect)
	{
		const FVector2 Min(FMath::Min(DragStart.X, DragLast.X), FMath::Min(DragStart.Y, DragLast.Y));
		const FVector2 Max(FMath::Max(DragStart.X, DragLast.X), FMath::Max(DragStart.Y, DragLast.Y));
		Out.AddSolidBox(2, Geo(Min, Max - Min), FColor(120, 170, 255, 40));
		Out.AddLines(3, Geo(FVector2::Zero(), PixelSize.ToVector2() / Scale), { Min, FVector2(Max.X, Min.Y), Max, FVector2(Min.X, Max.Y) },
			FColor(120, 170, 255), 1.0f, true);
	}

	const FSlateFontInfo Font = FAppStyle::Get().GetFont("NormalFont");
	if (!Font.Font)
	{
		return;
	}
	FString Label;
	if (IsPerspective())
	{
		Label = RenderMode == EMapRenderMode::Solid ? FString("3D  ·  Solid") : FString("3D  ·  Wireframe");
	}
	else if (MapView)
	{
		float Sign;
		const int RightAxis = DominantAxis(ViewToScope(Vector3d(1.0f, 0.0f, 0.0f)), Sign);
		const int UpAxis = DominantAxis(ViewToScope(Vector3d(0.0f, 0.0f, 1.0f)), Sign);
		Label = FString::Format("{}  ·  {}/{}  ·  {:.0f}%", ToString(Type), AxisName(RightAxis), AxisName(UpAxis), Zoom * 100.0f);
		const FString Hint = GetToolHint();
		if (!Hint.IsEmpty())
		{
			Label += FString::Format("  ·  {}", Hint);
		}
	}
	if (Document && &Document->GetScope() != &Document->GetRoot())
	{
		const char* Name = Document->GetScope().GetObjName();
		Label += FString::Format("  ·  in {}", Name && Name[0] ? FString::FromAnsi(Name) : FString("a group"));
	}
	FSlateFontCache& Fonts = FSlateApplication::Get().GetFontCache();
	const TSharedRef<const FShapedGlyphSequence> Text = Fonts.ShapeText(Label.View(), FSlateFontInfo(Font.Font, 11.0f), Scale);
	const FVector2 TextSize = Text->GetLogicalSize();
	Out.AddRoundedBox(4, Geo(FVector2(6.0f, 6.0f), TextSize + FVector2(12.0f, 6.0f)), FColor(12, 13, 16, 190), 4.0f);
	Out.AddText(5, Geo(FVector2(12.0f, 9.0f), TextSize), Text, FColor(176, 184, 198));
}

void FMapViewportClient::Draw(FRHICommandList& CommandList, FSlateRenderTargetRHI& RenderTarget)
{
	const FIntPoint PixelSize = RenderTarget.GetSize();
	const FLinearColor Clear(BackgroundColor.R / 255.0f, BackgroundColor.G / 255.0f, BackgroundColor.B / 255.0f, 1.0f);
	Elements.Reset();
	Elements.SetViewportSize(PixelSize);
	if (!MapView || !Document)
	{
		Renderer.Render(CommandList, RenderTarget.GetTexture(), Elements, Clear);
		return;
	}

	if (bLooking)
	{
		const double Now = FPlatformTime::Seconds();
		Fly(static_cast<float>(Now - LastFlyTime));
		LastFlyTime = Now;
	}

	UpdateProjection();
	MapDraw->SetLineThickness(FMath::Max(1.0f, std::floor(GetDpiScale() + 0.25f)));
	MapDraw->Reset();
	const bool bSolid = IsPerspective() && RenderMode == EMapRenderMode::Solid;
	if (bSolid)
	{
		MapDraw->ZBufTextured(false);
		MapView->RenderSolid(false);
		// The selection on top of the faces.
		MapView->RenderWireFrame(false, true, false);
	}
	else
	{
		if (!IsPerspective() && Settings.bShowGrid)
		{
			DrawGrid(*MapDraw);
		}
		UpdateDotDisplay();
		MapView->RenderWireFrame(true, true, false);
	}
	DrawTools(*MapDraw);
	MapDraw->FlushLines(Elements, 1);
	DrawOverlay(Elements, PixelSize);

	const TArray<FMapVertex>& Triangles = MapDraw->GetTriangles();
	FRHIGraphicsPipeline* Pipeline = bSolid && !Triangles.empty() ? Resources.GetSolidPipeline(RenderTarget.GetTexture().GetFormat()) : nullptr;
	if (!Pipeline)
	{
		Renderer.Render(CommandList, RenderTarget.GetTexture(), Elements, Clear);
		return;
	}

	if (!DepthBuffer || DepthBuffer->GetWidth() != static_cast<uint32>(PixelSize.X) || DepthBuffer->GetHeight() != static_cast<uint32>(PixelSize.Y))
	{
		if (DepthBuffer)
		{
			Device.DeferRelease(DepthBuffer);
		}
		FRHITextureDesc DepthDesc;
		DepthDesc.Width = static_cast<uint32>(PixelSize.X);
		DepthDesc.Height = static_cast<uint32>(PixelSize.Y);
		DepthDesc.Format = EPixelFormat::D32_FLOAT;
		DepthDesc.DebugName = "Map view depth";
		DepthBuffer = Device.CreateTexture(DepthDesc);
	}

	// One-frame buffers, released once the GPU is done with them.
	FRHIBufferDesc VertexDesc;
	VertexDesc.Size = Triangles.size() * sizeof(FMapVertex);
	VertexDesc.Usage = EBufferUsage::Vertex;
	VertexDesc.DebugName = "Map faces";
	TSharedPtr<FRHIBuffer> Vertices = Device.CreateBuffer(VertexDesc);
	TArray<uint32> IndexData(Triangles.size());
	for (SIZE_T Index = 0; Index < IndexData.size(); ++Index)
	{
		IndexData[Index] = static_cast<uint32>(Index);
	}
	FRHIBufferDesc IndexDesc;
	IndexDesc.Size = IndexData.size() * sizeof(uint32);
	IndexDesc.Usage = EBufferUsage::Index;
	IndexDesc.DebugName = "Map face indices";
	TSharedPtr<FRHIBuffer> Indices = Device.CreateBuffer(IndexDesc);
	if (!DepthBuffer || !Vertices || !Indices)
	{
		Renderer.Render(CommandList, RenderTarget.GetTexture(), Elements, Clear);
		return;
	}
	std::memcpy(Vertices->GetMappedData(), Triangles.data(), VertexDesc.Size);
	std::memcpy(Indices->GetMappedData(), IndexData.data(), IndexDesc.Size);

	CommandList.BeginRenderPass(RenderTarget.GetTexture(), *DepthBuffer, Clear, 1.0f);
	CommandList.SetPipeline(*Pipeline);
	const float Constants[4] = { 2.0f / static_cast<float>(PixelSize.X), 2.0f / static_cast<float>(PixelSize.Y), 0.0f, 0.0f };
	CommandList.SetPushConstants(Constants, sizeof(Constants));
	CommandList.SetTexture(Resources.GetWhiteTexture());
	CommandList.SetVertexBuffer(*Vertices, 0, sizeof(FMapVertex));
	CommandList.SetIndexBuffer(*Indices, 0, EIndexFormat::UInt32);
	CommandList.DrawIndexed(static_cast<uint32>(IndexData.size()), 0, 0);
	CommandList.EndRenderPass();
	Device.DeferRelease(Vertices);
	Device.DeferRelease(Indices);

	// Lines and labels over the faces.
	Renderer.Render(CommandList, RenderTarget.GetTexture(), Elements, {});
}
