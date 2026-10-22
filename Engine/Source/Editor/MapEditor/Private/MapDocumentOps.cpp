// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// FMapDocument's editing operations. Each one is an undo step of its own
// unless a transaction is open; they record FMapEdits (MapEdits.h).

#include "MapDocument.h"
#include "MapPackage.h"
#include "MapAssetLibrary.h"
#include "AssetPackage.h"

#include "MapEdits.h"

#include <algorithm>

using namespace MapEditorPrivate;

namespace
{
	// Children of the scope among Objects, each once.
	TArray<Object*> InScope(const TArray<Object*>& Objects, const Object& Scope)
	{
		TArray<Object*> Result;
		for (Object* Obj : Objects)
		{
			if (Obj && Obj->GetParentPtr() == &Scope && std::find(Result.begin(), Result.end(), Obj) == Result.end())
			{
				Result.push_back(Obj);
			}
		}
		return Result;
	}

	Matrix44 AboutPivot(const Matrix44& Transform, const Vector3d& Pivot)
	{
		Matrix44 Result, Back;
		Result.SetTranslation(Pivot);
		Result.Multiply(Transform);
		Back.SetInvTranslation(Pivot);
		Result.Multiply(Back);
		return Result;
	}

	bool HasBrushes(const Object& Obj)
	{
		return Obj.HasBrush() || Obj.CountBrushes() > 0;
	}
}

void FMapDocument::MoveObjects(const TArray<Object*>& Objects, const Vector3d& Delta)
{
	if (Objects.empty() || Delta == Vector3d(0.0f, 0.0f, 0.0f))
	{
		return;
	}
	const bool bOwn = BeginOwnTransaction("Move");
	MoveBy(Objects, Delta);
	FMoveEdit* Last = Pending->Edits.empty() ? nullptr : dynamic_cast<FMoveEdit*>(Pending->Edits.back().get());
	if (Last && Last->Objects == Objects)
	{
		Last->Delta.AddVector(Delta);
		Changed(EMapChange::Geometry);
	}
	else
	{
		auto Edit = std::make_unique<FMoveEdit>();
		Edit->Objects = Objects;
		Edit->Delta = Delta;
		Record(std::move(Edit), EMapChange::Geometry);
	}
	if (bOwn)
	{
		EndTransaction();
	}
}

void FMapDocument::AddObjects(const TArray<Object*>& NewObjects)
{
	if (NewObjects.empty())
	{
		return;
	}
	// Copies used by undo preserve IDs. A caller inserting such a copy must
	// not introduce aliases into the live world (including nested children).
	TSet<std::string> Ids;
	const auto Visit = [&](auto&& Self, Object& Obj, bool bIncoming) -> void
	{
		auto Id = Obj.GetIdentity();
		const auto Conflicts = [&]() { return !Id.IsValid() || Ids.contains(Id.Actor.ToStdString()) || Ids.contains(Id.Transform.ToStdString()) || Ids.contains(Id.Payload.ToStdString()) || Ids.contains(Id.Entity.ToStdString()); };
		if (bIncoming && Conflicts()) { Obj.RegenerateIdentity(false); Id = Obj.GetIdentity(); }
		for (const auto* Value : { &Id.Actor, &Id.Transform, &Id.Payload, &Id.Entity }) Ids.insert(Value->ToStdString());
		for (int I = 0; I < Obj.GetNumChildren(); ++I) Self(Self, Obj.GetChild(I), bIncoming);
	};
	Visit(Visit, *Root, false);
	for (Object* Obj : NewObjects) Visit(Visit, *Obj, true);
	const bool bOwn = BeginOwnTransaction(NewObjects.size() == 1 ? "Add Object" : "Add Objects");
	auto Edit = std::make_unique<FAddEdit>();
	Edit->Objects = NewObjects;
	Edit->Parent = Scope;
	Edit->Redo(*this);
	Record(std::move(Edit), EMapChange::Structure);
	SetSelection(NewObjects);
	if (bOwn)
	{
		EndTransaction();
	}
}

void FMapDocument::DeleteObjects(const TArray<Object*>& Objects)
{
	TArray<Object*> Valid;
	for (Object* Obj : Objects)
	{
		if (Obj && Obj != Root && Obj->GetParentPtr() && std::find(Valid.begin(), Valid.end(), Obj) == Valid.end())
		{
			Valid.push_back(Obj);
		}
	}
	if (Valid.empty())
	{
		return;
	}
	const bool bOwn = BeginOwnTransaction("Delete");
	// Unselect first: the selector must not hold objects outside the scope.
	TArray<Object*> Remaining = GetSelection();
	std::erase_if(Remaining, [&](Object* Obj) { return std::find(Valid.begin(), Valid.end(), Obj) != Valid.end(); });
	SetSelection(Remaining);
	auto Edit = std::make_unique<FDeleteEdit>();
	Edit->Objects = Valid;
	Edit->Redo(*this);
	Record(std::move(Edit), EMapChange::Structure);
	if (bOwn)
	{
		EndTransaction();
	}
}

