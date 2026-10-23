// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "MaterialEditingLibrary.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/WidgetDelegates.h"
class SMaterialGraph final : public SLeafWidget
{
  public:
	SLATE_BEGIN_ARGS(SMaterialGraph)
	SLATE_ARGUMENT(std::shared_ptr<FMaterialAsset>, Material, nullptr)
	SLATE_ARGUMENT(std::shared_ptr<FAssetRegistry>, Registry, nullptr)
	SLATE_EVENT(FSimpleDelegate, OnChanged)
	SLATE_EVENT(FSimpleDelegate, OnSelectionChanged)
	SLATE_EVENT(TDelegate<void(const FString&)>, OnError)
	SLATE_END_ARGS()
	void Construct(const FArguments&);
	uint32 AddNode(EMaterialExpression, FVector2 Position);
	bool Connect(uint32 From, uint32 To, int Input, FString& Error); // To=0: material output
	void Disconnect(uint32 To, int Input);
	bool DropAsset(const FAssetReference&, const FString& Schema, FVector2, FString&);
	FReply OnDragOver(const FGeometry&, const FDragDropEvent&) override;
	FReply OnDrop(const FGeometry&, const FDragDropEvent&) override;
	void DeleteSelected();
	void SetNodeSelection(const TArray<uint32>& Nodes);
	const TArray<uint32>& GetSelectedNodes() const
	{
		return Selection;
	}
	FString CopySelection() const;
	bool PasteNodes(const FString&, FVector2 Position, FString& Error);
	void DuplicateSelected();
	void Undo();
	void Redo();
	void FrameAll();
	void SetQualityLevel(uint8);
	void SetUseMaterialAttributes(bool);
	void SetMaterialSettings(EMaterialBlendMode, float MaskCutoff);
	void EditNode(const TFunction<void(FMaterialNode&)>&);
	uint32 GetSelectedNode() const
	{
		return Selected;
	}
	FVector2 GetPinPosition(uint32 Node, int Input, bool Output) const;
	bool SupportsKeyboardFocus() const override
	{
		return true;
	}
	FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override;
	FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override;
	FReply OnMouseMove(const FGeometry&, const FPointerEvent&) override;
	FReply OnMouseWheel(const FGeometry&, const FPointerEvent&) override;
	FReply OnKeyDown(const FGeometry&, const FKeyEvent&) override;
	void Tick(const FGeometry&, double, float) override;
	void OnMouseCaptureLost() override;

  protected:
	FVector2 ComputeDesiredSize(float) const override
	{
		return FVector2(800, 600);
	}
	int32 OnPaint(const FPaintArgs&, const FGeometry&, const FRect&, FSlateWindowElementList&, int32,
				  const FWidgetStyle&) const override;

  private:
	FMaterialGraph& Graph()
	{
		return *Material->Graph;
	}
	const FMaterialGraph& Graph() const
	{
		return *Material->Graph;
	}
	void Remember();
	void Changed();
	bool HitPin(FVector2, uint32&, int&, bool&) const;
	bool IsNodeSelected(uint32 Id) const;
	FVector2 ToCanvas(FVector2 P) const
	{
		return (P - Pan) / Zoom;
	}
	std::shared_ptr<FAssetRegistry> Registry;
	std::shared_ptr<FMaterialAsset> Material;
	FSimpleDelegate OnChanged, OnSelectionChanged;
	TDelegate<void(const FString&)> OnError;
	struct FHistoryEntry
	{
		FMaterialGraph Graph;
		EMaterialBlendMode Blend;
		float MaskCutoff;
	};
	TArray<FHistoryEntry> UndoStack, RedoStack;
	TArray<uint32> Selection;
	FVector2 Pan{30, 30}, Last, Cursor, MenuPosition;
	FVector2 MarqueeStart;
	float Zoom = .75f;
	uint32 Selected = 0, WireSource = 0;
	bool bDragging = false, bPanning = false, bMoved = false;
	bool bInitialFrame = true;
	bool bMarquee = false;
};
