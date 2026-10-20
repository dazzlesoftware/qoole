// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// An open map: the map core's object tree (Object), its selection
// (Selector) and an undo stack. It replaces the MFC editor's CDocument
// (FEditorDoc) and its operation classes (EditorOp).
//
// Edits go through the document so they can be undone. Each one is a
// transaction; several edits can be grouped into one with
// BeginTransaction/EndTransaction (a drag that moves objects many times is
// one undo step). A transaction also remembers the selection and the scope
// before and after it, and undo/redo restore them. Selecting, and changing
// the scope, are not undo steps on their own.
//
// The scope is the group being edited: the root object, or a group inside
// it. Only its children can be selected (the map core's Selector requires
// it), the views show only its contents (as in Qoole), and positions and
// movements are in its space.
//
// The map core has one current game (Game::Get()); a document belongs to
// the game set that was active when it was created or opened.

#pragma once

#include "CoreMinimal.h"
#include "MapEditorEnvironment.h"

class Landscape;
class Matrix44;
class Object;
class Plane;
class Selector;
class Vector3d;
class Edge3d;
class GPolygon;

enum class EMapChange : uint8
{
	None = 0,
	Selection = 1 << 0, // which objects are selected
	Geometry = 1 << 1,  // positions, shapes or properties of existing objects
	Structure = 1 << 2, // objects added, removed or regrouped
	File = 1 << 3,      // path or modified state
	Scope = 1 << 4,     // the group being edited
	All = 0xFF,
};

constexpr EMapChange operator|(EMapChange A, EMapChange B) { return static_cast<EMapChange>(static_cast<uint8>(A) | static_cast<uint8>(B)); }
constexpr EMapChange operator&(EMapChange A, EMapChange B) { return static_cast<EMapChange>(static_cast<uint8>(A) & static_cast<uint8>(B)); }
constexpr EMapChange& operator|=(EMapChange& A, EMapChange B) { return A = A | B; }
constexpr bool HasAnyChange(EMapChange Flags, EMapChange Test) { return (Flags & Test) != EMapChange::None; }

// What a clip keeps of the objects it cuts.
enum class EMapClip : uint8
{
	KeepFront, // the side the plane's normal points to
	KeepBack,
	Split,     // both, as separate objects
};

class FMapDocument
{
public:
	~FMapDocument();
	FMapDocument(const FMapDocument&) = delete;
	FMapDocument& operator=(const FMapDocument&) = delete;

	// An empty map (a worldspawn entity on the root) of the active game set.
	static TUniquePtr<FMapDocument> CreateNew();
	// Opens a .qle map or imports a .map file (saving then writes a .qle next
	// to it). Maps of games that are not available open with the built-in
	// game, keeping their texture names. Null with a reason on failure.
	static TUniquePtr<FMapDocument> Open(const FString& Path, FString& OutError);

	// Saves as .qle (the path becomes the document's) or exports .map (by
	// extension; the document keeps its path and modified state).
	bool Save(const FString& Path, FString& OutError);

	const FString& GetPath() const { return Path; }        // empty until saved
	FString GetDisplayName() const;                        // file name, or "Untitled"
	bool IsModified() const { return SavedVersion != Version; }
	const FMapGameSet& GetGameSet() const { return GameSet; }

	Object& GetRoot() const { return *Root; }
	Object& GetScope() const { return *Scope; }
	Selector& GetSelector() const { return *SelectorPtr; }

	// The scope's objects, and counts of the whole map for status text.
	TArray<Object*> GetObjects() const;
	int32 CountBrushes() const;
	int32 CountEntities() const;
	int32 CountMeshes() const;

	// ----- Scope -----
	// The root or a group in the map. Clears the selection.
	bool SetScope(Object& NewScope);
	bool CanScopeUp() const;
	void ScopeUp(); // selects the group that was the scope
	static bool IsGroup(const Object& Obj);

	// ----- Selection -----
	TArray<Object*> GetSelection() const;
	bool IsSelected(const Object* Obj) const;
	void SetSelection(const TArray<Object*>& Objects);
	void AddToSelection(const TArray<Object*>& Objects);
	void ToggleSelection(Object* Obj);
	void SelectAll();
	void ClearSelection();
	// Bounds of the selection in scope space; false if empty.
	bool GetSelectionBounds(Vector3d& OutMin, Vector3d& OutMax) const;

	// ----- Transactions -----
	void BeginTransaction(const FString& Description);
	void EndTransaction();
	// Undoes what the open transaction did and drops it.
	void CancelTransaction();
	bool IsInTransaction() const { return Pending != nullptr; }

