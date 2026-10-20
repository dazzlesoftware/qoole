# Genesis Editor

Genesis Editor is the level editor for the planned Genesis Engine. It is
built on the GPL source release of Qoole 99 v0.99 (see "Origins" below) and,
like that release, is licensed under the GNU GPL (see license.txt).

Design documents live in `Engine/Documentation/Source`; start with
[GenesisUI.md](Engine/Documentation/Source/GenesisUI.md), the design of the
Genesis UI framework the editor is built on, which replaced MFC.

## Layout

The tree follows Unreal Engine's:

    Genesis.slnx                 the solution
    Engine/
      Source/
        Runtime/                 Core, ApplicationCore, RHI, SlateCore, Slate, SlateRHIRenderer
        Editor/                  MapCore, MapEditor
        Programs/                GenesisEditor, GenesisTests, EngineTests, TestApps
        ThirdParty/              FreeType2, HarfBuzz, Vulkan, doctest
      Content/                   Slate/Fonts, Editor (the built-in game's data)
      Build/                     Engine.props, EnginePaths.props
      Documentation/Source/      design documents
      Binaries/Win64/            programs (built)
      Intermediate/Build/Win64/  libraries and objects (built)
      Saved/                     test app results (written at run time)

## Building

Open `Genesis.slnx` in Visual Studio 2026 with the "Desktop development with
C++" workload. The solution is x64 only and needs only the Windows SDK: the
Vulkan headers and the other third-party libraries are vendored in
`Engine/Source/ThirdParty`, and `vulkan-1.dll` is loaded at runtime from the
graphics driver. From a command line:

    msbuild Genesis.slnx -p:Configuration=Release -p:Platform=x64

`GenesisEditor.exe` (`GenesisEditorD.exe` for Debug), the test
applications, `EngineTests.exe` and `GenesisTests.exe` are written to
`Engine/Binaries/Win64`.

## The editor

`GenesisEditor.exe` is built on Genesis UI and draws through Direct3D 12 or
Vulkan:

- A viewport laid out like Unreal's: one to four panes in Unreal's twelve
  arrangements (four by default: top, perspective, front, right), any pane
  maximized. Each pane has its own toolbar: tools, grid and rotation
  snapping, viewport type (perspective, top, bottom, left, right, front,
  back; Alt+G/J/K/H, with Shift for the opposite side), field of view,
  camera speed, view mode (Lit, Unlit, Wireframe; Alt+4/3/2), show flags
  and settings. Perspective panes have a ground grid, fly with right drag
  and W/A/S/D (the wheel sets the speed), and orbit, dolly and pan with
  Alt + left, right and middle drag.
- The rest of Unreal's level editor layout: Place Actors (Basic, Lights,
  Geometry; drag into a viewport) and the Brush Editing mode's tools at the
  left, the Outliner and Details at the right, and Content Browser 1 (up to
  four, from the Window menu) and the Output Log at the bottom. The toolbar
  has Save, the editor mode (Selection Shift+1, Brush Editing Shift+6) and
  Add. Edit > Editor Preferences has the graphics RHI (DirectX 12 or
  Vulkan, from the next start), the debug layer, the interface scale,
  keyboard shortcuts, the map to load at startup, viewport controls, and
  grid and snapping. Menus and shortcuts come from commands.
- Tools: select and move (snapped to the grid), rotate, scale, clip, and
  vertex, edge and face editing. Cut, copy, paste, duplicate and delete;
  groups, and editing inside a group; hollow, subtract and intersect;
  quarter turns and flips. Every change can be undone.
- Prefab brushes and entities from the built-in Genesis game
  (`Engine/Content/Editor`); entity keys are edited with the types the
  entity set gives them. Selections can be saved as prefabs.
- Maps are saved as `.qle`, and `.map` files are imported and exported.
  Maps made for the Qoole-era games open as Genesis maps.

Pass a map on the command line to open it. `-vulkan` and `-d3d12` choose the
backend over the preference, `-appscale=1.25` adds an application scale, `-resetlayout` starts
from the default layout and key bindings, `-nodebug` / `-debug` turn the
D3D12 debug layer or Vulkan validation off or on (on by default in Debug
builds), and `-selftest=<dir>` runs a scripted check that saves screenshots
and a log. Settings and saved prefabs are kept in
`%LOCALAPPDATA%\Genesis\Editor`.

The MFC editor, its `GenesisDraw*` renderer plugins, `GenesisEditor.pak` and
the Qoole-era games (Quake, Quake 2, Hexen 2, Half-Life, Sin, Heretic 2)
were removed in milestone 6 of the Genesis UI plan.

