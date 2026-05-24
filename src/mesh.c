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

#include "mesh.h"
#include "lightmap.h"
#include "raylib.h"
#include "rlgl.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define MAX_FACE_VERTICES 256
#define TEX_PATH_PREFIX   "textures/"
#define MAX_TEX_PATH      256
#define FALLBACK_PATH     "__fallback__"

#define ALPHA_OPAQUE  1.0f
#define ALPHA_TRANS33 0.33f
#define ALPHA_TRANS66 0.66f

static int is_utility_texture(const char *tex_name) {
    static const char *suffixes[] = {
        "clip", "clip_mon", "hint", "origin", "skip", "sky1", "sky2", "trigger"
    };
    static const size_t count = sizeof(suffixes) / sizeof(suffixes[0]);

    if (tex_name == NULL) return 0;
    size_t name_len = strlen(tex_name);

    for (size_t i = 0; i < count; i++) {
        size_t s_len = strlen(suffixes[i]);
        if (name_len < s_len) continue;
        if (strcmp(tex_name + (name_len - s_len), suffixes[i]) == 0) {
            if (name_len == s_len || tex_name[name_len - s_len - 1] == '/') {
                return 1;
            }
        }
    }
    return 0;
}

static int find_texture_index(const mesh *m, const char *path) {
    if (m == NULL || path == NULL) return -1;

    for (uint32_t i = 0; i < m->texture_count; i++) {
        const texture *t = &m->textures[i];
        if (t->path == NULL) continue;
        if (strcmp(t->path, path) == 0) {
            return (int) i;
        }
    }
    return -1;
}

static int append_texture(mesh *m, const texture *src) {
    texture *new_textures = realloc(m->textures, (m->texture_count + 1) * sizeof(texture));
    if (new_textures == NULL) {
        printf("mesh: failed to allocate textures\n");
        return -1;
    }

    m->textures = new_textures;
    m->textures[m->texture_count] = *src;
    int index = (int) m->texture_count;
    m->texture_count += 1;
    return index;
}

#define FALLBACK_SIZE   32
#define FALLBACK_SIZE_2 (FALLBACK_SIZE / 2)

static int ensure_fallback_texture(mesh *m) {
    int existing = find_texture_index(m, FALLBACK_PATH);
    if (existing >= 0) return existing;

    Image img = GenImageColor(FALLBACK_SIZE, FALLBACK_SIZE, MAGENTA);
    for (int y = 0; y < FALLBACK_SIZE; y++) {
        for (int x = 0; x < FALLBACK_SIZE; x++) {
            if ((x >= FALLBACK_SIZE_2 && y < FALLBACK_SIZE_2) || (x < FALLBACK_SIZE_2 && y >= FALLBACK_SIZE_2)) {
                ImageDrawPixel(&img, x, y, BLACK);
            }
        }
    }
    Texture2D rl_tex = LoadTextureFromImage(img);
    UnloadImage(img);

    if (!IsTextureValid(rl_tex)) {
        printf("mesh: failed to create fallback texture\n");
        return -1;
    }

    texture t = {0};
    t.rl_texture = rl_tex;
    t.id = rl_tex.id;
    t.width = rl_tex.width;
    t.height = rl_tex.height;
    t.path = calloc(1, strlen(FALLBACK_PATH) + 1);
    if (t.path == NULL) {
        UnloadTexture(rl_tex);
        printf("mesh: failed to allocate fallback path\n");
        return -1;
    }
    strcpy(t.path, FALLBACK_PATH);

    int index = append_texture(m, &t);
    if (index < 0) {
        UnloadTexture(rl_tex);
        free(t.path);
        return -1;
    }
    return index;
}

