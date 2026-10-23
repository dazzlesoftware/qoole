// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "WorldCompositionEditor.h"
#include "SWorldCompositionGrid.h"
#include "World/WorldCompositionLimits.h"
#include "Templates/SharedPointer.h"
#include "Internationalization/TextLocalization.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Text/SEditableText.h"
#include "Widgets/Text/STextBlock.h"
#include "Services/Services.h"
#include "Services/MessageDialog.h"

namespace
{
	constexpr float DetailsWidth = 300.0f;

	class SWorldCompositionEditor final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SWorldCompositionEditor)
		SLATE_END_ARGS()
		void Construct(const FArguments&, FWorldCompositionEditorArgs InArgs,
		    std::shared_ptr<FWorldCompositionEditSession> InSession)
		{
			Args = std::move(InArgs);
			Session = std::move(InSession);
			TSharedRef<SHorizontalBox> Toolbar = SNew(SHorizontalBox);
			const TFunction<void(const FString&, const FString&, TFunction<void()>)> Button =
			    [&Toolbar](const FString& Label, const FString& Tip, TFunction<void()> Action)
			{
				Toolbar->AddSlot().AutoWidth()[SNew(SButton).Text(Label).ToolTipText(Tip).OnClicked([Action]
				{
					Action();
					return FReply::Handled();
				})];
			};
			Button(GENESIS_TEXT("New Region"), GENESIS_TEXT("Create a region map with a flat landscape in a free cell"),
			    [this] { NewRegion(SuggestedCellX(), SuggestedCellY()); });
			Button(GENESIS_TEXT("Undo"), GENESIS_TEXT("Undo the last region change"), [this] { History(true); });
			Button(
			    GENESIS_TEXT("Redo"), GENESIS_TEXT("Redo the last undone region change"), [this] { History(false); });
			Button(GENESIS_TEXT("Frame All"), GENESIS_TEXT("Show every region (F)"), [this] { Grid->FrameAll(); });
			Button(GENESIS_TEXT("Preview World"),
			    GENESIS_TEXT("Open every region together in the level editor, each at its place on the grid, as one "
			                 "unsaved level"),
			    [this] { PreviewWorld(); });
			Button(GENESIS_TEXT("Save"), GENESIS_TEXT("Save the region registry"), [this] { Save(); });
			Toolbar->AddSlot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(GENESIS_TEXT("  Cell size "))];
			Toolbar->AddSlot().AutoWidth()[SNew(SSpinBox<float>)
			        .MinValue(static_cast<float>(WorldCompositionLimits::MinCellSize))
			        .MaxValue(static_cast<float>(WorldCompositionLimits::MaxCellSize))
			        .FractionalDigits(0)
			        .MinDesiredWidth(90.0f)
			        .Value([this] { return static_cast<float>(Session->GetComposition().GetCellSize()); })
			        .OnValueCommitted([this](float Value, ETextCommit)
			{
				FString Error;
				if (Session->SetCellSize(Value, Error))
					Changed(GENESIS_TEXT("Changed the grid cell size; regions keep their cells."));
				else
					SetStatus(Error);
			})];

			ChildSlot[SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[Toolbar] +
			    SVerticalBox::Slot().FillHeight(1)[SNew(SHorizontalBox) +
			        SHorizontalBox::Slot().FillWidth(1)[SAssignNew(Grid, SWorldCompositionGrid)
			                .Session(Session)
			                .OnChanged([this] { Changed(GENESIS_TEXT("Edited the region layout.")); })
			                .OnSelectionChanged([this] { bDetailsStale = true; })
			                .OnError([this](const FString& Error) { SetStatus(Error); })
			                .OnOpenTile([this](const FString& TileId) { OpenRegion(TileId); })
			                .OnNewRegion([this](int32 X, int32 Y) { NewRegion(X, Y); })] +
			        SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(
			            DetailsWidth)[SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(Details, SVerticalBox)]]]] +
			    SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text([this] { return Status; }).AutoWrapText(true)]];
			RebuildDetails();
			SetCanTick(true); // rebuilds the details outside their own callbacks
		}
		bool IsDirty() const
		{
			return Session->IsDirty();
		}
		// Details rebuild here, never inside a callback from one of their own fields.
		void Tick(const FGeometry& Geometry, double Time, float Delta) override
		{
			SCompoundWidget::Tick(Geometry, Time, Delta);
			if (bDetailsStale)
				RebuildDetails();
		}

	private:
		void SetStatus(const FString& Message)
		{
			Status = Message;
		}
		void Changed(const FString& Message)
		{
			SetStatus(Message);
			bDetailsStale = true;
			Grid->Invalidate(EInvalidateWidgetReason::Paint);
		}
		int32 SuggestedCellX() const
		{
			const FWorldCompositionTile* Tile = Session->GetComposition().FindTile(Grid->GetSelectedTile());
			return Tile ? Tile->X + Tile->SizeX : 0;
		}
		int32 SuggestedCellY() const
		{
			const FWorldCompositionTile* Tile = Session->GetComposition().FindTile(Grid->GetSelectedTile());
			return Tile ? Tile->Y : 0;
		}
		void NewRegion(int32 X, int32 Y)
		{
			int32 FreeX = 0, FreeY = 0;
			FString Error, TileId;
			if (!Session->FindFreeCell(X, Y, 1, 1, FreeX, FreeY))
			{
				SetStatus(GENESIS_TEXT("No free cell is near enough for a new region."));
				return;
			}
			if (!Session->CreateRegion(Args.OnCreateRegionMap, FreeX, FreeY, TileId, Error))
			{
				SetStatus(Error);
				return;
			}
			Grid->SetSelectedTile(TileId);
			Changed(FTextLocalization::Format(
			    "Created {}; save to keep its registration.", Session->GetComposition().FindTile(TileId)->Name));
			Args.OnSaved.ExecuteIfBound(); // the new map file is now in the content browser
		}
		void PreviewWorld()
		{
			if (!Args.OnPreviewWorld.IsBound())
			{
				SetStatus(GENESIS_TEXT("This editor host cannot preview worlds."));
				return;
			}
			const FWorldComposition& World = Session->GetComposition();
			TArray<FWorldPreviewRegion> Regions;
			FString Missing;
			for (const FWorldCompositionTile& Tile : World.GetTiles())
			{
				FString Error;
				if (const FAssetData* Map = Session->ResolveMap(Tile.Id, Error))
					Regions.push_back({Tile.Name, *Map, World.GetTileOrigin(Tile)});
				else
					Missing += Missing.IsEmpty() ? Tile.Name : FString::Format(", {}", Tile.Name);
			}
			if (Regions.empty())
			{
				SetStatus(GENESIS_TEXT("No region has a map to preview."));
				return;
			}
			Args.OnPreviewWorld.Execute(Regions);
			SetStatus(Missing.IsEmpty()
			        ? FTextLocalization::Format("Previewing {} regions in the level editor.", Regions.size())
			        : FTextLocalization::Format(
			              "Previewing {} regions; these have no map: {}", Regions.size(), Missing));
		}
		void OpenRegion(const FString& TileId)
		{
			FString Error;
			const FAssetData* Map = Session->ResolveMap(TileId, Error);
			if (!Map)
			{
				SetStatus(Error);
				return;
			}
			if (Args.OnOpenRegion.IsBound())
				Args.OnOpenRegion.Execute(*Map);
		}
		void History(bool bBack)
		{
			if (!(bBack ? Session->Undo() : Session->Redo()))
				return;
			Grid->SetSelectedTile(Grid->GetSelectedTile());
			Changed(bBack ? GENESIS_TEXT("Undid a region change.") : GENESIS_TEXT("Redid a region change."));
		}
		void Save()
		{
			FString Error;
			if (!Session->Save(Error))
			{
				SetStatus(Error);
				return;
			}
			SetStatus(GENESIS_TEXT("Saved the region registry."));
			Args.OnSaved.ExecuteIfBound();
			Args.OnLog.ExecuteIfBound(Status);
		}
		// Applies a details edit; a rejected value is reported and the panel shows the current one again.
		void Edit(const TFunction<bool(FString&)>& Apply)
		{
			FString Error;
			if (Apply(Error))
				Changed(GENESIS_TEXT("Edited the selected region."));
			else
			{
				SetStatus(Error);
				bDetailsStale = true;
			}
		}
		void AddLabel(const FString& Text)
		{
			Details->AddSlot().AutoHeight()[SNew(STextBlock).Text(Text).AutoWrapText(true)];
		}
		void AddCellField(const FString& Label, int32 Value, TOptional<int32> Min, TOptional<int32> Max,
		    TFunction<bool(int32, FString&)> Apply)
		{
			AddLabel(Label);
			Details->AddSlot().AutoHeight()[SNew(SSpinBox<int>)
			        .MinValue(Min)
			        .MaxValue(Max)
			        .Delta(1.0f)
			        .Value(Value)
			        .OnValueCommitted([this, Apply](int NewValue, ETextCommit)
			{ Edit([&Apply, NewValue](FString& Error) { return Apply(NewValue, Error); }); })];
		}
		void RebuildDetails()
		{
			bDetailsStale = false;
			Details->ClearChildren();
			const FWorldComposition& World = Session->GetComposition();
			const FWorldCompositionTile* Found = World.FindTile(Grid->GetSelectedTile());
			if (!Found)
			{
				AddLabel(FTextLocalization::Format(
				    "{} regions on a {}-unit grid.", World.GetTiles().size(), World.GetCellSize()));
				AddLabel(GENESIS_TEXT("Drag world maps from the content browser onto the grid to register them, or "
				                      "right-click a free cell to create a new region."));
				return;
			}
			const FWorldCompositionTile Tile = *Found;
			const FString Id = Tile.Id;
			AddLabel(GENESIS_TEXT("Name"));
			Details->AddSlot().AutoHeight()
			    [SNew(SEditableTextBox).Text(Tile.Name).OnTextCommitted([this, Id](const FString& Text, ETextCommit) {
				Edit([this, &Id, &Text](FString& Error) { return Session->RenameTile(Id, Text, Error); });
			})];
			// Positions are unbounded fields (a slider over the whole grid shows nothing useful); the registry checks
			// them.
			const TOptional<int32> MinCell, MaxCell;
			AddCellField(GENESIS_TEXT("Cell X"), Tile.X, MinCell, MaxCell,
			    [this, Id, Tile](int32 X, FString& Error) { return Session->MoveTile(Id, X, Tile.Y, Error); });
			AddCellField(GENESIS_TEXT("Cell Y"), Tile.Y, MinCell, MaxCell,
			    [this, Id, Tile](int32 Y, FString& Error) { return Session->MoveTile(Id, Tile.X, Y, Error); });
			AddCellField(GENESIS_TEXT("Width (cells)"), Tile.SizeX, 1, WorldCompositionLimits::MaxTileCells,
			    [this, Id, Tile](int32 SizeX, FString& Error)
			{ return Session->ResizeTile(Id, SizeX, Tile.SizeY, Error); });
			AddCellField(GENESIS_TEXT("Depth (cells)"), Tile.SizeY, 1, WorldCompositionLimits::MaxTileCells,
			    [this, Id, Tile](int32 SizeY, FString& Error)
			{ return Session->ResizeTile(Id, Tile.SizeX, SizeY, Error); });
			AddLabel(GENESIS_TEXT("Region server (empty: loaded locally)"));
			Details->AddSlot().AutoHeight()[SNew(SEditableTextBox)
			        .Text(Tile.Host)
			        .HintText(GENESIS_TEXT("host:port"))
			        .OnTextCommitted([this, Id](const FString& Text, ETextCommit)
			{ Edit([this, &Id, &Text](FString& Error) { return Session->SetTileHost(Id, Text, Error); }); })];
			FString MapError;
			const FAssetData* Map = Session->ResolveMap(Id, MapError);
			AddLabel(Map ? FTextLocalization::Format("Map: {}", Map->PackagePath)
			             : FTextLocalization::Format("Map unavailable: {}", MapError));
			const FWorldTileBounds Bounds = World.GetTileBounds(Tile);
			AddLabel(FTextLocalization::Format(
			    "World X {} to {}, Y {} to {}", Bounds.MinX, Bounds.MaxX, Bounds.MinY, Bounds.MaxY));
			TSharedRef<SHorizontalBox> Actions = SNew(SHorizontalBox);
			Actions->AddSlot().AutoWidth()[SNew(SButton)
			        .Text(GENESIS_TEXT("Open Region Map"))
			        .IsEnabled(Map != nullptr && Args.OnOpenRegion.IsBound())
			        .OnClicked([this, Id]
			{
				OpenRegion(Id);
				return FReply::Handled();
			})];
			Actions->AddSlot().AutoWidth()[SNew(SButton)
			        .Text(GENESIS_TEXT("Remove"))
			        .ToolTipText(GENESIS_TEXT("Unregister the region; its map file is kept"))
			        .OnClicked([this]
			{
				Grid->DeleteSelected();
				return FReply::Handled();
			})];
			Details->AddSlot().AutoHeight()[Actions];
		}

		FWorldCompositionEditorArgs Args;
		std::shared_ptr<FWorldCompositionEditSession> Session;
		TSharedPtr<SWorldCompositionGrid> Grid;
		TSharedPtr<SVerticalBox> Details;
		bool bDetailsStale = false;
		FString Status = GENESIS_TEXT("Regions are independently saved world maps placed on the grid.");
	};
} // namespace

TSharedPtr<SWindow> FWorldCompositionEditor::Open(const FWorldCompositionEditorArgs& Args, FString& Error)
{
	TUniquePtr<FWorldCompositionEditSession> Opened =
	    FWorldCompositionEditSession::Open(Args.Registry, Args.Composition, Error);
	if (!Opened)
		return {};
	const std::shared_ptr<FWorldCompositionEditSession> Session(Opened.release());
	const TSharedRef<SWorldCompositionEditor> Panel = SNew(SWorldCompositionEditor, Args, Session);
	const TSharedRef<SWindow> Window = SNew(SWindow)
	                                       .Title(FTextLocalization::Format("World Composition: {}", Args.Name))
	                                       .ClientSize(FVector2(1100, 700))
	                                       .OnRequestClose([Panel]
	{
		return !Panel->IsDirty() ||
		    FServices::GetMessageDialog().Show(EMessageType::Question, EMessageButtons::YesNo,
		        GENESIS_TEXT("Discard unsaved region changes?"),
		        GENESIS_TEXT("World Composition")) == EMessageResult::Yes;
	})[Panel];
	FSlateApplication::Get().AddWindow(Window);
	Error.Empty();
	return Window.ToSharedPtr();
}
