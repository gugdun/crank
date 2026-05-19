# render

The render module is the smallest reasonable rendering layer that can
draw the world with `diffuse * lightmap` shading and a skybox. It owns
the lightmap shader, a reusable `Material`, and the skybox drawing
routine.

## Files

- `src/render.h` — `r_init`, `r_shutdown`, `r_draw_mesh`, `r_draw_sky`.
- `src/render.c` — implementation.
- `shaders/lightmap.vs` — vertex shader.
- `shaders/lightmap.fs` — fragment shader.

## Public API

```c
void r_init(void);                          // load shader, build material
void r_shutdown(void);                      // unload them

void r_draw_mesh(const mesh *m, Vector3 cam_pos);  // draw the world (two-pass: opaque then transparent)
void r_draw_sky(Vector3 cam_pos,
                uint32_t bk, uint32_t dn,
                uint32_t ft, uint32_t lf,
                uint32_t rt, uint32_t up);  // draw a static skybox
```

## Lifecycle

`r_init` must be called after `InitWindow` and before any `r_draw_*`.
It:

1. Loads `shaders/lightmap.vs` and `shaders/lightmap.fs` via raylib's
   `LoadShader`. raylib automatically wires the standard attribute and
   sampler uniforms (`vertexPosition`, `vertexTexCoord`,
   `vertexTexCoord2`, `texture0`, `texture1`, `mvp`).
2. Looks up the custom `lightScale` uniform and initialises it to
   `DEFAULT_LIGHT_SCALE` (2.0).
3. Looks up the custom `surfaceAlpha` uniform and initialises it to `1.0`.
4. Calls `LoadMaterialDefault` to get a `Material` (which allocates an
   array of `MAX_MATERIAL_MAPS` `MaterialMap` slots) and overwrites
   `mat.shader` with the lightmap shader.

If the shader fails to compile or link, `r_init` falls back to the
default raylib shader so the world can still be drawn (unlit). A
`printf` warns about the failure.

`r_shutdown` does the inverse: it frees `g_mat.maps` with `RL_FREE`
(NOT `UnloadMaterial`, which would also try to free shared default
shader resources), and unloads the lightmap shader.

## Drawing the world

`r_draw_mesh(m, cam_pos)` draws the world in two passes.

### Opaque pass

Sets `surfaceAlpha` to `1.0`, iterates `m->surfaces` and, for each
surface:

1. Sets `g_mat.maps[MATERIAL_MAP_DIFFUSE].texture` to a `Texture2D`
   that carries the surface's diffuse GL id.
2. Calls `DrawMesh(s->rl_mesh, g_mat, MatrixIdentity())`.

### Transparent pass

1. Computes squared distance from `cam_pos` to each transparent
   surface's centroid.
2. Sorts surface indices by descending distance (back-to-front) using
   insertion sort (the count is typically small).
3. Flushes raylib's internal batch with `rlDrawRenderBatchActive()`.
4. Enables alpha blending (`rlEnableColorBlend` + `BLEND_ALPHA`) and
   disables depth-mask writes (`rlDisableDepthMask`).
5. Iterates the sorted list, setting `surfaceAlpha` to the surface's
   `alpha` value (0.33 or 0.66) and drawing each.
6. Flushes again and restores depth-mask writes with
   `rlEnableDepthMask()`.

Because the diffuse texture is mapped to `MATERIAL_MAP_DIFFUSE` (slot
0) and the lightmap is mapped to `MATERIAL_MAP_SPECULAR` (slot 1), the
shader's `texture0` sampler always reads the diffuse and `texture1`
always reads the lightmap. This convention is locked in by raylib's
auto-resolution of sampler uniform locations in `LoadShader`.

### Transparent-surface centroid sort

Each transparent surface stores a `centroid` (arithmetic mean of its
vertex positions in raylib space, computed once at build time in
`mesh_from_bsp`). Every frame the renderer computes
`distance² = |centroid - cam_pos|²` for each live transparent surface
and sorts them descending. Static scratch buffers (`g_trans_order`,
`g_trans_dist`, `g_trans_cap`) are allocated lazily and freed in
`r_shutdown`.

## Shader

### Vertex (`shaders/lightmap.vs`)

```glsl
#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec2 vertexTexCoord2;
uniform mat4 mvp;
out vec2 fragTexCoord;
out vec2 fragLightCoord;
void main() {
    fragTexCoord = vertexTexCoord;
    fragLightCoord = vertexTexCoord2;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
```

### Fragment (`shaders/lightmap.fs`)

```glsl
#version 330
in vec2 fragTexCoord;
in vec2 fragLightCoord;
uniform sampler2D texture0;
uniform sampler2D texture1;
uniform float lightScale;
uniform float surfaceAlpha;
out vec4 finalColor;
void main() {
    vec4 diffuse = texture(texture0, fragTexCoord);
    vec3 light = texture(texture1, fragLightCoord).rgb * lightScale;
    finalColor = vec4(diffuse.rgb * light, diffuse.a * surfaceAlpha);
}
```

`lightScale` is a global brightness knob. Quake II's stored lightmap
values are deliberately dark; the standard runtime multiplier is
roughly `2.0`. Edit `DEFAULT_LIGHT_SCALE` in `render.c` to tune.

`surfaceAlpha` is a per-draw alpha multiplier. It is set to `1.0` for
the opaque pass and to the surface's `alpha` value (0.33 or 0.66) for
the transparent pass. The final fragment alpha is
`diffuse.a * surfaceAlpha`, so a texture's own alpha channel is also
respected.

## Drawing the skybox

`r_draw_sky` uses raylib's immediate-mode `rlBegin/rlEnd` API to emit
six quads around the camera. Steps:

1. `rlDisableDepthMask()` so the sky doesn't write to the depth
   buffer; world geometry will paint over it regardless of order.
2. `rlPushMatrix` / `rlTranslatef(cam_pos)` so the cube follows the
   camera (giving the illusion of an infinite-distance sky).
3. Emit six face quads, each with its own texture id, at a fixed
   half-size of 4096 world units. Each face is two triangles with
   hand-authored vertex/UV pairs.
4. `rlPopMatrix`, restore depth writes, clear the bound texture.

The sky uses the legacy `rlBegin` path because it's simple, fast for
72 vertices, and decoupled from the shader-based world drawing. The
sky uses raylib's *default* shader (whatever is currently active when
`rlBegin` runs).

## Why a single cached material

`LoadMaterialDefault` allocates an array of `MaterialMap` structs every
call. Doing it per-frame would be wasteful. Instead, `r_init` builds
one material with our shader plugged into it and reuses it; only the
two texture id fields change between draw calls.

Note that we never call `UnloadMaterial` on `g_mat`. `UnloadMaterial`
in raylib also tries to call `UnloadShader` on every map's texture and
to free the shader's `locs` array, which would conflict with our
ownership of the shader. We instead manually `RL_FREE(g_mat.maps)` in
`r_shutdown`.

## Extending the renderer

If you need to add another draw type (sprites, debug lines, GUI), keep
it isolated from the world draw path. The world path is intentionally
narrow: one shader, one material, two loops (opaque then transparent).
