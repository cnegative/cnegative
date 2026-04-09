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
    record->prev = NULL;
    record->next = NULL;
    record->bucket_next = NULL;
    return record;
}

static size_t cn_ptr_hash(void *ptr) {
    uintptr_t value = (uintptr_t)ptr >> 4;
    value ^= value >> 33;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 33;
    value *= 0xc4ceb9fe1a85ec53ULL;
    value ^= value >> 33;
    return (size_t)value;
}

static size_t cn_bucket_capacity_for(size_t required_blocks) {
    size_t capacity = 16;
    while (capacity < required_blocks * 2) {
        capacity *= 2;
    }
    return capacity;
}

static size_t cn_bucket_index(const cn_allocator *allocator, void *ptr) {
    return cn_ptr_hash(ptr) & (allocator->bucket_capacity - 1);
}

static void cn_allocator_rehash(cn_allocator *allocator, size_t required_blocks) {
    if (allocator->bucket_capacity >= required_blocks * 2 && allocator->bucket_capacity != 0) {
        return;
    }

    size_t new_capacity = cn_bucket_capacity_for(required_blocks);
    cn_mem_record **new_buckets = (cn_mem_record **)calloc(new_capacity, sizeof(cn_mem_record *));
    if (new_buckets == NULL) {
        cn_memory_fail(new_capacity * sizeof(cn_mem_record *));
    }

    for (cn_mem_record *record = allocator->head; record != NULL; record = record->next) {
        size_t index = cn_ptr_hash(record->ptr) & (new_capacity - 1);
        record->bucket_next = new_buckets[index];
        new_buckets[index] = record;
    }

    free(allocator->buckets);
    allocator->buckets = new_buckets;
    allocator->bucket_capacity = new_capacity;
}

static cn_mem_record *cn_find_record(cn_allocator *allocator, void *ptr) {
    if (allocator->bucket_capacity == 0) {
        return NULL;
    }

    cn_mem_record *record = allocator->buckets[cn_bucket_index(allocator, ptr)];
    while (record != NULL) {
        if (record->ptr == ptr) {
            return record;
        }
        record = record->bucket_next;
    }

    return NULL;
}

static void cn_bucket_remove_record(cn_allocator *allocator, cn_mem_record *record, void *ptr_for_hash) {
    if (allocator->bucket_capacity == 0) {
        return;
    }

    size_t index = cn_bucket_index(allocator, ptr_for_hash);
    cn_mem_record **slot = &allocator->buckets[index];
    while (*slot != NULL) {
        if (*slot == record) {
            *slot = record->bucket_next;
            record->bucket_next = NULL;
            return;
        }
        slot = &(*slot)->bucket_next;
    }
}

static void cn_bucket_insert_record(cn_allocator *allocator, cn_mem_record *record) {
    size_t index = cn_bucket_index(allocator, record->ptr);
    record->bucket_next = allocator->buckets[index];
    allocator->buckets[index] = record;
}

static void cn_list_remove_record(cn_allocator *allocator, cn_mem_record *record) {
    if (record->prev != NULL) {
        record->prev->next = record->next;
    } else {
        allocator->head = record->next;
    }

    if (record->next != NULL) {
        record->next->prev = record->prev;
    }

    record->prev = NULL;
    record->next = NULL;
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
    allocator->buckets = NULL;
    allocator->bucket_capacity = 0;
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
    free(allocator->buckets);
    allocator->buckets = NULL;
    allocator->bucket_capacity = 0;
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

    cn_allocator_rehash(allocator, allocator->live_blocks + 1);
    cn_mem_record *record = cn_record_create(ptr, actual_size, file, line);
    record->next = allocator->head;
    if (allocator->head != NULL) {
        allocator->head->prev = record;
    }
    allocator->head = record;
    cn_bucket_insert_record(allocator, record);
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

    cn_mem_record *record = cn_find_record(allocator, ptr);
    if (record == NULL) {
        fprintf(stderr, "fatal: realloc on unmanaged pointer at %s:%d\n", file, line);
        abort();
    }

    cn_bucket_remove_record(allocator, record, ptr);
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
    cn_bucket_insert_record(allocator, record);
    return new_ptr;
}

void cn_free_impl(cn_allocator *allocator, void *ptr, const char *file, int line) {
    if (ptr == NULL) {
        return;
    }

    cn_mem_record *record = cn_find_record(allocator, ptr);
    if (record == NULL) {
        fprintf(stderr, "fatal: free on unmanaged pointer at %s:%d\n", file, line);
        abort();
    }

    cn_bucket_remove_record(allocator, record, ptr);
    cn_list_remove_record(allocator, record);

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
