#define KW0 pulse_kc+0(%rip)
#define KW1 pulse_kc+32(%rip)
#define KW2 pulse_kc+64(%rip)
#define KW3 pulse_kc+96(%rip)
#define KW4 pulse_kc+128(%rip)
#define KW5 pulse_kc+160(%rip)
#define KW6 pulse_kc+192(%rip)
#define KE1 pulse_kc+224(%rip)
#define KE2 pulse_kc+256(%rip)
#define KE3 pulse_kc+288(%rip)
#define KE4 pulse_kc+320(%rip)
#define KE5 pulse_kc+352(%rip)
#define KE6 pulse_kc+384(%rip)
#define KK pulse_kc+416(%rip)
#define KG pulse_kc+448(%rip)
#define KTWO pulse_kc+480(%rip)

#define YP_1(b) (b,%r8)
#define YP_2(b) (b,%r8,2)
#define YP_3(b) (b,%r10)
#define YP_4(b) (b,%r8,4)
#define YP_5(b) (b,%rax)
#define YP_6(b) (b,%r10,2)
#define YM_1(b) (b,%r9)
#define YM_2(b) (b,%r9,2)
#define YM_3(b) (b,%r11)
#define YM_4(b) (b,%r9,4)
#define YM_5(b) (b,%rbx)
#define YM_6(b) (b,%r11,2)
#define ZP_1(b) (b,%r12)
#define ZP_2(b) (b,%r12,2)
#define ZP_3(b) (b,%r14)
#define ZP_4(b) (b,%r12,4)
#define ZP_5(b) (b,%rbp)
#define ZP_6(b) (b,%r14,2)
#define ZM_1(b) (b,%r13)
#define ZM_2(b) (b,%r13,2)
#define ZM_3(b) (b,%r15)
#define ZM_4(b) (b,%r13,4)
#define ZM_5(b) (b,%rcx)
#define ZM_6(b) (b,%r15,2)
#define YP(a, b) YP_##a(b)
#define YM(a, b) YM_##a(b)
#define ZP(a, b) ZP_##a(b)
#define ZM(a, b) ZM_##a(b)

#define TERM_FIRST(A, WA, EA) \
	vlddqu 4*A(%rdi), %ymm4;		  \
	vmovups YP(A, %rdi), %ymm5;		  \
	vmovups ZP(A, %rdi), %ymm6;		  \
	vaddps -4*A(%rdi), %ymm4, %ymm7;	  \
	vmulps WA, %ymm7, %ymm0;		  \
	vaddps YM(A, %rdi), %ymm5, %ymm7;	  \
	vmulps WA, %ymm7, %ymm1;		  \
	vaddps ZM(A, %rdi), %ymm6, %ymm7;	  \
	vmulps WA, %ymm7, %ymm2;		  \
	vaddps YM(A, %rsi), %ymm4, %ymm8;	  \
	vaddps %ymm5, %ymm8, %ymm8;		  \
	vaddps ZM(A, %rsi), %ymm6, %ymm7;	  \
	vaddps %ymm7, %ymm8, %ymm8;		  \
	vmulps EA, %ymm8, %ymm3

#define TERM(A, WA, EA) \
	vlddqu 4*A(%rdi), %ymm4; \
	vmovups YP(A, %rdi), %ymm5; \
	vmovups ZP(A, %rdi), %ymm6; \
	vaddps -4*A(%rdi), %ymm4, %ymm7; \
	vmulps WA, %ymm7, %ymm7; \
	vaddps %ymm7, %ymm0, %ymm0;		  \
	vaddps YM(A, %rdi), %ymm5, %ymm7; \
	vmulps WA, %ymm7, %ymm7; \
	vaddps %ymm7, %ymm1, %ymm1;		  \
	vaddps ZM(A, %rdi), %ymm6, %ymm7; \
	vmulps WA, %ymm7, %ymm7; \
	vaddps %ymm7, %ymm2, %ymm2;		  \
	vaddps YM(A, %rsi), %ymm4, %ymm8; \
	vaddps %ymm5, %ymm8, %ymm8; \
	vaddps ZM(A, %rsi), %ymm6, %ymm7; \
	vaddps %ymm7, %ymm8, %ymm8; \
	vmulps EA, %ymm8, %ymm8; \
	vaddps %ymm8, %ymm3, %ymm3

