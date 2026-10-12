// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Drag and drop (docs/GenesisUI.md §7.5). A widget starts a drag from
// OnDragDetected by returning FReply::Handled().BeginDragDrop(Operation).
// While the drag is on, widgets under the pointer get OnDragEnter /
// OnDragOver / OnDragLeave and the one released over gets OnDrop. The
// operation's decorator widget follows the cursor.
//
// Operation types declare themselves with GE_DRAG_DROP_OPERATION_TYPE so
// targets can test what is being dragged:
//
//   class FItemDragDrop : public FDragDropOperation
//   {
//   public:
//       GE_DRAG_DROP_OPERATION_TYPE(FItemDragDrop, FDragDropOperation)
//       int32 ItemIndex = 0;
//   };
//   if (TSharedPtr<FItemDragDrop> Op = Event.GetOperationAs<FItemDragDrop>()) ...

#pragma once

#include "Input/Events.h"

class SWidget;

#define GE_DRAG_DROP_OPERATION_TYPE(Type, Base) \
	static const void* GetStaticTypeId() { static const char Id = 0; return &Id; } \
	bool IsOfTypeImpl(const void* TypeId) const override { return TypeId == GetStaticTypeId() || Base::IsOfTypeImpl(TypeId); }

class FDragDropOperation : public TSharedFromThis<FDragDropOperation>
{
public:
	virtual ~FDragDropOperation() = default;

	static const void* GetStaticTypeId() { static const char Id = 0; return &Id; }
	virtual bool IsOfTypeImpl(const void* TypeId) const { return TypeId == GetStaticTypeId(); }
	template <typename T>
	bool IsOfType() const { return IsOfTypeImpl(T::GetStaticTypeId()); }

	// The widget drawn at the cursor; nullptr for none.
	virtual TSharedPtr<SWidget> GetDefaultDecorator() const { return nullptr; }
	// Offset of the decorator from the cursor, logical units.
	virtual FVector2 GetDecoratorOffset() const { return FVector2(12.0f, 12.0f); }
	// The cursor while dragging; unset leaves it to widgets under the pointer.
	virtual TOptional<EMouseCursor> GetCursor() const { return {}; }

	// The drag ended. bDropWasHandled is false when nothing accepted it or the
	// drag was cancelled (Escape, capture loss).
	virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) { (void)bDropWasHandled; (void)MouseEvent; }
	virtual void OnDragged(const FPointerEvent& MouseEvent) { (void)MouseEvent; }
};

class FDragDropEvent : public FPointerEvent
{
public:
	FDragDropEvent(const FPointerEvent& InPointerEvent, TSharedPtr<FDragDropOperation> InOperation)
		: FPointerEvent(InPointerEvent), Operation(std::move(InOperation))
	{
	}

	TSharedPtr<FDragDropOperation> GetOperation() const { return Operation; }

	template <typename T>
	TSharedPtr<T> GetOperationAs() const
	{
		return Operation && Operation->IsOfType<T>() ? StaticCastSharedPtr<T>(Operation) : nullptr;
	}

private:
	TSharedPtr<FDragDropOperation> Operation;
};
