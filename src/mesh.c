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

static int find_or_create_surface(mesh *m, uint32_t gl_texture_id) {
    for (uint32_t i = 0; i < m->surface_count; i++) {
        if (m->surfaces[i].texture_id == gl_texture_id) {
            return (int) i;
        }
    }

    mesh_surface *new_surfaces = realloc(m->surfaces, (m->surface_count + 1) * sizeof(mesh_surface));
    if (new_surfaces == NULL) {
        printf("mesh: failed to allocate surfaces\n");
        return -1;
    }

    m->surfaces = new_surfaces;
    mesh_surface *s = &m->surfaces[m->surface_count];
    s->vertices = NULL;
    s->vertex_count = 0;
    s->texture_id = gl_texture_id;
    memset(&s->rl_mesh, 0, sizeof(s->rl_mesh));
    s->uploaded = 0;

    int index = (int) m->surface_count;
    m->surface_count += 1;
    return index;
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
        // White tile at (0,0), 1x1, with 1-luxel-padded surroundings.
        // Sample at the center of the white tile.
        *lu = 0.5f / (float) atlas_w;
        *lv = 0.5f / (float) atlas_h;
        return;
    }

    float s = p->x * ti->u_axis.x + p->y * ti->u_axis.y + p->z * ti->u_axis.z + ti->u_offset;
    float t = p->x * ti->v_axis.x + p->y * ti->v_axis.y + p->z * ti->v_axis.z + ti->v_offset;

    // In Quake II, luxel (i,j) is centered at world (s_min + i*16, t_min + j*16).
    // So the float luxel index for an arbitrary point is:
    float lx = (s - info->s_min) / 16.0f;
    float ly = (t - info->t_min) / 16.0f;

    // Luxel index 0 corresponds to atlas texel at (info->atlas_x, info->atlas_y),
    // whose CENTER (where GL_LINEAR samples cleanly) is at (atlas_x + 0.5, atlas_y + 0.5).
    float atlas_px = (float) info->atlas_x + lx + 0.5f;
    float atlas_py = (float) info->atlas_y + ly + 0.5f;

    *lu = atlas_px / (float) atlas_w;
    *lv = atlas_py / (float) atlas_h;
}

