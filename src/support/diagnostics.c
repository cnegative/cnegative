#include "cnegative/diagnostics.h"

#include <stdio.h>
#include <string.h>

static const char *cn_diag_level_name(cn_diag_level level) {
    switch (level) {
    case CN_DIAG_ERROR:
        return "error";
    case CN_DIAG_NOTE:
        return "note";
    }

    return "unknown";
}

static void cn_diag_reserve(cn_diag_bag *bag, size_t required) {
    if (bag->capacity >= required) {
        return;
    }

    size_t new_capacity = bag->capacity == 0 ? 8 : bag->capacity * 2;
    while (new_capacity < required) {
        new_capacity *= 2;
    }

    bag->items = CN_REALLOC(bag->allocator, bag->items, cn_diagnostic, new_capacity);
    bag->capacity = new_capacity;
}

void cn_diag_bag_init(cn_diag_bag *bag, cn_allocator *allocator, const cn_source *source) {
    bag->allocator = allocator;
    bag->source = source;
    bag->items = NULL;
    bag->count = 0;
    bag->capacity = 0;
}

void cn_diag_bag_set_source(cn_diag_bag *bag, const cn_source *source) {
    bag->source = source;
}

void cn_diag_bag_destroy(cn_diag_bag *bag) {
    for (size_t i = 0; i < bag->count; ++i) {
        CN_FREE(bag->allocator, bag->items[i].message);
    }

    CN_FREE(bag->allocator, bag->items);
    bag->items = NULL;
    bag->count = 0;
    bag->capacity = 0;
}

void cn_diag_emit(cn_diag_bag *bag, cn_diag_level level, const char *code, size_t offset, const char *format, ...) {
    va_list args;
    va_start(args, format);
    cn_diag_vemit(bag, level, code, offset, format, args);
    va_end(args);
}

void cn_diag_vemit(cn_diag_bag *bag, cn_diag_level level, const char *code, size_t offset, const char *format, va_list args) {
    va_list args_copy;
    va_copy(args_copy, args);
    int length = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);

    if (length < 0) {
        return;
    }

    char *message = (char *)cn_alloc_impl(bag->allocator, (size_t)length + 1, __FILE__, __LINE__);
    vsnprintf(message, (size_t)length + 1, format, args);

    cn_diag_reserve(bag, bag->count + 1);
    cn_diagnostic *diagnostic = &bag->items[bag->count++];
    diagnostic->level = level;
    diagnostic->code = code;
    diagnostic->source = bag->source;
    diagnostic->offset = offset;
    diagnostic->message = message;
}

bool cn_diag_has_error(const cn_diag_bag *bag) {
    for (size_t i = 0; i < bag->count; ++i) {
        if (bag->items[i].level == CN_DIAG_ERROR) {
            return true;
        }
    }

    return false;
}

void cn_diag_print_all(const cn_diag_bag *bag, FILE *stream) {
    for (size_t i = 0; i < bag->count; ++i) {
        const cn_diagnostic *diagnostic = &bag->items[i];
        size_t line = 1;
        size_t column = diagnostic->offset + 1;
        cn_strview line_text = {0};
        const cn_source *source = diagnostic->source;

        if (source != NULL) {
            cn_source_position(source, diagnostic->offset, &line, &column);
            cn_source_line(source, line, &line_text);
        }

        if (source != NULL) {
            fprintf(
                stream,
                "%s:%zu:%zu: %s[%s]: %s\n",
                source->path,
                line,
                column,
                cn_diag_level_name(diagnostic->level),
                diagnostic->code,
                diagnostic->message
            );
        } else {
            fprintf(
                stream,
                "%s[%s]: %s\n",
                cn_diag_level_name(diagnostic->level),
                diagnostic->code,
                diagnostic->message
            );
        }

        if (line_text.data != NULL) {
            fprintf(stream, "  %.*s\n", (int)line_text.length, line_text.data);
            fprintf(stream, "  ");
            for (size_t cursor = 1; cursor < column; ++cursor) {
                fputc(' ', stream);
            }
            fprintf(stream, "^\n");
        }
    }
}