	// ----- Edits (each is an undo step unless a transaction is open) -----
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
	// A static mesh actor (StaticMesh::Find) with its pivot at Position; null
	// if there is no such mesh.
	Object* AddMesh(const FString& MeshName, const Vector3d& Position);
	// A flat landscape of QuadsX x QuadsY quads centered on Position.
	Object* AddLandscape(int32 QuadsX, int32 QuadsY, float QuadSize, const Vector3d& Position);
	// This landscape (an imported one) centered on Position.
	Object* AddLandscape(std::shared_ptr<const Landscape> Land, const Vector3d& Position, const FString& Description = "Add Landscape");
	// Gives a landscape other heights (sculpting). Inside an open transaction
	// the object is captured once, so a whole stroke is one undo step.
	void SetLandscape(Object& Obj, std::shared_ptr<const Landscape> NewLandscape, const FString& Description = "Sculpt Landscape");
	// Writes copies of objects as a prefab (.qle), centered on the origin.
	bool SavePrefab(const TArray<Object*>& Objects, const FString& PrefabPath, FString& OutError) const;

	// Records objects as about to change in place, so that undo restores
	// them; then change them and call ObjectsChanged. Objects already
	// recorded in the open transaction are skipped. Needs a transaction.
	void CaptureObjects(const TArray<Object*>& Objects);
	void ObjectsChanged(EMapChange Change = EMapChange::Geometry) { Changed(Change); }

	// Transforms objects about a pivot (scope space). Axis: 0 = X, 1 = Y, 2 = Z.
	void TransformObjects(const TArray<Object*>& Objects, const Matrix44& Transform, bool bMirror, const FString& Description);
	void RotateObjects(const TArray<Object*>& Objects, int32 Axis, float Degrees, const Vector3d& Pivot);
	void ScaleObjects(const TArray<Object*>& Objects, const Vector3d& Factors, const Vector3d& Pivot);
	void FlipObjects(const TArray<Object*>& Objects, int32 Axis, const Vector3d& Pivot);

	// Brush operations. Each returns whether anything changed.
	bool HollowObjects(const TArray<Object*>& Objects, float Thickness, bool bInward);
	// Subtracts the selected brushes from the other brushes of the scope.
	bool SubtractSelection();
	// Replaces the selected brushes by their common volume.
	bool IntersectSelection();
	bool ClipObjects(const TArray<Object*>& Objects, const Plane& ClipPlane, EMapClip Mode);

	// Groups objects of the scope into a new group (selected); ungroups
	// selected groups (their contents end up selected).
	Object* GroupObjects(const TArray<Object*>& Objects);
	bool UngroupObjects(const TArray<Object*>& Groups);

	// Properties.
	void SetObjectName(Object& Obj, const FString& Name);
	// Sets (or with an empty value removes) a key of the objects' entities.
	void SetEntityKey(const TArray<Object*>& Objects, const FString& Key, const FString& Value);
	// Applies a texture to every face of the objects' brushes (and groups').
	void SetTexture(const TArray<Object*>& Objects, const FString& TextureName);
	// A static mesh actor's rotation (degrees about X, Y and Z: roll, pitch,
	// yaw, as Unreal shows them), scale, and mesh.
	void SetMeshRotation(Object& Obj, const Vector3d& Degrees);
	void SetMeshScale(Object& Obj, const Vector3d& Scale);
	void SetMesh(Object& Obj, const FString& MeshName);
	// A mesh actor's rotation as SetMeshRotation takes it.
	static Vector3d GetMeshRotation(const Object& Obj);

	// Brush editing: moves a vertex, an edge or a face of one brush by Delta
	// (scope space) inside a transaction begun with BeginBrushEdit. The edit
	// ends with EndBrushEdit, which undoes the drag if the brush is no longer
	// convex.
	void BeginBrushEdit(Object& Brush, const FString& Description);
	void MoveBrushVertex(int32 VertexIndex, const Vector3d& Delta);
	void MoveBrushEdge(const Edge3d& Edge, const Vector3d& Delta);
	void MoveBrushFace(const GPolygon& Face, const Vector3d& Delta);
	bool EndBrushEdit();
	Object* GetBrushEditTarget() const { return BrushEditTarget; }

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

	// Undo records (Private/MapEdits.h).
	struct FEdit;
	struct FTransaction;

private:
	FMapDocument(Object* InRoot, const FMapGameSet& InGameSet);

	void ApplySelection(const TArray<Object*>& Objects);
	void ApplyScope(Object& NewScope);
	bool IsInMap(const Object& Obj) const;
	void Record(TUniquePtr<FEdit> Edit, EMapChange Change);
	void Changed(EMapChange Change);
	// Opens a transaction if none is (returns whether it did).
	bool BeginOwnTransaction(const FString& Description);
	// Removes objects an operation emptied.
	void DeleteEmptied(const TArray<Object*>& Objects);

	Object* Root = nullptr;
	Object* Scope = nullptr;
	Selector* SelectorPtr = nullptr;
	FMapGameSet GameSet;
	FString Path;

	TArray<TUniquePtr<FTransaction>> UndoStack;
	TArray<TUniquePtr<FTransaction>> RedoStack;
	TUniquePtr<FTransaction> Pending;
	int32 PendingDepth = 0;

	Object* BrushEditTarget = nullptr;
	bool bBrushEditStarted = false; // planes broken for the first move

	// Version counts applied transactions; SavedVersion is the version on
	// disk (-1 when no undo state matches it any more).
	int64 Version = 0;
	int64 NextVersion = 1;
	int64 SavedVersion = 0;
};
