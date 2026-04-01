#include "cnegative/token.h"

const char *cn_token_kind_name(cn_token_kind kind) {
    switch (kind) {
    case CN_TOKEN_EOF: return "eof";
    case CN_TOKEN_IDENTIFIER: return "identifier";
    case CN_TOKEN_INT_LITERAL: return "int-literal";
    case CN_TOKEN_STRING_LITERAL: return "string-literal";
    case CN_TOKEN_TRUE: return "true";
    case CN_TOKEN_FALSE: return "false";
    case CN_TOKEN_FN: return "fn";
    case CN_TOKEN_PFN: return "pfn";
    case CN_TOKEN_CONST: return "const";
    case CN_TOKEN_PCONST: return "pconst";
    case CN_TOKEN_LET: return "let";
    case CN_TOKEN_MUT: return "mut";
    case CN_TOKEN_RETURN: return "return";
    case CN_TOKEN_IF: return "if";
    case CN_TOKEN_ELSE: return "else";
    case CN_TOKEN_WHILE: return "while";
    case CN_TOKEN_LOOP: return "loop";
    case CN_TOKEN_FOR: return "for";
    case CN_TOKEN_IN: return "in";
    case CN_TOKEN_STRUCT: return "struct";
    case CN_TOKEN_PSTRUCT: return "pstruct";
    case CN_TOKEN_IMPORT: return "import";
    case CN_TOKEN_AS: return "as";
    case CN_TOKEN_INT: return "int";
    case CN_TOKEN_U8: return "u8";
    case CN_TOKEN_BYTE: return "byte";
    case CN_TOKEN_BOOL: return "bool";
    case CN_TOKEN_STR: return "str";
    case CN_TOKEN_VOID: return "void";
    case CN_TOKEN_RESULT: return "result";
    case CN_TOKEN_OK: return "ok";
    case CN_TOKEN_ERR: return "err";
    case CN_TOKEN_PTR: return "ptr";
    case CN_TOKEN_ALLOC: return "alloc";
    case CN_TOKEN_ADDR: return "addr";
    case CN_TOKEN_DEREF: return "deref";
    case CN_TOKEN_FREE: return "free";
    case CN_TOKEN_DEFER: return "defer";
    case CN_TOKEN_TRY: return "try";
    case CN_TOKEN_SLICE: return "slice";
    case CN_TOKEN_LPAREN: return "(";
    case CN_TOKEN_RPAREN: return ")";
    case CN_TOKEN_LBRACE: return "{";
    case CN_TOKEN_RBRACE: return "}";
    case CN_TOKEN_LBRACKET: return "[";
    case CN_TOKEN_RBRACKET: return "]";
    case CN_TOKEN_COLON: return ":";
    case CN_TOKEN_SEMICOLON: return ";";
    case CN_TOKEN_COMMA: return ",";
    case CN_TOKEN_DOT: return ".";
    case CN_TOKEN_RANGE: return "..";
    case CN_TOKEN_PLUS: return "+";
    case CN_TOKEN_MINUS: return "-";
    case CN_TOKEN_STAR: return "*";
    case CN_TOKEN_SLASH: return "/";
    case CN_TOKEN_PERCENT: return "%";
    case CN_TOKEN_BANG: return "!";
    case CN_TOKEN_EQUAL: return "=";
    case CN_TOKEN_EQUAL_EQUAL: return "==";
    case CN_TOKEN_BANG_EQUAL: return "!=";
    case CN_TOKEN_LESS: return "<";
    case CN_TOKEN_LESS_EQUAL: return "<=";
    case CN_TOKEN_GREATER: return ">";
    case CN_TOKEN_GREATER_EQUAL: return ">=";
    case CN_TOKEN_AMP_AMP: return "&&";
    case CN_TOKEN_PIPE_PIPE: return "||";
    }

    return "unknown-token";
}

void cn_token_buffer_init(cn_token_buffer *buffer, cn_allocator *allocator) {
    buffer->allocator = allocator;
    buffer->items = NULL;
    buffer->count = 0;
    buffer->capacity = 0;
}

void cn_token_buffer_destroy(cn_token_buffer *buffer) {
    CN_FREE(buffer->allocator, buffer->items);
    buffer->items = NULL;
    buffer->count = 0;
    buffer->capacity = 0;
}

bool cn_token_buffer_push(cn_token_buffer *buffer, cn_token token) {
    if (buffer->count == buffer->capacity) {
        size_t new_capacity = buffer->capacity == 0 ? 64 : buffer->capacity * 2;
        buffer->items = CN_REALLOC(buffer->allocator, buffer->items, cn_token, new_capacity);
        buffer->capacity = new_capacity;
    }

    buffer->items[buffer->count++] = token;
    return true;
}
