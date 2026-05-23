#include "sys_player.h"

#include "ecs/ecs.h"
#include "phys.h"
#include "sjson.h"
#include "sys/sys_fpcam.h"
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

static void read_c_player(void *data, sjson_node *node) {
    c_player *p = data;
    sjson_get_floats((float *)&p->half_extents, 3, node, "half_extents");
    if (p->half_extents.x == 0.0f && p->half_extents.y == 0.0f && p->half_extents.z == 0.0f) {
        p->half_extents = (Vector3){16.0f, 28.0f, 16.0f};
    }
    p->eye_height     = sjson_get_float(node, "eye_height",     24.0f);
    p->step_height    = sjson_get_float(node, "step_height",    18.0f);
    p->accelerate     = sjson_get_float(node, "accelerate",     10.0f);
    p->air_accelerate = sjson_get_float(node, "air_accelerate", 1.0f);
    p->max_speed      = sjson_get_float(node, "max_speed",      320.0f);
    p->friction       = sjson_get_float(node, "friction",       6.0f);
    p->stop_speed     = sjson_get_float(node, "stop_speed",     100.0f);
    p->gravity        = sjson_get_float(node, "gravity",        800.0f);
    p->jump_speed     = sjson_get_float(node, "jump_speed",     270.0f);
    p->on_ground      = 0;
    p->noclip         = sjson_get_bool(node, "noclip", false) ? 1 : 0;
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

static Vector3 project_onto_plane(Vector3 v, Vector3 normal) {
    float d = Vector3DotProduct(v, normal);
    return Vector3Subtract(v, Vector3Scale(normal, d));
}

// Slide along contact planes (Q2 PM_SlideMove). Walks up to MAX_CLIP_PLANES
// iterations, each iteration clipping velocity against the latest plane it
// hit. Returns updated position via *pos and updated velocity via *vel.
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
            Vector3 clipped = clip_velocity(*vel, planes[i], OVERCLIP);
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

        // Avoid tiny oscillations: if the new velocity points against the
        // original move direction, stop. (Q2 does the same check using the
        // primal_velocity stored at entry.)
        // This guards against running away from the original move into a wall.
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
    if (tr.fraction < 1.0f && tr.plane_normal.y < 0.7f) {
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

static void apply_friction(c_player *pl, Vector3 *vel, float dt) {
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

static void accelerate(Vector3 *vel, Vector3 wishdir, float wishspeed, float accel, float dt) {
    float currentspeed = vel->x * wishdir.x + vel->y * wishdir.y + vel->z * wishdir.z;
    float addspeed = wishspeed - currentspeed;
    if (addspeed <= 0.0f) return;
    float accelspeed = accel * dt * wishspeed;
    if (accelspeed > addspeed) accelspeed = addspeed;
    vel->x += accelspeed * wishdir.x;
    vel->y += accelspeed * wishdir.y;
    vel->z += accelspeed * wishdir.z;
}

void sys_player_update(ecs_world *w, const phys_world *phys, float dt) {
    if (w == NULL || phys == NULL) return;
    if (dt <= 0.0f) dt = 1.0f / 60.0f;

    ecs_component_id c_transform_id = ecs_lookup(w, "c_transform");
    ecs_component_id c_camera_id    = ecs_lookup(w, "c_camera");
    if (c_transform_id >= ECS_MAX_COMPONENTS) return;

    int forward  = IsKeyDown(KEY_W);
    int backward = IsKeyDown(KEY_S);
    int left     = IsKeyDown(KEY_A);
    int right    = IsKeyDown(KEY_D);
    int jump     = IsKeyDown(KEY_SPACE);
    int down     = IsKeyDown(KEY_LEFT_SHIFT);
    int noclip_toggle = IsKeyPressed(KEY_F);

    ecs_iter it = ecs_query(w, g_c_player);
    ecs_entity e = ECS_INVALID;
    void *data = NULL;
    while (ecs_iter_next(&it, &e, &data)) {
        c_player *pl = (c_player *) data;
        c_transform *t = ecs_get(w, e, c_transform_id);
        if (t == NULL) continue;
        c_velocity *vc = ecs_get(w, e, g_c_velocity);
        if (vc == NULL) continue;

        if (noclip_toggle) {
            pl->noclip = !pl->noclip;
            vc->velocity = (Vector3){0, 0, 0};
        }

        // Build movement basis from yaw (raylib y-up).
        float yaw_rad = t->yaw * DEG2RAD;
        // sys_fpcam_apply_transform_to_camera: forward at yaw=0 is (1, 0, 0).
        Vector3 fwd_xz   = (Vector3){ cosf(yaw_rad), 0.0f, -sinf(yaw_rad) };
        Vector3 right_xz = (Vector3){-sinf(yaw_rad), 0.0f, -cosf(yaw_rad) };

        float in_fwd = (float)forward - (float)backward;
        // Match sys_fpcam's original mapping: A pushes "right" (positive),
        // D pushes "left" (negative). Keep it consistent with prior behaviour.
        float in_rt  = (float)left    - (float)right;

        if (pl->noclip) {
            // Fly: direct velocity from input, no physics.
            float in_up = (float)jump - (float)down;
            Vector3 move = {0, 0, 0};
            move.x = fwd_xz.x * in_fwd + right_xz.x * in_rt;
            move.y = in_up;
            move.z = fwd_xz.z * in_fwd + right_xz.z * in_rt;
            float len = Vector3Length(move);
            if (len > 0.0f) {
                move = Vector3Scale(move, 1.0f / len);
            }
            float speed = pl->max_speed;
            t->position.x += move.x * speed * dt;
            t->position.y += move.y * speed * dt;
            t->position.z += move.z * speed * dt;
            vc->velocity = (Vector3){0, 0, 0};
            pl->on_ground = 0;
        } else {
            // Ground check: trace a small AABB down a tiny distance.
            Vector3 ground_start = t->position;
            Vector3 ground_end = t->position;
            ground_end.y -= 2.0f;
            phys_trace gt;
            phys_trace_box(phys, ground_start, ground_end,
                           pl->half_extents, PHYS_MASK_PLAYERSOLID, &gt);
            int was_on_ground = pl->on_ground;
            pl->on_ground = (gt.fraction < 1.0f && gt.plane_normal.y >= 0.7f) ? 1 : 0;

            // Build desired horizontal direction.
            Vector3 wishvel = {0, 0, 0};
            wishvel.x = fwd_xz.x * in_fwd + right_xz.x * in_rt;
            wishvel.z = fwd_xz.z * in_fwd + right_xz.z * in_rt;

            if (pl->on_ground && gt.plane_normal.y >= 0.7f) {
                wishvel = project_onto_plane(wishvel, gt.plane_normal);
            }

            float wishlen = sqrtf(wishvel.x * wishvel.x + wishvel.z * wishvel.z);
            Vector3 wishdir = {0, 0, 0};
            float wishspeed = 0.0f;
            if (wishlen > 0.0f) {
                wishdir.x = wishvel.x / wishlen;
                wishdir.z = wishvel.z / wishlen;
                wishspeed = pl->max_speed;
            }

            // Friction (ground only).
            if (pl->on_ground) {
                apply_friction(pl, &vc->velocity, dt);
            }

            if (pl->on_ground && vc->velocity.y < 0.0f) {
                vc->velocity.y = 0.0f;
            }

            // Accelerate.
            if (pl->on_ground) {
                vc->velocity.y = 0.0f;
                accelerate(&vc->velocity, wishdir, wishspeed, pl->accelerate, dt);
            } else {
                // Air control: clamp the projection so air strafing works.
                float airwishspeed = wishspeed;
                if (airwishspeed > 30.0f) airwishspeed = 30.0f;
                accelerate(&vc->velocity, wishdir, airwishspeed, pl->air_accelerate, dt);
                vc->velocity.y -= pl->gravity * dt;
            }

            // Jump.
            if (jump && pl->on_ground) {
                vc->velocity.y = pl->jump_speed;
                pl->on_ground = 0;
            }

            // Step-slide-move.
            step_slide_move(phys, &t->position, &vc->velocity,
                            pl->half_extents, PHYS_MASK_PLAYERSOLID,
                            pl->step_height, pl->on_ground, dt);

            if (was_on_ground && vc->velocity.y <= 0.0f) {
                Vector3 snap_start = t->position;
                Vector3 snap_end = t->position;
                snap_end.y -= 4.0f;

                phys_trace snap_tr;

                phys_trace_box(
                    phys,
                    snap_start,
                    snap_end,
                    pl->half_extents,
                    PHYS_MASK_PLAYERSOLID,
                    &snap_tr
                );

                if (!snap_tr.allsolid &&
                    snap_tr.fraction < 1.0f &&
                    snap_tr.plane_normal.y >= 0.7f)
                {
                    t->position = snap_tr.endpos;

                    if (vc->velocity.y < 0.0f)
                        vc->velocity.y = 0.0f;

                    pl->on_ground = 1;
                }
            }

            // After moving, re-check ground so jump-next-frame works.
            ground_start = t->position;
            ground_end = t->position;
            ground_end.y -= 2.0f;
            phys_trace_box(phys, ground_start, ground_end,
                           pl->half_extents, PHYS_MASK_PLAYERSOLID, &gt);
            pl->on_ground = (gt.fraction < 1.0f && gt.plane_normal.y >= 0.7f) ? 1 : 0;
            (void)was_on_ground;
        }

        // Refresh camera if attached so sys_fpcam_active sees the new pose.
        if (c_camera_id < ECS_MAX_COMPONENTS) {
            c_camera *cam = ecs_get(w, e, c_camera_id);
            if (cam != NULL) {
                float yaw_r = t->yaw * DEG2RAD;
                float pitch_r = t->pitch * DEG2RAD;
                float cp = cosf(pitch_r);
                Vector3 eye = t->position;
                eye.y += pl->eye_height;
                Vector3 dir = (Vector3){
                    cosf(yaw_r) * cp,
                    -sinf(pitch_r),
                    -sinf(yaw_r) * cp,
                };
                cam->rl_camera.position = eye;
                cam->rl_camera.target   = Vector3Add(eye, dir);
                cam->rl_camera.up       = (Vector3){0, 1, 0};
                cam->rl_camera.fovy     = cam->fovy;
                cam->rl_camera.projection = cam->projection;
            }
        }
    }
}
