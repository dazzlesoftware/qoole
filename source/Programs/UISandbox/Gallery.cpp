// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Gallery.h"

#include "Application/UIApplication.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SImage.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/SEditableText.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

namespace
{
	const FTextBlockStyle& TextStyle(const char* Name) { return FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>(Name); }
	const FButtonStyle& ButtonStyle(const char* Name) { return FAppStyle::Get().GetWidgetStyle<FButtonStyle>(Name); }

	// A titled card.
	TSharedRef<SWidget> Section(const char* Title, const char* Description, const TSharedRef<SWidget>& Content)
	{
		return SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Panel")).Padding(FMargin(14.0f, 10.0f, 14.0f, 14.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Title).TextStyle(&TextStyle("BoldText"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 2.0f, 0.0f, 10.0f))[SNew(STextBlock).Text(Description).TextStyle(&TextStyle("SmallText")).AutoWrapText(true)]
			+ SVerticalBox::Slot().AutoHeight()[Content]
		];
	}

	// A page: a scrolling column of sections.
	TSharedRef<SScrollBox> Page()
	{
		return SNew(SScrollBox);
	}

	void AddSection(const TSharedRef<SScrollBox>& PageBox, const TSharedRef<SWidget>& SectionWidget)
	{
		PageBox->AddSlot().Padding(FMargin(16.0f, 16.0f, 16.0f, 0.0f))[SectionWidget];
	}

	// A drag-and-drop payload: a color swatch.
	class FSwatchDragDrop final : public FDragDropOperation
	{
	public:
		GE_DRAG_DROP_OPERATION_TYPE(FSwatchDragDrop, FDragDropOperation)
		FStyleColor Color;
		FString Name;

		TSharedPtr<SWidget> GetDefaultDecorator() const override
		{
			return SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("DragDecorator")).Padding(FMargin(8.0f, 4.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(TOptional<float>(14.0f)).HeightOverride(TOptional<float>(14.0f))[SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Color)]]
				+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))[SNew(STextBlock).Text(FString("Dragging ") + Name)]
			];
		}
		TOptional<EMouseCursor> GetCursor() const override { return EMouseCursor::GrabHand; }
	};

	// A swatch you can drag.
	class SSwatch final : public SLeafWidget
	{
	public:
		GE_BEGIN_ARGS(SSwatch)
			GE_ARGUMENT(FStyleColor, Color, FStyleColor(FColor::White()))
			GE_ARGUMENT(FString, Name, "")
		GE_END_ARGS()

		void Construct(const FArguments& Args)
		{
			Color = Args._Color;
			Name = Args._Name;
			SetToolTipText(FString::Format("Drag {} onto the drop zone", Name));
		}

		FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event) override
		{
			return FReply::Handled().DetectDrag(SharedThis(this), Event.GetEffectingButton());
		}
		FReply OnDragDetected(const FGeometry&, const FPointerEvent&) override
		{
			auto Operation = std::make_shared<FSwatchDragDrop>();
			Operation->Color = Color;
			Operation->Name = Name;
			return FReply::Handled().BeginDragDrop(TSharedRef<FDragDropOperation>(StaticCastSharedPtr<FDragDropOperation>(Operation)));
		}
		FCursorReply OnCursorQuery(const FGeometry&, const FPointerEvent&) const override { return FCursorReply::Cursor(EMouseCursor::Hand); }

	protected:
		FVector2 ComputeDesiredSize(float) const override { return FVector2(40.0f, 40.0f); }
		int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FRect&, FDrawElementList& Out, int32 LayerId, const FWidgetStyle& Style) const override
		{
			Out.AddRoundedBox(LayerId, Geometry.ToPaintGeometry(), Style.Tinted(Color.Resolve()), 6.0f,
				IsHovered() ? FStyleColor(EStyleColor::ForegroundHover).Resolve() : FColor::Transparent(), 2.0f);
			return LayerId;
		}

	private:
		FStyleColor Color;
		FString Name;
	};

	// Accepts swatches and takes their color.
	class SDropZone final : public SLeafWidget
	{
	public:
		GE_BEGIN_ARGS(SDropZone)
			GE_EVENT(TDelegate<void(const FString&)>, OnDropped)
		GE_END_ARGS()

		void Construct(const FArguments& Args) { OnDropped = Args._OnDropped; }

		void OnDragEnter(const FGeometry&, const FDragDropEvent& Event) override
		{
			bHighlight = Event.GetOperationAs<FSwatchDragDrop>() != nullptr;
			Invalidate(EInvalidateWidgetReason::Paint);
		}
		void OnDragLeave(const FDragDropEvent&) override
		{
			bHighlight = false;
			Invalidate(EInvalidateWidgetReason::Paint);
		}
		FReply OnDrop(const FGeometry&, const FDragDropEvent& Event) override
		{
			bHighlight = false;
			if (const TSharedPtr<FSwatchDragDrop> Op = Event.GetOperationAs<FSwatchDragDrop>())
			{
				Fill = Op->Color;
				Invalidate(EInvalidateWidgetReason::Paint);
				OnDropped.ExecuteIfBound(Op->Name);
				return FReply::Handled();
			}
			return FReply::Unhandled();
		}

	protected:
		FVector2 ComputeDesiredSize(float) const override { return FVector2(160.0f, 90.0f); }
		int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FRect&, FDrawElementList& Out, int32 LayerId, const FWidgetStyle& Style) const override
		{
			Out.AddRoundedBox(LayerId, Geometry.ToPaintGeometry(), Style.Tinted(Fill.Resolve()), 8.0f,
				Style.Tinted((bHighlight ? FStyleColor(EStyleColor::Primary) : FStyleColor(EStyleColor::Border)).Resolve()), bHighlight ? 3.0f : 1.5f);
			return LayerId;
		}

	private:
		TDelegate<void(const FString&)> OnDropped;
		FStyleColor Fill = FStyleColor(EStyleColor::Recessed);
		bool bHighlight = false;
	};

	// The milestone 2 showcase inside the gallery.
	class SDrawElementsShowcase final : public SLeafWidget
	{
	public:
		GE_BEGIN_ARGS(SDrawElementsShowcase)
		GE_END_ARGS()

		void Construct(const FArguments&, FGalleryHost* InHost)
		{
			Host = InHost;
			SetVolatile(true);
			SetClipping(EWidgetClipping::ClipToBounds);
		}

	protected:
		FVector2 ComputeDesiredSize(float) const override { return FVector2(640.0f, 480.0f); }
		int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FRect&, FDrawElementList& Out, int32 LayerId, const FWidgetStyle&) const override
		{
			FSandboxFrameInfo Frame = Host->GetFrameInfo();
			Frame.Scale = Geometry.Scale;
			Frame.Origin = Geometry.AbsolutePosition / Geometry.Scale;
			Frame.LogicalSize = Geometry.Size;
			Frame.MouseLogical = IsHovered() ? FUIApplication::Get().GetCursorPosition() / Geometry.Scale - Frame.Origin : FVector2(-1.0f, -1.0f);
			// The showcase uses layers 0-6; keep them above the page.
			FDrawElementList Local;
			Local.SetViewportSize(Out.GetViewportSize());
			Host->Content->PaintMain(Local, Frame, *Host->ViewportTarget);
			int32 MaxLayer = LayerId;
			for (const FDrawElement& Element : Local.GetElements())
			{
				// Re-add with our layer offset; clip states are re-pushed.
				const int32 Layer = LayerId + Element.Layer;
				MaxLayer = FMath::Max(MaxLayer, Layer);
				if (Element.ClipIndex != INDEX_NONE)
				{
					Out.PushClipPixels(Local.GetClipRects()[Element.ClipIndex]);
				}
				switch (Element.Type)
				{
				case EDrawElementType::Box: Out.AddBox(Layer, Element.Geometry, Element.Get<FBoxElement>().Brush); break;
				case EDrawElementType::RoundedBox:
				{
					const FRoundedBoxElement& R = Element.Get<FRoundedBoxElement>();
					Out.AddRoundedBox(Layer, Element.Geometry, Element.Color, R.CornerRadii, R.OutlineColor, R.OutlineWidth);
					break;
				}
				case EDrawElementType::Text: Out.AddText(Layer, Element.Geometry, ToSharedRef(Element.Get<FTextElement>().Text), Element.Color); break;
				case EDrawElementType::Lines:
				{
					const FLinesElement& L = Element.Get<FLinesElement>();
					Out.AddLines(Layer, Element.Geometry, L.Points, Element.Color, L.Thickness, L.bClosed);
					break;
				}
				case EDrawElementType::CustomVerts:
				{
					const FCustomVertsElement& C = Element.Get<FCustomVertsElement>();
					Out.AddCustomVerts(Layer, Element.Geometry, C.Image, C.Vertices, C.Indices);
					break;
				}
				case EDrawElementType::Viewport:
				{
					const FViewportElement& V = Element.Get<FViewportElement>();
					Out.AddViewport(Layer, Element.Geometry, V.RenderTarget, V.bAlphaBlend, Element.Color);
					break;
				}
				}
				if (Element.ClipIndex != INDEX_NONE)
				{
					Out.PopClip();
				}
			}
			return MaxLayer;
		}

	private:
		FGalleryHost* Host = nullptr;
	};
}

