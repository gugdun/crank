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

#ifndef SYS_USERCMD_H
#define SYS_USERCMD_H

#include "ecs/ecs.h"

#include <stdint.h>

#define CMD_BUTTON_JUMP   (1u << 0)
#define CMD_BUTTON_NOCLIP (1u << 1)

typedef struct {
    float    in_fwd;
    float    in_rt;
    float    in_up;
    float    yaw;
    float    pitch;
    uint16_t buttons;
    float    dt_sec;
} c_usercmd;

typedef struct {
    c_usercmd current;
    int       has_current;
    c_usercmd last;
    // builder
    c_usercmd  pending;
    uint16_t   pending_buttons;
} c_usercmd_queue;

void sys_usercmd_register(ecs_world *w);
void sys_usercmd_accumulate(ecs_world *w, float frame_dt);
void sys_usercmd_finalize(ecs_world *w, float slice_dt);
int  sys_usercmd_consume(c_usercmd_queue *q, c_usercmd *out);

#endif
