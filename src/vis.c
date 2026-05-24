/*
===========================================================================
Copyright (C) 2026 gugdun

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, see <https://www.gnu.org/licenses/>.
===========================================================================
*/

#include "vis.h"

#include "bsp.h"
#include "mesh.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// One unsorted list of face indices per cluster. Built from leaf->cluster
// mapping and the BSP_LEAF_FACES indirection. We also keep a separate list
// for "detail" faces (faces in leaves with cluster == -1, which never appear
// in any PVS bitset but still need to be considered every frame).
typedef struct {
    uint32_t *faces;
    uint32_t  count;
} face_list;

struct vis_state {
    face_list *cluster_faces;     // size = num_clusters
    uint32_t   num_clusters;

    face_list  detail_faces;      // faces reachable only from cluster -1 leaves

    uint8_t   *pvs_bits;          // (num_clusters + 7) / 8 bytes
    uint8_t   *visible_face;      // bitset over bsp->num_faces

    int32_t    last_cluster;      // cluster index from previous frame (-2 = none)

    uint32_t   bsp_num_faces;     // cached
};

static void face_list_free(face_list *fl) {
    if (fl->faces != NULL) {
        free(fl->faces);
        fl->faces = NULL;
    }
    fl->count = 0;
}

// Append `face_idx` to `fl` if not already present. O(n); cluster face lists
// are typically small (tens to a few hundred) so this is fine at load time.
static int face_list_add_unique(face_list *fl, uint32_t face_idx) {
    for (uint32_t i = 0; i < fl->count; i++) {
        if (fl->faces[i] == face_idx) return 1;
    }
    uint32_t *grown = realloc(fl->faces, (fl->count + 1) * sizeof(uint32_t));
    if (grown == NULL) {
        printf("vis: failed to grow face_list\n");
        return 0;
    }
    fl->faces = grown;
    fl->faces[fl->count++] = face_idx;
    return 1;
}

static inline void bit_set(uint8_t *bits, uint32_t i) {
    bits[i >> 3] |= (uint8_t) (1u << (i & 7u));
}

static inline int bit_get(const uint8_t *bits, uint32_t i) {
    return (bits[i >> 3] >> (i & 7u)) & 1u;
}

vis_state *vis_create(const bsp_model *bsp, const mesh *m) {
    if (bsp == NULL || m == NULL) {
        printf("vis_create: null arguments\n");
        return NULL;
    }

    vis_state *v = calloc(1, sizeof(vis_state));
    if (v == NULL) {
        printf("vis_create: failed to allocate state\n");
        return NULL;
    }

    v->bsp_num_faces = bsp->num_faces;
    v->num_clusters  = bsp->num_clusters;
    v->last_cluster  = -2;

    if (v->num_clusters > 0) {
        v->cluster_faces = calloc(v->num_clusters, sizeof(face_list));
        if (v->cluster_faces == NULL) {
            printf("vis_create: failed to allocate cluster face table\n");
            vis_destroy(v);
            return NULL;
        }

        uint32_t row_bytes = (v->num_clusters + 7u) / 8u;
        v->pvs_bits = calloc(row_bytes, 1);
        if (v->pvs_bits == NULL) {
            printf("vis_create: failed to allocate pvs scratch\n");
            vis_destroy(v);
            return NULL;
        }
    }

    if (bsp->num_faces > 0) {
        uint32_t face_bits = (bsp->num_faces + 7u) / 8u;
        v->visible_face = calloc(face_bits, 1);
        if (v->visible_face == NULL) {
            printf("vis_create: failed to allocate visible_face bitset\n");
            vis_destroy(v);
            return NULL;
        }
    }

    // Populate per-cluster face lists from leaves. The BSP node/leaf tree
    // only describes model 0 (worldspawn); inline brush models (doors,
    // elevators, water/glass volumes, etc.) are handled separately below.
    for (uint32_t li = 0; li < bsp->num_leaves; li++) {
        const bsp_leaf *leaf = &bsp->leaves[li];
        int32_t cluster = (int16_t) leaf->cluster;  // cluster is uint16_t on disk
        // Cluster -1 is encoded as 0xFFFF; reinterpret.
        if (leaf->cluster == 0xFFFFu) {
            cluster = -1;
        }

        face_list *dst = NULL;
        if (cluster >= 0 && (uint32_t) cluster < v->num_clusters) {
            dst = &v->cluster_faces[cluster];
        } else {
            dst = &v->detail_faces;
        }

        for (uint32_t f = 0; f < leaf->num_leaf_faces; f++) {
            uint32_t lf_idx = (uint32_t) leaf->first_leaf_face + f;
            if (lf_idx >= bsp->num_leaf_faces) continue;
            uint16_t face_idx = bsp->leaf_faces[lf_idx];
            if (face_idx >= bsp->num_faces) continue;
            // Skip faces that the mesh builder discarded (e.g. utility textures).
            if (m->faces[face_idx].index_count == 0) continue;
            if (!face_list_add_unique(dst, face_idx)) {
                vis_destroy(v);
                return NULL;
            }
        }
    }

    // Add every face belonging to an inline brush model (models[1..N-1]) to
    // the detail face list. Inline models are not reachable through the leaf
    // tree, so they never appear in any PVS cluster's face list and would
    // otherwise be invisible. Adding them to detail_faces makes them always-
    // candidate; the per-face frustum test still culls them when off-screen.
    uint32_t inline_face_count = 0;
    for (uint32_t mi = 1; mi < bsp->num_models; mi++) {
        const bsp_model_lump *bm = &bsp->models[mi];
        if (bm->first_face < 0 || bm->num_faces <= 0) continue;
        uint32_t first = (uint32_t) bm->first_face;
        uint32_t end   = first + (uint32_t) bm->num_faces;
        if (end > bsp->num_faces) end = bsp->num_faces;
        for (uint32_t fi = first; fi < end; fi++) {
            if (m->faces[fi].index_count == 0) continue;
            if (!face_list_add_unique(&v->detail_faces, fi)) {
                vis_destroy(v);
                return NULL;
            }
            inline_face_count++;
        }
    }
    if (bsp->num_models > 1) {
        printf("vis_create: %u inline-model faces added to detail list (%u models)\n",
               inline_face_count, bsp->num_models - 1);
    }

    return v;
}

