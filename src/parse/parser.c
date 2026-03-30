#include "cnegative/parser.h"

static const cn_token *cn_parser_current(cn_parser *parser) {
    return &parser->tokens->items[parser->index];
}

static const cn_token *cn_parser_previous(cn_parser *parser) {
    return &parser->tokens->items[parser->index - 1];
}

static bool cn_parser_is_at_end(cn_parser *parser) {
    return cn_parser_current(parser)->kind == CN_TOKEN_EOF;
}

static const cn_token *cn_parser_advance(cn_parser *parser) {
    if (!cn_parser_is_at_end(parser)) {
        parser->index += 1;
    }
    return cn_parser_previous(parser);
}

static bool cn_parser_check(cn_parser *parser, cn_token_kind kind) {
    if (cn_parser_is_at_end(parser)) {
        return kind == CN_TOKEN_EOF;
    }
    return cn_parser_current(parser)->kind == kind;
}

static bool cn_parser_match(cn_parser *parser, cn_token_kind kind) {
    if (!cn_parser_check(parser, kind)) {
        return false;
    }

    cn_parser_advance(parser);
    return true;
}

static const cn_token *cn_parser_peek(cn_parser *parser, size_t distance) {
    size_t index = parser->index + distance;
    if (index >= parser->tokens->count) {
        index = parser->tokens->count - 1;
    }
    return &parser->tokens->items[index];
}

static const cn_token *cn_parser_consume(cn_parser *parser, cn_token_kind kind, const char *message) {
    if (cn_parser_check(parser, kind)) {
        return cn_parser_advance(parser);
    }

    cn_diag_emit(
        parser->diagnostics,
        CN_DIAG_ERROR,
        "E1001",
        cn_parser_current(parser)->offset,
        "%s, got '%s'",
        message,
        cn_token_kind_name(cn_parser_current(parser)->kind)
    );
    return NULL;
}

static const cn_token *cn_parser_consume_field_name(cn_parser *parser) {
    if (cn_parser_check(parser, CN_TOKEN_IDENTIFIER) || cn_parser_check(parser, CN_TOKEN_OK)) {
        return cn_parser_advance(parser);
    }

    cn_diag_emit(
        parser->diagnostics,
        CN_DIAG_ERROR,
        "E1001",
        cn_parser_current(parser)->offset,
        "expected field name after '.', got '%s'",
        cn_token_kind_name(cn_parser_current(parser)->kind)
    );
    return NULL;
}

static void cn_parser_require_semicolon(cn_parser *parser, const char *message) {
    cn_parser_consume(parser, CN_TOKEN_SEMICOLON, message);
}

static bool cn_parser_is_top_level_start(cn_token_kind kind) {
    return kind == CN_TOKEN_IMPORT ||
           kind == CN_TOKEN_CONST ||
           kind == CN_TOKEN_PCONST ||
           kind == CN_TOKEN_STRUCT ||
           kind == CN_TOKEN_PSTRUCT ||
           kind == CN_TOKEN_FN ||
           kind == CN_TOKEN_PFN ||
           kind == CN_TOKEN_EOF;
}

static bool cn_parser_is_statement_start(cn_token_kind kind) {
    return kind == CN_TOKEN_LET ||
           kind == CN_TOKEN_RETURN ||
           kind == CN_TOKEN_FREE ||
           kind == CN_TOKEN_IF ||
           kind == CN_TOKEN_WHILE ||
           kind == CN_TOKEN_LOOP ||
           kind == CN_TOKEN_FOR;
}

static void cn_parser_sync_statement(cn_parser *parser) {
    while (!cn_parser_is_at_end(parser)) {
        if (cn_parser_match(parser, CN_TOKEN_SEMICOLON)) {
            return;
        }

        if (cn_parser_check(parser, CN_TOKEN_RBRACE) ||
            cn_parser_is_statement_start(cn_parser_current(parser)->kind) ||
            cn_parser_is_top_level_start(cn_parser_current(parser)->kind)) {
            return;
        }

        cn_parser_advance(parser);
    }
}

static void cn_parser_sync_top_level(cn_parser *parser) {
    while (!cn_parser_is_at_end(parser)) {
        if (cn_parser_match(parser, CN_TOKEN_SEMICOLON)) {
            return;
        }

        if (cn_parser_is_top_level_start(cn_parser_current(parser)->kind)) {
            return;
        }

        cn_parser_advance(parser);
    }
}

static cn_type_ref *cn_parse_type(cn_parser *parser);
static cn_block *cn_parse_block(cn_parser *parser);
static cn_stmt *cn_parse_statement(cn_parser *parser);
static cn_expr *cn_parse_expression(cn_parser *parser);
static cn_expr *cn_parse_struct_literal(cn_parser *parser, cn_strview module_name, const cn_token *type_token);

