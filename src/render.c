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

#include "render.h"
#include "rlgl.h"
#include "raymath.h"

#include <stdio.h>
#include <stdlib.h>

// raylib's rlDrawVertexArrayElements draws with GL_UNSIGNED_SHORT, which
// caps the vertex buffer at 65k vertices. Quake II world meshes routinely
// exceed that after triangle-fanning, so we draw via glDrawElements directly
// with GL_UNSIGNED_INT. The OpenGL library is already linked transitively
// through raylib.
typedef unsigned int   r_glenum;
typedef int            r_glsizei;
extern void glDrawElements(r_glenum mode, r_glsizei count, r_glenum type, const void *indices);
#define R_GL_TRIANGLES      0x0004
#define R_GL_UNSIGNED_INT   0x1405

static Shader g_lightmap_shader;
static int g_lightmap_shader_loaded = 0;
static int g_loc_light_scale = -1;
static int g_loc_surface_alpha = -1;

// Scratch buffers for transparent-surface centroid sort
static uint32_t *g_trans_order = NULL;
static float *g_trans_dist = NULL;
static uint32_t g_trans_cap = 0;

#define DEFAULT_LIGHT_SCALE 2.0f

void r_init(void) {
    if (g_lightmap_shader_loaded) return;

    g_lightmap_shader = LoadShader("shaders/lightmap.vs", "shaders/lightmap.fs");

    if (g_lightmap_shader.id == 0) {
        printf("render: failed to load lightmap shader\n");
        return;
    }

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
    if (o == NULL) {
        printf("render: failed to allocate trans order scratch\n");
        return 0;
    }
    g_trans_order = o;
    float *d = realloc(g_trans_dist, n * sizeof(float));
    if (d == NULL) {
        printf("render: failed to allocate trans dist scratch\n");
        return 0;
    }
    g_trans_dist = d;
    g_trans_cap = n;
    return 1;
}

// Bind the shared VBO/VAO and set vertex attribute pointers for our shader.
// Mirrors what DrawMesh does internally for positions/texcoords/texcoords2.
static int bind_shared_attribs(const mesh *m) {
    if (!m->uploaded) return 0;
    const Mesh *rm = &m->rl_mesh;
    if (rm->vaoId == 0) {
        // No VAO support; fall back to manual VBO binds.
        int loc_pos = g_lightmap_shader.locs[SHADER_LOC_VERTEX_POSITION];
        int loc_tc0 = g_lightmap_shader.locs[SHADER_LOC_VERTEX_TEXCOORD01];
        int loc_tc1 = g_lightmap_shader.locs[SHADER_LOC_VERTEX_TEXCOORD02];

        if (loc_pos != -1) {
            rlEnableVertexBuffer(rm->vboId[0]);
            rlSetVertexAttribute((uint32_t) loc_pos, 3, RL_FLOAT, 0, 0, 0);
            rlEnableVertexAttribute((uint32_t) loc_pos);
        }
        if (loc_tc0 != -1) {
            rlEnableVertexBuffer(rm->vboId[1]);
            rlSetVertexAttribute((uint32_t) loc_tc0, 2, RL_FLOAT, 0, 0, 0);
            rlEnableVertexAttribute((uint32_t) loc_tc0);
        }
        if (loc_tc1 != -1) {
            // raylib stores texcoords2 at vboId index 5 (RL_DEFAULT_SHADER_ATTRIB_LOCATION_TEXCOORD2).
            rlEnableVertexBuffer(rm->vboId[5]);
            rlSetVertexAttribute((uint32_t) loc_tc1, 2, RL_FLOAT, 0, 0, 0);
            rlEnableVertexAttribute((uint32_t) loc_tc1);
        }
    } else {
        rlEnableVertexArray(rm->vaoId);
    }
    return 1;
}

static void unbind_shared_attribs(void) {
    rlDisableVertexArray();
    rlDisableVertexBuffer();
    rlDisableVertexBufferElement();
}

static void upload_matrices(void) {
    // Mirror what DrawMesh does: compute and upload the MVP. We don't apply
    // any model transform (identity), so MVP = projection * modelview.
    Matrix matView       = rlGetMatrixModelview();
    Matrix matProjection = rlGetMatrixProjection();
    Matrix matModel      = MatrixIdentity();
    Matrix matModelView  = MatrixMultiply(matModel, matView);
    Matrix matMVP        = MatrixMultiply(matModelView, matProjection);

    int loc_mvp = g_lightmap_shader.locs[SHADER_LOC_MATRIX_MVP];
    if (loc_mvp != -1) {
        rlSetUniformMatrix(loc_mvp, matMVP);
    }
}

