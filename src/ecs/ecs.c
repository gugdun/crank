/*
===========================================================================
Copyright (C) 2026 gugdun

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, see <https://www.gnu.org/licenses/>.
===========================================================================
*/

#include "ecs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_ALIVE_CAP   64u
#define INITIAL_POOL_CAP    8u

// Sentinel for "entity not present in this pool". Stored in ecs_pool.sparse[e].
// Must differ from every valid dense index. ECS_INVALID is 0u and is used as
// the invalid-entity id, but 0u is also a valid dense index (the first slot),
// so we use a distinct sentinel here.
#define POOL_SLOT_EMPTY  ((uint32_t) UINT32_MAX)

static int ensure_alive_capacity(ecs_world *w, uint32_t needed) {
    if (needed <= w->alive_capacity) {
        return 1;
    }

    uint32_t new_cap = w->alive_capacity == 0 ? INITIAL_ALIVE_CAP : w->alive_capacity;
    while (new_cap < needed) {
        new_cap *= 2;
    }

    uint8_t *grown = realloc(w->alive, new_cap);
    if (grown == NULL) {
        printf("ecs_world: failed to grow alive array to %u\n", new_cap);
        return 0;
    }

    memset(grown + w->alive_capacity, 0, new_cap - w->alive_capacity);
    w->alive = grown;
    w->alive_capacity = new_cap;
    return 1;
}

static int ensure_sparse_capacity(ecs_pool *p, uint32_t needed) {
    if (needed <= p->sparse_capacity) {
        return 1;
    }

    uint32_t new_cap = p->sparse_capacity == 0 ? INITIAL_ALIVE_CAP : p->sparse_capacity;
    while (new_cap < needed) {
        new_cap *= 2;
    }

    uint32_t *grown = realloc(p->sparse, new_cap * sizeof(uint32_t));
    if (grown == NULL) {
        printf("ecs_pool: failed to grow sparse for %s to %u\n", p->name, new_cap);
        return 0;
    }

    for (uint32_t i = p->sparse_capacity; i < new_cap; i++) {
        grown[i] = POOL_SLOT_EMPTY;
    }
    p->sparse = grown;
    p->sparse_capacity = new_cap;
    return 1;
}

static int ensure_dense_capacity(ecs_pool *p, uint32_t needed) {
    if (needed <= p->capacity) {
        return 1;
    }

    uint32_t new_cap = p->capacity == 0 ? INITIAL_POOL_CAP : p->capacity;
    while (new_cap < needed) {
        new_cap *= 2;
    }

    uint8_t *new_dense = realloc(p->dense, (size_t) new_cap * p->stride);
    if (new_dense == NULL) {
        printf("ecs_pool: failed to grow dense for %s to %u\n", p->name, new_cap);
        return 0;
    }

    uint32_t *new_d2e = realloc(p->dense_to_entity, new_cap * sizeof(uint32_t));
    if (new_d2e == NULL) {
        printf("ecs_pool: failed to grow dense_to_entity for %s to %u\n", p->name, new_cap);
        // new_dense remains assigned; safe to keep
        p->dense = new_dense;
        p->capacity = p->capacity;
        return 0;
    }

    p->dense = new_dense;
    p->dense_to_entity = new_d2e;
    p->capacity = new_cap;
    return 1;
}

ecs_world *ecs_world_create(void) {
    ecs_world *w = calloc(1, sizeof(ecs_world));
    if (w == NULL) {
        printf("ecs_world_create: failed to allocate world\n");
        return NULL;
    }

    w->next_entity = 1;
    return w;
}

void ecs_world_destroy(ecs_world *w) {
    if (w == NULL) {
        printf("ecs_world_destroy: w = NULL\n");
        return;
    }

    for (uint32_t i = 0; i < w->pool_count; i++) {
        ecs_pool *p = &w->pools[i];
        if (p->dtor != NULL) {
            for (uint32_t k = 0; k < p->count; k++) {
                p->dtor(p->dense + (size_t) k * p->stride);
            }
        }
        free(p->dense);
        free(p->dense_to_entity);
        free(p->sparse);
    }

    free(w->alive);
    free(w);
}

