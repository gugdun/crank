#include "bsp.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static bsp_header bsp_read_header(FILE *bsp_file) {
    bsp_header header = {0};

    if (bsp_file == NULL) {
        printf("bsp_read_header: bsp_file = NULL\n");
        return (bsp_header){0};
    }

    int result = fseek(bsp_file, 0, SEEK_END);
    if (result != 0) {
        printf("bsp_read_header: fseek error %d\n", result);
        return (bsp_header){0};
    }

    long bsp_size = ftell(bsp_file);
    if (bsp_size < 0) {
        printf("bsp_read_header: ftell error\n");
        return (bsp_header){0};
    }
    printf("bsp_read_header: bsp_size = %ld bytes\n", bsp_size);

    result = fseek(bsp_file, 0, SEEK_SET);
    if (result != 0) {
        printf("bsp_read_header: fseek error %d\n", result);
        return (bsp_header){0};
    }

    size_t header_size = sizeof(bsp_header);
    if (header_size > bsp_size) {
        printf("bsp_read_header: invalid bsp file\n");
        return (bsp_header){0};
    }

    size_t read = fread(&header, 1, header_size, bsp_file);
    printf("bsp_read_header: read = %zu bytes\n", read);

    if (read < header_size) {
        return (bsp_header){0};
    }

    printf("bsp_read_header: magic = 0x%x\n", header.magic);
    printf("bsp_read_header: version = 0x%x (%u)\n", header.version, header.version);

    if (header.magic != BSP_MAGIC) {
        printf("bsp_read_header: invalid bsp file\n");
        return (bsp_header){0};
    }

    if (header.version != BSP_VERSION) {
        printf("bsp_read_header: unsupported bsp version\n");
        return (bsp_header){0};
    }

    for (int i = 0; i < 19; i++) {
        printf("bsp_read_header: lump[%d].offset = 0x%x\n", i, header.lump[i].offset);
        printf("bsp_read_header: lump[%d].length = 0x%x\n", i, header.lump[i].length);
    }

    return header;
}

static uint8_t *bsp_read_lump(bsp_header header, int index, const char *tag, FILE *bsp_file) {
    if (bsp_file == NULL) {
        printf("%s: bsp_file = NULL\n", tag);
        return NULL;
    }

    if (header.magic != BSP_MAGIC) {
        printf("%s: invalid bsp file\n", tag);
        return NULL;
    }

    if (header.version != BSP_VERSION) {
        printf("%s: unsupported bsp version\n", tag);
        return NULL;
    }

    int result = fseek(bsp_file, 0, SEEK_END);
    if (result != 0) {
        printf("%s: fseek error %d\n", tag, result);
        return NULL;
    }

    long bsp_size = ftell(bsp_file);
    if (bsp_size < 0) {
        printf("%s: ftell error\n", tag);
        return NULL;
    }
    printf("%s: bsp_size = %ld bytes\n", tag, bsp_size);

    bsp_lump lump = header.lump[index];
    if (lump.length == 0) {
        printf("%s: lump is empty\n", tag);
        return NULL;
    }

    if (lump.offset > bsp_size || lump.length > bsp_size - lump.offset) {
        printf("%s: invalid bsp file\n", tag);
        return NULL;
    }

    result = fseek(bsp_file, lump.offset, SEEK_SET);
    if (result != 0) {
        printf("%s: fseek error %d\n", tag, result);
        return NULL;
    }

    uint8_t *data = malloc(lump.length);
    if (data == NULL) {
        printf("%s: failed to allocate memory\n", tag);
        return NULL;
    }

    size_t read = fread(data, 1, lump.length, bsp_file);
    printf("%s: read = %zu bytes\n", tag, read);

    if (read < lump.length) {
        free(data);
        return NULL;
    }

    return data;
}

static point3f *bsp_read_vertices(bsp_header header, uint32_t *count, FILE *bsp_file) {
    if (count == NULL) {
        printf("bsp_read_vertices: count = NULL\n");
        return NULL;
    }

    point3f *vertices = (point3f *) bsp_read_lump(header, BSP_VERTICES, "bsp_read_vertices", bsp_file);
    if (vertices == NULL) {
        return NULL;
    }

    *count = header.lump[BSP_VERTICES].length / sizeof(point3f);
    printf("bsp_read_vertices: count = %u\n", *count);

    return vertices;
}

