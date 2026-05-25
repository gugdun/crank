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

#include "sys_usercmd.h"

#include "ecs/ecs.h"
#include "sys_fpcam.h"
#include "sys_view.h"
#include "raylib.h"

#include <stdio.h>
#include <string.h>

static ecs_component_id g_c_usercmd_queue = ECS_MAX_COMPONENTS;

void sys_usercmd_register(ecs_world *w) {
    if (w == NULL) {
        printf("sys_usercmd_register: w = NULL\n");
        return;
    }

    g_c_usercmd_queue = ecs_register(
        w,
        "c_usercmd_queue",
        sizeof(c_usercmd_queue),
        NULL,
        NULL
    );
}

void sys_usercmd_accumulate(ecs_world *w, float frame_dt) {
    (void)frame_dt;

    if (w == NULL) return;

    ecs_iter it = ecs_query(w, g_c_usercmd_queue);
    ecs_entity e;
    void *data;
    while (ecs_iter_next(&it, &e, &data)) {
        c_usercmd_queue *q = data;

        q->pending.in_fwd = (float)IsKeyDown(KEY_W) - (float)IsKeyDown(KEY_S);
        q->pending.in_rt  = (float)IsKeyDown(KEY_A) - (float)IsKeyDown(KEY_D);
        q->pending.in_up  = (float)IsKeyDown(KEY_SPACE) - (float)IsKeyDown(KEY_LEFT_SHIFT);

        if (IsKeyPressed(KEY_SPACE) || GetMouseWheelMove()) q->pending_buttons |= CMD_BUTTON_JUMP;
        if (IsKeyPressed(KEY_F)) q->pending_buttons |= CMD_BUTTON_NOCLIP;
    }
}

void sys_usercmd_finalize(ecs_world *w, float slice_dt) {
    if (w == NULL) return;

    ecs_component_id c_transform_id = ecs_lookup(w, "c_transform");
    ecs_component_id c_view_id      = ecs_lookup(w, "c_view");

    ecs_iter it = ecs_query(w, g_c_usercmd_queue);
    ecs_entity e;
    void *data;
    while (ecs_iter_next(&it, &e, &data)) {
        c_usercmd_queue *q = data;

        c_usercmd cmd = q->pending;
        cmd.buttons = q->pending_buttons;
        cmd.dt_sec  = slice_dt;

        // Prefer c_view.yaw/pitch (frame-rate look) so the physics step
        // uses the most recent mouse-look orientation. Fall back to
        // c_transform.yaw/pitch for legacy entities that don't carry a
        // c_view.
        int view_set = 0;
        if (c_view_id < ECS_MAX_COMPONENTS) {
            c_view *v = ecs_get(w, e, c_view_id);
            if (v != NULL) {
                cmd.yaw   = v->yaw;
                cmd.pitch = v->pitch;
                view_set  = 1;
            }
        }
        if (!view_set && c_transform_id < ECS_MAX_COMPONENTS) {
            c_transform *t = ecs_get(w, e, c_transform_id);
            if (t != NULL) {
                cmd.yaw   = t->yaw;
                cmd.pitch = t->pitch;
            }
        }

        q->current      = cmd;
        q->has_current  = 1;

        q->pending_buttons = 0;
        // axes are overwritten each accumulate, so no explicit clear needed.
    }
}

int sys_usercmd_consume(c_usercmd_queue *q, c_usercmd *out) {
    if (q == NULL || out == NULL) return 0;

    if (q->has_current) {
        *out = q->current;
        q->last = *out;
        q->has_current = 0;
        return 1;
    }

    return 0;
}
