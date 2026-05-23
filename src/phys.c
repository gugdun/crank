#include "phys.h"

#include "bsp.h"
#include "raylib.h"
#include "raymath.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Tolerance used by the swept-AABB plane test to avoid jitter on contact.
#define DIST_EPSILON 0.03125f

typedef struct {
    Vector3 mins;
    Vector3 maxs;
} phys_aabb;

struct phys_world {
    phys_plane *brush_planes;       // flat array, brushes index into it
    uint32_t    num_brush_planes;

    phys_brush *brushes;            // baked brushes (raylib-space planes)
    phys_aabb  *brush_aabbs;        // parallel to brushes
    uint32_t    num_brushes;
};

// Convert a BSP-space point to raylib-space: (x, y, z)_bsp -> (x, z, -y)_rl
static inline Vector3 bsp_to_rl(point3f p) {
    return (Vector3){p.x, p.z, -p.y};
}

// Convert a BSP-space plane (normal, d) to raylib-space.
// Plane equation: dot(n, p) = d. The (x,y,z)_bsp -> (x,z,-y)_rl swap is a
// rigid rotation, so distance is preserved and normal swaps the same way.
static inline phys_plane bsp_plane_to_rl(const bsp_plane *bp) {
    phys_plane p;
    p.normal.x = bp->normal.x;
    p.normal.y = bp->normal.z;
    p.normal.z = -bp->normal.y;
    p.dist = bp->distance;
    return p;
}

// Translate a plane (n, d) so that the geometry shifts by `offset`:
// for point p' = p + offset, dot(n, p') = dot(n, p) + dot(n, offset)
// so the new d is d + dot(n, offset).
static inline void plane_translate(phys_plane *p, Vector3 offset) {
    p->dist += p->normal.x * offset.x + p->normal.y * offset.y + p->normal.z * offset.z;
}

// Compute an AABB for a convex brush given its planes, by maximizing/minimizing
// each axis subject to dot(n_i, p) <= d_i. Standard approach: start with a huge
// box and clip plane-by-plane using support directions. For BSP brushes that
// already contain 6 axial bevel planes (Q2 vbsp adds them), the AABB is implicit
// in the axial planes themselves. We use that fast path and fall back to a wide
// AABB if axial bounds are not found on all 6 sides.
static phys_aabb compute_brush_aabb(const phys_plane *planes, uint32_t num_planes) {
    phys_aabb out;
    out.mins = (Vector3){-FLT_MAX, -FLT_MAX, -FLT_MAX};
    out.maxs = (Vector3){ FLT_MAX,  FLT_MAX,  FLT_MAX};

    // Look for axial planes (n is ±x, ±y, ±z).
    for (uint32_t i = 0; i < num_planes; i++) {
        const phys_plane *p = &planes[i];
        // Treat |component| within 1e-3 of 1 as axial.
        if (fabsf(p->normal.x) > 0.999f) {
            // n.x ≈ +1 -> p.x <= d  (since dot(n,p) <= d defines inside)
            // n.x ≈ -1 -> -p.x <= d, i.e. p.x >= -d
            if (p->normal.x > 0.0f) {
                if (p->dist < out.maxs.x) out.maxs.x = p->dist;
            } else {
                if (-p->dist > out.mins.x) out.mins.x = -p->dist;
            }
        } else if (fabsf(p->normal.y) > 0.999f) {
            if (p->normal.y > 0.0f) {
                if (p->dist < out.maxs.y) out.maxs.y = p->dist;
            } else {
                if (-p->dist > out.mins.y) out.mins.y = -p->dist;
            }
        } else if (fabsf(p->normal.z) > 0.999f) {
            if (p->normal.z > 0.0f) {
                if (p->dist < out.maxs.z) out.maxs.z = p->dist;
            } else {
                if (-p->dist > out.mins.z) out.mins.z = -p->dist;
            }
        }
    }

    // If any axis is still unbounded, widen to a very large but finite box so
    // the broadphase test never trivially rejects this brush.
    const float huge = 65536.0f;
    if (out.mins.x == -FLT_MAX) out.mins.x = -huge;
    if (out.mins.y == -FLT_MAX) out.mins.y = -huge;
    if (out.mins.z == -FLT_MAX) out.mins.z = -huge;
    if (out.maxs.x ==  FLT_MAX) out.maxs.x =  huge;
    if (out.maxs.y ==  FLT_MAX) out.maxs.y =  huge;
    if (out.maxs.z ==  FLT_MAX) out.maxs.z =  huge;
    return out;
}

