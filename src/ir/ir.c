#include "cnegative/ir.h"

#include <stdio.h>

static void cn_ir_expr_destroy(cn_allocator *allocator, cn_ir_expr *expression);
static void cn_ir_stmt_destroy(cn_allocator *allocator, cn_ir_stmt *statement);
static void cn_ir_block_destroy(cn_allocator *allocator, cn_ir_block *block);
static void cn_ir_function_destroy(cn_allocator *allocator, cn_ir_function *function);
static void cn_ir_const_destroy(cn_allocator *allocator, cn_ir_const *const_decl);
static void cn_ir_struct_destroy(cn_allocator *allocator, cn_ir_struct *struct_decl);

static void *cn_ir_grow_items(cn_allocator *allocator, void *items, size_t item_size, size_t *capacity, size_t required) {
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

static cn_ir_type_kind cn_ir_type_kind_from_ast(cn_type_kind kind) {
    switch (kind) {
    case CN_TYPE_INT: return CN_IR_TYPE_INT;
    case CN_TYPE_U8: return CN_IR_TYPE_U8;
    case CN_TYPE_BOOL: return CN_IR_TYPE_BOOL;
    case CN_TYPE_STR: return CN_IR_TYPE_STR;
    case CN_TYPE_VOID: return CN_IR_TYPE_VOID;
    case CN_TYPE_RESULT: return CN_IR_TYPE_RESULT;
    case CN_TYPE_PTR: return CN_IR_TYPE_PTR;
    case CN_TYPE_ARRAY: return CN_IR_TYPE_ARRAY;
    case CN_TYPE_NAMED: return CN_IR_TYPE_NAMED;
    case CN_TYPE_UNKNOWN: return CN_IR_TYPE_UNKNOWN;
    }

    return CN_IR_TYPE_UNKNOWN;
}

cn_ir_type *cn_ir_type_create(
    cn_allocator *allocator,
    cn_ir_type_kind kind,
    cn_strview module_name,
    cn_strview name,
    cn_ir_type *inner,
    size_t array_size
) {
    cn_ir_type *type = CN_ALLOC(allocator, cn_ir_type);
    type->kind = kind;
    type->module_name = module_name;
    type->name = name;
    type->inner = inner;
    type->array_size = array_size;
    return type;
}

cn_ir_type *cn_ir_type_clone(cn_allocator *allocator, const cn_ir_type *type) {
    if (type == NULL) {
        return NULL;
    }

    return cn_ir_type_create(
        allocator,
        type->kind,
        type->module_name,
        type->name,
        cn_ir_type_clone(allocator, type->inner),
        type->array_size
    );
}

cn_ir_type *cn_ir_type_from_ast(cn_allocator *allocator, const cn_type_ref *type) {
    if (type == NULL) {
        return NULL;
    }

    return cn_ir_type_create(
        allocator,
        cn_ir_type_kind_from_ast(type->kind),
        type->module_name,
        type->name,
        cn_ir_type_from_ast(allocator, type->inner),
        type->array_size
    );
}

void cn_ir_type_describe(const cn_ir_type *type, char *buffer, size_t buffer_size) {
    if (type == NULL) {
        snprintf(buffer, buffer_size, "<null>");
        return;
    }

    switch (type->kind) {
    case CN_IR_TYPE_INT:
        snprintf(buffer, buffer_size, "int");
        break;
    case CN_IR_TYPE_U8:
        snprintf(buffer, buffer_size, "u8");
        break;
    case CN_IR_TYPE_BOOL:
        snprintf(buffer, buffer_size, "bool");
        break;
    case CN_IR_TYPE_STR:
        snprintf(buffer, buffer_size, "str");
        break;
    case CN_IR_TYPE_VOID:
        snprintf(buffer, buffer_size, "void");
        break;
    case CN_IR_TYPE_RESULT: {
        char inner[128];
        cn_ir_type_describe(type->inner, inner, sizeof(inner));
        snprintf(buffer, buffer_size, "result %s", inner);
        break;
    }
    case CN_IR_TYPE_PTR: {
        char inner[128];
        cn_ir_type_describe(type->inner, inner, sizeof(inner));
        snprintf(buffer, buffer_size, "ptr %s", inner);
        break;
    }
    case CN_IR_TYPE_ARRAY: {
        char inner[128];
        cn_ir_type_describe(type->inner, inner, sizeof(inner));
        snprintf(buffer, buffer_size, "%s[%zu]", inner, type->array_size);
        break;
    }
    case CN_IR_TYPE_NAMED:
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
    case CN_IR_TYPE_UNKNOWN:
        snprintf(buffer, buffer_size, "<unknown>");
        break;
    }
}

void cn_ir_type_destroy(cn_allocator *allocator, cn_ir_type *type) {
    if (type == NULL) {
        return;
    }

    cn_ir_type_destroy(allocator, type->inner);
    CN_FREE(allocator, type);
}

cn_ir_program *cn_ir_program_create(cn_allocator *allocator) {
    cn_ir_program *program = CN_ALLOC(allocator, cn_ir_program);
    program->allocator = allocator;
    program->modules.items = NULL;
    program->modules.count = 0;
    program->modules.capacity = 0;
    return program;
}

cn_ir_module *cn_ir_module_create(cn_allocator *allocator, cn_strview name, cn_strview path, const cn_source *source) {
    cn_ir_module *module = CN_ALLOC(allocator, cn_ir_module);
    module->name = name;
    module->path = path;
    module->source = source;
    module->consts.items = NULL;
    module->consts.count = 0;
    module->consts.capacity = 0;
    module->structs.items = NULL;
    module->structs.count = 0;
    module->structs.capacity = 0;
    module->functions.items = NULL;
    module->functions.count = 0;
    module->functions.capacity = 0;
    return module;
}

cn_ir_const *cn_ir_const_create(cn_allocator *allocator, cn_strview module_name, cn_strview name, bool is_public, size_t offset) {
    cn_ir_const *const_decl = CN_ALLOC(allocator, cn_ir_const);
    const_decl->is_public = is_public;
    const_decl->module_name = module_name;
    const_decl->name = name;
    const_decl->type = NULL;
    const_decl->initializer = NULL;
    const_decl->offset = offset;
    return const_decl;
}

cn_ir_struct *cn_ir_struct_create(cn_allocator *allocator, cn_strview module_name, cn_strview name, bool is_public, size_t offset) {
    cn_ir_struct *struct_decl = CN_ALLOC(allocator, cn_ir_struct);
    struct_decl->is_public = is_public;
    struct_decl->module_name = module_name;
    struct_decl->name = name;
    struct_decl->fields.items = NULL;
    struct_decl->fields.count = 0;
    struct_decl->fields.capacity = 0;
    struct_decl->offset = offset;
    return struct_decl;
}

cn_ir_function *cn_ir_function_create(cn_allocator *allocator, cn_strview module_name, cn_strview name, bool is_public, size_t offset) {
    cn_ir_function *function = CN_ALLOC(allocator, cn_ir_function);
    function->is_public = is_public;
    function->module_name = module_name;
    function->name = name;
    function->return_type = NULL;
    function->parameters.items = NULL;
    function->parameters.count = 0;
    function->parameters.capacity = 0;
    function->body = NULL;
    function->offset = offset;
    return function;
}

cn_ir_block *cn_ir_block_create(cn_allocator *allocator, size_t offset) {
    cn_ir_block *block = CN_ALLOC(allocator, cn_ir_block);
    block->statements.items = NULL;
    block->statements.count = 0;
    block->statements.capacity = 0;
    block->offset = offset;
    return block;
}

cn_ir_stmt *cn_ir_stmt_create(cn_allocator *allocator, cn_ir_stmt_kind kind, size_t offset) {
    cn_ir_stmt *statement = CN_ALLOC(allocator, cn_ir_stmt);
    statement->kind = kind;
    statement->offset = offset;
    return statement;
}

cn_ir_expr *cn_ir_expr_create(cn_allocator *allocator, cn_ir_expr_kind kind, size_t offset) {
    cn_ir_expr *expression = CN_ALLOC(allocator, cn_ir_expr);
    expression->kind = kind;
    expression->type = NULL;
    expression->offset = offset;
    return expression;
}

bool cn_ir_program_push_module(cn_ir_program *program, cn_ir_module *module) {
    program->modules.items = (cn_ir_module **)cn_ir_grow_items(
        program->allocator,
        program->modules.items,
        sizeof(cn_ir_module *),
        &program->modules.capacity,
        program->modules.count + 1
    );
    program->modules.items[program->modules.count++] = module;
    return true;
}

bool cn_ir_module_push_const(cn_ir_module *module, cn_allocator *allocator, cn_ir_const *const_decl) {
    module->consts.items = (cn_ir_const **)cn_ir_grow_items(
        allocator,
        module->consts.items,
        sizeof(cn_ir_const *),
        &module->consts.capacity,
        module->consts.count + 1
    );
    module->consts.items[module->consts.count++] = const_decl;
    return true;
}

bool cn_ir_module_push_struct(cn_ir_module *module, cn_allocator *allocator, cn_ir_struct *struct_decl) {
    module->structs.items = (cn_ir_struct **)cn_ir_grow_items(
        allocator,
        module->structs.items,
        sizeof(cn_ir_struct *),
        &module->structs.capacity,
        module->structs.count + 1
    );
    module->structs.items[module->structs.count++] = struct_decl;
    return true;
}

bool cn_ir_module_push_function(cn_ir_module *module, cn_allocator *allocator, cn_ir_function *function) {
    module->functions.items = (cn_ir_function **)cn_ir_grow_items(
        allocator,
        module->functions.items,
        sizeof(cn_ir_function *),
        &module->functions.capacity,
        module->functions.count + 1
    );
    module->functions.items[module->functions.count++] = function;
    return true;
}

bool cn_ir_param_list_push(cn_allocator *allocator, cn_ir_param_list *list, cn_ir_param param) {
    list->items = (cn_ir_param *)cn_ir_grow_items(
        allocator,
        list->items,
        sizeof(cn_ir_param),
        &list->capacity,
        list->count + 1
    );
    list->items[list->count++] = param;
    return true;
}

bool cn_ir_stmt_list_push(cn_allocator *allocator, cn_ir_stmt_list *list, cn_ir_stmt *statement) {
    list->items = (cn_ir_stmt **)cn_ir_grow_items(
        allocator,
        list->items,
        sizeof(cn_ir_stmt *),
        &list->capacity,
        list->count + 1
    );
    list->items[list->count++] = statement;
    return true;
}

bool cn_ir_expr_list_push(cn_allocator *allocator, cn_ir_expr_list *list, cn_ir_expr *expression) {
    list->items = (cn_ir_expr **)cn_ir_grow_items(
        allocator,
        list->items,
        sizeof(cn_ir_expr *),
        &list->capacity,
        list->count + 1
    );
    list->items[list->count++] = expression;
    return true;
}

bool cn_ir_field_init_list_push(cn_allocator *allocator, cn_ir_field_init_list *list, cn_ir_field_init field_init) {
    list->items = (cn_ir_field_init *)cn_ir_grow_items(
        allocator,
        list->items,
        sizeof(cn_ir_field_init),
        &list->capacity,
        list->count + 1
    );
    list->items[list->count++] = field_init;
    return true;
}

bool cn_ir_struct_field_list_push(cn_allocator *allocator, cn_ir_struct_field_list *list, cn_ir_struct_field field) {
    list->items = (cn_ir_struct_field *)cn_ir_grow_items(
        allocator,
        list->items,
        sizeof(cn_ir_struct_field),
        &list->capacity,
        list->count + 1
    );
    list->items[list->count++] = field;
    return true;
}

static void cn_ir_expr_destroy(cn_allocator *allocator, cn_ir_expr *expression) {
    if (expression == NULL) {
        return;
    }

    cn_ir_type_destroy(allocator, expression->type);

    switch (expression->kind) {
    case CN_IR_EXPR_UNARY:
        cn_ir_expr_destroy(allocator, expression->data.unary.operand);
        break;
    case CN_IR_EXPR_BINARY:
        cn_ir_expr_destroy(allocator, expression->data.binary.left);
        cn_ir_expr_destroy(allocator, expression->data.binary.right);
        break;
    case CN_IR_EXPR_IF:
        cn_ir_expr_destroy(allocator, expression->data.if_expr.condition);
        cn_ir_expr_destroy(allocator, expression->data.if_expr.then_expr);
        cn_ir_expr_destroy(allocator, expression->data.if_expr.else_expr);
        break;
    case CN_IR_EXPR_CALL:
        for (size_t i = 0; i < expression->data.call.arguments.count; ++i) {
            cn_ir_expr_destroy(allocator, expression->data.call.arguments.items[i]);
        }
        CN_FREE(allocator, expression->data.call.arguments.items);
        break;
    case CN_IR_EXPR_ARRAY_LITERAL:
        for (size_t i = 0; i < expression->data.array_literal.items.count; ++i) {
            cn_ir_expr_destroy(allocator, expression->data.array_literal.items.items[i]);
        }
        CN_FREE(allocator, expression->data.array_literal.items.items);
        break;
    case CN_IR_EXPR_INDEX:
        cn_ir_expr_destroy(allocator, expression->data.index.base);
        cn_ir_expr_destroy(allocator, expression->data.index.index);
        break;
    case CN_IR_EXPR_FIELD:
        cn_ir_expr_destroy(allocator, expression->data.field.base);
        break;
    case CN_IR_EXPR_STRUCT_LITERAL:
        for (size_t i = 0; i < expression->data.struct_literal.fields.count; ++i) {
            cn_ir_expr_destroy(allocator, expression->data.struct_literal.fields.items[i].value);
        }
        CN_FREE(allocator, expression->data.struct_literal.fields.items);
        break;
    case CN_IR_EXPR_OK:
        cn_ir_expr_destroy(allocator, expression->data.ok_expr.value);
        break;
    case CN_IR_EXPR_ALLOC:
        cn_ir_type_destroy(allocator, expression->data.alloc_expr.alloc_type);
        break;
    case CN_IR_EXPR_ADDR:
        cn_ir_expr_destroy(allocator, expression->data.addr_expr.target);
        break;
    case CN_IR_EXPR_DEREF:
        cn_ir_expr_destroy(allocator, expression->data.deref_expr.target);
        break;
    case CN_IR_EXPR_INT:
    case CN_IR_EXPR_BOOL:
    case CN_IR_EXPR_STRING:
    case CN_IR_EXPR_LOCAL:
    case CN_IR_EXPR_ERR:
        break;
    }

    CN_FREE(allocator, expression);
}

static void cn_ir_stmt_destroy(cn_allocator *allocator, cn_ir_stmt *statement) {
    if (statement == NULL) {
        return;
    }

    switch (statement->kind) {
    case CN_IR_STMT_LET:
        cn_ir_type_destroy(allocator, statement->data.let_stmt.type);
        cn_ir_expr_destroy(allocator, statement->data.let_stmt.initializer);
        break;
    case CN_IR_STMT_ASSIGN:
        cn_ir_expr_destroy(allocator, statement->data.assign_stmt.target);
        cn_ir_expr_destroy(allocator, statement->data.assign_stmt.value);
        break;
    case CN_IR_STMT_RETURN:
        cn_ir_expr_destroy(allocator, statement->data.return_stmt.value);
        break;
    case CN_IR_STMT_EXPR:
        cn_ir_expr_destroy(allocator, statement->data.expr_stmt.value);
        break;
    case CN_IR_STMT_IF:
        cn_ir_expr_destroy(allocator, statement->data.if_stmt.condition);
        cn_ir_block_destroy(allocator, statement->data.if_stmt.then_block);
        cn_ir_block_destroy(allocator, statement->data.if_stmt.else_block);
        break;
    case CN_IR_STMT_WHILE:
        cn_ir_expr_destroy(allocator, statement->data.while_stmt.condition);
        cn_ir_block_destroy(allocator, statement->data.while_stmt.body);
        break;
    case CN_IR_STMT_LOOP:
        cn_ir_block_destroy(allocator, statement->data.loop_stmt.body);
        break;
    case CN_IR_STMT_FOR:
        cn_ir_type_destroy(allocator, statement->data.for_stmt.type);
        cn_ir_expr_destroy(allocator, statement->data.for_stmt.start);
        cn_ir_expr_destroy(allocator, statement->data.for_stmt.end);
        cn_ir_block_destroy(allocator, statement->data.for_stmt.body);
        break;
    case CN_IR_STMT_FREE:
        cn_ir_expr_destroy(allocator, statement->data.free_stmt.value);
        break;
    }

    CN_FREE(allocator, statement);
}

static void cn_ir_block_destroy(cn_allocator *allocator, cn_ir_block *block) {
    if (block == NULL) {
        return;
    }

    for (size_t i = 0; i < block->statements.count; ++i) {
        cn_ir_stmt_destroy(allocator, block->statements.items[i]);
    }

    CN_FREE(allocator, block->statements.items);
    CN_FREE(allocator, block);
}

static void cn_ir_function_destroy(cn_allocator *allocator, cn_ir_function *function) {
    if (function == NULL) {
        return;
    }

    cn_ir_type_destroy(allocator, function->return_type);
    for (size_t i = 0; i < function->parameters.count; ++i) {
        cn_ir_type_destroy(allocator, function->parameters.items[i].type);
    }
    CN_FREE(allocator, function->parameters.items);
    cn_ir_block_destroy(allocator, function->body);
    CN_FREE(allocator, function);
}

static void cn_ir_const_destroy(cn_allocator *allocator, cn_ir_const *const_decl) {
    if (const_decl == NULL) {
        return;
    }

    cn_ir_type_destroy(allocator, const_decl->type);
    cn_ir_expr_destroy(allocator, const_decl->initializer);
    CN_FREE(allocator, const_decl);
}

static void cn_ir_struct_destroy(cn_allocator *allocator, cn_ir_struct *struct_decl) {
    if (struct_decl == NULL) {
        return;
    }

    for (size_t i = 0; i < struct_decl->fields.count; ++i) {
        cn_ir_type_destroy(allocator, struct_decl->fields.items[i].type);
    }

    CN_FREE(allocator, struct_decl->fields.items);
    CN_FREE(allocator, struct_decl);
}

void cn_ir_program_destroy(cn_allocator *allocator, cn_ir_program *program) {
    if (program == NULL) {
        return;
    }

    for (size_t i = 0; i < program->modules.count; ++i) {
        cn_ir_module *module = program->modules.items[i];

        for (size_t const_index = 0; const_index < module->consts.count; ++const_index) {
            cn_ir_const_destroy(allocator, module->consts.items[const_index]);
        }
        for (size_t struct_index = 0; struct_index < module->structs.count; ++struct_index) {
            cn_ir_struct_destroy(allocator, module->structs.items[struct_index]);
        }
        for (size_t function_index = 0; function_index < module->functions.count; ++function_index) {
            cn_ir_function_destroy(allocator, module->functions.items[function_index]);
        }

        CN_FREE(allocator, module->consts.items);
        CN_FREE(allocator, module->structs.items);
        CN_FREE(allocator, module->functions.items);
        CN_FREE(allocator, module);
    }

    CN_FREE(allocator, program->modules.items);
    CN_FREE(allocator, program);
}

static void cn_ir_dump_indent(FILE *stream, size_t depth) {
    for (size_t i = 0; i < depth; ++i) {
        fputs("    ", stream);
    }
}

static const char *cn_ir_unary_op_name(cn_ir_unary_op op) {
    switch (op) {
    case CN_IR_UNARY_NEGATE: return "-";
    case CN_IR_UNARY_NOT: return "!";
    }

    return "?";
}

static const char *cn_ir_binary_op_name(cn_ir_binary_op op) {
    switch (op) {
    case CN_IR_BINARY_ADD: return "+";
    case CN_IR_BINARY_SUB: return "-";
    case CN_IR_BINARY_MUL: return "*";
    case CN_IR_BINARY_DIV: return "/";
    case CN_IR_BINARY_MOD: return "%";
    case CN_IR_BINARY_EQUAL: return "==";
    case CN_IR_BINARY_NOT_EQUAL: return "!=";
    case CN_IR_BINARY_LESS: return "<";
    case CN_IR_BINARY_LESS_EQUAL: return "<=";
    case CN_IR_BINARY_GREATER: return ">";
    case CN_IR_BINARY_GREATER_EQUAL: return ">=";
    case CN_IR_BINARY_AND: return "&&";
    case CN_IR_BINARY_OR: return "||";
    }

    return "?";
}

static void cn_ir_dump_expr(FILE *stream, const cn_ir_expr *expression);

static void cn_ir_dump_call_target(FILE *stream, const cn_ir_expr *expression) {
    if (expression->data.call.target_kind == CN_IR_CALL_BUILTIN) {
        if (expression->data.call.module_name.length == 0) {
            fprintf(
                stream,
                "%.*s",
                (int)expression->data.call.function_name.length,
                expression->data.call.function_name.data
            );
            return;
        }

        fprintf(
            stream,
            "%.*s.%.*s",
            (int)expression->data.call.module_name.length,
            expression->data.call.module_name.data,
            (int)expression->data.call.function_name.length,
            expression->data.call.function_name.data
        );
        return;
    }

    fprintf(
        stream,
        "%.*s.%.*s",
        (int)expression->data.call.module_name.length,
        expression->data.call.module_name.data,
        (int)expression->data.call.function_name.length,
        expression->data.call.function_name.data
    );
}

static void cn_ir_dump_expr(FILE *stream, const cn_ir_expr *expression) {
    switch (expression->kind) {
    case CN_IR_EXPR_INT:
        fprintf(stream, "%lld", (long long)expression->data.int_value);
        break;
    case CN_IR_EXPR_BOOL:
        fputs(expression->data.bool_value ? "true" : "false", stream);
        break;
    case CN_IR_EXPR_STRING:
        fprintf(stream, "\"%.*s\"", (int)expression->data.string_value.length, expression->data.string_value.data);
        break;
    case CN_IR_EXPR_LOCAL:
        fprintf(stream, "%.*s", (int)expression->data.local_name.length, expression->data.local_name.data);
        break;
    case CN_IR_EXPR_UNARY:
        fprintf(stream, "%s", cn_ir_unary_op_name(expression->data.unary.op));
        cn_ir_dump_expr(stream, expression->data.unary.operand);
        break;
    case CN_IR_EXPR_BINARY:
        fputc('(', stream);
        cn_ir_dump_expr(stream, expression->data.binary.left);
        fprintf(stream, " %s ", cn_ir_binary_op_name(expression->data.binary.op));
        cn_ir_dump_expr(stream, expression->data.binary.right);
        fputc(')', stream);
        break;
    case CN_IR_EXPR_IF:
        fputs("if ", stream);
        cn_ir_dump_expr(stream, expression->data.if_expr.condition);
        fputs(" { ", stream);
        cn_ir_dump_expr(stream, expression->data.if_expr.then_expr);
        fputs(" } else { ", stream);
        cn_ir_dump_expr(stream, expression->data.if_expr.else_expr);
        fputs(" }", stream);
        break;
    case CN_IR_EXPR_CALL:
        cn_ir_dump_call_target(stream, expression);
        fputc('(', stream);
        for (size_t i = 0; i < expression->data.call.arguments.count; ++i) {
            if (i > 0) {
                fputs(", ", stream);
            }
            cn_ir_dump_expr(stream, expression->data.call.arguments.items[i]);
        }
        fputc(')', stream);
        break;
    case CN_IR_EXPR_ARRAY_LITERAL:
        fputc('[', stream);
        for (size_t i = 0; i < expression->data.array_literal.items.count; ++i) {
            if (i > 0) {
                fputs(", ", stream);
            }
            cn_ir_dump_expr(stream, expression->data.array_literal.items.items[i]);
        }
        fputc(']', stream);
        break;
    case CN_IR_EXPR_INDEX:
        cn_ir_dump_expr(stream, expression->data.index.base);
        fputc('[', stream);
        cn_ir_dump_expr(stream, expression->data.index.index);
        fputc(']', stream);
        break;
    case CN_IR_EXPR_FIELD:
        cn_ir_dump_expr(stream, expression->data.field.base);
        fprintf(stream, ".%.*s", (int)expression->data.field.field_name.length, expression->data.field.field_name.data);
        break;
    case CN_IR_EXPR_STRUCT_LITERAL:
        fprintf(
            stream,
            "%.*s.%.*s { ",
            (int)expression->data.struct_literal.module_name.length,
            expression->data.struct_literal.module_name.data,
            (int)expression->data.struct_literal.type_name.length,
            expression->data.struct_literal.type_name.data
        );
        for (size_t i = 0; i < expression->data.struct_literal.fields.count; ++i) {
            if (i > 0) {
                fputs(", ", stream);
            }
            fprintf(
                stream,
                "%.*s:",
                (int)expression->data.struct_literal.fields.items[i].name.length,
                expression->data.struct_literal.fields.items[i].name.data
            );
            cn_ir_dump_expr(stream, expression->data.struct_literal.fields.items[i].value);
        }
        fputs(" }", stream);
        break;
    case CN_IR_EXPR_OK:
        fputs("ok ", stream);
        cn_ir_dump_expr(stream, expression->data.ok_expr.value);
        break;
    case CN_IR_EXPR_ERR:
        fputs("err", stream);
        break;
    case CN_IR_EXPR_ALLOC: {
        char type_name[128];
        cn_ir_type_describe(expression->data.alloc_expr.alloc_type, type_name, sizeof(type_name));
        fprintf(stream, "alloc %s", type_name);
        break;
    }
    case CN_IR_EXPR_ADDR:
        fputs("addr ", stream);
        cn_ir_dump_expr(stream, expression->data.addr_expr.target);
        break;
    case CN_IR_EXPR_DEREF:
        fputs("deref ", stream);
        cn_ir_dump_expr(stream, expression->data.deref_expr.target);
        break;
    }
}

static void cn_ir_dump_stmt(FILE *stream, const cn_ir_stmt *statement, size_t depth);

static void cn_ir_dump_block(FILE *stream, const cn_ir_block *block, size_t depth) {
    fputs("{\n", stream);
    for (size_t i = 0; i < block->statements.count; ++i) {
        cn_ir_dump_stmt(stream, block->statements.items[i], depth + 1);
    }
    cn_ir_dump_indent(stream, depth);
    fputs("}", stream);
}

static void cn_ir_dump_stmt(FILE *stream, const cn_ir_stmt *statement, size_t depth) {
    char type_name[128];

    cn_ir_dump_indent(stream, depth);
    switch (statement->kind) {
    case CN_IR_STMT_LET:
        cn_ir_type_describe(statement->data.let_stmt.type, type_name, sizeof(type_name));
        fprintf(stream, "let%s %.*s:%s = ", statement->data.let_stmt.is_mutable ? " mut" : "", (int)statement->data.let_stmt.name.length, statement->data.let_stmt.name.data, type_name);
        cn_ir_dump_expr(stream, statement->data.let_stmt.initializer);
        fputs(";\n", stream);
        break;
    case CN_IR_STMT_ASSIGN:
        cn_ir_dump_expr(stream, statement->data.assign_stmt.target);
        fputs(" = ", stream);
        cn_ir_dump_expr(stream, statement->data.assign_stmt.value);
        fputs(";\n", stream);
        break;
    case CN_IR_STMT_RETURN:
        fputs("return", stream);
        if (statement->data.return_stmt.value != NULL) {
            fputc(' ', stream);
            cn_ir_dump_expr(stream, statement->data.return_stmt.value);
        }
        fputs(";\n", stream);
        break;
    case CN_IR_STMT_EXPR:
        cn_ir_dump_expr(stream, statement->data.expr_stmt.value);
        fputs(";\n", stream);
        break;
    case CN_IR_STMT_IF:
        fputs("if ", stream);
        cn_ir_dump_expr(stream, statement->data.if_stmt.condition);
        fputc(' ', stream);
        cn_ir_dump_block(stream, statement->data.if_stmt.then_block, depth);
        if (statement->data.if_stmt.else_block != NULL) {
            fputs(" else ", stream);
            cn_ir_dump_block(stream, statement->data.if_stmt.else_block, depth);
        }
        fputc('\n', stream);
        break;
    case CN_IR_STMT_WHILE:
        fputs("while ", stream);
        cn_ir_dump_expr(stream, statement->data.while_stmt.condition);
        fputc(' ', stream);
        cn_ir_dump_block(stream, statement->data.while_stmt.body, depth);
        fputc('\n', stream);
        break;
    case CN_IR_STMT_LOOP:
        fputs("loop ", stream);
        cn_ir_dump_block(stream, statement->data.loop_stmt.body, depth);
        fputc('\n', stream);
        break;
    case CN_IR_STMT_FOR:
        cn_ir_type_describe(statement->data.for_stmt.type, type_name, sizeof(type_name));
        fprintf(
            stream,
            "for %.*s:%s in ",
            (int)statement->data.for_stmt.name.length,
            statement->data.for_stmt.name.data,
            type_name
        );
        cn_ir_dump_expr(stream, statement->data.for_stmt.start);
        fputs("..", stream);
        cn_ir_dump_expr(stream, statement->data.for_stmt.end);
        fputc(' ', stream);
        cn_ir_dump_block(stream, statement->data.for_stmt.body, depth);
        fputc('\n', stream);
        break;
    case CN_IR_STMT_FREE:
        fputs("free ", stream);
        cn_ir_dump_expr(stream, statement->data.free_stmt.value);
        fputs(";\n", stream);
        break;
    }
}

static void cn_ir_dump_struct(FILE *stream, const cn_ir_struct *struct_decl, size_t depth) {
    cn_ir_dump_indent(stream, depth);
    fprintf(
        stream,
        "%s %.*s.%.*s {\n",
        struct_decl->is_public ? "pstruct" : "struct",
        (int)struct_decl->module_name.length,
        struct_decl->module_name.data,
        (int)struct_decl->name.length,
        struct_decl->name.data
    );

    for (size_t i = 0; i < struct_decl->fields.count; ++i) {
        char type_name[128];
        cn_ir_type_describe(struct_decl->fields.items[i].type, type_name, sizeof(type_name));
        cn_ir_dump_indent(stream, depth + 1);
        fprintf(
            stream,
            "%.*s:%s;\n",
            (int)struct_decl->fields.items[i].name.length,
            struct_decl->fields.items[i].name.data,
            type_name
        );
    }

    cn_ir_dump_indent(stream, depth);
    fputs("}\n", stream);
}

static void cn_ir_dump_const(FILE *stream, const cn_ir_const *const_decl, size_t depth) {
    char type_name[128];
    cn_ir_type_describe(const_decl->type, type_name, sizeof(type_name));

    cn_ir_dump_indent(stream, depth);
    fprintf(
        stream,
        "%s %.*s.%.*s:%s = ",
        const_decl->is_public ? "pconst" : "const",
        (int)const_decl->module_name.length,
        const_decl->module_name.data,
        (int)const_decl->name.length,
        const_decl->name.data,
        type_name
    );
    cn_ir_dump_expr(stream, const_decl->initializer);
    fputs(";\n", stream);
}

static void cn_ir_dump_function(FILE *stream, const cn_ir_function *function, size_t depth) {
    char return_type[128];
    cn_ir_type_describe(function->return_type, return_type, sizeof(return_type));

    cn_ir_dump_indent(stream, depth);
    fprintf(
        stream,
        "%s %.*s.%.*s(",
        function->is_public ? "pfn" : "fn",
        (int)function->module_name.length,
        function->module_name.data,
        (int)function->name.length,
        function->name.data
    );

    for (size_t i = 0; i < function->parameters.count; ++i) {
        char param_type[128];
        cn_ir_type_describe(function->parameters.items[i].type, param_type, sizeof(param_type));
        if (i > 0) {
            fputs(", ", stream);
        }
        fprintf(
            stream,
            "%.*s:%s",
            (int)function->parameters.items[i].name.length,
            function->parameters.items[i].name.data,
            param_type
        );
    }

    fprintf(stream, ") -> %s ", return_type);
    cn_ir_dump_block(stream, function->body, depth);
    fputc('\n', stream);
}

void cn_ir_program_dump(const cn_ir_program *program, FILE *stream) {
    for (size_t module_index = 0; module_index < program->modules.count; ++module_index) {
        const cn_ir_module *module = program->modules.items[module_index];

        fprintf(
            stream,
            "module %.*s (%.*s) {\n",
            (int)module->name.length,
            module->name.data,
            (int)module->path.length,
            module->path.data
        );

        for (size_t i = 0; i < module->consts.count; ++i) {
            if (i > 0) {
                fputc('\n', stream);
            }
            cn_ir_dump_const(stream, module->consts.items[i], 1);
        }

        if (module->consts.count > 0 && (module->structs.count > 0 || module->functions.count > 0)) {
            fputc('\n', stream);
        }

        for (size_t i = 0; i < module->structs.count; ++i) {
            if (i > 0) {
                fputc('\n', stream);
            }
            cn_ir_dump_struct(stream, module->structs.items[i], 1);
        }

        if (module->structs.count > 0 && module->functions.count > 0) {
            fputc('\n', stream);
        }

        for (size_t i = 0; i < module->functions.count; ++i) {
            if (i > 0) {
                fputc('\n', stream);
            }
            cn_ir_dump_function(stream, module->functions.items[i], 1);
        }

        fputs("}\n", stream);
        if (module_index + 1 < program->modules.count) {
            fputc('\n', stream);
        }
    }
}
