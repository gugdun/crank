#include "lightmap.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define MAX_FACE_VERTICES 256
#define MAX_LM_DIM        256
#define ATLAS_PADDING     1  // 1-luxel border around each face to prevent bilinear bleed

// Compute s,t in lightmap world units for a face's vertices, returning
// integer mins/maxes (snapped: floor(min/16)*16, ceil(max/16)*16).
static int compute_face_extents(const bsp_model *bsp, const bsp_face *face,
                                float *s_min_out, float *t_min_out,
                                int *lm_w_out, int *lm_h_out) {
    if (face->texture_info >= bsp->num_texinfo) return 0;
    const bsp_texinfo *ti = &bsp->texinfo[face->texture_info];

    float s_min = 1e30f, s_max = -1e30f;
    float t_min = 1e30f, t_max = -1e30f;

    int gathered = 0;
    for (int j = 0; j < face->num_edges; j++) {
        if (face->first_edge + j >= bsp->num_face_edges) continue;
        int32_t surf_edge = bsp->face_edges[face->first_edge + j];

        if (surf_edge >= (int32_t) bsp->num_edges || surf_edge < -(int32_t) bsp->num_edges)
            continue;

        uint16_t vidx;
        if (surf_edge >= 0) vidx = bsp->edges[surf_edge].v1;
        else                vidx = bsp->edges[-surf_edge].v2;

        if (vidx >= bsp->num_vertices) continue;
        const point3f *p = &bsp->vertices[vidx];

        float s = p->x * ti->u_axis.x + p->y * ti->u_axis.y + p->z * ti->u_axis.z + ti->u_offset;
        float t = p->x * ti->v_axis.x + p->y * ti->v_axis.y + p->z * ti->v_axis.z + ti->v_offset;

        if (s < s_min) s_min = s;
        if (s > s_max) s_max = s;
        if (t < t_min) t_min = t;
        if (t > t_max) t_max = t;
        gathered++;
    }

    if (gathered < 3) return 0;

    int bmins0 = (int) floorf(s_min / 16.0f);
    int bmins1 = (int) floorf(t_min / 16.0f);
    int bmaxs0 = (int) ceilf(s_max / 16.0f);
    int bmaxs1 = (int) ceilf(t_max / 16.0f);

    int lm_w = (bmaxs0 - bmins0) + 1;
    int lm_h = (bmaxs1 - bmins1) + 1;

    if (lm_w < 1) lm_w = 1;
    if (lm_h < 1) lm_h = 1;
    if (lm_w > MAX_LM_DIM) lm_w = MAX_LM_DIM;
    if (lm_h > MAX_LM_DIM) lm_h = MAX_LM_DIM;

    *s_min_out = (float)(bmins0 * 16);
    *t_min_out = (float)(bmins1 * 16);
    *lm_w_out = lm_w;
    *lm_h_out = lm_h;
    return 1;
}

// Shelf packer state
typedef struct {
    int x;        // next free x on this shelf
    int y;        // shelf baseline (top edge)
    int height;   // shelf height
} shelf;