// Find which BSP model owns a given brush index by scanning the leaves of each
// model's subtree. Returns model index (0 = worldspawn) or 0 if none found.
// This is needed because the file stores all brushes globally; inline-model
// brushes must be translated by their model's origin (and inline-model
// brushes owned by trigger-like entities must be excluded from collision).
//
// Implementation: walk each model's head_node subtree, and for every leaf,
// every brush referenced by leaf_brushes is "owned" by that model. We mark
// brush_to_model[brush_idx]. Inline models (1..N-1) are walked FIRST so they
// claim their brushes before worldspawn does; this matters because the file
// stores all brushes in one global array and worldspawn's tree can in
// principle reach the same brushes referenced by an inline model.
typedef struct {
    const bsp_model *bsp;
    uint32_t        *brush_to_model;   // size = bsp->num_brushes; 0xFFFFFFFF if unowned
} ownership_ctx;

static void walk_node_collect(const ownership_ctx *ctx, int32_t node_idx, uint32_t model_idx) {
    // Quake II BSP children: positive = node index; negative = -(leaf_index + 1).
    if (node_idx < 0) {
        int32_t leaf_idx = -(node_idx + 1);
        if (leaf_idx < 0 || (uint32_t) leaf_idx >= ctx->bsp->num_leaves) return;
        const bsp_leaf *leaf = &ctx->bsp->leaves[leaf_idx];
        for (uint32_t k = 0; k < leaf->num_leaf_brushes; k++) {
            uint32_t lb_idx = (uint32_t) leaf->first_leaf_brush + k;
            if (lb_idx >= ctx->bsp->num_leaf_brushes) continue;
            uint16_t brush_idx = ctx->bsp->leaf_brushes[lb_idx];
            if (brush_idx >= ctx->bsp->num_brushes) continue;
            if (ctx->brush_to_model[brush_idx] == 0xFFFFFFFFu) {
                ctx->brush_to_model[brush_idx] = model_idx;
            }
        }
        return;
    }
    if ((uint32_t) node_idx >= ctx->bsp->num_nodes) return;
    const bsp_node *node = &ctx->bsp->nodes[node_idx];
    walk_node_collect(ctx, node->front_child, model_idx);
    walk_node_collect(ctx, node->back_child, model_idx);
}

// Q2 stores trigger entity brushes with CONTENTS_SOLID on disk (qbsp3 stamps
// CONTENTS_SOLID on any side whose miptex declares no other content flag, and
// the "trigger" texture has none). The fact that those brushes are not
// supposed to collide is encoded only in the entity's classname; the engine
// at runtime spawns the inline model with SOLID_TRIGGER and the trace code
// skips it. We mirror that here: any inline brush model referenced by an
// entity whose classname starts with "trigger_" - or matches one of a small
// list of other non-blocking classes - is excluded from the bake.
//
// We deliberately use prefix matching for "trigger_" rather than a full
// allowlist so that game-specific trigger variants (trigger_push,
// trigger_hurt, trigger_multiple, trigger_once, trigger_relay, ...) all get
// caught without needing per-classname maintenance.
static int classname_is_non_blocking(const char *classname) {
    if (classname == NULL) return 0;
    if (strncmp(classname, "trigger_", 8) == 0) return 1;
    // Area portals are also brush entities that should never block movement;
    // they're handled by the vis/area system, not the collider.
    if (strcmp(classname, "func_areaportal") == 0) return 1;
    return 0;
}

// Parse the "*N" model reference an entity may carry, returning N or -1.
static int parse_inline_model_index(const char *model_str) {
    if (model_str == NULL) return -1;
    if (model_str[0] != '*') return -1;
    char *endp = NULL;
    long v = strtol(model_str + 1, &endp, 10);
    if (endp == model_str + 1) return -1;
    if (v < 0 || v > 0x7FFF) return -1;
    return (int) v;
}

