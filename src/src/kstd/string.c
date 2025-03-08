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