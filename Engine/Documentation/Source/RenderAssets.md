# Native mesh, texture and material assets

Genesis now has runtime codecs and loaders for three `.asset` types. All use
the existing package UUIDs, `.gexp` metadata, `.gbulk` payloads and asset registry.
No codec depends on MapCore, Slate, image decoders or a graphics API.

| Schema | Stored data |
| --- | --- |
| `Genesis.StaticMesh` v1 | Positions, indexed triangles, corner normals and UVs, wire edges, optional RGBA vertex colors, optional material reference |
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
2. Double-click the mesh asset, or drag it into a viewport, to place it.
3. **Import** a PNG, BMP, JPEG or TIFF to create a texture `.asset`. Pixels are
   embedded, so the original image path is no longer needed to load it.
4. Choose **Add → Material**. If a native texture tile is selected, the material
   starts with that texture. Double-click the material to edit its base color,
   surface values, base color texture and normal map. **Save and Apply to Selected Mesh Assets**
   saves the material and assigns it to selected native mesh actors. This
   assignment is undoable; saving the material file itself is not map undo.
5. To paint with a native texture, select a landscape and paint layer, then
   double-click the texture tile. Both the GPU layer blend and baked landscape
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
image-based lighting, reflection captures or multiple material slots. Material
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
convex polygon fans and negative indices. It does not import MTL materials,
groups as separate actors, animation, or coordinate-system/unit conversion.
Triangulate complex/nonplanar polygons in the source tool before importing.
Image decoding stays in Editor through WIC; runtime loading needs only the
embedded RGBA8 bytes. Image dimensions are 1–8192 on each axis.

Mesh bulk layout is little-endian: float32 positions (12 bytes per vertex),
triangle records (3 uint32 indices, 9 float32 normal coordinates, 6 float32 UV
coordinates: 72 bytes), uint32 edge pairs, then optional packed RGBA uint32
vertex colors. Counts in `.gexp` determine the exact byte length. Limits are
one million vertices, two million triangles and three million wire edges.
The codecs reject corrupt indices, invalid normals, nonfinite values, invalid
dimensions/counts, truncated data and unsupported schema versions.

Native world/prefab schema remains version 2. A `Genesis.StaticMesh` scene
component's `Properties.Data` is either an existing built-in name or an object
with `Asset` and `MaterialOverride` references. Its `Assets` list contains the
non-null references in that order. Native landscape texture references live in
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
one-sided. Multiple mesh material slots are still pending.
