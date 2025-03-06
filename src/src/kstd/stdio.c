#include <kstd/stdio.h>
#include <kstd/string.h>
#include <stdbool.h>
#include <stdint.h>

// Function pointers for standard I/O operations
static getchar_func_t stdin_getchar_func = NULL;
static putchar_func_t stdout_putchar_func = NULL;
static putchar_func_t stderr_putchar_func = NULL;

// Register standard input function
void stdio_set_stdin_getchar(getchar_func_t func) {
    stdin_getchar_func = func;
}

// Register standard output function
void stdio_set_stdout_putchar(putchar_func_t func) {
    stdout_putchar_func = func;
}

// Register standard error function
void stdio_set_stderr_putchar(putchar_func_t func) {
    stderr_putchar_func = func;
}

// Get character from standard input
int getchar(void) {
    if (stdin_getchar_func) {
        return stdin_getchar_func();
    }
    return -1;
}

// Put character to standard output
int putchar(int c) {
    if (stdout_putchar_func) {
        return stdout_putchar_func(c);
    }
    return -1;
}

// Put string to standard output
int puts(const char *s) {
    if (!s || !stdout_putchar_func) {
        return -1;
    }
    
    int count = 0;
    while (*s) {
        stdout_putchar_func(*s++);
        count++;
    }
    
    // Append newline
    stdout_putchar_func('\n');
    
    return count + 1;
}

// Format strings
enum format_flags {
    FLAG_LEFT_JUSTIFY = 1 << 0,   // '-'
    FLAG_PLUS_SIGN    = 1 << 1,   // '+'
    FLAG_SPACE        = 1 << 2,   // ' '
    FLAG_HASH         = 1 << 3,   // '#'
    FLAG_ZERO_PAD     = 1 << 4,   // '0'
    FLAG_UPPERCASE    = 1 << 5    // For 'X' vs 'x', etc.
};

// Helper function to check if a character is a digit
static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

// Helper function to convert a digit character to an integer
static int to_digit(char c) {
    return c - '0';
}

// Helper function for writing a number in a given base
static int write_num(char *str, size_t size, uintmax_t num, int base, int width, int precision, int flags) {
    // Define character set for different bases
    const char *digits_lower = "0123456789abcdef";
    const char *digits_upper = "0123456789ABCDEF";
    const char *digits = (flags & FLAG_UPPERCASE) ? digits_upper : digits_lower;
    
    // Buffer to store the number in reverse (max 64 bits in base 2)
    char buffer[64];
    size_t pos = 0;
    
    // Handle zero case
    if (num == 0) {
        buffer[pos++] = '0';
    } else {
        // Convert number to digits in reverse order
        while (num > 0) {
            buffer[pos++] = digits[num % base];
            num /= base;
        }
    }
    
    // Determine final length
    int length = pos;
    
    // Add prefix if needed (for base 16 with # flag)
    if ((flags & FLAG_HASH) && base == 16) {
        length += 2;  // "0x" or "0X"
    }
    
    // Add sign if needed
    bool need_sign = false;
    char sign_char = 0;
    if (flags & FLAG_PLUS_SIGN) {
        need_sign = true;
        sign_char = '+';
        length++;
    } else if (flags & FLAG_SPACE) {
        need_sign = true;
        sign_char = ' ';
        length++;
    }
    
    // Handle width
    int padding = (width > length) ? (width - length) : 0;
    
    // Calculate total characters needed
    int total_len = length + padding;
    
    // Begin writing to output string
    size_t written = 0;
    
    // Right-justified (default)
    if (!(flags & FLAG_LEFT_JUSTIFY)) {
        // Add padding if needed
        char pad_char = (flags & FLAG_ZERO_PAD) ? '0' : ' ';
        
        // If zero padding, add sign and prefix first
        if (pad_char == '0') {
            if (need_sign && written < size) {
                str[written++] = sign_char;
                need_sign = false;
            }
            
            if ((flags & FLAG_HASH) && base == 16 && written + 1 < size) {
                str[written++] = '0';
                str[written++] = (flags & FLAG_UPPERCASE) ? 'X' : 'x';
                flags &= ~FLAG_HASH;  // Clear the flag since we've handled it
            }
        }
        
        // Add padding
        while (padding-- > 0 && written < size) {
            str[written++] = pad_char;
        }
    }
    
    // Add sign if needed
    if (need_sign && written < size) {
        str[written++] = sign_char;
    }
    
    // Add prefix if needed
    if ((flags & FLAG_HASH) && base == 16 && written + 1 < size) {
        str[written++] = '0';
        str[written++] = (flags & FLAG_UPPERCASE) ? 'X' : 'x';
    }
    
    // Add digits
    for (int i = pos - 1; i >= 0; i--) {
        if (written < size) {
            str[written++] = buffer[i];
        }
    }
    
    // Left-justified padding
    if (flags & FLAG_LEFT_JUSTIFY) {
        while (padding-- > 0 && written < size) {
            str[written++] = ' ';
        }
    }
    
    return total_len;
}

