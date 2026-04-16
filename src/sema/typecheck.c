#include "cnegative/sema.h"

#include <stdio.h>

typedef struct cn_binding {
    cn_strview name;
    const cn_type_ref *type;
    bool is_mutable;
    size_t scope_zone_depth;
    size_t value_zone_depth;
    const struct cn_binding *result_guard_binding;
    bool has_result_guard_alias;
    bool result_guard_positive;
    struct cn_binding *next;
} cn_binding;

typedef struct cn_name_map_entry {
    cn_strview key;
    const void *value;
    size_t index;
    bool occupied;
} cn_name_map_entry;

typedef struct cn_name_map {
    cn_name_map_entry *entries;
    size_t capacity;
    size_t count;
} cn_name_map;

typedef struct cn_result_guard {
    const cn_binding *binding;
    struct cn_result_guard *next;
} cn_result_guard;

typedef struct cn_scope {
    cn_binding *bindings;
    cn_name_map binding_map;
    cn_result_guard *result_guards;
    size_t zone_depth;
    struct cn_scope *parent;
} cn_scope;

typedef struct cn_temp_type {
    cn_type_ref *type;
    struct cn_temp_type *next;
} cn_temp_type;

typedef struct cn_resolved_struct {
    const cn_module *module;
    const cn_struct_decl *decl;
} cn_resolved_struct;

typedef struct cn_resolved_const {
    const cn_module *module;
    const cn_const_decl *decl;
} cn_resolved_const;

typedef struct cn_module_symbol_cache {
    const cn_module *module;
    cn_name_map local_functions;
    cn_name_map public_functions;
    cn_name_map local_consts;
    cn_name_map public_consts;
    cn_name_map local_structs;
    cn_name_map public_structs;
    cn_name_map imports;
    struct cn_module_symbol_cache *next;
} cn_module_symbol_cache;

typedef struct cn_sema_ctx {
    const cn_project *project;
    const cn_module *module;
    cn_diag_bag *diagnostics;
    cn_allocator *allocator;
    const cn_function *current_function;
    cn_temp_type *temp_types;
    cn_module_symbol_cache *module_caches;
} cn_sema_ctx;

typedef struct cn_assignment_target {
    const cn_type_ref *type;
    bool requires_mutable_binding;
    cn_strview binding_name;
    size_t binding_scope_zone_depth;
} cn_assignment_target;

static const cn_type_ref g_type_int = {CN_TYPE_INT, {NULL, 0}, {NULL, 0}, NULL, NULL, 0, 0};
static const cn_type_ref g_type_u8 = {CN_TYPE_U8, {NULL, 0}, {NULL, 0}, NULL, NULL, 0, 0};
static const cn_type_ref g_type_bool = {CN_TYPE_BOOL, {NULL, 0}, {NULL, 0}, NULL, NULL, 0, 0};
static const cn_type_ref g_type_str = {CN_TYPE_STR, {NULL, 0}, {NULL, 0}, NULL, NULL, 0, 0};
static const cn_type_ref g_type_void = {CN_TYPE_VOID, {NULL, 0}, {NULL, 0}, NULL, NULL, 0, 0};
static const cn_type_ref g_type_null = {CN_TYPE_NULL, {NULL, 0}, {NULL, 0}, NULL, NULL, 0, 0};
static const cn_type_ref g_type_unknown = {CN_TYPE_UNKNOWN, {NULL, 0}, {NULL, 0}, NULL, NULL, 0, 0};

static const cn_type_ref *cn_builtin_type(cn_type_kind kind) {
    switch (kind) {
    case CN_TYPE_INT: return &g_type_int;
    case CN_TYPE_U8: return &g_type_u8;
    case CN_TYPE_BOOL: return &g_type_bool;
    case CN_TYPE_STR: return &g_type_str;
    case CN_TYPE_VOID: return &g_type_void;
    case CN_TYPE_NULL: return &g_type_null;
    default: return &g_type_unknown;
    }
}

static bool cn_resolve_non_negative_const_size(
    cn_sema_ctx *ctx,
    const cn_expr *expression,
    size_t offset,
    const char *code,
    const char *message,
    size_t *out_size
);

static bool cn_name_eq(cn_strview left, cn_strview right) {
    return cn_sv_eq(left, right);
}

static size_t cn_name_map_next_capacity(size_t required) {
    size_t capacity = 16;
    while (capacity < required) {
        capacity *= 2;
    }
    return capacity;
}

static cn_name_map_entry *cn_name_map_find_slot(cn_name_map_entry *entries, size_t capacity, cn_strview key) {
    size_t mask = capacity - 1;
    size_t index = (size_t)(cn_sv_hash(key) & (uint64_t)mask);
    while (entries[index].occupied && !cn_name_eq(entries[index].key, key)) {
        index = (index + 1) & mask;
    }
    return &entries[index];
}

static bool cn_name_map_reserve(cn_sema_ctx *ctx, cn_name_map *map, size_t required) {
    if (map->capacity >= required) {
        return true;
    }

    size_t new_capacity = cn_name_map_next_capacity(required);
    cn_name_map_entry *new_entries = CN_CALLOC(ctx->allocator, cn_name_map_entry, new_capacity);
    if (new_entries == NULL) {
        return false;
    }

    for (size_t i = 0; i < map->capacity; ++i) {
        if (!map->entries[i].occupied) {
            continue;
        }

        cn_name_map_entry *slot = cn_name_map_find_slot(new_entries, new_capacity, map->entries[i].key);
        *slot = map->entries[i];
    }

    CN_FREE(ctx->allocator, map->entries);
    map->entries = new_entries;
    map->capacity = new_capacity;
    return true;
}

static bool cn_name_map_insert(
    cn_sema_ctx *ctx,
    cn_name_map *map,
    cn_strview key,
    const void *value,
    size_t index,
    bool replace
) {
    size_t required_capacity = map->capacity == 0 ? 16 : map->capacity;
    if ((map->count + 1) * 10 >= required_capacity * 7) {
        required_capacity *= 2;
    }

    if (!cn_name_map_reserve(ctx, map, required_capacity)) {
        return false;
    }

    cn_name_map_entry *slot = cn_name_map_find_slot(map->entries, map->capacity, key);
    if (slot->occupied && !replace) {
        return false;
    }

    if (!slot->occupied) {
        map->count += 1;
        slot->occupied = true;
        slot->key = key;
    }

    slot->value = value;
    slot->index = index;
    return true;
}

static const cn_name_map_entry *cn_name_map_lookup_entry(const cn_name_map *map, cn_strview key) {
    if (map->capacity == 0) {
        return NULL;
    }

    size_t mask = map->capacity - 1;
    size_t index = (size_t)(cn_sv_hash(key) & (uint64_t)mask);
    while (map->entries[index].occupied) {
        if (cn_name_eq(map->entries[index].key, key)) {
            return &map->entries[index];
        }

        index = (index + 1) & mask;
    }

    return NULL;
}

static const void *cn_name_map_lookup_value(const cn_name_map *map, cn_strview key) {
    const cn_name_map_entry *entry = cn_name_map_lookup_entry(map, key);
    return entry == NULL ? NULL : entry->value;
}

static bool cn_name_map_lookup_index(const cn_name_map *map, cn_strview key, size_t *out_index) {
    const cn_name_map_entry *entry = cn_name_map_lookup_entry(map, key);
    if (entry == NULL) {
        return false;
    }
    if (out_index != NULL) {
        *out_index = entry->index;
    }
    return true;
}

static void cn_name_map_release(cn_sema_ctx *ctx, cn_name_map *map) {
    CN_FREE(ctx->allocator, map->entries);
    map->entries = NULL;
    map->capacity = 0;
    map->count = 0;
}

static cn_module_symbol_cache *cn_get_module_symbol_cache(cn_sema_ctx *ctx, const cn_module *module) {
    for (cn_module_symbol_cache *cache = ctx->module_caches; cache != NULL; cache = cache->next) {
        if (cache->module == module) {
            return cache;
        }
    }

    cn_module_symbol_cache *cache = CN_ALLOC(ctx->allocator, cn_module_symbol_cache);
    memset(cache, 0, sizeof(*cache));
    cache->module = module;
    cache->next = ctx->module_caches;
    ctx->module_caches = cache;

    if (module == NULL || module->program == NULL) {
        return cache;
    }

    for (size_t i = 0; i < module->program->function_count; ++i) {
        cn_function *function = module->program->functions[i];
        cn_name_map_insert(ctx, &cache->local_functions, function->name, function, 0, true);
        if (function->is_public) {
            cn_name_map_insert(ctx, &cache->public_functions, function->name, function, 0, true);
        }
    }

    for (size_t i = 0; i < module->program->const_count; ++i) {
        cn_const_decl *const_decl = module->program->consts[i];
        cn_name_map_insert(ctx, &cache->local_consts, const_decl->name, const_decl, 0, true);
        if (const_decl->is_public) {
            cn_name_map_insert(ctx, &cache->public_consts, const_decl->name, const_decl, 0, true);
        }
    }

    for (size_t i = 0; i < module->program->struct_count; ++i) {
        cn_struct_decl *struct_decl = module->program->structs[i];
        cn_name_map_insert(ctx, &cache->local_structs, struct_decl->name, struct_decl, 0, true);
        if (struct_decl->is_public) {
            cn_name_map_insert(ctx, &cache->public_structs, struct_decl->name, struct_decl, 0, true);
        }
    }

    for (size_t i = 0; i < module->program->import_count; ++i) {
        cn_import_decl *import_decl = &module->program->imports[i];
        cn_name_map_insert(ctx, &cache->imports, import_decl->module_name, NULL, i, true);
        cn_name_map_insert(ctx, &cache->imports, import_decl->alias, NULL, i, true);
    }

    return cache;
}

static void cn_release_module_symbol_caches(cn_sema_ctx *ctx) {
    cn_module_symbol_cache *cache = ctx->module_caches;
    while (cache != NULL) {
        cn_module_symbol_cache *next = cache->next;
        cn_name_map_release(ctx, &cache->local_functions);
        cn_name_map_release(ctx, &cache->public_functions);
        cn_name_map_release(ctx, &cache->local_consts);
        cn_name_map_release(ctx, &cache->public_consts);
        cn_name_map_release(ctx, &cache->local_structs);
        cn_name_map_release(ctx, &cache->public_structs);
        cn_name_map_release(ctx, &cache->imports);
        CN_FREE(ctx->allocator, cache);
        cache = next;
    }
    ctx->module_caches = NULL;
}

static const cn_function *cn_find_local_function(cn_sema_ctx *ctx, const cn_module *module, cn_strview name) {
    cn_module_symbol_cache *cache = cn_get_module_symbol_cache(ctx, module);
    return (const cn_function *)cn_name_map_lookup_value(&cache->local_functions, name);
}

static const cn_function *cn_find_public_function(cn_sema_ctx *ctx, const cn_module *module, cn_strview name) {
    cn_module_symbol_cache *cache = cn_get_module_symbol_cache(ctx, module);
    return (const cn_function *)cn_name_map_lookup_value(&cache->public_functions, name);
}

static const cn_const_decl *cn_find_const(cn_sema_ctx *ctx, const cn_module *module, cn_strview name) {
    cn_module_symbol_cache *cache = cn_get_module_symbol_cache(ctx, module);
    return (const cn_const_decl *)cn_name_map_lookup_value(&cache->local_consts, name);
}

static const cn_const_decl *cn_find_public_const(cn_sema_ctx *ctx, const cn_module *module, cn_strview name) {
    cn_module_symbol_cache *cache = cn_get_module_symbol_cache(ctx, module);
    return (const cn_const_decl *)cn_name_map_lookup_value(&cache->public_consts, name);
}

static const cn_struct_decl *cn_find_struct(cn_sema_ctx *ctx, const cn_module *module, cn_strview name) {
    cn_module_symbol_cache *cache = cn_get_module_symbol_cache(ctx, module);
    return (const cn_struct_decl *)cn_name_map_lookup_value(&cache->local_structs, name);
}

static const cn_struct_decl *cn_find_public_struct(cn_sema_ctx *ctx, const cn_module *module, cn_strview name) {
    cn_module_symbol_cache *cache = cn_get_module_symbol_cache(ctx, module);
    return (const cn_struct_decl *)cn_name_map_lookup_value(&cache->public_structs, name);
}

static const cn_module *cn_find_imported_module(cn_sema_ctx *ctx, const cn_module *module, cn_strview name);

static const cn_module *cn_resolve_source_named_module(cn_sema_ctx *ctx, cn_strview module_name) {
    if (module_name.length == 0 || cn_sv_eq_cstr(module_name, ctx->module->name)) {
        return ctx->module;
    }

    return cn_find_imported_module(ctx, ctx->module, module_name);
}

static const cn_module *cn_resolve_canonical_named_module(cn_sema_ctx *ctx, cn_strview module_name) {
    if (module_name.length == 0 || cn_sv_eq_cstr(module_name, ctx->module->name)) {
        return ctx->module;
    }

    const cn_module *project_module = cn_project_find_module_by_name(ctx->project, module_name);
    if (project_module != NULL) {
        return project_module;
    }

    return cn_find_imported_module(ctx, ctx->module, module_name);
}

static cn_resolved_const cn_resolve_const_in_module(cn_sema_ctx *ctx, const cn_module *module, cn_strview const_name) {
    cn_resolved_const result;
    result.module = NULL;
    result.decl = NULL;

    if (module == NULL || module->program == NULL) {
        return result;
    }

    result.module = module;
    result.decl = cn_find_const(ctx, module, const_name);
    if (result.decl == NULL) {
        result.module = NULL;
    }
    return result;
}

static cn_resolved_const cn_resolve_source_named_const(cn_sema_ctx *ctx, cn_strview module_name, cn_strview const_name) {
    const cn_module *module = cn_resolve_source_named_module(ctx, module_name);
    if (module == NULL) {
        return cn_resolve_const_in_module(ctx, NULL, const_name);
    }

    if (module == ctx->module) {
        return cn_resolve_const_in_module(ctx, module, const_name);
    }

    cn_resolved_const result;
    result.module = module;
    result.decl = cn_find_public_const(ctx, module, const_name);
    if (result.decl == NULL) {
        result.module = NULL;
    }
    return result;
}

