// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// FStyleSet: a named dictionary of style values (brushes, colors, fonts,
// margins, widget style structs). Sets register by name; FAppStyle is the
// one widgets fall back to (docs/GenesisUI.md §8).
//
//   const FButtonStyle& Style = FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
//
// Styles are built in code for now; loading them from data comes with the
// object model.

#pragma once

#include "Styling/WidgetStyles.h"

#include <any>

DECLARE_LOG_CATEGORY_EXTERN(LogStyle, Log);

class FStyleSet
{
public:
	explicit FStyleSet(FName InName) : Name(InName) {}

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
	const FStyleBrush& GetBrush(FName Key) const { return Get<FStyleBrush>(Key); }
	const FFontInfo& GetFont(FName Key) const { return Get<FFontInfo>(Key); }
	FStyleColor GetColor(FName Key) const { return Get<FStyleColor>(Key); }
	FMargin GetMargin(FName Key) const { return Get<FMargin>(Key); }
	float GetFloat(FName Key) const { return Get<float>(Key); }

	bool Contains(FName Key) const { return Values.contains(Key); }

	// Registry.
	static void Register(const TSharedRef<FStyleSet>& Set);
	static void Unregister(FName SetName);
	static TSharedPtr<FStyleSet> FindSet(FName SetName);

private:
	void ReportMissing(FName Key) const;

	FName Name;
	TMap<FName, TSharedPtr<std::any>> Values;
};

// The application's style set.
class FAppStyle
{
public:
	static const FStyleSet& Get();
	static bool IsSet();
	static void SetAppStyleSet(const TSharedRef<FStyleSet>& Set);
	static void Reset();
};
