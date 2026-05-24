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
