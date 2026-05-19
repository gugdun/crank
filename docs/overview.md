# Engine Overview

Crank is a small, fixed-pipeline-of-purpose 3D engine targeted at rendering
single-player Quake II BSP maps. There is no game logic, no AI, no physics;
the runtime is essentially: load a BSP, build static GPU resources from it,
and present them with a free-fly camera each frame.

## Runtime pipeline

```
                +-------------+
   main.c  --->|  bsp_load   |---->  bsp_model (CPU)
                +------+------+
                       |
                       v
                +-------------+      +---------------+
                | mesh_from_  |----->| lm_build      |---->  lm_atlas (CPU)
                |   bsp       |      +-------+-------+
                +------+------+              |
                       |   (atlas pixels uploaded as Texture2D)
                        v
                 +-------------+
                 |  mesh (GPU) |   opaque + transparent per-surface raylib Meshes
                 |             |   + lightmap atlas Texture2D
                 +------+------+
                        |
    game loop --------> | r_draw_mesh(mesh, cam_pos) every frame
                        |   (opaque pass + sorted transparent pass)
                        |   r_draw_sky(...)
                        v
                   OpenGL 3.3
```

## Modules at a glance

| Module     | Source files            | Doc                  | Responsibility                                                   |
| ---------- | ----------------------- | -------------------- | ---------------------------------------------------------------- |
| `bsp`      | `bsp.c`, `bsp.h`        | [bsp.md](bsp.md)           | Parse a Quake II BSP file into in-memory lumps.                  |
| `texture`  | `texture.c`, `texture.h`| [texture.md](texture.md)   | Load a PNG into a raylib `Texture2D` wrapper.                    |
| `lightmap` | `lightmap.c`, `lightmap.h` | [lightmap.md](lightmap.md) | Compute face extents and pack all face lightmaps into one atlas.|
| `mesh`     | `mesh.c`, `mesh.h`      | [mesh.md](mesh.md)         | Build opaque+transparent GPU meshes grouped by (texture, alpha).  |
| `render`   | `render.c`, `render.h`  | [render.md](render.md)     | Bind lightmap shader, two-pass draw (opaque, sorted transparent). |
| `main`     | `main.c`                | [main.md](main.md)         | Window init, entity scan, camera input, frame loop.              |

## Coordinate systems

There are two coordinate spaces in the engine:

1. **BSP space** (`x` right, `y` forward, `z` up). All vertices, planes,
   `texinfo` axes and lightmap math live in BSP space. Crank computes
   lightmap UVs in this space (because the texinfo axes are expressed in
   BSP space).
2. **Engine / Raylib space** (`x` right, `y` up, `z` backward). After
   computing UVs the engine swaps each vertex as `(x, y, z) -> (x, z, -y)`
   before uploading to the GPU. This puts BSP "up" along raylib's `y`
   axis and makes the camera behave naturally.

The `vec3_parse` helper in `main.c` applies the same swap to entity
`origin` values, so player spawns line up with the world geometry.

## Lifetime

```
InitWindow                              <- raylib
r_init                                  <- crank
bsp_load    -> mesh_from_bsp            <- crank (loads atlas, uploads GPU)
... game loop ...
mesh_free
bsp_free
r_shutdown
CloseWindow
```

All long-lived GPU resources (diffuse textures, lightmap atlas, vertex
buffers, the lightmap shader) are created once at startup and destroyed
once at shutdown. The frame loop never allocates GPU memory.

## Design constraints

- Everything is **static**. There is no support for streaming textures,
  reloading maps, or animating lightmaps. Style-0 lightmaps only.
- The engine targets **OpenGL 3.3 / GLSL 330** through raylib's desktop
  backend.
- The implementation favors **simplicity over performance**: one
  surface per unique (diffuse texture, alpha), one draw call per
  surface, no visibility culling beyond what the GPU does on the
  rasteriser.
- Transparent surfaces (`SURF_TRANS33`, `SURF_TRANS66`) are stored in a
  separate list and drawn in a second pass with alpha blending, sorted
  back-to-front by centroid every frame.
