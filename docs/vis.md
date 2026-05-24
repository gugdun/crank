# vis

The visibility module decides each frame which triangles of the world
mesh actually need to be drawn. It combines Quake II's potentially-
visible-set (PVS) with frustum culling on per-face AABBs, and rewrites
each `mesh_surface`'s dynamic index buffer with the surviving triangle
indices.

## Public API

```c
vis_state *vis_create(const bsp_model *bsp, const mesh *m);
void       vis_destroy(vis_state *v);

uint32_t   vis_update(vis_state *v,
                      const bsp_model *bsp,
                      mesh *m,
                      Vector3 cam_pos,
                      Matrix view_proj);
```

`vis_create` runs once at map load. It allocates the per-cluster face
table and the scratch buffers (`pvs_bits`, `visible_face`). It assumes
both `bsp` and `m` outlive the returned `vis_state` (typically until
the map is destroyed).

`vis_update` runs every frame, before the world draw. It:

1. Finds the leaf and cluster the camera is in (via `bsp_find_leaf`,
   after inverting the BSP-to-raylib coordinate swap).
2. Decompresses the PVS bit vector for that cluster (`bsp_decompress_pvs`).
3. Unions the face lists of every visible cluster into the
   `visible_face` bitset, then ORs in the "detail" face list (faces
   in leaves with cluster `0xFFFF`, which never appear in any PVS).
4. Also ORs in every face belonging to inline brush models (models
   1..N-1), since those are not reachable through the leaf tree.
5. Extracts the six frustum planes from `view_proj`.
6. For every face whose bit is set AND whose AABB passes the frustum
   test, copies its static index span into the surface's
   `frame_indices` staging buffer; accumulates a weighted centroid
   for transparent surfaces along the way.
7. Uploads each surface's `frame_indices` to the GPU via
   `rlUpdateVertexBufferElements`.

The return value is the number of faces that survived both PVS and
frustum culling (for telemetry).

If the BSP has no visibility data, no valid cluster (camera outside
the world), or PVS decompression fails, vis falls back to "draw every
kept face, frustum-cull only".

## Frustum extraction

`extract_frustum(view_proj, out[6])` derives six plane equations
`(nx, ny, nz, d)` from a column-major MVP matrix using the standard
Gribb/Hartmann method, then normalises each plane so distance
comparisons are scale-invariant.

Each plane is stored such that `dot(n, p) + d >= 0` means "inside the
frustum". The AABB-vs-plane test uses the p-vertex / n-vertex
shortcut: for each plane, pick the corner of the AABB that's farthest
along the plane normal; if that corner is on the negative side, the
whole box is outside.

## Transparent surface centroid

While walking visible faces, vis accumulates a weighted centroid for
each transparent surface using each face's `mesh_face_info.centroid`
weighted by its index count. The result is written to
`surface->frame_centroid` after the walk completes. The renderer uses
this per-frame centroid (rather than the static all-faces centroid)
for back-to-front sorting, so the sort is consistent with what's
actually visible.

When no faces are visible for a transparent surface, vis falls back
to the static `surface->centroid` to avoid having `frame_centroid`
stuck at an old value.

## Camera-cluster cache

`last_cluster` is recorded each frame. It's currently used only for
debug printing; a future optimisation could skip the PVS-decompression
+ visible-face-set rebuild when the camera hasn't crossed a cluster
boundary (the per-face frustum test still needs to run every frame).

## Failure modes

- **No visibility lump**: `num_clusters == 0`, cluster face table is
  `NULL`, vis falls back to "all faces". Frustum culling still runs.
- **Camera outside the world**: `bsp_find_leaf` returns `-1` or a
  leaf with cluster `0xFFFF`. Same fallback as above.
- **Malformed PVS data**: `bsp_decompress_pvs` returns `0` and vis
  uses the same fallback.

These are all correct (just less efficient) — they degrade to brute-
force frustum culling, which still produces the right image.
