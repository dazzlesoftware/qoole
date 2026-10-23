// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// The tools of the 2D map views (FMapViewSettings::Tool): rotate, scale,
// clip and brush editing. Select and move are in MapViewportClient.cpp.

#include "MapViewportClient.h"
#include "Containers/ContainerAliases.h"
#include "Internationalization/TextLocalization.h"

#include "MapDraw.h"
#include "MapEditorPrivate.h"

#include <cmath>

using namespace MapEditorPrivate;

const char* ToString(EMapTool Tool)
{
	switch (Tool)
	{
	case EMapTool::Select:
		return "Select";
	case EMapTool::Rotate:
		return "Rotate";
	case EMapTool::Scale:
		return "Scale";
	case EMapTool::Clip:
		return "Clip";
	case EMapTool::Vertex:
		return "Vertices";
	case EMapTool::Edge:
		return "Edges";
	case EMapTool::Face:
		return "Faces";
	}
	return "?";
}

namespace
{
	constexpr int ColorTool = 250;
	constexpr float DotPickRadius = 7.0f; // logical pixels
	constexpr float MinScale = 1.0f / 64.0f;

	float Component(const Vector3d& V, int32 Axis)
	{
		return Axis == 0 ? V.GetX() : (Axis == 1 ? V.GetY() : V.GetZ());
	}

	void SetComponent(Vector3d& V, int32 Axis, float Value)
	{
		if (Axis == 0)
			V.SetX(Value);
		else if (Axis == 1)
			V.SetY(Value);
		else
			V.SetZ(Value);
	}

	int32 Dominant(const Vector3d& V, float& OutSign)
	{
		int32 Axis = 0;
		float Best = FMath::Abs(V.GetX());
		if (FMath::Abs(V.GetY()) > Best)
		{
			Axis = 1;
			Best = FMath::Abs(V.GetY());
		}
		if (FMath::Abs(V.GetZ()) > Best)
		{
			Axis = 2;
		}
		OutSign = Component(V, Axis) < 0.0f ? -1.0f : 1.0f;
		return Axis;
	}

	// Whether Matrix44::SetRotateX/Y/Z turn counterclockwise (right-handed)
	// for a positive angle, per axis; measured once.
	struct FRotationSigns
	{
		float Sign[3] = {1.0f, 1.0f, 1.0f};
		FRotationSigns()
		{
			Matrix44 M;
			Vector3d V(0.0f, 1.0f, 0.0f);
			M.SetRotateX(0.1f);
			M.Transform(V);
			Sign[0] = V.GetZ() > 0.0f ? 1.0f : -1.0f;
			V.NewVector(0.0f, 0.0f, 1.0f);
			M.SetRotateY(0.1f);
			M.Transform(V);
			Sign[1] = V.GetX() > 0.0f ? 1.0f : -1.0f;
			V.NewVector(1.0f, 0.0f, 0.0f);
			M.SetRotateZ(0.1f);
			M.Transform(V);
			Sign[2] = V.GetY() > 0.0f ? 1.0f : -1.0f;
		}
	};

	float ScreenAngle(FVector2 From, FVector2 To)
	{
		// Counterclockwise on screen (y grows downwards), in degrees.
		return RAD2DEG(std::atan2(-(To.Y - From.Y), To.X - From.X));
	}
} // namespace

void FMapViewportClient::GetViewAxes(
    int32& OutRight, float& OutRightSign, int32& OutUp, float& OutUpSign, int32& OutDepth) const
{
	OutRight = Dominant(ViewToScope(Vector3d(1.0f, 0.0f, 0.0f)), OutRightSign);
	OutUp = Dominant(ViewToScope(Vector3d(0.0f, 0.0f, 1.0f)), OutUpSign);
	OutDepth = 3 - OutRight - OutUp;
}

Object* FMapViewportClient::GetSingleBrush() const
{
	if (!Document)
	{
		return nullptr;
	}
	const TArray<Object*> Selection = Document->GetSelection();
	return Selection.size() == 1 && Selection[0]->HasBrush() ? Selection[0] : nullptr;
}