static bsp_edge *bsp_read_edges(bsp_header header, uint32_t *count, FILE *bsp_file) {
    if (count == NULL) {
        printf("bsp_read_edges: count = NULL\n");
        return NULL;
    }

    bsp_edge *edges = (bsp_edge *) bsp_read_lump(header, BSP_EDGES, "bsp_read_edges", bsp_file);
    if (edges == NULL) {
        return NULL;
    }

    *count = header.lump[BSP_EDGES].length / sizeof(bsp_edge);
    printf("bsp_read_edges: count = %u\n", *count);

    return edges;
}

static bsp_face *bsp_read_faces(bsp_header header, uint32_t *count, FILE *bsp_file) {
    if (count == NULL) {
        printf("bsp_read_faces: count = NULL\n");
        return NULL;
    }

    bsp_face *faces = (bsp_face *) bsp_read_lump(header, BSP_FACES, "bsp_read_faces", bsp_file);
    if (faces == NULL) {
        return NULL;
    }

    *count = header.lump[BSP_FACES].length / sizeof(bsp_face);
    printf("bsp_read_faces: count = %u\n", *count);

    return faces;
}

static int32_t *bsp_read_face_edges(bsp_header header, uint32_t *count, FILE *bsp_file) {
    if (count == NULL) {
        printf("bsp_read_face_edges: count = NULL\n");
        return NULL;
    }

    int32_t *face_edges = (int32_t *) bsp_read_lump(header, BSP_FACE_EDGES, "bsp_read_face_edges", bsp_file);
    if (face_edges == NULL) {
        return NULL;
    }

    *count = header.lump[BSP_FACE_EDGES].length / sizeof(int32_t);
    printf("bsp_read_face_edges: count = %u\n", *count);

    return face_edges;
}

static bsp_plane *bsp_read_planes(bsp_header header, uint32_t *count, FILE *bsp_file) {
    if (count == NULL) {
        printf("bsp_read_planes: count = NULL\n");
        return NULL;
    }

    bsp_plane *planes = (bsp_plane *) bsp_read_lump(header, BSP_PLANES, "bsp_read_planes", bsp_file);
    if (planes == NULL) {
        return NULL;
    }

    *count = header.lump[BSP_PLANES].length / sizeof(bsp_plane);
    printf("bsp_read_planes: count = %u\n", *count);

    return planes;
}

static bsp_node *bsp_read_nodes(bsp_header header, uint32_t *count, FILE *bsp_file) {
    if (count == NULL) {
        printf("bsp_read_nodes: count = NULL\n");
        return NULL;
    }

    bsp_node *nodes = (bsp_node *) bsp_read_lump(header, BSP_NODES, "bsp_read_nodes", bsp_file);
    if (nodes == NULL) {
        return NULL;
    }

    *count = header.lump[BSP_NODES].length / sizeof(bsp_node);
    printf("bsp_read_nodes: count = %u\n", *count);

    return nodes;
}

static bsp_leaf *bsp_read_leaves(bsp_header header, uint32_t *count, FILE *bsp_file) {
    if (count == NULL) {
        printf("bsp_read_leaves: count = NULL\n");
        return NULL;
    }

    bsp_leaf *leaves = (bsp_leaf *) bsp_read_lump(header, BSP_LEAVES, "bsp_read_leaves", bsp_file);
    if (leaves == NULL) {
        return NULL;
    }

    *count = header.lump[BSP_LEAVES].length / sizeof(bsp_leaf);
    printf("bsp_read_leaves: count = %u\n", *count);

    return leaves;
}

static bsp_brush *bsp_read_brushes(bsp_header header, uint32_t *count, FILE *bsp_file) {
    if (count == NULL) {
        printf("bsp_read_brushes: count = NULL\n");
        return NULL;
    }

    bsp_brush *brushes = (bsp_brush *) bsp_read_lump(header, BSP_BRUSHES, "bsp_read_brushes", bsp_file);
    if (brushes == NULL) {
        return NULL;
    }

    *count = header.lump[BSP_BRUSHES].length / sizeof(bsp_brush);
    printf("bsp_read_brushes: count = %u\n", *count);

    return brushes;
}

