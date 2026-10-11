# Genesis Editor

Genesis Editor is the level editor for the planned Genesis Engine. It is
built on the GPL source release of Qoole 99 v0.99 (see "Origins" below) and,
like that release, is licensed under the GNU GPL (see license.txt).

## Building

Open `source/Genesis.slnx` in Visual Studio 2026. It needs the "Desktop
development with C++" workload and the "C++ MFC for latest build tools
(x86 & x64)" component. Build the x86 platform; `GenesisEditor.exe`
(`GenesisEditorD.exe` for Debug) and the `GenesisDraw*.dll` renderer plugins
are written to `source/Exec`.

At startup the editor loads its data from `GenesisEditor.pak` (formerly
`Qoole99.pak`), looked up in the working directory and then next to the
executable.

## Renderers

Each view type picks a renderer in the Render Library options. Besides the
original MFC, MGL, Glide and OpenGL plugins there are DX12
(`GenesisDrawDX12.dll`, Direct3D 12) and Vulkan (`GenesisDrawVulkan.dll`)
plugins. Both need only the Windows SDK to build: the Vulkan headers are
vendored in `source/External`, `vulkan-1.dll` is loaded at runtime from the
graphics driver, and the SPIR-V shaders are pre-generated (rerun
`source/Interface/BuildVulkanShaders.cmd` after editing `DrawShaders.hlsl`).

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
