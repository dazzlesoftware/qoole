# Script API coverage and acceptance

The requested completion gate is functional parity with the engine and editor
features that currently exist, across Lua, Python and C#. It is not satisfied by
installing three interpreters or by passing the provider conformance tests.
No native object pointers may cross a script binding. Large native resources need
handles/chunked APIs rather than raising JSON limits indefinitely.

| Surface | Current evidence | Remaining acceptance |
| --- | --- | --- |
| Provider execution | Real Lua/Python modules and compiled C# DLL; isolated state, host calls, errors, module replacement, thread checks | Project startup/PIE/input, debugging/reload and cooked packaging |
| Binding descriptions | Typed arguments/results, capabilities, editor-only flag, discoverable catalog | Generate language facades and reference docs from the complete catalog |
| World/actor/component ownership | 18 world operations plus API discovery and logging; all three languages tested against live stable IDs | Paging/large worlds, project startup/input/PIE; component lifecycle and named events now execute in the runtime subsystem |
| Transforms/hierarchy | Local and world transforms, parenting through bindings | Full geometry/math/query surface and examples |
| Assets/projects | Runtime discovery, ID-authoritative resolution, exports and chunked bulk reads; material cooks and map saves | Remaining typed load/save/import/export/cook APIs, project language picker |
| Materials | Expression/output discovery, graph creation/translation/load/cook tested through each language | Instance/dynamic bindings, broader assignment and dedicated feature projects |
| Landscape | Creation, chunked heights, layers, native brushes; three language feature projects with save/reload/undo | Remaining layer/spline/query/import/export surface and rendered assertions |
| Foliage | Presets, placement, paint/erase/clear and counts; three language feature projects | Full type/instance/query/transform surface and rendered assertions |
| Geometry/CSG/meshes | Basic/native mesh placement, transforms and CSG command bindings | Full mesh/brush/query APIs, stable resource handles and feature projects |
| Editor document | Selection/scope/actor edits, ownership-checked transactions, rollback, undo/redo and map saves | Remaining file, prefab and brush edit APIs plus wider feature regression |
| Editor tools/commands/UI | Mapped command discovery/invocation preserves enabled/visible state; native terrain tool bindings | Remaining panels/viewport/settings APIs and automation controls |
| Examples | Runtime-only lifecycle runner, editor script entry point and landscape/foliage projects in all languages | Remaining feature projects, automatic project startup and rendered regression matrix |

## Registered core/world operations

`API.Describe`, `Log.Write`, `World.Serialize`, `World.Replace`,
`World.GetActors`, `World.FindActor`, `World.FindComponent`, `World.Resolve`,
`World.SpawnActor`, `World.AddActor`, `World.AddComponent`, `World.SetComponent`,
`World.RemoveComponent`, `World.SetActorName`, `World.GetLocalTransform`,
`World.SetTransform`, `World.SetParent`, `World.GetWorldTransform`,
`World.DestroyActor`, `World.DuplicateActor`.

The `World.Read`, `World.Write`, `API.Read` and `Log.Write` capabilities gate these
operations. World mutations validate a candidate and result before publishing.
Components use the native structured scene representation, including properties,
asset references and stable actor/component references. Expired worlds and stale
IDs fail explicitly. World matrices are returned as 16 row-major numbers.

## Feature-project regression gate

Each project must be runnable through the same scripting entry point exposed to
users, assert structural/state results, save/reload its native content and, where
applicable, verify rendered output on D3D12 and Vulkan. Each language must run the
same host-operation scenarios; language examples must also exercise lifecycle,
errors and host capabilities. Feature projects must cover landscape creation,
sculpting/layers/splines, foliage types/scatter/erase, materials/instances, meshes,
CSG, entity/component/hierarchy changes, assets/projects, transactions/undo and
available editor commands. Missing surfaces in this table remain incomplete.

## Implemented runtime/editor additions

Material authoring adds Material.CreateFunctionGraph/LoadFunction/SaveFunction,
LoadCollection/SaveCollection and LoadInstance/SaveInstance. CookGraph refreshes
functions/collections before cooking; SaveFunction and material saves recook
affected dependents and report any dependent failures. Instance saves support
static/quality permutations. All use the same native codecs and UUID semantics.
Assets.CreateBasicMesh and Assets.CreateTexture create native regression assets
through Editor.Files (RGBA transfer capped at 65,536 bytes).

Samples/Features/MaterialAuthoring runs these operations through the public
execution service in Lua, Python and C#. Its shared named-expression scenario
creates collections, functions, attribute layers, a textured master, a static
instance and a saved/reloaded native scene. All six desktop language/backend
CLI runs pass; EditorScripting verifies GPU results on both RHIs. This closes
the material feature-project gate, not the remaining full API coverage gates.

Runtime assets: Assets.GetAssets (paged), Resolve, LoadExports, ReadBulk (up to
16,384 bytes), GetIssues and GetMounts. These use Assets.Read, retain authoritative
UUID resolution and fail on expired registries.

Editor execution is available through GenesisEditor -runscript and the same
ExecuteEditorScript service used by tests. Editor document, terrain, foliage and
command operations are discoverable in API.Describe. Files require Editor.Files;
commands require Editor.Commands. Material.Describe/CreateGraph/Translate use
Material.Read; Material.LoadGraph/CookGraph use Editor.Files. Graph JSON exposes
all current nodes, pins, parameters and six outputs without native pointers.

FScriptWorldSubsystem runs Genesis.Script components with independent state,
begin/tick/event/end callbacks, dynamic component reconciliation and fault handling.
RuntimeDataCheck exercises these callbacks without editor/UI/renderer dependencies.
Play-In-Editor, input routing, automatic project module loading and cooked script
packaging remain open; subsystem execution is not full gameplay integration.