static cn_resolved_struct cn_resolve_struct_in_module(cn_sema_ctx *ctx, const cn_module *module, cn_strview type_name) {
    cn_resolved_struct result;
    result.module = NULL;
    result.decl = NULL;

    if (module == NULL || module->program == NULL) {
        return result;
    }

    result.module = module;
    result.decl = cn_find_struct(ctx, module, type_name);
    if (result.decl == NULL) {
        result.module = NULL;
    }
    return result;
}

static cn_resolved_struct cn_resolve_source_named_struct(cn_sema_ctx *ctx, cn_strview module_name, cn_strview type_name) {
    const cn_module *module = cn_resolve_source_named_module(ctx, module_name);
    if (module == NULL) {
        return cn_resolve_struct_in_module(ctx, NULL, type_name);
    }

    if (module == ctx->module) {
        return cn_resolve_struct_in_module(ctx, module, type_name);
    }

    cn_resolved_struct result;
    result.module = module;
    result.decl = cn_find_public_struct(ctx, module, type_name);
    if (result.decl == NULL) {
        result.module = NULL;
    }
    return result;
}

static cn_resolved_struct cn_resolve_canonical_named_struct(cn_sema_ctx *ctx, cn_strview module_name, cn_strview type_name) {
    return cn_resolve_struct_in_module(ctx, cn_resolve_canonical_named_module(ctx, module_name), type_name);
}

static const cn_import_decl *cn_find_import_decl(cn_sema_ctx *ctx, const cn_module *module, cn_strview name, size_t *out_index) {
    if (module == NULL || module->program == NULL) {
        return NULL;
    }

    cn_module_symbol_cache *cache = cn_get_module_symbol_cache(ctx, module);
    size_t index = 0;
    if (!cn_name_map_lookup_index(&cache->imports, name, &index)) {
        return NULL;
    }

    if (out_index != NULL) {
        *out_index = index;
    }
    return &module->program->imports[index];
}

static const cn_module *cn_find_imported_module(cn_sema_ctx *ctx, const cn_module *module, cn_strview name) {
    size_t index = 0;
    if (cn_find_import_decl(ctx, module, name, &index) == NULL) {
        return NULL;
    }

    if (index >= module->import_count) {
        return NULL;
    }

    return module->imports[index];
}

static const cn_struct_field *cn_find_struct_field(const cn_struct_decl *struct_decl, cn_strview field_name, size_t *out_index) {
    for (size_t i = 0; i < struct_decl->fields.count; ++i) {
        if (cn_name_eq(struct_decl->fields.items[i].name, field_name)) {
            if (out_index != NULL) {
                *out_index = i;
            }
            return &struct_decl->fields.items[i];
        }
    }
    return NULL;
}

static const cn_binding *cn_scope_lookup(const cn_scope *scope, cn_strview name) {
    for (const cn_scope *cursor = scope; cursor != NULL; cursor = cursor->parent) {
        const cn_binding *binding = (const cn_binding *)cn_name_map_lookup_value(&cursor->binding_map, name);
        if (binding != NULL) {
            return binding;
        }
    }
    return NULL;
}

static cn_binding *cn_scope_lookup_mut(cn_scope *scope, cn_strview name) {
    for (cn_scope *cursor = scope; cursor != NULL; cursor = cursor->parent) {
        cn_binding *binding = (cn_binding *)cn_name_map_lookup_value(&cursor->binding_map, name);
        if (binding != NULL) {
            return binding;
        }
    }
    return NULL;
}

static bool cn_scope_result_is_ok(const cn_scope *scope, const cn_binding *binding) {
    if (binding == NULL) {
        return false;
    }

    for (const cn_scope *cursor = scope; cursor != NULL; cursor = cursor->parent) {
        for (const cn_result_guard *guard = cursor->result_guards; guard != NULL; guard = guard->next) {
            if (guard->binding == binding) {
                return true;
            }
        }
    }
    return false;
}

static void cn_scope_mark_result_ok(cn_sema_ctx *ctx, cn_scope *scope, const cn_binding *binding) {
    if (scope == NULL || binding == NULL) {
        return;
    }

    for (const cn_result_guard *guard = scope->result_guards; guard != NULL; guard = guard->next) {
        if (guard->binding == binding) {
            return;
        }
    }

    cn_result_guard *guard = CN_ALLOC(ctx->allocator, cn_result_guard);
    guard->binding = binding;
    guard->next = scope->result_guards;
    scope->result_guards = guard;
}

static bool cn_scope_define(
    cn_sema_ctx *ctx,
    cn_scope *scope,
    cn_strview name,
    const cn_type_ref *type,
    bool is_mutable,
    size_t value_zone_depth,
    size_t offset
) {
    if (cn_name_map_lookup_entry(&scope->binding_map, name) != NULL) {
        cn_diag_emit(
            ctx->diagnostics,
            CN_DIAG_ERROR,
            "E3003",
            offset,
            "duplicate local binding '%.*s'",
            (int)name.length,
            name.data
        );
        return false;
    }

    cn_binding *binding = CN_ALLOC(ctx->allocator, cn_binding);
    binding->name = name;
    binding->type = type;
    binding->is_mutable = is_mutable;
    binding->scope_zone_depth = scope != NULL ? scope->zone_depth : 0;
    binding->value_zone_depth = value_zone_depth;
    binding->result_guard_binding = NULL;
    binding->has_result_guard_alias = false;
    binding->result_guard_positive = true;
    binding->next = scope->bindings;
    scope->bindings = binding;
    cn_name_map_insert(ctx, &scope->binding_map, name, binding, 0, true);
    return true;
}

static void cn_scope_release(cn_sema_ctx *ctx, cn_scope *scope) {
    cn_binding *binding = scope->bindings;
    while (binding != NULL) {
        cn_binding *next = binding->next;
        CN_FREE(ctx->allocator, binding);
        binding = next;
    }
    scope->bindings = NULL;
    cn_name_map_release(ctx, &scope->binding_map);

    cn_result_guard *guard = scope->result_guards;
    while (guard != NULL) {
        cn_result_guard *next = guard->next;
        CN_FREE(ctx->allocator, guard);
        guard = next;
    }
    scope->result_guards = NULL;
}

static void cn_temp_types_release(cn_sema_ctx *ctx) {
    cn_temp_type *node = ctx->temp_types;
    while (node != NULL) {
        cn_temp_type *next = node->next;
        CN_FREE(ctx->allocator, node->type);
        CN_FREE(ctx->allocator, node);
        node = next;
    }
    ctx->temp_types = NULL;
}

static const cn_type_ref *cn_make_temp_type(
    cn_sema_ctx *ctx,
    cn_type_kind kind,
    cn_strview module_name,
    cn_strview name,
    const cn_type_ref *inner,
    size_t array_size,
    size_t offset
) {
    cn_type_ref *type = CN_ALLOC(ctx->allocator, cn_type_ref);
    type->kind = kind;
    type->module_name = module_name;
    type->name = name;
    type->inner = (cn_type_ref *)inner;
    type->array_size = array_size;
    type->offset = offset;

    cn_temp_type *node = CN_ALLOC(ctx->allocator, cn_temp_type);
    node->type = type;
    node->next = ctx->temp_types;
    ctx->temp_types = node;
    return type;
}

static void cn_emit_type_mismatch(cn_diag_bag *diagnostics, size_t offset, const char *message, const cn_type_ref *expected, const cn_type_ref *actual) {
    char expected_name[128];
    char actual_name[128];
    cn_type_describe(expected, expected_name, sizeof(expected_name));
    cn_type_describe(actual, actual_name, sizeof(actual_name));
    cn_diag_emit(diagnostics, CN_DIAG_ERROR, "E3004", offset, "%s: expected %s, got %s", message, expected_name, actual_name);
}

static bool cn_type_is_integer_like(const cn_type_ref *type) {
    return type != NULL && (type->kind == CN_TYPE_INT || type->kind == CN_TYPE_U8);
}

static bool cn_integer_types_match(const cn_type_ref *left, const cn_type_ref *right) {
    return cn_type_is_integer_like(left) && cn_type_equal(left, right);
}

static bool cn_type_can_assign_to(const cn_type_ref *expected, const cn_type_ref *actual) {
    if (expected == NULL || actual == NULL) {
        return false;
    }

    if (cn_type_equal(expected, actual)) {
        return true;
    }

    if (expected->kind == CN_TYPE_SLICE && actual->kind == CN_TYPE_ARRAY) {
        return cn_type_equal(expected->inner, actual->inner);
    }

    if (expected->kind == CN_TYPE_PTR && actual->kind == CN_TYPE_NULL) {
        return true;
    }

    return false;
}

static const cn_type_ref *cn_check_expression_hint(
    cn_sema_ctx *ctx,
    cn_scope *scope,
    const cn_expr *expression,
    const cn_type_ref *expected
);

static void cn_mark_result_ok_proofs_for_branch(
    cn_sema_ctx *ctx,
    cn_scope *target_scope,
    const cn_scope *analysis_scope,
    const cn_expr *expression,
    bool branch_truth
);

static bool cn_type_may_carry_zone_value_inner(
    cn_sema_ctx *ctx,
    const cn_type_ref *type,
    cn_strview *module_stack,
    cn_strview *name_stack,
    size_t stack_count
) {
    if (type == NULL) {
        return false;
    }

    switch (type->kind) {
    case CN_TYPE_PTR:
    case CN_TYPE_SLICE:
        return true;
    case CN_TYPE_RESULT:
    case CN_TYPE_ARRAY:
        return cn_type_may_carry_zone_value_inner(ctx, type->inner, module_stack, name_stack, stack_count);
    case CN_TYPE_NAMED: {
        cn_resolved_struct resolved = cn_resolve_canonical_named_struct(ctx, type->module_name, type->name);
        if (resolved.decl == NULL || resolved.module == NULL) {
            return true;
        }

        cn_strview module_name = cn_sv_from_cstr(resolved.module->name);
        for (size_t i = 0; i < stack_count; ++i) {
            if (cn_sv_eq(module_stack[i], module_name) && cn_sv_eq(name_stack[i], resolved.decl->name)) {
                return false;
            }
        }

        if (stack_count >= 32) {
            return true;
        }

        module_stack[stack_count] = module_name;
        name_stack[stack_count] = resolved.decl->name;
        stack_count += 1;

        for (size_t i = 0; i < resolved.decl->fields.count; ++i) {
            if (cn_type_may_carry_zone_value_inner(
                    ctx,
                    resolved.decl->fields.items[i].type,
                    module_stack,
                    name_stack,
                    stack_count
                )) {
                return true;
            }
        }
        return false;
    }
    case CN_TYPE_UNKNOWN:
        return true;
    case CN_TYPE_INT:
    case CN_TYPE_U8:
    case CN_TYPE_BOOL:
    case CN_TYPE_STR:
    case CN_TYPE_VOID:
    case CN_TYPE_NULL:
        return false;
    }

    return false;
}

static bool cn_type_may_carry_zone_value(cn_sema_ctx *ctx, const cn_type_ref *type) {
    cn_strview module_stack[32];
    cn_strview name_stack[32];
    return cn_type_may_carry_zone_value_inner(ctx, type, module_stack, name_stack, 0);
}

static size_t cn_expr_zone_depth(
    cn_sema_ctx *ctx,
    const cn_scope *scope,
    const cn_expr *expression,
    const cn_type_ref *value_type
) {
    if (expression == NULL || value_type == NULL) {
        return 0;
    }

    if (!cn_type_may_carry_zone_value(ctx, value_type)) {
        return 0;
    }

    switch (expression->kind) {
    case CN_EXPR_ZALLOC:
        return scope != NULL ? scope->zone_depth : 0;
    case CN_EXPR_NAME: {
        const cn_binding *binding = cn_scope_lookup(scope, expression->data.name);
        return binding != NULL ? binding->value_zone_depth : 0;
    }
    case CN_EXPR_IF: {
        size_t then_depth = cn_expr_zone_depth(ctx, scope, expression->data.if_expr.then_expr, value_type);
        size_t else_depth = cn_expr_zone_depth(ctx, scope, expression->data.if_expr.else_expr, value_type);
        return then_depth > else_depth ? then_depth : else_depth;
    }
    case CN_EXPR_OK:
        if (value_type->kind == CN_TYPE_RESULT) {
            return cn_expr_zone_depth(ctx, scope, expression->data.ok_expr.value, value_type->inner);
        }
        return 0;
    case CN_EXPR_FIELD:
        return cn_expr_zone_depth(ctx, scope, expression->data.field.base, value_type);
    case CN_EXPR_INDEX:
        return cn_expr_zone_depth(ctx, scope, expression->data.index.base, value_type);
    case CN_EXPR_SLICE_VIEW:
        return cn_expr_zone_depth(ctx, scope, expression->data.slice_view.base, value_type);
    case CN_EXPR_STRUCT_LITERAL:
        if (value_type->kind == CN_TYPE_NAMED) {
            cn_resolved_struct resolved = cn_resolve_canonical_named_struct(ctx, value_type->module_name, value_type->name);
            if (resolved.decl != NULL) {
                size_t max_depth = 0;
                for (size_t i = 0; i < expression->data.struct_literal.fields.count; ++i) {
                    const cn_field_init *field_init = &expression->data.struct_literal.fields.items[i];
                    const cn_struct_field *field = cn_find_struct_field(resolved.decl, field_init->name, NULL);
                    if (field == NULL) {
                        continue;
                    }

                    size_t depth = cn_expr_zone_depth(ctx, scope, field_init->value, field->type);
                    if (depth > max_depth) {
                        max_depth = depth;
                    }
                }
                return max_depth;
            }
        }
        return 0;
    case CN_EXPR_ARRAY_LITERAL: {
        if (value_type->kind != CN_TYPE_ARRAY && value_type->kind != CN_TYPE_SLICE) {
            return 0;
        }

        size_t max_depth = 0;
        for (size_t i = 0; i < expression->data.array_literal.items.count; ++i) {
            size_t depth = cn_expr_zone_depth(ctx, scope, expression->data.array_literal.items.items[i], value_type->inner);
            if (depth > max_depth) {
                max_depth = depth;
            }
        }
        return max_depth;
    }
    default:
        return 0;
    }
}

