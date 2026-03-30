#include "cnegative/ast.h"

#include <stdio.h>

static void cn_type_destroy(cn_allocator *allocator, cn_type_ref *type);
static void cn_expr_destroy(cn_allocator *allocator, cn_expr *expression);
static void cn_stmt_destroy(cn_allocator *allocator, cn_stmt *statement);
static void cn_block_destroy(cn_allocator *allocator, cn_block *block);
static void cn_function_destroy(cn_allocator *allocator, cn_function *function);
static void cn_const_decl_destroy(cn_allocator *allocator, cn_const_decl *const_decl);
static void cn_struct_decl_destroy(cn_allocator *allocator, cn_struct_decl *struct_decl);

static void *cn_grow_items(cn_allocator *allocator, void *items, size_t item_size, size_t *capacity, size_t required) {
    if (*capacity >= required) {
        return items;
    }

    size_t new_capacity = *capacity == 0 ? 8 : (*capacity * 2);
    while (new_capacity < required) {
        new_capacity *= 2;
    }

    *capacity = new_capacity;
    return cn_realloc_impl(allocator, items, item_size * new_capacity, __FILE__, __LINE__);
}

cn_program *cn_program_create(cn_allocator *allocator) {
    cn_program *program = CN_ALLOC(allocator, cn_program);
    program->allocator = allocator;
    program->imports = NULL;
    program->import_count = 0;
    program->import_capacity = 0;
    program->consts = NULL;
    program->const_count = 0;
    program->const_capacity = 0;
    program->structs = NULL;
    program->struct_count = 0;
    program->struct_capacity = 0;
    program->functions = NULL;
    program->function_count = 0;
    program->function_capacity = 0;
    return program;
}

void cn_program_destroy(cn_allocator *allocator, cn_program *program) {
    if (program == NULL) {
        return;
    }

    for (size_t i = 0; i < program->import_count; ++i) {
        CN_FREE(allocator, program->imports[i].owned_module_name);
    }

    for (size_t i = 0; i < program->struct_count; ++i) {
        cn_struct_decl_destroy(allocator, program->structs[i]);
    }

    for (size_t i = 0; i < program->const_count; ++i) {
        cn_const_decl_destroy(allocator, program->consts[i]);
    }

    for (size_t i = 0; i < program->function_count; ++i) {
        cn_function_destroy(allocator, program->functions[i]);
    }

    CN_FREE(allocator, program->imports);
    CN_FREE(allocator, program->consts);
    CN_FREE(allocator, program->structs);
    CN_FREE(allocator, program->functions);
    CN_FREE(allocator, program);
}

cn_type_ref *cn_type_create(cn_allocator *allocator, cn_type_kind kind, cn_strview name, cn_type_ref *inner, size_t offset) {
    cn_type_ref *type = CN_ALLOC(allocator, cn_type_ref);
    type->kind = kind;
    type->module_name = cn_sv_from_parts(NULL, 0);
    type->name = name;
    type->inner = inner;
    type->array_size = 0;
    type->offset = offset;
    return type;
}

cn_expr *cn_expr_create(cn_allocator *allocator, cn_expr_kind kind, size_t offset) {
    cn_expr *expression = CN_ALLOC(allocator, cn_expr);
    expression->kind = kind;
    expression->offset = offset;
    return expression;
}

cn_stmt *cn_stmt_create(cn_allocator *allocator, cn_stmt_kind kind, size_t offset) {
    cn_stmt *statement = CN_ALLOC(allocator, cn_stmt);
    statement->kind = kind;
    statement->offset = offset;
    return statement;
}

cn_block *cn_block_create(cn_allocator *allocator, size_t offset) {
    cn_block *block = CN_ALLOC(allocator, cn_block);
    block->statements.items = NULL;
    block->statements.count = 0;
    block->statements.capacity = 0;
    block->offset = offset;
    return block;
}

cn_function *cn_function_create(cn_allocator *allocator, size_t offset) {
    cn_function *function = CN_ALLOC(allocator, cn_function);
    function->is_public = false;
    function->is_builtin = false;
    function->name = cn_sv_from_parts(NULL, 0);
    function->return_type = NULL;
    function->parameters.items = NULL;
    function->parameters.count = 0;
    function->parameters.capacity = 0;
    function->body = NULL;
    function->offset = offset;
    return function;
}