void FMapDocument::DeleteEmptied(const TArray<Object*>& Objects)
{
	TArray<Object*> Empty;
	for (Object* Obj : Objects)
	{
		if (IsEmptyObject(*Obj))
		{
			Empty.push_back(Obj);
		}
	}
	DeleteObjects(Empty);
}

TArray<Object*> FMapDocument::DuplicateObjects(const TArray<Object*>& Objects, const Vector3d& Delta)
{
	TArray<Object*> Copies;
	for (Object* Obj : InScope(Objects, *Scope))
	{
		Object* Copy = new Object(*Obj);
		Copy->RegenerateIdentity();
		Vector3d Position;
		Copy->GetPosition(Position).AddVector(Delta);
		Copy->SetPosition(Position);
		Copies.push_back(Copy);
	}
	const bool bOwn = BeginOwnTransaction("Duplicate");
	AddObjects(Copies);
	if (bOwn)
	{
		EndTransaction();
	}
	return Copies;
}

Object* FMapDocument::AddPrefab(const FString& PrefabPath, const Vector3d& Position, FString* OutError, const FString& ExpectedId)
{
	Object* Prefab = nullptr;
	FString Error;
	if (PrefabPath.EndsWith(".asset", ESearchCase::IgnoreCase))
	{
		FString Id;
		Prefab = FMapPackage::Load(PrefabPath, Id, Error, true, ExpectedId).release();
	}
	else
	{
		Prefab = new Object;
		if (!Prefab->LoadObjFile(PrefabPath.ToAnsi().c_str())) { delete Prefab; Prefab = nullptr; }
	}
	if (!Prefab)
	{
		if (OutError)
		{
			*OutError = Error.IsEmpty() ? FString::Format("{} could not be loaded.", PrefabPath) : Error;
		}
		return nullptr;
	}
	Vector3d Offset;
	Prefab->RegenerateIdentity();
	Prefab->GetPosition(Offset).AddVector(Position);
	Prefab->SetPosition(Offset);
	const FString Name = FPaths::GetCleanFilename(PrefabPath);
	const bool bOwn = BeginOwnTransaction(FString::Format("Add {}", Name.Left(Name.Len() - 4)));
	AddObjects({ Prefab });
	if (bOwn)
	{
		EndTransaction();
	}
	return Prefab;
}

Object* FMapDocument::AddMesh(const FString& MeshName, const Vector3d& Position)
{
	std::shared_ptr<const StaticMesh> Mesh = StaticMesh::Find(MeshName.ToAnsi().c_str());
	if (!Mesh)
	{
		return nullptr;
	}
	Object* Obj = new Object;
	Obj->SetMesh(Mesh);
	Obj->SetObjName(Mesh->GetName());
	Obj->SetPosition(Position);
	const bool bOwn = BeginOwnTransaction(FString::Format("Add {}", Mesh->GetName()));
	AddObjects({ Obj });
	if (bOwn)
	{
		EndTransaction();
	}
	return Obj;
}

Object* FMapDocument::AddMeshAsset(const FAssetReference& Ref, const Vector3d& Position, FString& Error)
{
	const auto Asset=FMapAssetLibrary::LoadMesh(Ref,{},Error); if(!Asset) return nullptr;
	auto* Obj=new Object; Obj->SetMeshAsset(Asset); Obj->SetObjName(Asset->Data.Mesh->GetName()); Obj->SetPosition(Position);
	AddObjects({Obj}); return Obj;
}

bool FMapDocument::SetMeshMaterial(Object& Obj, const FAssetReference& Ref, FString& Error, int32 Slot)
{
	if (!Obj.GetMeshAsset())
	{
		Error = "Save this mesh as a native mesh asset and place it before assigning a material.";
		return false;
	}
	const auto& Old = *Obj.GetMeshAsset();
	if (Slot < 0 || Slot >= Old.Data.GetNumMaterials())
	{
		Error = "Material slot outside mesh.";
		return false;
	}
	auto Overrides = Old.AdditionalOverrides;
	auto First = Old.MaterialOverride;
	if (Slot == 0)
		First = Ref;
	else
	{
		Overrides.resize(Old.Data.AdditionalMaterials.size());
		Overrides[Slot - 1] = Ref;
	}
	const auto Asset = FMapAssetLibrary::LoadMesh(Old.Source, First, Error, Overrides);
	if (!Asset)
		return false;
	const bool Own = BeginOwnTransaction("Assign Material");
	CaptureObjects({&Obj});
	Obj.SetMeshAsset(Asset);
	Changed(EMapChange::Geometry);
	if (Own)
		EndTransaction();
	return true;
}