#define RHS8 \
	vmovups (%rdi), %ymm9;			  \
	TERM_FIRST(6, %ymm15, KE6); \
	TERM(5, %ymm14, KE5); \
	TERM(4, %ymm13, KE4); \
	TERM(3, %ymm12, KE3); \
	TERM(2, %ymm11, KE2); \
	TERM(1, %ymm10, KE1); \
	vmulps KW0, %ymm9, %ymm7;		  \
	vaddps %ymm7, %ymm0, %ymm0;		  \
	vaddps %ymm7, %ymm1, %ymm1;		  \
	vaddps %ymm7, %ymm2, %ymm2;		  \
	vaddps %ymm1, %ymm0, %ymm0;		  \
	vaddps %ymm2, %ymm0, %ymm0;		  \
	vmulps KK, %ymm0, %ymm0;		  \
	vmulps KTWO, %ymm9, %ymm8;		  \
	vsubps (%rsi), %ymm8, %ymm8;		  \
	vaddps %ymm0, %ymm8, %ymm8;		  \
	vmulps KG, %ymm8, %ymm8;		  \
	vaddps %ymm3, %ymm8, %ymm8

#define LOADW \
	vmovaps KW1, %ymm10; \
	vmovaps KW2, %ymm11; \
	vmovaps KW3, %ymm12; \
	vmovaps KW4, %ymm13; \
	vmovaps KW5, %ymm14; \
	vmovaps KW6, %ymm15

#define SETIDX(NR, ZR) \
	lea 0(,ZR,4), %r12;			  \
	lea (%r12,%r12,2), %r14;		  \
	lea (%r12,%r12,4), %rbp;		  \
	mov %r12, %r13; neg %r13; \
	mov %r14, %r15; neg %r15; \
	mov %rbp, %rcx; neg %rcx; \
	lea 0(,NR,4), %r8;			  \
	lea (%r8,%r8,2), %r10; \
	lea (%r8,%r8,4), %rax; \
	mov %r8, %r9; neg %r9; \
	mov %r10, %r11; neg %r11; \
	mov %rax, %rbx; neg %rbx

#define PUSHALL push %rbx; push %rbp; push %r12; push %r13; push %r14; push %r15
#define POPALL pop %r15; pop %r14; pop %r13; pop %r12; pop %rbp; pop %rbx

	.text

	.globl pulse_rhs8_avx2
	.hidden pulse_rhs8_avx2
	.type pulse_rhs8_avx2, @function
	.p2align 6
pulse_rhs8_avx2:
	PUSHALL
	sub $8, %rsp
	shl $5, %rcx
	add %rdx, %rcx
	mov %rcx, (%rsp)
	SETIDX(%r8, %r9)
	LOADW
	.p2align 6
1:
	RHS8
	vmovups %ymm8, (%rdx)
	add $32, %rdi
	add $32, %rsi
	add $32, %rdx
	cmp (%rsp), %rdx
	jb 1b
	vzeroupper
	add $8, %rsp
	POPALL
	ret
	.size pulse_rhs8_avx2, .-pulse_rhs8_avx2

#define TR4(A0, A1, A2, A3, B0, B1, B2, B3, T0, T1, T2, T3) \
	vunpcklps A1, A0, T0;			  \
	vunpckhps A1, A0, T1;			  \
	vunpcklps A3, A2, T2; \
	vunpckhps A3, A2, T3; \
	vshufps $0x44, T2, T0, B0;		  \
	vshufps $0xEE, T2, T0, B1; \
	vshufps $0x44, T3, T1, B2; \
	vshufps $0xEE, T3, T1, B3

