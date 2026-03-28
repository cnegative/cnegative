#include "cnegative/source.h"

#include <stdio.h>

static FILE *cn_source_open_file(const char *path, const char *mode) {
#ifdef _WIN32
    FILE *file = NULL;
    errno_t error = fopen_s(&file, path, mode);
    if (error != 0) {
        return NULL;
    }
    return file;
#else
    return fopen(path, mode);
#endif
}

static size_t cn_count_lines(const char *text, size_t length) {
    size_t lines = 1;
    for (size_t i = 0; i < length; ++i) {
        if (text[i] == '\n') {
            lines += 1;
        }
    }
    return lines;
}

bool cn_source_load(cn_allocator *allocator, const char *path, cn_source *out_source, char *error_buffer, size_t error_buffer_size) {
    FILE *file = cn_source_open_file(path, "rb");
    if (file == NULL) {
        snprintf(error_buffer, error_buffer_size, "could not open '%s'", path);
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        snprintf(error_buffer, error_buffer_size, "could not seek '%s'", path);
        return false;
    }

    long file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        snprintf(error_buffer, error_buffer_size, "could not measure '%s'", path);
        return false;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        snprintf(error_buffer, error_buffer_size, "could not rewind '%s'", path);
        return false;
    }

    char *text = (char *)cn_alloc_impl(allocator, (size_t)file_size + 1, __FILE__, __LINE__);
    size_t read_size = fread(text, 1, (size_t)file_size, file);
    fclose(file);

    if (read_size != (size_t)file_size) {
        CN_FREE(allocator, text);
        snprintf(error_buffer, error_buffer_size, "could not read '%s'", path);
        return false;
    }

    text[read_size] = '\0';

    size_t line_count = cn_count_lines(text, read_size);
    size_t *line_offsets = CN_CALLOC(allocator, size_t, line_count);
    line_offsets[0] = 0;

    size_t line_index = 1;
    for (size_t i = 0; i < read_size; ++i) {
        if (text[i] == '\n' && line_index < line_count) {
            line_offsets[line_index] = i + 1;
            line_index += 1;
        }
    }

    out_source->path = CN_STRDUP(allocator, path);
    out_source->text = text;
    out_source->length = read_size;
    out_source->line_offsets = line_offsets;
    out_source->line_count = line_count;
    return true;
}

void cn_source_destroy(cn_allocator *allocator, cn_source *source) {
    CN_FREE(allocator, source->path);
    CN_FREE(allocator, source->text);
    CN_FREE(allocator, source->line_offsets);
    source->path = NULL;
    source->text = NULL;
    source->line_offsets = NULL;
    source->length = 0;
    source->line_count = 0;
}

void cn_source_position(const cn_source *source, size_t offset, size_t *line, size_t *column) {
    size_t low = 0;
    size_t high = source->line_count;

    while (low + 1 < high) {
        size_t mid = low + (high - low) / 2;
        if (source->line_offsets[mid] <= offset) {
            low = mid;
        } else {
            high = mid;
        }
    }

    *line = low + 1;
    *column = (offset - source->line_offsets[low]) + 1;
}

bool cn_source_line(const cn_source *source, size_t line, cn_strview *out_line) {
    if (line == 0 || line > source->line_count) {
        return false;
    }

    size_t start = source->line_offsets[line - 1];
    size_t end = source->length;

    if (line < source->line_count) {
        end = source->line_offsets[line] - 1;
    }

    while (end > start && source->text[end - 1] == '\r') {
        end -= 1;
    }

    *out_line = cn_sv_from_parts(source->text + start, end - start);
    return true;
}
