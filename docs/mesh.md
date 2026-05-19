# mesh

The mesh module turns a `bsp_model` into a collection of GPU-ready
raylib `Mesh` objects, grouped by diffuse texture, with dual UV sets
(diffuse + lightmap atlas).

## Files

- `src/mesh.h` — `mesh_vertex`, `mesh_surface`, `mesh` structs and the
  `mesh_from_bsp` / `mesh_free` API.
- `src/mesh.c` — implementation.

## Public API

```c
typedef struct {
    float x, y, z;
    float u, v;     // diffuse texcoord
    float lu, lv;   // lightmap atlas texcoord (normalised 0..1)
} mesh_vertex;

typedef struct {
    mesh_vertex *vertices;      // CPU-side scratch (kept around)
    uint32_t     vertex_count;
    uint32_t     texture_id;    // diffuse GL texture id
    Mesh         rl_mesh;       // raylib GPU mesh
    int          uploaded;

    float        alpha;         // 1.0 opaque, 0.33 TRANS33, 0.66 TRANS66
    Vector3      centroid;      // mean vertex position (raylib space), for transparency sort
} mesh_surface;

typedef struct {
    mesh_surface *surfaces;          // opaque surfaces only
    texture      *textures;
    uint32_t      surface_count;     // opaque count
    uint32_t      texture_count;

    Texture2D     lightmap_atlas;
    uint32_t      lightmap_id;
    int           has_lightmap_atlas;

    mesh_surface *trans_surfaces;    // transparent surfaces
    uint32_t      trans_surface_count;
} mesh;

mesh *mesh_from_bsp(const bsp_model *bsp);
void  mesh_free(mesh *m);
```

## Build steps

`mesh_from_bsp` is the only entry point. It executes the following
sequence:

1. **Build the lightmap atlas** by calling `lm_build(bsp)`. The atlas
   pixels are uploaded as a `Texture2D` (clamp wrap, bilinear filter,
   no mipmaps), and the resulting `Texture2D` is stored on `mesh`.
2. **First pass over faces** (tallying): for every face that isn't a
   utility surface (`clip`, `hint`, `sky1`, ...), load or look up the
   diffuse texture. If the face's `texinfo.flags` contains
   `SURF_TRANS33` or `SURF_TRANS66` it is routed to the transparent
   surface list; otherwise to the opaque list. The per-surface vertex
   count is accumulated as `(face.num_edges - 2) * 3`.
3. **Allocate per-surface vertex buffers** based on the two tallies.
4. **Second pass over faces** (vertex generation): for each face, the
   module re-classifies it as opaque or transparent, finds the
   corresponding surface by `(gl_id, alpha)`, and:
     - fans the face's edge ring into triangles,
     - computes the per-vertex diffuse `(u, v)` from `texinfo` divided
       by the diffuse texture dimensions,
     - computes the per-vertex lightmap `(lu, lv)` via
       `compute_lightmap_uv`,
     - applies the BSP-to-raylib coordinate swap `(x, y, z) -> (x, z, -y)`,
     - writes three vertices per triangle into the surface's CPU array
       (winding is reversed compared to BSP order, see below).
5. **Compute centroids** for each transparent surface.
6. **Upload both lists** with `upload_surface_mesh`, which allocates
   raylib's `Mesh` arrays via `MemAlloc`, fills `vertices`, `texcoords`,
   and `texcoords2`, and calls `UploadMesh`.
7. **Free the atlas** with `lm_free`. The CPU lightmap pixels are no
   longer needed once the GPU texture exists.

## Surface grouping

A "surface" in this module is a batch of triangles that share a single
diffuse texture id **and** alpha value. Faces that reference the same
diffuse texture and have the same transparency flag end up in the same
`mesh_surface` regardless of where they are spatially. This minimises
texture binds at draw time at the cost of losing all spatial coherency
(which the engine doesn't use anyway, since there's no culling).

