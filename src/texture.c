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

#include "raylib.h"
#include "texture.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

texture *tex_load(const char *path) {
    size_t path_len = strlen(path);
    texture *tex = calloc(1, sizeof(texture));

    char *path_png = calloc(1, path_len + 5);
    strcpy(path_png, path);
    strcat(path_png, ".png");

    tex->rl_texture = LoadTexture(path_png);
    if (!IsTextureValid(tex->rl_texture)) {
        printf("tex_load: failed to load texture %s\n", path_png);
        free(path_png);
        free(tex);
        return NULL;
    }

    SetTextureWrap(tex->rl_texture, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(tex->rl_texture, TEXTURE_FILTER_BILINEAR);
    GenTextureMipmaps(&tex->rl_texture);

    tex->id = tex->rl_texture.id;
    tex->width = tex->rl_texture.width;
    tex->height = tex->rl_texture.height;
    tex->path = calloc(1, path_len + 1);
    strcpy(tex->path, path);

    free(path_png);
    return tex;
}

void tex_free(texture *tex) {
    UnloadTexture(tex->rl_texture);
    free(tex->path);
    free(tex);
}
