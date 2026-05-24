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

#include "sys_player.h"
#include "sys_fpcam.h"
#include "sys_usercmd.h"

#include "ecs/ecs.h"
#include "phys.h"
#include "sjson.h"
#include "raylib.h"
#include "raymath.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static ecs_component_id g_c_player   = ECS_MAX_COMPONENTS;
static ecs_component_id g_c_velocity = ECS_MAX_COMPONENTS;

#define OVERCLIP        1.0001f
#define MAX_CLIP_PLANES 5
#define STOP_EPSILON    0.1f

// GoldSrc PM_CategorizePosition uses 180 u/s as the threshold above which a
// downward ground re-test is skipped. We reuse the same number so that the
// snap-to-ground logic cannot eat a fresh jump impulse.
#define GROUND_VY_GUARD 180.0f

// A plane whose y-normal is at least this counts as walkable ground.
#define WALKABLE_NORMAL_Y 0.7f

static void read_c_player(void *data, sjson_node *node) {
    c_player *p = data;
    sjson_get_floats((float *)&p->half_extents, 3, node, "half_extents");
    if (p->half_extents.x == 0.0f && p->half_extents.y == 0.0f && p->half_extents.z == 0.0f) {
        p->half_extents = (Vector3){16.0f, 28.0f, 16.0f};
    }
    // Note: render-time eye_height lives on c_view (see sys_view.h),
    // not on c_player. This keeps the simulation pose pure and lets the
    // view system own all rendering-side offsets.
    p->step_height       = sjson_get_float(node, "step_height",       18.0f);
    p->accelerate        = sjson_get_float(node, "accelerate",        10.0f);
    p->air_accelerate    = sjson_get_float(node, "air_accelerate",    10.0f);
    p->air_wishspeed_cap = sjson_get_float(node, "air_wishspeed_cap", 30.0f);
    p->max_speed         = sjson_get_float(node, "max_speed",         320.0f);
    p->friction          = sjson_get_float(node, "friction",          6.0f);
    p->stop_speed        = sjson_get_float(node, "stop_speed",        100.0f);
    p->gravity           = sjson_get_float(node, "gravity",           800.0f);
    p->jump_speed        = sjson_get_float(node, "jump_speed",        270.0f);
    p->on_ground         = 0;
    p->noclip            = sjson_get_bool(node, "noclip", false) ? 1 : 0;
}

static void read_c_velocity(void *data, sjson_node *node) {
    c_velocity *v = data;
    sjson_get_floats((float *)&v->velocity, 3, node, "velocity");
}

void sys_player_register(ecs_world *w) {
    if (w == NULL) {
        printf("sys_player_register: w = NULL\n");
        return;
    }
    g_c_player   = ecs_register(w, "c_player",   sizeof(c_player),   NULL, read_c_player);
    g_c_velocity = ecs_register(w, "c_velocity", sizeof(c_velocity), NULL, read_c_velocity);
}

// Q2-style PM_ClipVelocity: reflect `in` off the plane `normal`, scaled by
// `overbounce` (typically 1.001). Returns the velocity sliding along the plane.
static Vector3 clip_velocity(Vector3 in, Vector3 normal, float overbounce) {
    float backoff = Vector3DotProduct(in, normal) * overbounce;
    Vector3 out;
    out.x = in.x - normal.x * backoff;
    out.y = in.y - normal.y * backoff;
    out.z = in.z - normal.z * backoff;
    if (fabsf(out.x) < STOP_EPSILON) out.x = 0.0f;
    if (fabsf(out.y) < STOP_EPSILON) out.y = 0.0f;
    if (fabsf(out.z) < STOP_EPSILON) out.z = 0.0f;
    return out;
}

