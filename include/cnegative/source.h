#ifndef CNEGATIVE_SOURCE_H
#define CNEGATIVE_SOURCE_H

#include "cnegative/common.h"
#include "cnegative/memory.h"

typedef struct cn_source {
    char *path;
    char *text;
    size_t length;
    size_t *line_offsets;
    size_t line_count;
} cn_source;

bool cn_source_load(cn_allocator *allocator, const char *path, cn_source *out_source, char *error_buffer, size_t error_buffer_size);
void cn_source_destroy(cn_allocator *allocator, cn_source *source);
void cn_source_position(const cn_source *source, size_t offset, size_t *line, size_t *column);
bool cn_source_line(const cn_source *source, size_t line, cn_strview *out_line);

#endif
