#include "cnegative/memory.h"

#include <stdio.h>
#include <string.h>

static int run_ok(void) {
    cn_allocator allocator;
    cn_allocator_init(&allocator);

    char *text = CN_ALLOC(&allocator, char);
    *text = 'a';
    CN_FREE(&allocator, text);
    cn_allocator_destroy(&allocator);
    return 0;
}

static int run_double_free(void) {
    cn_allocator allocator;
    cn_allocator_init(&allocator);

    int *value = CN_ALLOC(&allocator, int);
    *value = 7;
    CN_FREE(&allocator, value);
    CN_FREE(&allocator, value);
    return 0;
}

static int run_interior_free(void) {
    cn_allocator allocator;
    cn_allocator_init(&allocator);

    unsigned char *buffer = cn_alloc_impl(&allocator, 8, __FILE__, __LINE__);
    cn_free_impl(&allocator, buffer + 1, __FILE__, __LINE__);
    return 0;
}

static int run_overflow(void) {
    cn_allocator allocator;
    cn_allocator_init(&allocator);

    unsigned char *buffer = cn_alloc_impl(&allocator, 4, __FILE__, __LINE__);
    buffer[4] = 0xff;
    cn_free_impl(&allocator, buffer, __FILE__, __LINE__);
    return 0;
}

static int run_underflow(void) {
    cn_allocator allocator;
    cn_allocator_init(&allocator);

    unsigned char *buffer = cn_alloc_impl(&allocator, 4, __FILE__, __LINE__);
    buffer[-1] = 0xff;
    cn_free_impl(&allocator, buffer, __FILE__, __LINE__);
    return 0;
}

static int run_use_after_free(void) {
    cn_allocator allocator;
    cn_allocator_init(&allocator);

    unsigned char *buffer = cn_alloc_impl(&allocator, 8, __FILE__, __LINE__);
    cn_free_impl(&allocator, buffer, __FILE__, __LINE__);
    buffer[0] = 0xaa;
    cn_allocator_destroy(&allocator);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <case>\n", argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "ok") == 0) {
        return run_ok();
    }
    if (strcmp(argv[1], "double-free") == 0) {
        return run_double_free();
    }
    if (strcmp(argv[1], "interior-free") == 0) {
        return run_interior_free();
    }
    if (strcmp(argv[1], "overflow") == 0) {
        return run_overflow();
    }
    if (strcmp(argv[1], "underflow") == 0) {
        return run_underflow();
    }
    if (strcmp(argv[1], "use-after-free") == 0) {
        return run_use_after_free();
    }

    fprintf(stderr, "unknown case: %s\n", argv[1]);
    return 2;
}