// Slide along contact planes (Q2 PM_SlideMove with a GoldSrc tweak). Walks up
// to MAX_CLIP_PLANES iterations, each iteration clipping velocity against the
// latest plane it hit.
//
// GoldSrc tweak: when the contact plane is walkable ground (normal.y >= 0.7)
// and the player was moving up the slope, rescale the horizontal component
// of the clipped velocity so that |xz| is preserved. This is the source of
// the GoldSrc "no speed loss on slopes" feel and is required for strong
// uphill strafing.
//
// Returns updated position via *pos and updated velocity via *vel.
// `time_left` is the remaining dt for the move.
static void slide_move(const phys_world *phys,
                       Vector3 *pos,
                       Vector3 *vel,
                       Vector3 half_extents,
                       uint32_t mask,
                       float time_left) {
    Vector3 planes[MAX_CLIP_PLANES];
    int num_planes = 0;

    for (int bump = 0; bump < 4; bump++) {
        if (time_left <= 0.0f) break;
        Vector3 end;
        end.x = pos->x + vel->x * time_left;
        end.y = pos->y + vel->y * time_left;
        end.z = pos->z + vel->z * time_left;

        phys_trace tr;
        phys_trace_box(phys, *pos, end, half_extents, mask, &tr);

        if (tr.allsolid) {
            // Trapped; cancel vertical velocity to avoid sinking forever.
            vel->y = 0.0f;
            return;
        }

        if (tr.fraction > 0.0f) {
            *pos = tr.endpos;
            num_planes = 0;
        }

        if (tr.fraction == 1.0f) {
            return;
        }

        time_left -= time_left * tr.fraction;

        if (num_planes >= MAX_CLIP_PLANES) {
            // Should not happen; bail out.
            *vel = (Vector3){0, 0, 0};
            return;
        }

        // If this is a plane we've already clipped against, nudge along it.
        int i;
        for (i = 0; i < num_planes; i++) {
            if (Vector3DotProduct(tr.plane_normal, planes[i]) > 0.99f) {
                vel->x += tr.plane_normal.x;
                vel->y += tr.plane_normal.y;
                vel->z += tr.plane_normal.z;
                break;
            }
        }
        if (i < num_planes) continue;

        planes[num_planes++] = tr.plane_normal;

        // Try clipping velocity against all current planes; first one whose
        // result actually points off the plane wins. If two planes mutually
        // block the move, clip along their crease.
        int j;
        for (i = 0; i < num_planes; i++) {
            // Pre-clip horizontal speed (for the GoldSrc slope-speed boost).
            float pre_xz = sqrtf(vel->x * vel->x + vel->z * vel->z);

            Vector3 clipped = clip_velocity(*vel, planes[i], OVERCLIP);

            // GoldSrc slope-speed preservation: if we just clipped against a
            // walkable plane, restore the horizontal speed magnitude. This
            // keeps strafes on inclines responsive instead of bleeding into
            // the normal direction.
            if (planes[i].y >= WALKABLE_NORMAL_Y) {
                float post_xz = sqrtf(clipped.x * clipped.x + clipped.z * clipped.z);
                if (post_xz > 0.001f && pre_xz > post_xz) {
                    float scale = pre_xz / post_xz;
                    clipped.x *= scale;
                    clipped.z *= scale;
                }
            }

            // Make sure clipped velocity moves us away from every other plane.
            for (j = 0; j < num_planes; j++) {
                if (j == i) continue;
                if (Vector3DotProduct(clipped, planes[j]) < 0.0f) {
                    break;
                }
            }
            if (j == num_planes) {
                *vel = clipped;
                break;
            }
        }
        if (i == num_planes) {
            // Two-plane crease: move along the cross product of the two normals.
            if (num_planes != 2) {
                *vel = (Vector3){0, 0, 0};
                return;
            }
            Vector3 dir = Vector3CrossProduct(planes[0], planes[1]);
            float d = Vector3DotProduct(dir, *vel);
            *vel = Vector3Scale(dir, d);
        }
    }
}

