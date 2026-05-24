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

#ifndef RENDER_H
#define RENDER_H

#include "raylib.h"
#include "mesh.h"

#include <stdint.h>

void r_init(void);
void r_shutdown(void);

void r_draw_mesh(const mesh *m, Vector3 cam_pos);
void r_draw_sky(Vector3 cam_pos, uint32_t bk, uint32_t dn, uint32_t ft, uint32_t lf, uint32_t rt, uint32_t up);

#endif
