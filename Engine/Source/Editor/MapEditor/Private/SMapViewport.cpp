// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "SMapViewport.h"
#include "Containers/ContainerAliases.h"
#include "Templates/SharedPointer.h"
#include "Internationalization/TextLocalization.h"

#include "MapEditorIcons.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MenuRegistry.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SViewport.h"
#include "Widgets/Text/STextBlock.h"

#include <cmath>

using namespace MapEditorPrivate;

// ---------------------------------------------------------------------------
// Layouts

namespace
{
	struct FLayoutInfo
	{
		const char* Name;
		const char* Label;
		int32 NumPanes;
	};

	constexpr FLayoutInfo LayoutInfos[] = {
	    {"OnePane", "One pane", 1},
	    {"TwoPanesHoriz", "Two panes side by side", 2},
	    {"TwoPanesVert", "Two panes, one above the other", 2},
	    {"ThreePanesLeft", "Three panes, the large one on the left", 3},
	    {"ThreePanesRight", "Three panes, the large one on the right", 3},
	    {"ThreePanesTop", "Three panes, the large one on top", 3},
	    {"ThreePanesBottom", "Three panes, the large one at the bottom", 3},
	    {"FourPanes2x2", "Four panes in a 2x2 grid", 4},
	    {"FourPanesLeft", "Four panes, the large one on the left", 4},
	    {"FourPanesRight", "Four panes, the large one on the right", 4},
	    {"FourPanesTop", "Four panes, the large one on top", 4},
	    {"FourPanesBottom", "Four panes, the large one at the bottom", 4},
	};
	static_assert(std::size(LayoutInfos) == static_cast<SIZE_T>(EMapViewportLayout::Count));

	const FLayoutInfo& Info(EMapViewportLayout Layout)
	{
		return LayoutInfos[FMath::Clamp(
		    static_cast<int32>(Layout), 0, static_cast<int32>(EMapViewportLayout::Count) - 1)];
	}
} // namespace

const char* ToString(EMapViewportLayout Layout)
{
	return Info(Layout).Name;
}

TOptional<EMapViewportLayout> MapViewportLayoutFromString(const FString& Name)
{
	for (int32 Index = 0; Index < static_cast<int32>(EMapViewportLayout::Count); ++Index)
	{
		if (Name.Equals(LayoutInfos[Index].Name, ESearchCase::IgnoreCase))
		{
			return static_cast<EMapViewportLayout>(Index);
		}
	}
	return {};
}

int32 GetNumPanes(EMapViewportLayout Layout)
{
	return Info(Layout).NumPanes;
}

TArray<FRect> GetPaneRects(EMapViewportLayout Layout)
{
	constexpr float T = 1.0f / 3.0f;
	constexpr float Big = 0.62f; // the large pane's share
	switch (Layout)
	{
	case EMapViewportLayout::OnePane:
		return {FRect(0, 0, 1, 1)};
	case EMapViewportLayout::TwoPanesHoriz:
		return {FRect(0, 0, 0.5f, 1), FRect(0.5f, 0, 1, 1)};
	case EMapViewportLayout::TwoPanesVert:
		return {FRect(0, 0, 1, 0.5f), FRect(0, 0.5f, 1, 1)};
	case EMapViewportLayout::ThreePanesLeft:
		return {FRect(0, 0, 0.5f, 1), FRect(0.5f, 0, 1, 0.5f), FRect(0.5f, 0.5f, 1, 1)};
	case EMapViewportLayout::ThreePanesRight:
		return {FRect(0.5f, 0, 1, 1), FRect(0, 0, 0.5f, 0.5f), FRect(0, 0.5f, 0.5f, 1)};
	case EMapViewportLayout::ThreePanesTop:
		return {FRect(0, 0, 1, 0.5f), FRect(0, 0.5f, 0.5f, 1), FRect(0.5f, 0.5f, 1, 1)};
	case EMapViewportLayout::ThreePanesBottom:
		return {FRect(0, 0.5f, 1, 1), FRect(0, 0, 0.5f, 0.5f), FRect(0.5f, 0, 1, 0.5f)};
	case EMapViewportLayout::FourPanes2x2:
		return {FRect(0, 0, 0.5f, 0.5f), FRect(0.5f, 0, 1, 0.5f), FRect(0, 0.5f, 0.5f, 1), FRect(0.5f, 0.5f, 1, 1)};
	case EMapViewportLayout::FourPanesLeft:
		return {FRect(0, 0, Big, 1), FRect(Big, 0, 1, T), FRect(Big, T, 1, 2 * T), FRect(Big, 2 * T, 1, 1)};
	case EMapViewportLayout::FourPanesRight:
		return {
		    FRect(1 - Big, 0, 1, 1), FRect(0, 0, 1 - Big, T), FRect(0, T, 1 - Big, 2 * T), FRect(0, 2 * T, 1 - Big, 1)};
	case EMapViewportLayout::FourPanesTop:
		return {FRect(0, 0, 1, Big), FRect(0, Big, T, 1), FRect(T, Big, 2 * T, 1), FRect(2 * T, Big, 1, 1)};
	case EMapViewportLayout::FourPanesBottom:
		return {
		    FRect(0, 1 - Big, 1, 1), FRect(0, 0, T, 1 - Big), FRect(T, 0, 2 * T, 1 - Big), FRect(2 * T, 0, 1, 1 - Big)};
	case EMapViewportLayout::Count:
		break;
	}
	return {};
}

// ---------------------------------------------------------------------------
// Icons for the toolbar (the drawings are in MapEditorIcons.h)

namespace
{
	EIcon ToolIcon(EMapTool Tool)
	{
		switch (Tool)
		{
		case EMapTool::Select:
			return EIcon::Select;
		case EMapTool::Rotate:
			return EIcon::Rotate;
		case EMapTool::Scale:
			return EIcon::Scale;
		case EMapTool::Clip:
			return EIcon::Clip;
		case EMapTool::Vertex:
			return EIcon::Vertex;
		case EMapTool::Edge:
			return EIcon::Edge;
		case EMapTool::Face:
			return EIcon::Face;
		}
		return EIcon::None;
	}

	EIcon ViewModeIcon(EMapViewMode Mode)
	{
		switch (Mode)
		{
		case EMapViewMode::Wireframe:
			return EIcon::Wireframe;
		case EMapViewMode::Unlit:
			return EIcon::Unlit;
		case EMapViewMode::Lit:
			return EIcon::Lit;
		}
		return EIcon::None;
	}
	// A picture of a layout: its panes as outlined boxes.
	class SLayoutPicture final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SLayoutPicture)
		SLATE_END_ARGS()

		void Construct(const FArguments&, EMapViewportLayout InLayout)
		{
			Layout = InLayout;
		}

	protected:
		FVector2 ComputeDesiredSize(float) const override
		{
			return FVector2(30.0f, 24.0f);
		}

		int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FRect&, FSlateWindowElementList& Out,
		    int32 LayerId, const FWidgetStyle& Style) const override
		{
			const FColor Color = Style.GetForegroundColor();
			const FColor Fill(Color.R, Color.G, Color.B, static_cast<uint8>(Color.A / 5));
			constexpr float Gap = 1.5f;
			for (const FRect& Rect : GetPaneRects(Layout))
			{
				const FVector2 Min(Rect.Left * Geometry.Size.X + Gap, Rect.Top * Geometry.Size.Y + Gap);
				const FVector2 Max(Rect.Right * Geometry.Size.X - Gap, Rect.Bottom * Geometry.Size.Y - Gap);
				Out.AddRoundedBox(LayerId, Geometry.ToPaintGeometry(FRect(Min, Max)), Fill, 2.0f, Color, 1.0f);
			}
			return LayerId;
		}

	private:
		EMapViewportLayout Layout = EMapViewportLayout::OnePane;
	};
} // namespace

