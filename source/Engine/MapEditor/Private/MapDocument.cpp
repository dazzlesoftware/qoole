// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "MapDocument.h"

#include "MapEditorPrivate.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

using namespace MapEditorPrivate;

void MapEditorPrivate::GetObjectBounds(Object& Obj, Vector3d& OutMin, Vector3d& OutMax)
{
	Vector3d LocalMin, LocalMax;
	Obj.GetBoundingVectors(LocalMin, LocalMax);
	Matrix44 Transform;
	Obj.CalTransSpaceMatrix(Transform.SetIdentity());
	bool bFirst = true;
	for (int Corner = 0; Corner < 8; ++Corner)
	{
		Vector3d Point((Corner & 1) ? LocalMax.GetX() : LocalMin.GetX(), (Corner & 2) ? LocalMax.GetY() : LocalMin.GetY(),
			(Corner & 4) ? LocalMax.GetZ() : LocalMin.GetZ());
		Transform.Transform(Point);
		if (bFirst)
		{
			OutMin = OutMax = Point;
			bFirst = false;
			continue;
		}
		OutMin.NewVector(FMath::Min(OutMin.GetX(), Point.GetX()), FMath::Min(OutMin.GetY(), Point.GetY()), FMath::Min(OutMin.GetZ(), Point.GetZ()));
		OutMax.NewVector(FMath::Max(OutMax.GetX(), Point.GetX()), FMath::Max(OutMax.GetY(), Point.GetY()), FMath::Max(OutMax.GetZ(), Point.GetZ()));
	}
}

// ---------------------------------------------------------------------------
// Edits: one change each, applied when recorded. The objects an edit has
// taken out of the map belong to it, and it deletes them when it is dropped
// in that state (an undone add, or a delete still in effect).

struct FMapDocument::FEdit
{
	virtual ~FEdit() = default;
	virtual void Undo(FMapDocument& Doc) = 0;
	virtual void Redo(FMapDocument& Doc) = 0;
	bool bApplied = true;
};

namespace
{
	void MoveBy(const TArray<Object*>& Objects, const Vector3d& Delta)
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
}

struct FMoveEdit final : FMapDocument::FEdit
{
	TArray<Object*> Objects;
	Vector3d Delta;

	void Undo(FMapDocument&) override
	{
		Vector3d Back(Delta);
		MoveBy(Objects, Back.MultVector(-1.0f));
	}
	void Redo(FMapDocument&) override { MoveBy(Objects, Delta); }
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
	TArray<int> Indices; // where each was in Parent, in removal order
	Object* Parent = nullptr;

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
			Parent->AddChild(*Objects[Index], false, Indices[Index]);
		}
		Parent->SetBoundRadius();
	}
	void Redo(FMapDocument&) override
	{
		Indices.clear();
		for (Object* Obj : Objects)
		{
			int RemovedAt = -1;
			Parent->RemoveChild(*Obj, false, &RemovedAt);
			Indices.push_back(RemovedAt);
		}
		Parent->SetBoundRadius();
	}
};

struct FMapDocument::FTransaction
{
	FString Description;
	TArray<TUniquePtr<FEdit>> Edits;
	TArray<Object*> SelectionBefore;
	TArray<Object*> SelectionAfter;
	EMapChange Changes = EMapChange::None;
	int64 VersionBefore = 0;
	int64 VersionAfter = 0;
};

// ---------------------------------------------------------------------------

FMapDocument::FMapDocument(Object* InRoot, const FMapGameSet& InGameSet)
	: Root(InRoot)
	, GameSet(InGameSet)
{
	SelectorPtr = new Selector(*Root);
}

FMapDocument::~FMapDocument()
{
	// Edits delete the objects they hold first; the tree goes last.
	Pending.reset();
	RedoStack.clear();
	UndoStack.clear();
	SelectorPtr->MUnselect();
	delete SelectorPtr;
	delete Root;
}

