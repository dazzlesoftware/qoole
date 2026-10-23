# Genesis Editor

Genesis Editor is the level editor for the planned Genesis Engine. It is
built on the GPL source release of Qoole 99 v0.99 (see "Origins" below) and,
like that release, is licensed under the GNU GPL (see license.txt).

Design documents live in `Engine/Documentation/Source`; start with
[GenesisUI.md](Engine/Documentation/Source/GenesisUI.md), the design of the
Genesis UI framework the editor is built on, which replaced MFC.

Native project/package migration is underway. See
[ProjectAssetMigration.md](Engine/Documentation/Source/ProjectAssetMigration.md)
for the format specification, implemented scope, remaining landscape work,
runtime/editor ownership split, and subsequent plugin stage.

Create a project with `GenesisEditor.exe -newproject="D:\Projects\MyWorld\MyWorld.gproject"`,
then open that `.gproject` with the editor. Projects have their own Content,
Config, Source, Plugins, Saved and Intermediate folders. Save levels as `.gmap`
and prefab assets as `.asset`; the manifests reference `.gexp` exports and
`.gbulk` binary payloads. Keep their referenced sidecars together. QLE and MAP
files still import, choosing a separate `.gmap` destination. Standalone editing
without a project remains available.

The Content Browsers now share a runtime asset registry: Refresh discovers
native packages by schema and UUID without loading their bulk payloads. Unknown
asset types appear as generic assets, and duplicate/malformed packages show
content issues. Prefab updates retain their identity; imported copies get a new
one. See [AssetRegistry.md](Engine/Documentation/Source/AssetRegistry.md).

Runtime worlds now own actors and components with persistent IDs, hierarchy,
transforms and validated references. Native world/prefab schema version 2 stores
this scene model; older native files upgrade on save. Existing editing tools
use a transitional adapter. See [WorldModel.md](Engine/Documentation/Source/WorldModel.md)
for identity rules and the remaining migration boundary.

Native mesh, texture and material assets are now available in the Content Browser.
Right-click the browser to open its creation menu, including **New Folder**.
Import OBJ geometry or images, create a mesh from selection, and create/edit a
material from **Add**. Mesh assets place by double-click or drag; native textures
can be assigned to landscape paint layers. Maps retain asset UUID references.
Native meshes now render UV textures and tangent-space normal maps on DirectX 12
and Vulkan, with metallic/roughness shading in Lit mode and base color in Unlit.
The preview uses the editor's light; transparency and multiple material slots
remain pending. See [RenderAssets.md](Engine/Documentation/Source/RenderAssets.md).

The planned node-based material editor and full material feature roadmap are in
[MaterialSystemPlan.md](Engine/Documentation/Source/MaterialSystemPlan.md).
The first node editor now saves and renders compiled graphs on D3D12 and Vulkan;
see [MaterialGraph.md](Engine/Documentation/Source/MaterialGraph.md) for setup,
supported nodes and controls. Project scripting configuration and the shared
provider foundation are described in [Scripting.md](Engine/Documentation/Source/Scripting.md).

## Layout

