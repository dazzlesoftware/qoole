// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "DemoScene.h"

#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"

namespace
{
	const char* const KindNames[] = { "Brush", "Light", "Player start", "Trigger" };
}

const char* GetEntityKindName(int32 Kind)
{
	return Kind >= 0 && Kind < 4 ? KindNames[Kind] : "?";
}

// ---------------------------------------------------------------------------
// Scene and undo

FDemoScene::FDemoScene()
{
	const auto Add = [this](const char* Name, int32 Kind, FVector2 Position, FVector2 Size, FColor Color)
	{
		FDemoEntity E;
		E.Id = static_cast<int32>(Entities.size()) + 1;
		E.Name = Name;
		E.Kind = Kind;
		E.Position = Position;
		E.Size = Size;
		E.Color = Color;
		Entities.push_back(E);
	};
	Add("wall_north", 0, FVector2(0.0f, 0.0f), FVector2(512.0f, 32.0f), FColor(110, 120, 140));
	Add("wall_south", 0, FVector2(0.0f, 352.0f), FVector2(512.0f, 32.0f), FColor(110, 120, 140));
	Add("wall_west", 0, FVector2(0.0f, 32.0f), FVector2(32.0f, 320.0f), FColor(110, 120, 140));
	Add("wall_east", 0, FVector2(480.0f, 32.0f), FVector2(32.0f, 320.0f), FColor(110, 120, 140));
	Add("pillar", 0, FVector2(224.0f, 160.0f), FVector2(64.0f, 64.0f), FColor(150, 128, 104));
	Add("light_warm", 1, FVector2(96.0f, 96.0f), FVector2(32.0f, 32.0f), FColor(255, 196, 120));
	Add("light_cool", 1, FVector2(384.0f, 256.0f), FVector2(32.0f, 32.0f), FColor(130, 180, 255));
	Add("info_player_start", 2, FVector2(96.0f, 272.0f), FVector2(32.0f, 32.0f), FColor(112, 214, 140));
	Add("trigger_secret", 3, FVector2(352.0f, 64.0f), FVector2(96.0f, 64.0f), FColor(232, 98, 120));
	Entities[6].Brightness = 2.5f;
}

FDemoEntity* FDemoScene::Find(int32 Id)
{
	for (FDemoEntity& E : Entities)
	{
		if (E.Id == Id)
		{
			return &E;
		}
	}
	return nullptr;
}

const FDemoEntity* FDemoScene::Find(int32 Id) const
{
	return const_cast<FDemoScene*>(this)->Find(Id);
}

void FDemoScene::SetSelection(TArray<int32> Ids)
{
	std::erase_if(Ids, [this](int32 Id) { return !Find(Id); });
	if (Ids != Selection)
	{
		Selection = std::move(Ids);
		OnSelectionChanged.Broadcast();
	}
}

void FDemoScene::BeginTransaction(const FString& Description)
{
	if (Depth++ == 0)
	{
		Pending = FUndoStep{ Description, Entities, {} };
	}
}

void FDemoScene::EndTransaction()
{
	if (Depth == 0 || --Depth > 0 || !Pending)
	{
		return;
	}
	Pending->After = Entities;
	if (Pending->Before != Pending->After)
	{
		OnLog.Broadcast(FString::Format("Undoable: {}", Pending->Description));
		UndoStack.push_back(std::move(*Pending));
		RedoStack.clear();
	}
	Pending.reset();
}

void FDemoScene::Undo()
{
	if (UndoStack.empty())
	{
		return;
	}
	FUndoStep Step = std::move(UndoStack.back());
	UndoStack.pop_back();
	Entities = Step.Before;
	OnLog.Broadcast(FString::Format("Undo: {}", Step.Description));
	RedoStack.push_back(std::move(Step));
	SetSelection(Selection);
	Changed();
}

void FDemoScene::Redo()
{
	if (RedoStack.empty())
	{
		return;
	}
	FUndoStep Step = std::move(RedoStack.back());
	RedoStack.pop_back();
	Entities = Step.After;
	OnLog.Broadcast(FString::Format("Redo: {}", Step.Description));
	UndoStack.push_back(std::move(Step));
	SetSelection(Selection);
	Changed();
}

