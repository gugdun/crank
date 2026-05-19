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
    // 5. create ecs world, register c_transform/c_camera/c_fpcam,
    //    c_skybox, c_map via sys_*_register
    // 6. res_map_load -> map_handle
    // 7. spawn map / skybox / fpcam entities, then sys_map_apply_spawn
    // 8. while (!WindowShouldClose()) {
    //        sys_fpcam_update
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
SetTargetFPS(300);
DisableCursor();
```

The cursor is disabled so the OS pointer doesn't escape the window
during mouselook. The target framerate is artificially high so the
game loop runs as fast as the GPU allows.

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
sys_fpcam_register(world);
sys_map_register(world);
sys_skybox_register(world);
```

The three `_register` calls install the component pools used by the
respective systems. They must run before any `*_spawn`.

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
    sys_fpcam_update(world, dt);     // input + look + move
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

`sys_fpcam_update` also handles the `F11` fullscreen toggle, since
fullscreen is conceptually a player-controlled input.

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
