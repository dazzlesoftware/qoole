// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "World/WorldComposition.h"
#include "World/WorldCompositionLimits.h"
#include "Misc/ValidationContext.h"
#include "Internationalization/TextLocalization.h"
#include <cmath>
#include <limits>

namespace
{
	using J = FJsonValue;
	constexpr size_t NoTile = std::numeric_limits<size_t>::max();
	constexpr const char* CompositionSchema = "Genesis.WorldComposition";

	int32 FloorDiv(int64 Value, int32 Divisor)
	{
		int64 Quotient = Value / Divisor;
		if (Value % Divisor != 0 && Value < 0)
			--Quotient;
		return static_cast<int32>(Quotient);
	}
	int64 BucketKey(int32 BucketX, int32 BucketY)
	{
		return (static_cast<int64>(BucketX) << 32) ^ static_cast<int64>(static_cast<uint32>(BucketY));
	}
	bool ValidText(const FString& Value, int32 Limit)
	{
		return Value.Len() <= Limit && Value.View().find('\0') == std::string_view::npos;
	}
	bool ValidCellSize(double CellSize)
	{
		return std::isfinite(CellSize) && CellSize >= WorldCompositionLimits::MinCellSize &&
		    CellSize <= WorldCompositionLimits::MaxCellSize;
	}
	bool InsideWorld(const FWorldCompositionTile& Tile, double CellSize)
	{
		const double Limit = WorldCompositionLimits::MaxWorldExtent;
		for (int64 Cell : {static_cast<int64>(Tile.X), static_cast<int64>(Tile.X) + Tile.SizeX,
		         static_cast<int64>(Tile.Y), static_cast<int64>(Tile.Y) + Tile.SizeY})
			if (std::abs(static_cast<double>(Cell) * CellSize) > Limit)
				return false;
		return true;
	}
	bool RangesOverlap(int32 A, int32 SizeA, int32 B, int32 SizeB)
	{
		return static_cast<int64>(A) < static_cast<int64>(B) + SizeB &&
		    static_cast<int64>(B) < static_cast<int64>(A) + SizeA;
	}
	bool RangesTouch(int32 A, int32 SizeA, int32 B, int32 SizeB)
	{
		return static_cast<int64>(A) + SizeA == B || static_cast<int64>(B) + SizeB == A;
	}
	int32 ReadInt(FValidationContext& Validation, const J& In, int64 Min, int64 Max)
	{
		const double Value = In.AsNumber();
		if (!Validation.Check(In.GetType() == EJsonType::Number && std::isfinite(Value) && Value == std::floor(Value) &&
		            Value >= static_cast<double>(Min) && Value <= static_cast<double>(Max),
		        GENESIS_TEXT("Invalid world tile coordinate.")))
		{
			return 0;
		}
		return static_cast<int32>(Value);
	}
	FString ReadText(FValidationContext& Validation, const J& In, int32 Limit)
	{
		if (!Validation.Check(In.GetType() == EJsonType::String && ValidText(In.AsString(), Limit),
		        GENESIS_TEXT("Invalid world tile string.")))
		{
			return {};
		}
		return In.AsString();
	}
} // namespace