static cn_strview cn_parser_parse_import_path(cn_parser *parser, char **owned_module_name, cn_strview *default_alias, bool *ok) {
    const cn_token *segment = cn_parser_consume(parser, CN_TOKEN_IDENTIFIER, "expected module name after 'import'");
    if (segment == NULL) {
        *owned_module_name = NULL;
        *ok = false;
        *default_alias = cn_sv_from_parts(NULL, 0);
        return cn_sv_from_parts(NULL, 0);
    }

    cn_strview module_name = segment->lexeme;
    char *buffer = NULL;
    size_t length = segment->lexeme.length;
    *default_alias = segment->lexeme;

    while (cn_parser_match(parser, CN_TOKEN_DOT)) {
        const cn_token *next_segment = cn_parser_consume(parser, CN_TOKEN_IDENTIFIER, "expected module path segment after '.'");
        if (next_segment == NULL) {
            if (buffer != NULL) {
                CN_FREE(parser->allocator, buffer);
            }
            *owned_module_name = NULL;
            *ok = false;
            *default_alias = cn_sv_from_parts(NULL, 0);
            return cn_sv_from_parts(NULL, 0);
        }

        if (buffer == NULL) {
            buffer = CN_STRNDUP(parser->allocator, module_name.data, module_name.length);
        }

        buffer = CN_REALLOC(parser->allocator, buffer, char, length + 1 + next_segment->lexeme.length + 1);
        buffer[length] = '.';
        memcpy(buffer + length + 1, next_segment->lexeme.data, next_segment->lexeme.length);
        length += 1 + next_segment->lexeme.length;
        buffer[length] = '\0';
        module_name = cn_sv_from_parts(buffer, length);
        *default_alias = next_segment->lexeme;
    }

    *owned_module_name = buffer;
    *ok = true;
    return module_name;
}

void cn_parser_init(cn_parser *parser, cn_allocator *allocator, const cn_token_buffer *tokens, cn_diag_bag *diagnostics) {
    parser->allocator = allocator;
    parser->tokens = tokens;
    parser->diagnostics = diagnostics;
    parser->index = 0;
}

static cn_type_ref *cn_parse_type_base(cn_parser *parser) {
    const cn_token *token = cn_parser_current(parser);

    if (cn_parser_match(parser, CN_TOKEN_INT)) {
        return cn_type_create(parser->allocator, CN_TYPE_INT, token->lexeme, NULL, token->offset);
    }
    if (cn_parser_match(parser, CN_TOKEN_U8) || cn_parser_match(parser, CN_TOKEN_BYTE)) {
        return cn_type_create(parser->allocator, CN_TYPE_U8, token->lexeme, NULL, token->offset);
    }
    if (cn_parser_match(parser, CN_TOKEN_BOOL)) {
        return cn_type_create(parser->allocator, CN_TYPE_BOOL, token->lexeme, NULL, token->offset);
    }
    if (cn_parser_match(parser, CN_TOKEN_STR)) {
        return cn_type_create(parser->allocator, CN_TYPE_STR, token->lexeme, NULL, token->offset);
    }
    if (cn_parser_match(parser, CN_TOKEN_VOID)) {
        return cn_type_create(parser->allocator, CN_TYPE_VOID, token->lexeme, NULL, token->offset);
    }
    if (cn_parser_match(parser, CN_TOKEN_RESULT)) {
        cn_type_ref *inner = cn_parse_type(parser);
        if (inner == NULL) {
            return NULL;
        }
        return cn_type_create(parser->allocator, CN_TYPE_RESULT, token->lexeme, inner, token->offset);
    }
    if (cn_parser_match(parser, CN_TOKEN_PTR)) {
        cn_type_ref *inner = cn_parse_type(parser);
        if (inner == NULL) {
            return NULL;
        }
        return cn_type_create(parser->allocator, CN_TYPE_PTR, token->lexeme, inner, token->offset);
    }
    if (cn_parser_match(parser, CN_TOKEN_IDENTIFIER)) {
        if (cn_parser_match(parser, CN_TOKEN_DOT)) {
            const cn_token *type_name = cn_parser_consume(parser, CN_TOKEN_IDENTIFIER, "expected type name after module qualifier");
            if (type_name == NULL) {
                return NULL;
            }

            cn_type_ref *type = cn_type_create(parser->allocator, CN_TYPE_NAMED, type_name->lexeme, NULL, token->offset);
            type->module_name = token->lexeme;
            return type;
        }

        return cn_type_create(parser->allocator, CN_TYPE_NAMED, token->lexeme, NULL, token->offset);
    }

    cn_diag_emit(parser->diagnostics, CN_DIAG_ERROR, "E1003", token->offset, "expected a type, got '%s'", cn_token_kind_name(token->kind));
    return NULL;
}

static cn_type_ref *cn_parse_type(cn_parser *parser) {
    cn_type_ref *type = cn_parse_type_base(parser);
    if (type == NULL) {
        return NULL;
    }

    while (cn_parser_match(parser, CN_TOKEN_LBRACKET)) {
        const cn_token *size_token = cn_parser_consume(parser, CN_TOKEN_INT_LITERAL, "expected array size inside type");
        cn_parser_consume(parser, CN_TOKEN_RBRACKET, "expected ']' after array size");
        if (size_token == NULL) {
            return type;
        }

        size_t array_size = 0;
        for (size_t i = 0; i < size_token->lexeme.length; ++i) {
            array_size = (array_size * 10) + (size_t)(size_token->lexeme.data[i] - '0');
        }

        cn_type_ref *array_type = cn_type_create(parser->allocator, CN_TYPE_ARRAY, size_token->lexeme, type, type->offset);
        array_type->array_size = array_size;
        type = array_type;
    }

    return type;
}

