#ifndef ECS_H
#define ECS_H

#include <stdint.h>

#define ECS_INVALID         0u
#define ECS_MAX_ENTITIES    4096u
#define ECS_MAX_COMPONENTS  32u

typedef uint32_t ecs_entity;
typedef uint32_t ecs_component_id;

typedef void (*ecs_component_dtor)(void *component_data);

typedef struct {
    uint8_t  *dense;             // packed component bytes
    uint32_t *dense_to_entity;   // dense[i] belongs to dense_to_entity[i]
    uint32_t *sparse;            // sparse[entity_id] -> dense index, or ECS_INVALID
    uint32_t  count;
    uint32_t  capacity;
    uint32_t  stride;
    uint32_t  sparse_capacity;
    ecs_component_dtor dtor;
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
                              ecs_component_dtor dtor);

void *ecs_add(ecs_world *w, ecs_entity e, ecs_component_id c);
void *ecs_set(ecs_world *w, ecs_entity e, ecs_component_id c, const void *src);
void *ecs_get(const ecs_world *w, ecs_entity e, ecs_component_id c);
int   ecs_has(const ecs_world *w, ecs_entity e, ecs_component_id c);
void  ecs_remove(ecs_world *w, ecs_entity e, ecs_component_id c);

ecs_iter ecs_query(const ecs_world *w, ecs_component_id c);
int      ecs_iter_next(ecs_iter *it, ecs_entity *out_entity, void **out_data);

#endif
