/* string.c — Reimplementation minimale de fonctions libc, indispensables
 * puisqu'on compile en mode "freestanding" (aucune libc standard dispo).
 */
#include "kernel.h"

size_t strlen(const char* s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

int strcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char* a, const char* b, size_t n) {
    while (n && *a && (*a == *b)) { a++; b++; n--; }
    if (n == 0) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

void* memset(void* dst, int val, size_t n) {
    unsigned char* d = (unsigned char*) dst;
    for (size_t i = 0; i < n; i++) d[i] = (unsigned char) val;
    return dst;
}

/* Convertit un entier non signe en chaine decimale et l'affiche */
void terminal_write_uint(uint32_t n) {
    char buf[11]; /* max "4294967295" + \0 */
    int i = 10;
    buf[i--] = '\0';
    if (n == 0) {
        buf[i--] = '0';
    } else {
        while (n > 0) {
            buf[i--] = '0' + (n % 10);
            n /= 10;
        }
    }
    terminal_writestring(&buf[i + 1]);
}

/* Convertit un entier en hexadecimal et l'affiche (utile pour les adresses) */
void terminal_write_hex(uint32_t n) {
    char buf[11] = "0x00000000";
    const char* hex_digits = "0123456789abcdef";
    for (int i = 9; i >= 2; i--) {
        buf[i] = hex_digits[n & 0xF];
        n >>= 4;
    }
    terminal_writestring(buf);
}