// Step-up wrapper. Try the slide move; if blocked, also try a vertical-up,
// horizontal-slide, vertical-down sequence and keep whichever ended further
// along the original move direction. Standard Q2 PM_StepSlideMove.
static void step_slide_move(const phys_world *phys,
                            Vector3 *pos,
                            Vector3 *vel,
                            Vector3 half_extents,
                            uint32_t mask,
                            float step_height,
                            int on_ground,
                            float dt) {
    Vector3 start_pos = *pos;
    Vector3 start_vel = *vel;

    Vector3 down_pos = *pos;
    Vector3 down_vel = *vel;
    slide_move(phys, &down_pos, &down_vel, half_extents, mask, dt);

    if (!on_ground) {
        // In the air, no step-up; just take the slide result.
        *pos = down_pos;
        *vel = down_vel;
        return;
    }

    // Step up by step_height (clipped against ceiling).
    Vector3 up_pos = start_pos;
    Vector3 up_end = start_pos;
    up_end.y += step_height;
    phys_trace tr;
    phys_trace_box(phys, start_pos, up_end, half_extents, mask, &tr);
    if (tr.allsolid) {
        // Cannot step up: keep the slide result.
        *pos = down_pos;
        *vel = down_vel;
        return;
    }
    up_pos = tr.endpos;

    // Slide from the elevated position.
    Vector3 up_vel = start_vel;
    slide_move(phys, &up_pos, &up_vel, half_extents, mask, dt);

    // Step back down.
    Vector3 step_down_start = up_pos;
    Vector3 step_down_end = up_pos;
    step_down_end.y -= step_height;
    phys_trace_box(phys, step_down_start, step_down_end, half_extents, mask, &tr);
    if (!tr.allsolid) {
        up_pos = tr.endpos;
    }
    // Snap to ground if we landed on something solid pointing up.
    if (tr.fraction < 1.0f && tr.plane_normal.y < WALKABLE_NORMAL_Y) {
        // We didn't land on a walkable surface; reject the step-up.
        *pos = down_pos;
        *vel = down_vel;
        return;
    }

    // Pick whichever (down vs. step-up) ended further from the start, measured
    // in the horizontal plane only.
    float dx_down = down_pos.x - start_pos.x;
    float dz_down = down_pos.z - start_pos.z;
    float dx_up   = up_pos.x   - start_pos.x;
    float dz_up   = up_pos.z   - start_pos.z;
    float d_down2 = dx_down * dx_down + dz_down * dz_down;
    float d_up2   = dx_up   * dx_up   + dz_up   * dz_up;

    if (d_up2 > d_down2) {
        *pos = up_pos;
        // Preserve horizontal velocity from up slide; vertical from up slide too.
        *vel = up_vel;
    } else {
        *pos = down_pos;
        *vel = down_vel;
    }
}

static void apply_friction(const c_player *pl, Vector3 *vel, float dt) {
    // Horizontal speed only.
    float speed = sqrtf(vel->x * vel->x + vel->z * vel->z);
    if (speed < 1.0f) {
        vel->x = 0.0f;
        vel->z = 0.0f;
        return;
    }
    float control = speed < pl->stop_speed ? pl->stop_speed : speed;
    float drop = control * pl->friction * dt;
    float newspeed = speed - drop;
    if (newspeed < 0.0f) newspeed = 0.0f;
    newspeed /= speed;
    vel->x *= newspeed;
    vel->z *= newspeed;
}

static void pm_accelerate(Vector3 *vel, Vector3 wishdir, float wishspeed, float accel, float dt) {
    float currentspeed = vel->x * wishdir.x + vel->y * wishdir.y + vel->z * wishdir.z;
    float addspeed = wishspeed - currentspeed;
    if (addspeed <= 0.0f) return;
    float accelspeed = accel * dt * wishspeed;
    if (accelspeed > addspeed) accelspeed = addspeed;
    vel->x += accelspeed * wishdir.x;
    vel->y += accelspeed * wishdir.y;
    vel->z += accelspeed * wishdir.z;
}