int32 FDemoScene::AddEntity(int32 Kind)
{
	int32 NextId = 1;
	for (const FDemoEntity& E : Entities)
	{
		NextId = FMath::Max(NextId, E.Id + 1);
	}
	FDemoEntity E;
	E.Id = NextId;
	E.Kind = Kind;
	E.Name = FString::Format("{}_{:02}", Kind == 1 ? "light" : "brush", NextId);
	E.Position = FVector2(32.0f * static_cast<float>(NextId % 8), 32.0f * static_cast<float>(NextId % 5));
	E.Size = Kind == 1 ? FVector2(32.0f, 32.0f) : FVector2(96.0f, 64.0f);
	E.Color = Kind == 1 ? FColor(255, 230, 160) : FColor(130, 140, 160);
	BeginTransaction(FString::Format("Add {}", GetEntityKindName(Kind)));
	Entities.push_back(E);
	EndTransaction();
	SetSelection({ E.Id });
	Changed();
	return E.Id;
}

void FDemoScene::DeleteSelection()
{
	if (Selection.empty())
	{
		return;
	}
	BeginTransaction(FString::Format("Delete {} object(s)", Selection.size()));
	std::erase_if(Entities, [this](const FDemoEntity& E) { return IsSelected(E.Id); });
	EndTransaction();
	SetSelection({});
	Changed();
}

// ---------------------------------------------------------------------------
// Property source

FString FDemoEntitySource::GetDisplayName() const
{
	const FDemoEntity* E = Scene.Find(Id);
	return E ? FString::Format("{} ({})", E->Name, GetEntityKindName(E->Kind)) : FString("(deleted)");
}

void FDemoEntitySource::GetProperties(TArray<FPropertyDesc>& Out) const
{
	const FDemoEntity* E = Scene.Find(Id);
	Out.push_back(FPropertyDesc("Name", EPropertyType::String, "Name", "General"));
	Out.push_back(FPropertyDesc("Kind", EPropertyType::Enum, "Kind", "General").SetOptions({ KindNames[0], KindNames[1], KindNames[2], KindNames[3] }));
	Out.push_back(FPropertyDesc("Visible", EPropertyType::Bool, "Visible", "General").SetToolTip("Hidden objects are not drawn"));
	Out.push_back(FPropertyDesc("Position", EPropertyType::Vector2, "Position", "Transform"));
	Out.push_back(FPropertyDesc("Size", EPropertyType::Vector2, "Size", "Transform").SetRange(8.0f, 4096.0f));
	Out.push_back(FPropertyDesc("Color", EPropertyType::Color, "Color", "Appearance"));
	if (E && E->Kind == 1)
	{
		Out.push_back(FPropertyDesc("Brightness", EPropertyType::Float, "Brightness", "Appearance").SetRange(0.0f, 10.0f).SetToolTip("Lights only"));
	}
	Out.push_back(FPropertyDesc("Id", EPropertyType::Int, "Id", "Info").SetReadOnly());
}

FPropertyValue FDemoEntitySource::GetValue(FName Property) const
{
	const FDemoEntity* E = Scene.Find(Id);
	if (!E)
	{
		return FPropertyValue(false);
	}
	const std::string_view P = Property.View();
	if (P == "Name") return E->Name;
	if (P == "Kind") return E->Kind;
	if (P == "Visible") return E->bVisible;
	if (P == "Position") return E->Position;
	if (P == "Size") return E->Size;
	if (P == "Color") return E->Color;
	if (P == "Brightness") return E->Brightness;
	if (P == "Id") return E->Id;
	return FPropertyValue(false);
}

void FDemoEntitySource::SetValue(FName Property, const FPropertyValue& Value)
{
	FDemoEntity* E = Scene.Find(Id);
	if (!E)
	{
		return;
	}
	const std::string_view P = Property.View();
	if (P == "Name") E->Name = std::get<FString>(Value);
	else if (P == "Kind") E->Kind = std::get<int32>(Value);
	else if (P == "Visible") E->bVisible = std::get<bool>(Value);
	else if (P == "Position") E->Position = std::get<FVector2>(Value);
	else if (P == "Size") E->Size = FVector2::Max(std::get<FVector2>(Value), FVector2(8.0f, 8.0f));
	else if (P == "Color") E->Color = std::get<FColor>(Value);
	else if (P == "Brightness") E->Brightness = std::get<float>(Value);
	Scene.Changed();
}

