#include "cnegative/asm_scan.h"

#if !defined(CN_USE_X86_64_ASM_SCAN)
static int cn_is_ascii_digit(char value) {
    return value >= '0' && value <= '9';
}

static int cn_is_ascii_ident_continue(char value) {
    return value == '_' ||
           (value >= '0' && value <= '9') ||
           (value >= 'A' && value <= 'Z') ||
           (value >= 'a' && value <= 'z');
}

size_t cn_scan_number_tail(const char *text, size_t length, size_t index) {
    while (index < length && cn_is_ascii_digit(text[index])) {
        index += 1;
    }
    return index;
}

size_t cn_scan_identifier_tail(const char *text, size_t length, size_t index) {
    while (index < length && cn_is_ascii_ident_continue(text[index])) {
        index += 1;
    }
    return index;
}
#endif
