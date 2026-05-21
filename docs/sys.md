# systems

The `sys_*` modules implement the engine's behaviors: the BSP-based world,
the skybox, and the first-person camera. Each system module owns one or
more component types and a small set of public functions:
`register`, `spawn`, and one or more update/render entry points.
Systems are dispatched in a fixed order from `main`.

## Files

| File                            | Header                       |
| ------------------------------- | ---------------------------- |
| `src/sys/sys_fpcam.c`           | `src/sys/sys_fpcam.h`        |
| `src/sys/sys_skybox.c`          | `src/sys/sys_skybox.h`       |
| `src/sys/sys_map.c`             | `src/sys/sys_map.h`          |

## sys_fpcam — first-person camera

### Components

```c
typedef struct {
    Vector3 position;       // raylib space (y up)
    float   yaw;            // degrees around +Y
    float   pitch;          // degrees around local right, positive = look down
} c_transform;

typedef struct {
    float   fovy;
    int     projection;
    int     active;
    Camera  rl_camera;      // refreshed each frame from c_transform
} c_camera;

typedef struct {
    float run_speed;        // units per second; default 320
    float sensitivity;      // default 1.0
    float m_yaw;            // deg/pixel; default 0.022
    float m_pitch;          // deg/pixel; default 0.022
    float pitch_clamp;      // |pitch| max; default 89
} c_fpcam;
```

Each component has a registered `ecs_component_reader` so entities can be
constructed from JSON. `c_transform` fields are `position [x,y,z]`, `yaw`,
and `pitch`. `c_camera` fields are `fovy`, `projection`, and `active`.
`c_fpcam` fields are `run_speed`, `sensitivity`, `m_yaw`, `m_pitch`, and
`pitch_clamp`.

### API

```c
void       sys_fpcam_register(ecs_world *w);
ecs_entity sys_fpcam_spawn(ecs_world *w, Vector3 position, float yaw_deg);
void       sys_fpcam_update(ecs_world *w, float dt);
Camera     sys_fpcam_active(ecs_world *w);

void       sys_fpcam_set_position(ecs_world *w, ecs_entity e, Vector3 pos);
void       sys_fpcam_set_yaw(ecs_world *w, ecs_entity e, float yaw_deg);
```

`sys_fpcam_spawn` creates an entity with all three components and marks
`c_camera.active = 1`. The default direction at `yaw=0, pitch=0` is
`(1, 0, 0)` in raylib space, matching the original engine's
`info_player_start` behaviour where `angle = 0` looks along +X.

`sys_fpcam_update`:

- Toggles fullscreen on `F11` (player-controlled input, kept with the
  controller).
- Reads `W/A/S/D/SPACE/LEFT_SHIFT` and `GetMouseDelta()`.
- Updates each `c_fpcam` entity's yaw/pitch (clamping pitch to
  `±pitch_clamp`) and applies world-space movement (`SPACE`/`SHIFT`
  move along world Y, not the camera's local Z).
- Rebuilds `c_camera.rl_camera` from the new transform.

`sys_fpcam_active` returns the first entity whose `c_camera.active`
is non-zero. If none exist, it returns a sane default `Camera` so
`BeginMode3D` doesn't crash.

## sys_skybox — six-sided skybox

### Component

```c
typedef struct {
    tex_handle ft, bk, lf, rt, up, dn;
    float      size;        // world units; default 4096
} c_skybox;
```

`c_skybox` has a registered reader that initialises `size` from JSON.
The six texture handles are always zeroed by the reader; they are
filled in later by `sys_skybox_set_sides`.

### API

```c
void       sys_skybox_register(ecs_world *w);
ecs_entity sys_skybox_spawn(ecs_world *w);
void       sys_skybox_set_sides(ecs_world *w, ecs_entity e,
                                tex_handle ft, tex_handle bk,
                                tex_handle lf, tex_handle rt,
                                tex_handle up, tex_handle dn);
void       sys_skybox_render(ecs_world *w,
                             const res_texture_mgr *texmgr,
                             Vector3 cam_pos);
```

`sys_skybox_spawn` creates an entity with zeroed handles. Sides are
filled in later (typically by `main.c` after reading the BSP worldspawn).

`sys_skybox_render` resolves each `tex_handle` to an OpenGL id via
`res_texture_get` and calls `r_draw_sky`. If any side is missing the
skybox is skipped silently.

## sys_map — BSP world

### Components

```c
typedef struct {
    map_handle map;
} c_map;

typedef struct {
    char sky_prefix[32];
} c_worldspawn;

typedef struct {
    int  team;
    char targetname[64];
} c_spawn_point;
```

`c_map` stores the runtime map handle. `c_worldspawn` stores the default
sky texture prefix (overridden by the BSP worldspawn entity at load time).
`c_spawn_point` marks an entity as a player respawn location.

All three components have registered JSON readers. `c_map` is zeroed.
`c_worldspawn` reads `sky_prefix`. `c_spawn_point` reads `team`.

`targetname` is populated by `sys_map_process_entities` from the BSP
entity lump. The player spawn logic in `main` prefers spawn points whose
`targetname` is empty, because Quake II `info_player_start` entities with
a `targetname` are teleport destinations rather than normal spawn points.

### API

```c
void       sys_map_register(ecs_world *w);
ecs_entity sys_map_spawn(ecs_world *w, map_handle h);
int        sys_map_process_entities(ecs_world *w,
                                      const bsp_model *bsp,
                                      sjson_context *sctx);
void       sys_map_render(ecs_world *w,
                          res_map_mgr *mapmgr,
                          res_mesh_mgr *meshmgr,
                          Vector3 cam_pos);
```

`sys_map_spawn` is a convenience function for engine-internal map
entities. It creates an entity with `c_map` and sets the handle.

`sys_map_process_entities` is the generic BSP-to-JSON entity spawner.
For every entity in the BSP lump it:

1. Builds the path `entities/<classname>.json`.
2. Parses the JSON archetype and spawns an ECS entity.
3. Applies BSP `origin` and `angle` overrides to the entity's
   `c_transform` component (if present).

If no JSON file exists for a given `classname`, that entity is silently
skipped. This makes the engine automatically support any BSP entity type
as long as a matching JSON archetype is provided.

`sys_map_render` iterates every `c_map`, resolves the map view (which
exposes the BSP, the mesh, and the visibility state), captures the
current view-projection matrix from `rlGetMatrixModelview()` *
`rlGetMatrixProjection()`, calls `vis_update` to rebuild the per-bucket
IBOs for the visible/in-frustum subset of faces, and then calls
`r_draw_mesh`. Multiple map entities are supported but unused by the
current bootstrap. The `vis_update` call mutates per-surface IBO state,
which is why the const-pointer from the mesh manager is cast away here.

## Dispatch order

`main` invokes systems each frame in this strict order, all inside the
single window/event loop:

```
sys_fpcam_update(world, dt);          # 1. input + look + move
Camera cam = sys_fpcam_active(world); # 2. pick active camera
BeginMode3D(cam);
    sys_skybox_render(world, texmgr, cam.position);    # 3. sky first
    sys_map_render(world, mapmgr, meshmgr, cam.position); # 4. world (opaque + sorted trans)
EndMode3D();
```

The skybox is rendered before the world so depth-mask-off sky fragments
sit behind opaque world geometry.
