# Native mesh, texture and material assets

Genesis now has runtime codecs and loaders for three `.asset` types. All use
the existing package UUIDs, `.gexp` metadata, `.gbulk` payloads and asset registry.
No codec depends on MapCore, Slate, image decoders or a graphics API.

| Schema | Stored data |
| --- | --- |
| `Genesis.StaticMesh` v2 | Positions, indexed triangles, corner normals and UVs, wire edges, optional RGBA vertex colors, up to 64 material slots with names and per-triangle indices |
| `Genesis.Texture2D` v1 | Width, height, RGBA8 pixels, sRGB metadata |
| `Genesis.Material` v3 | RGBA base color, roughness, metallic, blend mode, mask cutoff, optional base color and tangent-space normal texture references |

The runtime API is `Runtime/Engine/Public/Assets/RenderAssets.h`.
`FRenderAssetCodec` encodes/decodes package payloads. `FRenderAssetLoader` resolves
references through an explicitly supplied registry. Loading a mesh also loads
its material and texture dependencies; a material override can be supplied.
Failures leave output values unchanged. Missing or stale UUIDs do not fall back
to a different package at the same path. There is no GPU allocation in this API.

## Editor workflow

Open a `.gproject` so the Content Browser has a `/Game` mount.

1. **Import** an OBJ file to create a static mesh `.asset`, or select one mesh
   actor and choose **Add → Static Mesh from Selection**. The latter creates a
   reusable copy of its local geometry, without baking the actor transform.
2. Double-click the mesh asset to open its editor. Drag it into a level viewport,
   or choose **Place in Level** in the mesh editor, to place it.
3. **Import** a PNG, BMP, JPEG or TIFF to create a texture `.asset`. Pixels are
   embedded, so the original image path is no longer needed to load it.
4. Choose **Add → Material**. If a native texture tile is selected, the material
   starts with that texture. Double-click the material to edit its base color,
   surface values, base color texture and normal map. **Save and Apply to Selected Mesh Assets**
   saves the material and assigns it to selected native mesh actors. This
   assignment is undoable; saving the material file itself is not map undo.
5. To paint with a native texture, select a landscape and paint layer, then
   open the texture editor and choose **Use for Selected Landscape Layer**.
   Both the GPU layer blend and baked landscape
   paths use its embedded pixels.
6. Save the level as `.gmap` or the selection as a prefab `.asset`. Mesh source
   and material override references persist across save/load and prefab placement.

The browser generates free filenames rather than overwriting existing assets.
Importing an existing native package makes a new package UUID and retains its
dependency references. It does not recursively copy dependencies: bring those
packages into the project too. Moving a complete project's Content directory
retains identities and references; manifests still need their referenced sidecars.

## Current rendering scope

Native meshes render, pick, transform and participate in selection like existing
mesh actors. DirectX 12 and Vulkan now sample their UV textures with perspective
interpolation and GPU clipping. Lit mode uses per-pixel GGX/Schlick metallic-
roughness shading under the editor's directional light and hemisphere fill.
Unlit displays base color (texture × material factor × vertex color), without
light or normal-map effects. The authoring factor and vertex colors are sRGB.

Color textures marked sRGB use hardware sRGB sampling, so decoding happens before
bilinear/trilinear filtering. Mip generation also averages in linear light.
Linear textures stay linear. Normal maps always use linear RGB data, independent
of the texture's sRGB flag. Their mip averages are renormalized. Tangent frames
come from position/UV derivatives, preserving mirrored UV handedness; degenerate
UVs retain the geometric normal. Green points in the direction of increasing V.
Object normals use the full inverse transpose, including parent shear and mirrors.

Materials support Opaque, Masked and Translucent blend modes. Alpha is the product
of the base color alpha, base texture alpha and vertex alpha. Opaque ignores it;
Masked discards values below Opacity Mask Cutoff and writes depth for surviving
pixels. Translucent uses alpha blending after opaque geometry and foliage, with
depth testing but no depth writes. Triangles sort back to front by view-space
centroid; intersecting surfaces can still show sorting artifacts. Blending currently
happens in the display-encoded viewport target. This is a preview without
scene-light integration, shadows,
image-based lighting or reflection captures. Material
alpha controls the selected blend mode. Roughness has a
small shader minimum for stable specular shading. Landscape native textures
continue through their existing rendering paths; landscape normal-map materials
are a separate future feature.

