# bsp

Loader for Quake II BSP files (version 38).

## Files

- `src/bsp.h` — public structs and the `bsp_load` / `bsp_free` API.
- `src/bsp.c` — implementation.

## Public API

```c
bsp_model  *bsp_load(const char *path);
void        bsp_free(bsp_model *bsp);
const char *bsp_entity_get(const bsp_entity *e, const char *key);

int     bsp_decompress_pvs(const bsp_model *bsp, int32_t cluster, uint8_t *out);
int32_t bsp_find_leaf(const bsp_model *bsp, point3f point);
```

`bsp_load(path)` opens `maps/<path>.bsp` and returns a heap-allocated
`bsp_model` populated with the lumps that the engine consumes. On any
parse error the function frees what it has allocated, closes the file,
and returns `NULL`.

`bsp_entity_get` is a linear-search helper that returns the value string
for a given key on an entity, or `NULL` if the key is absent.

`bsp_decompress_pvs(bsp, cluster, out)` decompresses the run-length-
encoded potentially-visible-set bit vector for `cluster` into `out`.
`out` must point to at least `(bsp->num_clusters + 7) / 8` bytes.
Returns `1` on success, `0` if there is no visibility data or `cluster`
is out of range.

`bsp_find_leaf(bsp, point)` walks the BSP tree from the root and
returns the index of the leaf that contains `point` (in BSP space), or
`-1` if the tree is empty or malformed. The visibility system uses this
every frame to look up the camera's cluster.

## File format

The file starts with a `bsp_header`:

```c
typedef struct {
    uint32_t magic;     // 'IBSP'  (0x50534249)
    uint32_t version;   // 38
    bsp_lump lump[19];  // {offset, length}
} __attribute__((packed)) bsp_header;
```

Lump indices the engine uses are defined as macros:

| Index | Macro              | Type read by               |
| ----- | ------------------ | -------------------------- |
| 0     | `BSP_ENTITIES`     | `bsp_read_entities`        |
| 1     | `BSP_PLANES`       | `bsp_read_planes`          |
| 2     | `BSP_VERTICES`     | `bsp_read_vertices`        |
| 3     | `BSP_VISIBILITY`   | `bsp_read_visibility`      |
| 4     | `BSP_NODES`        | `bsp_read_nodes`           |
| 5     | `BSP_TEXTURES`     | `bsp_read_texinfo`         |
| 6     | `BSP_FACES`        | `bsp_read_faces`           |
| 7     | `BSP_LIGHTMAPS`    | `bsp_read_lightmaps`       |
| 8     | `BSP_LEAVES`       | `bsp_read_leaves`          |
| 9     | `BSP_LEAF_FACES`   | `bsp_read_leaf_faces`      |
| 10    | `BSP_LEAF_BRUSHES` | `bsp_read_leaf_brushes`    |
| 11    | `BSP_EDGES`        | `bsp_read_edges`           |
| 12    | `BSP_FACE_EDGES`   | `bsp_read_face_edges`      |
| 13    | `BSP_MODELS`       | `bsp_read_models`          |
| 14    | `BSP_BRUSHES`      | `bsp_read_brushes`         |
| 15    | `BSP_BRUSH_SIDES`  | `bsp_read_brush_sides`     |

Other lumps (`BSP_AREAS`, `BSP_AREA_PORTALS`, `BSP_POP`) have their
offsets and lengths recorded in the header but are not currently
consumed.

The brush lumps are consumed by the `phys` module to build a static
collision world; see [phys.md](phys.md). They are not used by the
renderer.

## In-memory model

```c
typedef struct {
    bsp_header header;

    point3f        *vertices;       // BSP_VERTICES
    bsp_edge       *edges;          // BSP_EDGES
    int32_t        *face_edges;     // BSP_FACE_EDGES (signed)
    bsp_face       *faces;          // BSP_FACES
    bsp_texinfo    *texinfo;        // BSP_TEXTURES
    bsp_entity     *entities;       // BSP_ENTITIES (parsed key/value form)
    uint8_t        *lightmaps;      // BSP_LIGHTMAPS (raw RGB bytes)

    bsp_plane      *planes;         // BSP_PLANES (used by tree walk + PVS + collision)
    bsp_node       *nodes;          // BSP_NODES
    bsp_leaf       *leaves;         // BSP_LEAVES
    uint16_t       *leaf_faces;     // BSP_LEAF_FACES
    bsp_model_lump *models;         // BSP_MODELS (inline brush models; index 0 = worldspawn)
    uint8_t        *visibility;     // BSP_VISIBILITY (raw lump, RLE-encoded)

    bsp_brush      *brushes;        // BSP_BRUSHES (convex collision volumes)
    bsp_brush_side *brush_sides;    // BSP_BRUSH_SIDES (plane refs per brush)
    uint16_t       *leaf_brushes;   // BSP_LEAF_BRUSHES (leaf -> brush indirection)

    uint32_t        num_vertices;
    uint32_t        num_edges;
    uint32_t        num_face_edges;
    uint32_t        num_faces;
    uint32_t        num_texinfo;
    uint32_t        num_entities;
    uint32_t        lightmaps_size;

    uint32_t        num_planes;
    uint32_t        num_nodes;
    uint32_t        num_leaves;
    uint32_t        num_leaf_faces;
    uint32_t        num_models;
    uint32_t        visibility_size;
    uint32_t        num_clusters;   // parsed from the visibility lump header

    uint32_t        num_brushes;
    uint32_t        num_brush_sides;
    uint32_t        num_leaf_brushes;
} bsp_model;
```

