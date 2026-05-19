#ifndef SYS_FPCAM_H
#define SYS_FPCAM_H

#include "ecs/ecs.h"
#include "raylib.h"

#include <stdint.h>

typedef struct {
    Vector3 position;       // raylib space (y up)
    float   yaw;            // degrees, around +Y
    float   pitch;          // degrees, around local right axis, positive = look down
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
void       sys_fpcam_update(ecs_world *w, float dt);
Camera     sys_fpcam_active(ecs_world *w);

// Setters used by sys_map_apply_spawn (avoid leaking component ids).
void       sys_fpcam_set_position(ecs_world *w, ecs_entity e, Vector3 position);
void       sys_fpcam_set_yaw(ecs_world *w, ecs_entity e, float yaw_deg);

#endif