void vis_destroy(vis_state *v) {
    if (v == NULL) return;

    if (v->cluster_faces != NULL) {
        for (uint32_t i = 0; i < v->num_clusters; i++) {
            face_list_free(&v->cluster_faces[i]);
        }
        free(v->cluster_faces);
    }
    face_list_free(&v->detail_faces);

    if (v->pvs_bits != NULL)     free(v->pvs_bits);
    if (v->visible_face != NULL) free(v->visible_face);

    free(v);
}

// ----- Frustum extraction (Gribb / Hartmann, column-major matrix) -----
//
// raylib's Matrix is row-storage but member-named like a math row-major
// matrix. The MVP for clip-space comes out as: clip = mvp * vec4(p, 1).
// For raylib, the matrix struct is:
//   typedef struct Matrix { float m0, m4, m8, m12;     // row 0
//                                   m1, m5, m9, m13;     // row 1
//                                   m2, m6, m10, m14;    // row 2
//                                   m3, m7, m11, m15; }; // row 3
// where the matrix is column-major in memory but the field names describe
// the math layout. Multiplying (clip = M * p), the clip-space coordinates are:
//   clip.x = m0*x + m4*y + m8*z  + m12
//   clip.y = m1*x + m5*y + m9*z  + m13
//   clip.z = m2*x + m6*y + m10*z + m14
//   clip.w = m3*x + m7*y + m11*z + m15
// Planes follow the standard derivation:
//   left  = row4 + row1   right = row4 - row1
//   bot   = row4 + row2   top   = row4 - row2
//   near  = row4 + row3   far   = row4 - row3
typedef struct {
    float nx, ny, nz, d;
} vplane;

