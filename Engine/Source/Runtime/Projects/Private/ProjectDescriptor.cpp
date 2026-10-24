// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "ProjectDescriptor.h"
#include "Containers/ContainerAliases.h"
#include "Internationalization/TextLocalization.h"
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
	if (GameplayLanguage != "None" || !EditorLanguages.empty() || !ScriptModules.empty() || Json.Contains("Scripting"))
	{
		FJsonValue Settings = Json.Get("Scripting").IsObject() ? Json.Get("Scripting") : FJsonValue::MakeObject();
		Settings["GameplayLanguage"] = GameplayLanguage;
		Settings["EditorLanguages"] = FJsonValue::MakeArray();
		for (const FString& L : EditorLanguages)
			Settings["EditorLanguages"].Push(L);
		if (!ScriptModules.empty() || Settings.Contains("Modules"))
		{
			Settings["Modules"] = FJsonValue::MakeArray();
			for (const FProjectScriptModule& Module : ScriptModules)
			{
				FJsonValue M = Module.Extensions.IsObject() ? Module.Extensions : FJsonValue::MakeObject();
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
		Error = GENESIS_TEXT("Unsupported Genesis project version (expected FileVersion 1).");
		return {};
	}
	FProjectDescriptor Result;
	Result.Name = Json.Get("Name").AsString();
	if (Result.Name.TrimStartAndEnd().IsEmpty())
	{
		Error = GENESIS_TEXT("A project needs a Name.");
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
		const FJsonValue& S = Json.Get("Scripting");
		TFunction<bool(const FString&)> Valid = [](const FString& L)
		{
			if (L.IsEmpty() || L.Len() > 64)
				return false;
			for (char C : L.View())
				if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_' ||
				        C == '-'))
					return false;
			return true;
		};
		if (!S.IsObject() ||
		    (S.Contains("GameplayLanguage") && S.Get("GameplayLanguage").GetType() != EJsonType::String))
		{
			Error = GENESIS_TEXT("Invalid Scripting settings.");
			return {};
		}
		if (S.Contains("Modules"))
		{
			const FJsonValue& Modules = S.Get("Modules");
			if (!Modules.IsArray() || Modules.Num() > 256)
			{
				Error = GENESIS_TEXT("Invalid script module catalog.");
				return {};
			}
			for (const FJsonValue& M : Modules.GetItems())
			{
				FProjectScriptModule Module;
				Module.Name = M.Get("Name").AsString();
				Module.Source = M.Get("Source").AsString();
				Module.Runtime = M.Get("Runtime").AsString();
				Module.Extensions = M;
				const TFunction<bool(const FString&)> PathValid = [](const FString& P)
				{
					return !P.IsEmpty() && P.Len() <= 4096 && P.View().find('\0') == std::string_view::npos &&
					    !std::filesystem::path(P.ToWide()).is_absolute() &&
					    !std::filesystem::path(P.ToWide()).has_root_name() &&
					    !std::filesystem::path(P.ToWide()).has_root_directory();
				};
				if (!M.IsObject() || !Valid(Module.Name) || !PathValid(Module.Source) ||
				    (M.Contains("Runtime") &&
				        (M.Get("Runtime").GetType() != EJsonType::String ||
				            (!Module.Runtime.IsEmpty() && !PathValid(Module.Runtime)))) ||
				    std::any_of(Result.ScriptModules.begin(), Result.ScriptModules.end(),
				        [&](const FProjectScriptModule& Existing) { return Existing.Name == Module.Name; }))
				{
					Error = GENESIS_TEXT("Invalid or duplicate script module.");
					return {};
				}
				Result.ScriptModules.push_back(std::move(Module));
			}
		}
		Result.GameplayLanguage = S.Get("GameplayLanguage").AsString("None");
		if (!Valid(Result.GameplayLanguage))
		{
			Error = GENESIS_TEXT("Invalid gameplay language provider ID.");
			return {};
		}
		if (S.Contains("EditorLanguages"))
		{
			const FJsonValue& Ls = S.Get("EditorLanguages");
			if (!Ls.IsArray() || Ls.Num() > 16)
			{
				Error = GENESIS_TEXT("Invalid editor language list.");
				return {};
			}
			for (const FJsonValue& V : Ls.GetItems())
			{
				const FString L = V.AsString();
				if (V.GetType() != EJsonType::String || !Valid(L) || L == "None" ||
				    std::find(Result.EditorLanguages.begin(), Result.EditorLanguages.end(), L) !=
				        Result.EditorLanguages.end())
				{
					Error = GENESIS_TEXT("Invalid or duplicate editor language provider.");
					return {};
				}
				Result.EditorLanguages.push_back(L);
			}
		}
	}
	if (!Result.DefaultMap.IsEmpty() && !ValidPackage(Result.DefaultMap))
	{
		Error = GENESIS_TEXT("DefaultMap must be a logical path such as /Game/Maps/Main.");
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
		Error = GENESIS_TEXT("Project descriptors must use .gproject.");
		return {};
	}
	FString Text;
	if (!FFile::ReadAllText(Path, Text))
	{
		Error = FString::Format("Cannot read project {}.", Path);
		return {};
	}
	const TOptional<FJsonValue> Json = FJsonValue::Parse(Text.View(), &Error);
	if (!Json)
		return {};
	const TOptional<FProjectDescriptor> Parsed = FProjectDescriptor::FromJson(*Json, Error);
	if (!Parsed)
		return {};
	std::error_code EC;
	const std::filesystem::path Absolute = std::filesystem::absolute(Input, EC).lexically_normal();
	if (EC)
	{
		Error = GENESIS_TEXT("Cannot resolve the project directory.");
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
		Error = GENESIS_TEXT("New projects must use .gproject.");
		return {};
	}
	std::error_code EC;
	if (std::filesystem::exists(File, EC) || EC)
	{
		Error = GENESIS_TEXT("The project already exists or cannot be accessed.");
		return {};
	}
	for (const char* Directory : {"Content", "Config", "Source", "Plugins", "Saved", "Intermediate"})
	{
		std::filesystem::create_directories(File.parent_path() / Directory, EC);
		if (EC)
		{
			Error = GENESIS_TEXT("Cannot create project directories.");
			return {};
		}
	}
	// Exclusive create protects an existing descriptor even across processes.
	FILE* Out = _wfopen(File.c_str(), L"wbx");
	if (!Out)
	{
		Error = GENESIS_TEXT("Cannot create project descriptor (it may already exist).");
		return {};
	}
	const FString Text = Descriptor.ToJson().ToString();
	const bool Written = std::fwrite(*Text, 1, Text.Len(), Out) == static_cast<size_t>(Text.Len());
	const bool Closed = std::fclose(Out) == 0;
	if (!Written || !Closed)
	{
		Error = GENESIS_TEXT("Cannot write project descriptor.");
		return {};
	}
	return Open(Path, Error);
}

