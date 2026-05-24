# lightmap

The lightmap module turns the raw Quake II `BSP_LIGHTMAPS` byte buffer
into a single GPU-friendly atlas texture, plus a per-face record of
where each face's lightmap landed in the atlas.

## Public API

```c
typedef struct {
    float s_min, t_min;   // texture-space mins (snapped to 16) for the face
    int   lm_w, lm_h;     // lightmap size in luxels (1 if has_lightmap == 0)
    int   atlas_x;        // position of the face's lightmap in the atlas
    int   atlas_y;
    int   has_lightmap;   // 0 -> sample the 1x1 white tile at (0,0)
} lm_face_info;

typedef struct {
    uint8_t      *pixels;       // RGB atlas pixels (CPU-side)
    int           width;
    int           height;
    lm_face_info *faces;        // one entry per bsp face, indexed by face index
    uint32_t      face_count;
} lm_atlas;

lm_atlas *lm_build(const bsp_model *bsp);
void      lm_free(lm_atlas *atlas);
```

## Algorithm

`lm_build` runs in three logical phases.

### 1. Per-face extent computation

For every face the module computes the face's surface extents in
texture-space:

```
s = dot(v, texinfo.u_axis) + texinfo.u_offset
t = dot(v, texinfo.v_axis) + texinfo.v_offset
```

over each vertex of the face. The min/max are snapped:

```
s_min = floor(min(s) / 16) * 16
s_max =  ceil(max(s) / 16) * 16
```

and the face's lightmap dimensions follow the standard Quake II rule:

```
lm_w = ((s_max - s_min) / 16) + 1
lm_h = ((t_max - t_min) / 16) + 1
```

with `lm_w` and `lm_h` clamped to a hard maximum of 256.

A face is considered **unlit** (and gets `has_lightmap = 0`, `lm_w = 1`,
`lm_h = 1`) if any of the following hold:

- `face.num_edges < 3` or `face.texture_info >= num_texinfo`
- `texinfo.flags` contains `SURF_SKY`, `SURF_WARP`, or `SURF_NODRAW`
- `face.lightmap_offset == 0xFFFFFFFF` (the canonical "no lightmap" value)
- `bsp->lightmaps == NULL` or the lump is empty
- `face.lightmap_offset + lm_w * lm_h * 3` would read past the end of
  the lightmap lump

The Quake II surface-flag constants are defined as macros in the
lightmap header (`SURF_SKY`, `SURF_WARP`, `SURF_NODRAW`, etc.).

### 2. Shelf packing

Faces are sorted by descending `lm_h` and inserted into shelves:

```
+---------------------------+
| white | small  | small    |   <-- shelf 0 (height = max of its tiles)
|       |  tile  |  tile    |
+---------------------------+
|  taller tile  | taller    |   <-- shelf 1
|               |  tile     |
+---------------------------+
|       even taller tile    |   <-- shelf 2
+---------------------------+
```

The first shelf is seeded with a reserved **1x1 white tile** at
position `(0, 0)`. Faces marked unlit (in phase 1) keep their default
`atlas_x = 0, atlas_y = 0`, so they sample the white tile.

Each placed tile gets **1 luxel of padding** on each side. Padding
prevents `GL_LINEAR` filtering from bleeding adjacent faces' lightmaps
into the sampled value. The padding pixels are filled by replicating
the nearest border luxel of the tile.

The atlas width is chosen as `max(1024, next_pow2(max_tile_width))` and
is capped at 4096. If a face's tile cannot fit (would push the atlas
past 4096 luxels of height), it is downgraded to the white tile and a
warning is printed.

### 3. Pixel blit

After packing, the atlas pixel buffer is allocated and filled with
`0xFF` (white). For each lit face, `lm_w * lm_h * 3` bytes are copied
from `bsp->lightmaps + face.lightmap_offset` to the face's atlas
location, row by row. The 1-luxel padding rows and columns are filled
with replicated border luxels.

The atlas height is rounded to the next power of two for friendliness
with older GL drivers.

## Output ownership

`lm_build` returns a heap-allocated `lm_atlas` containing:

- A heap `pixels` buffer (RGB, 3 bytes per luxel).
- A heap `faces` array sized exactly to `bsp->num_faces`.

The mesh module reads `faces` to compute per-vertex lightmap UVs and
uploads `pixels` as a `Texture2D`, then frees the atlas with `lm_free`.
The atlas does not own the texture; the `mesh` module does.

## Performance characteristics

- `lm_build` runs once at map load. There is no streaming or runtime
  updating.
- Memory footprint is `atlas_w * atlas_h * 3` bytes for the CPU
  scratch buffer (freed immediately after upload) plus the same amount
  on the GPU. For a typical Quake II map, this is on the order of a
  few hundred kilobytes to a couple of megabytes.

## Failure modes

- `bsp == NULL`: returns `NULL`.
- `calloc`/`malloc` failures: every allocation path frees its own work
  and returns `NULL`. The caller (`mesh_from_bsp`) treats `NULL` as a
  fatal load error.
- Tile-too-big or atlas-overflow: the offending face is silently
  downgraded to the white tile and the build continues. A warning is
  printed.

## Constants you may want to tune

| Constant         | Default | Meaning                                |
| ---------------- | ------- | -------------------------------------- |
| `MAX_LM_DIM`     | 256     | Max luxels per side per face           |
| `ATLAS_PADDING`  | 1       | Luxels of replicated padding per tile  |

The atlas size cap (4096) is hardcoded inside `lm_build` and should
match the GL implementation's `GL_MAX_TEXTURE_SIZE` floor.
