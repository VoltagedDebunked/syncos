.section .text
.global process_switch_context

# void process_switch_context(uint64_t* old_ctx, uint64_t* new_ctx)
# rdi = old_ctx, rsi = new_ctx
process_switch_context:
    # Save current registers to old context
    movq %rax, 0(%rdi)
    movq %rbx, 8(%rdi)
    movq %rcx, 16(%rdi) 
    movq %rdx, 24(%rdi)
    movq %rsi, 32(%rdi)
    # Save original rdi after using it
    pushq %rdi
    movq (%rsp), %rax
    movq %rax, 40(%rdi)
    popq %rax
    movq %rbp, 48(%rdi)
    # Save stack pointer
    movq %rsp, 56(%rdi)
    movq %r8,  64(%rdi)
    movq %r9,  72(%rdi)
    movq %r10, 80(%rdi)
    movq %r11, 88(%rdi)
    movq %r12, 96(%rdi)
    movq %r13, 104(%rdi)
    movq %r14, 112(%rdi)
    movq %r15, 120(%rdi)
    # Save RFLAGS
    pushfq
    popq %rax
    movq %rax, 128(%rdi)

    # Load registers from new context
    movq 0(%rsi),   %rax
    movq 8(%rsi),   %rbx
    movq 16(%rsi),  %rcx
    movq 24(%rsi),  %rdx
    # Load %rdi last since we're using it
    movq 32(%rsi),  %r8    # Temporarily store %rsi value
    movq 40(%rsi),  %rdi
    movq %r8,       %rsi   # Now safe to overwrite %rsi
    movq 48(%rsi),  %rbp
    movq 56(%rsi),  %rsp
    movq 64(%rsi),  %r8
    movq 72(%rsi),  %r9
    movq 80(%rsi),  %r10
    movq 88(%rsi),  %r11
    movq 96(%rsi),  %r12
    movq 104(%rsi), %r13
    movq 112(%rsi), %r14
    movq 120(%rsi), %r15
    # Load RFLAGS
    movq 128(%rsi), %rax
    pushq %rax
    popfq

    # Return to new context
    ret