FWorldCompositionTile FWorldCompositionTile::New(const FString& Name, const FAssetReference& Map, int32 X, int32 Y)
{
	FWorldCompositionTile Tile;
	Tile.Id = FAssetId::New();
	Tile.Name = Name;
	Tile.Map = Map;
	Tile.X = X;
	Tile.Y = Y;
	return Tile;
}
bool FWorldCompositionTile::Overlaps(const FWorldCompositionTile& Other) const
{
	return RangesOverlap(X, SizeX, Other.X, Other.SizeX) && RangesOverlap(Y, SizeY, Other.Y, Other.SizeY);
}
bool FWorldCompositionTile::IsAdjacent(const FWorldCompositionTile& Other) const
{
	return (RangesTouch(X, SizeX, Other.X, Other.SizeX) && RangesOverlap(Y, SizeY, Other.Y, Other.SizeY)) ||
	    (RangesTouch(Y, SizeY, Other.Y, Other.SizeY) && RangesOverlap(X, SizeX, Other.X, Other.SizeX));
}
FJsonValue FWorldCompositionTile::ToJson() const
{
	FJsonValue Json = J::MakeObject();
	Json["Id"] = Id;
	Json["Name"] = Name;
	Json["Map"] = Map.ToJson();
	Json["X"] = X;
	Json["Y"] = Y;
	Json["SizeX"] = SizeX;
	Json["SizeY"] = SizeY;
	Json["Host"] = Host;
	return Json;
}
TOptional<FWorldCompositionTile> FWorldCompositionTile::FromJson(const FJsonValue& Json, FString& Error)
{
	FValidationContext Validation(Error);
	if (!Validation.Check(Json.IsObject(), GENESIS_TEXT("Expected a world tile object.")))
		return {};
	FWorldCompositionTile Tile;
	Tile.Id = ReadText(Validation, Json.Get("Id"), WorldCompositionLimits::MaxNameLength);
	Tile.Name = ReadText(Validation, Json.Get("Name"), WorldCompositionLimits::MaxNameLength);
	Tile.Host = ReadText(Validation, Json.Get("Host"), WorldCompositionLimits::MaxHostLength);
	const int64 MinCell = std::numeric_limits<int32>::min(), MaxCell = std::numeric_limits<int32>::max();
	Tile.X = ReadInt(Validation, Json.Get("X"), MinCell, MaxCell);
	Tile.Y = ReadInt(Validation, Json.Get("Y"), MinCell, MaxCell);
	Tile.SizeX = ReadInt(Validation, Json.Get("SizeX"), 1, WorldCompositionLimits::MaxTileCells);
	Tile.SizeY = ReadInt(Validation, Json.Get("SizeY"), 1, WorldCompositionLimits::MaxTileCells);
	if (!Validation.IsValid())
		return {};
	FString ReferenceError;
	TOptional<FAssetReference> Map = FAssetReference::FromJson(Json.Get("Map"), ReferenceError);
	if (!Validation.Check(Map.has_value(), GENESIS_TEXT("Invalid world tile map reference.")))
		return {};
	Tile.Map = *Map;
	Error.Empty();
	return Tile;
}

double FWorldTileBounds::GetDistance(double X, double Y) const
{
	const double DX = std::max({MinX - X, 0.0, X - MaxX});
	const double DY = std::max({MinY - Y, 0.0, Y - MaxY});
	return std::hypot(DX, DY);
}

FWorldComposition::FWorldComposition() : CellSize(WorldCompositionLimits::DefaultCellSize) {}

bool FWorldComposition::SetCellSize(double NewCellSize, FString& Error)
{
	FValidationContext Validation(Error);
	if (!Validation.Check(ValidCellSize(NewCellSize), GENESIS_TEXT("Invalid world grid cell size.")))
		return false;
	for (const FWorldCompositionTile& Tile : Tiles)
	{
		if (!Validation.Check(InsideWorld(Tile, NewCellSize),
		        FTextLocalization::Format("World tile {} would lie outside the world bounds.", Tile.Name)))
		{
			return false;
		}
	}
	CellSize = NewCellSize;
	Error.Empty();
	return true;
}

const FWorldCompositionTile* FWorldComposition::FindTile(const FString& Id) const
{
	const TMap<std::string, size_t>::const_iterator It = ById.find(Id.ToStdString());
	return It == ById.end() ? nullptr : &Tiles[It->second];
}
const FWorldCompositionTile* FWorldComposition::FindTileByName(const FString& Name) const
{
	const TMap<std::string, size_t>::const_iterator It = ByName.find(Name.ToStdString());
	return It == ByName.end() ? nullptr : &Tiles[It->second];
}
const FWorldCompositionTile* FWorldComposition::FindTileAt(double X, double Y) const
{
	if (!std::isfinite(X) || !std::isfinite(Y) || std::abs(X) > WorldCompositionLimits::MaxWorldExtent ||
	    std::abs(Y) > WorldCompositionLimits::MaxWorldExtent)
		return nullptr;
	const int64 CellX = static_cast<int64>(std::floor(X / CellSize));
	const int64 CellY = static_cast<int64>(std::floor(Y / CellSize));
	const TMap<int64, TArray<size_t>>::const_iterator It = Buckets.find(BucketKey(
	    FloorDiv(CellX, WorldCompositionLimits::MaxTileCells), FloorDiv(CellY, WorldCompositionLimits::MaxTileCells)));
	if (It == Buckets.end())
		return nullptr;
	for (size_t Index : It->second)
	{
		const FWorldCompositionTile& Tile = Tiles[Index];
		if (CellX >= Tile.X && CellX < static_cast<int64>(Tile.X) + Tile.SizeX && CellY >= Tile.Y &&
		    CellY < static_cast<int64>(Tile.Y) + Tile.SizeY)
			return &Tile;
	}
	return nullptr;
}