static cn_expr *cn_parse_array_literal(cn_parser *parser, const cn_token *left_bracket) {
    cn_expr *expression = cn_expr_create(parser->allocator, CN_EXPR_ARRAY_LITERAL, left_bracket->offset);
    expression->data.array_literal.items.items = NULL;
    expression->data.array_literal.items.count = 0;
    expression->data.array_literal.items.capacity = 0;

    if (!cn_parser_check(parser, CN_TOKEN_RBRACKET)) {
        do {
            cn_expr *item = cn_parse_expression(parser);
            if (item == NULL) {
                return expression;
            }
            cn_expr_list_push(parser->allocator, &expression->data.array_literal.items, item);
        } while (cn_parser_match(parser, CN_TOKEN_COMMA));
    }

    cn_parser_consume(parser, CN_TOKEN_RBRACKET, "expected ']' after array literal");
    return expression;
}

static cn_expr *cn_parse_struct_literal(cn_parser *parser, cn_strview module_name, const cn_token *type_token) {
    cn_expr *expression = cn_expr_create(parser->allocator, CN_EXPR_STRUCT_LITERAL, type_token->offset);
    expression->data.struct_literal.module_name = module_name;
    expression->data.struct_literal.type_name = type_token->lexeme;
    expression->data.struct_literal.fields.items = NULL;
    expression->data.struct_literal.fields.count = 0;
    expression->data.struct_literal.fields.capacity = 0;

    cn_parser_consume(parser, CN_TOKEN_LBRACE, "expected '{' after struct literal type name");
    while (!cn_parser_check(parser, CN_TOKEN_RBRACE) && !cn_parser_is_at_end(parser)) {
        const cn_token *field_name = cn_parser_consume(parser, CN_TOKEN_IDENTIFIER, "expected struct field name");
        cn_parser_consume(parser, CN_TOKEN_COLON, "expected ':' after struct field name");
        cn_expr *value = cn_parse_expression(parser);
        if (field_name == NULL || value == NULL) {
            break;
        }

        cn_field_init field_init;
        field_init.name = field_name->lexeme;
        field_init.value = value;
        field_init.offset = field_name->offset;
        cn_field_init_list_push(parser->allocator, &expression->data.struct_literal.fields, field_init);
        cn_parser_match(parser, CN_TOKEN_COMMA);
    }

    cn_parser_consume(parser, CN_TOKEN_RBRACE, "expected '}' after struct literal");
    return expression;
}

static cn_expr *cn_parse_alloc_expression(cn_parser *parser, const cn_token *alloc_token) {
    cn_type_ref *type = cn_parse_type(parser);
    if (type == NULL) {
        return NULL;
    }

    cn_expr *expression = cn_expr_create(parser->allocator, CN_EXPR_ALLOC, alloc_token->offset);
    expression->data.alloc_expr.type = type;
    return expression;
}

static cn_expr *cn_parse_primary(cn_parser *parser) {
    const cn_token *token = cn_parser_current(parser);

    if (cn_parser_match(parser, CN_TOKEN_INT_LITERAL)) {
        cn_expr *expression = cn_expr_create(parser->allocator, CN_EXPR_INT, token->offset);
        int64_t value = 0;
        for (size_t i = 0; i < token->lexeme.length; ++i) {
            value = (value * 10) + (token->lexeme.data[i] - '0');
        }
        expression->data.int_value = value;
        return expression;
    }

    if (cn_parser_match(parser, CN_TOKEN_TRUE)) {
        cn_expr *expression = cn_expr_create(parser->allocator, CN_EXPR_BOOL, token->offset);
        expression->data.bool_value = true;
        return expression;
    }

    if (cn_parser_match(parser, CN_TOKEN_FALSE)) {
        cn_expr *expression = cn_expr_create(parser->allocator, CN_EXPR_BOOL, token->offset);
        expression->data.bool_value = false;
        return expression;
    }

    if (cn_parser_match(parser, CN_TOKEN_STRING_LITERAL)) {
        cn_expr *expression = cn_expr_create(parser->allocator, CN_EXPR_STRING, token->offset);
        expression->data.string_value = token->lexeme;
        return expression;
    }

    if (cn_parser_match(parser, CN_TOKEN_ERR)) {
        return cn_expr_create(parser->allocator, CN_EXPR_ERR, token->offset);
    }

    if (cn_parser_match(parser, CN_TOKEN_ALLOC)) {
        return cn_parse_alloc_expression(parser, token);
    }

    if (cn_parser_check(parser, CN_TOKEN_IDENTIFIER)) {
        bool starts_local_struct_literal =
            cn_parser_peek(parser, 1)->kind == CN_TOKEN_LBRACE &&
            cn_parser_peek(parser, 2)->kind == CN_TOKEN_IDENTIFIER &&
            cn_parser_peek(parser, 3)->kind == CN_TOKEN_COLON;
        bool starts_qualified_struct_literal =
            cn_parser_peek(parser, 1)->kind == CN_TOKEN_DOT &&
            cn_parser_peek(parser, 2)->kind == CN_TOKEN_IDENTIFIER &&
            cn_parser_peek(parser, 3)->kind == CN_TOKEN_LBRACE &&
            cn_parser_peek(parser, 4)->kind == CN_TOKEN_IDENTIFIER &&
            cn_parser_peek(parser, 5)->kind == CN_TOKEN_COLON;

        cn_parser_advance(parser);
        if (starts_qualified_struct_literal) {
            cn_parser_advance(parser);
            const cn_token *type_token = cn_parser_consume(parser, CN_TOKEN_IDENTIFIER, "expected type name after module qualifier");
            if (type_token == NULL) {
                return NULL;
            }
            return cn_parse_struct_literal(parser, token->lexeme, type_token);
        }

        if (starts_local_struct_literal) {
            return cn_parse_struct_literal(parser, cn_sv_from_parts(NULL, 0), token);
        }

        cn_expr *expression = cn_expr_create(parser->allocator, CN_EXPR_NAME, token->offset);
        expression->data.name = token->lexeme;
        return expression;
    }

    if (cn_parser_match(parser, CN_TOKEN_LBRACKET)) {
        return cn_parse_array_literal(parser, token);
    }

    if (cn_parser_match(parser, CN_TOKEN_LPAREN)) {
        cn_expr *expression = cn_parse_expression(parser);
        cn_parser_consume(parser, CN_TOKEN_RPAREN, "expected ')' after grouped expression");
        return expression;
    }

    cn_diag_emit(parser->diagnostics, CN_DIAG_ERROR, "E1002", token->offset, "expected an expression, got '%s'", cn_token_kind_name(token->kind));
    return NULL;
}

