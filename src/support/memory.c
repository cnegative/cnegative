#include "cnegative/memory.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static cn_allocator g_default_allocator;
static bool g_default_allocator_initialized = false;

enum {
    CN_MEM_QUARANTINE_BLOCK_LIMIT = 64,
    CN_MEM_QUARANTINE_BYTE_LIMIT = 1024 * 1024
};

static const unsigned char g_cn_mem_alloc_poison = 0xcd;
static const unsigned char g_cn_mem_freed_poison = 0xdd;

static size_t cn_memory_prefix_size(void) {
    return sizeof(max_align_t);
}

static size_t cn_memory_tail_size(void) {
    return sizeof(uint64_t);
}

static void cn_memory_report(const char *level, const char *code, const char *format, va_list args) {
    fprintf(stderr, "%s[%s]: ", level, code);
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
}

static void cn_memory_fail_code(const char *code, const char *format, ...) {
    va_list args;
    va_start(args, format);
    cn_memory_report("runtime error", code, format, args);
    va_end(args);
    abort();
}

static void cn_memory_warn(const char *code, const char *format, ...) {
    va_list args;
    va_start(args, format);
    cn_memory_report("runtime warning", code, format, args);
    va_end(args);
}

static bool cn_size_mul_overflow(size_t left, size_t right, size_t *out_value) {
    if (left == 0 || right == 0) {
        *out_value = 0;
        return false;
    }

    if (left > SIZE_MAX / right) {
        return true;
    }

    *out_value = left * right;
    return false;
}

static bool cn_size_add_overflow(size_t left, size_t right, size_t *out_value) {
    if (left > SIZE_MAX - right) {
        return true;
    }

    *out_value = left + right;
    return false;
}

static bool cn_allocation_total_size(size_t payload_size, size_t *out_total_size) {
    size_t prefixed = 0;
    if (cn_size_add_overflow(payload_size, cn_memory_prefix_size(), &prefixed)) {
        return true;
    }

    return cn_size_add_overflow(prefixed, cn_memory_tail_size(), out_total_size);
}

static uint64_t cn_record_guard_value(const cn_mem_record *record) {
    return 0xC0DEC0DEC0DEC0DEull ^ (uint64_t)(uintptr_t)record->ptr ^ ((uint64_t)record->size << 1);
}

static unsigned char *cn_record_head_guard_ptr(const cn_mem_record *record) {
    return (unsigned char *)record->base_ptr;
}

static unsigned char *cn_record_tail_guard_ptr(const cn_mem_record *record) {
    return (unsigned char *)record->ptr + record->size;
}

static unsigned char cn_guard_byte(uint64_t guard, size_t index) {
    return (unsigned char)((guard >> ((index % sizeof(uint64_t)) * 8)) & 0xffu);
}

static void cn_record_write_guards(const cn_mem_record *record) {
    uint64_t guard = cn_record_guard_value(record);
    uint64_t tail_guard = ~guard;
    unsigned char *head = cn_record_head_guard_ptr(record);
    unsigned char *tail = cn_record_tail_guard_ptr(record);

    for (size_t i = 0; i < cn_memory_prefix_size(); ++i) {
        head[i] = cn_guard_byte(guard, i);
    }

    for (size_t i = 0; i < cn_memory_tail_size(); ++i) {
        tail[i] = cn_guard_byte(tail_guard, i);
    }
}

static void cn_record_fill_payload(const cn_mem_record *record, unsigned char value) {
    memset(record->ptr, value, record->size);
}

static bool cn_record_payload_matches(const cn_mem_record *record, unsigned char value) {
    const unsigned char *bytes = (const unsigned char *)record->ptr;
    for (size_t i = 0; i < record->size; ++i) {
        if (bytes[i] != value) {
            return false;
        }
    }
    return true;
}

static void cn_memory_fail(size_t size) {
    cn_memory_fail_code("R4001", "allocation failed for %zu bytes", size);
}

