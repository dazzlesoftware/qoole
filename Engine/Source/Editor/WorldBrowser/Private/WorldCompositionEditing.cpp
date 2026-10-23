// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "WorldCompositionEditing.h"
#include "Internationalization/TextLocalization.h"
#include <filesystem>
#include <limits>

namespace
{
	constexpr const char* CompositionSchema = "Genesis.WorldComposition";
	constexpr const char* MapSchema = "Genesis.World";
	constexpr int32 MaxFreeCellSearchRings = 64;
	constexpr int32 MaxRegionNumber = 100000;

	FString LastPathPart(const FString& PackagePath)
	{
		const std::string_view View = PackagePath.View();
		const size_t Slash = View.find_last_of('/');
		return FString(Slash == std::string_view::npos ? View : View.substr(Slash + 1));
	}
	// A footprint probe: fresh identity, name and map, so only placement is checked.
	FWorldCompositionTile Probe(int32 X, int32 Y, int32 SizeX, int32 SizeY)
	{
		FWorldCompositionTile Tile = FWorldCompositionTile::New(FAssetId::New(), {FAssetId::New(), {}}, X, Y);
		Tile.SizeX = SizeX;
		Tile.SizeY = SizeY;
		return Tile;
	}
} // namespace

TUniquePtr<FWorldCompositionEditSession> FWorldCompositionEditSession::Open(
    std::shared_ptr<FAssetRegistry> Registry, const FAssetReference& Reference, FString& Error)
{
	if (!Registry)
	{
		Error = GENESIS_TEXT("The world composition editor requires an asset registry.");
		return {};
	}
	const FAssetData* Data = Registry->Resolve(Reference, Error, CompositionSchema);
	if (!Data)
		return {};
	TUniquePtr<FWorldCompositionEditSession> Session = MakeUnique<FWorldCompositionEditSession>();
	Session->FilePath = Data->FilePath;
	if (!Registry->Load(Reference, Session->Package, Error, CompositionSchema) ||
	    !FWorldComposition::FromPackage(Session->Package, Session->Composition, Error))
		return {};
	Session->Registry = std::move(Registry);
	Error.Empty();
	return Session;
}

bool FWorldCompositionEditSession::Commit(FWorldComposition Next)
{
	if (UndoStack.size() >= static_cast<size_t>(MaxUndo))
		UndoStack.erase(UndoStack.begin());
	UndoStack.push_back(std::move(Composition));
	RedoStack.clear();
	Composition = std::move(Next);
	bDirty = true;
	return true;
}

FString FWorldCompositionEditSession::UniqueName(const FString& Base) const
{
	if (!Composition.FindTileByName(Base))
		return Base;
	for (int32 Number = 2;; ++Number)
	{
		const FString Candidate = FString::Format("{} {}", Base, Number);
		if (!Composition.FindTileByName(Candidate))
			return Candidate;
	}
}

bool FWorldCompositionEditSession::RegisterMap(
    const FAssetReference& Map, int32 X, int32 Y, FString& NewTileId, FString& Error)
{
	const FAssetData* Data = Registry->Resolve(Map, Error, MapSchema);
	if (!Data)
		return false;
	if (Data->Metadata.Kind != EAssetPackageKind::Map)
	{
		Error = GENESIS_TEXT("Only world maps can be registered as regions.");
		return false;
	}
	FWorldCompositionTile Tile =
	    FWorldCompositionTile::New(UniqueName(LastPathPart(Data->PackagePath)), Data->GetReference(), X, Y);
	FWorldComposition Next = Composition;
	if (!Next.AddTile(Tile, Error))
		return false;
	NewTileId = Tile.Id;
	return Commit(std::move(Next));
}

bool FWorldCompositionEditSession::CreateRegion(
    const FCreateRegionMap& Create, int32 X, int32 Y, FString& NewTileId, FString& Error)
{
	if (!Create.IsBound())
	{
		Error = GENESIS_TEXT("This editor host cannot create region maps.");
		return false;
	}
	if (!Composition.CanAddTile(Probe(X, Y, 1, 1), Error))
		return false;
	const std::filesystem::path Source(FilePath.ToWide());
	const std::filesystem::path Folder = Source.parent_path() / (Source.stem().wstring() + L"Regions");
	std::error_code FileError;
	std::filesystem::create_directories(Folder, FileError);
	if (FileError)
	{
		Error = FTextLocalization::Format("Could not create the region folder: {}", FileError.message());
		return false;
	}
	FString File;
	for (int32 Number = 1; Number <= MaxRegionNumber && File.IsEmpty(); ++Number)
	{
		const FString Name = FString::Format("Region {}", Number);
		const std::filesystem::path Candidate = Folder / (Name.ToWide() + L".gmap");
		if (!Composition.FindTileByName(Name) && !std::filesystem::exists(Candidate, FileError))
			File = FString::FromWide(Candidate.wstring());
	}
	if (File.IsEmpty())
	{
		Error = GENESIS_TEXT("No free region file name is available.");
		return false;
	}
	const double Size = Composition.GetCellSize();
	if (!Create.Execute(File, Size, Size, Error))
		return false;
	Registry->Scan();
	const FAssetData* Data = Registry->FindByFile(File);
	if (!Data)
	{
		Error = FTextLocalization::Format("The new region map {} is outside the mounted content folders.", File);
		return false;
	}
	return RegisterMap(Data->GetReference(), X, Y, NewTileId, Error);
}

