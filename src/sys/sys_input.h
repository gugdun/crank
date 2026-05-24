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

#ifndef SYS_INPUT_H
#define SYS_INPUT_H

#include "ecs/ecs.h"
#include "raylib.h"

typedef struct {
    Vector2 mouse_delta;
} c_input;

void sys_input_register(ecs_world* w);
void sys_input_update(ecs_world* w);

#endif
