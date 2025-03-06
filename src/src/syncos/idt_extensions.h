#ifndef _SYNCOS_IDT_EXTENSIONS_H
#define _SYNCOS_IDT_EXTENSIONS_H

#include <syncos/idt.h>
#include <stdint.h>
#include <stdbool.h>

// Enhanced error reporting for exceptions
typedef struct {
    uint64_t vector;        // Exception vector number
    uint64_t error_code;    // Error code (if applicable)
    uint64_t rip;           // Instruction pointer where exception occurred
    uint64_t rsp;           // Stack pointer at time of exception
    uint64_t rbp;           // Base pointer at time of exception
    uint64_t cs;            // Code segment at time of exception
    uint64_t ss;            // Stack segment at time of exception
    uint64_t rflags;        // CPU flags at time of exception
    uint64_t cr0;           // Control register 0
    uint64_t cr2;           // Control register 2 (page fault address)
    uint64_t cr3;           // Control register 3 (page directory base)
    uint64_t cr4;           // Control register 4
} exception_info_t;

// Enhanced exception handler type with more context
typedef void (*extended_exception_handler_t)(exception_info_t *info);

// Interrupt callback system - allows registration of multiple handlers per IRQ
typedef bool (*interrupt_callback_t)(uint8_t irq, void *context);

// Register an extended exception handler with more detailed context
void idt_register_extended_exception_handler(uint8_t vector, extended_exception_handler_t handler);

// IRQ management functions
bool idt_register_irq_handler(uint8_t irq, interrupt_callback_t callback, void *context);
bool idt_unregister_irq_handler(uint8_t irq, interrupt_callback_t callback);
void idt_mask_irq(uint8_t irq);
void idt_unmask_irq(uint8_t irq);
void idt_end_of_interrupt(uint8_t irq);

// PIC management functions
void idt_pic_init(uint8_t master_offset, uint8_t slave_offset);
void idt_pic_disable(void);

// Extended IDT management
void idt_set_privilege_level(uint8_t vector, uint8_t ring_level);
void idt_set_ist_stack(uint8_t vector, uint8_t ist_index);

// Debug helpers
void idt_dump_entry(uint8_t vector);
void idt_dump_table(void);
const char *idt_exception_name(uint8_t vector);

#endif // _SYNCOS_IDT_EXTENSIONS_H