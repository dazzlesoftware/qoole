// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// The undo records of FMapDocument. Each edit is applied when it is
// recorded; Undo and Redo switch it off and on. Objects an edit has taken
// out of the map belong to it, and it deletes them when it is dropped in
// that state (an undone add, a delete still in effect).
//
// Edits refer to objects by pointer, so no edit may replace an object by a
// copy: FContentEdit swaps what an object holds (brush, entity, children,
// transform, name) with a shadow object instead, and so every object keeps
// its identity however often a step is undone and redone.

#pragma once

#include "MapDocument.h"
#include "MapEditorPrivate.h"

struct FMapDocument::FEdit
{
	virtual ~FEdit() = default;
	virtual void Undo(FMapDocument& Doc) = 0;
	virtual void Redo(FMapDocument& Doc) = 0;
	bool bApplied = true;
};

struct FMapDocument::FTransaction
{
	FString Description;
	TArray<TUniquePtr<FEdit>> Edits;
	TArray<Object*> SelectionBefore;
	TArray<Object*> SelectionAfter;
	Object* ScopeBefore = nullptr;
	Object* ScopeAfter = nullptr;
	TArray<Object*> Captured; // objects with an FContentEdit in this transaction
	EMapChange Changes = EMapChange::None;
	int64 VersionBefore = 0;
	int64 VersionAfter = 0;
};

namespace MapEditorPrivate
{
	inline void MoveBy(const TArray<Object*>& Objects, const Vector3d& Delta)
	{
		for (Object* Obj : Objects)
		{
			Vector3d Position;
			Obj->GetPosition(Position).AddVector(Delta);
			Obj->TransSpace::SetPosition(Position);
		}
		if (!Objects.empty() && Objects[0]->GetParentPtr())
		{
			Objects[0]->GetParentPtr()->SetBoundRadius();
		}
	}

	// Exchanges everything two objects hold. B must be outside the map.
	void SwapContents(Object& A, Object& B);

	// An object that holds nothing (what is left of a brush clipped or
	// subtracted away).
	inline bool IsEmptyObject(const Object& Obj)
	{
		return !Obj.HasBrush() && !Obj.HasEntity() && !Obj.HasMesh() && !Obj.HasFoliage() && Obj.GetNumChildren() == 0;
	}
}

struct FMoveEdit final : FMapDocument::FEdit
{
	TArray<Object*> Objects;
	Vector3d Delta;

	void Undo(FMapDocument&) override
	{
		Vector3d Back(Delta);
		MapEditorPrivate::MoveBy(Objects, Back.MultVector(-1.0f));
	}
	void Redo(FMapDocument&) override { MapEditorPrivate::MoveBy(Objects, Delta); }
};

struct FAddEdit final : FMapDocument::FEdit
{
	TArray<Object*> Objects;
	Object* Parent = nullptr;

	~FAddEdit() override
	{
		if (!bApplied)
		{
			for (Object* Obj : Objects)
			{
				delete Obj;
			}
		}
	}
	void Undo(FMapDocument&) override
	{
		for (Object* Obj : Objects)
		{
			Parent->RemoveChild(*Obj, false);
		}
		Parent->SetBoundRadius();
	}
	void Redo(FMapDocument&) override
	{
		for (Object* Obj : Objects)
		{
			Parent->AddChild(*Obj, false);
		}
		Parent->SetBoundRadius();
	}
};

struct FDeleteEdit final : FMapDocument::FEdit
{
	TArray<Object*> Objects;
	TArray<Object*> Parents;
	TArray<int> Indices; // where each was in its parent, in removal order

	~FDeleteEdit() override
	{
		if (bApplied)
		{
			for (Object* Obj : Objects)
			{
				delete Obj;
			}
		}
	}
	void Undo(FMapDocument&) override
	{
		// Reinsert in reverse removal order so every index is valid again.
		for (SIZE_T Index = Objects.size(); Index-- > 0;)
		{
			Parents[Index]->AddChild(*Objects[Index], false, Indices[Index]);
			Parents[Index]->SetBoundRadius();
		}
	}
	void Redo(FMapDocument&) override
	{
		Indices.clear();
		Parents.clear();
		for (Object* Obj : Objects)
		{
			Object* Parent = Obj->GetParentPtr();
			int RemovedAt = -1;
			Parent->RemoveChild(*Obj, false, &RemovedAt);
			Parent->SetBoundRadius();
			Parents.push_back(Parent);
			Indices.push_back(RemovedAt);
		}
	}
};

// Moves objects to another parent (grouping), keeping their transforms.
struct FReparentEdit final : FMapDocument::FEdit
{
	TArray<Object*> Objects;
	Object* NewParent = nullptr;
	TArray<Object*> OldParents;
	TArray<int> OldIndices;

	void Undo(FMapDocument&) override
	{
		for (SIZE_T Index = Objects.size(); Index-- > 0;)
		{
			NewParent->RemoveChild(*Objects[Index], false);
			OldParents[Index]->AddChild(*Objects[Index], false, OldIndices[Index]);
			OldParents[Index]->SetBoundRadius();
		}
		NewParent->SetBoundRadius();
	}
	void Redo(FMapDocument&) override
	{
		OldParents.clear();
		OldIndices.clear();
		for (Object* Obj : Objects)
		{
			Object* Parent = Obj->GetParentPtr();
			int RemovedAt = -1;
			Parent->RemoveChild(*Obj, false, &RemovedAt);
			Parent->SetBoundRadius();
			OldParents.push_back(Parent);
			OldIndices.push_back(RemovedAt);
			NewParent->AddChild(*Obj, false);
		}
		NewParent->SetBoundRadius();
	}
};

// An object changed in place (transformed, clipped, hollowed, a key set...):
// the shadow holds the other state, and undo and redo exchange the two.
struct FContentEdit final : FMapDocument::FEdit
{
	Object* Target = nullptr;
	Object* Shadow = nullptr; // a copy of the target before the change

	explicit FContentEdit(Object& InTarget) : Target(&InTarget), Shadow(new Object(InTarget)) {}
	~FContentEdit() override { delete Shadow; }

	void Undo(FMapDocument&) override { MapEditorPrivate::SwapContents(*Target, *Shadow); }
	void Redo(FMapDocument&) override { MapEditorPrivate::SwapContents(*Target, *Shadow); }
};
