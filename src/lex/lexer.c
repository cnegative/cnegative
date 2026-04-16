#include "cnegative/asm_scan.h"
#include "cnegative/lexer.h"

static char cn_lexer_peek(const cn_lexer *lexer) {
    if (lexer->index >= lexer->source->length) {
        return '\0';
    }
    return lexer->source->text[lexer->index];
}

static char cn_lexer_peek_next(const cn_lexer *lexer) {
    if (lexer->index + 1 >= lexer->source->length) {
        return '\0';
    }
    return lexer->source->text[lexer->index + 1];
}

static char cn_lexer_advance(cn_lexer *lexer) {
    char value = cn_lexer_peek(lexer);
    if (value == '\0') {
        return value;
    }

    lexer->index += 1;
    if (value == '\n') {
        lexer->line += 1;
        lexer->column = 1;
    } else {
        lexer->column += 1;
    }

    return value;
}

static bool cn_lexer_match(cn_lexer *lexer, char expected) {
    if (cn_lexer_peek(lexer) != expected) {
        return false;
    }

    cn_lexer_advance(lexer);
    return true;
}

static void cn_lexer_emit(cn_lexer *lexer, cn_token_buffer *tokens, cn_token_kind kind, size_t offset, size_t line, size_t column, size_t length) {
    cn_token token;
    token.kind = kind;
    token.offset = offset;
    token.line = line;
    token.column = column;
    token.lexeme = cn_sv_from_parts(lexer->source->text + offset, length);
    cn_token_buffer_push(tokens, token);
}

static cn_token_kind cn_keyword_kind(cn_strview text) {
    if (cn_sv_eq_cstr(text, "fn")) return CN_TOKEN_FN;
    if (cn_sv_eq_cstr(text, "pfn")) return CN_TOKEN_PFN;
    if (cn_sv_eq_cstr(text, "const")) return CN_TOKEN_CONST;
    if (cn_sv_eq_cstr(text, "pconst")) return CN_TOKEN_PCONST;
    if (cn_sv_eq_cstr(text, "let")) return CN_TOKEN_LET;
    if (cn_sv_eq_cstr(text, "mut")) return CN_TOKEN_MUT;
    if (cn_sv_eq_cstr(text, "return")) return CN_TOKEN_RETURN;
    if (cn_sv_eq_cstr(text, "if")) return CN_TOKEN_IF;
    if (cn_sv_eq_cstr(text, "else")) return CN_TOKEN_ELSE;
    if (cn_sv_eq_cstr(text, "while")) return CN_TOKEN_WHILE;
    if (cn_sv_eq_cstr(text, "loop")) return CN_TOKEN_LOOP;
    if (cn_sv_eq_cstr(text, "for")) return CN_TOKEN_FOR;
    if (cn_sv_eq_cstr(text, "in")) return CN_TOKEN_IN;
    if (cn_sv_eq_cstr(text, "struct")) return CN_TOKEN_STRUCT;
    if (cn_sv_eq_cstr(text, "pstruct")) return CN_TOKEN_PSTRUCT;
    if (cn_sv_eq_cstr(text, "import")) return CN_TOKEN_IMPORT;
    if (cn_sv_eq_cstr(text, "as")) return CN_TOKEN_AS;
    if (cn_sv_eq_cstr(text, "int")) return CN_TOKEN_INT;
    if (cn_sv_eq_cstr(text, "u8")) return CN_TOKEN_U8;
    if (cn_sv_eq_cstr(text, "byte")) return CN_TOKEN_BYTE;
    if (cn_sv_eq_cstr(text, "bool")) return CN_TOKEN_BOOL;
    if (cn_sv_eq_cstr(text, "str")) return CN_TOKEN_STR;
    if (cn_sv_eq_cstr(text, "void")) return CN_TOKEN_VOID;
    if (cn_sv_eq_cstr(text, "ok")) return CN_TOKEN_OK;
    if (cn_sv_eq_cstr(text, "err")) return CN_TOKEN_ERR;
    if (cn_sv_eq_cstr(text, "ptr")) return CN_TOKEN_PTR;
    if (cn_sv_eq_cstr(text, "alloc")) return CN_TOKEN_ALLOC;
    if (cn_sv_eq_cstr(text, "zalloc")) return CN_TOKEN_ZALLOC;
    if (cn_sv_eq_cstr(text, "addr")) return CN_TOKEN_ADDR;
    if (cn_sv_eq_cstr(text, "deref")) return CN_TOKEN_DEREF;
    if (cn_sv_eq_cstr(text, "free")) return CN_TOKEN_FREE;
    if (cn_sv_eq_cstr(text, "defer")) return CN_TOKEN_DEFER;
    if (cn_sv_eq_cstr(text, "try")) return CN_TOKEN_TRY;
    if (cn_sv_eq_cstr(text, "zone")) return CN_TOKEN_ZONE;
    if (cn_sv_eq_cstr(text, "slice")) return CN_TOKEN_SLICE;
    if (cn_sv_eq_cstr(text, "true")) return CN_TOKEN_TRUE;
    if (cn_sv_eq_cstr(text, "false")) return CN_TOKEN_FALSE;
    if (cn_sv_eq_cstr(text, "null")) return CN_TOKEN_NULL;
    return CN_TOKEN_IDENTIFIER;
}

