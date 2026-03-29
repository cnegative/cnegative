#include "cnegative/ir.h"

typedef struct cn_ir_binding {
    cn_strview name;
    const cn_ir_type *type;
    bool is_mutable;
    struct cn_ir_binding *next;
} cn_ir_binding;

typedef struct cn_ir_scope {
    cn_ir_binding *bindings;
    struct cn_ir_scope *parent;
} cn_ir_scope;

typedef struct cn_ir_resolved_struct {
    const cn_module *module;
    const cn_struct_decl *decl;
} cn_ir_resolved_struct;

typedef struct cn_ir_resolved_const {
    const cn_module *module;
    const cn_const_decl *decl;
} cn_ir_resolved_const;

typedef struct cn_ir_lower_ctx {
    cn_allocator *allocator;
    const cn_project *project;
    const cn_module *module;
    cn_diag_bag *diagnostics;
} cn_ir_lower_ctx;

static void cn_ir_scope_release(cn_ir_lower_ctx *ctx, cn_ir_scope *scope) {
    cn_ir_binding *binding = scope->bindings;
    while (binding != NULL) {
        cn_ir_binding *next = binding->next;
        CN_FREE(ctx->allocator, binding);
        binding = next;
    }
    scope->bindings = NULL;
}

static const cn_ir_binding *cn_ir_scope_lookup(const cn_ir_scope *scope, cn_strview name) {
    for (const cn_ir_scope *cursor = scope; cursor != NULL; cursor = cursor->parent) {
        for (const cn_ir_binding *binding = cursor->bindings; binding != NULL; binding = binding->next) {
            if (cn_sv_eq(binding->name, name)) {
                return binding;
            }
        }
    }
    return NULL;
}

static bool cn_ir_scope_define(cn_ir_lower_ctx *ctx, cn_ir_scope *scope, cn_strview name, const cn_ir_type *type, bool is_mutable) {
    for (const cn_ir_binding *binding = scope->bindings; binding != NULL; binding = binding->next) {
        if (cn_sv_eq(binding->name, name)) {
            cn_diag_emit(
                ctx->diagnostics,
                CN_DIAG_ERROR,
                "E3020",
                0,
                "typed IR lowering hit a duplicate binding invariant for '%.*s'",
                (int)name.length,
                name.data
            );
            return false;
        }
    }

    cn_ir_binding *binding = CN_ALLOC(ctx->allocator, cn_ir_binding);
    binding->name = name;
    binding->type = type;
    binding->is_mutable = is_mutable;
    binding->next = scope->bindings;
    scope->bindings = binding;
    return true;
}

static const cn_function *cn_ir_find_local_function(const cn_module *module, cn_strview name) {
    for (size_t i = 0; i < module->program->function_count; ++i) {
        if (cn_sv_eq(module->program->functions[i]->name, name)) {
            return module->program->functions[i];
        }
    }
    return NULL;
}

static const cn_function *cn_ir_find_public_function(const cn_module *module, cn_strview name) {
    for (size_t i = 0; i < module->program->function_count; ++i) {
        if (module->program->functions[i]->is_public && cn_sv_eq(module->program->functions[i]->name, name)) {
            return module->program->functions[i];
        }
    }
    return NULL;
}

static const cn_const_decl *cn_ir_find_const(const cn_module *module, cn_strview name) {
    for (size_t i = 0; i < module->program->const_count; ++i) {
        if (cn_sv_eq(module->program->consts[i]->name, name)) {
            return module->program->consts[i];
        }
    }
    return NULL;
}

static const cn_const_decl *cn_ir_find_public_const(const cn_module *module, cn_strview name) {
    for (size_t i = 0; i < module->program->const_count; ++i) {
        if (module->program->consts[i]->is_public && cn_sv_eq(module->program->consts[i]->name, name)) {
            return module->program->consts[i];
        }
    }
    return NULL;
}

static const cn_struct_decl *cn_ir_find_struct(const cn_module *module, cn_strview name) {
    for (size_t i = 0; i < module->program->struct_count; ++i) {
        if (cn_sv_eq(module->program->structs[i]->name, name)) {
            return module->program->structs[i];
        }
    }
    return NULL;
}

static const cn_struct_decl *cn_ir_find_public_struct(const cn_module *module, cn_strview name) {
    for (size_t i = 0; i < module->program->struct_count; ++i) {
        if (module->program->structs[i]->is_public && cn_sv_eq(module->program->structs[i]->name, name)) {
            return module->program->structs[i];
        }
    }
    return NULL;
}

static const cn_struct_field *cn_ir_find_struct_field(const cn_struct_decl *struct_decl, cn_strview field_name) {
    for (size_t i = 0; i < struct_decl->fields.count; ++i) {
        if (cn_sv_eq(struct_decl->fields.items[i].name, field_name)) {
            return &struct_decl->fields.items[i];
        }
    }
    return NULL;
}

static const cn_import_decl *cn_ir_find_import_decl(const cn_module *module, cn_strview name, size_t *out_index) {
    for (size_t i = 0; i < module->program->import_count; ++i) {
        if (cn_sv_eq(module->program->imports[i].alias, name) || cn_sv_eq(module->program->imports[i].module_name, name)) {
            if (out_index != NULL) {
                *out_index = i;
            }
            return &module->program->imports[i];
        }
    }
    return NULL;
}

static const cn_module *cn_ir_find_imported_module(const cn_module *module, cn_strview name) {
    size_t index = 0;
    if (cn_ir_find_import_decl(module, name, &index) == NULL) {
        return NULL;
    }

    if (index >= module->import_count) {
        return NULL;
    }

    return module->imports[index];
}

static const cn_module *cn_ir_resolve_source_named_module(cn_ir_lower_ctx *ctx, cn_strview module_name) {
    if (module_name.length == 0 || cn_sv_eq_cstr(module_name, ctx->module->name)) {
        return ctx->module;
    }

    return cn_ir_find_imported_module(ctx->module, module_name);
}

