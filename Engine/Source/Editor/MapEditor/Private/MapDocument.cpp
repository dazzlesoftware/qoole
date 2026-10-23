// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// The document's core: files, scope, selection, transactions and undo. The
// editing operations are in MapDocumentOps.cpp.

#include "MapDocument.h"
#include "Containers/ContainerAliases.h"
#include "Internationalization/TextLocalization.h"
#include "MapPackage.h"
#include "MapAssetLibrary.h"

#include "MapEdits.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <string>

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
		Vector3d Point((Corner & 1) ? LocalMax.GetX() : LocalMin.GetX(),
		    (Corner & 2) ? LocalMax.GetY() : LocalMin.GetY(), (Corner & 4) ? LocalMax.GetZ() : LocalMin.GetZ());
		Transform.Transform(Point);
		if (bFirst)
		{
			OutMin = OutMax = Point;
			bFirst = false;
			continue;
		}
		OutMin.NewVector(FMath::Min(OutMin.GetX(), Point.GetX()), FMath::Min(OutMin.GetY(), Point.GetY()),
		    FMath::Min(OutMin.GetZ(), Point.GetZ()));
		OutMax.NewVector(FMath::Max(OutMax.GetX(), Point.GetX()), FMath::Max(OutMax.GetY(), Point.GetY()),
		    FMath::Max(OutMax.GetZ(), Point.GetZ()));
	}
}

void MapEditorPrivate::SwapContents(Object& A, Object& B)
{
	check(B.GetParentPtr() == nullptr);
	// Take everything out first: the map core only lets a leaf without an
	// entity change its brush, and only a leaf without a brush take a point
	// entity.
	const TFunction<TArray<Object*>(Object&)> TakeChildren = [](Object& Obj)
	{
		TArray<Object*> Children;
		while (Obj.GetNumChildren() > 0)
		{
			Object& Child = Obj.GetChild(0);
			Obj.RemoveChild(Child, false);
			Children.push_back(&Child);
		}
		return Children;
	};
	const TArray<Object*> ChildrenA = TakeChildren(A);
	const TArray<Object*> ChildrenB = TakeChildren(B);
	const std::string NameA = A.GetObjName();
	const std::string NameB = B.GetObjName();
	Entity* EntityA = A.SetEntity(nullptr, false);
	Entity* EntityB = B.SetEntity(nullptr, false);
	Geometry* BrushA = A.SetBrush(nullptr);
	Geometry* BrushB = B.SetBrush(nullptr);
	std::shared_ptr<const Landscape> LandA = A.SetLandscape(nullptr);
	std::shared_ptr<const Landscape> LandB = B.SetLandscape(nullptr);
	std::shared_ptr<const Foliage> FoliageA = A.SetFoliage(nullptr);
	std::shared_ptr<const Foliage> FoliageB = B.SetFoliage(nullptr);
	const std::shared_ptr<const FLoadedMeshAsset> AssetA = A.GetMeshAsset(), AssetB = B.GetMeshAsset();
	std::shared_ptr<const StaticMesh> MeshA = A.SetMesh(nullptr);
	std::shared_ptr<const StaticMesh> MeshB = B.SetMesh(nullptr);

	A.SetBrush(BrushB);
	B.SetBrush(BrushA);
	A.SetMesh(MeshB);
	B.SetMesh(MeshA);
	A.SetLandscape(LandB); // with its own mesh
	B.SetLandscape(LandA);
	if (AssetB)
		A.SetMeshAsset(AssetB);
	if (AssetA)
		B.SetMeshAsset(AssetA);
	A.SetFoliage(FoliageB);
	B.SetFoliage(FoliageA);
	A.SetEntity(EntityB, false);
	B.SetEntity(EntityA, false);
	const TransSpace SpaceA = A;
	A.TransSpace::operator=(B);
	B.TransSpace::operator=(SpaceA);
	A.SetObjName(NameB.c_str());
	B.SetObjName(NameA.c_str());
	for (Object* Child : ChildrenB)
	{
		A.AddChild(*Child, false);
	}
	for (Object* Child : ChildrenA)
	{
		B.AddChild(*Child, false);
	}
	B.SetBoundRadius(false);
	A.SetBoundRadius(true);
}

