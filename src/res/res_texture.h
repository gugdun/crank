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

#ifndef RES_TEXTURE_H
#define RES_TEXTURE_H

#include "texture.h"

#include <stdint.h>

typedef uint32_t tex_handle;

typedef struct res_texture_mgr res_texture_mgr;

res_texture_mgr *res_texture_create(void);
void             res_texture_destroy(res_texture_mgr *m);

// Loads "<path>.png" (path is passed through to tex_load as-is).
// Returns a previously cached handle if the same path has been loaded.
// Returns 0 (invalid handle) on failure.
tex_handle       res_texture_load(res_texture_mgr *m, const char *path);

// Returns a borrowed pointer; caller must not free.
const texture   *res_texture_get(const res_texture_mgr *m, tex_handle h);

#endif
