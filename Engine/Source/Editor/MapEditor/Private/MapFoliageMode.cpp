// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "MapFoliageMode.h"

#include "MapEditorPrivate.h"

#include <algorithm>
#include <cmath>

using namespace MapEditorPrivate;

const char* ToString(EFoliageTool Tool)
{
	switch (Tool)
	{
	case EFoliageTool::Paint: return "Paint";
	case EFoliageTool::Erase: return "Erase";
	case EFoliageTool::Single: return "Single";
	}
	return "?";
}

namespace
{
	constexpr float PaintRate = 6.0f;    // of the missing instances a second
	constexpr float EraseRate = 8.0f;    // chance a second at full strength
	constexpr float TraceTop = 100000.0f; // rays down to the ground start this high
	constexpr float Degrees = 57.29578f;

	Matrix44 ToScope(const Object& Obj)
	{
		Matrix44 M;
		Obj.CalTransSpaceMatrix(M.SetIdentity());
		return M;
	}

	Matrix44 ToLocal(const Object& Obj)
	{
		Matrix44 M;
		Obj.CalInvTransSpaceMatrix(M.SetIdentity());
		return M;
	}

	// 1 inside the inner part of the radius, fading to 0 at its edge.
	float Weight(float Distance, float Radius, float Falloff)
	{
		if (Distance >= Radius)
		{
			return 0.0f;
		}
		const float Inner = Radius * (1.0f - FMath::Clamp(Falloff, 0.0f, 1.0f));
		if (Distance <= Inner)
		{
			return 1.0f;
		}
		const float T = (Radius - Distance) / FMath::Max(Radius - Inner, 1e-4f);
		return T * T * (3.0f - 2.0f * T);
	}

	// A point in a foliage object's space, from scope space.
	Vector3d Local(const Matrix44& Inverse, const Vector3d& Scope)
	{
		Vector3d Out;
		Inverse.Transform(Out, Scope);
		return Out;
	}
}

void FMapFoliageMode::SetDocument(FMapDocument* InDocument)
{
	EndStroke();
	Document = InDocument;
	bHover = false;
	HoverView = nullptr;
}

const TArray<FString>& FMapFoliageMode::GetPresetNames()
{
	static const TArray<FString> Names = { "Grass", "Bush", "Tree", "Pine", "Rock" };
	return Names;
}

FoliageType FMapFoliageMode::MakePreset(const FString& Name)
{
	FoliageType Type;
	Type.name = Name.ToAnsi();
	Type.mesh = Name.ToAnsi();
	if (Name == "Grass")
	{
		Type.density = 2000.0f;
		Type.scaleMin = 0.7f;
		Type.scaleMax = 1.4f;
		Type.alignToNormal = true;
		Type.slopeMax = 40.0f;
		Type.cullDistance = 4000.0f;
	}
	else if (Name == "Bush")
	{
		Type.density = 60.0f;
		Type.radius = 70.0f;
		Type.scaleMin = 0.7f;
		Type.scaleMax = 1.3f;
		Type.slopeMax = 35.0f;
		Type.cullDistance = 12000.0f;
	}
	else if (Name == "Tree")
	{
		Type.density = 10.0f;
		Type.radius = 220.0f;
		Type.scaleMin = 0.8f;
		Type.scaleMax = 1.3f;
		Type.slopeMax = 30.0f;
		Type.zOffset = -4.0f;
	}
	else if (Name == "Pine")
	{
		Type.density = 12.0f;
		Type.radius = 180.0f;
		Type.scaleMin = 0.8f;
		Type.scaleMax = 1.4f;
		Type.slopeMax = 35.0f;
		Type.zOffset = -4.0f;
	}
	else if (Name == "Rock")
	{
		Type.density = 15.0f;
		Type.radius = 90.0f;
		Type.scaleMin = 0.5f;
		Type.scaleMax = 1.6f;
		Type.alignToNormal = true;
		Type.alignMaxAngle = 45.0f;
		Type.slopeMax = 90.0f;
	}
	return Type;
}

Object* FMapFoliageMode::GetFoliageObject(bool bCreate)
{
	if (!Document)
	{
		return nullptr;
	}
	for (Object* Obj : Document->GetObjects())
	{
		if (Obj->HasFoliage())
		{
			return Obj;
		}
	}
	return bCreate ? Document->AddFoliage(std::make_shared<Foliage>()) : nullptr;
}

