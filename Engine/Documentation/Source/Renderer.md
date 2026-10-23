# Runtime rendering and native scene construction

`Runtime/Renderer` references Core, Geometry, Engine and RHI, with no Editor,
MapCore, Slate or application/window dependency. Engine data and native payload
codecs do not depend on Renderer.

## Implemented boundary

- `FNativeMeshDrawList` owns mesh/material snapshots, transforms positions and
  inverse-transpose normals, culls faces, batches opaque surfaces and stably sorts
  transparent triangles across the scene.
- `FNativeMeshRenderer` owns material shaders, mip generation, texture caches,
  prepared buffers/pipelines and GPU submission for opaque, masked and translucent
  meshes. Prepared frames retain GPU resources until deferred release.
- `FSceneViewVolume`, `FLandscapeRenderPlan` and `BuildLandscapeComponent` handle
  transformed component visibility, perspective/orthographic LOD, partial boundary
  samples and seam stitching without requesting the full terrain mesh.
- `BuildLandscapeTriangles` emits backface-culled and frustum-clipped terrain,
  retaining interpolated UVs and preview lighting. Clipped vertex colors retain
  the previous mean-color fallback. `AppendProjectedLandscape` adapts this to
  runtime projected scene batches. The legacy editor has only a triangle adapter.
- Landscape procedural/file/asset texture lookup, mip images, incremental weights,
  eight-layer GPU blending and baked fallback now live in Renderer. Native texture
  tokens share the Engine codec. The texture library has an explicit active asset
  registry context; callers keep that registry alive and set it when switching
  projects/worlds. Raw image fallback uses WIC on Windows.
- `IsFoliageInstanceVisible` performs conservative volume and per-type distance
  culling. `BuildFoliageDrawList` and `AppendMeshInstance` prepare shared mesh
  snapshots and affine rows. Mirrored instances have separate winding batches and
  pipelines. Shader normals use inverse transpose for nonuniform scale and shear.
- `FProjectedSceneRenderer` owns terrain/solid/instance pipelines, texture uploads,
  reusable mesh buffers, frame uploads and draw submission. Prepare runs outside
  render passes. Frames pin resources across cache eviction and defer release;
  the device must outlive the renderer and prepared frames.

The caller supplies a color+D32 pass, viewport/scissor and camera. Draw projected
geometry, native opaque/masked meshes, instances, then native transparency. Preview
lighting and centroid-based transparency sorting remain deliberately limited.
Terrain preview normal handling retains the existing scale-based convention;
full physically based terrain materials remain future work.

## Native worlds

`Runtime/Engine/World/WorldGeometry` owns mesh component definitions and
terrain/foliage payload and bulk codecs,
shared by native package loading and the editor adapter. Sparse material override
indices are retained by `FStaticMeshComponentData`. Decode preserves its output
on failure; encode rolls back bulk/dependency appends. Mesh and landscape component
asset declarations are checked against their payload references.

`FWorldSceneSnapshot` constructs built-in/native meshes, terrain, spline meshes and
foliage directly from version 2/3 world/prefab packages and actor world transforms.
Snapshots expose read-only accessors; rebuild them after editing the source world.
It does not require a game definition set or legacy Object tree. Geometry snapshots
own their decoded payloads after the package is released; native terrain images
still resolve through the renderer's active asset registry. Process-local render
keys are not serialized. `FWorldSceneDrawList` performs scene traversal and builds
runtime material, projected-terrain and instance lists atomically.

The worldspawn legacy entity is counted explicitly as non-rendering metadata.
Other unsupported components, including legacy brushes/entities, fail with an
identified type instead of disappearing. This is a rendering snapshot, not a
component lifecycle/gameplay implementation.

## Build and validation

`RuntimeRenderCheck.exe [-d3d12|-vulkan]` uses offscreen targets without windows or
UI modules. It checks opaque/masked/translucent mesh pixels, actual terrain
projection, GPU layer blending, baked fallback, layer refresh, instance drawing,
mirrored/nonuniform normals and resources retained through cache eviction.

It can also load and render a saved world, with an optional diagnostic BMP:

```
RuntimeRenderCheck.exe -vulkan World.gmap Project/Content Capture.bmp
```

The map must contain supported runtime components and resolve its native assets
through the supplied `/Game` content mount. A blank readback fails the check.
`RuntimeDataCheck` retains its CPU-only dependency boundary.

`NativeRenderer` and `WorldGeometry` tests cover geometry preparation, clipping,
UVs/light, transforms, snapshot lifetime, malformed input, bulk rollback, native
payload construction and unsupported component rejection. Existing editor readback
and self-tests exercise the shared code from the viewport.

Shaders and generated SPIR-V headers live under Renderer/Private/Shaders.
`BuildSceneShaders.cmd` regenerates scene and material shaders; the former editor
script is a compatibility entry point. Renderer.vcxproj compiles DXIL.

## Remaining migration

The editor still traverses MapCore Object and owns selection, transforms, undo,
wireframe/selection overlays and legacy brush drawing. Its runtime GPU adapters
retain compatibility accessors during migration. Native world loading/drawing is
now available separately. Selection/scope transaction snapshots now resolve
actor IDs; mesh/landscape/foliage transform snapshots and native-only viewport
moves use runtime transforms. Switching live document ownership and the remaining
structural/brush operations to runtime actors is the next boundary. MapCore retirement, LegacyQoole isolation,
gameplay component lifecycle, streaming and plugin loading remain later stages.

The extracted geometry header no longer exports `Min`/`Max` macros. Legacy
algorithms use scoped `GeometryMath` value helpers with single argument evaluation.
A compile/test fixture deliberately includes geometry before Core/Renderer to
protect this public-header boundary.