static void extract_frustum(Matrix m, vplane out[6]) {
    // row4 = (m3, m7, m11, m15)  (this is the w-row in math notation)
    // row1 = (m0, m4, m8,  m12)
    // row2 = (m1, m5, m9,  m13)
    // row3 = (m2, m6, m10, m14)
    // left = row4 + row1
    out[0].nx = m.m3 + m.m0;
    out[0].ny = m.m7 + m.m4;
    out[0].nz = m.m11 + m.m8;
    out[0].d  = m.m15 + m.m12;
    // right = row4 - row1
    out[1].nx = m.m3 - m.m0;
    out[1].ny = m.m7 - m.m4;
    out[1].nz = m.m11 - m.m8;
    out[1].d  = m.m15 - m.m12;
    // bottom = row4 + row2
    out[2].nx = m.m3 + m.m1;
    out[2].ny = m.m7 + m.m5;
    out[2].nz = m.m11 + m.m9;
    out[2].d  = m.m15 + m.m13;
    // top = row4 - row2
    out[3].nx = m.m3 - m.m1;
    out[3].ny = m.m7 - m.m5;
    out[3].nz = m.m11 - m.m9;
    out[3].d  = m.m15 - m.m13;
    // near = row4 + row3
    out[4].nx = m.m3 + m.m2;
    out[4].ny = m.m7 + m.m6;
    out[4].nz = m.m11 + m.m10;
    out[4].d  = m.m15 + m.m14;
    // far = row4 - row3
    out[5].nx = m.m3 - m.m2;
    out[5].ny = m.m7 - m.m6;
    out[5].nz = m.m11 - m.m10;
    out[5].d  = m.m15 - m.m14;

    // Normalize so the AABB tests behave consistently across planes.
    for (int i = 0; i < 6; i++) {
        float n = sqrtf(out[i].nx * out[i].nx + out[i].ny * out[i].ny + out[i].nz * out[i].nz);
        if (n > 1e-6f) {
            float inv = 1.0f / n;
            out[i].nx *= inv;
            out[i].ny *= inv;
            out[i].nz *= inv;
            out[i].d  *= inv;
        }
    }
}

// AABB-vs-frustum test using the p-vertex/n-vertex shortcut: for each plane,
// the AABB is outside iff the "positive" corner is on the negative side.
static int aabb_in_frustum(const vplane planes[6], Vector3 bmin, Vector3 bmax) {
    for (int i = 0; i < 6; i++) {
        float px = (planes[i].nx >= 0.0f) ? bmax.x : bmin.x;
        float py = (planes[i].ny >= 0.0f) ? bmax.y : bmin.y;
        float pz = (planes[i].nz >= 0.0f) ? bmax.z : bmin.z;
        float dist = planes[i].nx * px + planes[i].ny * py + planes[i].nz * pz + planes[i].d;
        if (dist < 0.0f) {
            return 0;
        }
    }
    return 1;
}

