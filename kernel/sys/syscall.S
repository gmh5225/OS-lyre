#include <abi-bits/errno.h>

.section .text

.global syscall_sysenter_entry
syscall_sysenter_entry:
    swapgs

    push $0x53
    push %r10
    pushfq
    push $0x4b
    push %r11

    push $0

    push %r15
    push %r14
    push %r13
    push %r12
    push %r11
    push %r10
    push %r9
    push %r8
    push %rbp
    push %rdi
    push %rsi
    push %rdx
    push %rcx
    push %rbx
    push %rax

    mov %es, %eax
    push %rax
    mov %ds, %eax
    push %rax

    cld

    mov %rdi, %rbx
    mov %rsp, %rdi

    movq $-1, 16(%rsp)
    movq $EINVAL, 24(%rsp)

    cmp syscall_count, %rbx
    jae 1f

    sti
    call *syscall_table(,%rbx,8)
    cli

    mov %rax, 16(%rsp)
    mov %gs:0x8, %rax
    mov %rax, 24(%rsp)

1:
    pop %rax
    mov %eax, %ds
    pop %rax
    mov %eax, %es

    pop %rax
    pop %rbx
    pop %rcx
    pop %rdx
    pop %rsi
    pop %rdi
    pop %rbp
    pop %r8
    pop %r9
    pop %rcx
    pop %rdx
    pop %r12
    pop %r13
    pop %r14
    pop %r15

    swapgs

    sysexitq