// ---------------------------------------------------------------------------
// Styles

namespace
{
	struct FViewportToolBarStyles
	{
		FSlateBrush Background;
		FButtonStyle IconButton;  // flat, square
		FButtonStyle ComboButton; // a dark pill
		FSlateBrush Checked;
		FSlateBrush CheckedHovered;
		FTextBlockStyle Label;

		FViewportToolBarStyles()
		{
			Background = FSlateBrush::Solid(FSlateColor(EStyleColor::Header));
			IconButton = FAppStyle::Get().GetWidgetStyle<FButtonStyle>("FlatButton");
			IconButton.NormalPadding = FMargin(3.0f, 3.0f);
			IconButton.PressedPadding = FMargin(3.0f, 4.0f, 3.0f, 2.0f);
			ComboButton = IconButton;
			ComboButton.Normal = FSlateBrush::Rounded(FSlateColor(EStyleColor::Recessed), 4.0f);
			ComboButton.Hovered = FSlateBrush::Rounded(FSlateColor(EStyleColor::Hover), 4.0f);
			ComboButton.Pressed = FSlateBrush::Rounded(FSlateColor(EStyleColor::Pressed), 4.0f);
			ComboButton.NormalPadding = FMargin(5.0f, 3.0f);
			ComboButton.PressedPadding = FMargin(5.0f, 4.0f, 5.0f, 2.0f);
			Checked = FSlateBrush::Rounded(
			    FSlateColor(EStyleColor::Primary, 90), 4.0f, FSlateColor(EStyleColor::Primary), 1.0f);
			CheckedHovered = FSlateBrush::Rounded(
			    FSlateColor(EStyleColor::Primary, 130), 4.0f, FSlateColor(EStyleColor::Primary), 1.0f);
			Label = FTextBlockStyle(FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText")).SetFontSize(11.0f);
		}
	};

	const FViewportToolBarStyles& Styles()
	{
		static const FViewportToolBarStyles Instance;
		return Instance;
	}

	TSharedRef<SWidget> StaticCastSharedRefToWidget(const TSharedRef<SWidget>& Widget)
	{
		return Widget;
	}

	// An icon button that shows when it is on (a tool, a snapping toggle).
	class SToolBarIconButton final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SToolBarIconButton)
		SLATE_ATTRIBUTE(EIcon, Icon, EIcon::None)
		SLATE_ATTRIBUTE(bool, IsChecked, false)
		SLATE_EVENT(FSimpleDelegate, OnClicked)
		SLATE_DEFAULT_SLOT(Content) // instead of the icon
		SLATE_END_ARGS()

		void Construct(const FArguments& Args)
		{
			IsChecked.Assign(Args._IsChecked);
			const FSimpleDelegate OnClicked = Args._OnClicked;
			ChildSlot[SNew(SButton)
			        .ButtonStyle(&Styles().IconButton)
			        .IsFocusable(false)
			        .OnClicked(
			            [OnClicked]
			{
				OnClicked.ExecuteIfBound();
				return FReply::Handled();
			})[Args._Content.Widget ? ToSharedRef(Args._Content.Widget)
			                        : StaticCastSharedRefToWidget(SNew(SMapViewportIcon).Icon(Args._Icon))]];
		}

	protected:
		int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect,
		    FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& InWidgetStyle) const override
		{
			if (IsChecked.Get())
			{
				(IsHovered() ? Styles().CheckedHovered : Styles().Checked)
				    .Paint(Out, LayerId, Geometry.ToPaintGeometry(), InWidgetStyle.ColorAndOpacityTint);
			}
			return SCompoundWidget::OnPaint(Args, Geometry, CullingRect, Out, LayerId + 1, InWidgetStyle);
		}

	private:
		TWidgetAttribute<bool, EInvalidateWidgetReason::Paint> IsChecked{*this, false};
	};

	// "Label" and a spin box, for a menu.
	template <typename T>
	TSharedRef<SWidget> MakeSpinRow(const FString& Label, TFunction<T()> Get, TFunction<void(T)> Set, T Min, T Max,
	    int32 Digits, TFunction<bool()> Enabled = nullptr)
	{
		return SNew(SHorizontalBox).IsEnabled([Enabled] { return !Enabled || Enabled(); }) +
		    SHorizontalBox::Slot()
		        .FillWidth(1.0f)
		        .VAlign(VAlign_Center)
		        .Padding(FMargin(0.0f, 0.0f, 12.0f, 0.0f))[SNew(STextBlock).Text(Label)] +
		    SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(90.0f)[SNew(SSpinBox<T>)
		            .Value([Get] { return TOptional<T>(Get()); })
		            .MinValue(Min)
		            .MaxValue(Max)
		            .FractionalDigits(Digits)
		            .OnValueChanged([Set](T Value) { Set(Value); })]];
	}

	TSharedRef<SWidget> MakeNote(const FString& Text)
	{
		return SNew(SBox).MaxDesiredWidth(260.0f)[SNew(STextBlock)
		        .AutoWrapText(true)
		        .TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText"))
		        .Text(Text)];
	}

	SMapViewport* PaneOf(const FMenuContext& Context)
	{
		return static_cast<SMapViewport*>(Context.Payload.get());
	}

	FUIAction CheckAction(
	    TFunction<void()> Execute, TFunction<bool()> IsChecked, TFunction<bool()> CanExecute = nullptr)
	{
		FUIAction Action;
		Action.Execute = FSimpleDelegate::CreateLambda(std::move(Execute));
		Action.GetCheckState = TDelegate<ECheckBoxState()>::CreateLambda(
		    [IsChecked] { return IsChecked() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; });
		if (CanExecute)
		{
			Action.CanExecute = TDelegate<bool()>::CreateLambda(std::move(CanExecute));
		}
		return Action;
	}

	const char* ToolDescription(EMapTool Tool)
	{
		switch (Tool)
		{
		case EMapTool::Select:
			return "Select, and move the selection by dragging it";
		case EMapTool::Rotate:
			return "Turn the selection about its center by dragging";
		case EMapTool::Scale:
			return "Stretch the selection by dragging (Shift: evenly)";
		case EMapTool::Clip:
			return "Draw a line to clip the selected brushes";
		case EMapTool::Vertex:
			return "Drag the corners of the selected brush";
		case EMapTool::Edge:
			return "Drag the edges of the selected brush";
		case EMapTool::Face:
			return "Drag the faces of the selected brush";
		}
		return "";
	}
} // namespace

// ---------------------------------------------------------------------------
// Commands and menus