For opaque faces the grouping key is `(gl_texture_id, ALPHA_OPAQUE)`.
For transparent faces the grouping key is `(gl_texture_id, alpha)`,
where `alpha` is `ALPHA_TRANS33` (0.33) or `ALPHA_TRANS66` (0.66).
This means `SURF_TRANS33` and `SURF_TRANS66` faces with the same
texture never share a transparent surface — they need different
`alpha` uniforms at draw time.

## Diffuse UV computation

```c
u = (p . texinfo.u_axis + texinfo.u_offset) / texture_width
v = (p . texinfo.v_axis + texinfo.v_offset) / texture_height
```

The dot product is taken in BSP space against the texinfo axes. The
division normalises into `[0..1]`-style UV space (with values outside
that range relying on `TEXTURE_WRAP_REPEAT`).

## Lightmap UV computation

`compute_lightmap_uv(texinfo, info, p, atlas_w, atlas_h, *lu, *lv)`
returns atlas-normalised UVs for a vertex. The math:

```
s = p . u_axis + u_offset
t = p . v_axis + v_offset
lx = (s - info.s_min) / 16
ly = (t - info.t_min) / 16
atlas_px = info.atlas_x + lx + 0.5     // +0.5 = GL_LINEAR texel centre
atlas_py = info.atlas_y + ly + 0.5
lu = atlas_px / atlas_w
lv = atlas_py / atlas_h
```

For unlit faces (`info.has_lightmap == 0`), the function returns the
UV of the centre of the 1x1 white tile at `(0, 0)`:
`(0.5/atlas_w, 0.5/atlas_h)`.

Lightmap UVs are computed from the **pre-swap** BSP-space position
`p`, because `texinfo.u_axis` and `texinfo.v_axis` are defined in BSP
space. Computing them from the post-swap raylib-space vertex would
produce nonsense.

## Winding

The BSP encodes face vertices in a specific winding order. The mesh
module triangulates as a fan around vertex 0 (`p0, p1, p2`,
`p0, p2, p3`, ...) but emits each triangle in reverse order
(`p2, p1, p0`). Combined with the `(x, z, -y)` coordinate swap (which
flips handedness along the y axis), this yields counter-clockwise
front faces in raylib's left-handed-feeling but actually right-handed
coordinate system.

## Coordinate swap

```
raylib_x =  bsp_x
raylib_y =  bsp_z
raylib_z = -bsp_y
```

This puts BSP's `+z` (up) along raylib's `+y`, and BSP's `+y`
(forward, into the screen for a default-oriented player) along
raylib's `-z` (also into the screen for raylib's default camera).
`vec3_parse` in `main.c` applies the same swap to entity `origin`
strings.

## Fallback texture

If a face references a texture that fails to load (missing file, path
too long, allocation failure), `ensure_fallback_texture` synthesises a
32x32 magenta/black checkerboard, registers it under the synthetic
path `__fallback__`, and returns its index. All subsequent missing
textures reuse the same fallback texture.

## Cleanup

`mesh_free` iterates the opaque and transparent surface lists (via the
`free_surface_list` helper) and:

1. Calls `UnloadMesh(s->rl_mesh)` for each uploaded surface to release
   GPU buffers.
2. Frees the CPU `vertices` arrays.
3. Walks `m->textures` and calls `UnloadTexture` plus `free(path)` for
   each.
4. Calls `UnloadTexture` on the lightmap atlas if it was created.
5. Frees the top-level `mesh` struct.

The order matters: GPU resources are released before the CPU-side
arrays that referenced them, so there are no dangling-id concerns.

## Transparent surfaces

Faces whose `texinfo.flags` contain `SURF_TRANS33` or `SURF_TRANS66`
are routed to `m->trans_surfaces` instead of `m->surfaces`. The alpha
value (0.33 or 0.66) is stored on `mesh_surface.alpha` and becomes
the `surfaceAlpha` shader uniform at draw time. Transparent surfaces
still receive a lightmap and participate in the atlas the same way as
opaque surfaces.

After vertex generation, the module computes each transparent surface's
`centroid` (arithmetic mean of all vertex positions in raylib space).
The render module uses this centroid to sort transparent surfaces
back-to-front relative to the camera every frame.

The `free_surface_list` helper is used for both the opaque and
transparent lists in `mesh_free`.
