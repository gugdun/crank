#include "sys_map.h"

#include "bsp.h"
#include "ecs/ecs.h"
#include "mesh.h"
#include "render.h"
#include "res/res_map.h"
#include "res/res_mesh.h"
#include "res/res_texture.h"
#include "sys/sys_fpcam.h"
#include "sys/sys_skybox.h"
#include "raylib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_SKY     "unit1_"
#define SKY_PATH_PREFIX "env/"

static ecs_component_id g_c_map = ECS_MAX_COMPONENTS;

void sys_map_register(ecs_world *w) {
    if (w == NULL) {
        printf("sys_map_register: w = NULL\n");
        return;
    }
    g_c_map = ecs_register(w, "c_map", sizeof(c_map), NULL);
}

ecs_entity sys_map_spawn(ecs_world *w, map_handle h) {
    if (w == NULL) {
        printf("sys_map_spawn: w = NULL\n");
        return ECS_INVALID;
    }

    ecs_entity e = ecs_create(w);
    if (e == ECS_INVALID) {
        return ECS_INVALID;
    }

    c_map *m = ecs_add(w, e, g_c_map);
    if (m == NULL) {
        ecs_destroy(w, e);
        return ECS_INVALID;
    }
    m->map = h;
    return e;
}

// Parse a "x y z" string from BSP entity space into raylib y-up space
// using the same (x, z, -y) swap used by the original main.c.
static Vector3 parse_origin(const char *str) {
    Vector3 result = (Vector3){0};

    if (str == NULL) {
        printf("parse_origin: str = NULL\n");
        return result;
    }

    size_t len = strlen(str);
    char *s = calloc(1, len + 1);
    if (s == NULL) {
        printf("parse_origin: failed to allocate scratch\n");
        return result;
    }
    memcpy(s, str, len + 1);

    char *x_str = strtok(s, " ");
    if (x_str == NULL) {
        printf("parse_origin: wrong input format\n");
        free(s);
        return result;
    }
    result.x = strtof(x_str, NULL);

    char *y_str = strtok(NULL, " ");
    if (y_str == NULL) {
        printf("parse_origin: wrong input format\n");
        free(s);
        return result;
    }
    result.y = strtof(y_str, NULL);

    char *z_str = strtok(NULL, " ");
    if (z_str == NULL) {
        printf("parse_origin: wrong input format\n");
        free(s);
        return result;
    }
    result.z = strtof(z_str, NULL);

    free(s);

    return (Vector3){result.x, result.z, -result.y};
}

static void load_skybox_sides(res_texture_mgr *texmgr,
                              const char *sky_name,
                              tex_handle out[6]) {
    static const char *suffix[6] = {"ft", "dn", "bk", "lf", "rt", "up"};

    size_t prefix_len = strlen(SKY_PATH_PREFIX);
    size_t name_len = strlen(sky_name);
    size_t suffix_max = 2; // all suffixes are 2 chars
    size_t path_size = prefix_len + name_len + suffix_max + 1;

    char *path = calloc(1, path_size);
    if (path == NULL) {
        printf("load_skybox_sides: failed to allocate path\n");
        for (int i = 0; i < 6; i++) out[i] = 0;
        return;
    }

    for (int i = 0; i < 6; i++) {
        snprintf(path, path_size, "%s%s%s", SKY_PATH_PREFIX, sky_name, suffix[i]);
        out[i] = res_texture_load(texmgr, path);

        // Original code clamps wrap mode on skybox textures.
        const texture *t = res_texture_get(texmgr, out[i]);
        if (t != NULL) {
            SetTextureWrap(t->rl_texture, TEXTURE_WRAP_CLAMP);
        }
    }

    free(path);
}

