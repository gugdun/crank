#ifndef SYS_FPCAM_H
#define SYS_FPCAM_H

#include "ecs/ecs.h"
#include "raylib.h"

#include <stdint.h>

typedef struct {
    Vector3 position;       // raylib space (y up); simulation pose, advanced by physics tick
    float   yaw;            // degrees, around +Y; simulation yaw, sampled into usercmd at finalize
    float   pitch;          // degrees, around local right axis, positive = look down

    // Per-tick interpolation history. sys_player_update copies position/
    // yaw/pitch into these *before* integrating each fixed step.
    // sys_view_update reads them to interpolate the rendered eye pose.
    // Not serialised from JSON; runtime-only.
    Vector3 prev_position;
    float   prev_yaw;
    float   prev_pitch;
} c_transform;

typedef struct {
    float   fovy;
    int     projection;
    int     active;         // 1 = this camera is currently used by render systems
    Camera  rl_camera;      // refreshed each frame from c_transform
} c_camera;

typedef struct {
    float run_speed;        // units per second
    float sensitivity;
    float m_yaw;            // degrees per pixel scale (yaw)
    float m_pitch;          // degrees per pixel scale (pitch)
    float pitch_clamp;      // degrees, absolute cap
} c_fpcam;

void       sys_fpcam_register(ecs_world *w);
ecs_entity sys_fpcam_spawn(ecs_world *w, Vector3 position, float yaw_deg);
Camera     sys_fpcam_active(ecs_world *w);

// Setters used by spawn-placement code (avoid leaking component ids).
// Both also seed the corresponding prev_* interpolation history so the
// next rendered frame does not lerp from the pre-placement pose.
void       sys_fpcam_set_position(ecs_world *w, ecs_entity e, Vector3 position);
void       sys_fpcam_set_yaw(ecs_world *w, ecs_entity e, float yaw_deg);

#endif
