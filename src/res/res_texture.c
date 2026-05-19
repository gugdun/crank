#include "res_texture.h"

#include "texture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct res_texture_mgr {
    texture **entries;     // entries[i] for handle (i + 1); NULL slot means free
    uint32_t  count;
    uint32_t  capacity;
};

res_texture_mgr *res_texture_create(void) {
    res_texture_mgr *m = calloc(1, sizeof(res_texture_mgr));
    if (m == NULL) {
        printf("res_texture_create: failed to allocate manager\n");
        return NULL;
    }
    return m;
}

void res_texture_destroy(res_texture_mgr *m) {
    if (m == NULL) {
        printf("res_texture_destroy: m = NULL\n");
        return;
    }
    for (uint32_t i = 0; i < m->count; i++) {
        if (m->entries[i] != NULL) {
            tex_free(m->entries[i]);
            m->entries[i] = NULL;
        }
    }
    free(m->entries);
    free(m);
}

static int grow_entries(res_texture_mgr *m) {
    uint32_t new_cap = m->capacity == 0 ? 8u : m->capacity * 2u;
    texture **grown = realloc(m->entries, new_cap * sizeof(texture *));
    if (grown == NULL) {
        printf("res_texture: failed to grow entries to %u\n", new_cap);
        return 0;
    }
    for (uint32_t i = m->capacity; i < new_cap; i++) {
        grown[i] = NULL;
    }
    m->entries = grown;
    m->capacity = new_cap;
    return 1;
}

tex_handle res_texture_load(res_texture_mgr *m, const char *path) {
    if (m == NULL || path == NULL) {
        printf("res_texture_load: invalid arguments\n");
        return 0;
    }

    // Cache lookup
    for (uint32_t i = 0; i < m->count; i++) {
        const texture *t = m->entries[i];
        if (t == NULL || t->path == NULL) {
            continue;
        }
        if (strcmp(t->path, path) == 0) {
            return i + 1;
        }
    }

    texture *loaded = tex_load(path);
    if (loaded == NULL) {
        printf("res_texture_load: failed to load %s\n", path);
        return 0;
    }

    if (m->count >= m->capacity) {
        if (!grow_entries(m)) {
            tex_free(loaded);
            return 0;
        }
    }

    uint32_t idx = m->count++;
    m->entries[idx] = loaded;
    return idx + 1;
}

const texture *res_texture_get(const res_texture_mgr *m, tex_handle h) {
    if (m == NULL || h == 0) {
        return NULL;
    }
    uint32_t idx = h - 1;
    if (idx >= m->count) {
        return NULL;
    }
    return m->entries[idx];
}