static int get_or_load_texture(mesh *m, const char *tex_name) {
    char full_path[MAX_TEX_PATH];
    int written = snprintf(full_path, sizeof(full_path), "%s%s", TEX_PATH_PREFIX, tex_name);
    if (written < 0 || written >= (int) sizeof(full_path)) {
        printf("mesh: texture path too long: %s\n", tex_name);
        return ensure_fallback_texture(m);
    }

    int existing = find_texture_index(m, full_path);
    if (existing >= 0) return existing;

    texture *loaded = tex_load(full_path);
    if (loaded == NULL) {
        printf("mesh: missing texture %s, using fallback\n", full_path);
        return ensure_fallback_texture(m);
    }

    int index = append_texture(m, loaded);
    if (index < 0) {
        UnloadTexture(loaded->rl_texture);
        free(loaded->path);
        free(loaded);
        return ensure_fallback_texture(m);
    }

    free(loaded);
    return index;
}

static int find_or_create_surface_in(mesh_surface **list, uint32_t *count,
                                     uint32_t gl_texture_id, float alpha) {
    for (uint32_t i = 0; i < *count; i++) {
        if ((*list)[i].texture_id == gl_texture_id && (*list)[i].alpha == alpha) {
            return (int) i;
        }
    }

    mesh_surface *grown = realloc(*list, (*count + 1) * sizeof(mesh_surface));
    if (grown == NULL) {
        printf("mesh: failed to allocate surfaces\n");
        return -1;
    }

    *list = grown;
    mesh_surface *s = &(*list)[*count];
    memset(s, 0, sizeof(*s));
    s->texture_id = gl_texture_id;
    s->alpha = alpha;

    int index = (int) *count;
    *count += 1;
    return index;
}

static int find_surface_by_id_alpha(const mesh_surface *list, uint32_t count,
                                     uint32_t gl_id, float alpha) {
    for (uint32_t i = 0; i < count; i++) {
        if (list[i].texture_id == gl_id && list[i].alpha == alpha) {
            return (int) i;
        }
    }
    return -1;
}

static int gather_face_points(const bsp_model *bsp, const bsp_face *face, point3f *out) {
    int count = 0;

    for (int j = 0; j < face->num_edges; j++) {
        if (face->first_edge + j >= bsp->num_face_edges)
            continue;

        int32_t surf_edge = bsp->face_edges[face->first_edge + j];

        if (surf_edge >= (int32_t) bsp->num_edges || surf_edge < -(int32_t) bsp->num_edges)
            continue;

        uint16_t vertex_index;

        if (surf_edge >= 0) {
            vertex_index = bsp->edges[surf_edge].v1;
        } else {
            vertex_index = bsp->edges[-surf_edge].v2;
        }

        if (vertex_index >= bsp->num_vertices)
            continue;

        out[count++] = bsp->vertices[vertex_index];
        if (count >= MAX_FACE_VERTICES) break;
    }

    return count;
}

// Compute lightmap atlas UV for a world-space point on a face.
//   In Quake II, luxel (i,j) covers s in [s_min + i*16 , s_min + (i+1)*16),
//   so the luxel center is at s_min + i*16 + 8.  The texture sample point in
//   atlas pixel coords is:
//       atlas_px = info.atlas_x + (s - s_min - 8) / 16 + 0.5
//   which we then normalize by atlas dims.
static void compute_lightmap_uv(const bsp_texinfo *ti,
                                const lm_face_info *info,
                                const point3f *p,
                                int atlas_w, int atlas_h,
                                float *lu, float *lv) {
    if (!info->has_lightmap) {
        *lu = 0.5f / (float) atlas_w;
        *lv = 0.5f / (float) atlas_h;
        return;
    }

    float s = p->x * ti->u_axis.x + p->y * ti->u_axis.y + p->z * ti->u_axis.z + ti->u_offset;
    float t = p->x * ti->v_axis.x + p->y * ti->v_axis.y + p->z * ti->v_axis.z + ti->v_offset;

    float lx = (s - info->s_min) / 16.0f;
    float ly = (t - info->t_min) / 16.0f;

    float atlas_px = (float) info->atlas_x + lx + 0.5f;
    float atlas_py = (float) info->atlas_y + ly + 0.5f;

    *lu = atlas_px / (float) atlas_w;
    *lv = atlas_py / (float) atlas_h;
}