bool FWorldCompositionEditSession::MoveTile(const FString& Id, int32 X, int32 Y, FString& Error)
{
	const FWorldCompositionTile* Found = Composition.FindTile(Id);
	if (!Found)
	{
		Error = GENESIS_TEXT("Unknown world tile.");
		return false;
	}
	if (Found->X == X && Found->Y == Y)
		return true;
	FWorldCompositionTile Tile = *Found;
	Tile.X = X;
	Tile.Y = Y;
	FWorldComposition Next = Composition;
	return Next.UpdateTile(std::move(Tile), Error) && Commit(std::move(Next));
}
bool FWorldCompositionEditSession::ResizeTile(const FString& Id, int32 SizeX, int32 SizeY, FString& Error)
{
	const FWorldCompositionTile* Found = Composition.FindTile(Id);
	if (!Found)
	{
		Error = GENESIS_TEXT("Unknown world tile.");
		return false;
	}
	if (Found->SizeX == SizeX && Found->SizeY == SizeY)
		return true;
	FWorldCompositionTile Tile = *Found;
	Tile.SizeX = SizeX;
	Tile.SizeY = SizeY;
	FWorldComposition Next = Composition;
	return Next.UpdateTile(std::move(Tile), Error) && Commit(std::move(Next));
}
bool FWorldCompositionEditSession::RenameTile(const FString& Id, const FString& Name, FString& Error)
{
	const FWorldCompositionTile* Found = Composition.FindTile(Id);
	if (!Found)
	{
		Error = GENESIS_TEXT("Unknown world tile.");
		return false;
	}
	if (Found->Name == Name)
		return true;
	FWorldCompositionTile Tile = *Found;
	Tile.Name = Name;
	FWorldComposition Next = Composition;
	return Next.UpdateTile(std::move(Tile), Error) && Commit(std::move(Next));
}
bool FWorldCompositionEditSession::SetTileHost(const FString& Id, const FString& Host, FString& Error)
{
	const FWorldCompositionTile* Found = Composition.FindTile(Id);
	if (!Found)
	{
		Error = GENESIS_TEXT("Unknown world tile.");
		return false;
	}
	if (Found->Host == Host)
		return true;
	FWorldCompositionTile Tile = *Found;
	Tile.Host = Host;
	FWorldComposition Next = Composition;
	return Next.UpdateTile(std::move(Tile), Error) && Commit(std::move(Next));
}
bool FWorldCompositionEditSession::RemoveTile(const FString& Id, FString& Error)
{
	FWorldComposition Next = Composition;
	return Next.RemoveTile(Id, Error) && Commit(std::move(Next));
}
bool FWorldCompositionEditSession::SetCellSize(double CellSize, FString& Error)
{
	if (Composition.GetCellSize() == CellSize)
		return true;
	FWorldComposition Next = Composition;
	return Next.SetCellSize(CellSize, Error) && Commit(std::move(Next));
}

bool FWorldCompositionEditSession::FindFreeCell(
    int32 X, int32 Y, int32 SizeX, int32 SizeY, int32& OutX, int32& OutY) const
{
	FString Error;
	FWorldCompositionTile Candidate = Probe(X, Y, SizeX, SizeY);
	const TFunction<bool(int32, int32)> Try = [&](int32 DX, int32 DY) -> bool
	{
		const int64 CandidateX = static_cast<int64>(X) + DX, CandidateY = static_cast<int64>(Y) + DY;
		if (CandidateX < std::numeric_limits<int32>::min() || CandidateX > std::numeric_limits<int32>::max() ||
		    CandidateY < std::numeric_limits<int32>::min() || CandidateY > std::numeric_limits<int32>::max())
			return false;
		Candidate.X = static_cast<int32>(CandidateX);
		Candidate.Y = static_cast<int32>(CandidateY);
		if (!Composition.CanAddTile(Candidate, Error))
			return false;
		OutX = Candidate.X;
		OutY = Candidate.Y;
		return true;
	};
	if (Try(0, 0))
		return true;
	// Walk each ring's perimeter only: its top and bottom rows, then its sides.
	for (int32 Ring = 1; Ring <= MaxFreeCellSearchRings; ++Ring)
	{
		for (int32 D = -Ring; D <= Ring; ++D)
			if (Try(D, -Ring) || Try(D, Ring))
				return true;
		for (int32 D = -Ring + 1; D < Ring; ++D)
			if (Try(-Ring, D) || Try(Ring, D))
				return true;
	}
	return false;
}

const FAssetData* FWorldCompositionEditSession::ResolveMap(const FString& TileId, FString& Error) const
{
	const FWorldCompositionTile* Tile = Composition.FindTile(TileId);
	if (!Tile)
	{
		Error = GENESIS_TEXT("Unknown world tile.");
		return nullptr;
	}
	return Registry->Resolve(Tile->Map, Error, MapSchema);
}

bool FWorldCompositionEditSession::Undo()
{
	if (UndoStack.empty())
		return false;
	RedoStack.push_back(std::move(Composition));
	Composition = std::move(UndoStack.back());
	UndoStack.pop_back();
	bDirty = true;
	return true;
}
bool FWorldCompositionEditSession::Redo()
{
	if (RedoStack.empty())
		return false;
	UndoStack.push_back(std::move(Composition));
	Composition = std::move(RedoStack.back());
	RedoStack.pop_back();
	bDirty = true;
	return true;
}

bool FWorldCompositionEditSession::Save(FString& Error)
{
	FAssetPackage Next = Package;
	Composition.ToPackage(Next);
	if (!FAssetPackageStore::Save(FilePath, Next, Error))
		return false;
	Package = std::move(Next);
	bDirty = false;
	Registry->Scan();
	Error.Empty();
	return true;
}