The tree follows Unreal Engine's:

    Genesis.slnx                 the solution
    Engine/
      Source/
        Runtime/                 Core, Projects, AssetCore, Geometry, Landscape, Foliage, Engine, Renderer, ApplicationCore, RHI, SlateCore, Slate, SlateRHIRenderer
        Editor/                  MapCore, MapEditor, MaterialEditor
        Programs/                GenesisEditor, GenesisTests, EngineTests, TestApps
        ThirdParty/              FreeType2, HarfBuzz, Vulkan, doctest
      Content/                   Slate/Fonts, Editor (the built-in game's data)
      Build/                     Engine.props, EnginePaths.props
      Documentation/Source/      design documents
      Binaries/Win64/            programs (built)
      Intermediate/Build/Win64/  libraries and objects (built)
      Saved/                     test app results (written at run time)

`Geometry` owns the 3D math and static mesh data; `Engine` owns landscape,
spline and foliage data. These runtime libraries have no Editor dependencies.
`RuntimeDataCheck.exe` exercises them without MapCore, UI or graphics startup.
MapCore still supplies the editor's legacy object tree, brushes, entity definitions and
viewport renderer while their replacements are migrated.

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

For material graph authoring and its compiler tests, first run
`Engine/Build/SetupMaterialCompiler.ps1` to install the desktop DXIL/SPIR-V compiler.

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
  has Save, the editor mode (Selection Shift+1, Landscape Shift+2, Brush
  Editing Shift+6) and
  Add. Edit > Editor Preferences has the graphics RHI (DirectX 12 or
  Vulkan, from the next start), the debug layer, the interface scale,
  keyboard shortcuts, the map to load at startup, viewport controls, and
  grid and snapping. Menus and shortcuts come from commands.
- Tools: select and move (snapped to the grid), rotate, scale, clip, and
  vertex, edge and face editing. Cut, copy, paste, duplicate and delete;
  groups, and editing inside a group; hollow, subtract and intersect;
  quarter turns and flips. Every change can be undone.
- Static mesh actors, as in Unreal: Cube, Sphere, Cylinder, Cone and Plane
  (Place Actors > Shapes, 128 units across). They keep their location,
  rotation and scale (Details > Transform, and the Static Mesh to show),
  draw lit with smooth shading, are picked like brushes, are saved in
  native `.gmap` packages, and take no part in CSG, clipping or `.map` export.
- Landscapes, as in Unreal: the Landscape mode (Shift+2) makes a flat
  heightfield of up to 512 x 512 quads (New Landscape: quads and quad
  size) and sculpts it with a left drag in any view, under a brush ring:
  Sculpt (Shift lowers), Smooth, Flatten and Noise, with a brush size,
  strength and falloff, and Paint: weight-blended paint layers (Grass,
  Rock, Dirt, Sand, Snow, or your own names and colors) painted under the
  brush, Shift erasing. A stroke is one undo step. Landscapes are saved in
  native `.gmap` packages with their heights and layers, and draw and pick like static meshes.
  Heightmaps and weightmaps import and export as in Unreal: grayscale PNG
  (16-bit heights, 8-bit weights; BMP, JPEG and TIFF are read too) or
  headerless RAW (`.r16`, `.r8`, `.raw`), mid-grey being height 0 and a
  Z Range (512 by default, Unreal's Z scale of 100) from black to white. A
  heightmap makes a new landscape (a point per pixel, big images resampled
  to 512 quads) or new heights for an existing one; a weightmap gives the
  selected paint layer its weights, the other layers giving way.
  Each paint layer has a texture, repeating every Tiling units: one of the
  built-in Grass, Rock, Dirt, Sand and Snow patterns (tinted with the
  layer's color; preset layers start with theirs), an image file (PNG,
  BMP, JPEG, TIFF, drawn as it is) or none (its plain color). The layers
  are blended by their weights into one image per landscape, up to
  2048 x 2048, that the views draw it with; a stroke re-bakes only the part
  it touched.
  More of Unreal's tools and settings: Ramp (drag from one point to another
  for a slope the brush's size wide), Erosion (steep slopes wear down past a
  talus angle), Flatten to a target height or only up or only down, Paint
  toward a target weight, and Auto Paint (a layer laid wherever the slope
  and height are in a range). In Landscape mode, [ and ] size the brush and
  Ctrl+[ and Ctrl+] set its strength; the mode follows the Landscape tab,
  and the transform tools' keys are off, as in Unreal.
  Landscape splines, as Unreal's: the Splines tool makes chains of control
  points over the ground (Ctrl+click adds one after the selected point or
  starts a spline, a drag moves one, Delete removes it), each with a width
  and a side falloff, the curve passing smoothly through them; Apply
  Splines to Landscape raises and/or lowers the ground to them and paints
  their layer along them (roads, paths, river beds), foliage on the ground
  moving with it. Splines show in every view and are saved with the
  landscape. A spline can carry a mesh (Unreal's spline meshes): Road,
  Fence, Wall, River or any static mesh, repeated a whole number of times
  along it and bent to the curve, fitted to its width or scaled, raised by
  an offset; it is drawn with the landscape and kept through sculpting.
  Splines join at junctions: Ctrl+click on another point continues the
  selected spline to it (or, from a middle point, links the two with a new
  spline; with nothing selected, starts a branch there), and the joined
  points move together, drawn as larger squares, until Break Junction.
  The selected point shows tangent handles: dragging one sets the curve's
  direction and pull there by hand, and Auto Tangent goes back to the
  smooth automatic one.
  Big landscapes stay quick: they are drawn in components of 32 x 32
  quads, those out of view culled and the rest coarser with distance (or,
  in 2D views, with the zoom), their seams stitched so no cracks open.
  Picking and the brush trace the heightfield itself, and the full
  triangle mesh is only built when something needs it, so a sculpt step
  on 512 x 512 quads takes milliseconds. The views see 65536 units deep.
  Textured layers are blended per pixel on the GPU (up to 8 layers): the
  weights go up as a texture and each layer's texture repeats at full
  resolution with mipmaps, so ground stays sharp up close; a stroke uploads
  only the weights it changed. Landscapes with more layers fall back to a
  baked image. The Vulkan shaders are built with glslc
  (`BuildMapShaders.cmd`; the Android NDK's or the Vulkan SDK's).
- Foliage, as in Unreal: the Foliage mode (Shift+3) paints foliage types
  (Grass, Bush, Tree, Pine and Rock meshes, or any static mesh) onto
  whatever is under the brush, each type to its density, keeping its
  spacing, on the slopes and heights it allows, scaled and turned at random,
  upright or along the ground; Shift or Erase takes them away, Single places
  one. A stroke is one undo step. The instances live in the map's Foliage
  object (saved in `.qle`), are culled out of view and past their type's
  cull distance, and move with a landscape as it is sculpted. The GPU draws
  them instanced: each mesh's triangles are uploaded once and each visible
  copy is one matrix a frame (28,000 bushes and grass tufts in view: 3 ms a
  frame on an RTX 4090, against 53 ms as CPU-made triangles).
- Prefab brushes and entities from the built-in Genesis game
  (`Engine/Content/Editor`); entity keys are edited with the types the
  entity set gives them. Selections can be saved as prefabs.
- Maps are saved as `.qle`, and `.map` files are imported and exported
  (brushes and entities only).
  Maps made for the Qoole-era games open as Genesis maps.

Pass a map on the command line to open it. `-vulkan` and `-d3d12` choose the
backend over the preference, `-appscale=1.25` adds an application scale, `-resetlayout` starts
from the default layout and key bindings, `-nodebug` / `-debug` turn the
D3D12 debug layer or Vulkan validation off or on (on by default in Debug
builds). Run `GenesisEditorTest.exe -selftest=<dir>` for the separate scripted
check that saves screenshots and a log. Settings and saved prefabs are kept in
`%LOCALAPPDATA%\Genesis\Editor`.

The MFC editor, its `GenesisDraw*` renderer plugins, `GenesisEditor.pak` and
the Qoole-era games (Quake, Quake 2, Hexen 2, Half-Life, Sin, Heretic 2)
were removed in milestone 6 of the Genesis UI plan.

## Genesis Engine modules

The code lives in static-library modules named after their Unreal
counterparts: `Core`, `ApplicationCore`, `RHI`, `SlateCore`, `Slate` and
`SlateRHIRenderer` in `Engine/Source/Runtime`, `MapCore`, `MapEditor`, `LandscapeEditor`, `FoliageEditor` and
`LandscapeEditorUtilities` in `Engine/Source/Editor`. Runtime landscape and
foliage data have independent `Landscape` and `Foliage` modules. See
[FeatureModules.md](Engine/Documentation/Source/FeatureModules.md) for ownership
and dependencies. `MapCore` holds Qoole's map model, geometry and game
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
  and the scene shaders in `Engine/Source/Runtime/Renderer/Private/Shaders`. DXIL is compiled
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

Native mesh material assets now support Opaque, Masked and Translucent modes.
Open a material in the Content Browser to edit base-color alpha, blend mode and
mask cutoff, then save and apply it to selected native mesh actors. Texture
alpha participates in masking and translucency. Existing materials retain opaque
rendering. Transparent triangles render after opaque geometry and foliage; their
centroid sorting has the usual limitations for intersecting surfaces.

Native static meshes support up to 64 named material slots. OBJ `usemtl` groups
become slots; assign materials per slot in Details, or use **Apply to Slot** in
the material editor. **Use Mesh Default** clears an actor override. Assignments
support undo, native worlds and prefabs. Mesh schema v2 and world/prefab schema
v3 keep older assets readable. MTL material definitions are not imported.

Double-click a native mesh tile to edit its material defaults in a live preview.
The mesh editor supports Lit/Unlit/wireframe display and staged OBJ reimport.
Save retains the asset UUID and refreshes placed instances while preserving
transforms and material overrides. Reimport keeps named slot indices stable;
removed slots are retained. Unsaved drafts are discarded on close. Drag a mesh
into the viewport or use **Place in Level** to place it.

Native texture assets now have an editor with image/channel previews, sRGB
controls, zoom/pan and staged image reimport. Save preserves the texture UUID
and refreshes level texture dependencies. Open a texture and choose **Use for
Selected Landscape Layer** to assign it to terrain. Preview is limited to
2048 pixels per side; original image resolution is retained in saved assets.

`Runtime/Renderer` owns native mesh/material drawing, terrain visibility/LOD and
clipped triangle preparation, landscape layer textures/baked fallback, and foliage
visibility/instancing. Shader sources and GPU resources live in that runtime module.
The editor uses thin adapters while its selection/undo model remains in MapCore.

Native terrain/foliage codecs and `FWorldSceneSnapshot` live in Runtime/Engine.
`FWorldSceneDrawList` renders supported native world components independently of
the legacy object tree. `RuntimeRenderCheck.exe -vulkan World.gmap Project/Content
Capture.bmp` loads a saved world and checks offscreen readback without editor/UI
modules; the capture argument is optional. Unsupported legacy components are
reported explicitly. See Engine/Documentation/Source/Renderer.md for the boundary
and remaining editor migration.


First-party C++ formatting is configured in `.clang-format`. The code audit and
remaining feature gaps are recorded in `Engine/Documentation/Source/CodeAudit.md`.
`-localizationcatalog=<path>` loads a UTF-8 JSON object mapping source display
strings to translations before the editor UI is constructed.
