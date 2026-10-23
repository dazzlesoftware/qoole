# Material system roadmap

Status: material authoring for the current six-channel desktop renderer completed,
2026-10-23. Functions, collections, static/quality/platform choices, instances,
attribute layers and the editor workflow are implemented and regression tested.
See [MaterialGraph.md](MaterialGraph.md) for the actual supported subset and
remaining limitations. Later stages below remain a roadmap.
The requested destination is Unreal-style material authoring and broad material
feature parity. This document defines an implementation sequence and completion
gates; no milestone implies that later renderer features already exist.

## Scope and reference baseline

Use a visual, typed data-flow graph with nodes and wires, a material output,
searchable palette, Details, and live mesh preview. The interaction should feel
familiar to users of Unreal's Material Editor. Materials compute shader values;
gameplay Blueprint execution is a separate system.

Functional parity means equivalent authoring and rendering capabilities in
Genesis, with its own asset formats and implementation. It does not imply
loading `.uasset` files, executing Unreal plugins, or identical rendered pixels.
Treat legacy Unreal materials and modern Substrate-style layered scattering as
separate compatibility targets. Do not lock the graph into today's fixed PBR
structure and then require another asset rewrite for layered materials.

Epic's documentation is the reference inventory, accessed 2026-10-21:

- [Materials overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-materials)
- [Expression reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-material-expressions-reference)
- [Substrate overview](https://dev.epicgames.com/documentation/unreal-engine/overview-of-substrate-materials-in-unreal-engine)

Before claiming parity, freeze an explicit Unreal release baseline and audit
each relevant documentation category into an itemized checklist. These pages
are a starting inventory, not proof that every Unreal expression is listed here.
Record additions, aliases, platform limits and unsupported features explicitly.

## Historical baseline (before graph implementation)

The following records the initial planning audit. For current formats, authoring
features and limits use [MaterialGraph.md](MaterialGraph.md). Stages 1–3 now have
the everyday authoring foundation; the complete expression inventory and later
renderer stages remain open. Attribute layers blend six existing channels, not
BSDF closures. Shader caching is bounded and in-memory; no distributed/persistent
shader build service is claimed.

Code inspected: `RenderAssets.h/.cpp`, `SContentBrowser.cpp`,
`MapEditorPanels.cpp`, `MapDraw.cpp`, `MapMaterialTextures.cpp`,
`MeshMaterial.hlsl`, and the D3D12/Vulkan RHI implementations.

- Runtime `FMaterialAsset` has base RGBA, roughness, metallic, two texture
  references, blend mode and mask cutoff. The codec currently writes v3 and
  reads v1-v3. UUID resolution and package dependencies already exist.
- The editor opens a property form. There is no graph, material function,
  instance asset, generic shader compiler or dedicated material preview panel.
- Native mesh shaders sample base color and tangent-space normals and use a
  fixed GGX preview light. Current working-tree changes include masked alpha,
  translucent batching and blend settings; these need their own validation
  before being treated as a stable foundation. RenderAssets.md predates them.
- Landscape materials use a separate layer system. Native meshes currently
  expose one material reference. General scene lighting and advanced rendering
  services must be established alongside material support.

Preserve the existing uncommitted asset/rendering work. This roadmap does not
replace it or change its schema assumptions during the folder-menu fix.

## Feature coverage and dependencies

| Area | Destination | Main prerequisite | Stage |
| --- | --- | --- | --- |
| Graph workflow | Add/connect/disconnect, drag, pan/zoom, selection, clipboard, undo, comments, reroutes, search, shortcuts, previews, diagnostics | Shared graph canvas and command model | 2 |
| Core expressions | Constants, arithmetic, vector math, masks/swizzles, interpolation, logic, derivatives, coordinates, transforms, time, procedural noise | Typed compiler IR and shader stages | 1-3 |
| Textures | Multiple samples, sampler settings, UV sets, channel packing, cube/array/volume textures, explicit mip/gradient reads | Descriptor/resource model and texture pipeline | 1-3, 6 |
| Surface outputs | Base color, normal, metallic, roughness, specular, emissive, AO, opacity/mask, anisotropy/tangent, WPO, pixel depth offset | Surface contract, vertex/pixel compilation and passes | 1-4 |
| Reuse | Scalar/vector/texture/static parameters, instance inheritance, dynamic instances, parameter collections, functions and layers | Stable parameter IDs, dependency invalidation, permutation cache | 3 |
| Blend and raster states | Opaque, masked, translucent, additive, modulate, premultiplied/alpha-composite, holdout, two-sided variants | Explicit pass/state compatibility and sorting | 4 |
| Lighting models | Unlit, default lit, clear coat, subsurface/profile, skin, foliage, cloth, hair, eye, thin translucent, water | Lighting, shadowing, transmission and specialized geometry data | 4-6 |
| Domains | Surface, deferred decal, light function, post process, UI, volume | Renderer pass scheduling and domain-specific inputs | 5 |
| Scene expressions | Depth/color, screen/view data, scene textures, distance fields, per-object/instance/custom data | Named scene resources and availability validation | 4-6 |
| World systems | Landscape blends/visibility/grass, foliage/instances, particles, skeletal meshes, spline meshes | Each producer supplies a defined vertex/data interface | 5-6 |
| Layered scattering | BSDF closures, vertical/horizontal layers, coatings, transmission, absorption, scattering, parameter blending | Closure IR, lighting integration and bounded evaluation | 7 |
| Advanced textures | Streaming, virtual textures/runtime virtual textures, UDIM, render targets, media, compression and cooking | Residency, feedback, import and runtime systems | 6 |
| Advanced geometry | Displacement/tessellation, deformation bounds, velocity and shadow consistency | Geometry pipeline, temporal rendering and culling | 6 |
| Production tooling | Shader stats, platform/quality switches, async compile, debugging, profiling, cooking | Compiler services and capability matrix | 1, 8 |

Node labels alone do not count as support. Every node needs validated semantics,
resource bindings and working rendering in its advertised domains/backends.
Features tied to missing engine systems stay visible in the coverage backlog
and unavailable in authoring until those systems exist.

## Proposed ownership and asset contracts

1. Runtime material types, graph translation and codecs live under
   `Runtime/Engine/{Public,Private}/Materials`. `Editor/MaterialEditor` owns
   authoring, preview and toolchain adapters. Runtime must not depend on Slate or
   MapCore; MaterialEditor must not depend on MapEditor. Host-specific selection
   and transactions enter through callbacks.
2. Store versioned graph nodes, stable node/pin IDs, expression type/version,
   properties, typed links, output/domain settings and parameter IDs in the
   package. Keep editor positions/comments separate from semantic graph data.
   Derive package dependencies from all resource/function/parent references.
3. Introduce distinct material-function and material-instance schemas. Preserve
   UUIDs on edits. Reject parent/function recursion, stale references, duplicate
   IDs, dangling pins and incompatible type changes without altering assets.
4. Keep loading v1-v3. Explicit upgrade builds a graph reproducing the old
   texture-factor multiplication, vertex color, normal map and blend settings.
   Do not rewrite packages merely by opening them. Test color-space equivalence.
5. Lower graph outputs to a typed intermediate representation, then shared HLSL
   and DXIL/SPIR-V. Runtime consumes cooked shaders and binding layouts; editor
   compiler produces them asynchronously. Keep generated shader blobs/cache
   outside authored graph metadata.
6. Hash semantic graph, transitive functions, static parameters, domain/pass,
   compiler version, platform and quality into cache keys. Uniform changes must
   update buffers without recompilation. Layout-only changes must not compile.
7. Define a binding contract beyond today's two fixed textures and small push
   constants: reflected uniform layout, resource table, samplers and stage use.
   Query RHI limits and reject unsupported combinations with node diagnostics.
8. Check cycles, numeric conversions, shader-stage legality, resource limits and
   domain legality before shader generation. Bound graph size/function depth
   and compile work. Diagnostics map generated source back to graph nodes.
   Preserve unknown node payloads for inspection; never silently substitute them.
9. Retain the last successful preview while compilation fails, clearly mark it
   stale, and keep edits recoverable. Publish compiled resources atomically and
   retire old GPU resources only after in-flight work completes.

## Implementation stages and completion gates

### 0. Freeze the baseline and inventory

Validate current v3 codecs and opaque/masked/translucent rendering on both RHIs.
Build an itemized parity register from the chosen Unreal release, including
inputs, expression families, domains, blend modes, shading models and tooling.
Each entry records semantics, dependencies, stage, tests and supported platforms.
Resolve the minimum hardware/shader target and compiler distribution strategy.

Gate: existing assets reopen unchanged; feature register and cross-backend test
baseline are checked in. Full parity remains a target until the register closes.

### 1. Runtime graph and compiler foundation

Implement typed graph serialization/validation, expression registration, IR,
deterministic compilation, resource reflection and DXIL/SPIR-V cache. Start with
constants, scalar/vector parameters, UV, texture sample, multiply/add/lerp,
normal decode and a surface output for existing PBR channels. Extend the RHI
binding layout and native mesh path to execute compiled results.

Gate: a graph changes rendered pixels on both backends, survives save/load,
rejects invalid links/cycles and migrates legacy materials without visual drift.
No disconnected graph UI or CPU-baked substitute for shader evaluation.

### 2. Material Editor

Build a reusable graph canvas in Slate and a dockable Material Editor: graph,
palette, selected-node Details, searchable add menu, compile status, preview
mesh/lighting controls, save/apply and dirty state. Support pin drag creation,
wire replacement/removal, multi-selection, copy/paste, duplicate/delete, framing,
pan/zoom, undo/redo, comments and reroutes. Texture drops create sample nodes.
Use sphere/cube/plane/custom mesh previews and optional node-output preview.

Gate: author, save, close, reopen and edit a textured material entirely through
the graph; input tests cover wires, shortcuts, DPI, undo and failed compilation.

### 3. Reuse and complete everyday expression coverage

Add instances and inheritance, parameter groups/defaults/overrides, runtime
updates, parameter collections, functions, static switches and material
attributes/layers. Expand math, coordinate, utility and procedural expressions.
Track function edits and recompile affected materials only. Diagnose parameter
type conflicts and control static permutation growth.

Gate: one master material drives many instances; uniform edits avoid shader
compiles, static edits use cached permutations, and recursive functions fail.

### 4. Renderer surface and lighting expansion

Add full surface channels and vertex deformation with correct bounds, picking,
depth, shadow and velocity behavior. Establish scene lighting/shadows, environment
lighting/reflections and consistent HDR color management. Implement complete
blend/raster rules, transparency ordering, refraction and appropriate depth
access. Add lighting models only with their required data and render passes.

Gate: channel/blend reference scenes pass on both RHIs, including intersecting
transparent meshes, masks in depth/shadow passes and deformed silhouettes.
Document sorting limitations; basic alpha blending does not establish glass parity.

### 5. Domains and world integration

Introduce explicit domain/pass contracts for decals, light functions, post
process, UI and volume materials. Integrate landscape graphs, per-layer data,
visibility and foliage; add multiple material slots/submeshes. Establish shared
vertex interfaces for static/skeletal/spline meshes and instancing/particles.

Gate: each advertised domain has a functioning producer/pass and an authored
sample. Invalid scene reads fail at compile time rather than binding garbage.

### 6. Specialized systems

Deliver remaining shading models with their renderer owners: skin/subsurface,
hair/eye/cloth/foliage/water, volume and transmission. Build advanced texture
residency, virtual texture feedback, media/render-target integration, distance
fields and displacement where required by the parity register. Define temporal
and platform behavior for scene-dependent expressions.

Gate: each feature has reference scenes, resource-budget checks and explicit
backend capability results. Missing prerequisites cannot be bypassed by UI flags.

### 7. Substrate-style layering

Extend the IR with typed scattering closures and bounded composition. Implement
coatings, layered interfaces and scattering/transmission semantics alongside
lighting, shadows and transparency. Supply complexity views, budgets and quality
fallbacks. Preserve conventional PBR graphs through a compatibility closure.

Gate: layered reference materials pass energy/stability tests and image checks;
budgets and unsupported combinations produce useful editor diagnostics.

### 8. Production and parity audit

Finish background compilation, shader statistics/debug views, platform previews,
cook validation, warm caches, reimport/dependency propagation, profiling and
regression scenes. Publish a supported-feature matrix linked to automated tests.
Audit every parity-register entry against the frozen Unreal baseline; name any
intentional differences and remaining omissions instead of declaring blanket parity.

## Validation and delivery policy

- Headless tests: graph/instance/function codecs, corruption, cycles, types,
  deterministic hashes, invalidation and legacy migrations.
- UI tests: graph operations, history, clipboard IDs, menu actions, scaled input,
  previews, diagnostics and save/reopen. Test all advertised editor actions.
- GPU tests: offscreen D3D12 and Vulkan readbacks for every new material output,
  texture operation, domain and lighting model; include mirrored UVs, nonuniform
  transforms, color spaces, depth, alpha and resource lifetime changes.
- Runtime check: cooked graph materials load without editor/UI/compiler startup.
- Performance: measure compile latency, cache hit rates, shader permutations,
  CPU/GPU frame time and memory on declared target hardware. Set budgets before
  accepting advanced features; no unmeasured frame-time promises.

Implement stages as reviewable changes with passing gates. Start with stage 0,
then the compiler and real rendered graph slice. Do not estimate a full parity
delivery date until that slice establishes compiler and renderer costs.
