/* SPDX-License-Identifier: GPL-2.0-or-later */

.text
.globl main
main:
    mov $1, %eax
    cpuid
    mov %ecx, %r10d

    mov $7, %eax
    xor %ecx, %ecx
    cpuid
    mov %ebx, %r8d

    mov %cr4, %rax
    or $0x40600, %rax       /* OSXSAVE, OSXMMEXCPT, OSFXSR */
    mov %rax, %cr4

    mov $3, %eax           /* x87 and SSE */
    bt $5, %r8d            /* AVX2 */
    jnc 1f
    or $4, %eax            /* YMM */
1:
    mov %eax, %r9d
    xor %ecx, %ecx
    xor %edx, %edx
    xsetbv
    xgetbv
    cmp %r9d, %eax
    jne fail
    test %edx, %edx
    jne fail

    movdqa payload(%rip), %xmm1
    test $4, %r9d
    jz 2f
    movdqa expected_avx(%rip), %xmm2
    vmovdqa payload(%rip), %ymm1
    vpaddq %ymm1, %ymm1, %ymm1
    movdqa %xmm2, result(%rip)
    cmpq $2, result(%rip)
    jne fail
    cmpq $4, result+8(%rip)
    jne fail
2:
    xsave64 state(%rip)
    test $4, %r9d
    jz 3f
    /* The YMMH entry for register 1 begins at 576 + 16. */
    cmpq $6, state+592(%rip)
    jne fail
    cmpq $8, state+600(%rip)
    jne fail
    vzeroall
    vmovdqa %ymm1, result(%rip)
    cmpq $0, result+16(%rip)
    jne fail
    jmp 4f
3:
    pxor %xmm1, %xmm1
4:
    xrstor64 state(%rip)
    lea result(%rip), %rdi
    lea payload(%rip), %rsi
    mov $2, %ecx
    test $4, %r9d
    jz 5f
    vmovdqa %ymm1, (%rdi)
    lea expected_avx(%rip), %rsi
    mov $4, %ecx
    jmp 6f
5:
    movdqa %xmm1, (%rdi)
6:
    repe cmpsq
    jne fail

    xsaveopt64 optimized_state(%rip)
    mov optimized_state+512(%rip), %rcx
    and %r9, %rcx
    cmp %r9, %rcx
    jne fail

    test $4, %r9d
    jz 7f
    vzeroupper
    vmovdqa %ymm1, result(%rip)
    cmpq $2, result(%rip)
    jne fail
    cmpq $0, result+16(%rip)
    jne fail
    cmpq $0, result+24(%rip)
    jne fail

    /* A VEX.128 instruction clears YMMH even when its operands alias. */
    vmovdqu payload(%rip), %ymm1
    vpaddq %xmm1, %xmm1, %xmm1
    vmovdqu %ymm1, result(%rip)
    cmpq $2, result(%rip)
    jne fail
    cmpq $0, result+16(%rip)
    jne fail

    /* The legacy register write must preserve the high half. */
    vmovdqu payload(%rip), %ymm1
    pxor %xmm1, %xmm1
    vmovdqu %ymm1, result(%rip)
    cmpq $0, result(%rip)
    jne fail
    cmpq $3, result+16(%rip)
    jne fail

    /* Also check memory operands and the direction of noncommutative ops. */
    vmovdqu payload(%rip), %ymm1
    vpsubq expected_avx(%rip), %ymm1, %ymm1
    vmovdqu %ymm1, result(%rip)
    cmpq $-1, result(%rip)
    jne fail
    cmpq $-4, result+24(%rip)
    jne fail
    vmovdqu payload(%rip), %ymm1
    vpandn expected_avx(%rip), %ymm1, %ymm1
    vmovdqu %ymm1, result(%rip)
    cmpq $2, result(%rip)
    jne fail
    cmpq $4, result+16(%rip)
    jne fail

    vmovdqu payload(%rip), %ymm1
    vmovdqu expected_avx(%rip), %ymm2
    vperm2i128 $2, %ymm2, %ymm1, %ymm1
    vmovdqu %ymm1, result(%rip)
    cmpq $2, result(%rip)
    jne fail
    cmpq $1, result+16(%rip)
    jne fail
    vinserti128 $1, %xmm2, %ymm1, %ymm1
    vextracti128 $1, %ymm1, %xmm1
    vmovdqu %ymm1, result(%rip)
    cmpq $4, result+8(%rip)
    jne fail
    cmpq $0, result+16(%rip)
    jne fail

    vmovdqu split_operand(%rip), %ymm1
    vpshufd $0x4e, %ymm1, %ymm1
    vmovdqu %ymm1, result(%rip)
    cmpq $2, result(%rip)
    jne fail
    cmpq $3, result+24(%rip)
    jne fail

    vmovdqa payload(%rip), %ymm1
    vpermq $0x1b, %ymm1, %ymm1
    vmovdqa %ymm1, result(%rip)
    cmpq $4, result(%rip)
    jne fail
    cmpq $1, result+24(%rip)
    jne fail

    vpblendd $0xf0, expected_avx(%rip), %ymm1, %ymm1
    vmovdqa %ymm1, result(%rip)
    cmpq $4, result(%rip)
    jne fail
    cmpq $6, result+16(%rip)
    jne fail

    vmovq split_operand(%rip), %xmm1
    vmovdqa %ymm1, result(%rip)
    cmpq $1, result(%rip)
    jne fail
    cmpq $0, result+8(%rip)
    jne fail
    cmpq $0, result+24(%rip)
    jne fail

    vmovdqa payload(%rip), %ymm1
    vpinsrd $1, expected_avx(%rip), %xmm1, %xmm1
    vmovdqa %ymm1, result(%rip)
    movabs $0x0000000200000001, %rax
    cmp %rax, result(%rip)
    jne fail
    cmpq $0, result+16(%rip)
    jne fail

    vinsertps $0x1c, expected_avx+8(%rip), %xmm1, %xmm1
    vmovdqa %ymm1, result(%rip)
    movabs $0x0000000400000001, %rax
    cmp %rax, result(%rip)
    jne fail
    cmpq $0, result+8(%rip)
    jne fail

    vbroadcasti128 split_operand(%rip), %ymm1
    vmovdqa %ymm1, result(%rip)
    cmpq $1, result(%rip)
    jne fail
    cmpq $1, result+16(%rip)
    jne fail
    cmpq $2, result+24(%rip)
    jne fail

    vpmovzxbq split_operand(%rip), %xmm1
    vmovdqa %ymm1, result(%rip)
    cmpq $1, result(%rip)
    jne fail
    cmpq $0, result+8(%rip)
    jne fail
    cmpq $0, result+24(%rip)
    jne fail

    vmovdqa payload(%rip), %ymm1
    vcmpps $2, expected_avx(%rip), %ymm1, %ymm1
    vmovdqa %ymm1, result(%rip)
    cmpq $-1, result(%rip)
    jne fail
    cmpq $-1, result+24(%rip)
    jne fail

    vpbroadcastd split_operand(%rip), %ymm1
    vmovdqa %ymm1, result(%rip)
    movabs $0x0000000100000001, %rax
    cmp %rax, result+24(%rip)
    jne fail
    vpsllvd shift_counts(%rip), %ymm1, %ymm1
    vmovdqa %ymm1, result(%rip)
    movabs $0x8000000000000001, %rax
    cmp %rax, result(%rip)
    jne fail
    cmpq $0, result+8(%rip)
    jne fail
    vpslld payload(%rip), %ymm1, %ymm1
    vmovdqa %ymm1, result(%rip)
    cmpq $2, result(%rip)
    jne fail

    bt $25, %r10d          /* AES */
    jnc 7f
    vpxor %ymm1, %ymm1, %ymm1
    vmovdqa payload(%rip), %ymm2
    pxor %xmm2, %xmm2
    vaesenc %xmm1, %xmm1, %xmm2
    vmovdqa %ymm2, result(%rip)
    movabs $0x6363636363636363, %rax
    cmp %rax, result(%rip)
    jne fail
    cmp %rax, result+8(%rip)
    jne fail
    cmpq $0, result+16(%rip)
    jne fail