bool FWorldComposition::ValidateTile(const FWorldCompositionTile& Tile, size_t Skip, FString& Error) const
{
	FValidationContext Validation(Error);
	if (!Validation.Check(Skip != NoTile || Tiles.size() < static_cast<size_t>(WorldCompositionLimits::MaxTiles),
	        GENESIS_TEXT("The world has too many tiles.")))
	{
		return false;
	}
	if (!Validation.Check(FAssetId::IsValid(Tile.Id), GENESIS_TEXT("A world tile needs a valid identity.")))
		return false;
	if (!Validation.Check(!Tile.Name.IsEmpty() && ValidText(Tile.Name, WorldCompositionLimits::MaxNameLength),
	        GENESIS_TEXT("A world tile needs a name.")))
	{
		return false;
	}
	FString ReferenceError;
	if (!Validation.Check(!Tile.Map.IsNull() && FAssetReference::FromJson(Tile.Map.ToJson(), ReferenceError),
	        FTextLocalization::Format("World tile {} needs a valid map reference.", Tile.Name)))
	{
		return false;
	}
	if (!Validation.Check(ValidText(Tile.Host, WorldCompositionLimits::MaxHostLength),
	        FTextLocalization::Format("World tile {} has an invalid host.", Tile.Name)))
	{
		return false;
	}
	if (!Validation.Check(Tile.SizeX >= 1 && Tile.SizeX <= WorldCompositionLimits::MaxTileCells && Tile.SizeY >= 1 &&
	            Tile.SizeY <= WorldCompositionLimits::MaxTileCells,
	        FTextLocalization::Format("World tile {} has an invalid size.", Tile.Name)))
	{
		return false;
	}
	if (!Validation.Check(InsideWorld(Tile, CellSize),
	        FTextLocalization::Format("World tile {} lies outside the world bounds.", Tile.Name)))
	{
		return false;
	}

	const TMap<std::string, size_t>::const_iterator SameId = ById.find(Tile.Id.ToStdString());
	if (!Validation.Check(SameId == ById.end() || SameId->second == Skip,
	        FTextLocalization::Format("World tile {} has a duplicate identity.", Tile.Name)))
	{
		return false;
	}
	const TMap<std::string, size_t>::const_iterator SameName = ByName.find(Tile.Name.ToStdString());
	if (!Validation.Check(SameName == ByName.end() || SameName->second == Skip,
	        FTextLocalization::Format("Another world tile is already named {}.", Tile.Name)))
	{
		return false;
	}
	for (const TPair<const TMap<std::string, size_t>*, const FString*> Key :
	    {TPair<const TMap<std::string, size_t>*, const FString*>(&ByMapId, &Tile.Map.Id),
	        TPair<const TMap<std::string, size_t>*, const FString*>(&ByMapPath, &Tile.Map.Path)})
	{
		if (Key.second->IsEmpty())
			continue;
		const TMap<std::string, size_t>::const_iterator Same = Key.first->find(Key.second->ToStdString());
		if (Same != Key.first->end() && Same->second != Skip)
		{
			return Validation.Fail(FTextLocalization::Format(
			    "World tile {} uses a map already registered by tile {}.", Tile.Name, Tiles[Same->second].Name));
		}
	}

	const int32 Bucket = WorldCompositionLimits::MaxTileCells;
	for (int32 BucketX = FloorDiv(Tile.X, Bucket);
	    BucketX <= FloorDiv(static_cast<int64>(Tile.X) + Tile.SizeX - 1, Bucket); ++BucketX)
		for (int32 BucketY = FloorDiv(Tile.Y, Bucket);
		    BucketY <= FloorDiv(static_cast<int64>(Tile.Y) + Tile.SizeY - 1, Bucket); ++BucketY)
		{
			const TMap<int64, TArray<size_t>>::const_iterator It = Buckets.find(BucketKey(BucketX, BucketY));
			if (It == Buckets.end())
				continue;
			for (size_t Other : It->second)
			{
				if (!Validation.Check(Other == Skip || !Tile.Overlaps(Tiles[Other]),
				        FTextLocalization::Format("World tile {} overlaps tile {}.", Tile.Name, Tiles[Other].Name)))
				{
					return false;
				}
			}
		}
	Error.Empty();
	return true;
}