TUniquePtr<FMapDocument> FMapDocument::CreateNew()
{
	const FMapGameSet* Active = FMapEditorEnvironment::GetActiveGameSet();
	check(Active);
	Object* Root = new Object;
	Root->SetEntity(new Entity("worldspawn"));
	return TUniquePtr<FMapDocument>(new FMapDocument(Root, *Active));
}

namespace
{
	// The header MFC Genesis Editor and Qoole 99 write before a .qle body.
	struct FQleHeader
	{
		FString Game;
		FString EntList;
		FString Palette;
	};

	bool ReadQleHeader(const FString& Path, FQleHeader& Out)
	{
		LFile File;
		if (!File.Open(Path.ToAnsi().c_str()))
		{
			return false;
		}
		File.ResetLineNumber();
		const char* First = File.GetNextLine();
		if (!First || (strnicmp(First, "// GENESIS", 10) != 0 && strnicmp(First, "// QOOLE", 8) != 0))
		{
			return false;
		}
		bool bGame = false;
		bool bEnt = false;
		while (const char* Line = File.GetNextLine())
		{
			if (File.GetLineNumber() > 10)
			{
				break;
			}
			if (!strnicmp(Line, "// Game:", 8))
			{
				Out.Game = FString::FromAnsi(Line + 8).TrimStartAndEnd();
				bGame = true;
			}
			else if (!strnicmp(Line, "// EntList:", 11))
			{
				Out.EntList = FString::FromAnsi(Line + 11).TrimStartAndEnd();
				bEnt = true;
			}
			else if (!strnicmp(Line, "// Palette:", 11))
			{
				Out.Palette = FString::FromAnsi(Line + 11).TrimStartAndEnd();
			}
		}
		return bGame && bEnt;
	}

	bool WriteQleHeader(const FString& Path, const FMapGameSet& Set)
	{
		FILE* Out = _wfopen(Path.ToWide().c_str(), L"wt");
		if (!Out)
		{
			return false;
		}
		std::fprintf(Out, "// GENESIS EDITOR\n");
		std::fprintf(Out, "// Game: %s\n", Set.Game.ToAnsi().c_str());
		std::fprintf(Out, "// EntList: %s\n", Set.EntFile.ToAnsi().c_str());
		std::fprintf(Out, "// Palette: %s\n", Set.Palette.ToAnsi().c_str());
		std::fclose(Out);
		return true;
	}

	bool HasExtension(const FString& Path, std::string_view Extension)
	{
		return FPaths::GetExtension(Path).Equals(Extension, ESearchCase::IgnoreCase);
	}
}

TUniquePtr<FMapDocument> FMapDocument::Open(const FString& InPath, FString& OutError)
{
	if (!FFileHelper::FileExists(InPath))
	{
		OutError = FString::Format("{} does not exist.", InPath);
		return nullptr;
	}
	const std::string AnsiPath = InPath.ToAnsi();

	if (HasExtension(InPath, "map"))
	{
		const FMapGameSet* Active = FMapEditorEnvironment::GetActiveGameSet();
		check(Active);
		Object* Root = MapIO::ReadMap(AnsiPath.c_str());
		if (!Root)
		{
			OutError = FString::Format("{} could not be read as a .map file.", FPaths::GetCleanFilename(InPath));
			return nullptr;
		}
		TUniquePtr<FMapDocument> Doc(new FMapDocument(Root, *Active));
		// Like the MFC editor: an imported map is saved as .qle next to it.
		const std::filesystem::path QlePath = std::filesystem::path(InPath.ToWide()).replace_extension(L".qle");
		Doc->Path = FString::FromWide(QlePath.wstring());
		Doc->SavedVersion = -1;
		return Doc;
	}

	FQleHeader Header;
	const FMapGameSet* Set = nullptr;
	if (ReadQleHeader(InPath, Header))
	{
		Set = FMapEditorEnvironment::FindGameSet(Header.Game, Header.EntList);
		if (!Set)
		{
			OutError = FString::Format("{} is a map for {} ({}), which is not available.", FPaths::GetCleanFilename(InPath), Header.Game, Header.EntList);
			return nullptr;
		}
		if (FMapEditorEnvironment::GetActiveGameSet() != Set && !FMapEditorEnvironment::Activate(*Set, &OutError))
		{
			return nullptr;
		}
	}
	else
	{
		// No header (a prefab, or a map written by hand): the active set.
		Set = FMapEditorEnvironment::GetActiveGameSet();
		check(Set);
	}

	Object* Root = new Object;
	if (!Root->LoadObjFile(AnsiPath.c_str()))
	{
		delete Root;
		OutError = FString::Format("{} could not be read.", FPaths::GetCleanFilename(InPath));
		return nullptr;
	}
	if (!Root->HasEntity())
	{
		Root->SetEntity(new Entity("worldspawn"));
	}
	TUniquePtr<FMapDocument> Doc(new FMapDocument(Root, *Set));
	Doc->Path = InPath;
	return Doc;
}

