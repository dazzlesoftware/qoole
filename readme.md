# Genesis Editor

Genesis Editor is the level editor for the planned Genesis Engine. It is
built on the GPL source release of Qoole 99 v0.99 (see "Origins" below) and,
like that release, is licensed under the GNU GPL (see license.txt).

Design documents live in `docs/`; start with
[docs/GenesisUI.md](docs/GenesisUI.md), the plan for replacing MFC with the
Genesis UI framework.

## Building

Open `source/Genesis.slnx` in Visual Studio 2026. It needs the "Desktop
development with C++" workload and the "C++ MFC for latest build tools
(x86 & x64)" component. The solution is x64 only; `GenesisEditor.exe`
(`GenesisEditorD.exe` for Debug) and the `GenesisDraw*.dll` renderer plugins
are written to `source/Exec`. From a command line:

    msbuild source\Genesis.slnx -p:Configuration=Release -p:Platform=x64

The solution also builds the engine modules under `source/Engine` (`Core`,
and `MapCore`, the MFC-free map core the editor links) and `GenesisTests`,
headless doctest tests of both. Run them after building:

    source\Build\GenesisTests\x64\Release\GenesisTests.exe

At startup the editor loads its data from `GenesisEditor.pak` (formerly
`Qoole99.pak`), looked up in the working directory and then next to the
executable.

## Renderers

Each view type picks a renderer in the Render Library options:

- MFC (`GenesisDrawMFC.dll`): GDI software renderer, wireframe only. Default
  for the 2D and 3D wireframe views.
- OpenGL (`GenesisDrawOpenGL.dll`).
- DX12 (`GenesisDrawDX12.dll`): Direct3D 12, falling back to the WARP
  software rasterizer when no hardware device is available. Default for the
  solid and textured 3D views.
- Vulkan (`GenesisDrawVulkan.dll`).

DX12 and Vulkan need only the Windows SDK to build: the Vulkan headers are
vendored in `source/External`, `vulkan-1.dll` is loaded at runtime from the
graphics driver, and the SPIR-V shaders are pre-generated (rerun
`source/Interface/BuildVulkanShaders.cmd` after editing `DrawShaders.hlsl`).

The original MGL and Glide renderers have been removed; their libraries were
32-bit only. Saved settings that name a renderer that no longer exists are
reset to the defaults, and if a renderer plugin fails to load the editor falls
back to MFC.

## Genesis Engine modules

New code lives in static-library modules under `source/Engine` (`Core`,
`ApplicationCore`, `RHI`, `UICore`, `UI`, `UIRenderer`, `MapEditor`), with third-party
libraries (FreeType, HarfBuzz, doctest, Vulkan headers) vendored in
`source/External`. They build as part of `source/Genesis.slnx` and need no
MFC. See sections 17 to 20 of [docs/GenesisUI.md](docs/GenesisUI.md) for
what is there.

- `GenesisMapEditor.exe` (`source/Exec`) is the new map editor on Genesis UI,
  which will replace the MFC editor: four docked map views (top, front,
  side, 3D with solid faces), an outliner and a log, menus, a toolbar and
  shortcuts. It opens `.qle` maps, imports and exports `.map`, and adds
  prefab brushes and entities; select, move (snapped to the grid), duplicate,
  delete, undo and redo. It needs no game installed: new maps use the
  built-in Genesis game (`source/Content/Editor`); the legacy games are
  offered when `GenesisEditor.pak` is next to it. Pass a map to open it;
  `-vulkan`, `-appscale=`, `-resetlayout` and `-selftest=<dir>` work as in
  UISandbox. Settings are kept in `%LOCALAPPDATA%\Genesis\MapEditor`.
- `UISandbox.exe` (`source/Exec`) is the Genesis UI sandbox: an
  editor-shaped window built from the editor framework (menus, a toolbar and
  shortcuts from commands, dockable tabs, a property grid with undo, an RHI
  viewport, rebindable keys) with the widget gallery as one of its tabs, and
  a borderless inspector window. `-vulkan` switches from D3D12 to Vulkan,
  `-appscale=1.25` adds an application scale, `-resetlayout` starts from the
  default layout and key bindings (both are saved to
  `%LOCALAPPDATA%\Genesis\UISandbox`), and `-selftest=<dir>` runs a scripted
  check (every gallery page, menus, a tooltip, the color picker, a tab torn
  off and docked back, DPI changes, minimize/restore, moving between
  monitors, resizing) that saves screenshots and a log. Keys: F2 cycles the
  app scale, F3 simulates a DPI change, F4 shows or hides the inspector.
- `EngineTests.exe` runs the headless doctest suite: layout, input, text
  editing, list/tree, editor framework and map editor tests on the null platform, plus an offscreen GPU
  test on D3D12 (WARP) and Vulkan (skipped without a Vulkan driver).
- Debug builds enable the D3D12 debug layer (the Windows "Graphics Tools"
  optional feature) and the Vulkan validation layer when installed;
  `-nodebug` / `-debug` override that in UISandbox.
- The UI shaders are in `source/Engine/UIRenderer/Private/Shaders`. DXIL is
  compiled at build time with the Windows SDK; after editing the HLSL, rerun
  `BuildUIShaders.cmd` with a SPIR-V capable `dxc.exe` (Vulkan SDK or a
  DirectXShaderCompiler release) to regenerate the Vulkan headers.

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
