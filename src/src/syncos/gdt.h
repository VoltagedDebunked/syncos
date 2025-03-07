#ifndef _SYNCOS_GDT_H
#define _SYNCOS_GDT_H

#include <stdint.h>
#include <stdbool.h>

// Number of GDT entries (including NULL, code, data, user segments, and TSS)
#define GDT_ENTRIES 6

// GDT Segment Selectors
#define GDT_NULL        0x00
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE   0x18
#define GDT_USER_DATA   0x20
#define GDT_TSS         0x28

// Access Byte Flags
#define GDT_KERNEL_CODE_ACCESS 0x9A  // Present, Ring 0, Executable, Readable
#define GDT_KERNEL_DATA_ACCESS 0x92  // Present, Ring 0, Writable
#define GDT_USER_CODE_ACCESS   0xFA  // Present, Ring 3, Executable, Readable
#define GDT_USER_DATA_ACCESS   0xF2  // Present, Ring 3, Writable
#define GDT_TSS_ACCESS         0x89  // Present, TSS Descriptor

// Granularity Flags
#define GDT_KERNEL_CODE_FLAGS  0xA0  // 4KB Gran, 64-bit
#define GDT_KERNEL_DATA_FLAGS  0xA0  // 4KB Gran, 64-bit
#define GDT_USER_CODE_FLAGS    0xA0  // 4KB Gran, 64-bit
#define GDT_USER_DATA_FLAGS    0xA0  // 4KB Gran, 64-bit
#define GDT_TSS_FLAGS          0x00  // No granularity for TSS

// GDT Entry Structure
typedef struct __attribute__((packed)) {
    uint16_t limit_low;      // Lower 16 bits of limit
    uint16_t base_low;       // Lower 16 bits of base
    uint8_t  base_middle;    // Next 8 bits of base
    uint8_t  access;         // Access flags
    uint8_t  granularity;    // Granularity and upper 4 bits of limit
    uint8_t  base_high;      // Highest 8 bits of base
} gdt_entry_t;

// GDTR Structure for loading GDT
typedef struct __attribute__((packed)) {
    uint16_t limit;          // Size of GDT
    uint64_t base;           // Base address of GDT
} gdtr_t;

// Task State Segment (TSS) Structure
typedef struct __attribute__((packed)) {
    uint32_t reserved0;      // Reserved
    uint64_t rsp0;           // Ring 0 Stack Pointer
    uint64_t rsp1;           // Ring 1 Stack Pointer
    uint64_t rsp2;           // Ring 2 Stack Pointer
    uint64_t reserved1;      // Reserved
    uint64_t ist[7];         // Interrupt Stack Table
    uint32_t reserved2;      // Reserved
    uint32_t reserved3;      // Reserved
    uint16_t reserved4;      // Reserved
    uint16_t iomap_base;     // Base address of I/O permissions bitmap
} tss_t;

// Basic GDT functions
void gdt_init(void);
void gdt_set_kernel_stack(uint64_t stack);
uint64_t gdt_get_kernel_stack(void);

// Enhanced GDT functions for fault tolerance
void gdt_flush(void);        // Load the GDT into the CPU
void gdt_reload(void);       // Reload GDT and all segment registers
bool gdt_recover(void);      // Attempt to recover corrupted GDT
void gdt_verify(uint64_t tick_count, void *context); // Verify GDT integrity periodically
void gdt_register_verification(void); // Register GDT verification with timer system
void gdt_dump(void);         // Dump GDT info

#endif