// ---------------------------------------------------------------------------

void SWidgetGallery::Construct(const FArguments&, FGalleryHost InHost)
{
	Host = std::move(InHost);
	PageNames = { "Buttons and toggles", "Text", "Layout", "Lists and trees", "Menus, tooltips, drag and drop", "Draw elements (milestone 2)" };
	for (int32 I = 0; I < GetPageCount(); ++I)
	{
		PageIndices.push_back(I);
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[Host.bShowHeader ? MakeHeader() : SNullWidget::NullWidget()]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SSplitter)
			+ SSplitter::Slot().Value(0.2f).MinSize(140.0f)
			[
				SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FStyleColor(EStyleColor::Recessed)).Padding(FMargin(0.0f, 8.0f))
				[
					SAssignNew(Nav, SListView<int32>)
					.ListItemsSource(&PageIndices)
					.OnGenerateRow([this](int32 Page, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
					{
						return SNew(STableRow<int32>, Owner).Padding(FMargin(12.0f, 6.0f))[SNew(STextBlock).Text(PageNames[Page])];
					})
					.OnSelectionChanged([this](int32 Page, ESelectInfo)
					{
						SetPage(Page);
					})
				]
			]
			+ SSplitter::Slot().Value(0.8f)
			[
				SAssignNew(Switcher, SWidgetSwitcher)
				+ SWidgetSwitcher::Slot()[MakeButtonsPage()]
				+ SWidgetSwitcher::Slot()[MakeTextPage()]
				+ SWidgetSwitcher::Slot()[MakeLayoutPage()]
				+ SWidgetSwitcher::Slot()[MakeViewsPage()]
				+ SWidgetSwitcher::Slot()[MakeMenusPage()]
				+ SWidgetSwitcher::Slot()[MakeDrawPage()]
			]
		]
		+ SVerticalBox::Slot().AutoHeight()[MakeStatusBar()]
	];
	Nav->SetSelection(0);
}

