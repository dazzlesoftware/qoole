// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// What can be placed in a map (Unreal's placeable items), and the drag that
// carries one from the Place Actors panel or a content browser into a
// viewport, which places it under the cursor (SMapViewportLayout::OnPlace).

#pragma once

#include "CoreMinimal.h"
#include "Input/DragAndDrop.h"

enum class EMapPlaceableKind : uint8
{
	Entity, // Id: the entity class ("light")
	Prefab, // Id: the prefab's .qle file
	Mesh,   // Id: the static mesh (StaticMesh::Find): a static mesh actor
};

struct FMapPlaceable
{
	EMapPlaceableKind Kind = EMapPlaceableKind::Prefab;
	FString Id;
	FString Label;
	FString Category; // Place Actors: "Basic", "Lights", "Geometry"...
	FString ToolTip;

	bool operator==(const FMapPlaceable& Other) const { return Kind == Other.Kind && Id == Other.Id; }
};

class FMapPlaceDragDrop final : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FMapPlaceDragDrop, FDragDropOperation)

	explicit FMapPlaceDragDrop(FMapPlaceable InItem) : Item(std::move(InItem)) {}

	static TSharedRef<FDragDropOperation> New(FMapPlaceable Item);

	TSharedPtr<SWidget> GetDefaultDecorator() const override;
	TOptional<EMouseCursor> GetCursor() const override { return EMouseCursor::GrabHand; }

	FMapPlaceable Item;
};
