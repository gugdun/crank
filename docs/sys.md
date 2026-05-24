# systems

The `sys_*` modules implement the engine's behaviors: the BSP-based world,
the skybox, and the first-person camera. Each system module owns one or
more component types and a small set of public functions:
`register`, `spawn`, and one or more update/render entry points.
Systems are dispatched in a fixed order from `main`.

## Files

| File                            | Header                       |
| ------------------------------- | ---------------------------- |
| `src/sys/sys_input.c`           | `src/sys/sys_input.h`        |
| `src/sys/sys_usercmd.c`         | `src/sys/sys_usercmd.h`      |
| `src/sys/sys_fpcam.c`           | `src/sys/sys_fpcam.h`        |
| `src/sys/sys_player.c`          | `src/sys/sys_player.h`       |
| `src/sys/sys_skybox.c`          | `src/sys/sys_skybox.h`       |
| `src/sys/sys_map.c`             | `src/sys/sys_map.h`          |

## sys_input — per-frame input capture

### Component

```c
typedef struct {
    Vector2 mouse_delta;
} c_input;
```

`c_input` carries per-frame data from `sys_input_update` to camera
systems. It has no registered JSON reader; it is zeroed on creation and
refreshed each frame. Movement and button data lives in `c_usercmd_queue`
(see `sys_usercmd` below).

### API

```c
void sys_input_register(ecs_world *w);
void sys_input_update(ecs_world *w);
```

`sys_input_update` runs once per frame. It reads `GetMouseDelta()` and
writes it to every `c_input`. Movement and button data is accumulated
into `c_usercmd_queue` by `sys_usercmd_accumulate` (see below).

## sys_usercmd — command slot for fixed-tick consumption

### Component

```c
#define CMD_BUTTON_JUMP   (1u << 0)
#define CMD_BUTTON_NOCLIP (1u << 1)

typedef struct {
    float    in_fwd;
    float    in_rt;
    float    in_up;
    float    yaw;
    float    pitch;
    uint16_t buttons;
    float    dt_sec;
} c_usercmd;

typedef struct {
    c_usercmd current;
    int       has_current;
    c_usercmd last;
    // builder
    c_usercmd  pending;
    uint16_t   pending_buttons;
} c_usercmd_queue;
```

`c_usercmd_queue` is no longer a ring buffer. It holds a single
pending-builder slot and one finalized `current` command per entity.
This removes the variable latency that a growing/shrinking ring buffer
introduces when frame rate differs from the fixed tick rate.

Each `c_usercmd` now carries its own `dt_sec` and the absolute
`yaw`/`pitch` at the moment the command was finalized, so every
physics integration step is self-contained and deterministic.

No JSON reader is registered — the queue is wholly runtime-managed.

### API

```c
void sys_usercmd_register(ecs_world *w);
void sys_usercmd_accumulate(ecs_world *w, float frame_dt);
void sys_usercmd_finalize(ecs_world *w, float slice_dt);
int  sys_usercmd_consume(c_usercmd_queue *q, c_usercmd *out);
```

`sys_usercmd_accumulate` (per-frame):
- Reads raw key state via Raylib (`IsKeyDown`).
- Overwrites `pending.in_fwd/in_rt/in_up` with the latest WASD/Shift/Space
  axes.
- ORs `pending_buttons` with the current jump / noclip bits so short
  taps that fall between two accumulates are never lost.

`sys_usercmd_finalize` (per-tick, inside the fixed-tick loop):
- Builds a finalized `c_usercmd` from the pending axes, the accumulated
  button bits, and the entity's current `c_transform.yaw/pitch`.
- Sets `cmd.dt_sec = slice_dt`.
- Stores it in `q->current` and sets `q->has_current = 1`.
- Clears `pending_buttons` for the next slice.

`sys_usercmd_consume` (per-tick, inside `sys_player_update`):
- If `has_current` is set, copies the command to `*out`, updates
  `q->last`, clears the flag, and returns `1`.
- Otherwise returns `0`. The caller skips physics for this tick — no
  stale command is replayed and no artificial zero-cmd is injected.

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

- Toggles fullscreen on `F11`.
- Reads `GetMouseDelta()` and updates each `c_fpcam` entity's yaw/pitch
  (clamping pitch to `±pitch_clamp`).
- If the entity has no `c_player`, rebuilds `c_camera.rl_camera` from
  `c_transform` (used by the legacy fly-cam path / non-player cameras).
  Entities with `c_player` have their camera matrix written by
  `sys_player_update` instead, with the eye-height offset applied.

