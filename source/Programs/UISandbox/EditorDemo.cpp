// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "EditorDemo.h"

#include "Application/UIApplication.h"
#include "Framework/MultiBox/MenuRegistry.h"
#include "UIWindowRenderer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/SViewport.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSandbox, Log);

namespace
{
	const char* const KindNames[] = { "Brush", "Light", "Player start", "Trigger" };

	const FTextBlockStyle& SmallText() { return FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText"); }
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
// Property sources

namespace
{
	class FEntitySource final : public IPropertySource
	{
	public:
		FEntitySource(FDemoScene& InScene, int32 InId) : Scene(InScene), Id(InId) {}

		FString GetDisplayName() const override
		{
			const FDemoEntity* E = Scene.Find(Id);
			return E ? FString::Format("{} ({})", E->Name, GetEntityKindName(E->Kind)) : FString("(deleted)");
		}

		void GetProperties(TArray<FPropertyDesc>& Out) const override
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

		FPropertyValue GetValue(FName Property) const override
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

		void SetValue(FName Property, const FPropertyValue& Value) override
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

	private:
		FDemoScene& Scene;
		int32 Id;
	};

	// A button showing a command's shortcut; click it and press keys to
	// rebind (Escape cancels, Backspace clears).
	class SChordEditor final : public SCompoundWidget
	{
	public:
		GE_BEGIN_ARGS(SChordEditor)
		GE_END_ARGS()

		void Construct(const FArguments&, const TSharedRef<FUICommandInfo>& InCommand, TFunction<void(const FString&)> InLog)
		{
			Command = InCommand.ToSharedPtr();
			Log = std::move(InLog);
			ChildSlot
			[
				SNew(SButton).HAlign(HAlign_Left).IsFocusable(false)
				.Text([this] { return bCapturing ? FString("Press keys (Esc cancels, Backspace clears)") : (Command->GetInputText().IsEmpty() ? FString("(none)") : Command->GetInputText()); })
				.OnClicked([this]
				{
					bCapturing = true;
					return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
				})
			];
		}

		bool SupportsKeyboardFocus() const override { return true; }

		FReply OnPreviewKeyDown(const FGeometry&, const FKeyEvent& Event) override
		{
			if (!bCapturing)
			{
				return FReply::Unhandled();
			}
			const EKey Key = Event.GetKey();
			if (Key == EKey::LeftShift || Key == EKey::RightShift || Key == EKey::LeftControl || Key == EKey::RightControl || Key == EKey::LeftAlt || Key == EKey::RightAlt)
			{
				return FReply::Handled(); // wait for the real key
			}
			bCapturing = false;
			if (Key == EKey::Escape)
			{
				return FReply::Handled().ClearUserFocus();
			}
			FInputChord Chord;
			if (Key != EKey::BackSpace)
			{
				Chord.Key = Key;
				Chord.Modifiers = (Event.IsControlDown() ? EModifierKey::Control : EModifierKey::None) | (Event.IsShiftDown() ? EModifierKey::Shift : EModifierKey::None)
					| (Event.IsAltDown() ? EModifierKey::Alt : EModifierKey::None);
			}
			const TSharedPtr<FUICommandInfo> Displaced = FInputBindingManager::Get().SetUserChord(ToSharedRef(Command), Chord);
			Log(FString::Format("Bound \"{}\" to {}{}", Command->GetLabel(), Chord.IsValid() ? Chord.ToString() : FString("nothing"),
				Displaced ? FString::Format(" (taken from \"{}\")", Displaced->GetLabel()) : FString()));
			return FReply::Handled().ClearUserFocus();
		}

		void OnFocusLost(const FFocusEvent&) override { bCapturing = false; }

	private:
		TSharedPtr<FUICommandInfo> Command;
		TFunction<void(const FString&)> Log;
		bool bCapturing = false;
	};
}

// ---------------------------------------------------------------------------
// Viewport client

FDemoViewportClient::FDemoViewportClient(FRHIDevice& InDevice, FUIRenderer& InRenderer, FDemoScene& InScene)
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

void FDemoViewportClient::Draw(FRHICommandList& CommandList, FUIRenderTarget& RenderTarget)
{
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

	const FFontInfo Font = FAppStyle::Get().GetFont("NormalFont");
	FFontCache& Fonts = FUIApplication::Get().GetFontCache();
	const FColor Selected = FStyleColor(EStyleColor::Primary).Resolve();
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
			const TSharedRef<const FShapedText> Text = Fonts.ShapeText(E.Name.View(), FFontInfo(Font.Font, 11.0f), Scale);
			Elements.AddText(6, Geo(P + FVector2(2.0f, -16.0f), Text->GetLogicalSize()), Text, bSelected ? Selected : FColor(200, 205, 215));
		}
	}
	if (Font.Font)
	{
		const FString Hint = FString::Format("Zoom {:.0f}%   ·   left: select/move   middle: pan   right: pan (raw mouse)   wheel: zoom", Zoom * 100.0f);
		const TSharedRef<const FShapedText> Text = Fonts.ShapeText(Hint.View(), FFontInfo(Font.Font, 11.0f), Scale);
		Elements.AddText(7, Geo(FVector2(8.0f, View.Y - 20.0f), Text->GetLogicalSize()), Text, FColor(140, 146, 160));
	}
	Renderer.Render(CommandList, RenderTarget.GetTexture(), Elements, FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
}

// ---------------------------------------------------------------------------
// The editor demo

FEditorDemo::FEditorDemo(FHost InHost)
	: Host(std::move(InHost))
{
	Commands = std::make_shared<FUICommandList>();
	TabManager = FTabManager::Create().ToSharedPtr();
	ViewportClient = std::make_shared<FDemoViewportClient>(*Host.Device, *Host.Renderer, Scene);
	Host.WindowRenderer->AddViewportClient(ToSharedRef(StaticCastSharedPtr<FRenderTargetViewportClient>(ViewportClient)));
	Scene.OnChanged.AddLambda([this] { ViewportClient->RequestRedraw(); });
	Scene.OnSelectionChanged.AddLambda([this] { ViewportClient->RequestRedraw(); });
	Scene.OnLog.AddLambda([this](const FString& Line) { Log(Line); });
	RegisterCommands();
	RegisterMenus();
	RegisterTabs();
	Log("Editor framework demo ready. Drag tabs by their titles: onto another stack, a stack's edge, or outside the window.");
}

void FEditorDemo::Log(const FString& Line)
{
	LogLines.push_back(Line);
	StatusText = Line;
	GE_LOG(LogSandbox, Log, "{}", Line);
}

void FEditorDemo::RegisterCommands()
{
	auto Context = MakeShared<FBindingContext>(FName("SandboxEditor"), "UI Sandbox editor demo");
	using T = EUserInterfaceActionType;
	Cmd.SaveLayout = Context->AddCommand("SaveLayout", "Save layout", "Save the dock layout and key bindings now", T::Button, FInputChord(EKey::S, EModifierKey::Control)).ToSharedPtr();
	Cmd.ResetLayout = Context->AddCommand("ResetLayout", "Reset layout", "Go back to the default dock layout").ToSharedPtr();
	Cmd.Quit = Context->AddCommand("Quit", "Exit", "Close the sandbox").ToSharedPtr();
	Cmd.Undo = Context->AddCommand("Undo", "Undo", "Undo the last change", T::Button, FInputChord(EKey::Z, EModifierKey::Control)).ToSharedPtr();
	Cmd.Redo = Context->AddCommand("Redo", "Redo", "Redo the change undone last", T::Button, FInputChord(EKey::Y, EModifierKey::Control)).ToSharedPtr();
	Cmd.Delete = Context->AddCommand("Delete", "Delete", "Delete the selected objects", T::Button, FInputChord(EKey::Delete)).ToSharedPtr();
	Cmd.SelectAll = Context->AddCommand("SelectAll", "Select all", "Select every object", T::Button, FInputChord(EKey::A, EModifierKey::Control)).ToSharedPtr();
	Cmd.ShowGrid = Context->AddCommand("ShowGrid", "Grid", "Show the grid in the viewport", T::ToggleButton, FInputChord(EKey::G)).ToSharedPtr();
	Cmd.Snap = Context->AddCommand("Snap", "Snap", "Snap moves to the 16-unit grid", T::ToggleButton, FInputChord(EKey::N)).ToSharedPtr();
	Cmd.FrameSelection = Context->AddCommand("FrameSelection", "Frame selection", "Zoom the viewport to the selection", T::Button, FInputChord(EKey::F)).ToSharedPtr();
	Cmd.ToggleInspector = Context->AddCommand("ToggleInspector", "Inspector window", "Show or hide the borderless inspector window", T::Button, FInputChord(EKey::F4)).ToSharedPtr();
	Cmd.AddBrush = Context->AddCommand("AddBrush", "Add brush", "Add a brush at the origin", T::Button, FInputChord(EKey::B)).ToSharedPtr();
	Cmd.AddLight = Context->AddCommand("AddLight", "Add light", "Add a light at the origin", T::Button, FInputChord(EKey::L)).ToSharedPtr();
	FInputBindingManager::Get().RegisterContext(Context);

	FUICommandList& List = *Commands;
	List.MapAction(ToSharedRef(Cmd.SaveLayout), [this] { SaveSettings(); Log("Layout and key bindings saved"); });
	List.MapAction(ToSharedRef(Cmd.ResetLayout), [this] { ResetLayout(); });
	List.MapAction(ToSharedRef(Cmd.Quit), [this] { Host.Quit(); });
	List.MapAction(ToSharedRef(Cmd.Undo), [this] { Scene.Undo(); }, [this] { return Scene.CanUndo(); });
	List.MapAction(ToSharedRef(Cmd.Redo), [this] { Scene.Redo(); }, [this] { return Scene.CanRedo(); });
	List.MapAction(ToSharedRef(Cmd.Delete), [this] { Scene.DeleteSelection(); }, [this] { return !Scene.Selection.empty(); });
	List.MapAction(ToSharedRef(Cmd.SelectAll), [this]
	{
		TArray<int32> All;
		for (const FDemoEntity& E : Scene.Entities)
		{
			All.push_back(E.Id);
		}
		Scene.SetSelection(All);
	});
	List.MapAction(ToSharedRef(Cmd.ShowGrid), [this] { ViewportClient->bShowGrid = !ViewportClient->bShowGrid; ViewportClient->RequestRedraw(); }, nullptr, [this] { return ViewportClient->bShowGrid; });
	List.MapAction(ToSharedRef(Cmd.Snap), [this] { ViewportClient->bSnap = !ViewportClient->bSnap; }, nullptr, [this] { return ViewportClient->bSnap; });
	List.MapAction(ToSharedRef(Cmd.FrameSelection), [this] { ViewportClient->FrameSelection(); }, [this] { return !Scene.Selection.empty(); });
	List.MapAction(ToSharedRef(Cmd.ToggleInspector), [this] { Host.ToggleInspector(); });
	List.MapAction(ToSharedRef(Cmd.AddBrush), [this] { AddEntity(0); });
	List.MapAction(ToSharedRef(Cmd.AddLight), [this] { AddEntity(1); });

	if (!Host.SettingsDir.IsEmpty())
	{
		FString Text;
		if (FFileHelper::LoadFileToString(Text, Host.SettingsDir + "Bindings.json"))
		{
			if (const TOptional<FJsonValue> Json = FJsonValue::Parse(Text.View()))
			{
				FInputBindingManager::Get().LoadUserBindings(*Json);
			}
		}
	}
}

void FEditorDemo::AddEntity(int32 Kind)
{
	int32 NextId = 1;
	for (const FDemoEntity& E : Scene.Entities)
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
	Scene.BeginTransaction(FString::Format("Add {}", GetEntityKindName(Kind)));
	Scene.Entities.push_back(E);
	Scene.EndTransaction();
	Scene.SetSelection({ E.Id });
	Scene.Changed();
}

void FEditorDemo::RegisterMenus()
{
	FMenuRegistry& Registry = FMenuRegistry::Get();
	FMenuSection& Bar = Registry.RegisterMenu("SandboxEditor.MainMenu").AddSection("Menus");
	Bar.AddSubMenu("File", "File", "", FName("SandboxEditor.MainMenu.File"));
	Bar.AddSubMenu("Edit", "Edit", "", FName("SandboxEditor.MainMenu.Edit"));
	Bar.AddSubMenu("View", "View", "", FName("SandboxEditor.MainMenu.View"));
	Bar.AddSubMenu("Window", "Window", "", FName("SandboxEditor.MainMenu.Window"));

	FMenuDefinition& File = Registry.RegisterMenu("SandboxEditor.MainMenu.File");
	File.AddSection("Layout", "Layout").AddCommand(Cmd.SaveLayout);
	File.FindOrAddSection("Layout").AddCommand(Cmd.ResetLayout);
	File.AddSection("Exit").AddCommand(Cmd.Quit);

	// Edit: Undo/Redo name what they undo, so the section is rebuilt on open.
	FMenuDefinition& Edit = Registry.RegisterMenu("SandboxEditor.MainMenu.Edit");
	Edit.AddDynamicSection("History", [this](FMenuSection& Section, const FMenuContext&)
	{
		Section.AddCommand(Cmd.Undo, Scene.CanUndo() ? FString::Format("Undo {}", Scene.GetUndoDescription()) : FString("Undo"));
		Section.AddCommand(Cmd.Redo);
	});
	FMenuSection& Selection = Edit.AddSection("Selection");
	Selection.AddCommand(Cmd.SelectAll);
	Selection.AddCommand(Cmd.Delete);
	FMenuSection& Create = Edit.AddSection("Create", "Create");
	Create.AddCommand(Cmd.AddBrush);
	Create.AddCommand(Cmd.AddLight);

	FMenuDefinition& View = Registry.RegisterMenu("SandboxEditor.MainMenu.View");
	FMenuSection& Viewport = View.AddSection("Viewport", "Viewport");
	Viewport.AddCommand(Cmd.ShowGrid);
	Viewport.AddCommand(Cmd.Snap);
	Viewport.AddCommand(Cmd.FrameSelection);
	FMenuSection& Windows = View.AddSection("Windows");
	Windows.AddCommand(Cmd.ToggleInspector);
	Windows.AddSubMenu("AppScale", "Application scale", "Scale the UI on top of the monitor's DPI", [this](FMenuSection& Section, const FMenuContext&)
	{
		for (const float Scale : { 1.0f, 1.25f, 1.5f, 2.0f })
		{
			FUIAction Action;
			Action.Execute = FSimpleDelegate::CreateLambda([this, Scale] { Host.SetAppScale(Scale); });
			Action.GetCheckState = TDelegate<ECheckBoxState()>::CreateLambda([Scale]
			{
				return FMath::IsNearlyEqual(FUIApplication::Get().GetApplicationScale(), Scale, 0.01f) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			});
			Section.AddEntry(FName(FString::Format("Scale{}", Scale * 100.0f)), FString::Format("{:.0f}%", Scale * 100.0f), "", Action, EUserInterfaceActionType::RadioButton);
		}
	});

	FMenuDefinition& Window = Registry.RegisterMenu("SandboxEditor.MainMenu.Window");
	Window.AddDynamicSection("Tabs", [this](FMenuSection& Section, const FMenuContext&) { TabManager->PopulateTabSpawnerMenu(Section); });
	Window.AddSection("Layout").AddCommand(Cmd.ResetLayout);

	FMenuDefinition& Tools = Registry.RegisterMenu("SandboxEditor.ToolBar");
	FMenuSection& History = Tools.AddSection("History");
	History.AddCommand(Cmd.Undo);
	History.AddCommand(Cmd.Redo);
	FMenuSection& Objects = Tools.AddSection("Objects");
	Objects.AddCommand(Cmd.AddBrush);
	Objects.AddCommand(Cmd.AddLight);
	Objects.AddCommand(Cmd.Delete);
	FMenuSection& Toggles = Tools.AddSection("View");
	Toggles.AddCommand(Cmd.ShowGrid);
	Toggles.AddCommand(Cmd.Snap);
	Toggles.AddCommand(Cmd.FrameSelection);
}

TSharedRef<FDockLayout> FEditorDemo::MakeDefaultLayout()
{
	return FDockLayout::New("SandboxEditor", 1)->AddArea(
		FDockLayout::NewPrimaryArea()->SetOrientation(EOrientation::Horizontal)
			->Split(FDockLayout::NewStack()->SetSizeCoefficient(0.18f)->AddTab("Outliner"))
			->Split(FDockLayout::NewSplitter()->SetOrientation(EOrientation::Vertical)->SetSizeCoefficient(0.57f)
				->Split(FDockLayout::NewStack()->SetSizeCoefficient(0.72f)->AddTab("Viewport")->AddTab("Gallery")->SetForegroundTab("Viewport"))
				->Split(FDockLayout::NewStack()->SetSizeCoefficient(0.28f)->AddTab("Log")->AddTab("KeyBindings")->SetForegroundTab("Log")))
			->Split(FDockLayout::NewStack()->SetSizeCoefficient(0.25f)->AddTab("Details")));
}

void FEditorDemo::RegisterTabs()
{
	FTabManager& Tabs = *TabManager;

	Tabs.RegisterTabSpawner("Viewport", [this](const FSpawnTabArgs&)
	{
		return SNew(SDockTab).Label("Viewport").Role(ETabRole::Document)[SNew(SViewport).ViewportClient(StaticCastSharedPtr<IViewportClient>(ViewportClient))];
	}).SetDisplayName("Viewport").SetToolTip("The map, drawn through the RHI by a viewport client");

	Tabs.RegisterTabSpawner("Gallery", [this](const FSpawnTabArgs&)
	{
		FGalleryHost GalleryHost = Host.Gallery;
		GalleryHost.bShowHeader = false;
		return SNew(SDockTab).Label("Widget gallery").Role(ETabRole::Document)[SAssignNew(Gallery, SWidgetGallery, GalleryHost)];
	}).SetDisplayName("Widget gallery").SetToolTip("Every built-in widget (milestone 3)");

	Tabs.RegisterTabSpawner("Outliner", [this](const FSpawnTabArgs&)
	{
		// The list follows the scene and its selection, and selects back.
		struct FState
		{
			TArray<int32> Items;
			TWeakPtr<SListView<int32>> List;
			bool bSyncing = false;
		};
		auto State = std::make_shared<FState>();
		TSharedPtr<SListView<int32>> List;
		const TSharedRef<SDockTab> Tab = SNew(SDockTab).Label("Outliner")
		[
			SAssignNew(List, SListView<int32>)
			.ListItemsSource(&State->Items)
			.SelectionMode(ESelectionMode::Multi)
			.OnGenerateRow([this, State](int32 Id, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
			{
				return SNew(STableRow<int32>, Owner)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(STextBlock).Clipping(EWidgetClipping::ClipToBounds).Text([this, Id] { const FDemoEntity* E = Scene.Find(Id); return E ? E->Name : FString(); })]
					+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))[SNew(STextBlock).TextStyle(&SmallText()).Text([this, Id] { const FDemoEntity* E = Scene.Find(Id); return E ? FString(GetEntityKindName(E->Kind)) : FString(); })]
				];
			})
			.OnSelectionChanged([this, State](int32, ESelectInfo)
			{
				const TSharedPtr<SListView<int32>> Pinned = State->List.lock();
				if (Pinned && !State->bSyncing)
				{
					Scene.SetSelection(Pinned->GetSelectedItems());
				}
			})
		];
		State->List = List;
		const auto Sync = [this, State]()
		{
			const TSharedPtr<SListView<int32>> Pinned = State->List.lock();
			if (!Pinned)
			{
				return;
			}
			State->bSyncing = true;
			TArray<int32> Ids;
			for (const FDemoEntity& E : Scene.Entities)
			{
				Ids.push_back(E.Id);
			}
			if (Ids != State->Items)
			{
				State->Items = Ids;
				Pinned->RequestListRefresh();
			}
			Pinned->ClearSelection();
			for (const int32 Id : Scene.Selection)
			{
				Pinned->SetItemSelection(Id, true);
			}
			State->bSyncing = false;
		};
		Sync();
		const FDelegateHandle Changed = Scene.OnChanged.AddLambda(Sync);
		const FDelegateHandle Selected = Scene.OnSelectionChanged.AddLambda(Sync);
		Tab->SetOnTabClosed(FOnTabClosed::CreateLambda([this, Changed, Selected](const TSharedRef<SDockTab>&)
		{
			Scene.OnChanged.Remove(Changed);
			Scene.OnSelectionChanged.Remove(Selected);
		}));
		return Tab;
	}).SetDisplayName("Outliner").SetToolTip("The objects of the map");

	Tabs.RegisterTabSpawner("Details", [this](const FSpawnTabArgs&)
	{
		const TSharedRef<SDockTab> Tab = SNew(SDockTab).Label("Details")
		[
			SAssignNew(Details, SPropertyGrid)
			.OnBeginTransaction([this](const FString& Description) { Scene.BeginTransaction(Description); })
			.OnEndTransaction([this] { Scene.EndTransaction(); })
		];
		const TWeakPtr<SPropertyGrid> WeakGrid = Details;
		// The kinds of the selected objects decide which properties exist
		// (lights have a brightness): rebuild the rows only when they change,
		// never in the middle of an edit.
		auto Kinds = std::make_shared<TArray<int32>>();
		const auto SelectedKinds = [this]()
		{
			TArray<int32> Result;
			for (const int32 Id : Scene.Selection)
			{
				const FDemoEntity* E = Scene.Find(Id);
				Result.push_back(E ? E->Kind : -1);
			}
			return Result;
		};
		const auto Update = [this, WeakGrid, Kinds, SelectedKinds]()
		{
			const TSharedPtr<SPropertyGrid> Grid = WeakGrid.lock();
			if (!Grid)
			{
				return;
			}
			TArray<TSharedRef<IPropertySource>> Sources;
			for (const int32 Id : Scene.Selection)
			{
				Sources.push_back(TSharedRef<IPropertySource>(std::make_shared<FEntitySource>(Scene, Id)));
			}
			*Kinds = SelectedKinds();
			Grid->SetObjects(Sources);
		};
		Update();
		const FDelegateHandle Selected = Scene.OnSelectionChanged.AddLambda(Update);
		const FDelegateHandle Changed = Scene.OnChanged.AddLambda([WeakGrid, Kinds, SelectedKinds]()
		{
			const TSharedPtr<SPropertyGrid> Grid = WeakGrid.lock();
			if (Grid && SelectedKinds() != *Kinds)
			{
				*Kinds = SelectedKinds();
				// Next frame: the change may come from a widget in the grid,
				// which the rebuild would destroy under its own feet.
				Grid->RegisterActiveTimer(0.0f, [WeakGrid](double, float)
				{
					if (const TSharedPtr<SPropertyGrid> Pinned = WeakGrid.lock())
					{
						Pinned->Refresh();
					}
					return EActiveTimerReturn::Stop;
				});
			}
		});
		Tab->SetOnTabClosed(FOnTabClosed::CreateLambda([this, Changed, Selected](const TSharedRef<SDockTab>&)
		{
			Scene.OnChanged.Remove(Changed);
			Scene.OnSelectionChanged.Remove(Selected);
		}));
		return Tab;
	}).SetDisplayName("Details").SetToolTip("Properties of the selection");

	Tabs.RegisterTabSpawner("Log", [this](const FSpawnTabArgs&)
	{
		auto Items = std::make_shared<TArray<int32>>();
		TSharedPtr<SListView<int32>> List;
		const TSharedRef<SDockTab> Tab = SNew(SDockTab).Label("Log")
		[
			SAssignNew(List, SListView<int32>)
			.ListItemsSource(Items.get())
			.SelectionMode(ESelectionMode::None)
			.OnGenerateRow([this](int32 Index, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
			{
				return SNew(STableRow<int32>, Owner).Padding(FMargin(8.0f, 1.0f))[SNew(STextBlock).TextStyle(&SmallText()).Text(Index < static_cast<int32>(LogLines.size()) ? LogLines[Index] : FString())];
			})
		];
		const TWeakPtr<SListView<int32>> WeakList = List;
		List->RegisterActiveTimer(0.1f, [this, Items, WeakList](double, float)
		{
			const TSharedPtr<SListView<int32>> Pinned = WeakList.lock();
			if (Pinned && Items->size() != LogLines.size())
			{
				while (Items->size() < LogLines.size())
				{
					Items->push_back(static_cast<int32>(Items->size()));
				}
				Pinned->RequestListRefresh();
				Pinned->ScrollToBottom();
			}
			return EActiveTimerReturn::Continue;
		});
		return Tab;
	}).SetDisplayName("Log").SetToolTip("What happened");

	Tabs.RegisterTabSpawner("KeyBindings", [this](const FSpawnTabArgs&)
	{
		auto Items = std::make_shared<TArray<TSharedPtr<FUICommandInfo>>>();
		if (const TSharedPtr<FBindingContext> Context = FInputBindingManager::Get().FindContext("SandboxEditor"))
		{
			for (const TSharedRef<FUICommandInfo>& Command : Context->GetCommands())
			{
				Items->push_back(Command.ToSharedPtr());
			}
		}
		const TSharedRef<SDockTab> Tab = SNew(SDockTab).Label("Key bindings")
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(8.0f, 6.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).TextStyle(&SmallText()).Text("Click a shortcut, then press the new keys. Saved on exit (Ctrl+S saves now).")]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text("Reset all").OnClicked([this] { FInputBindingManager::Get().ResetToDefaults(); Log("Key bindings reset"); return FReply::Handled(); })]
			]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SNew(SListView<TSharedPtr<FUICommandInfo>>)
				.ListItemsSource(Items.get())
				.SelectionMode(ESelectionMode::None)
				.OnGenerateRow([this, Items](TSharedPtr<FUICommandInfo> Command, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
				{
					return SNew(STableRow<TSharedPtr<FUICommandInfo>>, Owner).Padding(FMargin(8.0f, 2.0f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(0.35f).VAlign(VAlign_Center)[SNew(STextBlock).Text(Command->GetLabel())]
						+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)[SNew(STextBlock).Text(Command->GetDescription()).TextStyle(&SmallText())]
						+ SHorizontalBox::Slot().FillWidth(0.25f)[SNew(SChordEditor, ToSharedRef(Command), [this](const FString& Line) { Log(Line); })]
					];
				})
			]
		];
		return Tab;
	}).SetDisplayName("Key bindings").SetToolTip("Rebind the commands' shortcuts");
}

