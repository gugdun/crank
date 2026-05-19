#include "raylib.h"
#include "rlgl.h"

#include "ecs/ecs.h"
#include "render.h"
#include "res/res_map.h"
#include "res/res_mesh.h"
#include "res/res_texture.h"
#include "sys/sys_fpcam.h"
#include "sys/sys_map.h"
#include "sys/sys_skybox.h"

#include <stdio.h>

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

    map_handle map_h = res_map_load(mapmgr, map_name);
    if (map_h == 0) {
        printf("main: failed to load map %s\n", map_name);
        ecs_world_destroy(world);
        res_map_destroy(mapmgr);
        res_mesh_destroy(meshmgr);
        res_texture_destroy(texmgr);
        r_shutdown();
        CloseWindow();
        return 1;
    }

    ecs_entity map_ent = sys_map_spawn(world, map_h);
    ecs_entity sky_ent = sys_skybox_spawn(world);
    ecs_entity cam_ent = sys_fpcam_spawn(world, (Vector3){0.0f, 0.0f, 0.0f}, 0.0f);
    sys_map_apply_spawn(world, map_ent, cam_ent, sky_ent, texmgr, mapmgr);

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

    ecs_world_destroy(world);
    res_map_destroy(mapmgr);
    res_mesh_destroy(meshmgr);
    res_texture_destroy(texmgr);
    r_shutdown();
    CloseWindow();
    return 0;
}