#define CSTEP(S, U1, U2, U3, U4, U5, U6, V, T1, T2) \
	vmulps KE6, U6, T1; \
	vmulps KE5, U5, T2; \
	vaddps T2, T1, T1;			  \
	vmulps KE4, U4, T2; \
	vaddps T2, T1, T1;			  \
	vmulps KE3, U3, T2; \
	vaddps T2, T1, T1;			  \
	vmulps KE2, U2, T2; \
	vaddps T2, T1, T1;			  \
	vaddps T1, S, T1;			  \
	vmulps KE1, U1, T2; \
	vaddps T2, T1, V

	.globl pulse_chain4_avx2
	.hidden pulse_chain4_avx2
	.type pulse_chain4_avx2, @function
	.p2align 6
pulse_chain4_avx2:
	push %r12; push %r13; push %r14; push %r15
	mov 0(%rdi), %r8
	mov 8(%rdi), %r9
	mov 16(%rdi), %r10
	mov 24(%rdi), %r11
	mov 0(%rsi), %r12
	mov 8(%rsi), %r13
	mov 16(%rsi), %r14
	mov 24(%rsi), %r15
	vmovups 0(%rcx), %xmm10
	vmovups 16(%rcx), %xmm11
	vmovups 32(%rcx), %xmm12
	vmovups 48(%rcx), %xmm13
	vmovups 64(%rcx), %xmm14
	vmovups 80(%rcx), %xmm15
	xor %eax, %eax
	shl $4, %rdx
	.p2align 6
