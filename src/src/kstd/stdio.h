#ifndef SYNCOS_KSTD_STDIO_H
#define SYNCOS_KSTD_STDIO_H

#include <stddef.h>
#include <stdarg.h>

// Function pointer types for standard I/O operations
typedef int (*getchar_func_t)(void);
typedef int (*putchar_func_t)(int c);

// Standard I/O function registration
void stdio_set_stdin_getchar(getchar_func_t func);
void stdio_set_stdout_putchar(putchar_func_t func);
void stdio_set_stderr_putchar(putchar_func_t func);

// Standard I/O functions
int getchar(void);
int putchar(int c);
int puts(const char *s);

// Formatted output functions
int printf(const char *format, ...);
int fprintf(int fd, const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);
int vsnprintf(char *str, size_t size, const char *format, va_list args);

// File descriptor constants
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#endif // SYNCOS_KSTD_STDIO_H