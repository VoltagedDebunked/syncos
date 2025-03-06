# Timer IRQ Assembly Stub for x86_64
# Dedicated assembly handler for IRQ0 (Timer)

.section .text
.global timer_irq_stub

# C function for timer handling
.extern timer_irq_handler
.extern pic_send_eoi

# Timer IRQ handler - optimized for minimal latency
timer_irq_stub:
    # Save all registers that might be modified
    push %rax
    push %rcx
    push %rdx
    push %rdi
    push %rsi
    push %r8
    push %r9
    push %r10
    push %r11
    
    # First parameter (RDI) = IRQ number (0 for timer)
    mov $0, %rdi
    # Second parameter (RSI) = NULL context
    xor %rsi, %rsi
    
    # Call the timer handler
    call timer_irq_handler
    
    # Send EOI to PIC - IRQ0 only needs to be sent to master PIC
    mov $0x20, %al    # EOI command
    mov $0x20, %dx    # PIC1 command port
    out %al, %dx
    
    # Restore registers
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rsi
    pop %rdi
    pop %rdx
    pop %rcx
    pop %rax
    
    # Return from interrupt
    iretq