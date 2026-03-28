#ifndef CNEGATIVE_DIAGNOSTICS_H
#define CNEGATIVE_DIAGNOSTICS_H

#include "cnegative/common.h"
#include "cnegative/memory.h"
#include "cnegative/source.h"

#include <stdarg.h>
#include <stdio.h>

typedef enum cn_diag_level {
    CN_DIAG_ERROR,
    CN_DIAG_NOTE
} cn_diag_level;

typedef struct cn_diagnostic {
    cn_diag_level level;
    const char *code;
    const cn_source *source;
    size_t offset;
    char *message;
} cn_diagnostic;

typedef struct cn_diag_bag {
    cn_allocator *allocator;
    const cn_source *source;
    cn_diagnostic *items;
    size_t count;
    size_t capacity;
} cn_diag_bag;

void cn_diag_bag_init(cn_diag_bag *bag, cn_allocator *allocator, const cn_source *source);
void cn_diag_bag_destroy(cn_diag_bag *bag);
void cn_diag_bag_set_source(cn_diag_bag *bag, const cn_source *source);
void cn_diag_emit(cn_diag_bag *bag, cn_diag_level level, const char *code, size_t offset, const char *format, ...);
void cn_diag_vemit(cn_diag_bag *bag, cn_diag_level level, const char *code, size_t offset, const char *format, va_list args);
bool cn_diag_has_error(const cn_diag_bag *bag);
void cn_diag_print_all(const cn_diag_bag *bag, FILE *stream);

#endif
