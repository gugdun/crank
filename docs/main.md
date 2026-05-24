# main

The `main` module is the bootstrap. It opens the window, creates the
resource managers, creates the ECS world, registers components, loads
the map, spawns the initial entities, and drives the per-frame loop.

After the ECS refactor, `main` contains no game logic of its own. All
behaviour lives in the systems under `src/sys/` and the resources under
`src/res/`.

## File

- `src/main.c`

## Outline

```c
int main(int argc, char *argv[]) {
    // 1. parse argv -> map name
    // 2. InitWindow + SetTargetFPS + DisableCursor
    // 3. r_init + rlSetClipPlanes
    // 4. create resource managers: res_texture, res_mesh, res_map
    // 5. create ecs world, register sys_input, sys_usercmd, sys_fpcam,
    //    sys_view, sys_player, sys_map, sys_skybox via sys_*_register
    // 6. res_map_load -> map_handle
    // 7. spawn entities, process BSP entities, configure skybox
    // 8. while (!WindowShouldClose()) {
    //        sys_input_update          (mouse delta into c_input)
    //        sys_view_look             (mouse-look + F11; writes c_view)
    //        sys_usercmd_accumulate    (buffer WASD/jump/noclip)
    //        while (fixed_accumulator) sys_sim_tick (finalize + player_update)
    //        alpha = accumulator / fixed_dt
    //        sys_view_update(alpha)    (interpolate + build c_camera.rl_camera)
    //        BeginDrawing + BeginMode3D(sys_fpcam_active)
    //            sys_skybox_render
    //            sys_map_render
    //        EndMode3D + DrawFPS + EndDrawing
    //    }
    // 9. tear down: ecs_world_destroy -> res_map_destroy ->
    //    res_mesh_destroy -> res_texture_destroy -> r_shutdown ->
    //    CloseWindow
}
```

## Command line

```sh
crank [mapname]
```

If `mapname` is omitted, defaults to `base1`. The actual file path is
resolved by `bsp_load` to `maps/<mapname>.bsp` relative to the working
directory.

## Window setup

```c
InitWindow(1280, 720, "crank");
SetTargetFPS(0);
DisableCursor();
```

The cursor is disabled so the OS pointer doesn't escape the window
during mouselook. `SetTargetFPS(0)` lets the game loop run as fast as
the GPU allows. Physics still runs at a fixed 128 Hz via the
accumulator loop, and the camera position is interpolated between the
previous and current physics-tick pose by
`alpha = accumulator / fixed_dt`, so motion stays smooth at any render
rate.

## Resource managers

`main` creates three managers in this order and destroys them in
reverse:

```c
res_texture_mgr *texmgr  = res_texture_create();
res_mesh_mgr    *meshmgr = res_mesh_create();
res_map_mgr     *mapmgr  = res_map_create(meshmgr);
```

`res_map_create` borrows the mesh manager so that loading a map can
register the produced world mesh under a stable handle.

See [res.md](res.md).

## World and systems

```c
ecs_world *world = ecs_world_create();
sys_input_register(world);
sys_usercmd_register(world);
sys_fpcam_register(world);
sys_view_register(world);
sys_map_register(world);
sys_player_register(world);
sys_skybox_register(world);
```

The `_register` calls install the component pools used by the
respective systems. They must run before any `*_spawn`. `sys_view`
must be registered after `sys_fpcam` because `c_view`'s registration
order is independent but the player archetype JSON expects the pool to
exist before `entity_spawn_from_file` runs. `sys_sim` does not register
any components, so it has no `_register` function.

See [ecs.md](ecs.md) and [sys.md](sys.md).

## Bootstrap entities

```c
map_handle map_h  = res_map_load(mapmgr, map_name);
ecs_entity map_e  = sys_map_spawn(world, map_h);
ecs_entity sky_e  = sys_skybox_spawn(world);
ecs_entity cam_e  = sys_fpcam_spawn(world, (Vector3){0}, 0.0f);
sys_map_apply_spawn(world, map_e, cam_e, sky_e, texmgr, mapmgr);
```

`sys_map_apply_spawn` reads the BSP entity lump and:

- Configures the skybox component with six `tex_handle`s loaded via
  `res_texture_load("env/<sky><suffix>")`.
- Sets `c_transform.position` and `c_transform.yaw` on the fpcam entity
  from `info_player_start.origin` and `.angle`.

## Per-frame loop

```c
while (!WindowShouldClose()) {
    float dt = GetFrameTime();
    if (dt > 0.25f) dt = 0.25f;
    accumulator += dt;

    sys_input_update(world);                  // 1. mouse delta into c_input
    sys_view_look(world);                     // 2. mouse-look + F11 (writes c_view)
    sys_usercmd_accumulate(world, dt);        // 3. buffer WASD/jump/noclip

    while (accumulator >= fixed_dt) {
        sys_sim_tick(world, view.phys, fixed_dt);    // 4. finalize + physics
        accumulator -= fixed_dt;
    }

    float alpha = accumulator / fixed_dt;     // 5. interpolation factor
    sys_view_update(world, alpha);            // 6. lerp + build c_camera.rl_camera

    Camera cam = sys_fpcam_active(world);

    BeginDrawing();
    ClearBackground(BLACK);
    BeginMode3D(cam);
        sys_skybox_render(world, texmgr, cam.position);
        sys_map_render(world, mapmgr, meshmgr, cam.position);
    EndMode3D();
    DrawFPS(16, 16);
    EndDrawing();
}
```

Steps 1, 2, 3, 5, and 6 run exactly once per frame. Step 4 runs zero or
more times per frame to keep the simulation aligned with real time.

- `sys_input_update` writes the current mouse delta into every `c_input`.
- `sys_view_look` updates `c_view.yaw / c_view.pitch` from the mouse delta
  every frame and toggles fullscreen on `F11`. This is what makes look
  feel render-rate-paced rather than physics-rate-paced.
- `sys_usercmd_accumulate` writes the latest WASD/Shift/Space axes into
  the pending slot of every `c_usercmd_queue` and ORs the jump / noclip
  bits so short taps that fall between ticks are never lost.
- `sys_sim_tick` finalises the user command (sampling `c_view.yaw/pitch`
  into `cmd.yaw/pitch`) and runs `sys_player_update` for one fixed step.
  The player controller snapshots `prev_position / prev_yaw / prev_pitch`
  on every tick before integrating, so multi-tick frames remain smooth.
- `sys_view_update(alpha)` is the *only* writer of `c_camera.rl_camera`.
  It interpolates `prev_position -> position` by `alpha`, adds
  `c_view.eye_height` on Y, and rebuilds the camera direction from
  `c_view.yaw / c_view.pitch`.

## Cleanup order

```c
ecs_world_destroy(world);     // destroys components first
res_map_destroy(mapmgr);      // frees bsp_model* and name copies
res_mesh_destroy(meshmgr);    // mesh_free -> UnloadTexture (needs GL)
res_texture_destroy(texmgr);  // tex_free (needs GL)
r_shutdown();                 // shader + scratch buffers
CloseWindow();                // GL context dies here
```

The order matters: any manager that owns GPU resources must be
destroyed before `r_shutdown` and `CloseWindow`. `ecs_world_destroy`
runs every component pool's destructor; since none of the current
components own GPU resources (they hold handles, not pointers) this is
safe to run first.
