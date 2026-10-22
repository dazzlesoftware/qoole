// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "MapEditorEnvironment.h"
#include "AssetPackage.h"

#include "MapEditorPrivate.h"

#include <algorithm>
#include <filesystem>

DEFINE_LOG_CATEGORY(LogMapEditor, Log);

namespace
{
	struct FEnvironment
	{
		bool bStarted = false;
		TArray<FMapGameSet> GameSets;
		TArray<FMapEditorEnvironment::FPrefab> Prefabs;
		FString EditorContentDir;
		FString UserPrefabFolder;
		int32 Active = INDEX_NONE;
	};

	FEnvironment& Env()
	{
		static FEnvironment Instance;
		return Instance;
	}

	void FindPrefabs(const std::filesystem::path& Dir, bool bUser, TArray<FMapEditorEnvironment::FPrefab>& Out)
	{
		std::error_code Error;
		const SIZE_T First = Out.size();
		for (const auto& Entry : std::filesystem::directory_iterator(Dir, Error))
		{
			if (!Entry.is_regular_file() || (Entry.path().extension() != L".qle" && Entry.path().extension() != L".asset"))
			{
				continue;
			}
			if (Entry.path().extension() == L".asset")
			{
				FAssetPackageMetadata Metadata;
				FString PackageError;
				if (!FAssetPackageStore::ReadMetadata(FString::FromWide(Entry.path().wstring()), Metadata, PackageError) ||
					Metadata.Kind != EAssetPackageKind::Asset || Metadata.Schema != "Genesis.Prefab" || (Metadata.SchemaVersion < 1 || Metadata.SchemaVersion > 3)) continue;
			}
			FString Name = FString::FromWide(Entry.path().stem().wstring());
			if (!Name.IsEmpty())
			{
				Name = Name.Left(1).ToUpper() + Name.Mid(1);
			}
			Out.push_back({ Name, FString::FromWide(Entry.path().wstring()), bUser });
		}
		std::sort(Out.begin() + static_cast<std::ptrdiff_t>(First), Out.end(), [](const auto& A, const auto& B) { return A.Name.View() < B.Name.View(); });
	}
}

bool FMapEditorEnvironment::Startup(const FStartupOptions& Options)
{
	FEnvironment& E = Env();
	if (!ensuref(!E.bStarted, "FMapEditorEnvironment::Startup called twice"))
	{
		return true;
	}

	LConfig::Init(1, 1);
	FDraw::Init();
	LFile::Init();

	// Entity sets and wireframes are read from Content/Editor.
	if (!Options.EditorContentDir.IsEmpty())
	{
		LFile::UseDir(Options.EditorContentDir.ToAnsi().c_str());
	}

	new GenesisGame; // registers itself in Game's list

	E.GameSets.clear();
	FMapGameSet BuiltIn;
	BuiltIn.Name = "Genesis";
	BuiltIn.Game = "Genesis";
	BuiltIn.EntFile = GenesisGame::GetEntFileName();
	E.GameSets.push_back(BuiltIn);
	E.EditorContentDir = Options.EditorContentDir;
	RefreshPrefabs();
	E.Active = INDEX_NONE;
	E.bStarted = true;
	return true;
}

void FMapEditorEnvironment::Shutdown()
{
	FEnvironment& E = Env();
	if (!E.bStarted)
	{
		return;
	}
	EntList::Exit();
	Game::Exit();
	FDraw::Exit();
	LFile::Exit();
	LConfig::Exit();
	E.GameSets.clear();
	E.Prefabs.clear();
	E.Active = INDEX_NONE;
	E.bStarted = false;
}

bool FMapEditorEnvironment::IsStarted()
{
	return Env().bStarted;
}

const TArray<FMapGameSet>& FMapEditorEnvironment::GetGameSets()
{
	return Env().GameSets;
}

const FMapGameSet& FMapEditorEnvironment::GetBuiltInGameSet()
{
	check(!Env().GameSets.empty());
	return Env().GameSets[0];
}

