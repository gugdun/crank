#include "render.h"
#include "rlgl.h"
#include "raymath.h"

#include <stdio.h>
#include <stdlib.h>

static Shader g_lightmap_shader;
static int g_lightmap_shader_loaded = 0;
static int g_loc_light_scale = -1;
static Material g_mat;
static int g_mat_ready = 0;

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

    g_lightmap_shader_loaded = 1;

    // Build a reusable material with our shader.
    g_mat = LoadMaterialDefault();
    g_mat.shader = g_lightmap_shader;
    g_mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    g_mat.maps[MATERIAL_MAP_SPECULAR].color = WHITE;
    g_mat_ready = 1;
}

void r_shutdown(void) {
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

void r_draw_mesh(const mesh *m) {
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

    for (uint32_t i = 0; i < m->surface_count; i++) {
        const mesh_surface *s = &m->surfaces[i];
        if (!s->uploaded || s->vertex_count == 0) continue;

        g_mat.maps[MATERIAL_MAP_DIFFUSE].texture = (Texture2D){
            .id = s->texture_id,
            .width = 1, .height = 1, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
        };

        DrawMesh(s->rl_mesh, g_mat, transform);
    }
}

void r_draw_sky(Vector3 cam_pos, uint32_t bk, uint32_t dn, uint32_t ft, uint32_t lf, uint32_t rt, uint32_t up) {
    const float s = 4096.0f;

    rlDisableDepthMask();

    rlPushMatrix();
    rlTranslatef(cam_pos.x, cam_pos.y, cam_pos.z);

    rlSetTexture(ft);
    rlBegin(RL_TRIANGLES);
    rlColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    rlTexCoord2f(0.0f, 0.0f); rlVertex3f( s,  s,  s);
    rlTexCoord2f(1.0f, 0.0f); rlVertex3f( s,  s, -s);
    rlTexCoord2f(1.0f, 1.0f); rlVertex3f( s, -s, -s);
    rlTexCoord2f(0.0f, 0.0f); rlVertex3f( s,  s,  s);
    rlTexCoord2f(1.0f, 1.0f); rlVertex3f( s, -s, -s);
    rlTexCoord2f(0.0f, 1.0f); rlVertex3f( s, -s,  s);
    rlEnd();

    rlSetTexture(bk);
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