void SWidgetGallery::SetPage(int32 Index)
{
	Index = FMath::Clamp(Index, 0, GetPageCount() - 1);
	Switcher->SetActiveWidgetIndex(Index);
	if (Nav && !Nav->IsItemSelected(Index))
	{
		Nav->SetSelection(Index); // calls back here; the check above stops the loop
	}
	LastEvent = FString::Format("Page: {}", PageNames[Switcher->GetActiveWidgetIndex()]);
}

TSharedRef<SWidget> SWidgetGallery::MakeMenuContent(const FString& MenuName)
{
	const auto Picked = [this](const char* What) { return [this, What]() { LastEvent = FString::Format("Menu: {}", What); }; };
	if (MenuName == "File")
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry).Text("New map").InputGesture("Ctrl+N").OnSelected(Picked("New map"))]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry).Text("Open...").InputGesture("Ctrl+O").OnSelected(Picked("Open"))]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry).Text("Open recent")
				.SubMenu(SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry).Text("castle.map").OnSelected(Picked("castle.map"))]
					+ SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry).Text("dungeon.map").OnSelected(Picked("dungeon.map"))]
					+ SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry).Text("arena.map").OnSelected(Picked("arena.map"))])]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SSeparator)]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry).Text("Save").InputGesture("Ctrl+S").OnSelected(Picked("Save"))]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry).Text("Exit").OnSelected([this] { Host.Quit(); })];
	}
	if (MenuName == "Edit")
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry).Text("Undo").InputGesture("Ctrl+Z").OnSelected(Picked("Undo"))]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry).Text("Redo").InputGesture("Ctrl+Y").OnSelected(Picked("Redo"))]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SSeparator)]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry).Text("Cut").InputGesture("Ctrl+X").OnSelected(Picked("Cut"))]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry).Text("Copy").InputGesture("Ctrl+C").OnSelected(Picked("Copy"))]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry).Text("Paste").InputGesture("Ctrl+V").OnSelected(Picked("Paste"))];
	}
	// View: application scale and the inspector.
	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	for (const float Scale : { 1.0f, 1.25f, 1.5f, 2.0f })
	{
		Box->AddSlot().AutoHeight()[SNew(SMenuEntry).Text(FString::Format("App scale {:.0f}%", Scale * 100.0f)).OnSelected([this, Scale] { Host.SetAppScale(Scale); })];
	}
	Box->AddSlot().AutoHeight()[SNew(SSeparator)];
	Box->AddSlot().AutoHeight()[SNew(SMenuEntry).Text("Toggle inspector window").InputGesture("F4").OnSelected([this] { Host.ToggleInspector(); })];
	return Box;
}

