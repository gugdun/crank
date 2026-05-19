# resource managers

The `res_*` managers centralize ownership of long-lived assets:
textures, world meshes, and BSP maps. Components store opaque uint32
**handles** into these managers, not raw pointers, so they remain
valid across manager reallocations.

Handle convention: `0` is always invalid. A live handle is
`index + 1`, where `index` is the position in the manager's internal
array.

## Files

| File                       | Header                  |
| -------------------------- | ----------------------- |
| `src/res/res_texture.c`    | `src/res/res_texture.h` |
| `src/res/res_mesh.c`       | `src/res/res_mesh.h`    |
| `src/res/res_map.c`        | `src/res/res_map.h`     |

## Lifetime and ordering

Managers are created/destroyed in `main` between `r_init` and
`r_shutdown`, in the order:

1. `res_texture_create`
2. `res_mesh_create`
3. `res_map_create(meshmgr)`   <- borrows mesh manager

and torn down in **reverse order**. Any manager that owns GPU resources
(textures, uploaded meshes) must be destroyed before `r_shutdown` and
`CloseWindow`.

`res_map_destroy` only frees `bsp_model*` (CPU data) and stored name
strings. The world mesh associated with each map is owned by
`res_mesh`, so the call chain is:

- `res_map_destroy(mapmgr)` -> frees bsp_models, keeps mesh handles
  dangling (the mesh manager still owns the memory until its destroy).
- `res_mesh_destroy(meshmgr)` -> `mesh_free` per entry.
- `res_texture_destroy(texmgr)` -> `tex_free` per entry.

## res_texture

```c
typedef uint32_t tex_handle;
typedef struct res_texture_mgr res_texture_mgr;

res_texture_mgr *res_texture_create(void);
void             res_texture_destroy(res_texture_mgr *m);

tex_handle       res_texture_load(res_texture_mgr *m, const char *path);
const texture   *res_texture_get(const res_texture_mgr *m, tex_handle h);
```

`res_texture_load` calls `tex_load(path)` (which itself appends `.png`),
caches the result by `path` via linear scan, and returns the
existing handle on subsequent loads of the same path. Returns `0` if
`tex_load` fails.

`res_texture_get` returns a borrowed pointer for the lifetime of the
manager; the caller must not free.

## res_mesh

```c
typedef uint32_t mesh_handle;
typedef struct res_mesh_mgr res_mesh_mgr;

res_mesh_mgr *res_mesh_create(void);
void          res_mesh_destroy(res_mesh_mgr *m);

mesh_handle   res_mesh_adopt(res_mesh_mgr *m, mesh *mesh_obj);
const mesh   *res_mesh_get(const res_mesh_mgr *m, mesh_handle h);
```

`res_mesh_adopt` takes ownership of a freshly built `mesh*` (typically
the return of `mesh_from_bsp`). On failure to register, the mesh is
freed and `0` is returned. The mesh manager does not deduplicate; every
adopt produces a new handle.

## res_map

```c
typedef uint32_t map_handle;
typedef struct res_map_mgr res_map_mgr;

typedef struct {
    const bsp_model *bsp;     // borrowed
    mesh_handle      mesh;    // resolve via res_mesh_get
    const char      *name;    // borrowed (manager-owned copy)
} map_view;

res_map_mgr *res_map_create(res_mesh_mgr *meshes);
void         res_map_destroy(res_map_mgr *m);

map_handle   res_map_load(res_map_mgr *m, const char *name);
int          res_map_get(const res_map_mgr *m, map_handle h, map_view *out);
```

`res_map_load` does three things atomically:

1. `bsp_load(name)` -> `bsp_model *`.
2. `mesh_from_bsp(bsp)` -> `mesh *`, then `res_mesh_adopt`.
3. Stores `{bsp, mesh_handle, strdup(name)}` and returns `index + 1`.

If any step fails, all partial state is freed before returning `0`.

`res_map_get` fills `*out` with a borrowed view (the strings and `bsp_model`
remain owned by the manager). Returns `1` on success, `0` on invalid
handle.
