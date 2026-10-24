# Game runtime: playing a project's world

A project's startup world, either a single level or a world composition of
many regions, can be played in two ways:

- **Play** (Alt+P) plays it inside the editor's viewport, as Unreal's
  Play-in-Editor.
- **Standalone Game** starts `GenesisGame`, a separate program that links no
  editor, MapCore or editor-UI modules.

Both run the same game:

- `FGameInstance` (`Engine/GameFramework`, after Unreal's `UGameInstance`)
  holds the world, the player, their input, quick save and load, and the HUD's
  text. It has no rendering, so tests drive it directly.
- `FGameViewportClient` (the `Runtime/GameViewport` module, after Unreal's game
  viewport client) draws the loaded regions and handles the keyboard and mouse.
  `MakeGameView` builds the view with its HUD.

```
GenesisGame.exe -project=<file.gproject> [-world=/Game/...] [-d3d12 | -vulkan] [-load] [-playtest=<report.json>]
```

`-playtest` plays for three seconds, then writes a JSON report and quits. The
report gives the world, its region, how many regions were loaded and drawn,
the ticks and frames drawn, any drawing error and the backend.

## Play-in-Editor

- **Play** on the toolbar plays the current level as it is in memory, saved or
  not. It plays a copy made in memory, as Unreal's Play-in-Editor duplicates the
  editor's world, so nothing is written to disk. An unsaved level keeps the same
  identity across plays, so a quick save loads in the next play. Without an open
  level it plays the project's startup world.
- When Play cannot start, a dialog gives the reason, as does the Output Log.
- **Play** in the region editor plays that world composition.
- The game view replaces the Viewport tab's content, and the rest of the editor
  stays usable.
- **Stop**, or Esc when the mouse is not captured, puts the level viewports
  back and gives them the keyboard, so Play or Alt+P plays again. Esc stops on
  the next frame, outside the game view's own key handling. Closing the
  Viewport tab also stops.
- While playing, the movement keys go to the game, so the editor's shortcuts do
  not fire.
- Quick saves go to `Saved/PlayInEditor/QuickSave.json`, kept apart from the
  standalone game's saves.
- **Standalone Game** saves or discards the level as asked, then starts
  `GenesisGame` on the current saved level or the startup world, with the
  editor's graphics backend.

## Startup world

The project descriptor's `DefaultMap` names the startup world: a `/Game` path
to a `Genesis.World` level or a `Genesis.WorldComposition`. `-world=` overrides
it. To set it in the editor:

- **File > Set as Startup World** uses the current level.
- **Set as Startup World** in the region editor uses that world composition.

`FProjectContext::SetDefaultMap` validates the path and rewrites the descriptor
through a temporary file. It keeps every other field, including unknown
extension fields, and leaves the old descriptor in place when writing fails.

When the startup world is a world composition, the editor opens its region
editor at launch instead of loading it as a level.

## The game world and region-relative positions

`FGameWorld` (`World/GameWorld.h`, after Unreal's `UWorld` and its streaming
levels) opens the startup world.

- **A level** is one region at the origin, with the level's asset ID as its
  region ID. It is always loaded and never rebases.
- **A world composition** streams its regions around the player through
  `FWorldCompositionStreaming` and `FLocalWorldTileSource`.

Positions are `FWorldLocation` values: a region's permanent ID plus a position
in that region's map space. They stay correct when regions are moved on the
grid, and their values stay small however large the world is. `FGameWorld`
converts between three kinds of position:

- **Region-local:** stored in `FWorldLocation`.
- **Absolute:** double precision, from the world's origin.
- **Render:** relative to the render origin.

`Normalize` re-expresses a location in the region it has moved into. A point
between regions keeps its region.

**Origin rebasing.** Rendering is relative to an origin cell near the player.
When the player is more than `RebaseCells` (2) cells away, the origin moves to
the player's cell. Region-to-render transforms are rebuilt from integer cells
and double centres, so render-space values never grow beyond a few cells,
whatever the world's size. `FWorldSceneDrawList::Append` draws each loaded
region through its own region-to-render transform.

`SampleGround` returns the highest loaded terrain under a location, through
each terrain actor's inverse transform. `FindPlayerStart` returns the first
`info_player_start` in a loaded region, else the centre of the first region on
the ground. Point entities such as worldspawn, player starts and lights now
load at runtime as data. Brushes are still rejected.

## Player, saved games and region state

`FCharacterMovement` (`GameFramework/`, after Unreal's character movement)
walks with gravity and jumping, or flies. It moves the feet in region space and
normalises across region boundaries. Where nothing is loaded below, the
character holds its height instead of falling.

`FSaveGame` (after Unreal's `USaveGame`) stores:

- the world;
- the player as region ID plus local position;
- the absolute position as a fallback;
- yaw and pitch;
- every region's state;
- a game-wide state object.

Saves are JSON in the project's `Saved/SaveGames` folder, written through a
temporary file and then replaced. `ResolvePlayer` uses the saved region when it
still exists, and the absolute position when the region has been removed.

`FRegionStateStore` holds gameplay changes to each region (doors opened, items
taken), keyed by region ID. It lives outside streaming, so a region that
unloads and loads again finds its state unchanged. The game applies a region's
state when `FGameWorld::Update` reports it loaded.

## Controls

| Input | Action |
|---|---|
| Click / Esc | Capture the mouse to look around / release it |
| W A S D, arrows | Move |
| Shift | Run |
| Space | Jump |
| F | Toggle flying |
| Q / E | Down / up while flying |
| F5 / F9 | Quick save / quick load |
| F10 | Quit |

The HUD shows:

- the world and region;
- the region-local and absolute positions;
- the origin cell;
- how many regions are loaded;
- the frame rate;
- whether the player is walking or flying.

## Not done yet

- **Background region loading.** Loading is synchronous, a few regions per
  update.
- **Collision with meshes, and step-height limits.** The player climbs any
  slope.
- **Distant low-detail terrain.**
- **Gameplay that writes region state.** Scripts or game code will fill it.

## Validation

`GameWorld` tests (`Programs/EngineTests/GameWorldTests.cpp`) cover:

- A level opened as one region, with its player start and ground under a moved
  terrain actor.
- A six-region composition: streaming, walking across a region boundary onto
  higher ground, and rebasing with render positions kept small.
- Absolute and nearest-region lookups.
- Region state across unload and reload.
- Saves that round-trip, survive a region being moved or removed, and reject
  damaged files.
- Multi-region draw lists.

A `GameWorld` test also plays a game instance on a composed world (input,
region changes, quick save and load) and on an unsaved level from memory. A
`ProjectsPackages` test covers `SetDefaultMap`.

`GenesisGameTest` (`Programs/TestApps`) runs the game with a scripted player,
for example on the demo world:

```
GenesisGameTest.exe -project=<WorldDemo.gproject> -world=/Game/Worlds/WorldDemo -selftest=<dir> [-vulkan]
```

It:

1. Starts at the player start.
2. Flies east across three region boundaries, which triggers a rebase.
3. Arrives over Mesa.
4. Quick-saves, moves back to the start and quick-loads.
5. Lands and walks.

It captures `game_start`, `game_flight` and `game_ground`, and passes 15
checks on D3D12 and Vulkan. Started without `-world`, it opens the project's
`DefaultMap`.

The editor automation program (`GenesisEditorTest`) exercises playing through
the real toolbar:

1. Clicks **Play**: Play-in-Editor on the current level, flown and drawn,
   captured as `pie_level`.
2. Presses Esc, which stops and restores the level viewports.
3. Plays the world composition in the viewport, captured as `pie_world`.
4. Clicks **Stop**.
5. On a new unsaved level with a landscape and a player start, plays, clicks
   into the game to capture the mouse, presses Esc twice to release it and
   stop, then clicks **Play** again, a frame apart as a user would.
6. Clicks **Standalone Game** with `-playtest` added, waits for the launched
   game's report, and checks that it played and drew the startup world.

With `-openworld` it also clicks **Set as Startup World**.