TSharedRef<SWidget> SWidgetGallery::MakeHeader()
{
	const auto MenuButton = [this](const char* Name) -> TSharedRef<SComboButton>
	{
		const FString MenuName = Name;
		return SNew(SComboButton).Text(Name).HasDownArrow(false).Placement(EMenuPlacement::BelowAnchor).ButtonStyle(&ButtonStyle("FlatButton"))
			.OnGetMenuContent([this, MenuName] { return MakeMenuContent(MenuName); });
	};
	return SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Header")).Padding(FMargin(12.0f, 6.0f))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 16.0f, 0.0f))[SNew(STextBlock).Text("Genesis UI").TextStyle(&TextStyle("HeadingText"))]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SAssignNew(FileMenu, SComboButton).Text("File").HasDownArrow(false).Placement(EMenuPlacement::BelowAnchor).ButtonStyle(&ButtonStyle("FlatButton"))
			.OnGetMenuContent([this] { return MakeMenuContent("File"); })]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[MenuButton("Edit")]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[MenuButton("View")]
		+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right).VAlign(VAlign_Center)
		[
			SNew(STextBlock).TextStyle(&TextStyle("SmallText")).Text([this]
			{
				const FSandboxFrameInfo Frame = Host.GetFrameInfo();
				return FString::Format("{} · {} · DPI {:.0f}% · app scale {:.0f}% · {:.0f} fps · {} batches",
					Frame.BackendName, Frame.AdapterName, Frame.DpiScale * 100.0f, Frame.AppScale * 100.0f, Frame.FramesPerSecond, Frame.Stats.Batches);
			})
		]
	];
}

TSharedRef<SWidget> SWidgetGallery::MakeStatusBar()
{
	return SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Header")).Padding(FMargin(12.0f, 4.0f))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(STextBlock).TextStyle(&TextStyle("SmallText")).Text([this] { return LastEvent; })]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).TextStyle(&TextStyle("SmallText")).Text([]
		{
			const TSharedPtr<SWidget> Focused = FUIApplication::Get().GetKeyboardFocusedWidget();
			return FString::Format("Focus: {}   ·   Tab / arrows move focus, Enter or Space presses, Esc closes menus", Focused ? Focused->GetTypeName() : FString("none"));
		})]
	];
}