7:
    /* Check register and memory forms, including ignored immediate bits. */
    movdqa pclmul_inputs(%rip), %xmm1
    movdqa pclmul_inputs+16(%rip), %xmm2
    pclmulqdq $0xef, %xmm2, %xmm1
    movdqa %xmm1, result(%rip)
    cmpq $0, result(%rip)
    jne fail
    movabs $0x4000000000000000, %rax
    cmp %rax, result+8(%rip)
    jne fail

    movdqa pclmul_inputs(%rip), %xmm1
    pclmulqdq $0x10, pclmul_inputs+16(%rip), %xmm1
    movdqa %xmm1, result(%rip)
    cmpq $1, result(%rip)
    jne fail
    cmpq $0, result+8(%rip)
    jne fail

    bt $30, %r10d          /* RDRAND */
    jnc 8f
    pushq $0x8d6          /* Seed OF/SF/ZF/AF/PF with CF clear. */
    popfq
    rdrand %rax
    pushfq
    pop %rcx
    and $0x8d5, %ecx
    cmp $1, %ecx
    jne fail
8:
    bt $18, %r8d           /* RDSEED */
    jnc 9f
    pushq $0x8d6
    popfq
    rdseed %eax
    pushfq
    pop %rcx
    and $0x8d5, %ecx
    cmp $1, %ecx
    jne fail
    shr $32, %rax
    jne fail
9:
    bt $23, %r10d          /* POPCNT */
    jnc 10f
    mov $-1, %r11d
    movabs $0x112233445566ffff, %rax
    /* Mandatory F3 follows the operand-size prefix, with REX.B for r11w. */
    .byte 0x66, 0xf3, 0x41, 0x0f, 0xb8, 0xc3
    movabs $0x1122334455660010, %rcx
    cmp %rcx, %rax
    jne fail
10:

    mov $0x10, %eax
    jmp finish
fail:
    mov $0x11, %eax
finish:
    out %eax, $0xf4
    hlt

.data
.balign 64
state: .space 1024
optimized_state: .space 1024
.balign 32
result: .space 32
payload: .quad 1, 2, 3, 4
expected_avx: .quad 2, 4, 6, 8
shift_counts: .long 0, 31, 32, -1, 1, 63, 64, 5
pclmul_inputs: .quad 1, 0x8000000000000000, 0x8000000000000000, 1
.balign 4096
.space 4088
split_operand: .quad 1, 2, 3, 4
