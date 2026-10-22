// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "AssetRegistry.h"
#include <algorithm>
#include <filesystem>

namespace
{
	namespace fs = std::filesystem;
	FString PathString(const fs::path& Path) { return FString::FromWide(Path.wstring()); }
	std::string Key(const FString& Value) { return Value.ToLower().ToStdString(); }
	bool ValidComponent(const FString& Part)
	{
		if (Part.IsEmpty() || Part == "." || Part == ".." || Part.EndsWith(".") || Part.EndsWith(" ")) return false;
		for (const unsigned char C : Part.View())
			if (C < 32 || C == 127 || std::string_view("<>:\"\\|?*/").find(static_cast<char>(C)) != std::string_view::npos) return false;
		return true;
	}
	bool ValidRoot(const FString& Root) { return Root.StartsWith("/") && ValidComponent(Root.Mid(1)); }
	fs::path Absolute(const FString& File, std::error_code& EC) { return fs::absolute(fs::path(File.ToWide()), EC).lexically_normal(); }
	std::string FileKey(const FString& File)
	{
		std::error_code EC;
		const auto Path = Absolute(File, EC);
		return EC ? std::string() : Key(FString::FromWide(Path.generic_wstring()));
	}
	bool Under(const fs::path& File, const fs::path& Directory)
	{
		const auto Relative = File.lexically_relative(Directory);
		return !Relative.empty() && *Relative.begin() != L".." && !Relative.is_absolute();
	}
}

bool FAssetRegistry::IsPackagePath(const FString& Path)
{
	if (!Path.StartsWith("/")) return false;
	const auto Parts = Path.Mid(1).ParseIntoArray("/", false);
	if (Parts.size() < 2) return false;
	for (const auto& Part : Parts) if (!ValidComponent(Part)) return false;
	return true;
}

FJsonValue FAssetReference::ToJson() const
{
	auto Json = FJsonValue::MakeObject();
	Json["Id"] = Id; Json["Path"] = Path;
	return Json;
}

TOptional<FAssetReference> FAssetReference::FromJson(const FJsonValue& Json, FString& Error)
{
	Error.Empty();
	if (!Json.IsObject() || (Json.Contains("Id") && Json.Get("Id").GetType() != EJsonType::String) ||
		(Json.Contains("Path") && Json.Get("Path").GetType() != EJsonType::String))
	{ Error = "An asset reference must contain string Id/Path fields."; return {}; }
	FAssetReference Ref{ Json.Get("Id").AsString(), Json.Get("Path").AsString() };
	if ((!Ref.Id.IsEmpty() && !FAssetId::IsValid(Ref.Id)) || (!Ref.Path.IsEmpty() && !FAssetRegistry::IsPackagePath(Ref.Path)))
	{ Error = "Invalid asset reference UUID or logical path."; return {}; }
	return Ref;
}

bool FAssetRegistry::SetMounts(TArray<FAssetMount> NewMounts, FString& Error)
{
	Error.Empty();
	for (size_t I = 0; I < NewMounts.size(); ++I)
	{
		auto& Mount = NewMounts[I];
		if (!ValidRoot(Mount.Root) || Mount.Directory.IsEmpty()) { Error = "Invalid content mount root or directory."; return false; }
		std::error_code EC;
		const auto AbsoluteDirectory = Absolute(Mount.Directory, EC);
		if (EC) { Error = FString::Format("Cannot resolve mount directory {}.", Mount.Directory); return false; }
		const auto Directory = fs::weakly_canonical(AbsoluteDirectory, EC);
		if (EC) { Error = FString::Format("Cannot resolve mount directory {}.", Mount.Directory); return false; }
		Mount.Directory = PathString(Directory);
		for (size_t J = 0; J < I; ++J)
		{
			// Use a case-folded path for Windows directory overlap checks.
			const fs::path A(FString(FileKey(Mount.Directory)).ToWide()), B(FString(FileKey(NewMounts[J].Directory)).ToWide());
			if (Key(Mount.Root) == Key(NewMounts[J].Root) || Under(A, B) || Under(B, A))
			{ Error = "Content mount roots must be unique and directories must not overlap."; return false; }
		}
	}
	Mounts = std::move(NewMounts);
	Assets.clear(); Issues.clear(); ById.clear(); ByPath.clear(); ByFile.clear();
	++Revision;
	return true;
}

