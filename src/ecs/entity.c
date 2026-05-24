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

#include "entity.h"

#include "ecs/ecs.h"
#include "sjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ecs_entity entity_spawn_json(ecs_world *w, sjson_node *root) {
    if (w == NULL) {
        printf("entity_spawn_json: w = NULL\n");
        return ECS_INVALID;
    }
    if (root == NULL || root->tag != SJSON_OBJECT) {
        printf("entity_spawn_json: root is not an object\n");
        return ECS_INVALID;
    }

    sjson_node *components = sjson_find_member(root, "components");
    if (components == NULL || components->tag != SJSON_OBJECT) {
        printf("entity_spawn_json: missing 'components' object\n");
        return ECS_INVALID;
    }

    ecs_entity e = ecs_create(w);
    if (e == ECS_INVALID) {
        return ECS_INVALID;
    }

    int any_added = 0;
    sjson_node *comp_node;
    sjson_foreach(comp_node, components) {
        if (comp_node->key == NULL) {
            continue;
        }

        ecs_component_id cid = ecs_lookup(w, comp_node->key);
        if (cid >= ECS_MAX_COMPONENTS) {
            printf("entity_spawn_json: unknown component '%s'\n", comp_node->key);
            continue;
        }

        void *slot = ecs_add(w, e, cid);
        if (slot == NULL) {
            printf("entity_spawn_json: failed to add component '%s'\n", comp_node->key);
            continue;
        }

        // If the pool has a registered reader, call it to populate fields.
        // Otherwise the slot remains zeroed (default values).
        ecs_component_reader reader = ecs_pool_reader(w, cid);
        if (reader != NULL && comp_node->tag == SJSON_OBJECT) {
            reader(slot, comp_node);
        }

        any_added = 1;
    }

    if (!any_added) {
        // No valid components were added; clean up the empty entity.
        ecs_destroy(w, e);
        return ECS_INVALID;
    }

    return e;
}

ecs_entity entity_spawn_from_file(ecs_world *w,
                                  sjson_context *sctx,
                                  const char *path) {
    if (w == NULL) {
        printf("entity_spawn_from_file: w = NULL\n");
        return ECS_INVALID;
    }
    if (sctx == NULL) {
        printf("entity_spawn_from_file: sctx = NULL\n");
        return ECS_INVALID;
    }
    if (path == NULL) {
        printf("entity_spawn_from_file: path = NULL\n");
        return ECS_INVALID;
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        // Silently ignore missing files; the caller decides whether to warn.
        return ECS_INVALID;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        printf("entity_spawn_from_file: failed to seek %s\n", path);
        fclose(f);
        return ECS_INVALID;
    }

    long len = ftell(f);
    if (len < 0) {
        printf("entity_spawn_from_file: failed to tell %s\n", path);
        fclose(f);
        return ECS_INVALID;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        printf("entity_spawn_from_file: failed to rewind %s\n", path);
        fclose(f);
        return ECS_INVALID;
    }

    char *buf = calloc(1, (size_t) len + 1);
    if (buf == NULL) {
        printf("entity_spawn_from_file: failed to allocate buffer for %s\n", path);
        fclose(f);
        return ECS_INVALID;
    }

    size_t read = fread(buf, 1, (size_t) len, f);
    fclose(f);
    if (read != (size_t) len) {
        printf("entity_spawn_from_file: failed to read %s\n", path);
        free(buf);
        return ECS_INVALID;
    }

    sjson_reset_context(sctx);
    sjson_node *root = sjson_decode(sctx, buf);
    free(buf);
    if (root == NULL) {
        printf("entity_spawn_from_file: failed to parse JSON %s\n", path);
        return ECS_INVALID;
    }

    ecs_entity e = entity_spawn_json(w, root);
    // We intentionally do NOT call sjson_delete_node here because the
    // sjson_context owns the node memory. The caller manages the context.
    return e;
}
