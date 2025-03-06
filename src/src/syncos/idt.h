#ifndef _SYNCOS_IDT_H
#define _SYNCOS_IDT_H

#include <stdint.h>
#include <stdbool.h>

// Total number of interrupt vectors
#define IDT_ENTRIES 256

// Interrupt gate types
#define IDT_GATE_INTERRUPT 0x8E    // Present, Ring 0, Interrupt Gate
#define IDT_GATE_TRAP      0x8F    // Present, Ring 0, Trap Gate
#define IDT_GATE_TASK      0x85    // Present, Ring 0, Task Gate

// IDT Entry Structure for x86_64
typedef struct __attribute__((packed)) {
    uint16_t offset_low;       // Lower 16 bits of ISR address
    uint16_t selector;         // Kernel code segment selector
    uint8_t  ist;              // Interrupt Stack Table index
    uint8_t  type_attr;        // Gate type and attributes
    uint16_t offset_mid;       // Middle 16 bits of ISR address
    uint32_t offset_high;      // Upper 32 bits of ISR address
    uint32_t reserved;         // Reserved, must be zero
} idt_entry_t;

// IDTR Structure for loading IDT
typedef struct __attribute__((packed)) {
    uint16_t limit;            // Size of IDT
    uint64_t base;             // Base address of IDT
} idtr_t;

// Interrupt handler function type
typedef void (*interrupt_handler_t)(void);

// Public function prototypes
void idt_init(void);
void idt_set_handler(uint8_t vector, interrupt_handler_t handler, uint8_t type);
void idt_enable_interrupts(void);
void idt_disable_interrupts(void);
bool idt_are_interrupts_enabled(void);

// Exception error handler type
typedef void (*exception_handler_t)(uint64_t error_code, uint64_t rip);

// Register exception handlers
void idt_register_exception_handler(uint8_t vector, exception_handler_t handler);

#endif // _SYNCOS_IDT_H