// Upload the world vertex buffer as a single raylib Mesh.
// We populate positions, texcoords, and texcoords2; raylib will create one
// VAO and three VBOs. The visibility system supplies a per-bucket IBO at
// draw time via rlgl, so we do not populate Mesh.indices.
static void upload_shared_mesh(mesh *m) {
    if (m->vertex_count == 0 || m->vertices == NULL) return;

    Mesh *rm = &m->rl_mesh;
    memset(rm, 0, sizeof(*rm));

    rm->vertexCount = (int) m->vertex_count;
    rm->triangleCount = 0; // Drawing happens via per-bucket IBOs.

    rm->vertices   = (float *) MemAlloc(sizeof(float) * 3 * m->vertex_count);
    rm->texcoords  = (float *) MemAlloc(sizeof(float) * 2 * m->vertex_count);
    rm->texcoords2 = (float *) MemAlloc(sizeof(float) * 2 * m->vertex_count);

    for (uint32_t i = 0; i < m->vertex_count; i++) {
        const mesh_vertex *v = &m->vertices[i];
        rm->vertices[i * 3 + 0] = v->x;
        rm->vertices[i * 3 + 1] = v->y;
        rm->vertices[i * 3 + 2] = v->z;
        rm->texcoords[i * 2 + 0] = v->u;
        rm->texcoords[i * 2 + 1] = v->v;
        rm->texcoords2[i * 2 + 0] = v->lu;
        rm->texcoords2[i * 2 + 1] = v->lv;
    }

    UploadMesh(rm, false);
    m->uploaded = 1;
}

// Allocate an IBO for a surface, sized to its all_index_count, in dynamic mode.
// Initial contents are zero; vis_update will overwrite each frame.
static int upload_surface_ibo(mesh_surface *s) {
    if (s->all_index_count == 0) {
        s->ibo_id = 0;
        return 1;
    }
    // rlLoadVertexBufferElement takes a (void*, int sizeBytes, bool dynamic) tuple.
    // Pass NULL to allocate without an initial upload; some drivers require a
    // non-NULL pointer, so initialize with zeros.
    uint32_t *zero = calloc(s->all_index_count, sizeof(uint32_t));
    if (zero == NULL) {
        printf("mesh: failed to allocate IBO scratch\n");
        return 0;
    }
    s->ibo_id = rlLoadVertexBufferElement(zero,
                                          (int) (s->all_index_count * sizeof(uint32_t)),
                                          true);
    free(zero);
    if (s->ibo_id == 0) {
        printf("mesh: rlLoadVertexBufferElement returned 0\n");
        return 0;
    }
    // Also allocate the CPU-side staging buffer.
    s->frame_index_capacity = s->all_index_count;
    s->frame_indices = calloc(s->frame_index_capacity, sizeof(uint32_t));
    if (s->frame_indices == NULL) {
        printf("mesh: failed to allocate frame_indices\n");
        return 0;
    }
    s->frame_index_count = 0;
    return 1;
}

