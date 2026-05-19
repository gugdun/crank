# systems

The `sys_*` modules implement the three feature behaviors of the
engine: the BSP-based world, the skybox, and the first-person camera.
Each system module owns one or more component types and a small set of
public functions: `register`, `spawn`, and one or more update/render
entry points. Systems are dispatched in a fixed order from `main`.

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
filled in later (typically by `sys_map_apply_spawn`).

`sys_skybox_render` resolves each `tex_handle` to an OpenGL id via
`res_texture_get` and calls `r_draw_sky`. If any side is missing the
skybox is skipped silently.

## sys_map — BSP world

### Component

```c
typedef struct {
    map_handle map;
} c_map;
```

### API

```c
void       sys_map_register(ecs_world *w);
ecs_entity sys_map_spawn(ecs_world *w, map_handle h);
void       sys_map_apply_spawn(ecs_world *w,
                               ecs_entity map_entity,
                               ecs_entity fpcam_entity,
                               ecs_entity skybox_entity,
                               res_texture_mgr *texmgr,
                               res_map_mgr *mapmgr);
void       sys_map_render(ecs_world *w,
                          res_map_mgr *mapmgr,
                          res_mesh_mgr *meshmgr,
                          Vector3 cam_pos);
```

`sys_map_apply_spawn` reads the BSP entity lump (the same data
previously parsed inline in the old `main.c`) and applies it to other
entities:

- `worldspawn.sky` is read, defaulting to `unit1_`. Six PNGs are loaded
  via `res_texture_load` with paths `env/<sky><suffix>` for
  `suffix in {ft, dn, bk, lf, rt, up}`. Each side's wrap mode is set to
  `TEXTURE_WRAP_CLAMP` (matching the original behavior). The resulting
  handles are written to the skybox entity via `sys_skybox_set_sides`.
- `info_player_start.origin` is parsed by the file-static
  `parse_origin` (3 floats, `(x, z, -y)` swap) and written to the fpcam
  entity via `sys_fpcam_set_position`.
- `info_player_start.angle` is read as degrees and written via
  `sys_fpcam_set_yaw`.

If either `fpcam_entity` or `skybox_entity` is `ECS_INVALID`, the
corresponding step is skipped.

`sys_map_render` iterates every `c_map`, resolves its mesh via
`mapmgr -> meshmgr`, and calls `r_draw_mesh` per entity. Multiple map
entities are supported but unused by the current bootstrap.

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