TSharedRef<SWidget> SWidgetGallery::MakeButtonsPage()
{
	const TSharedRef<SScrollBox> PageBox = Page();
	AddSection(PageBox, Section("Buttons", "Click, or focus with Tab and press Enter or Space. The disabled one ignores everything.",
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2(8.0f, 8.0f))
			+ SWrapBox::Slot()[SNew(SButton).Text("Default").ToolTipText("A default button").OnClicked([this] { ++Clicks; LastEvent = "Clicked Default"; return FReply::Handled(); })]
			+ SWrapBox::Slot()[SNew(SButton).Text("Primary").ButtonStyle(&ButtonStyle("PrimaryButton")).OnClicked([this] { ++Clicks; LastEvent = "Clicked Primary"; return FReply::Handled(); })]
			+ SWrapBox::Slot()[SNew(SButton).Text("Flat").ButtonStyle(&ButtonStyle("FlatButton")).OnClicked([this] { ++Clicks; LastEvent = "Clicked Flat"; return FReply::Handled(); })]
			+ SWrapBox::Slot()[SNew(SButton).Text("Disabled").IsEnabled(false)]
			+ SWrapBox::Slot()[SNew(SButton).Text("Not focusable").IsFocusable(false).ToolTipText("Clicking this keeps focus where it was").OnClicked([this] { ++Clicks; LastEvent = "Clicked Not focusable"; return FReply::Handled(); })]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 10.0f, 0.0f, 0.0f))[SNew(STextBlock).Text([this] { return FString::Format("Buttons clicked {} times", Clicks); })]));

	AddSection(PageBox, Section("Check boxes", "\"All layers\" shows the undetermined state when only some layers are on.",
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 2.0f))[SNew(SCheckBox).Text("All layers")
			.IsChecked([this] { return LayerA == LayerB ? LayerA : ECheckBoxState::Undetermined; })
			.OnCheckStateChanged([this](ECheckBoxState) { const ECheckBoxState New = (LayerA == ECheckBoxState::Checked && LayerB == ECheckBoxState::Checked) ? ECheckBoxState::Unchecked : ECheckBoxState::Checked; LayerA = LayerB = New; })]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(22.0f, 2.0f, 0.0f, 2.0f))[SNew(SCheckBox).Text("Brushes").IsChecked([this] { return LayerA; }).OnCheckStateChanged([this](ECheckBoxState S) { LayerA = S; })]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(22.0f, 2.0f, 0.0f, 2.0f))[SNew(SCheckBox).Text("Entities").IsChecked([this] { return LayerB; }).OnCheckStateChanged([this](ECheckBoxState S) { LayerB = S; })]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 2.0f))[SNew(SCheckBox).Text("Snap to grid (keeps its own state)").IsChecked(ECheckBoxState::Checked)]));

	AddSection(PageBox, Section("Progress", "A bound fraction, a busy bar and a throbber (these repaint every frame; the rest of the window only when something changes).",
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 4.0f))[SNew(SProgressBar).Percent([] { return TOptional<float>(FMath::Fmod(static_cast<float>(FUIApplication::Get().GetTime()) * 0.2f, 1.0f)); })]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 4.0f))[SNew(SProgressBar)]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 8.0f)).HAlign(HAlign_Left)[SNew(SThrobber)]));
	PageBox->AddSlot().Padding(FMargin(0.0f, 16.0f, 0.0f, 0.0f))[SNew(SSpacer)];
	return PageBox;
}

TSharedRef<SWidget> SWidgetGallery::MakeTextPage()
{
	const TSharedRef<SScrollBox> PageBox = Page();
	AddSection(PageBox, Section("Text blocks", "Wrapped to the available width; resize the window or drag the splitter.",
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).AutoWrapText(true).Text(
			"Genesis UI lays out text with FTextLayout: lines are shaped by HarfBuzz, rasterized by FreeType at the final pixel size, and wrapped at word "
			"boundaries. Layout runs only when something changes, so an idle editor redraws nothing.")]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))[SNew(STextBlock).Text("Fallback fonts: नमस्ते दुनिया — Devanagari, shaped").TextStyle(&TextStyle("BoldText"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))[SNew(STextBlock).Text("Centered text").Justification(ETextJustify::Center)]
		+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text("Right-aligned text").Justification(ETextJustify::Right)]));

	const TSharedRef<SGridPanel> Form = SNew(SGridPanel);
	Form->SetColumnFill(1, 1.0f);
	const auto Row = [&Form](int32 Index, const char* Label, const TSharedRef<SWidget>& Field)
	{
		Form->AddSlot(0, Index).VAlign(VAlign_Center).Padding(FMargin(0.0f, 4.0f, 12.0f, 4.0f))[SNew(STextBlock).Text(Label)];
		Form->AddSlot(1, Index).Padding(FMargin(0.0f, 4.0f))[Field];
	};
	const auto OnCommit = [this](const FString& Text, ETextCommit How)
	{
		const char* HowText = How == ETextCommit::OnEnter ? "Enter" : (How == ETextCommit::OnUserMovedFocus ? "focus moved" : (How == ETextCommit::OnCleared ? "Escape (reverted)" : "code"));
		LastCommit = FString::Format("Committed \"{}\" ({})", Text, HowText);
	};
	Row(0, "Name", SNew(SEditableTextBox).HintText("Type a name").OnTextCommitted(OnCommit).RevertTextOnEscape(true));
	Row(1, "Password", SNew(SEditableTextBox).IsPassword(true).HintText("Masked; cannot be copied").OnTextCommitted(OnCommit));
	Row(2, "Read-only", SNew(SEditableTextBox).Text("Select and copy me, but no editing").IsReadOnly(true));
	Row(3, "Select all", SNew(SEditableTextBox).Text("Focus selects everything").SelectAllTextWhenFocused(true).OnTextCommitted(OnCommit));
	Row(4, "Notes", SNew(SBox).HeightOverride(TOptional<float>(110.0f))[SNew(SEditableTextBox).IsMultiLine(true).AutoWrapText(true)
		.Text("Multiple lines: Enter adds a line, Ctrl+Enter commits.\nUp and Down keep the column; Ctrl+arrows move by word.").OnTextCommitted(OnCommit)]);
	AddSection(PageBox, Section("Text boxes", "Caret, selection (mouse drag, double-click, Shift+arrows), undo/redo, clipboard, IME composition.",
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[Form]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 10.0f, 0.0f, 0.0f))[SNew(STextBlock).TextStyle(&TextStyle("SmallText")).Text([this] { return LastCommit; })]));
	PageBox->AddSlot().Padding(FMargin(0.0f, 16.0f, 0.0f, 0.0f))[SNew(SSpacer)];
	return PageBox;
}

