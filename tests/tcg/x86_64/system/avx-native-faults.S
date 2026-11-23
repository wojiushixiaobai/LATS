/* SPDX-License-Identifier: GPL-2.0-or-later */

.text
.globl main

.macro idt_gate n, handler
    lea idt+16*\n(%rip), %rdi
    lea \handler(%rip), %rax
    mov %ax, 0(%rdi)
    movw $8, 2(%rdi)
    movw $0x8e00, 4(%rdi)
    shr $16, %rax
    mov %ax, 6(%rdi)
    shr $16, %rax
    mov %eax, 8(%rdi)
.endm

main:
    mov %cr4, %rax
    or $0x40600, %rax
    mov %rax, %cr4
    xor %ecx, %ecx
    xor %edx, %edx
    mov $7, %eax
    xsetbv

    idt_gate 6, ud
    idt_gate 13, gp
    idt_gate 14, pf
    idt_gate 19, xm
    lidt idtr(%rip)

    /* Reject a non-reserved VEX.vvvv on a two-operand move. */
    lea ud_return(%rip), %r12
    xor %r15d, %r15d
    .byte 0xc5, 0xf1, 0x6f, 0xc1
ud_return:
    cmp $6, %r15d
    jne fail

    /* Misaligned VMOVDQA must fault before writing the destination. */
    lea gp_return(%rip), %r12
    xor %r15d, %r15d
    vmovdqa data+16(%rip), %ymm0
gp_return:
    cmp $13, %r15d
    jne fail

    /* Unmap the 2 MiB page starting at 4 MiB. */
    mov %cr3, %rax
    mov (%rax), %rcx
    and $-4096, %rcx
    mov (%rcx), %rcx
    and $-4096, %rcx
    movq $0, 16(%rcx)
    mov %rax, %cr3

    mov $0x3ffffc, %ebx
    movl $0x718293a4, (%rbx)
    vmovdqa mask_safe(%rip), %ymm1
    vpmaskmovd (%rbx), %ymm1, %ymm0
    vmovdqa %ymm0, result(%rip)
    cmpq $0x718293a4, result(%rip)
    jne fail
    cmpq $0, result+24(%rip)
    jne fail
    vmovdqa data(%rip), %ymm0
    vpmaskmovd %ymm0, %ymm1, (%rbx)
    cmpl $0x01020304, (%rbx)
    jne fail

    /* An active second element faults and must preserve both destination halves. */
    vmovdqa mask_bad(%rip), %ymm1
    lea pf_return(%rip), %r12
    xor %r15d, %r15d
    vpmaskmovd (%rbx), %ymm1, %ymm0
pf_return:
    cmp $14, %r15d
    jne fail
    mov %cr2, %rax
    cmp $0x400000, %rax
    jne fail
    vmovdqa %ymm0, result(%rip)
    cmpq $0x01020304, result(%rip)
    jne fail
    cmpq $0x01020304, result+24(%rip)
    jne fail

    /* Masked denormal inputs set DE. DAZ preserves the sign of the zero. */
    ldmxcsr fp_masked(%rip)
    vmovdqa fp_subnormal(%rip), %ymm1
    vmovdqa fp_pair(%rip), %ymm2
    vminps %ymm2, %ymm1, %ymm0
    vmovdqa %ymm0, result(%rip)
    movabs $0x8000000100000000, %rax
    cmp %rax, result(%rip)
    jne fail
    stmxcsr fp_status(%rip)
    testl $2, fp_status(%rip)
    jz fail

    ldmxcsr fp_daz(%rip)
    vminps %ymm2, %ymm1, %ymm0
    vmovdqa %ymm0, result(%rip)
    movabs $0x8000000000000000, %rax
    cmp %rax, result(%rip)
    jne fail
    stmxcsr fp_status(%rip)
    testl $2, fp_status(%rip)
    jnz fail

    /* Only the upper lane raises invalid. The whole destination survives. */
    ldmxcsr fp_unmasked(%rip)
    vmovdqa data(%rip), %ymm0
    vmovdqa fp_nan(%rip), %ymm1
    lea xm_return(%rip), %r12
    xor %r15d, %r15d
    vminps %ymm2, %ymm1, %ymm0
xm_return:
    cmp $19, %r15d
    jne fail
    vmovdqa %ymm0, result(%rip)
    cmpq $0x01020304, result(%rip)
    jne fail
    cmpq $0x01020304, result+24(%rip)
    jne fail
    mov $0x10, %eax
    out %eax, $0xf4
fail:
    mov $0x11, %eax
    out %eax, $0xf4
    hlt
ud:
    mov $6, %r15d
    jmp noerr
xm:
    mov $19, %r15d
noerr:
    mov %r12, (%rsp)
    iretq
gp:
    mov $13, %r15d
    jmp err
pf:
    mov $14, %r15d
err:
    add $8, %rsp
    mov %r12, (%rsp)
    iretq
.data
.balign 32
data: .quad 0x01020304, 0x01020304, 0x01020304, 0x01020304
mask_safe: .long 0x80000000, 0, 0, 0, 0, 0, 0, 0
mask_bad: .long 0x80000000, 0x80000000, 0, 0, 0, 0, 0, 0
result: .space 32
fp_subnormal: .long 1, 0, 0, 0, 0, 0, 0, 0
fp_pair: .long 0, 0x80000001, 0, 0, 0, 0, 0, 0
fp_nan: .long 0, 0, 0, 0, 0x7fc00001, 0, 0, 0
fp_masked: .long 0x1f80
fp_daz: .long 0x1fc0
fp_unmasked: .long 0x1f00
fp_status: .long 0
idtr: .short 20*16-1
      .quad idt
.balign 16
idt: .space 20*16