static size_t cn_assignment_storage_zone_depth(cn_sema_ctx *ctx, cn_scope *scope, const cn_expr *target) {
    if (target == NULL) {
        return 0;
    }

    switch (target->kind) {
    case CN_EXPR_NAME: {
        const cn_binding *binding = cn_scope_lookup(scope, target->data.name);
        return binding != NULL ? binding->scope_zone_depth : 0;
    }
    case CN_EXPR_FIELD:
        if (target->data.field.base->kind == CN_EXPR_NAME) {
            cn_resolved_const resolved_const = cn_resolve_source_named_const(
                ctx,
                target->data.field.base->data.name,
                target->data.field.field_name
            );
            if (resolved_const.decl != NULL) {
                return 0;
            }
        }
        return cn_assignment_storage_zone_depth(ctx, scope, target->data.field.base);
    case CN_EXPR_INDEX:
        return cn_assignment_storage_zone_depth(ctx, scope, target->data.index.base);
    case CN_EXPR_DEREF:
        return cn_assignment_storage_zone_depth(ctx, scope, target->data.deref_expr.target);
    default:
        return 0;
    }
}

static const cn_type_ref *cn_check_expression_hint(
    cn_sema_ctx *ctx,
    cn_scope *scope,
    const cn_expr *expression,
    const cn_type_ref *expected
);

static const cn_type_ref *cn_check_expression_against_peer(
    cn_sema_ctx *ctx,
    cn_scope *scope,
    const cn_expr *expression,
    const cn_type_ref *peer_type
);

static const cn_type_ref *cn_check_expression_in_proof_scope(
    cn_sema_ctx *ctx,
    cn_scope *scope,
    const cn_expr *expression,
    const cn_type_ref *expected,
    const cn_expr *condition,
    bool branch_truth
) {
    if (condition == NULL) {
        return cn_check_expression_hint(ctx, scope, expression, expected);
    }

    cn_scope guard_scope = {0};
    guard_scope.parent = scope;
    guard_scope.zone_depth = scope != NULL ? scope->zone_depth : 0;
    cn_mark_result_ok_proofs_for_branch(ctx, &guard_scope, scope, condition, branch_truth);
    const cn_type_ref *type = cn_check_expression_hint(ctx, &guard_scope, expression, expected);
    cn_scope_release(ctx, &guard_scope);
    return type;
}

static const cn_type_ref *cn_check_expression_against_peer_in_proof_scope(
    cn_sema_ctx *ctx,
    cn_scope *scope,
    const cn_expr *expression,
    const cn_type_ref *peer_type,
    const cn_expr *condition,
    bool branch_truth
) {
    if (condition == NULL) {
        return cn_check_expression_against_peer(ctx, scope, expression, peer_type);
    }

    cn_scope guard_scope = {0};
    guard_scope.parent = scope;
    guard_scope.zone_depth = scope != NULL ? scope->zone_depth : 0;
    cn_mark_result_ok_proofs_for_branch(ctx, &guard_scope, scope, condition, branch_truth);
    const cn_type_ref *type = cn_check_expression_against_peer(ctx, &guard_scope, expression, peer_type);
    cn_scope_release(ctx, &guard_scope);
    return type;
}

static bool cn_int_literal_fits_u8(const cn_expr *expression) {
    return expression->kind == CN_EXPR_INT &&
           expression->data.int_value >= 0 &&
           expression->data.int_value <= 255;
}

static const cn_type_ref *cn_check_expression_against_peer(
    cn_sema_ctx *ctx,
    cn_scope *scope,
    const cn_expr *expression,
    const cn_type_ref *peer_type
) {
    if (peer_type != NULL && peer_type->kind == CN_TYPE_U8 && expression->kind == CN_EXPR_INT) {
        return cn_check_expression_hint(ctx, scope, expression, peer_type);
    }

    if (peer_type != NULL && peer_type->kind == CN_TYPE_PTR && expression->kind == CN_EXPR_NULL) {
        return cn_check_expression_hint(ctx, scope, expression, peer_type);
    }

    return cn_check_expression_hint(ctx, scope, expression, NULL);
}

static bool cn_type_matches_equality_operands(const cn_type_ref *left, const cn_type_ref *right) {
    if (cn_type_equal(left, right)) {
        return true;
    }

    return (left->kind == CN_TYPE_PTR && right->kind == CN_TYPE_NULL) ||
           (left->kind == CN_TYPE_NULL && right->kind == CN_TYPE_PTR);
}

static bool cn_validate_type_ref(cn_sema_ctx *ctx, const cn_type_ref *type, size_t offset) {
    if (type == NULL) {
        return false;
    }

    switch (type->kind) {
    case CN_TYPE_INT:
    case CN_TYPE_U8:
    case CN_TYPE_BOOL:
    case CN_TYPE_STR:
    case CN_TYPE_VOID:
    case CN_TYPE_NULL:
    case CN_TYPE_UNKNOWN:
        return true;
    case CN_TYPE_RESULT:
    case CN_TYPE_PTR:
    case CN_TYPE_SLICE:
        return cn_validate_type_ref(ctx, type->inner, offset);
    case CN_TYPE_ARRAY: {
        bool inner_valid = cn_validate_type_ref(ctx, type->inner, offset);
        if (type->array_size_expr == NULL) {
            return inner_valid;
        }

        size_t resolved_size = 0;
        bool size_valid = cn_resolve_non_negative_const_size(
            ctx,
            type->array_size_expr,
            type->array_size_expr->offset,
            "E3039",
            "array size must be a non-negative integer constant",
            &resolved_size
        );
        ((cn_type_ref *)type)->array_size = resolved_size;
        return inner_valid && size_valid;
    }
    case CN_TYPE_NAMED: {
        cn_resolved_struct resolved = cn_resolve_source_named_struct(ctx, type->module_name, type->name);
        if (resolved.decl == NULL) {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3012",
                offset,
                "unknown type '%.*s%.*s%.*s'",
                (int)type->module_name.length,
                type->module_name.data != NULL ? type->module_name.data : "",
                (int)(type->module_name.length > 0),
                type->module_name.length > 0 ? "." : "",
                (int)type->name.length,
                type->name.data
            );
            return false;
        }

        ((cn_type_ref *)type)->module_name = cn_sv_from_cstr(resolved.module->name);
        return true;
    }
    }

    return false;
}

static bool cn_type_is_exportable(cn_sema_ctx *ctx, const cn_type_ref *type) {
    if (type == NULL) {
        return false;
    }

    switch (type->kind) {
    case CN_TYPE_INT:
    case CN_TYPE_U8:
    case CN_TYPE_BOOL:
    case CN_TYPE_STR:
    case CN_TYPE_VOID:
    case CN_TYPE_NULL:
        return true;
    case CN_TYPE_RESULT:
    case CN_TYPE_PTR:
    case CN_TYPE_SLICE:
    case CN_TYPE_ARRAY:
        return cn_type_is_exportable(ctx, type->inner);
    case CN_TYPE_NAMED: {
        cn_resolved_struct resolved = cn_resolve_canonical_named_struct(ctx, type->module_name, type->name);
        if (resolved.decl == NULL) {
            return false;
        }
        if (resolved.module != ctx->module) {
            return resolved.decl->is_public;
        }
        return resolved.decl->is_public;
    }
    case CN_TYPE_UNKNOWN:
        return false;
    }

    return false;
}

static void cn_emit_private_type_in_public_api(
    cn_sema_ctx *ctx,
    size_t offset,
    const char *subject_kind,
    cn_strview subject_name,
    const cn_type_ref *type
) {
    char type_name[128];
    cn_type_describe(type, type_name, sizeof(type_name));
    cn_diag_emit(
        ctx->diagnostics,
        CN_DIAG_ERROR,
        "E3023",
        offset,
        "public %s '%.*s' cannot expose private type '%s'",
        subject_kind,
        (int)subject_name.length,
        subject_name.data,
        type_name
    );
}

static void cn_emit_top_level_name_conflict(
    cn_sema_ctx *ctx,
    size_t offset,
    const char *name_kind,
    cn_strview name,
    const char *other_kind
) {
    cn_diag_emit(
        ctx->diagnostics,
        CN_DIAG_ERROR,
        "E3027",
        offset,
        "%s name '%.*s' conflicts with existing %s",
        name_kind,
        (int)name.length,
        name.data,
        other_kind
    );
}

static bool cn_extract_bool_literal(const cn_expr *expression, bool *out_value) {
    if (expression->kind != CN_EXPR_BOOL) {
        return false;
    }

    *out_value = expression->data.bool_value;
    return true;
}

static bool cn_extract_result_ok_alias_inner(
    const cn_scope *scope,
    const cn_expr *expression,
    const cn_binding **out_binding,
    bool *out_positive,
    size_t depth
) {
    if (expression == NULL || depth >= 32) {
        return false;
    }

    const cn_expr *guard = expression;
    bool is_positive = true;

    if (guard->kind == CN_EXPR_UNARY && guard->data.unary.op == CN_UNARY_NOT) {
        bool operand_positive = true;
        if (!cn_extract_result_ok_alias_inner(scope, guard->data.unary.operand, out_binding, &operand_positive, depth + 1)) {
            return false;
        }
        *out_positive = !operand_positive;
        return true;
    }

    if (guard->kind == CN_EXPR_BINARY &&
        (guard->data.binary.op == CN_BINARY_EQUAL || guard->data.binary.op == CN_BINARY_NOT_EQUAL)) {
        bool literal_value = false;
        if (cn_extract_result_ok_alias_inner(scope, guard->data.binary.left, out_binding, &is_positive, depth + 1) &&
            cn_extract_bool_literal(guard->data.binary.right, &literal_value)) {
            if (!literal_value) {
                is_positive = !is_positive;
            }
            if (guard->data.binary.op == CN_BINARY_NOT_EQUAL) {
                is_positive = !is_positive;
            }
            *out_positive = is_positive;
            return true;
        }

        if (cn_extract_result_ok_alias_inner(scope, guard->data.binary.right, out_binding, &is_positive, depth + 1) &&
            cn_extract_bool_literal(guard->data.binary.left, &literal_value)) {
            if (!literal_value) {
                is_positive = !is_positive;
            }
            if (guard->data.binary.op == CN_BINARY_NOT_EQUAL) {
                is_positive = !is_positive;
            }
            *out_positive = is_positive;
            return true;
        }
    }

    if (guard->kind == CN_EXPR_NAME) {
        const cn_binding *binding = cn_scope_lookup(scope, guard->data.name);
        if (binding == NULL || !binding->has_result_guard_alias || binding->result_guard_binding == NULL) {
            return false;
        }

        *out_binding = binding->result_guard_binding;
        *out_positive = binding->result_guard_positive;
        return true;
    }

    if (guard->kind != CN_EXPR_FIELD || !cn_sv_eq_cstr(guard->data.field.field_name, "ok")) {
        return false;
    }

    if (guard->data.field.base->kind != CN_EXPR_NAME) {
        return false;
    }

    const cn_binding *binding = cn_scope_lookup(scope, guard->data.field.base->data.name);
    if (binding == NULL || binding->type == NULL || binding->type->kind != CN_TYPE_RESULT) {
        return false;
    }

    *out_binding = binding;
    *out_positive = is_positive;
    return true;
}

static bool cn_extract_result_ok_alias(
    const cn_scope *scope,
    const cn_expr *expression,
    const cn_binding **out_binding,
    bool *out_positive
) {
    return cn_extract_result_ok_alias_inner(scope, expression, out_binding, out_positive, 0);
}

static void cn_mark_result_ok_proofs_for_branch(
    cn_sema_ctx *ctx,
    cn_scope *target_scope,
    const cn_scope *analysis_scope,
    const cn_expr *expression,
    bool branch_truth
) {
    if (target_scope == NULL || analysis_scope == NULL || expression == NULL) {
        return;
    }

    if (expression->kind == CN_EXPR_BINARY) {
        if (branch_truth && expression->data.binary.op == CN_BINARY_AND) {
            cn_mark_result_ok_proofs_for_branch(
                ctx,
                target_scope,
                analysis_scope,
                expression->data.binary.left,
                true
            );
            cn_mark_result_ok_proofs_for_branch(
                ctx,
                target_scope,
                analysis_scope,
                expression->data.binary.right,
                true
            );
            return;
        }

        if (!branch_truth && expression->data.binary.op == CN_BINARY_OR) {
            cn_mark_result_ok_proofs_for_branch(
                ctx,
                target_scope,
                analysis_scope,
                expression->data.binary.left,
                false
            );
            cn_mark_result_ok_proofs_for_branch(
                ctx,
                target_scope,
                analysis_scope,
                expression->data.binary.right,
                false
            );
            return;
        }
    }

    const cn_binding *binding = NULL;
    bool positive = false;
    if (!cn_extract_result_ok_alias(analysis_scope, expression, &binding, &positive)) {
        return;
    }

    if ((branch_truth && positive) || (!branch_truth && !positive)) {
        cn_scope_mark_result_ok(ctx, target_scope, binding);
    }
}

static bool cn_check_const_decl(cn_sema_ctx *ctx, const cn_module *module, const cn_const_decl *const_decl);

static bool cn_eval_const_int_expr_in_module(
    cn_sema_ctx *ctx,
    const cn_module *module,
    const cn_expr *expression,
    int64_t *out_value
);

static bool cn_eval_const_int_expr(cn_sema_ctx *ctx, const cn_expr *expression, int64_t *out_value) {
    return cn_eval_const_int_expr_in_module(ctx, ctx->module, expression, out_value);
}

