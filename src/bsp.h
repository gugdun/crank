#ifndef BSP_H
#define BSP_H

#include <stdint.h>

#define BSP_MAGIC        0x50534249
#define BSP_VERSION      38

#define BSP_ENTITIES     0
#define BSP_PLANES       1
#define BSP_VERTICES     2
#define BSP_VISIBILITY   3
#define BSP_NODES        4
#define BSP_TEXTURES     5
#define BSP_FACES        6
#define BSP_LIGHTMAPS    7
#define BSP_LEAVES       8
#define BSP_LEAF_FACES   9
#define BSP_LEAF_BRUSHES 10
#define BSP_EDGES        11
#define BSP_FACE_EDGES   12
#define BSP_MODELS       13
#define BSP_BRUSHES      14
#define BSP_BRUSH_SIDES  15
#define BSP_POP          16
#define BSP_AREAS        17
#define BSP_AREA_PORTALS 18

typedef struct {
    float x;
    float y;
    float z;
} __attribute__((packed)) point3f;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} __attribute__((packed)) point3s;

typedef struct {
    uint32_t offset;
    uint32_t length;
} __attribute__((packed)) bsp_lump;

typedef struct {
    uint32_t magic;
    uint32_t version;
    bsp_lump lump[19];
} __attribute__((packed)) bsp_header;

typedef struct {
    uint16_t v1;
    uint16_t v2;
} __attribute__((packed)) bsp_edge;

typedef struct {
    uint16_t plane;
    uint16_t plane_side;
    uint32_t first_edge;
    uint16_t num_edges;
    uint16_t texture_info;
    uint8_t lightmap_styles[4];
    uint32_t lightmap_offset;
} __attribute__((packed)) bsp_face;

typedef struct {
    point3f normal;
    float distance;
    uint32_t type;
} __attribute__((packed)) bsp_plane;

typedef struct {
    uint32_t plane;
    int32_t front_child;
    int32_t back_child;
    point3s bbox_min;
    point3s bbox_max;
    uint16_t first_face;
    uint16_t num_faces;
} __attribute__((packed)) bsp_node;

typedef struct {
    uint32_t brush_or;
    uint16_t cluster;
    uint16_t area;
    point3s bbox_min;
    point3s bbox_max;
    uint16_t first_leaf_face;
    uint16_t num_leaf_faces;
    uint16_t first_leaf_brush;
    uint16_t num_leaf_brushes;
} __attribute__((packed)) bsp_leaf;

typedef struct {
    point3f u_axis;
    float u_offset;
    point3f v_axis;
    float v_offset;
    uint32_t flags;
    uint32_t value;
    char texture_name[32];
    uint32_t next_texinfo;
} __attribute__((packed)) bsp_texinfo;

typedef struct {
    uint32_t pvs;
    uint32_t phs;
} __attribute__((packed)) bsp_vis_offset;

typedef struct {
    char name[32];
    uint32_t width;
    uint32_t height;
    int32_t offset[4];
    char next_name[32];
    uint32_t flags;
    uint32_t contents;
    uint32_t value;
} __attribute__((packed)) wal_header;

typedef struct {
    char *key;
    char *value;
} bsp_entity_prop;

typedef struct {
    bsp_entity_prop *props;
    uint32_t num_props;
} bsp_entity;

typedef struct {
    point3f bbox_min;
    point3f bbox_max;
    point3f origin;
    int32_t head_node;
    int32_t first_face;
    int32_t num_faces;
} __attribute__((packed)) bsp_model_lump;

typedef struct {
    uint32_t first_side;
    uint32_t num_sides;
    uint32_t contents;
} __attribute__((packed)) bsp_brush;

typedef struct {
    uint16_t plane;
    uint16_t texinfo;       // 0xFFFF = no texture
} __attribute__((packed)) bsp_brush_side;

typedef struct {
    bsp_header header;

    point3f *vertices;
    bsp_edge *edges;
    int32_t *face_edges;
    bsp_face *faces;
    bsp_texinfo *texinfo;
    bsp_entity *entities;
    uint8_t *lightmaps;

    bsp_plane *planes;
    bsp_node *nodes;
    bsp_leaf *leaves;
    uint16_t *leaf_faces;
    bsp_model_lump *models;
    uint8_t *visibility;

    bsp_brush *brushes;
    bsp_brush_side *brush_sides;
    uint16_t *leaf_brushes;

    uint32_t num_vertices;
    uint32_t num_edges;
    uint32_t num_face_edges;
    uint32_t num_faces;
    uint32_t num_texinfo;
    uint32_t num_entities;
    uint32_t lightmaps_size;

    uint32_t num_planes;
    uint32_t num_nodes;
    uint32_t num_leaves;
    uint32_t num_leaf_faces;
    uint32_t num_models;
    uint32_t visibility_size;
    uint32_t num_clusters;

    uint32_t num_brushes;
    uint32_t num_brush_sides;
    uint32_t num_leaf_brushes;
} bsp_model;

const char *bsp_entity_get(const bsp_entity *e, const char *key);
bsp_model *bsp_load(const char *path);
void bsp_free(bsp_model *bsp);

// Decompress the PVS bit vector for `cluster` into `out`.
// `out` must have at least ((num_clusters + 7) / 8) bytes.
// Returns 1 on success; 0 if there is no visibility data or cluster is invalid.
int bsp_decompress_pvs(const bsp_model *bsp, int32_t cluster, uint8_t *out);

// Walk the BSP tree to find the leaf containing `point` (in BSP space).
// Returns leaf index or -1 if the BSP has no nodes/leaves.
int32_t bsp_find_leaf(const bsp_model *bsp, point3f point);

#endif
