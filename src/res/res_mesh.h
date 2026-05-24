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

#ifndef RES_MESH_H
#define RES_MESH_H

#include "mesh.h"

#include <stdint.h>

typedef uint32_t mesh_handle;

typedef struct res_mesh_mgr res_mesh_mgr;

res_mesh_mgr *res_mesh_create(void);
void          res_mesh_destroy(res_mesh_mgr *m);

// Takes ownership of mesh_obj (must be from mesh_from_bsp or compatible).
// On failure, mesh_obj is freed and 0 is returned.
mesh_handle   res_mesh_adopt(res_mesh_mgr *m, mesh *mesh_obj);

// Returns a borrowed pointer; caller must not free.
const mesh   *res_mesh_get(const res_mesh_mgr *m, mesh_handle h);

#endif