static cn_expr *cn_parse_postfix(cn_parser *parser) {
    cn_expr *expression = cn_parse_primary(parser);
    if (expression == NULL) {
        return NULL;
    }

    for (;;) {
        if (cn_parser_match(parser, CN_TOKEN_LPAREN)) {
            cn_expr *call_expr = cn_expr_create(parser->allocator, CN_EXPR_CALL, expression->offset);
            call_expr->data.call.callee = expression;
            call_expr->data.call.arguments.items = NULL;
            call_expr->data.call.arguments.count = 0;
            call_expr->data.call.arguments.capacity = 0;

            if (!cn_parser_check(parser, CN_TOKEN_RPAREN)) {
                do {
                    cn_expr *argument = cn_parse_expression(parser);
                    if (argument == NULL) {
                        return call_expr;
                    }
                    cn_expr_list_push(parser->allocator, &call_expr->data.call.arguments, argument);
                } while (cn_parser_match(parser, CN_TOKEN_COMMA));
            }

            cn_parser_consume(parser, CN_TOKEN_RPAREN, "expected ')' after arguments");
            expression = call_expr;
            continue;
        }

        if (cn_parser_match(parser, CN_TOKEN_LBRACKET)) {
            cn_expr *index_value = cn_parse_expression(parser);
            cn_parser_consume(parser, CN_TOKEN_RBRACKET, "expected ']' after index expression");
            if (index_value == NULL) {
                return expression;
            }

            cn_expr *index_expr = cn_expr_create(parser->allocator, CN_EXPR_INDEX, expression->offset);
            index_expr->data.index.base = expression;
            index_expr->data.index.index = index_value;
            expression = index_expr;
            continue;
        }

        if (cn_parser_match(parser, CN_TOKEN_DOT)) {
            const cn_token *field_name = cn_parser_consume_field_name(parser);
            if (field_name == NULL) {
                return expression;
            }

            cn_expr *field_expr = cn_expr_create(parser->allocator, CN_EXPR_FIELD, expression->offset);
            field_expr->data.field.base = expression;
            field_expr->data.field.field_name = field_name->lexeme;
            expression = field_expr;
            continue;
        }

        return expression;
    }
}

static cn_expr *cn_parse_unary(cn_parser *parser) {
    const cn_token *token = cn_parser_current(parser);

    if (cn_parser_match(parser, CN_TOKEN_MINUS) || cn_parser_match(parser, CN_TOKEN_BANG)) {
        cn_unary_op op = token->kind == CN_TOKEN_MINUS ? CN_UNARY_NEGATE : CN_UNARY_NOT;
        cn_expr *operand = cn_parse_unary(parser);
        if (operand == NULL) {
            return NULL;
        }

        cn_expr *expression = cn_expr_create(parser->allocator, CN_EXPR_UNARY, token->offset);
        expression->data.unary.op = op;
        expression->data.unary.operand = operand;
        return expression;
    }

    if (cn_parser_match(parser, CN_TOKEN_ADDR)) {
        cn_expr *target = cn_parse_unary(parser);
        if (target == NULL) {
            return NULL;
        }

        cn_expr *expression = cn_expr_create(parser->allocator, CN_EXPR_ADDR, token->offset);
        expression->data.addr_expr.target = target;
        return expression;
    }

    if (cn_parser_match(parser, CN_TOKEN_DEREF)) {
        cn_expr *target = cn_parse_unary(parser);
        if (target == NULL) {
            return NULL;
        }

        cn_expr *expression = cn_expr_create(parser->allocator, CN_EXPR_DEREF, token->offset);
        expression->data.deref_expr.target = target;
        return expression;
    }

    return cn_parse_postfix(parser);
}

