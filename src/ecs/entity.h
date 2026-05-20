#ifndef ENTITY_H
#define ENTITY_H

#include "ecs/ecs.h"
#include "sjson.h"

// Spawn an entity from a parsed JSON root node.
// The node must be an object with a "components" member.
// Returns entity id, or ECS_INVALID on failure.
ecs_entity entity_spawn_json(ecs_world *w, sjson_node *root);

// Load a JSON file, parse it, and spawn.
// Returns entity id, or ECS_INVALID on failure.
ecs_entity entity_spawn_from_file(ecs_world *w,
                                  sjson_context *sctx,
                                  const char *path);

#endif