static bsp_brush_side *bsp_read_brush_sides(bsp_header header, uint32_t *count, FILE *bsp_file) {
    if (count == NULL) {
        printf("bsp_read_brush_sides: count = NULL\n");
        return NULL;
    }

    bsp_brush_side *sides = (bsp_brush_side *) bsp_read_lump(header, BSP_BRUSH_SIDES, "bsp_read_brush_sides", bsp_file);
    if (sides == NULL) {
        return NULL;
    }

    *count = header.lump[BSP_BRUSH_SIDES].length / sizeof(bsp_brush_side);
    printf("bsp_read_brush_sides: count = %u\n", *count);

    return sides;
}

static uint16_t *bsp_read_leaf_brushes(bsp_header header, uint32_t *count, FILE *bsp_file) {
    if (count == NULL) {
        printf("bsp_read_leaf_brushes: count = NULL\n");
        return NULL;
    }

    uint16_t *lb = (uint16_t *) bsp_read_lump(header, BSP_LEAF_BRUSHES, "bsp_read_leaf_brushes", bsp_file);
    if (lb == NULL) {
        return NULL;
    }

    *count = header.lump[BSP_LEAF_BRUSHES].length / sizeof(uint16_t);
    printf("bsp_read_leaf_brushes: count = %u\n", *count);

    return lb;
}

static bsp_model_lump *bsp_read_models(bsp_header header, uint32_t *count, FILE *bsp_file) {
    if (count == NULL) {
        printf("bsp_read_models: count = NULL\n");
        return NULL;
    }

    bsp_model_lump *models = (bsp_model_lump *) bsp_read_lump(header, BSP_MODELS, "bsp_read_models", bsp_file);
    if (models == NULL) {
        return NULL;
    }

    *count = header.lump[BSP_MODELS].length / sizeof(bsp_model_lump);
    printf("bsp_read_models: count = %u\n", *count);

    return models;
}

static uint8_t *bsp_read_visibility(bsp_header header, uint32_t *size, FILE *bsp_file) {
    if (size == NULL) {
        printf("bsp_read_visibility: size = NULL\n");
        return NULL;
    }

    uint32_t length = header.lump[BSP_VISIBILITY].length;
    if (length == 0) {
        printf("bsp_read_visibility: visibility lump is empty\n");
        *size = 0;
        return NULL;
    }

    uint8_t *vis = bsp_read_lump(header, BSP_VISIBILITY, "bsp_read_visibility", bsp_file);
    if (vis == NULL) {
        *size = 0;
        return NULL;
    }

    *size = length;
    printf("bsp_read_visibility: size = %u bytes\n", *size);
    return vis;
}

static uint16_t *bsp_read_leaf_faces(bsp_header header, uint32_t *count, FILE *bsp_file) {
    if (count == NULL) {
        printf("bsp_read_leaf_faces: count = NULL\n");
        return NULL;
    }

    uint16_t *leaf_faces = (uint16_t *) bsp_read_lump(header, BSP_LEAF_FACES, "bsp_read_leaf_faces", bsp_file);
    if (leaf_faces == NULL) {
        return NULL;
    }

    *count = header.lump[BSP_LEAF_FACES].length / sizeof(uint16_t);
    printf("bsp_read_leaf_faces: count = %u\n", *count);

    return leaf_faces;
}

static uint8_t *bsp_read_lightmaps(bsp_header header, uint32_t *size, FILE *bsp_file) {
    if (size == NULL) {
        printf("bsp_read_lightmaps: size = NULL\n");
        return NULL;
    }

    uint32_t length = header.lump[BSP_LIGHTMAPS].length;
    if (length == 0) {
        printf("bsp_read_lightmaps: lightmap lump is empty\n");
        *size = 0;
        return NULL;
    }

    uint8_t *lightmaps = bsp_read_lump(header, BSP_LIGHTMAPS, "bsp_read_lightmaps", bsp_file);
    if (lightmaps == NULL) {
        *size = 0;
        return NULL;
    }

    *size = length;
    printf("bsp_read_lightmaps: size = %u bytes\n", *size);
    return lightmaps;
}

