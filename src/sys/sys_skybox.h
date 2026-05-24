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

#ifndef SYS_SKYBOX_H
#define SYS_SKYBOX_H

#include "ecs/ecs.h"
#include "res/res_texture.h"
#include "raylib.h"

typedef struct {
    tex_handle ft;
    tex_handle bk;
    tex_handle lf;
    tex_handle rt;
    tex_handle up;
    tex_handle dn;
    float      size;        // world units; default 4096
} c_skybox;

void       sys_skybox_register(ecs_world *w);
ecs_entity sys_skybox_spawn(ecs_world *w);
void       sys_skybox_set_sides(ecs_world *w,
                                ecs_entity e,
                                tex_handle ft,
                                tex_handle bk,
                                tex_handle lf,
                                tex_handle rt,
                                tex_handle up,
                                tex_handle dn);
void       sys_skybox_render(ecs_world *w,
                             const res_texture_mgr *texmgr,
                             Vector3 cam_pos);

#endif