FMapViewportCommands::FMapViewportCommands()
{
	TSharedRef<FBindingContext> Context = MakeShared<FBindingContext>(FName("MapViewport"), "Map viewports");
	using T = EUserInterfaceActionType;
	using M = EModifierKey;
	struct FCommandBuilder
	{
		TSharedRef<FBindingContext> Context;
		TSharedPtr<FUICommandInfo> operator()(const FString& Name, const FString& Label, const FString& Tip,
		    T Type = T::Button, FInputChord Chord = FInputChord()) const
		{
			return Context->AddCommand(FName(Name), GENESIS_TEXT(Label.View()), GENESIS_TEXT(Tip.View()), Type, Chord)
			    .ToSharedPtr();
		}
	};
	const FCommandBuilder Add{Context};
	struct FTypeCommand
	{
		EMapViewType Type;
		const char* Tip;
		FInputChord Chord;
	};
	const FTypeCommand Types[] = {
	    {EMapViewType::Perspective, "Look at the map in perspective", FInputChord(EKey::G, M::Alt)},
	    {EMapViewType::Top, "Look down at the map", FInputChord(EKey::J, M::Alt)},
	    {EMapViewType::Bottom, "Look up at the map", FInputChord(EKey::J, M::Alt | M::Shift)},
	    {EMapViewType::Left, "Look at the map from the left", FInputChord(EKey::K, M::Alt)},
	    {EMapViewType::Right, "Look at the map from the right", FInputChord(EKey::K, M::Alt | M::Shift)},
	    {EMapViewType::Front, "Look at the map from the front", FInputChord(EKey::H, M::Alt)},
	    {EMapViewType::Back, "Look at the map from the back", FInputChord(EKey::H, M::Alt | M::Shift)},
	};
	for (const FTypeCommand& Command : Types)
	{
		ViewportTypes[static_cast<int32>(Command.Type)] =
		    Add(ToString(Command.Type), ToString(Command.Type), Command.Tip, T::RadioButton, Command.Chord);
	}
	LitMode = Add("LitMode", "Lit", "Solid faces, shaded by the way they face (perspective views)", T::RadioButton,
	    FInputChord(EKey::Four, M::Alt));
	UnlitMode = Add("UnlitMode", "Unlit", "Solid faces in their flat colors (perspective views)", T::RadioButton,
	    FInputChord(EKey::Three, M::Alt));
	WireframeMode =
	    Add("WireframeMode", "Wireframe", "The edges of the brushes", T::RadioButton, FInputChord(EKey::Two, M::Alt));
	FocusViewportToSelection =
	    Add("FocusViewportToSelection", "Frame Selected", "Fit this viewport to the selection (or the whole map)");
	ResetCamera = Add("ResetCamera", "Reset Camera", "Put this viewport's camera back at the origin");
	ToggleMaximize =
	    Add("ToggleMaximize", "Maximize Viewport", "Show this viewport alone, or all of them again", T::ToggleButton);
	for (int32 Index = 0; Index < static_cast<int32>(EMapViewportLayout::Count); ++Index)
	{
		ViewportConfigs[Index] =
		    Add(FString::Format("ViewportConfig_{}", LayoutInfos[Index].Name), LayoutInfos[Index].Label,
		        FTextLocalization::Format("Arrange the viewports: {}", LayoutInfos[Index].Label), T::RadioButton);
	}
	FInputBindingManager::Get().RegisterContext(Context);
}

const FMapViewportCommands& FMapViewportCommands::Get()
{
	static const FMapViewportCommands Instance;
	return Instance;
}

