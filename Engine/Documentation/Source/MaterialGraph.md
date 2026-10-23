# Material authoring

The Material Editor provides typed node authoring for the current native-mesh
renderer: reusable functions, instances, parameter collections and six-channel
attribute layers. The broader [material roadmap](MaterialSystemPlan.md) includes
separate renderer and Unreal parity work; this authoring milestone does not
imply full Unreal expression or rendering parity.

## Setup and workflow

Run `Engine/Build/SetupMaterialCompiler.ps1` to install the pinned DXC toolchain,
or set `GENESIS_DXC` to a compiler supporting DXIL and SPIR-V. Shipped games
consume cooked shaders and do not require DXC.

Open a project and use the Content Browser Add/context menu to create a Material,
Material Function or Material Parameter Collection. Select a saved material to
create a Material Instance. Double-click to open an editor; reopening an already
open asset focuses its existing draft.

- Right-click the graph to search/add expressions. Drag output pins onto inputs;
  Alt-click an input to disconnect it. Each input has one source.
- Drag nodes, middle-drag to pan, scroll to zoom and use Frame to fit.
  Ctrl/Shift-click changes selection; empty-space dragging selects a marquee.
  Moving a selected node moves the group.
- Ctrl+A selects all; Ctrl+C/X/V copy/cut/paste; Ctrl+D duplicates; Delete removes
  selected nodes and wires. Internal wires, properties and function snapshots
  survive clipboard operations. Ctrl+Z/Ctrl+Y undo/redo graph, layout, blend
  settings and attribute-output mode.
- Details edits values, resource references, parameter names/groups, descriptions
  and comment bounds. Reroutes forward typed values. Drag textures, functions
  and collections from the browser onto the graph.
- Compile / Preview cooks both desktop targets in a background worker. Live
  Preview debounces edits; superseded results cannot replace newer revisions.
  Failures retain the last successful preview and label it stale.
- Preview sphere, cube, plane or a native custom mesh; adjust yaw/pitch/zoom and
  Lit/Unlit. Preview Selected Node does not replace the saved material program.
  Save returns to the complete material target and waits for its valid compile.
- Save preserves the package UUID. Save and Apply assigns the chosen material
  slot on selected native-mesh actors through a host map transaction. File saves
  are separate from map undo. Dirty-window close asks before discard.

## Expressions and surface contract

`Material.Describe` is the machine-readable expression/pin inventory. Families
include constants, UV0, vertex color, 2D texture samples, scalar/vector/texture
parameters, arithmetic/interpolation, trigonometry, exponent/log, rounding,
clamp/saturate, step/smooth-step, vector append/mask/normalize/length/dot/cross/
distance, UV rotation, normal decode, comments and reroutes.

Numeric values have widths 1–4. Scalars broadcast in compatible arithmetic;
incompatible widths and cycles are errors. Vector/texture/vertex-color results
are RGBA: mask RGB for color and alpha for opacity. Rotator uses radians; Divide
guards near-zero divisors with signed epsilon. Layout and annotations do not
change shader semantics. Disconnected nodes remain editable; structural links
and cycles are still validated.

Outputs: Base Color, Roughness, Metallic, Opacity, tangent-space Normal and
Emissive. Blend choices: Opaque, Masked and Translucent. sRGB textures decode
during sampling; normal samples always use linear values.

Enable material attributes to replace individual output pins with one typed
bundle. MakeMaterialAttributes supplies six channels with defaults;
BreakMaterialAttributes selects a channel; BlendMaterialAttributes interpolates
two bundles and normalizes the blended normal. Functions can accept/return
bundles to build reusable layers. This blends existing surface parameters;
layered BSDF scattering and Substrate are separate roadmap features.

## Functions and permutations

A Material Function has one result and up to eight named input slots (0–7).
FunctionInput types are float1–4 or attributes; defaults apply to unbound
arguments. MaterialFunctionCall exposes declared slots. Functions may call
functions and use parameters/collections. Missing/type-invalid links, duplicate
references, recursion and exceeded budgets fail explicitly.

