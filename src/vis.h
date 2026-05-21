#ifndef VIS_H
#define VIS_H

#include "bsp.h"
#include "mesh.h"
#include "raylib.h"

#include <stdint.h>

typedef struct vis_state vis_state;

// Builds the visibility state for `bsp` against `m`.
// Precomputes the cluster -> face-list table. Owns no references after
// construction; the caller must not free bsp or m before vis_destroy.
// Returns NULL on failure.
vis_state *vis_create(const bsp_model *bsp, const mesh *m);
void       vis_destroy(vis_state *v);

// Per-frame update. Determines the camera leaf/cluster, decompresses the PVS,
// applies frustum culling, and rewrites each surface's IBO with the visible
// triangles. `cam_pos` is in raylib (y-up) space; the matrix `view_proj`
// must equal projection * view (column-major as raylib stores matrices).
// Returns the number of visible faces (for telemetry / debugging).
uint32_t   vis_update(vis_state *v,
                      const bsp_model *bsp,
                      mesh *m,
                      Vector3 cam_pos,
                      Matrix view_proj);

#endif
