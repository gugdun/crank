#ifndef SYS_SKYBOX_H
#define SYS_SKYBOX_H

#include "ecs/ecs.h"
#include "res/res_texture.h"
#include "raylib.h"

typedef struct {
    tex_handle ft;
    tex_handle bk;
    tex_handle lf;
    tex_handle rt;
    tex_handle up;
    tex_handle dn;
    float      size;        // world units; default 4096
} c_skybox;

void       sys_skybox_register(ecs_world *w);
ecs_entity sys_skybox_spawn(ecs_world *w);
void       sys_skybox_set_sides(ecs_world *w,
                                ecs_entity e,
                                tex_handle ft,
                                tex_handle bk,
                                tex_handle lf,
                                tex_handle rt,
                                tex_handle up,
                                tex_handle dn);
void       sys_skybox_render(ecs_world *w,
                             const res_texture_mgr *texmgr,
                             Vector3 cam_pos);

#endif
