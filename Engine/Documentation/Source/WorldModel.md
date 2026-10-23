# Runtime worlds and persistent scene identity

`Runtime/Engine/Public/World/World.h` defines the first runtime scene model.
It depends on Core, Geometry and AssetCore, with no Editor, MapCore, UI or RHI
dependency. It is a foundation for replacing the old editor object tree.

## Ownership and transforms

`FWorld` owns actors. Each `FActor` has a UUID, name, optional parent actor ID,
one root `FSceneComponent`, and a list of `FActorComponent` data records. Every
component has its own UUID and a schema type. UUIDs are unique across actors
and components within a world. Identity is scoped to a world, so Save As can
retain scene IDs while giving the new package its own package UUID.

The root component holds local translation, rotation (radians in Geometry's
coordinate convention), and scale. World transforms compose parent and local
matrices, including mirrored and nonuniform scale. Reparenting preserves the
local transform. Preserving a world transform during reparenting, especially
when nonuniform scale introduces shear, needs a separate editor operation.

Components contain schema JSON properties, explicit world-local references
to actors/components, and AssetCore asset references. A component reference
must name its owning actor; dangling or differently owned references fail
validation. A null reference has both fields empty. Asset IDs and logical
paths use the registry's existing rules; resolving them remains explicit.

Mutations reject duplicate IDs, missing parents, cycles, invalid transforms,
and dangling references. Removing an actor removes its subtree, but fails if
an outside component still references that subtree. Duplicating a subtree
creates new actor/component IDs and remaps its internal references. References
to outside actors and assets retain their identities. Structural mutation
attempts can invalidate pointers returned by lookups.

## Native scene schema

World and prefab packages now use type schema version **2**. Their `.gexp`
exports contain `Scene`, with runtime scene `Version: 1` and an `Actors` array.
Each actor stores `Id`, `Name`, `Parent`, `Root` and `Components`. Components
store `Id`, `Type`, `Properties`, `References` and `Assets`; the root also stores
`Transform`. This is separate from the package envelope's format version.

The transitional editor maps its existing payloads to these schema types:

| Component type | Current payload |
| --- | --- |
| `Genesis.StaticMesh` | Built-in mesh name |
| `Genesis.Landscape` | Terrain/layer/spline metadata and bulk ranges |
| `Genesis.Foliage` | Foliage metadata and instance bulk ranges |
| `Genesis.LegacyBrush` | Legacy brush data |
| `Genesis.LegacyEntity` | Legacy entity definition data |

Payload values are stored in `Properties.Data`. Bulk ranges still address the
enclosing package's `.gbulk`; `FWorld` validates the scene graph, while the
shared Runtime/Engine `FWorldGeometryCodec` validates mesh component definitions,
sparse material overrides, terrain/foliage bulk ranges and payload schemas. `FWorldSceneSnapshot` constructs supported render components
without editor modules; unsupported components are reported explicitly. See
[Renderer.md](Renderer.md) for the current drawing boundary.

The editor reads version 1 native worlds/prefabs through the old codec. Opening
a version 1 world creates scene IDs and marks it modified; saving writes version
3. QLE/MAP import remains available. Native version 2 and 3 files preserve active actor
and component IDs across save/load. Older editor builds cannot open version 2.

## Editor bridge and migration boundary

`FMapWorldAdapter` converts between runtime scenes and the existing payload
codec. Editing still uses MapCore's `Object`, selection and transaction code.
Objects carry identity slots for their actor, transform, geometry payload and
entity. Only components that exist are exported. Unused slots are not persisted.
Content snapshots retain identities. Duplicate, paste, split and prefab
placement create new identities; the insertion boundary also renews conflicting
IDs in copied trees. Undo/redo then preserves those assigned identities.

The adapter rejects scene features the legacy editor cannot preserve, including
unknown component types, extra properties/references, multiple geometry payloads
on one actor, forests without a single root, and names it cannot represent.
It fails instead of silently discarding those features.

The editor bridge now authors and retains explicit asset references for native
mesh sources, material overrides and landscape layer textures. Other arbitrary
component reference arrangements remain unsupported by that bridge.
Prefab placement remains a copied scene, without a live prefab-instance link.

Typed mesh/material/texture assets and payload loaders are now implemented (see
[RenderAssets.md](RenderAssets.md)). Opaque mesh textures, normal maps and metallic/
roughness preview shading are implemented. Material slots, masking/transparency, and mesh/texture editors with reimport
are implemented. Runtime/Renderer now owns native mesh/material rendering.
Runtime terrain/foliage preparation, GPU rendering and native scene traversal are
also implemented. Remaining work includes migrating selection, transactions and
the editor's live document/operations to runtime actors. MapCore retirement, gameplay component lifecycle, cross-world actor
references, and plugin loading remain later stages. Regions registered on a world grid
and streamed by distance are described in [WorldComposition.md](WorldComposition.md).

## Validation

`World` tests cover hierarchy/transforms, mutation rollback, owned references,
subtree duplication/removal, identity collisions and malformed scenes.
`MapPackages` tests cover version 1 upgrades, version 2/3 save/load, prefab instance
identity, grouped copies and content undo. The editor self-test checks identity
across native save/reopen. `RuntimeDataCheck.exe [World.gmap]` can load a version
2 world and evaluate its actor transforms without linking any editor modules.

World/prefab schema v3 adds per-slot mesh material overrides. Versions 1/2
remain readable and are marked for upgrade. Version 2 scene identities survive
the upgrade. Mesh component assets list source followed by non-null overrides
in slot order, matching the component Data payload.

### Actor editing transition

The live editor still owns the legacy object tree, but transaction selection and
scope snapshots now store persistent actor IDs and resolve current objects when
undoing, redoing or cancelling. `GetSelectedActorIds` / `SetSelectedActorIds`
provide an identity-based selection boundary without exporting terrain payloads.
Missing or out-of-scope IDs are ignored and duplicate selections are removed.

Mesh, landscape and foliage actors expose validated local `FTransform3D` edits.
Their Details rotation/scale fields and native-only viewport moves record before
and after transforms by actor ID. Consecutive edits to one actor coalesce within
a transaction. Legacy brushes, mixed selections and structural edits retain the
existing adapters; authoritative native-world ownership is still pending.
