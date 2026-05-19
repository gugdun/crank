#include "res_mesh.h"

#include "mesh.h"

#include <stdio.h>
#include <stdlib.h>

struct res_mesh_mgr {
    mesh    **entries;
    uint32_t  count;
    uint32_t  capacity;
};

res_mesh_mgr *res_mesh_create(void) {
    res_mesh_mgr *m = calloc(1, sizeof(res_mesh_mgr));
    if (m == NULL) {
        printf("res_mesh_create: failed to allocate manager\n");
        return NULL;
    }
    return m;
}

void res_mesh_destroy(res_mesh_mgr *m) {
    if (m == NULL) {
        printf("res_mesh_destroy: m = NULL\n");
        return;
    }
    for (uint32_t i = 0; i < m->count; i++) {
        if (m->entries[i] != NULL) {
            mesh_free(m->entries[i]);
            m->entries[i] = NULL;
        }
    }
    free(m->entries);
    free(m);
}

static int grow_entries(res_mesh_mgr *m) {
    uint32_t new_cap = m->capacity == 0 ? 4u : m->capacity * 2u;
    mesh **grown = realloc(m->entries, new_cap * sizeof(mesh *));
    if (grown == NULL) {
        printf("res_mesh: failed to grow entries to %u\n", new_cap);
        return 0;
    }
    for (uint32_t i = m->capacity; i < new_cap; i++) {
        grown[i] = NULL;
    }
    m->entries = grown;
    m->capacity = new_cap;
    return 1;
}

mesh_handle res_mesh_adopt(res_mesh_mgr *m, mesh *mesh_obj) {
    if (m == NULL || mesh_obj == NULL) {
        printf("res_mesh_adopt: invalid arguments\n");
        if (mesh_obj != NULL) {
            mesh_free(mesh_obj);
        }
        return 0;
    }

    if (m->count >= m->capacity) {
        if (!grow_entries(m)) {
            mesh_free(mesh_obj);
            return 0;
        }
    }

    uint32_t idx = m->count++;
    m->entries[idx] = mesh_obj;
    return idx + 1;
}

const mesh *res_mesh_get(const res_mesh_mgr *m, mesh_handle h) {
    if (m == NULL || h == 0) {
        return NULL;
    }
    uint32_t idx = h - 1;
    if (idx >= m->count) {
        return NULL;
    }
    return m->entries[idx];
}
