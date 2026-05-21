#include "sys_fpcam.h"

#include "ecs/ecs.h"
#include "raylib.h"
#include "raymath.h"
#include "sjson.h"

#include <math.h>
#include <stdio.h>

static ecs_component_id g_c_transform = ECS_MAX_COMPONENTS;
static ecs_component_id g_c_camera    = ECS_MAX_COMPONENTS;
static ecs_component_id g_c_fpcam     = ECS_MAX_COMPONENTS;

static void read_c_transform(void *data, sjson_node *node) {
    c_transform *t = data;
    sjson_get_floats((float *)&t->position, 3, node, "position");
    t->yaw   = sjson_get_float(node, "yaw", 0.0f);
    t->pitch = sjson_get_float(node, "pitch", 0.0f);
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

static void apply_transform_to_camera(const c_transform *t, c_camera *cam) {
    float yaw_rad = t->yaw * DEG2RAD;
    float pitch_rad = t->pitch * DEG2RAD;
    float cp = cosf(pitch_rad);

    // yaw=0, pitch=0  ->  forward = (1, 0, 0) (matches original info_player_start angle code)
    // yaw rotates around +Y; positive pitch tilts the view down.
    Vector3 dir = {
        cosf(yaw_rad) * cp,
        -sinf(pitch_rad),
        -sinf(yaw_rad) * cp,
    };

    cam->rl_camera.position = t->position;
    cam->rl_camera.target   = Vector3Add(t->position, dir);
    cam->rl_camera.up       = (Vector3){0.0f, 1.0f, 0.0f};
    cam->rl_camera.fovy     = cam->fovy;
    cam->rl_camera.projection = cam->projection;
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

    c_transform *t = ecs_add(w, e, g_c_transform);
    c_camera    *cam = ecs_add(w, e, g_c_camera);
    c_fpcam     *fp  = ecs_add(w, e, g_c_fpcam);

    if (t == NULL || cam == NULL || fp == NULL) {
        ecs_destroy(w, e);
        return ECS_INVALID;
    }

    t->position = position;
    t->yaw      = yaw_deg;
    t->pitch    = 0.0f;

    cam->fovy = 90.0f;
    cam->projection = CAMERA_PERSPECTIVE;
    cam->active = 1;

    fp->run_speed   = 320.0f;
    fp->sensitivity = 1.0f;
    fp->m_yaw       = 0.022f;
    fp->m_pitch     = 0.022f;
    fp->pitch_clamp = 89.0f;

    apply_transform_to_camera(t, cam);
    return e;
}

void sys_fpcam_set_position(ecs_world *w, ecs_entity e, Vector3 position) {
    c_transform *t = ecs_get(w, e, g_c_transform);
    if (t == NULL) {
        printf("sys_fpcam_set_position: entity %u has no c_transform\n", e);
        return;
    }
    t->position = position;

    c_camera *cam = ecs_get(w, e, g_c_camera);
    if (cam != NULL) {
        apply_transform_to_camera(t, cam);
    }
}

void sys_fpcam_set_yaw(ecs_world *w, ecs_entity e, float yaw_deg) {
    c_transform *t = ecs_get(w, e, g_c_transform);
    if (t == NULL) {
        printf("sys_fpcam_set_yaw: entity %u has no c_transform\n", e);
        return;
    }
    t->yaw = yaw_deg;

    c_camera *cam = ecs_get(w, e, g_c_camera);
    if (cam != NULL) {
        apply_transform_to_camera(t, cam);
    }
}

void sys_fpcam_update(ecs_world *w, float dt) {
    if (w == NULL) {
        return;
    }

    (void)dt;

    // Global toggles (input-tied; lives with the camera controller).
    if (IsKeyPressed(KEY_F11)) {
        ToggleFullscreen();
    }

    Vector2 mouse_delta = GetMouseDelta();

    // Look only. Movement is owned by sys_player (which also overrides the
    // camera matrix with the player's eye position). Entities that have a
    // c_fpcam but no c_player still get their camera matrix rebuilt from
    // c_transform here, so the legacy "fly camera" entity still works.
    ecs_component_id c_player_id = ecs_lookup(w, "c_player");

    ecs_iter it = ecs_query(w, g_c_fpcam);
    ecs_entity e = ECS_INVALID;
    void *data = NULL;
    while (ecs_iter_next(&it, &e, &data)) {
        c_fpcam *fp = (c_fpcam *) data;
        c_transform *t = ecs_get(w, e, g_c_transform);
        c_camera *cam = ecs_get(w, e, g_c_camera);
        if (t == NULL || cam == NULL) {
            continue;
        }

        // Mouse look.
        t->yaw   -= mouse_delta.x * fp->sensitivity * fp->m_yaw;
        t->pitch += mouse_delta.y * fp->sensitivity * fp->m_pitch;
        if (t->pitch >  fp->pitch_clamp) t->pitch =  fp->pitch_clamp;
        if (t->pitch < -fp->pitch_clamp) t->pitch = -fp->pitch_clamp;

        // If this entity has no c_player, refresh the camera matrix from
        // c_transform here. Otherwise sys_player_update will do it with the
        // eye-height offset applied.
        int has_player = 0;
        if (c_player_id < ECS_MAX_COMPONENTS) {
            has_player = ecs_has(w, e, c_player_id);
        }
        if (!has_player) {
            apply_transform_to_camera(t, cam);
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
