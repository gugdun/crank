# main

The `main` module is the bootstrap. It opens the window, creates the
resource managers, creates the ECS world, registers components, loads
the map, spawns the initial entities, and drives the per-frame loop.

After the ECS refactor, `main` contains no game logic of its own. All
behaviour lives in the systems and the resource managers.

## Outline

```
int main(int argc, char *argv[]) {
    // 1. parse argv -> map name
    // 2. InitWindow + SetTargetFPS + DisableCursor
    // 3. r_init + rlSetClipPlanes
    // 4. create resource managers: res_texture, res_mesh, res_map
    // 5. create ecs world, register all systems
    // 6. res_map_load -> map_handle
    // 7. spawn entities, process BSP entities, configure skybox
    // 8. while (!WindowShouldClose()) {
    //        sys_input_update
    //        sys_view_look
    //        sys_usercmd_accumulate
    //        while (fixed_accumulator) sys_sim_tick
    //        alpha = accumulator / fixed_dt
    //        sys_view_update(alpha)
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

If `mapname` is omitted, defaults to `c1a0`. The actual file path is
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

## World and systems

All systems are registered before any spawning happens. `sys_view`
must be registered after `sys_fpcam` because the player archetype JSON
expects the `c_view` pool to exist before `entity_spawn_from_file`
runs. `sys_sim` does not register any components, so it has no
`_register` function.

## Bootstrap entities

The bootstrap sequence:

1. Spawn skybox from JSON, or fall back to hardcoded spawn.
2. `res_map_load(map_name)` -> `map_handle`.
3. `sys_map_process_entities` reads the BSP entity lump and spawns
   entities from JSON archetypes.
4. Post-processing attaches the map handle to the worldspawn entity,
   configures skybox sides from the BSP `worldspawn.sky` key, and
   places the player at the first untargeted spawn point.
5. Spawn player from JSON, or fall back to `sys_fpcam_spawn`.
6. Ensure the player has a `c_view` with a default `eye_height` of
   24 units so the engine works even if the asset bundle is missing.

## Per-frame loop

```c
while (!WindowShouldClose()) {
    float dt = GetFrameTime();
    if (dt > 0.25f) dt = 0.25f;
    accumulator += dt;

    sys_input_update(world);                  // 1. mouse delta into c_input
    sys_view_look(world);                     // 2. mouse-look + F11 (writes c_view)
    sys_usercmd_accumulate(world, dt);        // 3. buffer latest axes + buttons

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
  every frame and toggles fullscreen on `F11`.
- `sys_usercmd_accumulate` writes the latest WASD/Shift/Space axes into
  the pending slot of every `c_usercmd_queue` and ORs the jump / noclip
  bits so short taps that fall between ticks are never lost.
- `sys_sim_tick` finalises the user command and runs `sys_player_update`
  for one fixed step. The player controller snapshots the pre-integration
  pose on every tick so multi-tick frames remain smooth.
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