Object* FMapDocument::AddLandscape(int32 QuadsX, int32 QuadsY, float QuadSize, const Vector3d& Position)
{
	return AddLandscape(std::make_shared<Landscape>(QuadsX, QuadsY, QuadSize), Position);
}

Object* FMapDocument::AddLandscape(std::shared_ptr<const Landscape> Land, const Vector3d& Position, const FString& Description)
{
	if (!Land)
	{
		return nullptr;
	}
	Object* Obj = new Object;
	Obj->SetLandscape(std::move(Land));
	Obj->SetObjName("Landscape");
	Obj->SetPosition(Position);
	const bool bOwn = BeginOwnTransaction(Description);
	AddObjects({ Obj });
	if (bOwn)
	{
		EndTransaction();
	}
	return Obj;
}

Object* FMapDocument::AddFoliage(std::shared_ptr<const Foliage> NewFoliage, const FString& Description)
{
	if (!NewFoliage)
	{
		return nullptr;
	}
	Object* Obj = new Object;
	Obj->SetFoliage(std::move(NewFoliage));
	Obj->SetObjName("Foliage");
	const bool bOwn = BeginOwnTransaction(Description);
	// Made by painting: what was selected stays so.
	const TArray<Object*> Selected = GetSelection();
	AddObjects({ Obj });
	SetSelection(Selected);
	if (bOwn)
	{
		EndTransaction();
	}
	return Obj;
}

void FMapDocument::SetFoliage(Object& Obj, std::shared_ptr<const Foliage> NewFoliage, const FString& Description)
{
	if (!Obj.HasFoliage() || !NewFoliage)
	{
		return;
	}
	const bool bOwn = BeginOwnTransaction(Description);
	CaptureObjects({ &Obj });
	Obj.SetFoliage(std::move(NewFoliage));
	if (Obj.GetParentPtr())
	{
		Obj.GetParentPtr()->SetBoundRadius();
	}
	Changed(EMapChange::Geometry);
	if (bOwn)
	{
		EndTransaction();
	}
}

void FMapDocument::SetLandscape(Object& Obj, std::shared_ptr<const Landscape> NewLandscape, const FString& Description)
{
	if (!Obj.HasLandscape() || !NewLandscape)
	{
		return;
	}
	const bool bOwn = BeginOwnTransaction(Description);
	CaptureObjects({ &Obj });
	Obj.SetLandscape(std::move(NewLandscape));
	if (Obj.GetParentPtr())
	{
		Obj.GetParentPtr()->SetBoundRadius();
	}
	Changed(EMapChange::Geometry);
	if (bOwn)
	{
		EndTransaction();
	}
}

Object* FMapDocument::AddEntity(const FString& ClassName, const Vector3d& Position)
{
	Object* Obj = new Object;
	Obj->SetEntity(new Entity(ClassName.ToAnsi().c_str()));
	Obj->SetPosition(Position);
	const bool bOwn = BeginOwnTransaction(FString::Format("Add {}", ClassName));
	AddObjects({ Obj });
	if (bOwn)
	{
		EndTransaction();
	}
	return Obj;
}

bool FMapDocument::SavePrefab(const TArray<Object*>& Objects, const FString& PrefabPath, FString& OutError) const
{
	if (Objects.empty())
	{
		OutError = "Nothing is selected.";
		return false;
	}
	// Copies around the origin, in a root that is not part of the map.
	Vector3d Min, Max;
	bool bFirst = true;
	for (Object* Obj : Objects)
	{
		Vector3d ObjMin, ObjMax;
		GetObjectBounds(*Obj, ObjMin, ObjMax);
		if (bFirst)
		{
			Min = ObjMin;
			Max = ObjMax;
			bFirst = false;
			continue;
		}
		Min.NewVector(FMath::Min(Min.GetX(), ObjMin.GetX()), FMath::Min(Min.GetY(), ObjMin.GetY()), FMath::Min(Min.GetZ(), ObjMin.GetZ()));
		Max.NewVector(FMath::Max(Max.GetX(), ObjMax.GetX()), FMath::Max(Max.GetY(), ObjMax.GetY()), FMath::Max(Max.GetZ(), ObjMax.GetZ()));
	}
	Vector3d Center(Min);
	Center.AddVector(Max).MultVector(-0.5f);
	Object Prefab;
	for (Object* Obj : Objects)
	{
		Object* Copy = new Object(*Obj);
		Prefab.AddChild(*Copy, false);
	}
	TArray<Object*> Copies;
	for (int Index = 0; Index < Prefab.GetNumChildren(); ++Index)
	{
		Copies.push_back(&Prefab.GetChild(Index));
	}
	MoveBy(Copies, Center);
	Prefab.SetBoundRadius();
	if (PrefabPath.EndsWith(".asset", ESearchCase::IgnoreCase))
	{
		FString Id;
		if (FFileHelper::FileExists(PrefabPath))
		{
			FAssetPackageMetadata Existing;
			if (!FAssetPackageStore::ReadMetadata(PrefabPath, Existing, OutError)) return false;
			if (Existing.Kind != EAssetPackageKind::Asset || Existing.Schema != "Genesis.Prefab" || (Existing.SchemaVersion < 1 || Existing.SchemaVersion > 3))
			{ OutError = "Choose a different filename: the existing package is not a supported prefab asset."; return false; }
			Id = Existing.Id;
		}
		return FMapPackage::Save(PrefabPath, Prefab, Id, OutError, true);
	}
	if(FMapAssetLibrary::HasNativeAssets(Prefab)) { OutError="Save as .asset to preserve native asset references."; return false; }
	if (!Prefab.SaveObjFile(PrefabPath.ToAnsi().c_str()))
	{
		OutError = FString::Format("{} could not be written.", PrefabPath);
		return false;
	}
	return true;
}

