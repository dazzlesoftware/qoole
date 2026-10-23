// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "ProjectDescriptor.h"
#include "HAL/File.h"
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace
{
TOptional<FProjectContext> ActiveProject;
FString StringPath(const std::filesystem::path& Path)
{
	return FString::FromWide(Path.wstring());
}
bool ValidPackage(const FString& Package)
{
	if (!Package.StartsWith("/Game/") || Package.Len() <= 6)
		return false;
	for (const FString& Part : Package.Mid(6).ParseIntoArray("/", false))
	{
		if (Part.IsEmpty())
			return false;
		for (const char C : Part.View())
			if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_' ||
				  C == '-'))
				return false;
	}
	return true;
}
} // namespace

FJsonValue FProjectDescriptor::ToJson() const
{
	FJsonValue Json = Extensions.IsObject() ? Extensions : FJsonValue::MakeObject();
	Json["FileVersion"] = CurrentVersion;
	Json["Name"] = Name;
	Json["EngineAssociation"] = EngineAssociation;
	Json["Description"] = Description;
	Json["DefaultMap"] = DefaultMap;
	if (GameplayLanguage != "None" || !EditorLanguages.empty() || !ScriptModules.empty() ||
		Json.Contains("Scripting"))
	{
		auto Settings = Json.Get("Scripting").IsObject() ? Json.Get("Scripting") : FJsonValue::MakeObject();
		Settings["GameplayLanguage"] = GameplayLanguage;
		Settings["EditorLanguages"] = FJsonValue::MakeArray();
		for (const auto& L : EditorLanguages)
			Settings["EditorLanguages"].Push(L);
		if (!ScriptModules.empty() || Settings.Contains("Modules"))
		{
			Settings["Modules"] = FJsonValue::MakeArray();
			for (const auto& Module : ScriptModules)
			{
				auto M = Module.Extensions.IsObject() ? Module.Extensions : FJsonValue::MakeObject();
				M["Name"] = Module.Name;
				M["Source"] = Module.Source;
				if (!Module.Runtime.IsEmpty() || M.Contains("Runtime"))
					M["Runtime"] = Module.Runtime;
				Settings["Modules"].Push(std::move(M));
			}
		}
		Json["Scripting"] = Settings;
	}
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
	if (Result.Name.TrimStartAndEnd().IsEmpty())
	{
		Error = "A project needs a Name.";
		return {};
	}
	for (const char* Key : {"EngineAssociation", "Description", "DefaultMap"})
		if (Json.Contains(Key) && Json.Get(Key).GetType() != EJsonType::String)
		{
			Error = FString::Format("Project field {} must be a string.", Key);
			return {};
		}
	Result.EngineAssociation = Json.Get("EngineAssociation").AsString("Genesis");
	Result.Description = Json.Get("Description").AsString();
	Result.DefaultMap = Json.Get("DefaultMap").AsString();
	if (Json.Contains("Scripting"))
	{
		const auto& S = Json.Get("Scripting");
		auto Valid = [](const FString& L) {
			if (L.IsEmpty() || L.Len() > 64)
				return false;
			for (char C : L.View())
				if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') ||
					  C == '_' || C == '-'))
					return false;
			return true;
		};
		if (!S.IsObject() ||
			(S.Contains("GameplayLanguage") && S.Get("GameplayLanguage").GetType() != EJsonType::String))
		{
			Error = "Invalid Scripting settings.";
			return {};
		}
		if (S.Contains("Modules"))
		{
			const auto& Modules = S.Get("Modules");
			if (!Modules.IsArray() || Modules.Num() > 256)
			{
				Error = "Invalid script module catalog.";
				return {};
			}
			for (const auto& M : Modules.GetItems())
			{
				FProjectScriptModule Module;
				Module.Name = M.Get("Name").AsString();
				Module.Source = M.Get("Source").AsString();
				Module.Runtime = M.Get("Runtime").AsString();
				Module.Extensions = M;
				const auto PathValid = [](const FString& P) {
					return !P.IsEmpty() && P.Len() <= 4096 && P.View().find('\0') == std::string_view::npos &&
						   !std::filesystem::path(P.ToWide()).is_absolute() &&
						   !std::filesystem::path(P.ToWide()).has_root_name() &&
						   !std::filesystem::path(P.ToWide()).has_root_directory();
				};
				if (!M.IsObject() || !Valid(Module.Name) || !PathValid(Module.Source) ||
					(M.Contains("Runtime") && (M.Get("Runtime").GetType() != EJsonType::String ||
											   (!Module.Runtime.IsEmpty() && !PathValid(Module.Runtime)))) ||
					std::any_of(Result.ScriptModules.begin(), Result.ScriptModules.end(),
								[&](const auto& Existing) { return Existing.Name == Module.Name; }))
				{
					Error = "Invalid or duplicate script module.";
					return {};
				}
				Result.ScriptModules.push_back(std::move(Module));
			}
		}
		Result.GameplayLanguage = S.Get("GameplayLanguage").AsString("None");
		if (!Valid(Result.GameplayLanguage))
		{
			Error = "Invalid gameplay language provider ID.";
			return {};
		}
		if (S.Contains("EditorLanguages"))
		{
			const auto& Ls = S.Get("EditorLanguages");
			if (!Ls.IsArray() || Ls.Num() > 16)
			{
				Error = "Invalid editor language list.";
				return {};
			}
			for (const auto& V : Ls.GetItems())
			{
				const auto L = V.AsString();
				if (V.GetType() != EJsonType::String || !Valid(L) || L == "None" ||
					std::find(Result.EditorLanguages.begin(), Result.EditorLanguages.end(), L) !=
						Result.EditorLanguages.end())
				{
					Error = "Invalid or duplicate editor language provider.";
					return {};
				}
				Result.EditorLanguages.push_back(L);
			}
		}
	}
	if (!Result.DefaultMap.IsEmpty() && !ValidPackage(Result.DefaultMap))
	{
		Error = "DefaultMap must be a logical path such as /Game/Maps/Main.";
		return {};
	}
	Result.Extensions = Json;
	return Result;
}

