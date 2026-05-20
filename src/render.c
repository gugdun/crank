#include "render.h"
#include "rlgl.h"
#include "raymath.h"

#include <stdio.h>
#include <stdlib.h>

static Shader g_lightmap_shader;
static int g_lightmap_shader_loaded = 0;
static int g_loc_light_scale = -1;
static int g_loc_surface_alpha = -1;
static Material g_mat;
static int g_mat_ready = 0;

// Scratch buffers for transparent-surface centroid sort
static uint32_t *g_trans_order = NULL;
static float *g_trans_dist = NULL;
static uint32_t g_trans_cap = 0;

#define DEFAULT_LIGHT_SCALE 2.0f

void r_init(void) {
    if (g_lightmap_shader_loaded) return;

    g_lightmap_shader = LoadShader("shaders/lightmap.vs", "shaders/lightmap.fs");

    if (g_lightmap_shader.id == 0) {
        printf("render: failed to load lightmap shader, falling back to default\n");
        // Still set up a default material so we can render unlit
        g_mat = LoadMaterialDefault();
        g_mat_ready = 1;
        return;
    }

    // Resolve our custom uniform (light scale). texture0/texture1 are auto-wired
    // by LoadShader into shader.locs[SHADER_LOC_MAP_DIFFUSE/SPECULAR].
    g_loc_light_scale = GetShaderLocation(g_lightmap_shader, "lightScale");

    if (g_loc_light_scale != -1) {
        float scale = DEFAULT_LIGHT_SCALE;
        SetShaderValue(g_lightmap_shader, g_loc_light_scale, &scale, SHADER_UNIFORM_FLOAT);
    }

    g_loc_surface_alpha = GetShaderLocation(g_lightmap_shader, "surfaceAlpha");
    if (g_loc_surface_alpha != -1) {
        float a = 1.0f;
        SetShaderValue(g_lightmap_shader, g_loc_surface_alpha, &a, SHADER_UNIFORM_FLOAT);
    }

    g_lightmap_shader_loaded = 1;

    // Build a reusable material with our shader.
    g_mat = LoadMaterialDefault();
    g_mat.shader = g_lightmap_shader;
    g_mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    g_mat.maps[MATERIAL_MAP_SPECULAR].color = WHITE;
    g_mat_ready = 1;
}

void r_shutdown(void) {
    if (g_trans_order != NULL) {
        free(g_trans_order);
        g_trans_order = NULL;
    }
    if (g_trans_dist != NULL) {
        free(g_trans_dist);
        g_trans_dist = NULL;
    }
    g_trans_cap = 0;

    if (g_mat_ready) {
        // Don't UnloadMaterial - it would free shader and default locs.
        // We only allocated maps[] via LoadMaterialDefault; free it manually.
        if (g_mat.maps != NULL) {
            RL_FREE(g_mat.maps);
            g_mat.maps = NULL;
        }
        g_mat_ready = 0;
    }
    if (g_lightmap_shader_loaded) {
        UnloadShader(g_lightmap_shader);
        g_lightmap_shader_loaded = 0;
    }
}

static void set_surface_alpha(float a) {
    if (g_loc_surface_alpha != -1) {
        SetShaderValue(g_lightmap_shader, g_loc_surface_alpha, &a, SHADER_UNIFORM_FLOAT);
    }
}

static int ensure_trans_scratch(uint32_t n) {
    if (n <= g_trans_cap) return 1;
    uint32_t *o = realloc(g_trans_order, n * sizeof(uint32_t));
    float *d = realloc(g_trans_dist, n * sizeof(float));
    if (o == NULL || d == NULL) {
        printf("render: failed to allocate trans sort scratch\n");
        free(o);
        free(d);
        return 0;
    }
    g_trans_order = o;
    g_trans_dist = d;
    g_trans_cap = n;
    return 1;
}

