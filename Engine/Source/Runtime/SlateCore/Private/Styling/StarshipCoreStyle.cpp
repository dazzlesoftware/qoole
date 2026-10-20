// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Styling/StarshipCoreStyle.h"

#include "HAL/PlatformMisc.h"

FStarshipCoreFonts FStarshipCoreFonts::Load(FSlateFontCache& FontCache, FString FontDirectory)
{
	if (FontDirectory.IsEmpty())
	{
		FontDirectory = FPaths::FindContentDir() + "Slate/Fonts/";
	}
	FStarshipCoreFonts Fonts;
	const TSharedPtr<FFontFace> Regular = FontCache.LoadFace(FontDirectory + "NotoSans-Regular.ttf");
	const TSharedPtr<FFontFace> Bold = FontCache.LoadFace(FontDirectory + "NotoSans-Bold.ttf");
	const TSharedPtr<FFontFace> Devanagari = FontCache.LoadFace(FontDirectory + "NotoSansDevanagari-Regular.ttf");
	if (!Regular || !Bold)
	{
		GE_LOG(LogStyle, Error, "Genesis fonts not found in \"{}\"", FontDirectory);
		return Fonts;
	}
	TArray<TSharedPtr<FFontFace>> RegularFaces{ Regular };
	TArray<TSharedPtr<FFontFace>> BoldFaces{ Bold };
	if (Devanagari)
	{
		RegularFaces.push_back(Devanagari);
		BoldFaces.push_back(Devanagari);
	}
	Fonts.Regular = std::make_shared<FCompositeFont>(FName("Genesis.Regular"), RegularFaces);
	Fonts.Bold = std::make_shared<FCompositeFont>(FName("Genesis.Bold"), BoldFaces);
	return Fonts;
}