TSharedRef<SWidget> SWidgetGallery::MakeLayoutPage()
{
	const TSharedRef<SScrollBox> PageBox = Page();
	const TSharedRef<SWrapBox> Chips = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2(6.0f, 6.0f));
	for (const char* Word : { "worldspawn", "func_door", "light", "info_player_start", "trigger_once", "func_wall", "path_corner", "monster_army", "item_health", "weapon_nailgun" })
	{
		Chips->AddSlot()[SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Recessed")).Padding(FMargin(8.0f, 3.0f))[SNew(STextBlock).Text(Word)]];
	}
	AddSection(PageBox, Section("Wrap box", "Children flow onto new lines as the width shrinks.", Chips));

	const TSharedRef<SGridPanel> Grid = SNew(SGridPanel);
	const FStyleColor Colors[] = { EStyleColor::Primary, EStyleColor::Success, EStyleColor::Warning, EStyleColor::Error };
	for (int32 I = 0; I < 4; ++I)
	{
		Grid->AddSlot(I % 2, I / 2).Padding(3.0f)[SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Colors[I]).Padding(FMargin(10.0f))
			[SNew(STextBlock).Text(FString::Format("Cell {},{}", I % 2, I / 2)).ColorAndOpacity(FStyleColor(EStyleColor::ForegroundInverted))]];
	}
	Grid->AddSlot(0, 2).ColumnSpan(2).Padding(3.0f)[SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Recessed")).Padding(FMargin(10.0f))[SNew(STextBlock).Text("Spanning two columns")]];
	Grid->SetColumnFill(1, 1.0f);

	AddSection(PageBox, Section("Grid, overlay, canvas, scale box", "Panels from docs/GenesisUI.md §6.6.",
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(0.0f, 0.0f, 12.0f, 0.0f))[Grid]
		+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(0.0f, 0.0f, 12.0f, 0.0f))
		[
			SNew(SBox).HeightOverride(TOptional<float>(140.0f))
			[
				SNew(SOverlay)
				+ SOverlay::Slot()[SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Recessed"))]
				+ SOverlay::Slot()
				[
					SNew(SCanvas)
					+ SCanvas::Slot().Anchors(FAnchors(0.0f, 0.0f)).Offset(FMargin(8.0f, 8.0f, 0.0f, 0.0f)).AutoSize(true)[SNew(STextBlock).Text("top-left")]
					+ SCanvas::Slot().Anchors(FAnchors(1.0f, 1.0f)).Alignment(FVector2(1.0f, 1.0f)).Offset(FMargin(-8.0f, -8.0f, 0.0f, 0.0f)).AutoSize(true)[SNew(STextBlock).Text("bottom-right")]
					+ SCanvas::Slot().Anchors(FAnchors(0.5f, 0.5f)).Alignment(FVector2(0.5f, 0.5f)).AutoSize(true)[SNew(SButton).Text("Centered")]
				]
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		[
			SNew(SBox).HeightOverride(TOptional<float>(140.0f))
			[
				SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
				[
					SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Panel")).Padding(FMargin(6.0f))[SNew(STextBlock).Text("Scaled to fit")]
				]
			]
		]));

	AddSection(PageBox, Section("Splitters", "Drag the handles; the inner splitter is vertical.",
		SNew(SBox).HeightOverride(TOptional<float>(180.0f))
		[
			SNew(SSplitter)
			+ SSplitter::Slot().Value(0.3f)[SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Recessed")).Padding(8.0f)[SNew(STextBlock).Text("Left")]]
			+ SSplitter::Slot().Value(0.7f)
			[
				SNew(SSplitter).Orientation(EOrientation::Vertical)
				+ SSplitter::Slot().Value(0.5f)[SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Recessed")).Padding(8.0f)[SNew(STextBlock).Text("Top right")]]
				+ SSplitter::Slot().Value(0.5f)[SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Recessed")).Padding(8.0f)[SNew(STextBlock).Text("Bottom right")]]
			]
		]));
	PageBox->AddSlot().Padding(FMargin(0.0f, 16.0f, 0.0f, 0.0f))[SNew(SSpacer)];
	return PageBox;
}

TSharedRef<SWidget> SWidgetGallery::MakeViewsPage()
{
	for (int32 I = 0; I < 10000; ++I)
	{
		auto Item = std::make_shared<FGalleryItem>();
		Item->Index = I;
		Item->Name = FString::Format("Texture {:05}", I);
		ListItems.push_back(Item);
	}
	const auto Node = [](const char* Name, const char* Kind, TArray<TSharedPtr<FMapNode>> Children = {})
	{
		auto N = std::make_shared<FMapNode>();
		N->Name = Name;
		N->Kind = Kind;
		N->Children = std::move(Children);
		return N;
	};
	TreeRoots = {
		Node("worldspawn", "world", {
			Node("Brushes", "group", { Node("floor_01", "brush"), Node("wall_north", "brush"), Node("wall_south", "brush"), Node("pillar", "brush") }),
			Node("Detail", "group", { Node("crate_a", "brush"), Node("crate_b", "brush") }),
		}),
		Node("Entities", "group", {
			Node("info_player_start", "entity"),
			Node("light", "entity"),
			Node("func_door", "entity", { Node("door_brush", "brush") }),
		}),
	};

	TSharedPtr<STreeView<TSharedPtr<FMapNode>>> Tree;
	TSharedPtr<SListView<TSharedPtr<FGalleryItem>>> List;
	const TSharedRef<SWidget> Content = SNew(SBox).HeightOverride(TOptional<float>(420.0f))
	[
		SNew(SSplitter)
		+ SSplitter::Slot().Value(0.5f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))[SNew(STextBlock).Text([this] { return FString::Format("10,000 items, {} selected (Ctrl/Shift-click, Ctrl+A, arrows, Home/End)", GetSelectedCount ? GetSelectedCount() : 0); })]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Recessed")).Padding(1.0f)
				[
					SAssignNew(List, SListView<TSharedPtr<FGalleryItem>>)
					.ListItemsSource(&ListItems)
					.SelectionMode(ESelectionMode::Multi)
					.OnGenerateRow([](TSharedPtr<FGalleryItem> Item, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
					{
						return SNew(STableRow<TSharedPtr<FGalleryItem>>, Owner)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(STextBlock).Text(Item->Name)]
							+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text(FString::Format("{}x{}", 64 << (Item->Index % 4), 64 << (Item->Index % 3))).TextStyle(&TextStyle("SmallText"))]
						];
					})
					.OnSelectionChanged([this](TSharedPtr<FGalleryItem> Item, ESelectInfo) { if (Item) LastEvent = FString::Format("List: {}", Item->Name); })
				]
			]
		]
		+ SSplitter::Slot().Value(0.5f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))[SNew(STextBlock).Text("Map outline (Left/Right collapse and expand)")]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Recessed")).Padding(1.0f)
				[
					SAssignNew(Tree, STreeView<TSharedPtr<FMapNode>>)
					.TreeItemsSource(&TreeRoots)
					.OnGetChildren([](TSharedPtr<FMapNode> N, TArray<TSharedPtr<FMapNode>>& Out) { Out = N->Children; })
					.OnGenerateRow([](TSharedPtr<FMapNode> N, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
					{
						return SNew(STableRow<TSharedPtr<FMapNode>>, Owner)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(STextBlock).Text(N->Name)]
							+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text(N->Kind).TextStyle(&TextStyle("SmallText"))]
						];
					})
					.OnSelectionChanged([this](TSharedPtr<FMapNode> N, ESelectInfo) { if (N) LastEvent = FString::Format("Tree: {} ({})", N->Name, N->Kind); })
				]
			]
		]
	];
	Tree->SetItemExpansion(TreeRoots[0], true);
	Tree->SetItemExpansion(TreeRoots[0]->Children[0], true);

	GetSelectedCount = [WeakList = TWeakPtr<SListView<TSharedPtr<FGalleryItem>>>(List)]
	{
		const TSharedPtr<SListView<TSharedPtr<FGalleryItem>>> Pinned = WeakList.lock();
		return Pinned ? Pinned->GetNumItemsSelected() : 0;
	};
	const TSharedRef<SScrollBox> PageBox = Page();
	AddSection(PageBox, Section("Virtualized views", "Only visible rows exist as widgets; scrolling reuses them.", Content));
	PageBox->AddSlot().Padding(FMargin(0.0f, 16.0f, 0.0f, 0.0f))[SNew(SSpacer)];
	return PageBox;
}