static bool cn_resolve_non_negative_const_size(
    cn_sema_ctx *ctx,
    const cn_expr *expression,
    size_t offset,
    const char *code,
    const char *message,
    size_t *out_size
) {
    const cn_type_ref *size_type = cn_check_expression_hint(ctx, NULL, expression, cn_builtin_type(CN_TYPE_INT));
    if (!cn_type_is_integer_like(size_type)) {
        cn_emit_type_mismatch(ctx->diagnostics, expression->offset, message, cn_builtin_type(CN_TYPE_INT), size_type);
        return false;
    }

    int64_t value = 0;
    if (!cn_eval_const_int_expr(ctx, expression, &value) || value < 0) {
        cn_diag_emit(ctx->diagnostics, CN_DIAG_ERROR, code, offset, "%s", message);
        return false;
    }

    *out_size = (size_t)value;
    return true;
}

static bool cn_eval_const_int_expr_in_module(
    cn_sema_ctx *ctx,
    const cn_module *module,
    const cn_expr *expression,
    int64_t *out_value
) {
    if (expression == NULL) {
        return false;
    }

    const cn_module *previous_module = ctx->module;
    ctx->module = module;

    bool ok = true;
    int64_t left = 0;
    int64_t right = 0;

    switch (expression->kind) {
    case CN_EXPR_INT:
        *out_value = expression->data.int_value;
        break;
    case CN_EXPR_NAME: {
        cn_resolved_const resolved = cn_resolve_const_in_module(ctx, ctx->module, expression->data.name);
        if (resolved.decl == NULL || !cn_check_const_decl(ctx, resolved.module, resolved.decl) ||
            !cn_type_is_integer_like(resolved.decl->type)) {
            ok = false;
            break;
        }

        ok = cn_eval_const_int_expr_in_module(ctx, resolved.module, resolved.decl->initializer, out_value);
        break;
    }
    case CN_EXPR_FIELD:
        if (expression->data.field.base->kind != CN_EXPR_NAME) {
            ok = false;
            break;
        } else {
            cn_resolved_const resolved = cn_resolve_source_named_const(
                ctx,
                expression->data.field.base->data.name,
                expression->data.field.field_name
            );
            if (resolved.decl == NULL || !cn_check_const_decl(ctx, resolved.module, resolved.decl) ||
                !cn_type_is_integer_like(resolved.decl->type)) {
                ok = false;
                break;
            }

            ok = cn_eval_const_int_expr_in_module(ctx, resolved.module, resolved.decl->initializer, out_value);
            break;
        }
    case CN_EXPR_UNARY:
        if (expression->data.unary.op != CN_UNARY_NEGATE) {
            ok = false;
            break;
        }
        ok = cn_eval_const_int_expr_in_module(ctx, ctx->module, expression->data.unary.operand, out_value);
        if (ok) {
            *out_value = -*out_value;
        }
        break;
    case CN_EXPR_BINARY:
        ok = cn_eval_const_int_expr_in_module(ctx, ctx->module, expression->data.binary.left, &left) &&
             cn_eval_const_int_expr_in_module(ctx, ctx->module, expression->data.binary.right, &right);
        if (!ok) {
            break;
        }

        switch (expression->data.binary.op) {
        case CN_BINARY_ADD: *out_value = left + right; break;
        case CN_BINARY_SUB: *out_value = left - right; break;
        case CN_BINARY_MUL: *out_value = left * right; break;
        case CN_BINARY_DIV:
            if (right == 0) {
                ok = false;
                break;
            }
            *out_value = left / right;
            break;
        case CN_BINARY_MOD:
            if (right == 0) {
                ok = false;
                break;
            }
            *out_value = left % right;
            break;
        default:
            ok = false;
            break;
        }
        break;
    default:
        ok = false;
        break;
    }

    ctx->module = previous_module;
    return ok;
}

static bool cn_check_const_expr(cn_sema_ctx *ctx, const cn_expr *expression) {
    switch (expression->kind) {
    case CN_EXPR_INT:
    case CN_EXPR_BOOL:
    case CN_EXPR_STRING:
    case CN_EXPR_NULL:
    case CN_EXPR_ERR:
        return true;
    case CN_EXPR_NAME: {
        cn_resolved_const resolved = cn_resolve_const_in_module(ctx, ctx->module, expression->data.name);
        if (resolved.decl == NULL) {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3025",
                expression->offset,
                "module-level constant initializers can only use other constants, got '%.*s'",
                (int)expression->data.name.length,
                expression->data.name.data
            );
            return false;
        }
        return cn_check_const_decl(ctx, resolved.module, resolved.decl);
    }
    case CN_EXPR_UNARY:
        return cn_check_const_expr(ctx, expression->data.unary.operand);
    case CN_EXPR_BINARY:
        return cn_check_const_expr(ctx, expression->data.binary.left) &&
               cn_check_const_expr(ctx, expression->data.binary.right);
    case CN_EXPR_IF:
        return cn_check_const_expr(ctx, expression->data.if_expr.condition) &&
               cn_check_const_expr(ctx, expression->data.if_expr.then_expr) &&
               cn_check_const_expr(ctx, expression->data.if_expr.else_expr);
    case CN_EXPR_ARRAY_LITERAL:
        for (size_t i = 0; i < expression->data.array_literal.items.count; ++i) {
            if (!cn_check_const_expr(ctx, expression->data.array_literal.items.items[i])) {
                return false;
            }
        }
        if (expression->data.array_literal.repeat_count_expr != NULL &&
            !cn_check_const_expr(ctx, expression->data.array_literal.repeat_count_expr)) {
            return false;
        }
        return true;
    case CN_EXPR_INDEX:
        return cn_check_const_expr(ctx, expression->data.index.base) &&
               cn_check_const_expr(ctx, expression->data.index.index);
    case CN_EXPR_SLICE_VIEW: {
        if (!cn_check_const_expr(ctx, expression->data.slice_view.base)) {
            return false;
        }
        if (expression->data.slice_view.start != NULL &&
            !cn_check_const_expr(ctx, expression->data.slice_view.start)) {
            return false;
        }
        if (expression->data.slice_view.end != NULL &&
            !cn_check_const_expr(ctx, expression->data.slice_view.end)) {
            return false;
        }
        return true;
    }
    case CN_EXPR_FIELD:
        if (expression->data.field.base->kind == CN_EXPR_NAME) {
            cn_resolved_const resolved = cn_resolve_source_named_const(
                ctx,
                expression->data.field.base->data.name,
                expression->data.field.field_name
            );
            if (resolved.decl != NULL) {
                return cn_check_const_decl(ctx, resolved.module, resolved.decl);
            }
        }
        return cn_check_const_expr(ctx, expression->data.field.base);
    case CN_EXPR_STRUCT_LITERAL:
        for (size_t i = 0; i < expression->data.struct_literal.fields.count; ++i) {
            if (!cn_check_const_expr(ctx, expression->data.struct_literal.fields.items[i].value)) {
                return false;
            }
        }
        return true;
    case CN_EXPR_OK:
        return cn_check_const_expr(ctx, expression->data.ok_expr.value);
    case CN_EXPR_CALL:
        cn_diag_emit(
            ctx->diagnostics,
            CN_DIAG_ERROR,
            "E3025",
            expression->offset,
            "module-level constant initializers cannot call functions or builtins"
        );
        return false;
    case CN_EXPR_ALLOC:
    case CN_EXPR_ZALLOC:
    case CN_EXPR_ADDR:
    case CN_EXPR_DEREF:
        cn_diag_emit(
            ctx->diagnostics,
            CN_DIAG_ERROR,
            "E3025",
            expression->offset,
            "module-level constant initializers cannot use runtime memory operations"
        );
        return false;
    }

    return false;
}

static const cn_type_ref *cn_check_expression_hint(cn_sema_ctx *ctx, cn_scope *scope, const cn_expr *expression, const cn_type_ref *expected);
static bool cn_check_block(cn_sema_ctx *ctx, cn_scope *parent, const cn_block *block, bool creates_scope);
static bool cn_check_block_with_guard(
    cn_sema_ctx *ctx,
    cn_scope *parent,
    const cn_block *block,
    bool creates_scope,
    const cn_expr *guard_condition,
    bool guard_branch_truth
);

static const cn_type_ref *cn_check_array_literal(cn_sema_ctx *ctx, cn_scope *scope, const cn_expr *expression, const cn_type_ref *expected) {
    const cn_type_ref *element_expected = NULL;
    const cn_type_ref *result_type = NULL;
    size_t item_count = expression->data.array_literal.items.count;

    if (expression->data.array_literal.repeat_count_expr != NULL) {
        if (!cn_resolve_non_negative_const_size(
                ctx,
                expression->data.array_literal.repeat_count_expr,
                expression->data.array_literal.repeat_count_expr->offset,
                "E3040",
                "array repeat count must be a non-negative integer constant",
                &item_count
            )) {
            item_count = 0;
        }
        ((cn_expr *)expression)->data.array_literal.repeat_count = item_count;
    }

    if (expected != NULL && expected->kind == CN_TYPE_ARRAY) {
        result_type = expected;
        element_expected = expected->inner;
        if (item_count != expected->array_size) {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3011",
                expression->offset,
                "array literal size mismatch: expected %zu items, got %zu",
                expected->array_size,
                item_count
            );
        }
    }

    const cn_type_ref *element_type = element_expected;
    for (size_t i = 0; i < expression->data.array_literal.items.count; ++i) {
        const cn_expr *item = expression->data.array_literal.items.items[i];
        const cn_type_ref *item_type = cn_check_expression_hint(ctx, scope, item, element_expected);
        if (element_type == NULL && item_type->kind != CN_TYPE_UNKNOWN) {
            element_type = item_type;
        } else if (element_type != NULL && item_type->kind != CN_TYPE_UNKNOWN && !cn_type_equal(element_type, item_type)) {
            cn_emit_type_mismatch(ctx->diagnostics, item->offset, "array element type mismatch", element_type, item_type);
        }
    }

    if (result_type != NULL) {
        return result_type;
    }

    if (element_type == NULL) {
        element_type = cn_builtin_type(CN_TYPE_UNKNOWN);
    }

    return cn_make_temp_type(
        ctx,
        CN_TYPE_ARRAY,
        cn_sv_from_parts(NULL, 0),
        cn_sv_from_parts(NULL, 0),
        element_type,
        item_count,
        expression->offset
    );
}

static const cn_type_ref *cn_check_struct_literal(cn_sema_ctx *ctx, cn_scope *scope, const cn_expr *expression) {
    cn_resolved_struct resolved = cn_resolve_source_named_struct(
        ctx,
        expression->data.struct_literal.module_name,
        expression->data.struct_literal.type_name
    );
    const cn_struct_decl *struct_decl = resolved.decl;
    if (struct_decl == NULL) {
        cn_diag_emit(
            ctx->diagnostics,
            CN_DIAG_ERROR,
            "E3012",
            expression->offset,
            "unknown struct '%.*s%.*s%.*s'",
            (int)expression->data.struct_literal.module_name.length,
            expression->data.struct_literal.module_name.data != NULL ? expression->data.struct_literal.module_name.data : "",
            (int)(expression->data.struct_literal.module_name.length > 0),
            expression->data.struct_literal.module_name.length > 0 ? "." : "",
            (int)expression->data.struct_literal.type_name.length,
            expression->data.struct_literal.type_name.data
        );
        return cn_builtin_type(CN_TYPE_UNKNOWN);
    }

    bool *seen = CN_CALLOC(ctx->allocator, bool, struct_decl->fields.count);
    for (size_t i = 0; i < expression->data.struct_literal.fields.count; ++i) {
        const cn_field_init *field_init = &expression->data.struct_literal.fields.items[i];
        size_t field_index = 0;
        const cn_struct_field *field = cn_find_struct_field(struct_decl, field_init->name, &field_index);
        if (field == NULL) {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3009",
                field_init->offset,
                "unknown field '%.*s' on struct '%.*s'",
                (int)field_init->name.length,
                field_init->name.data,
                (int)struct_decl->name.length,
                struct_decl->name.data
            );
            continue;
        }

        if (seen[field_index]) {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3003",
                field_init->offset,
                "duplicate field initializer '%.*s'",
                (int)field_init->name.length,
                field_init->name.data
            );
            continue;
        }

        seen[field_index] = true;
        const cn_type_ref *value_type = cn_check_expression_hint(ctx, scope, field_init->value, field->type);
        if (!cn_type_can_assign_to(field->type, value_type)) {
            cn_emit_type_mismatch(ctx->diagnostics, field_init->offset, "struct field type mismatch", field->type, value_type);
        }
    }

    for (size_t i = 0; i < struct_decl->fields.count; ++i) {
        if (!seen[i]) {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3004",
                expression->offset,
                "missing field '%.*s' in struct literal '%.*s'",
                (int)struct_decl->fields.items[i].name.length,
                struct_decl->fields.items[i].name.data,
                (int)struct_decl->name.length,
                struct_decl->name.data
            );
        }
    }

    CN_FREE(ctx->allocator, seen);
    return cn_make_temp_type(
        ctx,
        CN_TYPE_NAMED,
        cn_sv_from_cstr(resolved.module->name),
        struct_decl->name,
        NULL,
        0,
        expression->offset
    );
}

