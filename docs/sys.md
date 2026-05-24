# systems

The `sys_*` modules implement the engine's behaviors: the BSP-based world,
the skybox, and the first-person camera. Each system module owns one or
more component types and a small set of public functions:
`register`, `spawn`, and one or more update/render entry points.
Systems are dispatched in a fixed order from `main`.

The engine separates *per-frame* systems from *per-tick* (fixed-rate)
systems. Mouse input, view orientation (mouse-look), and camera assembly
run every render frame. Physics, the player controller, and user-command
finalisation run at a fixed 128 Hz from inside an accumulator while-loop
in `main`. Render-time camera position is interpolated between the
previous and current tick's simulation pose so motion stays smooth at any
render rate.

## Files

| File                            | Header                       |
| ------------------------------- | ---------------------------- |
| `src/sys/sys_input.c`           | `src/sys/sys_input.h`        |
| `src/sys/sys_usercmd.c`         | `src/sys/sys_usercmd.h`      |
| `src/sys/sys_fpcam.c`           | `src/sys/sys_fpcam.h`        |
| `src/sys/sys_view.c`            | `src/sys/sys_view.h`         |
| `src/sys/sys_sim.c`             | `src/sys/sys_sim.h`          |
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
  button bits, and the entity's current view orientation.
- For `cmd.yaw / cmd.pitch` the finaliser prefers `c_view.yaw / c_view.pitch`
  (frame-rate mouse-look output) so each physics step uses the most
  recent look. It falls back to `c_transform.yaw / c_transform.pitch`
  for entities that don't carry a `c_view` (legacy / non-player).
- Sets `cmd.dt_sec = slice_dt`.
- Stores it in `q->current` and sets `q->has_current = 1`.
- Clears `pending_buttons` for the next slice.

`sys_usercmd_consume` (per-tick, inside `sys_player_update`):
- If `has_current` is set, copies the command to `*out`, updates
  `q->last`, clears the flag, and returns `1`.
- Otherwise returns `0`. The caller skips physics for this tick — no
  stale command is replayed and no artificial zero-cmd is injected.

## sys_fpcam — first-person camera components and spawn helpers

`sys_fpcam` owns the three shared component types used by any first-person
camera entity (`c_transform`, `c_camera`, `c_fpcam`). It does *not* run a
per-frame update any more; mouse-look lives in `sys_view` and camera
matrix assembly lives in `sys_view_update`.

### Components

```c
typedef struct {
    Vector3 position;       // raylib space (y up); simulation pose, advanced by physics tick
    float   yaw;            // degrees around +Y;   simulation yaw, sampled into usercmd
    float   pitch;          // degrees around local right, positive = look down

    // Per-tick interpolation history. sys_player_update copies the
    // current pose into these before integrating each fixed step.
    // sys_view_update reads them to lerp the rendered eye pose.
    // Not serialised from JSON; runtime-only.
    Vector3 prev_position;
    float   prev_yaw;
    float   prev_pitch;
} c_transform;

typedef struct {
    float   fovy;
    int     projection;
    int     active;
    Camera  rl_camera;      // refreshed each frame by sys_view_update
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
constructed from JSON. `c_transform` reads `position [x,y,z]`, `yaw`, and
`pitch` and seeds the `prev_*` fields with the same values so the first
rendered frame doesn't lerp from `(0, 0, 0)`. `c_camera` reads `fovy`,
`projection`, and `active`. `c_fpcam` reads `run_speed`, `sensitivity`,
`m_yaw`, `m_pitch`, and `pitch_clamp`. The `prev_*` fields on
`c_transform` are runtime-only and are never read from JSON.

### API

```c
void       sys_fpcam_register(ecs_world *w);
ecs_entity sys_fpcam_spawn(ecs_world *w, Vector3 position, float yaw_deg);
Camera     sys_fpcam_active(ecs_world *w);

