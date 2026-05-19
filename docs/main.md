# main

The `main` module is the entry point. It owns the lifecycle of the
window, the BSP model, the world mesh, the skybox textures, and the
camera, and drives the per-frame loop.

## File

- `src/main.c`

## Outline

```c
int main(int argc, char *argv[]) {
    // 1. parse argv -> map name
    // 2. InitWindow + SetTargetFPS + DisableCursor
    // 3. r_init
    // 4. bsp_load -> mesh_from_bsp
    // 5. scan entities for sky name and player start
    // 6. load 6 skybox PNGs into env/<sky><suffix>.png
    // 7. configure clip planes and input constants
    // 8. while (!WindowShouldClose()) { input; UpdateCameraPro; BeginDrawing; world; sky; FPS; EndDrawing }
    // 9. tear everything down in reverse
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
game loop runs as fast as the GPU allows (raylib will still honour
vsync if the user has it forced on).

## Entity scan

After the BSP is loaded, `main` walks `bsp->entities` once and
inspects two classnames:

- `worldspawn`: reads the `sky` key (if present) to determine the
  skybox texture prefix. The default is `unit1_`.
- `info_player_start`: reads the `origin` (a 3-component string of
  floats) and the optional `angle` (degrees). The origin is converted
  to raylib space by `vec3_parse`, which applies the same `(x, z, -y)`
  swap used in the mesh build. The angle is converted to a unit
  direction in the XZ plane:
  `dir = (cos(angle), 0, -sin(angle))`.

The camera position and target are set from these values. If no
`info_player_start` is found, the camera defaults to looking down +X
from the origin.

### `vec3_parse`

```c
Vector3 vec3_parse(const char *str);
```

A small string-to-`Vector3` helper. It uses `strtok` on a copy of the
input to split on spaces, parses each component with `strtof`, and
returns `(x, z, -y)` to apply the BSP-to-raylib coordinate swap.
Failing inputs print a warning and return `(0, 0, 0)`.

## Skybox loading

Six PNG files are loaded from `env/<sky><suffix>.png` where
`<suffix>` is one of `ft`, `dn`, `bk`, `lf`, `rt`, `up`. After
loading, each face's wrap mode is changed to `TEXTURE_WRAP_CLAMP` so
the seams don't bleed.

If any face fails to load, `sky_tex[j]` is `NULL` and the per-frame
draw skips the sky. (`r_draw_sky` itself doesn't validate inputs; the
main loop checks that all six pointers are non-NULL before calling
it.)

## Camera and input

The camera is a `Camera` struct (`CAMERA_PERSPECTIVE`, FOV 90).
Each frame:

1. Read movement keys (`W`/`A`/`S`/`D`/`Space`/`LeftShift`).
2. Toggle fullscreen on `F11`.
3. Call `UpdateCameraPro`:
   - Movement vector: `forward * run_speed - backward * run_speed`,
     same for strafe and vertical, all scaled by `delta`. (run_speed
     is currently 320 world units / sec.)
   - Rotation vector: `mouse_delta * sensitivity * yaw_pitch_factor`.
     The factors are 0.022 degrees per mouse pixel, matching common
     Quake-style mouselook tuning.
4. Render: clear, `BeginMode3D(camera)`, sky, world, `EndMode3D`,
   FPS overlay, `EndDrawing`.

## Cleanup order

```c
for (i = 0..5) tex_free(sky_tex[i]);
mesh_free(m);
bsp_free(bsp);
free(sky_name);
r_shutdown();
CloseWindow();
```

The order matters:

- `tex_free` and `mesh_free` need a valid GL context (still alive
  until `CloseWindow`).
- `bsp_free` only frees CPU buffers and can be called after the mesh
  has been destroyed.
- `r_shutdown` releases the shader (also requires a GL context).
- `CloseWindow` last.

## Tunables in main.c

| Variable      | Default   | Meaning                                  |
| ------------- | --------- | ---------------------------------------- |
| `width`       | 1280      | Initial window width                     |
| `height`      | 720       | Initial window height                    |
| `run_speed`   | 320       | Movement speed in world units per second |
| `sensitivity` | 1.0       | Mouse sensitivity multiplier             |
| `m_yaw`       | 0.022     | Degrees of yaw per mouse pixel           |
| `m_pitch`     | 0.022     | Degrees of pitch per mouse pixel         |

The near/far clip planes are set globally via
`rlSetClipPlanes(0.1, 8192.0)`.