static void draw_trans_sorted(const mesh *m, Vector3 cam_pos, Matrix transform) {
    uint32_t n = m->trans_surface_count;
    if (n == 0) return;
    if (!ensure_trans_scratch(n)) return;

    // Filter live surfaces and compute distances
    uint32_t live = 0;
    for (uint32_t i = 0; i < n; i++) {
        const mesh_surface *s = &m->trans_surfaces[i];
        if (!s->uploaded || s->vertex_count == 0) continue;
        Vector3 d = Vector3Subtract(s->centroid, cam_pos);
        g_trans_dist[live] = d.x * d.x + d.y * d.y + d.z * d.z;
        g_trans_order[live] = i;
        live++;
    }
    if (live == 0) return;

    // Insertion sort by descending distance (back-to-front = farthest first)
    for (uint32_t i = 1; i < live; i++) {
        float di = g_trans_dist[i];
        uint32_t oi = g_trans_order[i];
        uint32_t j = i;
        while (j > 0 && g_trans_dist[j - 1] < di) {
            g_trans_dist[j] = g_trans_dist[j - 1];
            g_trans_order[j] = g_trans_order[j - 1];
            j--;
        }
        g_trans_dist[j] = di;
        g_trans_order[j] = oi;
    }

    // Draw sorted transparent surfaces
    rlDrawRenderBatchActive();
    rlEnableColorBlend();
    rlSetBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();

    for (uint32_t k = 0; k < live; k++) {
        const mesh_surface *s = &m->trans_surfaces[g_trans_order[k]];
        set_surface_alpha(s->alpha);
        g_mat.maps[MATERIAL_MAP_DIFFUSE].texture = (Texture2D){
            .id = s->texture_id,
            .width = 1, .height = 1, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
        };
        DrawMesh(s->rl_mesh, g_mat, transform);
    }

    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

void r_draw_mesh(const mesh *m, Vector3 cam_pos) {
    if (m == NULL) {
        printf("render: mesh = NULL\n");
        return;
    }

    if (!g_mat_ready) {
        printf("render: r_init was not called\n");
        return;
    }

    // Bind lightmap atlas to MATERIAL_MAP_SPECULAR (slot 1, mapped to texture1)
    if (m->has_lightmap_atlas) {
        g_mat.maps[MATERIAL_MAP_SPECULAR].texture = m->lightmap_atlas;
    } else {
        // Clear so DrawMesh skips binding slot 1
        g_mat.maps[MATERIAL_MAP_SPECULAR].texture = (Texture2D){0};
    }

    Matrix transform = MatrixIdentity();

    // ---- Opaque pass ----
    set_surface_alpha(1.0f);
    for (uint32_t i = 0; i < m->surface_count; i++) {
        const mesh_surface *s = &m->surfaces[i];
        if (!s->uploaded || s->vertex_count == 0) continue;

        g_mat.maps[MATERIAL_MAP_DIFFUSE].texture = (Texture2D){
            .id = s->texture_id,
            .width = 1, .height = 1, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
        };

        DrawMesh(s->rl_mesh, g_mat, transform);
    }

    // ---- Transparent pass ----
    draw_trans_sorted(m, cam_pos, transform);
}

void r_draw_sky(Vector3 cam_pos, uint32_t bk, uint32_t dn, uint32_t ft, uint32_t lf, uint32_t rt, uint32_t up) {
    const float s = 4096.0f;

    rlDisableDepthMask();

    rlPushMatrix();
    rlTranslatef(cam_pos.x, cam_pos.y, cam_pos.z);

    rlSetTexture(bk);
    rlBegin(RL_TRIANGLES);
    rlColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    rlTexCoord2f(0.0f, 0.0f); rlVertex3f( s,  s,  s);
    rlTexCoord2f(1.0f, 0.0f); rlVertex3f( s,  s, -s);
    rlTexCoord2f(1.0f, 1.0f); rlVertex3f( s, -s, -s);
    rlTexCoord2f(0.0f, 0.0f); rlVertex3f( s,  s,  s);
    rlTexCoord2f(1.0f, 1.0f); rlVertex3f( s, -s, -s);
    rlTexCoord2f(0.0f, 1.0f); rlVertex3f( s, -s,  s);
    rlEnd();

    rlSetTexture(ft);
    rlBegin(RL_TRIANGLES);
    rlColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    rlTexCoord2f(0.0f, 0.0f); rlVertex3f(-s,  s, -s);
    rlTexCoord2f(1.0f, 0.0f); rlVertex3f(-s,  s,  s);
    rlTexCoord2f(1.0f, 1.0f); rlVertex3f(-s, -s,  s);
    rlTexCoord2f(0.0f, 0.0f); rlVertex3f(-s,  s, -s);
    rlTexCoord2f(1.0f, 1.0f); rlVertex3f(-s, -s,  s);
    rlTexCoord2f(0.0f, 1.0f); rlVertex3f(-s, -s, -s);
    rlEnd();

    rlSetTexture(rt);
    rlBegin(RL_TRIANGLES);
    rlColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    rlTexCoord2f(0.0f, 0.0f); rlVertex3f( s,  s, -s);
    rlTexCoord2f(1.0f, 0.0f); rlVertex3f(-s,  s, -s);
    rlTexCoord2f(1.0f, 1.0f); rlVertex3f(-s, -s, -s);
    rlTexCoord2f(0.0f, 0.0f); rlVertex3f( s,  s, -s);
    rlTexCoord2f(1.0f, 1.0f); rlVertex3f(-s, -s, -s);
    rlTexCoord2f(0.0f, 1.0f); rlVertex3f( s, -s, -s);
    rlEnd();

    rlSetTexture(lf);
    rlBegin(RL_TRIANGLES);
    rlColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    rlTexCoord2f(0.0f, 0.0f); rlVertex3f(-s,  s,  s);
    rlTexCoord2f(1.0f, 0.0f); rlVertex3f( s,  s,  s);
    rlTexCoord2f(1.0f, 1.0f); rlVertex3f( s, -s,  s);
    rlTexCoord2f(0.0f, 0.0f); rlVertex3f(-s,  s,  s);
    rlTexCoord2f(1.0f, 1.0f); rlVertex3f( s, -s,  s);
    rlTexCoord2f(0.0f, 1.0f); rlVertex3f(-s, -s,  s);
    rlEnd();

    rlSetTexture(up);
    rlBegin(RL_TRIANGLES);
    rlColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    rlTexCoord2f(1.0f, 1.0f); rlVertex3f(-s,  s, -s);
    rlTexCoord2f(0.0f, 1.0f); rlVertex3f( s,  s, -s);
    rlTexCoord2f(0.0f, 0.0f); rlVertex3f( s,  s,  s);
    rlTexCoord2f(1.0f, 1.0f); rlVertex3f(-s,  s, -s);
    rlTexCoord2f(0.0f, 0.0f); rlVertex3f( s,  s,  s);
    rlTexCoord2f(1.0f, 0.0f); rlVertex3f(-s,  s,  s);
    rlEnd();

    rlSetTexture(dn);
    rlBegin(RL_TRIANGLES);
    rlColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    rlTexCoord2f(1.0f, 1.0f); rlVertex3f(-s, -s,  s);
    rlTexCoord2f(0.0f, 1.0f); rlVertex3f( s, -s,  s);
    rlTexCoord2f(0.0f, 0.0f); rlVertex3f( s, -s, -s);
    rlTexCoord2f(1.0f, 1.0f); rlVertex3f(-s, -s,  s);
    rlTexCoord2f(0.0f, 0.0f); rlVertex3f( s, -s, -s);
    rlTexCoord2f(1.0f, 0.0f); rlVertex3f(-s, -s, -s);
    rlEnd();

    rlPopMatrix();

    rlSetTexture(0);
    rlEnableDepthMask();
}