Newly created built-in basic shapes have UVs: box/planar mapping, spherical
mapping and cylindrical sides/caps. Previously saved native shape assets with
constant UVs keep that data; recreate those assets from the updated basic shapes,
or reimport a UV-authored OBJ. The renderer does not invent UVs for imported meshes.

The editor shares loaded mesh assets for repeated references within a registry
revision. Refresh invalidates future mesh loads and native landscape texture
caches. Existing mesh actors keep their loaded snapshot until reassigned or the
map is reopened; this is not automatic live reimport of every placed actor.

The material editor provides basic property editing, without a dedicated mesh
preview viewport or shader graph. Transparency/masking, texture compression/cooking,
multiple material slots, skeletal meshes and asset thumbnails remain future work.

Material v1 packages still load with no normal map; saving writes material v2.
Mesh, texture, world and prefab schema versions are unchanged. Older builds that
only understand material v1 cannot load the new material payloads.

## Import and format constraints

The geometry-only OBJ importer reads positions, normals, UVs, triangle faces,
convex polygon fans and negative indices. It preserves `usemtl` names and face groups as material slots, in first-use order.
Assign materials in the actor Details panel. It does not import MTL material definitions,
groups as separate actors, animation, or coordinate-system/unit conversion.
Triangulate complex/nonplanar polygons in the source tool before importing.
Image decoding stays in Editor through WIC; runtime loading needs only the
embedded RGBA8 bytes. Image dimensions are 1–8192 on each axis.

Mesh bulk layout is little-endian: float32 positions (12 bytes per vertex),
triangle records (3 uint32 indices, 9 float32 normal coordinates, 6 float32 UV
coordinates, uint32 material slot: 76 bytes; version 1 records are 72 bytes), uint32 edge pairs, then optional packed RGBA uint32
vertex colors. Counts in `.gexp` determine the exact byte length. Limits are
one million vertices, two million triangles and three million wire edges.
The codecs reject corrupt indices, invalid normals, nonfinite values, invalid
dimensions/counts, truncated data and unsupported schema versions.

Native world/prefab schema is version 3; versions 1 and 2 remain readable. A `Genesis.StaticMesh` scene
component's `Properties.Data` is either an existing built-in name or an object
with `Asset`, slot-zero `MaterialOverride`, and `AdditionalOverrides` for slots
1 onward (null entries keep slot alignment). Its `Assets` list contains source,
then all non-null overrides in slot order. Native landscape texture references live in
the transitional layer texture string as an `asset:` JSON reference; the
landscape component's `Assets` list mirrors these references in layer order.
The adapter verifies those lists against the payload. World package dependencies
record unique logical paths. Legacy QLE/MAP saving refuses native asset references
instead of writing a file that silently loses them.

`RenderAssets` and `MapPackages` tests cover binary codecs, malformed payloads,
OBJ/image import, asset sharing, dependency resolution, stale identities,
material undo, native landscape texture refresh, map and prefab round trips.
`MeshMaterials` tests read back images on both graphics backends to check UV
placement, sRGB decoding/filtering, mirrored meshes, mixed legacy/native depth,
near-plane clipping, roughness, metallic and normal-map effects. They also check
normal transforms under shear and linear-light/normal-map mip generation.
The editor self-test builds and reopens a world using all three native types.
`RuntimeDataCheck.exe <World.gmap> <ContentDirectory>` validates its mesh/material/
texture dependencies with no editor libraries.

Material schema version 3 stores `BlendMode` (0 Opaque, 1 Masked, 2 Translucent)
and `OpacityMaskClip` (0�1). Versions 1 and 2 remain readable and default to
Opaque. The material editor exposes base-color alpha, blend mode and cutoff.
Masked/translucent rendering currently applies to native mesh materials;
landscape and foliage keep their existing rendering paths. Surfaces remain
one-sided. Slots independently select opaque, masked or translucent pipelines.