phys_world *phys_create(const bsp_model *bsp) {
    if (bsp == NULL) {
        printf("phys_create: bsp = NULL\n");
        return NULL;
    }

    phys_world *w = calloc(1, sizeof(phys_world));
    if (w == NULL) {
        printf("phys_create: failed to allocate world\n");
        return NULL;
    }

    if (bsp->num_brushes == 0 || bsp->brushes == NULL ||
        bsp->brush_sides == NULL || bsp->planes == NULL) {
        // Empty world is fine; traces will all clear.
        printf("phys_create: warning - bsp has no brush data\n");
        return w;
    }

    // Assign each brush to a model (worldspawn or inline brush model).
    uint32_t *brush_to_model = malloc(bsp->num_brushes * sizeof(uint32_t));
    if (brush_to_model == NULL) {
        printf("phys_create: failed to allocate brush_to_model\n");
        phys_destroy(w);
        return NULL;
    }
    for (uint32_t i = 0; i < bsp->num_brushes; i++) {
        brush_to_model[i] = 0xFFFFFFFFu;
    }

    // Walk inline models (1..N-1) first so they claim their brushes before
    // worldspawn does. Worldspawn's tree can in principle visit the same leaf
    // brushes referenced from an inline model's tree; the inline model is the
    // semantically meaningful owner because that's the entity-level grouping
    // we need to consult for the trigger filter below.
    ownership_ctx octx = { .bsp = bsp, .brush_to_model = brush_to_model };
    for (uint32_t mi = 1; mi < bsp->num_models; mi++) {
        const bsp_model_lump *bm = &bsp->models[mi];
        walk_node_collect(&octx, bm->head_node, mi);
    }
    if (bsp->num_models > 0) {
        walk_node_collect(&octx, bsp->models[0].head_node, 0);
    }
    // Any leftover brushes not reached via any model's tree default to worldspawn.
    for (uint32_t i = 0; i < bsp->num_brushes; i++) {
        if (brush_to_model[i] == 0xFFFFFFFFu) {
            brush_to_model[i] = 0;
        }
    }

    // Build per-model "blocks movement" flag. Model 0 (worldspawn) always
    // blocks. Inline models block by default; any inline model referenced by
    // an entity with a non-blocking classname (trigger_*, func_areaportal)
    // becomes non-blocking and its brushes will be skipped during the bake.
    uint8_t *model_blocks = calloc(bsp->num_models > 0 ? bsp->num_models : 1,
                                   sizeof(uint8_t));
    if (model_blocks == NULL) {
        printf("phys_create: failed to allocate model_blocks\n");
        free(brush_to_model);
        phys_destroy(w);
        return NULL;
    }
    for (uint32_t mi = 0; mi < bsp->num_models; mi++) {
        model_blocks[mi] = 1;
    }
    uint32_t num_filtered_models = 0;
    for (uint32_t ei = 0; ei < bsp->num_entities; ei++) {
        const bsp_entity *be = &bsp->entities[ei];
        const char *classname = bsp_entity_get(be, "classname");
        if (!classname_is_non_blocking(classname)) continue;
        const char *model_str = bsp_entity_get(be, "model");
        int mi = parse_inline_model_index(model_str);
        if (mi <= 0 || (uint32_t) mi >= bsp->num_models) continue;
        if (model_blocks[mi]) {
            model_blocks[mi] = 0;
            num_filtered_models++;
        }
    }

    // Allocate output arrays. We size them to the full BSP brush count for
    // simplicity; brushes owned by non-blocking inline models stay zeroed
    // (num_planes == 0, contents == 0) and are cheaply skipped by the trace
    // mask test, but they don't get plane data written into brush_planes.
    w->brushes = calloc(bsp->num_brushes, sizeof(phys_brush));
    w->brush_aabbs = calloc(bsp->num_brushes, sizeof(phys_aabb));
    if (w->brushes == NULL || w->brush_aabbs == NULL) {
        printf("phys_create: failed to allocate brushes\n");
        free(brush_to_model);
        free(model_blocks);
        phys_destroy(w);
        return NULL;
    }

    // Count total brush sides to allocate a flat plane array. Brushes from
    // non-blocking models are excluded here so we don't waste plane slots.
    uint32_t total_planes = 0;
    for (uint32_t i = 0; i < bsp->num_brushes; i++) {
        uint32_t mi = brush_to_model[i];
        if (mi < bsp->num_models && !model_blocks[mi]) continue;
        total_planes += bsp->brushes[i].num_sides;
    }
    w->brush_planes = calloc(total_planes > 0 ? total_planes : 1, sizeof(phys_plane));
    if (w->brush_planes == NULL) {
        printf("phys_create: failed to allocate brush_planes\n");
        free(brush_to_model);
        free(model_blocks);
        phys_destroy(w);
        return NULL;
    }

    // Bake each brush. Brushes owned by non-blocking inline models
    // (trigger_*, func_areaportal, ...) are left zeroed; their contents stay
    // at 0 so both phys_trace_box's per-brush mask check and
    // clip_box_to_brush's contents-mask early-out reject them without
    // touching their (unset) plane data.
    uint32_t plane_cursor = 0;
    uint32_t num_filtered_brushes = 0;
    for (uint32_t i = 0; i < bsp->num_brushes; i++) {
        const bsp_brush *bb = &bsp->brushes[i];
        phys_brush *pb = &w->brushes[i];

        uint32_t model_idx = brush_to_model[i];
        if (model_idx < bsp->num_models && !model_blocks[model_idx]) {
            // Leave pb zeroed; nothing further to do for this brush.
            num_filtered_brushes++;
            continue;
        }

        pb->first_plane = plane_cursor;
        pb->num_planes  = bb->num_sides;
        pb->contents    = bb->contents;

        Vector3 offset = (Vector3){0, 0, 0};
        if (model_idx > 0 && model_idx < bsp->num_models) {
            offset = bsp_to_rl(bsp->models[model_idx].origin);
        }

        for (uint32_t s = 0; s < bb->num_sides; s++) {
            uint32_t side_idx = bb->first_side + s;
            if (side_idx >= bsp->num_brush_sides) {
                printf("phys_create: brush %u side %u out of range\n", i, s);
                continue;
            }
            uint16_t plane_idx = bsp->brush_sides[side_idx].plane;
            if (plane_idx >= bsp->num_planes) {
                printf("phys_create: brush %u side %u plane %u out of range\n", i, s, plane_idx);
                continue;
            }
            phys_plane pl = bsp_plane_to_rl(&bsp->planes[plane_idx]);
            if (offset.x != 0.0f || offset.y != 0.0f || offset.z != 0.0f) {
                plane_translate(&pl, offset);
            }
            w->brush_planes[plane_cursor++] = pl;
        }

        // Compute AABB from the baked planes (with offset already applied).
        w->brush_aabbs[i] = compute_brush_aabb(w->brush_planes + pb->first_plane,
                                               pb->num_planes);
    }
    w->num_brush_planes = plane_cursor;
    w->num_brushes = bsp->num_brushes;

    free(brush_to_model);
    free(model_blocks);
    printf("phys_create: %u brushes (%u filtered from %u non-blocking models),"
           " %u brush planes\n",
           w->num_brushes, num_filtered_brushes, num_filtered_models,
           w->num_brush_planes);
    return w;
}