Materials embed immutable function snapshots: runtime compilation/rendering
does not load function sources. Authoring refreshes through the registry.
Saving a function or master recooks its affected dependency closure, including
static instances, while preserving unrelated assets. A failed dependent cook is
reported without overwriting that dependent. External changes refresh open
editors and invalidate old worker results.

StaticSwitchParameter chooses True/False at cook time. QualitySwitch selects
Low/Medium/High. PlatformSwitch chooses D3D12/Vulkan in the target compiler;
resource layouts include both platform branches. Static/quality edits cook;
a bounded 32 MiB source/compiler cache reuses earlier permutations. Uniform and
texture replacements reuse shader binaries.

## Instances and shared parameters

Instances inherit materials or instances. Enable/reset scalar/vector/texture/
static overrides, choose inherited or explicit quality, preview, undo/redo and
save/apply. Names are case sensitive; shared names must agree in type/defaults.
ParameterGroup labels organize numeric overrides. Remove obsolete overrides
after parent changes. Chains are bounded to 32 assets and reject cycles/stale
cooks. Static/quality instances store a validated cooked permutation.

Collections contain up to 256 uniquely named scalar/float4 defaults. Their editor
supports add/remove/rename/value edits, history and save. Collection expressions
use UUID plus parameter name. Material loading refreshes defaults without shader
recooking; shared values are excluded from per-material instance overrides.

At runtime, `FMaterialParameterCollectionInstance` validates scalar/vector
updates; register it through `FNativeMeshRenderer::SetParameterCollectionInstance`.
It requires a UUID-backed reference and owning-thread use. Prepared frames retain
immutable uniform snapshots. `FMaterialInstanceDynamic` validates numeric/texture
updates; publish its resolved material through a new loaded material/mesh snapshot.
These APIs do not modify asset files or automatically attach to actors.

## Formats, ownership and limits

Runtime types/codecs/HLSL translation live in `Runtime/Engine/Materials`.
The independent `Editor/MaterialEditor` owns authoring/DXC adapters;
`Editor/EditorFramework` owns shared asset drag data. Selection/transactions
enter through host callbacks. MaterialEditor depends on neither MapEditor nor
MapCore, and runtime code does not depend on editor modules.

Material schema v4 stores graph v6, metadata, generated source, DXIL and SPIR-V.
Graph versions 1–6 and legacy material versions 1–3 remain readable. Opening does
not rewrite files. Functions/collections use schema v1. Instances read v1/v2;
v3 adds static/quality permutations. UUIDs remain authoritative.

Budgets: 256 expanded nodes, 12 reachable texture samples, function expansion
depth 16, at most 64 refreshed dependencies, embedded metadata limited to
65 graphs/4,096 nodes/depth 16. Function interfaces have eight inputs/one result.
Invalid references/types/unknown expressions fail. Saved shaders whose generated
source contract changes require recooking.

Codec and compiler validation uses explicit error returns. The checked graph
serializer `Encode(Graph, Out, Error)` preserves `Out` on failure; the convenience
snapshot overload returns null for invalid bounded structure. Collection runtime
instances are created with `FMaterialParameterCollectionInstance::Create`, which
returns an empty pointer and localized diagnostic for invalid inputs. Failed
collection refresh preserves the original graph and change flag. No exceptions
are used to signal ordinary malformed asset or graph data in these paths.


Targets are desktop D3D12 and Vulkan. Switch 2, Xbox Series X/S and PS5 require
licensed compilers, RHIs, capability profiles, packaging and hardware tests.
Additional UV sets/texture dimensions, scene inputs, time/noise, custom HLSL,
WPO/displacement, landscape graphs, extra surface channels/domains/shading models
remain roadmap work. Full Unreal parity is not claimed.

## Acceptance and examples

`EngineTests --test-suite=MaterialGraph,EditorScripting` covers compilation,
serialization/migration, dependencies/recursion, caching, collections, instances,
multi-DPI canvas input, clipboard/history, background revision/save behavior and
D3D12/Vulkan pixel readbacks. Tests are linked only into EngineTests.

Run `Samples/Features/MaterialAuthoring/Run.ps1` for Lua, Python and C# projects
using the public editor scripting entry point. See the sample README for setup,
generated assets and regression expectations.
