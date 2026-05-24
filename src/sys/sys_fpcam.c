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

#include "sys_fpcam.h"
#include "sys_input.h"
#include "sys_view.h"

#include "ecs/ecs.h"
#include "raylib.h"
#include "raymath.h"
#include "sjson.h"

#include <stdio.h>

static ecs_component_id g_c_transform = ECS_MAX_COMPONENTS;
static ecs_component_id g_c_camera    = ECS_MAX_COMPONENTS;
static ecs_component_id g_c_fpcam     = ECS_MAX_COMPONENTS;

static void read_c_transform(void *data, sjson_node *node) {
    c_transform *t = data;
    sjson_get_floats((float *)&t->position, 3, node, "position");
    t->yaw   = sjson_get_float(node, "yaw", 0.0f);
    t->pitch = sjson_get_float(node, "pitch", 0.0f);
    // Seed interpolation history to the spawn pose so the first rendered
    // frame doesn't lerp from (0, 0, 0). main.c re-seeds these again
    // after applying BSP origin/angle and spawn-point placement.
    t->prev_position = t->position;
    t->prev_yaw      = t->yaw;
    t->prev_pitch    = t->pitch;
}

static void read_c_camera(void *data, sjson_node *node) {
    c_camera *cam = data;
    cam->fovy = sjson_get_float(node, "fovy", 90.0f);
    cam->projection = sjson_get_int(node, "projection", CAMERA_PERSPECTIVE);
    cam->active = sjson_get_bool(node, "active", true);
}

static void read_c_fpcam(void *data, sjson_node *node) {
    c_fpcam *fp = data;
    fp->run_speed   = sjson_get_float(node, "run_speed", 320.0f);
    fp->sensitivity = sjson_get_float(node, "sensitivity", 1.0f);
    fp->m_yaw       = sjson_get_float(node, "m_yaw", 0.022f);
    fp->m_pitch     = sjson_get_float(node, "m_pitch", 0.022f);
    fp->pitch_clamp = sjson_get_float(node, "pitch_clamp", 89.0f);
}

void sys_fpcam_register(ecs_world *w) {
    if (w == NULL) {
        printf("sys_fpcam_register: w = NULL\n");
        return;
    }
    g_c_transform = ecs_register(w, "c_transform", sizeof(c_transform), NULL, read_c_transform);
    g_c_camera    = ecs_register(w, "c_camera",    sizeof(c_camera),    NULL, read_c_camera);
    g_c_fpcam     = ecs_register(w, "c_fpcam",     sizeof(c_fpcam),     NULL, read_c_fpcam);
}

ecs_entity sys_fpcam_spawn(ecs_world *w, Vector3 position, float yaw_deg) {
    if (w == NULL) {
        printf("sys_fpcam_spawn: w = NULL\n");
        return ECS_INVALID;
    }

    ecs_entity e = ecs_create(w);
    if (e == ECS_INVALID) {
        return ECS_INVALID;
    }

    ecs_component_id c_view_id    = ecs_lookup(w, "c_view");
    ecs_component_id c_input_id   = ecs_lookup(w, "c_input");
    ecs_component_id c_usercmd_id = ecs_lookup(w, "c_usercmd_queue");

    c_transform *t   = ecs_add(w, e, g_c_transform);
    c_camera    *cam = ecs_add(w, e, g_c_camera);
    c_fpcam     *fp  = ecs_add(w, e, g_c_fpcam);
    c_input     *in  = (c_input_id  < ECS_MAX_COMPONENTS) ? ecs_add(w, e, c_input_id)  : NULL;
    void        *q   = (c_usercmd_id < ECS_MAX_COMPONENTS) ? ecs_add(w, e, c_usercmd_id) : NULL;
    void        *v   = (c_view_id   < ECS_MAX_COMPONENTS) ? ecs_add(w, e, c_view_id)   : NULL;

    if (t == NULL || cam == NULL || fp == NULL || in == NULL || q == NULL || v == NULL) {
        ecs_destroy(w, e);
        return ECS_INVALID;
    }

    t->position      = position;
    t->yaw           = yaw_deg;
    t->pitch         = 0.0f;
    t->prev_position = position;
    t->prev_yaw      = yaw_deg;
    t->prev_pitch    = 0.0f;

    cam->fovy = 90.0f;
    cam->projection = CAMERA_PERSPECTIVE;
    cam->active = 1;

    fp->run_speed   = 320.0f;
    fp->sensitivity = 1.0f;
    fp->m_yaw       = 0.022f;
    fp->m_pitch     = 0.022f;
    fp->pitch_clamp = 89.0f;

    {
        c_view *cv = (c_view *) v;
        cv->yaw        = yaw_deg;
        cv->pitch      = 0.0f;
        cv->eye_height = 0.0f; // fly-cam default; player archetype overrides
    }

    // sys_view_update will fill cam->rl_camera on the first frame.
    return e;
}

void sys_fpcam_set_position(ecs_world *w, ecs_entity e, Vector3 position) {
    c_transform *t = ecs_get(w, e, g_c_transform);
    if (t == NULL) {
        printf("sys_fpcam_set_position: entity %u has no c_transform\n", e);
        return;
    }
    t->position      = position;
    t->prev_position = position;
}

void sys_fpcam_set_yaw(ecs_world *w, ecs_entity e, float yaw_deg) {
    c_transform *t = ecs_get(w, e, g_c_transform);
    if (t == NULL) {
        printf("sys_fpcam_set_yaw: entity %u has no c_transform\n", e);
        return;
    }
    t->yaw      = yaw_deg;
    t->prev_yaw = yaw_deg;

    // Keep c_view.yaw in sync if present, so the next sys_view_update
    // renders the spawn orientation rather than the pre-spawn one.
    ecs_component_id c_view_id = ecs_lookup(w, "c_view");
    if (c_view_id < ECS_MAX_COMPONENTS) {
        c_view *v = ecs_get(w, e, c_view_id);
        if (v != NULL) {
            v->yaw = yaw_deg;
        }
    }
}

Camera sys_fpcam_active(ecs_world *w) {
    Camera fallback = {0};
    fallback.up = (Vector3){0.0f, 1.0f, 0.0f};
    fallback.fovy = 90.0f;
    fallback.projection = CAMERA_PERSPECTIVE;

    if (w == NULL) {
        return fallback;
    }

    ecs_iter it = ecs_query(w, g_c_camera);
    ecs_entity e = ECS_INVALID;
    void *data = NULL;
    while (ecs_iter_next(&it, &e, &data)) {
        c_camera *cam = (c_camera *) data;
        if (cam->active) {
            return cam->rl_camera;
        }
    }
    return fallback;
}
