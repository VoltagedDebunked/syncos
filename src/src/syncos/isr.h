#ifndef _SYNCOS_ISR_H
#define _SYNCOS_ISR_H

#include <stdint.h>

// Interrupt stack frame for x86_64
typedef struct __attribute__((packed)) {
    uint64_t ds;           // Data segment selector
    uint64_t rdi;          // Destination index
    uint64_t rsi;          // Source index
    uint64_t rbp;          // Base pointer
    uint64_t rsp;          // Stack pointer
    uint64_t rbx;          // Base register
    uint64_t rdx;          // Data register
    uint64_t rcx;          // Counter register
    uint64_t rax;          // Accumulator register
    uint64_t int_no;       // Interrupt number
    uint64_t err_code;     // Error code (if applicable)
    uint64_t rip;          // Instruction pointer
    uint64_t cs;           // Code segment
    uint64_t rflags;       // Flags register
    uint64_t user_rsp;     // User stack pointer
    uint64_t user_ss;      // User stack segment
} interrupt_frame_t;

// Exception handler declaration macros
#define DECLARE_ISR(name) void name(void)
#define DECLARE_ISR_ERROR(name) void name(uint64_t error_code)

// Interrupt Service Routines
DECLARE_ISR(isr_divide_by_zero);
DECLARE_ISR(isr_debug);
DECLARE_ISR(isr_nmi);
DECLARE_ISR(isr_breakpoint);
DECLARE_ISR(isr_overflow);
DECLARE_ISR(isr_bound_range_exceeded);
DECLARE_ISR(isr_invalid_opcode);
DECLARE_ISR(isr_device_not_available);
DECLARE_ISR_ERROR(isr_double_fault);
DECLARE_ISR(isr_coprocessor_segment_overrun);
DECLARE_ISR_ERROR(isr_invalid_tss);
DECLARE_ISR_ERROR(isr_segment_not_present);
DECLARE_ISR_ERROR(isr_stack_segment_fault);
DECLARE_ISR_ERROR(isr_general_protection);
DECLARE_ISR_ERROR(isr_page_fault);
DECLARE_ISR(isr_x87_floating_point);
DECLARE_ISR_ERROR(isr_alignment_check);
DECLARE_ISR(isr_machine_check);
DECLARE_ISR(isr_simd_floating_point);
DECLARE_ISR(isr_virtualization);
DECLARE_ISR_ERROR(isr_security_exception);

// Common interrupt handler
void isr_common_handler(interrupt_frame_t *frame);

#endif // _SYNCOS_ISR_H