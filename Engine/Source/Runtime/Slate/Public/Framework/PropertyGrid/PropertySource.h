// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// What the property grid edits (Engine/Documentation/Source/GenesisUI.md §9). Until the object
// model exists (§10) the editor describes each selected object by hand with
// an IPropertySource; later the reflection system generates them.

#pragma once

#include "CoreMinimal.h"

#include <variant>

struct FPropertyVector3
{
	float X = 0.0f;
	float Y = 0.0f;
	float Z = 0.0f;
	bool operator==(const FPropertyVector3&) const = default;
};

enum class EPropertyType : uint8
{
	Bool,
	Int,
	Float,
	String,
	Color,   // FColor (8-bit sRGB)
	Enum,    // int32 index into FPropertyDesc::EnumOptions
	Vector2,
	Vector3,
};

using FPropertyValue = std::variant<bool, int32, float, FString, FColor, FVector2, FPropertyVector3>;

struct FPropertyDesc
{
	FName Name;
	FString DisplayName;
	FString Category = "General";
	FString ToolTip;
	EPropertyType Type = EPropertyType::Float;
	TArray<FString> EnumOptions;
	TOptional<float> Min; // numbers and vector components
	TOptional<float> Max;
	bool bReadOnly = false;

	FPropertyDesc() = default;
	FPropertyDesc(FName InName, EPropertyType InType, FString InDisplayName, FString InCategory = "General")
		: Name(InName), DisplayName(std::move(InDisplayName)), Category(std::move(InCategory)), Type(InType)
	{
	}
	FPropertyDesc& SetRange(float InMin, float InMax) { Min = InMin; Max = InMax; return *this; }
	FPropertyDesc& SetOptions(TArray<FString> Options) { EnumOptions = std::move(Options); return *this; }
	FPropertyDesc& SetToolTip(FString Text) { ToolTip = std::move(Text); return *this; }
	FPropertyDesc& SetReadOnly(bool bInReadOnly = true) { bReadOnly = bInReadOnly; return *this; }
};

class IPropertySource
{
public:
	virtual ~IPropertySource() = default;

	// Shown in the grid's header for a single selection ("light_03").
	virtual FString GetDisplayName() const = 0;
	virtual void GetProperties(TArray<FPropertyDesc>& OutProperties) const = 0;
	virtual FPropertyValue GetValue(FName Property) const = 0;
	virtual void SetValue(FName Property, const FPropertyValue& Value) = 0;
};