// ----- In-place changes -----

void FMapDocument::CaptureObjects(const TArray<Object*>& Objects)
{
	check(Pending);
	for (Object* Obj : Objects)
	{
		if (!Obj || std::find(Pending->Captured.begin(), Pending->Captured.end(), Obj) != Pending->Captured.end())
		{
			continue;
		}
		Pending->Captured.push_back(Obj);
		Pending->Edits.push_back(std::make_unique<FContentEdit>(*Obj));
		Pending->Changes |= EMapChange::Geometry;
	}
}

void FMapDocument::TransformObjects(const TArray<Object*>& Objects, const Matrix44& Transform, bool bMirror, const FString& Description)
{
	const TArray<Object*> Targets = InScope(Objects, *Scope);
	if (Targets.empty())
	{
		return;
	}
	const bool bOwn = BeginOwnTransaction(Description);
	CaptureObjects(Targets);
	for (Object* Obj : Targets)
	{
		Obj->Transform(Transform, true, bMirror);
	}
	Scope->SetBoundRadius();
	Changed(EMapChange::Geometry);
	if (bOwn)
	{
		EndTransaction();
	}
}

void FMapDocument::RotateObjects(const TArray<Object*>& Objects, int32 Axis, float Degrees, const Vector3d& Pivot)
{
	Matrix44 Rotation;
	const float Radians = DEG2RAD(Degrees);
	if (Axis == 0) Rotation.SetRotateX(Radians);
	else if (Axis == 1) Rotation.SetRotateY(Radians);
	else Rotation.SetRotateZ(Radians);
	TransformObjects(Objects, AboutPivot(Rotation, Pivot), false, "Rotate");
}

void FMapDocument::ScaleObjects(const TArray<Object*>& Objects, const Vector3d& Factors, const Vector3d& Pivot)
{
	Matrix44 Scale;
	Scale.SetScale(Factors);
	TransformObjects(Objects, AboutPivot(Scale, Pivot), false, "Scale");
}

void FMapDocument::FlipObjects(const TArray<Object*>& Objects, int32 Axis, const Vector3d& Pivot)
{
	Matrix44 Flip;
	Flip.SetScale(Vector3d(Axis == 0 ? -1.0f : 1.0f, Axis == 1 ? -1.0f : 1.0f, Axis == 2 ? -1.0f : 1.0f));
	static const char* const Names[3] = { "Flip X", "Flip Y", "Flip Z" };
	TransformObjects(Objects, AboutPivot(Flip, Pivot), true, Names[FMath::Clamp(Axis, 0, 2)]);
}

// ----- Brush operations -----

bool FMapDocument::HollowObjects(const TArray<Object*>& Objects, float Thickness, bool bInward)
{
	TArray<Object*> Brushes;
	for (Object* Obj : InScope(Objects, *Scope))
	{
		if (Obj->HasBrush())
		{
			Brushes.push_back(Obj);
		}
	}
	if (Brushes.empty())
	{
		return false;
	}
	BeginTransaction("Hollow");
	CaptureObjects(Brushes);
	bool bChanged = false;
	for (Object* Obj : Brushes)
	{
		bChanged |= bInward ? Obj->HollowInBrush(Thickness) : Obj->HollowOutBrush(Thickness);
	}
	if (!bChanged)
	{
		CancelTransaction();
		return false;
	}
	Changed(EMapChange::Geometry | EMapChange::Structure);
	EndTransaction();
	return true;
}