## Genesis Engine modules

The code lives in static-library modules named after their Unreal
counterparts: `Core`, `ApplicationCore`, `RHI`, `SlateCore`, `Slate` and
`SlateRHIRenderer` in `Engine/Source/Runtime`, `MapCore` and `MapEditor` in
`Engine/Source/Editor`. `MapCore` holds Qoole's map model, geometry and game
definitions (`Basic`, `Objects`, `Render` and `Game`, headers under
`Public`, sources under `Private`). Third-party libraries (FreeType2,
HarfBuzz, doctest, Vulkan headers) are vendored in
`Engine/Source/ThirdParty`. See sections 14 and 17 to 21 of
[GenesisUI.md](Engine/Documentation/Source/GenesisUI.md) for what is there.

- The test applications (`Engine/Source/Programs/TestApps`, built to
  `Engine/Binaries/Win64`) each show one feature in a window of their own, so a
  feature can be tried, debugged and checked on its own:
  - Genesis UI: `UIRenderTest` (draw elements, without widgets),
    `UIButtonsTest`, `UIInputsTest` (combo boxes, sliders, spin boxes,
    colors), `UITextTest`, `UILayoutTest`, `UIListsTest`, `UIMenusTest`
    (menus and tooltips), `UIDragDropTest`, `UIWindowsTest` (windows, DPI,
    monitors), `UIViewportTest` (an RHI viewport), `UIPropertyGridTest`,
    `UICommandsTest` (commands, menu bar, toolbar, key bindings) and
    `UIDockingTest`.
  - The map editor: `MapViewsTest` (the viewport layout, its toolbars and the grid) and
    `MapToolsTest` (the editing tools, brush operations, undo, saving).

  They share a harness (`TestAppHarness`) and take the editor's `-vulkan`,
  `-appscale=`, `-nodebug` / `-debug` and `-resetlayout` options. With
  `-selftest=<dir>` each runs a script that drives it with simulated input,
  checks the results, saves screenshots and a log, and exits with 1 if a
  check failed; `Engine/Source/Programs/TestApps/RunTestApps.ps1` runs them
  all and writes the results to `Engine/Saved/TestAppResults`.
  F2 cycles the app scale and F3 simulates a DPI change.
- `EngineTests.exe` runs the headless doctest suite: layout, input, text
  editing, list/tree, editor framework and map editor tests on the null
  platform, plus offscreen GPU tests on D3D12 (WARP) and Vulkan (skipped
  without a Vulkan driver).
- Debug builds enable the D3D12 debug layer (the Windows "Graphics Tools"
  optional feature) and the Vulkan validation layer when installed.
- The UI shaders are in `Engine/Source/Runtime/SlateRHIRenderer/Private/Shaders`
  and the map views' in `Engine/Source/Editor/MapEditor/Private/Shaders`. DXIL is compiled
  at build time with the Windows SDK; after editing the HLSL, rerun
  `BuildSlateShaders.cmd` or `BuildMapShaders.cmd` with a SPIR-V capable
  `dxc.exe` (Vulkan SDK or a DirectXShaderCompiler release) to regenerate
  the Vulkan headers.

## Origins

The text below is the readme of the original Qoole 99 source release.

> This is the complete source code for Qoole 99 v0.99.
>
> The projects have been tested with Visual C++ 6.0, and Intel C/C++ Compiler 4.5
> (but the last one is not needed, you can compile with Visual C++ 6.0 compiler).
>
> The code is all licensed under the terms of the GPL (gnu public license).
> You should read the entire license, but the gist of it is that you can do
> anything you want with the code, including sell your new version. The catch
> is that if you distribute new binary versions, you are required to make the
> entire source code available for free to everyone.
>
> If you are going to publicly release modified versions of this code, you must
> also make source code available.  I would encourage teams to even go a step
> farther and investigate using public CVS servers such as sourceforge.net
> for development where possible.
>
> The primary intent of this release is for entertainment and educational
> purposes, but the GPL does allow commercial exploitation if you obey the
> full license.  If you want to do something commercial and you just can't bear
> to have your source changes released, it is posible to still negotiate a
> separate license agreement for money(check contact.txt for details).
>
> Thanks to: Matt "WhiteFang" Ayres, Paul "Hsup" Hsu, Barnaby "VolVE" Claydon,
> Aaron "TheRelentless" Bellante, Kenn Hwang, GX Media Inc. and everybody that
> used to be in the dead #Qoole EFnet.
>
> Pablo "^Fishman" Zurita.
> Ex-Qoole programmer.
