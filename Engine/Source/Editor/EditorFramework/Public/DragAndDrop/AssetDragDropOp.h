// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "AssetRegistry.h"
#include "Input/DragAndDrop.h"
class FAssetDragDropOp final : public FDragDropOperation
{
  public:
	DRAG_DROP_OPERATOR_TYPE(FAssetDragDropOp, FDragDropOperation)
	FAssetReference Asset;
	FString Schema, Label;
	static TSharedRef<FDragDropOperation> New(FAssetReference, FString Schema, FString Label);
	TSharedPtr<SWidget> GetDefaultDecorator() const override;
};
