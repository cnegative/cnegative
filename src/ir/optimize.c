#include "cnegative/ir.h"

static void cn_ir_opt_expr_destroy(cn_allocator *allocator, cn_ir_expr *expression);
static void cn_ir_opt_stmt_destroy(cn_allocator *allocator, cn_ir_stmt *statement);
static void cn_ir_opt_block_destroy(cn_allocator *allocator, cn_ir_block *block);

static void cn_ir_opt_expr_destroy(cn_allocator *allocator, cn_ir_expr *expression) {
    if (expression == NULL) {
        return;
    }

    cn_ir_type_destroy(allocator, expression->type);

    switch (expression->kind) {
    case CN_IR_EXPR_UNARY:
        cn_ir_opt_expr_destroy(allocator, expression->data.unary.operand);
        break;
    case CN_IR_EXPR_BINARY:
        cn_ir_opt_expr_destroy(allocator, expression->data.binary.left);
        cn_ir_opt_expr_destroy(allocator, expression->data.binary.right);
        break;
    case CN_IR_EXPR_CALL:
        for (size_t i = 0; i < expression->data.call.arguments.count; ++i) {
            cn_ir_opt_expr_destroy(allocator, expression->data.call.arguments.items[i]);
        }
        CN_FREE(allocator, expression->data.call.arguments.items);
        break;
    case CN_IR_EXPR_ARRAY_LITERAL:
        for (size_t i = 0; i < expression->data.array_literal.items.count; ++i) {
            cn_ir_opt_expr_destroy(allocator, expression->data.array_literal.items.items[i]);
        }
        CN_FREE(allocator, expression->data.array_literal.items.items);
        break;
    case CN_IR_EXPR_INDEX:
        cn_ir_opt_expr_destroy(allocator, expression->data.index.base);
        cn_ir_opt_expr_destroy(allocator, expression->data.index.index);
        break;
    case CN_IR_EXPR_FIELD:
        cn_ir_opt_expr_destroy(allocator, expression->data.field.base);
        break;
    case CN_IR_EXPR_STRUCT_LITERAL:
        for (size_t i = 0; i < expression->data.struct_literal.fields.count; ++i) {
            cn_ir_opt_expr_destroy(allocator, expression->data.struct_literal.fields.items[i].value);
        }
        CN_FREE(allocator, expression->data.struct_literal.fields.items);
        break;
    case CN_IR_EXPR_OK:
        cn_ir_opt_expr_destroy(allocator, expression->data.ok_expr.value);
        break;
    case CN_IR_EXPR_ALLOC:
        cn_ir_type_destroy(allocator, expression->data.alloc_expr.alloc_type);
        break;
    case CN_IR_EXPR_ADDR:
        cn_ir_opt_expr_destroy(allocator, expression->data.addr_expr.target);
        break;
    case CN_IR_EXPR_DEREF:
        cn_ir_opt_expr_destroy(allocator, expression->data.deref_expr.target);
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

static void cn_ir_opt_stmt_destroy(cn_allocator *allocator, cn_ir_stmt *statement) {
    if (statement == NULL) {
        return;
    }

    switch (statement->kind) {
    case CN_IR_STMT_LET:
        cn_ir_type_destroy(allocator, statement->data.let_stmt.type);
        cn_ir_opt_expr_destroy(allocator, statement->data.let_stmt.initializer);
        break;
    case CN_IR_STMT_ASSIGN:
        cn_ir_opt_expr_destroy(allocator, statement->data.assign_stmt.target);
        cn_ir_opt_expr_destroy(allocator, statement->data.assign_stmt.value);
        break;
    case CN_IR_STMT_RETURN:
        cn_ir_opt_expr_destroy(allocator, statement->data.return_stmt.value);
        break;
    case CN_IR_STMT_EXPR:
        cn_ir_opt_expr_destroy(allocator, statement->data.expr_stmt.value);
        break;
    case CN_IR_STMT_IF:
        cn_ir_opt_expr_destroy(allocator, statement->data.if_stmt.condition);
        cn_ir_opt_block_destroy(allocator, statement->data.if_stmt.then_block);
        cn_ir_opt_block_destroy(allocator, statement->data.if_stmt.else_block);
        break;
    case CN_IR_STMT_WHILE:
        cn_ir_opt_expr_destroy(allocator, statement->data.while_stmt.condition);
        cn_ir_opt_block_destroy(allocator, statement->data.while_stmt.body);
        break;
    case CN_IR_STMT_LOOP:
        cn_ir_opt_block_destroy(allocator, statement->data.loop_stmt.body);
        break;
    case CN_IR_STMT_FOR:
        cn_ir_type_destroy(allocator, statement->data.for_stmt.type);
        cn_ir_opt_expr_destroy(allocator, statement->data.for_stmt.start);
        cn_ir_opt_expr_destroy(allocator, statement->data.for_stmt.end);
        cn_ir_opt_block_destroy(allocator, statement->data.for_stmt.body);
        break;
    case CN_IR_STMT_FREE:
        cn_ir_opt_expr_destroy(allocator, statement->data.free_stmt.value);
        break;
    }

    CN_FREE(allocator, statement);
}

static void cn_ir_opt_block_destroy(cn_allocator *allocator, cn_ir_block *block) {
    if (block == NULL) {
        return;
    }

    for (size_t i = 0; i < block->statements.count; ++i) {
        cn_ir_opt_stmt_destroy(allocator, block->statements.items[i]);
    }

    CN_FREE(allocator, block->statements.items);
    CN_FREE(allocator, block);
}

static bool cn_ir_expr_is_int(const cn_ir_expr *expression) {
    return expression != NULL && expression->kind == CN_IR_EXPR_INT;
}

static bool cn_ir_expr_is_bool(const cn_ir_expr *expression) {
    return expression != NULL && expression->kind == CN_IR_EXPR_BOOL;
}

static bool cn_ir_expr_is_string(const cn_ir_expr *expression) {
    return expression != NULL && expression->kind == CN_IR_EXPR_STRING;
}

static void cn_ir_opt_replace_with_int(cn_allocator *allocator, cn_ir_expr *expression, int64_t value) {
    cn_ir_opt_expr_destroy(allocator, expression->data.binary.left);
    cn_ir_opt_expr_destroy(allocator, expression->data.binary.right);
    expression->kind = CN_IR_EXPR_INT;
    expression->data.int_value = value;
}

static void cn_ir_opt_replace_with_bool(cn_allocator *allocator, cn_ir_expr *expression, bool value) {
    cn_ir_opt_expr_destroy(allocator, expression->data.binary.left);
    cn_ir_opt_expr_destroy(allocator, expression->data.binary.right);
    expression->kind = CN_IR_EXPR_BOOL;
    expression->data.bool_value = value;
}

static void cn_ir_opt_fold_unary(cn_allocator *allocator, cn_ir_expr *expression) {
    cn_ir_expr *operand = expression->data.unary.operand;

    if (expression->data.unary.op == CN_IR_UNARY_NEGATE && cn_ir_expr_is_int(operand)) {
        int64_t value = -operand->data.int_value;
        cn_ir_opt_expr_destroy(allocator, operand);
        expression->kind = CN_IR_EXPR_INT;
        expression->data.int_value = value;
        return;
    }

    if (expression->data.unary.op == CN_IR_UNARY_NOT && cn_ir_expr_is_bool(operand)) {
        bool value = !operand->data.bool_value;
        cn_ir_opt_expr_destroy(allocator, operand);
        expression->kind = CN_IR_EXPR_BOOL;
        expression->data.bool_value = value;
    }
}

static void cn_ir_opt_fold_binary(cn_allocator *allocator, cn_ir_expr *expression) {
    cn_ir_expr *left = expression->data.binary.left;
    cn_ir_expr *right = expression->data.binary.right;

    if (cn_ir_expr_is_int(left) && cn_ir_expr_is_int(right)) {
        int64_t lhs = left->data.int_value;
        int64_t rhs = right->data.int_value;

        switch (expression->data.binary.op) {
        case CN_IR_BINARY_ADD: cn_ir_opt_replace_with_int(allocator, expression, lhs + rhs); return;
        case CN_IR_BINARY_SUB: cn_ir_opt_replace_with_int(allocator, expression, lhs - rhs); return;
        case CN_IR_BINARY_MUL: cn_ir_opt_replace_with_int(allocator, expression, lhs * rhs); return;
        case CN_IR_BINARY_DIV:
            if (rhs != 0) {
                cn_ir_opt_replace_with_int(allocator, expression, lhs / rhs);
            }
            return;
        case CN_IR_BINARY_EQUAL: cn_ir_opt_replace_with_bool(allocator, expression, lhs == rhs); return;
        case CN_IR_BINARY_NOT_EQUAL: cn_ir_opt_replace_with_bool(allocator, expression, lhs != rhs); return;
        case CN_IR_BINARY_LESS: cn_ir_opt_replace_with_bool(allocator, expression, lhs < rhs); return;
        case CN_IR_BINARY_LESS_EQUAL: cn_ir_opt_replace_with_bool(allocator, expression, lhs <= rhs); return;
        case CN_IR_BINARY_GREATER: cn_ir_opt_replace_with_bool(allocator, expression, lhs > rhs); return;
        case CN_IR_BINARY_GREATER_EQUAL: cn_ir_opt_replace_with_bool(allocator, expression, lhs >= rhs); return;
        case CN_IR_BINARY_AND:
        case CN_IR_BINARY_OR:
            return;
        }
    }

    if (cn_ir_expr_is_bool(left) && cn_ir_expr_is_bool(right)) {
        bool lhs = left->data.bool_value;
        bool rhs = right->data.bool_value;

        switch (expression->data.binary.op) {
        case CN_IR_BINARY_EQUAL: cn_ir_opt_replace_with_bool(allocator, expression, lhs == rhs); return;
        case CN_IR_BINARY_NOT_EQUAL: cn_ir_opt_replace_with_bool(allocator, expression, lhs != rhs); return;
        case CN_IR_BINARY_AND: cn_ir_opt_replace_with_bool(allocator, expression, lhs && rhs); return;
        case CN_IR_BINARY_OR: cn_ir_opt_replace_with_bool(allocator, expression, lhs || rhs); return;
        default:
            return;
        }
    }

    if (cn_ir_expr_is_string(left) && cn_ir_expr_is_string(right)) {
        bool equal = cn_sv_eq(left->data.string_value, right->data.string_value);
        if (expression->data.binary.op == CN_IR_BINARY_EQUAL) {
            cn_ir_opt_replace_with_bool(allocator, expression, equal);
        } else if (expression->data.binary.op == CN_IR_BINARY_NOT_EQUAL) {
            cn_ir_opt_replace_with_bool(allocator, expression, !equal);
        }
    }
}

static void cn_ir_optimize_expr(cn_allocator *allocator, cn_ir_expr *expression) {
    if (expression == NULL) {
        return;
    }

    switch (expression->kind) {
    case CN_IR_EXPR_UNARY:
        cn_ir_optimize_expr(allocator, expression->data.unary.operand);
        cn_ir_opt_fold_unary(allocator, expression);
        return;
    case CN_IR_EXPR_BINARY:
        cn_ir_optimize_expr(allocator, expression->data.binary.left);
        cn_ir_optimize_expr(allocator, expression->data.binary.right);
        cn_ir_opt_fold_binary(allocator, expression);
        return;
    case CN_IR_EXPR_CALL:
        for (size_t i = 0; i < expression->data.call.arguments.count; ++i) {
            cn_ir_optimize_expr(allocator, expression->data.call.arguments.items[i]);
        }
        return;
    case CN_IR_EXPR_ARRAY_LITERAL:
        for (size_t i = 0; i < expression->data.array_literal.items.count; ++i) {
            cn_ir_optimize_expr(allocator, expression->data.array_literal.items.items[i]);
        }
        return;
    case CN_IR_EXPR_INDEX:
        cn_ir_optimize_expr(allocator, expression->data.index.base);
        cn_ir_optimize_expr(allocator, expression->data.index.index);
        return;
    case CN_IR_EXPR_FIELD:
        cn_ir_optimize_expr(allocator, expression->data.field.base);
        return;
    case CN_IR_EXPR_STRUCT_LITERAL:
        for (size_t i = 0; i < expression->data.struct_literal.fields.count; ++i) {
            cn_ir_optimize_expr(allocator, expression->data.struct_literal.fields.items[i].value);
        }
        return;
    case CN_IR_EXPR_OK:
        cn_ir_optimize_expr(allocator, expression->data.ok_expr.value);
        return;
    case CN_IR_EXPR_ADDR:
        cn_ir_optimize_expr(allocator, expression->data.addr_expr.target);
        return;
    case CN_IR_EXPR_DEREF:
        cn_ir_optimize_expr(allocator, expression->data.deref_expr.target);
        return;
    case CN_IR_EXPR_INT:
    case CN_IR_EXPR_BOOL:
    case CN_IR_EXPR_STRING:
    case CN_IR_EXPR_LOCAL:
    case CN_IR_EXPR_ERR:
    case CN_IR_EXPR_ALLOC:
        return;
    }
}

static void cn_ir_optimize_block(cn_allocator *allocator, cn_ir_block *block) {
    size_t write_index = 0;

    for (size_t i = 0; i < block->statements.count; ++i) {
        cn_ir_stmt *statement = block->statements.items[i];

        switch (statement->kind) {
        case CN_IR_STMT_LET:
            cn_ir_optimize_expr(allocator, statement->data.let_stmt.initializer);
            break;
        case CN_IR_STMT_ASSIGN:
            cn_ir_optimize_expr(allocator, statement->data.assign_stmt.target);
            cn_ir_optimize_expr(allocator, statement->data.assign_stmt.value);
            break;
        case CN_IR_STMT_RETURN:
            cn_ir_optimize_expr(allocator, statement->data.return_stmt.value);
            break;
        case CN_IR_STMT_EXPR:
            cn_ir_optimize_expr(allocator, statement->data.expr_stmt.value);
            break;
        case CN_IR_STMT_IF:
            cn_ir_optimize_expr(allocator, statement->data.if_stmt.condition);
            cn_ir_optimize_block(allocator, statement->data.if_stmt.then_block);
            if (statement->data.if_stmt.else_block != NULL) {
                cn_ir_optimize_block(allocator, statement->data.if_stmt.else_block);
            }
            break;
        case CN_IR_STMT_WHILE:
            cn_ir_optimize_expr(allocator, statement->data.while_stmt.condition);
            cn_ir_optimize_block(allocator, statement->data.while_stmt.body);
            break;
        case CN_IR_STMT_LOOP:
            cn_ir_optimize_block(allocator, statement->data.loop_stmt.body);
            break;
        case CN_IR_STMT_FOR:
            cn_ir_optimize_expr(allocator, statement->data.for_stmt.start);
            cn_ir_optimize_expr(allocator, statement->data.for_stmt.end);
            cn_ir_optimize_block(allocator, statement->data.for_stmt.body);
            break;
        case CN_IR_STMT_FREE:
            cn_ir_optimize_expr(allocator, statement->data.free_stmt.value);
            break;
        }

        block->statements.items[write_index++] = statement;
        if (statement->kind == CN_IR_STMT_RETURN) {
            for (size_t j = i + 1; j < block->statements.count; ++j) {
                cn_ir_opt_stmt_destroy(allocator, block->statements.items[j]);
            }
            break;
        }
    }

    block->statements.count = write_index;
}

void cn_ir_optimize_program(cn_allocator *allocator, cn_ir_program *program) {
    for (size_t module_index = 0; module_index < program->modules.count; ++module_index) {
        cn_ir_module *module = program->modules.items[module_index];

        for (size_t const_index = 0; const_index < module->consts.count; ++const_index) {
            cn_ir_optimize_expr(allocator, module->consts.items[const_index]->initializer);
        }

        for (size_t function_index = 0; function_index < module->functions.count; ++function_index) {
            cn_ir_optimize_block(allocator, module->functions.items[function_index]->body);
        }
    }
}