static const cn_module *cn_ir_resolve_canonical_named_module(cn_ir_lower_ctx *ctx, cn_strview module_name) {
    if (module_name.length == 0 || cn_sv_eq_cstr(module_name, ctx->module->name)) {
        return ctx->module;
    }

    const cn_module *project_module = cn_project_find_module_by_name(ctx->project, module_name);
    if (project_module != NULL) {
        return project_module;
    }

    return cn_ir_find_imported_module(ctx->module, module_name);
}

static cn_ir_resolved_const cn_ir_resolve_const_in_module(const cn_module *module, cn_strview const_name) {
    cn_ir_resolved_const result;
    result.module = NULL;
    result.decl = NULL;

    if (module == NULL || module->program == NULL) {
        return result;
    }

    result.module = module;
    result.decl = cn_ir_find_const(module, const_name);
    if (result.decl == NULL) {
        result.module = NULL;
    }
    return result;
}

static cn_ir_resolved_const cn_ir_resolve_source_named_const(cn_ir_lower_ctx *ctx, cn_strview module_name, cn_strview const_name) {
    const cn_module *module = cn_ir_resolve_source_named_module(ctx, module_name);
    if (module == NULL) {
        return cn_ir_resolve_const_in_module(NULL, const_name);
    }

    if (module == ctx->module) {
        return cn_ir_resolve_const_in_module(module, const_name);
    }

    cn_ir_resolved_const result;
    result.module = module;
    result.decl = cn_ir_find_public_const(module, const_name);
    if (result.decl == NULL) {
        result.module = NULL;
    }
    return result;
}

static cn_ir_resolved_struct cn_ir_resolve_struct_in_module(const cn_module *module, cn_strview type_name) {
    cn_ir_resolved_struct result;
    result.module = NULL;
    result.decl = NULL;

    if (module == NULL || module->program == NULL) {
        return result;
    }

    result.module = module;
    result.decl = cn_ir_find_struct(module, type_name);
    if (result.decl == NULL) {
        result.module = NULL;
    }
    return result;
}

static cn_ir_resolved_struct cn_ir_resolve_source_named_struct(cn_ir_lower_ctx *ctx, cn_strview module_name, cn_strview type_name) {
    const cn_module *module = cn_ir_resolve_source_named_module(ctx, module_name);
    if (module == NULL) {
        return cn_ir_resolve_struct_in_module(NULL, type_name);
    }

    if (module == ctx->module) {
        return cn_ir_resolve_struct_in_module(module, type_name);
    }

    cn_ir_resolved_struct result;
    result.module = module;
    result.decl = cn_ir_find_public_struct(module, type_name);
    if (result.decl == NULL) {
        result.module = NULL;
    }
    return result;
}

static cn_ir_resolved_struct cn_ir_resolve_canonical_named_struct(cn_ir_lower_ctx *ctx, cn_strview module_name, cn_strview type_name) {
    return cn_ir_resolve_struct_in_module(cn_ir_resolve_canonical_named_module(ctx, module_name), type_name);
}

static void cn_ir_emit_internal_error(cn_ir_lower_ctx *ctx, size_t offset, const char *message) {
    cn_diag_emit(ctx->diagnostics, CN_DIAG_ERROR, "E3020", offset, "%s", message);
}

static cn_ir_type *cn_ir_make_builtin_type(cn_allocator *allocator, cn_ir_type_kind kind) {
    return cn_ir_type_create(allocator, kind, cn_sv_from_parts(NULL, 0), cn_sv_from_parts(NULL, 0), NULL, 0);
}

static cn_ir_type *cn_ir_make_ptr_type(cn_allocator *allocator, const cn_ir_type *inner) {
    return cn_ir_type_create(
        allocator,
        CN_IR_TYPE_PTR,
        cn_sv_from_parts(NULL, 0),
        cn_sv_from_parts(NULL, 0),
        cn_ir_type_clone(allocator, inner),
        0
    );
}

static cn_ir_type *cn_ir_make_result_type(cn_allocator *allocator, const cn_ir_type *inner) {
    return cn_ir_type_create(
        allocator,
        CN_IR_TYPE_RESULT,
        cn_sv_from_parts(NULL, 0),
        cn_sv_from_parts(NULL, 0),
        cn_ir_type_clone(allocator, inner),
        0
    );
}

static cn_ir_unary_op cn_ir_unary_from_ast(cn_unary_op op) {
    switch (op) {
    case CN_UNARY_NEGATE: return CN_IR_UNARY_NEGATE;
    case CN_UNARY_NOT: return CN_IR_UNARY_NOT;
    }

    return CN_IR_UNARY_NEGATE;
}

static cn_ir_binary_op cn_ir_binary_from_ast(cn_binary_op op) {
    switch (op) {
    case CN_BINARY_ADD: return CN_IR_BINARY_ADD;
    case CN_BINARY_SUB: return CN_IR_BINARY_SUB;
    case CN_BINARY_MUL: return CN_IR_BINARY_MUL;
    case CN_BINARY_DIV: return CN_IR_BINARY_DIV;
    case CN_BINARY_EQUAL: return CN_IR_BINARY_EQUAL;
    case CN_BINARY_NOT_EQUAL: return CN_IR_BINARY_NOT_EQUAL;
    case CN_BINARY_LESS: return CN_IR_BINARY_LESS;
    case CN_BINARY_LESS_EQUAL: return CN_IR_BINARY_LESS_EQUAL;
    case CN_BINARY_GREATER: return CN_IR_BINARY_GREATER;
    case CN_BINARY_GREATER_EQUAL: return CN_IR_BINARY_GREATER_EQUAL;
    case CN_BINARY_AND: return CN_IR_BINARY_AND;
    case CN_BINARY_OR: return CN_IR_BINARY_OR;
    }

    return CN_IR_BINARY_ADD;
}