void sys_map_apply_spawn(ecs_world *w,
                         ecs_entity map_entity,
                         ecs_entity fpcam_entity,
                         ecs_entity skybox_entity,
                         res_texture_mgr *texmgr,
                         res_map_mgr *mapmgr) {
    if (w == NULL || mapmgr == NULL || texmgr == NULL) {
        printf("sys_map_apply_spawn: invalid arguments\n");
        return;
    }

    c_map *cm = ecs_get(w, map_entity, g_c_map);
    if (cm == NULL) {
        printf("sys_map_apply_spawn: map entity %u has no c_map\n", map_entity);
        return;
    }

    map_view view = {0};
    if (!res_map_get(mapmgr, cm->map, &view)) {
        printf("sys_map_apply_spawn: invalid map handle %u\n", cm->map);
        return;
    }
    if (view.bsp == NULL) {
        printf("sys_map_apply_spawn: bsp is NULL\n");
        return;
    }

    const bsp_model *bsp = view.bsp;

    size_t sky_cap = strlen(DEFAULT_SKY) + 1;
    char *sky_name = calloc(1, sky_cap);
    if (sky_name == NULL) {
        printf("sys_map_apply_spawn: failed to allocate sky_name\n");
        return;
    }
    memcpy(sky_name, DEFAULT_SKY, sky_cap);

    for (uint32_t i = 0; i < bsp->num_entities; i++) {
        const bsp_entity *e = &bsp->entities[i];
        const char *classname = bsp_entity_get(e, "classname");
        if (classname == NULL) {
            continue;
        }

        if (strcmp(classname, "worldspawn") == 0) {
            const char *sky_str = bsp_entity_get(e, "sky");
            if (sky_str != NULL) {
                printf("{\n\"classname\" \"%s\"\n\"sky\" \"%s\"\n}\n", classname, sky_str);
                size_t new_len = strlen(sky_str) + 1;
                char *grown = realloc(sky_name, new_len);
                if (grown == NULL) {
                    printf("sys_map_apply_spawn: failed to grow sky_name\n");
                    continue;
                }
                sky_name = grown;
                memcpy(sky_name, sky_str, new_len);
            }
        } else if (strcmp(classname, "info_player_start") == 0) {
            if (fpcam_entity == ECS_INVALID) {
                continue;
            }
            const char *origin_str = bsp_entity_get(e, "origin");
            if (origin_str != NULL) {
                printf("{\n\"classname\" \"%s\"\n\"origin\" \"%s\"\n", classname, origin_str);
                Vector3 pos = parse_origin(origin_str);
                sys_fpcam_set_position(w, fpcam_entity, pos);
            }

            const char *angle_str = bsp_entity_get(e, "angle");
            if (angle_str != NULL) {
                printf("\"angle\" \"%s\"\n", angle_str);
                float angle = strtof(angle_str, NULL);
                sys_fpcam_set_yaw(w, fpcam_entity, angle);
            }
            puts("}\n");
        }
    }

    if (skybox_entity != ECS_INVALID) {
        tex_handle sides[6] = {0};
        load_skybox_sides(texmgr, sky_name, sides);
        // suffix order in load_skybox_sides is {ft, dn, bk, lf, rt, up}
        sys_skybox_set_sides(w,
                             skybox_entity,
                             sides[0], // ft
                             sides[2], // bk
                             sides[3], // lf
                             sides[4], // rt
                             sides[5], // up
                             sides[1]);// dn
    }

    free(sky_name);
}

void sys_map_render(ecs_world *w,
                    res_map_mgr *mapmgr,
                    res_mesh_mgr *meshmgr,
                    Vector3 cam_pos) {
    if (w == NULL || mapmgr == NULL || meshmgr == NULL) {
        return;
    }

    ecs_iter it = ecs_query(w, g_c_map);
    ecs_entity e = ECS_INVALID;
    void *data = NULL;
    while (ecs_iter_next(&it, &e, &data)) {
        c_map *cm = (c_map *) data;

        map_view view = {0};
        if (!res_map_get(mapmgr, cm->map, &view)) {
            continue;
        }
        const mesh *m = res_mesh_get(meshmgr, view.mesh);
        if (m == NULL) {
            continue;
        }
        r_draw_mesh(m, cam_pos);
    }
}