cn_const_decl *cn_const_decl_create(cn_allocator *allocator, size_t offset) {
    cn_const_decl *const_decl = CN_ALLOC(allocator, cn_const_decl);
    const_decl->is_public = false;
    const_decl->name = cn_sv_from_parts(NULL, 0);
    const_decl->type = NULL;
    const_decl->initializer = NULL;
    const_decl->sema_checked = false;
    const_decl->sema_checking = false;
    const_decl->offset = offset;
    return const_decl;
}

cn_struct_decl *cn_struct_decl_create(cn_allocator *allocator, size_t offset) {
    cn_struct_decl *struct_decl = CN_ALLOC(allocator, cn_struct_decl);
    struct_decl->is_public = false;
    struct_decl->name = cn_sv_from_parts(NULL, 0);
    struct_decl->fields.items = NULL;
    struct_decl->fields.count = 0;
    struct_decl->fields.capacity = 0;
    struct_decl->offset = offset;
    return struct_decl;
}

bool cn_program_push_import(cn_program *program, cn_import_decl import_decl) {
    program->imports = (cn_import_decl *)cn_grow_items(
        program->allocator,
        program->imports,
        sizeof(cn_import_decl),
        &program->import_capacity,
        program->import_count + 1
    );
    program->imports[program->import_count++] = import_decl;
    return true;
}

bool cn_program_push_const(cn_program *program, cn_const_decl *const_decl) {
    program->consts = (cn_const_decl **)cn_grow_items(
        program->allocator,
        program->consts,
        sizeof(cn_const_decl *),
        &program->const_capacity,
        program->const_count + 1
    );
    program->consts[program->const_count++] = const_decl;
    return true;
}

bool cn_program_push_struct(cn_program *program, cn_struct_decl *struct_decl) {
    program->structs = (cn_struct_decl **)cn_grow_items(
        program->allocator,
        program->structs,
        sizeof(cn_struct_decl *),
        &program->struct_capacity,
        program->struct_count + 1
    );
    program->structs[program->struct_count++] = struct_decl;
    return true;
}

bool cn_program_push_function(cn_program *program, cn_function *function) {
    program->functions = (cn_function **)cn_grow_items(
        program->allocator,
        program->functions,
        sizeof(cn_function *),
        &program->function_capacity,
        program->function_count + 1
    );
    program->functions[program->function_count++] = function;
    return true;
}

bool cn_param_list_push(cn_allocator *allocator, cn_param_list *list, cn_param param) {
    list->items = (cn_param *)cn_grow_items(
        allocator,
        list->items,
        sizeof(cn_param),
        &list->capacity,
        list->count + 1
    );
    list->items[list->count++] = param;
    return true;
}

bool cn_stmt_list_push(cn_allocator *allocator, cn_stmt_list *list, cn_stmt *statement) {
    list->items = (cn_stmt **)cn_grow_items(
        allocator,
        list->items,
        sizeof(cn_stmt *),
        &list->capacity,
        list->count + 1
    );
    list->items[list->count++] = statement;
    return true;
}

bool cn_expr_list_push(cn_allocator *allocator, cn_expr_list *list, cn_expr *expression) {
    list->items = (cn_expr **)cn_grow_items(
        allocator,
        list->items,
        sizeof(cn_expr *),
        &list->capacity,
        list->count + 1
    );
    list->items[list->count++] = expression;
    return true;
}

bool cn_field_init_list_push(cn_allocator *allocator, cn_field_init_list *list, cn_field_init field_init) {
    list->items = (cn_field_init *)cn_grow_items(
        allocator,
        list->items,
        sizeof(cn_field_init),
        &list->capacity,
        list->count + 1
    );
    list->items[list->count++] = field_init;
    return true;
}

bool cn_struct_field_list_push(cn_allocator *allocator, cn_struct_field_list *list, cn_struct_field field) {
    list->items = (cn_struct_field *)cn_grow_items(
        allocator,
        list->items,
        sizeof(cn_struct_field),
        &list->capacity,
        list->count + 1
    );
    list->items[list->count++] = field;
    return true;
}

