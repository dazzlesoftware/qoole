// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "MapAssetLibrary.h"
#include "MapCoreHeaders.h"
namespace
{
std::weak_ptr<FAssetRegistry> Registry;
uint64 Context = 0, MeshRevision = 0;
TMap<std::string, std::weak_ptr<const FLoadedMeshAsset>> Meshes;
} // namespace
void FMapAssetLibrary::SetRegistry(const std::shared_ptr<FAssetRegistry>& In)
{
	Registry = In;
	++Context;
	Meshes.clear();
	MeshRevision = 0;
}
std::shared_ptr<FAssetRegistry> FMapAssetLibrary::GetRegistry()
{
	return Registry.lock();
}
std::string FMapAssetLibrary::RevisionKey()
{
	const auto R = GetRegistry();
	return FString::Format("{}:{}", Context, R ? R->GetRevision() : 0).ToStdString();
}
std::shared_ptr<const FLoadedMeshAsset> FMapAssetLibrary::LoadMesh(const FAssetReference& Ref,
																   const FAssetReference& Override, FString& Error,
																   const TArray<FAssetReference>& AdditionalOverrides)
{
	const auto R = GetRegistry();
	if (!R)
	{
		Error = "Open the project containing this mesh asset before loading the map.";
		return {};
	}
	if (MeshRevision != R->GetRevision())
	{
		Meshes.clear();
		MeshRevision = R->GetRevision();
	}
	auto Key = Ref.ToJson().ToString(false).ToStdString() + Override.ToJson().ToString(false).ToStdString();
	for (const auto& SlotRef : AdditionalOverrides)
		Key += SlotRef.ToJson().ToString(false).ToStdString();
	if (const auto It = Meshes.find(Key); It != Meshes.end())
		if (auto Existing = It->second.lock())
		{
			Error.Empty();
			return Existing;
		}
	auto Loaded = std::make_shared<FLoadedMeshAsset>();
	if (!FRenderAssetLoader(*R).LoadMesh(Ref, *Loaded, Error, Override, AdditionalOverrides))
		return {};
	Meshes[Key] = Loaded;
	return Loaded;
}
FString FMapAssetLibrary::TextureToken(const FAssetReference& Ref)
{
	return "asset:" + Ref.ToJson().ToString(false);
}
bool FMapAssetLibrary::ParseTextureToken(const FString& Token, FAssetReference& Out, FString& Error)
{
	if (Token.View().substr(0, 6) != "asset:")
	{
		Error = "Expected a native texture reference.";
		return false;
	}
	const auto Json = FJsonValue::Parse(Token.View().substr(6), &Error);
	if (!Json)
		return false;
	const auto Ref = FAssetReference::FromJson(*Json, Error);
	if (!Ref || Ref->IsNull())
	{
		Error = "Invalid native texture reference.";
		return false;
	}
	Out = *Ref;
	return true;
}
bool FMapAssetLibrary::LoadTexture(const FString& Token, FTextureAsset& Out, FString& Error)
{
	FAssetReference Ref;
	if (!ParseTextureToken(Token, Ref, Error))
		return false;
	const auto R = GetRegistry();
	if (!R)
	{
		Error = "This texture's project is not open.";
		return false;
	}
	return FRenderAssetLoader(*R).LoadTexture(Ref, Out, Error);
}
bool FMapAssetLibrary::HasNativeAssets(const Object& Obj)
{
	if (Obj.GetMeshAsset())
		return true;
	if (Obj.HasLandscape())
		for (const auto& Layer : Obj.GetLandscape().GetLayers())
			if (Layer.texture.rfind("asset:", 0) == 0)
				return true;
	for (int I = 0; I < Obj.GetNumChildren(); ++I)
		if (HasNativeAssets(Obj.GetChild(I)))
			return true;
	return false;
}