bool FMapFoliageMode::AddType(const FString& Preset)
{
	if (!Document)
	{
		return false;
	}
	const bool bOwn = !Document->IsInTransaction();
	if (bOwn)
	{
		Document->BeginTransaction("Add Foliage Type");
	}
	Object* Obj = GetFoliageObject(true);
	const Foliage& Current = Obj->GetFoliage();
	// The next of the usual ones not there yet, else Grass again.
	FString Name = Preset;
	if (Name.IsEmpty())
	{
		Name = GetPresetNames()[0];
		for (const FString& Candidate : GetPresetNames())
		{
			const bool bThere = std::any_of(Current.GetTypes().begin(), Current.GetTypes().end(),
				[&Candidate](const FoliageType& Type) { return Type.mesh == Candidate.ToAnsi(); });
			if (!bThere)
			{
				Name = Candidate;
				break;
			}
		}
	}
	std::vector<FoliageType> Types = Current.GetTypes();
	std::vector<int> Keep;
	for (int Index = 0; Index < static_cast<int>(Types.size()); ++Index)
	{
		Keep.push_back(Index);
	}
	Types.push_back(MakePreset(Name));
	Keep.push_back(-1);
	Document->SetFoliage(*Obj, Current.WithTypes(std::move(Types), Keep), "Add Foliage Type");
	if (bOwn)
	{
		Document->EndTransaction();
	}
	SetActive(Obj->GetFoliage().GetNumTypes() - 1, true);
	return true;
}

bool FMapFoliageMode::RemoveType(int32 Type)
{
	Object* Obj = GetFoliageObject();
	if (!Obj || Type < 0 || Type >= Obj->GetFoliage().GetNumTypes())
	{
		return false;
	}
	const Foliage& Current = Obj->GetFoliage();
	std::vector<FoliageType> Types;
	std::vector<int> Keep;
	for (int Index = 0; Index < Current.GetNumTypes(); ++Index)
	{
		if (Index != Type)
		{
			Types.push_back(Current.GetTypes()[static_cast<SIZE_T>(Index)]);
			Keep.push_back(Index);
		}
	}
	Document->SetFoliage(*Obj, Current.WithTypes(std::move(Types), Keep), "Remove Foliage Type");
	if (Type < static_cast<int32>(Active.size()))
	{
		Active.erase(Active.begin() + Type);
	}
	return true;
}

bool FMapFoliageMode::SetType(int32 Type, const FoliageType& NewType, const FString& Description)
{
	Object* Obj = GetFoliageObject();
	if (!Obj || Type < 0 || Type >= Obj->GetFoliage().GetNumTypes() || Obj->GetFoliage().GetTypes()[static_cast<SIZE_T>(Type)] == NewType)
	{
		return false;
	}
	const Foliage& Current = Obj->GetFoliage();
	std::vector<FoliageType> Types = Current.GetTypes();
	Types[static_cast<SIZE_T>(Type)] = NewType;
	std::vector<int> Keep;
	for (int Index = 0; Index < Current.GetNumTypes(); ++Index)
	{
		Keep.push_back(Index);
	}
	Document->SetFoliage(*Obj, Current.WithTypes(std::move(Types), Keep), Description);
	return true;
}

bool FMapFoliageMode::IsActive(int32 Type) const
{
	return Type >= 0 && (Type >= static_cast<int32>(Active.size()) || Active[static_cast<SIZE_T>(Type)]);
}

void FMapFoliageMode::SetActive(int32 Type, bool bActive)
{
	if (Type < 0)
	{
		return;
	}
	if (Type >= static_cast<int32>(Active.size()))
	{
		Active.resize(static_cast<SIZE_T>(Type) + 1, true);
	}
	Active[static_cast<SIZE_T>(Type)] = bActive;
}

bool FMapFoliageMode::ClearType(int32 Type)
{
	Object* Obj = GetFoliageObject();
	if (!Obj || Type < 0 || Type >= Obj->GetFoliage().GetNumTypes() || Obj->GetFoliage().CountInstances(Type) == 0)
	{
		return false;
	}
	std::vector<FoliageInstance> Kept;
	for (const FoliageInstance& Instance : Obj->GetFoliage().GetInstances())
	{
		if (Instance.type != Type)
		{
			Kept.push_back(Instance);
		}
	}
	Document->SetFoliage(*Obj, Obj->GetFoliage().WithInstances(std::move(Kept)), "Remove Foliage");
	return true;
}