// ---------------------------------------------------------------------------
// Viewport client

FDemoViewportClient::FDemoViewportClient(FRHIDevice& InDevice, FSlateRHIRenderingPolicy& InRenderer, FDemoScene& InScene)
	: FRenderTargetViewportClient(InDevice, InRenderer), Scene(InScene)
{
}

FVector2 FDemoViewportClient::ToWorld(const FGeometry& Geometry, FVector2 Absolute) const
{
	return (Geometry.AbsoluteToLocal(Absolute) - Pan) / Zoom;
}

int32 FDemoViewportClient::PickEntity(FVector2 World) const
{
	for (auto It = Scene.Entities.rbegin(); It != Scene.Entities.rend(); ++It)
	{
		if (It->bVisible && FRect::FromPosSize(It->Position, It->Size).Contains(World))
		{
			return It->Id;
		}
	}
	return INDEX_NONE;
}

void FDemoViewportClient::FrameSelection()
{
	const TSharedPtr<SViewport> ViewportWidget = GetViewport();
	if (!ViewportWidget || Scene.Selection.empty())
	{
		return;
	}
	FRect Bounds(1e9f, 1e9f, -1e9f, -1e9f);
	for (const int32 Id : Scene.Selection)
	{
		if (const FDemoEntity* E = Scene.Find(Id))
		{
			Bounds = FRect(FVector2::Min(Bounds.GetTopLeft(), E->Position), FVector2::Max(Bounds.GetBottomRight(), E->Position + E->Size));
		}
	}
	const FVector2 View = ViewportWidget->GetCachedGeometry().Size;
	Zoom = FMath::Clamp(FMath::Min(View.X / (Bounds.Width() + 128.0f), View.Y / (Bounds.Height() + 128.0f)), 0.1f, 8.0f);
	Pan = View * 0.5f - Bounds.GetCenter() * Zoom;
	RequestRedraw();
}

FReply FDemoViewportClient::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	const TSharedPtr<SViewport> ViewportWidget = GetViewport();
	if (!ViewportWidget)
	{
		return FReply::Unhandled();
	}
	LastAbsolute = Event.GetAbsolutePosition();
	const TSharedRef<SWidget> Self = ToSharedRef(StaticCastSharedPtr<SWidget>(ViewportWidget));
	switch (Event.GetEffectingButton())
	{
	case EMouseButton::Left:
	{
		const FVector2 World = ToWorld(Geometry, Event.GetAbsolutePosition());
		const int32 Hit = PickEntity(World);
		TArray<int32> Selection = Scene.Selection;
		if (Hit == INDEX_NONE)
		{
			if (!Event.IsShiftDown())
			{
				Scene.SetSelection({});
			}
			return FReply::Handled();
		}
		if (Event.IsShiftDown())
		{
			if (ArrayUtil::Contains(Selection, Hit)) ArrayUtil::Remove(Selection, Hit);
			else Selection.push_back(Hit);
			Scene.SetSelection(Selection);
			return FReply::Handled();
		}
		if (!Scene.IsSelected(Hit))
		{
			Scene.SetSelection({ Hit });
		}
		Drag = EDrag::Move;
		DragStartWorld = World;
		DragStartPositions.clear();
		for (const int32 Id : Scene.Selection)
		{
			DragStartPositions[Id] = Scene.Find(Id)->Position;
		}
		Scene.BeginTransaction(FString::Format("Move {} object(s)", Scene.Selection.size()));
		return FReply::Handled().CaptureMouse(Self);
	}
	case EMouseButton::Middle:
		Drag = EDrag::Pan;
		return FReply::Handled().CaptureMouse(Self);
	case EMouseButton::Right:
		Drag = EDrag::Look;
		return FReply::Handled().UseHighPrecisionMouseMovement(Self);
	default:
		return FReply::Unhandled();
	}
}

