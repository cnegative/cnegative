#ifndef CNEGATIVE_COMMON_H
#define CNEGATIVE_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CN_ARRAY_LEN(value) (sizeof(value) / sizeof((value)[0]))
#define CN_UNUSED(value) ((void)(value))

typedef struct cn_strview {
    const char *data;
    size_t length;
} cn_strview;

static inline cn_strview cn_sv_from_parts(const char *data, size_t length) {
    cn_strview value;
    value.data = data;
    value.length = length;
    return value;
}

static inline cn_strview cn_sv_from_cstr(const char *data) {
    return cn_sv_from_parts(data, strlen(data));
}

static inline bool cn_sv_eq(cn_strview left, cn_strview right) {
    if (left.length != right.length) {
        return false;
    }

    if (left.length == 0) {
        return true;
    }

    return memcmp(left.data, right.data, left.length) == 0;
}

static inline bool cn_sv_eq_cstr(cn_strview left, const char *right) {
    return cn_sv_eq(left, cn_sv_from_cstr(right));
}

static inline uint64_t cn_sv_hash(cn_strview value) {
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < value.length; ++i) {
        hash ^= (unsigned char)value.data[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

#endif