ecs_entity ecs_create(ecs_world *w) {
    if (w == NULL) {
        printf("ecs_create: w = NULL\n");
        return ECS_INVALID;
    }

    if (w->next_entity >= ECS_MAX_ENTITIES) {
        printf("ecs_create: entity capacity exhausted (max %u)\n", ECS_MAX_ENTITIES);
        return ECS_INVALID;
    }

    ecs_entity id = w->next_entity++;
    if (!ensure_alive_capacity(w, id + 1)) {
        return ECS_INVALID;
    }
    w->alive[id] = 1;
    w->live_count++;
    return id;
}

int ecs_alive(const ecs_world *w, ecs_entity e) {
    if (w == NULL || e == ECS_INVALID) {
        return 0;
    }
    if (e >= w->alive_capacity) {
        return 0;
    }
    return w->alive[e] != 0;
}

static void pool_remove_internal(ecs_pool *p, ecs_entity e) {
    if (e >= p->sparse_capacity) {
        return;
    }
    uint32_t dense_idx = p->sparse[e];
    if (dense_idx == POOL_SLOT_EMPTY) {
        return;
    }

    if (p->dtor != NULL) {
        p->dtor(p->dense + (size_t) dense_idx * p->stride);
    }

    uint32_t last = p->count - 1;
    if (dense_idx != last) {
        // Swap last into dense_idx
        memcpy(p->dense + (size_t) dense_idx * p->stride,
               p->dense + (size_t) last * p->stride,
               p->stride);
        uint32_t moved_entity = p->dense_to_entity[last];
        p->dense_to_entity[dense_idx] = moved_entity;
        p->sparse[moved_entity] = dense_idx;
    }

    p->sparse[e] = POOL_SLOT_EMPTY;
    p->count = last;
}

void ecs_destroy(ecs_world *w, ecs_entity e) {
    if (w == NULL) {
        printf("ecs_destroy: w = NULL\n");
        return;
    }
    if (!ecs_alive(w, e)) {
        printf("ecs_destroy: entity %u not alive\n", e);
        return;
    }

    for (uint32_t i = 0; i < w->pool_count; i++) {
        pool_remove_internal(&w->pools[i], e);
    }

    w->alive[e] = 0;
    if (w->live_count > 0) {
        w->live_count--;
    }
}

ecs_component_id ecs_register(ecs_world *w,
                              const char *name,
                              uint32_t stride,
                              ecs_component_dtor dtor,
                              ecs_component_reader reader) {
    if (w == NULL) {
        printf("ecs_register: w = NULL\n");
        return ECS_MAX_COMPONENTS;
    }
    if (stride == 0) {
        printf("ecs_register: stride must be > 0\n");
        return ECS_MAX_COMPONENTS;
    }
    if (w->pool_count >= ECS_MAX_COMPONENTS) {
        printf("ecs_register: component capacity exhausted (max %u)\n", ECS_MAX_COMPONENTS);
        return ECS_MAX_COMPONENTS;
    }

    ecs_component_id id = w->pool_count++;
    ecs_pool *p = &w->pools[id];
    memset(p, 0, sizeof(*p));
    p->stride = stride;
    p->dtor = dtor;
    p->reader = reader;
    if (name != NULL) {
        size_t n = strlen(name);
        if (n >= sizeof(p->name)) {
            n = sizeof(p->name) - 1;
        }
        memcpy(p->name, name, n);
        p->name[n] = 0;
    } else {
        p->name[0] = 0;
    }
    return id;
}

ecs_component_id ecs_lookup(const ecs_world *w, const char *name) {
    if (w == NULL || name == NULL) {
        return ECS_MAX_COMPONENTS;
    }
    for (uint32_t i = 0; i < w->pool_count; i++) {
        if (strcmp(w->pools[i].name, name) == 0) {
            return i;
        }
    }
    return ECS_MAX_COMPONENTS;
}