TSharedRef<SWidget> SWidgetGallery::MakeMenusPage()
{
	const TSharedRef<SScrollBox> PageBox = Page();

	TSharedPtr<SBorder> ContextArea;
	AddSection(PageBox, Section("Menus", "Drop-down menus with a submenu (the header has File / Edit / View too). Right-click the area for a context menu.",
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 8.0f, 0.0f))[SNew(SComboButton).Text("Choose a map").OnGetMenuContent([this] { return MakeMenuContent("File"); })]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SComboButton).Text("Edit").OnGetMenuContent([this] { return MakeMenuContent("Edit"); })]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 10.0f, 0.0f, 0.0f))
		[
			SAssignNew(ContextArea, SBorder).BorderImage(&FAppStyle::Get().GetBrush("Recessed")).Padding(FMargin(16.0f, 28.0f)).HAlign(HAlign_Center)
			.OnMouseButtonDown([this](const FGeometry& Geometry, const FPointerEvent& Event)
			{
				if (Event.GetEffectingButton() != EMouseButton::Right)
				{
					return FReply::Unhandled();
				}
				FUIApplication& App = FUIApplication::Get();
				const TSharedPtr<SWindow> Window = App.FindWindowForWidget(*this);
				const FVector2 Position = Event.GetAbsolutePosition() / (Window ? Window->GetScale() : 1.0f);
				(void)Geometry;
				App.PushMenu(SharedThis(this), MakeMenuContent("Edit"), Position);
				LastEvent = "Context menu";
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text("Right-click here").TextStyle(&TextStyle("SmallText"))
			]
		]));

	TSharedPtr<SButton> CustomTip;
	AddSection(PageBox, Section("Tooltips", "Rest the pointer on a widget. They close on a click and stay closed until the pointer moves on.",
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 8.0f, 0.0f))[SNew(SButton).Text("Text tooltip").ToolTipText("A plain text tooltip, wrapped when it gets long enough to need more than one line of text.")]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SAssignNew(CustomTip, SButton).Text("Custom tooltip")
			.ToolTip(SNew(SToolTip)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text("Any widget can be a tooltip").TextStyle(&TextStyle("BoldText"))]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))[SNew(SProgressBar)]
				])
		]));
	ToolTipDemo = CustomTip;

	const TSharedRef<SHorizontalBox> Swatches = SNew(SHorizontalBox);
	const TPair<const char*, EStyleColor> SwatchColors[] = { { "blue", EStyleColor::Primary }, { "green", EStyleColor::Success }, { "orange", EStyleColor::Warning }, { "red", EStyleColor::Error } };
	for (const auto& [Name, Color] : SwatchColors)
	{
		Swatches->AddSlot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 8.0f, 0.0f))[SNew(SSwatch).Name(Name).Color(FStyleColor(Color))];
	}
	AddSection(PageBox, Section("Drag and drop", "Drag a swatch onto the zone; Escape cancels a drag.",
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Swatches]
		+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(24.0f, 0.0f, 0.0f, 0.0f))[SNew(SDropZone).OnDropped([this](const FString& Name) { LastEvent = FString::Format("Dropped {}", Name); })]));
	PageBox->AddSlot().Padding(FMargin(0.0f, 16.0f, 0.0f, 0.0f))[SNew(SSpacer)];
	return PageBox;
}

TSharedRef<SWidget> SWidgetGallery::MakeDrawPage()
{
	return SNew(SDrawElementsShowcase, &Host);
}