void FMapViewportCommands::Register()
{
	static bool bRegistered = false;
	const FMapViewportCommands& C = Get();
	if (bRegistered)
	{
		return;
	}
	bRegistered = true;
	FMenuRegistry& Registry = FMenuRegistry::Get();

	// Viewport type, camera and field of view.
	FMenuDefinition& TypeMenu = Registry.RegisterMenu("MapViewport.ViewportType");
	TypeMenu.AddSection("Perspective", "Perspective").AddCommand(C.ViewportType(EMapViewType::Perspective));
	FMenuSection& Ortho = TypeMenu.AddSection("Orthographic", "Orthographic");
	for (const EMapViewType Type : {EMapViewType::Top, EMapViewType::Bottom, EMapViewType::Left, EMapViewType::Right,
	         EMapViewType::Front, EMapViewType::Back})
	{
		Ortho.AddCommand(C.ViewportType(Type));
	}
	FMenuSection& Camera = TypeMenu.AddSection("Camera", "Camera");
	Camera.AddCommand(C.FocusViewportToSelection);
	Camera.AddCommand(C.ResetCamera);
	TypeMenu.AddDynamicSection("View", [](FMenuSection& Section, const FMenuContext& Context)
	{
		SMapViewport* Pane = PaneOf(Context);
		if (!Pane)
		{
			return;
		}
		Section.Label = "View";
		const TSharedPtr<FMapViewportClient> Client = Pane->GetClientPtr();
		Section.AddWidget("FieldOfView", [Client](const FMenuContext&)
		{
			return MakeSpinRow<float>("Field of View", [Client] { return Client->GetViewFOV(); },
			    [Client](float Value) { Client->SetViewFOV(Value); }, FMapViewportClient::MinViewFOV,
			    FMapViewportClient::MaxViewFOV, 1, [Client] { return Client->IsPerspective(); });
		});
	});

	FMenuDefinition& SpeedMenu = Registry.RegisterMenu("MapViewport.CameraSpeed");
	SpeedMenu.AddDynamicSection("Speed", [](FMenuSection& Section, const FMenuContext& Context)
	{
		SMapViewport* Pane = PaneOf(Context);
		if (!Pane)
		{
			return;
		}
		Section.Label = "Camera Speed";
		const TSharedPtr<FMapViewportClient> Client = Pane->GetClientPtr();
		Section.AddWidget("Speed", [Client](const FMenuContext&)
		{
			return SNew(SVerticalBox) +
			    SVerticalBox::Slot().AutoHeight()[MakeSpinRow<float>("Speed", [Client]
			{ return Client->GetCameraSpeed(); }, [Client](float Value) { Client->SetCameraSpeed(Value); },
			        FMapViewportClient::MinCameraSpeed, FMapViewportClient::MaxCameraSpeed, 2)] +
			    SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 6.0f, 0.0f, 0.0f))[MakeNote(
			        "Hold the right mouse button and turn the wheel to change the speed while flying.")];
		});
	});

	FMenuDefinition& ModeMenu = Registry.RegisterMenu("MapViewport.ViewMode");
	FMenuSection& Modes = ModeMenu.AddSection("ViewMode", "View Mode");
	Modes.AddCommand(C.LitMode);
	Modes.AddCommand(C.UnlitMode);
	Modes.AddCommand(C.WireframeMode);
	ModeMenu.AddDynamicSection("Note", [](FMenuSection& Section, const FMenuContext& Context)
	{
		SMapViewport* Pane = PaneOf(Context);
		if (Pane && Pane->GetClient().IsOrtho())
		{
			Section.AddWidget(
			    "OrthoNote", [](const FMenuContext&) { return MakeNote("Orthographic views draw wireframes."); });
		}
	});

	// Show flags.
	FMenuDefinition& ShowMenu = Registry.RegisterMenu("MapViewport.Show");
	ShowMenu.AddDynamicSection("Flags", [](FMenuSection& Section, const FMenuContext& Context)
	{
		SMapViewport* Pane = PaneOf(Context);
		if (!Pane)
		{
			return;
		}
		const TSharedPtr<FMapViewportClient> Client = Pane->GetClientPtr();
		FUIAction Defaults;
		Defaults.Execute = FSimpleDelegate::CreateLambda([Client] { Client->SetShowFlags(FMapShowFlags()); });
		Section.AddEntry("UseDefaults", "Use Defaults", "Show everything again", Defaults);
		Section.AddSeparator();
		struct FFlag
		{
			const char* Name;
			const char* Label;
			const char* Tip;
			bool FMapShowFlags::* Member;
		};
		static const FFlag Flags[] = {
		    {"Grid", "Grid", "The grid (View > Grid hides it in every view)", &FMapShowFlags::bGrid},
		    {"Entities", "Entities", "Lights, player starts and the other entities (hidden ones cannot be picked)",
		        &FMapShowFlags::bEntities},
		    {"ViewportStats", "Viewport Stats", "The zoom, the tool and the group being edited, at the top left",
		        &FMapShowFlags::bViewportStats},
		    {"Axes", "Axis Indicator", "Which way X, Y and Z point, and the scale of 2D views", &FMapShowFlags::bAxes},
		};
		for (const FFlag& Flag : Flags)
		{
			bool FMapShowFlags::* const Member = Flag.Member;
			Section.AddEntry(Flag.Name, Flag.Label, Flag.Tip,
			    CheckAction(
			        [Client, Member]
			{
				FMapShowFlags ShowFlags = Client->GetShowFlags();
				ShowFlags.*Member = !(ShowFlags.*Member);
				Client->SetShowFlags(ShowFlags);
			}, [Client, Member] { return Client->GetShowFlags().*Member; }),
			    EUserInterfaceActionType::Check);
		}
	});

	// Viewport settings (the gear).
	FMenuDefinition& OptionsMenu = Registry.RegisterMenu("MapViewport.Options");
	OptionsMenu.AddDynamicSection("Controls", [](FMenuSection& Section, const FMenuContext& Context)
	{
		SMapViewport* Pane = PaneOf(Context);
		if (!Pane)
		{
			return;
		}
		Section.Label = "Controls";
		FMapViewSettings* Settings = &Pane->GetSettings();
		Section.AddWidget("MouseSensitivity", [Settings](const FMenuContext&)
		{
			return MakeSpinRow<float>("Mouse Sensitivity", [Settings] { return Settings->MouseSensitivity; },
			    [Settings](float Value)
			{
				Settings->MouseSensitivity = Value;
				Settings->Changed();
			}, 0.1f, 4.0f, 2);
		});
		Section.AddEntry("InvertMiddleMousePan", "Invert Middle Mouse Pan",
		    "Middle drag moves the camera instead of the map",
		    CheckAction(
		        [Settings]
		{
			Settings->bInvertMiddleMousePan = !Settings->bInvertMiddleMousePan;
			Settings->Changed();
		}, [Settings] { return Settings->bInvertMiddleMousePan; }),
		    EUserInterfaceActionType::Check);
	});
	OptionsMenu.AddDynamicSection("Grid", [](FMenuSection& Section, const FMenuContext& Context)
	{
		SMapViewport* Pane = PaneOf(Context);
		if (!Pane)
		{
			return;
		}
		Section.Label = "All Viewports";
		FMapViewSettings* Settings = &Pane->GetSettings();
		Section.AddEntry("ShowGridEverywhere", "Grid", "Show the grid in every view",
		    CheckAction(
		        [Settings]
		{
			Settings->bShowGrid = !Settings->bShowGrid;
			Settings->Changed();
		}, [Settings] { return Settings->bShowGrid; }),
		    EUserInterfaceActionType::Check);
	});

	// Layouts: pictures of the arrangements, as in Unreal.
	FMenuDefinition& LayoutMenu = Registry.RegisterMenu("MapViewport.Layout");
	const TPair<FName, const char*> Groups[] = {
	    {"OnePane", "One Pane"}, {"TwoPanes", "Two Panes"}, {"ThreePanes", "Three Panes"}, {"FourPanes", "Four Panes"}};
	for (int32 Group = 0; Group < 4; ++Group)
	{
		const int32 NumPanes = Group + 1;
		LayoutMenu.AddDynamicSection(Groups[Group].first,
		    [NumPanes, Label = FString(Groups[Group].second)](FMenuSection& Section, const FMenuContext& Context)
		{
			SMapViewport* Pane = PaneOf(Context);
			if (!Pane)
			{
				return;
			}
			Section.Label = Label;
			const TSharedRef<FUICommandList> List = Pane->GetCommandList();
			Section.AddWidget("Pictures", [NumPanes, List](const FMenuContext&)
			{
				const TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
				for (int32 Index = 0; Index < static_cast<int32>(EMapViewportLayout::Count); ++Index)
				{
					const EMapViewportLayout Layout = static_cast<EMapViewportLayout>(Index);
					if (GetNumPanes(Layout) != NumPanes)
					{
						continue;
					}
					const TSharedPtr<FUICommandInfo> Command = FMapViewportCommands::Get().ViewportConfig(Layout);
					Row->AddSlot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 4.0f, 0.0f))[SNew(SToolBarIconButton)
					        .IsChecked(
					            [List, Command] { return List->GetCheckState(*Command) == ECheckBoxState::Checked; })
					        .OnClicked([List, Command]
					{
						FSlateApplication::Get().DismissAllMenus();
						List->ExecuteAction(*Command);
					}).ToolTipText(Command->GetDescription())[SNew(SLayoutPicture, Layout)]];
				}
				return Row;
			});
		});
	}
	LayoutMenu.AddSection("Maximize").AddCommand(C.ToggleMaximize);

	// Tools: the editor's commands when it gave them, else the settings.
	FMenuDefinition& ToolMenu = Registry.RegisterMenu("MapViewport.Tools");
	const TPair<FName, TArray<EMapTool>> ToolGroups[] = {
	    {"TransformTools", {EMapTool::Select, EMapTool::Rotate, EMapTool::Scale}},
	    {"BrushTools", {EMapTool::Clip, EMapTool::Vertex, EMapTool::Edge, EMapTool::Face}},
	};
	for (const TPair<FName, TArray<EMapTool>>& Group : ToolGroups)
	{
		const FName& GroupName = Group.first;
		const TArray<EMapTool>& Tools = Group.second;
		const bool bTransform = GroupName == FName("TransformTools");
		ToolMenu.AddDynamicSection(GroupName, [Tools, bTransform](FMenuSection& Section, const FMenuContext& Context)
		{
			SMapViewport* Pane = PaneOf(Context);
			if (!Pane)
			{
				return;
			}
			Section.Label = bTransform ? "Transform Tools" : "Brush Tools";
			const SMapViewportLayout& Layout = Pane->GetLayout();
			FMapViewSettings* Settings = &Pane->GetSettings();
			for (const EMapTool Tool : Tools)
			{
				const SIZE_T Index = static_cast<SIZE_T>(Tool);
				if (Index < Layout.GetToolCommands().size() && Layout.GetToolCommands()[Index] &&
				    Layout.GetToolCommandList())
				{
					Section.AddCommand(Layout.GetToolCommands()[Index], FString(), Layout.GetToolCommandList());
					continue;
				}
				Section.AddEntry(FName(ToString(Tool)), ToString(Tool), ToolDescription(Tool),
				    CheckAction(
				        [Settings, Tool]
				{
					Settings->Tool = Tool;
					Settings->Changed();
				}, [Settings, Tool] { return Settings->Tool == Tool; }),
				    EUserInterfaceActionType::RadioButton);
			}
		});
	}

	FMenuDefinition& GridMenu = Registry.RegisterMenu("MapViewport.GridSize");
	GridMenu.AddDynamicSection("Sizes", [](FMenuSection& Section, const FMenuContext& Context)
	{
		SMapViewport* Pane = PaneOf(Context);
		if (!Pane)
		{
			return;
		}
		Section.Label = "Grid Size";
		FMapViewSettings* Settings = &Pane->GetSettings();
		for (float Size = FMapViewSettings::MinGridSize; Size <= FMapViewSettings::MaxGridSize; Size *= 2.0f)
		{
			Section.AddEntry(FName(FString::Format("Grid{:.0f}", Size)), FString::Format("{:.0f}", Size),
			    "Map units between grid lines",
			    CheckAction(
			        [Settings, Size]
			{
				Settings->GridSize = Size;
				Settings->Changed();
			}, [Settings, Size] { return Settings->GridSize == Size; }),
			    EUserInterfaceActionType::RadioButton);
		}
	});

	FMenuDefinition& RotationMenu = Registry.RegisterMenu("MapViewport.RotationSnap");
	RotationMenu.AddDynamicSection("Steps", [](FMenuSection& Section, const FMenuContext& Context)
	{
		SMapViewport* Pane = PaneOf(Context);
		if (!Pane)
		{
			return;
		}
		Section.Label = "Rotation Increment";
		FMapViewSettings* Settings = &Pane->GetSettings();
		for (const float Step : {1.0f, 5.0f, 10.0f, 15.0f, 22.5f, 30.0f, 45.0f, 90.0f})
		{
			Section.AddEntry(FName(FString::Format("Step{}", Step)), FString::Format("{}°", Step),
			    "Degrees the Rotate tool turns by",
			    CheckAction(
			        [Settings, Step]
			{
				Settings->RotateSnapDegrees = Step;
				Settings->bSnapRotation = true;
				Settings->Changed();
			}, [Settings, Step] { return Settings->RotateSnapDegrees == Step; }),
			    EUserInterfaceActionType::RadioButton);
		}
	});
}