void FWorldComposition::IndexTile(size_t TileIndex)
{
	const FWorldCompositionTile& Tile = Tiles[TileIndex];
	ById[Tile.Id.ToStdString()] = TileIndex;
	ByName[Tile.Name.ToStdString()] = TileIndex;
	if (!Tile.Map.Id.IsEmpty())
		ByMapId[Tile.Map.Id.ToStdString()] = TileIndex;
	if (!Tile.Map.Path.IsEmpty())
		ByMapPath[Tile.Map.Path.ToStdString()] = TileIndex;
	const int32 Bucket = WorldCompositionLimits::MaxTileCells;
	for (int32 BucketX = FloorDiv(Tile.X, Bucket);
	    BucketX <= FloorDiv(static_cast<int64>(Tile.X) + Tile.SizeX - 1, Bucket); ++BucketX)
		for (int32 BucketY = FloorDiv(Tile.Y, Bucket);
		    BucketY <= FloorDiv(static_cast<int64>(Tile.Y) + Tile.SizeY - 1, Bucket); ++BucketY)
			Buckets[BucketKey(BucketX, BucketY)].push_back(TileIndex);
}
void FWorldComposition::RebuildIndex()
{
	ById.clear();
	ByName.clear();
	ByMapId.clear();
	ByMapPath.clear();
	Buckets.clear();
	for (size_t I = 0; I < Tiles.size(); ++I)
		IndexTile(I);
}

bool FWorldComposition::CanAddTile(const FWorldCompositionTile& Tile, FString& Error) const
{
	return ValidateTile(Tile, NoTile, Error);
}
bool FWorldComposition::CanUpdateTile(const FWorldCompositionTile& Tile, FString& Error) const
{
	const TMap<std::string, size_t>::const_iterator It = ById.find(Tile.Id.ToStdString());
	if (It == ById.end())
	{
		Error = GENESIS_TEXT("Unknown world tile.");
		return false;
	}
	return ValidateTile(Tile, It->second, Error);
}
bool FWorldComposition::AddTile(FWorldCompositionTile Tile, FString& Error)
{
	if (!ValidateTile(Tile, NoTile, Error))
		return false;
	Tiles.push_back(std::move(Tile));
	IndexTile(Tiles.size() - 1);
	return true;
}
bool FWorldComposition::UpdateTile(FWorldCompositionTile Tile, FString& Error)
{
	const TMap<std::string, size_t>::const_iterator It = ById.find(Tile.Id.ToStdString());
	if (It == ById.end())
	{
		Error = GENESIS_TEXT("Unknown world tile.");
		return false;
	}
	const size_t Index = It->second;
	if (!ValidateTile(Tile, Index, Error))
		return false;
	Tiles[Index] = std::move(Tile);
	RebuildIndex();
	return true;
}
bool FWorldComposition::RemoveTile(const FString& Id, FString& Error)
{
	const TMap<std::string, size_t>::const_iterator It = ById.find(Id.ToStdString());
	if (It == ById.end())
	{
		Error = GENESIS_TEXT("Unknown world tile.");
		return false;
	}
	Tiles.erase(Tiles.begin() + static_cast<std::ptrdiff_t>(It->second));
	RebuildIndex();
	Error.Empty();
	return true;
}