static cn_ir_expr *cn_ir_lower_expression(cn_ir_lower_ctx *ctx, cn_ir_scope *scope, const cn_expr *expression, const cn_ir_type *expected);
static cn_ir_expr *cn_ir_lower_const_use(
    cn_ir_lower_ctx *ctx,
    const cn_module *module,
    const cn_const_decl *const_decl,
    const cn_ir_type *expected
);
static cn_ir_block *cn_ir_lower_block(
    cn_ir_lower_ctx *ctx,
    cn_ir_scope *parent,
    const cn_block *block,
    bool creates_scope,
    const cn_ir_type *function_return_type
);

static cn_ir_expr *cn_ir_lower_const_use(
    cn_ir_lower_ctx *ctx,
    const cn_module *module,
    const cn_const_decl *const_decl,
    const cn_ir_type *expected
) {
    const cn_module *previous_module = ctx->module;
    cn_ir_expr *expression = NULL;

    ctx->module = module;
    expression = cn_ir_lower_expression(ctx, NULL, const_decl->initializer, expected);
    ctx->module = previous_module;
    return expression;
}

static cn_ir_expr *cn_ir_lower_with_builtin_expected(
    cn_ir_lower_ctx *ctx,
    cn_ir_scope *scope,
    const cn_expr *expression,
    cn_ir_type_kind expected_kind
) {
    cn_ir_type *expected = cn_ir_make_builtin_type(ctx->allocator, expected_kind);
    cn_ir_expr *result = cn_ir_lower_expression(ctx, scope, expression, expected);
    cn_ir_type_destroy(ctx->allocator, expected);
    return result;
}

static cn_ir_expr *cn_ir_lower_array_literal(cn_ir_lower_ctx *ctx, cn_ir_scope *scope, const cn_expr *expression, const cn_ir_type *expected) {
    cn_ir_expr *ir_expression = cn_ir_expr_create(ctx->allocator, CN_IR_EXPR_ARRAY_LITERAL, expression->offset);
    ir_expression->data.array_literal.items.items = NULL;
    ir_expression->data.array_literal.items.count = 0;
    ir_expression->data.array_literal.items.capacity = 0;

    const cn_ir_type *element_expected = NULL;
    if (expected != NULL && expected->kind == CN_IR_TYPE_ARRAY) {
        element_expected = expected->inner;
    }

    for (size_t i = 0; i < expression->data.array_literal.items.count; ++i) {
        cn_ir_expr *item = cn_ir_lower_expression(ctx, scope, expression->data.array_literal.items.items[i], element_expected);
        if (item == NULL) {
            return ir_expression;
        }
        cn_ir_expr_list_push(ctx->allocator, &ir_expression->data.array_literal.items, item);
    }

    if (expected != NULL && expected->kind == CN_IR_TYPE_ARRAY) {
        ir_expression->type = cn_ir_type_clone(ctx->allocator, expected);
        return ir_expression;
    }

    cn_ir_type *element_type = NULL;
    if (ir_expression->data.array_literal.items.count > 0) {
        element_type = cn_ir_type_clone(ctx->allocator, ir_expression->data.array_literal.items.items[0]->type);
    } else {
        element_type = cn_ir_make_builtin_type(ctx->allocator, CN_IR_TYPE_UNKNOWN);
    }

    ir_expression->type = cn_ir_type_create(
        ctx->allocator,
        CN_IR_TYPE_ARRAY,
        cn_sv_from_parts(NULL, 0),
        cn_sv_from_parts(NULL, 0),
        element_type,
        ir_expression->data.array_literal.items.count
    );
    return ir_expression;
}

static cn_ir_expr *cn_ir_lower_struct_literal(cn_ir_lower_ctx *ctx, cn_ir_scope *scope, const cn_expr *expression) {
    cn_ir_resolved_struct resolved = cn_ir_resolve_source_named_struct(
        ctx,
        expression->data.struct_literal.module_name,
        expression->data.struct_literal.type_name
    );

    if (resolved.decl == NULL) {
        cn_ir_emit_internal_error(ctx, expression->offset, "typed IR lowering could not resolve a checked struct literal");
        return NULL;
    }

    cn_ir_expr *ir_expression = cn_ir_expr_create(ctx->allocator, CN_IR_EXPR_STRUCT_LITERAL, expression->offset);
    ir_expression->data.struct_literal.module_name = cn_sv_from_cstr(resolved.module->name);
    ir_expression->data.struct_literal.type_name = resolved.decl->name;
    ir_expression->data.struct_literal.fields.items = NULL;
    ir_expression->data.struct_literal.fields.count = 0;
    ir_expression->data.struct_literal.fields.capacity = 0;

    for (size_t i = 0; i < expression->data.struct_literal.fields.count; ++i) {
        const cn_field_init *field_init = &expression->data.struct_literal.fields.items[i];
        const cn_struct_field *field = cn_ir_find_struct_field(resolved.decl, field_init->name);
        if (field == NULL) {
            cn_ir_emit_internal_error(ctx, field_init->offset, "typed IR lowering lost a checked struct field");
            return ir_expression;
        }

        cn_ir_type *field_expected = cn_ir_type_from_ast(ctx->allocator, field->type);
        cn_ir_expr *value = cn_ir_lower_expression(ctx, scope, field_init->value, field_expected);
        cn_ir_type_destroy(ctx->allocator, field_expected);
        if (value == NULL) {
            return ir_expression;
        }

        cn_ir_field_init ir_field;
        ir_field.name = field_init->name;
        ir_field.value = value;
        cn_ir_field_init_list_push(ctx->allocator, &ir_expression->data.struct_literal.fields, ir_field);
    }

    ir_expression->type = cn_ir_type_create(
        ctx->allocator,
        CN_IR_TYPE_NAMED,
        cn_sv_from_cstr(resolved.module->name),
        resolved.decl->name,
        NULL,
        0
    );
    return ir_expression;
}

