#ifndef CNEGATIVE_MEMORY_H
#define CNEGATIVE_MEMORY_H

#include "cnegative/common.h"

#include <stdio.h>

typedef struct cn_mem_record {
    void *ptr;
    size_t size;
    const char *file;
    int line;
    struct cn_mem_record *prev;
    struct cn_mem_record *next;
    struct cn_mem_record *bucket_next;
} cn_mem_record;

typedef struct cn_allocator {
    cn_mem_record *head;
    cn_mem_record **buckets;
    size_t bucket_capacity;
    size_t live_blocks;
    size_t live_bytes;
    size_t peak_bytes;
    bool atexit_registered;
} cn_allocator;

cn_allocator *cn_default_allocator(void);
void cn_allocator_init(cn_allocator *allocator);
void cn_allocator_destroy(cn_allocator *allocator);
void cn_allocator_dump_leaks(cn_allocator *allocator, FILE *stream);

void *cn_alloc_impl(cn_allocator *allocator, size_t size, const char *file, int line);
void *cn_calloc_impl(cn_allocator *allocator, size_t count, size_t size, const char *file, int line);
void *cn_realloc_impl(cn_allocator *allocator, void *ptr, size_t size, const char *file, int line);
void cn_free_impl(cn_allocator *allocator, void *ptr, const char *file, int line);
char *cn_strdup_impl(cn_allocator *allocator, const char *text, const char *file, int line);
char *cn_strndup_impl(cn_allocator *allocator, const char *text, size_t length, const char *file, int line);

#define CN_ALLOC(allocator, type) ((type *)cn_alloc_impl((allocator), sizeof(type), __FILE__, __LINE__))
#define CN_CALLOC(allocator, type, count) ((type *)cn_calloc_impl((allocator), (count), sizeof(type), __FILE__, __LINE__))
#define CN_REALLOC(allocator, ptr, type, count) ((type *)cn_realloc_impl((allocator), (ptr), sizeof(type) * (count), __FILE__, __LINE__))
#define CN_FREE(allocator, ptr) cn_free_impl((allocator), (ptr), __FILE__, __LINE__)
#define CN_STRDUP(allocator, text) cn_strdup_impl((allocator), (text), __FILE__, __LINE__)
#define CN_STRNDUP(allocator, text, length) cn_strndup_impl((allocator), (text), (length), __FILE__, __LINE__)

#endif