void FWorldComposition::GetNeighbours(const FString& Id, TArray<const FWorldCompositionTile*>& Out) const
{
	Out.clear();
	const FWorldCompositionTile* Tile = FindTile(Id);
	if (!Tile)
		return;
	// Neighbours touch the footprint grown by one cell, which spans at most
	// two buckets more a side than the tile itself.
	const int32 Bucket = WorldCompositionLimits::MaxTileCells;
	TSet<size_t> Seen;
	for (int32 BucketX = FloorDiv(static_cast<int64>(Tile->X) - 1, Bucket);
	    BucketX <= FloorDiv(static_cast<int64>(Tile->X) + Tile->SizeX, Bucket); ++BucketX)
		for (int32 BucketY = FloorDiv(static_cast<int64>(Tile->Y) - 1, Bucket);
		    BucketY <= FloorDiv(static_cast<int64>(Tile->Y) + Tile->SizeY, Bucket); ++BucketY)
		{
			const TMap<int64, TArray<size_t>>::const_iterator It = Buckets.find(BucketKey(BucketX, BucketY));
			if (It == Buckets.end())
				continue;
			for (size_t Other : It->second)
				if (Seen.insert(Other).second && Tile->IsAdjacent(Tiles[Other]))
					Out.push_back(&Tiles[Other]);
		}
	std::sort(Out.begin(), Out.end(), [](const FWorldCompositionTile* A, const FWorldCompositionTile* B)
	{ return A->Id.ToStdString() < B->Id.ToStdString(); });
}

void FWorldComposition::GetTilesWithinDistance(
    double X, double Y, double Distance, TArray<const FWorldCompositionTile*>& Out) const
{
	Out.clear();
	if (!std::isfinite(X) || !std::isfinite(Y) || !std::isfinite(Distance) || Distance < 0 ||
	    std::abs(X) - Distance > WorldCompositionLimits::MaxWorldExtent ||
	    std::abs(Y) - Distance > WorldCompositionLimits::MaxWorldExtent)
		return;
	TArray<TPair<double, const FWorldCompositionTile*>> Found;
	const double Reach = static_cast<double>(WorldCompositionLimits::MaxTileCells) * CellSize;
	const double BucketsX = std::floor((X + Distance) / Reach) - std::floor((X - Distance) / Reach) + 1;
	const double BucketsY = std::floor((Y + Distance) / Reach) - std::floor((Y - Distance) / Reach) + 1;
	if (BucketsX * BucketsY > static_cast<double>(Tiles.size()))
	{
		for (const FWorldCompositionTile& Tile : Tiles)
		{
			const double D = GetTileBounds(Tile).GetDistance(X, Y);
			if (D <= Distance)
				Found.push_back({D, &Tile});
		}
	}
	else
	{
		TSet<size_t> Seen;
		const int32 MinBucketX = static_cast<int32>(std::floor((X - Distance) / Reach));
		const int32 MinBucketY = static_cast<int32>(std::floor((Y - Distance) / Reach));
		for (int32 BucketX = MinBucketX; BucketX < MinBucketX + static_cast<int32>(BucketsX); ++BucketX)
			for (int32 BucketY = MinBucketY; BucketY < MinBucketY + static_cast<int32>(BucketsY); ++BucketY)
			{
				const TMap<int64, TArray<size_t>>::const_iterator It = Buckets.find(BucketKey(BucketX, BucketY));
				if (It == Buckets.end())
					continue;
				for (size_t Index : It->second)
				{
					if (!Seen.insert(Index).second)
						continue;
					const double D = GetTileBounds(Tiles[Index]).GetDistance(X, Y);
					if (D <= Distance)
						Found.push_back({D, &Tiles[Index]});
				}
			}
	}
	std::sort(Found.begin(), Found.end(),
	    [](const TPair<double, const FWorldCompositionTile*>& A, const TPair<double, const FWorldCompositionTile*>& B)
	{ return A.first != B.first ? A.first < B.first : A.second->Id.ToStdString() < B.second->Id.ToStdString(); });
	for (const TPair<double, const FWorldCompositionTile*>& Item : Found)
		Out.push_back(Item.second);
}