static cn_ir_expr *cn_ir_lower_field_access(cn_ir_lower_ctx *ctx, cn_ir_scope *scope, const cn_expr *expression) {
    if (expression->data.field.base->kind == CN_EXPR_NAME) {
        cn_ir_resolved_const resolved_const = cn_ir_resolve_source_named_const(
            ctx,
            expression->data.field.base->data.name,
            expression->data.field.field_name
        );
        if (resolved_const.decl != NULL) {
            return cn_ir_lower_const_use(ctx, resolved_const.module, resolved_const.decl, NULL);
        }
    }

    cn_ir_expr *base = cn_ir_lower_expression(ctx, scope, expression->data.field.base, NULL);
    if (base == NULL) {
        return NULL;
    }

    cn_ir_expr *ir_expression = cn_ir_expr_create(ctx->allocator, CN_IR_EXPR_FIELD, expression->offset);
    ir_expression->data.field.base = base;
    ir_expression->data.field.field_name = expression->data.field.field_name;

    if (base->type->kind == CN_IR_TYPE_PTR) {
        ir_expression->type = cn_ir_type_clone(ctx->allocator, base->type->inner);
        return ir_expression;
    }

    if (base->type->kind == CN_IR_TYPE_RESULT) {
        if (cn_sv_eq_cstr(expression->data.field.field_name, "ok")) {
            ir_expression->type = cn_ir_make_builtin_type(ctx->allocator, CN_IR_TYPE_BOOL);
        } else {
            ir_expression->type = cn_ir_type_clone(ctx->allocator, base->type->inner);
        }
        return ir_expression;
    }

    if (base->type->kind != CN_IR_TYPE_NAMED) {
        cn_ir_emit_internal_error(ctx, expression->offset, "typed IR lowering expected a named struct type for field access");
        ir_expression->type = cn_ir_make_builtin_type(ctx->allocator, CN_IR_TYPE_UNKNOWN);
        return ir_expression;
    }

    cn_ir_resolved_struct resolved = cn_ir_resolve_canonical_named_struct(ctx, base->type->module_name, base->type->name);
    if (resolved.decl == NULL) {
        cn_ir_emit_internal_error(ctx, expression->offset, "typed IR lowering lost a checked named struct type");
        ir_expression->type = cn_ir_make_builtin_type(ctx->allocator, CN_IR_TYPE_UNKNOWN);
        return ir_expression;
    }

    const cn_struct_field *field = cn_ir_find_struct_field(resolved.decl, expression->data.field.field_name);
    if (field == NULL) {
        cn_ir_emit_internal_error(ctx, expression->offset, "typed IR lowering lost a checked struct field access");
        ir_expression->type = cn_ir_make_builtin_type(ctx->allocator, CN_IR_TYPE_UNKNOWN);
        return ir_expression;
    }

    ir_expression->type = cn_ir_type_from_ast(ctx->allocator, field->type);
    return ir_expression;
}

static cn_ir_expr *cn_ir_lower_index(cn_ir_lower_ctx *ctx, cn_ir_scope *scope, const cn_expr *expression) {
    cn_ir_expr *base = cn_ir_lower_expression(ctx, scope, expression->data.index.base, NULL);
    cn_ir_expr *index = cn_ir_lower_expression(ctx, scope, expression->data.index.index, NULL);
    if (base == NULL || index == NULL) {
        return NULL;
    }

    cn_ir_expr *ir_expression = cn_ir_expr_create(ctx->allocator, CN_IR_EXPR_INDEX, expression->offset);
    ir_expression->data.index.base = base;
    ir_expression->data.index.index = index;

    if (base->type->kind == CN_IR_TYPE_ARRAY) {
        ir_expression->type = cn_ir_type_clone(ctx->allocator, base->type->inner);
    } else {
        cn_ir_emit_internal_error(ctx, expression->offset, "typed IR lowering expected an array type for indexing");
        ir_expression->type = cn_ir_make_builtin_type(ctx->allocator, CN_IR_TYPE_UNKNOWN);
    }

    return ir_expression;
}

static bool cn_ir_lower_call_arguments(
    cn_ir_lower_ctx *ctx,
    cn_ir_scope *scope,
    cn_ir_expr *ir_expression,
    const cn_expr_list *arguments,
    const cn_param_list *parameters
) {
    for (size_t i = 0; i < arguments->count; ++i) {
        cn_ir_type *expected = NULL;
        if (parameters != NULL && i < parameters->count) {
            expected = cn_ir_type_from_ast(ctx->allocator, parameters->items[i].type);
        }

        cn_ir_expr *argument = cn_ir_lower_expression(ctx, scope, arguments->items[i], expected);
        cn_ir_type_destroy(ctx->allocator, expected);
        if (argument == NULL) {
            return false;
        }

        cn_ir_expr_list_push(ctx->allocator, &ir_expression->data.call.arguments, argument);
    }

    return true;
}