TSharedRef<SWidget> FEditorDemo::MakeContent(const TSharedRef<SWindow>& Window)
{
	MainWindow = Window.ToSharedPtr();
	FString Saved;
	if (!Host.SettingsDir.IsEmpty())
	{
		FFileHelper::LoadFileToString(Saved, Host.SettingsDir + "Layout.json");
	}
	const TSharedRef<FDockLayout> Layout = FTabManager::LoadLayout(Saved, MakeDefaultLayout());
	DockArea = TabManager->RestoreFrom(Layout, Window.ToSharedPtr()).ToSharedPtr();

	const FMenuContext Context(ToSharedRef(Commands));
	FMenuRegistry& Registry = FMenuRegistry::Get();
	return SNew(SCommandBindingScope).CommandList(Commands)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Header")).Padding(FMargin(10.0f, 2.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 12.0f, 0.0f))[SNew(STextBlock).Text("Genesis").TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("BoldText"))]
				+ SHorizontalBox::Slot().AutoWidth()[Registry.GenerateMenuBar("SandboxEditor.MainMenu", Context)]
				+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right).VAlign(VAlign_Center)
				[
					SNew(STextBlock).TextStyle(&SmallText()).Text([this]
					{
						const FSandboxFrameInfo Frame = Host.Gallery.GetFrameInfo();
						return FString::Format("{} · DPI {:.0f}% · app scale {:.0f}% · {:.0f} fps", Frame.BackendName, Frame.DpiScale * 100.0f, Frame.AppScale * 100.0f, Frame.FramesPerSecond);
					})
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight()[Registry.GenerateToolBar("SandboxEditor.ToolBar", Context)]
		+ SVerticalBox::Slot().FillHeight(1.0f)[SAssignNew(DockHost, SBox)[ToSharedRef(DockArea)]]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Header")).Padding(FMargin(10.0f, 3.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(STextBlock).TextStyle(&SmallText()).Text([this] { return StatusText; })]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).TextStyle(&SmallText()).Text([this]
				{
					const TSharedPtr<SDockTab> Active = TabManager->GetActiveTab();
					return FString::Format("{} selected   ·   active tab: {}", Scene.Selection.size(), Active ? Active->GetLabel() : FString("none"));
				})]
			]
		]
	];
}

void FEditorDemo::ResetLayout()
{
	const TSharedPtr<SWindow> Window = MainWindow.lock();
	if (!Window || !DockHost)
	{
		return;
	}
	TabManager->CloseAllAreas();
	Details.reset();
	Gallery.reset();
	DockArea = TabManager->RestoreFrom(MakeDefaultLayout(), Window).ToSharedPtr();
	DockHost->SetContent(ToSharedRef(DockArea));
	Log("Layout reset");
}

void FEditorDemo::SaveSettings()
{
	if (Host.SettingsDir.IsEmpty())
	{
		return;
	}
	const FString Layout = TabManager->PersistLayout()->ToJson().ToString();
	FFileHelper::SaveArrayToFile(TArrayView<const uint8>(reinterpret_cast<const uint8*>(*Layout), static_cast<SIZE_T>(Layout.Len())), Host.SettingsDir + "Layout.json");
	const FString Bindings = FInputBindingManager::Get().SaveUserBindings().ToString();
	FFileHelper::SaveArrayToFile(TArrayView<const uint8>(reinterpret_cast<const uint8*>(*Bindings), static_cast<SIZE_T>(Bindings.Len())), Host.SettingsDir + "Bindings.json");
}