static cn_binary_op cn_binary_from_token(cn_token_kind kind) {
    switch (kind) {
    case CN_TOKEN_PLUS: return CN_BINARY_ADD;
    case CN_TOKEN_MINUS: return CN_BINARY_SUB;
    case CN_TOKEN_STAR: return CN_BINARY_MUL;
    case CN_TOKEN_SLASH: return CN_BINARY_DIV;
    case CN_TOKEN_EQUAL_EQUAL: return CN_BINARY_EQUAL;
    case CN_TOKEN_BANG_EQUAL: return CN_BINARY_NOT_EQUAL;
    case CN_TOKEN_LESS: return CN_BINARY_LESS;
    case CN_TOKEN_LESS_EQUAL: return CN_BINARY_LESS_EQUAL;
    case CN_TOKEN_GREATER: return CN_BINARY_GREATER;
    case CN_TOKEN_GREATER_EQUAL: return CN_BINARY_GREATER_EQUAL;
    case CN_TOKEN_AMP_AMP: return CN_BINARY_AND;
    case CN_TOKEN_PIPE_PIPE: return CN_BINARY_OR;
    default: return CN_BINARY_ADD;
    }
}

static cn_expr *cn_parse_binary_chain(cn_parser *parser, cn_expr *(*subparser)(cn_parser *), const cn_token_kind *kinds, size_t kind_count) {
    cn_expr *expression = subparser(parser);
    if (expression == NULL) {
        return NULL;
    }

    for (;;) {
        bool matched = false;
        for (size_t i = 0; i < kind_count; ++i) {
            if (cn_parser_check(parser, kinds[i])) {
                matched = true;
                break;
            }
        }

        if (!matched) {
            return expression;
        }

        const cn_token *operator_token = cn_parser_advance(parser);
        cn_expr *right = subparser(parser);
        if (right == NULL) {
            return expression;
        }

        cn_expr *binary = cn_expr_create(parser->allocator, CN_EXPR_BINARY, operator_token->offset);
        binary->data.binary.op = cn_binary_from_token(operator_token->kind);
        binary->data.binary.left = expression;
        binary->data.binary.right = right;
        expression = binary;
    }
}

static cn_expr *cn_parse_factor(cn_parser *parser) {
    const cn_token_kind kinds[] = {CN_TOKEN_STAR, CN_TOKEN_SLASH};
    return cn_parse_binary_chain(parser, cn_parse_unary, kinds, 2);
}

static cn_expr *cn_parse_term(cn_parser *parser) {
    const cn_token_kind kinds[] = {CN_TOKEN_PLUS, CN_TOKEN_MINUS};
    return cn_parse_binary_chain(parser, cn_parse_factor, kinds, 2);
}

static cn_expr *cn_parse_comparison(cn_parser *parser) {
    const cn_token_kind kinds[] = {CN_TOKEN_LESS, CN_TOKEN_LESS_EQUAL, CN_TOKEN_GREATER, CN_TOKEN_GREATER_EQUAL};
    return cn_parse_binary_chain(parser, cn_parse_term, kinds, 4);
}

static cn_expr *cn_parse_equality(cn_parser *parser) {
    const cn_token_kind kinds[] = {CN_TOKEN_EQUAL_EQUAL, CN_TOKEN_BANG_EQUAL};
    return cn_parse_binary_chain(parser, cn_parse_comparison, kinds, 2);
}

static cn_expr *cn_parse_and(cn_parser *parser) {
    const cn_token_kind kinds[] = {CN_TOKEN_AMP_AMP};
    return cn_parse_binary_chain(parser, cn_parse_equality, kinds, 1);
}

static cn_expr *cn_parse_or(cn_parser *parser) {
    const cn_token_kind kinds[] = {CN_TOKEN_PIPE_PIPE};
    return cn_parse_binary_chain(parser, cn_parse_and, kinds, 1);
}

static cn_expr *cn_parse_ok_expression(cn_parser *parser) {
    const cn_token *token = cn_parser_current(parser);
    if (!cn_parser_match(parser, CN_TOKEN_OK)) {
        return cn_parse_or(parser);
    }

    cn_expr *value = cn_parse_expression(parser);
    if (value == NULL) {
        return NULL;
    }

    cn_expr *expression = cn_expr_create(parser->allocator, CN_EXPR_OK, token->offset);
    expression->data.ok_expr.value = value;
    return expression;
}

static cn_expr *cn_parse_expression(cn_parser *parser) {
    return cn_parse_ok_expression(parser);
}

static bool cn_expr_is_assignable(const cn_expr *expression) {
    return expression->kind == CN_EXPR_NAME ||
           expression->kind == CN_EXPR_FIELD ||
           expression->kind == CN_EXPR_INDEX ||
           expression->kind == CN_EXPR_DEREF;
}

static cn_stmt *cn_parse_let_statement(cn_parser *parser, const cn_token *let_token) {
    bool is_mutable = cn_parser_match(parser, CN_TOKEN_MUT);
    const cn_token *name = cn_parser_consume(parser, CN_TOKEN_IDENTIFIER, "expected local name after 'let'");
    cn_parser_consume(parser, CN_TOKEN_COLON, "expected ':' after local name");
    cn_type_ref *type = cn_parse_type(parser);
    cn_parser_consume(parser, CN_TOKEN_EQUAL, "expected '=' after variable type");
    cn_expr *initializer = cn_parse_expression(parser);
    cn_parser_require_semicolon(parser, "expected ';' after let statement");

    if (name == NULL || type == NULL || initializer == NULL) {
        return NULL;
    }

    cn_stmt *statement = cn_stmt_create(parser->allocator, CN_STMT_LET, let_token->offset);
    statement->data.let_stmt.name = name->lexeme;
    statement->data.let_stmt.is_mutable = is_mutable;
    statement->data.let_stmt.type = type;
    statement->data.let_stmt.initializer = initializer;
    return statement;
}