bool FProjectContext::SetDefaultMap(const FString& Package, FString& Error)
{
	FProjectDescriptor Next = Descriptor;
	Next.DefaultMap = Package;
	const TOptional<FProjectDescriptor> Checked = FProjectDescriptor::FromJson(Next.ToJson(), Error);
	if (!Checked)
		return false;
	const std::filesystem::path File(FilePath.ToWide());
	const std::filesystem::path Temporary = File.wstring() + L".tmp";
	FILE* Out = _wfopen(Temporary.c_str(), L"wb");
	if (!Out)
	{
		Error = GENESIS_TEXT("Cannot write project descriptor.");
		return false;
	}
	const FString Text = Checked->ToJson().ToString();
	const bool Written = std::fwrite(*Text, 1, Text.Len(), Out) == static_cast<size_t>(Text.Len());
	const bool Closed = std::fclose(Out) == 0;
	std::error_code EC;
	if (Written && Closed)
		std::filesystem::rename(Temporary, File, EC);
	if (!Written || !Closed || EC)
	{
		std::filesystem::remove(Temporary, EC);
		Error = GENESIS_TEXT("Cannot write project descriptor.");
		return false;
	}
	Descriptor = *Checked;
	Error.Empty();
	return true;
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
		Error = GENESIS_TEXT("Invalid /Game package path.");
		return false;
	}
	const std::filesystem::path Relative = std::filesystem::path(Package.Mid(6).ToWide());
	File = StringPath(std::filesystem::path(GetContentDir().ToWide()) / Relative) + (bMap ? ".gmap" : ".asset");
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

bool FProjectContext::ResolveScriptModule(const FString& Name, bool bRuntime, FString& File, FString& Error) const
{
	for (const FProjectScriptModule& Module : Descriptor.ScriptModules)
		if (Module.Name == Name)
		{
			const FString& Relative = bRuntime && !Module.Runtime.IsEmpty() ? Module.Runtime : Module.Source;
			File = StringPath((std::filesystem::path(GetRootDir().ToWide()) / std::filesystem::path(Relative.ToWide()))
			        .lexically_normal());
			Error.Empty();
			return true;
		}
	Error = GENESIS_TEXT("Project script module is missing: ") + Name;
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
		Error = GENESIS_TEXT("Cannot read project script module: ") + File;
		return false;
	}
	const int64 Size = Input.Size();
	if (Size < 0 || Size > 4 * 1024 * 1024)
	{
		Error = GENESIS_TEXT("Project script modules must not exceed 4 MiB.");
		return false;
	}
	TArray<uint8> Candidate;
	Candidate.resize(static_cast<size_t>(Size));
	if (Input.Read(Candidate.data(), Size) != Size)
	{
		Error = GENESIS_TEXT("Cannot read complete project script module.");
		return false;
	}
	Payload = std::move(Candidate);
	Error.Empty();
	return true;
}
