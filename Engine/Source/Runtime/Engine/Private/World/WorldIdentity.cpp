// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "World/WorldIdentity.h"
#include "AssetPackage.h"

FActorIdentity FActorIdentity::New() { return { FAssetId::New(), FAssetId::New(), FAssetId::New(), FAssetId::New() }; }
bool FActorIdentity::IsValid() const
{
	TSet<std::string> Seen;
	for (const auto* Id : { &Actor, &Transform, &Payload, &Entity })
		if (!FAssetId::IsValid(*Id) || !Seen.insert(Id->ToStdString()).second) return false;
	return true;
}
FJsonValue FActorIdentity::ToJson() const
{
	auto Json = FJsonValue::MakeObject();
	Json["Actor"] = Actor; Json["Transform"] = Transform; Json["Payload"] = Payload; Json["Entity"] = Entity;
	return Json;
}
TOptional<FActorIdentity> FActorIdentity::FromJson(const FJsonValue& Json, FString& Error)
{
	FActorIdentity Result{ Json.Get("Actor").AsString(), Json.Get("Transform").AsString(), Json.Get("Payload").AsString(), Json.Get("Entity").AsString() };
	if (!Json.IsObject() || !Result.IsValid()) { Error = "Invalid actor/component identity."; return {}; }
	Error.Empty(); return Result;
}
