#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include "bsp.h"
#include "mesh.h"
#include "render.h"
#include "texture.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

Vector3 vec3_parse(const char *str) {
    Vector3 result = (Vector3){0};

    char *s = calloc(1, strlen(str) + 1);
    strcpy(s, str);

    char *x_str = strtok(s, " ");
    if (x_str == NULL) {
        printf("vec3_parse: wrong input format\n");
        free(s);
        return (Vector3){0};
    }
    result.x = strtof(x_str, NULL);

    char *y_str = strtok(NULL, " ");
    if (y_str == NULL) {
        printf("vec3_parse: wrong input format\n");
        free(s);
        return (Vector3){0};
    }
    result.y = strtof(y_str, NULL);

    char *z_str = strtok(NULL, " ");
    if (z_str == NULL) {
        printf("vec3_parse: wrong input format\n");
        free(s);
        return (Vector3){0};
    }
    result.z = strtof(z_str, NULL);

    free(s);

    return (Vector3){result.x, result.z, -result.y};
}

int main(int argc, char *argv[]) {
    char *bsp_path = "base1";
    if (argc > 1) {
        bsp_path = argv[1];
    }

    int width = 1280;
    int height = 720;

    InitWindow(width, height, "crank");
    SetTargetFPS(300);
    DisableCursor();

    r_init();

    bsp_model *bsp = bsp_load(bsp_path);
    if (bsp == NULL) {
        printf("main: failed to load bsp\n");
        CloseWindow();
        return 1;
    }

    mesh *m = mesh_from_bsp(bsp);

    const char *default_sky = "unit1_";
    char *sky_name = calloc(1, strlen(default_sky) + 1);
    strcpy(sky_name, default_sky);
    texture *sky_tex[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
    const char *sky_suffix[6] = {"ft", "dn", "bk", "lf", "rt", "up"};

    Camera camera = {0};
    camera.position = (Vector3){0.0f, 0.0f, 0.0f};
    camera.target = (Vector3){1.0f, 0.0f, 0.0f};
    camera.up = (Vector3){0.0f, 1.0f, 0.0f};
    camera.fovy = 90.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    for (uint32_t i = 0; i < bsp->num_entities; i++) {
        bsp_entity *e = &bsp->entities[i];
        if (e == NULL) continue;

        const char *classname = bsp_entity_get(e, "classname");
        if (classname == NULL) continue;

        if (strcmp(classname, "worldspawn") == 0) {
            const char *sky_str = bsp_entity_get(e, "sky");
            if (sky_str != NULL) {
                printf("{\n\"classname\" \"%s\"\n\"sky\" \"%s\"\n}\n", classname, sky_str);
                sky_name = realloc(sky_name, strlen(sky_str) + 1);
                strcpy(sky_name, sky_str);
            }
        } else if (strcmp(classname, "info_player_start") == 0) {
            const char *origin_str = bsp_entity_get(e, "origin");
            if (origin_str != NULL) {
                printf("{\n\"classname\" \"%s\"\n\"origin\" \"%s\"\n", classname, origin_str);
                camera.position = vec3_parse(origin_str);
                camera.target = (Vector3){camera.position.x + 1.0f, camera.position.y, camera.position.z};
            }

            const char *angle_str = bsp_entity_get(e, "angle");
            if (angle_str != NULL) {
                printf("\"angle\" \"%s\"\n", angle_str);
                float angle = strtof(angle_str, NULL);
                float rad = angle * DEG2RAD;
                Vector3 dir = (Vector3){cosf(rad), 0.0f, -sinf(rad)};
                camera.target = Vector3Add(camera.position, dir);
            }
            puts("}\n");
        }
    }

    size_t path_len = 4 + strlen(sky_name) + 2;
    char *sky_path = calloc(1, path_len + 1);
    for (int j = 0; j < 6; j++) {
        strcpy(sky_path, "env/");
        strcat(sky_path, sky_name);
        strcat(sky_path, sky_suffix[j]);
        sky_tex[j] = tex_load(sky_path);
        SetTextureWrap(sky_tex[j]->rl_texture, TEXTURE_WRAP_CLAMP);
    }
    free(sky_path);

    rlSetClipPlanes(0.1, 8192.0);

    float run_speed = 320.0f;
    float sensitivity = 1.0f;
    float m_yaw = 0.022f;
    float m_pitch = 0.022f;

    while (!WindowShouldClose()) {
        float delta = GetFrameTime();

        int forward = IsKeyDown(KEY_W);
        int backward = IsKeyDown(KEY_S);
        int left = IsKeyDown(KEY_A);
        int right = IsKeyDown(KEY_D);
        int up = IsKeyDown(KEY_SPACE);
        int down = IsKeyDown(KEY_LEFT_SHIFT);
        
        if (IsKeyPressed(KEY_F11)) ToggleFullscreen();

        UpdateCameraPro(&camera,
                        (Vector3){
                            ((float)forward * run_speed - (float)backward * run_speed) * delta,
                            ((float)right * run_speed - (float)left * run_speed) * delta,
                            ((float)up * run_speed - (float)down * run_speed) * delta,
                        },
                        (Vector3){
                            GetMouseDelta().x * sensitivity * m_yaw,
                            GetMouseDelta().y * sensitivity * m_pitch,
                            0.0f
                        },
                        0.0f);

        BeginDrawing();
        ClearBackground(BLACK);

        BeginMode3D(camera);
        if (sky_tex[0] && sky_tex[1] && sky_tex[2] && sky_tex[3] && sky_tex[4] && sky_tex[5]) {
            r_draw_sky(camera.position, sky_tex[0]->id, sky_tex[1]->id, sky_tex[2]->id, sky_tex[3]->id, sky_tex[4]->id, sky_tex[5]->id);
        }
        r_draw_mesh(m);
        EndMode3D();

        DrawFPS(16, 16);
        EndDrawing();
    }

    for (int i = 0; i < 6; i++) {
        if (sky_tex[i] != NULL) tex_free(sky_tex[i]);
    }

    mesh_free(m);
    bsp_free(bsp);
    free(sky_name);
    r_shutdown();
    CloseWindow();
}