// Compute horizontal wishdir + wishspeed from the user command. wishdir is
// kept strictly horizontal (no projection onto the ground plane); GoldSrc
// behaviour on slopes is produced by the slide-move + ground snap, not by
// rotating the input vector.
static void pm_build_wish(const c_usercmd *cmd, float max_speed,
                          Vector3 *out_wishdir, float *out_wishspeed) {
    float yaw_rad = cmd->yaw * DEG2RAD;
    // sys_fpcam_apply_transform_to_camera: forward at yaw=0 is (1, 0, 0).
    Vector3 fwd_xz   = (Vector3){ cosf(yaw_rad), 0.0f, -sinf(yaw_rad) };
    Vector3 right_xz = (Vector3){-sinf(yaw_rad), 0.0f, -cosf(yaw_rad) };

    Vector3 wishvel = {0, 0, 0};
    wishvel.x = fwd_xz.x * cmd->in_fwd + right_xz.x * cmd->in_rt;
    wishvel.z = fwd_xz.z * cmd->in_fwd + right_xz.z * cmd->in_rt;

    float wishlen = sqrtf(wishvel.x * wishvel.x + wishvel.z * wishvel.z);
    Vector3 wishdir = {0, 0, 0};
    float wishspeed = 0.0f;
    if (wishlen > 0.0f) {
        wishdir.x = wishvel.x / wishlen;
        wishdir.z = wishvel.z / wishlen;
        wishspeed = max_speed;
    }
    *out_wishdir = wishdir;
    *out_wishspeed = wishspeed;
}

// PM_CategorizePosition: short downward trace to decide if we are standing
// on a walkable surface. Returns 1 if grounded.
//
// `just_jumped` short-circuits the trace: on the tick a jump fires we must
// not re-categorize as grounded, otherwise the snap-down at the bottom of
// the move would glue the player back onto a slope. The same guard applies
// when the vertical velocity is still high (> GROUND_VY_GUARD) because that
// only happens on a fresh jump or pad bounce.
static int pm_categorize_position(const phys_world *phys,
                                  Vector3 pos,
                                  Vector3 half_extents,
                                  Vector3 velocity,
                                  int just_jumped,
                                  phys_trace *out_tr) {
    Vector3 end = pos;
    end.y -= 2.0f;
    phys_trace tr;
    phys_trace_box(phys, pos, end, half_extents, PHYS_MASK_PLAYERSOLID, &tr);
    if (out_tr != NULL) *out_tr = tr;

    if (just_jumped) return 0;
    if (velocity.y > GROUND_VY_GUARD) return 0;
    if (tr.fraction >= 1.0f) return 0;
    if (tr.plane_normal.y < WALKABLE_NORMAL_Y) return 0;
    return 1;
}

// Rising-edge jump check. If pressed and we are on the ground, apply jump
// impulse and clear on_ground. Returns 1 if a jump fired this tick.
static int pm_check_jump(c_player *pl, c_velocity *vc,
                         uint16_t prev_buttons, uint16_t curr_buttons) {
    uint16_t prev_jump = prev_buttons & CMD_BUTTON_JUMP;
    uint16_t curr_jump = curr_buttons & CMD_BUTTON_JUMP;
    if (curr_jump && !prev_jump && pl->on_ground) {
        vc->velocity.y = pl->jump_speed;
        pl->on_ground = 0;
        return 1;
    }
    return 0;
}

// Ground move (GoldSrc PM_WalkMove): friction -> accel -> step-slide.
static void pm_walk_move(c_player *pl, c_transform *t, c_velocity *vc,
                         const phys_world *phys, const c_usercmd *cmd) {
    apply_friction(pl, &vc->velocity, cmd->dt_sec);

    // Strip residual downward velocity so the slide move travels horizontally
    // along the floor. The post-move snap re-attaches us to the slope.
    if (vc->velocity.y < 0.0f) vc->velocity.y = 0.0f;

    Vector3 wishdir;
    float   wishspeed;
    pm_build_wish(cmd, pl->max_speed, &wishdir, &wishspeed);

    pm_accelerate(&vc->velocity, wishdir, wishspeed,
                  pl->accelerate, cmd->dt_sec);

    step_slide_move(phys, &t->position, &vc->velocity,
                    pl->half_extents, PHYS_MASK_PLAYERSOLID,
                    pl->step_height, /*on_ground=*/1, cmd->dt_sec);
}

