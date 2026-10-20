// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Styling/GenesisStyle.h"

#include "HAL/PlatformMisc.h"

FGenesisFonts FGenesisFonts::Load(FFontCache& FontCache, FString FontDirectory)
{
	if (FontDirectory.IsEmpty())
	{
		FontDirectory = FPaths::FindContentDir() + "Slate/Fonts/";
	}
	FGenesisFonts Fonts;
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

TSharedRef<FStyleSet> FGenesisStyle::Create(const FGenesisFonts& Fonts)
{
	using C = EStyleColor;
	auto Set = MakeShared<FStyleSet>(FName(SetName));
	constexpr float Radius = 4.0f;
	constexpr float FontSize = 13.0f;

	// Fonts and text.
	Set->Set("NormalFont", FFontInfo(Fonts.Regular, FontSize));
	Set->Set("BoldFont", FFontInfo(Fonts.Bold, FontSize));
	FTextBlockStyle Normal;
	Normal.Font = FFontInfo(Fonts.Regular, FontSize);
	Set->Set("NormalText", Normal);
	Set->Set("SmallText", FTextBlockStyle(Normal).SetFontSize(11.0f).SetColor(C::ForegroundSecondary));
	Set->Set("BoldText", FTextBlockStyle(Normal).SetFont(FFontInfo(Fonts.Bold, FontSize)));
	Set->Set("HeadingText", FTextBlockStyle(Normal).SetFont(FFontInfo(Fonts.Bold, 18.0f)).SetColor(C::ForegroundHover));

	// Brushes.
	Set->Set("WhiteBrush", FStyleBrush::Solid(FColor::White()));
	Set->Set("Border", FStyleBrush::Solid(C::Panel));
	Set->Set("Panel", FStyleBrush::Rounded(C::Panel, Radius, C::Border, 1.0f));
	Set->Set("Recessed", FStyleBrush::Rounded(C::Recessed, Radius, C::Border, 1.0f));
	Set->Set("Header", FStyleBrush::Solid(C::Header));
	Set->Set("FocusRing", FStyleBrush::Rounded(FColor::Transparent(), Radius, C::Primary, 1.5f));
	Set->Set("DropIndicator", FStyleBrush::Solid(C::Primary));
	Set->Set("DragDecorator", FStyleBrush::Rounded(FStyleColor(C::Dropdown, 230), Radius, C::Primary, 1.0f));

	// Buttons.
	FButtonStyle Button;
	Button.Normal = FStyleBrush::Rounded(C::Header, Radius, C::Border, 1.0f);
	Button.Hovered = FStyleBrush::Rounded(C::Hover, Radius, C::Border, 1.0f);
	Button.Pressed = FStyleBrush::Rounded(C::Pressed, Radius, C::Border, 1.0f);
	Button.Disabled = FStyleBrush::Rounded(C::Panel, Radius, C::Border, 1.0f);
	Button.Focused = FStyleBrush::Rounded(FColor::Transparent(), Radius, C::Primary, 1.5f);
	Set->Set("Button", Button);

	FButtonStyle Primary = Button;
	Primary.Normal = FStyleBrush::Rounded(C::Primary, Radius);
	Primary.Hovered = FStyleBrush::Rounded(C::PrimaryHover, Radius);
	Primary.Pressed = FStyleBrush::Rounded(C::PrimaryPressed, Radius);
	Primary.NormalForeground = C::ForegroundInverted;
	Primary.HoveredForeground = C::ForegroundInverted;
	Primary.PressedForeground = C::ForegroundInverted;
	Primary.Focused = FStyleBrush::Rounded(FColor::Transparent(), Radius, C::ForegroundHover, 1.5f);
	Set->Set("PrimaryButton", Primary);

	FButtonStyle Flat = Button;
	Flat.Normal = FStyleBrush::None();
	Flat.Hovered = FStyleBrush::Rounded(C::Hover, Radius);
	Flat.Pressed = FStyleBrush::Rounded(C::Pressed, Radius);
	Flat.Disabled = FStyleBrush::None();
	Set->Set("FlatButton", Flat);

	FButtonStyle MenuItem = Flat;
	MenuItem.Hovered = FStyleBrush::Rounded(C::Select, 3.0f);
	MenuItem.Pressed = FStyleBrush::Rounded(C::Select, 3.0f);
	MenuItem.NormalPadding = FMargin(10.0f, 4.0f);
	MenuItem.PressedPadding = MenuItem.NormalPadding;
	MenuItem.Focused = FStyleBrush::Rounded(C::Select, 3.0f);
	Set->Set("MenuItem", MenuItem);

	// Check boxes.
	FCheckBoxStyle CheckBox;
	CheckBox.Box = FStyleBrush::Rounded(C::Input, 3.0f, C::Border, 1.0f);
	CheckBox.BoxHovered = FStyleBrush::Rounded(C::Input, 3.0f, C::ForegroundSecondary, 1.0f);
	CheckBox.BoxChecked = FStyleBrush::Rounded(C::Primary, 3.0f);
	CheckBox.BoxCheckedHovered = FStyleBrush::Rounded(C::PrimaryHover, 3.0f);
	CheckBox.BoxDisabled = FStyleBrush::Rounded(C::Panel, 3.0f, C::Border, 1.0f);
	CheckBox.Focused = FStyleBrush::Rounded(FColor::Transparent(), 4.0f, C::Primary, 1.5f);
	Set->Set("CheckBox", CheckBox);

	// Text editing.
	FEditableTextStyle EditableText;
	EditableText.Font = FFontInfo(Fonts.Regular, FontSize);
	Set->Set("EditableText", EditableText);
	FEditableTextBoxStyle TextBox;
	TextBox.Normal = FStyleBrush::Rounded(C::Input, Radius, C::Border, 1.0f);
	TextBox.Hovered = FStyleBrush::Rounded(C::Input, Radius, C::ForegroundSecondary, 1.0f);
	TextBox.Focused = FStyleBrush::Rounded(C::Input, Radius, C::Primary, 1.0f);
	TextBox.ReadOnly = FStyleBrush::Rounded(C::Panel, Radius, C::Border, 1.0f);
	TextBox.Text = EditableText;
	Set->Set("EditableTextBox", TextBox);

	// Scrolling and splitting.
	FScrollBarStyle ScrollBar;
	ScrollBar.Track = FStyleBrush::None();
	ScrollBar.Thumb = FStyleBrush::Rounded(FStyleColor(C::ForegroundSecondary, 110), 3.0f);
	ScrollBar.ThumbHovered = FStyleBrush::Rounded(FStyleColor(C::ForegroundSecondary, 180), 3.0f);
	ScrollBar.ThumbDragged = FStyleBrush::Rounded(C::ForegroundSecondary, 3.0f);
	Set->Set("ScrollBar", ScrollBar);

	FSplitterStyle Splitter;
	Splitter.Handle = FStyleBrush::Solid(C::Background);
	Splitter.HandleHighlight = FStyleBrush::Solid(C::Primary);
	Set->Set("Splitter", Splitter);

	// Lists and trees.
	FTableRowStyle Row;
	Row.Even = FStyleBrush::None();
	Row.Odd = FStyleBrush::Solid(FStyleColor(C::Panel, 120));
	Row.Hovered = FStyleBrush::Solid(C::SelectHover);
	Row.ActiveSelected = FStyleBrush::Solid(C::Select);
	Row.InactiveSelected = FStyleBrush::Solid(C::SelectInactive);
	Row.SelectedHovered = FStyleBrush::Solid(C::Select);
	Set->Set("TableRow", Row);

	// Feedback.
	FProgressBarStyle Progress;
	Progress.Background = FStyleBrush::Rounded(C::Input, 3.0f);
	Progress.Fill = FStyleBrush::Rounded(C::Primary, 3.0f);
	Set->Set("ProgressBar", Progress);

	FSeparatorStyle Separator;
	Set->Set("Separator", Separator);

	// Value editors.
	FSliderStyle Slider;
	Slider.Bar = FStyleBrush::Rounded(C::Input, 2.0f, C::Border, 1.0f);
	Slider.BarFill = FStyleBrush::Rounded(C::Primary, 2.0f);
	Slider.Thumb = FStyleBrush::Rounded(C::Foreground, 7.0f, C::Background, 1.0f);
	Slider.ThumbHovered = FStyleBrush::Rounded(C::ForegroundHover, 7.0f, C::Primary, 1.5f);
	Slider.Focused = FStyleBrush::Rounded(FColor::Transparent(), 9.0f, C::Primary, 1.5f);
	Set->Set("Slider", Slider);

	FSpinBoxStyle SpinBox;
	SpinBox.Normal = FStyleBrush::Rounded(C::Input, Radius, C::Border, 1.0f);
	SpinBox.Hovered = FStyleBrush::Rounded(C::Input, Radius, C::ForegroundSecondary, 1.0f);
	SpinBox.Active = FStyleBrush::Rounded(C::Input, Radius, C::Primary, 1.0f);
	SpinBox.Fill = FStyleBrush::Rounded(FStyleColor(C::Primary, 70), Radius);
	SpinBox.Text = EditableText;
	Set->Set("SpinBox", SpinBox);

	// Docking.
	FDockTabStyle DockTab;
	DockTab.WellBackground = FStyleBrush::Solid(C::Recessed);
	DockTab.Normal = FStyleBrush::None();
	DockTab.Hovered = FStyleBrush::Solid(C::Hover);
	DockTab.Active = FStyleBrush::Solid(C::Panel);
	DockTab.Foreground = FStyleBrush::Solid(C::Header);
	DockTab.ActiveIndicator = FStyleBrush::Solid(C::Primary);
	DockTab.ContentBackground = FStyleBrush::Solid(C::Panel);
	DockTab.DropTarget = FStyleBrush::Rounded(FStyleColor(C::Primary, 60), 2.0f, C::Primary, 2.0f);
	DockTab.DropZoneIcon = FStyleBrush::Rounded(FStyleColor(C::Dropdown, 235), 4.0f, C::Primary, 1.5f);
	DockTab.Text = Normal;
	DockTab.CloseButton = Flat;
	DockTab.CloseButton.NormalPadding = FMargin(4.0f, 0.0f);
	DockTab.CloseButton.PressedPadding = FMargin(4.0f, 0.0f);
	Set->Set("DockTab", DockTab);

	FToolBarStyle ToolBar;
	ToolBar.Background = FStyleBrush::Solid(C::Header);
	ToolBar.Button = Flat;
	ToolBar.Button.NormalPadding = FMargin(8.0f, 4.0f);
	ToolBar.Button.PressedPadding = FMargin(8.0f, 5.0f, 8.0f, 3.0f);
	ToolBar.ToggleButtonChecked = ToolBar.Button;
	ToolBar.ToggleButtonChecked.Normal = FStyleBrush::Rounded(FStyleColor(C::Primary, 90), Radius, C::Primary, 1.0f);
	ToolBar.ToggleButtonChecked.Hovered = FStyleBrush::Rounded(FStyleColor(C::Primary, 130), Radius, C::Primary, 1.0f);
	ToolBar.LabelText = FTextBlockStyle(Normal).SetFontSize(12.0f);
	Set->Set("ToolBar", ToolBar);

	FPropertyGridStyle PropertyGrid;
	PropertyGrid.CategoryHeader = FStyleBrush::Solid(C::Header);
	PropertyGrid.RowEven = FStyleBrush::None();
	PropertyGrid.RowOdd = FStyleBrush::Solid(FStyleColor(C::Recessed, 160));
	PropertyGrid.RowHovered = FStyleBrush::Solid(C::SelectHover);
	PropertyGrid.CategoryText = FTextBlockStyle(Normal).SetFont(FFontInfo(Fonts.Bold, 12.0f));
	PropertyGrid.NameText = FTextBlockStyle(Normal).SetFontSize(12.0f);
	Set->Set("PropertyGrid", PropertyGrid);

	// Popups.
	FToolTipStyle ToolTip;
	ToolTip.Background = FStyleBrush::Rounded(C::Dropdown, Radius, C::Border, 1.0f);
	ToolTip.Text = FTextBlockStyle(Normal).SetFontSize(12.0f).SetColor(C::ForegroundHover);
	Set->Set("ToolTip", ToolTip);

	FMenuStyle Menu;
	Menu.Background = FStyleBrush::Rounded(C::Dropdown, Radius, C::Border, 1.0f);
	Menu.Item = MenuItem;
	Set->Set("Menu", Menu);

	// Windows.
	FWindowStyle Window;
	Window.TitleBar = FStyleBrush::Solid(C::Header);
	Window.TitleText = FTextBlockStyle(Normal).SetColor(C::ForegroundHover);
	Window.TitleButton = Flat;
	Window.TitleButton.Hovered = FStyleBrush::Solid(C::Hover);
	Window.TitleButton.Pressed = FStyleBrush::Solid(C::Pressed);
	Window.CloseButton = Window.TitleButton;
	Window.CloseButton.Hovered = FStyleBrush::Solid(FColor(196, 43, 28));
	Window.CloseButton.Pressed = FStyleBrush::Solid(FColor(150, 32, 22));
	Window.Outline = FStyleBrush::Rounded(FColor::Transparent(), 0.0f, C::Border, 1.0f);
	Set->Set("Window", Window);

	return Set;
}
