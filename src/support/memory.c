#include "cnegative/memory.h"

#include <stdlib.h>
#include <string.h>

static cn_allocator g_default_allocator;
static bool g_default_allocator_initialized = false;

static void cn_memory_fail(size_t size) {
    fprintf(stderr, "fatal: allocation failed for %zu bytes\n", size);
    abort();
}

static cn_mem_record *cn_record_create(void *ptr, size_t size, const char *file, int line) {
    cn_mem_record *record = (cn_mem_record *)malloc(sizeof(cn_mem_record));
    if (record == NULL) {
        cn_memory_fail(sizeof(cn_mem_record));
    }

    record->ptr = ptr;
    record->size = size;
    record->file = file;
    record->line = line;
    record->next = NULL;
    return record;
}

static cn_mem_record *cn_find_record(cn_allocator *allocator, void *ptr, cn_mem_record **out_previous) {
    cn_mem_record *previous = NULL;
    cn_mem_record *record = allocator->head;

    while (record != NULL) {
        if (record->ptr == ptr) {
            if (out_previous != NULL) {
                *out_previous = previous;
            }
            return record;
        }

        previous = record;
        record = record->next;
    }

    if (out_previous != NULL) {
        *out_previous = NULL;
    }
    return NULL;
}

static void cn_default_allocator_report(void) {
    cn_allocator_dump_leaks(&g_default_allocator, stderr);
    cn_allocator_destroy(&g_default_allocator);
}

cn_allocator *cn_default_allocator(void) {
    if (!g_default_allocator_initialized) {
        cn_allocator_init(&g_default_allocator);
        g_default_allocator_initialized = true;
    }

    if (!g_default_allocator.atexit_registered) {
        atexit(cn_default_allocator_report);
        g_default_allocator.atexit_registered = true;
    }

    return &g_default_allocator;
}

void cn_allocator_init(cn_allocator *allocator) {
    allocator->head = NULL;
    allocator->live_blocks = 0;
    allocator->live_bytes = 0;
    allocator->peak_bytes = 0;
    allocator->atexit_registered = false;
}

void cn_allocator_destroy(cn_allocator *allocator) {
    cn_mem_record *record = allocator->head;
    while (record != NULL) {
        cn_mem_record *next = record->next;
        free(record->ptr);
        free(record);
        record = next;
    }

    allocator->head = NULL;
    allocator->live_blocks = 0;
    allocator->live_bytes = 0;
}

void cn_allocator_dump_leaks(cn_allocator *allocator, FILE *stream) {
    if (allocator->live_blocks == 0) {
        return;
    }

    fprintf(
        stream,
        "memory-leak summary: %zu live blocks, %zu live bytes, %zu peak bytes\n",
        allocator->live_blocks,
        allocator->live_bytes,
        allocator->peak_bytes
    );

    for (cn_mem_record *record = allocator->head; record != NULL; record = record->next) {
        fprintf(
            stream,
            "  leak: %p size=%zu allocated at %s:%d\n",
            record->ptr,
            record->size,
            record->file,
            record->line
        );
    }
}

void *cn_alloc_impl(cn_allocator *allocator, size_t size, const char *file, int line) {
    size_t actual_size = size == 0 ? 1 : size;
    void *ptr = malloc(actual_size);
    if (ptr == NULL) {
        cn_memory_fail(actual_size);
    }

    cn_mem_record *record = cn_record_create(ptr, actual_size, file, line);
    record->next = allocator->head;
    allocator->head = record;
    allocator->live_blocks += 1;
    allocator->live_bytes += actual_size;
    if (allocator->live_bytes > allocator->peak_bytes) {
        allocator->peak_bytes = allocator->live_bytes;
    }

    return ptr;
}

void *cn_calloc_impl(cn_allocator *allocator, size_t count, size_t size, const char *file, int line) {
    size_t total_size = count * size;
    void *ptr = cn_alloc_impl(allocator, total_size, file, line);
    memset(ptr, 0, total_size);
    return ptr;
}

void *cn_realloc_impl(cn_allocator *allocator, void *ptr, size_t size, const char *file, int line) {
    if (ptr == NULL) {
        return cn_alloc_impl(allocator, size, file, line);
    }

    cn_mem_record *previous = NULL;
    cn_mem_record *record = cn_find_record(allocator, ptr, &previous);
    if (record == NULL) {
        fprintf(stderr, "fatal: realloc on unmanaged pointer at %s:%d\n", file, line);
        abort();
    }

    size_t actual_size = size == 0 ? 1 : size;
    void *new_ptr = realloc(ptr, actual_size);
    if (new_ptr == NULL) {
        cn_memory_fail(actual_size);
    }

    allocator->live_bytes -= record->size;
    allocator->live_bytes += actual_size;
    if (allocator->live_bytes > allocator->peak_bytes) {
        allocator->peak_bytes = allocator->live_bytes;
    }

    record->ptr = new_ptr;
    record->size = actual_size;
    record->file = file;
    record->line = line;

    CN_UNUSED(previous);
    return new_ptr;
}

void cn_free_impl(cn_allocator *allocator, void *ptr, const char *file, int line) {
    if (ptr == NULL) {
        return;
    }

    cn_mem_record *previous = NULL;
    cn_mem_record *record = cn_find_record(allocator, ptr, &previous);
    if (record == NULL) {
        fprintf(stderr, "fatal: free on unmanaged pointer at %s:%d\n", file, line);
        abort();
    }

    if (previous == NULL) {
        allocator->head = record->next;
    } else {
        previous->next = record->next;
    }

    allocator->live_blocks -= 1;
    allocator->live_bytes -= record->size;

    free(record->ptr);
    free(record);
}

char *cn_strdup_impl(cn_allocator *allocator, const char *text, const char *file, int line) {
    return cn_strndup_impl(allocator, text, strlen(text), file, line);
}

char *cn_strndup_impl(cn_allocator *allocator, const char *text, size_t length, const char *file, int line) {
    char *copy = (char *)cn_alloc_impl(allocator, length + 1, file, line);
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}