static void draw_surface(mesh_surface *s, Texture2D lightmap_atlas, int has_lightmap) {
    if (s->ibo_id == 0 || s->frame_index_count == 0) return;

    // Bind diffuse to slot 0.
    rlActiveTextureSlot(0);
    rlEnableTexture(s->texture_id);
    int loc_diffuse = g_lightmap_shader.locs[SHADER_LOC_MAP_DIFFUSE];
    if (loc_diffuse != -1) {
        int slot = 0;
        rlSetUniform(loc_diffuse, &slot, SHADER_UNIFORM_INT, 1);
    }

    // Bind lightmap to slot 1.
    if (has_lightmap) {
        rlActiveTextureSlot(1);
        rlEnableTexture(lightmap_atlas.id);
        int loc_specular = g_lightmap_shader.locs[SHADER_LOC_MAP_SPECULAR];
        if (loc_specular != -1) {
            int slot = 1;
            rlSetUniform(loc_specular, &slot, SHADER_UNIFORM_INT, 1);
        }
    }

    // Bind IBO and draw. We use glDrawElements directly because raylib's
    // rlDrawVertexArrayElements hard-codes GL_UNSIGNED_SHORT.
    rlEnableVertexBufferElement(s->ibo_id);
    glDrawElements(R_GL_TRIANGLES, (r_glsizei) s->frame_index_count, R_GL_UNSIGNED_INT, 0);
}

static void draw_trans_sorted(const mesh *m, Vector3 cam_pos) {
    uint32_t n = m->trans_surface_count;
    if (n == 0) return;
    if (!ensure_trans_scratch(n)) return;

    // Filter visible surfaces and compute distances.
    uint32_t live = 0;
    for (uint32_t i = 0; i < n; i++) {
        const mesh_surface *s = &m->trans_surfaces[i];
        if (s->ibo_id == 0 || s->frame_index_count == 0) continue;
        Vector3 d = Vector3Subtract(s->frame_centroid, cam_pos);
        g_trans_dist[live] = d.x * d.x + d.y * d.y + d.z * d.z;
        g_trans_order[live] = i;
        live++;
    }
    if (live == 0) return;

    // Insertion sort by descending distance (back-to-front).
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

    // NOTE: do not call rlDrawRenderBatchActive() here. We have already
    // flushed at the top of r_draw_mesh and the only thing it would do now
    // is unbind our shader and VAO (see rlDrawRenderBatch in rlgl.h: it
    // unconditionally calls glUseProgram(0) and glBindVertexArray(0) at the
    // end, regardless of whether any batch geometry was actually drawn).
    rlEnableColorBlend();
    rlSetBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();

    for (uint32_t k = 0; k < live; k++) {
        mesh_surface *s = &m->trans_surfaces[g_trans_order[k]];
        set_surface_alpha(s->alpha);
        draw_surface(s, m->lightmap_atlas, m->has_lightmap_atlas);
    }

    rlEnableDepthMask();
}

void r_draw_mesh(const mesh *m, Vector3 cam_pos) {
    if (m == NULL) {
        printf("render: mesh = NULL\n");
        return;
    }
    if (!g_lightmap_shader_loaded) {
        printf("render: r_init was not called\n");
        return;
    }
    if (!m->uploaded) {
        return;
    }

    // Flush the default raylib batch so our state doesn't interfere.
    rlDrawRenderBatchActive();

    rlEnableShader(g_lightmap_shader.id);
    if (!bind_shared_attribs(m)) {
        rlDisableShader();
        return;
    }
    upload_matrices();

    // ---- Opaque pass ----
    set_surface_alpha(1.0f);
    for (uint32_t i = 0; i < m->surface_count; i++) {
        draw_surface(&m->surfaces[i], m->lightmap_atlas, m->has_lightmap_atlas);
    }

    // ---- Transparent pass ----
    draw_trans_sorted(m, cam_pos);

    // Cleanup
    rlActiveTextureSlot(1);
    rlDisableTexture();
    rlActiveTextureSlot(0);
    rlDisableTexture();
    unbind_shared_attribs();
    rlDisableShader();
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