bool FMapDocument::SubtractSelection()
{
	TArray<Object*> Cutters;
	for (Object* Obj : GetSelection())
	{
		if (!Obj->IsItemNode() && HasBrushes(*Obj))
		{
			Cutters.push_back(Obj);
		}
	}
	if (Cutters.empty())
	{
		return false;
	}
	LinkList<ObjectPtr> CutterList;
	ToList(Cutters, CutterList);

	BeginTransaction("Subtract");
	// A cut that leaves several pieces moves them up into the scope (and
	// empties the object that was cut); those new objects are recorded as
	// added.
	const TArray<Object*> ScopeBefore = GetObjects();
	TArray<Object*> Modified;
	for (Object* Obj : GetObjects())
	{
		if (Obj->IsItemNode() || !HasBrushes(*Obj) || std::find(Cutters.begin(), Cutters.end(), Obj) != Cutters.end())
		{
			continue;
		}
		// Skip what no cutter can reach (like the MFC editor).
		bool bMayIntersect = false;
		for (Object* Cutter : Cutters)
		{
			Vector3d Distance;
			Distance.SubVector(Obj->GetPosition(), Cutter->GetPosition());
			bMayIntersect |= Distance.GetMag() <= Obj->GetBoundRadius() + Cutter->GetBoundRadius();
		}
		if (!bMayIntersect)
		{
			continue;
		}
		CaptureObjects({ Obj });
		if (Obj->CSGSubtract(CutterList))
		{
			Modified.push_back(Obj);
		}
	}
	if (Modified.empty())
	{
		CancelTransaction();
		return false;
	}
	TArray<Object*> Pieces;
	for (Object* Obj : GetObjects())
	{
		if (std::find(ScopeBefore.begin(), ScopeBefore.end(), Obj) == ScopeBefore.end())
		{
			Pieces.push_back(Obj);
		}
	}
	if (!Pieces.empty())
	{
		auto Added = std::make_unique<FAddEdit>(); // already in the scope
		Added->Objects = Pieces;
		Added->Parent = Scope;
		Record(std::move(Added), EMapChange::Structure);
	}
	DeleteEmptied(Modified);
	Scope->SetBoundRadius();
	Changed(EMapChange::Geometry | EMapChange::Structure);
	EndTransaction();
	return true;
}

bool FMapDocument::IntersectSelection()
{
	const TArray<Object*> Selection = GetSelection();
	if (Selection.size() < 2)
	{
		return false;
	}
	LinkList<ObjectPtr> List;
	ToList(Selection, List);
	Object* Common = Scope->CSGIntersect(List);
	if (!Common)
	{
		return false;
	}
	BeginTransaction("Intersect");
	DeleteObjects(Selection);
	AddObjects({ Common });
	EndTransaction();
	return true;
}

bool FMapDocument::ClipObjects(const TArray<Object*>& Objects, const Plane& ClipPlane, EMapClip Mode)
{
	TArray<Object*> Targets;
	for (Object* Obj : InScope(Objects, *Scope))
	{
		if (!Obj->IsItemNode() && HasBrushes(*Obj))
		{
			Targets.push_back(Obj);
		}
	}
	if (Targets.empty())
	{
		return false;
	}
	// Plane::PlaneClip keeps the back side; the front is kept by clipping
	// with the reversed plane.
	Vector3d Reversed(ClipPlane.GetNorm());
	Reversed.MultVector(-1.0f);
	const Plane Front(Reversed, ClipPlane.GetPoint());
	const Plane& Keep = Mode == EMapClip::KeepFront ? Front : ClipPlane;

	BeginTransaction(Mode == EMapClip::Split ? "Split" : "Clip");
	TArray<Object*> Modified;
	TArray<Object*> Pieces;
	for (Object* Obj : Targets)
	{
		Object* Other = Mode == EMapClip::Split ? new Object(*Obj) : nullptr;
		if (Other) Other->RegenerateIdentity();
		CaptureObjects({ Obj });
		const bool bClipped = Obj->PlaneClip(Keep);
		if (!bClipped)
		{
			delete Other;
			continue;
		}
		Modified.push_back(Obj);
		if (Other)
		{
			Other->PlaneClip(Front);
			if (IsEmptyObject(*Other))
			{
				delete Other;
			}
			else
			{
				Pieces.push_back(Other);
			}
		}
	}
	if (Modified.empty())
	{
		CancelTransaction();
		return false;
	}
	DeleteEmptied(Modified);
	if (!Pieces.empty())
	{
		TArray<Object*> Kept;
		for (Object* Obj : Modified)
		{
			if (Obj->GetParentPtr())
			{
				Kept.push_back(Obj);
			}
		}
		AddObjects(Pieces);
		AddToSelection(Kept);
	}
	Scope->SetBoundRadius();
	Changed(EMapChange::Geometry | EMapChange::Structure);
	EndTransaction();
	return true;
}

// ----- Groups -----