void FMapViewportClient::UpdateDotDisplay()
{
	if (!MapView)
	{
		return;
	}
	const bool bBrush = !IsPerspective() && GetSingleBrush() != nullptr;
	switch (bBrush ? Settings.Tool : EMapTool::Select)
	{
	case EMapTool::Vertex:
		MapView->DisplayVertexPts();
		break;
	case EMapTool::Edge:
		MapView->DisplayEdgePts();
		break;
	case EMapTool::Face:
		MapView->DisplayFacePts();
		break;
	default:
		MapView->DisplayNoPts();
		break;
	}
}

bool FMapViewportClient::PickBrushDot(FVector2 Local)
{
	GrabbedDot = EDot::None;
	if (IsPerspective() || !GetSingleBrush())
	{
		return false;
	}
	UpdateProjection();
	UpdateDotDisplay();
	float X, Y;
	ToScreen(Local, X, Y);
	const float Epsilon = DotPickRadius * GetDpiScale() / GetDrawZoom();
	switch (Settings.Tool)
	{
	case EMapTool::Vertex:
		GrabbedVertex = MapView->SelectVertex(X, Y, Epsilon);
		GrabbedDot = GrabbedVertex >= 0 ? EDot::Vertex : EDot::None;
		break;
	case EMapTool::Edge:
		GrabbedEdge = MapView->SelectEdge(X, Y, Epsilon);
		GrabbedDot = GrabbedEdge ? EDot::Edge : EDot::None;
		break;
	case EMapTool::Face:
		GrabbedFace = MapView->SelectFace(X, Y, Epsilon);
		GrabbedDot = GrabbedFace ? EDot::Face : EDot::None;
		break;
	default:
		break;
	}
	RequestRedraw();
	return GrabbedDot != EDot::None;
}

bool FMapViewportClient::BeginToolDrag(FVector2 Local, const FPointerEvent& Event)
{
	if (IsPerspective() || !Document)
	{
		return false;
	}
	const TArray<Object*> Selection = Document->GetSelection();
	switch (Settings.Tool)
	{
	case EMapTool::Rotate:
	case EMapTool::Scale:
	{
		if (Selection.empty())
		{
			return false;
		}
		MovedObjects = Selection;
		Vector3d Min, Max;
		Document->GetSelectionBounds(Min, Max);
		Vector3d Center(Min);
		Center.AddVector(Max).MultVector(0.5f);
		Vector3d Extent(Max);
		ScaleStartSize = Extent.SubVector(Min);
		if (Settings.Tool == EMapTool::Rotate)
		{
			Drag = EDrag::Rotate;
			Pivot = Center;
			RotateApplied = 0.0f;
			Document->BeginTransaction("Rotate");
		}
		else
		{
			// Scale from the corner opposite to where the drag started.
			Drag = EDrag::Scale;
			Vector3d StartPoint;
			ScreenToMap(DragStart, StartPoint);
			Pivot = Center;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				SetComponent(Pivot, Axis,
				    Component(StartPoint, Axis) >= Component(Center, Axis) ? Component(Min, Axis)
				                                                           : Component(Max, Axis));
			}
			ScaleApplied.NewVector(1.0f, 1.0f, 1.0f);
			Document->BeginTransaction("Scale");
		}
		UpdateToolDrag(Local, Event);
		return true;
	}
	case EMapTool::Clip:
		Drag = EDrag::ClipLine;
		bHasClipLine = true;
		bClipFlipped = false;
		UpdateToolDrag(Local, Event);
		return true;
	case EMapTool::Vertex:
	case EMapTool::Edge:
	case EMapTool::Face:
	{
		Object* Brush = GetSingleBrush();
		if (GrabbedDot == EDot::None || !Brush)
		{
			return false;
		}
		Drag = EDrag::BrushEdit;
		static const char* const Names[] = {"", "Move Vertex", "Move Edge", "Move Face"};
		Document->BeginBrushEdit(*Brush, Names[static_cast<int32>(GrabbedDot)]);
		MoveApplied.NewVector(0.0f, 0.0f, 0.0f);
		UpdateToolDrag(Local, Event);
		return true;
	}
	default:
		break;
	}
	return false;
}

