// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// A small 2D "map" shared by the editor framework test applications
// (UIViewportTest, UIPropertyGridTest, UIDockingTest): entities with an
// undo stack of whole-scene snapshots, a property source for them, and a
// viewport client that draws and edits them through the RHI.

#pragma once

#include "Framework/PropertyGrid/PropertySource.h"
#include "RenderTargetViewportClient.h"

struct FDemoEntity
{
	int32 Id = 0;
	FString Name;
	int32 Kind = 0; // 0 brush, 1 light, 2 player start, 3 trigger
	FVector2 Position;
	FVector2 Size = FVector2(64.0f, 64.0f);
	FColor Color = FColor(120, 140, 170);
	float Brightness = 1.0f; // lights only
	bool bVisible = true;

	bool operator==(const FDemoEntity&) const = default;
};

const char* GetEntityKindName(int32 Kind);

// The edited data, its selection and an undo stack of whole-scene snapshots
// (the scene is small).
class FDemoScene
{
public:
	FDemoScene();

	TArray<FDemoEntity> Entities;
	TArray<int32> Selection;

	FDemoEntity* Find(int32 Id);
	const FDemoEntity* Find(int32 Id) const;
	bool IsSelected(int32 Id) const { return ArrayUtil::Contains(Selection, Id); }
	void SetSelection(TArray<int32> Ids);

	// Undo: changes between Begin and End are one step.
	void BeginTransaction(const FString& Description);
	void EndTransaction();
	bool CanUndo() const { return !UndoStack.empty(); }
	bool CanRedo() const { return !RedoStack.empty(); }
	FString GetUndoDescription() const { return UndoStack.empty() ? FString() : UndoStack.back().Description; }
	void Undo();
	void Redo();
	// Adds an entity of a kind (0 brush, 1 light) as an undo step and selects it.
	int32 AddEntity(int32 Kind);
	void DeleteSelection();
	void Changed() { OnChanged.Broadcast(); }

	TMulticastDelegate<void()> OnChanged;
	TMulticastDelegate<void()> OnSelectionChanged;
	TMulticastDelegate<void(const FString&)> OnLog;

private:
	struct FUndoStep
	{
		FString Description;
		TArray<FDemoEntity> Before;
		TArray<FDemoEntity> After;
	};
	TArray<FUndoStep> UndoStack;
	TArray<FUndoStep> RedoStack;
	TOptional<FUndoStep> Pending;
	int32 Depth = 0;
};

// Draws the scene into a render target: grid, entities, selection.
// Left click selects (Shift adds), left drag moves the selection, the wheel
// zooms, the middle button pans, and the right button pans with raw mouse
// input (the cursor hides until it is released).
class FDemoViewportClient final : public FRenderTargetViewportClient
{
public:
	FDemoViewportClient(FRHIDevice& InDevice, FUIRenderer& InRenderer, FDemoScene& InScene);

	bool bShowGrid = true;
	bool bSnap = true;
	void FrameSelection();
	float GetZoom() const { return Zoom; }
	// How many times the viewport was drawn (it draws only when something changes).
	int32 GetDrawCount() const { return DrawCount; }
	// A world position in the viewport, in logical units.
	FVector2 WorldToLocal(FVector2 World) const { return World * Zoom + Pan; }

	FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
	FReply OnRawMouseMove(FVector2 Delta) override;
	void OnMouseCaptureLost() override;
	FCursorReply OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const override;

protected:
	void Draw(FRHICommandList& CommandList, FUIRenderTarget& Target) override;

private:
	FVector2 ToWorld(const FGeometry& Geometry, FVector2 Absolute) const;
	int32 PickEntity(FVector2 World) const;
	float Snap(float Value) const { return bSnap ? FMath::RoundToFloat(Value / 16.0f) * 16.0f : Value; }

	FDemoScene& Scene;
	FVector2 Pan = FVector2(40.0f, 40.0f); // pixels of the world origin, in logical units
	float Zoom = 1.0f;
	float DpiScale = 1.0f;
	enum class EDrag : uint8 { None, Move, Pan, Look } Drag = EDrag::None;
	FVector2 DragStartWorld;
	FVector2 LastAbsolute;
	TMap<int32, FVector2> DragStartPositions;
	FDrawElementList Elements;
	int32 DrawCount = 0;
};

// An entity's properties for SPropertyGrid. Lights have a brightness.
class FDemoEntitySource final : public IPropertySource
{
public:
	FDemoEntitySource(FDemoScene& InScene, int32 InId) : Scene(InScene), Id(InId) {}

	FString GetDisplayName() const override;
	void GetProperties(TArray<FPropertyDesc>& Out) const override;
	FPropertyValue GetValue(FName Property) const override;
	void SetValue(FName Property, const FPropertyValue& Value) override;

private:
	FDemoScene& Scene;
	int32 Id;
};
