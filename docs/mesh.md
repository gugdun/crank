# mesh

The mesh module turns a `bsp_model` into a single GPU vertex buffer plus
a collection of per-bucket index buffers, with face-level metadata used
by the visibility system to decide which triangles to actually draw.

## Files

- `src/mesh.h` — `mesh_vertex`, `mesh_face_info`, `mesh_surface`, `mesh`
  structs and the `mesh_from_bsp` / `mesh_free` API.
- `src/mesh.c` — implementation.

## Public API

```c
typedef struct {
    float x, y, z;
    float u, v;     // diffuse texcoord
    float lu, lv;   // lightmap atlas texcoord (normalised 0..1)
} mesh_vertex;

typedef struct {
    uint32_t surface_index;   // index into mesh.surfaces or mesh.trans_surfaces
    uint32_t first_index;     // offset into surface->all_indices
    uint32_t index_count;     // (num_face_vertices - 2) * 3; 0 if face was skipped
    Vector3  bbox_min;        // AABB in raylib (y-up) space, for frustum culling
    Vector3  bbox_max;
    Vector3  centroid;        // mean vertex position (raylib space)
    uint8_t  is_trans;        // 1 if this face's bucket is in trans_surfaces
    uint8_t  pad[3];
} mesh_face_info;

typedef struct {
    uint32_t  texture_id;
    float     alpha;          // 1.0 opaque, 0.33 TRANS33, 0.66 TRANS66
    Vector3   centroid;       // static all-faces centroid (fallback for sort)

    uint32_t *all_indices;
    uint32_t  all_index_count;

    uint32_t  ibo_id;         // GL element-array buffer (dynamic, refreshed per frame)
    uint32_t *frame_indices;
    uint32_t  frame_index_count;
    uint32_t  frame_index_capacity;
    Vector3   frame_centroid;
} mesh_surface;

typedef struct {
    mesh_vertex    *vertices;      // CPU-side copy of the shared vertex buffer
    uint32_t        vertex_count;
    Mesh            rl_mesh;       // raylib upload (positions, texcoords, texcoords2)
    int             uploaded;

    mesh_surface   *surfaces;
    uint32_t        surface_count;
    mesh_surface   *trans_surfaces;
    uint32_t        trans_surface_count;

    mesh_face_info *faces;
    uint32_t        face_count;

    texture        *textures;
    uint32_t        texture_count;

    Texture2D       lightmap_atlas;
    uint32_t        lightmap_id;
    int             has_lightmap_atlas;
} mesh;

mesh *mesh_from_bsp(const bsp_model *bsp);
void  mesh_free(mesh *m);
```

## Layout

All world vertices are packed into **one** array (`mesh.vertices`) and
uploaded as a single raylib `Mesh` (positions, texcoords, texcoords2;
no indices). Each `mesh_surface` is a draw bucket — a unique
`(diffuse_texture_id, alpha)` pair — that owns a static `all_indices`
array (every triangle that would be drawn if every face in the bucket
were visible) and a dynamic GL index buffer (`ibo_id`) whose contents
are rewritten every frame by the visibility system.

`mesh.faces` is a parallel array sized to `bsp->num_faces`. Each entry
records the bucket and index-range a single BSP face contributes, plus
its AABB and centroid (for frustum culling and back-to-front
transparency sorting). Faces that were skipped during build (utility
textures, degenerate geometry, missing texinfo) keep `index_count = 0`.

## Build steps

`mesh_from_bsp` runs once at map load:

1. **Build the lightmap atlas** by calling `lm_build(bsp)`. The atlas
   pixels are uploaded as a `Texture2D` (clamp wrap, bilinear filter,
   no mipmaps).
2. **Allocate `mesh.faces`** sized to `bsp->num_faces`, zero-initialised.
3. **Count total triangle-list vertices** by walking faces once. Each
   kept face contributes `(num_edges - 2) * 3` vertices (no vertex
   dedup; p0 is re-emitted for every triangle in the fan to keep
   lightmap UVs exact per-triangle).
4. **Allocate the shared `mesh_vertex` array** of that size.
5. **First bucket pass**: walk faces again, create the
   `(texture_id, alpha)` bucket for each kept face, and tally
   per-bucket index counts.
6. **Allocate per-bucket `all_indices`** arrays.
7. **Second pass**: emit vertices into the shared buffer, append
   indices to each face's bucket, and populate that face's
   `mesh_face_info` (surface_index, first_index, index_count,
   bbox_min/max, centroid, is_trans).