bool FMapDocument::Save(const FString& InPath, FString& OutError)
{
	check(!Pending);
	const std::string AnsiPath = InPath.ToAnsi();
	if (HasExtension(InPath, "map"))
	{
		if (!MapIO::WriteMap(AnsiPath.c_str(), *Root, nullptr))
		{
			OutError = FString::Format("{} could not be written.", InPath);
			return false;
		}
		return true;
	}
	if (!WriteQleHeader(InPath, GameSet) || !Root->SaveObjFile(AnsiPath.c_str(), true))
	{
		OutError = FString::Format("{} could not be written.", InPath);
		return false;
	}
	Path = InPath;
	SavedVersion = Version;
	Changed(EMapChange::File);
	return true;
}

FString FMapDocument::GetDisplayName() const
{
	return Path.IsEmpty() ? FString("Untitled") : FPaths::GetCleanFilename(Path);
}

TArray<Object*> FMapDocument::GetObjects() const
{
	TArray<Object*> Result;
	for (int Index = 0; Index < Root->GetNumChildren(); ++Index)
	{
		Result.push_back(&Root->GetChild(Index));
	}
	return Result;
}

int32 FMapDocument::CountBrushes() const
{
	return Root->CountBrushes();
}

int32 FMapDocument::CountEntities() const
{
	// The root's worldspawn is not counted.
	return FMath::Max(0, Root->CountEntities() - (Root->HasEntity() ? 1 : 0));
}

// ----- Selection -----

TArray<Object*> FMapDocument::GetSelection() const
{
	return ToArray(SelectorPtr->GetMSelectedObjects());
}

bool FMapDocument::IsSelected(const Object* Obj) const
{
	return Obj && SelectorPtr->IsObjectMSelected(const_cast<Object*>(Obj));
}

void FMapDocument::ApplySelection(const TArray<Object*>& Objects)
{
	// Only children of the scope can be selected (and only once each).
	TArray<Object*> Valid;
	for (Object* Obj : Objects)
	{
		if (Obj && Obj->GetParentPtr() == Root && std::find(Valid.begin(), Valid.end(), Obj) == Valid.end())
		{
			Valid.push_back(Obj);
		}
	}
	LinkList<ObjectPtr> List;
	ToList(Valid, List);
	SelectorPtr->MSelectObjects(List);
}

void FMapDocument::SetSelection(const TArray<Object*>& Objects)
{
	const TArray<Object*> Before = GetSelection();
	ApplySelection(Objects);
	if (GetSelection() != Before)
	{
		if (Pending)
		{
			Pending->Changes |= EMapChange::Selection;
		}
		Changed(EMapChange::Selection);
	}
}

void FMapDocument::AddToSelection(const TArray<Object*>& Objects)
{
	TArray<Object*> Selection = GetSelection();
	Selection.insert(Selection.end(), Objects.begin(), Objects.end());
	SetSelection(Selection);
}

void FMapDocument::ToggleSelection(Object* Obj)
{
	TArray<Object*> Selection = GetSelection();
	const auto It = std::find(Selection.begin(), Selection.end(), Obj);
	if (It != Selection.end())
	{
		Selection.erase(It);
	}
	else
	{
		Selection.push_back(Obj);
	}
	SetSelection(Selection);
}

void FMapDocument::SelectAll()
{
	SetSelection(GetObjects());
}