static cn_mem_record *cn_record_create(void *base_ptr, void *ptr, size_t size, const char *file, int line) {
    cn_mem_record *record = (cn_mem_record *)malloc(sizeof(cn_mem_record));
    if (record == NULL) {
        cn_memory_fail(sizeof(cn_mem_record));
    }

    record->base_ptr = base_ptr;
    record->ptr = ptr;
    record->size = size;
    record->file = file;
    record->line = line;
    record->released_file = NULL;
    record->released_line = 0;
    record->prev = NULL;
    record->next = NULL;
    record->bucket_next = NULL;
    return record;
}

static void cn_record_release_storage(cn_mem_record *record) {
    free(record->base_ptr);
    free(record);
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

static size_t cn_bucket_index_for(size_t capacity, void *ptr) {
    return cn_ptr_hash(ptr) & (capacity - 1);
}

static void cn_rehash_records(
    cn_mem_record *head,
    cn_mem_record ***buckets_ptr,
    size_t *capacity_ptr,
    size_t required_blocks
) {
    if (required_blocks == 0) {
        free(*buckets_ptr);
        *buckets_ptr = NULL;
        *capacity_ptr = 0;
        return;
    }

    if (*capacity_ptr >= required_blocks * 2 && *capacity_ptr != 0) {
        return;
    }

    size_t new_capacity = cn_bucket_capacity_for(required_blocks);
    cn_mem_record **new_buckets = (cn_mem_record **)calloc(new_capacity, sizeof(cn_mem_record *));
    if (new_buckets == NULL) {
        cn_memory_fail(new_capacity * sizeof(cn_mem_record *));
    }

    for (cn_mem_record *record = head; record != NULL; record = record->next) {
        size_t index = cn_bucket_index_for(new_capacity, record->ptr);
        record->bucket_next = new_buckets[index];
        new_buckets[index] = record;
    }

    free(*buckets_ptr);
    *buckets_ptr = new_buckets;
    *capacity_ptr = new_capacity;
}

static cn_mem_record *cn_find_record_in_buckets(cn_mem_record **buckets, size_t capacity, void *ptr) {
    if (capacity == 0) {
        return NULL;
    }

    cn_mem_record *record = buckets[cn_bucket_index_for(capacity, ptr)];
    while (record != NULL) {
        if (record->ptr == ptr) {
            return record;
        }
        record = record->bucket_next;
    }

    return NULL;
}

static cn_mem_record *cn_find_containing_record_in_list(cn_mem_record *head, void *ptr) {
    uintptr_t target = (uintptr_t)ptr;
    for (cn_mem_record *record = head; record != NULL; record = record->next) {
        uintptr_t start = (uintptr_t)record->ptr;
        uintptr_t end = start + record->size;
        if (target > start && target < end) {
            return record;
        }
    }
    return NULL;
}

static void cn_bucket_remove_record(
    cn_mem_record **buckets,
    size_t capacity,
    cn_mem_record *record,
    void *ptr_for_hash
) {
    if (capacity == 0) {
        return;
    }

    size_t index = cn_bucket_index_for(capacity, ptr_for_hash);
    cn_mem_record **slot = &buckets[index];
    while (*slot != NULL) {
        if (*slot == record) {
            *slot = record->bucket_next;
            record->bucket_next = NULL;
            return;
        }
        slot = &(*slot)->bucket_next;
    }
}

static void cn_bucket_insert_record(cn_mem_record **buckets, size_t capacity, cn_mem_record *record) {
    size_t index = cn_bucket_index_for(capacity, record->ptr);
    record->bucket_next = buckets[index];
    buckets[index] = record;
}

static void cn_list_insert_head(cn_mem_record **head, cn_mem_record **tail, cn_mem_record *record) {
    record->prev = NULL;
    record->next = *head;
    if (*head != NULL) {
        (*head)->prev = record;
    } else if (tail != NULL) {
        *tail = record;
    }
    *head = record;
}

static void cn_list_remove_record(cn_mem_record **head, cn_mem_record **tail, cn_mem_record *record) {
    if (record->prev != NULL) {
        record->prev->next = record->next;
    } else {
        *head = record->next;
    }

    if (record->next != NULL) {
        record->next->prev = record->prev;
    } else if (tail != NULL) {
        *tail = record->prev;
    }

    record->prev = NULL;
    record->next = NULL;
}

static void cn_record_verify_guards(const cn_mem_record *record, const char *file, int line) {
    uint64_t guard = cn_record_guard_value(record);
    uint64_t tail_guard = ~guard;
    const unsigned char *head = cn_record_head_guard_ptr(record);
    const unsigned char *tail = cn_record_tail_guard_ptr(record);

    for (size_t i = 0; i < cn_memory_prefix_size(); ++i) {
        if (head[i] != cn_guard_byte(guard, i)) {
            cn_memory_fail_code(
                "R4017",
                "buffer underflow detected for allocation %p size=%zu allocated at %s:%d (checked at %s:%d)",
                record->ptr,
                record->size,
                record->file,
                record->line,
                file,
                line
            );
        }
    }

    for (size_t i = 0; i < cn_memory_tail_size(); ++i) {
        if (tail[i] != cn_guard_byte(tail_guard, i)) {
            cn_memory_fail_code(
                "R4016",
                "buffer overflow detected for allocation %p size=%zu allocated at %s:%d (checked at %s:%d)",
                record->ptr,
                record->size,
                record->file,
                record->line,
                file,
                line
            );
        }
    }

}

static void cn_record_verify_quarantine(const cn_mem_record *record, const char *file, int line) {
    cn_record_verify_guards(record, file, line);
    if (!cn_record_payload_matches(record, g_cn_mem_freed_poison)) {
        cn_memory_fail_code(
            "R4013",
            "use-after-free detected for allocation %p size=%zu freed at %s:%d (checked at %s:%d)",
            record->ptr,
            record->size,
            record->released_file != NULL ? record->released_file : record->file,
            record->released_line,
            file,
            line
        );
    }
}

static void cn_allocator_rehash_active(cn_allocator *allocator, size_t required_blocks) {
    cn_rehash_records(allocator->head, &allocator->buckets, &allocator->bucket_capacity, required_blocks);
}

static void cn_allocator_rehash_freed(cn_allocator *allocator, size_t required_blocks) {
    cn_rehash_records(allocator->freed_head, &allocator->freed_buckets, &allocator->freed_bucket_capacity, required_blocks);
}

static void cn_allocator_evict_quarantine(cn_allocator *allocator) {
    while (allocator->quarantine_blocks > CN_MEM_QUARANTINE_BLOCK_LIMIT ||
           allocator->quarantine_bytes > CN_MEM_QUARANTINE_BYTE_LIMIT) {
        cn_mem_record *record = allocator->freed_tail;
        if (record == NULL) {
            return;
        }

        cn_record_verify_quarantine(record, "<quarantine>", 0);
        cn_bucket_remove_record(allocator->freed_buckets, allocator->freed_bucket_capacity, record, record->ptr);
        cn_list_remove_record(&allocator->freed_head, &allocator->freed_tail, record);
        allocator->quarantine_blocks -= 1;
        allocator->quarantine_bytes -= record->size;
        cn_record_release_storage(record);
    }
}

static void cn_allocator_move_to_quarantine(cn_allocator *allocator, cn_mem_record *record, const char *file, int line) {
    record->released_file = file;
    record->released_line = line;
    cn_record_fill_payload(record, g_cn_mem_freed_poison);
    cn_allocator_rehash_freed(allocator, allocator->quarantine_blocks + 1);
    cn_list_insert_head(&allocator->freed_head, &allocator->freed_tail, record);
    cn_bucket_insert_record(allocator->freed_buckets, allocator->freed_bucket_capacity, record);
    allocator->quarantine_blocks += 1;
    allocator->quarantine_bytes += record->size;
    cn_allocator_evict_quarantine(allocator);
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
    allocator->freed_head = NULL;
    allocator->freed_tail = NULL;
    allocator->buckets = NULL;
    allocator->freed_buckets = NULL;
    allocator->bucket_capacity = 0;
    allocator->freed_bucket_capacity = 0;
    allocator->live_blocks = 0;
    allocator->live_bytes = 0;
    allocator->peak_bytes = 0;
    allocator->quarantine_blocks = 0;
    allocator->quarantine_bytes = 0;
    allocator->atexit_registered = false;
}

void cn_allocator_destroy(cn_allocator *allocator) {
    cn_mem_record *record = allocator->head;
    while (record != NULL) {
        cn_mem_record *next = record->next;
        cn_record_verify_guards(record, "<destroy>", 0);
        cn_record_release_storage(record);
        record = next;
    }

    record = allocator->freed_head;
    while (record != NULL) {
        cn_mem_record *next = record->next;
        cn_record_verify_quarantine(record, "<destroy>", 0);
        cn_record_release_storage(record);
        record = next;
    }

    allocator->head = NULL;
    allocator->freed_head = NULL;
    allocator->freed_tail = NULL;
    free(allocator->buckets);
    free(allocator->freed_buckets);
    allocator->buckets = NULL;
    allocator->freed_buckets = NULL;
    allocator->bucket_capacity = 0;
    allocator->freed_bucket_capacity = 0;
    allocator->live_blocks = 0;
    allocator->live_bytes = 0;
    allocator->quarantine_blocks = 0;
    allocator->quarantine_bytes = 0;
}

void cn_allocator_dump_leaks(cn_allocator *allocator, FILE *stream) {
    if (allocator->live_blocks == 0) {
        return;
    }

    (void)stream;
    cn_memory_warn(
        "R4004",
        "memory leak summary: %zu live blocks, %zu live bytes, %zu peak bytes",
        allocator->live_blocks,
        allocator->live_bytes,
        allocator->peak_bytes
    );

    for (cn_mem_record *record = allocator->head; record != NULL; record = record->next) {
        cn_record_verify_guards(record, "<leak-dump>", 0);
        cn_memory_warn(
            "R4005",
            "leak: %p size=%zu allocated at %s:%d",
            record->ptr,
            record->size,
            record->file,
            record->line
        );
    }
}

void *cn_alloc_impl(cn_allocator *allocator, size_t size, const char *file, int line) {
    size_t actual_size = size == 0 ? 1 : size;
    size_t total_size = 0;
    if (cn_allocation_total_size(actual_size, &total_size)) {
        cn_memory_fail_code(
            "R4006",
            "allocation size overflow for %zu bytes at %s:%d",
            actual_size,
            file,
            line
        );
    }

    void *base_ptr = malloc(total_size);
    if (base_ptr == NULL) {
        cn_memory_fail(actual_size);
    }

    void *ptr = (unsigned char *)base_ptr + cn_memory_prefix_size();
    cn_allocator_rehash_active(allocator, allocator->live_blocks + 1);
    cn_mem_record *record = cn_record_create(base_ptr, ptr, actual_size, file, line);
    cn_list_insert_head(&allocator->head, NULL, record);
    cn_bucket_insert_record(allocator->buckets, allocator->bucket_capacity, record);
    cn_record_write_guards(record);
    cn_record_fill_payload(record, g_cn_mem_alloc_poison);
    allocator->live_blocks += 1;
    allocator->live_bytes += actual_size;
    if (allocator->live_bytes > allocator->peak_bytes) {
        allocator->peak_bytes = allocator->live_bytes;
    }

    return ptr;
}

void *cn_calloc_impl(cn_allocator *allocator, size_t count, size_t size, const char *file, int line) {
    size_t total_size = 0;
    if (cn_size_mul_overflow(count, size, &total_size)) {
        cn_memory_fail_code(
            "R4006",
            "allocation size overflow for %zu * %zu bytes at %s:%d",
            count,
            size,
            file,
            line
        );
    }

    void *ptr = cn_alloc_impl(allocator, total_size, file, line);
    memset(ptr, 0, total_size);
    return ptr;
}

void *cn_realloc_impl(cn_allocator *allocator, void *ptr, size_t size, const char *file, int line) {
    if (ptr == NULL) {
        return cn_alloc_impl(allocator, size, file, line);
    }

    cn_mem_record *record = cn_find_record_in_buckets(allocator->buckets, allocator->bucket_capacity, ptr);
    if (record == NULL) {
        cn_mem_record *freed = cn_find_record_in_buckets(allocator->freed_buckets, allocator->freed_bucket_capacity, ptr);
        if (freed != NULL) {
            cn_memory_fail_code(
                "R4013",
                "use-after-free detected while reallocating allocation %p freed at %s:%d",
                ptr,
                freed->released_file != NULL ? freed->released_file : freed->file,
                freed->released_line
            );
        }
        cn_memory_fail_code("R4002", "realloc on unmanaged pointer at %s:%d", file, line);
    }

    cn_record_verify_guards(record, file, line);

    size_t actual_size = size == 0 ? 1 : size;
    size_t total_size = 0;
    if (cn_allocation_total_size(actual_size, &total_size)) {
        cn_memory_fail_code(
            "R4009",
            "realloc size overflow for %zu bytes at %s:%d",
            actual_size,
            file,
            line
        );
    }

    size_t old_size = record->size;
    cn_bucket_remove_record(allocator->buckets, allocator->bucket_capacity, record, ptr);
    void *new_base_ptr = realloc(record->base_ptr, total_size);
    if (new_base_ptr == NULL) {
        cn_memory_fail_code(
            "R4008",
            "realloc failed for %zu bytes at %s:%d; original pointer preserved",
            actual_size,
            file,
            line
        );
    }

    void *new_ptr = (unsigned char *)new_base_ptr + cn_memory_prefix_size();
    allocator->live_bytes -= record->size;
    allocator->live_bytes += actual_size;
    if (allocator->live_bytes > allocator->peak_bytes) {
        allocator->peak_bytes = allocator->live_bytes;
    }

    record->base_ptr = new_base_ptr;
    record->ptr = new_ptr;
    record->size = actual_size;
    record->file = file;
    record->line = line;
    record->released_file = NULL;
    record->released_line = 0;
    cn_record_write_guards(record);
    if (actual_size > old_size) {
        memset((unsigned char *)record->ptr + old_size, g_cn_mem_alloc_poison, actual_size - old_size);
    }
    cn_bucket_insert_record(allocator->buckets, allocator->bucket_capacity, record);
    return new_ptr;
}

void *cn_realloc_mul_impl(
    cn_allocator *allocator,
    void *ptr,
    size_t left,
    size_t right,
    const char *file,
    int line
) {
    size_t total_size = 0;
    if (cn_size_mul_overflow(left, right, &total_size)) {
        cn_memory_fail_code(
            "R4009",
            "realloc size overflow for %zu * %zu bytes at %s:%d",
            left,
            right,
            file,
            line
        );
    }

    return cn_realloc_impl(allocator, ptr, total_size, file, line);
}

void cn_free_impl(cn_allocator *allocator, void *ptr, const char *file, int line) {
    if (ptr == NULL) {
        return;
    }

    cn_mem_record *record = cn_find_record_in_buckets(allocator->buckets, allocator->bucket_capacity, ptr);
    if (record == NULL) {
        cn_mem_record *freed = cn_find_record_in_buckets(allocator->freed_buckets, allocator->freed_bucket_capacity, ptr);
        if (freed != NULL) {
            cn_memory_fail_code(
                "R4010",
                "double free detected for allocation %p first released at %s:%d",
                ptr,
                freed->released_file != NULL ? freed->released_file : freed->file,
                freed->released_line
            );
        }

        cn_mem_record *containing = cn_find_containing_record_in_list(allocator->head, ptr);
        if (containing == NULL) {
            containing = cn_find_containing_record_in_list(allocator->freed_head, ptr);
        }
        if (containing != NULL) {
            cn_memory_fail_code(
                "R4011",
                "free used on interior pointer %p at %s:%d; allocation starts at %p",
                ptr,
                file,
                line,
                containing->ptr
            );
        }

        cn_memory_fail_code("R4003", "free on unmanaged pointer at %s:%d", file, line);
    }

    cn_record_verify_guards(record, file, line);
    cn_bucket_remove_record(allocator->buckets, allocator->bucket_capacity, record, ptr);
    cn_list_remove_record(&allocator->head, NULL, record);
    allocator->live_blocks -= 1;
    allocator->live_bytes -= record->size;
    cn_allocator_move_to_quarantine(allocator, record, file, line);
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
