# Active completion checklist

This tracks the requested work across continuations. Do not declare the overall
request complete while an unchecked requirement remains. Update this file with
implementation evidence and tests as work advances. Console ports are separate
future platform work; desktop implementations must retain a path to cooked,
platform-specific deployment.

## Material authoring

- [x] Typed graph, desktop shader cooking, asset save/load and mesh preview.
- [x] Multi-selection, marquee selection, clipboard, duplication and comments.
- [x] Expanded math/vector/UV/utility expression library and reroutes.
- [x] Background compilation, live preview, stale result suppression and diagnostics.
- [x] Scalar/vector/texture parameters and material instances, with uniform updates.
- [x] Reusable material functions, dependencies and recursion validation.
- [x] Parameter collections, static switches and quality/platform permutations.
- [x] Complete editor workflows, tests and documented compatibility/limits.

Authoring completion applies to the current six-channel desktop surface contract,
including material-attribute bundles/layers. It does not close the advanced
rendering checklist or claim the full Unreal expression inventory. Supported
features, compatibility and budgets are documented in [MaterialGraph.md](MaterialGraph.md).

## Advanced material rendering

- [ ] Surface texture inputs, specular/AO/anisotropy and complete raster/blend states.
- [ ] World-position offset/displacement with depth, bounds and picking consistency.
- [ ] Scene lighting/shadows, HDR/environment lighting and reflections.
- [ ] Scene textures, refraction and transparency pass integration.
- [ ] Decal, post-process, light-function, UI and volume material domains.
- [ ] Landscape and other geometry-producer graph integration.
- [ ] Specialized shading models (coat, subsurface/skin, foliage, cloth, hair, eye, water).
- [ ] Layered scattering/BSDF composition and complexity budgets.
- [ ] GPU regression scenes on D3D12 and Vulkan for all advertised capabilities.

## Scripting

- [x] Language-neutral provider interfaces and per-project configuration.
- [x] Shared binding descriptions, entity IDs, host capabilities and errors.
- [x] Executable Lua provider and conformance tests.
- [x] Executable Python provider and conformance tests.
- [x] Executable C#/.NET provider and conformance tests.
- [ ] Script packages/components and world gameplay lifecycle/event integration.
- [ ] Editor automation with undo, project picker and output/console tooling.
- [ ] Debugging, reload/state handling and cooked packaging per provider.
- [ ] End-to-end sample projects and runtime-only validation.

## Full scripting API and regression projects (expanded request)

- [ ] Complete runtime API coverage for all currently implemented engine features.
- [ ] Complete editor API coverage, preserving transactions, validation and command behavior.
- [ ] Generated language facades/reference documentation and an auditable coverage catalog.
- [ ] Runnable Lua, Python and C# gameplay/editor example projects.
- [ ] Feature projects for landscape, foliage, materials, meshes/CSG, assets, components and editor workflows.
- [ ] Run each feature scenario through all three languages and relevant GPU regression checks.

Coverage and acceptance are tracked in [ScriptingAPICoverage.md](ScriptingAPICoverage.md).

## World composition

- [x] Region registry: independently saved maps registered on a world grid, with overlap checks.
- [x] Distance streaming of nearby regions through a replaceable tile source.
- [x] Editor tools for registering and arranging regions (Editor/WorldBrowser).
- [ ] Region servers, boundary handoff, ownership/access and edge matching.

Design and limits are in [WorldComposition.md](WorldComposition.md).

## Progress

2026-10-23 material authoring completion: reusable typed functions (eight inputs,
one result), transitive dependency refresh/affected-only recooking, collections
with runtime uniform snapshots, static/quality instance permutations, platform
branches, bounded shader cache and six-channel attribute layers implemented.
Editor workflows include asset drops, selection/clipboard/history, background
revision suppression, selected-node preview, save-target validation, preview
mesh/view controls and single-draft windows. MaterialEditor remains independent
of MapEditor/MapCore; shared drag data belongs to EditorFramework.

Release x64 solution build succeeds, including runtime-only validation tools.
Full EngineTests: 293 tests / 58,405 assertions, zero failures/skips. Focused
MaterialGraph/EditorScripting: 36 tests / 1,466 assertions. MaterialAuthoring
projects pass all six Lua/Python/C# and D3D12/Vulkan CLI runs; native tests verify
their saved scenes, transactions and rendered pixels on both RHIs. D3D12 editor
captures inspected under Engine/Saved/MaterialGraph/FinalQA. Advanced rendering
and the broader scripting/API checklist remain open.

2026-10-22: baseline audited. The first implementation and module refactor pass
255 tests. Work is starting with material editor usability and expression coverage;
parameter/instance and renderer contracts follow before scripting bindings depend
on them. Actual Lua/Python/.NET adapters and advanced renderer features are not
yet implemented.

