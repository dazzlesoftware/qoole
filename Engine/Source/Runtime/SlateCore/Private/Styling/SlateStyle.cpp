// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Styling/SlateStyle.h"

DEFINE_LOG_CATEGORY(LogStyle, Log);

namespace
{
	FStyleTheme& ActivePalette()
	{
		static FStyleTheme Palette = FStyleTheme::MakeDark();
		return Palette;
	}

	TMap<FName, TSharedPtr<FSlateStyleSet>>& Registry()
	{
		static TMap<FName, TSharedPtr<FSlateStyleSet>> Sets;
		return Sets;
	}

	TSharedPtr<FSlateStyleSet>& AppStyleSet()
	{
		static TSharedPtr<FSlateStyleSet> Set;
		return Set;
	}
}

const char* ToString(EStyleColor Color)
{
	static const char* const Names[] = {
		"Background", "Recessed", "Panel", "Header", "Input", "Dropdown", "Hover", "Pressed", "Border",
		"Foreground", "ForegroundHover", "ForegroundSecondary", "ForegroundDisabled", "ForegroundInverted",
		"Primary", "PrimaryHover", "PrimaryPressed", "Select", "SelectInactive", "SelectHover",
		"Error", "Warning", "Success", "White", "Black", "Transparent",
	};
	static_assert(std::size(Names) == static_cast<SIZE_T>(EStyleColor::Count));
	return Color < EStyleColor::Count ? Names[static_cast<int32>(Color)] : "?";
}

FStyleTheme::FStyleTheme()
{
	Colors.fill(FColor::White());
	Set(EStyleColor::Black, FColor::Black());
	Set(EStyleColor::Transparent, FColor::Transparent());
}

FStyleTheme FStyleTheme::MakeDark()
{
	FStyleTheme P;
	P.Set(EStyleColor::Background, FColor(24, 26, 31));
	P.Set(EStyleColor::Recessed, FColor(28, 30, 36));
	P.Set(EStyleColor::Panel, FColor(36, 39, 46));
	P.Set(EStyleColor::Header, FColor(44, 48, 57));
	P.Set(EStyleColor::Input, FColor(20, 22, 26));
	P.Set(EStyleColor::Dropdown, FColor(46, 50, 59));
	P.Set(EStyleColor::Hover, FColor(60, 65, 77));
	P.Set(EStyleColor::Pressed, FColor(32, 35, 42));
	P.Set(EStyleColor::Border, FColor(62, 67, 79));
	P.Set(EStyleColor::Foreground, FColor(204, 208, 218));
	P.Set(EStyleColor::ForegroundHover, FColor(242, 244, 248));
	P.Set(EStyleColor::ForegroundSecondary, FColor(138, 145, 160));
	P.Set(EStyleColor::ForegroundDisabled, FColor(100, 106, 118));
	P.Set(EStyleColor::ForegroundInverted, FColor(16, 18, 22));
	P.Set(EStyleColor::Primary, FColor(86, 156, 255));
	P.Set(EStyleColor::PrimaryHover, FColor(116, 176, 255));
	P.Set(EStyleColor::PrimaryPressed, FColor(62, 126, 222));
	P.Set(EStyleColor::Select, FColor(48, 98, 176));
	P.Set(EStyleColor::SelectInactive, FColor(60, 66, 80));
	P.Set(EStyleColor::SelectHover, FColor(46, 51, 62));
	P.Set(EStyleColor::Error, FColor(232, 98, 120));
	P.Set(EStyleColor::Warning, FColor(255, 166, 77));
	P.Set(EStyleColor::Success, FColor(112, 214, 140));
	P.Set(EStyleColor::White, FColor::White());
	return P;
}

const FStyleTheme& FStyleTheme::GetActive()
{
	return ActivePalette();
}

void FStyleTheme::SetActive(const FStyleTheme& Palette)
{
	ActivePalette() = Palette;
}

FVector2 FSlateBrush::GetImageSize() const
{
	if (ImageSize.X > 0.0f || ImageSize.Y > 0.0f)
	{
		return ImageSize;
	}
	return Image ? Image->GetSize().ToVector2() : FVector2::Zero();
}

void FSlateBrush::Paint(FSlateWindowElementList& Out, int32 Layer, const FPaintGeometry& Geometry, FColor Tint) const
{
	const auto Multiply = [](FColor A, FColor B)
	{
		return FColor(uint8((A.R * B.R + 127) / 255), uint8((A.G * B.G + 127) / 255), uint8((A.B * B.B + 127) / 255), uint8((A.A * B.A + 127) / 255));
	};
	const FColor FillColor = Multiply(Fill.Resolve(), Tint);
	switch (Type)
	{
	case ESlateBrushDrawType::None:
		break;
	case ESlateBrushDrawType::Solid:
		Out.AddSolidBox(Layer, Geometry, FillColor);
		break;
	case ESlateBrushDrawType::RoundedBox:
		Out.AddRoundedBox(Layer, Geometry, FillColor, CornerRadii, Multiply(Outline.Resolve(), Tint), OutlineWidth);
		break;
	case ESlateBrushDrawType::Image:
		Out.AddBox(Layer, Geometry, FBrush::FromImage(Image, FillColor));
		break;
	case ESlateBrushDrawType::NineSlice:
		Out.AddBox(Layer, Geometry, FBrush::NineSlice(Image, Margin, FillColor));
		break;
	}
}

void FSlateStyleSet::ReportMissing(FName Key) const
{
	GE_LOG(LogStyle, Warning, "Style set \"{}\" has no value \"{}\" of the requested type", Name, Key);
}

void FSlateStyleSet::Register(const TSharedRef<FSlateStyleSet>& Set)
{
	Registry()[Set->GetName()] = Set.ToSharedPtr();
}

void FSlateStyleSet::Unregister(FName SetName)
{
	Registry().erase(SetName);
}

TSharedPtr<FSlateStyleSet> FSlateStyleSet::FindSet(FName SetName)
{
	auto It = Registry().find(SetName);
	return It == Registry().end() ? nullptr : It->second;
}

const FSlateStyleSet& FAppStyle::Get()
{
	if (!AppStyleSet())
	{
		// Widgets used before a style is set get defaults (and warnings).
		static const FSlateStyleSet Empty(FName("Empty"));
		return Empty;
	}
	return *AppStyleSet();
}

bool FAppStyle::IsSet()
{
	return AppStyleSet() != nullptr;
}

void FAppStyle::SetAppStyleSet(const TSharedRef<FSlateStyleSet>& Set)
{
	AppStyleSet() = Set.ToSharedPtr();
}

void FAppStyle::Reset()
{
	AppStyleSet().reset();
}
