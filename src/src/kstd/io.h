#ifndef SYNCOS_KSTD_IO_H
#define SYNCOS_KSTD_IO_H

#include <stdint.h>

/**
 * Read a byte from an I/O port
 * 
 * @param port The I/O port to read from
 * @return The byte read from the port
 */
static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/**
 * Write a byte to an I/O port
 * 
 * @param port The I/O port to write to
 * @param value The byte to write
 */
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

/**
 * Read a word (2 bytes) from an I/O port
 * 
 * @param port The I/O port to read from
 * @return The word read from the port
 */
static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/**
 * Write a word (2 bytes) to an I/O port
 * 
 * @param port The I/O port to write to
 * @param value The word to write
 */
static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

/**
 * Read a double word (4 bytes) from an I/O port
 * 
 * @param port The I/O port to read from
 * @return The double word read from the port
 */
static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/**
 * Write a double word (4 bytes) to an I/O port
 * 
 * @param port The I/O port to write to
 * @param value The double word to write
 */
static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

/**
 * Insert a short delay by reading from an unused port
 * This is commonly used after I/O operations that need a small delay
 */
static inline void io_wait(void) {
    // Port 0x80 is used for POST codes during boot
    // Reading from it causes a short delay
    __asm__ volatile("inb $0x80, %%al" : : : "eax");
}

#endif // SYNCOS_KSTD_IO_H