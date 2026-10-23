# Material graph first implementation

Implemented 2026-10-22: an initial graph compiler, editor and desktop rendering
slice. This is the start of the [material roadmap](MaterialSystemPlan.md), not
full Unreal feature parity.

## Using the editor

Run `Engine/Build/SetupMaterialCompiler.ps1` once to install the pinned Microsoft
DXC distribution under ignored `Engine/Binaries/ThirdParty/DXC`. It includes the
compiler and license notices. Alternatively point `GENESIS_DXC` at a DXC build
that can emit both DXIL and SPIR-V. The Windows SDK compiler alone may not have
SPIR-V enabled. Shipped games do not need this tool.

Create a material through Add or the Content Browser context menu and open it.
Existing v1-v3 materials convert in memory to explicit texture/factor/vertex-color
nodes. Opening or compiling does not change the file; saving upgrades to v4.

- Right-click the graph and search for a node to add it.
- Drag an output pin to an input. Scalar values broadcast to vector arithmetic;
  incompatible vector widths and cycles are rejected. An input has one source.
- Drag nodes to arrange them, middle-drag to pan and scroll to zoom around the
  pointer. Frame fits all nodes. Alt-click an input to disconnect it.
- Select nodes to edit constants, arithmetic defaults, channel masks, textures
  and normal sampling in Details. Values are linear shader values. A color
  texture marked sRGB decodes during sampling; normals always sample linear.
- TextureSample and VertexColor output RGBA. Use ComponentMask (RGB=7, alpha=8)
  to connect them to the material's RGB and scalar opacity outputs.
- Ctrl/Shift-click adds or removes nodes from the selection; drag empty space
  for marquee selection. Drag a selected node to move the group. Ctrl+A selects
  all; Ctrl+C/X/V copy, cut and paste; Ctrl+D duplicates with fresh node IDs.
  Delete removes selected nodes and their wires. Ctrl+Z/Ctrl+Y undo and redo.
- Comment nodes group annotations visually. Node descriptions and comment sizes
  live in Details and do not affect generated shaders. Reroutes pass values through.
- Compile / Preview cooks both desktop targets and updates the sphere/cube
  preview. Lit / Unlit changes the preview lighting. Compilation runs on a
  background worker; Live Preview debounces edits and drops superseded results.
  Save waits for the current revision to compile successfully.
- Save writes the material; Save and Apply also assigns it to the selected mesh
  actors' chosen material slot, as an undoable map transaction. Closing a dirty
  material window asks before discarding changes. File saves are not map undo.

Supported nodes: Constant, Constant2Vector, Constant3Vector, Constant4Vector,
TextureCoordinate (UV0), TextureSample (2D repeat sampling), VertexColor, Add,
Subtract, Multiply, Divide, LinearInterpolate, OneMinus, Clamp, ComponentMask and
NormalDecode, Reroute, Comment, Abs, Ceil, Floor, Frac, Sine, Cosine, Tangent,
SquareRoot, Power, Min, Max, Saturate, Normalize, Length, DotProduct, CrossProduct,
AppendVector, Rotator, Distance, Step, SmoothStep, Sign, Fmod, Exp and Log.
ScalarParameter and VectorParameter expose named defaults through uniform buffers.
Shared names must have matching types/defaults. Vector parameters output RGBA;
mask RGB for Base Color. Changing defaults keeps the cooked shader unchanged.
Rotator uses radians. Clamp has input/min/max pins. Divide protects a near-zero divisor
with signed epsilon. Disconnected expressions are retained for editing but not
emitted; structural link/cycle validation still checks them.

Outputs: Base Color, Roughness, Metallic, Opacity, tangent-space Normal and
Emissive Color. Blend choices remain opaque, masked and translucent with the
existing renderer's lighting and transparency limitations. The first preview
offers sphere/cube and Lit/Unlit; advanced environment/mesh controls are pending.

## Runtime and format

