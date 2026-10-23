# Material authoring regression projects

Lua, Python and C# projects execute the same `Source/Scenario.json` through the
public editor scripting service. Each creates native assets and a saved map you
can reopen and edit in GenesisEditor.

From the repository root, build Release x64 GenesisEditor, run
`Engine/Build/SetupMaterialCompiler.ps1` and
`Engine/Build/SetupScriptingRuntimes.ps1`, then:

```powershell
Samples/Features/MaterialAuthoring/Run.ps1 -Backend d3d12
Samples/Features/MaterialAuthoring/Run.ps1 -Backend vulkan
# Optional: -Language Lua, Python or DotNet (default: All)
```

The runner launches each project, executes its named `main` module and validates
the result. Generated `Content` packages/maps and `Saved` results are ignored by
Git. Open a language's `MaterialAuthoring.gproject` after running to inspect:

- A shared vector parameter collection, color function and typed layer function.
- A native texture and master material using texture sampling, collection values,
  functions, a roughness parameter, static/quality switches and material attributes.
- A red static/quality material instance, native cube and two mesh actors.
- A saved/reloaded native scene and grouped transaction undo/redo assertions.

`EngineTests --test-suite=MaterialGraph,EditorScripting` additionally runs the
scenario in all three providers and checks both permutations with D3D12/Vulkan
GPU pixel readbacks. The CLI runner verifies execution/save/reload; it does not
itself compare pixels. Focused material tests cover editing, async saves, failure
atomicity, function recursion, dependency recooking and collection updates.

The shared language modules resolve expression names through `Material.Describe`;
the scenario does not depend on hard-coded expression enum ordinals. Scripted
texture creation supports small RGBA test assets (65,536-byte transfer limit);
use native import for larger production textures.

This tests the current six-channel desktop surface renderer. Advanced domains,
lighting models and console targets remain separate work. See
`Engine/Documentation/Source/MaterialGraph.md` for supported features and limits.
