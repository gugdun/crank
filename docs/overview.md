# Engine Overview

Crank is a small 3D engine targeted at rendering single-player Quake II
BSP maps. It uses a minimal Entity-Component-System (ECS) layer to
organize behavior, centralized resource managers for GPU/CPU
ownership, and raylib (OpenGL 3.3) for presentation.

The runtime is essentially: load a BSP, build static GPU resources
from it, spawn entities for the map, skybox, and first-person camera,
and present them with the camera each frame.

## Architecture layers

```
                                +----------------+
   main.c (bootstrap)  ------>  | window / r_init|
                                +-------+--------+
                                        |
        +-------------------------------+-------------------------------+
        |                                                               |
        v                                                               v
+-------------------+                                          +-------------------+
| Resource managers |                                          |    ECS world      |
|  res_texture      | <----- borrowed by ----                  |  pools + entities |
|  res_mesh         |                                          +---------+---------+
|  res_map          |                                                    |
+--------+----------+                                                    v
         |                                                       +---------------+
         | owns                                                  |   systems     |
         v                                                       |  sys_fpcam    |
   bsp_model / mesh / texture                                    |  sys_skybox   |
   (low-level modules unchanged)                                 |  sys_map      |
                                                                  +-------+-------+
                                                                          |
                                                                          v
                                                                    r_draw_* (raylib/GL)

+----------------------------------------------------------------+
| entity factory (entity.c)                                      |
|  - parses JSON archetypes from entities/<classname>.json         |
|  - spawns ECS entities with registered component readers        |
+----------------------------------------------------------------+
```

- **Low-level modules** (`bsp`, `mesh`, `lightmap`, `texture`,
  `render`) keep their pre-ECS APIs. They are pure loaders and drawing
  primitives.
- **Resource managers** wrap the loaders and expose opaque uint32
  handles. Components store handles, never raw pointers.
- **ECS world** holds entities and per-component sparse-set pools.
  Components are POD structs.
- **Systems** are plain functions that query the world, mutate
  components, and call into resource managers + `render.h`.

## Modules at a glance

| Module       | Source                                       | Doc                          | Responsibility                                                  |
| ------------ | -------------------------------------------- | ---------------------------- | --------------------------------------------------------------- |
| `bsp`        | `bsp.c`, `bsp.h`                             | [bsp.md](bsp.md)             | Parse a Quake II BSP file into in-memory lumps.                 |
| `texture`    | `texture.c`, `texture.h`                     | [texture.md](texture.md)     | Load a PNG into a raylib `Texture2D` wrapper.                   |
| `lightmap`   | `lightmap.c`, `lightmap.h`                   | [lightmap.md](lightmap.md)   | Compute face extents and pack lightmaps into one atlas.         |
| `mesh`       | `mesh.c`, `mesh.h`                           | [mesh.md](mesh.md)           | Build a shared world VBO + per-bucket dynamic IBOs + face metadata. |
| `vis`        | `vis.c`, `vis.h`                             | [vis.md](vis.md)             | PVS + frustum culling; rewrites per-bucket IBOs each frame.     |
| `render`     | `render.c`, `render.h`                       | [render.md](render.md)       | Bind lightmap shader, draw the pre-culled IBOs, immediate-mode skybox. |
| `ecs`        | `ecs/ecs.c`, `ecs/ecs.h`                     | [ecs.md](ecs.md)             | Entity ids, sparse-set component pools, query iterator.         |
| `res_texture`| `res/res_texture.c`, `res/res_texture.h`     | [res.md](res.md)             | Cache and own loaded `texture*` instances behind `tex_handle`.  |
| `res_mesh`   | `res/res_mesh.c`, `res/res_mesh.h`           | [res.md](res.md)             | Own `mesh*` instances behind `mesh_handle`.                     |
| `res_map`    | `res/res_map.c`, `res/res_map.h`             | [res.md](res.md)             | Load BSP + build mesh, expose `map_handle` views.               |
| `sys_fpcam`  | `sys/sys_fpcam.c`, `sys/sys_fpcam.h`         | [sys.md](sys.md)             | First-person camera: transform/camera/fpcam components.         |
| `sys_skybox` | `sys/sys_skybox.c`, `sys/sys_skybox.h`       | [sys.md](sys.md)             | Skybox component + render.                                      |
| `sys_map`    | `sys/sys_map.c`, `sys/sys_map.h`             | [sys.md](sys.md)             | Map component + render + generic BSP entity spawner.            |
| `entity`     | `ecs/entity.c`, `ecs/entity.h`               | (see ecs.md)                | JSON archetype parser / ECS entity factory.                   |
| `main`       | `main.c`                                     | [main.md](main.md)           | Window init, manager + world setup, frame loop dispatch.        |

## Coordinate systems

There are two coordinate spaces in the engine:

1. **BSP space** (`x` right, `y` forward, `z` up). All vertices,
   planes, `texinfo` axes and lightmap math live in BSP space. Crank
   computes lightmap UVs in this space.
2. **Engine / Raylib space** (`x` right, `y` up, `z` backward). After
   computing UVs the engine swaps each vertex as
   `(x, y, z) -> (x, z, -y)` before uploading to the GPU.

`sys_map.c` applies the same swap to entity `origin` values via the
file-static `parse_origin` helper, so player spawns line up with the
world geometry.

## Lifetime

```
InitWindow
r_init
res_texture_create / res_mesh_create / res_map_create
ecs_world_create + sys_*_register
sjson_create_context
res_map_load
sys_map_process_entities (spawns from JSON)
post-process: attach map handle, load sky textures, spawn player
... per-frame: sys_fpcam_update + sys_*_render ...
sjson_destroy_context
ecs_world_destroy
res_map_destroy
res_mesh_destroy
res_texture_destroy
r_shutdown
CloseWindow
```

All long-lived GPU resources (diffuse textures, lightmap atlas, vertex
buffers, the lightmap shader) are created once at startup and destroyed
once at shutdown. The frame loop never allocates GPU memory.

## Design constraints

- Everything is **static**. There is no support for streaming textures,
  reloading maps, or animating lightmaps. Style-0 lightmaps only.
- The engine targets **OpenGL 3.3 / GLSL 330** through raylib's desktop
  backend.
- The ECS is **deliberately minimal**: sparse-set pools, monotonic
  entity ids (no recycling), explicit system dispatch from `main`. No
  scheduler, no archetypes, no events.
- Resource handles are **opaque integers** so components survive
  internal manager reallocations.
