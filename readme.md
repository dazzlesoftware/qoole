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
(x86 & x64)" component. Build the x64 platform (the default); `GenesisEditor.exe`
(`GenesisEditorD.exe` for Debug) and the `GenesisDraw*.dll` renderer plugins
are written to `source/Exec`. The x86 platform still builds and goes to
`source/Exec/x86`. From a command line:

    msbuild source\Genesis.slnx -p:Configuration=Release -p:Platform=x64

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