Object* FMapDocument::GroupObjects(const TArray<Object*>& Objects)
{
	const TArray<Object*> Members = InScope(Objects, *Scope);
	if (Members.empty())
	{
		return nullptr;
	}
	Vector3d Center(0.0f, 0.0f, 0.0f);
	for (Object* Obj : Members)
	{
		Center.AddVector(Obj->GetPosition());
	}
	Center.MultVector(1.0f / static_cast<float>(Members.size()));

	BeginTransaction("Group");
	Object* Group = new Object;
	Group->TransSpace::SetPosition(Center);
	{
		auto Edit = std::make_unique<FAddEdit>();
		Edit->Objects = { Group };
		Edit->Parent = Scope;
		Edit->Redo(*this);
		Record(std::move(Edit), EMapChange::Structure);
	}
	// The members keep their place: their positions become relative to the group.
	ClearSelection();
	CaptureObjects(Members);
	Vector3d Back(Center);
	Back.MultVector(-1.0f);
	MoveBy(Members, Back);
	auto Reparent = std::make_unique<FReparentEdit>();
	Reparent->Objects = Members;
	Reparent->NewParent = Group;
	Reparent->Redo(*this);
	Record(std::move(Reparent), EMapChange::Structure);
	Group->SetBoundRadius();
	SetSelection({ Group });
	EndTransaction();
	return Group;
}

bool FMapDocument::UngroupObjects(const TArray<Object*>& Groups)
{
	TArray<Object*> Targets;
	for (Object* Obj : InScope(Groups, *Scope))
	{
		if (IsGroup(*Obj))
		{
			Targets.push_back(Obj);
		}
	}
	if (Targets.empty())
	{
		return false;
	}
	BeginTransaction("Ungroup");
	ClearSelection();
	TArray<Object*> Freed;
	for (Object* Group : Targets)
	{
		TArray<Object*> Members;
		for (int Index = 0; Index < Group->GetNumChildren(); ++Index)
		{
			Members.push_back(&Group->GetChild(Index));
		}
		// Bake the group's transform into its members, then lift them out.
		CaptureObjects(Members);
		Matrix44 GroupTransform;
		Group->CalTransSpaceMatrix(GroupTransform.SetIdentity());
		for (Object* Member : Members)
		{
			Member->Transform(GroupTransform, true);
		}
		auto Reparent = std::make_unique<FReparentEdit>();
		Reparent->Objects = Members;
		Reparent->NewParent = Scope;
		Reparent->Redo(*this);
		Record(std::move(Reparent), EMapChange::Structure);
		Freed.insert(Freed.end(), Members.begin(), Members.end());
	}
	DeleteObjects(Targets);
	SetSelection(Freed);
	EndTransaction();
	return true;
}

// ----- Properties -----

void FMapDocument::SetObjectName(Object& Obj, const FString& Name)
{
	if (FString::FromAnsi(Obj.GetObjName()) == Name)
	{
		return;
	}
	const bool bOwn = BeginOwnTransaction("Rename");
	CaptureObjects({ &Obj });
	Obj.SetObjName(Name.ToAnsi().c_str());
	Changed(EMapChange::Geometry);
	if (bOwn)
	{
		EndTransaction();
	}
}

void FMapDocument::SetEntityKey(const TArray<Object*>& Objects, const FString& Key, const FString& Value)
{
	TArray<Object*> Entities;
	for (Object* Obj : Objects)
	{
		if (Obj && Obj->HasEntity())
		{
			Entities.push_back(Obj);
		}
	}
	if (Entities.empty() || Key.IsEmpty())
	{
		return;
	}
	const bool bOwn = BeginOwnTransaction(FString::Format("Set {}", Key));
	CaptureObjects(Entities);
	const std::string AnsiKey = Key.ToAnsi();
	const std::string AnsiValue = Value.ToAnsi();
	for (Object* Obj : Entities)
	{
		Entity* Ent = Obj->GetEntityPtr();
		if (AnsiValue.empty())
		{
			Ent->DelKey(AnsiKey.c_str());
		}
		else
		{
			Ent->SetKey(AnsiKey.c_str(), AnsiValue.c_str());
		}
	}
	Changed(EMapChange::Geometry);
	if (bOwn)
	{
		EndTransaction();
	}
}

namespace
{
	void ApplyTexture(Object& Obj, const char* Name)
	{
		if (Obj.HasBrush())
		{
			Geometry& Brush = Obj.GetBrush();
			for (int Face = 0; Face < Brush.GetNumFaces(); ++Face)
			{
				Brush.GetFaceTexturePtr(Face)->SetTName(Name);
			}
		}
		for (int Index = 0; Index < Obj.GetNumChildren(); ++Index)
		{
			ApplyTexture(Obj.GetChild(Index), Name);
		}
	}
}

