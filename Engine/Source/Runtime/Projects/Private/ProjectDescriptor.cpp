// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "ProjectDescriptor.h"
#include "HAL/File.h"
#include <filesystem>
#include <fstream>

namespace
{
	TOptional<FProjectContext> ActiveProject;
	FString StringPath(const std::filesystem::path& Path) { return FString::FromWide(Path.wstring()); }
	bool ValidPackage(const FString& Package)
	{
		if (!Package.StartsWith("/Game/") || Package.Len() <= 6) return false;
		for (const FString& Part : Package.Mid(6).ParseIntoArray("/", false))
		{
			if (Part.IsEmpty()) return false;
			for (const char C : Part.View())
				if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') ||
					(C >= '0' && C <= '9') || C == '_' || C == '-')) return false;
		}
		return true;
	}
}

FJsonValue FProjectDescriptor::ToJson() const
{
	FJsonValue Json = Extensions.IsObject() ? Extensions : FJsonValue::MakeObject();
	Json["FileVersion"] = CurrentVersion;
	Json["Name"] = Name;
	Json["EngineAssociation"] = EngineAssociation;
	Json["Description"] = Description;
	Json["DefaultMap"] = DefaultMap;
	return Json;
}

TOptional<FProjectDescriptor> FProjectDescriptor::FromJson(const FJsonValue& Json, FString& Error)
{
	Error.Empty();
	if (!Json.IsObject() || Json.Get("FileVersion").GetType() != EJsonType::Number ||
		Json.Get("FileVersion").AsNumber() != CurrentVersion)
	{
		Error = "Unsupported Genesis project version (expected FileVersion 1).";
		return {};
	}
	FProjectDescriptor Result;
	Result.Name = Json.Get("Name").AsString();
	if (Result.Name.TrimStartAndEnd().IsEmpty()) { Error = "A project needs a Name."; return {}; }
	for (const char* Key : { "EngineAssociation", "Description", "DefaultMap" })
		if (Json.Contains(Key) && Json.Get(Key).GetType() != EJsonType::String)
		{ Error = FString::Format("Project field {} must be a string.", Key); return {}; }
	Result.EngineAssociation = Json.Get("EngineAssociation").AsString("Genesis");
	Result.Description = Json.Get("Description").AsString();
	Result.DefaultMap = Json.Get("DefaultMap").AsString();
	if (!Result.DefaultMap.IsEmpty() && !ValidPackage(Result.DefaultMap))
	{ Error = "DefaultMap must be a logical path such as /Game/Maps/Main."; return {}; }
	Result.Extensions = Json;
	return Result;
}

TOptional<FProjectContext> FProjectContext::Open(const FString& Path, FString& Error)
{
	Error.Empty();
	const std::filesystem::path Input(Path.ToWide());
	if (!FString::FromWide(Input.extension().wstring()).Equals(".gproject", ESearchCase::IgnoreCase))
	{ Error = "Project descriptors must use .gproject."; return {}; }
	FString Text;
	if (!FFile::ReadAllText(Path, Text)) { Error = FString::Format("Cannot read project {}.", Path); return {}; }
	const auto Json = FJsonValue::Parse(Text.View(), &Error);
	if (!Json) return {};
	const auto Parsed = FProjectDescriptor::FromJson(*Json, Error);
	if (!Parsed) return {};
	std::error_code EC;
	const auto Absolute = std::filesystem::absolute(Input, EC).lexically_normal();
	if (EC) { Error = "Cannot resolve the project directory."; return {}; }
	FProjectContext Result;
	Result.FilePath = StringPath(Absolute);
	Result.Descriptor = *Parsed;
	return Result;
}

TOptional<FProjectContext> FProjectContext::Create(const FString& Path, const FString& Name, FString& Error)
{
	FProjectDescriptor Descriptor;
	Descriptor.Name = Name;
	if (!FProjectDescriptor::FromJson(Descriptor.ToJson(), Error)) return {};
	const std::filesystem::path File(Path.ToWide());
	if (!StringPath(File.extension()).Equals(".gproject", ESearchCase::IgnoreCase))
	{ Error = "New projects must use .gproject."; return {}; }
	std::error_code EC;
	if (std::filesystem::exists(File, EC) || EC) { Error = "The project already exists or cannot be accessed."; return {}; }
	for (const char* Directory : { "Content", "Config", "Source", "Plugins", "Saved", "Intermediate" })
	{
		std::filesystem::create_directories(File.parent_path() / Directory, EC);
		if (EC) { Error = "Cannot create project directories."; return {}; }
	}
	// Exclusive create protects an existing descriptor even across processes.
	FILE* Out = _wfopen(File.c_str(), L"wbx");
	if (!Out) { Error = "Cannot create project descriptor (it may already exist)."; return {}; }
	const FString Text = Descriptor.ToJson().ToString();
	const bool Written = std::fwrite(*Text, 1, Text.Len(), Out) == static_cast<size_t>(Text.Len());
	const bool Closed = std::fclose(Out) == 0;
	if (!Written || !Closed) { Error = "Cannot write project descriptor."; return {}; }
	return Open(Path, Error);
}

FString FProjectContext::GetRootDir() const { return StringPath(std::filesystem::path(FilePath.ToWide()).parent_path()); }
FString FProjectContext::GetContentDir() const { return StringPath(std::filesystem::path(GetRootDir().ToWide()) / "Content"); }
FString FProjectContext::GetSavedDir() const { return StringPath(std::filesystem::path(GetRootDir().ToWide()) / "Saved"); }

bool FProjectContext::ResolvePackage(const FString& Package, bool bMap, FString& File, FString& Error) const
{
	Error.Empty();
	if (!ValidPackage(Package)) { Error = "Invalid /Game package path."; return false; }
	const auto Relative = std::filesystem::path(Package.Mid(6).ToWide());
	File = StringPath(std::filesystem::path(GetContentDir().ToWide()) / Relative) + (bMap ? ".gmap" : ".asset");
	return true;
}

const FProjectContext* FProjectContext::GetActive() { return ActiveProject ? &*ActiveProject : nullptr; }
void FProjectContext::SetActive(TOptional<FProjectContext> Project) { ActiveProject = std::move(Project); }