`Runtime/Engine/Public/Materials` owns the material and graph types.
`Runtime/Engine/Private/Materials` owns material serialization/resource loading,
graph validation and HLSL translation (`HLSLMaterialTranslator.cpp`).
`Editor/MaterialEditor` is an independent module owning the toolkit, graph canvas,
preview viewport and desktop compilation utilities (`MaterialEditingLibrary`).
GenesisEditor supplies callbacks for map selection, transactions and host refresh;
the material editor does not depend on MapEditor or MapCore.
`Runtime/Renderer` consumes cooked shader bytes and immutable texture snapshots,
caches pipelines/textures and defers releases while GPU work is in flight.
Material v4 stores graph version 3 (versions 1 and 2 remain readable), editor node
positions and descriptions, generated source and
DXIL/SPIR-V payloads. Asset UUIDs are preserved on edits. Shader source comparison
detects semantic edits without penalizing layout changes. Texture identity and
sampler role remain resource-binding metadata, independent of shader source.

Limits for this slice: 256 nodes and 12 reachable texture samples. Dependencies
include graph texture references used by the shader. Stale cooks and invalid
graphs fail instead of silently rendering with an unrelated program. A compiler
failure retains the last successful preview; the status shows the failure.
Saved materials whose generated source no longer matches the engine's compiler
contract require recooking; cross-version automatic recooking is not implemented.

The first cooked format contains two desktop target binaries. Additional target
blob formats and shader-toolchain adapters are a later schema extension. The
portable graph does not assume D3D12 on Xbox, SPIR-V on Switch 2, or desktop
resource limits on PS5. Future console targets require their licensed shader
compilers, RHI implementations, capability profiles, packaging and hardware tests.
None of those platforms is currently supported.

Next milestones are reusable material functions and additional editor workflows,
followed by the renderer features in the roadmap. Landscape graphs, WPO, scene
texture nodes, Substrate-style layering and custom HLSL are not implemented.

`MaterialGraph` tests cover graph validation, cooking, package round trips,
stale-cook rejection, migration, scaled pin dragging, undo/redo and GPU readbacks
for constant and texture-driven outputs on D3D12 and Vulkan. A Slate-rendered
canvas artifact is written under `Engine/Saved/MaterialGraph` for layout inspection.

The material test source lives in `Editor/MaterialEditor/Private/Tests` and is
compiled by the EngineTests program, keeping test dependencies out of the
MaterialEditor production library. Source ownership follows the rules in
[`AGENTS.md`](../../../AGENTS.md).

## Material instances and runtime updates

Select a saved graph material in Content Browser and choose **Material Instance
from Selection**. Instances can also parent other instances. Open an instance
to choose its parent, enable scalar/vector/texture overrides, reset to inherited
values, undo/redo changes, preview sphere/cube and save/apply to a mesh slot.
The instance editor rejects parent cycles before saving. A changed parent may
leave an obsolete override; its removal button lets you repair the instance.

`TextureSampleParameter2D` exposes a named 2D texture using its parent's sampling
role. Repeated texture names share one binding and must have matching defaults
and sampling roles. Scalar/vector names must also have matching types/defaults.
Parameter names are case sensitive. Texture replacements and numeric defaults
reuse the parent's cooked shader; static permutations are a later feature.

Instance package schema `Genesis.MaterialInstance` version 2 stores a parent
reference, numeric overrides and texture overrides. Version 1 numeric-only
instances remain readable. Dependencies include parent and overridden textures.
Loading resolves at most 32 assets in a parent chain, detects cycles, validates
override names/types and commits output only after success. Runtime-only loaders
and material pickers accept instances alongside materials.

`FMaterialInstanceDynamic::Create` takes a valid cooked parent snapshot. Its
scalar/vector/texture setters and clear method validate before replacing the
resolved snapshot. Publish `GetMaterial()` through `LoadMaterialResources` and
a new loaded material/mesh snapshot; existing GPU frames retain their old
resources until they finish. This API does not mutate an asset file, discover
actors, or automatically attach itself to a world component.
