#ifndef CNEGATIVE_TOKEN_H
#define CNEGATIVE_TOKEN_H

#include "cnegative/common.h"
#include "cnegative/memory.h"

typedef enum cn_token_kind {
    CN_TOKEN_EOF,
    CN_TOKEN_IDENTIFIER,
    CN_TOKEN_INT_LITERAL,
    CN_TOKEN_STRING_LITERAL,
    CN_TOKEN_TRUE,
    CN_TOKEN_FALSE,
    CN_TOKEN_NULL,
    CN_TOKEN_FN,
    CN_TOKEN_PFN,
    CN_TOKEN_CONST,
    CN_TOKEN_PCONST,
    CN_TOKEN_LET,
    CN_TOKEN_MUT,
    CN_TOKEN_RETURN,
    CN_TOKEN_IF,
    CN_TOKEN_ELSE,
    CN_TOKEN_WHILE,
    CN_TOKEN_LOOP,
    CN_TOKEN_FOR,
    CN_TOKEN_IN,
    CN_TOKEN_STRUCT,
    CN_TOKEN_PSTRUCT,
    CN_TOKEN_IMPORT,
    CN_TOKEN_AS,
    CN_TOKEN_INT,
    CN_TOKEN_U8,
    CN_TOKEN_BYTE,
    CN_TOKEN_BOOL,
    CN_TOKEN_STR,
    CN_TOKEN_VOID,
    CN_TOKEN_RESULT,
    CN_TOKEN_OK,
    CN_TOKEN_ERR,
    CN_TOKEN_PTR,
    CN_TOKEN_ALLOC,
    CN_TOKEN_ZALLOC,
    CN_TOKEN_ADDR,
    CN_TOKEN_DEREF,
    CN_TOKEN_FREE,
    CN_TOKEN_DEFER,
    CN_TOKEN_TRY,
    CN_TOKEN_ZONE,
    CN_TOKEN_SLICE,
    CN_TOKEN_LPAREN,
    CN_TOKEN_RPAREN,
    CN_TOKEN_LBRACE,
    CN_TOKEN_RBRACE,
    CN_TOKEN_LBRACKET,
    CN_TOKEN_RBRACKET,
    CN_TOKEN_COLON,
    CN_TOKEN_SEMICOLON,
    CN_TOKEN_COMMA,
    CN_TOKEN_DOT,
    CN_TOKEN_RANGE,
    CN_TOKEN_PLUS,
    CN_TOKEN_MINUS,
    CN_TOKEN_STAR,
    CN_TOKEN_SLASH,
    CN_TOKEN_PERCENT,
    CN_TOKEN_BANG,
    CN_TOKEN_EQUAL,
    CN_TOKEN_EQUAL_EQUAL,
    CN_TOKEN_BANG_EQUAL,
    CN_TOKEN_LESS,
    CN_TOKEN_LESS_EQUAL,
    CN_TOKEN_GREATER,
    CN_TOKEN_GREATER_EQUAL,
    CN_TOKEN_AMP_AMP,
    CN_TOKEN_PIPE_PIPE
} cn_token_kind;

typedef struct cn_token {
    cn_token_kind kind;
    cn_strview lexeme;
    size_t offset;
    size_t line;
    size_t column;
} cn_token;

typedef struct cn_token_buffer {
    cn_allocator *allocator;
    cn_token *items;
    size_t count;
    size_t capacity;
} cn_token_buffer;

const char *cn_token_kind_name(cn_token_kind kind);
void cn_token_buffer_init(cn_token_buffer *buffer, cn_allocator *allocator);
void cn_token_buffer_destroy(cn_token_buffer *buffer);
bool cn_token_buffer_push(cn_token_buffer *buffer, cn_token token);

#endif
