#ifndef MESH_H
#define MESH_H

#include "bsp.h"
#include "texture.h"
#include "raylib.h"

#include <stdint.h>

typedef struct {
    float x, y, z;
    float u, v;     // diffuse texcoord
    float lu, lv;   // lightmap atlas texcoord (0..1)
} mesh_vertex;

typedef struct {
    mesh_vertex *vertices;
    uint32_t vertex_count;
    uint32_t texture_id;
    Mesh rl_mesh;       // GPU-side raylib mesh (uploaded)
    int uploaded;       // 1 if rl_mesh contains a live GL buffer

    float alpha;        // 1.0 opaque, 0.33 TRANS33, 0.66 TRANS66
    Vector3 centroid;   // mean vertex position (raylib space), for transparency sort
} mesh_surface;

typedef struct {
    mesh_surface *surfaces;
    texture *textures;
    uint32_t surface_count;
    uint32_t texture_count;

    Texture2D lightmap_atlas;
    uint32_t lightmap_id;
    int has_lightmap_atlas;

    mesh_surface *trans_surfaces;    // transparent surfaces (sorted per-frame by centroid)
    uint32_t trans_surface_count;
} mesh;

mesh *mesh_from_bsp(const bsp_model *bsp);
void mesh_free(mesh *m);

#endif
