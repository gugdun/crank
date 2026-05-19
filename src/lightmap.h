#ifndef LIGHTMAP_H
#define LIGHTMAP_H

#include "bsp.h"

#include <stdint.h>

// Quake II surface flags (subset relevant to lightmap exclusion)
#define SURF_LIGHT    0x1
#define SURF_SLICK    0x2
#define SURF_SKY      0x4
#define SURF_WARP     0x8
#define SURF_TRANS33  0x10
#define SURF_TRANS66  0x20
#define SURF_FLOWING  0x40
#define SURF_NODRAW   0x80

typedef struct {
    // Texture-space mins (snapped to 16) used to compute per-vertex luxel coords
    float s_min;
    float t_min;
    // Lightmap dims in luxels (1 if has_lightmap == 0)
    int lm_w;
    int lm_h;
    // Position in the atlas (in pixels/luxels)
    int atlas_x;
    int atlas_y;
    // 1 if the face has a valid lightmap, 0 -> use 1x1 white tile at (0,0)
    int has_lightmap;
} lm_face_info;

typedef struct {
    uint8_t *pixels;       // RGB atlas pixels (CPU-side)
    int width;             // atlas dims (in luxels/pixels)
    int height;
    lm_face_info *faces;   // size = bsp->num_faces
    uint32_t face_count;
} lm_atlas;

lm_atlas *lm_build(const bsp_model *bsp);
void lm_free(lm_atlas *atlas);

#endif
