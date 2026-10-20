// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// FSlateStyleSet: a named dictionary of style values (brushes, colors, fonts,
// margins, widget style structs). Sets register by name; FAppStyle is the
// one widgets fall back to (docs/GenesisUI.md §8).
//
//   const FButtonStyle& Style = FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
//
// Styles are built in code for now; loading them from data comes with the
// object model.

#pragma once

#include "Styling/SlateTypes.h"

#include <any>

DECLARE_LOG_CATEGORY_EXTERN(LogStyle, Log);

class FSlateStyleSet
{
public:
	explicit FSlateStyleSet(FName InName) : Name(InName) {}

	FName GetName() const { return Name; }

	template <typename T>
	void Set(FName Key, T Value)
	{
		Values[Key] = std::make_shared<std::any>(std::move(Value));
	}

	template <typename T>
	const T* Find(FName Key) const
	{
		auto It = Values.find(Key);
		return It == Values.end() ? nullptr : std::any_cast<T>(It->second.get());
	}

	// The value, or a default-constructed one (with a warning) when missing.
	template <typename T>
	const T& Get(FName Key) const
	{
		if (const T* Value = Find<T>(Key))
		{
			return *Value;
		}
		ReportMissing(Key);
		static const T Default{};
		return Default;
	}

	template <typename T>
	const T& GetWidgetStyle(FName Key) const { return Get<T>(Key); }
	const FSlateBrush& GetBrush(FName Key) const { return Get<FSlateBrush>(Key); }
	const FSlateFontInfo& GetFont(FName Key) const { return Get<FSlateFontInfo>(Key); }
	FSlateColor GetColor(FName Key) const { return Get<FSlateColor>(Key); }
	FMargin GetMargin(FName Key) const { return Get<FMargin>(Key); }
	float GetFloat(FName Key) const { return Get<float>(Key); }

	bool Contains(FName Key) const { return Values.contains(Key); }

	// Registry.
	static void Register(const TSharedRef<FSlateStyleSet>& Set);
	static void Unregister(FName SetName);
	static TSharedPtr<FSlateStyleSet> FindSet(FName SetName);

private:
	void ReportMissing(FName Key) const;

	FName Name;
	TMap<FName, TSharedPtr<std::any>> Values;
};

// The application's style set.
class FAppStyle
{
public:
	static const FSlateStyleSet& Get();
	static bool IsSet();
	static void SetAppStyleSet(const TSharedRef<FSlateStyleSet>& Set);
	static void Reset();
};
