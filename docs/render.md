# render

The render module owns the lightmap shader and draws the world using
the per-bucket index buffers prepared by the visibility module.

## Public API

```c
void r_init(void);
void r_shutdown(void);

void r_draw_mesh(const mesh *m, Vector3 cam_pos);
void r_draw_sky(Vector3 cam_pos,
                uint32_t bk, uint32_t dn,
                uint32_t ft, uint32_t lf,
                uint32_t rt, uint32_t up);
```

## Precondition

`r_draw_mesh` assumes that `vis_update` was called on the same `mesh`
immediately before. The visibility module rewrites every surface's
dynamic IBO; `r_draw_mesh` simply binds and draws each surface that has
`frame_index_count > 0`.

## Lifecycle

`r_init` loads the vertex and fragment shaders via `LoadShader`.
raylib auto-resolves the standard attribute names
(`vertexPosition`, `vertexTexCoord`, `vertexTexCoord2`) and the
standard sampler uniforms (`texture0` -> `SHADER_LOC_MAP_DIFFUSE`,
`texture1` -> `SHADER_LOC_MAP_SPECULAR`). It then resolves the two
custom uniforms (`lightScale`, `surfaceAlpha`) and seeds them with
their defaults.

`r_shutdown` frees the sort scratch buffers and unloads the shader.

## Drawing the world

`r_draw_mesh(m, cam_pos)` runs the full world draw:

1. Flush raylib's active batch so any prior immediate-mode geometry
   (e.g. the skybox) is submitted before we change shader state.
2. Bind the lightmap shader.
3. Bind the shared VAO. This wires positions / texcoords / texcoords2
   in one call. The VAO was set up by `UploadMesh` to bind attributes
   at the fixed raylib-default locations, and `LoadShader` calls
   `glBindAttribLocation` to match.
4. Compute and upload the MVP uniform from `rlGetMatrixModelview()` *
   `rlGetMatrixProjection()` (model is identity).
5. Opaque pass: set `surfaceAlpha = 1.0`. For each surface in
   `m->surfaces` with `frame_index_count > 0`, bind diffuse to slot 0,
   bind lightmap to slot 1, bind the surface's IBO, call
   `glDrawElements(GL_TRIANGLES, frame_index_count, GL_UNSIGNED_INT, 0)`.
6. Transparent pass: distance-sort the live transparent surfaces
   back-to-front by their `frame_centroid` (computed by `vis_update`),
   enable alpha blending + disable depth writes, then draw the sorted
   list with the surface's `alpha` written into `surfaceAlpha`.
7. Restore: disable both texture slots, unbind VAO/IBO, disable shader.

### Why `glDrawElements` directly?

raylib's `rlDrawVertexArrayElements` hard-codes the index type to
`GL_UNSIGNED_SHORT`. Quake II world meshes routinely exceed 65536
vertices after triangle-fanning, so the engine uses 32-bit indices and
calls `glDrawElements` with `GL_UNSIGNED_INT`. The prototype is
declared `extern` in the renderer; the symbol resolves against the
OpenGL library that raylib transitively links.

### Transparent-surface centroid sort

The visibility module computes a per-frame weighted centroid for each
transparent surface based only on the faces it actually drew this
frame. The renderer's scratch buffers are grown lazily; insertion-sort
is used because the live count is small.

## Shader

### Vertex shader

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

### Fragment shader

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

`lightScale` is a global brightness multiplier (default 2.0; tune via
`DEFAULT_LIGHT_SCALE` in the renderer).

`surfaceAlpha` is a per-draw alpha multiplier set to 1.0 for the
opaque pass and to the surface's `alpha` (0.33 or 0.66) for each
transparent draw.

## Drawing the skybox

`r_draw_sky` uses raylib's immediate-mode `rlBegin/rlEnd` to emit six
quads around the camera. Steps:

1. `rlDisableDepthMask()` so the sky doesn't write depth.
2. `rlPushMatrix` + `rlTranslatef(cam_pos)` so the cube follows the
   camera.
3. Emit six face quads at a fixed half-size of 4096 world units.
4. `rlPopMatrix`, clear bound texture, restore depth-mask writes.

The sky uses raylib's default shader. The world draw, which runs
after the sky, flushes the immediate-mode batch before binding the
lightmap shader.

## Extending the renderer

If you need to add another draw type (sprites, debug lines, GUI),
keep it isolated from the world draw path. The world path now relies
on a precondition (vis was just updated) that any added system must
respect.