void FMapDocument::SetTexture(const TArray<Object*>& Objects, const FString& TextureName)
{
	TArray<Object*> Targets;
	for (Object* Obj : Objects)
	{
		if (Obj && HasBrushes(*Obj))
		{
			Targets.push_back(Obj);
		}
	}
	if (Targets.empty())
	{
		return;
	}
	const bool bOwn = BeginOwnTransaction("Apply Texture");
	CaptureObjects(Targets);
	const std::string Name = TextureName.ToAnsi();
	for (Object* Obj : Targets)
	{
		ApplyTexture(*Obj, Name.c_str());
	}
	Changed(EMapChange::Geometry);
	if (bOwn)
	{
		EndTransaction();
	}
}

// ----- Brush editing -----

void FMapDocument::BeginBrushEdit(Object& Brush, const FString& Description)
{
	check(Brush.HasBrush() && !BrushEditTarget);
	BeginTransaction(Description);
	CaptureObjects({ &Brush });
	BrushEditTarget = &Brush;
	bBrushEditStarted = false;
}

namespace
{
	// A movement in the scope's space, in the brush object's own space.
	Vector3d ToObjectSpace(const Object& Obj, const Vector3d& Delta)
	{
		Matrix44 Inverse;
		Obj.CalInvTransSpaceMatrix(Inverse.SetIdentity());
		Vector3d Origin(0.0f, 0.0f, 0.0f);
		Vector3d Moved(Delta);
		Inverse.Transform(Origin);
		Inverse.Transform(Moved);
		return Moved.SubVector(Origin);
	}
}

void FMapDocument::MoveBrushVertex(int32 VertexIndex, const Vector3d& Delta)
{
	check(BrushEditTarget);
	Geometry& Brush = BrushEditTarget->GetBrush();
	if (!bBrushEditStarted)
	{
		Brush.BreakPlanes(VertexIndex);
		bBrushEditStarted = true;
	}
	Brush.MoveVertex(VertexIndex, ToObjectSpace(*BrushEditTarget, Delta));
	BrushEditTarget->SetBoundRadius(true);
	Changed(EMapChange::Geometry);
}

void FMapDocument::MoveBrushEdge(const Edge3d& Edge, const Vector3d& Delta)
{
	check(BrushEditTarget);
	Geometry& Brush = BrushEditTarget->GetBrush();
	if (!bBrushEditStarted)
	{
		Brush.BreakPlanes(Edge);
		bBrushEditStarted = true;
	}
	Brush.MoveEdge(Edge, ToObjectSpace(*BrushEditTarget, Delta));
	BrushEditTarget->SetBoundRadius(true);
	Changed(EMapChange::Geometry);
}

void FMapDocument::MoveBrushFace(const GPolygon& Face, const Vector3d& Delta)
{
	check(BrushEditTarget);
	Geometry& Brush = BrushEditTarget->GetBrush();
	if (!bBrushEditStarted)
	{
		Brush.BreakPlanes(Face);
		bBrushEditStarted = true;
	}
	Brush.MoveFace(Face, ToObjectSpace(*BrushEditTarget, Delta));
	BrushEditTarget->SetBoundRadius(true);
	Changed(EMapChange::Geometry);
}

bool FMapDocument::EndBrushEdit()
{
	check(BrushEditTarget);
	Object* Target = BrushEditTarget;
	BrushEditTarget = nullptr;
	if (!bBrushEditStarted)
	{
		CancelTransaction(); // nothing moved
		return true;
	}
	if (!Target->GetBrush().IntegrityCheck())
	{
		CancelTransaction();
		return false;
	}
	Target->SetBoundRadius(true);
	EndTransaction();
	return true;
}

// ----- Static mesh actors -----

namespace
{
	// Unreal's rotator about the map's axes: roll about X, pitch about Y,
	// yaw about Z, applied in that order (R = Rz Ry Rx). The map core keeps
	// an orientation as the rotated +Y (front) and +Z (top); the rotated +X
	// is front x top.
	void AxesFromDegrees(const Vector3d& Degrees, Vector3d& OutFront, Vector3d& OutTop)
	{
		const float Roll = DEG2RAD(Degrees.GetX());
		const float Pitch = DEG2RAD(Degrees.GetY());
		const float Yaw = DEG2RAD(Degrees.GetZ());
		const float cr = std::cos(Roll), sr = std::sin(Roll);
		const float cp = std::cos(Pitch), sp = std::sin(Pitch);
		const float cy = std::cos(Yaw), sy = std::sin(Yaw);
		OutFront.NewVector(cy * sp * sr - sy * cr, sy * sp * sr + cy * cr, cp * sr);
		OutTop.NewVector(cy * sp * cr + sy * sr, sy * sp * cr - cy * sr, cp * cr);
	}
}