2026-10-22: expanded expressions cook on both desktop targets; clipboard internal
wiring/undo and background revision suppression pass. Full regression: 258 tests,
56,895 assertions. Background UI and selection workflow checks are still underway.

2026-10-22: scalar/vector graph parameters now bind immutable per-frame snapshots
through D3D12 root CBVs and Vulkan uniform descriptor sets. Parameter edits reuse
cooked shaders. GPU tests confirm red/green changes on both backends without
recooking (12 material tests, 387 assertions). Texture parameters and instances
remain pending. D3D12 live editor screenshot checked at
`Engine/Saved/MaterialGraph/AuthoringQA/D3D12_material_asset.png`.

2026-10-22: runtime instance packages (`Genesis.MaterialInstance`, schema 1) now
resolve parent chains and scalar/vector overrides atomically, retaining parent
shader bytes. Missing names/type mismatches, cycles and chains beyond 32 assets
fail explicitly. Material tests: 13 passed, 408 assertions. Latest full suite
before instance addition: 259 passed, 56,929 assertions. Instance authoring UI,
texture overrides and dynamic instance API remain pending.

2026-10-22: texture parameter overrides, dynamic instance snapshots and instance
editor implemented. Creation, inherited/reset values, numeric editing, undo/redo,
saving and D3D12/Vulkan pixels verified. Scripted instance editor captures inspected
under `Engine/Saved/MaterialGraph/InstanceQA`. Full regression before Lua work:
262 tests, 57,056 assertions.

2026-10-22: Lua 5.4.9 embedded with verified upstream checksum and license. Shared
binding descriptors enforce argument/result types, capabilities and editor-only
access. Actual Lua execution, VM isolation, host failures, module replacement,
JSON round trips, memory/instruction limits and finalizer rejection pass. Targeted
scripting/material suite: 20 tests, 623 assertions. Python/.NET, world bindings,
lifecycle and editor integration remain pending.

2026-10-22 checkpoint: full EngineTests regression passes 265 tests / 57,138
assertions; RuntimeDataCheck builds with the new Lua dependency. Next work is
shared world/entity bindings and script component lifecycle, then executable
Python and .NET adapters against that contract. Advanced rendering and remaining
material authoring checklist items remain part of the same active goal.

2026-10-22: executable CPython 3.14.6 and .NET 10 providers added. CPython uses
isolated subinterpreters; C# uses compiled IScriptModule assemblies in collectible
load contexts. Shared core/world bindings now expose 20 discoverable operations.
Lua, Python and C# sample source creates real world actors and passes transforms,
component edits, duplication/deletion and expired-world checks. Targeted scripting
suite: 8 tests / 254 assertions. Setup scripts pin the PSF/Microsoft runtime
archives and build the managed bridge/example. Full API/editor/lifecycle and
feature-project coverage is explicitly still pending.

2026-10-22 validation checkpoint: full regression passes 268 tests / 57,283
assertions. All three languages execute the checked-in language example modules
against the same live world bindings. SetupScriptingRuntimes.ps1 installs and
verifies bundled runtime archives and builds C# artifacts. The next implementation
pass must continue editor operation bindings, landscape/foliage and material API
coverage, script components/lifecycle, and standalone feature-project execution;
provider conformance alone is not completion.

2026-10-22: ScriptEditorPlugin now supplies document/selection/actor/transaction,
CSG and command bindings plus the public ExecuteEditorScript entry point.
GenesisEditor -runscript executes real language modules, reports JSON outcomes
and cancels unfinished script transactions. Landscape/foliage feature projects
passed all six language/backend runs and native save/reload/grouped-undo checks.
MaterialEditor owns material scripting for expression discovery, graph creation,
translation, load and DXIL/SPIR-V cooking. All three languages verify rejected
cooks preserve files and resaves retain UUIDs. Runtime asset discovery, exports
and bounded bulk reads retain native identity semantics.

2026-10-22: runtime-only FScriptWorldSubsystem adds isolated Genesis.Script
component instances, begin/tick/event/end, added/removed component reconciliation,
fault suppression and thread/reentry checks. RuntimeDataCheck executes all three
language examples without editor/UI/RHI dependencies. Targeted scripting/editor
tests pass 17 cases / 654 assertions. Automatic project startup/PIE/input, complete
API and feature coverage, cooked packaging, advanced material authoring and
advanced rendering remain unfinished acceptance gates.

2026-10-22 checkpoint: project module catalogs now validate source/runtime paths,
unique names and limits while preserving unknown module options. RuntimeDataCheck
-scriptproject selects the project's gameplay language and compiled/source payload;
GenesisEditor -scriptmodule resolves the same catalog for editor automation. All
three project-based runtime checks and six named-module terrain/backend runs pass.
Full regression: 278 tests / 57,716 assertions; zero failures or skips. Full API,
advanced materials/rendering, packaging, project UI and PIE/input remain open.