static const cn_type_ref *cn_check_field_access(cn_sema_ctx *ctx, cn_scope *scope, const cn_expr *expression) {
    if (expression->data.field.base->kind == CN_EXPR_NAME) {
        cn_resolved_const resolved_const = cn_resolve_source_named_const(
            ctx,
            expression->data.field.base->data.name,
            expression->data.field.field_name
        );
        if (resolved_const.decl != NULL) {
            return resolved_const.decl->type;
        }

        if (cn_find_imported_module(ctx, ctx->module, expression->data.field.base->data.name) != NULL) {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3014",
                expression->offset,
                "module member access is only valid for exported constants or calls like module.function(...)"
            );
            return cn_builtin_type(CN_TYPE_UNKNOWN);
        }
    }

    const cn_type_ref *base_type = cn_check_expression_hint(ctx, scope, expression->data.field.base, NULL);

    if (base_type->kind == CN_TYPE_PTR) {
        if (cn_sv_eq_cstr(expression->data.field.field_name, "value")) {
            return base_type->inner;
        }

        cn_diag_emit(ctx->diagnostics, CN_DIAG_ERROR, "E3009", expression->offset, "pointer values only expose '.value'; use 'deref value' for explicit dereference");
        return cn_builtin_type(CN_TYPE_UNKNOWN);
    }

    if (base_type->kind == CN_TYPE_RESULT) {
        if (cn_sv_eq_cstr(expression->data.field.field_name, "ok")) {
            return cn_builtin_type(CN_TYPE_BOOL);
        }
        if (cn_sv_eq_cstr(expression->data.field.field_name, "value")) {
            const cn_binding *binding = NULL;
            if (expression->data.field.base->kind == CN_EXPR_NAME) {
                binding = cn_scope_lookup(scope, expression->data.field.base->data.name);
            }
            if (binding == NULL || !cn_scope_result_is_ok(scope, binding)) {
                cn_diag_emit(
                    ctx->diagnostics,
                    CN_DIAG_ERROR,
                    "E3024",
                    expression->offset,
                    "result '.value' requires a proven-ok named result in this scope"
                );
            }
            return base_type->inner;
        }

        cn_diag_emit(ctx->diagnostics, CN_DIAG_ERROR, "E3009", expression->offset, "result values only expose '.ok' and '.value'");
        return cn_builtin_type(CN_TYPE_UNKNOWN);
    }

    if (base_type->kind == CN_TYPE_SLICE) {
        if (cn_sv_eq_cstr(expression->data.field.field_name, "length")) {
            return cn_builtin_type(CN_TYPE_INT);
        }

        cn_diag_emit(ctx->diagnostics, CN_DIAG_ERROR, "E3009", expression->offset, "slice values only expose '.length'");
        return cn_builtin_type(CN_TYPE_UNKNOWN);
    }

    if (base_type->kind != CN_TYPE_NAMED) {
        cn_diag_emit(ctx->diagnostics, CN_DIAG_ERROR, "E3009", expression->offset, "field access requires a struct value");
        return cn_builtin_type(CN_TYPE_UNKNOWN);
    }

    cn_resolved_struct resolved = cn_resolve_canonical_named_struct(ctx, base_type->module_name, base_type->name);
    const cn_struct_decl *struct_decl = resolved.decl;
    if (struct_decl == NULL) {
        cn_diag_emit(
            ctx->diagnostics,
            CN_DIAG_ERROR,
            "E3009",
            expression->offset,
            "type '%.*s' does not support field access",
            (int)base_type->name.length,
            base_type->name.data
        );
        return cn_builtin_type(CN_TYPE_UNKNOWN);
    }

    const cn_struct_field *field = cn_find_struct_field(struct_decl, expression->data.field.field_name, NULL);
    if (field == NULL) {
        cn_diag_emit(
            ctx->diagnostics,
            CN_DIAG_ERROR,
            "E3009",
            expression->offset,
            "unknown field '%.*s' on struct '%.*s'",
            (int)expression->data.field.field_name.length,
            expression->data.field.field_name.data,
            (int)struct_decl->name.length,
            struct_decl->name.data
        );
        return cn_builtin_type(CN_TYPE_UNKNOWN);
    }

    return field->type;
}

static const cn_type_ref *cn_check_index(cn_sema_ctx *ctx, cn_scope *scope, const cn_expr *expression) {
    const cn_type_ref *base_type = cn_check_expression_hint(ctx, scope, expression->data.index.base, NULL);
    const cn_type_ref *index_type = cn_check_expression_hint(ctx, scope, expression->data.index.index, cn_builtin_type(CN_TYPE_INT));

    if (!cn_type_equal(index_type, cn_builtin_type(CN_TYPE_INT))) {
        cn_emit_type_mismatch(ctx->diagnostics, expression->data.index.index->offset, "array index must be int", cn_builtin_type(CN_TYPE_INT), index_type);
    }

    if (base_type->kind != CN_TYPE_ARRAY && base_type->kind != CN_TYPE_SLICE) {
        cn_diag_emit(ctx->diagnostics, CN_DIAG_ERROR, "E3010", expression->offset, "indexing requires an array or slice value");
        return cn_builtin_type(CN_TYPE_UNKNOWN);
    }

    return base_type->inner;
}

static const cn_type_ref *cn_check_slice_view(cn_sema_ctx *ctx, cn_scope *scope, const cn_expr *expression) {
    const cn_type_ref *base_type = cn_check_expression_hint(ctx, scope, expression->data.slice_view.base, NULL);
    const cn_type_ref *start_type = cn_builtin_type(CN_TYPE_INT);
    const cn_type_ref *end_type = cn_builtin_type(CN_TYPE_INT);

    if (expression->data.slice_view.start != NULL) {
        start_type = cn_check_expression_hint(ctx, scope, expression->data.slice_view.start, cn_builtin_type(CN_TYPE_INT));
        if (!cn_type_equal(start_type, cn_builtin_type(CN_TYPE_INT))) {
            cn_emit_type_mismatch(
                ctx->diagnostics,
                expression->data.slice_view.start->offset,
                "slice start must be int",
                cn_builtin_type(CN_TYPE_INT),
                start_type
            );
        }
    }

    if (expression->data.slice_view.end != NULL) {
        end_type = cn_check_expression_hint(ctx, scope, expression->data.slice_view.end, cn_builtin_type(CN_TYPE_INT));
        if (!cn_type_equal(end_type, cn_builtin_type(CN_TYPE_INT))) {
            cn_emit_type_mismatch(
                ctx->diagnostics,
                expression->data.slice_view.end->offset,
                "slice end must be int",
                cn_builtin_type(CN_TYPE_INT),
                end_type
            );
        }
    }

    if (base_type->kind != CN_TYPE_ARRAY && base_type->kind != CN_TYPE_SLICE) {
        cn_diag_emit(ctx->diagnostics, CN_DIAG_ERROR, "E3010", expression->offset, "slicing requires an array or slice value");
        return cn_builtin_type(CN_TYPE_UNKNOWN);
    }

    return cn_make_temp_type(
        ctx,
        CN_TYPE_SLICE,
        cn_sv_from_parts(NULL, 0),
        cn_sv_from_parts(NULL, 0),
        (cn_type_ref *)base_type->inner,
        0,
        expression->offset
    );
}

static void cn_check_call_arguments(
    cn_sema_ctx *ctx,
    cn_scope *scope,
    const cn_expr *expression,
    const cn_function *function
) {
    if (function->parameters.count != expression->data.call.arguments.count) {
        cn_diag_emit(
            ctx->diagnostics,
            CN_DIAG_ERROR,
            "E3008",
            expression->offset,
            "function '%.*s' expects %zu arguments, got %zu",
            (int)function->name.length,
            function->name.data,
            function->parameters.count,
            expression->data.call.arguments.count
        );
    }

    size_t arg_count = expression->data.call.arguments.count;
    if (function->parameters.count < arg_count) {
        arg_count = function->parameters.count;
    }

    for (size_t i = 0; i < arg_count; ++i) {
        const cn_type_ref *actual = cn_check_expression_hint(ctx, scope, expression->data.call.arguments.items[i], function->parameters.items[i].type);
        if (!cn_type_can_assign_to(function->parameters.items[i].type, actual)) {
            cn_emit_type_mismatch(ctx->diagnostics, expression->data.call.arguments.items[i]->offset, "argument type mismatch", function->parameters.items[i].type, actual);
        }

        size_t zone_depth = cn_expr_zone_depth(ctx, scope, expression->data.call.arguments.items[i], function->parameters.items[i].type);
        if (zone_depth > 0 && cn_type_may_carry_zone_value(ctx, function->parameters.items[i].type)) {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3045",
                expression->data.call.arguments.items[i]->offset,
                "zone-owned value cannot cross a function call boundary yet; copy data out before calling"
            );
        }
    }
}

static const cn_type_ref *cn_check_call(cn_sema_ctx *ctx, cn_scope *scope, const cn_expr *expression) {
    if (expression->data.call.callee->kind == CN_EXPR_NAME) {
        cn_strview callee = expression->data.call.callee->data.name;

        if (cn_sv_eq_cstr(callee, "print") || cn_sv_eq_cstr(callee, "println")) {
            if (expression->data.call.arguments.count != 1) {
                cn_diag_emit(
                    ctx->diagnostics,
                    CN_DIAG_ERROR,
                    "E3008",
                    expression->offset,
                    "%s expects exactly 1 argument",
                    cn_sv_eq_cstr(callee, "print") ? "print" : "println"
                );
            } else {
                cn_check_expression_hint(ctx, scope, expression->data.call.arguments.items[0], NULL);
            }
            return cn_builtin_type(CN_TYPE_VOID);
        }

        if (cn_sv_eq_cstr(callee, "input")) {
            if (expression->data.call.arguments.count != 0) {
                cn_diag_emit(ctx->diagnostics, CN_DIAG_ERROR, "E3008", expression->offset, "input expects 0 arguments");
            }
            return cn_builtin_type(CN_TYPE_STR);
        }

        if (cn_sv_eq_cstr(callee, "str_copy")) {
            if (expression->data.call.arguments.count != 1) {
                cn_diag_emit(ctx->diagnostics, CN_DIAG_ERROR, "E3008", expression->offset, "str_copy expects exactly 1 argument");
            } else {
                const cn_type_ref *value_type = cn_check_expression_hint(ctx, scope, expression->data.call.arguments.items[0], cn_builtin_type(CN_TYPE_STR));
                if (!cn_type_equal(value_type, cn_builtin_type(CN_TYPE_STR))) {
                    cn_emit_type_mismatch(ctx->diagnostics, expression->data.call.arguments.items[0]->offset, "str_copy requires str", cn_builtin_type(CN_TYPE_STR), value_type);
                }
            }
            return cn_builtin_type(CN_TYPE_STR);
        }

        if (cn_sv_eq_cstr(callee, "str_concat")) {
            if (expression->data.call.arguments.count != 2) {
                cn_diag_emit(ctx->diagnostics, CN_DIAG_ERROR, "E3008", expression->offset, "str_concat expects exactly 2 arguments");
            } else {
                for (size_t i = 0; i < 2; ++i) {
                    const cn_type_ref *value_type = cn_check_expression_hint(ctx, scope, expression->data.call.arguments.items[i], cn_builtin_type(CN_TYPE_STR));
                    if (!cn_type_equal(value_type, cn_builtin_type(CN_TYPE_STR))) {
                        cn_emit_type_mismatch(ctx->diagnostics, expression->data.call.arguments.items[i]->offset, "str_concat requires str", cn_builtin_type(CN_TYPE_STR), value_type);
                    }
                }
            }
            return cn_builtin_type(CN_TYPE_STR);
        }

        if (cn_find_imported_module(ctx, ctx->module, callee) != NULL) {
            cn_diag_emit(ctx->diagnostics, CN_DIAG_ERROR, "E3014", expression->offset, "module names are not callable; use module.function(...)");
            return cn_builtin_type(CN_TYPE_UNKNOWN);
        }

        if (cn_find_const(ctx, ctx->module, callee) != NULL) {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3014",
                expression->offset,
                "constant '%.*s' is not callable",
                (int)callee.length,
                callee.data
            );
            return cn_builtin_type(CN_TYPE_UNKNOWN);
        }

        const cn_function *function = cn_find_local_function(ctx, ctx->module, callee);
        if (function == NULL) {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3002",
                expression->offset,
                "unknown function '%.*s'",
                (int)callee.length,
                callee.data
            );
            return cn_builtin_type(CN_TYPE_UNKNOWN);
        }

        cn_check_call_arguments(ctx, scope, expression, function);
        return function->return_type;
    }

    if (expression->data.call.callee->kind == CN_EXPR_FIELD && expression->data.call.callee->data.field.base->kind == CN_EXPR_NAME) {
        cn_strview module_name = expression->data.call.callee->data.field.base->data.name;
        cn_strview function_name = expression->data.call.callee->data.field.field_name;
        const cn_module *imported = cn_find_imported_module(ctx, ctx->module, module_name);

        if (imported == NULL) {
            cn_diag_emit(ctx->diagnostics, CN_DIAG_ERROR, "E3014", expression->offset, "call target must be a function name or imported module function");
            return cn_builtin_type(CN_TYPE_UNKNOWN);
        }

        if (cn_find_public_const(ctx, imported, function_name) != NULL) {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3014",
                expression->offset,
                "module '%s' exports constant '%.*s', not a function",
                imported->name,
                (int)function_name.length,
                function_name.data
            );
            return cn_builtin_type(CN_TYPE_UNKNOWN);
        }

        const cn_function *function = cn_find_public_function(ctx, imported, function_name);
        if (function == NULL) {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3002",
                expression->offset,
                "module '%s' does not export function '%.*s'",
                imported->name,
                (int)function_name.length,
                function_name.data
            );
            return cn_builtin_type(CN_TYPE_UNKNOWN);
        }

        cn_check_call_arguments(ctx, scope, expression, function);
        return function->return_type;
    }

    cn_diag_emit(ctx->diagnostics, CN_DIAG_ERROR, "E3014", expression->offset, "call target must be a function name or imported module function");
    for (size_t i = 0; i < expression->data.call.arguments.count; ++i) {
        cn_check_expression_hint(ctx, scope, expression->data.call.arguments.items[i], NULL);
    }
    return cn_builtin_type(CN_TYPE_UNKNOWN);
}

