save_stack_state:
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, %rax
    jmp *%rdi

.globl __ll_co_yield_now
__ll_co_yield_now:
    movq %rdi, %r8
    leaq .__ll_co_yield_now.cont(%rip), %rdi
    jmp save_stack_state
.__ll_co_yield_now.cont:
    movq %rax, (%r8) # Store the old RSP into (%RDI)
    movq %rsi, %rsp # Load the new RSP
    # We are now on the target stack.
    # We assume that the target stack is yielded previously.
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    ret

.globl __ll_init_co_stack
__ll_init_co_stack:
    movq %rdi, %r8
    leaq .__ll_init_co_stack.cont(%rip), %rdi
    jmp save_stack_state
.__ll_init_co_stack.cont:
    movq %rax, (%r8) # Store the old RSP into (%RDI)
    movq %rsi, %rsp # Load the new RSP
    # We are now on the target stack.
    # Nothing is on the stack now.
    movq %rcx, %rdi # user_data

    call *%rdx # The initialization function.
    call abort@PLT
    ret
