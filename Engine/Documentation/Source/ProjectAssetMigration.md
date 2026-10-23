# Genesis projects, packages, and module migration

This is an incremental migration, not Unreal file compatibility. The local
Unreal source was consulted for responsibility boundaries, particularly
`Runtime/Projects/Public/ProjectDescriptor.h` and `ModuleDescriptor.h`.
Genesis uses its own smaller descriptors, package schema, and implementation.

## Implemented: native storage foundation

| File | Purpose | Current implementation |
| --- | --- | --- |
| `.gproject` | Project descriptor | Versioned JSON, name, engine association, default map, retained extension fields |
| `.asset` | Asset package manifest | Native prefab assets; common package API for future asset types |
| `.gmap` | World package manifest | Native world hierarchy and payload schema |
| `.gexp` | Export data | Structured UTF-8 JSON, independently versioned world/prefab schema |
| `.gbulk` | Large payloads | Little-endian IEEE-754 float32 heights, weights, foliage instance records |

`Runtime/Projects` owns descriptors, project directories and logical `/Game`
paths. `Runtime/AssetCore` owns package I/O and integrity checks. Both depend
only on Core, with no MapCore, UI, or editor references.

The first data extraction is also implemented: `Runtime/Geometry` owns
Math3D and static mesh data/library generation; `Runtime/Engine` owns landscape,
spline and foliage data and algorithms. These compile without any Editor include
directories. Mesh wire edges now use a standard vector rather than the legacy
linked list, and the old viewport point-buffer conversion lives in an editor
adapter. Compatibility include headers remain at the old MapCore locations.
`RuntimeDataCheck.exe` links just runtime libraries and exercises terrain,
paint data, raycasts, spline meshes and foliage without initializing MapCore.

`Runtime/Engine` now also owns the world/actor/component model and persistent
scene identities. Native world/prefab schema version 3 stores that runtime scene and per-slot
mesh material overrides; versions 1/2 remain readable.
`Editor/MapEditor/MapPackage` uses a transitional adapter for the existing editor
model. Component payloads explicitly describe brushes with texture settings, entity keys and
flags, built-in mesh references, landscapes, paint layers, foliage types and
instances, splines, junctions, and tangents. It does not call the QLE writer or
embed QLE text. Custom meshes can be stored in native mesh assets; unbacked
custom mesh pointers are still rejected rather than discarded during map saving.
The legacy entity definition set is recorded while that dependency remains.

Each package has a UUID preserved by normal saves. Save As and Content Browser
imports allocate a new UUID. Scene-local actor/component identities and subtree
reference fixup are implemented; editor-authored references, external-world
references and streaming are still pending. See [WorldModel.md](WorldModel.md).
Updating an existing prefab asset also retains its identity. The runtime
[asset registry](AssetRegistry.md) now discovers mounted packages by metadata,
resolves UUID/path references and diagnoses duplicate identities. All four
Content Browsers share it; native prefab placement verifies the resolved UUID.

Each save writes generation-qualified sidecars, e.g.:

```
Main.gmap
Main.<generation-uuid>.gexp
Main.<generation-uuid>.gbulk
```

The manifest references exact sidecar names, sizes and FNV-1a checksums. Those
checks detect accidental corruption, not hostile signatures. The reader
rejects unsupported versions, missing or corrupt sidecars, invalid bulk ranges,
invalid hierarchy, and sidecar path traversal. Version 1 limits manifests to
1 MiB and each sidecar to 512 MiB. Streaming reads are future work.

Sidecars are immutable. The writer closes both sidecars before atomically
replacing the manifest using the Windows file API. Earlier generations and
failed-save remnants remain on disk; an explicit package garbage collector
is still needed. This is a process-failure consistency mechanism, not a promise
of power-loss durability. Keep the manifest and its referenced sidecars together
in backups/version control. Do not rename only the manifest in Explorer.
Content Browser Import reads and rewrites complete packages, including sidecars.

## Project workflow

```
GenesisEditor.exe -newproject="D:\Projects\MyWorld\MyWorld.gproject"
GenesisEditor.exe "D:\Projects\MyWorld\MyWorld.gproject"
GenesisEditor.exe -project="D:\Projects\MyWorld\MyWorld.gproject" "D:\Projects\MyWorld\Content\Maps\Main.gmap"
```

Inside the editor, **File > Open Project...** and **File > New Project...**
do the same. **File > Open...** also accepts a `.gproject`. The project is
chosen at startup, before settings and content browsers exist, so switching
projects asks to save the current level, starts a new editor with
`-project=` or `-newproject=`, and closes this one. Choosing an existing file
in **New Project...** opens that project.

Creation never overwrites a descriptor. It creates `Content`, `Config`,
`Source`, `Plugins`, `Saved`, and `Intermediate` directories. Project editor
settings, layout and bindings are isolated in `Saved/Editor`. All four Content
Browsers expose project Content plus Engine Content. Project creation/opening
currently uses the command line; a project browser and launcher are pending.

Example descriptor:

```json
{
  "FileVersion": 1,
  "Name": "MyWorld",
  "EngineAssociation": "Genesis",
  "Description": "Landscape project",
  "DefaultMap": "/Game/Maps/Main"
}
```

DefaultMap is optional. Its logical path resolves to
`<project>/Content/Maps/Main.gmap`. With no project argument, standalone editing
and the previous global settings location still work. EngineAssociation is
metadata at present; there is no installation selector or build-version routing.
Unknown descriptor fields survive parsing/serialization; module/plugin fields
are not executable configuration until the module system is implemented.

