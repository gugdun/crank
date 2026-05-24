#ifndef SYS_PLAYER_H
#define SYS_PLAYER_H

#include "ecs/ecs.h"
#include "phys.h"
#include "raylib.h"

// Quake-style first-person player controller.
//
// Owns c_player + c_velocity. Reads (and writes) c_transform from sys_fpcam.
// All units are in BSP/raylib world units (Quake II's "32 units = 1 m").
typedef struct {
    Vector3 half_extents;       // AABB half-size, e.g. {16, 28, 16}
    float   step_height;        // max step-up

    float   accelerate;         // ground acceleration
    float   air_accelerate;     // air acceleration
    float   air_wishspeed_cap;  // GoldSrc-style cap on wishspeed in air (30 = HL)
    float   max_speed;          // target horizontal speed
    float   friction;           // ground friction
    float   stop_speed;         // floor below which friction uses stop_speed
    float   gravity;            // downward accel (units/sec^2)
    float   jump_speed;         // initial upward velocity on jump

    int     on_ground;          // updated each frame: 1 if standing on a surface
    int     noclip;             // 1 disables physics; fly through everything
} c_player;

typedef struct {
    Vector3 velocity;           // raylib space (y up), units/sec
} c_velocity;

void sys_player_register(ecs_world *w);

// Run physics + input on every entity that owns a c_player.
// Writes c_transform.position and c_velocity.velocity. Refreshes any attached
// c_camera so sys_fpcam_active returns the up-to-date camera matrix.
void sys_player_update(ecs_world *w, const phys_world *phys);

#endif