Vector3d FMapDocument::GetMeshRotation(const Object& Obj)
{
	Vector3d Y, Z, X;
	Obj.GetOrientation(Y, Z);
	X.CrossVector(Y, Z);
	// X = (cy cp, sy cp, -sp), Y.z = cp sr, Z.z = cp cr.
	const float PitchRad = std::asin(FMath::Clamp(-X.GetZ(), -1.0f, 1.0f));
	float YawRad, RollRad;
	if (std::fabs(X.GetZ()) < 0.9999f)
	{
		YawRad = std::atan2(X.GetY(), X.GetX());
		RollRad = std::atan2(Y.GetZ(), Z.GetZ());
	}
	else
	{
		YawRad = std::atan2(-Y.GetX(), Y.GetY());
		RollRad = 0.0f;
	}
	const auto Clean = [](float Radians)
	{
		const float Degrees = RAD2DEG(Radians);
		return std::fabs(Degrees) < 0.0005f ? 0.0f : Degrees;
	};
	return Vector3d(Clean(RollRad), Clean(PitchRad), Clean(YawRad));
}

void FMapDocument::SetMeshRotation(Object& Obj, const Vector3d& Degrees)
{
	if (!Obj.HasMesh())
	{
		return;
	}
	Vector3d Front, Top;
	AxesFromDegrees(Degrees, Front, Top);
	const bool bOwn = BeginOwnTransaction("Rotate");
	CaptureObjects({ &Obj });
	Obj.TransSpace::SetOrientation(Front, Top);
	if (Obj.GetParentPtr())
	{
		Obj.GetParentPtr()->SetBoundRadius();
	}
	Changed(EMapChange::Geometry);
	if (bOwn)
	{
		EndTransaction();
	}
}

void FMapDocument::SetMeshScale(Object& Obj, const Vector3d& Scale)
{
	if (!Obj.HasMesh())
	{
		return;
	}
	const auto Safe = [](float V) { return std::fabs(V) < 1e-3f ? (V < 0.0f ? -1e-3f : 1e-3f) : V; };
	const bool bOwn = BeginOwnTransaction("Scale");
	CaptureObjects({ &Obj });
	Obj.SetScale(Vector3d(Safe(Scale.GetX()), Safe(Scale.GetY()), Safe(Scale.GetZ())));
	Changed(EMapChange::Geometry);
	if (bOwn)
	{
		EndTransaction();
	}
}

void FMapDocument::SetMesh(Object& Obj, const FString& MeshName)
{
	std::shared_ptr<const StaticMesh> Mesh = StaticMesh::Find(MeshName.ToAnsi().c_str());
	if (!Obj.HasMesh() || !Mesh || Mesh == Obj.GetMeshPtr())
	{
		return;
	}
	const bool bOwn = BeginOwnTransaction("Change Static Mesh");
	CaptureObjects({ &Obj });
	Obj.SetMesh(Mesh);
	if (Obj.GetParentPtr())
	{
		Obj.GetParentPtr()->SetBoundRadius();
	}
	Changed(EMapChange::Geometry);
	if (bOwn)
	{
		EndTransaction();
	}
}

bool FMapDocument::ReloadMeshAssets(const FAssetReference* Filter, FString& Error)
{
	TArray<std::pair<Object*, std::shared_ptr<const FLoadedMeshAsset>>> Updates;
	const auto Visit = [&](auto&& Self, Object& Obj) -> bool {
		if (const auto Old = Obj.GetMeshAsset(); Old && (!Filter || Old->Source.Id == Filter->Id))
		{
			const auto Loaded =
				FMapAssetLibrary::LoadMesh(Old->Source, Old->MaterialOverride, Error, Old->AdditionalOverrides);
			if (!Loaded)
				return false;
			Updates.push_back({&Obj, Loaded});
		}
		for (int I = 0; I < Obj.GetNumChildren(); ++I)
			if (!Self(Self, Obj.GetChild(I)))
				return false;
		return true;
	};
	if (!Visit(Visit, GetRoot()))
		return false;
	if (Updates.empty())
	{
		Error.Empty();
		return true;
	}
	const bool Own = BeginOwnTransaction(Filter ? "Refresh Mesh Asset" : "Refresh Render Assets");
	TArray<Object*> Objects;
	for (const auto& Update : Updates)
		Objects.push_back(Update.first);
	CaptureObjects(Objects);
	for (const auto& Update : Updates)
		Update.first->SetMeshAsset(Update.second);
	Changed(EMapChange::Geometry);
	if (Own)
		EndTransaction();
	Error.Empty();
	return true;
}

bool FMapDocument::ReloadMeshAsset(const FAssetReference& Ref,FString& Error) {
 if(Ref.Id.IsEmpty()) { Error="A mesh identity is required to refresh instances."; return false; }
 return ReloadMeshAssets(&Ref,Error);
}
bool FMapDocument::ReloadRenderAssets(FString& Error) { return ReloadMeshAssets(nullptr,Error); }