Save/Save As default to `.gmap`; Save Selection as Prefab defaults to `.asset`.
Opening `.qle` or `.map` chooses a new `.gmap` destination and leaves the source
intact. Legacy writers remain available to existing tests and compatibility
code during migration. Built-in brush templates still come from old QLE files.
Existing layer image paths and built-in mesh names retain their resolution.
Native texture import, mesh asset references and material overrides are available;
see [RenderAssets.md](RenderAssets.md) for the workflow and rendering limits.

## Remaining landscape work

The requested remaining landscape work is tracked here so it is not confused
with the completed sculpt, paint, import/export, LOD, foliage and spline tools:

- Visibility holes, consistently affecting render, raycast and future collision.
- Non-destructive height/paint/spline edit layers with ordered composition,
  enable/disable, opacity, lock and undo.
- Alpha/pattern brushes, region selection/copy/paste, mirror and retopologize.
- Add/remove/resize components, preserving neighboring data and undo.
- Streaming landscape proxies, persistent component IDs and tiled import/export.
- Height-based texture blending and normal-map shading on D3D12 and Vulkan.
- Grass generated from paint weights with deterministic placement and updates.
- Per-segment spline settings, roll, junction surface geometry and water.
- Runtime collision, serialization/export and a standalone runtime validation app.

Edit-layer composition should precede destructive component editing. Streaming
needs actor/component identity and asset references first. Holes must be stored
as actual visibility data rather than painting ground black.

## Ownership migration after native storage

Target responsibilities (not a claim that these modules already exist):

```
Engine/Source/Runtime/
  Core/              containers, math, files, logging, JSON
  Projects/          project descriptors, mounts, project context
  AssetCore/         packages, asset IDs, registry, loaders, bulk storage
  Engine/            World, Actor, Component, transforms, scene ownership
  Geometry/          mesh data and geometry operations without editor globals
  Renderer/          scene drawing, visibility, LOD and instancing
  Landscape/         terrain data, composition, streaming, collision
  Foliage/           instance data and runtime generation
Engine/Source/Editor/
  EditorFramework/   editor services, transactions and selection
  LevelEditor/       viewport interaction, outliner and level UI
  AssetTools/        importers, editors, content-browser actions
  LandscapeEditor/   sculpt/paint/region/component/spline tools
  FoliageEditor/     foliage painting and settings
  LegacyQoole/       QLE/MAP/entity/template conversion adapters
Engine/Source/Programs/
  GenesisEditor/     application bootstrap
```

Move data and algorithms by dependency, not by relabeling MapCore. Runtime
modules must never include Editor headers or link editor libraries. The current
`Object` mixes tree ownership, transforms, brush CSG, entities, persistence,
selection and rendering; its replacement must separate those responsibilities.

Migration order:

1. Native package/project foundation (implemented here).
2. Package identities, registry and identity-reference resolution (implemented).
   Runtime actor/component IDs and explicit scene references are implemented;
   portable mesh/texture/material codecs and references are implemented (see
   [RenderAssets.md](RenderAssets.md)). Editor-authored actor references,
   a project browser are pending. Material slots, masking/transparency, mesh/texture
   editors and reimport are implemented. Opaque mesh
   textures, normal maps and metallic/roughness preview shading are implemented.
3. Runtime math/mesh and landscape/foliage data extraction (implemented), then
   Engine world/actor/component model, native schema version 2 adapter and
   identity round-trip tests (implemented; see [WorldModel.md](WorldModel.md)).
   Selection/scope undo snapshots now use actor IDs; native actor transform edits
   and native-only viewport moves use runtime transform snapshots. Structural and
   brush operations and live document ownership still need migration.
4. Runtime/Renderer now owns mesh/material rendering, terrain visibility/LOD,
   triangle preparation, layer resources and foliage instancing. Native payload
   construction, shared component codecs and world traversal render supported `.gmap` scenes without
   editor modules (see [Renderer.md](Renderer.md)). Migrate the editor's live
   document/operations to runtime actors, then remove legacy drawing. Implement
   remaining landscape features against that data model.
5. Move QLE/MAP conversion and game definitions into LegacyQoole. Remove native
   runtime/editor dependence on the old model, then retire MapCore's target.
6. Make optional features plugins after these migrations, as requested.

## Plugin stage

Keep a small mandatory kernel (Core, project/package and module bootstrapping).
Use Engine/Plugins and `<project>/Plugins` for optional features. A future
`.gplugin` descriptor should declare identity/version, Runtime/Editor modules,
dependencies, content mounts and load phase. The resolver must reject cycles,
duplicate IDs, missing dependencies, and editor modules in runtime targets.
Then migrate Landscape, Foliage, geometry/legacy importers and optional tools
to plugins; RHI implementations can follow once backend factories are registered.

Do not claim plugin support from a descriptor alone. Completion requires
startup/shutdown registration, build integration, enable/disable behavior,
dependency resolution, content discovery and tests with plugins disabled.

Existing GPL notices and Qoole attribution remain on derived code throughout
these moves. Consult Unreal for structure; Genesis package formats are its own.

Landscape and foliage now have separate runtime targets (`Landscape`, `Foliage`)
and editor targets (`LandscapeEditor`, `FoliageEditor`). Heightmap/weightmap file
utilities live in `LandscapeEditorUtilities`. These are build modules, with
plugin packaging still scheduled after native document/legacy separation.
See [FeatureModules.md](FeatureModules.md) for their dependency boundaries.

Feature panel ownership is now inside LandscapeEditor and FoliageEditor as well.
GenesisEditor provides host callbacks and docking only. Foliage reprojection after
terrain edits is shared Runtime/Engine code; the document adapter only records
undoable results. Native live-document ownership and structural editing remain
separate migration tasks before retiring MapCore or adding plugin loading.