Each per-lump array's length is computed by dividing the lump's byte
length by the size of its element struct (or, for the lightmap lump,
stored as a raw byte count because individual entries are variable-sized
and located via `bsp_face::lightmap_offset`).

## Faces, edges, and vertices

A face references its vertices indirectly:

```
bsp_face::first_edge -> face_edges[N]  (an int32_t, signed)
                          ^
                          |
            signed index into bsp_model::edges
                          |
                          v
                    bsp_edge{v1, v2}
                          |
                          v
                  bsp_model::vertices[v1 or v2]
```

If the signed index in `face_edges` is positive, take `edges[i].v1`; if
negative, take `edges[-i].v2`. This is the standard Quake II winding
mechanism. The `mesh` module follows exactly this rule in
`gather_face_points`.

## Lightmap data layout

The lightmap lump is a raw byte buffer of style-0 RGB triplets. Each
face has a `lightmap_offset` (a byte offset into the lump) and a
`lightmap_styles[4]` array indicating up to four animated style
indices. Crank uses only style 0; styles 1-3 are ignored.

The face's lightmap is `lm_w * lm_h * 3` bytes starting at
`lightmaps + face.lightmap_offset`, where `lm_w` and `lm_h` are derived
from the face's surface extents (see [lightmap.md](lightmap.md)).

A `lightmap_offset` value of `0xFFFFFFFF` (`-1` interpreted as
`uint32_t`) means the face has no lightmap (sky, warp surface, etc.).

## Entity parsing

`bsp_read_entities` is a hand-written state machine that walks the entity
text lump and produces an array of `bsp_entity` records, each containing
a heap array of `{key, value}` string pairs. The state machine accepts
the standard Quake II syntax:

```
{
"classname" "worldspawn"
"sky"       "unit1_"
}
{
"classname" "info_player_start"
"origin"    "32 64 16"
"angle"     "90"
}
```

The parser allocates each key and value with `calloc` and stores plain
C strings. Quoted strings without escape sequences are assumed.

## Error handling

Every helper in `bsp.c` prints a tagged error message on failure (e.g.
`bsp_read_faces: invalid bsp file`). The top-level `bsp_load` is a
straight-line sequence of `bsp_read_*` calls; if any returns `NULL`,
`bsp_load` calls `bsp_free` on the partially-built model, closes the
file, and returns `NULL`. This means callers always get either a fully
populated model or `NULL`.

The lightmap lump is the only exception: it is allowed to be missing or
empty, because some maps legitimately ship without lighting. The engine
falls back to a white lightmap in that case. The visibility lump is
also optional; if absent, `num_clusters` stays at 0 and the visibility
system treats every face as potentially visible.

## Visibility lump layout

```
uint32_t        num_clusters;
bsp_vis_offset  offsets[num_clusters];   // byte offsets into the lump
uint8_t         rle_data[...];           // run-length-encoded bit vectors
```

Each cluster has two byte offsets (`pvs` and `phs`); the engine uses
only `pvs`. The bit vector for a cluster is `(num_clusters + 7) / 8`
bytes wide and is stored RLE-compressed: any non-zero byte is emitted
as-is, and a zero byte is followed by a count byte specifying how many
consecutive zero bytes to expand. `bsp_decompress_pvs` performs the
expansion into a caller-provided buffer.

A leaf's `cluster` field is `uint16_t`; the sentinel value `0xFFFF`
means "no cluster" (typically detail brushes), which never appears in
any PVS bitset.

## Adding a new lump

1. Add the relevant `__attribute__((packed))` struct to `bsp.h`,
   matching the on-disk layout exactly.
2. Add a `bsp_read_<lump>` helper in `bsp.c` modelled on the existing
   helpers; use `bsp_read_lump` for the raw byte read.
3. Add a `<name>` and `num_<name>` field to `bsp_model`.
4. Call the helper from `bsp_load` and free the buffer in `bsp_free`
   via `bsp_free_lump`.
