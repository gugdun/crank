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

#ifndef SYS_MAP_H
#define SYS_MAP_H

#include "bsp.h"
#include "ecs/ecs.h"
#include "res/res_map.h"
#include "res/res_mesh.h"
#include "sjson.h"
#include "raylib.h"

typedef struct {
    map_handle map;
} c_map;

typedef struct {
    char sky_prefix[32];
} c_worldspawn;

typedef struct {
    int team;
    char targetname[64];
} c_spawn_point;

void       sys_map_register(ecs_world *w);
ecs_entity sys_map_spawn(ecs_world *w, map_handle h);

// Process every BSP entity as a JSON archetype.
// Loads entities/<classname>.json for each entity, applies origin/angle overrides.
// Returns number of entities spawned, or -1 on error.
int sys_map_process_entities(ecs_world *w,
                              const bsp_model *bsp,
                              sjson_context *sctx);

void sys_map_render(ecs_world *w,
                     res_map_mgr *mapmgr,
                     res_mesh_mgr *meshmgr,
                     Vector3 cam_pos);

#endif