static cn_ir_expr *cn_ir_lower_call(cn_ir_lower_ctx *ctx, cn_ir_scope *scope, const cn_expr *expression) {
    cn_ir_expr *ir_expression = cn_ir_expr_create(ctx->allocator, CN_IR_EXPR_CALL, expression->offset);
    ir_expression->data.call.module_name = cn_sv_from_parts(NULL, 0);
    ir_expression->data.call.function_name = cn_sv_from_parts(NULL, 0);
    ir_expression->data.call.arguments.items = NULL;
    ir_expression->data.call.arguments.count = 0;
    ir_expression->data.call.arguments.capacity = 0;

    if (expression->data.call.callee->kind == CN_EXPR_NAME) {
        cn_strview callee = expression->data.call.callee->data.name;

        if (cn_sv_eq_cstr(callee, "print")) {
            ir_expression->data.call.target_kind = CN_IR_CALL_BUILTIN;
            ir_expression->data.call.function_name = callee;
            if (!cn_ir_lower_call_arguments(ctx, scope, ir_expression, &expression->data.call.arguments, NULL)) {
                return ir_expression;
            }
            ir_expression->type = cn_ir_make_builtin_type(ctx->allocator, CN_IR_TYPE_VOID);
            return ir_expression;
        }

        if (cn_sv_eq_cstr(callee, "input")) {
            ir_expression->data.call.target_kind = CN_IR_CALL_BUILTIN;
            ir_expression->data.call.function_name = callee;
            if (!cn_ir_lower_call_arguments(ctx, scope, ir_expression, &expression->data.call.arguments, NULL)) {
                return ir_expression;
            }
            ir_expression->type = cn_ir_make_builtin_type(ctx->allocator, CN_IR_TYPE_STR);
            return ir_expression;
        }

        if (cn_sv_eq_cstr(callee, "str_copy") || cn_sv_eq_cstr(callee, "str_concat")) {
            ir_expression->data.call.target_kind = CN_IR_CALL_BUILTIN;
            ir_expression->data.call.function_name = callee;
            if (!cn_ir_lower_call_arguments(ctx, scope, ir_expression, &expression->data.call.arguments, NULL)) {
                return ir_expression;
            }
            ir_expression->type = cn_ir_make_builtin_type(ctx->allocator, CN_IR_TYPE_STR);
            return ir_expression;
        }

        const cn_function *function = cn_ir_find_local_function(ctx->module, callee);
        if (function == NULL) {
            cn_ir_emit_internal_error(ctx, expression->offset, "typed IR lowering could not resolve a checked local function call");
            ir_expression->type = cn_ir_make_builtin_type(ctx->allocator, CN_IR_TYPE_UNKNOWN);
            return ir_expression;
        }

        ir_expression->data.call.target_kind = CN_IR_CALL_LOCAL;
        ir_expression->data.call.module_name = cn_sv_from_cstr(ctx->module->name);
        ir_expression->data.call.function_name = function->name;
        if (!cn_ir_lower_call_arguments(ctx, scope, ir_expression, &expression->data.call.arguments, &function->parameters)) {
            return ir_expression;
        }
        ir_expression->type = cn_ir_type_from_ast(ctx->allocator, function->return_type);
        return ir_expression;
    }

    if (expression->data.call.callee->kind == CN_EXPR_FIELD && expression->data.call.callee->data.field.base->kind == CN_EXPR_NAME) {
        cn_strview module_name = expression->data.call.callee->data.field.base->data.name;
        cn_strview function_name = expression->data.call.callee->data.field.field_name;
        const cn_module *imported = cn_ir_find_imported_module(ctx->module, module_name);
        if (imported == NULL) {
            cn_ir_emit_internal_error(ctx, expression->offset, "typed IR lowering could not resolve a checked imported module call");
            ir_expression->type = cn_ir_make_builtin_type(ctx->allocator, CN_IR_TYPE_UNKNOWN);
            return ir_expression;
        }

        const cn_function *function = cn_ir_find_public_function(imported, function_name);
        if (function == NULL) {
            cn_ir_emit_internal_error(ctx, expression->offset, "typed IR lowering could not resolve a checked public module function");
            ir_expression->type = cn_ir_make_builtin_type(ctx->allocator, CN_IR_TYPE_UNKNOWN);
            return ir_expression;
        }

        ir_expression->data.call.target_kind = CN_IR_CALL_MODULE;
        ir_expression->data.call.module_name = cn_sv_from_cstr(imported->name);
        ir_expression->data.call.function_name = function->name;
        if (!cn_ir_lower_call_arguments(ctx, scope, ir_expression, &expression->data.call.arguments, &function->parameters)) {
            return ir_expression;
        }
        ir_expression->type = cn_ir_type_from_ast(ctx->allocator, function->return_type);
        return ir_expression;
    }

    cn_ir_emit_internal_error(ctx, expression->offset, "typed IR lowering expected a resolved call target");
    ir_expression->type = cn_ir_make_builtin_type(ctx->allocator, CN_IR_TYPE_UNKNOWN);
    return ir_expression;
}