// Helper function for string formatting
static int write_string(char *dest, size_t dest_size, const char *src, int width, int precision, int flags) {
    if (!src) {
        src = "(null)";
    }
    
    // Determine string length
    size_t len = 0;
    while (src[len] && (precision < 0 || len < (size_t)precision)) {
        len++;
    }
    
    // Calculate total characters needed
    int padding = (width > (int)len) ? (width - (int)len) : 0;
    int total_len = len + padding;
    
    // Begin writing to output string
    size_t written = 0;
    
    // Right-justified (default)
    if (!(flags & FLAG_LEFT_JUSTIFY)) {
        // Add padding if needed
        char pad_char = (flags & FLAG_ZERO_PAD) ? '0' : ' ';
        while (padding-- > 0 && written < dest_size) {
            dest[written++] = pad_char;
        }
    }
    
    // Copy the string
    for (size_t i = 0; i < len && written < dest_size; i++) {
        dest[written++] = src[i];
    }
    
    // Left-justified padding
    if (flags & FLAG_LEFT_JUSTIFY) {
        while (padding-- > 0 && written < dest_size) {
            dest[written++] = ' ';
        }
    }
    
    return total_len;
}

// Core vsnprintf implementation
int vsnprintf(char *str, size_t size, const char *format, va_list args) {
    if (!format || !str || size == 0) {
        return -1;
    }
    
    // Reserve space for the null terminator
    size_t max_size = size - 1;
    size_t written = 0;
    int total_written = 0;
    
    // Process the format string
    for (size_t i = 0; format[i] != '\0'; i++) {
        // Normal character, not a format specifier
        if (format[i] != '%') {
            if (written < max_size) {
                str[written++] = format[i];
            }
            total_written++;
            continue;
        }
        
        // Handle '%%' case
        if (format[i + 1] == '%') {
            if (written < max_size) {
                str[written++] = '%';
            }
            total_written++;
            i++;
            continue;
        }
        
        // Parse flags
        int flags = 0;
        bool parsing_flags = true;
        i++;  // Skip '%'
        
        while (parsing_flags && format[i] != '\0') {
            switch (format[i]) {
                case '-': flags |= FLAG_LEFT_JUSTIFY; i++; break;
                case '+': flags |= FLAG_PLUS_SIGN; i++; break;
                case ' ': flags |= FLAG_SPACE; i++; break;
                case '#': flags |= FLAG_HASH; i++; break;
                case '0': flags |= FLAG_ZERO_PAD; i++; break;
                default: parsing_flags = false; break;
            }
        }
        
        // Parse width
        int width = 0;
        if (format[i] == '*') {
            width = va_arg(args, int);
            if (width < 0) {
                flags |= FLAG_LEFT_JUSTIFY;
                width = -width;
            }
            i++;
        } else {
            while (is_digit(format[i])) {
                width = width * 10 + to_digit(format[i]);
                i++;
            }
        }
        
        // Parse precision
        int precision = -1;
        if (format[i] == '.') {
            i++;
            precision = 0;
            if (format[i] == '*') {
                precision = va_arg(args, int);
                i++;
            } else {
                while (is_digit(format[i])) {
                    precision = precision * 10 + to_digit(format[i]);
                    i++;
                }
            }
        }
        
        // Parse length modifiers
        enum {
            LENGTH_NONE,
            LENGTH_HH,
            LENGTH_H,
            LENGTH_L,
            LENGTH_LL,
            LENGTH_J,
            LENGTH_Z,
            LENGTH_T,
            LENGTH_CAPITAL_L
        } length = LENGTH_NONE;
        
        if (format[i] == 'h') {
            length = LENGTH_H;
            i++;
            if (format[i] == 'h') {
                length = LENGTH_HH;
                i++;
            }
        } else if (format[i] == 'l') {
            length = LENGTH_L;
            i++;
            if (format[i] == 'l') {
                length = LENGTH_LL;
                i++;
            }
        } else if (format[i] == 'j') {
            length = LENGTH_J;
            i++;
        } else if (format[i] == 'z') {
            length = LENGTH_Z;
            i++;
        } else if (format[i] == 't') {
            length = LENGTH_T;
            i++;
        } else if (format[i] == 'L') {
            length = LENGTH_CAPITAL_L;
            i++;
        }
        
        // Parse conversion specifier
        if (format[i] == '\0') {
            break;
        }
        
        int len = 0;
        
        switch (format[i]) {
            case 'd':
            case 'i': {
                // Signed decimal integer
                intmax_t num;
                
                switch (length) {
                    case LENGTH_HH: num = (signed char)va_arg(args, int); break;
                    case LENGTH_H:  num = (short)va_arg(args, int); break;
                    case LENGTH_L:  num = va_arg(args, long); break;
                    case LENGTH_LL: num = va_arg(args, long long); break;
                    case LENGTH_J:  num = va_arg(args, intmax_t); break;
                    case LENGTH_Z:  num = va_arg(args, size_t); break;
                    case LENGTH_T:  num = va_arg(args, ptrdiff_t); break;
                    default:        num = va_arg(args, int); break;
                }
                
                // Handle negative numbers
                bool negative = num < 0;
                uintmax_t abs_num = negative ? -num : num;
                
                if (negative) {
                    if (written < max_size) {
                        str[written++] = '-';
                    }
                    total_written++;
                    // Remove any conflicting flags
                    flags &= ~(FLAG_PLUS_SIGN | FLAG_SPACE);
                }
                
                len = write_num(str + written, max_size - written, abs_num, 10, width, precision, flags);
                break;
            }
            
            case 'u': {
                // Unsigned decimal integer
                uintmax_t num;
                
                switch (length) {
                    case LENGTH_HH: num = (unsigned char)va_arg(args, unsigned int); break;
                    case LENGTH_H:  num = (unsigned short)va_arg(args, unsigned int); break;
                    case LENGTH_L:  num = va_arg(args, unsigned long); break;
                    case LENGTH_LL: num = va_arg(args, unsigned long long); break;
                    case LENGTH_J:  num = va_arg(args, uintmax_t); break;
                    case LENGTH_Z:  num = va_arg(args, size_t); break;
                    case LENGTH_T:  num = va_arg(args, ptrdiff_t); break;
                    default:        num = va_arg(args, unsigned int); break;
                }
                
                len = write_num(str + written, max_size - written, num, 10, width, precision, flags);
                break;
            }
            
            case 'o': {
                // Octal
                uintmax_t num;
                
                switch (length) {
                    case LENGTH_HH: num = (unsigned char)va_arg(args, unsigned int); break;
                    case LENGTH_H:  num = (unsigned short)va_arg(args, unsigned int); break;
                    case LENGTH_L:  num = va_arg(args, unsigned long); break;
                    case LENGTH_LL: num = va_arg(args, unsigned long long); break;
                    case LENGTH_J:  num = va_arg(args, uintmax_t); break;
                    case LENGTH_Z:  num = va_arg(args, size_t); break;
                    case LENGTH_T:  num = va_arg(args, ptrdiff_t); break;
                    default:        num = va_arg(args, unsigned int); break;
                }
                
                len = write_num(str + written, max_size - written, num, 8, width, precision, flags);
                break;
            }
            
            case 'x':
            case 'X': {
                // Hexadecimal
                if (format[i] == 'X') {
                    flags |= FLAG_UPPERCASE;
                }
                
                uintmax_t num;
                
                switch (length) {
                    case LENGTH_HH: num = (unsigned char)va_arg(args, unsigned int); break;
                    case LENGTH_H:  num = (unsigned short)va_arg(args, unsigned int); break;
                    case LENGTH_L:  num = va_arg(args, unsigned long); break;
                    case LENGTH_LL: num = va_arg(args, unsigned long long); break;
                    case LENGTH_J:  num = va_arg(args, uintmax_t); break;
                    case LENGTH_Z:  num = va_arg(args, size_t); break;
                    case LENGTH_T:  num = va_arg(args, ptrdiff_t); break;
                    default:        num = va_arg(args, unsigned int); break;
                }
                
                len = write_num(str + written, max_size - written, num, 16, width, precision, flags);
                break;
            }
            
            case 'c': {
                // Character
                char c = (char)va_arg(args, int);
                
                if (!(flags & FLAG_LEFT_JUSTIFY)) {
                    // Right justify
                    while (--width > 0 && written < max_size) {
                        str[written++] = ' ';
                        total_written++;
                    }
                }
                
                if (written < max_size) {
                    str[written++] = c;
                }
                total_written++;
                
                if (flags & FLAG_LEFT_JUSTIFY) {
                    // Left justify
                    while (--width > 0 && written < max_size) {
                        str[written++] = ' ';
                        total_written++;
                    }
                }
                
                continue;  // Skip the written += len part
            }
            
            case 's': {
                // String
                const char *s = va_arg(args, const char *);
                len = write_string(str + written, max_size - written, s, width, precision, flags);
                break;
            }
            
            case 'p': {
                // Pointer
                void *ptr = va_arg(args, void *);
                
                // Apply hash flag to show 0x prefix
                flags |= FLAG_HASH;
                
                len = write_num(str + written, max_size - written, (uintptr_t)ptr, 16, width, precision, flags);
                break;
            }
            
            case 'n': {
                // Write current output length to argument
                void *ptr = va_arg(args, void *);
                
                switch (length) {
                    case LENGTH_HH: *(signed char *)ptr = total_written; break;
                    case LENGTH_H:  *(short *)ptr = total_written; break;
                    case LENGTH_L:  *(long *)ptr = total_written; break;
                    case LENGTH_LL: *(long long *)ptr = total_written; break;
                    case LENGTH_J:  *(intmax_t *)ptr = total_written; break;
                    case LENGTH_Z:  *(size_t *)ptr = total_written; break;
                    case LENGTH_T:  *(ptrdiff_t *)ptr = total_written; break;
                    default:        *(int *)ptr = total_written; break;
                }
                
                continue;  // Skip the written += len part
            }
            
            default: {
                // Unknown format specifier, output as-is
                if (written < max_size) {
                    str[written++] = '%';
                }
                total_written++;
                
                if (written < max_size) {
                    str[written++] = format[i];
                }
                total_written++;
                
                continue;  // Skip the written += len part
            }
        }
        
        written += (len > (max_size - written)) ? (max_size - written) : len;
        total_written += len;
    }
    
    // Null-terminate the string
    str[written] = '\0';
    
    return total_written;
}

// Format and print to string
int snprintf(char *str, size_t size, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(str, size, format, args);
    va_end(args);
    return ret;
}

// Format and print to standard output
int printf(const char *format, ...) {
    static char buffer[1024];
    
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    if (ret > 0) {
        const char *p = buffer;
        while (*p) {
            putchar(*p++);
        }
    }
    
    return ret;
}

// Format and print to specified file descriptor
int fprintf(int fd, const char *format, ...) {
    static char buffer[1024];
    
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    if (ret > 0) {
        const char *p = buffer;
        while (*p) {
            switch (fd) {
                case STDOUT_FILENO:
                    if (stdout_putchar_func) {
                        stdout_putchar_func(*p);
                    }
                    break;
                case STDERR_FILENO:
                    if (stderr_putchar_func) {
                        stderr_putchar_func(*p);
                    }
                    break;
            }
            p++;
        }
    }
    
    return ret;
}