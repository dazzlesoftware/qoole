// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "World/WorldLocation.h"
#include "AssetPackage.h"
#include "Internationalization/TextLocalization.h"
#include "Misc/ValidationContext.h"
#include <cmath>
#include <limits>

namespace
{
	// Region-local values stay well inside the scene's coordinate bound.
	constexpr double MaxLocalCoordinate = 1.0e9;
} // namespace

FJsonValue FWorldLocation::ToJson() const
{
	FJsonValue Json = FJsonValue::MakeObject();
	Json["Region"] = RegionId;
	Json["Local"] = FJsonValue::MakeArray();
	Json["Local"].Push(Local.GetX()).Push(Local.GetY()).Push(Local.GetZ());
	return Json;
}

TOptional<FWorldLocation> FWorldLocation::FromJson(const FJsonValue& Json, FString& Error)
{
	FValidationContext Validation(Error);
	const FJsonValue& Local = Json.Get("Local");
	if (!Validation.Check(
	        Json.IsObject() && FAssetId::IsValid(Json.Get("Region").AsString()) && Local.IsArray() && Local.Num() == 3,
	        GENESIS_TEXT("A world location needs a region identity and three local coordinates.")))
		return {};
	float Values[3] = {};
	for (int32 I = 0; I < 3; ++I)
	{
		const double Value = Local[I].AsNumber(std::numeric_limits<double>::quiet_NaN());
		if (!Validation.Check(Local[I].GetType() == EJsonType::Number && std::isfinite(Value) &&
		            std::abs(Value) <= MaxLocalCoordinate,
		        GENESIS_TEXT("Invalid world location coordinate.")))
			return {};
		Values[I] = static_cast<float>(Value);
	}
	Error.Empty();
	return FWorldLocation{Json.Get("Region").AsString(), Vector3d(Values[0], Values[1], Values[2])};
}