// Air move (GoldSrc PM_AirMove): air-accel with capped wishspeed -> gravity
// -> plain slide. No step-up while airborne.
static void pm_air_move(c_player *pl, c_transform *t, c_velocity *vc,
                        const phys_world *phys, const c_usercmd *cmd) {
    Vector3 wishdir;
    float   wishspeed;
    pm_build_wish(cmd, pl->max_speed, &wishdir, &wishspeed);

    // GoldSrc air control: clamp the wishspeed used by accelerate to a small
    // value (sv_air_wishspeed_cap, default 30). The wishdir itself is
    // unchanged, so strafing into the side of your motion still steers it
    // -- this is what produces the classic HL air-strafe / bunny-hop feel.
    float air_wishspeed = wishspeed;
    if (air_wishspeed > pl->air_wishspeed_cap) {
        air_wishspeed = pl->air_wishspeed_cap;
    }
    pm_accelerate(&vc->velocity, wishdir, air_wishspeed,
                  pl->air_accelerate, cmd->dt_sec);

    vc->velocity.y -= pl->gravity * cmd->dt_sec;

    slide_move(phys, &t->position, &vc->velocity,
               pl->half_extents, PHYS_MASK_PLAYERSOLID, cmd->dt_sec);
}

// Post-move snap-to-ground. After the slide move, if we were almost certainly
// still on the floor (no fresh jump, vy not high), trace a short distance
// down to glue us to the slope. This is what stops the player drifting off
// the surface of a ramp while running along it.
static void pm_snap_to_ground(c_player *pl, c_transform *t, c_velocity *vc,
                              const phys_world *phys, int just_jumped) {
    if (just_jumped) return;
    if (vc->velocity.y > GROUND_VY_GUARD) return;

    Vector3 start = t->position;
    Vector3 end = t->position;
    end.y -= 4.0f;
    phys_trace tr;
    phys_trace_box(phys, start, end, pl->half_extents,
                   PHYS_MASK_PLAYERSOLID, &tr);

    if (tr.allsolid) return;
    if (tr.fraction >= 1.0f) return;
    if (tr.plane_normal.y < WALKABLE_NORMAL_Y) return;

    t->position = tr.endpos;
    if (vc->velocity.y < 0.0f) vc->velocity.y = 0.0f;
    pl->on_ground = 1;
}

