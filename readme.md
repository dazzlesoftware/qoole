# Genesis Editor

Genesis Editor is the level editor for the planned Genesis Engine. It is
built on the GPL source release of Qoole 99 v0.99 (see "Origins" below) and,
like that release, is licensed under the GNU GPL (see license.txt).

Design documents live in `docs/`; start with
[docs/GenesisUI.md](docs/GenesisUI.md), the design of the Genesis UI
framework the editor is built on, which replaced MFC.

## Building

Open `source/Genesis.slnx` in Visual Studio 2026 with the "Desktop
development with C++" workload. The solution is x64 only and needs only the
Windows SDK: the Vulkan headers and the other third-party libraries are
vendored in `source/External`, and `vulkan-1.dll` is loaded at runtime from
the graphics driver. From a command line:

    msbuild source\Genesis.slnx -p:Configuration=Release -p:Platform=x64

`GenesisEditor.exe` (`GenesisEditorD.exe` for Debug), the test
applications and `EngineTests.exe` are written to `source/Exec`. `GenesisTests`, the tests of
the map core, is in `source\Build\GenesisTests\x64\<Configuration>`.

## The editor

`GenesisEditor.exe` is built on Genesis UI and draws through Direct3D 12 or
Vulkan:

- Four docked map views (top, front, side, and 3D in wireframe or with
  solid faces), an outliner, properties, prefabs, settings and a log, with
  menus, a toolbar and rebindable shortcuts.
- Tools: select and move (snapped to the grid), rotate, scale, clip, and
  vertex, edge and face editing. Cut, copy, paste, duplicate and delete;
  groups, and editing inside a group; hollow, subtract and intersect;
  quarter turns and flips. Every change can be undone.
- Prefab brushes and entities from the built-in Genesis game
  (`source/Content/Editor`); entity keys are edited with the types the
  entity set gives them. Selections can be saved as prefabs.
- Maps are saved as `.qle`, and `.map` files are imported and exported.
  Maps made for the Qoole-era games open as Genesis maps.

Pass a map on the command line to open it. `-vulkan` switches from D3D12 to
Vulkan, `-appscale=1.25` adds an application scale, `-resetlayout` starts
from the default layout and key bindings, `-nodebug` / `-debug` turn the
D3D12 debug layer or Vulkan validation off or on (on by default in Debug
builds), and `-selftest=<dir>` runs a scripted check that saves screenshots
and a log. Settings and saved prefabs are kept in
`%LOCALAPPDATA%\Genesis\Editor`.

The MFC editor, its `GenesisDraw*` renderer plugins, `GenesisEditor.pak` and
the Qoole-era games (Quake, Quake 2, Hexen 2, Half-Life, Sin, Heretic 2)
were removed in milestone 6 of the Genesis UI plan.

## Genesis Engine modules

The code lives in static-library modules under `source/Engine` (`Core`,
`ApplicationCore`, `RHI`, `UICore`, `UI`, `UIRenderer`, `MapCore`,
`MapEditor`); `MapCore` builds Qoole's map model, geometry and game
definitions from `source/Basic`, `Objects`, `Render` and `Game`.
Third-party libraries (FreeType, HarfBuzz, doctest, Vulkan headers) are
vendored in `source/External`. See sections 14 and 17 to 21 of
[docs/GenesisUI.md](docs/GenesisUI.md) for what is there.

- The test applications (`source/Programs/TestApps`, built to
  `source/Exec`) each show one feature in a window of their own, so a
  feature can be tried, debugged and checked on its own:
  - Genesis UI: `UIRenderTest` (draw elements, without widgets),
    `UIButtonsTest`, `UIInputsTest` (combo boxes, sliders, spin boxes,
    colors), `UITextTest`, `UILayoutTest`, `UIListsTest`, `UIMenusTest`
    (menus and tooltips), `UIDragDropTest`, `UIWindowsTest` (windows, DPI,
    monitors), `UIViewportTest` (an RHI viewport), `UIPropertyGridTest`,
    `UICommandsTest` (commands, menu bar, toolbar, key bindings) and
    `UIDockingTest`.
  - The map editor: `MapViewsTest` (the four views and the grid) and
    `MapToolsTest` (the editing tools, brush operations, undo, saving).

  They share a harness (`TestAppHarness`) and take the editor's `-vulkan`,
  `-appscale=`, `-nodebug` / `-debug` and `-resetlayout` options. With
  `-selftest=<dir>` each runs a script that drives it with simulated input,
  checks the results, saves screenshots and a log, and exits with 1 if a
  check failed; `source/Programs/TestApps/RunTestApps.ps1` runs them all.
  F2 cycles the app scale and F3 simulates a DPI change.
- `EngineTests.exe` runs the headless doctest suite: layout, input, text
  editing, list/tree, editor framework and map editor tests on the null
  platform, plus offscreen GPU tests on D3D12 (WARP) and Vulkan (skipped
  without a Vulkan driver).
- Debug builds enable the D3D12 debug layer (the Windows "Graphics Tools"
  optional feature) and the Vulkan validation layer when installed.
- The UI shaders are in `source/Engine/UIRenderer/Private/Shaders` and the
  map views' in `source/Engine/MapEditor/Private/Shaders`. DXIL is compiled
  at build time with the Windows SDK; after editing the HLSL, rerun
  `BuildUIShaders.cmd` or `BuildMapShaders.cmd` with a SPIR-V capable
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