static cn_ir_expr *cn_ir_lower_expression(cn_ir_lower_ctx *ctx, cn_ir_scope *scope, const cn_expr *expression, const cn_ir_type *expected) {
    cn_ir_expr *ir_expression = NULL;

    switch (expression->kind) {
    case CN_EXPR_INT:
        ir_expression = cn_ir_expr_create(ctx->allocator, CN_IR_EXPR_INT, expression->offset);
        ir_expression->data.int_value = expression->data.int_value;
        ir_expression->type = cn_ir_make_builtin_type(ctx->allocator, CN_IR_TYPE_INT);
        return ir_expression;
    case CN_EXPR_BOOL:
        ir_expression = cn_ir_expr_create(ctx->allocator, CN_IR_EXPR_BOOL, expression->offset);
        ir_expression->data.bool_value = expression->data.bool_value;
        ir_expression->type = cn_ir_make_builtin_type(ctx->allocator, CN_IR_TYPE_BOOL);
        return ir_expression;
    case CN_EXPR_STRING:
        ir_expression = cn_ir_expr_create(ctx->allocator, CN_IR_EXPR_STRING, expression->offset);
        ir_expression->data.string_value = expression->data.string_value;
        ir_expression->type = cn_ir_make_builtin_type(ctx->allocator, CN_IR_TYPE_STR);
        return ir_expression;
    case CN_EXPR_NAME: {
        const cn_ir_binding *binding = cn_ir_scope_lookup(scope, expression->data.name);
        if (binding != NULL) {
            ir_expression = cn_ir_expr_create(ctx->allocator, CN_IR_EXPR_LOCAL, expression->offset);
            ir_expression->data.local_name = expression->data.name;
            ir_expression->type = cn_ir_type_clone(ctx->allocator, binding->type);
            return ir_expression;
        }

        cn_ir_resolved_const resolved_const = cn_ir_resolve_const_in_module(ctx->module, expression->data.name);
        if (resolved_const.decl != NULL) {
            return cn_ir_lower_const_use(ctx, resolved_const.module, resolved_const.decl, expected);
        }

        cn_ir_emit_internal_error(ctx, expression->offset, "typed IR lowering could not resolve a checked local or constant name");
        return NULL;
    }
    case CN_EXPR_UNARY: {
        cn_ir_expr *operand = cn_ir_lower_expression(ctx, scope, expression->data.unary.operand, NULL);
        if (operand == NULL) {
            return NULL;
        }

        ir_expression = cn_ir_expr_create(ctx->allocator, CN_IR_EXPR_UNARY, expression->offset);
        ir_expression->data.unary.op = cn_ir_unary_from_ast(expression->data.unary.op);
        ir_expression->data.unary.operand = operand;
        ir_expression->type = cn_ir_make_builtin_type(
            ctx->allocator,
            expression->data.unary.op == CN_UNARY_NEGATE ? CN_IR_TYPE_INT : CN_IR_TYPE_BOOL
        );
        return ir_expression;
    }
    case CN_EXPR_BINARY: {
        cn_ir_expr *left = cn_ir_lower_expression(ctx, scope, expression->data.binary.left, NULL);
        cn_ir_expr *right = cn_ir_lower_expression(ctx, scope, expression->data.binary.right, NULL);
        if (left == NULL || right == NULL) {
            return NULL;
        }

        ir_expression = cn_ir_expr_create(ctx->allocator, CN_IR_EXPR_BINARY, expression->offset);
        ir_expression->data.binary.op = cn_ir_binary_from_ast(expression->data.binary.op);
        ir_expression->data.binary.left = left;
        ir_expression->data.binary.right = right;

        switch (expression->data.binary.op) {
        case CN_BINARY_ADD:
        case CN_BINARY_SUB:
        case CN_BINARY_MUL:
        case CN_BINARY_DIV:
            ir_expression->type = cn_ir_make_builtin_type(ctx->allocator, CN_IR_TYPE_INT);
            break;
        case CN_BINARY_EQUAL:
        case CN_BINARY_NOT_EQUAL:
        case CN_BINARY_LESS:
        case CN_BINARY_LESS_EQUAL:
        case CN_BINARY_GREATER:
        case CN_BINARY_GREATER_EQUAL:
        case CN_BINARY_AND:
        case CN_BINARY_OR:
            ir_expression->type = cn_ir_make_builtin_type(ctx->allocator, CN_IR_TYPE_BOOL);
            break;
        }
        return ir_expression;
    }
    case CN_EXPR_CALL:
        return cn_ir_lower_call(ctx, scope, expression);
    case CN_EXPR_ARRAY_LITERAL:
        return cn_ir_lower_array_literal(ctx, scope, expression, expected);
    case CN_EXPR_INDEX:
        return cn_ir_lower_index(ctx, scope, expression);
    case CN_EXPR_FIELD:
        return cn_ir_lower_field_access(ctx, scope, expression);
    case CN_EXPR_STRUCT_LITERAL:
        return cn_ir_lower_struct_literal(ctx, scope, expression);
    case CN_EXPR_OK: {
        const cn_ir_type *value_expected = NULL;
        ir_expression = cn_ir_expr_create(ctx->allocator, CN_IR_EXPR_OK, expression->offset);
        if (expected != NULL && expected->kind == CN_IR_TYPE_RESULT) {
            value_expected = expected->inner;
        }

        ir_expression->data.ok_expr.value = cn_ir_lower_expression(ctx, scope, expression->data.ok_expr.value, value_expected);
        if (ir_expression->data.ok_expr.value == NULL) {
            return ir_expression;
        }

        if (expected != NULL && expected->kind == CN_IR_TYPE_RESULT) {
            ir_expression->type = cn_ir_type_clone(ctx->allocator, expected);
        } else {
            ir_expression->type = cn_ir_make_result_type(ctx->allocator, ir_expression->data.ok_expr.value->type);
        }
        return ir_expression;
    }
    case CN_EXPR_ERR:
        ir_expression = cn_ir_expr_create(ctx->allocator, CN_IR_EXPR_ERR, expression->offset);
        if (expected == NULL || expected->kind != CN_IR_TYPE_RESULT) {
            cn_ir_emit_internal_error(ctx, expression->offset, "typed IR lowering expected a result type for 'err'");
            ir_expression->type = cn_ir_make_builtin_type(ctx->allocator, CN_IR_TYPE_UNKNOWN);
        } else {
            ir_expression->type = cn_ir_type_clone(ctx->allocator, expected);
        }
        return ir_expression;
    case CN_EXPR_ALLOC:
        ir_expression = cn_ir_expr_create(ctx->allocator, CN_IR_EXPR_ALLOC, expression->offset);
        ir_expression->data.alloc_expr.alloc_type = cn_ir_type_from_ast(ctx->allocator, expression->data.alloc_expr.type);
        ir_expression->type = cn_ir_make_ptr_type(ctx->allocator, ir_expression->data.alloc_expr.alloc_type);
        return ir_expression;
    case CN_EXPR_ADDR: {
        cn_ir_expr *target = cn_ir_lower_expression(ctx, scope, expression->data.addr_expr.target, NULL);
        if (target == NULL) {
            return NULL;
        }

        ir_expression = cn_ir_expr_create(ctx->allocator, CN_IR_EXPR_ADDR, expression->offset);
        ir_expression->data.addr_expr.target = target;
        ir_expression->type = cn_ir_make_ptr_type(ctx->allocator, target->type);
        return ir_expression;
    }
    case CN_EXPR_DEREF: {
        cn_ir_expr *target = cn_ir_lower_expression(ctx, scope, expression->data.deref_expr.target, NULL);
        if (target == NULL) {
            return NULL;
        }

        ir_expression = cn_ir_expr_create(ctx->allocator, CN_IR_EXPR_DEREF, expression->offset);
        ir_expression->data.deref_expr.target = target;
        if (target->type->kind != CN_IR_TYPE_PTR) {
            cn_ir_emit_internal_error(ctx, expression->offset, "typed IR lowering expected a pointer operand for deref");
            ir_expression->type = cn_ir_make_builtin_type(ctx->allocator, CN_IR_TYPE_UNKNOWN);
        } else {
            ir_expression->type = cn_ir_type_clone(ctx->allocator, target->type->inner);
        }
        return ir_expression;
    }
    }

    cn_ir_emit_internal_error(ctx, expression->offset, "typed IR lowering hit an unsupported expression kind");
    return NULL;
}

