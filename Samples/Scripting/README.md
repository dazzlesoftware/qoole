# Scripting examples and regression projects

Each language has a `.gproject` and real module source. `BuildWorld` creates an
actor through the same `World.SpawnActor` binding; `Invoke` forwards a named API
call and arguments. Project settings choose the gameplay/editor language.

From the repository root on Win64:

```powershell
./Engine/Build/SetupScriptingRuntimes.ps1
# Build EngineTests with MSBuild, then run the shared world regression:
./Engine/Binaries/Win64/EngineTests.exe --test-case="All three languages execute the live world API through stable IDs"
```

The setup script builds the C# module into
`Engine/Binaries/ThirdParty/DotNet/Examples/Genesis.ScriptExample.dll`; Lua and
Python source is loaded directly from these projects. Tests call each real
module, validate actor/component/transform changes and check stale IDs/lifetimes.
The providers preserve JSON object/array/null values across the shared interface.

Build RuntimeDataCheck Release x64, then run `./Samples/Scripting/RunGameplay.ps1`
to execute begin-play, three ticks, an event and end-play for each language in a
runtime-only process. Each script component owns an isolated module instance.

For editor automation, run `./Samples/Features/LandscapeFoliage/Run.ps1` after
building GenesisEditor. Use `-Backend d3d12` or `-Backend vulkan`. The three
feature projects create, sculpt, paint, place foliage, assert state and save maps.
`BuildTerrain` is callable through GenesisEditor -runscript; `Invoke` forwards any
registered host operation. See Engine/Documentation/Source/Scripting.md for flags.

Opening these descriptors does not auto-execute a module. Project module startup,
Play-In-Editor, complete API/feature coverage, debugging and cooked packaging are
still being implemented. See ScriptingAPICoverage.md for acceptance gates.
`Echo`, `Step` and `Fail` in C# also serve the provider conformance harness;
`Echo` requires that harness's test host.