static bsp_texinfo *bsp_read_texinfo(bsp_header header, uint32_t *count, FILE *bsp_file) {
    if (count == NULL) {
        printf("bsp_read_texinfo: count = NULL\n");
        return NULL;
    }

    bsp_texinfo *textures = (bsp_texinfo *) bsp_read_lump(header, BSP_TEXTURES, "bsp_read_texinfo", bsp_file);
    if (textures == NULL) {
        return NULL;
    }

    *count = header.lump[BSP_TEXTURES].length / sizeof(bsp_texinfo);
    printf("bsp_read_texinfo: count = %u\n", *count);

    return textures;
}

static void bsp_free_entity_props(bsp_entity_prop *props, uint32_t num_props) {
    if (props == NULL) {
        printf("bsp_free_entity_props: props = NULL\n");
        return;
    }

    for (uint32_t i = 0; i < num_props; i++) {
        bsp_entity_prop *p = &props[i];
        if (p == NULL) {
            printf("bsp_free_entity_props: props[%u] = NULL\n", i);
            continue;
        }

        if (p->key == NULL) {
            printf("bsp_free_entity_props: props[%u].key = NULL\n", i);
        } else {
            free(p->key);
        }

        if (p->value == NULL) {
            printf("bsp_free_entity_props: props[%u].value = NULL\n", i);
        } else {
            free(p->value);
        }
    }

    free(props);
}

static void bsp_free_entities(bsp_entity *entities, uint32_t num_entities) {
    if (entities == NULL) {
        printf("bsp_free_entities: entities = NULL\n");
        return;
    }

    for (uint32_t i = 0; i < num_entities; i++) {
        bsp_entity *e = &entities[i];
        if (e == NULL) {
            printf("bsp_free_entities: entities[%u] = NULL\n", i);
            continue;
        }
        bsp_free_entity_props(e->props, e->num_props);
    }

    free(entities);
}

#define MAX_KEY_LEN   256
#define MAX_VALUE_LEN 4096

#define SM_ENT_OUTSIDE    0
#define SM_ENT_IN_ENTITY  1
#define SM_ENT_IN_KEY     2
#define SM_ENT_IN_VALUE   3
#define SM_ENT_WAIT_VALUE 4

static bsp_entity *bsp_read_entities(bsp_header header, uint32_t *count, FILE *bsp_file) {
    if (count == NULL) {
        printf("bsp_read_entities: count = NULL\n");
        return NULL;
    }

    char *text = (char *) bsp_read_lump(header, BSP_ENTITIES, "bsp_read_entities", bsp_file);
    if (text == NULL) {
        return NULL;
    }

    bsp_entity *entities = NULL;
    bsp_entity current = {0};
    char key[MAX_KEY_LEN] = {0};
    char value[MAX_VALUE_LEN] = {0};
    int key_len = 0;
    int value_len = 0;

    *count = 0;
    int state = SM_ENT_OUTSIDE;
    uint32_t length = header.lump[BSP_ENTITIES].length;

    for (uint32_t i = 0; i < length; i++) {
        char c = text[i];
        if (!c) break;

        switch (state) {
            case SM_ENT_OUTSIDE:
                if (c == '{') {
                    state = SM_ENT_IN_ENTITY;
                    current = (bsp_entity){0};
                }
                break;

            case SM_ENT_IN_ENTITY:
                if (c == '"') {
                    state = SM_ENT_IN_KEY;
                    key_len = 0;
                } else if (c == '}') {
                    bsp_entity *new_entities = realloc(entities, (*count + 1) * sizeof(bsp_entity));

                    if (new_entities == NULL) {
                        printf("bsp_read_entities: failed to allocate memory\n");
                        bsp_free_entity_props(current.props, current.num_props);
                        bsp_free_entities(entities, *count);
                        free(text);
                        return NULL;
                    }

                    entities = new_entities;
                    entities[*count] = current;
                    state = SM_ENT_OUTSIDE;
                    *count += 1;
                }
                break;

            case SM_ENT_IN_KEY:
                if (key_len == MAX_KEY_LEN - 1 || c == '"') {
                    state = SM_ENT_WAIT_VALUE;
                    key[key_len] = 0;
                } else {
                    key[key_len++] = c;
                }
                break;

            case SM_ENT_IN_VALUE:
                if (value_len == MAX_VALUE_LEN - 1 || c == '"') {
                    state = SM_ENT_IN_ENTITY;
                    value[value_len] = 0;

                    bsp_entity_prop *new_props = realloc(
                        current.props, (current.num_props + 1) * sizeof(bsp_entity_prop));

                    if (new_props == NULL) {
                        printf("bsp_read_entities: failed to allocate memory\n");
                        bsp_free_entity_props(current.props, current.num_props);
                        bsp_free_entities(entities, *count);
                        free(text);
                        return NULL;
                    }

                    current.props = new_props;
                    bsp_entity_prop *new_prop = &current.props[current.num_props++];
                    new_prop->key = (char *) calloc(1, key_len + 1);
                    strcpy(new_prop->key, key);
                    new_prop->value = (char *) calloc(1, value_len + 1);
                    strcpy(new_prop->value, value);
                } else {
                    value[value_len++] = c;
                }
                break;

            case SM_ENT_WAIT_VALUE:
                if (c == '"') {
                    state = SM_ENT_IN_VALUE;
                    value_len = 0;
                }
                break;

            default: ;
        }
    }

    printf("bsp_read_entities: count = %u\n", *count);

    free(text);
    return entities;
}

