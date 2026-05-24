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

#ifndef ECS_H
#define ECS_H

#include <stdint.h>

#define ECS_INVALID         0u
#define ECS_MAX_ENTITIES    4096u
#define ECS_MAX_COMPONENTS  32u

typedef uint32_t ecs_entity;
typedef uint32_t ecs_component_id;

typedef void (*ecs_component_dtor)(void *component_data);

// Forward declaration so the ECS header does not depend on sjson.h.
struct sjson_node;
typedef void (*ecs_component_reader)(void *component_data,
                                      struct sjson_node *node);

typedef struct {
    uint8_t  *dense;             // packed component bytes
    uint32_t *dense_to_entity;   // dense[i] belongs to dense_to_entity[i]
    uint32_t *sparse;            // sparse[entity_id] -> dense index, or ECS_INVALID
    uint32_t  count;
    uint32_t  capacity;
    uint32_t  stride;
    uint32_t  sparse_capacity;
    ecs_component_dtor dtor;
    ecs_component_reader reader;
    char      name[32];
} ecs_pool;

typedef struct {
    uint8_t   *alive;            // alive[entity_id] (byte per entity)
    uint32_t   alive_capacity;
    uint32_t   next_entity;      // monotonic id counter, starts at 1
    uint32_t   live_count;

    ecs_pool   pools[ECS_MAX_COMPONENTS];
    uint32_t   pool_count;
} ecs_world;

typedef struct {
    const ecs_world *world;
    ecs_component_id c;
    uint32_t i;
} ecs_iter;

ecs_world *ecs_world_create(void);
void       ecs_world_destroy(ecs_world *w);

ecs_entity ecs_create(ecs_world *w);
void       ecs_destroy(ecs_world *w, ecs_entity e);
int        ecs_alive(const ecs_world *w, ecs_entity e);

ecs_component_id ecs_register(ecs_world *w,
                              const char *name,
                              uint32_t stride,
                              ecs_component_dtor dtor,
                              ecs_component_reader reader);

// Look up a registered component by its name string.
// Returns the component id, or ECS_MAX_COMPONENTS if not found.
ecs_component_id ecs_lookup(const ecs_world *w, const char *name);

// Return the registered reader for a component, or NULL if none.
ecs_component_reader ecs_pool_reader(const ecs_world *w, ecs_component_id c);

void *ecs_add(ecs_world *w, ecs_entity e, ecs_component_id c);
void *ecs_set(ecs_world *w, ecs_entity e, ecs_component_id c, const void *src);
void *ecs_get(const ecs_world *w, ecs_entity e, ecs_component_id c);
int   ecs_has(const ecs_world *w, ecs_entity e, ecs_component_id c);
void  ecs_remove(ecs_world *w, ecs_entity e, ecs_component_id c);

ecs_iter ecs_query(const ecs_world *w, ecs_component_id c);
int      ecs_iter_next(ecs_iter *it, ecs_entity *out_entity, void **out_data);

#endif