static cn_ir_stmt *cn_ir_lower_statement(
    cn_ir_lower_ctx *ctx,
    cn_ir_scope *scope,
    const cn_stmt *statement,
    const cn_ir_type *function_return_type
) {
    cn_ir_stmt *ir_statement = cn_ir_stmt_create(ctx->allocator, CN_IR_STMT_EXPR, statement->offset);

    switch (statement->kind) {
    case CN_STMT_LET:
        ir_statement->kind = CN_IR_STMT_LET;
        ir_statement->data.let_stmt.name = statement->data.let_stmt.name;
        ir_statement->data.let_stmt.is_mutable = statement->data.let_stmt.is_mutable;
        ir_statement->data.let_stmt.type = cn_ir_type_from_ast(ctx->allocator, statement->data.let_stmt.type);
        ir_statement->data.let_stmt.initializer = cn_ir_lower_expression(
            ctx,
            scope,
            statement->data.let_stmt.initializer,
            ir_statement->data.let_stmt.type
        );
        if (ir_statement->data.let_stmt.initializer == NULL) {
            return ir_statement;
        }
        cn_ir_scope_define(
            ctx,
            scope,
            ir_statement->data.let_stmt.name,
            ir_statement->data.let_stmt.type,
            ir_statement->data.let_stmt.is_mutable
        );
        return ir_statement;
    case CN_STMT_ASSIGN:
        ir_statement->kind = CN_IR_STMT_ASSIGN;
        ir_statement->data.assign_stmt.target = cn_ir_lower_expression(ctx, scope, statement->data.assign_stmt.target, NULL);
        if (ir_statement->data.assign_stmt.target == NULL) {
            return ir_statement;
        }
        ir_statement->data.assign_stmt.value = cn_ir_lower_expression(
            ctx,
            scope,
            statement->data.assign_stmt.value,
            ir_statement->data.assign_stmt.target->type
        );
        return ir_statement;
    case CN_STMT_RETURN:
        ir_statement->kind = CN_IR_STMT_RETURN;
        if (statement->data.return_stmt.value != NULL) {
            ir_statement->data.return_stmt.value = cn_ir_lower_expression(
                ctx,
                scope,
                statement->data.return_stmt.value,
                function_return_type
            );
        } else {
            ir_statement->data.return_stmt.value = NULL;
        }
        return ir_statement;
    case CN_STMT_EXPR:
        ir_statement->kind = CN_IR_STMT_EXPR;
        ir_statement->data.expr_stmt.value = cn_ir_lower_expression(ctx, scope, statement->data.expr_stmt.value, NULL);
        return ir_statement;
    case CN_STMT_IF:
        ir_statement->kind = CN_IR_STMT_IF;
        ir_statement->data.if_stmt.condition = cn_ir_lower_with_builtin_expected(
            ctx,
            scope,
            statement->data.if_stmt.condition,
            CN_IR_TYPE_BOOL
        );
        ir_statement->data.if_stmt.then_block = cn_ir_lower_block(
            ctx,
            scope,
            statement->data.if_stmt.then_block,
            true,
            function_return_type
        );
        ir_statement->data.if_stmt.else_block = NULL;
        if (statement->data.if_stmt.else_block != NULL) {
            ir_statement->data.if_stmt.else_block = cn_ir_lower_block(
                ctx,
                scope,
                statement->data.if_stmt.else_block,
                true,
                function_return_type
            );
        }
        return ir_statement;
    case CN_STMT_WHILE:
        ir_statement->kind = CN_IR_STMT_WHILE;
        ir_statement->data.while_stmt.condition = cn_ir_lower_with_builtin_expected(
            ctx,
            scope,
            statement->data.while_stmt.condition,
            CN_IR_TYPE_BOOL
        );
        ir_statement->data.while_stmt.body = cn_ir_lower_block(
            ctx,
            scope,
            statement->data.while_stmt.body,
            true,
            function_return_type
        );
        return ir_statement;
    case CN_STMT_LOOP:
        ir_statement->kind = CN_IR_STMT_LOOP;
        ir_statement->data.loop_stmt.body = cn_ir_lower_block(
            ctx,
            scope,
            statement->data.loop_stmt.body,
            true,
            function_return_type
        );
        return ir_statement;
    case CN_STMT_FOR: {
        cn_ir_scope loop_scope = {0};
        loop_scope.parent = scope;

        ir_statement->kind = CN_IR_STMT_FOR;
        ir_statement->data.for_stmt.name = statement->data.for_stmt.name;
        ir_statement->data.for_stmt.type = cn_ir_type_from_ast(ctx->allocator, statement->data.for_stmt.type);
        ir_statement->data.for_stmt.start = cn_ir_lower_with_builtin_expected(
            ctx,
            scope,
            statement->data.for_stmt.start,
            CN_IR_TYPE_INT
        );
        ir_statement->data.for_stmt.end = cn_ir_lower_with_builtin_expected(
            ctx,
            scope,
            statement->data.for_stmt.end,
            CN_IR_TYPE_INT
        );

        cn_ir_scope_define(ctx, &loop_scope, ir_statement->data.for_stmt.name, ir_statement->data.for_stmt.type, false);
        ir_statement->data.for_stmt.body = cn_ir_lower_block(
            ctx,
            &loop_scope,
            statement->data.for_stmt.body,
            true,
            function_return_type
        );
        cn_ir_scope_release(ctx, &loop_scope);
        return ir_statement;
    }
    case CN_STMT_FREE:
        ir_statement->kind = CN_IR_STMT_FREE;
        ir_statement->data.free_stmt.value = cn_ir_lower_expression(ctx, scope, statement->data.free_stmt.value, NULL);
        return ir_statement;
    }

    cn_ir_emit_internal_error(ctx, statement->offset, "typed IR lowering hit an unsupported statement kind");
    return ir_statement;
}