Translation movement (W/A/S/D, jump, crouch) is owned by `sys_player`;
`sys_fpcam` is look-only.

`sys_fpcam_active` returns the first entity whose `c_camera.active`
is non-zero. If none exist, it returns a sane default `Camera` so
`BeginMode3D` doesn't crash.

## sys_player — Quake-style first-person controller

### Components

```c
typedef struct {
    Vector3 half_extents;       // AABB half-size (default {16, 28, 16})
    float   eye_height;         // camera Y offset (default 24)
    float   step_height;        // step-up max (default 18)

    float   accelerate;         // ground accel (default 10)
    float   air_accelerate;     // air accel (default 1)
    float   max_speed;          // target speed (default 320)
    float   friction;           // ground friction (default 6)
    float   stop_speed;         // floor for friction (default 100)
    float   gravity;            // units/sec^2 (default 800)
    float   jump_speed;         // initial jump velocity (default 270)

    int     on_ground;          // refreshed each frame
    int     noclip;             // 1 disables physics, fly mode
} c_player;

typedef struct {
    Vector3 velocity;           // raylib space, units/sec
} c_velocity;
```

Both components have a registered JSON reader; the player archetype
(`entities/player.json`) sets every field.

### API

```c
void sys_player_register(ecs_world *w);
void sys_player_update(ecs_world *w, const phys_world *phys);
```

`sys_player_update` runs inside the fixed-tick loop, *after*
`sys_usercmd_finalize`. For every entity with `c_player` + `c_transform`
+ `c_velocity` + `c_usercmd_queue` it:

1. Consumes one `c_usercmd` via `sys_usercmd_consume`. If none is
   available the entity is skipped for this tick.
2. Edge-detects `CMD_BUTTON_NOCLIP` (0→1 transition since the last
   consumed command) to toggle noclip.
3. **noclip path**: direct velocity from `cmd.in_fwd/in_rt/in_up`
   (with `SPACE`/`SHIFT` as world-Y), skip physics. Uses `cmd.yaw` for
   the movement basis.
4. **physics path**:
    - Ground-trace 2 units down; `on_ground = 1` iff hit normal Y ≥ 0.7.
    - Apply friction on ground (Q2 `PM_Friction`).
    - Accelerate horizontally toward `wishdir` at `accelerate` (ground)
      or `air_accelerate` (air, clamped to 30 units/sec wishspeed).
    - Apply gravity if airborne.
    - Apply jump if `SPACE` pressed and on ground (auto-hop).
    - `step_slide_move`: try the slide move flat; if on ground, also
      try step-up (move up by `step_height`, slide, step back down) and
      keep whichever ended further horizontally on a walkable surface.
    - Re-check ground for the next tick.
5. Refresh `c_camera.rl_camera` from the new transform + `eye_height`.

The `slide_move` helper is the Quake II `PM_SlideMove` algorithm with
up to four bumps. Velocity is clipped against contact planes with
`OVERCLIP = 1.001` to avoid jittering between near-parallel walls;
two-plane creases fall back to motion along the cross product of the
two normals.

`PHYS_MASK_PLAYERSOLID` is used for every trace — solid world,
windows, and `func_*` player-clip brushes block the player.

### Trace usage

Each frame the controller issues:

- 1 ground-check trace (downward, 2 units).
- Up to 4 slide-move traces.
- (Optional) 3 step-up traces (vertical up, slide, vertical down).
- 1 final ground-check trace for next frame's jump latch.

On Quake II maps this resolves under 10 µs total per frame, dominated
by the broadphase AABB rejection.

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
sys_input_update(world);                                   # 1. mouse delta into c_input
sys_fpcam_update(world, dt);                               # 2. look (yaw/pitch)
sys_usercmd_accumulate(world, dt);                         # 3. buffer latest axes + buttons
while (accumulator >= fixed_dt) {
    sys_usercmd_finalize(world, fixed_dt);                 # 4. snapshot angles + dt
    sys_player_update(world, view.phys);                 # 5. physics + move (consume cmd)
    accumulator -= fixed_dt;
}
Camera cam = sys_fpcam_active(world);                      # 6. pick active camera
BeginMode3D(cam);
    sys_skybox_render(world, texmgr, cam.position);        # 7. sky first
    sys_map_render(world, mapmgr, meshmgr, cam.position);  # 8. world (opaque + sorted trans)
EndMode3D();
```

The skybox is rendered before the world so depth-mask-off sky fragments
sit behind opaque world geometry.