FWorldTileBounds FWorldComposition::GetTileBounds(const FWorldCompositionTile& Tile) const
{
	FWorldTileBounds Bounds;
	Bounds.MinX = static_cast<double>(Tile.X) * CellSize;
	Bounds.MinY = static_cast<double>(Tile.Y) * CellSize;
	Bounds.MaxX = (static_cast<double>(Tile.X) + Tile.SizeX) * CellSize;
	Bounds.MaxY = (static_cast<double>(Tile.Y) + Tile.SizeY) * CellSize;
	return Bounds;
}
Vector3d FWorldComposition::GetTileOrigin(const FWorldCompositionTile& Tile) const
{
	const FWorldTileBounds Bounds = GetTileBounds(Tile);
	return Vector3d(static_cast<float>((Bounds.MinX + Bounds.MaxX) * 0.5),
	    static_cast<float>((Bounds.MinY + Bounds.MaxY) * 0.5), 0.0f);
}

FJsonValue FWorldComposition::ToJson() const
{
	FJsonValue Json = J::MakeObject();
	Json["Version"] = WorldCompositionLimits::SchemaVersion;
	Json["CellSize"] = CellSize;
	Json["Tiles"] = J::MakeArray();
	for (const FWorldCompositionTile& Tile : Tiles)
		Json["Tiles"].Push(Tile.ToJson());
	return Json;
}
TOptional<FWorldComposition> FWorldComposition::FromJson(const FJsonValue& Json, FString& Error)
{
	FValidationContext Validation(Error);
	if (!Validation.Check(Json.IsObject() && Json.Get("Version").GetType() == EJsonType::Number &&
	            Json.Get("Version").AsNumber() == WorldCompositionLimits::SchemaVersion,
	        GENESIS_TEXT("Unsupported world composition version.")))
	{
		return {};
	}
	FWorldComposition Result;
	if (!Validation.Check(
	        Json.Get("CellSize").GetType() == EJsonType::Number && ValidCellSize(Json.Get("CellSize").AsNumber()),
	        GENESIS_TEXT("Invalid world grid cell size.")))
	{
		return {};
	}
	Result.CellSize = Json.Get("CellSize").AsNumber();
	const FJsonValue& TilesJson = Json.Get("Tiles");
	if (!Validation.Check(TilesJson.IsArray() && TilesJson.Num() <= WorldCompositionLimits::MaxTiles,
	        GENESIS_TEXT("Invalid or excessive world tile list.")))
	{
		return {};
	}
	Result.Tiles.reserve(TilesJson.GetItems().size());
	for (const FJsonValue& TileJson : TilesJson.GetItems())
	{
		TOptional<FWorldCompositionTile> Tile = FWorldCompositionTile::FromJson(TileJson, Error);
		if (!Tile || !Result.AddTile(std::move(*Tile), Error))
			return {};
	}
	Error.Empty();
	return Result;
}

void FWorldComposition::ToPackage(FAssetPackage& Package) const
{
	Package.Kind = EAssetPackageKind::Map;
	Package.Schema = CompositionSchema;
	Package.SchemaVersion = WorldCompositionLimits::SchemaVersion;
	Package.Exports = J::MakeObject();
	Package.Exports["Composition"] = ToJson();
	Package.Bulk.clear();
	Package.Dependencies.clear();
	for (const FWorldCompositionTile& Tile : Tiles)
		if (!Tile.Map.Path.IsEmpty())
			Package.Dependencies.push_back(Tile.Map.Path);
}
bool FWorldComposition::FromPackage(const FAssetPackage& Package, FWorldComposition& Composition, FString& Error)
{
	FValidationContext Validation(Error);
	if (!Validation.Check(Package.Schema == CompositionSchema && Package.Kind == EAssetPackageKind::Map,
	        GENESIS_TEXT("Not a world composition package.")))
	{
		return false;
	}
	if (!Validation.Check(Package.SchemaVersion == WorldCompositionLimits::SchemaVersion,
	        GENESIS_TEXT("Unsupported world composition version.")))
	{
		return false;
	}
	TOptional<FWorldComposition> Result = FromJson(Package.Exports.Get("Composition"), Error);
	if (!Result)
		return false;
	Composition = std::move(*Result);
	return true;
}
