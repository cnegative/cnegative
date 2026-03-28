#ifndef CNEGATIVE_ASM_SCAN_H
#define CNEGATIVE_ASM_SCAN_H

#include <stddef.h>

size_t cn_scan_number_tail(const char *text, size_t length, size_t index);
size_t cn_scan_identifier_tail(const char *text, size_t length, size_t index);

#endif