// ---------------------------------------------------------------------------

FMapDocument::FMapDocument(Object* InRoot, const FMapGameSet& InGameSet)
    : Root(InRoot), Scope(InRoot), GameSet(InGameSet)
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
	// The header Genesis Editor and Qoole 99 write before a .qle body.
	struct FQleHeader
	{
		FString Game;
		FString EntList;
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

	FString NativeImportDestination(const FString& Source)
	{
		const FString Native =
		    FString::FromWide(std::filesystem::path(Source.ToWide()).replace_extension(L".gmap").wstring());
		// If a native level already exists beside an import, ask for a save
		// destination through the ordinary untitled-document Save As flow.
		return FFileHelper::FileExists(Native) ? FString() : Native;
	}
} // namespace

TUniquePtr<FMapDocument> FMapDocument::Open(const FString& InPath, FString& OutError)
{
	if (!FFileHelper::FileExists(InPath))
	{
		OutError = FTextLocalization::Format("{} does not exist.", InPath);
		return nullptr;
	}
	const std::string AnsiPath = InPath.ToAnsi();
	if (HasExtension(InPath, "gmap"))
	{
		FString Id;
		bool Upgraded = false;
		TUniquePtr<Object> Loaded = FMapPackage::Load(InPath, Id, OutError, false, {}, &Upgraded);
		if (!Loaded)
			return nullptr;
		const FMapGameSet* Active = FMapEditorEnvironment::GetActiveGameSet();
		check(Active);
		TUniquePtr<FMapDocument> Doc(new FMapDocument(Loaded.release(), *Active));
		Doc->Path = InPath;
		Doc->PackageId = Id;
		if (Upgraded)
			Doc->SavedVersion = -1;
		return Doc;
	}

	if (HasExtension(InPath, "map"))
	{
		const FMapGameSet* Active = FMapEditorEnvironment::GetActiveGameSet();
		check(Active);
		Object* Root = MapIO::ReadMap(AnsiPath.c_str());
		if (!Root)
		{
			OutError =
			    FTextLocalization::Format("{} could not be read as a .map file.", FPaths::GetCleanFilename(InPath));
			return nullptr;
		}
		TUniquePtr<FMapDocument> Doc(new FMapDocument(Root, *Active));
		// An imported map is saved as .qle next to it.
		Doc->Path = NativeImportDestination(InPath);
		Doc->SavedVersion = -1;
		return Doc;
	}

	FQleHeader Header;
	const FMapGameSet* Set = nullptr;
	bool bConverted = false;
	if (ReadQleHeader(InPath, Header))
	{
		Set = FMapEditorEnvironment::FindGameSet(Header.Game, Header.EntList);
		if (!Set)
		{
			// A map of a game the editor no longer has (Qoole's Quake, Half-Life...):
			// it opens with the built-in game and saves as a Genesis map.
			GENESIS_LOG(LogMapEditor, Display, "{} is a {} map; opening it as a Genesis map",
			    FPaths::GetCleanFilename(InPath), Header.Game);
			Set = &FMapEditorEnvironment::GetBuiltInGameSet();
			bConverted = true;
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
		OutError = FTextLocalization::Format("{} could not be read.", FPaths::GetCleanFilename(InPath));
		return nullptr;
	}
	if (!Root->HasEntity())
	{
		Root->SetEntity(new Entity("worldspawn"));
	}
	TUniquePtr<FMapDocument> Doc(new FMapDocument(Root, *Set));
	Doc->Path = NativeImportDestination(InPath);
	if (bConverted || HasExtension(InPath, "qle"))
	{
		Doc->SavedVersion = -1; // saving writes the new header
	}
	return Doc;
}

bool FMapDocument::Save(const FString& InPath, FString& OutError)
{
	check(!Pending);
	if (HasExtension(InPath, "gmap"))
	{
		// Save As creates a distinct package; ordinary saves keep its identity.
		FString Id = Path == InPath ? PackageId : FString();
		if (!FMapPackage::Save(InPath, *Root, Id, OutError))
			return false;
		PackageId = Id;
		Path = InPath;
		SavedVersion = Version;
		Changed(EMapChange::File);
		return true;
	}
	if (FMapAssetLibrary::HasNativeAssets(*Root))
	{
		OutError = "Save as .gmap to preserve native mesh, material and texture references.";
		return false;
	}
	const std::string AnsiPath = InPath.ToAnsi();
	if (HasExtension(InPath, "map"))
	{
		if (!MapIO::WriteMap(AnsiPath.c_str(), *Root, nullptr))
		{
			OutError = FTextLocalization::Format("{} could not be written.", InPath);
			return false;
		}
		return true;
	}
	if (!WriteQleHeader(InPath, GameSet) || !Root->SaveObjFile(AnsiPath.c_str(), true))
	{
		OutError = FTextLocalization::Format("{} could not be written.", InPath);
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
	for (int Index = 0; Index < Scope->GetNumChildren(); ++Index)
	{
		Result.push_back(&Scope->GetChild(Index));
	}
	return Result;
}

int32 FMapDocument::CountBrushes() const
{
	return Root->CountBrushes();
}

int32 FMapDocument::CountMeshes() const
{
	return Root->CountMeshes();
}

int32 FMapDocument::CountEntities() const
{
	// The root's worldspawn is not counted.
	return FMath::Max(0, Root->CountEntities() - (Root->HasEntity() ? 1 : 0));
}

bool FMapDocument::IsInMap(const Object& Obj) const
{
	const Object* Node = &Obj;
	while (Node->GetParentPtr())
	{
		Node = Node->GetParentPtr();
	}
	return Node == Root;
}

// ----- Scope -----

bool FMapDocument::IsGroup(const Object& Obj)
{
	return Obj.GetNumChildren() > 0 && !Obj.HasBrush();
}

void FMapDocument::ApplyScope(Object& NewScope)
{
	if (&NewScope == Scope)
	{
		return;
	}
	SelectorPtr->MUnselect();
	delete SelectorPtr;
	Scope = &NewScope;
	SelectorPtr = new Selector(*Scope);
}

Object* FMapDocument::FindActorObject(const FString& ActorId) const
{
	TArray<Object*> Nodes{Root};
	while (!Nodes.empty())
	{
		Object* Node = Nodes.back();
		Nodes.pop_back();
		if (Node->GetIdentity().Actor == ActorId)
		{
			return Node;
		}
		for (int Index = 0; Index < Node->GetNumChildren(); ++Index)
		{
			Nodes.push_back(&Node->GetChild(Index));
		}
	}
	return nullptr;
}

void FMapDocument::ApplyActorScope(const FString& ActorId)
{
	Object* Node = FindActorObject(ActorId);
	ApplyScope(Node ? *Node : *Root);
}

bool FMapDocument::SetScope(Object& NewScope)
{
	if (&NewScope == Scope)
	{
		return true;
	}
	if (!IsInMap(NewScope) || (&NewScope != Root && !IsGroup(NewScope)))
	{
		return false;
	}
	ApplyScope(NewScope);
	if (Pending)
	{
		Pending->Changes |= EMapChange::Scope;
	}
	Changed(EMapChange::Scope | EMapChange::Selection);
	return true;
}

bool FMapDocument::CanScopeUp() const
{
	return Scope != Root;
}

void FMapDocument::ScopeUp()
{
	if (!CanScopeUp())
	{
		return;
	}
	Object* Group = Scope;
	SetScope(*Scope->GetParentPtr());
	SetSelection({Group});
}

// ----- Selection -----

TArray<Object*> FMapDocument::GetSelection() const
{
	return ToArray(SelectorPtr->GetMSelectedObjects());
}

TArray<FString> FMapDocument::GetSelectedActorIds() const
{
	TArray<FString> Result;
	for (const Object* Obj : GetSelection())
	{
		Result.push_back(Obj->GetIdentity().Actor);
	}
	return Result;
}

void FMapDocument::ApplyActorSelection(const TArray<FString>& ActorIds)
{
	TMap<std::string, Object*> Children;
	for (int Index = 0; Index < Scope->GetNumChildren(); ++Index)
	{
		Object& Child = Scope->GetChild(Index);
		Children.emplace(Child.GetIdentity().Actor.ToStdString(), &Child);
	}
	TArray<Object*> Resolved;
	TSet<std::string> Seen;
	for (const FString& Id : ActorIds)
	{
		const std::string Key = Id.ToStdString();
		const TMap<std::string, Object*>::iterator Found = Children.find(Key);
		if (Found != Children.end() && Seen.insert(Key).second)
		{
			Resolved.push_back(Found->second);
		}
	}
	// Resolved contains only unique children, so do not repeat the pointer
	// adapter's quadratic duplicate search for large native selections.
	LinkList<ObjectPtr> List;
	ToList(Resolved, List);
	SelectorPtr->MSelectObjects(List);
}

void FMapDocument::SetSelectedActorIds(const TArray<FString>& ActorIds)
{
	const TArray<FString> Before = GetSelectedActorIds();
	ApplyActorSelection(ActorIds);
	if (GetSelectedActorIds() != Before)
	{
		if (Pending)
		{
			Pending->Changes |= EMapChange::Selection;
		}
		Changed(EMapChange::Selection);
	}
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
		if (Obj && Obj->GetParentPtr() == Scope && std::find(Valid.begin(), Valid.end(), Obj) == Valid.end())
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
	const TArray<Object*>::iterator It = std::find(Selection.begin(), Selection.end(), Obj);
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
	bool bFirst = true;
	for (Object* Obj : GetSelection())
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
		OutMin.NewVector(FMath::Min(OutMin.GetX(), Min.GetX()), FMath::Min(OutMin.GetY(), Min.GetY()),
		    FMath::Min(OutMin.GetZ(), Min.GetZ()));
		OutMax.NewVector(FMath::Max(OutMax.GetX(), Max.GetX()), FMath::Max(OutMax.GetY(), Max.GetY()),
		    FMath::Max(OutMax.GetZ(), Max.GetZ()));
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
	static std::atomic<uint64> NextTransactionId{0};
	TransactionId = ++NextTransactionId;
	Pending = std::make_unique<FTransaction>();
	Pending->Description = Description;
	Pending->SelectionBefore = GetSelectedActorIds();
	Pending->ScopeBefore = Scope->GetIdentity().Actor;
}

bool FMapDocument::BeginOwnTransaction(const FString& Description)
{
	if (Pending)
	{
		return false;
	}
	BeginTransaction(Description);
	return true;
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
		return; // selection and scope changes alone are not undo steps
	}
	Done->SelectionAfter = GetSelectedActorIds();
	Done->ScopeAfter = Scope->GetIdentity().Actor;
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

void FMapDocument::CancelTransaction()
{
	if (!Pending)
	{
		return;
	}
	TUniquePtr<FTransaction> Undone = std::move(Pending);
	PendingDepth = 0;
	BrushEditTarget = nullptr;
	SelectorPtr->MUnselect();
	for (SIZE_T Index = Undone->Edits.size(); Index-- > 0;)
	{
		Undone->Edits[Index]->Undo(*this);
		Undone->Edits[Index]->bApplied = false;
	}
	ApplyActorScope(Undone->ScopeBefore);
	ApplyActorSelection(Undone->SelectionBefore);
	Changed(Undone->Changes | EMapChange::Selection);
}

void FMapDocument::Record(TUniquePtr<FEdit> Edit, EMapChange Change)
{
	check(Pending);
	Pending->Edits.push_back(std::move(Edit));
	Pending->Changes |= Change;
	Changed(Change);
}

void FMapDocument::Changed(EMapChange Change)
{
	OnChanged.Broadcast(Change);
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
	ApplyActorScope(Step->ScopeBefore);
	ApplyActorSelection(Step->SelectionBefore);
	Version = Step->VersionBefore;
	const EMapChange Changes = Step->Changes | EMapChange::Selection | EMapChange::Scope | EMapChange::File;
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
	ApplyActorScope(Step->ScopeAfter);
	ApplyActorSelection(Step->SelectionAfter);
	Version = Step->VersionAfter;
	const EMapChange Changes = Step->Changes | EMapChange::Selection | EMapChange::Scope | EMapChange::File;
	UndoStack.push_back(std::move(Step));
	Changed(Changes);
}
