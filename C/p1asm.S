#if defined(__x86_64__)
.macro FRAC V, F, T, LO, W
        vpsrlq  $32, \V, \T
        vpmuludq %ymm15, \V, \F
        vpmuludq %ymm15, \T, \T
        vpsrlq  $20, \F, \F
        vpsllq  $12, \T, \T
        vpblendd $0xAA, \T, \F, \F
        vpsubd  \LO, \F, \F
        vpcmpgtd \W, \F, \F
.endm
.macro PAIR OFF, REG
        vpaddd  \OFF-1984(%r8), %ymm0, %ymm6
        vpaddd  \OFF-1952(%r8), %ymm1, %ymm7
        vpaddd  \OFF-1920(%r8), %ymm4, %ymm8
        vpaddd  \OFF-1888(%r8), %ymm5, %ymm9
        vmovdqa %ymm6, \OFF+0(%r8)
        vmovdqa %ymm7, \OFF+32(%r8)
        vmovdqa %ymm8, \OFF+64(%r8)
        vmovdqa %ymm9, \OFF+96(%r8)
        vmovdqa %ymm2, %ymm0
        vmovdqa %ymm3, %ymm1
        vmovdqa %ymm8, %ymm2
        vmovdqa %ymm9, %ymm3
        vmovdqa %ymm6, %ymm4
        vmovdqa %ymm7, %ymm5
        FRAC    %ymm6, %ymm10, %ymm11, 0(%rsp), 32(%rsp)
        FRAC    %ymm8, %ymm12, %ymm13, 64(%rsp), 96(%rsp)
        vpor    %ymm12, %ymm10, %ymm10
        vmovmskps %ymm10, %r9d
        FRAC    %ymm7, %ymm11, %ymm12, 0(%rsp), 32(%rsp)
        FRAC    %ymm9, %ymm13, %ymm14, 64(%rsp), 96(%rsp)
        vpor    %ymm13, %ymm11, %ymm11
        vmovmskps %ymm11, \REG
        shll    $8, \REG
        orl     %r9d, \REG
        xorl    $0xFFFF, \REG
.endm
        .text
        .globl  qt_p1_frac_block_avx2
        .type   qt_p1_frac_block_avx2, @function
        .p2align 5
qt_p1_frac_block_avx2:
        pushq   %rbp
        movq    %rsp, %rbp
        andq    $-32, %rsp
        subq    $128, %rsp
        movl    $0x80000000, %eax
        vmovd   %eax, %xmm10
        vpbroadcastd %xmm10, %ymm10
        vpbroadcastd (%rsi), %ymm11
        vpxor   %ymm10, %ymm11, %ymm11
        vmovdqa %ymm11, 0(%rsp)
        vpbroadcastd 4(%rsi), %ymm11
        vpxor   %ymm10, %ymm11, %ymm11
        vmovdqa %ymm11, 32(%rsp)
        vpbroadcastd 8(%rsi), %ymm11
        vpxor   %ymm10, %ymm11, %ymm11
        vmovdqa %ymm11, 64(%rsp)
        vpbroadcastd 12(%rsi), %ymm11
        vpxor   %ymm10, %ymm11, %ymm11
        vmovdqa %ymm11, 96(%rsp)
        movl    $2251799814, %eax
        vmovq   %rax, %xmm15
        vpbroadcastq %xmm15, %ymm15
        vmovdqa 64*28(%rdi), %ymm0
        vmovdqa 64*28+32(%rdi), %ymm1
        vmovdqa 64*30(%rdi), %ymm2
        vmovdqa 64*30+32(%rdi), %ymm3
        vmovdqa 64*29(%rdi), %ymm4
        vmovdqa 64*29+32(%rdi), %ymm5
        leaq    64*31(%rdi), %r8
        xorl    %ecx, %ecx
        xorl    %eax, %eax
        .p2align 5
1:
        PAIR    0, %r10d
        PAIR    128, %r11d
        shll    $16, %r11d
        orl     %r10d, %r11d
        movq    %rcx, %r10
        shlq    $32, %r10
        orq     %r11, %r10
        movq    %r10, (%rdx,%rax,8)
        xorl    %r10d, %r10d
        testl   %r11d, %r11d
        setnz   %r10b
        addq    %r10, %rax
        addq    $256, %r8
        addl    $2, %ecx
        cmpl    $128, %ecx
        jne     1b
        vzeroupper
        movq    %rbp, %rsp
        popq    %rbp
        ret
        .size   qt_p1_frac_block_avx2, .-qt_p1_frac_block_avx2
#endif
        .section .note.GNU-stack,"",@progbits