void FMapViewportClient::UpdateToolDrag(FVector2 Local, const FPointerEvent& Event)
{
	const bool bSnap = Settings.bSnapToGrid && !Event.IsAltDown();
	const float Grid = Settings.GridSize;
	int32 Right, Up, Depth;
	float RightSign, UpSign;
	GetViewAxes(Right, RightSign, Up, UpSign, Depth);

	switch (Drag)
	{
	case EDrag::Rotate:
	{
		FVector2 PivotLocal;
		if (!MapToScreen(Pivot, PivotLocal))
		{
			return;
		}
		float Angle = ScreenAngle(PivotLocal, Local) - ScreenAngle(PivotLocal, DragStart);
		while (Angle > 180.0f)
			Angle -= 360.0f;
		while (Angle < -180.0f)
			Angle += 360.0f;
		if (bSnap && Settings.GetRotateSnap() > 0.0f)
		{
			Angle = std::round(Angle / Settings.GetRotateSnap()) * Settings.GetRotateSnap();
		}
		const float Step = Angle - RotateApplied;
		if (Step != 0.0f)
		{
			// Counterclockwise on screen turns about right x up, the axis
			// pointing out of the screen.
			Vector3d Out;
			Out.CrossVector(ViewToScope(Vector3d(1.0f, 0.0f, 0.0f)), ViewToScope(Vector3d(0.0f, 0.0f, 1.0f)));
			static const FRotationSigns Signs;
			const float Sign = (Component(Out, Depth) < 0.0f ? -1.0f : 1.0f) * Signs.Sign[Depth];
			Document->RotateObjects(MovedObjects, Depth, Sign * Step, Pivot);
			RotateApplied = Angle;
		}
		Message(FString::Format("Rotate {:.0f}°", Angle));
		break;
	}
	case EDrag::Scale:
	{
		FVector2 PivotLocal;
		if (!MapToScreen(Pivot, PivotLocal))
		{
			return;
		}
		const FVector2 From = DragStart - PivotLocal;
		const FVector2 To = Local - PivotLocal;
		float FactorX = FMath::Abs(From.X) > 4.0f ? To.X / From.X : 1.0f;
		float FactorY = FMath::Abs(From.Y) > 4.0f ? To.Y / From.Y : 1.0f;
		FactorX = FMath::Max(FactorX, MinScale);
		FactorY = FMath::Max(FactorY, MinScale);
		Vector3d Wanted(1.0f, 1.0f, 1.0f);
		if (Event.IsShiftDown())
		{
			const float Even = FMath::Max(FactorX, FactorY);
			Wanted.NewVector(Even, Even, Even);
		}
		else
		{
			SetComponent(Wanted, Right, FactorX);
			SetComponent(Wanted, Up, FactorY);
		}
		if (bSnap)
		{
			// Sizes in whole grid steps.
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				const float StartSize = Component(ScaleStartSize, Axis);
				if (StartSize > 0.01f && Component(Wanted, Axis) != 1.0f)
				{
					const float Snapped =
					    FMath::Max(Grid, std::round(StartSize * Component(Wanted, Axis) / Grid) * Grid);
					SetComponent(Wanted, Axis, Snapped / StartSize);
				}
			}
		}
		const Vector3d Ratio(Wanted.GetX() / ScaleApplied.GetX(), Wanted.GetY() / ScaleApplied.GetY(),
		    Wanted.GetZ() / ScaleApplied.GetZ());
		if (!(Ratio == Vector3d(1.0f, 1.0f, 1.0f)))
		{
			Document->ScaleObjects(MovedObjects, Ratio, Pivot);
			ScaleApplied = Wanted;
		}
		Message(FString::Format("Scale {:.0f} x {:.0f}", Component(ScaleStartSize, Right) * Component(Wanted, Right),
		    Component(ScaleStartSize, Up) * Component(Wanted, Up)));
		break;
	}
	case EDrag::ClipLine:
	{
		ScreenToMap(DragStart, ClipA);
		ScreenToMap(Local, ClipB);
		if (bSnap)
		{
			for (const int32 Axis : {Right, Up})
			{
				SetComponent(ClipA, Axis, std::round(Component(ClipA, Axis) / Grid) * Grid);
				SetComponent(ClipB, Axis, std::round(Component(ClipB, Axis) / Grid) * Grid);
			}
		}
		RequestRedraw();
		break;
	}
	case EDrag::BrushEdit:
	{
		const FVector2 Total = Local - DragStart;
		Vector3d Wanted = ViewToScope(Vector3d(Total.X / Zoom, 0.0f, -Total.Y / Zoom));
		SetComponent(Wanted, Depth, 0.0f);
		if (bSnap)
		{
			for (const int32 Axis : {Right, Up})
			{
				SetComponent(Wanted, Axis, std::round(Component(Wanted, Axis) / Grid) * Grid);
			}
		}
		Vector3d Step(Wanted);
		Step.SubVector(MoveApplied);
		if (!(Step == Vector3d(0.0f, 0.0f, 0.0f)))
		{
			switch (GrabbedDot)
			{
			case EDot::Vertex:
				Document->MoveBrushVertex(GrabbedVertex, Step);
				break;
			case EDot::Edge:
				Document->MoveBrushEdge(*GrabbedEdge, Step);
				break;
			case EDot::Face:
				Document->MoveBrushFace(*GrabbedFace, Step);
				break;
			default:
				break;
			}
			MoveApplied = Wanted;
		}
		break;
	}
	default:
		break;
	}
}

