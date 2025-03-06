# Interrupt Service Routine Assembly Stubs for x86_64
# These stubs handle CPU exceptions and interrupts

.section .text
.global isr_0, isr_1, isr_2, isr_3, isr_4, isr_5, isr_6, isr_7
.global isr_8, isr_9, isr_10, isr_11, isr_12, isr_13, isr_14, isr_15
.global isr_16, isr_17, isr_18, isr_19, isr_20, isr_21, isr_22, isr_23
.global isr_24, isr_25, isr_26, isr_27, isr_28, isr_29, isr_30, isr_31

# C function for common ISR handling
.extern isr_common_handler

# Macro for ISR handlers that do not push an error code
.macro ISR_NOERR num
isr_\num:
    push $0              # Push dummy error code
    push $\num           # Push interrupt number
    jmp isr_common_stub
.endm

# Macro for ISR handlers that push an error code
.macro ISR_ERR num
isr_\num:
    # Error code automatically pushed by CPU
    push $\num           # Push interrupt number
    jmp isr_common_stub
.endm

# Common ISR stub that saves processor state and calls C handler
isr_common_stub:
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
    
    # Save data segment
    mov %ds, %ax
    push %rax
    
    # Load kernel data segment
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    
    # Call C handler - pass stack pointer as argument
    mov %rsp, %rdi
    call isr_common_handler
    
    # Restore data segment
    pop %rax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    
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
    
    # Clean up error code and interrupt number
    add $16, %rsp
    
    # Return from interrupt
    iretq

# Define all 32 ISR handlers
# Error code is pushed by the CPU for these specific exceptions
ISR_NOERR 0    # Divide by Zero
ISR_NOERR 1    # Debug Exception
ISR_NOERR 2    # Non-maskable Interrupt
ISR_NOERR 3    # Breakpoint
ISR_NOERR 4    # Overflow
ISR_NOERR 5    # Bound Range Exceeded
ISR_NOERR 6    # Invalid Opcode
ISR_NOERR 7    # Device Not Available
ISR_ERR   8    # Double Fault
ISR_NOERR 9    # Coprocessor Segment Overrun
ISR_ERR   10   # Invalid TSS
ISR_ERR   11   # Segment Not Present
ISR_ERR   12   # Stack-Segment Fault
ISR_ERR   13   # General Protection Fault
ISR_ERR   14   # Page Fault
ISR_NOERR 15   # Reserved
ISR_NOERR 16   # x87 Floating-Point Exception
ISR_ERR   17   # Alignment Check
ISR_NOERR 18   # Machine Check
ISR_NOERR 19   # SIMD Floating-Point Exception
ISR_NOERR 20   # Virtualization Exception
ISR_ERR   21   # Control Protection Exception
ISR_NOERR 22   # Reserved
ISR_NOERR 23   # Reserved
ISR_NOERR 24   # Reserved
ISR_NOERR 25   # Reserved
ISR_NOERR 26   # Reserved
ISR_NOERR 27   # Reserved
ISR_NOERR 28   # Reserved
ISR_NOERR 29   # Reserved
ISR_ERR   30   # Security Exception
ISR_NOERR 31   # Reserved