static void upload_surface_mesh(mesh_surface *s) {
    if (s->vertex_count == 0 || s->vertices == NULL) return;

    Mesh *rm = &s->rl_mesh;
    memset(rm, 0, sizeof(*rm));

    rm->vertexCount = (int) s->vertex_count;
    rm->triangleCount = (int) (s->vertex_count / 3);

    rm->vertices = (float *) MemAlloc(sizeof(float) * 3 * s->vertex_count);
    rm->texcoords = (float *) MemAlloc(sizeof(float) * 2 * s->vertex_count);
    rm->texcoords2 = (float *) MemAlloc(sizeof(float) * 2 * s->vertex_count);

    for (uint32_t i = 0; i < s->vertex_count; i++) {
        const mesh_vertex *v = &s->vertices[i];
        rm->vertices[i * 3 + 0] = v->x;
        rm->vertices[i * 3 + 1] = v->y;
        rm->vertices[i * 3 + 2] = v->z;
        rm->texcoords[i * 2 + 0] = v->u;
        rm->texcoords[i * 2 + 1] = v->v;
        rm->texcoords2[i * 2 + 0] = v->lu;
        rm->texcoords2[i * 2 + 1] = v->lv;
    }

    UploadMesh(rm, false);
    s->uploaded = 1;
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

    uint32_t *per_surface_count = NULL;
    uint32_t per_surface_cap = 0;

    for (uint32_t i = 0; i < bsp->num_faces; i++) {
        const bsp_face *face = &bsp->faces[i];

        if (face->num_edges < 3)
            continue;

        if (face->texture_info >= bsp->num_texinfo)
            continue;

        const bsp_texinfo *ti = &bsp->texinfo[face->texture_info];

        if (is_utility_texture(ti->texture_name))
            continue;

        int tex_index = get_or_load_texture(m, ti->texture_name);
        if (tex_index < 0) continue;

        uint32_t gl_id = m->textures[tex_index].id;
        int surf_index = find_or_create_surface(m, gl_id);
        if (surf_index < 0) continue;

        if (m->surface_count > per_surface_cap) {
            uint32_t new_cap = m->surface_count;
            uint32_t *new_tally = realloc(per_surface_count, new_cap * sizeof(uint32_t));
            if (new_tally == NULL) {
                printf("mesh: failed to allocate per-surface tally\n");
                free(per_surface_count);
                lm_free(atlas);
                mesh_free(m);
                return NULL;
            }
            for (uint32_t k = per_surface_cap; k < new_cap; k++) {
                new_tally[k] = 0;
            }
            per_surface_count = new_tally;
            per_surface_cap = new_cap;
        }

        int count = gather_face_points(bsp, face, temp);
        if (count < 3) continue;

        per_surface_count[surf_index] += (uint32_t) (count - 2) * 3u;
    }

    for (uint32_t i = 0; i < m->surface_count; i++) {
        uint32_t n = per_surface_count[i];
        if (n == 0) {
            m->surfaces[i].vertices = NULL;
            m->surfaces[i].vertex_count = 0;
            continue;
        }
        m->surfaces[i].vertices = calloc(n, sizeof(mesh_vertex));
        if (m->surfaces[i].vertices == NULL) {
            printf("mesh: failed to allocate vertices for surface %u\n", i);
            free(per_surface_count);
            lm_free(atlas);
            mesh_free(m);
            return NULL;
        }
        m->surfaces[i].vertex_count = 0;
    }

    for (uint32_t i = 0; i < bsp->num_faces; i++) {
        const bsp_face *face = &bsp->faces[i];

        if (face->num_edges < 3)
            continue;

        if (face->texture_info >= bsp->num_texinfo)
            continue;

        const bsp_texinfo *ti = &bsp->texinfo[face->texture_info];

        if (is_utility_texture(ti->texture_name))
            continue;

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
        int surf_index = -1;
        for (uint32_t s = 0; s < m->surface_count; s++) {
            if (m->surfaces[s].texture_id == gl_id) {
                surf_index = (int) s;
                break;
            }
        }
        if (surf_index < 0) continue;

        int count = gather_face_points(bsp, face, temp);
        if (count < 3) continue;

        float tex_w = (float) m->textures[tex_index].width;
        float tex_h = (float) m->textures[tex_index].height;
        if (tex_w <= 0.0f) tex_w = 1.0f;
        if (tex_h <= 0.0f) tex_h = 1.0f;

        mesh_surface *s = &m->surfaces[surf_index];
        const lm_face_info *finfo = &atlas->faces[i];

        point3f p0 = temp[0];
        float u0 = (p0.x * ti->u_axis.x + p0.y * ti->u_axis.y + p0.z * ti->u_axis.z + ti->u_offset) / tex_w;
        float v0 = (p0.x * ti->v_axis.x + p0.y * ti->v_axis.y + p0.z * ti->v_axis.z + ti->v_offset) / tex_h;
        float lu0, lv0;
        compute_lightmap_uv(ti, finfo, &p0, atlas->width, atlas->height, &lu0, &lv0);

        for (int j = 1; j < count - 1; j++) {
            point3f p1 = temp[j];
            point3f p2 = temp[j + 1];

            float u1 = (p1.x * ti->u_axis.x + p1.y * ti->u_axis.y + p1.z * ti->u_axis.z + ti->u_offset) / tex_w;
            float v1 = (p1.x * ti->v_axis.x + p1.y * ti->v_axis.y + p1.z * ti->v_axis.z + ti->v_offset) / tex_h;

            float u2 = (p2.x * ti->u_axis.x + p2.y * ti->u_axis.y + p2.z * ti->u_axis.z + ti->u_offset) / tex_w;
            float v2 = (p2.x * ti->v_axis.x + p2.y * ti->v_axis.y + p2.z * ti->v_axis.z + ti->v_offset) / tex_h;

            float lu1, lv1, lu2, lv2;
            compute_lightmap_uv(ti, finfo, &p1, atlas->width, atlas->height, &lu1, &lv1);
            compute_lightmap_uv(ti, finfo, &p2, atlas->width, atlas->height, &lu2, &lv2);

            s->vertices[s->vertex_count++] = (mesh_vertex){p2.x, p2.z, -p2.y, u2, v2, lu2, lv2};
            s->vertices[s->vertex_count++] = (mesh_vertex){p1.x, p1.z, -p1.y, u1, v1, lu1, lv1};
            s->vertices[s->vertex_count++] = (mesh_vertex){p0.x, p0.z, -p0.y, u0, v0, lu0, lv0};
        }
    }

    free(per_surface_count);
    lm_free(atlas);

    // Upload each surface as a raylib Mesh
    for (uint32_t i = 0; i < m->surface_count; i++) {
        upload_surface_mesh(&m->surfaces[i]);
    }

    return m;
}

void mesh_free(mesh *m) {
    if (m == NULL) {
        printf("mesh: m = NULL\n");
        return;
    }

    if (m->surfaces != NULL) {
        for (uint32_t i = 0; i < m->surface_count; i++) {
            mesh_surface *s = &m->surfaces[i];
            if (s->uploaded) {
                UnloadMesh(s->rl_mesh);
                s->uploaded = 0;
            }
            if (s->vertices != NULL) {
                free(s->vertices);
            }
        }
        free(m->surfaces);
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
