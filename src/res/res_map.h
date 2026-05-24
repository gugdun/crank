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

#ifndef RES_MAP_H
#define RES_MAP_H

#include "bsp.h"
#include "phys.h"
#include "res_mesh.h"
#include "vis.h"

#include <stdint.h>

typedef uint32_t map_handle;

typedef struct res_map_mgr res_map_mgr;

typedef struct {
    const bsp_model *bsp;       // borrowed
    mesh_handle      mesh;      // resolves via the mesh manager passed to create
    vis_state       *vis;       // borrowed; per-frame visibility state
    phys_world      *phys;      // borrowed; static collision world
    const char      *name;      // borrowed (manager-owned copy)
} map_view;

res_map_mgr *res_map_create(res_mesh_mgr *meshes);
void         res_map_destroy(res_map_mgr *m);

// Loads "maps/<name>.bsp" (bsp_load handles the prefix), builds the world mesh,
// registers it with the mesh manager. Returns 0 on failure.
map_handle   res_map_load(res_map_mgr *m, const char *name);

// Fills *out with a borrowed view of the map. Returns 1 on success, 0 on failure.
int          res_map_get(const res_map_mgr *m, map_handle h, map_view *out);

#endif