mesh *mesh_from_bsp(const bsp_model *bsp) {
    if (bsp == NULL) {
        printf("mesh: bsp = NULL\n");
        return NULL;
    }

    mesh *m = calloc(1, sizeof(mesh));
    if (m == NULL) {
        printf("mesh: failed to allocate memory\n");
        return NULL;
    }

    // Build the lightmap atlas first
    lm_atlas *atlas = lm_build(bsp);
    if (atlas == NULL) {
        printf("mesh: failed to build lightmap atlas\n");
        free(m);
        return NULL;
    }

    // Upload the atlas as a raylib Texture2D
    Image atlas_img = {0};
    atlas_img.data = atlas->pixels;
    atlas_img.width = atlas->width;
    atlas_img.height = atlas->height;
    atlas_img.mipmaps = 1;
    atlas_img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8;

    m->lightmap_atlas = LoadTextureFromImage(atlas_img);
    if (IsTextureValid(m->lightmap_atlas)) {
        SetTextureWrap(m->lightmap_atlas, TEXTURE_WRAP_CLAMP);
        SetTextureFilter(m->lightmap_atlas, TEXTURE_FILTER_BILINEAR);
        m->lightmap_id = m->lightmap_atlas.id;
        m->has_lightmap_atlas = 1;
    } else {
        printf("mesh: failed to upload lightmap atlas\n");
        m->has_lightmap_atlas = 0;
    }

    point3f temp[MAX_FACE_VERTICES];

    // Per-face metadata, sized to the BSP face count. Faces we skip stay
    // zeroed (index_count == 0).
    m->faces = calloc(bsp->num_faces, sizeof(mesh_face_info));
    if (m->faces == NULL) {
        printf("mesh: failed to allocate face metadata\n");
        lm_free(atlas);
        mesh_free(m);
        return NULL;
    }
    m->face_count = bsp->num_faces;

    // First pass: count vertices and indices.
    //
    // Vertices: total triangle-list vertex count across all kept faces. Each
    // face contributes (n - 2) * 3 vertices (we re-emit p0 for every triangle
    // in the fan; this matches the previous behaviour and keeps lightmap UVs
    // exact per-triangle).
    uint32_t total_verts = 0;
    for (uint32_t i = 0; i < bsp->num_faces; i++) {
        const bsp_face *face = &bsp->faces[i];
        if (face->num_edges < 3) continue;
        if (face->texture_info >= bsp->num_texinfo) continue;
        const bsp_texinfo *ti = &bsp->texinfo[face->texture_info];
        if (is_utility_texture(ti->texture_name)) continue;

        int n = gather_face_points(bsp, face, temp);
        if (n < 3) continue;

        total_verts += (uint32_t) (n - 2) * 3u;
    }

    if (total_verts > 0) {
        m->vertices = calloc(total_verts, sizeof(mesh_vertex));
        if (m->vertices == NULL) {
            printf("mesh: failed to allocate shared vertex buffer\n");
            lm_free(atlas);
            mesh_free(m);
            return NULL;
        }
    }
    m->vertex_count = 0;

    // Per-surface tally arrays (count indices we'll push into each bucket).
    uint32_t *per_opaque_count = NULL;
    uint32_t  per_opaque_cap = 0;
    uint32_t *per_trans_count = NULL;
    uint32_t  per_trans_cap = 0;

    // ---- First pass over surfaces: create buckets and tally per-bucket index counts. ----
    for (uint32_t i = 0; i < bsp->num_faces; i++) {
        const bsp_face *face = &bsp->faces[i];
        if (face->num_edges < 3) continue;
        if (face->texture_info >= bsp->num_texinfo) continue;
        const bsp_texinfo *ti = &bsp->texinfo[face->texture_info];
        if (is_utility_texture(ti->texture_name)) continue;

        float face_alpha = ALPHA_OPAQUE;
        if (ti->flags & SURF_TRANS33) face_alpha = ALPHA_TRANS33;
        else if (ti->flags & SURF_TRANS66) face_alpha = ALPHA_TRANS66;
        int is_trans = (face_alpha < 1.0f);

        int tex_index = get_or_load_texture(m, ti->texture_name);
        if (tex_index < 0) continue;

        uint32_t gl_id = m->textures[tex_index].id;

        mesh_surface **target_list  = is_trans ? &m->trans_surfaces : &m->surfaces;
        uint32_t      *target_count = is_trans ? &m->trans_surface_count : &m->surface_count;

        int surf_index = find_or_create_surface_in(target_list, target_count, gl_id, face_alpha);
        if (surf_index < 0) continue;

        uint32_t **tally_ptr = is_trans ? &per_trans_count : &per_opaque_count;
        uint32_t  *cap_ptr   = is_trans ? &per_trans_cap   : &per_opaque_cap;

        if (*target_count > *cap_ptr) {
            uint32_t new_cap = *target_count;
            uint32_t *new_tally = realloc(*tally_ptr, new_cap * sizeof(uint32_t));
            if (new_tally == NULL) {
                printf("mesh: failed to allocate per-surface tally\n");
                free(per_opaque_count);
                free(per_trans_count);
                lm_free(atlas);
                mesh_free(m);
                return NULL;
            }
            for (uint32_t k = *cap_ptr; k < new_cap; k++) {
                new_tally[k] = 0;
            }
            *tally_ptr = new_tally;
            *cap_ptr = new_cap;
        }

        int n = gather_face_points(bsp, face, temp);
        if (n < 3) continue;

        (*tally_ptr)[surf_index] += (uint32_t) (n - 2) * 3u;
    }

    // ---- Allocate per-surface static index buffers. ----
    for (uint32_t i = 0; i < m->surface_count; i++) {
        uint32_t n = per_opaque_count[i];
        m->surfaces[i].all_index_count = n;
        if (n == 0) continue;
        m->surfaces[i].all_indices = calloc(n, sizeof(uint32_t));
        if (m->surfaces[i].all_indices == NULL) {
            printf("mesh: failed to allocate opaque index buffer %u\n", i);
            free(per_opaque_count);
            free(per_trans_count);
            lm_free(atlas);
            mesh_free(m);
            return NULL;
        }
    }
    for (uint32_t i = 0; i < m->trans_surface_count; i++) {
        uint32_t n = per_trans_count[i];
        m->trans_surfaces[i].all_index_count = n;
        if (n == 0) continue;
        m->trans_surfaces[i].all_indices = calloc(n, sizeof(uint32_t));
        if (m->trans_surfaces[i].all_indices == NULL) {
            printf("mesh: failed to allocate trans index buffer %u\n", i);
            free(per_opaque_count);
            free(per_trans_count);
            lm_free(atlas);
            mesh_free(m);
            return NULL;
        }
    }

    // Reset surface index write cursors; we reuse the count fields as cursors.
    uint32_t *opaque_cursor = calloc(m->surface_count + 1u, sizeof(uint32_t));
    uint32_t *trans_cursor  = calloc(m->trans_surface_count + 1u, sizeof(uint32_t));
    if ((m->surface_count > 0 && opaque_cursor == NULL) ||
        (m->trans_surface_count > 0 && trans_cursor == NULL)) {
        printf("mesh: failed to allocate per-surface cursor\n");
        free(per_opaque_count);
        free(per_trans_count);
        free(opaque_cursor);
        free(trans_cursor);
        lm_free(atlas);
        mesh_free(m);
        return NULL;
    }

    // ---- Second pass: emit vertices into shared buffer, indices into buckets,
    //      face metadata into mesh.faces. ----
    for (uint32_t i = 0; i < bsp->num_faces; i++) {
        const bsp_face *face = &bsp->faces[i];

        // Default: this face is skipped (index_count == 0 already from calloc).
        mesh_face_info *fi = &m->faces[i];

        if (face->num_edges < 3) continue;
        if (face->texture_info >= bsp->num_texinfo) continue;
        const bsp_texinfo *ti = &bsp->texinfo[face->texture_info];
        if (is_utility_texture(ti->texture_name)) continue;

        float face_alpha = ALPHA_OPAQUE;
        if (ti->flags & SURF_TRANS33) face_alpha = ALPHA_TRANS33;
        else if (ti->flags & SURF_TRANS66) face_alpha = ALPHA_TRANS66;
        int is_trans = (face_alpha < 1.0f);

        char full_path[MAX_TEX_PATH];
        int written = snprintf(full_path, sizeof(full_path), "%s%s", TEX_PATH_PREFIX, ti->texture_name);
        int tex_index;
        if (written < 0 || written >= (int) sizeof(full_path)) {
            tex_index = find_texture_index(m, FALLBACK_PATH);
        } else {
            tex_index = find_texture_index(m, full_path);
            if (tex_index < 0) tex_index = find_texture_index(m, FALLBACK_PATH);
        }
        if (tex_index < 0) continue;

        uint32_t gl_id = m->textures[tex_index].id;

        mesh_surface *list       = is_trans ? m->trans_surfaces      : m->surfaces;
        uint32_t      list_count = is_trans ? m->trans_surface_count : m->surface_count;

        int surf_index = find_surface_by_id_alpha(list, list_count, gl_id, face_alpha);
        if (surf_index < 0) continue;

        int n = gather_face_points(bsp, face, temp);
        if (n < 3) continue;

        float tex_w = (float) m->textures[tex_index].width;
        float tex_h = (float) m->textures[tex_index].height;
        if (tex_w <= 0.0f) tex_w = 1.0f;
        if (tex_h <= 0.0f) tex_h = 1.0f;

        const lm_face_info *finfo = &atlas->faces[i];

        // Triangle-fan: emit (n - 2) triangles, each as 3 fresh vertices.
        uint32_t *cursor = is_trans ? &trans_cursor[surf_index] : &opaque_cursor[surf_index];
        mesh_surface *s = &list[surf_index];

        // Record face metadata BEFORE we start writing this face's indices.
        fi->surface_index = (uint32_t) surf_index;
        fi->first_index   = *cursor;
        fi->is_trans      = (uint8_t) is_trans;

        Vector3 bbmin = (Vector3){ 1e30f,  1e30f,  1e30f};
        Vector3 bbmax = (Vector3){-1e30f, -1e30f, -1e30f};
        double cx = 0.0, cy = 0.0, cz = 0.0;
        uint32_t verts_written = 0;

        point3f p0 = temp[0];
        float u0 = (p0.x * ti->u_axis.x + p0.y * ti->u_axis.y + p0.z * ti->u_axis.z + ti->u_offset) / tex_w;
        float v0 = (p0.x * ti->v_axis.x + p0.y * ti->v_axis.y + p0.z * ti->v_axis.z + ti->v_offset) / tex_h;
        float lu0, lv0;
        compute_lightmap_uv(ti, finfo, &p0, atlas->width, atlas->height, &lu0, &lv0);

        for (int j = 1; j < n - 1; j++) {
            point3f p1 = temp[j];
            point3f p2 = temp[j + 1];

            float u1 = (p1.x * ti->u_axis.x + p1.y * ti->u_axis.y + p1.z * ti->u_axis.z + ti->u_offset) / tex_w;
            float v1 = (p1.x * ti->v_axis.x + p1.y * ti->v_axis.y + p1.z * ti->v_axis.z + ti->v_offset) / tex_h;

            float u2 = (p2.x * ti->u_axis.x + p2.y * ti->u_axis.y + p2.z * ti->u_axis.z + ti->u_offset) / tex_w;
            float v2 = (p2.x * ti->v_axis.x + p2.y * ti->v_axis.y + p2.z * ti->v_axis.z + ti->v_offset) / tex_h;

            float lu1, lv1, lu2, lv2;
            compute_lightmap_uv(ti, finfo, &p1, atlas->width, atlas->height, &lu1, &lv1);
            compute_lightmap_uv(ti, finfo, &p2, atlas->width, atlas->height, &lu2, &lv2);

            // Emit three new vertices, in the original winding order:
            // (p2 swapped, p1 swapped, p0 swapped) as the previous code did.
            uint32_t vi_a = m->vertex_count++;
            m->vertices[vi_a] = (mesh_vertex){p2.x, p2.z, -p2.y, u2, v2, lu2, lv2};
            uint32_t vi_b = m->vertex_count++;
            m->vertices[vi_b] = (mesh_vertex){p1.x, p1.z, -p1.y, u1, v1, lu1, lv1};
            uint32_t vi_c = m->vertex_count++;
            m->vertices[vi_c] = (mesh_vertex){p0.x, p0.z, -p0.y, u0, v0, lu0, lv0};

            s->all_indices[(*cursor)++] = vi_a;
            s->all_indices[(*cursor)++] = vi_b;
            s->all_indices[(*cursor)++] = vi_c;

            // AABB / centroid accumulation, in raylib (post-swap) space.
            for (int kk = 0; kk < 3; kk++) {
                const mesh_vertex *vv = (kk == 0) ? &m->vertices[vi_a]
                                        : (kk == 1) ? &m->vertices[vi_b]
                                                    : &m->vertices[vi_c];
                if (vv->x < bbmin.x) bbmin.x = vv->x;
                if (vv->y < bbmin.y) bbmin.y = vv->y;
                if (vv->z < bbmin.z) bbmin.z = vv->z;
                if (vv->x > bbmax.x) bbmax.x = vv->x;
                if (vv->y > bbmax.y) bbmax.y = vv->y;
                if (vv->z > bbmax.z) bbmax.z = vv->z;
                cx += vv->x;
                cy += vv->y;
                cz += vv->z;
                verts_written++;
            }
        }

        fi->index_count = *cursor - fi->first_index;
        fi->bbox_min    = bbmin;
        fi->bbox_max    = bbmax;
        if (verts_written > 0) {
            double inv = 1.0 / (double) verts_written;
            fi->centroid = (Vector3){(float)(cx * inv), (float)(cy * inv), (float)(cz * inv)};
        }
    }

    free(per_opaque_count);
    free(per_trans_count);
    free(opaque_cursor);
    free(trans_cursor);
    lm_free(atlas);

    // ---- Per-surface static centroid (mean of vertex positions) ----
    // Used as a fallback when no faces are visible for the back-to-front sort.
    for (uint32_t i = 0; i < m->trans_surface_count; i++) {
        mesh_surface *s = &m->trans_surfaces[i];
        if (s->all_index_count == 0) continue;
        double cx = 0.0, cy = 0.0, cz = 0.0;
        for (uint32_t k = 0; k < s->all_index_count; k++) {
            const mesh_vertex *v = &m->vertices[s->all_indices[k]];
            cx += v->x;
            cy += v->y;
            cz += v->z;
        }
        double inv = 1.0 / (double) s->all_index_count;
        s->centroid = (Vector3){(float)(cx * inv), (float)(cy * inv), (float)(cz * inv)};
        s->frame_centroid = s->centroid;
    }

    // ---- Upload the shared vertex buffer once. ----
    upload_shared_mesh(m);

    // ---- Allocate per-surface dynamic IBOs and CPU staging arrays. ----
    for (uint32_t i = 0; i < m->surface_count; i++) {
        if (!upload_surface_ibo(&m->surfaces[i])) {
            printf("mesh: failed to upload IBO for opaque surface %u\n", i);
            mesh_free(m);
            return NULL;
        }
    }
    for (uint32_t i = 0; i < m->trans_surface_count; i++) {
        if (!upload_surface_ibo(&m->trans_surfaces[i])) {
            printf("mesh: failed to upload IBO for trans surface %u\n", i);
            mesh_free(m);
            return NULL;
        }
    }

    return m;
}

