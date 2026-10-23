// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Assets/RenderAssets.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace RenderAssetCodecPrivate
{
using J = FJsonValue;
inline void Need(bool Value, const char* Error)
{
	if (!Value)
		throw std::runtime_error(Error);
}
inline int Count(const J& V, int Max)
{
	const auto N = V.AsNumber(-1);
	Need(V.GetType() == EJsonType::Number && std::isfinite(N) && N >= 0 && N <= Max && N == std::floor(N),
		 "Invalid asset count or offset.");
	return static_cast<int>(N);
}
inline float Number(const J& V, float Low = 0, float High = 1)
{
	const auto N = V.AsNumber();
	Need(V.GetType() == EJsonType::Number && std::isfinite(N) && N >= Low && N <= High,
		 "Invalid asset numeric value.");
	return static_cast<float>(N);
}
inline FAssetPackage Package(const FAssetPackage& Previous, const char* Schema)
{
	FAssetPackage P;
	P.Id = Previous.Id;
	P.Schema = Schema;
	return P;
}
inline FAssetReference Reference(const J& Value)
{
	FString Error;
	auto Ref = FAssetReference::FromJson(Value, Error);
	Need(Ref.has_value(), "Invalid render asset reference.");
	return *Ref;
}
inline void Dependency(FAssetPackage& P, const FAssetReference& R)
{
	Reference(R.ToJson());
	if (!R.Path.IsEmpty() &&
		std::find(P.Dependencies.begin(), P.Dependencies.end(), R.Path) == P.Dependencies.end())
		P.Dependencies.push_back(R.Path);
}
} // namespace RenderAssetCodecPrivate
