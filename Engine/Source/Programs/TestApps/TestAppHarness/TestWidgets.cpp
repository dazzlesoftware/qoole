// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "TestWidgets.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

const FTextBlockStyle& TestTextStyle(const char* Name)
{
	return FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>(Name);
}

const FButtonStyle& TestButtonStyle(const char* Name)
{
	return FAppStyle::Get().GetWidgetStyle<FButtonStyle>(Name);
}

TSharedRef<SWidget> TestSection(const FString& Title, const FString& Description, const TSharedRef<SWidget>& Content)
{
	return SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Panel")).Padding(FMargin(14.0f, 10.0f, 14.0f, 14.0f))
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Title).TextStyle(&TestTextStyle("BoldText"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 2.0f, 0.0f, 10.0f))[SNew(STextBlock).Text(Description).TextStyle(&TestTextStyle("SmallText")).AutoWrapText(true)]
		+ SVerticalBox::Slot().AutoHeight()[Content]
	];
}

TSharedRef<SScrollBox> TestPage(std::initializer_list<TSharedRef<SWidget>> Sections)
{
	const TSharedRef<SScrollBox> Page = SNew(SScrollBox);
	for (const TSharedRef<SWidget>& Section : Sections)
	{
		Page->AddSlot().Padding(FMargin(16.0f, 16.0f, 16.0f, 0.0f))[Section];
	}
	Page->AddSlot().Padding(FMargin(0.0f, 16.0f, 0.0f, 0.0f))[SNew(SSpacer)];
	return Page;
}

TSharedRef<SWidget> TestFrame(FTestApp& App, const FString& Title, const TSharedRef<SWidget>& Content)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Header")).Padding(FMargin(12.0f, 6.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 16.0f, 0.0f))[SNew(STextBlock).Text(Title).TextStyle(&TestTextStyle("HeadingText"))]
				+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right).VAlign(VAlign_Center)
				[
					SNew(STextBlock).TextStyle(&TestTextStyle("SmallText")).Text([&App] { return App.GetStatusLine(); })
				]
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f)[Content]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Header")).Padding(FMargin(12.0f, 4.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(STextBlock).TextStyle(&TestTextStyle("SmallText")).Text([&App] { return App.GetLastEvent(); })]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).TextStyle(&TestTextStyle("SmallText")).Text([]
				{
					const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
					return FString::Format("Focus: {}   ·   F2 app scale, F3 DPI", Focused ? Focused->GetTypeName() : FString("none"));
				})]
			]
		];
}