static bool cn_is_ascii_digit(char value) {
    return value >= '0' && value <= '9';
}

static bool cn_is_ascii_alpha(char value) {
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

static void cn_lexer_skip_whitespace(cn_lexer *lexer) {
    for (;;) {
        char value = cn_lexer_peek(lexer);
        if (value == ' ' || value == '\t' || value == '\r' || value == '\n') {
            cn_lexer_advance(lexer);
            continue;
        }

        if (value == '/' && cn_lexer_peek_next(lexer) == '/') {
            while (cn_lexer_peek(lexer) != '\0' && cn_lexer_peek(lexer) != '\n') {
                cn_lexer_advance(lexer);
            }
            continue;
        }

        break;
    }
}

static void cn_lexer_scan_number(cn_lexer *lexer, cn_token_buffer *tokens, size_t offset, size_t line, size_t column) {
    size_t end = cn_scan_number_tail(lexer->source->text, lexer->source->length, lexer->index);
    lexer->column += end - lexer->index;
    lexer->index = end;

    size_t length = lexer->index - offset;
    cn_lexer_emit(lexer, tokens, CN_TOKEN_INT_LITERAL, offset, line, column, length);
}

static void cn_lexer_scan_identifier(cn_lexer *lexer, cn_token_buffer *tokens, size_t offset, size_t line, size_t column) {
    size_t end = cn_scan_identifier_tail(lexer->source->text, lexer->source->length, lexer->index);
    lexer->column += end - lexer->index;
    lexer->index = end;

    size_t length = lexer->index - offset;
    cn_strview text = cn_sv_from_parts(lexer->source->text + offset, length);
    cn_lexer_emit(lexer, tokens, cn_keyword_kind(text), offset, line, column, length);
}

static void cn_lexer_scan_string(cn_lexer *lexer, cn_token_buffer *tokens, size_t offset, size_t line, size_t column) {
    size_t content_start = lexer->index;

    while (cn_lexer_peek(lexer) != '\0' && cn_lexer_peek(lexer) != '"') {
        if (cn_lexer_peek(lexer) == '\n') {
            cn_diag_emit(lexer->diagnostics, CN_DIAG_ERROR, "E1005", offset, "unterminated string literal");
            return;
        }

        if (cn_lexer_peek(lexer) == '\\' && cn_lexer_peek_next(lexer) != '\0') {
            cn_lexer_advance(lexer);
        }
        cn_lexer_advance(lexer);
    }

    if (cn_lexer_peek(lexer) != '"') {
        cn_diag_emit(lexer->diagnostics, CN_DIAG_ERROR, "E1005", offset, "unterminated string literal");
        return;
    }

    size_t content_end = lexer->index;
    cn_lexer_advance(lexer);

    cn_token token;
    token.kind = CN_TOKEN_STRING_LITERAL;
    token.offset = offset;
    token.line = line;
    token.column = column;
    token.lexeme = cn_sv_from_parts(lexer->source->text + content_start, content_end - content_start);
    cn_token_buffer_push(tokens, token);
}

static void cn_lexer_scan_raw_string(cn_lexer *lexer, cn_token_buffer *tokens, size_t offset, size_t line, size_t column) {
    size_t content_start = lexer->index;

    while (cn_lexer_peek(lexer) != '\0' && cn_lexer_peek(lexer) != '`') {
        cn_lexer_advance(lexer);
    }

    if (cn_lexer_peek(lexer) != '`') {
        cn_diag_emit(lexer->diagnostics, CN_DIAG_ERROR, "E1005", offset, "unterminated raw string literal");
        return;
    }

    size_t content_end = lexer->index;
    cn_lexer_advance(lexer);

    cn_token token;
    token.kind = CN_TOKEN_STRING_LITERAL;
    token.offset = offset;
    token.line = line;
    token.column = column;
    token.lexeme = cn_sv_from_parts(lexer->source->text + content_start, content_end - content_start);
    cn_token_buffer_push(tokens, token);
}

void cn_lexer_init(cn_lexer *lexer, const cn_source *source, cn_diag_bag *diagnostics) {
    lexer->source = source;
    lexer->diagnostics = diagnostics;
    lexer->index = 0;
    lexer->line = 1;
    lexer->column = 1;
}

bool cn_lexer_run(cn_lexer *lexer, cn_token_buffer *tokens) {
    while (lexer->index < lexer->source->length) {
        cn_lexer_skip_whitespace(lexer);
        if (lexer->index >= lexer->source->length) {
            break;
        }

        size_t offset = lexer->index;
        size_t line = lexer->line;
        size_t column = lexer->column;
        char value = cn_lexer_advance(lexer);

        switch (value) {
        case '(':
            cn_lexer_emit(lexer, tokens, CN_TOKEN_LPAREN, offset, line, column, 1);
            break;
        case ')':
            cn_lexer_emit(lexer, tokens, CN_TOKEN_RPAREN, offset, line, column, 1);
            break;
        case '{':
            cn_lexer_emit(lexer, tokens, CN_TOKEN_LBRACE, offset, line, column, 1);
            break;
        case '}':
            cn_lexer_emit(lexer, tokens, CN_TOKEN_RBRACE, offset, line, column, 1);
            break;
        case '[':
            cn_lexer_emit(lexer, tokens, CN_TOKEN_LBRACKET, offset, line, column, 1);
            break;
        case ']':
            cn_lexer_emit(lexer, tokens, CN_TOKEN_RBRACKET, offset, line, column, 1);
            break;
        case ':':
            cn_lexer_emit(lexer, tokens, CN_TOKEN_COLON, offset, line, column, 1);
            break;
        case ';':
            cn_lexer_emit(lexer, tokens, CN_TOKEN_SEMICOLON, offset, line, column, 1);
            break;
        case ',':
            cn_lexer_emit(lexer, tokens, CN_TOKEN_COMMA, offset, line, column, 1);
            break;
        case '.':
            if (cn_lexer_match(lexer, '.')) {
                cn_lexer_emit(lexer, tokens, CN_TOKEN_RANGE, offset, line, column, 2);
            } else {
                cn_lexer_emit(lexer, tokens, CN_TOKEN_DOT, offset, line, column, 1);
            }
            break;
        case '+':
            cn_lexer_emit(lexer, tokens, CN_TOKEN_PLUS, offset, line, column, 1);
            break;
        case '-':
            cn_lexer_emit(lexer, tokens, CN_TOKEN_MINUS, offset, line, column, 1);
            break;
        case '*':
            cn_lexer_emit(lexer, tokens, CN_TOKEN_STAR, offset, line, column, 1);
            break;
        case '/':
            cn_lexer_emit(lexer, tokens, CN_TOKEN_SLASH, offset, line, column, 1);
            break;
        case '%':
            cn_lexer_emit(lexer, tokens, CN_TOKEN_PERCENT, offset, line, column, 1);
            break;
        case '!':
            cn_lexer_emit(
                lexer,
                tokens,
                cn_lexer_match(lexer, '=') ? CN_TOKEN_BANG_EQUAL : CN_TOKEN_BANG,
                offset,
                line,
                column,
                lexer->index - offset
            );
            break;
        case '=':
            cn_lexer_emit(
                lexer,
                tokens,
                cn_lexer_match(lexer, '=') ? CN_TOKEN_EQUAL_EQUAL : CN_TOKEN_EQUAL,
                offset,
                line,
                column,
                lexer->index - offset
            );
            break;
        case '<':
            cn_lexer_emit(
                lexer,
                tokens,
                cn_lexer_match(lexer, '=') ? CN_TOKEN_LESS_EQUAL : CN_TOKEN_LESS,
                offset,
                line,
                column,
                lexer->index - offset
            );
            break;
        case '>':
            cn_lexer_emit(
                lexer,
                tokens,
                cn_lexer_match(lexer, '=') ? CN_TOKEN_GREATER_EQUAL : CN_TOKEN_GREATER,
                offset,
                line,
                column,
                lexer->index - offset
            );
            break;
        case '&':
            if (cn_lexer_match(lexer, '&')) {
                cn_lexer_emit(lexer, tokens, CN_TOKEN_AMP_AMP, offset, line, column, 2);
            } else {
                cn_diag_emit(lexer->diagnostics, CN_DIAG_ERROR, "E1004", offset, "unexpected character '&'");
            }
            break;
        case '|':
            if (cn_lexer_match(lexer, '|')) {
                cn_lexer_emit(lexer, tokens, CN_TOKEN_PIPE_PIPE, offset, line, column, 2);
            } else {
                cn_diag_emit(lexer->diagnostics, CN_DIAG_ERROR, "E1004", offset, "unexpected character '|'");
            }
            break;
        case '"':
            cn_lexer_scan_string(lexer, tokens, offset, line, column);
            break;
        case '`':
            cn_lexer_scan_raw_string(lexer, tokens, offset, line, column);
            break;
        default:
            if (cn_is_ascii_digit(value)) {
                cn_lexer_scan_number(lexer, tokens, offset, line, column);
            } else if (cn_is_ascii_alpha(value) || value == '_') {
                cn_lexer_scan_identifier(lexer, tokens, offset, line, column);
            } else {
                cn_diag_emit(lexer->diagnostics, CN_DIAG_ERROR, "E1004", offset, "unexpected character '%c'", value);
            }
            break;
        }
    }

    cn_token eof_token;
    eof_token.kind = CN_TOKEN_EOF;
    eof_token.lexeme = cn_sv_from_parts(lexer->source->text + lexer->source->length, 0);
    eof_token.offset = lexer->source->length;
    eof_token.line = lexer->line;
    eof_token.column = lexer->column;
    cn_token_buffer_push(tokens, eof_token);
    return !cn_diag_has_error(lexer->diagnostics);
}
