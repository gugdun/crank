# Engine Overview

Crank is a small 3D engine targeted at rendering single-player Quake II
BSP maps. It uses a minimal Entity-Component-System (ECS) layer to
organize behaviour, centralized resource managers for GPU/CPU
ownership, and raylib (OpenGL 3.3) for presentation.

The runtime is essentially: load a BSP, build static GPU resources
from it, spawn entities for the map, skybox, and first-person camera,
and present them with the camera each frame.

## Architecture layers

```
main (bootstrap)  ------>  window / r_init
                                 |
         +-----------------------+-----------------------+
         |                                               |
         v                                               v
  Resource managers                                ECS world
  (texture, mesh, map)                             (pools + entities)
         |                                               |
         | owns                                          v
         v                                          systems
   bsp_model / mesh / texture                   (input, view,
   (low-level modules)                          player, map, skybox)
                                                        |
                                                        v
                                                  r_draw_* (raylib/GL)

entity factory
  - parses JSON archetypes from entities/<classname>.json
  - spawns ECS entities with registered component readers
```

- **Low-level modules** (`bsp`, `mesh`, `lightmap`, `texture`,
  `render`) keep their pre-ECS APIs. They are pure loaders and drawing
  primitives.
- **Resource managers** wrap the loaders and expose opaque uint32
  handles. Components store handles, never raw pointers.
- **ECS world** holds entities and per-component sparse-set pools.
  Components are POD structs.
- **Systems** are plain functions that query the world, mutate
  components, and call into resource managers and the renderer.

## Modules at a glance

| Module       | Responsibility                                                  |
| ------------ | --------------------------------------------------------------- |
| `bsp`        | Parse a Quake II BSP file into in-memory lumps.               |
| `texture`    | Load a PNG into a raylib `Texture2D` wrapper.                 |
| `lightmap`   | Compute face extents and pack lightmaps into one atlas.       |
| `mesh`       | Build a shared world VBO + per-bucket dynamic IBOs + face metadata. |
| `vis`        | PVS + frustum culling; rewrites per-bucket IBOs each frame.   |
| `phys`       | Static collision world built from BSP brushes; swept-AABB trace.|
| `render`     | Bind lightmap shader, draw the pre-culled IBOs, immediate-mode skybox. |
| `ecs`        | Entity ids, sparse-set component pools, query iterator.       |
| `res_texture`| Cache and own loaded texture instances behind handles.        |
| `res_mesh`   | Own mesh instances behind handles.                            |
| `res_map`    | Load BSP + build mesh, expose map views.                      |
| `sys_input`  | Per-frame mouse delta into `c_input`.                         |
| `sys_view`   | Per-frame mouse-look + interpolated camera assembly.            |
| `sys_sim`    | Per-tick wrapper: usercmd finalize + player update.           |
| `sys_player` | Quake-style first-person controller; physics + movement.      |
| `sys_skybox` | Skybox component + render.                                     |
| `sys_map`    | Map component + render + generic BSP entity spawner.           |
| `entity`     | JSON archetype parser / ECS entity factory.                   |
| `main`       | Window init, manager + world setup, frame loop dispatch.      |

## Coordinate systems

There are two coordinate spaces in the engine:

1. **BSP space** (`x` right, `y` forward, `z` up). All vertices,
   planes, `texinfo` axes and lightmap math live in BSP space.
2. **Engine / Raylib space** (`x` right, `y` up, `z` backward). After
   computing UVs the engine swaps each vertex as
   `(x, y, z) -> (x, z, -y)` before uploading to the GPU.

The map system applies the same swap to entity `origin` values, so
player spawns line up with the world geometry.

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
... per-frame:
        sys_input_update        (mouse delta into c_input)
        sys_view_look           (mouse-look writes c_view; F11 toggle)
        sys_usercmd_accumulate  (WASD/jump/noclip into pending)
        while accumulator >= fixed_dt:
            sys_sim_tick        (usercmd_finalize + player_update)
        alpha = accumulator / fixed_dt
        sys_view_update(alpha)  (interpolated eye -> c_camera.rl_camera)
        sys_*_render            (with sys_fpcam_active's Camera)
sjson_destroy_context
ecs_world_destroy
res_map_destroy
res_mesh_destroy
res_texture_destroy
r_shutdown
CloseWindow
```

Look (yaw/pitch) is updated at render rate via `c_view`. Physics still
runs at a fixed 128 Hz and writes the *simulation* pose into
`c_transform.position / yaw / pitch`; `sys_player_update` also snapshots
the pre-integration pose into `c_transform.prev_*` on every tick so
`sys_view_update` can lerp the rendered eye position by
`alpha = accumulator / fixed_dt` and avoid per-tick stutter at render
rates above the simulation rate.

All long-lived GPU resources (diffuse textures, lightmap atlas, vertex
buffers, the lightmap shader) are created once at startup and destroyed
once at shutdown. The frame loop never allocates GPU memory.

## Design constraints

- Everything is **static**. There is no support for streaming textures,
  reloading maps, or animating lightmaps. Style-0 lightmaps only.
- The engine targets **OpenGL 3.3 / GLSL 330** through raylib's desktop
  backend.
- The ECS is **deliberately minimal**: sparse-set pools, monotonic
  entity ids (no recycling), explicit system dispatch from main. No
  scheduler, no archetypes, no events.
- Resource handles are **opaque integers** so components survive
  internal manager reallocations.