void FMapDocument::ClearSelection()
{
	SetSelection({});
}

bool FMapDocument::GetSelectionBounds(Vector3d& OutMin, Vector3d& OutMax) const
{
	const TArray<Object*> Selection = GetSelection();
	bool bFirst = true;
	for (Object* Obj : Selection)
	{
		Vector3d Min, Max;
		GetObjectBounds(*Obj, Min, Max);
		if (bFirst)
		{
			OutMin = Min;
			OutMax = Max;
			bFirst = false;
			continue;
		}
		OutMin.NewVector(FMath::Min(OutMin.GetX(), Min.GetX()), FMath::Min(OutMin.GetY(), Min.GetY()), FMath::Min(OutMin.GetZ(), Min.GetZ()));
		OutMax.NewVector(FMath::Max(OutMax.GetX(), Max.GetX()), FMath::Max(OutMax.GetY(), Max.GetY()), FMath::Max(OutMax.GetZ(), Max.GetZ()));
	}
	return !bFirst;
}

// ----- Transactions -----

void FMapDocument::BeginTransaction(const FString& Description)
{
	if (PendingDepth++ > 0)
	{
		return;
	}
	Pending = std::make_unique<FTransaction>();
	Pending->Description = Description;
	Pending->SelectionBefore = GetSelection();
}

void FMapDocument::EndTransaction()
{
	check(PendingDepth > 0);
	if (--PendingDepth > 0)
	{
		return;
	}
	TUniquePtr<FTransaction> Done = std::move(Pending);
	if (Done->Edits.empty())
	{
		return; // selection changes alone are not undo steps
	}
	Done->SelectionAfter = GetSelection();
	Done->VersionBefore = Version;
	Done->VersionAfter = NextVersion++;
	Version = Done->VersionAfter;
	RedoStack.clear();
	UndoStack.push_back(std::move(Done));
	if (static_cast<int32>(UndoStack.size()) > MaxUndoSteps)
	{
		UndoStack.erase(UndoStack.begin());
	}
	Changed(EMapChange::File);
}

void FMapDocument::Record(TUniquePtr<FEdit> Edit, EMapChange Change)
{
	check(Pending);
	Pending->Edits.push_back(std::move(Edit));
	Pending->Changes |= Change;
	Changed(Change);
}

void FMapDocument::MoveObjects(const TArray<Object*>& Objects, const Vector3d& Delta)
{
	if (Objects.empty() || Delta == Vector3d(0.0f, 0.0f, 0.0f))
	{
		return;
	}
	const bool bOwnTransaction = !Pending;
	if (bOwnTransaction)
	{
		BeginTransaction("Move");
	}
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
	if (bOwnTransaction)
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
	const bool bOwnTransaction = !Pending;
	if (bOwnTransaction)
	{
		BeginTransaction(NewObjects.size() == 1 ? "Add Object" : "Add Objects");
	}
	auto Edit = std::make_unique<FAddEdit>();
	Edit->Objects = NewObjects;
	Edit->Parent = Root;
	Edit->Redo(*this);
	Record(std::move(Edit), EMapChange::Structure);
	SetSelection(NewObjects);
	if (bOwnTransaction)
	{
		EndTransaction();
	}
}

void FMapDocument::DeleteObjects(const TArray<Object*>& Objects)
{
	TArray<Object*> Valid;
	for (Object* Obj : Objects)
	{
		if (Obj && Obj->GetParentPtr() == Root)
		{
			Valid.push_back(Obj);
		}
	}
	if (Valid.empty())
	{
		return;
	}
	const bool bOwnTransaction = !Pending;
	if (bOwnTransaction)
	{
		BeginTransaction("Delete");
	}
	// Unselect first: the selector must not hold objects outside the scope.
	TArray<Object*> Remaining = GetSelection();
	std::erase_if(Remaining, [&](Object* Obj) { return std::find(Valid.begin(), Valid.end(), Obj) != Valid.end(); });
	SetSelection(Remaining);
	auto Edit = std::make_unique<FDeleteEdit>();
	Edit->Objects = Valid;
	Edit->Parent = Root;
	Edit->Redo(*this);
	Record(std::move(Edit), EMapChange::Structure);
	if (bOwnTransaction)
	{
		EndTransaction();
	}
}