// ---------------------------------------------------------------------------
// The toolbar

namespace
{
	class SMapViewportToolBar final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SMapViewportToolBar)
		SLATE_END_ARGS()

		void Construct(const FArguments&, SMapViewport* InPane)
		{
			Pane = InPane;
			Client = InPane->GetClientPtr();
			FMapViewSettings* Settings = &InPane->GetSettings();
			SMapViewportLayout* Layout = &InPane->GetLayout();
			const FMapViewportCommands& C = FMapViewportCommands::Get();
			const TSharedPtr<FMapViewportClient> View = Client;
			const TSharedRef<FUICommandList> List = InPane->GetCommandList();

			const TSharedRef<SHorizontalBox> Left = SNew(SHorizontalBox);
			const TSharedRef<SWidget> ToolMenu = MakeCombo("MapViewport.Tools",
			    [Settings] { return ToolIcon(Settings->Tool); }, nullptr, "Tools: what a left drag does in 2D views");
			// Narrowest panes: the tool buttons stay, their menu goes.
			AddHiding(ToolMenu, 3, [] { return true; });
			Left->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 4.0f, 0.0f))[ToolMenu];
			for (const EMapTool Tool : {EMapTool::Select, EMapTool::Rotate, EMapTool::Scale, EMapTool::Clip})
			{
				const TSharedRef<SWidget> Button = SNew(SToolBarIconButton)
				                                       .Icon(ToolIcon(Tool))
				                                       .IsChecked([Settings, Tool] { return Settings->Tool == Tool; })
				                                       .OnClicked([this, Settings, Tool]
				{ SetTool(*Settings, Tool); }).ToolTipText([this, Tool] { return ToolToolTip(Tool); });
				if (Tool == EMapTool::Clip)
				{
					AddHiding(Button, 5, [] { return true; }); // also in the tool menu
				}
				Left->AddSlot().AutoWidth().VAlign(VAlign_Center)[Button];
			}
			Left->AddSlot().AutoWidth().Padding(FMargin(6.0f, 0.0f));
			Left->AddSlot().AutoWidth().VAlign(VAlign_Center)[SNew(SToolBarIconButton)
			        .Icon(EIcon::GridSnap)
			        .IsChecked([Settings] { return Settings->bSnapToGrid; })
			        .OnClicked([Settings]
			{
				Settings->bSnapToGrid = !Settings->bSnapToGrid;
				Settings->Changed();
			}).ToolTipText(GENESIS_TEXT("Snap moves and tools to the grid (hold Alt while dragging to move freely)"))];
			const TSharedRef<SWidget> GridSize = MakeCombo("MapViewport.GridSize", nullptr,
			    [Settings] { return FString::Format("{:.0f}", Settings->GridSize); }, "Grid size, in map units");
			AddHiding(GridSize, 5, [] { return true; });
			Left->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(1.0f, 0.0f, 6.0f, 0.0f))[GridSize];
			// Rotation snapping goes before the grid's: it only matters to the Rotate tool.
			const TSharedRef<SWidget> RotationToggle = SNew(SToolBarIconButton)
			                                               .Icon(EIcon::RotationSnap)
			                                               .IsChecked([Settings] { return Settings->bSnapRotation; })
			                                               .OnClicked([Settings]
			{
				Settings->bSnapRotation = !Settings->bSnapRotation;
				Settings->Changed();
			}).ToolTipText(GENESIS_TEXT("Turn the Rotate tool in steps"));
			const TSharedRef<SWidget> RotationStep = MakeCombo("MapViewport.RotationSnap", nullptr,
			    [Settings] { return FString::Format("{}°", Settings->RotateSnapDegrees); }, "Rotation increment");
			AddHiding(RotationToggle, 4, [] { return true; });
			AddHiding(RotationStep, 4, [] { return true; });
			Left->AddSlot().AutoWidth().VAlign(VAlign_Center)[RotationToggle];
			Left->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(1.0f, 0.0f, 0.0f, 0.0f))[RotationStep];

			const TSharedRef<SHorizontalBox> Right = SNew(SHorizontalBox);
			struct FAddRightBuilder
			{
				const TSharedRef<SHorizontalBox>& Right;
				void operator()(const TSharedRef<SWidget>& Widget, float Gap = 3.0f) const
				{
					Right->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(Gap, 0.0f, 0.0f, 0.0f))[Widget];
				}
			};
			const FAddRightBuilder AddRight{Right};
			AddRight(MakeCombo("MapViewport.ViewportType", [View] {
				return View->IsPerspective() ? EIcon::Perspective : EIcon::Ortho;
			}, [View] { return FString(ToString(View->GetViewportType())); }, "Viewport type and camera", true, 2));
			const TSharedRef<SWidget> Speed = MakeCombo("MapViewport.CameraSpeed", [] { return EIcon::CameraSpeed; },
			    [View] { return FString::Format("{:.2g}", View->GetCameraSpeed()); },
			    "Camera speed (right drag and W/A/S/D to fly)", true, 1);
			// The wheel sets the speed while flying, so the combo can go in the narrowest panes.
			AddHiding(Speed, 3, [View] { return View->IsPerspective(); });
			AddRight(Speed);
			AddRight(MakeCombo("MapViewport.ViewMode", [View] { return ViewModeIcon(View->GetViewMode()); },
			             [View] { return FString(ToString(View->GetViewMode())); }, "View mode", true, 1),
			    8.0f);
			AddRight(MakeCombo(
			    "MapViewport.Show", [] { return EIcon::Show; }, nullptr, "Show flags: what the viewport draws"));
			AddRight(MakeCombo("MapViewport.Options", [] { return EIcon::Settings; }, nullptr, "Viewport settings"));
			AddRight(MakeCombo("MapViewport.Layout", [] { return EIcon::Options; }, nullptr, "Viewport layouts", false),
			    8.0f);
			const int32 PaneIndex = InPane->GetPaneIndex();
			const TSharedPtr<FUICommandInfo> Maximize = C.ToggleMaximize;
			const TSharedRef<SWidget> MaximizeButton = SNew(SToolBarIconButton)
			                                               .Icon([Layout, PaneIndex] {
				return Layout->IsMaximized(PaneIndex) ? EIcon::Restore : EIcon::Maximize;
			}).OnClicked([List, Maximize] {
				List->ExecuteAction(*Maximize);
			}).ToolTipText([Layout, PaneIndex] {
				return Layout->IsMaximized(PaneIndex) ? FString("Restore all viewports")
				                                      : FString("Maximize this viewport");
			});
			MaximizeButton->SetVisibility([Layout]
			{ return GetNumPanes(Layout->GetLayout()) > 1 ? EVisibility::Visible : EVisibility::Collapsed; });
			AddRight(MaximizeButton, 1.0f);

			LeftBox = Left.ToSharedPtr();
			RightBox = Right.ToSharedPtr();
			ChildSlot[SNew(SBorder)
			        .BorderImage(&Styles().Background)
			        .Padding(FMargin(ToolBarPadding, 2.0f))
			        .Clipping(
			            EWidgetClipping::ClipToBounds)[SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth()[Left] +
			            SHorizontalBox::Slot()
			                .FillWidth(1.0f)
			                .Padding(FMargin(ToolBarGap, 0.0f, 0.0f, 0.0f))
			                .HAlign(HAlign_Right)[Right]]];
			SetCanTick(true);
		}

		// Labels go when the pane is too narrow for everything, as in
		// Unreal: the view mode's and the camera speed's first, then the
		// viewport type's; then the tool menu and the camera speed, the
		// rotation snapping, and last the grid size and the Clip button.
		void Tick(const FGeometry& Geometry, double, float) override
		{
			float Width = LeftBox->GetDesiredSize().X + RightBox->GetDesiredSize().X;
			for (FHidingLabel& Label : Labels)
			{
				if (Label.Widget->GetVisibility() != EVisibility::Collapsed)
				{
					Label.Width = Label.Widget->GetDesiredSize().X + LabelGap;
				}
				else if (Label.Level <= CompactLevel && Label.Applies())
				{
					Width += Label.Width; // what it takes when shown
				}
			}
			const float Available = Geometry.GetLocalSize().X - 2.0f * ToolBarPadding - ToolBarGap;
			int32 Level = 0;
			for (; Level < MaxCompactLevel; ++Level)
			{
				float Saved = 0.0f;
				for (const FHidingLabel& Label : Labels)
				{
					if (Label.Level <= Level && Label.Applies())
					{
						Saved += Label.Width;
					}
				}
				if (Width - Saved <= Available)
				{
					break;
				}
			}
			CompactLevel = Level;
		}

	private:
		static constexpr float ToolBarPadding = 4.0f;
		static constexpr float ToolBarGap = 8.0f;
		static constexpr float LabelGap = 5.0f;
		static constexpr int32 MaxCompactLevel = 5;

		struct FHidingLabel
		{
			TSharedPtr<SWidget> Widget;
			int32 Level = 1;
			float Width = 0.0f;        // with its gap, measured while it shows
			TFunction<bool()> Applies; // whether its combo is shown at all
		};

		// A widget shown while Applies says so and the pane is wide enough for Level.
		void AddHiding(const TSharedRef<SWidget>& Widget, int32 Level, TFunction<bool()> Applies)
		{
			Widget->SetVisibility([this, Level, Applies]
			{ return Applies() && CompactLevel < Level ? EVisibility::Visible : EVisibility::Collapsed; });
			FHidingLabel& Hiding = Labels.emplace_back();
			Hiding.Widget = Widget.ToSharedPtr();
			Hiding.Level = Level;
			Hiding.Applies = std::move(Applies);
		}

		TSharedRef<SWidget> MakeCombo(FName Menu, TFunction<EIcon()> Icon, TFunction<FString()> Label,
		    const FString& Tip, bool bArrow = true, int32 HideLevel = 0)
		{
			const TSharedRef<SHorizontalBox> Content = SNew(SHorizontalBox);
			if (Icon)
			{
				Content->AddSlot().AutoWidth().VAlign(
				    VAlign_Center)[SNew(SMapViewportIcon).Icon([Icon] { return Icon(); })];
			}
			if (Label)
			{
				const bool bIcon = static_cast<bool>(Icon);
				const TSharedRef<SWidget> Text =
				    SNew(STextBlock).TextStyle(&Styles().Label).Text([Label] { return Label(); });
				if (bIcon && HideLevel > 0)
				{
					// Icon and label: the label hides in narrow panes.
					AddHiding(Text, HideLevel, [View = Client, bSpeed = Menu == FName("MapViewport.CameraSpeed")]
					{ return !bSpeed || View->IsPerspective(); });
				}
				Content->AddSlot()
				    .AutoWidth()
				    .VAlign(VAlign_Center)
				    .Padding(FMargin(bIcon ? LabelGap : 0.0f, 0.0f, 0.0f, 0.0f))[Text];
			}
			SMapViewport* Owner = Pane;
			return SNew(SComboButton)
			    .ButtonStyle(&Styles().ComboButton)
			    .HasDownArrow(bArrow)
			    .IsFocusable(false)
			    .Placement(EMenuPlacement::BelowAnchor)
			    .ToolTipText(Tip)
			    .OnGetMenuContent([Owner, Menu]() -> TSharedRef<SWidget>
			{
				FMenuContext Context(Owner->GetCommandList());
				Context.Payload =
				    std::static_pointer_cast<void>(StaticCastSharedPtr<SMapViewport>(Owner->shared_from_this()));
				return FMenuRegistry::Get().GenerateMenuWidget(Menu, Context);
			}).ButtonContent()[Content];
		}

		void SetTool(FMapViewSettings& Settings, EMapTool Tool) const
		{
			const SMapViewportLayout& Layout = Pane->GetLayout();
			const SIZE_T Index = static_cast<SIZE_T>(Tool);
			if (Index < Layout.GetToolCommands().size() && Layout.GetToolCommands()[Index] &&
			    Layout.GetToolCommandList())
			{
				Layout.GetToolCommandList()->ExecuteAction(*Layout.GetToolCommands()[Index]);
				return;
			}
			Settings.Tool = Tool;
			Settings.Changed();
		}

		FString ToolToolTip(EMapTool Tool) const
		{
			const SMapViewportLayout& Layout = Pane->GetLayout();
			const SIZE_T Index = static_cast<SIZE_T>(Tool);
			FString Shortcut;
			if (Index < Layout.GetToolCommands().size() && Layout.GetToolCommands()[Index])
			{
				Shortcut = Layout.GetToolCommands()[Index]->GetInputText();
			}
			return FString::Format("{}{}\n{}", ToString(Tool),
			    Shortcut.IsEmpty() ? FString() : FString::Format(" ({})", Shortcut), ToolDescription(Tool));
		}

		SMapViewport* Pane = nullptr;
		TSharedPtr<FMapViewportClient> Client;
		TSharedPtr<SHorizontalBox> LeftBox;
		TSharedPtr<SHorizontalBox> RightBox;
		TArray<FHidingLabel> Labels;
		int32 CompactLevel = 0;
	};
} // namespace

