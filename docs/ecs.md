# ecs

The `ecs` module is the entity-component-system core. It is intentionally
small: monotonic entity ids, one sparse-set pool per registered component
type, and a single-component query iterator. Systems are plain functions
that live elsewhere (see [sys.md](sys.md)) and call into `ecs.h`.

## Files

- `src/ecs/ecs.h`
- `src/ecs/ecs.c`

## Public API

### Types

```c
typedef uint32_t ecs_entity;
typedef uint32_t ecs_component_id;
typedef void (*ecs_component_dtor)(void *component_data);

typedef struct ecs_world ecs_world;
typedef struct ecs_iter {
    const ecs_world *world;
    ecs_component_id c;
    uint32_t i;
} ecs_iter;
```

`ECS_INVALID` is the reserved entity id `0`. Pass it anywhere a value is
expected when "no entity" is meaningful (e.g. optional arguments to
`sys_map_apply_spawn`).

`ECS_MAX_ENTITIES` (4096) caps the total number of `ecs_create` calls
across the world's lifetime. Entity ids are **never recycled**;
destroyed entities leave a hole in the alive bitmap.

`ECS_MAX_COMPONENTS` (32) caps the number of `ecs_register` calls. Each
registration allocates one fixed slot in `ecs_world::pools[]`.

### World

```c
ecs_world *ecs_world_create(void);
void       ecs_world_destroy(ecs_world *w);
```

`ecs_world_destroy` walks every pool, calls the registered destructor on
each live component, then frees pool storage and the alive array.

### Entities

```c
ecs_entity ecs_create(ecs_world *w);
void       ecs_destroy(ecs_world *w, ecs_entity e);
int        ecs_alive(const ecs_world *w, ecs_entity e);
```

`ecs_destroy` removes every component owned by `e` (calling dtors), then
marks `e` dead.

### Components

```c
ecs_component_id ecs_register(ecs_world *w,
                              const char *name,
                              uint32_t stride,
                              ecs_component_dtor dtor /* may be NULL */);

void *ecs_add(ecs_world *w, ecs_entity e, ecs_component_id c);
void *ecs_set(ecs_world *w, ecs_entity e, ecs_component_id c,
              const void *src);
void *ecs_get(const ecs_world *w, ecs_entity e, ecs_component_id c);
int   ecs_has(const ecs_world *w, ecs_entity e, ecs_component_id c);
void  ecs_remove(ecs_world *w, ecs_entity e, ecs_component_id c);
```

`ecs_add` returns a zeroed slot; the caller fills it in. `ecs_set`
memcpys from `src` (and adds the component first if missing).
`ecs_remove` performs a swap-and-pop in the dense array and patches the
sparse map for the moved component.

### Iteration

```c
ecs_iter ecs_query(const ecs_world *w, ecs_component_id c);
int      ecs_iter_next(ecs_iter *it, ecs_entity *out_entity, void **out_data);
```

`ecs_iter_next` returns 1 and writes to the out-params on each
iteration; returns 0 when exhausted. For multi-component queries, use
`ecs_get` on the secondary components from inside the loop.

## Storage layout

Each `ecs_pool` is a classic sparse set:

- `dense` is a packed byte buffer of `count * stride` bytes. Iteration
  walks `dense[0..count)` in insertion order (mutated by swap-and-pop on
  removal).
- `dense_to_entity[i]` maps `dense[i]` back to its owning entity.
- `sparse[entity_id]` is the dense index for that entity, or an
  internal `POOL_SLOT_EMPTY` sentinel (`UINT32_MAX`) when the entity is
  not in the pool. The sentinel is distinct from `ECS_INVALID` (`0`)
  because dense index `0` is a valid slot.

This gives `O(1)` `add`/`get`/`remove` and `O(N)` cache-friendly
iteration over a single component.

## Conventions for system authors

- Cache the `ecs_component_id` returned by `ecs_register` in a
  file-scope `static` variable of the system module.
- Initialize it from a public `sys_xxx_register(ecs_world *)` function
  that `main` calls before any `sys_xxx_spawn`.
- Components must be POD. If a component needs to own heap memory or
  GPU resources, register a `dtor` and clean it up there.
- Never store pointers into pool storage across calls that mutate the
  same pool. `ecs_add` and `ecs_remove` may reallocate `dense`.
