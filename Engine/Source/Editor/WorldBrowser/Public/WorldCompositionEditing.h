// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "World/WorldComposition.h"
#include "Delegates/Delegate.h"

// Writes a new region map that the level editor can open: a flat landscape
// SizeX x SizeY world units, centred on the map origin. Supplied by the host,
// which owns the level document format.
using FCreateRegionMap = TDelegate<bool(const FString& File, double SizeX, double SizeY, FString& Error)>;

// One open world composition. Every edit validates first, records undo and
// leaves the composition unchanged when rejected. No UI dependencies.
class FWorldCompositionEditSession
{
public:
	static constexpr int32 MaxUndo = 100;
	static TUniquePtr<FWorldCompositionEditSession> Open(
	    std::shared_ptr<FAssetRegistry> Registry, const FAssetReference& Composition, FString& Error);
	const FWorldComposition& GetComposition() const
	{
		return Composition;
	}
	const FString& GetFilePath() const
	{
		return FilePath;
	}
	bool IsDirty() const
	{
		return bDirty;
	}

	// Registers an existing Genesis.World map, named after its file.
	bool RegisterMap(const FAssetReference& Map, int32 X, int32 Y, FString& NewTileId, FString& Error);
	// Creates "<composition>Regions/Region N.gmap" beside the composition through
	// the host and registers it at the cell. The new file is kept if registration fails.
	bool CreateRegion(const FCreateRegionMap& Create, int32 X, int32 Y, FString& NewTileId, FString& Error);
	bool MoveTile(const FString& Id, int32 X, int32 Y, FString& Error);
	bool ResizeTile(const FString& Id, int32 SizeX, int32 SizeY, FString& Error);
	bool RenameTile(const FString& Id, const FString& Name, FString& Error);
	bool SetTileHost(const FString& Id, const FString& Host, FString& Error);
	bool RemoveTile(const FString& Id, FString& Error);
	bool SetCellSize(double CellSize, FString& Error);
	// The free footprint nearest the cell, searching outwards ring by ring.
	bool FindFreeCell(int32 X, int32 Y, int32 SizeX, int32 SizeY, int32& OutX, int32& OutY) const;
	// The registry entry for a tile's map, or null with a reason.
	const FAssetData* ResolveMap(const FString& TileId, FString& Error) const;

	bool CanUndo() const
	{
		return !UndoStack.empty();
	}
	bool CanRedo() const
	{
		return !RedoStack.empty();
	}
	bool Undo();
	bool Redo();
	// Keeps the package identity; rescans the registry so new maps resolve.
	bool Save(FString& Error);

private:
	bool Commit(FWorldComposition Next);
	FString UniqueName(const FString& Base) const;
	std::shared_ptr<FAssetRegistry> Registry;
	FAssetPackage Package;
	FString FilePath;
	FWorldComposition Composition;
	TArray<FWorldComposition> UndoStack, RedoStack;
	bool bDirty = false;
};
