#include "res_map.h"

#include "bsp.h"
#include "mesh.h"
#include "phys.h"
#include "res_mesh.h"
#include "vis.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    bsp_model  *bsp;        // owned
    mesh_handle mesh;       // mesh manager owns the mesh
    vis_state  *vis;        // owned
    phys_world *phys;       // owned
    char       *name;       // owned
} map_entry;

struct res_map_mgr {
    map_entry    *entries;
    uint32_t      count;
    uint32_t      capacity;
    res_mesh_mgr *meshes;   // borrowed
};

res_map_mgr *res_map_create(res_mesh_mgr *meshes) {
    if (meshes == NULL) {
        printf("res_map_create: meshes = NULL\n");
        return NULL;
    }

    res_map_mgr *m = calloc(1, sizeof(res_map_mgr));
    if (m == NULL) {
        printf("res_map_create: failed to allocate manager\n");
        return NULL;
    }

    m->meshes = meshes;
    return m;
}

void res_map_destroy(res_map_mgr *m) {
    if (m == NULL) {
        printf("res_map_destroy: m = NULL\n");
        return;
    }
    for (uint32_t i = 0; i < m->count; i++) {
        if (m->entries[i].phys != NULL) {
            phys_destroy(m->entries[i].phys);
            m->entries[i].phys = NULL;
        }
        if (m->entries[i].vis != NULL) {
            vis_destroy(m->entries[i].vis);
            m->entries[i].vis = NULL;
        }
        if (m->entries[i].bsp != NULL) {
            bsp_free(m->entries[i].bsp);
            m->entries[i].bsp = NULL;
        }
        if (m->entries[i].name != NULL) {
            free(m->entries[i].name);
            m->entries[i].name = NULL;
        }
        // mesh is owned by res_mesh_mgr; do not free here
    }
    free(m->entries);
    free(m);
}

static int grow_entries(res_map_mgr *m) {
    uint32_t new_cap = m->capacity == 0 ? 4u : m->capacity * 2u;
    map_entry *grown = realloc(m->entries, new_cap * sizeof(map_entry));
    if (grown == NULL) {
        printf("res_map: failed to grow entries to %u\n", new_cap);
        return 0;
    }
    memset(grown + m->capacity, 0, (new_cap - m->capacity) * sizeof(map_entry));
    m->entries = grown;
    m->capacity = new_cap;
    return 1;
}

map_handle res_map_load(res_map_mgr *m, const char *name) {
    if (m == NULL || name == NULL) {
        printf("res_map_load: invalid arguments\n");
        return 0;
    }

    bsp_model *bsp = bsp_load(name);
    if (bsp == NULL) {
        printf("res_map_load: failed to load %s\n", name);
        return 0;
    }

    mesh *world_mesh = mesh_from_bsp(bsp);
    if (world_mesh == NULL) {
        printf("res_map_load: failed to build mesh for %s\n", name);
        bsp_free(bsp);
        return 0;
    }

    mesh_handle mh = res_mesh_adopt(m->meshes, world_mesh);
    if (mh == 0) {
        printf("res_map_load: failed to adopt mesh for %s\n", name);
        bsp_free(bsp);
        // mesh has been freed by res_mesh_adopt on failure
        return 0;
    }

    // Visibility state requires the live mesh pointer (it caches face metadata).
    const mesh *live_mesh = res_mesh_get(m->meshes, mh);
    vis_state *vis = vis_create(bsp, live_mesh);
    if (vis == NULL) {
        printf("res_map_load: failed to build visibility state for %s\n", name);
        bsp_free(bsp);
        // mesh stays in the mesh manager; cannot easily revoke
        return 0;
    }

    phys_world *phys = phys_create(bsp);
    if (phys == NULL) {
        printf("res_map_load: failed to build physics world for %s\n", name);
        vis_destroy(vis);
        bsp_free(bsp);
        return 0;
    }

    if (m->count >= m->capacity) {
        if (!grow_entries(m)) {
            phys_destroy(phys);
            vis_destroy(vis);
            bsp_free(bsp);
            // mesh stays in the mesh manager; cannot easily revoke
            return 0;
        }
    }

    size_t name_len = strlen(name);
    char *name_copy = calloc(1, name_len + 1);
    if (name_copy == NULL) {
        printf("res_map_load: failed to allocate name copy\n");
        phys_destroy(phys);
        vis_destroy(vis);
        bsp_free(bsp);
        return 0;
    }
    memcpy(name_copy, name, name_len + 1);

    uint32_t idx = m->count++;
    m->entries[idx].bsp = bsp;
    m->entries[idx].mesh = mh;
    m->entries[idx].vis = vis;
    m->entries[idx].phys = phys;
    m->entries[idx].name = name_copy;
    return idx + 1;
}

int res_map_get(const res_map_mgr *m, map_handle h, map_view *out) {
    if (m == NULL || h == 0 || out == NULL) {
        return 0;
    }
    uint32_t idx = h - 1;
    if (idx >= m->count) {
        return 0;
    }
    out->bsp = m->entries[idx].bsp;
    out->mesh = m->entries[idx].mesh;
    out->vis = m->entries[idx].vis;
    out->phys = m->entries[idx].phys;
    out->name = m->entries[idx].name;
    return 1;
}
