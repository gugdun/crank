#ifndef PHYS_H
#define PHYS_H

#include "bsp.h"
#include "raylib.h"

#include <stdint.h>

// Q2 content flags relevant to v1 collision.
#define PHYS_CONTENTS_SOLID      0x00000001u
#define PHYS_CONTENTS_WINDOW     0x00000002u
#define PHYS_CONTENTS_AUX        0x00000004u
#define PHYS_CONTENTS_LAVA       0x00000008u
#define PHYS_CONTENTS_SLIME      0x00000010u
#define PHYS_CONTENTS_WATER      0x00000020u
#define PHYS_CONTENTS_MIST       0x00000040u
#define PHYS_CONTENTS_PLAYERCLIP 0x00010000u
#define PHYS_CONTENTS_MONSTERCLIP 0x00020000u

// Trace mask used by the player controller.
#define PHYS_MASK_PLAYERSOLID    (PHYS_CONTENTS_SOLID | PHYS_CONTENTS_WINDOW | PHYS_CONTENTS_PLAYERCLIP)

typedef struct {
    Vector3 normal;     // raylib space
    float   dist;       // dot(normal, p) = dist
} phys_plane;

typedef struct {
    uint32_t first_plane;       // into phys_world::brush_planes
    uint32_t num_planes;
    uint32_t contents;
} phys_brush;

typedef struct phys_world phys_world;

typedef struct {
    float    fraction;          // 0..1 along the move; 1 = no hit
    Vector3  endpos;            // start + (end - start) * fraction
    Vector3  plane_normal;      // surface hit normal (raylib space)
    float    plane_dist;
    uint32_t contents;          // brush contents hit (0 if none)
    int      startsolid;        // start point was already inside a brush
    int      allsolid;          // box fully inside a brush; cannot escape
} phys_trace;

// Build a collision world from the BSP. Includes worldspawn (model 0)
// and every inline brush model (model 1..N-1), translated by the entity
// origin baked into the model lump. Returns NULL on failure.
phys_world *phys_create(const bsp_model *bsp);
void        phys_destroy(phys_world *w);

// Swept AABB from `start` to `end`. `half_extents` is half the box size on
// each axis (raylib space). `mask` is a bitmask of PHYS_CONTENTS_* the trace
// should consider solid. On return, `out` always has a valid fraction in
// [0, 1] and `endpos` set.
void phys_trace_box(const phys_world *w,
                    Vector3 start, Vector3 end,
                    Vector3 half_extents,
                    uint32_t mask,
                    phys_trace *out);

#endif
