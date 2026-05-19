# bsp

Loader for Quake II BSP files (version 38).

## Files

- `src/bsp.h` — public structs and the `bsp_load` / `bsp_free` API.
- `src/bsp.c` — implementation.

## Public API

```c
bsp_model *bsp_load(const char *path);
void bsp_free(bsp_model *bsp);
const char *bsp_entity_get(const bsp_entity *e, const char *key);
```

`bsp_load(path)` opens `maps/<path>.bsp` and returns a heap-allocated
`bsp_model` populated with the lumps that the engine consumes. On any
parse error the function frees what it has allocated, closes the file,
and returns `NULL`.

`bsp_entity_get` is a linear-search helper that returns the value string
for a given key on an entity, or `NULL` if the key is absent.

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

| Index | Macro             | Type read by               |
| ----- | ----------------- | -------------------------- |
| 0     | `BSP_ENTITIES`    | `bsp_read_entities`        |
| 2     | `BSP_VERTICES`    | `bsp_read_vertices`        |
| 5     | `BSP_TEXTURES`    | `bsp_read_texinfo`         |
| 6     | `BSP_FACES`       | `bsp_read_faces`           |
| 7     | `BSP_LIGHTMAPS`   | `bsp_read_lightmaps`       |
| 11    | `BSP_EDGES`       | `bsp_read_edges`           |
| 12    | `BSP_FACE_EDGES`  | `bsp_read_face_edges`      |

Other lumps (`BSP_PLANES`, `BSP_NODES`, `BSP_LEAVES`, `BSP_LEAF_FACES`,
`BSP_MODELS`, `BSP_BRUSHES`, etc.) have their offsets and lengths
recorded in the header but are not currently consumed.

## In-memory model

```c
typedef struct {
    bsp_header header;

    point3f      *vertices;       // BSP_VERTICES
    bsp_edge     *edges;          // BSP_EDGES
    int32_t      *face_edges;     // BSP_FACE_EDGES (signed: +i = edge i v1->v2, -i = v2->v1)
    bsp_face     *faces;          // BSP_FACES
    bsp_texinfo  *texinfo;        // BSP_TEXTURES
    bsp_entity   *entities;       // BSP_ENTITIES (parsed key/value form)
    uint8_t      *lightmaps;      // BSP_LIGHTMAPS (raw RGB bytes)

    uint32_t      num_vertices;
    uint32_t      num_edges;
    uint32_t      num_face_edges;
    uint32_t      num_faces;
    uint32_t      num_texinfo;
    uint32_t      num_entities;
    uint32_t      lightmaps_size;
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
falls back to a white lightmap in that case.

## Adding a new lump

1. Add the relevant `__attribute__((packed))` struct to `bsp.h`,
   matching the on-disk layout exactly.
2. Add a `bsp_read_<lump>` helper in `bsp.c` modelled on the existing
   helpers; use `bsp_read_lump` for the raw byte read.
3. Add a `<name>` and `num_<name>` field to `bsp_model`.
4. Call the helper from `bsp_load` and free the buffer in `bsp_free`
   via `bsp_free_lump`.
