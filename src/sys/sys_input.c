#include "sys_input.h"

#include "ecs/ecs.h"
#include "raylib.h"

#include <stdio.h>
#include <string.h>

static ecs_component_id g_c_input = ECS_MAX_COMPONENTS;

typedef struct {
    int w, a, s, d;
    int space;
    int shift;
    int f;
    int f11;
} key_state;

static key_state g_prev = { 0 };
static key_state g_curr = { 0 };

static void read_keys(key_state* k) {
    k->w = IsKeyDown(KEY_W);
    k->a = IsKeyDown(KEY_A);
    k->s = IsKeyDown(KEY_S);
    k->d = IsKeyDown(KEY_D);

    k->space = IsKeyDown(KEY_SPACE);
    k->shift = IsKeyDown(KEY_LEFT_SHIFT);

    k->f = IsKeyDown(KEY_F);
    k->f11 = IsKeyDown(KEY_F11);
}

void sys_input_register(ecs_world* w) {
    if (w == NULL) {
        printf("sys_input_register: w = NULL\n");
        return;
    }

    g_c_input = ecs_register(
        w,
        "c_input",
        sizeof(c_input),
        NULL,
        NULL
    );
}

void sys_input_update(ecs_world* w) {
    if (w == NULL) return;

    // Shift states
    g_prev = g_curr;
    read_keys(&g_curr);

    // Compute derived input
    Vector2 mouse_delta = GetMouseDelta();

    float in_fwd = (float)g_curr.w - (float)g_curr.s;
    float in_rt = (float)g_curr.a - (float)g_curr.d;
    float in_up = (float)g_curr.space - (float)g_curr.shift;

    int jump_down = g_curr.space;
    int jump_pressed = g_curr.space && !g_prev.space;

    int noclip_down = g_curr.f;
    int noclip_pressed = g_curr.f && !g_prev.f;

    int fullscreen_down = g_curr.f11;
    int fullscreen_pressed = g_curr.f11 && !g_prev.f11;

    // Write ECS input components
    ecs_iter it = ecs_query(w, g_c_input);

    ecs_entity e;
    void* data;

    while (ecs_iter_next(&it, &e, &data)) {
        c_input* in = data;

        in->mouse_delta = mouse_delta;

        in->in_fwd = in_fwd;
        in->in_rt = in_rt;
        in->in_up = in_up;

        in->jump_down = jump_down;
        in->jump_pressed = jump_pressed;

        in->noclip_down = noclip_down;
        in->noclip_pressed = noclip_pressed;

        in->fullscreen_down = fullscreen_down;
        in->fullscreen_pressed = fullscreen_pressed;
    }
}