float FMapFoliageMode::Random()
{
	Seed = Seed * 1664525u + 1013904223u;
	return static_cast<float>(Seed >> 8) / 16777216.0f;
}

bool FMapFoliageMode::TraceSurface(const Vector3d& Origin, const Vector3d& Direction, Vector3d& OutHit) const
{
	if (!Document)
	{
		return false;
	}
	Vector3d Unit(Direction);
	const float Length = Unit.GetMag();
	if (Length < 1e-6f)
	{
		return false;
	}
	Unit.MultVector(1.0f / Length);
	float Depth = 0.0f;
	// Foliage takes no hits (Object::HitObject): rays go to the ground.
	if (!Document->GetScope().HitObject(Line(Origin, Unit), 0.0f, 1.0e7f, Depth))
	{
		return false;
	}
	OutHit = Unit;
	OutHit.MultVector(Depth).AddVector(Origin);
	return true;
}

bool FMapFoliageMode::GroundAt(float X, float Y, Vector3d& OutPoint, Vector3d& OutNormal) const
{
	const Vector3d Down(0.0f, 0.0f, -1.0f);
	if (!TraceSurface(Vector3d(X, Y, TraceTop), Down, OutPoint))
	{
		return false;
	}
	// The normal from the ground a little to either side.
	constexpr float Step = 4.0f;
	Vector3d Right, Ahead;
	if (!TraceSurface(Vector3d(X + Step, Y, TraceTop), Down, Right) || !TraceSurface(Vector3d(X, Y + Step, TraceTop), Down, Ahead))
	{
		OutNormal.NewVector(0.0f, 0.0f, 1.0f);
		return true;
	}
	Right.SubVector(OutPoint);
	Ahead.SubVector(OutPoint);
	OutNormal.CrossVector(Right, Ahead);
	const float Length = OutNormal.GetMag();
	if (Length < 1e-6f || OutNormal.GetZ() <= 0.0f)
	{
		OutNormal.NewVector(0.0f, 0.0f, 1.0f);
		return true;
	}
	OutNormal.MultVector(1.0f / Length);
	return true;
}

bool FMapFoliageMode::MakeInstance(const Foliage& Current, int32 Type, const Vector3d& Ground, const Vector3d& Normal,
	const TArray<FoliageInstance>& Existing, FoliageInstance& Out)
{
	const FoliageType& Kind = Current.GetTypes()[static_cast<SIZE_T>(Type)];
	const float Slope = std::acos(FMath::Clamp(Normal.GetZ(), -1.0f, 1.0f)) * Degrees;
	if (Slope < Kind.slopeMin || Slope > Kind.slopeMax || Ground.GetZ() < Kind.heightMin || Ground.GetZ() > Kind.heightMax)
	{
		return false;
	}
	Object* Obj = GetFoliageObject();
	const Matrix44 Inverse = ToLocal(*Obj);
	const Vector3d At = Local(Inverse, Ground);
	// Its spacing from its own kind.
	if (Kind.radius > 0.0f)
	{
		const float R2 = Kind.radius * Kind.radius;
		for (const FoliageInstance& Other : Existing)
		{
			if (Other.type != Type)
			{
				continue;
			}
			const float Dx = Other.position.GetX() - At.GetX();
			const float Dy = Other.position.GetY() - At.GetY();
			if (Dx * Dx + Dy * Dy < R2)
			{
				return false;
			}
		}
	}
	// Up: along the ground (leaning at most so far), or straight up.
	Vector3d Up(0.0f, 0.0f, 1.0f);
	if (Kind.alignToNormal)
	{
		Up = Normal;
		const float MaxLean = FMath::Clamp(Kind.alignMaxAngle, 0.0f, 90.0f) / Degrees;
		if (Slope / Degrees > MaxLean)
		{
			Vector3d Flat(Normal.GetX(), Normal.GetY(), 0.0f);
			Flat.MultVector(std::sin(MaxLean) / FMath::Max(Flat.GetMag(), 1e-6f));
			Up.NewVector(Flat.GetX(), Flat.GetY(), std::cos(MaxLean));
		}
	}
	Vector3d Placed(Up);
	Placed.MultVector(Kind.zOffset).AddVector(Ground);
	Vector3d UpEnd(Up);
	UpEnd.AddVector(Ground);
	Vector3d LocalUp = Local(Inverse, UpEnd);
	LocalUp.SubVector(At);
	Out.type = Type;
	Out.position = Local(Inverse, Placed);
	Out.up = LocalUp;
	Out.yaw = Kind.randomYaw ? Random() * 6.2831853f : 0.0f;
	Out.scale = Kind.scaleMin + (Kind.scaleMax - Kind.scaleMin) * Random();
	return true;
}