TArray<Object*> FMapDocument::DuplicateObjects(const TArray<Object*>& Objects, const Vector3d& Delta)
{
	TArray<Object*> Copies;
	for (Object* Obj : Objects)
	{
		Object* Copy = new Object(*Obj);
		Vector3d Position;
		Copy->GetPosition(Position).AddVector(Delta);
		Copy->SetPosition(Position);
		Copies.push_back(Copy);
	}
	const bool bOwnTransaction = !Pending;
	if (bOwnTransaction)
	{
		BeginTransaction("Duplicate");
	}
	AddObjects(Copies);
	if (bOwnTransaction)
	{
		EndTransaction();
	}
	return Copies;
}

Object* FMapDocument::AddPrefab(const FString& PrefabPath, const Vector3d& Position, FString* OutError)
{
	Object* Prefab = new Object;
	if (!Prefab->LoadObjFile(PrefabPath.ToAnsi().c_str()))
	{
		delete Prefab;
		if (OutError)
		{
			*OutError = FString::Format("{} could not be loaded.", PrefabPath);
		}
		return nullptr;
	}
	Vector3d Offset;
	Prefab->GetPosition(Offset).AddVector(Position);
	Prefab->SetPosition(Offset);
	const FString Name = FPaths::GetCleanFilename(PrefabPath);
	BeginTransaction(FString::Format("Add {}", Name.Left(Name.Len() - 4)));
	AddObjects({ Prefab });
	EndTransaction();
	return Prefab;
}

Object* FMapDocument::AddEntity(const FString& ClassName, const Vector3d& Position)
{
	Object* Obj = new Object;
	Obj->SetEntity(new Entity(ClassName.ToAnsi().c_str()));
	Obj->SetPosition(Position);
	BeginTransaction(FString::Format("Add {}", ClassName));
	AddObjects({ Obj });
	EndTransaction();
	return Obj;
}

// ----- Undo -----

bool FMapDocument::CanUndo() const
{
	return !Pending && !UndoStack.empty();
}

bool FMapDocument::CanRedo() const
{
	return !Pending && !RedoStack.empty();
}

FString FMapDocument::GetUndoDescription() const
{
	return UndoStack.empty() ? FString() : UndoStack.back()->Description;
}

FString FMapDocument::GetRedoDescription() const
{
	return RedoStack.empty() ? FString() : RedoStack.back()->Description;
}

void FMapDocument::Undo()
{
	if (!CanUndo())
	{
		return;
	}
	TUniquePtr<FTransaction> Step = std::move(UndoStack.back());
	UndoStack.pop_back();
	// The selector may hold objects the edits take out of the scope.
	SelectorPtr->MUnselect();
	for (SIZE_T Index = Step->Edits.size(); Index-- > 0;)
	{
		Step->Edits[Index]->Undo(*this);
		Step->Edits[Index]->bApplied = false;
	}
	ApplySelection(Step->SelectionBefore);
	Version = Step->VersionBefore;
	const EMapChange Changes = Step->Changes | EMapChange::Selection | EMapChange::File;
	RedoStack.push_back(std::move(Step));
	Changed(Changes);
}

void FMapDocument::Redo()
{
	if (!CanRedo())
	{
		return;
	}
	TUniquePtr<FTransaction> Step = std::move(RedoStack.back());
	RedoStack.pop_back();
	SelectorPtr->MUnselect();
	for (TUniquePtr<FEdit>& Edit : Step->Edits)
	{
		Edit->Redo(*this);
		Edit->bApplied = true;
	}
	ApplySelection(Step->SelectionAfter);
	Version = Step->VersionAfter;
	const EMapChange Changes = Step->Changes | EMapChange::Selection | EMapChange::File;
	UndoStack.push_back(std::move(Step));
	Changed(Changes);
}

void FMapDocument::Changed(EMapChange Change)
{
	OnChanged.Broadcast(Change);
}