FReply FDemoViewportClient::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	DpiScale = Geometry.Scale;
	if (Drag == EDrag::Move)
	{
		const FVector2 Delta = ToWorld(Geometry, Event.GetAbsolutePosition()) - DragStartWorld;
		for (const auto& [Id, Start] : DragStartPositions)
		{
			if (FDemoEntity* E = Scene.Find(Id))
			{
				E->Position = FVector2(Snap(Start.X + Delta.X), Snap(Start.Y + Delta.Y));
			}
		}
		Scene.Changed();
		return FReply::Handled();
	}
	if (Drag == EDrag::Pan)
	{
		Pan += (Event.GetAbsolutePosition() - LastAbsolute) / FMath::Max(Geometry.Scale, 0.01f);
		LastAbsolute = Event.GetAbsolutePosition();
		RequestRedraw();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply FDemoViewportClient::OnRawMouseMove(FVector2 Delta)
{
	if (Drag != EDrag::Look)
	{
		return FReply::Unhandled();
	}
	Pan += Delta / FMath::Max(DpiScale, 0.01f);
	RequestRedraw();
	return FReply::Handled();
}

FReply FDemoViewportClient::OnMouseButtonUp(const FGeometry&, const FPointerEvent&)
{
	if (Drag == EDrag::None)
	{
		return FReply::Unhandled();
	}
	return FReply::Handled().ReleaseMouseCapture(); // OnMouseCaptureLost ends the drag
}

void FDemoViewportClient::OnMouseCaptureLost()
{
	if (Drag == EDrag::Move)
	{
		Scene.EndTransaction();
	}
	Drag = EDrag::None;
}

FReply FDemoViewportClient::OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
	// Zoom around the cursor.
	const FVector2 Local = Geometry.AbsoluteToLocal(Event.GetAbsolutePosition());
	const FVector2 World = (Local - Pan) / Zoom;
	Zoom = FMath::Clamp(Zoom * std::pow(1.15f, Event.GetWheelDelta()), 0.1f, 8.0f);
	Pan = Local - World * Zoom;
	RequestRedraw();
	return FReply::Handled();
}

FCursorReply FDemoViewportClient::OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const
{
	if (Drag == EDrag::Move || Drag == EDrag::Pan)
	{
		return FCursorReply::Cursor(Drag == EDrag::Move ? EMouseCursor::CardinalCross : EMouseCursor::GrabHand);
	}
	return PickEntity(ToWorld(Geometry, Event.GetAbsolutePosition())) != INDEX_NONE ? FCursorReply::Cursor(EMouseCursor::Hand) : FCursorReply::Cursor(EMouseCursor::Crosshairs);
}