bool FMapFoliageMode::ApplyAt(const Vector3d& ScopePoint, float DeltaSeconds, bool bEraseTool)
{
	if (!Document || DeltaSeconds <= 0.0f)
	{
		return false;
	}
	const bool bRemoving = bEraseTool || Tool == EFoliageTool::Erase;
	if (Tool == EFoliageTool::Single && !bRemoving)
	{
		return false; // one a click (BeginStroke)
	}
	Object* Obj = GetFoliageObject(!bRemoving);
	if (!Obj || Obj->GetFoliage().GetNumTypes() == 0)
	{
		return false;
	}
	const Foliage& Current = Obj->GetFoliage();
	const Matrix44 Forward = ToScope(*Obj);
	const float Radius = FMath::Max(Brush.Radius, 1.0f);
	const float Strength = FMath::Clamp(Brush.Strength, 0.0f, 1.0f);
	const auto Distance = [&](const FoliageInstance& Instance)
	{
		Vector3d At;
		Forward.Transform(At, Instance.position);
		const float Dx = At.GetX() - ScopePoint.GetX();
		const float Dy = At.GetY() - ScopePoint.GetY();
		return std::sqrt(Dx * Dx + Dy * Dy);
	};

	TArray<FoliageInstance> Instances(Current.GetInstances().begin(), Current.GetInstances().end());
	bool bChanged = false;
	if (bRemoving)
	{
		// The active types' instances under the brush go, the middle first.
		TArray<FoliageInstance> Kept;
		for (const FoliageInstance& Instance : Instances)
		{
			const float W = IsActive(Instance.type) ? Weight(Distance(Instance), Radius, Brush.Falloff) : 0.0f;
			if (W > 0.0f && Random() < FMath::Min(1.0f, W * FMath::Max(Strength, 0.05f) * EraseRate * DeltaSeconds))
			{
				bChanged = true;
				continue;
			}
			Kept.push_back(Instance);
		}
		if (!bChanged)
		{
			return false;
		}
		Document->SetFoliage(*Obj, Current.WithInstances(std::move(Kept)), "Erase Foliage");
		return true;
	}

	// Each active type up to its density under the brush.
	const float Area = 3.14159265f * Radius * Radius;
	for (int32 Type = 0; Type < Current.GetNumTypes(); ++Type)
	{
		if (!IsActive(Type) || !Current.GetMesh(Type))
		{
			continue;
		}
		const float Wanted = Current.GetTypes()[static_cast<SIZE_T>(Type)].density * 1.0e-6f * Area * Strength;
		int32 Have = 0;
		for (const FoliageInstance& Instance : Instances)
		{
			Have += Instance.type == Type && Distance(Instance) < Radius ? 1 : 0;
		}
		const float Missing = Wanted - static_cast<float>(Have);
		if (Missing <= 0.0f)
		{
			continue;
		}
		const int32 Tries = FMath::Min(static_cast<int32>(Missing * FMath::Min(1.0f, PaintRate * DeltaSeconds) + Random()), 400);
		for (int32 Try = 0; Try < Tries; ++Try)
		{
			// Anywhere in the brush, thinning out over its falloff.
			const float Rho = Radius * std::sqrt(Random());
			const float Theta = Random() * 6.2831853f;
			if (Random() > Weight(Rho, Radius, Brush.Falloff))
			{
				continue;
			}
			Vector3d Ground, Normal;
			FoliageInstance Made;
			if (GroundAt(ScopePoint.GetX() + Rho * std::cos(Theta), ScopePoint.GetY() + Rho * std::sin(Theta), Ground, Normal)
				&& MakeInstance(Current, Type, Ground, Normal, Instances, Made))
			{
				Instances.push_back(Made);
				bChanged = true;
			}
		}
	}
	if (!bChanged)
	{
		return false;
	}
	Document->SetFoliage(*Obj, Current.WithInstances(std::vector<FoliageInstance>(Instances.begin(), Instances.end())), "Paint Foliage");
	return true;
}