static bool cn_check_address_target_storage(cn_sema_ctx *ctx, cn_scope *scope, const cn_expr *target) {
    switch (target->kind) {
    case CN_EXPR_NAME: {
        const cn_binding *binding = cn_scope_lookup(scope, target->data.name);
        if (binding != NULL) {
            if (!binding->is_mutable) {
                cn_diag_emit(
                    ctx->diagnostics,
                    CN_DIAG_ERROR,
                    "E3035",
                    target->offset,
                    "cannot take address of immutable binding '%.*s'; use 'let mut' for mutable storage",
                    (int)target->data.name.length,
                    target->data.name.data
                );
                return false;
            }
            return true;
        }

        if (cn_find_const(ctx, ctx->module, target->data.name) != NULL) {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3036",
                target->offset,
                "cannot take address of module constant '%.*s'",
                (int)target->data.name.length,
                target->data.name.data
            );
            return false;
        }

        return true;
    }
    case CN_EXPR_FIELD:
        if (target->data.field.base->kind == CN_EXPR_NAME &&
            cn_sv_eq_cstr(target->data.field.field_name, "value")) {
            const cn_binding *binding = cn_scope_lookup(scope, target->data.field.base->data.name);
            if (binding != NULL && binding->type->kind == CN_TYPE_PTR) {
                return true;
            }
        }
        if (target->data.field.base->kind == CN_EXPR_NAME) {
            cn_resolved_const resolved_const = cn_resolve_source_named_const(
                ctx,
                target->data.field.base->data.name,
                target->data.field.field_name
            );
            if (resolved_const.decl != NULL) {
                cn_diag_emit(
                    ctx->diagnostics,
                    CN_DIAG_ERROR,
                    "E3036",
                    target->offset,
                    "cannot take address of module constant '%.*s.%.*s'",
                    (int)target->data.field.base->data.name.length,
                    target->data.field.base->data.name.data,
                    (int)target->data.field.field_name.length,
                    target->data.field.field_name.data
                );
                return false;
            }
        }
        return cn_check_address_target_storage(ctx, scope, target->data.field.base);
    case CN_EXPR_INDEX:
        return cn_check_address_target_storage(ctx, scope, target->data.index.base);
    case CN_EXPR_DEREF:
        return true;
    default:
        cn_diag_emit(
            ctx->diagnostics,
            CN_DIAG_ERROR,
            "E3031",
            target->offset,
            "address-of target must be a named value, dereference, field, or array element"
        );
        return false;
    }
}

static const cn_type_ref *cn_check_address_of(cn_sema_ctx *ctx, cn_scope *scope, const cn_expr *expression) {
    const cn_expr *target = expression->data.addr_expr.target;
    if (!cn_check_address_target_storage(ctx, scope, target)) {
        return cn_builtin_type(CN_TYPE_UNKNOWN);
    }

    const cn_type_ref *target_type = cn_check_expression_hint(ctx, scope, target, NULL);
    return cn_make_temp_type(
        ctx,
        CN_TYPE_PTR,
        cn_sv_from_parts(NULL, 0),
        cn_sv_from_parts(NULL, 0),
        target_type,
        0,
        expression->offset
    );
}

static const cn_type_ref *cn_check_if_expression(
    cn_sema_ctx *ctx,
    cn_scope *scope,
    const cn_expr *expression,
    const cn_type_ref *expected
) {
    const cn_type_ref *condition_type = cn_check_expression_hint(
        ctx,
        scope,
        expression->data.if_expr.condition,
        cn_builtin_type(CN_TYPE_BOOL)
    );
    if (!cn_type_equal(condition_type, cn_builtin_type(CN_TYPE_BOOL))) {
        cn_diag_emit(
            ctx->diagnostics,
            CN_DIAG_ERROR,
            "E3005",
            expression->data.if_expr.condition->offset,
            "if expression condition must be bool"
        );
    }

    const cn_type_ref *then_type = NULL;
    const cn_type_ref *else_type = NULL;

    if (expected != NULL) {
        then_type = cn_check_expression_in_proof_scope(
            ctx,
            scope,
            expression->data.if_expr.then_expr,
            expected,
            expression->data.if_expr.condition,
            true
        );
        else_type = cn_check_expression_in_proof_scope(
            ctx,
            scope,
            expression->data.if_expr.else_expr,
            expected,
            expression->data.if_expr.condition,
            false
        );
    } else {
        then_type = cn_check_expression_in_proof_scope(
            ctx,
            scope,
            expression->data.if_expr.then_expr,
            NULL,
            expression->data.if_expr.condition,
            true
        );
        else_type = cn_check_expression_against_peer_in_proof_scope(
            ctx,
            scope,
            expression->data.if_expr.else_expr,
            then_type,
            expression->data.if_expr.condition,
            false
        );
        if (else_type->kind == CN_TYPE_U8 && expression->data.if_expr.then_expr->kind == CN_EXPR_INT) {
            then_type = cn_check_expression_in_proof_scope(
                ctx,
                scope,
                expression->data.if_expr.then_expr,
                else_type,
                expression->data.if_expr.condition,
                true
            );
        }
        if (else_type->kind == CN_TYPE_PTR && expression->data.if_expr.then_expr->kind == CN_EXPR_NULL) {
            then_type = cn_check_expression_in_proof_scope(
                ctx,
                scope,
                expression->data.if_expr.then_expr,
                else_type,
                expression->data.if_expr.condition,
                true
            );
        }
    }

    if (then_type->kind == CN_TYPE_VOID || else_type->kind == CN_TYPE_VOID) {
        cn_diag_emit(
            ctx->diagnostics,
            CN_DIAG_ERROR,
            "E3029",
            expression->offset,
            "if expressions must produce a non-void value on both branches"
        );
        return cn_builtin_type(CN_TYPE_UNKNOWN);
    }

    if (expected != NULL &&
        cn_type_can_assign_to(expected, then_type) &&
        cn_type_can_assign_to(expected, else_type)) {
        return expected;
    }

    if (!cn_type_equal(then_type, else_type)) {
        char then_name[64];
        char else_name[64];
        cn_type_describe(then_type, then_name, sizeof(then_name));
        cn_type_describe(else_type, else_name, sizeof(else_name));
        cn_diag_emit(
            ctx->diagnostics,
            CN_DIAG_ERROR,
            "E3030",
            expression->offset,
            "if expression branch types must match: then is %s, else is %s",
            then_name,
            else_name
        );
        return cn_builtin_type(CN_TYPE_UNKNOWN);
    }

    return then_type;
}

static const cn_type_ref *cn_check_expression_hint(cn_sema_ctx *ctx, cn_scope *scope, const cn_expr *expression, const cn_type_ref *expected) {
    switch (expression->kind) {
    case CN_EXPR_INT:
        if (expected != NULL && expected->kind == CN_TYPE_U8) {
            if (cn_int_literal_fits_u8(expression)) {
                return cn_builtin_type(CN_TYPE_U8);
            }

            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3028",
                expression->offset,
                "u8 literal out of range: expected 0..255, got %lld",
                (long long)expression->data.int_value
            );
            return cn_builtin_type(CN_TYPE_U8);
        }
        return cn_builtin_type(CN_TYPE_INT);
    case CN_EXPR_BOOL:
        return cn_builtin_type(CN_TYPE_BOOL);
    case CN_EXPR_STRING:
        return cn_builtin_type(CN_TYPE_STR);
    case CN_EXPR_NULL:
        if (expected != NULL && expected->kind == CN_TYPE_PTR) {
            return expected;
        }
        return cn_builtin_type(CN_TYPE_NULL);
    case CN_EXPR_NAME: {
        const cn_binding *binding = cn_scope_lookup(scope, expression->data.name);
        if (binding != NULL) {
            return binding->type;
        }

        const cn_const_decl *const_decl = cn_find_const(ctx, ctx->module, expression->data.name);
        if (const_decl != NULL) {
            return const_decl->type;
        }

        if (cn_find_imported_module(ctx, ctx->module, expression->data.name) != NULL) {
            cn_diag_emit(ctx->diagnostics, CN_DIAG_ERROR, "E3014", expression->offset, "module names are not values yet");
        } else {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3002",
                expression->offset,
                "unknown name '%.*s'",
                (int)expression->data.name.length,
                expression->data.name.data
            );
        }
        return cn_builtin_type(CN_TYPE_UNKNOWN);
    }
    case CN_EXPR_UNARY: {
        const cn_type_ref *operand_type = cn_check_expression_hint(ctx, scope, expression->data.unary.operand, NULL);
        if (expression->data.unary.op == CN_UNARY_NEGATE) {
            if (!cn_type_equal(operand_type, cn_builtin_type(CN_TYPE_INT))) {
                cn_emit_type_mismatch(ctx->diagnostics, expression->offset, "unary '-' requires int", cn_builtin_type(CN_TYPE_INT), operand_type);
            }
            return cn_builtin_type(CN_TYPE_INT);
        }

        if (!cn_type_equal(operand_type, cn_builtin_type(CN_TYPE_BOOL))) {
            cn_emit_type_mismatch(ctx->diagnostics, expression->offset, "unary '!' requires bool", cn_builtin_type(CN_TYPE_BOOL), operand_type);
        }
        return cn_builtin_type(CN_TYPE_BOOL);
    }
    case CN_EXPR_BINARY: {
        const cn_type_ref *left = cn_check_expression_hint(ctx, scope, expression->data.binary.left, NULL);
        const cn_type_ref *right = cn_check_expression_against_peer(ctx, scope, expression->data.binary.right, left);
        if (right->kind == CN_TYPE_U8 && expression->data.binary.left->kind == CN_EXPR_INT) {
            left = cn_check_expression_hint(ctx, scope, expression->data.binary.left, right);
        }
        if (right->kind == CN_TYPE_PTR && expression->data.binary.left->kind == CN_EXPR_NULL) {
            left = cn_check_expression_hint(ctx, scope, expression->data.binary.left, right);
        }

        switch (expression->data.binary.op) {
        case CN_BINARY_ADD:
        case CN_BINARY_SUB:
        case CN_BINARY_MUL:
        case CN_BINARY_DIV:
        case CN_BINARY_MOD:
            if (!cn_type_equal(left, cn_builtin_type(CN_TYPE_INT))) {
                cn_emit_type_mismatch(ctx->diagnostics, expression->offset, "arithmetic operator requires int", cn_builtin_type(CN_TYPE_INT), left);
            }
            if (!cn_type_equal(right, cn_builtin_type(CN_TYPE_INT))) {
                cn_emit_type_mismatch(ctx->diagnostics, expression->offset, "arithmetic operator requires int", cn_builtin_type(CN_TYPE_INT), right);
            }
            return cn_builtin_type(CN_TYPE_INT);
        case CN_BINARY_EQUAL:
        case CN_BINARY_NOT_EQUAL:
            if (!cn_type_matches_equality_operands(left, right)) {
                cn_emit_type_mismatch(ctx->diagnostics, expression->offset, "equality operands must match", left, right);
            }
            return cn_builtin_type(CN_TYPE_BOOL);
        case CN_BINARY_LESS:
        case CN_BINARY_LESS_EQUAL:
        case CN_BINARY_GREATER:
        case CN_BINARY_GREATER_EQUAL:
            if (!cn_integer_types_match(left, right)) {
                if (!cn_type_is_integer_like(left)) {
                    cn_emit_type_mismatch(ctx->diagnostics, expression->offset, "comparison operator requires int or u8", cn_builtin_type(CN_TYPE_INT), left);
                } else if (!cn_type_is_integer_like(right)) {
                    cn_emit_type_mismatch(ctx->diagnostics, expression->offset, "comparison operator requires int or u8", left, right);
                } else {
                    cn_emit_type_mismatch(ctx->diagnostics, expression->offset, "comparison operands must use the same integer type", left, right);
                }
            }
            return cn_builtin_type(CN_TYPE_BOOL);
        case CN_BINARY_AND:
        case CN_BINARY_OR:
            if (!cn_type_equal(left, cn_builtin_type(CN_TYPE_BOOL))) {
                cn_emit_type_mismatch(ctx->diagnostics, expression->offset, "logical operator requires bool", cn_builtin_type(CN_TYPE_BOOL), left);
            }
            if (!cn_type_equal(right, cn_builtin_type(CN_TYPE_BOOL))) {
                cn_emit_type_mismatch(ctx->diagnostics, expression->offset, "logical operator requires bool", cn_builtin_type(CN_TYPE_BOOL), right);
            }
            return cn_builtin_type(CN_TYPE_BOOL);
        }
        return cn_builtin_type(CN_TYPE_UNKNOWN);
    }
    case CN_EXPR_IF:
        return cn_check_if_expression(ctx, scope, expression, expected);
    case CN_EXPR_CALL:
        return cn_check_call(ctx, scope, expression);
    case CN_EXPR_ARRAY_LITERAL:
        return cn_check_array_literal(ctx, scope, expression, expected);
    case CN_EXPR_INDEX:
        return cn_check_index(ctx, scope, expression);
    case CN_EXPR_SLICE_VIEW:
        return cn_check_slice_view(ctx, scope, expression);
    case CN_EXPR_FIELD:
        return cn_check_field_access(ctx, scope, expression);
    case CN_EXPR_STRUCT_LITERAL:
        return cn_check_struct_literal(ctx, scope, expression);
    case CN_EXPR_OK: {
        if (expected != NULL && expected->kind == CN_TYPE_RESULT) {
            const cn_type_ref *value_type = cn_check_expression_hint(ctx, scope, expression->data.ok_expr.value, expected->inner);
            if (!cn_type_can_assign_to(expected->inner, value_type)) {
                cn_emit_type_mismatch(ctx->diagnostics, expression->offset, "ok value type mismatch", expected->inner, value_type);
            }
            return expected;
        }

        const cn_type_ref *value_type = cn_check_expression_hint(ctx, scope, expression->data.ok_expr.value, NULL);
        return cn_make_temp_type(
            ctx,
            CN_TYPE_RESULT,
            cn_sv_from_parts(NULL, 0),
            cn_sv_from_parts(NULL, 0),
            value_type,
            0,
            expression->offset
        );
    }
    case CN_EXPR_ERR:
        if (expected != NULL && expected->kind == CN_TYPE_RESULT) {
            return expected;
        }
        cn_diag_emit(ctx->diagnostics, CN_DIAG_ERROR, "E3015", expression->offset, "'err' requires an expected result type");
        return cn_builtin_type(CN_TYPE_UNKNOWN);
    case CN_EXPR_ALLOC:
        cn_validate_type_ref(ctx, expression->data.alloc_expr.type, expression->offset);
        return cn_make_temp_type(
            ctx,
            CN_TYPE_PTR,
            cn_sv_from_parts(NULL, 0),
            cn_sv_from_parts(NULL, 0),
            expression->data.alloc_expr.type,
            0,
            expression->offset
        );
    case CN_EXPR_ZALLOC:
        if (scope == NULL || scope->zone_depth == 0) {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3041",
                expression->offset,
                "zalloc requires an enclosing zone block"
            );
        }
        cn_validate_type_ref(ctx, expression->data.zalloc_expr.type, expression->offset);
        return cn_make_temp_type(
            ctx,
            CN_TYPE_PTR,
            cn_sv_from_parts(NULL, 0),
            cn_sv_from_parts(NULL, 0),
            expression->data.zalloc_expr.type,
            0,
            expression->offset
        );
    case CN_EXPR_ADDR:
        return cn_check_address_of(ctx, scope, expression);
    case CN_EXPR_DEREF: {
        const cn_type_ref *target_type = cn_check_expression_hint(ctx, scope, expression->data.deref_expr.target, NULL);
        if (target_type->kind != CN_TYPE_PTR) {
            cn_diag_emit(ctx->diagnostics, CN_DIAG_ERROR, "E3032", expression->offset, "deref requires a ptr value");
            return cn_builtin_type(CN_TYPE_UNKNOWN);
        }
        return target_type->inner;
    }
    }

    return cn_builtin_type(CN_TYPE_UNKNOWN);
}