void phys_destroy(phys_world *w) {
    if (w == NULL) return;
    if (w->brushes != NULL)      free(w->brushes);
    if (w->brush_planes != NULL) free(w->brush_planes);
    if (w->brush_aabbs != NULL)  free(w->brush_aabbs);
    free(w);
}

// Move the trace bounding box from [start, end] sweep against a single brush.
// Adjusts trace in place. Implements the Quake II swept-AABB-vs-convex-brush
// algorithm: for each plane, compute the box's support distance, then track
// the latest entering plane and earliest leaving plane. If enter > leave the
// segment misses the brush.
static void clip_box_to_brush(const phys_world *w,
                              const phys_brush *brush,
                              Vector3 start, Vector3 end,
                              Vector3 half_extents,
                              uint32_t mask,
                              phys_trace *trace) {
    if (brush->num_planes == 0) return;
    if ((brush->contents & mask) == 0) return;

    float enter_frac = -1.0f;
    float leave_frac = 1.0f;
    const phys_plane *clip_plane = NULL;
    int getout = 0;
    int startout = 0;

    for (uint32_t i = 0; i < brush->num_planes; i++) {
        const phys_plane *p = &w->brush_planes[brush->first_plane + i];

        // Distance from the closest box corner (toward the plane) to the plane.
        // Expand the plane outward by the box's "support" along its normal.
        float ofs = fabsf(p->normal.x) * half_extents.x
                  + fabsf(p->normal.y) * half_extents.y
                  + fabsf(p->normal.z) * half_extents.z;

        float d1 = Vector3DotProduct(p->normal, start) - (p->dist + ofs);
        float d2 = Vector3DotProduct(p->normal, end)   - (p->dist + ofs);

        if (d2 > 0.0f) getout = 1;    // ends outside this plane
        if (d1 > 0.0f) startout = 1;  // started outside this plane

        // If completely in front of this plane (start and end), brush is missed.
        if (d1 > 0.0f && (d2 >= DIST_EPSILON || d2 >= d1)) {
            return;
        }

        // If completely behind the plane (inside w.r.t. this plane), no constraint.
        if (d1 <= 0.0f && d2 <= 0.0f) continue;

        if (d1 > d2) {
            // Crossing the plane from outside to inside.
            float f = (d1 - DIST_EPSILON) / (d1 - d2);
            if (f < 0.0f) f = 0.0f;
            if (f > enter_frac) {
                enter_frac = f;
                clip_plane = p;
            }
        } else {
            // Crossing from inside to outside.
            float f = (d1 + DIST_EPSILON) / (d1 - d2);
            if (f > 1.0f) f = 1.0f;
            if (f < leave_frac) {
                leave_frac = f;
            }
        }
    }

    if (!startout) {
        // Start point is fully inside this brush.
        trace->startsolid = 1;
        if (!getout) {
            trace->allsolid = 1;
            trace->fraction = 0.0f;
            trace->contents = brush->contents;
        }
        return;
    }

    if (enter_frac < leave_frac) {
        if (enter_frac > -1.0f && enter_frac < trace->fraction) {
            if (enter_frac < 0.0f) enter_frac = 0.0f;
            trace->fraction = enter_frac;
            if (clip_plane != NULL) {
                trace->plane_normal = clip_plane->normal;
                trace->plane_dist   = clip_plane->dist;
            }
            trace->contents = brush->contents;
        }
    }
}