uint32_t vis_update(vis_state *v,
                    const bsp_model *bsp,
                    mesh *m,
                    Vector3 cam_pos,
                    Matrix view_proj) {
    if (v == NULL || bsp == NULL || m == NULL) {
        return 0;
    }

    // Reset visible_face bitset.
    uint32_t face_bytes = (bsp->num_faces + 7u) / 8u;
    if (v->visible_face != NULL) {
        memset(v->visible_face, 0, face_bytes);
    }

    // Convert raylib camera position back to BSP space:
    //   (x_bsp, y_bsp, z_bsp) = (x_rl, -z_rl, y_rl)
    point3f cam_bsp;
    cam_bsp.x =  cam_pos.x;
    cam_bsp.y = -cam_pos.z;
    cam_bsp.z =  cam_pos.y;

    int32_t leaf = bsp_find_leaf(bsp, cam_bsp);
    int32_t cluster = -1;
    if (leaf >= 0 && (uint32_t) leaf < bsp->num_leaves) {
        uint16_t raw = bsp->leaves[leaf].cluster;
        if (raw == 0xFFFFu) {
            cluster = -1;
        } else {
            cluster = (int32_t) raw;
        }
    }

    // Build visible-face set. Three cases:
    //   1. No PVS data or no valid cluster: mark every kept face visible.
    //   2. Valid cluster: OR in cluster_faces for every set bit in PVS,
    //      then OR in detail_faces.
    int draw_all = 0;
    if (v->num_clusters == 0 || cluster < 0 || v->pvs_bits == NULL) {
        draw_all = 1;
    } else {
        if (!bsp_decompress_pvs(bsp, cluster, v->pvs_bits)) {
            draw_all = 1;
        }
    }

    if (draw_all) {
        for (uint32_t i = 0; i < bsp->num_faces; i++) {
            if (m->faces[i].index_count != 0) {
                bit_set(v->visible_face, i);
            }
        }
    } else {
        for (uint32_t c = 0; c < v->num_clusters; c++) {
            if (!bit_get(v->pvs_bits, c)) continue;
            const face_list *fl = &v->cluster_faces[c];
            for (uint32_t i = 0; i < fl->count; i++) {
                bit_set(v->visible_face, fl->faces[i]);
            }
        }
        // Detail faces are always candidates.
        for (uint32_t i = 0; i < v->detail_faces.count; i++) {
            bit_set(v->visible_face, v->detail_faces.faces[i]);
        }
    }

    // Extract frustum planes from the supplied view-projection matrix.
    vplane planes[6];
    extract_frustum(view_proj, planes);

    // Reset per-surface frame index counters and centroid accumulators.
    for (uint32_t i = 0; i < m->surface_count; i++) {
        m->surfaces[i].frame_index_count = 0;
    }
    for (uint32_t i = 0; i < m->trans_surface_count; i++) {
        m->trans_surfaces[i].frame_index_count = 0;
        m->trans_surfaces[i].frame_centroid = (Vector3){0};
    }

    // Per-transparent-surface centroid accumulators (weighted by index count).
    double *t_cx = NULL;
    double *t_cy = NULL;
    double *t_cz = NULL;
    uint32_t *t_w = NULL;
    if (m->trans_surface_count > 0) {
        t_cx = calloc(m->trans_surface_count, sizeof(double));
        t_cy = calloc(m->trans_surface_count, sizeof(double));
        t_cz = calloc(m->trans_surface_count, sizeof(double));
        t_w  = calloc(m->trans_surface_count, sizeof(uint32_t));
        if (t_cx == NULL || t_cy == NULL || t_cz == NULL || t_w == NULL) {
            // Non-fatal: fall back to static centroids.
            free(t_cx); free(t_cy); free(t_cz); free(t_w);
            t_cx = NULL; t_cy = NULL; t_cz = NULL; t_w = NULL;
        }
    }

    // Walk every face: if it's marked visible and passes frustum culling,
    // copy its index span into its surface's frame_indices.
    uint32_t visible_count = 0;
    for (uint32_t i = 0; i < bsp->num_faces; i++) {
        const mesh_face_info *fi = &m->faces[i];
        if (fi->index_count == 0) continue;
        if (!bit_get(v->visible_face, i)) continue;
        if (!aabb_in_frustum(planes, fi->bbox_min, fi->bbox_max)) continue;

        mesh_surface *s;
        if (fi->is_trans) {
            if (fi->surface_index >= m->trans_surface_count) continue;
            s = &m->trans_surfaces[fi->surface_index];
        } else {
            if (fi->surface_index >= m->surface_count) continue;
            s = &m->surfaces[fi->surface_index];
        }

        if (s->frame_index_count + fi->index_count > s->frame_index_capacity) {
            // all_index_count was the upper bound; this should never trip.
            continue;
        }

        memcpy(s->frame_indices + s->frame_index_count,
               s->all_indices + fi->first_index,
               fi->index_count * sizeof(uint32_t));
        s->frame_index_count += fi->index_count;

        if (fi->is_trans && t_cx != NULL) {
            t_cx[fi->surface_index] += (double) fi->centroid.x * (double) fi->index_count;
            t_cy[fi->surface_index] += (double) fi->centroid.y * (double) fi->index_count;
            t_cz[fi->surface_index] += (double) fi->centroid.z * (double) fi->index_count;
            t_w[fi->surface_index]  += fi->index_count;
        }

        visible_count++;
    }

    // Upload each surface's IBO (only those that have visible triangles).
    for (uint32_t i = 0; i < m->surface_count; i++) {
        mesh_surface *s = &m->surfaces[i];
        if (s->ibo_id == 0 || s->frame_index_count == 0) continue;
        rlUpdateVertexBufferElements(s->ibo_id,
                                     s->frame_indices,
                                     (int) (s->frame_index_count * sizeof(uint32_t)),
                                     0);
    }
    for (uint32_t i = 0; i < m->trans_surface_count; i++) {
        mesh_surface *s = &m->trans_surfaces[i];
        if (s->ibo_id == 0 || s->frame_index_count == 0) {
            // Fall back to the static centroid when nothing is visible this frame.
            s->frame_centroid = s->centroid;
            continue;
        }
        rlUpdateVertexBufferElements(s->ibo_id,
                                     s->frame_indices,
                                     (int) (s->frame_index_count * sizeof(uint32_t)),
                                     0);
        if (t_cx != NULL && t_w[i] > 0) {
            double inv = 1.0 / (double) t_w[i];
            s->frame_centroid = (Vector3){
                (float)(t_cx[i] * inv),
                (float)(t_cy[i] * inv),
                (float)(t_cz[i] * inv)
            };
        } else {
            s->frame_centroid = s->centroid;
        }
    }

    free(t_cx);
    free(t_cy);
    free(t_cz);
    free(t_w);

    v->last_cluster = cluster;
    (void) cam_pos;
    return visible_count;
}
