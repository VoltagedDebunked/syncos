#include <kstd/string.h>

// Copy memory area
void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *pdest = (uint8_t *)dest;
    const uint8_t *psrc = (const uint8_t *)src;

    // Check for overlapping memory regions
    if ((pdest <= psrc && pdest + n > psrc) ||
        (psrc <= pdest && psrc + n > pdest)) {
        return memmove(dest, src, n);
    }

    // Simple byte-by-byte copy for non-overlapping regions
    for (size_t i = 0; i < n; i++) {
        pdest[i] = psrc[i];
    }

    return dest;
}

// Fill memory with a constant byte
void *memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    
    // Use byte-by-byte filling for simplicity and reliability
    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }

    return s;
}

// Copy memory area, handling overlaps
void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *pdest = (uint8_t *)dest;
    const uint8_t *psrc = (const uint8_t *)src;

    // If source and destination don't overlap, use simple copy
    if (src > dest) {
        for (size_t i = 0; i < n; i++) {
            pdest[i] = psrc[i];
        }
    } 
    // If destination is after source, copy from end to avoid overwrites
    else if (src < dest) {
        for (size_t i = n; i > 0; i--) {
            pdest[i-1] = psrc[i-1];
        }
    }
    // If they're the same, no copy needed

    return dest;
}

// Compare memory areas
int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;

    // Byte-by-byte comparison
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] < p2[i] ? -1 : 1;
        }
    }

    // Memory areas are identical
    return 0;
}

// Copy string from source to destination
char *strcpy(char *dest, const char *src) {
    char *original_dest = dest;
    
    // Copy characters until null terminator
    while (*src != '\0') {
        *dest = *src;
        dest++;
        src++;
    }
    
    // Add null terminator
    *dest = '\0';
    
    return original_dest;
}

// Copy at most n characters from src to dest
char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    
    // Copy characters up to n or null terminator
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    
    // If we haven't filled n characters, pad with null terminators
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    
    return dest;
}

size_t strlen(const char *str) {
    const char *s = str;
    while (*s) {
        s++;
    }
    return s - str;
}

int strcmp(const char *str1, const char *str2) {
    while (*str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    return (unsigned char)*str1 - (unsigned char)*str2;
}

// Concatenate strings
char* strcat(char* dest, const char* src) {
    char* original_dest = dest;
    
    // Move to the end of dest
    while (*dest) {
        dest++;
    }
    
    // Copy src to the end of dest
    while ((*dest++ = *src++)) {
        // Empty loop body
    }
    
    return original_dest;
}

// Find substring in string
char* strstr(const char* haystack, const char* needle) {
    // Empty needle edge case
    if (!*needle) {
        return (char*)haystack;
    }
    
    // For each character in haystack
    for (char* h = (char*)haystack; *h; h++) {
        char* h_pos = h;
        char* n_pos = (char*)needle;
        
        // Check if substring matches at this position
        while (*h_pos && *n_pos && (*h_pos == *n_pos)) {
            h_pos++;
            n_pos++;
        }
        
        // If we reached the end of needle, we found a match
        if (!*n_pos) {
            return h;
        }
    }
    
    // No match found
    return NULL;
}

// Find first occurrence of character in string
char* strchr(const char* str, int ch) {
    while (*str) {
        if (*str == (char)ch) {
            return (char*)str;
        }
        str++;
    }
    
    // Check for null terminator if we're looking for it
    if ((char)ch == '\0') {
        return (char*)str;
    }
    
    return NULL;
}

// Convert string to integer
int atoi(const char* str) {
    int result = 0;
    int sign = 1;
    
    // Skip whitespace
    while (*str == ' ' || *str == '\t' || *str == '\n') {
        str++;
    }
    
    // Handle sign
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    // Convert digits
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return sign * result;
}