static cn_stmt *cn_parse_return_statement(cn_parser *parser, const cn_token *return_token) {
    cn_stmt *statement = cn_stmt_create(parser->allocator, CN_STMT_RETURN, return_token->offset);
    statement->data.return_stmt.value = NULL;

    if (!cn_parser_check(parser, CN_TOKEN_SEMICOLON)) {
        statement->data.return_stmt.value = cn_parse_expression(parser);
    }

    cn_parser_require_semicolon(parser, "expected ';' after return statement");
    return statement;
}

static cn_stmt *cn_parse_free_statement(cn_parser *parser, const cn_token *free_token) {
    cn_expr *value = cn_parse_expression(parser);
    cn_parser_require_semicolon(parser, "expected ';' after free statement");
    if (value == NULL) {
        return NULL;
    }

    cn_stmt *statement = cn_stmt_create(parser->allocator, CN_STMT_FREE, free_token->offset);
    statement->data.free_stmt.value = value;
    return statement;
}

static cn_stmt *cn_parse_if_statement(cn_parser *parser, const cn_token *if_token) {
    cn_expr *condition = cn_parse_expression(parser);
    cn_block *then_block = cn_parse_block(parser);
    cn_block *else_block = NULL;

    if (condition == NULL || then_block == NULL) {
        return NULL;
    }

    if (cn_parser_match(parser, CN_TOKEN_ELSE)) {
        if (cn_parser_check(parser, CN_TOKEN_IF)) {
            const cn_token *nested_if = cn_parser_advance(parser);
            cn_stmt *nested = cn_parse_if_statement(parser, nested_if);
            if (nested != NULL) {
                else_block = cn_block_create(parser->allocator, nested_if->offset);
                cn_stmt_list_push(parser->allocator, &else_block->statements, nested);
            }
        } else {
            else_block = cn_parse_block(parser);
        }
    }

    cn_stmt *statement = cn_stmt_create(parser->allocator, CN_STMT_IF, if_token->offset);
    statement->data.if_stmt.condition = condition;
    statement->data.if_stmt.then_block = then_block;
    statement->data.if_stmt.else_block = else_block;
    return statement;
}

static cn_stmt *cn_parse_while_statement(cn_parser *parser, const cn_token *while_token) {
    cn_expr *condition = cn_parse_expression(parser);
    cn_block *body = cn_parse_block(parser);
    if (condition == NULL || body == NULL) {
        return NULL;
    }

    cn_stmt *statement = cn_stmt_create(parser->allocator, CN_STMT_WHILE, while_token->offset);
    statement->data.while_stmt.condition = condition;
    statement->data.while_stmt.body = body;
    return statement;
}

static cn_stmt *cn_parse_loop_statement(cn_parser *parser, const cn_token *loop_token) {
    cn_block *body = cn_parse_block(parser);
    if (body == NULL) {
        return NULL;
    }

    cn_stmt *statement = cn_stmt_create(parser->allocator, CN_STMT_LOOP, loop_token->offset);
    statement->data.loop_stmt.body = body;
    return statement;
}

static cn_stmt *cn_parse_for_statement(cn_parser *parser, const cn_token *for_token) {
    const cn_token *name = cn_parser_consume(parser, CN_TOKEN_IDENTIFIER, "expected loop binding name after 'for'");
    cn_parser_consume(parser, CN_TOKEN_COLON, "expected ':' after loop binding name");
    cn_type_ref *type = cn_parse_type(parser);
    cn_parser_consume(parser, CN_TOKEN_IN, "expected 'in' in range loop");
    cn_expr *start = cn_parse_expression(parser);
    cn_parser_consume(parser, CN_TOKEN_RANGE, "expected '..' in range loop");
    cn_expr *end = cn_parse_expression(parser);
    cn_block *body = cn_parse_block(parser);

    if (name == NULL || type == NULL || start == NULL || end == NULL || body == NULL) {
        return NULL;
    }

    cn_stmt *statement = cn_stmt_create(parser->allocator, CN_STMT_FOR, for_token->offset);
    statement->data.for_stmt.name = name->lexeme;
    statement->data.for_stmt.type = type;
    statement->data.for_stmt.start = start;
    statement->data.for_stmt.end = end;
    statement->data.for_stmt.body = body;
    return statement;
}

static cn_stmt *cn_parse_assignment_or_expr_statement(cn_parser *parser) {
    cn_expr *left = cn_parse_expression(parser);
    if (left == NULL) {
        return NULL;
    }

    if (cn_parser_match(parser, CN_TOKEN_EQUAL)) {
        cn_expr *value = cn_parse_expression(parser);
        cn_parser_require_semicolon(parser, "expected ';' after assignment");
        if (value == NULL) {
            return NULL;
        }

        if (!cn_expr_is_assignable(left)) {
            cn_diag_emit(parser->diagnostics, CN_DIAG_ERROR, "E1002", left->offset, "invalid assignment target");
        }

        cn_stmt *statement = cn_stmt_create(parser->allocator, CN_STMT_ASSIGN, left->offset);
        statement->data.assign_stmt.target = left;
        statement->data.assign_stmt.value = value;
        return statement;
    }

    cn_parser_require_semicolon(parser, "expected ';' after expression statement");
    cn_stmt *statement = cn_stmt_create(parser->allocator, CN_STMT_EXPR, left->offset);
    statement->data.expr_stmt.value = left;
    return statement;
}