ecs_component_reader ecs_pool_reader(const ecs_world *w, ecs_component_id c) {
    if (w == NULL) {
        return NULL;
    }
    if (c >= w->pool_count) {
        return NULL;
    }
    return w->pools[c].reader;
}

static int pool_check(const ecs_world *w, ecs_component_id c) {
    if (w == NULL) {
        return 0;
    }
    if (c >= w->pool_count) {
        return 0;
    }
    return 1;
}

void *ecs_add(ecs_world *w, ecs_entity e, ecs_component_id c) {
    if (!pool_check(w, c)) {
        printf("ecs_add: invalid pool %u\n", c);
        return NULL;
    }
    if (!ecs_alive(w, e)) {
        printf("ecs_add: entity %u not alive\n", e);
        return NULL;
    }

    ecs_pool *p = &w->pools[c];

    if (!ensure_sparse_capacity(p, e + 1)) {
        return NULL;
    }

    uint32_t existing = p->sparse[e];
    if (existing != POOL_SLOT_EMPTY) {
        // Already present; zero and return
        void *slot = p->dense + (size_t) existing * p->stride;
        memset(slot, 0, p->stride);
        return slot;
    }

    if (!ensure_dense_capacity(p, p->count + 1)) {
        return NULL;
    }

    uint32_t idx = p->count++;
    p->sparse[e] = idx;
    p->dense_to_entity[idx] = e;

    void *slot = p->dense + (size_t) idx * p->stride;
    memset(slot, 0, p->stride);
    return slot;
}

void *ecs_set(ecs_world *w, ecs_entity e, ecs_component_id c, const void *src) {
    void *slot = ecs_get(w, e, c);
    if (slot == NULL) {
        slot = ecs_add(w, e, c);
    }
    if (slot == NULL) {
        return NULL;
    }
    if (src != NULL) {
        memcpy(slot, src, w->pools[c].stride);
    }
    return slot;
}

void *ecs_get(const ecs_world *w, ecs_entity e, ecs_component_id c) {
    if (!pool_check(w, c)) {
        return NULL;
    }
    if (!ecs_alive(w, e)) {
        return NULL;
    }
    const ecs_pool *p = &w->pools[c];
    if (e >= p->sparse_capacity) {
        return NULL;
    }
    uint32_t idx = p->sparse[e];
    if (idx == POOL_SLOT_EMPTY) {
        return NULL;
    }
    return p->dense + (size_t) idx * p->stride;
}

int ecs_has(const ecs_world *w, ecs_entity e, ecs_component_id c) {
    return ecs_get(w, e, c) != NULL;
}

void ecs_remove(ecs_world *w, ecs_entity e, ecs_component_id c) {
    if (!pool_check(w, c)) {
        printf("ecs_remove: invalid pool %u\n", c);
        return;
    }
    if (!ecs_alive(w, e)) {
        printf("ecs_remove: entity %u not alive\n", e);
        return;
    }
    pool_remove_internal(&w->pools[c], e);
}

ecs_iter ecs_query(const ecs_world *w, ecs_component_id c) {
    ecs_iter it = {0};
    it.world = w;
    it.c = c;
    it.i = 0;
    return it;
}

int ecs_iter_next(ecs_iter *it, ecs_entity *out_entity, void **out_data) {
    if (it == NULL || it->world == NULL) {
        return 0;
    }
    if (it->c >= it->world->pool_count) {
        return 0;
    }
    const ecs_pool *p = &it->world->pools[it->c];
    if (it->i >= p->count) {
        return 0;
    }

    uint32_t idx = it->i++;
    if (out_entity != NULL) {
        *out_entity = p->dense_to_entity[idx];
    }
    if (out_data != NULL) {
        *out_data = p->dense + (size_t) idx * p->stride;
    }
    return 1;
}
