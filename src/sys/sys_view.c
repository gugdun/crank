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

#include "sys_view.h"

#include "sys_fpcam.h"
#include "sys_input.h"

#include "ecs/ecs.h"
#include "sjson.h"
#include "raylib.h"
#include "raymath.h"

#include <math.h>
#include <stdio.h>

static ecs_component_id g_c_view = ECS_MAX_COMPONENTS;

static void read_c_view(void *data, sjson_node *node) {
    c_view *v = data;
    v->yaw        = sjson_get_float(node, "yaw",        0.0f);
    v->pitch      = sjson_get_float(node, "pitch",      0.0f);
    v->eye_height = sjson_get_float(node, "eye_height", 24.0f);
}

void sys_view_register(ecs_world *w) {
    if (w == NULL) {
        printf("sys_view_register: w = NULL\n");
        return;
    }
    g_c_view = ecs_register(w, "c_view", sizeof(c_view), NULL, read_c_view);
}

// Build the Camera matrix for an eye position + view orientation. This is
// the single canonical site for this conversion. yaw=0, pitch=0 looks
// along +X (matches the original info_player_start convention).
static void apply_view_to_camera(const c_view *v, Vector3 eye, c_camera *cam) {
    float yaw_rad = v->yaw * DEG2RAD;
    float pitch_rad = v->pitch * DEG2RAD;
    float cp = cosf(pitch_rad);

    Vector3 dir = {
        cosf(yaw_rad) * cp,
        -sinf(pitch_rad),
        -sinf(yaw_rad) * cp,
    };

    cam->rl_camera.position   = eye;
    cam->rl_camera.target     = Vector3Add(eye, dir);
    cam->rl_camera.up         = (Vector3){0.0f, 1.0f, 0.0f};
    cam->rl_camera.fovy       = cam->fovy;
    cam->rl_camera.projection = cam->projection;
}

void sys_view_look(ecs_world *w) {
    if (w == NULL) return;

    // Global toggles (input-tied; lives with the view controller).
    if (IsKeyPressed(KEY_F11)) {
        ToggleFullscreen();
    }

    ecs_component_id c_input_id = ecs_lookup(w, "c_input");
    ecs_component_id c_fpcam_id = ecs_lookup(w, "c_fpcam");
    if (c_input_id >= ECS_MAX_COMPONENTS) return;

    ecs_iter it = ecs_query(w, g_c_view);
    ecs_entity e = ECS_INVALID;
    void *data = NULL;
    while (ecs_iter_next(&it, &e, &data)) {
        c_view *v = (c_view *) data;
        c_input *in = ecs_get(w, e, c_input_id);
        if (in == NULL) continue;

        float sensitivity = 1.0f;
        float m_yaw       = 0.022f;
        float m_pitch     = 0.022f;
        float pitch_clamp = 89.0f;

        if (c_fpcam_id < ECS_MAX_COMPONENTS) {
            c_fpcam *fp = ecs_get(w, e, c_fpcam_id);
            if (fp != NULL) {
                sensitivity = fp->sensitivity;
                m_yaw       = fp->m_yaw;
                m_pitch     = fp->m_pitch;
                pitch_clamp = fp->pitch_clamp;
            }
        }

        v->yaw   -= in->mouse_delta.x * sensitivity * m_yaw;
        v->pitch += in->mouse_delta.y * sensitivity * m_pitch;
        if (v->pitch >  pitch_clamp) v->pitch =  pitch_clamp;
        if (v->pitch < -pitch_clamp) v->pitch = -pitch_clamp;
    }
}

void sys_view_update(ecs_world *w, float alpha) {
    if (w == NULL) return;

    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;

    ecs_component_id c_transform_id = ecs_lookup(w, "c_transform");
    ecs_component_id c_camera_id    = ecs_lookup(w, "c_camera");
    if (c_transform_id >= ECS_MAX_COMPONENTS) return;
    if (c_camera_id    >= ECS_MAX_COMPONENTS) return;

    ecs_iter it = ecs_query(w, g_c_view);
    ecs_entity e = ECS_INVALID;
    void *data = NULL;
    while (ecs_iter_next(&it, &e, &data)) {
        c_view *v = (c_view *) data;
        c_transform *t = ecs_get(w, e, c_transform_id);
        c_camera *cam = ecs_get(w, e, c_camera_id);
        if (t == NULL || cam == NULL) continue;

        Vector3 pos = Vector3Lerp(t->prev_position, t->position, alpha);
        Vector3 eye;
        eye.x = pos.x;
        eye.y = pos.y + v->eye_height;
        eye.z = pos.z;

        apply_view_to_camera(v, eye, cam);
    }
}
