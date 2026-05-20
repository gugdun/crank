#include "sys_map.h"

#include "bsp.h"
#include "ecs/ecs.h"
#include "ecs/entity.h"
#include "mesh.h"
#include "render.h"
#include "res/res_map.h"
#include "res/res_mesh.h"
#include "res/res_texture.h"
#include "sjson.h"
#include "sys/sys_fpcam.h"
#include "raylib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ecs_component_id g_c_map         = ECS_MAX_COMPONENTS;
static ecs_component_id g_c_worldspawn  = ECS_MAX_COMPONENTS;
static ecs_component_id g_c_spawn_point = ECS_MAX_COMPONENTS;

static void read_c_map(void *data, sjson_node *node) {
    (void)node;
    memset(data, 0, sizeof(c_map));
}

static void read_c_worldspawn(void *data, sjson_node *node) {
    c_worldspawn *ws = data;
    const char *prefix = sjson_get_string(node, "sky_prefix", NULL);
    if (prefix != NULL) {
        size_t n = strlen(prefix);
        if (n >= sizeof(ws->sky_prefix)) {
            n = sizeof(ws->sky_prefix) - 1;
        }
        memcpy(ws->sky_prefix, prefix, n);
        ws->sky_prefix[n] = '\0';
    } else {
        ws->sky_prefix[0] = '\0';
    }
}

static void read_c_spawn_point(void *data, sjson_node *node) {
    c_spawn_point *sp = data;
    sp->team = sjson_get_int(node, "team", 0);
}

void sys_map_register(ecs_world *w) {
    if (w == NULL) {
        printf("sys_map_register: w = NULL\n");
        return;
    }
    g_c_map         = ecs_register(w, "c_map",         sizeof(c_map),         NULL, read_c_map);
    g_c_worldspawn  = ecs_register(w, "c_worldspawn",  sizeof(c_worldspawn),  NULL, read_c_worldspawn);
    g_c_spawn_point = ecs_register(w, "c_spawn_point", sizeof(c_spawn_point), NULL, read_c_spawn_point);
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

int sys_map_process_entities(ecs_world *w,
                              const bsp_model *bsp,
                              sjson_context *sctx) {
    if (w == NULL || bsp == NULL || sctx == NULL) {
        printf("sys_map_process_entities: invalid arguments\n");
        return -1;
    }

    int spawned = 0;
    ecs_component_id c_transform_id = ecs_lookup(w, "c_transform");

    for (uint32_t i = 0; i < bsp->num_entities; i++) {
        const bsp_entity *be = &bsp->entities[i];
        const char *classname = bsp_entity_get(be, "classname");
        if (classname == NULL) {
            continue;
        }

        // Build path: entities/<classname>.json
        char path[256];
        int n = snprintf(path, sizeof(path), "entities/%s.json", classname);
        if (n < 0 || (size_t)n >= sizeof(path)) {
            printf("sys_map_process_entities: path too long for '%s'\n", classname);
            continue;
        }

        ecs_entity e = entity_spawn_from_file(w, sctx, path);
        if (e == ECS_INVALID) {
            // No JSON archetype for this classname; silently skip.
            continue;
        }

        // Apply BSP origin/angle overrides to c_transform when present.
        if (c_transform_id < ECS_MAX_COMPONENTS) {
            c_transform *t = ecs_get(w, e, c_transform_id);
            if (t != NULL) {
                const char *origin = bsp_entity_get(be, "origin");
                if (origin != NULL) {
                    t->position = parse_origin(origin);
                }
                const char *angle = bsp_entity_get(be, "angle");
                if (angle != NULL) {
                    t->yaw = strtof(angle, NULL);
                }
            }
        }

        spawned++;
    }

    return spawned;
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
