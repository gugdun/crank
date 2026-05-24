#ifndef SYS_SIM_H
#define SYS_SIM_H

#include "ecs/ecs.h"
#include "phys.h"

// Single fixed-rate simulation tick: finalize one user command and run
// the player controller for fixed_dt seconds. Called from main.c inside
// the accumulator while-loop. Per-tick interpolation history is
// snapshotted inside sys_player_update so it stays correct across
// multiple ticks within a frame.
void sys_sim_tick(ecs_world *w, const phys_world *phys, float fixed_dt);

#endif