2:
	vmovups (%r8,%rax), %xmm0
	vmovups (%r9,%rax), %xmm1
	vmovups (%r10,%rax), %xmm2
	vmovups (%r11,%rax), %xmm3
	TR4(%xmm0, %xmm1, %xmm2, %xmm3, %xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
	CSTEP(%xmm0, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14, %xmm15, %xmm4, %xmm8, %xmm9)
	CSTEP(%xmm1, %xmm4, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14, %xmm5, %xmm8, %xmm9)
	CSTEP(%xmm2, %xmm5, %xmm4, %xmm10, %xmm11, %xmm12, %xmm13, %xmm6, %xmm8, %xmm9)
	CSTEP(%xmm3, %xmm6, %xmm5, %xmm4, %xmm10, %xmm11, %xmm12, %xmm7, %xmm8, %xmm9)
	vmovaps %xmm11, %xmm15
	vmovaps %xmm10, %xmm14
	vmovaps %xmm4, %xmm13
	vmovaps %xmm5, %xmm12
	vmovaps %xmm6, %xmm11
	vmovaps %xmm7, %xmm10
	TR4(%xmm4, %xmm5, %xmm6, %xmm7, %xmm2, %xmm3, %xmm0, %xmm1, %xmm8, %xmm9, %xmm0, %xmm1)
	vmovups %xmm2, (%r12,%rax)
	vmovups %xmm3, (%r13,%rax)
	vmovups %xmm0, (%r14,%rax)
	vmovups %xmm1, (%r15,%rax)
	add $16, %rax
	cmp %rdx, %rax
	jb 2b
	vmovups %xmm10, 0(%rcx)
	vmovups %xmm11, 16(%rcx)
	vmovups %xmm12, 32(%rcx)
	vmovups %xmm13, 48(%rcx)
	vmovups %xmm14, 64(%rcx)
	vmovups %xmm15, 80(%rcx)
	pop %r15; pop %r14; pop %r13; pop %r12
	ret
	.size pulse_chain4_avx2, .-pulse_chain4_avx2

#define F_RB 0
#define F_U 128
#define F_QL 224
#define F_XL 256
#define F_VL 288
#define F_NB 320
#define F_OLDSP 328
#define F_ARGP 336
#define F_SIZE 352

#define FZ_RHS(L) \
	cmpq $0, F_VL+8*L(%rsp); \
	jle 7f; \
	mov F_QL+8*L(%rsp), %rdi; \
	mov F_XL+8*L(%rsp), %rsi; \
	RHS8; \
	vmovaps %ymm8, F_RB+32*L(%rsp); \
	jmp 6f; \
7:	vxorps %ymm8, %ymm8, %ymm8; \
	vmovaps %ymm8, F_RB+32*L(%rsp); \
6:	addq $32, F_QL+8*L(%rsp)

#define FZ_ST(L, H, XMM) \
	mov F_VL+8*L(%rsp), %rdx; \
	mov F_XL+8*L(%rsp), %rdi; \
	cmp $(4*H+4), %rdx; \
	jl 5f; \
	vmovups XMM, 16*H(%rdi); \
	jmp 4f; \
5:	sub $(4*H), %rdx; \
	jle 4f; \
	shl $4, %rdx; \
	lea pulse_mask4(%rip), %rsi; \
	vmovdqu (%rsi,%rdx), %xmm9; \
	vmaskmovps XMM, %xmm9, 16*H(%rdi); \
4:

	.section .rodata
	.p2align 6
pulse_mask4:
	.long 0, 0, 0, 0
	.long -1, 0, 0, 0
	.long -1, -1, 0, 0
	.long -1, -1, -1, 0
	.long -1, -1, -1, -1
	.text

	.globl pulse_fused4_avx2
	.hidden pulse_fused4_avx2
	.type pulse_fused4_avx2, @function
	.p2align 6
pulse_fused4_avx2:
	PUSHALL
	mov %rsp, %rax
	and $-32, %rsp
	sub $F_SIZE, %rsp
	mov %rax, F_OLDSP(%rsp)
	mov %rdi, F_ARGP(%rsp)
	mov 0(%rdi), %rax; mov %rax, F_QL+0(%rsp)
	mov 8(%rdi), %rax; mov %rax, F_QL+8(%rsp)
	mov 16(%rdi), %rax; mov %rax, F_QL+16(%rsp)
	mov 24(%rdi), %rax; mov %rax, F_QL+24(%rsp)
	mov 32(%rdi), %rax; mov %rax, F_XL+0(%rsp)
	mov 40(%rdi), %rax; mov %rax, F_XL+8(%rsp)
	mov 48(%rdi), %rax; mov %rax, F_XL+16(%rsp)
	mov 56(%rdi), %rax; mov %rax, F_XL+24(%rsp)
	mov 64(%rdi), %rax; mov %rax, F_VL+0(%rsp)
	mov 72(%rdi), %rax; mov %rax, F_VL+8(%rsp)
	mov 80(%rdi), %rax; mov %rax, F_VL+16(%rsp)
	mov 88(%rdi), %rax; mov %rax, F_VL+24(%rsp)
	mov 96(%rdi), %rax; mov %rax, F_NB(%rsp)
	vmovaps 128(%rdi), %xmm0; vmovaps %xmm0, F_U+0(%rsp)
	vmovaps 144(%rdi), %xmm0; vmovaps %xmm0, F_U+16(%rsp)
	vmovaps 160(%rdi), %xmm0; vmovaps %xmm0, F_U+32(%rsp)
	vmovaps 176(%rdi), %xmm0; vmovaps %xmm0, F_U+48(%rsp)
	vmovaps 192(%rdi), %xmm0; vmovaps %xmm0, F_U+64(%rsp)
	vmovaps 208(%rdi), %xmm0; vmovaps %xmm0, F_U+80(%rsp)
	mov 104(%rdi), %r8
	mov 112(%rdi), %r9
	SETIDX(%r8, %r9)
	.p2align 6
3:
	LOADW
	FZ_RHS(0)
	FZ_RHS(1)
	FZ_RHS(2)
	FZ_RHS(3)
	vmovaps F_U+0(%rsp), %xmm10
	vmovaps F_U+16(%rsp), %xmm11
	vmovaps F_U+32(%rsp), %xmm12
	vmovaps F_U+48(%rsp), %xmm13
	vmovaps F_U+64(%rsp), %xmm14
	vmovaps F_U+80(%rsp), %xmm15
	vmovaps F_RB+0(%rsp), %xmm0
	vmovaps F_RB+32(%rsp), %xmm1
	vmovaps F_RB+64(%rsp), %xmm2
	vmovaps F_RB+96(%rsp), %xmm3
	TR4(%xmm0, %xmm1, %xmm2, %xmm3, %xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
	CSTEP(%xmm0, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14, %xmm15, %xmm15, %xmm8, %xmm9)
	CSTEP(%xmm1, %xmm15, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14, %xmm14, %xmm8, %xmm9)
	CSTEP(%xmm2, %xmm14, %xmm15, %xmm10, %xmm11, %xmm12, %xmm13, %xmm13, %xmm8, %xmm9)
	CSTEP(%xmm3, %xmm13, %xmm14, %xmm15, %xmm10, %xmm11, %xmm12, %xmm12, %xmm8, %xmm9)
	TR4(%xmm15, %xmm14, %xmm13, %xmm12, %xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
	FZ_ST(0, 0, %xmm0)
	FZ_ST(1, 0, %xmm1)
	FZ_ST(2, 0, %xmm2)
	FZ_ST(3, 0, %xmm3)
	vmovaps F_RB+16(%rsp), %xmm0
	vmovaps F_RB+48(%rsp), %xmm1
	vmovaps F_RB+80(%rsp), %xmm2
	vmovaps F_RB+112(%rsp), %xmm3
	TR4(%xmm0, %xmm1, %xmm2, %xmm3, %xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
	CSTEP(%xmm0, %xmm12, %xmm13, %xmm14, %xmm15, %xmm10, %xmm11, %xmm11, %xmm8, %xmm9)
	CSTEP(%xmm1, %xmm11, %xmm12, %xmm13, %xmm14, %xmm15, %xmm10, %xmm10, %xmm8, %xmm9)
	CSTEP(%xmm2, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14, %xmm15, %xmm15, %xmm8, %xmm9)
	CSTEP(%xmm3, %xmm15, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14, %xmm14, %xmm8, %xmm9)
	vmovaps %xmm14, F_U+0(%rsp)
	vmovaps %xmm15, F_U+16(%rsp)
	vmovaps %xmm10, F_U+32(%rsp)
	vmovaps %xmm11, F_U+48(%rsp)
	vmovaps %xmm12, F_U+64(%rsp)
	vmovaps %xmm13, F_U+80(%rsp)
	TR4(%xmm11, %xmm10, %xmm15, %xmm14, %xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
	FZ_ST(0, 1, %xmm0)
	FZ_ST(1, 1, %xmm1)
	FZ_ST(2, 1, %xmm2)
	FZ_ST(3, 1, %xmm3)
	addq $32, F_XL+0(%rsp)
	addq $32, F_XL+8(%rsp)
	addq $32, F_XL+16(%rsp)
	addq $32, F_XL+24(%rsp)
	subq $8, F_VL+0(%rsp)
	subq $8, F_VL+8(%rsp)
	subq $8, F_VL+16(%rsp)
	subq $8, F_VL+24(%rsp)
	decq F_NB(%rsp)
	jnz 3b
	mov F_ARGP(%rsp), %rdi
	vmovaps F_U+0(%rsp), %xmm0; vmovaps %xmm0, 128(%rdi)
	vmovaps F_U+16(%rsp), %xmm0; vmovaps %xmm0, 144(%rdi)
	vmovaps F_U+32(%rsp), %xmm0; vmovaps %xmm0, 160(%rdi)
	vmovaps F_U+48(%rsp), %xmm0; vmovaps %xmm0, 176(%rdi)
	vmovaps F_U+64(%rsp), %xmm0; vmovaps %xmm0, 192(%rdi)
	vmovaps F_U+80(%rsp), %xmm0; vmovaps %xmm0, 208(%rdi)
	vzeroupper
	mov F_OLDSP(%rsp), %rsp
	POPALL
	ret
	.size pulse_fused4_avx2, .-pulse_fused4_avx2

	.section .note.GNU-stack,"",@progbits
