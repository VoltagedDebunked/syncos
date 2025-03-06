# Interrupt Service Routine Assembly Stubs for x86_64

.section .text
.global isr_stubs
.global isr_common_handler_wrapper

# Macro to create an ISR stub without error code
.macro ISR_NOERR vector
.global isr_\vector
isr_\vector:
    cli                     # Disable interrupts
    push $0                 # Push dummy error code
    push $\vector           # Push interrupt vector
    jmp isr_common_stub     # Jump to common handler
.endm

# Macro to create an ISR stub with error code
.macro ISR_ERR vector
.global isr_\vector
isr_\vector:
    cli                     # Disable interrupts
    push $\vector           # Push interrupt vector
    jmp isr_common_stub     # Jump to common handler
.endm

# Common stub for all interrupts
isr_common_stub:
    # Save all general-purpose registers
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15

    # Prepare stack frame for C handler
    mov %rsp, %rdi          # Pass stack pointer as argument
    call isr_common_handler # Call C exception handler

    # We should never return from the handler, but just in case
    cli
    hlt

# Define individual ISR stubs
# Exceptions (0-31)
ISR_NOERR 0   # Divide by Zero
ISR_NOERR 1   # Debug
ISR_NOERR 2   # Non-Maskable Interrupt
ISR_NOERR 3   # Breakpoint
ISR_NOERR 4   # Overflow
ISR_NOERR 5   # Bound Range Exceeded
ISR_NOERR 6   # Invalid Opcode
ISR_NOERR 7   # Device Not Available
ISR_ERR   8   # Double Fault
ISR_NOERR 9   # Coprocessor Segment Overrun
ISR_ERR   10  # Invalid TSS
ISR_ERR   11  # Segment Not Present
ISR_ERR   12  # Stack-Segment Fault
ISR_ERR   13  # General Protection
ISR_ERR   14  # Page Fault
ISR_NOERR 15  # Reserved
ISR_NOERR 16  # x87 Floating-Point Exception
ISR_ERR   17  # Alignment Check
ISR_NOERR 18  # Machine Check
ISR_NOERR 19  # SIMD Floating-Point Exception
ISR_NOERR 20  # Virtualization Exception
ISR_NOERR 21  # Reserved
ISR_NOERR 22  # Reserved
ISR_NOERR 23  # Reserved
ISR_NOERR 24  # Reserved
ISR_NOERR 25  # Reserved
ISR_NOERR 26  # Reserved
ISR_NOERR 27  # Reserved
ISR_NOERR 28  # Reserved
ISR_NOERR 29  # Reserved
ISR_ERR   30  # Security Exception
ISR_NOERR 31  # Reserved

# Pad remaining vectors with null handlers
.altmacro
.macro DEFINE_NULL_ISR num
.global isr_\num
isr_\num:
    iret
.endm

# Define null ISRs for remaining vectors
.set i, 32
.rept 224
    DEFINE_NULL_ISR %i
    .set i, i+1
.endr