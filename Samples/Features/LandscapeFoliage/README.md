# Landscape and foliage regression projects

Run `./Run.ps1` after building GenesisEditor Release x64 and running
`Engine/Build/SetupScriptingRuntimes.ps1`. Use `-Language Lua|Python|DotNet`
and `-Backend d3d12|vulkan` to select one configuration.

Each language project invokes `BuildTerrain` from its shared language sample
through the public `GenesisEditor -runscript` entry point. The script creates a
32x32 landscape, sculpts a hill, paints a Grass layer, places foliage, checks
native state and saves `Content/Maps/Main.gmap`. Open the project to inspect it.
All creation edits form one undo step. Native EngineTests also runs the same
entry point for each language, reloads each saved map, checks stable actor IDs,
heights, layer weights, foliage counts, undo and redo.

This scenario currently covers structural state and persistence. Backend
selection initializes the corresponding renderer; rendered image assertions
remain pending. Other feature projects and the complete script API remain in
progress; see Engine/Documentation/Source/ScriptingAPICoverage.md.