TSharedRef<FSlateStyleSet> FStarshipCoreStyle::Create(const FStarshipCoreFonts& Fonts)
{
	using C = EStyleColor;
	auto Set = MakeShared<FSlateStyleSet>(FName(SetName));
	constexpr float Radius = 4.0f;
	constexpr float FontSize = 13.0f;

	// Fonts and text.
	Set->Set("NormalFont", FSlateFontInfo(Fonts.Regular, FontSize));
	Set->Set("BoldFont", FSlateFontInfo(Fonts.Bold, FontSize));
	FTextBlockStyle Normal;
	Normal.Font = FSlateFontInfo(Fonts.Regular, FontSize);
	Set->Set("NormalText", Normal);
	Set->Set("SmallText", FTextBlockStyle(Normal).SetFontSize(11.0f).SetColor(C::ForegroundSecondary));
	Set->Set("BoldText", FTextBlockStyle(Normal).SetFont(FSlateFontInfo(Fonts.Bold, FontSize)));
	Set->Set("HeadingText", FTextBlockStyle(Normal).SetFont(FSlateFontInfo(Fonts.Bold, 18.0f)).SetColor(C::ForegroundHover));

	// Brushes.
	Set->Set("WhiteBrush", FSlateBrush::Solid(FColor::White()));
	Set->Set("Border", FSlateBrush::Solid(C::Panel));
	Set->Set("Panel", FSlateBrush::Rounded(C::Panel, Radius, C::Border, 1.0f));
	Set->Set("Recessed", FSlateBrush::Rounded(C::Recessed, Radius, C::Border, 1.0f));
	Set->Set("Header", FSlateBrush::Solid(C::Header));
	Set->Set("FocusRing", FSlateBrush::Rounded(FColor::Transparent(), Radius, C::Primary, 1.5f));
	Set->Set("DropIndicator", FSlateBrush::Solid(C::Primary));
	Set->Set("DragDecorator", FSlateBrush::Rounded(FSlateColor(C::Dropdown, 230), Radius, C::Primary, 1.0f));

	// Buttons.
	FButtonStyle Button;
	Button.Normal = FSlateBrush::Rounded(C::Header, Radius, C::Border, 1.0f);
	Button.Hovered = FSlateBrush::Rounded(C::Hover, Radius, C::Border, 1.0f);
	Button.Pressed = FSlateBrush::Rounded(C::Pressed, Radius, C::Border, 1.0f);
	Button.Disabled = FSlateBrush::Rounded(C::Panel, Radius, C::Border, 1.0f);
	Button.Focused = FSlateBrush::Rounded(FColor::Transparent(), Radius, C::Primary, 1.5f);
	Set->Set("Button", Button);

	FButtonStyle Primary = Button;
	Primary.Normal = FSlateBrush::Rounded(C::Primary, Radius);
	Primary.Hovered = FSlateBrush::Rounded(C::PrimaryHover, Radius);
	Primary.Pressed = FSlateBrush::Rounded(C::PrimaryPressed, Radius);
	Primary.NormalForeground = C::ForegroundInverted;
	Primary.HoveredForeground = C::ForegroundInverted;
	Primary.PressedForeground = C::ForegroundInverted;
	Primary.Focused = FSlateBrush::Rounded(FColor::Transparent(), Radius, C::ForegroundHover, 1.5f);
	Set->Set("PrimaryButton", Primary);

	FButtonStyle Flat = Button;
	Flat.Normal = FSlateBrush::None();
	Flat.Hovered = FSlateBrush::Rounded(C::Hover, Radius);
	Flat.Pressed = FSlateBrush::Rounded(C::Pressed, Radius);
	Flat.Disabled = FSlateBrush::None();
	Set->Set("FlatButton", Flat);

	FButtonStyle MenuItem = Flat;
	MenuItem.Hovered = FSlateBrush::Rounded(C::Select, 3.0f);
	MenuItem.Pressed = FSlateBrush::Rounded(C::Select, 3.0f);
	MenuItem.NormalPadding = FMargin(10.0f, 4.0f);
	MenuItem.PressedPadding = MenuItem.NormalPadding;
	MenuItem.Focused = FSlateBrush::Rounded(C::Select, 3.0f);
	Set->Set("MenuItem", MenuItem);

	// Check boxes.
	FCheckBoxStyle CheckBox;
	CheckBox.Box = FSlateBrush::Rounded(C::Input, 3.0f, C::Border, 1.0f);
	CheckBox.BoxHovered = FSlateBrush::Rounded(C::Input, 3.0f, C::ForegroundSecondary, 1.0f);
	CheckBox.BoxChecked = FSlateBrush::Rounded(C::Primary, 3.0f);
	CheckBox.BoxCheckedHovered = FSlateBrush::Rounded(C::PrimaryHover, 3.0f);
	CheckBox.BoxDisabled = FSlateBrush::Rounded(C::Panel, 3.0f, C::Border, 1.0f);
	CheckBox.Focused = FSlateBrush::Rounded(FColor::Transparent(), 4.0f, C::Primary, 1.5f);
	Set->Set("CheckBox", CheckBox);

	// Text editing.
	FEditableTextStyle EditableText;
	EditableText.Font = FSlateFontInfo(Fonts.Regular, FontSize);
	Set->Set("EditableText", EditableText);
	FEditableTextBoxStyle TextBox;
	TextBox.Normal = FSlateBrush::Rounded(C::Input, Radius, C::Border, 1.0f);
	TextBox.Hovered = FSlateBrush::Rounded(C::Input, Radius, C::ForegroundSecondary, 1.0f);
	TextBox.Focused = FSlateBrush::Rounded(C::Input, Radius, C::Primary, 1.0f);
	TextBox.ReadOnly = FSlateBrush::Rounded(C::Panel, Radius, C::Border, 1.0f);
	TextBox.Text = EditableText;
	Set->Set("EditableTextBox", TextBox);

	// Scrolling and splitting.
	FScrollBarStyle ScrollBar;
	ScrollBar.Track = FSlateBrush::None();
	ScrollBar.Thumb = FSlateBrush::Rounded(FSlateColor(C::ForegroundSecondary, 110), 3.0f);
	ScrollBar.ThumbHovered = FSlateBrush::Rounded(FSlateColor(C::ForegroundSecondary, 180), 3.0f);
	ScrollBar.ThumbDragged = FSlateBrush::Rounded(C::ForegroundSecondary, 3.0f);
	Set->Set("ScrollBar", ScrollBar);

	FSplitterStyle Splitter;
	Splitter.Handle = FSlateBrush::Solid(C::Background);
	Splitter.HandleHighlight = FSlateBrush::Solid(C::Primary);
	Set->Set("Splitter", Splitter);

	// Lists and trees.
	FTableRowStyle Row;
	Row.Even = FSlateBrush::None();
	Row.Odd = FSlateBrush::Solid(FSlateColor(C::Panel, 120));
	Row.Hovered = FSlateBrush::Solid(C::SelectHover);
	Row.ActiveSelected = FSlateBrush::Solid(C::Select);
	Row.InactiveSelected = FSlateBrush::Solid(C::SelectInactive);
	Row.SelectedHovered = FSlateBrush::Solid(C::Select);
	Set->Set("TableRow", Row);

	// Feedback.
	FProgressBarStyle Progress;
	Progress.Background = FSlateBrush::Rounded(C::Input, 3.0f);
	Progress.Fill = FSlateBrush::Rounded(C::Primary, 3.0f);
	Set->Set("ProgressBar", Progress);

	FSeparatorStyle Separator;
	Set->Set("Separator", Separator);

	// Value editors.
	FSliderStyle Slider;
	Slider.Bar = FSlateBrush::Rounded(C::Input, 2.0f, C::Border, 1.0f);
	Slider.BarFill = FSlateBrush::Rounded(C::Primary, 2.0f);
	Slider.Thumb = FSlateBrush::Rounded(C::Foreground, 7.0f, C::Background, 1.0f);
	Slider.ThumbHovered = FSlateBrush::Rounded(C::ForegroundHover, 7.0f, C::Primary, 1.5f);
	Slider.Focused = FSlateBrush::Rounded(FColor::Transparent(), 9.0f, C::Primary, 1.5f);
	Set->Set("Slider", Slider);

	FSpinBoxStyle SpinBox;
	SpinBox.Normal = FSlateBrush::Rounded(C::Input, Radius, C::Border, 1.0f);
	SpinBox.Hovered = FSlateBrush::Rounded(C::Input, Radius, C::ForegroundSecondary, 1.0f);
	SpinBox.Active = FSlateBrush::Rounded(C::Input, Radius, C::Primary, 1.0f);
	SpinBox.Fill = FSlateBrush::Rounded(FSlateColor(C::Primary, 70), Radius);
	SpinBox.Text = EditableText;
	Set->Set("SpinBox", SpinBox);

	// Docking.
	FDockTabStyle DockTab;
	DockTab.WellBackground = FSlateBrush::Solid(C::Recessed);
	DockTab.Normal = FSlateBrush::None();
	DockTab.Hovered = FSlateBrush::Solid(C::Hover);
	DockTab.Active = FSlateBrush::Solid(C::Panel);
	DockTab.Foreground = FSlateBrush::Solid(C::Header);
	DockTab.ActiveIndicator = FSlateBrush::Solid(C::Primary);
	DockTab.ContentBackground = FSlateBrush::Solid(C::Panel);
	DockTab.DropTarget = FSlateBrush::Rounded(FSlateColor(C::Primary, 60), 2.0f, C::Primary, 2.0f);
	DockTab.DropZoneIcon = FSlateBrush::Rounded(FSlateColor(C::Dropdown, 235), 4.0f, C::Primary, 1.5f);
	DockTab.Text = Normal;
	DockTab.CloseButton = Flat;
	DockTab.CloseButton.NormalPadding = FMargin(4.0f, 0.0f);
	DockTab.CloseButton.PressedPadding = FMargin(4.0f, 0.0f);
	Set->Set("DockTab", DockTab);

	FToolBarStyle ToolBar;
	ToolBar.Background = FSlateBrush::Solid(C::Header);
	ToolBar.Button = Flat;
	ToolBar.Button.NormalPadding = FMargin(8.0f, 4.0f);
	ToolBar.Button.PressedPadding = FMargin(8.0f, 5.0f, 8.0f, 3.0f);
	ToolBar.ToggleButtonChecked = ToolBar.Button;
	ToolBar.ToggleButtonChecked.Normal = FSlateBrush::Rounded(FSlateColor(C::Primary, 90), Radius, C::Primary, 1.0f);
	ToolBar.ToggleButtonChecked.Hovered = FSlateBrush::Rounded(FSlateColor(C::Primary, 130), Radius, C::Primary, 1.0f);
	ToolBar.LabelText = FTextBlockStyle(Normal).SetFontSize(12.0f);
	Set->Set("ToolBar", ToolBar);

	FPropertyGridStyle PropertyGrid;
	PropertyGrid.CategoryHeader = FSlateBrush::Solid(C::Header);
	PropertyGrid.RowEven = FSlateBrush::None();
	PropertyGrid.RowOdd = FSlateBrush::Solid(FSlateColor(C::Recessed, 160));
	PropertyGrid.RowHovered = FSlateBrush::Solid(C::SelectHover);
	PropertyGrid.CategoryText = FTextBlockStyle(Normal).SetFont(FSlateFontInfo(Fonts.Bold, 12.0f));
	PropertyGrid.NameText = FTextBlockStyle(Normal).SetFontSize(12.0f);
	Set->Set("PropertyGrid", PropertyGrid);

	// Popups.
	FToolTipStyle ToolTip;
	ToolTip.Background = FSlateBrush::Rounded(C::Dropdown, Radius, C::Border, 1.0f);
	ToolTip.Text = FTextBlockStyle(Normal).SetFontSize(12.0f).SetColor(C::ForegroundHover);
	Set->Set("ToolTip", ToolTip);

	FMenuStyle Menu;
	Menu.Background = FSlateBrush::Rounded(C::Dropdown, Radius, C::Border, 1.0f);
	Menu.Item = MenuItem;
	Set->Set("Menu", Menu);

	// Windows.
	FWindowStyle Window;
	Window.TitleBar = FSlateBrush::Solid(C::Header);
	Window.TitleText = FTextBlockStyle(Normal).SetColor(C::ForegroundHover);
	Window.TitleButton = Flat;
	Window.TitleButton.Hovered = FSlateBrush::Solid(C::Hover);
	Window.TitleButton.Pressed = FSlateBrush::Solid(C::Pressed);
	Window.CloseButton = Window.TitleButton;
	Window.CloseButton.Hovered = FSlateBrush::Solid(FColor(196, 43, 28));
	Window.CloseButton.Pressed = FSlateBrush::Solid(FColor(150, 32, 22));
	Window.Outline = FSlateBrush::Rounded(FColor::Transparent(), 0.0f, C::Border, 1.0f);
	Set->Set("Window", Window);

	return Set;
}
