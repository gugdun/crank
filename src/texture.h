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

#ifndef TEXTURE_H
#define TEXTURE_H

#include "raylib.h"

#include <stdint.h>

typedef struct {
    uint32_t id;
    int width;
    int height;
    char *path;
    Texture2D rl_texture;
} texture;

texture *tex_load(const char *path);
void tex_free(texture *tex);

#endif