static void free_surface_list(mesh_surface *list, uint32_t count) {
    if (list == NULL) return;
    for (uint32_t i = 0; i < count; i++) {
        mesh_surface *s = &list[i];
        if (s->ibo_id != 0) {
            rlUnloadVertexBuffer(s->ibo_id);
            s->ibo_id = 0;
        }
        if (s->all_indices != NULL) {
            free(s->all_indices);
            s->all_indices = NULL;
        }
        if (s->frame_indices != NULL) {
            free(s->frame_indices);
            s->frame_indices = NULL;
        }
    }
    free(list);
}

void mesh_free(mesh *m) {
    if (m == NULL) {
        printf("mesh: m = NULL\n");
        return;
    }

    free_surface_list(m->surfaces, m->surface_count);
    m->surfaces = NULL;
    m->surface_count = 0;

    free_surface_list(m->trans_surfaces, m->trans_surface_count);
    m->trans_surfaces = NULL;
    m->trans_surface_count = 0;

    if (m->faces != NULL) {
        free(m->faces);
        m->faces = NULL;
        m->face_count = 0;
    }

    if (m->vertices != NULL) {
        free(m->vertices);
        m->vertices = NULL;
        m->vertex_count = 0;
    }

    if (m->uploaded) {
        UnloadMesh(m->rl_mesh);
        m->uploaded = 0;
    }

    if (m->textures != NULL) {
        for (uint32_t i = 0; i < m->texture_count; i++) {
            texture *t = &m->textures[i];
            UnloadTexture(t->rl_texture);
            if (t->path != NULL) {
                free(t->path);
            }
        }
        free(m->textures);
    }

    if (m->has_lightmap_atlas) {
        UnloadTexture(m->lightmap_atlas);
        m->has_lightmap_atlas = 0;
    }

    free(m);
}