const char *bsp_entity_get(const bsp_entity *e, const char *key) {
    for (uint32_t i = 0; i < e->num_props; i++) {
        bsp_entity_prop *p = &e->props[i];
        if (p == NULL) continue;

        if (strcmp(p->key, key) == 0) {
            return p->value;
        }
    }
    return NULL;
}

bsp_model *bsp_load(const char *path) {
    size_t path_len = strlen(path);
    char *path_bsp = calloc(1, path_len + 10);
    strcpy(path_bsp, "maps/");
    strcat(path_bsp, path);
    strcat(path_bsp, ".bsp");

    FILE *bsp_file = fopen(path_bsp, "rb");
    free(path_bsp);

    if (bsp_file == NULL) {
        printf("bsp_load: failed to open %s\n", path);
        return NULL;
    }

    bsp_model *bsp = calloc(1, sizeof(bsp_model));
    if (bsp == NULL) {
        printf("bsp_load: failed to allocate memory\n");
        fclose(bsp_file);
        return NULL;
    }

    bsp->header = bsp_read_header(bsp_file);
    if (bsp->header.magic != BSP_MAGIC || bsp->header.version != BSP_VERSION) {
        bsp_free(bsp);
        fclose(bsp_file);
        return NULL;
    }

    bsp->vertices = bsp_read_vertices(bsp->header, &bsp->num_vertices, bsp_file);
    if (bsp->vertices == NULL) {
        bsp_free(bsp);
        fclose(bsp_file);
        return NULL;
    }

    bsp->edges = bsp_read_edges(bsp->header, &bsp->num_edges, bsp_file);
    if (bsp->edges == NULL) {
        bsp_free(bsp);
        fclose(bsp_file);
        return NULL;
    }

    bsp->face_edges = bsp_read_face_edges(bsp->header, &bsp->num_face_edges, bsp_file);
    if (bsp->face_edges == NULL) {
        bsp_free(bsp);
        fclose(bsp_file);
        return NULL;
    }

    bsp->faces = bsp_read_faces(bsp->header, &bsp->num_faces, bsp_file);
    if (bsp->faces == NULL) {
        bsp_free(bsp);
        fclose(bsp_file);
        return NULL;
    }

    bsp->texinfo = bsp_read_texinfo(bsp->header, &bsp->num_texinfo, bsp_file);
    if (bsp->texinfo == NULL) {
        bsp_free(bsp);
        fclose(bsp_file);
        return NULL;
    }

    bsp->entities = bsp_read_entities(bsp->header, &bsp->num_entities, bsp_file);
    if (bsp->entities == NULL) {
        bsp_free(bsp);
        fclose(bsp_file);
        return NULL;
    }

    // Lightmaps are optional - some maps have no lighting
    bsp->lightmaps = bsp_read_lightmaps(bsp->header, &bsp->lightmaps_size, bsp_file);

    // Structural data for PVS culling and BSP traversal
    bsp->planes = bsp_read_planes(bsp->header, &bsp->num_planes, bsp_file);
    if (bsp->planes == NULL) {
        bsp_free(bsp);
        fclose(bsp_file);
        return NULL;
    }

    bsp->nodes = bsp_read_nodes(bsp->header, &bsp->num_nodes, bsp_file);
    if (bsp->nodes == NULL) {
        bsp_free(bsp);
        fclose(bsp_file);
        return NULL;
    }

    bsp->leaves = bsp_read_leaves(bsp->header, &bsp->num_leaves, bsp_file);
    if (bsp->leaves == NULL) {
        bsp_free(bsp);
        fclose(bsp_file);
        return NULL;
    }

    bsp->leaf_faces = bsp_read_leaf_faces(bsp->header, &bsp->num_leaf_faces, bsp_file);
    if (bsp->leaf_faces == NULL) {
        bsp_free(bsp);
        fclose(bsp_file);
        return NULL;
    }

    // Models lump is normally present; allow it to be missing only if the engine
    // can render without inline models.
    bsp->models = bsp_read_models(bsp->header, &bsp->num_models, bsp_file);

    // Brush data for collision.
    bsp->brushes = bsp_read_brushes(bsp->header, &bsp->num_brushes, bsp_file);
    if (bsp->brushes == NULL) {
        bsp_free(bsp);
        fclose(bsp_file);
        return NULL;
    }

    bsp->brush_sides = bsp_read_brush_sides(bsp->header, &bsp->num_brush_sides, bsp_file);
    if (bsp->brush_sides == NULL) {
        bsp_free(bsp);
        fclose(bsp_file);
        return NULL;
    }

    bsp->leaf_brushes = bsp_read_leaf_brushes(bsp->header, &bsp->num_leaf_brushes, bsp_file);
    if (bsp->leaf_brushes == NULL) {
        bsp_free(bsp);
        fclose(bsp_file);
        return NULL;
    }

    // Visibility is optional (some maps have no PVS data, e.g. test maps)
    bsp->visibility = bsp_read_visibility(bsp->header, &bsp->visibility_size, bsp_file);
    if (bsp->visibility != NULL && bsp->visibility_size >= sizeof(uint32_t)) {
        uint32_t nc;
        memcpy(&nc, bsp->visibility, sizeof(uint32_t));
        bsp->num_clusters = nc;
        // Sanity check: header table is num_clusters * sizeof(bsp_vis_offset) bytes
        // following the uint32_t count.
        uint32_t header_bytes = sizeof(uint32_t) + bsp->num_clusters * sizeof(bsp_vis_offset);
        if (header_bytes > bsp->visibility_size) {
            printf("bsp_load: corrupt visibility lump (clusters=%u, size=%u)\n",
                   bsp->num_clusters, bsp->visibility_size);
            bsp->num_clusters = 0;
        }
        printf("bsp_load: num_clusters = %u\n", bsp->num_clusters);
    } else {
        bsp->num_clusters = 0;
    }

    fclose(bsp_file);
    return bsp;
}