static cn_ir_block *cn_ir_lower_block(
    cn_ir_lower_ctx *ctx,
    cn_ir_scope *parent,
    const cn_block *block,
    bool creates_scope,
    const cn_ir_type *function_return_type
) {
    cn_ir_scope scope = {0};
    cn_ir_scope *active_scope = parent;

    if (creates_scope) {
        scope.parent = parent;
        active_scope = &scope;
    }

    cn_ir_block *ir_block = cn_ir_block_create(ctx->allocator, block->offset);
    for (size_t i = 0; i < block->statements.count; ++i) {
        cn_ir_stmt *statement = cn_ir_lower_statement(ctx, active_scope, block->statements.items[i], function_return_type);
        if (statement != NULL) {
            cn_ir_stmt_list_push(ctx->allocator, &ir_block->statements, statement);
        }
    }

    if (creates_scope) {
        cn_ir_scope_release(ctx, &scope);
    }

    return ir_block;
}

static cn_ir_const *cn_ir_lower_const(cn_ir_lower_ctx *ctx, const cn_const_decl *const_decl) {
    cn_ir_const *ir_const = cn_ir_const_create(
        ctx->allocator,
        cn_sv_from_cstr(ctx->module->name),
        const_decl->name,
        const_decl->is_public,
        const_decl->offset
    );
    ir_const->type = cn_ir_type_from_ast(ctx->allocator, const_decl->type);
    ir_const->initializer = cn_ir_lower_const_use(ctx, ctx->module, const_decl, ir_const->type);
    return ir_const;
}

static cn_ir_struct *cn_ir_lower_struct(cn_ir_lower_ctx *ctx, const cn_struct_decl *struct_decl) {
    cn_ir_struct *ir_struct = cn_ir_struct_create(
        ctx->allocator,
        cn_sv_from_cstr(ctx->module->name),
        struct_decl->name,
        struct_decl->is_public,
        struct_decl->offset
    );

    for (size_t i = 0; i < struct_decl->fields.count; ++i) {
        cn_ir_struct_field field;
        field.name = struct_decl->fields.items[i].name;
        field.type = cn_ir_type_from_ast(ctx->allocator, struct_decl->fields.items[i].type);
        field.offset = struct_decl->fields.items[i].offset;
        cn_ir_struct_field_list_push(ctx->allocator, &ir_struct->fields, field);
    }

    return ir_struct;
}

static cn_ir_function *cn_ir_lower_function(cn_ir_lower_ctx *ctx, const cn_function *function) {
    cn_ir_scope root_scope = {0};
    cn_ir_function *ir_function = cn_ir_function_create(
        ctx->allocator,
        cn_sv_from_cstr(ctx->module->name),
        function->name,
        function->is_public,
        function->offset
    );
    ir_function->return_type = cn_ir_type_from_ast(ctx->allocator, function->return_type);

    for (size_t i = 0; i < function->parameters.count; ++i) {
        cn_ir_param param;
        param.name = function->parameters.items[i].name;
        param.type = cn_ir_type_from_ast(ctx->allocator, function->parameters.items[i].type);
        param.offset = function->parameters.items[i].offset;
        cn_ir_param_list_push(ctx->allocator, &ir_function->parameters, param);
        cn_ir_scope_define(ctx, &root_scope, param.name, param.type, false);
    }

    ir_function->body = cn_ir_lower_block(ctx, &root_scope, function->body, false, ir_function->return_type);
    cn_ir_scope_release(ctx, &root_scope);
    return ir_function;
}

static cn_ir_module *cn_ir_lower_module(cn_ir_lower_ctx *ctx) {
    cn_ir_module *ir_module = cn_ir_module_create(
        ctx->allocator,
        cn_sv_from_cstr(ctx->module->name),
        cn_sv_from_cstr(ctx->module->path),
        &ctx->module->source
    );

    for (size_t i = 0; i < ctx->module->program->const_count; ++i) {
        cn_ir_module_push_const(ir_module, ctx->allocator, cn_ir_lower_const(ctx, ctx->module->program->consts[i]));
    }

    for (size_t i = 0; i < ctx->module->program->struct_count; ++i) {
        cn_ir_module_push_struct(ir_module, ctx->allocator, cn_ir_lower_struct(ctx, ctx->module->program->structs[i]));
    }

    for (size_t i = 0; i < ctx->module->program->function_count; ++i) {
        cn_ir_module_push_function(ir_module, ctx->allocator, cn_ir_lower_function(ctx, ctx->module->program->functions[i]));
    }

    return ir_module;
}

bool cn_ir_lower_project(cn_allocator *allocator, const cn_project *project, cn_diag_bag *diagnostics, cn_ir_program **out_program) {
    cn_ir_lower_ctx ctx = {0};
    ctx.allocator = allocator;
    ctx.project = project;
    ctx.module = NULL;
    ctx.diagnostics = diagnostics;

    cn_ir_program *program = cn_ir_program_create(allocator);
    for (size_t module_index = 0; module_index < project->module_count; ++module_index) {
        ctx.module = project->modules[module_index];
        if (ctx.module->program == NULL) {
            continue;
        }

        cn_diag_bag_set_source(diagnostics, &ctx.module->source);
        cn_ir_program_push_module(program, cn_ir_lower_module(&ctx));
        if (cn_diag_has_error(diagnostics)) {
            cn_ir_program_destroy(allocator, program);
            *out_program = NULL;
            return false;
        }
    }

    *out_program = program;
    return true;
}
