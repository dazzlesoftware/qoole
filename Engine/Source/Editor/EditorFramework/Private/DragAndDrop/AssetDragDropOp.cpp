// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "DragAndDrop/AssetDragDropOp.h"
#include "Templates/SharedPointer.h"
#include "Widgets/Text/STextBlock.h"
TSharedRef<FDragDropOperation> FAssetDragDropOp::New(FAssetReference Asset, FString Schema, FString Label)
{
	TSharedRef<FAssetDragDropOp> Op = MakeShared<FAssetDragDropOp>();
	Op->Asset = std::move(Asset);
	Op->Schema = std::move(Schema);
	Op->Label = std::move(Label);
	return Op;
}
TSharedPtr<SWidget> FAssetDragDropOp::GetDefaultDecorator() const
{
	return (SNew(STextBlock).Text(Label)).ToSharedPtr();
}
