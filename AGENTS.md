# AGENTS.md

Guidance for AI coding agents (OpenCode and similar) working on this
repository. Human contributors should also follow these rules.

## Project at a glance

Crank is a Quake-II-BSP-based 3D engine written in C11, built with CMake,
and rendered through Raylib (fetched from GitHub via `FetchContent`).
It loads a BSP file, builds static GPU resources (world mesh, lightmap
atlas), and renders with a custom GLSL shader. The runtime uses a minimal
Entity-Component-System (ECS) layer, resource managers that own long-lived
assets behind opaque handles, and systems dispatched from the main loop.

The project is licensed under the **GNU General Public License v2 or later**.
See the `LICENSE` file at the repository root. All source files carry the
standard GPL header. Do not remove or replace these headers.

## Build and run

```sh
cmake -S . -B build
cmake --build build
./build/crank <mapname>
```

Always build with the project's `CMakeLists.txt`. Do not bypass it with
ad-hoc compile commands; the CMake build also handles the shader copy
step.

## Language and standard

- **C11** (`CMAKE_C_STANDARD 11`, `CMAKE_C_STANDARD_REQUIRED ON`).
  Do not use C99-or-earlier idioms when a C11 idiom exists, but also do
  not use C17/C23 features.
- No GCC/Clang language extensions in the codebase. The current code is
  strictly portable C11. Specifically: **no nested functions**, **no
  statement expressions**, **no variable-length arrays in struct
  members**, **no compound-literal trickery beyond standard C11**.
- Build must remain warning-clean with the default compiler flags used
  by Raylib's CMake (`-Werror=pointer-arith`,
  `-Werror=implicit-function-declaration`).

## Code style

### File organisation

- Each low-level module is a `.c`/`.h` pair. ECS-related code lives
  under `ecs/`, systems under `sys/`, and resource managers under `res/`.
- Header includes go in this order, each group separated by a blank line:
  1. The module's own header (in the `.c` file).
  2. Headers from this project.
  3. Raylib headers (`raylib.h`, `rlgl.h`, `raymath.h`).
  4. Standard C headers (`<stdint.h>`, `<stdlib.h>`, ...).
- Headers use include guards in the form `#ifndef MODULE_H ... #endif`.
- Public structs and functions are declared in the header; internal
  helpers are `static` in the `.c` file.

### Naming

- **Functions:** `lower_snake_case`. Module-prefixed for public API
  (e.g. `bsp_load`, `mesh_from_bsp`, `lm_build`, `r_draw_mesh`,
  `tex_load`).
- **Types:** `lower_snake_case` (e.g. `bsp_model`, `mesh_surface`,
  `lm_atlas`, `texture`). Raylib types are kept in PascalCase since they
  come from the library.
- **Macros / constants:** `UPPER_SNAKE_CASE` (e.g. `BSP_MAGIC`,
  `MAX_FACE_VERTICES`, `FALLBACK_PATH`, `DEFAULT_LIGHT_SCALE`).
- **File-scope globals (only when justified):** `g_` prefix
  (e.g. `g_lightmap_shader`, `g_mat`). Avoid mutable globals when
  possible.
- **Static module-private functions:** plain `lower_snake_case`, no
  prefix.

### Layout

- 4-space indentation, no tabs.
- Opening braces on the same line as the statement (`if (...) {`,
  `void f(void) {`).
- One statement per line.
- Pointer asterisks bind to the variable: `char *path`, not `char* path`.
- Always brace `if`/`for`/`while` blocks, even single-statement ones
  (existing one-liner `if (... == NULL) return ...` patterns are
  tolerated where they make a guard clause obvious, but a brace is the
  default).
- Cast to `(int)` / `(uint32_t)` etc. explicitly when mixing signed and
  unsigned arithmetic.

### Memory and error handling

- Allocate with `calloc`/`malloc`/`realloc`. Always check for `NULL`
  return.
- On allocation failure inside a loader, **free every partial allocation
  along the failure path** before returning `NULL`. The existing loaders
  (`bsp_load`, `lm_build`, `mesh_from_bsp`) demonstrate this pattern -
  follow it.
- Errors are reported via `printf` with a function-name tag prefix:
  `printf("module_func: description %d\n", value);`. Do not call
  `exit()` from library code; return `NULL` or a sentinel and let the
  caller decide.
- Pair allocations with frees in the corresponding `*_free` function.
- Avoid `goto` for error handling; the codebase prefers explicit
  inline cleanup.

### Raylib interop

- Use `LoadShader`, `LoadTextureFromImage`, `UploadMesh`, `DrawMesh`
  rather than raw GL when possible.
- When sharing a `Material` with raylib's default shader, never
  `UnloadMaterial` it - only `RL_FREE(mat.maps)`. The default
  `shader.locs` array is shared and must not be freed.
- Diffuse textures map to `MATERIAL_MAP_DIFFUSE` (slot 0, sampler
  `texture0` in the shader). Lightmap maps to `MATERIAL_MAP_SPECULAR`
  (slot 1, sampler `texture1`).
- The custom shader's vertex attribute names MUST match raylib's
  defaults: `vertexPosition`, `vertexTexCoord`, `vertexTexCoord2`.
  The uniform name for the MVP must be `mvp`.
- Coordinate convention: BSP uses `(x, y, z)` with `z` up. Engine
  remaps to raylib's `(x, y, z)` with `y` up via `(x, z, -y)` swap.
  Lightmap UVs are computed in pre-swap BSP coordinates because they
  derive from `texinfo.u_axis / v_axis`, which are in BSP space.

### BSP-specific

- All BSP structs in `bsp.h` are packed via `PACKED_STRUCT` and must
  match the on-disk layout byte-for-byte. **Never reorder fields, never
  add fields, never change types** in those structs without verifying
  against the Quake II format spec.
- Lump indices and the magic number live in the macros at the top of
  `bsp.h`.
- New lumps should be loaded through `bsp_read_lump` and exposed on the
  `bsp_model` struct alongside a corresponding count/size field.

## Architecture conventions

- **ECS components** are POD structs. If a component owns heap memory
  or GPU resources, register a destructor with `ecs_register`.
- **Resource handles** are opaque integers (`tex_handle`, `mesh_handle`,
  `map_handle`). Components store handles, never raw pointers, so they
  survive internal manager reallocations.
- **Systems** are plain functions dispatched in a fixed order from the
  main loop. There is no scheduler.
- **JSON entity archetypes** live in the `entities/` runtime directory.
  Each file is named `<classname>.json` and defines the components that
  make up that entity type. When a map is loaded, every entity in the
  BSP entity lump is matched to a JSON file by its `classname` and
  spawned into the ECS world.

## Testing and verification

There is no automated test suite. Verification is manual:

1. `cmake --build build` must complete with no warnings or errors.
2. Run `./crank <known_map>`. The world should render with the skybox
   and lightmap-modulated textures.
3. Smoke-test a map with no lightmap data and a map with full lightmap
   data when touching lightmap or mesh code.
4. Verify player movement (WASD, jump, noclip) and mouse look feel
   smooth at various frame rates.

Always rebuild after edits and confirm the binary still links before
declaring work done.

## Conventions for changes by agents

- Prefer editing existing files. Do not split a module into multiple
  files unless asked.
- Update per-module docs when you change a public API or alter how a
  module works internally.
- Update `CMakeLists.txt` when adding a new `.c` file.
- Update `README.md` if user-visible behaviour (controls, asset
  locations, command-line arguments) changes.
- Do not add new third-party dependencies without explicit permission.
  Raylib is the only allowed dependency.
- Do not introduce shaders that depend on features beyond GLSL 330
  (the existing target).