void sys_player_update(ecs_world *w, const phys_world *phys) {
    if (w == NULL || phys == NULL) return;

    ecs_component_id c_transform_id     = ecs_lookup(w, "c_transform");
    ecs_component_id c_usercmd_queue_id = ecs_lookup(w, "c_usercmd_queue");
    if (c_transform_id >= ECS_MAX_COMPONENTS) return;

    ecs_iter it = ecs_query(w, g_c_player);
    ecs_entity e = ECS_INVALID;
    void *data = NULL;
    while (ecs_iter_next(&it, &e, &data)) {
        c_player *pl = (c_player *) data;
        c_transform *t = ecs_get(w, e, c_transform_id);
        c_velocity *vc = ecs_get(w, e, g_c_velocity);
        c_usercmd_queue *q = ecs_get(w, e, c_usercmd_queue_id);
        if (t == NULL || vc == NULL || q == NULL) continue;

        c_usercmd cmd;
        uint16_t prev_buttons = q->last.buttons;
        if (!sys_usercmd_consume(q, &cmd)) continue;

        // Snapshot per-tick interpolation history *before* any integration
        // or noclip translation. sys_view_update will lerp prev_* -> current
        // by alpha = accumulator / fixed_dt to produce a smooth eye pose at
        // render time. This runs on every tick so multi-tick frames stay
        // continuous.
        t->prev_position = t->position;
        t->prev_yaw      = t->yaw;
        t->prev_pitch    = t->pitch;

        // The simulation samples its yaw/pitch from the user command, which
        // sys_usercmd_finalize populated from c_view (frame-rate look). This
        // keeps simulation orientation in lock-step with the latest look
        // while leaving render orientation entirely to c_view.
        t->yaw   = cmd.yaw;
        t->pitch = cmd.pitch;

        {
            uint16_t prev_noclip = prev_buttons & CMD_BUTTON_NOCLIP;
            uint16_t curr_noclip = cmd.buttons & CMD_BUTTON_NOCLIP;
            if (curr_noclip && !prev_noclip) {
                pl->noclip = !pl->noclip;
                vc->velocity = (Vector3){0, 0, 0};
            }
        }

        if (pl->noclip) {
            // Fly: direct velocity from input, no physics.
            float yaw_rad = cmd.yaw * DEG2RAD;
            Vector3 fwd_xz   = (Vector3){ cosf(yaw_rad), 0.0f, -sinf(yaw_rad) };
            Vector3 right_xz = (Vector3){-sinf(yaw_rad), 0.0f, -cosf(yaw_rad) };
            Vector3 move = {0, 0, 0};
            move.x = fwd_xz.x * cmd.in_fwd + right_xz.x * cmd.in_rt;
            move.y = cmd.in_up;
            move.z = fwd_xz.z * cmd.in_fwd + right_xz.z * cmd.in_rt;
            float len = Vector3Length(move);
            if (len > 0.0f) {
                move = Vector3Scale(move, 1.0f / len);
            }
            float speed = pl->max_speed;
            t->position.x += move.x * speed * cmd.dt_sec;
            t->position.y += move.y * speed * cmd.dt_sec;
            t->position.z += move.z * speed * cmd.dt_sec;
            vc->velocity = (Vector3){0, 0, 0};
            pl->on_ground = 0;
            continue;
        }

        // ----- GoldSrc PM_PlayerMove order -----
        //
        // 1. CategorizePosition (pre-move): decides whether we are on the
        //    ground for this tick. The vy / just_jumped guards are not yet
        //    in play -- this is the first categorization of the tick so
        //    just_jumped is always 0 here.
        //
        // 2. CheckJump: rising-edge jump. Must run BEFORE friction/accel so
        //    the impulse is not consumed by friction this tick. Sets a flag
        //    that disables every subsequent ground re-categorization /
        //    snap-down for the rest of the tick.
        //
        // 3. WalkMove or AirMove: friction+accel+step_slide on the ground,
        //    air_accel+gravity+slide in the air.
        //
        // 4. Snap-to-ground (only if !just_jumped && vy <= 180): pulls the
        //    player back onto a walkable surface ~4 units below them. This
        //    is what keeps you attached to slopes while running across them.
        //
        // 5. CategorizePosition (post-move): refresh on_ground for next tick.
        //    Same vy / just_jumped guards apply, so a fresh jump leaves us
        //    airborne until vy decays below the guard or we land.

        phys_trace gt;
        pl->on_ground = pm_categorize_position(phys, t->position,
                                               pl->half_extents,
                                               vc->velocity,
                                               /*just_jumped=*/0, &gt);

        int just_jumped = pm_check_jump(pl, vc, prev_buttons, cmd.buttons);

        if (pl->on_ground && !just_jumped) {
            pm_walk_move(pl, t, vc, phys, &cmd);
        } else {
            pm_air_move(pl, t, vc, phys, &cmd);
        }

        pm_snap_to_ground(pl, t, vc, phys, just_jumped);

        pl->on_ground = pm_categorize_position(phys, t->position,
                                               pl->half_extents,
                                               vc->velocity,
                                               just_jumped, NULL);

        // Camera matrix assembly is owned by sys_view_update (called once
        // per frame, with interpolation). sys_player_update no longer
        // touches c_camera.
    }
}