8. **Compute static per-surface centroids** for transparent surfaces
   (used as the back-to-front sort fallback when nothing is visible
   this frame).
9. **Upload the shared mesh** with `UploadMesh` once.
10. **Allocate per-surface dynamic IBOs** with `rlLoadVertexBufferElement`,
    sized to `all_index_count`, plus a CPU-side `frame_indices` staging
    buffer of the same capacity.

The atlas's CPU pixels are freed with `lm_free` after upload; the
atlas's `Texture2D` lives on `mesh` for the engine's lifetime.

## Surface grouping

A "surface" is a batch of triangles that share a diffuse texture id and
an alpha value. Faces with `SURF_TRANS33` (0.33) or `SURF_TRANS66`
(0.66) flags are routed to `m->trans_surfaces` and grouped by their
alpha; opaque faces share a single alpha (`1.0`) and live in
`m->surfaces`. This minimises texture binds at draw time while keeping
the transparent draws sortable as a list.

`SURF_TRANS33` and `SURF_TRANS66` faces with the same texture never
share a bucket: they need different `surfaceAlpha` uniforms.

## Diffuse UV computation

```c
u = (p . texinfo.u_axis + texinfo.u_offset) / texture_width
v = (p . texinfo.v_axis + texinfo.v_offset) / texture_height
```

Dot product in BSP space against the texinfo axes; division normalises
into roughly `[0..1]` UV space (values outside are handled by raylib's
default `TEXTURE_WRAP_REPEAT`).

## Lightmap UV computation

`compute_lightmap_uv(texinfo, info, p, atlas_w, atlas_h, *lu, *lv)`:

```
s = p . u_axis + u_offset
t = p . v_axis + v_offset
lx = (s - info.s_min) / 16
ly = (t - info.t_min) / 16
atlas_px = info.atlas_x + lx + 0.5
atlas_py = info.atlas_y + ly + 0.5
lu = atlas_px / atlas_w
lv = atlas_py / atlas_h
```

For unlit faces (`info.has_lightmap == 0`), returns the centre of the
1x1 white tile at atlas `(0, 0)`.

Lightmap UVs are computed from the **pre-swap** BSP-space position,
because the texinfo axes are defined in BSP space.

## Winding

The BSP encodes face vertices in a specific winding order. The mesh
module triangulates as a fan around vertex 0 and emits each triangle in
reverse order (`p2, p1, p0`). Combined with the `(x, z, -y)` coordinate
swap, this yields counter-clockwise front faces under raylib's
projection.

## Coordinate swap

```
raylib_x =  bsp_x
raylib_y =  bsp_z
raylib_z = -bsp_y
```

Vertex positions, AABBs, and centroids stored in `mesh.faces` are all
in **raylib** space. The visibility module performs the inverse swap on
the camera position before calling `bsp_find_leaf`.

## Per-frame interaction with vis

The mesh module never touches the IBOs after build. Per-frame index
buffer regeneration is the job of `vis_update` in `vis.c`:

1. `vis_update` resets every surface's `frame_index_count` to 0.
2. For each visible face that passes frustum culling, it appends
   `mesh.faces[f].index_count` indices from
   `surface->all_indices + first_index` into
   `surface->frame_indices`.
3. It calls `rlUpdateVertexBufferElements` to upload the staging
   buffer to the GPU.

The render module then issues one draw call per surface with the
appropriate texture bound.

## Fallback texture

If a face references a texture that fails to load (missing file, path
too long, allocation failure), `ensure_fallback_texture` synthesises a
32x32 magenta/black checkerboard, registers it under the synthetic
path `__fallback__`, and returns its index. Subsequent missing
textures reuse the same fallback.

## Cleanup

`mesh_free` releases resources in this order:

1. Per-surface: unload the IBO with `rlUnloadVertexBuffer`, free
   `all_indices` and `frame_indices`.
2. Free `mesh.faces`.
3. Free `mesh.vertices`.
4. `UnloadMesh(mesh.rl_mesh)` — releases the shared VBOs.
5. Iterate `mesh.textures`, `UnloadTexture` + `free(path)` each.
6. `UnloadTexture` on the lightmap atlas.
7. `free(mesh)`.

The order matters: GPU buffers are released before the CPU arrays
that referenced them.
