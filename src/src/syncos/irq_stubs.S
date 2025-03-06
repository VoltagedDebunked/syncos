# IRQ Service Routine Assembly Stubs for x86_64
# These stubs handle hardware interrupts from the PIC

.section .text
.global irq0_handler, irq1_handler, irq2_handler, irq3_handler
.global irq4_handler, irq5_handler, irq6_handler, irq7_handler
.global irq8_handler, irq9_handler, irq10_handler, irq11_handler
.global irq12_handler, irq13_handler, irq14_handler, irq15_handler

# C function for IRQ handling
.extern irq_dispatch

# Macro for IRQ handlers that do not push an error code
.macro IRQ_HANDLER num
irq\num\()_handler:
    # Save all registers
    push %rax
    push %rcx
    push %rdx
    push %rbx
    push %rbp
    push %rsi
    push %rdi
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15
    
    # Save RFLAGS
    pushfq
    
    # Disable interrupts while handling
    cli
    
    # Pass the IRQ number as a parameter to the handler
    mov $\num, %rdi
    call irq_dispatch
    
    # Restore RFLAGS (includes interrupt flag)
    popfq
    
    # Restore all registers
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rdi
    pop %rsi
    pop %rbp
    pop %rbx
    pop %rdx
    pop %rcx
    pop %rax
    
    # Return from interrupt
    iretq
.endm

# Create all the IRQ handlers
IRQ_HANDLER 0
IRQ_HANDLER 1
IRQ_HANDLER 2
IRQ_HANDLER 3
IRQ_HANDLER 4
IRQ_HANDLER 5
IRQ_HANDLER 6
IRQ_HANDLER 7
IRQ_HANDLER 8
IRQ_HANDLER 9
IRQ_HANDLER 10
IRQ_HANDLER 11
IRQ_HANDLER 12
IRQ_HANDLER 13
IRQ_HANDLER 14
IRQ_HANDLER 15