bool cn_type_equal(const cn_type_ref *left, const cn_type_ref *right) {
    if (left == NULL || right == NULL) {
        return false;
    }

    if (left->kind != right->kind) {
        return false;
    }

    switch (left->kind) {
    case CN_TYPE_RESULT:
    case CN_TYPE_PTR:
        return cn_type_equal(left->inner, right->inner);
    case CN_TYPE_ARRAY:
        return left->array_size == right->array_size && cn_type_equal(left->inner, right->inner);
    case CN_TYPE_NAMED:
        return cn_sv_eq(left->module_name, right->module_name) && cn_sv_eq(left->name, right->name);
    default:
        return true;
    }
}

void cn_type_describe(const cn_type_ref *type, char *buffer, size_t buffer_size) {
    if (type == NULL) {
        snprintf(buffer, buffer_size, "<null>");
        return;
    }

    switch (type->kind) {
    case CN_TYPE_INT:
        snprintf(buffer, buffer_size, "int");
        break;
    case CN_TYPE_U8:
        snprintf(buffer, buffer_size, "u8");
        break;
    case CN_TYPE_BOOL:
        snprintf(buffer, buffer_size, "bool");
        break;
    case CN_TYPE_STR:
        snprintf(buffer, buffer_size, "str");
        break;
    case CN_TYPE_VOID:
        snprintf(buffer, buffer_size, "void");
        break;
    case CN_TYPE_RESULT: {
        char inner[128];
        cn_type_describe(type->inner, inner, sizeof(inner));
        snprintf(buffer, buffer_size, "result %s", inner);
        break;
    }
    case CN_TYPE_PTR: {
        char inner[128];
        cn_type_describe(type->inner, inner, sizeof(inner));
        snprintf(buffer, buffer_size, "ptr %s", inner);
        break;
    }
    case CN_TYPE_ARRAY: {
        char inner[128];
        cn_type_describe(type->inner, inner, sizeof(inner));
        snprintf(buffer, buffer_size, "%s[%zu]", inner, type->array_size);
        break;
    }
    case CN_TYPE_NAMED:
        if (type->module_name.length > 0) {
            snprintf(
                buffer,
                buffer_size,
                "%.*s.%.*s",
                (int)type->module_name.length,
                type->module_name.data,
                (int)type->name.length,
                type->name.data
            );
        } else {
            snprintf(buffer, buffer_size, "%.*s", (int)type->name.length, type->name.data);
        }
        break;
    case CN_TYPE_UNKNOWN:
        snprintf(buffer, buffer_size, "<unknown>");
        break;
    }
}

static void cn_type_destroy(cn_allocator *allocator, cn_type_ref *type) {
    if (type == NULL) {
        return;
    }

    cn_type_destroy(allocator, type->inner);
    CN_FREE(allocator, type);
}

static void cn_expr_destroy(cn_allocator *allocator, cn_expr *expression) {
    if (expression == NULL) {
        return;
    }

    switch (expression->kind) {
    case CN_EXPR_UNARY:
        cn_expr_destroy(allocator, expression->data.unary.operand);
        break;
    case CN_EXPR_BINARY:
        cn_expr_destroy(allocator, expression->data.binary.left);
        cn_expr_destroy(allocator, expression->data.binary.right);
        break;
    case CN_EXPR_CALL:
        cn_expr_destroy(allocator, expression->data.call.callee);
        for (size_t i = 0; i < expression->data.call.arguments.count; ++i) {
            cn_expr_destroy(allocator, expression->data.call.arguments.items[i]);
        }
        CN_FREE(allocator, expression->data.call.arguments.items);
        break;
    case CN_EXPR_ARRAY_LITERAL:
        for (size_t i = 0; i < expression->data.array_literal.items.count; ++i) {
            cn_expr_destroy(allocator, expression->data.array_literal.items.items[i]);
        }
        CN_FREE(allocator, expression->data.array_literal.items.items);
        break;
    case CN_EXPR_INDEX:
        cn_expr_destroy(allocator, expression->data.index.base);
        cn_expr_destroy(allocator, expression->data.index.index);
        break;
    case CN_EXPR_FIELD:
        cn_expr_destroy(allocator, expression->data.field.base);
        break;
    case CN_EXPR_STRUCT_LITERAL:
        for (size_t i = 0; i < expression->data.struct_literal.fields.count; ++i) {
            cn_expr_destroy(allocator, expression->data.struct_literal.fields.items[i].value);
        }
        CN_FREE(allocator, expression->data.struct_literal.fields.items);
        break;
    case CN_EXPR_OK:
        cn_expr_destroy(allocator, expression->data.ok_expr.value);
        break;
    case CN_EXPR_ALLOC:
        cn_type_destroy(allocator, expression->data.alloc_expr.type);
        break;
    case CN_EXPR_ADDR:
        cn_expr_destroy(allocator, expression->data.addr_expr.target);
        break;
    case CN_EXPR_DEREF:
        cn_expr_destroy(allocator, expression->data.deref_expr.target);
        break;
    case CN_EXPR_ERR:
        break;
    default:
        break;
    }

    CN_FREE(allocator, expression);
}