## Multiple material slots

Native meshes support up to 64 slots (0-63). The Details panel lists each slot,
including names from OBJ `usemtl` groups. Choose a registered material to create
an actor override; choose **Use Mesh Default** to clear that override. Other
slots stay unchanged. Assignments are undoable and survive `.gmap` and prefab
save/load. The material editor also has an **Apply to Slot** index. **Static Mesh
from Selection** bakes the selected actor's overrides into the new mesh defaults.

Mesh schema v2 stores `Materials` (slot zero followed by additional slots),
optional `MaterialSlotNames`, and a uint32 slot index after each triangle's UVs.
Version 1 meshes load with a single slot zero. Invalid slot counts, names and
triangle indices are rejected without replacing outputs. World/prefab schema v3
stores additional actor overrides; opening earlier versions marks the document
for upgrade while preserving existing scene IDs from v2.

## Mesh asset editor and geometry reimport

Double-click a native static mesh tile to open its editor. The independent
preview supports orbit/zoom, framing, Lit, Unlit and wireframe display. The
panel lists geometry counts and the mesh's material defaults. Choosing **None**
clears a default; actor overrides in the level still take precedence. Preview
and edits stay in a draft until **Save Mesh Asset**. Closing discards unsaved
draft edits. Saving an asset file is separate from map undo.

OBJ imports now record `ImportSource.File` in mesh exports as editor metadata,
relative to the asset directory where possible. Runtime loading does not read
that file. Existing assets without source metadata can use **Reimport from
OBJ...**. **Reimport Recorded Source** uses the last recorded path. Reimport is
staged in the draft; save explicitly to publish the new geometry/defaults.

Reimport matches named groups to existing slot indices. Removed slots remain
in the mesh so placed actors keep their overrides. New groups append slots;
unnamed existing slots can adopt incoming groups at the same index. Ambiguous
duplicate names or more than 64 retained slots reject the reimport. Vertex
positions, normals and UVs come from the OBJ; source units/axes remain unchanged.
MTL material definitions and automatic source watching are not provided by the
mesh editor. Texture reimport is available in the texture editor below.

Saving retains the package UUID and rejects a changed disk manifest, a moved
asset, or missing material/texture dependencies. Reopen an editor whose draft
conflicts with a newer save. A successful save refreshes matching level mesh
instances without changing actor/component identity, transforms or per-instance
material overrides. Instance refresh is undoable in the level; the asset-file
save itself is not. Referenced immutable asset snapshots in existing undo history
are retained until those edits are discarded.

## Texture asset editor

Double-click a native texture tile to open its image editor. It shows dimensions,
RGBA8 storage size, an sRGB setting, and RGBA/RGB/Red/Green/Blue/Alpha preview modes.
RGBA composites alpha over a checkerboard in linear light. RGB display respects
the sRGB setting; individual channels show stored byte values as grayscale.
The wheel zooms, middle drag pans, and **Fit Preview** recenters the image.
Preview images are limited to 2048 pixels on each side and use nearest source
samples when downscaled; saved texture data keeps its original resolution.

Use sRGB for color maps and disable it for linear data. Mesh normal-map sampling
always stays linear regardless of this flag. Native landscape color textures
marked linear are converted to the display-encoded pixels expected by the
existing landscape layer renderer, without changing the stored asset bytes.

Imports record a project-portable `ImportSource.File` relative to the asset
folder where possible. **Reimport Recorded Source** and **Reimport Image...** stage
new image pixels/dimensions while retaining the asset UUID and sRGB setting.
PNG/BMP/JPEG/TIFF decoding uses the existing image importer. **Save Texture Asset**
publishes the draft and refreshes native mesh texture dependencies and landscape
texture caches. Existing material/paint-layer references stay intact. Landscape
assignment remains available through **Use for Selected Landscape Layer**.

Older textures without source metadata can use **Reimport Image...**. Closing
an editor discards unsaved changes; changing preview channels never dirties an
asset. Saving rejects a stale manifest, moved package or changed identity. The
texture payload remains `Genesis.Texture2D` version 1; import-source information
is optional editor metadata and runtime loaders do not read the source file.