static bool cn_stmt_guarantees_return(const cn_stmt *statement);

static bool cn_block_guarantees_return(const cn_block *block) {
    for (size_t i = 0; i < block->statements.count; ++i) {
        const cn_stmt *statement = block->statements.items[i];
        if (cn_stmt_guarantees_return(statement)) {
            return true;
        }
    }
    return false;
}

static bool cn_stmt_guarantees_return(const cn_stmt *statement) {
    switch (statement->kind) {
    case CN_STMT_RETURN:
        return true;
    case CN_STMT_IF:
        return statement->data.if_stmt.else_block != NULL &&
               cn_block_guarantees_return(statement->data.if_stmt.then_block) &&
               cn_block_guarantees_return(statement->data.if_stmt.else_block);
    case CN_STMT_LOOP:
        return cn_block_guarantees_return(statement->data.loop_stmt.body);
    case CN_STMT_LET:
    case CN_STMT_ASSIGN:
    case CN_STMT_EXPR:
    case CN_STMT_DEFER:
    case CN_STMT_TRY:
    case CN_STMT_ZONE:
    case CN_STMT_WHILE:
    case CN_STMT_FOR:
    case CN_STMT_FREE:
        return false;
    }

    return false;
}

static cn_assignment_target cn_check_assignment_target(cn_sema_ctx *ctx, cn_scope *scope, const cn_expr *target) {
    cn_assignment_target result;
    result.type = cn_builtin_type(CN_TYPE_UNKNOWN);
    result.requires_mutable_binding = false;
    result.binding_name = cn_sv_from_parts(NULL, 0);
    result.binding_scope_zone_depth = 0;

    if (target->kind == CN_EXPR_NAME) {
        const cn_binding *binding = cn_scope_lookup(scope, target->data.name);
        if (binding == NULL) {
            const cn_const_decl *const_decl = cn_find_const(ctx, ctx->module, target->data.name);
            if (const_decl != NULL) {
                cn_diag_emit(
                    ctx->diagnostics,
                    CN_DIAG_ERROR,
                    "E3006",
                    target->offset,
                    "cannot assign to module constant '%.*s'",
                    (int)target->data.name.length,
                    target->data.name.data
                );
                result.type = const_decl->type;
                return result;
            }

            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3002",
                target->offset,
                "unknown name '%.*s'",
                (int)target->data.name.length,
                target->data.name.data
            );
            return result;
        }

        result.type = binding->type;
        result.requires_mutable_binding = true;
        result.binding_name = binding->name;
        result.binding_scope_zone_depth = binding->scope_zone_depth;
        return result;
    }

    if (target->kind == CN_EXPR_FIELD || target->kind == CN_EXPR_INDEX || target->kind == CN_EXPR_DEREF) {
        if (target->kind == CN_EXPR_FIELD && target->data.field.base->kind == CN_EXPR_NAME) {
            cn_resolved_const resolved_const = cn_resolve_source_named_const(
                ctx,
                target->data.field.base->data.name,
                target->data.field.field_name
            );
            if (resolved_const.decl != NULL) {
                cn_diag_emit(
                    ctx->diagnostics,
                    CN_DIAG_ERROR,
                    "E3006",
                    target->offset,
                    "cannot assign to module constant '%.*s.%.*s'",
                    (int)target->data.field.base->data.name.length,
                    target->data.field.base->data.name.data,
                    (int)target->data.field.field_name.length,
                    target->data.field.field_name.data
                );
                result.type = resolved_const.decl->type;
                return result;
            }
        }

        result.type = cn_check_expression_hint(ctx, scope, target, NULL);
        result.binding_scope_zone_depth = cn_assignment_storage_zone_depth(ctx, scope, target);
        return result;
    }

    cn_diag_emit(ctx->diagnostics, CN_DIAG_ERROR, "E3004", target->offset, "invalid assignment target");
    return result;
}

static bool cn_check_statement(cn_sema_ctx *ctx, cn_scope *scope, const cn_stmt *statement) {
    switch (statement->kind) {
    case CN_STMT_LET: {
        cn_validate_type_ref(ctx, statement->data.let_stmt.type, statement->offset);
        const cn_type_ref *initializer_type = cn_check_expression_hint(ctx, scope, statement->data.let_stmt.initializer, statement->data.let_stmt.type);
        size_t initializer_zone_depth = cn_expr_zone_depth(ctx, scope, statement->data.let_stmt.initializer, statement->data.let_stmt.type);
        if (!cn_type_can_assign_to(statement->data.let_stmt.type, initializer_type)) {
            cn_emit_type_mismatch(ctx->diagnostics, statement->offset, "initializer type mismatch", statement->data.let_stmt.type, initializer_type);
        }
        cn_scope_define(
            ctx,
            scope,
            statement->data.let_stmt.name,
            statement->data.let_stmt.type,
            statement->data.let_stmt.is_mutable,
            initializer_zone_depth,
            statement->offset
        );
        if (!statement->data.let_stmt.is_mutable && cn_type_equal(statement->data.let_stmt.type, cn_builtin_type(CN_TYPE_BOOL))) {
            cn_binding *binding = cn_scope_lookup_mut(scope, statement->data.let_stmt.name);
            const cn_binding *guard_binding = NULL;
            bool guard_positive = false;
            if (binding != NULL &&
                cn_extract_result_ok_alias(scope, statement->data.let_stmt.initializer, &guard_binding, &guard_positive)) {
                binding->result_guard_binding = guard_binding;
                binding->has_result_guard_alias = true;
                binding->result_guard_positive = guard_positive;
            }
        }
        return true;
    }
    case CN_STMT_ASSIGN: {
        cn_assignment_target target_info = cn_check_assignment_target(ctx, scope, statement->data.assign_stmt.target);
        if (target_info.requires_mutable_binding) {
            const cn_binding *binding = cn_scope_lookup(scope, target_info.binding_name);
            if (binding != NULL && !binding->is_mutable) {
                cn_diag_emit(
                    ctx->diagnostics,
                    CN_DIAG_ERROR,
                    "E3006",
                    statement->offset,
                    "cannot assign to immutable binding '%.*s'",
                    (int)target_info.binding_name.length,
                    target_info.binding_name.data
                );
            }
        }

        const cn_type_ref *value_type = cn_check_expression_hint(ctx, scope, statement->data.assign_stmt.value, target_info.type);
        size_t value_zone_depth = cn_expr_zone_depth(ctx, scope, statement->data.assign_stmt.value, target_info.type);
        if (!cn_type_can_assign_to(target_info.type, value_type)) {
            cn_emit_type_mismatch(ctx->diagnostics, statement->offset, "assignment type mismatch", target_info.type, value_type);
        }

        if (value_zone_depth > 0 && target_info.binding_scope_zone_depth < value_zone_depth) {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3044",
                statement->offset,
                "zone-owned value cannot escape into outer storage"
            );
        } else if (target_info.binding_name.length > 0) {
            cn_binding *binding = cn_scope_lookup_mut(scope, target_info.binding_name);
            if (binding != NULL) {
                binding->value_zone_depth = value_zone_depth;
            }
        }
        return true;
    }
    case CN_STMT_RETURN: {
        const cn_type_ref *function_type = ctx->current_function->return_type;
        if (statement->data.return_stmt.value == NULL) {
            if (!cn_type_equal(function_type, cn_builtin_type(CN_TYPE_VOID))) {
                cn_emit_type_mismatch(ctx->diagnostics, statement->offset, "return value required", function_type, cn_builtin_type(CN_TYPE_VOID));
            }
            return true;
        }

        const cn_type_ref *value_type = cn_check_expression_hint(ctx, scope, statement->data.return_stmt.value, function_type);
        if (!cn_type_can_assign_to(function_type, value_type)) {
            cn_emit_type_mismatch(ctx->diagnostics, statement->offset, "return type mismatch", function_type, value_type);
        }
        if (cn_expr_zone_depth(ctx, scope, statement->data.return_stmt.value, function_type) > 0) {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3042",
                statement->offset,
                "zone-owned value cannot escape through return"
            );
        }
        return true;
    }
    case CN_STMT_EXPR:
        cn_check_expression_hint(ctx, scope, statement->data.expr_stmt.value, NULL);
        return true;
    case CN_STMT_DEFER:
        return cn_check_statement(ctx, scope, statement->data.defer_stmt.statement);
    case CN_STMT_TRY: {
        const cn_type_ref *initializer_type = cn_check_expression_hint(ctx, scope, statement->data.try_stmt.initializer, NULL);
        size_t initializer_zone_depth = initializer_type->kind == CN_TYPE_RESULT
            ? cn_expr_zone_depth(ctx, scope, statement->data.try_stmt.initializer, initializer_type->inner)
            : 0;
        if (ctx->current_function->return_type->kind != CN_TYPE_RESULT) {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3033",
                statement->offset,
                "try requires the enclosing function to return result ..."
            );
        }

        const cn_type_ref *value_type = cn_builtin_type(CN_TYPE_UNKNOWN);
        if (initializer_type->kind != CN_TYPE_RESULT) {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3034",
                statement->offset,
                "try initializer must have type result ..."
            );
        } else {
            value_type = initializer_type->inner;
        }

        cn_scope_define(
            ctx,
            scope,
            statement->data.try_stmt.name,
            value_type,
            false,
            initializer_zone_depth,
            statement->offset
        );
        return true;
    }
    case CN_STMT_ZONE: {
        cn_scope zone_scope = {0};
        zone_scope.parent = scope;
        zone_scope.zone_depth = (scope != NULL ? scope->zone_depth : 0) + 1;
        cn_check_block(ctx, &zone_scope, statement->data.zone_stmt.body, false);
        cn_scope_release(ctx, &zone_scope);
        return true;
    }
    case CN_STMT_IF: {
        const cn_type_ref *condition_type = cn_check_expression_hint(ctx, scope, statement->data.if_stmt.condition, cn_builtin_type(CN_TYPE_BOOL));
        if (!cn_type_equal(condition_type, cn_builtin_type(CN_TYPE_BOOL))) {
            cn_diag_emit(ctx->diagnostics, CN_DIAG_ERROR, "E3005", statement->data.if_stmt.condition->offset, "if condition must be bool");
        }

        cn_check_block_with_guard(ctx, scope, statement->data.if_stmt.then_block, true, statement->data.if_stmt.condition, true);
        if (statement->data.if_stmt.else_block != NULL) {
            cn_check_block_with_guard(ctx, scope, statement->data.if_stmt.else_block, true, statement->data.if_stmt.condition, false);
        }

        if (statement->data.if_stmt.else_block != NULL && cn_block_guarantees_return(statement->data.if_stmt.else_block)) {
            cn_mark_result_ok_proofs_for_branch(ctx, scope, scope, statement->data.if_stmt.condition, true);
        }
        if (cn_block_guarantees_return(statement->data.if_stmt.then_block)) {
            cn_mark_result_ok_proofs_for_branch(ctx, scope, scope, statement->data.if_stmt.condition, false);
        }
        return true;
    }
    case CN_STMT_WHILE: {
        const cn_type_ref *condition_type = cn_check_expression_hint(ctx, scope, statement->data.while_stmt.condition, cn_builtin_type(CN_TYPE_BOOL));
        if (!cn_type_equal(condition_type, cn_builtin_type(CN_TYPE_BOOL))) {
            cn_diag_emit(ctx->diagnostics, CN_DIAG_ERROR, "E3005", statement->data.while_stmt.condition->offset, "while condition must be bool");
        }
        cn_check_block_with_guard(ctx, scope, statement->data.while_stmt.body, true, statement->data.while_stmt.condition, true);
        return true;
    }
    case CN_STMT_LOOP:
        cn_check_block(ctx, scope, statement->data.loop_stmt.body, true);
        return true;
    case CN_STMT_FOR: {
        cn_validate_type_ref(ctx, statement->data.for_stmt.type, statement->offset);
        if (!cn_type_equal(statement->data.for_stmt.type, cn_builtin_type(CN_TYPE_INT))) {
            cn_emit_type_mismatch(ctx->diagnostics, statement->offset, "range loop binding must be int", cn_builtin_type(CN_TYPE_INT), statement->data.for_stmt.type);
        }

        const cn_type_ref *start_type = cn_check_expression_hint(ctx, scope, statement->data.for_stmt.start, cn_builtin_type(CN_TYPE_INT));
        const cn_type_ref *end_type = cn_check_expression_hint(ctx, scope, statement->data.for_stmt.end, cn_builtin_type(CN_TYPE_INT));
        if (!cn_type_equal(start_type, cn_builtin_type(CN_TYPE_INT))) {
            cn_emit_type_mismatch(ctx->diagnostics, statement->data.for_stmt.start->offset, "range start must be int", cn_builtin_type(CN_TYPE_INT), start_type);
        }
        if (!cn_type_equal(end_type, cn_builtin_type(CN_TYPE_INT))) {
            cn_emit_type_mismatch(ctx->diagnostics, statement->data.for_stmt.end->offset, "range end must be int", cn_builtin_type(CN_TYPE_INT), end_type);
        }

        cn_scope loop_scope = {0};
        loop_scope.parent = scope;
        loop_scope.zone_depth = scope != NULL ? scope->zone_depth : 0;
        cn_scope_define(ctx, &loop_scope, statement->data.for_stmt.name, statement->data.for_stmt.type, false, 0, statement->offset);
        cn_check_block(ctx, &loop_scope, statement->data.for_stmt.body, true);
        cn_scope_release(ctx, &loop_scope);
        return true;
    }
    case CN_STMT_FREE: {
        const cn_type_ref *value_type = cn_check_expression_hint(ctx, scope, statement->data.free_stmt.value, NULL);
        if (cn_expr_zone_depth(ctx, scope, statement->data.free_stmt.value, value_type) > 0) {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3043",
                statement->offset,
                "free cannot release zone-owned memory; zone allocations are released when the zone ends"
            );
            return true;
        }
        if (value_type->kind == CN_TYPE_SLICE) {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3037",
                statement->offset,
                "free cannot release a slice value; slices are non-owning views"
            );
            return true;
        }
        if (value_type->kind == CN_TYPE_RESULT) {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3038",
                statement->offset,
                "free cannot release a result value; unwrap the owned payload first"
            );
            return true;
        }
        if (value_type->kind != CN_TYPE_PTR && value_type->kind != CN_TYPE_STR) {
            cn_diag_emit(ctx->diagnostics, CN_DIAG_ERROR, "E3019", statement->offset, "free requires a ptr or str value");
        }
        return true;
    }
    }

    return true;
}