void FMapViewportClient::EndToolDrag()
{
	switch (Drag)
	{
	case EDrag::Rotate:
	case EDrag::Scale:
		Document->EndTransaction();
		MovedObjects.clear();
		break;
	case EDrag::ClipLine:
	{
		Vector3d Line(ClipB);
		Line.SubVector(ClipA);
		bHasClipLine = Line.GetMag() > 0.5f;
		if (bHasClipLine)
		{
			Message("Clip: Enter keeps the marked side, Ctrl+Enter splits, Tab changes sides, Esc drops the line");
		}
		break;
	}
	case EDrag::BrushEdit:
		if (!Document->EndBrushEdit())
		{
			Message("The brush would no longer be convex; the change was undone");
		}
		GrabbedDot = EDot::None;
		break;
	default:
		break;
	}
}

void FMapViewportClient::CancelToolDrag()
{
	switch (Drag)
	{
	case EDrag::Rotate:
	case EDrag::Scale:
		Document->EndTransaction(); // keep what was done
		MovedObjects.clear();
		break;
	case EDrag::BrushEdit:
		Document->EndBrushEdit();
		GrabbedDot = EDot::None;
		break;
	default:
		break;
	}
}

void FMapViewportClient::ApplyClip(EMapClip Mode)
{
	if (!bHasClipLine || !Document)
	{
		return;
	}
	Vector3d Along(ClipB);
	Along.SubVector(ClipA);
	Vector3d Normal;
	Normal.CrossVector(Along, ViewToScope(Vector3d(0.0f, 1.0f, 0.0f)));
	Normal.Normalize();
	if (bClipFlipped)
	{
		Normal.MultVector(-1.0f);
	}
	const Plane ClipPlane(Normal, ClipA);
	if (!Document->ClipObjects(Document->GetSelection(), ClipPlane, Mode))
	{
		Message("Nothing to clip: select brushes the line crosses");
		return;
	}
	bHasClipLine = false;
	RequestRedraw();
}