int bsp_decompress_pvs(const bsp_model *bsp, int32_t cluster, uint8_t *out) {
    if (bsp == NULL || out == NULL) {
        return 0;
    }
    if (bsp->visibility == NULL || bsp->num_clusters == 0) {
        return 0;
    }
    if (cluster < 0 || (uint32_t) cluster >= bsp->num_clusters) {
        return 0;
    }

    uint32_t row_bytes = (bsp->num_clusters + 7u) / 8u;
    memset(out, 0, row_bytes);

    // Visibility lump layout:
    //   uint32_t num_clusters;
    //   bsp_vis_offset offsets[num_clusters];   // pvs/phs are byte offsets into the lump
    //   uint8_t        rle_data[...];
    uint32_t off_table = sizeof(uint32_t);
    uint32_t off_entry = off_table + (uint32_t) cluster * sizeof(bsp_vis_offset);
    if (off_entry + sizeof(bsp_vis_offset) > bsp->visibility_size) {
        return 0;
    }

    bsp_vis_offset offsets;
    memcpy(&offsets, bsp->visibility + off_entry, sizeof(offsets));

    if (offsets.pvs >= bsp->visibility_size) {
        return 0;
    }

    const uint8_t *in = bsp->visibility + offsets.pvs;
    const uint8_t *in_end = bsp->visibility + bsp->visibility_size;

    uint32_t out_pos = 0;
    while (out_pos < row_bytes && in < in_end) {
        uint8_t c = *in++;
        if (c != 0) {
            out[out_pos++] = c;
            continue;
        }
        // Zero run: next byte is the count of zero bytes to emit (including this one).
        if (in >= in_end) {
            break;
        }
        uint8_t run = *in++;
        if (run == 0) {
            // Treat malformed zero-zero as a single zero to avoid infinite loops.
            run = 1;
        }
        while (run > 0 && out_pos < row_bytes) {
            out[out_pos++] = 0;
            run--;
        }
    }

    return 1;
}

