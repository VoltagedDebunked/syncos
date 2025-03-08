#ifndef SYNCOS_KSTD_STRING_H
#define SYNCOS_KSTD_STRING_H

#include <stddef.h>
#include <stdint.h>

// Memory manipulation functions
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

// String functions
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
size_t strlen(const char *str);
int strcmp(const char *str1, const char *str2);

#endif // SYNCOS_KSTD_STRING_H