// ---------------------------------------------------------------------------
// SMapViewport

void SMapViewport::Construct(
    const FArguments&, const TSharedRef<FMapViewportClient>& InClient, SMapViewportLayout* InLayout, int32 InPaneIndex)
{
	Client = InClient.ToSharedPtr();
	Layout = InLayout;
	PaneIndex = InPaneIndex;
	CommandList = std::make_shared<FUICommandList>();
	BindCommands();

	ChildSlot[SNew(SCommandBindingScope)
	        .CommandList(
	            CommandList)[SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[SNew(SMapViewportToolBar, this)] +
	            SVerticalBox::Slot().FillHeight(1.0f)[SAssignNew(ViewportWidget, SViewport)
	                    .ViewportClient(StaticCastSharedPtr<IViewportClient>(Client))]]];
}

FMapViewSettings& SMapViewport::GetSettings() const
{
	return Layout->GetSettings();
}

bool SMapViewport::IsActive() const
{
	return Layout->GetActivePane() == PaneIndex;
}

void SMapViewport::BindCommands()
{
	const FMapViewportCommands& C = FMapViewportCommands::Get();
	FUICommandList& List = *CommandList;
	const TSharedPtr<FMapViewportClient> View = Client;
	for (int32 Index = 0; Index < static_cast<int32>(EMapViewType::Count); ++Index)
	{
		const EMapViewType Type = static_cast<EMapViewType>(Index);
		List.MapAction(ToSharedRef(C.ViewportTypes[Index]), [View, Type] { View->SetViewportType(Type); }, nullptr,
		    [View, Type] { return View->GetViewportType() == Type; });
	}
	const TPair<TSharedPtr<FUICommandInfo>, EMapViewMode> Modes[] = {
	    {C.LitMode, EMapViewMode::Lit},
	    {C.UnlitMode, EMapViewMode::Unlit},
	    {C.WireframeMode, EMapViewMode::Wireframe},
	};
	for (const TPair<TSharedPtr<FUICommandInfo>, EMapViewMode>& Entry : Modes)
	{
		const TSharedPtr<FUICommandInfo>& Command = Entry.first;
		const EMapViewMode Mode = Entry.second;
		const EMapViewMode ThisMode = Mode;
		List.MapAction(ToSharedRef(Command), [View, ThisMode] { View->SetViewMode(ThisMode); }, [View, ThisMode]
		{ return View->SupportsViewMode(ThisMode); }, [View, ThisMode] { return View->GetViewMode() == ThisMode; });
	}
	List.MapAction(ToSharedRef(C.FocusViewportToSelection), [View] { View->FrameSelection(); },
	    [View] { return View->GetDocument() != nullptr; });
	List.MapAction(
	    ToSharedRef(C.ResetCamera), [View] { View->ResetCamera(); }, [View] { return View->GetDocument() != nullptr; });
	SMapViewportLayout* Owner = Layout;
	const int32 Pane = PaneIndex;
	List.MapAction(ToSharedRef(C.ToggleMaximize), [Owner, Pane] { Owner->ToggleMaximize(Pane); },
	    [Owner] { return GetNumPanes(Owner->GetLayout()) > 1; }, [Owner, Pane] { return Owner->IsMaximized(Pane); });
	for (int32 Index = 0; Index < static_cast<int32>(EMapViewportLayout::Count); ++Index)
	{
		const EMapViewportLayout ThisLayout = static_cast<EMapViewportLayout>(Index);
		List.MapAction(ToSharedRef(C.ViewportConfigs[Index]), [Owner, ThisLayout] { Owner->SetLayout(ThisLayout); },
		    nullptr, [Owner, ThisLayout] { return Owner->GetLayout() == ThisLayout; });
	}
}