void FDemoViewportClient::Draw(FRHICommandList& CommandList, FSlateRenderTargetRHI& RenderTarget)
{
	++DrawCount;
	const TSharedPtr<SViewport> ViewportWidget = GetViewport();
	const float Scale = ViewportWidget ? FMath::Max(ViewportWidget->GetCachedGeometry().Scale, 0.01f) : 1.0f;
	DpiScale = Scale;
	const FIntPoint PixelSize = RenderTarget.GetSize();
	const FVector2 View = PixelSize.ToVector2() / Scale;
	Elements.Reset();
	Elements.SetViewportSize(PixelSize);
	const auto Geo = [Scale](FVector2 Position, FVector2 Extent) { return FPaintGeometry::Make(Position, Extent, Scale); };

	Elements.AddSolidBox(0, Geo(FVector2::Zero(), View), FColor(20, 22, 27));
	if (bShowGrid)
	{
		// Minor lines every 16 units (when not too dense), major every 128.
		for (const float Step : { 16.0f, 128.0f })
		{
			const float Pixels = Step * Zoom;
			if (Pixels < 6.0f)
			{
				continue;
			}
			const FColor Color = Step > 16.0f ? FColor(52, 58, 70) : FColor(32, 36, 44);
			const float StartX = FMath::Fmod(Pan.X, Pixels) - Pixels;
			for (float X = StartX; X < View.X; X += Pixels)
			{
				Elements.AddSolidBox(1, Geo(FVector2(X, 0.0f), FVector2(1.0f / Scale, View.Y)), Color);
			}
			const float StartY = FMath::Fmod(Pan.Y, Pixels) - Pixels;
			for (float Y = StartY; Y < View.Y; Y += Pixels)
			{
				Elements.AddSolidBox(1, Geo(FVector2(0.0f, Y), FVector2(View.X, 1.0f / Scale)), Color);
			}
		}
		// The origin.
		Elements.AddSolidBox(2, Geo(FVector2(Pan.X, 0.0f), FVector2(1.0f, View.Y)), FColor(90, 60, 60));
		Elements.AddSolidBox(2, Geo(FVector2(0.0f, Pan.Y), FVector2(View.X, 1.0f)), FColor(60, 90, 60));
	}

	const FSlateFontInfo Font = FAppStyle::Get().GetFont("NormalFont");
	FSlateFontCache& Fonts = FSlateApplication::Get().GetFontCache();
	const FColor Selected = FSlateColor(EStyleColor::Primary).Resolve();
	for (const FDemoEntity& E : Scene.Entities)
	{
		if (!E.bVisible)
		{
			continue;
		}
		const FVector2 P = E.Position * Zoom + Pan;
		const FVector2 S = E.Size * Zoom;
		const bool bSelected = Scene.IsSelected(E.Id);
		switch (E.Kind)
		{
		case 1: // light: a glow sized by brightness
		{
			const FVector2 C = P + S * 0.5f;
			const float Glow = S.X * (1.0f + E.Brightness);
			Elements.AddRoundedBox(3, Geo(C - FVector2(Glow * 0.5f), FVector2(Glow)), E.Color.WithAlpha(40), Glow * 0.5f);
			Elements.AddRoundedBox(4, Geo(P, S), E.Color, S.X * 0.5f, bSelected ? Selected : FColor(0, 0, 0, 120), bSelected ? 2.0f : 1.0f);
			break;
		}
		case 2: // player start: an arrow
			Elements.AddRoundedBox(4, Geo(P, S), E.Color.WithAlpha(60), 4.0f, bSelected ? Selected : E.Color, bSelected ? 2.0f : 1.5f);
			Elements.AddLines(5, Geo(P, S), { FVector2(S.X * 0.25f, S.Y * 0.25f), FVector2(S.X * 0.8f, S.Y * 0.5f), FVector2(S.X * 0.25f, S.Y * 0.75f) }, E.Color, 2.0f, true);
			break;
		case 3: // trigger: translucent with an outline
			Elements.AddRoundedBox(3, Geo(P, S), E.Color.WithAlpha(50), 2.0f, bSelected ? Selected : E.Color, bSelected ? 2.0f : 1.0f);
			break;
		default: // brush
			Elements.AddRoundedBox(3, Geo(P, S), E.Color, 2.0f, bSelected ? Selected : FColor(0, 0, 0, 160), bSelected ? 2.0f : 1.0f);
			break;
		}
		if (Font.Font && Zoom >= 0.5f)
		{
			const TSharedRef<const FShapedGlyphSequence> Text = Fonts.ShapeText(E.Name.View(), FSlateFontInfo(Font.Font, 11.0f), Scale);
			Elements.AddText(6, Geo(P + FVector2(2.0f, -16.0f), Text->GetLogicalSize()), Text, bSelected ? Selected : FColor(200, 205, 215));
		}
	}
	if (Font.Font)
	{
		const FString Hint = FString::Format("Zoom {:.0f}%   ·   left: select/move   middle: pan   right: pan (raw mouse)   wheel: zoom", Zoom * 100.0f);
		const TSharedRef<const FShapedGlyphSequence> Text = Fonts.ShapeText(Hint.View(), FSlateFontInfo(Font.Font, 11.0f), Scale);
		Elements.AddText(7, Geo(FVector2(8.0f, View.Y - 20.0f), Text->GetLogicalSize()), Text, FColor(140, 146, 160));
	}
	Renderer.Render(CommandList, RenderTarget.GetTexture(), Elements, FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
}
