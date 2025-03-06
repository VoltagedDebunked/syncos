#ifndef _SYNCOS_IRQ_H
#define _SYNCOS_IRQ_H

#include <stdint.h>
#include <stdbool.h>

// Maximum number of IRQ handlers per interrupt
#define MAX_IRQ_HANDLERS 8

// IRQ numbers corresponding to legacy devices
// IRQ range is 0-15 for PIC
typedef enum {
    IRQ_TIMER           = 0,
    IRQ_KEYBOARD        = 1,
    IRQ_CASCADE         = 2,    // Used for cascading the slave PIC
    IRQ_COM2            = 3,    // COM2 and COM4
    IRQ_COM1            = 4,    // COM1 and COM3
    IRQ_LPT2            = 5,    // LPT2 or sound card
    IRQ_FLOPPY          = 6,    // Floppy disk controller
    IRQ_LPT1            = 7,    // LPT1 or sound card
    IRQ_RTC             = 8,    // Real-time clock
    IRQ_AVAILABLE_1     = 9,    // Available or redirected IRQ2
    IRQ_AVAILABLE_2     = 10,   // Available
    IRQ_AVAILABLE_3     = 11,   // Available
    IRQ_PS2_MOUSE       = 12,   // PS/2 Mouse
    IRQ_FPU             = 13,   // Floating point unit / coprocessor
    IRQ_PRIMARY_ATA     = 14,   // Primary ATA channel
    IRQ_SECONDARY_ATA   = 15    // Secondary ATA channel
} irq_number_t;

// IRQ handler function type
typedef bool (*irq_handler_t)(uint8_t irq, void *context);

// IRQ initialization and management
void irq_init(void);
bool irq_register_handler(uint8_t irq, irq_handler_t handler, void *context);
bool irq_unregister_handler(uint8_t irq, irq_handler_t handler);
void irq_enable(uint8_t irq);
void irq_disable(uint8_t irq);
bool irq_is_enabled(uint8_t irq);
void irq_dispatch(uint8_t irq);
void irq_end_of_interrupt(uint8_t irq);

// IRQ handler utilities
void irq_set_all_masked(void);
void irq_clear_all_masked(void);

// High-precision timer helpers using IRQ0
void irq_sleep_ms(uint32_t milliseconds);
uint64_t irq_get_ticks(void);
uint64_t irq_get_uptime_ms(void);

// IRQ debugging
void irq_dump_statistics(void);
void irq_dump_handlers(void);

#endif