static int next_pow2(int v) {
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

// File-scope pointer for qsort comparator (single-threaded build, so this is fine)
static const lm_face_info *g_sort_faces = NULL;

static int cmp_by_height_desc(const void *a, const void *b) {
    uint32_t ia = *(const uint32_t *) a;
    uint32_t ib = *(const uint32_t *) b;
    int ha = g_sort_faces[ia].has_lightmap ? g_sort_faces[ia].lm_h : 0;
    int hb = g_sort_faces[ib].has_lightmap ? g_sort_faces[ib].lm_h : 0;
    return hb - ha;
}

lm_atlas *lm_build(const bsp_model *bsp) {
    if (bsp == NULL) {
        printf("lm_build: bsp = NULL\n");
        return NULL;
    }

    lm_atlas *atlas = calloc(1, sizeof(lm_atlas));
    if (atlas == NULL) {
        printf("lm_build: failed to allocate atlas\n");
        return NULL;
    }

    atlas->face_count = bsp->num_faces;
    atlas->faces = calloc(bsp->num_faces, sizeof(lm_face_info));
    if (atlas->faces == NULL) {
        printf("lm_build: failed to allocate face infos\n");
        free(atlas);
        return NULL;
    }

    // First pass: compute lm dimensions per face and classify lit/unlit.
    // Sort indices by descending height for the shelf packer.
    uint32_t *order = malloc(bsp->num_faces * sizeof(uint32_t));
    if (order == NULL) {
        printf("lm_build: failed to allocate order\n");
        free(atlas->faces);
        free(atlas);
        return NULL;
    }
    for (uint32_t i = 0; i < bsp->num_faces; i++) order[i] = i;

    for (uint32_t i = 0; i < bsp->num_faces; i++) {
        const bsp_face *face = &bsp->faces[i];
        lm_face_info *info = &atlas->faces[i];

        info->has_lightmap = 0;
        info->lm_w = 1;
        info->lm_h = 1;
        info->s_min = 0.0f;
        info->t_min = 0.0f;
        info->atlas_x = 0;
        info->atlas_y = 0;

        if (face->num_edges < 3) continue;
        if (face->texture_info >= bsp->num_texinfo) continue;

        const bsp_texinfo *ti = &bsp->texinfo[face->texture_info];

        // Surfaces with these flags have no lightmap in Quake II
        if (ti->flags & (SURF_SKY | SURF_WARP | SURF_NODRAW)) continue;

        // -1 cast to uint32_t == 0xFFFFFFFF means no lightmap data
        if (face->lightmap_offset == 0xFFFFFFFFu) continue;

        // Must have lightmap data available
        if (bsp->lightmaps == NULL || bsp->lightmaps_size == 0) continue;

        float s_min, t_min;
        int lm_w, lm_h;
        if (!compute_face_extents(bsp, face, &s_min, &t_min, &lm_w, &lm_h)) continue;

        // Validate we won't read past the lightmap buffer (style 0 only: lm_w*lm_h*3 bytes)
        size_t needed = (size_t) lm_w * (size_t) lm_h * 3u;
        if ((size_t) face->lightmap_offset + needed > bsp->lightmaps_size) {
            printf("lm_build: face %u lightmap out of bounds (offset=%u, need=%zu, size=%u)\n",
                   i, face->lightmap_offset, needed, bsp->lightmaps_size);
            continue;
        }

        info->has_lightmap = 1;
        info->s_min = s_min;
        info->t_min = t_min;
        info->lm_w = lm_w;
        info->lm_h = lm_h;
    }

    // Sort indices by descending lm_h for shelf packing.
    g_sort_faces = atlas->faces;
    qsort(order, bsp->num_faces, sizeof(uint32_t), cmp_by_height_desc);
    g_sort_faces = NULL;

    // Pack. Start with a guess atlas width; grow rows down. We'll try 1024 first;
    // if total area suggests more, bump up. Then place each face onto shelves.
    int atlas_w = 1024;

    // Reserve (0,0) for the 1x1 white tile (with its own padding row to keep things simple)
    // We just treat the white tile as always located at (0, 0) and put real tiles starting
    // after it via the shelf packer.
    int reserved_w = 1 + ATLAS_PADDING * 2;
    int reserved_h = 1 + ATLAS_PADDING * 2;

    // Decide atlas width: ensure largest tile fits
    int max_tile_w = reserved_w;
    for (uint32_t i = 0; i < bsp->num_faces; i++) {
        const lm_face_info *info = &atlas->faces[i];
        if (!info->has_lightmap) continue;
        int tw = info->lm_w + ATLAS_PADDING * 2;
        if (tw > max_tile_w) max_tile_w = tw;
    }
    if (max_tile_w > atlas_w) atlas_w = next_pow2(max_tile_w);
    if (atlas_w > 4096) atlas_w = 4096;

    // Shelf packer
    shelf *shelves = NULL;
    int shelf_count = 0;
    int shelf_cap = 0;

    // First shelf reserved for the white tile and possibly more small tiles
    // We won't reserve the whole shelf for the white tile; we just initialise it
    // so that placement starts at (reserved_w, 0) for height-1 tiles. To keep it
    // simple, treat the white tile as the first placement on shelf 0.
    int cur_y = 0;
    {
        // Add initial shelf with the white tile already placed
        shelves = malloc(sizeof(shelf));
        shelves[0].x = reserved_w;
        shelves[0].y = 0;
        shelves[0].height = reserved_h;
        shelf_count = 1;
        shelf_cap = 1;
        cur_y = reserved_h;
    }

    for (uint32_t k = 0; k < bsp->num_faces; k++) {
        uint32_t i = order[k];
        lm_face_info *info = &atlas->faces[i];
        if (!info->has_lightmap) continue;

        int tw = info->lm_w + ATLAS_PADDING * 2;
        int th = info->lm_h + ATLAS_PADDING * 2;

        // Try to place on an existing shelf
        int placed = 0;
        for (int s = 0; s < shelf_count; s++) {
            if (shelves[s].x + tw <= atlas_w && th <= shelves[s].height) {
                info->atlas_x = shelves[s].x + ATLAS_PADDING;
                info->atlas_y = shelves[s].y + ATLAS_PADDING;
                shelves[s].x += tw;
                placed = 1;
                break;
            }
        }

        if (!placed) {
            // Create new shelf
            if (shelf_count == shelf_cap) {
                int new_cap = shelf_cap * 2;
                shelf *ns = realloc(shelves, new_cap * sizeof(shelf));
                if (ns == NULL) {
                    printf("lm_build: failed to allocate shelves\n");
                    free(shelves);
                    free(order);
                    free(atlas->faces);
                    free(atlas);
                    return NULL;
                }
                shelves = ns;
                shelf_cap = new_cap;
            }
            shelves[shelf_count].x = tw;
            shelves[shelf_count].y = cur_y;
            shelves[shelf_count].height = th;
            info->atlas_x = ATLAS_PADDING;
            info->atlas_y = cur_y + ATLAS_PADDING;
            cur_y += th;
            shelf_count++;

            if (cur_y > 4096) {
                printf("lm_build: atlas height exceeded 4096, marking face %u unlit\n", i);
                info->has_lightmap = 0;
                info->lm_w = 1;
                info->lm_h = 1;
                info->atlas_x = 0;
                info->atlas_y = 0;
                cur_y -= th;
                shelf_count--;
            }
        }
    }

    free(shelves);
    free(order);

    // Final atlas dims (power of two)
    atlas->width = atlas_w;
    atlas->height = next_pow2(cur_y);
    if (atlas->height < 16) atlas->height = 16;
    if (atlas->height > 4096) atlas->height = 4096;

    // Allocate atlas pixels, initialised to white
    size_t total = (size_t) atlas->width * (size_t) atlas->height * 3u;
    atlas->pixels = malloc(total);
    if (atlas->pixels == NULL) {
        printf("lm_build: failed to allocate atlas pixels (%zu bytes)\n", total);
        free(atlas->faces);
        free(atlas);
        return NULL;
    }
    memset(atlas->pixels, 0xFF, total);

    // Blit each lit face's lightmap into the atlas.
    // Quake II lightmap data is RGB rows, lm_w columns wide, lm_h rows tall.
    for (uint32_t i = 0; i < bsp->num_faces; i++) {
        const lm_face_info *info = &atlas->faces[i];
        if (!info->has_lightmap) continue;

        const bsp_face *face = &bsp->faces[i];
        const uint8_t *src = bsp->lightmaps + face->lightmap_offset;

        for (int y = 0; y < info->lm_h; y++) {
            uint8_t *dst = atlas->pixels +
                ((size_t)(info->atlas_y + y) * (size_t) atlas->width + (size_t) info->atlas_x) * 3u;
            const uint8_t *row = src + (size_t) y * (size_t) info->lm_w * 3u;
            memcpy(dst, row, (size_t) info->lm_w * 3u);
        }

        // Fill 1-luxel padding around the tile by replicating border luxels.
        // Top/bottom rows
        for (int px = -ATLAS_PADDING; px < info->lm_w + ATLAS_PADDING; px++) {
            int clamped_x = px;
            if (clamped_x < 0) clamped_x = 0;
            if (clamped_x >= info->lm_w) clamped_x = info->lm_w - 1;
            const uint8_t *src_top = src + (size_t) clamped_x * 3u;
            const uint8_t *src_bot = src + ((size_t)(info->lm_h - 1) * (size_t) info->lm_w + (size_t) clamped_x) * 3u;
            for (int p = 1; p <= ATLAS_PADDING; p++) {
                uint8_t *dst_top = atlas->pixels +
                    ((size_t)(info->atlas_y - p) * (size_t) atlas->width + (size_t)(info->atlas_x + px)) * 3u;
                uint8_t *dst_bot = atlas->pixels +
                    ((size_t)(info->atlas_y + info->lm_h + p - 1) * (size_t) atlas->width + (size_t)(info->atlas_x + px)) * 3u;
                memcpy(dst_top, src_top, 3);
                memcpy(dst_bot, src_bot, 3);
            }
        }
        // Left/right columns
        for (int py = 0; py < info->lm_h; py++) {
            const uint8_t *src_l = src + (size_t) py * (size_t) info->lm_w * 3u;
            const uint8_t *src_r = src_l + (size_t)(info->lm_w - 1) * 3u;
            for (int p = 1; p <= ATLAS_PADDING; p++) {
                uint8_t *dst_l = atlas->pixels +
                    ((size_t)(info->atlas_y + py) * (size_t) atlas->width + (size_t)(info->atlas_x - p)) * 3u;
                uint8_t *dst_r = atlas->pixels +
                    ((size_t)(info->atlas_y + py) * (size_t) atlas->width + (size_t)(info->atlas_x + info->lm_w + p - 1)) * 3u;
                memcpy(dst_l, src_l, 3);
                memcpy(dst_r, src_r, 3);
            }
        }
    }

    printf("lm_build: atlas %dx%d (%zu KB), faces=%u\n",
           atlas->width, atlas->height, total / 1024, bsp->num_faces);

    return atlas;
}

void lm_free(lm_atlas *atlas) {
    if (atlas == NULL) return;
    if (atlas->pixels) free(atlas->pixels);
    if (atlas->faces) free(atlas->faces);
    free(atlas);
}
