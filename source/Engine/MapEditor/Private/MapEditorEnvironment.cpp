// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "MapEditorEnvironment.h"

#include "MapEditorPrivate.h"

#pragma warning(push, 3)
#include "HalfLife.h"
#include "Heretic2.h"
#include "Hexen2.h"
#include "Quake.h"
#include "Quake2.h"
#include "Sin.h"
#pragma warning(pop)
#undef Min
#undef Max

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
		int32 Active = INDEX_NONE;
	};

	FEnvironment& Env()
	{
		static FEnvironment Instance;
		return Instance;
	}

	// gamesets.txt: "Name, Game, EntFile[, Palette]" per line.
	void ReadGameSets(TArray<FMapGameSet>& Out)
	{
		LFile File;
		if (!File.Open("gamesets.txt"))
		{
			return;
		}
		while (const char* Line = File.GetNextLine())
		{
			const FString Text = FString::FromAnsi(Line).TrimStartAndEnd();
			if (Text.IsEmpty() || Text.StartsWith("//"))
			{
				continue;
			}
			TArray<FString> Fields = Text.ParseIntoArray(",", false);
			for (FString& Field : Fields)
			{
				Field = Field.TrimStartAndEnd();
			}
			if (Fields.size() < 3 || !Game::Find(const_cast<char*>(Fields[1].ToAnsi().c_str())))
			{
				continue;
			}
			FMapGameSet Set;
			Set.Name = FString::Format("{} ({})", Fields[0], Fields[1]);
			Set.Game = Fields[1];
			Set.EntFile = Fields[2];
			Set.Palette = Fields.size() > 3 ? Fields[3] : FString();
			Out.push_back(std::move(Set));
		}
	}

	void FindPrefabs(const FString& EditorContentDir, TArray<FMapEditorEnvironment::FPrefab>& Out)
	{
		std::error_code Error;
		const std::filesystem::path Dir = std::filesystem::path(EditorContentDir.ToWide()) / L"brushes";
		for (const auto& Entry : std::filesystem::directory_iterator(Dir, Error))
		{
			if (!Entry.is_regular_file() || Entry.path().extension() != L".qle")
			{
				continue;
			}
			FString Name = FString::FromWide(Entry.path().stem().wstring());
			if (!Name.IsEmpty())
			{
				Name = Name.Left(1).ToUpper() + Name.Mid(1);
			}
			Out.push_back({ Name, FString::FromWide(Entry.path().wstring()) });
		}
		std::sort(Out.begin(), Out.end(), [](const auto& A, const auto& B) { return A.Name.View() < B.Name.View(); });
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

	// Content/Editor first, so its files win over the pak's.
	if (!Options.EditorContentDir.IsEmpty())
	{
		LFile::UseDir(Options.EditorContentDir.ToAnsi().c_str());
	}
	bool bHavePak = false;
	if (!Options.PakPath.IsEmpty() && FFileHelper::FileExists(Options.PakPath))
	{
		bHavePak = LFile::UsePak(Options.PakPath.ToAnsi().c_str()) != nullptr;
	}
	if (!bHavePak)
	{
		GE_LOG(LogMapEditor, Display, "GenesisEditor.pak not found; only the built-in Genesis game is available");
	}

	// The games register themselves in Game's list. The legacy ones read
	// their settings now (game folders, compile tools).
	new GenesisGame;
	if (Options.bLegacyGames && bHavePak)
	{
		new Quake;
		new Quake2;
		new Hexen2;
		new HalfLife;
		new Sin;
		new Heretic2;
	}

	E.GameSets.clear();
	FMapGameSet BuiltIn;
	BuiltIn.Name = "Genesis";
	BuiltIn.Game = "Genesis";
	BuiltIn.EntFile = GenesisGame::GetEntFileName();
	E.GameSets.push_back(BuiltIn);
	if (bHavePak)
	{
		ReadGameSets(E.GameSets);
	}
	E.Prefabs.clear();
	FindPrefabs(Options.EditorContentDir, E.Prefabs);
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
	GE_LOG(LogMapEditor, Display, "Game set: {}", GameSet.Name);
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