bool FAssetRegistry::Scan()
{
	Assets.clear(); Issues.clear(); ById.clear(); ByPath.clear(); ByFile.clear();
	for (const auto& Mount : Mounts)
	{
		std::error_code EC;
		const fs::path Directory(Mount.Directory.ToWide());
		if (!fs::is_directory(Directory, EC))
		{ Issues.push_back({ Mount.Directory, "Content mount directory is missing or inaccessible." }); continue; }
		for (fs::recursive_directory_iterator It(Directory, EC), End; !EC && It != End; It.increment(EC))
		{
			std::error_code EntryError;
			if (It->is_symlink(EntryError)) { It.disable_recursion_pending(); continue; }
			if (!It->is_regular_file(EntryError)) continue;
			const FString Extension = PathString(It->path().extension()).ToLower();
			if (Extension != ".asset" && Extension != ".gmap") continue;
			FAssetData Data;
			Data.FilePath = PathString(It->path());
			auto Relative = It->path().lexically_relative(Directory);
			Relative.replace_extension();
			Data.PackagePath = Mount.Root + "/" + FString::FromWide(Relative.generic_wstring());
			FString Error;
			if (!IsPackagePath(Data.PackagePath))
			{ Issues.push_back({ Data.FilePath, "Invalid logical package path." }); continue; }
			if (!FAssetPackageStore::ReadMetadata(Data.FilePath, Data.Metadata, Error))
			{ Issues.push_back({ Data.FilePath, std::move(Error) }); continue; }
			Assets.push_back(std::move(Data));
		}
		if (EC) Issues.push_back({ Mount.Directory, FString::Format("Cannot finish content scan: {}", EC.message()) });
	}
	std::sort(Assets.begin(), Assets.end(), [](const FAssetData& A, const FAssetData& B)
	{
		const auto KA = Key(A.PackagePath), KB = Key(B.PackagePath);
		return KA != KB ? KA < KB : A.FilePath < B.FilePath;
	});
	for (size_t I = 0; I < Assets.size(); ++I)
	{
		ById[Key(Assets[I].Metadata.Id)].push_back(I);
		ByPath[Key(Assets[I].PackagePath)].push_back(I);
		ByFile[FileKey(Assets[I].FilePath)] = I;
	}
	for (const auto& Asset : Assets)
	{
		const auto Conflict = GetConflict(Asset);
		if (!Conflict.IsEmpty()) Issues.push_back({ Asset.FilePath, Conflict });
	}
	++Revision;
	return Issues.empty();
}

const FAssetData* FAssetRegistry::FindById(const FString& Id) const
{
	const auto It = ById.find(Key(Id));
	return It != ById.end() && It->second.size() == 1 ? &Assets[It->second.front()] : nullptr;
}

const FAssetData* FAssetRegistry::FindByPath(const FString& Path) const
{
	const auto It = ByPath.find(Key(Path));
	return It != ByPath.end() && It->second.size() == 1 ? &Assets[It->second.front()] : nullptr;
}

const FAssetData* FAssetRegistry::FindByFile(const FString& File) const
{
	const auto It = ByFile.find(FileKey(File));
	return It != ByFile.end() ? &Assets[It->second] : nullptr;
}

FString FAssetRegistry::GetConflict(const FAssetData& Asset) const
{
	const auto Id = ById.find(Key(Asset.Metadata.Id)), Path = ByPath.find(Key(Asset.PackagePath));
	if (Id != ById.end() && Id->second.size() > 1) return "Duplicate package UUID; import copies through the Content Browser to allocate a new identity.";
	if (Path != ByPath.end() && Path->second.size() > 1) return "Ambiguous logical path: more than one package has this name.";
	return {};
}

const FAssetData* FAssetRegistry::Resolve(const FAssetReference& Reference, FString& Error, std::string_view ExpectedSchema) const
{
	Error.Empty();
	const auto Validated = FAssetReference::FromJson(Reference.ToJson(), Error);
	if (!Validated) return nullptr;
	if (Reference.IsNull()) { Error = "Asset reference is empty."; return nullptr; }
	const bool ByIdentity = !Reference.Id.IsEmpty();
	const auto* Data = ByIdentity ? FindById(Reference.Id) : FindByPath(Reference.Path);
	if (!Data)
	{
		Error = ByIdentity ? "Asset UUID is missing or duplicated in mounted content." : "Asset path is missing or ambiguous in mounted content.";
		return nullptr;
	}
	if (!ByIdentity && !FindById(Data->Metadata.Id))
	{ Error = "The asset at this path has a duplicated UUID."; return nullptr; }
	if (!ExpectedSchema.empty() && Data->Metadata.Schema.View() != ExpectedSchema)
	{ Error = FString::Format("Asset has schema {}; expected {}.", Data->Metadata.Schema, ExpectedSchema); return nullptr; }
	return Data;
}

bool FAssetRegistry::Load(const FAssetReference& Reference, FAssetPackage& Package, FString& Error, std::string_view ExpectedSchema) const
{
	const auto* Data = Resolve(Reference, Error, ExpectedSchema);
	if (!Data) return false;
	FAssetPackage Loaded;
	if (!FAssetPackageStore::Load(Data->FilePath, Loaded, Error)) return false;
	if (Loaded.Id != Data->Metadata.Id || (!ExpectedSchema.empty() && Loaded.Schema.View() != ExpectedSchema))
	{ Error = "Package identity or schema changed since discovery; refresh content."; return false; }
	Package = std::move(Loaded);
	return true;
}