bool FMapFoliageMode::PlaceAt(int32 Type, const Vector3d& ScopePoint)
{
	Object* Obj = GetFoliageObject();
	if (!Obj || Type < 0 || Type >= Obj->GetFoliage().GetNumTypes())
	{
		return false;
	}
	Vector3d Ground, Normal;
	if (!GroundAt(ScopePoint.GetX(), ScopePoint.GetY(), Ground, Normal))
	{
		return false;
	}
	const Foliage& Current = Obj->GetFoliage();
	TArray<FoliageInstance> Instances(Current.GetInstances().begin(), Current.GetInstances().end());
	FoliageInstance Made;
	if (!MakeInstance(Current, Type, Ground, Normal, Instances, Made))
	{
		return false;
	}
	Instances.push_back(Made);
	Document->SetFoliage(*Obj, Current.WithInstances(std::vector<FoliageInstance>(Instances.begin(), Instances.end())), "Place Foliage");
	return true;
}

bool FMapFoliageMode::BeginStroke(const Vector3d& ScopePoint, bool bEraseTool)
{
	if (!Document)
	{
		return false;
	}
	EndStroke();
	const bool bRemoving = bEraseTool || Tool == EFoliageTool::Erase;
	Document->BeginTransaction(Tool == EFoliageTool::Single && !bRemoving ? "Place Foliage" : (bRemoving ? "Erase Foliage" : "Paint Foliage"));
	bStroking = true;
	bErase = bEraseTool;
	StrokePoint = ScopePoint;
	if (Tool == EFoliageTool::Single && !bRemoving)
	{
		// The first active type, once.
		if (const Object* Obj = GetFoliageObject())
		{
			for (int32 Type = 0; Type < Obj->GetFoliage().GetNumTypes(); ++Type)
			{
				if (IsActive(Type))
				{
					PlaceAt(Type, ScopePoint);
					break;
				}
			}
		}
		return true;
	}
	ApplyAt(ScopePoint, 1.0f / 60.0f, bErase);
	return true;
}

void FMapFoliageMode::EndStroke()
{
	if (!bStroking)
	{
		return;
	}
	bStroking = false;
	if (Document && Document->IsInTransaction())
	{
		Document->EndTransaction();
	}
}

bool FMapFoliageMode::UpdateHover(FMapViewportClient& View, FVector2 Local)
{
	HoverView = &View;
	Vector3d Origin, Direction;
	bHover = View.GetCursorRay(Local, Origin, Direction) && TraceSurface(Origin, Direction, HoverPoint);
	return bHover;
}

bool FMapFoliageMode::InputPress(FMapViewportClient& View, FVector2 Local, const FPointerEvent& Event)
{
	// Every left press is the mode's: off any surface it does nothing.
	bHeld = true;
	bErase = Event.IsShiftDown();
	if (UpdateHover(View, Local))
	{
		BeginStroke(HoverPoint, bErase);
	}
	return true;
}

void FMapFoliageMode::InputRelease(FMapViewportClient&)
{
	bHeld = false;
	EndStroke();
}

void FMapFoliageMode::MouseMove(FMapViewportClient& View, FVector2 Local, const FPointerEvent& Event)
{
	UpdateHover(View, Local);
	if (Tool != EFoliageTool::Single)
	{
		bErase = Event.IsShiftDown();
		if (bHeld && !bStroking && bHover)
		{
			BeginStroke(HoverPoint, bErase);
		}
	}
	if (bStroking && bHover)
	{
		StrokePoint = HoverPoint;
	}
}

void FMapFoliageMode::MouseLeave(FMapViewportClient& View)
{
	if (HoverView == &View)
	{
		bHover = false;
		HoverView = nullptr;
	}
}

void FMapFoliageMode::Tick(FMapViewportClient&, float DeltaSeconds)
{
	if (bStroking && bHover && Tool != EFoliageTool::Single)
	{
		ApplyAt(StrokePoint, DeltaSeconds, bErase);
	}
}

