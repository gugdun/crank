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
