#ifndef RENDER_H
#define RENDER_H

#include "raylib.h"
#include "mesh.h"

#include <stdint.h>

void r_init(void);
void r_shutdown(void);

void r_draw_mesh(const mesh *m, Vector3 cam_pos);
void r_draw_sky(Vector3 cam_pos, uint32_t bk, uint32_t dn, uint32_t ft, uint32_t lf, uint32_t rt, uint32_t up);

#endif
