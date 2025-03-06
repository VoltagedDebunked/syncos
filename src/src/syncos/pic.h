#ifndef _SYNCOS_PIC_H
#define _SYNCOS_PIC_H

#include <stdint.h>
#include <stdbool.h>

// 8259 Programmable Interrupt Controller ports
#define PIC1_COMMAND    0x20
#define PIC1_DATA       0x21
#define PIC2_COMMAND    0xA0
#define PIC2_DATA       0xA1

// PIC commands
#define PIC_EOI         0x20    // End of Interrupt
#define PIC_READ_IRR    0x0A    // Read Interrupt Request Register
#define PIC_READ_ISR    0x0B    // Read In-Service Register

// IRQ numbers
#define IRQ_TIMER       0
#define IRQ_KEYBOARD    1
#define IRQ_CASCADE     2
#define IRQ_COM2        3
#define IRQ_COM1        4
#define IRQ_LPT2        5
#define IRQ_FLOPPY      6
#define IRQ_LPT1        7
#define IRQ_RTC         8
#define IRQ_PS2_MOUSE   12
#define IRQ_COPROC      13
#define IRQ_PRIMARY_ATA 14
#define IRQ_SECOND_ATA  15

// Initialize the PIC with the given vector offsets
void pic_init(uint8_t master_offset, uint8_t slave_offset);

// Disable the PIC (useful when switching to APIC)
void pic_disable(void);

// Enable (unmask) a specific IRQ
void pic_enable_irq(uint8_t irq);

// Disable (mask) a specific IRQ
void pic_disable_irq(uint8_t irq);

// Send an End-Of-Interrupt signal
void pic_send_eoi(uint8_t irq);

// Get the combined IRR (Interrupt Request Register)
uint16_t pic_get_irr(void);

// Get the combined ISR (In-Service Register)
uint16_t pic_get_isr(void);

// Check if the PIC is currently servicing the specified IRQ
bool pic_is_irq_in_service(uint8_t irq);

// Check if an IRQ is pending
bool pic_is_irq_pending(uint8_t irq);

// Wait for an interrupt to finish
void pic_wait_for_irq_completion(uint8_t irq);

#endif // _SYNCOS_PIC_H