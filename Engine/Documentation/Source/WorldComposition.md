# World composition: registered regions

A composed world is many independently saved regions instead of one large
landscape. Each region is an ordinary `Genesis.World` map, usually holding its
own landscape, foliage and actors, registered on the world's grid. This follows
Unreal's world composition (level tiles placed in a persistent world and
streamed by distance); a tile is the Genesis name for a region. The code lives
in the runtime Engine module under `World/`, with no editor dependencies.

## Registry

`FWorldComposition` owns the tiles and a grid of `CellSize` world units
(default 16384). Each `FWorldCompositionTile` has:

- `Id`: a permanent UUID, kept when the tile is moved, resized or renamed.
- `Name`: unique within the world.
- `Map`: an asset reference to the region's map. Each map is registered once.
- `X`, `Y`, `SizeX`, `SizeY`: its footprint in whole grid cells, measured from its
  -X/-Y corner. A tile spans 1 to 64 cells a side.
- `Host`: empty for tiles this process loads. It is reserved for region servers.

The map's local origin is the centre of the tile's footprint, matching a
landscape centred on its actor. `GetTileOrigin` gives that point in the world,
and `GetTileBounds` gives the footprint.

Registration is validated before it commits, and a rejected `AddTile`,
`UpdateTile`, `RemoveTile` or `SetCellSize` leaves the composition unchanged.
Footprints must not overlap. Tiles may share edges and corners. Overlap is
tested on integer cells, so the test is exact. A bucket index keeps lookups
local: registration, `FindTileAt`, `GetNeighbours` (tiles sharing an edge, for
boundary matching and future handoff) and `GetTilesWithinDistance` (nearest
first). Footprints must stay inside the scene's ±1e9 coordinate bound.
Limits and defaults are in `World/WorldCompositionLimits.h`.

## Persistence

`ToPackage` / `FromPackage` store the registry as a `Genesis.WorldComposition`
map package, schema version 1:

```json
{ "Composition": { "Version": 1, "CellSize": 16384, "Tiles": [
  { "Id": "…", "Name": "Home", "Map": { "Id": "…", "Path": "/Game/Regions/Home" },
    "X": 0, "Y": 0, "SizeX": 1, "SizeY": 1, "Host": "" } ] } }
```

Package dependencies list the tile maps' logical paths. Region maps are
unchanged `Genesis.World` packages, so each region is saved, versioned and
edited on its own. Loading revalidates every tile, so a hand-edited file with
overlapping or duplicate registrations is rejected without changing the
caller's composition.

## Streaming

`FWorldCompositionStreaming` keeps the tiles around one viewer loaded. Each
`Update(Composition, ViewLocation)`:

1. Releases tiles that were removed, changed map or host, or lie beyond
   `UnloadDistance`.
2. Loads up to `MaxLoadsPerUpdate` of the nearest unloaded tiles within
   `LoadDistance`.

`UnloadDistance` must be at least `LoadDistance`. The gap stops a viewer
standing on a boundary from loading and unloading the same tile every frame.
Distances are horizontal, from the viewer to the nearest point of a footprint.
Moving a loaded tile updates its transform without reloading it.

Each streamed tile carries its `FWorldSceneSnapshot` and a `ToWorld` matrix.
Snapshot item transforms are in the region map's space, so renderers compose
them with the tile's `ToWorld`. Changes are reported as loaded, unloaded and
failed tile IDs.

Content comes through `IWorldTileSource`. `FLocalWorldTileSource` loads the
map through an `FAssetRegistry` in this process and rejects hosted tiles. A
failed tile records a localized error and is not retried on every update. It
is retried after `Retry`, after its map or host registration changes, or once
the viewer has moved beyond `UnloadDistance` and come back.

## Toward region servers

The registry already carries what a multi-server world needs: permanent tile
IDs, an owner-neutral `Host` field, edge neighbours and a tile-source interface.
A networked `IWorldTileSource` can stream hosted tiles without changing
callers. Still to come:

- A region-server protocol and tile source.
- Player handoff across tile boundaries.
- Ownership and access rules.
- Editor tools for registering and arranging tiles.
- Matching landscape heights along shared edges.
- Origin rebasing. Tile transforms use single-precision vectors, so content
  far from the world origin loses precision.
- Asynchronous loading. Streaming is currently synchronous, bounded by the
  per-update load budget.

## Validation

`WorldComposition` tests (`Programs/EngineTests/WorldCompositionTests.cpp`)
cover:

- Overlap, duplicate identity, name and map, size and bounds rejection.
- Point lookup, including negative coordinates.
- Edge-only neighbours.
- Move, rename and remove rollback.
- Package round trips and malformed or overlapping files.
- Distance queries, checked against a full scan.
- Streaming budget, load/unload hysteresis, moved and removed tiles.
- Failure retry rules.
- End-to-end loading of two saved region maps, each with its own landscape,
  through the asset registry.
