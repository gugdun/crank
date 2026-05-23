#ifndef SYS_INPUT_H
#define SYS_INPUT_H

#include "ecs/ecs.h"
#include "raylib.h"

typedef struct {
    Vector2 mouse_delta;

    float in_fwd;
    float in_rt;
    float in_up;

    int jump_pressed;
    int jump_down;

    int noclip_pressed;
    int noclip_down;

    int fullscreen_pressed;
    int fullscreen_down;
} c_input;

void sys_input_register(ecs_world* w);
void sys_input_update(ecs_world* w);

#endif