static cn_stmt *cn_parse_statement(cn_parser *parser) {
    if (cn_parser_check(parser, CN_TOKEN_LET)) {
        const cn_token *token = cn_parser_advance(parser);
        return cn_parse_let_statement(parser, token);
    }
    if (cn_parser_check(parser, CN_TOKEN_RETURN)) {
        const cn_token *token = cn_parser_advance(parser);
        return cn_parse_return_statement(parser, token);
    }
    if (cn_parser_check(parser, CN_TOKEN_FREE)) {
        const cn_token *token = cn_parser_advance(parser);
        return cn_parse_free_statement(parser, token);
    }
    if (cn_parser_check(parser, CN_TOKEN_IF)) {
        const cn_token *token = cn_parser_advance(parser);
        return cn_parse_if_statement(parser, token);
    }
    if (cn_parser_check(parser, CN_TOKEN_WHILE)) {
        const cn_token *token = cn_parser_advance(parser);
        return cn_parse_while_statement(parser, token);
    }
    if (cn_parser_check(parser, CN_TOKEN_LOOP)) {
        const cn_token *token = cn_parser_advance(parser);
        return cn_parse_loop_statement(parser, token);
    }
    if (cn_parser_check(parser, CN_TOKEN_FOR)) {
        const cn_token *token = cn_parser_advance(parser);
        return cn_parse_for_statement(parser, token);
    }

    return cn_parse_assignment_or_expr_statement(parser);
}

static cn_block *cn_parse_block(cn_parser *parser) {
    const cn_token *left_brace = cn_parser_consume(parser, CN_TOKEN_LBRACE, "expected '{' to start block");
    if (left_brace == NULL) {
        return NULL;
    }

    cn_block *block = cn_block_create(parser->allocator, left_brace->offset);
    while (!cn_parser_check(parser, CN_TOKEN_RBRACE) && !cn_parser_is_at_end(parser)) {
        cn_stmt *statement = cn_parse_statement(parser);
        if (statement == NULL) {
            cn_parser_sync_statement(parser);
            continue;
        }
        cn_stmt_list_push(parser->allocator, &block->statements, statement);
    }

    cn_parser_consume(parser, CN_TOKEN_RBRACE, "expected '}' after block");
    return block;
}

static bool cn_parse_import_decl(cn_parser *parser, cn_program *program, const cn_token *import_token) {
    cn_import_decl import_decl;
    cn_strview default_alias = cn_sv_from_parts(NULL, 0);
    char *owned_module_name = NULL;
    bool has_module_name = false;

    import_decl.module_name = cn_parser_parse_import_path(parser, &owned_module_name, &default_alias, &has_module_name);
    if (!has_module_name) {
        return false;
    }

    import_decl.owned_module_name = owned_module_name;
    import_decl.alias = default_alias;
    import_decl.has_alias = false;
    import_decl.offset = import_token->offset;

    if (cn_parser_match(parser, CN_TOKEN_AS)) {
        const cn_token *alias = cn_parser_consume(parser, CN_TOKEN_IDENTIFIER, "expected alias after 'as'");
        if (alias != NULL) {
            import_decl.alias = alias->lexeme;
            import_decl.has_alias = true;
        }
    }

    cn_parser_require_semicolon(parser, "expected ';' after import declaration");
    cn_program_push_import(program, import_decl);
    return true;
}

static cn_const_decl *cn_parse_const_decl(cn_parser *parser, bool is_public, const cn_token *const_token) {
    const cn_token *name = cn_parser_consume(parser, CN_TOKEN_IDENTIFIER, "expected constant name");
    if (name == NULL) {
        return NULL;
    }

    if (cn_parser_consume(parser, CN_TOKEN_COLON, "expected ':' after constant name") == NULL) {
        return NULL;
    }

    cn_type_ref *type = cn_parse_type(parser);
    if (type == NULL) {
        return NULL;
    }

    if (cn_parser_consume(parser, CN_TOKEN_EQUAL, "expected '=' after constant type") == NULL) {
        return NULL;
    }

    cn_expr *initializer = cn_parse_expression(parser);
    cn_parser_require_semicolon(parser, "expected ';' after constant declaration");
    if (initializer == NULL) {
        return NULL;
    }

    cn_const_decl *const_decl = cn_const_decl_create(parser->allocator, const_token->offset);
    const_decl->is_public = is_public;
    const_decl->name = name->lexeme;
    const_decl->type = type;
    const_decl->initializer = initializer;
    return const_decl;
}

static cn_struct_decl *cn_parse_struct_decl(cn_parser *parser, bool is_public, const cn_token *struct_token) {
    const cn_token *name = cn_parser_consume(parser, CN_TOKEN_IDENTIFIER, "expected struct name");
    if (name == NULL) {
        return NULL;
    }
    if (cn_parser_consume(parser, CN_TOKEN_LBRACE, "expected '{' after struct name") == NULL) {
        return NULL;
    }

    cn_struct_decl *struct_decl = cn_struct_decl_create(parser->allocator, struct_token->offset);
    struct_decl->is_public = is_public;
    struct_decl->name = name->lexeme;

    while (!cn_parser_check(parser, CN_TOKEN_RBRACE) && !cn_parser_is_at_end(parser)) {
        const cn_token *field_name = cn_parser_consume(parser, CN_TOKEN_IDENTIFIER, "expected struct field name");
        cn_parser_consume(parser, CN_TOKEN_COLON, "expected ':' after struct field name");
        cn_type_ref *field_type = cn_parse_type(parser);
        cn_parser_require_semicolon(parser, "expected ';' after struct field");
        if (field_name != NULL && field_type != NULL) {
            cn_struct_field field;
            field.name = field_name->lexeme;
            field.type = field_type;
            field.offset = field_name->offset;
            cn_struct_field_list_push(parser->allocator, &struct_decl->fields, field);
        }
    }

    cn_parser_consume(parser, CN_TOKEN_RBRACE, "expected '}' after struct declaration");
    return struct_decl;
}