const FMapGameSet* FMapEditorEnvironment::FindGameSet(const FString& Game, const FString& EntFile)
{
	for (const FMapGameSet& Set : Env().GameSets)
	{
		if (Set.Game.Equals(Game.View(), ESearchCase::IgnoreCase) && Set.EntFile.Equals(EntFile.View(), ESearchCase::IgnoreCase))
		{
			return &Set;
		}
	}
	return nullptr;
}

bool FMapEditorEnvironment::Activate(const FMapGameSet& GameSet, FString* OutError)
{
	FEnvironment& E = Env();
	check(E.bStarted);
	Game* GamePtr = Game::Find(const_cast<char*>(GameSet.Game.ToAnsi().c_str()));
	if (!GamePtr)
	{
		if (OutError)
		{
			*OutError = FString::Format("The game \"{}\" is not available.", GameSet.Game);
		}
		return false;
	}
	std::string Palette = GameSet.Palette.ToAnsi();
	if (!Game::Set(GamePtr, Palette.data(), FDraw::textureGamma))
	{
		if (OutError)
		{
			*OutError = FString::Format("{} could not be set up (its game folder is needed).", GameSet.Game);
		}
		return false;
	}
	std::string EntFile = GameSet.EntFile.ToAnsi();
	EntList::Set(EntList::Find(EntFile.data()));

	E.Active = INDEX_NONE;
	for (SIZE_T Index = 0; Index < E.GameSets.size(); ++Index)
	{
		if (&E.GameSets[Index] == &GameSet || (E.GameSets[Index].Game == GameSet.Game && E.GameSets[Index].EntFile == GameSet.EntFile))
		{
			E.Active = static_cast<int32>(Index);
			break;
		}
	}
	UE_LOG(LogMapEditor, Display, "Game set: {}", GameSet.Name);
	return true;
}

const FMapGameSet* FMapEditorEnvironment::GetActiveGameSet()
{
	const FEnvironment& E = Env();
	return E.Active == INDEX_NONE ? nullptr : &E.GameSets[static_cast<SIZE_T>(E.Active)];
}

const TArray<FMapEditorEnvironment::FPrefab>& FMapEditorEnvironment::GetPrefabs()
{
	return Env().Prefabs;
}

void FMapEditorEnvironment::SetUserPrefabFolder(const FString& Folder)
{
	Env().UserPrefabFolder = Folder;
	RefreshPrefabs();
}

const FString& FMapEditorEnvironment::GetUserPrefabFolder()
{
	return Env().UserPrefabFolder;
}

void FMapEditorEnvironment::RefreshPrefabs()
{
	FEnvironment& E = Env();
	E.Prefabs.clear();
	FindPrefabs(std::filesystem::path(E.EditorContentDir.ToWide()) / L"brushes", false, E.Prefabs);
	if (!E.UserPrefabFolder.IsEmpty())
	{
		FindPrefabs(std::filesystem::path(E.UserPrefabFolder.ToWide()), true, E.Prefabs);
	}
}

TArray<FMapEditorEnvironment::FEntityClass> FMapEditorEnvironment::GetPlaceableEntities()
{
	TArray<FEntityClass> Result;
	EntList* List = EntList::Get();
	if (!List)
	{
		return Result;
	}
	IterLinkList<EntInfo> Iter(List->GetInfoList());
	Iter.Reset();
	while (!Iter.IsDone())
	{
		EntInfo* Info = Iter.GetNext();
		if (Info->IsModel() || !stricmp(Info->GetClassname(), "worldspawn"))
		{
			continue;
		}
		FEntityClass Class;
		Class.ClassName = FString::FromAnsi(Info->GetClassname());
		Class.Category = Info->GetEntClass() ? FString::FromAnsi(Info->GetEntClass()->name) : FString("Other");
		Class.Label = FString::FromAnsi(Info->GetClassdesc());
		Result.push_back(std::move(Class));
	}
	return Result;
}
