#include "sys_sim.h"

#include "sys_player.h"
#include "sys_usercmd.h"

void sys_sim_tick(ecs_world *w, const phys_world *phys, float fixed_dt) {
    sys_usercmd_finalize(w, fixed_dt);
    sys_player_update(w, phys);
}
