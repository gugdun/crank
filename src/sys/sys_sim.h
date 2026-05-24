/*
===========================================================================
Copyright (C) 2026 gugdun

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, see <https://www.gnu.org/licenses/>.
===========================================================================
*/

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
