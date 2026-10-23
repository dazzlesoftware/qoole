# Code audit — 23 October 2026

This audit compares first-party Genesis source with the local reference at
`G:/GitHub/dazzlesoftware/UnrealEngine/Engine/Source`. Reference code was inspected
for class responsibilities, public/private boundaries, localized display text,
explicit types, and error-result APIs. Genesis retains its own implementation,
asset schemas and API branding. Vendored libraries and generated shader payloads
are outside the style migration.

## Findings addressed

- Genesis logging and build macros replace Unreal product prefixes. The Core
  string implementation is named `Containers/String.h` and `String.cpp`; include
  paths and project registration follow the rename.
- Material and editor-scripting tests live in `Programs/EngineTests`, rather than
  the corresponding editor modules. The scripted editor application belongs to
  `Programs/TestApps/GenesisEditorTest`, with a separate executable and shared
  application-host callbacks. The shipping editor has no scripted input or
  screenshot test implementation.
- The editor automation program reports check failures through its exit status
  and a runner-compatible summary, rather than only writing failure messages.
- Core owns a thread-safe display-text catalog with source-language fallback,
  atomic replacement, and validation before installation. The editor loads
  `-localizationcatalog=<path>` before building UI. Display literals and formatted
  diagnostics use that service; translated format errors fall back to the
  compile-time-validated source pattern. The gathered English catalog contains
  1,133 source messages. Command identities and user-authored asset values stay
  invariant; shared command/feature labels are localized at display boundaries.
- Shared feature-panel widgets moved out of MapEditor into the existing
  EditorFramework module, with public includes and project dependencies updated.
- Seven catch blocks were replaced with scoped transaction/stroke cleanup,
  explicit collection validation and nonthrowing filesystem operations.
- Collection serialization validates expected malformed-input cases using
  explicit error results, retaining commit-on-success output behavior.
- Shared material budgets have an owning `Materials/MaterialLimits.h` instead
  of repeated literals for cooked shader size, parent depth, parameter names,
  collection size and numeric magnitude. Serialized version numbers, shader
  container signatures, channel indices and protocol keys stay invariant.
- `.clang-format` specifies tabs, Allman braces and explicit multiline control
  flow for first-party C++. `Engine/Build/CheckCodingStandards.py` audits inferred
  declarations, product prefixes and misplaced tests. Feature dependency checks
  remain in `CheckFeatureModuleBoundaries.py`.

## Missing integration in existing features

These are implementation gaps, not a claim that all Unreal features should be
added to Genesis.

| Existing feature | Missing integration | Owning code / reference |
| --- | --- | --- |
| Runtime world/actor model | The editor still owns a legacy Object tree; structural and brush operations have not moved to runtime actor ownership. The adapter rejects component properties/references that it cannot preserve. | `Editor/MapEditor/Private/MapWorldAdapter.cpp`, `Runtime/Engine/World`; reference `Runtime/Engine/Classes/Engine/World.h` and `GameFramework/Actor.h` |
| Native prefabs | Placement copies a scene; there is no persistent prefab-instance relationship or change propagation. | `Editor/MapEditor/Private/MapPlacement.cpp`, `WorldModel.md` |
| Dynamic material instances and collections | Runtime APIs provide validated resolved values, but changing an instance does not automatically attach or republish it to an actor's render state. | `Runtime/Engine/Materials/MaterialInstanceDynamic`, `MaterialParameterCollection`, `MaterialGraph.md`; reference material instance/component render-state integration |
| Script world lifecycle | A runtime subsystem exists, but the editor does not provide Play-in-Editor/input integration. Loading a project does not execute gameplay modules. | `Runtime/Engine/Private/Scripting/ScriptWorldSubsystem.cpp`, `Scripting.md` |
| Script providers | Live reload/state migration, managed cancellation, provider dependency closure and target-specific cooking remain incomplete. | `Scripting.md`, provider adapters |
| Mesh and texture authoring | These real editors still live inside MapEditor. Their independent session/UI responsibilities should move into owning asset-editor modules, with host callbacks retained for document operations. | `Editor/MapEditor/Private/MeshAssetEditor.cpp`, `TextureAssetEditor.cpp`; reference `Editor/StaticMeshEditor`, `Editor/TextureEditor` |
| Native rendering | Native snapshots and D3D12/Vulkan paths exist; legacy brush rendering and live-document traversal remain editor-owned. Streaming, cross-world references and plugin loading are later stages. | `Renderer.md`, `WorldModel.md` |
| Material graphs | Extra UV sets/texture dimensions, scene/time/noise inputs, custom shader source, vertex displacement, landscape graphs and additional domains/surface channels are not implemented. | `MaterialGraph.md`, `MaterialSystemPlan.md`; existing graph expression enum/compiler |
| Project workflow | Command-line project creation exists; a project browser/launcher is missing. | `ProjectAssetMigration.md` |