TOptional<FProjectContext> FProjectContext::Open(const FString& Path, FString& Error)
{
	Error.Empty();
	const std::filesystem::path Input(Path.ToWide());
	if (!FString::FromWide(Input.extension().wstring()).Equals(".gproject", ESearchCase::IgnoreCase))
	{
		Error = "Project descriptors must use .gproject.";
		return {};
	}
	FString Text;
	if (!FFile::ReadAllText(Path, Text))
	{
		Error = FString::Format("Cannot read project {}.", Path);
		return {};
	}
	const auto Json = FJsonValue::Parse(Text.View(), &Error);
	if (!Json)
		return {};
	const auto Parsed = FProjectDescriptor::FromJson(*Json, Error);
	if (!Parsed)
		return {};
	std::error_code EC;
	const auto Absolute = std::filesystem::absolute(Input, EC).lexically_normal();
	if (EC)
	{
		Error = "Cannot resolve the project directory.";
		return {};
	}
	FProjectContext Result;
	Result.FilePath = StringPath(Absolute);
	Result.Descriptor = *Parsed;
	return Result;
}

TOptional<FProjectContext> FProjectContext::Create(const FString& Path, const FString& Name, FString& Error)
{
	FProjectDescriptor Descriptor;
	Descriptor.Name = Name;
	if (!FProjectDescriptor::FromJson(Descriptor.ToJson(), Error))
		return {};
	const std::filesystem::path File(Path.ToWide());
	if (!StringPath(File.extension()).Equals(".gproject", ESearchCase::IgnoreCase))
	{
		Error = "New projects must use .gproject.";
		return {};
	}
	std::error_code EC;
	if (std::filesystem::exists(File, EC) || EC)
	{
		Error = "The project already exists or cannot be accessed.";
		return {};
	}
	for (const char* Directory : {"Content", "Config", "Source", "Plugins", "Saved", "Intermediate"})
	{
		std::filesystem::create_directories(File.parent_path() / Directory, EC);
		if (EC)
		{
			Error = "Cannot create project directories.";
			return {};
		}
	}
	// Exclusive create protects an existing descriptor even across processes.
	FILE* Out = _wfopen(File.c_str(), L"wbx");
	if (!Out)
	{
		Error = "Cannot create project descriptor (it may already exist).";
		return {};
	}
	const FString Text = Descriptor.ToJson().ToString();
	const bool Written = std::fwrite(*Text, 1, Text.Len(), Out) == static_cast<size_t>(Text.Len());
	const bool Closed = std::fclose(Out) == 0;
	if (!Written || !Closed)
	{
		Error = "Cannot write project descriptor.";
		return {};
	}
	return Open(Path, Error);
}

FString FProjectContext::GetRootDir() const
{
	return StringPath(std::filesystem::path(FilePath.ToWide()).parent_path());
}
FString FProjectContext::GetContentDir() const
{
	return StringPath(std::filesystem::path(GetRootDir().ToWide()) / "Content");
}
FString FProjectContext::GetSavedDir() const
{
	return StringPath(std::filesystem::path(GetRootDir().ToWide()) / "Saved");
}

bool FProjectContext::ResolvePackage(const FString& Package, bool bMap, FString& File, FString& Error) const
{
	Error.Empty();
	if (!ValidPackage(Package))
	{
		Error = "Invalid /Game package path.";
		return false;
	}
	const auto Relative = std::filesystem::path(Package.Mid(6).ToWide());
	File =
		StringPath(std::filesystem::path(GetContentDir().ToWide()) / Relative) + (bMap ? ".gmap" : ".asset");
	return true;
}

const FProjectContext* FProjectContext::GetActive()
{
	return ActiveProject ? &*ActiveProject : nullptr;
}
void FProjectContext::SetActive(TOptional<FProjectContext> Project)
{
	ActiveProject = std::move(Project);
}

bool FProjectContext::ResolveScriptModule(const FString& Name, bool bRuntime, FString& File,
										  FString& Error) const
{
	for (const auto& Module : Descriptor.ScriptModules)
		if (Module.Name == Name)
		{
			const auto& Relative = bRuntime && !Module.Runtime.IsEmpty() ? Module.Runtime : Module.Source;
			File = StringPath(
				(std::filesystem::path(GetRootDir().ToWide()) / std::filesystem::path(Relative.ToWide()))
					.lexically_normal());
			Error.Empty();
			return true;
		}
	Error = "Project script module is missing: " + Name;
	return false;
}
bool FProjectContext::LoadScriptModule(const FString& Name, TArray<uint8>& Payload, FString& Error) const
{
	FString File;
	if (!ResolveScriptModule(Name, true, File, Error))
		return false;
	FFile Input;
	if (!Input.Open(File, EFileOpenMode::Read))
	{
		Error = "Cannot read project script module: " + File;
		return false;
	}
	const auto Size = Input.Size();
	if (Size < 0 || Size > 4 * 1024 * 1024)
	{
		Error = "Project script modules must not exceed 4 MiB.";
		return false;
	}
	TArray<uint8> Candidate;
	Candidate.resize(static_cast<size_t>(Size));
	if (Input.Read(Candidate.data(), Size) != Size)
	{
		Error = "Cannot read complete project script module.";
		return false;
	}
	Payload = std::move(Candidate);
	Error.Empty();
	return true;
}