int32_t bsp_find_leaf(const bsp_model *bsp, point3f point) {
    if (bsp == NULL || bsp->nodes == NULL || bsp->num_nodes == 0 || bsp->planes == NULL) {
        return -1;
    }

    int32_t index = 0;
    // Walk down the tree. Negative child index encodes a leaf as -(leaf_index + 1).
    while (index >= 0) {
        if ((uint32_t) index >= bsp->num_nodes) {
            return -1;
        }
        const bsp_node *node = &bsp->nodes[index];
        if (node->plane >= bsp->num_planes) {
            return -1;
        }
        const bsp_plane *plane = &bsp->planes[node->plane];

        float d = point.x * plane->normal.x
                + point.y * plane->normal.y
                + point.z * plane->normal.z
                - plane->distance;

        if (d >= 0.0f) {
            index = node->front_child;
        } else {
            index = node->back_child;
        }
    }

    int32_t leaf_idx = -(index + 1);
    if (leaf_idx < 0 || (uint32_t) leaf_idx >= bsp->num_leaves) {
        return -1;
    }
    return leaf_idx;
}

static void bsp_free_lump(void *lump, const char *tag) {
    if (lump == NULL) {
        printf("bsp_free_lump: %s = NULL\n", tag);
        return;
    }

    free(lump);
}

void bsp_free(bsp_model *bsp) {
    if (bsp == NULL) {
        printf("bsp_free: bsp = NULL\n");
        return;
    }

    if (bsp->vertices != NULL)   bsp_free_lump(bsp->vertices, "vertices");
    if (bsp->edges != NULL)      bsp_free_lump(bsp->edges, "edges");
    if (bsp->face_edges != NULL) bsp_free_lump(bsp->face_edges, "face_edges");
    if (bsp->faces != NULL)      bsp_free_lump(bsp->faces, "faces");
    if (bsp->texinfo != NULL)    bsp_free_lump(bsp->texinfo, "texinfo");
    if (bsp->entities != NULL)   bsp_free_entities(bsp->entities, bsp->num_entities);
    if (bsp->lightmaps != NULL)  bsp_free_lump(bsp->lightmaps, "lightmaps");
    if (bsp->planes != NULL)     bsp_free_lump(bsp->planes, "planes");
    if (bsp->nodes != NULL)      bsp_free_lump(bsp->nodes, "nodes");
    if (bsp->leaves != NULL)     bsp_free_lump(bsp->leaves, "leaves");
    if (bsp->leaf_faces != NULL) bsp_free_lump(bsp->leaf_faces, "leaf_faces");
    if (bsp->models != NULL)     bsp_free_lump(bsp->models, "models");
    if (bsp->visibility != NULL) bsp_free_lump(bsp->visibility, "visibility");
    if (bsp->brushes != NULL)      bsp_free_lump(bsp->brushes, "brushes");
    if (bsp->brush_sides != NULL)  bsp_free_lump(bsp->brush_sides, "brush_sides");
    if (bsp->leaf_brushes != NULL) bsp_free_lump(bsp->leaf_brushes, "leaf_brushes");

    free(bsp);
}