static inline int aabb_overlaps(phys_aabb a, Vector3 bmin, Vector3 bmax) {
    if (a.maxs.x < bmin.x || a.mins.x > bmax.x) return 0;
    if (a.maxs.y < bmin.y || a.mins.y > bmax.y) return 0;
    if (a.maxs.z < bmin.z || a.mins.z > bmax.z) return 0;
    return 1;
}

void phys_trace_box(const phys_world *w,
                    Vector3 start, Vector3 end,
                    Vector3 half_extents,
                    uint32_t mask,
                    phys_trace *out) {
    if (out == NULL) return;
    out->fraction = 1.0f;
    out->endpos = end;
    out->plane_normal = (Vector3){0, 0, 0};
    out->plane_dist = 0.0f;
    out->contents = 0;
    out->startsolid = 0;
    out->allsolid = 0;

    if (w == NULL || w->num_brushes == 0) {
        return;
    }

    // Broadphase AABB: the swept-box AABB plus a small epsilon margin.
    Vector3 box_min, box_max;
    box_min.x = fminf(start.x, end.x) - half_extents.x - 1.0f;
    box_min.y = fminf(start.y, end.y) - half_extents.y - 1.0f;
    box_min.z = fminf(start.z, end.z) - half_extents.z - 1.0f;
    box_max.x = fmaxf(start.x, end.x) + half_extents.x + 1.0f;
    box_max.y = fmaxf(start.y, end.y) + half_extents.y + 1.0f;
    box_max.z = fmaxf(start.z, end.z) + half_extents.z + 1.0f;

    for (uint32_t i = 0; i < w->num_brushes; i++) {
        if (out->allsolid) break;
        if ((w->brushes[i].contents & mask) == 0) continue;
        if (!aabb_overlaps(w->brush_aabbs[i], box_min, box_max)) continue;
        clip_box_to_brush(w, &w->brushes[i], start, end, half_extents, mask, out);
    }

    // Finalise endpos.
    out->endpos.x = start.x + (end.x - start.x) * out->fraction;
    out->endpos.y = start.y + (end.y - start.y) * out->fraction;
    out->endpos.z = start.z + (end.z - start.z) * out->fraction;
}
