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

#ifndef MESH_H
#define MESH_H

#include "bsp.h"
#include "texture.h"
#include "raylib.h"

#include <stdint.h>

typedef struct {
    float x, y, z;
    float u, v;     // diffuse texcoord
    float lu, lv;   // lightmap atlas texcoord (0..1)
} mesh_vertex;

// Per-face metadata, used by the visibility system to decide which face's
// pre-built index range to copy into the per-frame IBO of its bucket.
typedef struct {
    uint32_t surface_index;   // index into mesh.surfaces or mesh.trans_surfaces
    uint32_t first_index;     // offset into surface's static all_indices array
    uint32_t index_count;     // (num_face_vertices - 2) * 3; 0 if face is skipped
    Vector3  bbox_min;        // AABB in raylib (y-up) space, for frustum culling
    Vector3  bbox_max;
    Vector3  centroid;        // mean vertex position (raylib space)
    uint8_t  is_trans;        // 1 if this face's bucket lives in trans_surfaces
    uint8_t  pad[3];
} mesh_face_info;

// A "surface" is a draw bucket: one texture, one alpha. It owns a static
// `all_indices` array containing every triangle that would be drawn if every
// face in this bucket were visible, AND a dynamic IBO whose contents are
// rewritten each frame from the visible subset of those indices.
typedef struct {
    uint32_t texture_id;
    float    alpha;            // 1.0 opaque, 0.33 TRANS33, 0.66 TRANS66
    Vector3  centroid;         // static all-faces centroid (fallback for sort)

    // Static index data: every triangle this surface could draw, packed
    // contiguously, grouped by source face (mesh_face_info.first_index points
    // at the per-face span).
    uint32_t *all_indices;
    uint32_t  all_index_count;

    // Dynamic IBO refreshed by the visibility system each frame.
    uint32_t  ibo_id;          // GL element-array buffer id (0 if not created)
    uint32_t *frame_indices;   // CPU-side staging buffer
    uint32_t  frame_index_count;
    uint32_t  frame_index_capacity;

    // Visible-faces centroid for the back-to-front sort of transparent draws.
    Vector3   frame_centroid;
} mesh_surface;

typedef struct {
    // Shared world-vertex array, uploaded once as a single raylib Mesh
    // (positions + texcoords + texcoords2). All surfaces' indices reference
    // vertices in this single VBO.
    mesh_vertex *vertices;
    uint32_t     vertex_count;
    Mesh         rl_mesh;       // GPU upload (positions, texcoords, texcoords2)
    int          uploaded;      // 1 if rl_mesh contains live GL buffers

    // Draw buckets.
    mesh_surface *surfaces;
    uint32_t      surface_count;

    mesh_surface *trans_surfaces;
    uint32_t      trans_surface_count;

    // Per-face metadata (size = bsp->num_faces; faces with index_count == 0
    // were skipped during build, e.g. utility textures or degenerate geometry).
    mesh_face_info *faces;
    uint32_t        face_count;

    // Texture cache shared across surfaces.
    texture *textures;
    uint32_t texture_count;

    // Lightmap atlas (shared across all surfaces).
    Texture2D lightmap_atlas;
    uint32_t  lightmap_id;
    int       has_lightmap_atlas;
} mesh;

mesh *mesh_from_bsp(const bsp_model *bsp);
void  mesh_free(mesh *m);

#endif
