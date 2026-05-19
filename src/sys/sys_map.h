#ifndef SYS_MAP_H
#define SYS_MAP_H

#include "ecs/ecs.h"
#include "res/res_map.h"
#include "res/res_mesh.h"
#include "res/res_texture.h"
#include "raylib.h"

typedef struct {
    map_handle map;
} c_map;

void       sys_map_register(ecs_world *w);
ecs_entity sys_map_spawn(ecs_world *w, map_handle h);

// Reads info_player_start from the map's BSP and writes position/yaw into the
// fpcam entity. Reads worldspawn.sky and loads the six skybox sides via texmgr,
// then configures the skybox entity. Either of fpcam_entity / skybox_entity may
// be ECS_INVALID to skip that step.
void sys_map_apply_spawn(ecs_world *w,
                         ecs_entity map_entity,
                         ecs_entity fpcam_entity,
                         ecs_entity skybox_entity,
                         res_texture_mgr *texmgr,
                         res_map_mgr *mapmgr);

void sys_map_render(ecs_world *w,
                    res_map_mgr *mapmgr,
                    res_mesh_mgr *meshmgr,
                    Vector3 cam_pos);

#endif
