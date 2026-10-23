// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Assets/RenderAssets.h"
#include "Misc/ValidationContext.h"
#include "Internationalization/TextLocalization.h"
#include <algorithm>
#include <cmath>

namespace RenderAssetCodecPrivate
{
	using J = FJsonValue;

	inline int Count(FValidationContext& Validation, const J& V, int Max)
	{
		const double N = V.AsNumber(-1);
		if (!Validation.Check(
		        V.GetType() == EJsonType::Number && std::isfinite(N) && N >= 0 && N <= Max && N == std::floor(N),
		        GENESIS_TEXT("Invalid asset count or offset.")))
		{
			return {};
		}
		return static_cast<int>(N);
	}
	inline float Number(FValidationContext& Validation, const J& V, float Low = 0, float High = 1)
	{
		const double N = V.AsNumber();
		if (!Validation.Check(V.GetType() == EJsonType::Number && std::isfinite(N) && N >= Low && N <= High,
		        GENESIS_TEXT("Invalid asset numeric value.")))
		{
			return {};
		}
		return static_cast<float>(N);
	}
	inline FAssetPackage Package(const FAssetPackage& Previous, const char* Schema)
	{
		FAssetPackage P;
		P.Id = Previous.Id;
		P.Schema = Schema;
		return P;
	}
	inline FAssetReference Reference(FValidationContext& Validation, const J& Value)
	{
		FString Error;
		TOptional<FAssetReference> Ref = FAssetReference::FromJson(Value, Error);
		if (!Validation.Check(Ref.has_value(), GENESIS_TEXT("Invalid render asset reference.")))
		{
			return {};
		}
		return *Ref;
	}
	inline void Dependency(FValidationContext& Validation, FAssetPackage& P, const FAssetReference& R)
	{
		Reference(Validation, R.ToJson());
		if (!Validation.IsValid())
		{
			return;
		}
		if (!R.Path.IsEmpty() &&
		    std::find(P.Dependencies.begin(), P.Dependencies.end(), R.Path) == P.Dependencies.end())
			P.Dependencies.push_back(R.Path);
	}
} // namespace RenderAssetCodecPrivate
