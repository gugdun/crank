#include "raylib.h"
#include "rlgl.h"

#include "bsp.h"
#include "ecs/ecs.h"
#include "ecs/entity.h"
#include "render.h"
#include "res/res_map.h"
#include "res/res_mesh.h"
#include "res/res_texture.h"
#include "sjson.h"
#include "sys/sys_fpcam.h"
#include "sys/sys_map.h"
#include "sys/sys_skybox.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_SKY     "unit1_"
#define SKY_PATH_PREFIX "env/"

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

int main(int argc, char *argv[]) {
    const char *map_name = "base1";
    if (argc > 1) {
        map_name = argv[1];
    }

    int width = 1280;
    int height = 720;

    InitWindow(width, height, "crank");
    SetTargetFPS(300);
    DisableCursor();

    r_init();
    rlSetClipPlanes(0.1, 8192.0);

    res_texture_mgr *texmgr  = res_texture_create();
    res_mesh_mgr    *meshmgr = res_mesh_create();
    res_map_mgr     *mapmgr  = res_map_create(meshmgr);

    if (texmgr == NULL || meshmgr == NULL || mapmgr == NULL) {
        printf("main: failed to create resource managers\n");
        if (mapmgr != NULL) res_map_destroy(mapmgr);
        if (meshmgr != NULL) res_mesh_destroy(meshmgr);
        if (texmgr != NULL) res_texture_destroy(texmgr);
        r_shutdown();
        CloseWindow();
        return 1;
    }

    ecs_world *world = ecs_world_create();
    if (world == NULL) {
        printf("main: failed to create world\n");
        res_map_destroy(mapmgr);
        res_mesh_destroy(meshmgr);
        res_texture_destroy(texmgr);
        r_shutdown();
        CloseWindow();
        return 1;
    }

    sys_fpcam_register(world);
    sys_map_register(world);
    sys_skybox_register(world);

    sjson_context *sctx = sjson_create_context(512, 4096, NULL);
    if (sctx == NULL) {
        printf("main: failed to create sjson context\n");
        ecs_world_destroy(world);
        res_map_destroy(mapmgr);
        res_mesh_destroy(meshmgr);
        res_texture_destroy(texmgr);
        r_shutdown();
        CloseWindow();
        return 1;
    }

    // Spawn skybox from JSON, or fall back to hardcoded spawn.
    ecs_entity sky_e = entity_spawn_from_file(world, sctx, "entities/skybox.json");
    if (sky_e == ECS_INVALID) {
        sky_e = sys_skybox_spawn(world);
    }

    map_handle map_h = res_map_load(mapmgr, map_name);
    if (map_h == 0) {
        printf("main: failed to load map %s\n", map_name);
        sjson_destroy_context(sctx);
        ecs_world_destroy(world);
        res_map_destroy(mapmgr);
        res_mesh_destroy(meshmgr);
        res_texture_destroy(texmgr);
        r_shutdown();
        CloseWindow();
        return 1;
    }

    // Get bsp_model for entity processing and sky lookup.
    map_view view = {0};
    if (!res_map_get(mapmgr, map_h, &view)) {
        printf("main: failed to get map view\n");
        sjson_destroy_context(sctx);
        ecs_world_destroy(world);
        res_map_destroy(mapmgr);
        res_mesh_destroy(meshmgr);
        res_texture_destroy(texmgr);
        r_shutdown();
        CloseWindow();
        return 1;
    }

    // Process all BSP entities as JSON archetypes.
    int spawned = sys_map_process_entities(world, view.bsp, sctx);
    if (spawned < 0) {
        printf("main: failed to process BSP entities\n");
    }

    // Post-processing: attach the map handle to the worldspawn entity.
    {
        ecs_component_id c_map_id = ecs_lookup(world, "c_map");
        ecs_iter it = ecs_query(world, c_map_id);
        ecs_entity e;
        void *data;
        while (ecs_iter_next(&it, &e, &data)) {
            c_map *m = (c_map *)data;
            if (m->map == 0) {
                m->map = map_h;
                break;
            }
        }
    }

    // Post-processing: read BSP worldspawn.sky and configure skybox.
    {
        const char *sky_prefix = NULL;
        for (uint32_t i = 0; i < view.bsp->num_entities; i++) {
            const bsp_entity *be = &view.bsp->entities[i];
            const char *classname = bsp_entity_get(be, "classname");
            if (classname != NULL && strcmp(classname, "worldspawn") == 0) {
                const char *sky = bsp_entity_get(be, "sky");
                if (sky != NULL) {
                    sky_prefix = sky;
                }
                break;
            }
        }

        // If BSP has no sky, fall back to worldspawn component or default.
        if (sky_prefix == NULL) {
            ecs_component_id c_worldspawn_id = ecs_lookup(world, "c_worldspawn");
            ecs_iter it = ecs_query(world, c_worldspawn_id);
            ecs_entity e;
            void *data;
            while (ecs_iter_next(&it, &e, &data)) {
                c_worldspawn *ws = (c_worldspawn *)data;
                if (ws->sky_prefix[0] != '\0') {
                    sky_prefix = ws->sky_prefix;
                }
                break;
            }
        }

        if (sky_prefix == NULL) {
            sky_prefix = DEFAULT_SKY;
        }

        tex_handle sides[6] = {0};
        load_skybox_sides(texmgr, sky_prefix, sides);
        // suffix order in load_skybox_sides is {ft, dn, bk, lf, rt, up}
        sys_skybox_set_sides(world,
                             sky_e,
                             sides[0], // ft
                             sides[2], // bk
                             sides[3], // lf
                             sides[4], // rt
                             sides[5], // up
                             sides[1]);// dn
    }

    // Spawn player from JSON, or fall back to hardcoded spawn.
    ecs_entity player_e = entity_spawn_from_file(world, sctx, "entities/player.json");
    if (player_e == ECS_INVALID) {
        player_e = sys_fpcam_spawn(world, (Vector3){0.0f, 0.0f, 0.0f}, 0.0f);
    }

    // Place player at the first spawn point.
    {
        ecs_component_id c_spawn_point_id = ecs_lookup(world, "c_spawn_point");
        ecs_component_id c_transform_id = ecs_lookup(world, "c_transform");
        ecs_iter it = ecs_query(world, c_spawn_point_id);
        ecs_entity e;
        void *data;
        while (ecs_iter_next(&it, &e, &data)) {
            c_transform *st = ecs_get(world, e, c_transform_id);
            c_transform *pt = ecs_get(world, player_e, c_transform_id);
            if (st != NULL && pt != NULL) {
                *pt = *st;
                break;
            }
        }
    }

    while (!WindowShouldClose()) {
        float delta = GetFrameTime();
        sys_fpcam_update(world, delta);

        Camera cam = sys_fpcam_active(world);

        BeginDrawing();
        ClearBackground(BLACK);
        BeginMode3D(cam);
            sys_skybox_render(world, texmgr, cam.position);
            sys_map_render(world, mapmgr, meshmgr, cam.position);
        EndMode3D();
        DrawFPS(16, 16);
        EndDrawing();
    }

    sjson_destroy_context(sctx);
    ecs_world_destroy(world);
    res_map_destroy(mapmgr);
    res_mesh_destroy(meshmgr);
    res_texture_destroy(texmgr);
    r_shutdown();
    CloseWindow();
    return 0;
}
