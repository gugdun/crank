#ifndef SYS_VIEW_H
#define SYS_VIEW_H

#include "ecs/ecs.h"
#include "raylib.h"

// Per-frame view state, decoupled from the simulation pose held in
// c_transform. Mouse-look writes c_view every frame; physics never reads
// it. sys_view_update consumes c_view + interpolated c_transform.position
// to produce the final c_camera.rl_camera each render frame.
typedef struct {
    float yaw;          // degrees, around +Y; matches c_transform.yaw convention
    float pitch;        // degrees, positive = look down
    float eye_height;   // added on Y to the interpolated position to form the eye
} c_view;

void sys_view_register(ecs_world *w);

// Per-frame: consume c_input.mouse_delta and update c_view.yaw/pitch on
// every entity that has both c_view and c_input (uses c_fpcam sensitivity
// fields when present, falls back to sane defaults otherwise). Also owns
// the F11 fullscreen toggle (moved out of sys_fpcam_update).
void sys_view_look(ecs_world *w);

// Per-frame, called after the fixed-step loop. For each entity that has
// c_transform + c_view + c_camera, interpolate position by `alpha`
// (accumulator / fixed_dt, in [0, 1]) between prev_position and position,
// add eye_height on Y, and rebuild c_camera.rl_camera from the c_view
// yaw/pitch. This is the *only* writer of c_camera.rl_camera for entities
// with c_view.
void sys_view_update(ecs_world *w, float alpha);

#endif