Older overview paragraphs in README and migration documents understate newer
implemented material slots, masking/transparency, asset editors and scripting
lifecycle work. The detailed feature documents and source were used to distinguish
implemented behavior from actual gaps.

## Remaining architectural cleanup

Asset/world/material validation now uses explicit operation status and error
returns. The conversion removes 33 catch blocks and 46 explicit throw sites
from these codecs, native draw-list validation, map adapters, OBJ import and mesh
reimport. Nested readers stop before consuming invalid values, and output is
published only after successful validation. Scope guards restore appended bulk
data, dependencies, draw-list batches and temporary entity-definition changes.
The first failure diagnostic is localized and preserved through recursive calls.

`FMaterialGraphCompiler::Encode(Graph, Out, Error)` is the checked serializer used
by asset persistence and validation-sensitive comparisons. Its convenience
snapshot overload returns null on invalid bounded structure. Recursive codec
budgets recover after failures. Collection instances use the checked `Create`
factory; failed collection refresh leaves both the graph and change flag intact.
Serialized schemas and existing asset versions are unchanged.

Scripting validation is also converted: 18 native throw sites and all throwing
`Require` helpers were removed from runtime/editor bindings and the Lua, Python
and .NET adapters. Expected failures use explicit status, preserve results and
world/editor rollback, and reject malformed API names before host invocation.
Shared transfer budgets live in `Scripting/ScriptLimits.h`. Native diagnostics
are localized, filesystem checks use error results, and the coding policy now
prevents explicit throws in native scripting source.

External binding/provider/lifecycle callbacks, foreign ABI calls and worker
threads retain exception barriers. Lua protected errors and Python/managed
language exceptions remain part of their language contracts. These barriers are
covered by injected callback-failure and post-failure recovery tests, rather than
being removed as ordinary validation control flow.

Localization must distinguish display text from asset names, user-authored text,
serialized keys, paths, shader source and script APIs. Source-string catalogs do
not yet support contextual identities, plural rules, or live language switching;
they are an initial display-text service, not full internationalization parity.

The supported platform scope remains Windows desktop D3D12/Vulkan. Console SDK
compilers, RHIs, deployment and hardware verification are outside this audit.

## Validation

Validation completed on Windows x64, Release:

- `msbuild Genesis.slnx /m /p:Configuration=Release /p:Platform=x64`: passed.
  The final incremental build emitted no C++ compiler warnings.
- `EngineTests.exe --no-colors`: **303 cases, 58,739 assertions passed**, no skips.
  This includes nested malformed-input/output preservation, recursive codec
  budget recovery, failing-node diagnostics, checked collection construction and
  atomic dependency refresh, scripting rollback/malformed arguments/host response
  failures across Lua, Python and .NET, alongside existing render coverage.
- `GenesisTests.exe --no-colors`: **18 cases, 290 assertions passed**.
- `RuntimeDataCheck.exe`: passed with no editor dependencies.
- `Samples/Scripting/RunGameplay.ps1`: Lua, Python and .NET begin/tick/event/end
  lifecycle smoke checks passed with no editor dependencies.
- `RuntimeRenderCheck.exe -d3d12` and `-vulkan`: both passed runtime-only pixel
  readbacks for native materials, terrain, layers and instanced meshes.
- `GenesisEditorTest.exe` with a fresh disposable project on each backend:
  **72 checks, zero failures on D3D12; 72 checks, zero failures on Vulkan**.
  These exercise project assets, material/instance previews, native save/reopen,
  landscape/foliage operations and screenshot capture in the separate program.
  The standalone harness now skips the project-dependent material framing check
  consistently with its fixture-creation stages.
- `CheckCodingStandards.py`: **zero findings** across first-party Runtime,
  Editor and Programs C++ tokens. Vendored and generated payloads are excluded.
- `CheckFeatureModuleBoundaries.py`: passed across **46 project graphs**.
- `git diff --check`: passed.

Latest scripting logs live in `Intermediate/Scripting*.log`; disposable GUI
projects, images and logs live under `Engine/Saved/ScriptingAudit`. Earlier
validation artifacts remain under `Intermediate/Validation*.log`,
`Engine/Saved/ValidationAudit`, `Intermediate/Audit*.log` and
`Engine/Saved/CodeAudit`. No Debug build or console-platform validation
was performed. The policy scripts return nonzero on findings rather than hiding
violations.