static cn_function *cn_parse_function(cn_parser *parser, bool is_public, const cn_token *start_token) {
    if (cn_parser_consume(parser, CN_TOKEN_COLON, "expected ':' after function visibility keyword") == NULL) {
        return NULL;
    }
    cn_type_ref *return_type = cn_parse_type(parser);
    if (return_type == NULL) {
        return NULL;
    }
    const cn_token *name = cn_parser_consume(parser, CN_TOKEN_IDENTIFIER, "expected function name");
    if (name == NULL) {
        return NULL;
    }
    if (cn_parser_consume(parser, CN_TOKEN_LPAREN, "expected '(' after function name") == NULL) {
        return NULL;
    }

    cn_function *function = cn_function_create(parser->allocator, start_token->offset);
    function->is_public = is_public;
    function->return_type = return_type;

    if (!cn_parser_check(parser, CN_TOKEN_RPAREN)) {
        do {
            const cn_token *param_name = cn_parser_consume(parser, CN_TOKEN_IDENTIFIER, "expected parameter name");
            cn_parser_consume(parser, CN_TOKEN_COLON, "expected ':' after parameter name");
            cn_type_ref *param_type = cn_parse_type(parser);
            if (param_name != NULL && param_type != NULL) {
                cn_param param;
                param.name = param_name->lexeme;
                param.type = param_type;
                param.offset = param_name->offset;
                cn_param_list_push(parser->allocator, &function->parameters, param);
            }
        } while (cn_parser_match(parser, CN_TOKEN_COMMA));
    }

    if (cn_parser_consume(parser, CN_TOKEN_RPAREN, "expected ')' after parameters") == NULL) {
        return NULL;
    }
    function->name = name->lexeme;
    function->body = cn_parse_block(parser);
    if (function->body == NULL) {
        return NULL;
    }
    return function;
}

cn_program *cn_parse_program(cn_parser *parser) {
    cn_program *program = cn_program_create(parser->allocator);

    while (!cn_parser_is_at_end(parser)) {
        if (cn_parser_match(parser, CN_TOKEN_IMPORT)) {
            if (!cn_parse_import_decl(parser, program, cn_parser_previous(parser))) {
                cn_parser_sync_top_level(parser);
            }
            continue;
        }

        if (cn_parser_match(parser, CN_TOKEN_CONST)) {
            cn_const_decl *const_decl = cn_parse_const_decl(parser, false, cn_parser_previous(parser));
            if (const_decl != NULL) {
                cn_program_push_const(program, const_decl);
            } else {
                cn_parser_sync_top_level(parser);
            }
            continue;
        }

        if (cn_parser_match(parser, CN_TOKEN_PCONST)) {
            cn_const_decl *const_decl = cn_parse_const_decl(parser, true, cn_parser_previous(parser));
            if (const_decl != NULL) {
                cn_program_push_const(program, const_decl);
            } else {
                cn_parser_sync_top_level(parser);
            }
            continue;
        }

        if (cn_parser_match(parser, CN_TOKEN_STRUCT)) {
            cn_struct_decl *struct_decl = cn_parse_struct_decl(parser, false, cn_parser_previous(parser));
            if (struct_decl != NULL) {
                cn_program_push_struct(program, struct_decl);
            } else {
                cn_parser_sync_top_level(parser);
            }
            continue;
        }

        if (cn_parser_match(parser, CN_TOKEN_PSTRUCT)) {
            cn_struct_decl *struct_decl = cn_parse_struct_decl(parser, true, cn_parser_previous(parser));
            if (struct_decl != NULL) {
                cn_program_push_struct(program, struct_decl);
            } else {
                cn_parser_sync_top_level(parser);
            }
            continue;
        }

        if (cn_parser_match(parser, CN_TOKEN_FN)) {
            cn_function *function = cn_parse_function(parser, false, cn_parser_previous(parser));
            if (function != NULL) {
                cn_program_push_function(program, function);
            } else {
                cn_parser_sync_top_level(parser);
            }
            continue;
        }

        if (cn_parser_match(parser, CN_TOKEN_PFN)) {
            cn_function *function = cn_parse_function(parser, true, cn_parser_previous(parser));
            if (function != NULL) {
                cn_program_push_function(program, function);
            } else {
                cn_parser_sync_top_level(parser);
            }
            continue;
        }

        cn_diag_emit(
            parser->diagnostics,
            CN_DIAG_ERROR,
            "E1002",
            cn_parser_current(parser)->offset,
            "unexpected top-level token '%s'",
            cn_token_kind_name(cn_parser_current(parser)->kind)
        );
        cn_parser_sync_top_level(parser);
    }

    return program;
}
