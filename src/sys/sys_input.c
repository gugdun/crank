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

#include "sys_input.h"

#include "ecs/ecs.h"
#include "raylib.h"

#include <stdio.h>
#include <string.h>

static ecs_component_id g_c_input = ECS_MAX_COMPONENTS;

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

    Vector2 mouse_delta = GetMouseDelta();

    ecs_iter it = ecs_query(w, g_c_input);
    ecs_entity e;
    void* data;
    while (ecs_iter_next(&it, &e, &data)) {
        c_input* in = data;
        in->mouse_delta = mouse_delta;
    }
}