static void cn_stmt_destroy(cn_allocator *allocator, cn_stmt *statement) {
    if (statement == NULL) {
        return;
    }

    switch (statement->kind) {
    case CN_STMT_LET:
        cn_type_destroy(allocator, statement->data.let_stmt.type);
        cn_expr_destroy(allocator, statement->data.let_stmt.initializer);
        break;
    case CN_STMT_ASSIGN:
        cn_expr_destroy(allocator, statement->data.assign_stmt.target);
        cn_expr_destroy(allocator, statement->data.assign_stmt.value);
        break;
    case CN_STMT_RETURN:
        cn_expr_destroy(allocator, statement->data.return_stmt.value);
        break;
    case CN_STMT_EXPR:
        cn_expr_destroy(allocator, statement->data.expr_stmt.value);
        break;
    case CN_STMT_IF:
        cn_expr_destroy(allocator, statement->data.if_stmt.condition);
        cn_block_destroy(allocator, statement->data.if_stmt.then_block);
        cn_block_destroy(allocator, statement->data.if_stmt.else_block);
        break;
    case CN_STMT_WHILE:
        cn_expr_destroy(allocator, statement->data.while_stmt.condition);
        cn_block_destroy(allocator, statement->data.while_stmt.body);
        break;
    case CN_STMT_LOOP:
        cn_block_destroy(allocator, statement->data.loop_stmt.body);
        break;
    case CN_STMT_FOR:
        cn_type_destroy(allocator, statement->data.for_stmt.type);
        cn_expr_destroy(allocator, statement->data.for_stmt.start);
        cn_expr_destroy(allocator, statement->data.for_stmt.end);
        cn_block_destroy(allocator, statement->data.for_stmt.body);
        break;
    case CN_STMT_FREE:
        cn_expr_destroy(allocator, statement->data.free_stmt.value);
        break;
    }

    CN_FREE(allocator, statement);
}

static void cn_block_destroy(cn_allocator *allocator, cn_block *block) {
    if (block == NULL) {
        return;
    }

    for (size_t i = 0; i < block->statements.count; ++i) {
        cn_stmt_destroy(allocator, block->statements.items[i]);
    }

    CN_FREE(allocator, block->statements.items);
    CN_FREE(allocator, block);
}

static void cn_function_destroy(cn_allocator *allocator, cn_function *function) {
    if (function == NULL) {
        return;
    }

    cn_type_destroy(allocator, function->return_type);
    for (size_t i = 0; i < function->parameters.count; ++i) {
        cn_type_destroy(allocator, function->parameters.items[i].type);
    }
    CN_FREE(allocator, function->parameters.items);
    cn_block_destroy(allocator, function->body);
    CN_FREE(allocator, function);
}

static void cn_const_decl_destroy(cn_allocator *allocator, cn_const_decl *const_decl) {
    if (const_decl == NULL) {
        return;
    }

    cn_type_destroy(allocator, const_decl->type);
    cn_expr_destroy(allocator, const_decl->initializer);
    CN_FREE(allocator, const_decl);
}

static void cn_struct_decl_destroy(cn_allocator *allocator, cn_struct_decl *struct_decl) {
    if (struct_decl == NULL) {
        return;
    }

    for (size_t i = 0; i < struct_decl->fields.count; ++i) {
        cn_type_destroy(allocator, struct_decl->fields.items[i].type);
    }

    CN_FREE(allocator, struct_decl->fields.items);
    CN_FREE(allocator, struct_decl);
}
