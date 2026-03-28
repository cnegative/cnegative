#ifndef CNEGATIVE_PARSER_H
#define CNEGATIVE_PARSER_H

#include "cnegative/ast.h"
#include "cnegative/diagnostics.h"
#include "cnegative/token.h"

typedef struct cn_parser {
    cn_allocator *allocator;
    const cn_token_buffer *tokens;
    cn_diag_bag *diagnostics;
    size_t index;
} cn_parser;

void cn_parser_init(cn_parser *parser, cn_allocator *allocator, const cn_token_buffer *tokens, cn_diag_bag *diagnostics);
cn_program *cn_parse_program(cn_parser *parser);

#endif
