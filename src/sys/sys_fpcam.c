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

    // Global toggles (input-tied; lives with the player controller for now).
    if (IsKeyPressed(KEY_F11)) {
        ToggleFullscreen();
    }

    int forward  = IsKeyDown(KEY_W);
    int backward = IsKeyDown(KEY_S);
    int left     = IsKeyDown(KEY_A);
    int right    = IsKeyDown(KEY_D);
    int up       = IsKeyDown(KEY_SPACE);
    int down     = IsKeyDown(KEY_LEFT_SHIFT);

    Vector2 mouse_delta = GetMouseDelta();

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

        // Mouse look
        t->yaw   -= mouse_delta.x * fp->sensitivity * fp->m_yaw;
        t->pitch += mouse_delta.y * fp->sensitivity * fp->m_pitch;
        if (t->pitch >  fp->pitch_clamp) t->pitch =  fp->pitch_clamp;
        if (t->pitch < -fp->pitch_clamp) t->pitch = -fp->pitch_clamp;

        // Movement vectors in world space.
        // Forward (xz only) follows yaw; up/down is world axis.
        float yaw_rad = t->yaw * DEG2RAD;
        Vector3 fwd_xz = { cosf(yaw_rad), 0.0f, -sinf(yaw_rad) };
        Vector3 right_xz = { -sinf(yaw_rad), 0.0f, -cosf(yaw_rad) };
        Vector3 world_up = { 0.0f, 1.0f, 0.0f };

        float move_fwd = ((float)forward - (float)backward) * fp->run_speed * dt;
        float move_rt  = ((float)right   - (float)left)     * fp->run_speed * dt;
        float move_up  = ((float)up      - (float)down)     * fp->run_speed * dt;

        t->position = Vector3Add(t->position, Vector3Scale(fwd_xz,  move_fwd));
        t->position = Vector3Add(t->position, Vector3Scale(right_xz, move_rt));
        t->position = Vector3Add(t->position, Vector3Scale(world_up, move_up));

        apply_transform_to_camera(t, cam);
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