void       sys_fpcam_set_position(ecs_world *w, ecs_entity e, Vector3 pos);
void       sys_fpcam_set_yaw(ecs_world *w, ecs_entity e, float yaw_deg);
```

`sys_fpcam_spawn` creates an entity with `c_transform`, `c_camera`,
`c_fpcam`, `c_input`, `c_usercmd_queue`, and `c_view` (fly-cam default,
`eye_height = 0`). It marks `c_camera.active = 1` and seeds the
interpolation history to the spawn pose. The default direction at
`yaw=0, pitch=0` is `(1, 0, 0)` in raylib space, matching the original
engine's `info_player_start` behaviour where `angle = 0` looks along +X.

`sys_fpcam_set_position` and `sys_fpcam_set_yaw` update `c_transform`
*and* the matching `prev_*` field so the next rendered frame doesn't lerp
from the pre-placement pose. `sys_fpcam_set_yaw` also mirrors the new
yaw into `c_view.yaw` if present, so the first `sys_view_update` after
placement renders the placed orientation rather than the JSON default 0.

`sys_fpcam_active` returns the first entity whose `c_camera.active`
is non-zero. If none exist, it returns a sane default `Camera` so
`BeginMode3D` doesn't crash.

## sys_view — per-frame mouse-look and camera assembly

`sys_view` is the only system that writes `c_camera.rl_camera`. It runs
twice per frame: `sys_view_look` before the fixed-step loop (to consume
mouse delta into `c_view`) and `sys_view_update` after the fixed-step
loop (to build the interpolated camera matrix using
`alpha = accumulator / fixed_dt`).

### Component

```c
typedef struct {
    float yaw;          // degrees around +Y
    float pitch;        // degrees, positive = look down
    float eye_height;   // added on Y to the interpolated eye position
} c_view;
```

`c_view` decouples the *render-time* view orientation from the
*simulation* orientation held in `c_transform`. Mouse-look writes
`c_view` every frame so look feels render-rate-paced rather than
physics-rate-paced. `sys_usercmd_finalize` samples `c_view.yaw/pitch`
into the user command, so the physics step still uses the most recent
look without ever blocking on it.

A registered JSON reader populates `yaw`, `pitch`, and `eye_height`
(default 24.0). The player archetype should declare `c_view` with the
desired eye height; if it doesn't, `main.c` adds a fallback `c_view`
with `eye_height = 24` so the engine still runs against an unmodified
asset bundle.

### API

```c
void sys_view_register(ecs_world *w);
void sys_view_look(ecs_world *w);
void sys_view_update(ecs_world *w, float alpha);
```

`sys_view_look` (per-frame):
- Toggles fullscreen on `F11`.
- For every entity that has `c_view + c_input`, updates `c_view.yaw`
  and `c_view.pitch` from `c_input.mouse_delta`. Sensitivity, `m_yaw`,
  `m_pitch`, and `pitch_clamp` are pulled from `c_fpcam` when present;
  otherwise sane defaults apply.

`sys_view_update` (per-frame, after the fixed-step loop):
- Clamps `alpha` to `[0, 1]`.
- For every entity with `c_view + c_transform + c_camera`:
  - `pos = Vector3Lerp(t.prev_position, t.position, alpha)`
  - `eye = pos + (0, view.eye_height, 0)`
  - Build the look direction from `c_view.yaw / c_view.pitch` (yaw=0,
    pitch=0 looks along +X) and write the full `Camera` into
    `cam.rl_camera`.

This is the single, canonical writer of `cam.rl_camera`. Nothing else
in the engine writes that field.

## sys_sim — per-tick simulation driver

```c
void sys_sim_tick(ecs_world *w, const phys_world *phys, float fixed_dt);
```

Thin wrapper: calls `sys_usercmd_finalize(w, fixed_dt)` then
`sys_player_update(w, phys)`. `main.c` invokes it once per accumulated
fixed step. Per-tick interpolation history is captured inside
`sys_player_update` so multi-tick frames stay continuous.

## sys_player — Quake-style first-person controller

### Components

```c
typedef struct {
    Vector3 half_extents;       // AABB half-size (default {16, 28, 16})
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
`sys_usercmd_finalize` (both are invoked by `sys_sim_tick`). For every
entity with `c_player` + `c_transform` + `c_velocity` + `c_usercmd_queue`
it:

1. Consumes one `c_usercmd` via `sys_usercmd_consume`. If none is
   available the entity is skipped for this tick.
2. Snapshots interpolation history: copies `c_transform.position`,
   `c_transform.yaw`, and `c_transform.pitch` into the matching
   `prev_*` fields. This happens *before* any integration or noclip
   translation, on every tick, so multi-tick frames stay continuous and
   noclip motion still gets smoothed by `sys_view_update`.
3. Latches `c_transform.yaw / c_transform.pitch` to `cmd.yaw / cmd.pitch`
   so the simulation pose ends each tick in lock-step with the latest
   user-command orientation (`sys_usercmd_finalize` sourced these from
   `c_view`).
4. Edge-detects `CMD_BUTTON_NOCLIP` (0→1 transition since the last
   consumed command) to toggle noclip.
5. **noclip path**: direct velocity from `cmd.in_fwd/in_rt/in_up`
   (with `SPACE`/`SHIFT` as world-Y), skip physics. Uses `cmd.yaw` for
   the movement basis.
6. **physics path**:
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

`sys_player_update` no longer writes to `c_camera`. The view system owns
all camera-matrix assembly.

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
sys_view_look(world);                                      # 2. mouse-look + F11 (writes c_view)
sys_usercmd_accumulate(world, dt);                         # 3. buffer latest axes + buttons
while (accumulator >= fixed_dt) {
    sys_sim_tick(world, view.phys, fixed_dt);              # 4. usercmd_finalize + player_update
    accumulator -= fixed_dt;
}
float alpha = accumulator / fixed_dt;                      # 5. interpolation factor in [0, 1)
sys_view_update(world, alpha);                             # 6. build c_camera.rl_camera (lerp)
Camera cam = sys_fpcam_active(world);                      # 7. pick active camera
BeginMode3D(cam);
    sys_skybox_render(world, texmgr, cam.position);        # 8. sky first
    sys_map_render(world, mapmgr, meshmgr, cam.position);  # 9. world (opaque + sorted trans)
EndMode3D();
```

Steps 1, 2, and 3 run once per render frame. Step 4 runs zero or more
times per frame, exactly enough to keep the simulation up to date with
real time. Step 6 turns the interpolated simulation pose into a final
camera matrix; it always runs once per frame regardless of how many
physics ticks happened, so render rate and physics rate are fully
decoupled.

The skybox is rendered before the world so depth-mask-off sky fragments
sit behind opaque world geometry.
