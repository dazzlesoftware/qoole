// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// UIInputsTest: value inputs. Combo boxes, sliders, spin boxes (integer and
// floating point, with and without a range), and color swatches with the
// color picker; each also disabled.
//
// Self-test: picks a combo box option with the keyboard and with the mouse,
// drags and steps a slider, drags a spin box, and drags a channel of the
// color picker.

#include "TestApp.h"
#include "Containers/ContainerAliases.h"
#include "Templates/SharedPointer.h"
#include "TestWidgets.h"
#include "Widgets/Input/SColorPicker.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Text/STextBlock.h"

class FInputsTestApp final : public FTestApp
{
public:
	FInputsTestApp() : FTestApp({"UIInputsTest", "Genesis UI - Value inputs", FVector2(900.0f, 760.0f)}) {}

protected:
	TSharedRef<SWidget> MakeContent() override
	{
		Games = {"Genesis", "Quake", "Half-Life", "Heretic II", "Sin"};
		for (int32 I = 1; I <= 40; ++I)
		{
			Textures.push_back(FString::Format("texture_{:02}", I));
		}

		const TSharedRef<SGridPanel> Form = SNew(SGridPanel);
		Form->SetColumnFill(1, 1.0f);
		int32 Row = 0;
		const TFunction<void(const char*, const TSharedRef<SWidget>&)> AddRow =
		    [&Form, &Row](const char* Label, const TSharedRef<SWidget>& Field)
		{
			Form->AddSlot(0, Row)
			    .VAlign(VAlign_Center)
			    .Padding(FMargin(0.0f, 4.0f, 12.0f, 4.0f))[SNew(STextBlock).Text(Label)];
			Form->AddSlot(1, Row).Padding(FMargin(0.0f, 4.0f)).HAlign(HAlign_Left)[Field];
			++Row;
		};
		AddRow("Game",
		    SNew(SBox).WidthOverride(TOptional<float>(220.0f))[SAssignNew(GameCombo, SComboBox<FString>)
		            .OptionsSource(&Games)
		            .InitiallySelectedItem(TOptional<FString>(Games[0]))
		            .OnGenerateWidget([](FString Item) -> TSharedRef<SWidget> {
			return SNew(STextBlock).Text(Item);
		}).OnSelectionChanged([this](FString Item, ESelectInfo) {
			SetLastEvent(FString::Format("Game: {}", Item));
		})[SNew(STextBlock).Text([this] {
			return GameCombo ? GameCombo->GetSelectedItem().value_or("(none)") : FString();
		})]]);
		AddRow("Texture (long list)",
		    SNew(SBox).WidthOverride(TOptional<float>(220.0f))[SAssignNew(TextureCombo, SComboBox<FString>)
		            .OptionsSource(&Textures)
		            .MaxListHeight(200.0f)
		            .OnGenerateWidget([](FString Item) -> TSharedRef<SWidget> {
			return SNew(STextBlock).Text(Item);
		}).OnSelectionChanged([this](FString Item, ESelectInfo) {
			SetLastEvent(FString::Format("Texture: {}", Item));
		})[SNew(STextBlock).Text([this] {
			return TextureCombo ? TextureCombo->GetSelectedItem().value_or("(pick one)") : FString();
		})]]);
		AddRow("Disabled",
		    SNew(SBox).WidthOverride(TOptional<float>(220.0f))[SNew(SComboBox<FString>)
		            .IsEnabled(false)
		            .OptionsSource(&Games)
		            .OnGenerateWidget([](FString Item) -> TSharedRef<SWidget>
		{ return SNew(STextBlock).Text(Item); })[SNew(STextBlock).Text("Genesis")]]);

		const TSharedRef<SGridPanel> Numbers = SNew(SGridPanel);
		Numbers->SetColumnFill(1, 1.0f);
		Row = 0;
		const TFunction<void(const char*, const TSharedRef<SWidget>&, TAttribute<FString>)> AddNumber =
		    [&Numbers, &Row](const char* Label, const TSharedRef<SWidget>& Field, TAttribute<FString> Value)
		{
			Numbers->AddSlot(0, Row)
			    .VAlign(VAlign_Center)
			    .Padding(FMargin(0.0f, 4.0f, 12.0f, 4.0f))[SNew(STextBlock).Text(Label)];
			Numbers->AddSlot(1, Row).VAlign(VAlign_Center).Padding(FMargin(0.0f, 4.0f))[Field];
			Numbers->AddSlot(2, Row)
			    .VAlign(VAlign_Center)
			    .Padding(FMargin(12.0f, 4.0f, 0.0f, 4.0f))[SNew(SBox).WidthOverride(
			        TOptional<float>(80.0f))[SNew(STextBlock).Text(Value).TextStyle(&TestTextStyle("SmallText"))]];
			++Row;
		};
		AddNumber("Brightness",
		    SAssignNew(Slider, SSlider)
		        .Value([this] { return Brightness; })
		        .StepSize(0.05f)
		        .OnValueChanged(
		            [this](float Value)
		{
			Brightness = Value;
			SetLastEvent(FString::Format("Brightness {:.2f}", Value));
		}),
		    TAttribute<FString>([this] { return FString::Format("{:.2f}", Brightness); }));
		AddNumber("Volume (tinted)",
		    SNew(SSlider)
		        .Value([this] { return Volume; })
		        .BarColor(FSlateColor(EStyleColor::Success))
		        .OnValueChanged([this](float Value) { Volume = Value; }),
		    TAttribute<FString>([this] { return FString::Format("{:.2f}", Volume); }));
		AddNumber("Disabled slider", SNew(SSlider).Value(0.3f).IsEnabled(false), FString("0.30"));
		AddNumber("Grid size (int, 1-256)",
		    SNew(SBox).WidthOverride(TOptional<float>(160.0f))[SAssignNew(GridSpin, SSpinBox<int32>)
		            .Value([this] { return TOptional<int32>(GridSize); })
		            .MinValue(1)
		            .MaxValue(256)
		            .OnValueChanged([this](int32 Value) { GridSize = Value; })
		            .OnValueCommitted(
		                [this](int32 Value, ETextCommit)
		{
			GridSize = Value;
			SetLastEvent(FString::Format("Grid size {}", Value));
		})],
		    TAttribute<FString>([this] { return FString::Format("{}", GridSize); }));
		AddNumber("Scale (float, no range)",
		    SNew(SBox).WidthOverride(TOptional<float>(160.0f))[SNew(SSpinBox<float>)
		            .Value([this] { return TOptional<float>(Scale); })
		            .FractionalDigits(2)
		            .OnValueChanged([this](float Value) { Scale = Value; })],
		    TAttribute<FString>([this] { return FString::Format("{:.2f}", Scale); }));
		AddNumber("Mixed values",
		    SNew(SBox).WidthOverride(TOptional<float>(160.0f))[SNew(SSpinBox<float>).Value(TOptional<float>())],
		    FString("undetermined"));
		AddNumber("Disabled spin box",
		    SNew(SBox).WidthOverride(
		        TOptional<float>(160.0f))[SNew(SSpinBox<int32>).Value(TOptional<int32>(8)).IsEnabled(false)],
		    FString("8"));

		return TestFrame(
		    *this, "Value inputs",
		    TestPage({
		        TestSection("Combo boxes",
		                    "Click to open; arrows move through the list, Enter or a click picks, Escape closes.",
		                    Form),
		        TestSection("Sliders and spin boxes",
		                    "Drag a slider or click its bar; Left/Right step it while it has focus. Drag a spin box "
		                    "sideways (Shift: faster, Ctrl: slower), or click it to type.",
		                    Numbers),
		        TestSection(
		            "Colors", "A swatch shows a color; the picker edits one channel at a time or as hex.",
		            SNew(SHorizontalBox) +
		                SHorizontalBox::Slot()
		                    .AutoWidth()
		                    .VAlign(VAlign_Top)
		                    .Padding(FMargin(0.0f, 0.0f, 16.0f, 0.0f))
		                        [SNew(SVerticalBox) +
		                         SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 2.0f))
		                             [SNew(SColorBlock).Color([this] { return Color; }).Size(FVector2(64.0f, 32.0f))] +
		                         SVerticalBox::Slot().AutoHeight().Padding(FMargin(
		                             0.0f, 2.0f))[SNew(SColorBlock).IsUndetermined(true).Size(FVector2(64.0f, 32.0f))] +
		                         SVerticalBox::Slot().AutoHeight().Padding(FMargin(
		                             0.0f, 2.0f))[SNew(STextBlock)
		                                              .TextStyle(&TestTextStyle("SmallText"))
		                                              .Text([this] { return SColorPicker::ToHex(Color, true); })]] +
		                SHorizontalBox::Slot().FillWidth(1.0f)[SAssignNew(Picker, SColorPicker)
		                                                           .TargetColor([this] { return Color; })
		                                                           .OnColorChanged(
		                                                               [this](FColor NewColor)
		                                                               {
			                                                               Color = NewColor;
			                                                               SetLastEvent(FString::Format(
			                                                                   "Color {}",
			                                                                   SColorPicker::ToHex(NewColor, true)));
		                                                               })]),
		    }));
	}

	void BuildSelfTest(FTestScript& Script) override
	{
		Script
		    .Capture("start")
		    // The game combo: open it, step down with the keyboard, Enter.
		    .Do([this] { Click(GetCenter(*GameCombo)); })
		    .Check("the combo box opens", [this] { return GameCombo->IsOpen(); }, 5)
		    .Capture("combo_open")
		    .Do(
		        [this]
		{
			PressKey(EKey::Down);
			PressKey(EKey::Down);
		})
		    .Do([this] { PressKey(EKey::Enter); })
		    .Check("Enter picks the third game",
		        [this] { return !GameCombo->IsOpen() && GameCombo->GetSelectedItem() == FString("Half-Life"); })
		    // The texture combo: open it and click a row.
		    .Do([this] { Click(GetCenter(*TextureCombo)); })
		    .Capture("long_list", 10)
		    .Do([this] { PointAtText("texture_03", true); })
		    .Check("a click picks the texture",
		        [this] { return !TextureCombo->IsOpen() && TextureCombo->GetSelectedItem() == FString("texture_03"); })
		    // The slider: drag from the left edge to three quarters.
		    .Do(
		        [this]
		{
			const FRect Bar = Slider->GetCachedGeometry().GetLayoutBoundingRect();
			Drag(FVector2(Bar.Left + 2.0f, Bar.GetCenter().Y),
			    FVector2(Bar.Left + Bar.Width() * 0.75f, Bar.GetCenter().Y));
		})
		    .Check("the slider follows the drag", [this] { return FMath::Abs(Brightness - 0.75f) < 0.05f; })
		    .Do(
		        [this]
		{
			PressKey(EKey::Right);
			PressKey(EKey::Right);
		})
		    .Check("Right steps the focused slider", [this] { return Brightness > 0.79f; })
		    // The grid spin box: drag it to the right.
		    .Do(
		        [this]
		{
			StartGridSize = GridSize;
			const FVector2 Center = GetCenter(*GridSpin);
			Drag(Center, Center + FVector2(120.0f, 0.0f));
		})
		    .Check("dragging a spin box raises it", [this] { return GridSize > StartGridSize && GridSize <= 256; })
		    // The color picker: drag its first channel (red) to zero.
		    .Do([this]
		{
			StartColor = Color;
			const SSlider* Red = FindWidget<SSlider>(*Picker);
			if (Expect(Red != nullptr, "the color picker has channel sliders"))
			{
				const FRect Bar = Red->GetCachedGeometry().GetLayoutBoundingRect();
				Drag(FVector2(Bar.GetCenter().X, Bar.GetCenter().Y), FVector2(Bar.Left - 20.0f, Bar.GetCenter().Y));
			}
		}).Check("the picker's red channel goes to zero", [this] {
			return Color.R == 0 && Color.G == StartColor.G;
		}).Capture("edited");
	}

private:
	TArray<FString> Games;
	TArray<FString> Textures;
	TSharedPtr<SComboBox<FString>> GameCombo;
	TSharedPtr<SComboBox<FString>> TextureCombo;
	TSharedPtr<SSlider> Slider;
	TSharedPtr<SSpinBox<int32>> GridSpin;
	TSharedPtr<SColorPicker> Picker;
	float Brightness = 0.25f;
	float Volume = 0.6f;
	int32 GridSize = 16;
	int32 StartGridSize = 0;
	float Scale = 1.0f;
	FColor Color = FColor(232, 140, 64);
	FColor StartColor;
};

IMPLEMENT_TEST_APP(FInputsTestApp)
