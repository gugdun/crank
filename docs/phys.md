# phys

Static collision world built from the Quake II BSP brush lumps, plus a
swept-AABB trace primitive that the player controller (and any future
movable body) uses to move through the level.

## Files

- `src/phys.h` — public API: `phys_world`, `phys_trace`, content-flag
  macros, `phys_create` / `phys_destroy` / `phys_trace_box`.
- `src/phys.c` — implementation.

## Public API

```c
phys_world *phys_create(const bsp_model *bsp);
void        phys_destroy(phys_world *w);

void phys_trace_box(const phys_world *w,
                    Vector3 start, Vector3 end,
                    Vector3 half_extents,
                    uint32_t mask,
                    phys_trace *out);
```

`phys_create(bsp)` walks every BSP model (worldspawn at index 0 plus all
inline brush models) and builds a flat array of convex brushes in
**raylib world space**, including:

- a copy of each brush's bounding planes converted from BSP space
  (`(x, y, z) -> (x, z, -y)`),
- the brush's `contents` bitmask,
- an axis-aligned bounding box used for broadphase rejection.

Inline-brush-model planes are translated by their parent model's
`origin` so doors, plates and other inline geometry sit at their final
world position. The `head_node` of every model is walked to attribute
brushes to the model that owns them, so the translation is applied to
the right brushes even when the file's brush array is shared across
models.

`phys_destroy(w)` frees the world. `NULL` is tolerated.

`phys_trace_box` performs a swept-AABB-versus-brush-world trace from
`start` to `end`. `half_extents` is half the box dimensions on each
axis. `mask` is a bitmask of `PHYS_CONTENTS_*` flags; brushes whose
`contents` shares no bits with `mask` are skipped entirely.

The output `phys_trace` always has a valid `fraction` in `[0, 1]` and
`endpos = start + (end - start) * fraction`. When the move hits, it also
fills `plane_normal`, `plane_dist`, and `contents`. The `startsolid`
flag indicates the box started inside a brush; `allsolid` indicates the
box could not escape (typically a stuck player).

## Content flags

```c
#define PHYS_CONTENTS_SOLID       0x00000001
#define PHYS_CONTENTS_WINDOW      0x00000002
#define PHYS_CONTENTS_AUX         0x00000004
#define PHYS_CONTENTS_LAVA        0x00000008
#define PHYS_CONTENTS_SLIME       0x00000010
#define PHYS_CONTENTS_WATER       0x00000020
#define PHYS_CONTENTS_MIST        0x00000040
#define PHYS_CONTENTS_PLAYERCLIP  0x00010000
#define PHYS_CONTENTS_MONSTERCLIP 0x00020000

#define PHYS_MASK_PLAYERSOLID     (SOLID | WINDOW | PLAYERCLIP)
```

These mirror the Quake II BSP `contents` field. Only `SOLID`, `WINDOW`,
and `PLAYERCLIP` participate in v1 player traces; liquids, mist, and
monster clip volumes are defined but ignored by the controller.

## Algorithm

The world is a flat array of convex brushes. Each brush is stored as a
range into a shared `brush_planes` array plus a baked AABB. There is no
BSP-tree-based broadphase in v1; the per-trace cost is

```
for brush in brushes:
    if not aabb_overlaps(brush.aabb, swept_aabb): continue
    if (brush.contents & mask) == 0:              continue
    clip_box_to_brush(brush, start, end, half_extents)
```

For Quake II's normal map sizes (a few thousand brushes) this resolves
in a small fraction of a millisecond per trace; the player controller
issues four-to-six traces per frame.

`clip_box_to_brush` is the classic swept-AABB-vs-convex-brush plane
walk:

1. For each plane `n·p = d`, expand `d` by the box's support along `n`:
   `d' = d + sum(|n_i| * half_extents_i)`. This converts the
   point-vs-plane test into a swept-box-vs-plane test.
2. Compute `d1 = n·start - d'` and `d2 = n·end - d'`.
3. If both are positive, the segment is on the outside of this plane
   for its whole length — the brush is missed; bail out.
4. Otherwise track `enter_frac` (latest plane entered from outside) and
   `leave_frac` (earliest plane exited to outside).
5. If `enter_frac < leave_frac` and `enter_frac > -1`, the segment
   pierces the brush. The earliest such hit across all brushes wins.

A small `DIST_EPSILON` (1/32 unit) prevents jitter on contact.

## Coordinate transform

The BSP lump stores planes in BSP space (`+x` right, `+y` forward,
`+z` up). The renderer converts vertices to raylib space at upload time
using `(x, y, z) -> (x, z, -y)`. `phys_create` does the same conversion
for every plane normal, plus translates each inline-brush-model plane
by its model's raylib-space origin. All traces therefore run entirely
in raylib space and `c_transform.position` can be passed in directly.

## Lifetime

Owned by `res_map_mgr`. Created right after `bsp_load` succeeds in
`res_map_load`; destroyed in `res_map_destroy`. Exposed on `map_view`
as `phys_world *phys` (borrowed) so systems can access it without
leaking the manager. The `sys_player_update` entry point takes a
`const phys_world *` argument.

## Extending

- **Movers** (doors that actually move). Convert the inline-model
  brush set into a *dynamic* node: keep its planes in model-local
  space, store the current world offset on the entity, and either
  re-translate the planes each frame or transform the trace into the
  mover's local space and back. Add a per-mover entity that triggers
  on touch.
- **Movable AABB props** (HL1 crates). Add a `phys_body` per dynamic
  body holding mass, velocity, AABB. Each frame run the same
  `step_slide_move` used by the player against the static world, then
  do a quadratic body-vs-body sweep. The static world stays unchanged.
- **Liquids / triggers**. Brushes with `CONTENTS_WATER` already pass
  through the trace; the player controller currently ignores them via
  `PHYS_MASK_PLAYERSOLID`. A `phys_point_contents(world, p, mask)`
  helper that reports which brushes a point is inside (no sweep) is a
  natural follow-up.
