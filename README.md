# crank

A small 3D game engine built around Quake II BSP maps, written in C and
powered by [Raylib](https://www.raylib.com/). Crank loads BSP files, builds
GPU meshes from the world geometry, assembles a lightmap atlas, and renders
the world with diffuse + lightmap shading and a six-sided skybox.

## Features

- Quake II BSP (version 38) loader
- Texture lump and entity lump parsing
- World mesh built once at load time and uploaded to the GPU
- Static lightmap atlas with shelf packing and 1-luxel padding
- Diffuse * lightmap shading via a custom GLSL shader
- Six-face skybox rendering
- WASD + mouselook free-fly camera, fullscreen toggle

## Project layout

```
crank/
  src/              C source files
  shaders/          GLSL shaders (shipped with the repo)
  docs/             Per-module documentation
  maps/             *.bsp map files                          (create manually)
  textures/         Diffuse textures as *.png                (create manually)
  env/              Skybox textures as *.png                 (create manually)
  entities/         JSON entity archetypes (*.json)          (create manually)
  CMakeLists.txt
  README.md
  AGENTS.md
  LICENSE
```

### Asset directories you must create

These are NOT committed to the repository. Create them in the working directory
before running the engine:

- **`maps/`** — Place Quake II `.bsp` files here. The map name passed on the
  command line is resolved as `maps/<name>.bsp`.
- **`textures/`** — Place diffuse textures here as `.png` files. The directory
  structure must mirror the texture names referenced in the BSP's texinfo
  lump. For example, if a face references `e1u1/floor1_2`, place the image at
  `textures/e1u1/floor1_2.png`.
- **`env/`** — Place skybox textures here as `.png` files, one per face,
  named `<skyname><suffix>.png` where suffix is one of `ft`, `bk`, `lf`,
  `rt`, `up`, `dn`. The sky name is read from the `worldspawn` entity's
  `sky` key, defaulting to `unit1_`.
- **`entities/`** — Place JSON entity archetype files here. Each file is
  named `<classname>.json` and defines the components that make up that
  entity type (e.g. `entities/player.json`, `entities/worldspawn.json`).
  When a map is loaded, every entity in the BSP entity lump is matched
  to a JSON file by its `classname` and spawned into the ECS world.
  See the sample JSON files shipped with the repo for the expected format.

The `shaders/` directory IS in the repository and is copied next to the
executable automatically by the CMake build.

## Building

Requirements:

- CMake 3.11 or newer
- A C11-capable C compiler (GCC or Clang)
- An OpenGL 3.3 capable GPU and drivers
- Build dependencies needed by Raylib's bundled GLFW
  (X11 headers on Linux: `libx11-dev`, `libxrandr-dev`, `libxinerama-dev`,
  `libxcursor-dev`, `libxi-dev`)

Raylib itself is fetched and built automatically by CMake.

```sh
cmake -S . -B build
cmake --build build
```

The output binary is `build/crank`.

## Running

From the project root (so the executable can see the asset directories
either by symlink or by being placed alongside them):

```sh
mkdir -p maps textures env             # if you haven't already
# ... drop your assets in ...
./build/crank base1
```

If no map name is given, the engine tries `base1`. Working directory must
contain `maps/`, `textures/`, `env/`, and `shaders/`.

### Controls

| Input              | Action                |
| ------------------ | --------------------- |
| `W` / `S`          | Move forward / back   |
| `A` / `D`          | Strafe left / right   |
| `Space`            | Move up               |
| `Left Shift`       | Move down             |
| Mouse              | Look around           |
| `F11`              | Toggle fullscreen     |
| `Esc`              | Close window          |

## License

Proprietary. Copyright (C) 2026 Romance Games. All rights reserved.
See [LICENSE](LICENSE) for the full license terms.