static bool cn_check_block(cn_sema_ctx *ctx, cn_scope *parent, const cn_block *block, bool creates_scope) {
    return cn_check_block_with_guard(ctx, parent, block, creates_scope, NULL, false);
}

static bool cn_check_block_with_guard(
    cn_sema_ctx *ctx,
    cn_scope *parent,
    const cn_block *block,
    bool creates_scope,
    const cn_expr *guard_condition,
    bool guard_branch_truth
) {
    cn_scope scope = {0};
    cn_scope *active_scope = parent;

    if (creates_scope || guard_condition != NULL) {
        scope.parent = parent;
        scope.zone_depth = parent != NULL ? parent->zone_depth : 0;
        active_scope = &scope;
    }

    if (guard_condition != NULL) {
        cn_mark_result_ok_proofs_for_branch(ctx, active_scope, parent, guard_condition, guard_branch_truth);
    }

    for (size_t i = 0; i < block->statements.count; ++i) {
        cn_check_statement(ctx, active_scope, block->statements.items[i]);
    }

    if (creates_scope || guard_condition != NULL) {
        cn_scope_release(ctx, &scope);
    }

    return !cn_diag_has_error(ctx->diagnostics);
}

static bool cn_check_struct_decl(cn_sema_ctx *ctx, const cn_struct_decl *struct_decl) {
    for (size_t i = 0; i < struct_decl->fields.count; ++i) {
        for (size_t j = i + 1; j < struct_decl->fields.count; ++j) {
            if (cn_name_eq(struct_decl->fields.items[i].name, struct_decl->fields.items[j].name)) {
                cn_diag_emit(
                    ctx->diagnostics,
                    CN_DIAG_ERROR,
                    "E3003",
                    struct_decl->fields.items[j].offset,
                    "duplicate field '%.*s' in struct '%.*s'",
                    (int)struct_decl->fields.items[j].name.length,
                    struct_decl->fields.items[j].name.data,
                    (int)struct_decl->name.length,
                    struct_decl->name.data
                );
            }
        }

        bool field_type_valid = cn_validate_type_ref(ctx, struct_decl->fields.items[i].type, struct_decl->fields.items[i].offset);
        if (struct_decl->is_public && field_type_valid && !cn_type_is_exportable(ctx, struct_decl->fields.items[i].type)) {
            cn_emit_private_type_in_public_api(
                ctx,
                struct_decl->fields.items[i].offset,
                "struct",
                struct_decl->name,
                struct_decl->fields.items[i].type
            );
        }
    }

    return !cn_diag_has_error(ctx->diagnostics);
}

static bool cn_check_const_decl(cn_sema_ctx *ctx, const cn_module *module, const cn_const_decl *const_decl) {
    const cn_module *previous_module = ctx->module;
    bool type_valid = false;

    if (const_decl->sema_checked) {
        return true;
    }

    if (const_decl->sema_checking) {
        cn_diag_emit(
            ctx->diagnostics,
            CN_DIAG_ERROR,
            "E3026",
            const_decl->offset,
            "cyclic constant definition involving '%.*s.%.*s'",
            (int)cn_sv_from_cstr(module->name).length,
            module->name,
            (int)const_decl->name.length,
            const_decl->name.data
        );
        return false;
    }

    ctx->module = module;
    ((cn_const_decl *)const_decl)->sema_checking = true;

    type_valid = cn_validate_type_ref(ctx, const_decl->type, const_decl->offset);
    if (const_decl->is_public && type_valid && !cn_type_is_exportable(ctx, const_decl->type)) {
        cn_emit_private_type_in_public_api(ctx, const_decl->offset, "constant", const_decl->name, const_decl->type);
    }

    cn_check_const_expr(ctx, const_decl->initializer);

    const cn_type_ref *initializer_type = cn_check_expression_hint(ctx, NULL, const_decl->initializer, const_decl->type);
    if (!cn_type_equal(const_decl->type, initializer_type)) {
        cn_emit_type_mismatch(ctx->diagnostics, const_decl->offset, "constant initializer type mismatch", const_decl->type, initializer_type);
    }

    ((cn_const_decl *)const_decl)->sema_checking = false;
    ((cn_const_decl *)const_decl)->sema_checked = true;
    ctx->module = previous_module;
    return !cn_diag_has_error(ctx->diagnostics);
}

static bool cn_check_module_header(cn_sema_ctx *ctx) {
    cn_name_map import_aliases = {0};
    cn_name_map const_names = {0};
    cn_name_map struct_names = {0};
    cn_name_map function_names = {0};

    for (size_t i = 0; i < ctx->module->program->const_count; ++i) {
        const cn_const_decl *previous = (const cn_const_decl *)cn_name_map_lookup_value(&const_names, ctx->module->program->consts[i]->name);
        if (previous != NULL) {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3027",
                ctx->module->program->consts[i]->offset,
                "duplicate constant '%.*s'",
                (int)ctx->module->program->consts[i]->name.length,
                ctx->module->program->consts[i]->name.data
            );
            continue;
        }

        cn_name_map_insert(ctx, &const_names, ctx->module->program->consts[i]->name, ctx->module->program->consts[i], 0, false);
    }

    for (size_t i = 0; i < ctx->module->program->struct_count; ++i) {
        const cn_struct_decl *previous = (const cn_struct_decl *)cn_name_map_lookup_value(&struct_names, ctx->module->program->structs[i]->name);
        if (previous != NULL) {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3013",
                ctx->module->program->structs[i]->offset,
                "duplicate struct '%.*s'",
                (int)ctx->module->program->structs[i]->name.length,
                ctx->module->program->structs[i]->name.data
            );
            continue;
        }

        cn_name_map_insert(ctx, &struct_names, ctx->module->program->structs[i]->name, ctx->module->program->structs[i], 0, false);
    }

    for (size_t i = 0; i < ctx->module->program->function_count; ++i) {
        const cn_function *previous = (const cn_function *)cn_name_map_lookup_value(&function_names, ctx->module->program->functions[i]->name);
        if (previous != NULL) {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3001",
                ctx->module->program->functions[i]->offset,
                "duplicate function '%.*s'",
                (int)ctx->module->program->functions[i]->name.length,
                ctx->module->program->functions[i]->name.data
            );
            continue;
        }

        cn_name_map_insert(ctx, &function_names, ctx->module->program->functions[i]->name, ctx->module->program->functions[i], 0, false);
    }

    for (size_t i = 0; i < ctx->module->program->import_count; ++i) {
        if (ctx->module->imports[i] == NULL) {
            continue;
        }

        const cn_import_decl *previous_import = (const cn_import_decl *)cn_name_map_lookup_value(&import_aliases, ctx->module->program->imports[i].alias);
        if (previous_import != NULL) {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3016",
                ctx->module->program->imports[i].offset,
                "duplicate import alias '%.*s'",
                (int)ctx->module->program->imports[i].alias.length,
                ctx->module->program->imports[i].alias.data
            );
        } else {
            cn_name_map_insert(ctx, &import_aliases, ctx->module->program->imports[i].alias, &ctx->module->program->imports[i], 0, false);
        }

        const cn_const_decl *const_decl = (const cn_const_decl *)cn_name_map_lookup_value(&const_names, ctx->module->program->imports[i].alias);
        if (const_decl != NULL) {
            cn_emit_top_level_name_conflict(
                ctx,
                const_decl->offset,
                "constant",
                const_decl->name,
                "import alias"
            );
        }

        const cn_function *function_decl = (const cn_function *)cn_name_map_lookup_value(&function_names, ctx->module->program->imports[i].alias);
        if (function_decl != NULL) {
            cn_emit_top_level_name_conflict(
                ctx,
                function_decl->offset,
                "function",
                function_decl->name,
                "import alias"
            );
        }
    }

    for (size_t i = 0; i < ctx->module->program->function_count; ++i) {
        const cn_const_decl *const_decl = (const cn_const_decl *)cn_name_map_lookup_value(&const_names, ctx->module->program->functions[i]->name);
        if (const_decl != NULL) {
            cn_emit_top_level_name_conflict(
                ctx,
                const_decl->offset,
                "constant",
                const_decl->name,
                "function"
            );
        }
    }

    cn_name_map_release(ctx, &import_aliases);
    cn_name_map_release(ctx, &const_names);
    cn_name_map_release(ctx, &struct_names);
    cn_name_map_release(ctx, &function_names);
    return !cn_diag_has_error(ctx->diagnostics);
}

static bool cn_check_function_signature(cn_sema_ctx *ctx, const cn_function *function) {
    bool return_type_valid = cn_validate_type_ref(ctx, function->return_type, function->offset);
    if (function->is_public && return_type_valid && !cn_type_is_exportable(ctx, function->return_type)) {
        cn_emit_private_type_in_public_api(ctx, function->offset, "function", function->name, function->return_type);
    }

    for (size_t i = 0; i < function->parameters.count; ++i) {
        bool param_type_valid = cn_validate_type_ref(ctx, function->parameters.items[i].type, function->parameters.items[i].offset);
        if (function->is_public && param_type_valid && !cn_type_is_exportable(ctx, function->parameters.items[i].type)) {
            cn_emit_private_type_in_public_api(
                ctx,
                function->parameters.items[i].offset,
                "function",
                function->name,
                function->parameters.items[i].type
            );
        }
    }

    return !cn_diag_has_error(ctx->diagnostics);
}

static bool cn_check_function(cn_sema_ctx *ctx, const cn_function *function) {
    cn_scope root_scope = {0};
    ctx->current_function = function;

    if (function->body == NULL) {
        if (ctx->module != NULL && ctx->module->is_builtin_stdlib && function->is_builtin) {
            return true;
        }

        cn_diag_emit(
            ctx->diagnostics,
            CN_DIAG_ERROR,
            "E3020",
            function->offset,
            "semantic analysis expected a function body for '%.*s'",
            (int)function->name.length,
            function->name.data
        );
        return false;
    }

    for (size_t i = 0; i < function->parameters.count; ++i) {
        cn_scope_define(ctx, &root_scope, function->parameters.items[i].name, function->parameters.items[i].type, false, 0, function->parameters.items[i].offset);
    }

    cn_check_block(ctx, &root_scope, function->body, false);

    if (!cn_type_equal(function->return_type, cn_builtin_type(CN_TYPE_VOID))) {
        if (!cn_block_guarantees_return(function->body)) {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3007",
                function->offset,
                "non-void function '%.*s' must return explicitly on every path",
                (int)function->name.length,
                function->name.data
            );
        }
    }

    cn_scope_release(ctx, &root_scope);
    return !cn_diag_has_error(ctx->diagnostics);
}

bool cn_sema_check_project(cn_project *project, cn_diag_bag *diagnostics) {
    cn_sema_ctx ctx = {0};
    ctx.project = project;
    ctx.module = NULL;
    ctx.diagnostics = diagnostics;
    ctx.allocator = project->allocator;
    ctx.current_function = NULL;
    ctx.temp_types = NULL;
    ctx.module_caches = NULL;

    for (size_t module_index = 0; module_index < project->module_count; ++module_index) {
        ctx.module = project->modules[module_index];
        if (ctx.module->program == NULL) {
            continue;
        }

        cn_diag_bag_set_source(diagnostics, &ctx.module->source);
        cn_check_module_header(&ctx);

        for (size_t i = 0; i < ctx.module->program->struct_count; ++i) {
            cn_check_struct_decl(&ctx, ctx.module->program->structs[i]);
        }

        for (size_t i = 0; i < ctx.module->program->function_count; ++i) {
            cn_check_function_signature(&ctx, ctx.module->program->functions[i]);
        }
    }

    for (size_t module_index = 0; module_index < project->module_count; ++module_index) {
        ctx.module = project->modules[module_index];
        if (ctx.module->program == NULL) {
            continue;
        }

        cn_diag_bag_set_source(diagnostics, &ctx.module->source);

        for (size_t i = 0; i < ctx.module->program->const_count; ++i) {
            cn_check_const_decl(&ctx, ctx.module, ctx.module->program->consts[i]);
        }
    }

    for (size_t module_index = 0; module_index < project->module_count; ++module_index) {
        ctx.module = project->modules[module_index];
        if (ctx.module->program == NULL) {
            continue;
        }

        cn_diag_bag_set_source(diagnostics, &ctx.module->source);

        for (size_t i = 0; i < ctx.module->program->function_count; ++i) {
            cn_check_function(&ctx, ctx.module->program->functions[i]);
        }
    }

    cn_temp_types_release(&ctx);
    cn_release_module_symbol_caches(&ctx);
    return !cn_diag_has_error(diagnostics);
}
