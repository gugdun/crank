#include "sys_skybox.h"

#include "ecs/ecs.h"
#include "render.h"
#include "res/res_texture.h"
#include "sjson.h"
#include "texture.h"

#include <stdio.h>
#include <string.h>

static ecs_component_id g_c_skybox = ECS_MAX_COMPONENTS;

static void read_c_skybox(void *data, sjson_node *node) {
    c_skybox *s = data;
    memset(s, 0, sizeof(*s));
    s->size = sjson_get_float(node, "size", 4096.0f);
}

void sys_skybox_register(ecs_world *w) {
    if (w == NULL) {
        printf("sys_skybox_register: w = NULL\n");
        return;
    }
    g_c_skybox = ecs_register(w, "c_skybox", sizeof(c_skybox), NULL, read_c_skybox);
}

ecs_entity sys_skybox_spawn(ecs_world *w) {
    if (w == NULL) {
        printf("sys_skybox_spawn: w = NULL\n");
        return ECS_INVALID;
    }

    ecs_entity e = ecs_create(w);
    if (e == ECS_INVALID) {
        return ECS_INVALID;
    }

    c_skybox *s = ecs_add(w, e, g_c_skybox);
    if (s == NULL) {
        ecs_destroy(w, e);
        return ECS_INVALID;
    }
    s->ft = 0;
    s->bk = 0;
    s->lf = 0;
    s->rt = 0;
    s->up = 0;
    s->dn = 0;
    s->size = 4096.0f;
    return e;
}

void sys_skybox_set_sides(ecs_world *w,
                          ecs_entity e,
                          tex_handle ft,
                          tex_handle bk,
                          tex_handle lf,
                          tex_handle rt,
                          tex_handle up,
                          tex_handle dn) {
    c_skybox *s = ecs_get(w, e, g_c_skybox);
    if (s == NULL) {
        printf("sys_skybox_set_sides: entity %u has no c_skybox\n", e);
        return;
    }
    s->ft = ft;
    s->bk = bk;
    s->lf = lf;
    s->rt = rt;
    s->up = up;
    s->dn = dn;
}

static uint32_t resolve_id(const res_texture_mgr *texmgr, tex_handle h) {
    const texture *t = res_texture_get(texmgr, h);
    if (t == NULL) {
        return 0;
    }
    return t->id;
}

void sys_skybox_render(ecs_world *w,
                       const res_texture_mgr *texmgr,
                       Vector3 cam_pos) {
    if (w == NULL || texmgr == NULL) {
        return;
    }

    ecs_iter it = ecs_query(w, g_c_skybox);
    ecs_entity e = ECS_INVALID;
    void *data = NULL;
    while (ecs_iter_next(&it, &e, &data)) {
        c_skybox *s = (c_skybox *) data;

        uint32_t ft = resolve_id(texmgr, s->ft);
        uint32_t bk = resolve_id(texmgr, s->bk);
        uint32_t lf = resolve_id(texmgr, s->lf);
        uint32_t rt = resolve_id(texmgr, s->rt);
        uint32_t up = resolve_id(texmgr, s->up);
        uint32_t dn = resolve_id(texmgr, s->dn);

        if (ft == 0 || bk == 0 || lf == 0 || rt == 0 || up == 0 || dn == 0) {
            continue;
        }

        r_draw_sky(cam_pos, bk, dn, ft, lf, rt, up);
    }
}