FReply SMapViewport::OnPreviewMouseButtonDown(const FGeometry&, const FPointerEvent&)
{
	Layout->SetActivePane(PaneIndex);
	return FReply::Unhandled();
}

FReply SMapViewport::OnDragOver(const FGeometry&, const FDragDropEvent& Event)
{
	return Event.GetOperationAs<FMapPlaceDragDrop>() && Client->GetDocument() ? FReply::Handled() : FReply::Unhandled();
}

FReply SMapViewport::OnDrop(const FGeometry&, const FDragDropEvent& Event)
{
	const TSharedPtr<FMapPlaceDragDrop> Operation = Event.GetOperationAs<FMapPlaceDragDrop>();
	if (!Operation || !Client->GetDocument() || !ViewportWidget)
	{
		return FReply::Unhandled();
	}
	Layout->SetActivePane(PaneIndex);
	const FVector2 Local = ViewportWidget->GetCachedGeometry().AbsoluteToLocal(Event.GetAbsolutePosition());
	Layout->Place(Operation->Item, *Client, Client->GetDropPoint(Local));
	return FReply::Handled();
}

int32 SMapViewport::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect,
    FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	const int32 Top = SCompoundWidget::OnPaint(Args, Geometry, CullingRect, OutDrawElements, LayerId, InWidgetStyle);
	if (!IsActive() || Layout->GetNumVisiblePanes() < 2)
	{
		return Top;
	}
	// The active pane is outlined, so the commands' target is clear.
	const FVector2 Size = Geometry.Size;
	OutDrawElements.AddLines(Top + 1, Geometry.ToPaintGeometry(),
	    {FVector2(0.5f, 0.5f), FVector2(Size.X - 0.5f, 0.5f), FVector2(Size.X - 0.5f, Size.Y - 0.5f),
	        FVector2(0.5f, Size.Y - 0.5f)},
	    FSlateColor(EStyleColor::Primary).Resolve(), 1.0f, true);
	return Top + 1;
}