void FMapFoliageMode::DrawOverlay(const FMapViewportClient& View, FSlateWindowElementList& Out, float DpiScale) const
{
	if (!Document || HoverView != &View || !bHover)
	{
		return;
	}
	const FIntPoint Pixels = View.GetSize();
	const FPaintGeometry Whole = FPaintGeometry::Make(FVector2::Zero(), Pixels.ToVector2() / DpiScale, DpiScale);
	const bool bRemoving = bErase || Tool == EFoliageTool::Erase;
	const FColor Color = bRemoving ? FColor(255, 150, 90) : FColor(120, 220, 120);
	// The brush on the ground under it.
	TArray<FVector2> Run;
	const auto Flush = [&]
	{
		if (Run.size() > 1)
		{
			Out.AddLines(8, Whole, Run, Color, 2.0f);
		}
		Run.clear();
	};
	const float Radius = Tool == EFoliageTool::Single ? 24.0f : Brush.Radius;
	constexpr int32 Steps = 48;
	for (int32 Step = 0; Step <= Steps; ++Step)
	{
		const float Angle = static_cast<float>(Step) / Steps * 6.2831853f;
		Vector3d Ground, Normal;
		FVector2 Screen;
		if (GroundAt(HoverPoint.GetX() + std::cos(Angle) * Radius, HoverPoint.GetY() + std::sin(Angle) * Radius, Ground, Normal)
			&& View.MapToScreen(Ground, Screen))
		{
			Run.push_back(Screen);
		}
		else
		{
			Flush();
		}
	}
	Flush();
	FVector2 Dot;
	if (View.MapToScreen(HoverPoint, Dot))
	{
		Out.AddRoundedBox(8, FPaintGeometry::Make(Dot - FVector2(2.5f, 2.5f), FVector2(5.0f, 5.0f), DpiScale), Color, 2.5f);
	}
}

FString FMapFoliageMode::GetHint() const
{
	switch (Tool)
	{
	case EFoliageTool::Paint: return "Foliage: Paint (Shift erases)";
	case EFoliageTool::Erase: return "Foliage: Erase";
	case EFoliageTool::Single: return "Foliage: Single (click to place one)";
	}
	return "Foliage";
}

void SnapFoliageToLandscape(FMapDocument& Document, const Object& Land, const Landscape& Before)
{
	if (!Land.HasLandscape())
	{
		return;
	}
	const Landscape& After = Land.GetLandscape();
	const Matrix44 LandToScope = ToScope(Land);
	const Matrix44 ScopeToLand = ToLocal(Land);
	const float HalfX = After.GetSizeX() * 0.5f, HalfY = After.GetSizeY() * 0.5f;
	for (Object* Obj : Document.GetObjects())
	{
		if (!Obj->HasFoliage())
		{
			continue;
		}
		const Matrix44 FoliageToScope = ToScope(*Obj);
		const Matrix44 ScopeToFoliage = ToLocal(*Obj);
		std::vector<FoliageInstance> Instances = Obj->GetFoliage().GetInstances();
		bool bMoved = false;
		for (FoliageInstance& Instance : Instances)
		{
			const FoliageType& Kind = Obj->GetFoliage().GetTypes()[static_cast<SIZE_T>(Instance.type)];
			// Its foot on the ground as it was: there it moves with it.
			Vector3d At, OnLand;
			FoliageToScope.Transform(At, Instance.position);
			ScopeToLand.Transform(OnLand, At);
			if (std::fabs(OnLand.GetX()) > HalfX || std::fabs(OnLand.GetY()) > HalfY)
			{
				continue;
			}
			const float Was = Before.SampleHeight(OnLand.GetX(), OnLand.GetY());
			const float Now = After.SampleHeight(OnLand.GetX(), OnLand.GetY());
			if (Was == Now || std::fabs(OnLand.GetZ() - Kind.zOffset - Was) > 2.0f)
			{
				continue;
			}
			Vector3d Moved(OnLand.GetX(), OnLand.GetY(), OnLand.GetZ() + (Now - Was));
			Vector3d Scope;
			LandToScope.Transform(Scope, Moved);
			ScopeToFoliage.Transform(Instance.position, Scope);
			bMoved = true;
		}
		if (bMoved)
		{
			Document.SetFoliage(*Obj, Obj->GetFoliage().WithInstances(std::move(Instances)), "Sculpt Landscape");
		}
	}
}
