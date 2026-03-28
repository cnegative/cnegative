#ifndef CNEGATIVE_LEXER_H
#define CNEGATIVE_LEXER_H

#include "cnegative/diagnostics.h"
#include "cnegative/source.h"
#include "cnegative/token.h"

typedef struct cn_lexer {
    const cn_source *source;
    cn_diag_bag *diagnostics;
    size_t index;
    size_t line;
    size_t column;
} cn_lexer;

void cn_lexer_init(cn_lexer *lexer, const cn_source *source, cn_diag_bag *diagnostics);
bool cn_lexer_run(cn_lexer *lexer, cn_token_buffer *tokens);

#endif