FReply FMapViewportClient::ToolKeyDown(const FKeyEvent& Event)
{
	if (!bHasClipLine || Settings.Tool != EMapTool::Clip)
	{
		return FReply::Unhandled();
	}
	switch (Event.GetKey())
	{
	case EKey::Enter:
		ApplyClip(Event.IsControlDown() ? EMapClip::Split : EMapClip::KeepFront);
		return FReply::Handled();
	case EKey::Tab:
		bClipFlipped = !bClipFlipped;
		RequestRedraw();
		return FReply::Handled();
	case EKey::Escape:
		bHasClipLine = false;
		RequestRedraw();
		return FReply::Handled();
	default:
		break;
	}
	return FReply::Unhandled();
}

void FMapViewportClient::DrawTools(FMapDraw& Draw)
{
	const TFunction<bool(const Vector3d&, int&, int&)> ToPixels = [this](const Vector3d& Point, int& OutX, int& OutY)
	{
		FVector2 Local;
		if (!MapToScreen(Point, Local))
		{
			return false;
		}
		OutX = static_cast<int>(std::floor(Local.X * GetDpiScale()));
		OutY = static_cast<int>(std::floor(Local.Y * GetDpiScale()));
		return true;
	};
	Draw.Color(ColorTool);
	const int Mark = FMath::RoundToInt(5.0f * GetDpiScale());
	if (Drag == EDrag::Rotate || Drag == EDrag::Scale)
	{
		int X, Y;
		if (ToPixels(Pivot, X, Y))
		{
			Draw.Cross(X, Y, Mark * 2);
		}
	}
	if (bHasClipLine && Settings.Tool == EMapTool::Clip && !IsPerspective())
	{
		int X1, Y1, X2, Y2;
		if (ToPixels(ClipA, X1, Y1) && ToPixels(ClipB, X2, Y2))
		{
			Draw.Line(X1, Y1, X2, Y2);
			Draw.Box(X1 - Mark / 2, Y1 - Mark / 2, X1 + Mark / 2, Y1 + Mark / 2);
			Draw.Box(X2 - Mark / 2, Y2 - Mark / 2, X2 + Mark / 2, Y2 + Mark / 2);
			// An arrow from the middle toward the side that is kept.
			const float Dx = static_cast<float>(X2 - X1);
			const float Dy = static_cast<float>(Y2 - Y1);
			const float Length = std::sqrt(Dx * Dx + Dy * Dy);
			if (Length > 1.0f)
			{
				Vector3d Along(ClipB);
				Along.SubVector(ClipA);
				Vector3d Normal;
				Normal.CrossVector(Along, ViewToScope(Vector3d(0.0f, 1.0f, 0.0f)));
				Normal.Normalize();
				if (bClipFlipped)
				{
					Normal.MultVector(-1.0f);
				}
				Vector3d Middle(ClipA);
				Middle.AddVector(ClipB).MultVector(0.5f);
				Vector3d Tip(Middle);
				Tip.AddVector(Normal.MultVector(24.0f / Zoom));
				int Mx, My, Tx, Ty;
				if (ToPixels(Middle, Mx, My) && ToPixels(Tip, Tx, Ty))
				{
					Draw.Line(Mx, My, Tx, Ty);
					Draw.Box(Tx - Mark / 3, Ty - Mark / 3, Tx + Mark / 3, Ty + Mark / 3);
				}
			}
		}
	}
}

FString FMapViewportClient::GetToolHint() const
{
	if (IsPerspective() || Settings.Tool == EMapTool::Select)
	{
		return FString();
	}
	switch (Settings.Tool)
	{
	case EMapTool::Vertex:
	case EMapTool::Edge:
	case EMapTool::Face:
		return GetSingleBrush() ? FString(ToString(Settings.Tool))
		                        : FTextLocalization::Format("{}: select one brush", ToString(Settings.Tool));
	default:
		return FString(ToString(Settings.Tool));
	}
}