// ---------------------------------------------------------------------------
// SMapViewportLayout

void SMapViewportLayout::Construct(
    const FArguments& Args, const TArray<TSharedPtr<FMapViewportClient>>& InClients, FMapViewSettings* InSettings)
{
	FMapViewportCommands::Register();
	Settings = InSettings;
	Clients = InClients;
	Clients.resize(MaxPanes);
	Layout = Args._Layout;
	ToolCommands = Args._ToolCommands;
	ToolCommandList = Args._ToolCommandList;
	OnPlace = Args._OnPlace;
	Panes.resize(MaxPanes);
	ChildSlot[SAssignNew(Host, SBox)];
	Rebuild();
}

void SMapViewportLayout::SetLayout(EMapViewportLayout InLayout)
{
	if (InLayout == Layout && MaximizedPane == INDEX_NONE)
	{
		return;
	}
	Layout = InLayout;
	MaximizedPane = INDEX_NONE;
	// The pane worked in last stays on screen: it trades places with the first.
	if (ActivePane >= GetNumPanes(Layout))
	{
		std::swap(Clients[0], Clients[static_cast<SIZE_T>(ActivePane)]);
		ActivePane = 0;
	}
	QueueRebuild();
	OnLayoutChanged.Broadcast();
}

void SMapViewportLayout::QueueRebuild()
{
	// Next frame: the change may come from a button in a pane, which the
	// rebuild replaces.
	if (bRebuildQueued)
	{
		return;
	}
	bRebuildQueued = true;
	RegisterActiveTimer(0.0f, [this](double, float)
	{
		bRebuildQueued = false;
		Rebuild();
		return EActiveTimerReturn::Stop;
	});
}

void SMapViewportLayout::ToggleMaximize(int32 Pane)
{
	if (Pane < 0 || Pane >= GetNumPanes(Layout) || GetNumPanes(Layout) < 2)
	{
		return;
	}
	MaximizedPane = MaximizedPane == Pane ? INDEX_NONE : Pane;
	ActivePane = Pane;
	QueueRebuild();
	OnLayoutChanged.Broadcast();
}

void SMapViewportLayout::SetActivePane(int32 Pane)
{
	if (Pane != ActivePane && Pane >= 0 && Pane < MaxPanes)
	{
		ActivePane = Pane;
		OnLayoutChanged.Broadcast();
	}
}

TSharedRef<SWidget> SMapViewportLayout::MakePane(int32 Pane)
{
	if (!Clients[static_cast<SIZE_T>(Pane)])
	{
		return SNullWidget::NullWidget();
	}
	const TSharedRef<SMapViewport> Widget =
	    SNew(SMapViewport, ToSharedRef(Clients[static_cast<SIZE_T>(Pane)]), this, Pane);
	Panes[static_cast<SIZE_T>(Pane)] = Widget.ToSharedPtr();
	return Widget;
}

void SMapViewportLayout::Rebuild()
{
	for (TWeakPtr<SMapViewport>& Pane : Panes)
	{
		Pane.reset();
	}
	if (MaximizedPane != INDEX_NONE)
	{
		Host->SetContent(MakePane(MaximizedPane));
		return;
	}
	const TFunction<TSharedRef<SWidget>(EOrientation, std::initializer_list<TPair<TSharedRef<SWidget>, float>>)> Split =
	    [](EOrientation Orientation,
	        std::initializer_list<TPair<TSharedRef<SWidget>, float>> Children) -> TSharedRef<SWidget>
	{
		const TSharedRef<SSplitter> Splitter = SNew(SSplitter).Orientation(Orientation);
		for (const TPair<TSharedRef<SWidget>, float>& Entry : Children)
		{
			const TSharedRef<SWidget>& Child = Entry.first;
			const float Share = Entry.second;
			Splitter->AddSlot().Value(Share).MinSize(60.0f)[Child];
		}
		return Splitter;
	};
	constexpr EOrientation H = EOrientation::Horizontal;
	constexpr EOrientation V = EOrientation::Vertical;
	constexpr float Big = 0.62f;
	TSharedRef<SWidget> Content = SNullWidget::NullWidget();
	switch (Layout)
	{
	case EMapViewportLayout::OnePane:
		Content = MakePane(0);
		break;
	case EMapViewportLayout::TwoPanesHoriz:
		Content = Split(H, {{MakePane(0), 0.5f}, {MakePane(1), 0.5f}});
		break;
	case EMapViewportLayout::TwoPanesVert:
		Content = Split(V, {{MakePane(0), 0.5f}, {MakePane(1), 0.5f}});
		break;
	case EMapViewportLayout::ThreePanesLeft:
		Content = Split(H, {{MakePane(0), 0.5f}, {Split(V, {{MakePane(1), 0.5f}, {MakePane(2), 0.5f}}), 0.5f}});
		break;
	case EMapViewportLayout::ThreePanesRight:
		Content = Split(H, {{Split(V, {{MakePane(1), 0.5f}, {MakePane(2), 0.5f}}), 0.5f}, {MakePane(0), 0.5f}});
		break;
	case EMapViewportLayout::ThreePanesTop:
		Content = Split(V, {{MakePane(0), 0.5f}, {Split(H, {{MakePane(1), 0.5f}, {MakePane(2), 0.5f}}), 0.5f}});
		break;
	case EMapViewportLayout::ThreePanesBottom:
		Content = Split(V, {{Split(H, {{MakePane(1), 0.5f}, {MakePane(2), 0.5f}}), 0.5f}, {MakePane(0), 0.5f}});
		break;
	case EMapViewportLayout::FourPanes2x2:
		Content = Split(V,
		    {{Split(H, {{MakePane(0), 0.5f}, {MakePane(1), 0.5f}}), 0.5f},
		        {Split(H, {{MakePane(2), 0.5f}, {MakePane(3), 0.5f}}), 0.5f}});
		break;
	case EMapViewportLayout::FourPanesLeft:
		Content = Split(H,
		    {{MakePane(0), Big},
		        {Split(V, {{MakePane(1), 1.0f}, {MakePane(2), 1.0f}, {MakePane(3), 1.0f}}), 1.0f - Big}});
		break;
	case EMapViewportLayout::FourPanesRight:
		Content = Split(H,
		    {{Split(V, {{MakePane(1), 1.0f}, {MakePane(2), 1.0f}, {MakePane(3), 1.0f}}), 1.0f - Big},
		        {MakePane(0), Big}});
		break;
	case EMapViewportLayout::FourPanesTop:
		Content = Split(V,
		    {{MakePane(0), Big},
		        {Split(H, {{MakePane(1), 1.0f}, {MakePane(2), 1.0f}, {MakePane(3), 1.0f}}), 1.0f - Big}});
		break;
	case EMapViewportLayout::FourPanesBottom:
		Content = Split(V,
		    {{Split(H, {{MakePane(1), 1.0f}, {MakePane(2), 1.0f}, {MakePane(3), 1.0f}}), 1.0f - Big},
		        {MakePane(0), Big}});
		break;
	case EMapViewportLayout::Count:
		break;
	}
	Host->SetContent(Content);
}
