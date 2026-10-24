# Landscape and foliage module boundaries

Features own their runtime data and editor tools in separate build modules.
Each has Public/ and Private/ directories and a .vcxproj target in Genesis.slnx.

| Module | Owns | Main dependencies |
| --- | --- | --- |
| Runtime/Landscape | Heightfields, paint weights, terrain components, splines and generated spline meshes | Core, Geometry |
| Runtime/Foliage | Foliage types, instances and instance transforms | Core, Geometry |
| Runtime/GameViewport | The game view shared by GenesisGame and Play-in-Editor: drawing streamed regions from the player, game input and the HUD ([GameRuntime.md](GameRuntime.md)) | Engine, Renderer, Slate, SlateRHIRenderer |
| Editor/LandscapeEditor | Sculpt, paint, terrain creation/import workflows, spline interaction, overlays and the Landscape panel | MapEditor, Landscape, LandscapeEditorUtilities |
| Editor/FoliageEditor | Foliage paint, erase, single placement, the Foliage panel and type settings | MapEditor, Foliage |
| Editor/WorldBrowser | World composition editing: the region grid, details, registration, region creation through a host callback, undo and saving | Engine, Slate, EditorFramework |
| Editor/LandscapeEditorUtilities | Heightmap/weightmap image and RAW file conversion | Core, Geometry, Landscape, ApplicationCore file-dialog types |

Runtime features have no dependency on Engine, Renderer, UI or Editor. Engine
uses the feature data through its component codecs and world snapshots. Renderer
owns GPU resources, landscape drawing and foliage instancing and consumes the
runtime feature APIs. A feature's editor may use runtime rendering services, but
runtime code must never import that editor's headers.

MapEditor provides shared document, viewport and transaction adapters. The two
feature editors do not depend on each other. Shared brush settings live in
MapBrushSettings; MapTerrainAttachments is the transitional document bridge that
records foliage changes after a terrain edit. The actual reprojection runs in
Runtime/Engine through `SnapFoliageToHeightfield`, using immutable data and local
space transforms without Object, MapCore or UI dependencies. It has no dependency on either
feature editor. These bridges will move into native editor services as the legacy
document is retired. Existing Map-prefixed type names are retained during that
migration to avoid mixing architectural changes with API renaming.

LandscapeEditor registers spline scene overlays through MapSceneOverlays. The
editor host retains the returned delegate handle and removes it on shutdown;
MapEditor does not import LandscapeEditor. Tests register the same overlay when
their map environment starts. This keeps the feature removable from another host
without leaving a hard-coded viewport call behind.

GenesisEditor remains the composition root: it docks feature-owned panels,
constructs modes and menus and chooses which modules to use. `SLandscapeEditorPanel`
and `SFoliageEditorPanel` are owned and built by their feature modules. They take
only a mode pointer and `FMapFeaturePanelHost` callbacks for the current document,
viewport invalidation, log messages and the default file folder; no feature module
includes the application shell. The host and mode must outlive their panels.

Panels own their deferred rebuild timers. Current-document lookups replace captured
document pointers, and the application refreshes panels whenever it replaces a
level. Before replacing a document or shutting down, the host calls each panel's
`EndInteraction`. A shared panel transaction guard ends only the transaction it
opened, matched by document and transaction identity. It preserves outer edits
and ignores a cancelled transaction that has since been replaced. Menus are
closed before a document switch so their controls cannot target another level.

Each host's spline overlay registration is restricted to that host's current
document and is removed when the host closes; multiple hosts can coexist.
Replacing a feature also requires changing that host's wiring and any component-format conversion. Separate libraries do not
by themselves provide binary plugin loading or automatic package compatibility.

Plugin descriptors, discovery, dependency resolution and enable/disable support
remain a later stage. No empty utilities module is created for Foliage until there
is reusable file/tool code to put there. Unreal's module layout informed these
boundaries; Genesis retains its own APIs and package formats.

Run `python Engine/Build/CheckFeatureModuleBoundaries.py` to check source ownership,
module references, runtime/editor direction and dependency cycles. Build and run
EngineTests, RuntimeDataCheck, RuntimeRenderCheck and the map test applications
for behavioral validation.
