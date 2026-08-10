#ifndef STRING_H
#define STRING_H

#include <stdint.h>
#include <stddef.h>

size_t strlen(const char* s);
int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, size_t n);
void* memset(void* dst, int val, size_t n);

#endif
