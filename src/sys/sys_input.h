#ifndef SYS_INPUT_H
#define SYS_INPUT_H

#include "ecs/ecs.h"
#include "raylib.h"

typedef struct {
    Vector2 mouse_delta;
} c_input;

void sys_input_register(ecs_world* w);
void sys_input_update(ecs_world* w);

#endif
