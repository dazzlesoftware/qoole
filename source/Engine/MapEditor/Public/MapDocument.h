// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// An open map: the map core's object tree (Object), its selection
// (Selector) and an undo stack. It replaces the MFC editor's CDocument
// (FEditorDoc) and its operation classes (EditorOp) for the new editor.
//
// Edits go through the document so they can be undone. Each one is a
// transaction; several edits can be grouped into one with
// BeginTransaction/EndTransaction (a drag that moves objects many times is
// one undo step). A transaction also remembers the selection before and
// after it, and undo/redo restore it. Selecting on its own is not an undo
// step.
//
// Objects edited are direct children of the edit scope, which is the root
// object (the map core's Selector requires selected objects to be children
// of the scope; groups and scope changes come later). Positions and
// movements are in the scope's space.
//
// The map core has one current game (Game::Get()); a document belongs to
// the game set that was active when it was created or opened.

#pragma once

#include "CoreMinimal.h"
#include "MapEditorEnvironment.h"

class Object;
class Selector;
class Vector3d;

enum class EMapChange : uint8
{
	None = 0,
	Selection = 1 << 0, // which objects are selected
	Geometry = 1 << 1,  // positions or shapes of existing objects
	Structure = 1 << 2, // objects added or removed
	File = 1 << 3,      // path or modified state
	All = 0xFF,
};

constexpr EMapChange operator|(EMapChange A, EMapChange B) { return static_cast<EMapChange>(static_cast<uint8>(A) | static_cast<uint8>(B)); }
constexpr EMapChange operator&(EMapChange A, EMapChange B) { return static_cast<EMapChange>(static_cast<uint8>(A) & static_cast<uint8>(B)); }
constexpr EMapChange& operator|=(EMapChange& A, EMapChange B) { return A = A | B; }
constexpr bool HasAnyChange(EMapChange Flags, EMapChange Test) { return (Flags & Test) != EMapChange::None; }

class FMapDocument
{
public:
	~FMapDocument();
	FMapDocument(const FMapDocument&) = delete;
	FMapDocument& operator=(const FMapDocument&) = delete;

	// An empty map (a worldspawn entity on the root) of the active game set.
	static TUniquePtr<FMapDocument> CreateNew();
	// Opens a .qle map (its header names its game set, which is activated)
	// or imports a .map file (into the active game set; saving then writes a
	// .qle next to it). Null with a reason on failure.
	static TUniquePtr<FMapDocument> Open(const FString& Path, FString& OutError);

	// Saves as .qle (the path becomes the document's) or exports .map (by
	// extension; the document keeps its path and modified state).
	bool Save(const FString& Path, FString& OutError);

	const FString& GetPath() const { return Path; }        // empty until saved
	FString GetDisplayName() const;                        // file name, or "Untitled"
	bool IsModified() const { return SavedVersion != Version; }
	const FMapGameSet& GetGameSet() const { return GameSet; }

	Object& GetRoot() const { return *Root; }
	Object& GetScope() const { return *Root; }
	Selector& GetSelector() const { return *SelectorPtr; }

	// Objects in the map (children of the scope) and counts for status text.
	TArray<Object*> GetObjects() const;
	int32 CountBrushes() const;
	int32 CountEntities() const;

	// ----- Selection -----
	TArray<Object*> GetSelection() const;
	bool IsSelected(const Object* Obj) const;
	void SetSelection(const TArray<Object*>& Objects);
	void AddToSelection(const TArray<Object*>& Objects);
	void ToggleSelection(Object* Obj);
	void SelectAll();
	void ClearSelection();
	// Center and bounds of the selection in scope space; false if empty.
	bool GetSelectionBounds(Vector3d& OutMin, Vector3d& OutMax) const;

	// ----- Edits -----
	void BeginTransaction(const FString& Description);
	void EndTransaction();
	bool IsInTransaction() const { return Pending != nullptr; }

	// Moves objects by Delta (scope space). Consecutive moves of the same
	// objects in one transaction are merged.
	void MoveObjects(const TArray<Object*>& Objects, const Vector3d& Delta);
	// Takes ownership of new, parentless objects, adds them to the scope and
	// selects them.
	void AddObjects(const TArray<Object*>& NewObjects);
	// Removes objects (kept by the undo stack until it lets go of them).
	void DeleteObjects(const TArray<Object*>& Objects);
	// Copies of the objects, offset by Delta, added and selected.
	TArray<Object*> DuplicateObjects(const TArray<Object*>& Objects, const Vector3d& Delta);
	// Loads a prefab (.qle) as one object centered on Position. Null with a
	// reason on failure.
	Object* AddPrefab(const FString& PrefabPath, const Vector3d& Position, FString* OutError = nullptr);
	// A point entity of the given class at Position.
	Object* AddEntity(const FString& ClassName, const Vector3d& Position);

	// ----- Undo -----
	bool CanUndo() const;
	bool CanRedo() const;
	FString GetUndoDescription() const;
	FString GetRedoDescription() const;
	void Undo();
	void Redo();
	// Undo steps kept (older ones are dropped).
	static constexpr int32 MaxUndoSteps = 200;

	// Every change, with what changed.
	TMulticastDelegate<void(EMapChange)> OnChanged;

	// Undo records (MapDocument.cpp).
	struct FEdit;
	struct FTransaction;

private:
	FMapDocument(Object* InRoot, const FMapGameSet& InGameSet);

	void ApplySelection(const TArray<Object*>& Objects);
	void Record(TUniquePtr<FEdit> Edit, EMapChange Change);
	void Changed(EMapChange Change);

	Object* Root = nullptr;
	Selector* SelectorPtr = nullptr;
	FMapGameSet GameSet;
	FString Path;

	TArray<TUniquePtr<FTransaction>> UndoStack;
	TArray<TUniquePtr<FTransaction>> RedoStack;
	TUniquePtr<FTransaction> Pending;
	int32 PendingDepth = 0;

	// Version counts applied transactions; SavedVersion is the version on
	// disk (-1 when no undo state matches it any more).
	int64 Version = 0;
	int64 NextVersion = 1;
	int64 SavedVersion = 0;
};
