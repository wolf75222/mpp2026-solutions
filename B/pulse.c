#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <sched.h>
#include <omp.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>

#define BORDER 6
#define NOISE_AMP 1.0f
#define LANES 4
#define BLOCK_LINES 32
#define PRINT_MIN 0.0005
#define PRINT_MAX 500
#ifndef DEF_LANES_AVX2
#define DEF_LANES_AVX2 4
#endif
#ifndef DEF_LANES_AVX512
#define DEF_LANES_AVX512 4
#endif
#ifndef DEFAULT_KER
#define DEFAULT_KER 0
#endif

typedef struct
{
    float W[7];
    float e[7];
    float k, g, r;
    long pitch, slab;
    int n;
    int lo[3], hi[3];
    int src[3];
} stGS;

static void init_constants(stGS* c, float dx, float dt, float vp)
{
    int a;
    float fat = ((dt * dt) / (dx * dx));

    c->W[0] = (float)(-5369.0 / 1800.0);
    c->W[1] = (float)(12.0 / 7.0);
    c->W[2] = (float)(-15.0 / 56.0);
    c->W[3] = (float)(10.0 / 189.0);
    c->W[4] = (float)(-1.0 / 112.0);
    c->W[5] = (float)(2.0 / 1925.0);
    c->W[6] = (float)(-1.0 / 16632.0);

    c->r = fat * vp * vp;
    c->k = 0.5f * c->r;
    c->g = 1.0f / (1.0f - 3.0f * c->k * c->W[0]);
    c->e[0] = 0.0f;
    for (a = 1; a <= 6; a++)
        c->e[a] = c->g * c->k * c->W[a];
}

static inline __attribute__((always_inline)) void gs_rhs(const stGS* c, const float* restrict Q, const float* restrict X,
    long p0, int i0, int i1, float* restrict S)
{
    const long N = c->pitch, N2 = c->slab;
    const float W0 = c->W[0], W1 = c->W[1], W2 = c->W[2], W3 = c->W[3], W4 = c->W[4], W5 = c->W[5], W6 = c->W[6];
    const float e1 = c->e[1], e2 = c->e[2], e3 = c->e[3], e4 = c->e[4], e5 = c->e[5], e6 = c->e[6];
    const float k = c->k, g = c->g;
    const float* q = Q + p0;
    const float* x = X + p0;
    int i;

    for (i = i0; i < i1; i++) {
        float lx = W6 * (q[i - 6] + q[i + 6]) + W5 * (q[i - 5] + q[i + 5]) + W4 * (q[i - 4] + q[i + 4]) + W3 * (q[i - 3] + q[i + 3]) + W2 * (q[i - 2] + q[i + 2]) + W1 * (q[i - 1] + q[i + 1]) + W0 * q[i];
        float ly = W6 * (q[i - 6 * N] + q[i + 6 * N]) + W5 * (q[i - 5 * N] + q[i + 5 * N]) + W4 * (q[i - 4 * N] + q[i + 4 * N]) + W3 * (q[i - 3 * N] + q[i + 3 * N]) + W2 * (q[i - 2 * N] + q[i + 2 * N]) + W1 * (q[i - N] + q[i + N]) + W0 * q[i];
        float lz = W6 * (q[i - 6 * N2] + q[i + 6 * N2]) + W5 * (q[i - 5 * N2] + q[i + 5 * N2]) + W4 * (q[i - 4 * N2] + q[i + 4 * N2]) + W3 * (q[i - 3 * N2] + q[i + 3 * N2]) + W2 * (q[i - 2 * N2] + q[i + 2 * N2]) + W1 * (q[i - N2] + q[i + N2]) + W0 * q[i];
        float R = g * ((2.0f * q[i] - x[i]) + k * ((lx + ly) + lz));
        float o = e6 * (((q[i + 6] + x[i - 6 * N]) + q[i + 6 * N]) + (x[i - 6 * N2] + q[i + 6 * N2]))
            + e5 * (((q[i + 5] + x[i - 5 * N]) + q[i + 5 * N]) + (x[i - 5 * N2] + q[i + 5 * N2]))
            + e4 * (((q[i + 4] + x[i - 4 * N]) + q[i + 4 * N]) + (x[i - 4 * N2] + q[i + 4 * N2]))
            + e3 * (((q[i + 3] + x[i - 3 * N]) + q[i + 3 * N]) + (x[i - 3 * N2] + q[i + 3 * N2]))
            + e2 * (((q[i + 2] + x[i - 2 * N]) + q[i + 2 * N]) + (x[i - 2 * N2] + q[i + 2 * N2]))
            + e1 * (((q[i + 1] + x[i - N]) + q[i + N]) + (x[i - N2] + q[i + N2]));
        S[i] = R + o;
    }
}

#if defined(__x86_64__)
typedef float v8f __attribute__((vector_size(32)));
#define ldu8(p) __builtin_ia32_loadups256(p)
#define ldr8(p) ((v8f)__builtin_ia32_lddqu256((const char*)(p)))
#define stu8(p, v) __builtin_ia32_storeups256((p), (v))
static inline __attribute__((always_inline, target("avx2"))) void gs_rhs_v8(const stGS* c,
    const float* restrict Q, const float* restrict X, long p0, int i0, int i1, float* restrict S)
{
    const long N = c->pitch, N2 = c->slab;
    const v8f z = { 0, 0, 0, 0, 0, 0, 0, 0 };
    const v8f W0 = z + c->W[0], W1 = z + c->W[1], W2 = z + c->W[2], W3 = z + c->W[3], W4 = z + c->W[4], W5 = z + c->W[5], W6 = z + c->W[6];
    const v8f e1 = z + c->e[1], e2 = z + c->e[2], e3 = z + c->e[3], e4 = z + c->e[4], e5 = z + c->e[5], e6 = z + c->e[6];
    const v8f k = z + c->k, g = z + c->g, two = z + 2.0f;
    const float* q = Q + p0;
    const float* x = X + p0;
    int i;

    for (i = i0; i + 8 <= i1; i += 8) {
        const float* qi = q + i;
        const float* xi = x + i;
        const v8f qc = ldr8(qi);
        v8f qa, qn, qz, lx, ly, lz, o;
#define RHS8_TERM(A, WA, EA, FIRST)                                                            \
        qa = ldr8(qi + (A));                                                                   \
        qn = ldr8(qi + (A) * N);                                                               \
        qz = ldr8(qi + (A) * N2);                                                              \
        if (FIRST) {                                                                           \
            lx = WA * (ldu8(qi - (A)) + qa);                                                   \
            ly = WA * (ldu8(qi - (A) * N) + qn);                                               \
            lz = WA * (ldu8(qi - (A) * N2) + qz);                                              \
            o = EA * (((qa + ldu8(xi - (A) * N)) + qn) + (ldu8(xi - (A) * N2) + qz));          \
        } else {                                                                               \
            lx = lx + WA * (ldu8(qi - (A)) + qa);                                              \
            ly = ly + WA * (ldu8(qi - (A) * N) + qn);                                          \
            lz = lz + WA * (ldu8(qi - (A) * N2) + qz);                                         \
            o = o + EA * (((qa + ldu8(xi - (A) * N)) + qn) + (ldu8(xi - (A) * N2) + qz));      \
        }
        RHS8_TERM(6, W6, e6, 1)
        RHS8_TERM(5, W5, e5, 0)
        RHS8_TERM(4, W4, e4, 0)
        RHS8_TERM(3, W3, e3, 0)
        RHS8_TERM(2, W2, e2, 0)
        RHS8_TERM(1, W1, e1, 0)
#undef RHS8_TERM
        lx = lx + W0 * qc;
        ly = ly + W0 * qc;
        lz = lz + W0 * qc;
        {
            v8f R = g * ((two * qc - ldu8(xi)) + k * ((lx + ly) + lz));
            stu8(S + i, R + o);
        }
    }
    if (i < i1)
        gs_rhs(c, Q, X, p0, i, i1, S);
}
#ifndef RHS_AVX2
#define RHS_AVX2 gs_rhs_v8
#endif
#else
#define RHS_AVX2 gs_rhs
#endif

static void gs_chain(const stGS* c, const float* restrict S, float* restrict out,
    int i0, int i1, float* u, int srci, float sv)
{
    const float e1 = c->e[1], e2 = c->e[2], e3 = c->e[3], e4 = c->e[4], e5 = c->e[5], e6 = c->e[6];
    float u1 = u[0], u2 = u[1], u3 = u[2], u4 = u[3], u5 = u[4], u6 = u[5];
    int i;

    for (i = i0; i < i1; i++) {
        float v = (S[i] + ((((e6 * u6 + e5 * u5) + e4 * u4) + e3 * u3) + e2 * u2)) + e1 * u1;
        if (i == srci)
            v = v + sv;
        out[i] = v;
        u6 = u5;
        u5 = u4;
        u4 = u3;
        u3 = u2;
        u2 = u1;
        u1 = v;
    }
    u[0] = u1;
    u[1] = u2;
    u[2] = u3;
    u[3] = u4;
    u[4] = u5;
    u[5] = u6;
}

static int g_pf = 2;
static int g_pfw = 1;
#define PF_ROWS(LOC)                                                              \
    for (i = b; i < e + 16; i += 16) {                                           \
        const int ii = i < e ? i : e - 1;                                        \
        for (a = 1; a <= 6; a++) {                                               \
            __builtin_prefetch(q + ii - a * N2, 0, LOC);                         \
            __builtin_prefetch(q + ii + a * N2, 0, LOC);                         \
            __builtin_prefetch(x + ii - a * N2, 0, LOC);                         \
            if (a == 1 && g_pfw)                                                 \
                __builtin_prefetch(x + ii, 1, LOC);                              \
            if (y) {                                                             \
                __builtin_prefetch(q + ii - a * N, 0, LOC);                      \
                __builtin_prefetch(q + ii + a * N, 0, LOC);                      \
            }                                                                    \
        }                                                                        \
    }
static inline __attribute__((always_inline)) void pf_stage(const stGS* c, const float* Q, const float* X, int j0, int nl, int nc, int C,
    int m, int k, int s)
{
    const int n0 = s - nc + 1 > 0 ? s - nc + 1 : 0, n1 = s < nl - 1 ? s : nl - 1;
    const long N = c->pitch, N2 = c->slab;
    const int y = g_pf == 2 || g_pf == 4;
    int n, a, i;
    for (n = n0; n <= n1; n++) {
        const int b = (s - n) * C, e = b + C < m ? b + C : m;
        const long p0 = k * N2 + (j0 + n) * N + c->lo[0];
        const float* q = Q + p0;
        const float* x = X + p0;
        if (b >= e)
            continue;
        if (g_pf <= 2)
            PF_ROWS(2)
        else
            PF_ROWS(3)
    }
}

typedef float v4c __attribute__((vector_size(16)));
typedef int v4ci __attribute__((vector_size(16)));
static inline v4c ld4c(const float* p)
{
    v4c v;
    memcpy(&v, p, 16);
    return v;
}
static inline void st4c(float* p, v4c v) { memcpy(p, &v, 16); }
#define TR4C(a0, a1, a2, a3, b0, b1, b2, b3)                          \
    do {                                                              \
        v4c t0_ = __builtin_shuffle(a0, a1, (v4ci) { 0, 4, 1, 5 });  \
        v4c t1_ = __builtin_shuffle(a0, a1, (v4ci) { 2, 6, 3, 7 });  \
        v4c t2_ = __builtin_shuffle(a2, a3, (v4ci) { 0, 4, 1, 5 });  \
        v4c t3_ = __builtin_shuffle(a2, a3, (v4ci) { 2, 6, 3, 7 });  \
        b0 = __builtin_shuffle(t0_, t2_, (v4ci) { 0, 1, 4, 5 });     \
        b1 = __builtin_shuffle(t0_, t2_, (v4ci) { 2, 3, 6, 7 });     \
        b2 = __builtin_shuffle(t1_, t3_, (v4ci) { 0, 1, 4, 5 });     \
        b3 = __builtin_shuffle(t1_, t3_, (v4ci) { 2, 3, 6, 7 });     \
    } while (0)
static int g_tch = 1;
static int g_maskc = 0;
static inline __attribute__((always_inline)) void chain4_tr(const stGS* c, const float* const* S, float* const* out,
    int L, int srcI, int srcLane, float sv, float* carry, const int* busy)
{
    const v4c zv = { 0, 0, 0, 0 };
    const v4c E1 = zv + c->e[1], E2 = zv + c->e[2], E3 = zv + c->e[3];
    const v4c E4 = zv + c->e[4], E5 = zv + c->e[5], E6 = zv + c->e[6];
    v4c U1, U2, U3, U4, U5, U6;
    v4c SRC = zv - 0.0f;
    int l, i;
    if (srcLane >= 0)
        SRC[srcLane] = sv;
    for (l = 0; l < 4; l++) {
        U1[l] = busy[l] ? carry[l * 6 + 0] : 0.0f;
        U2[l] = busy[l] ? carry[l * 6 + 1] : 0.0f;
        U3[l] = busy[l] ? carry[l * 6 + 2] : 0.0f;
        U4[l] = busy[l] ? carry[l * 6 + 3] : 0.0f;
        U5[l] = busy[l] ? carry[l * 6 + 4] : 0.0f;
        U6[l] = busy[l] ? carry[l * 6 + 5] : 0.0f;
    }
    for (i = 0; i + 4 <= L; i += 4) {
        v4c a0 = ld4c(S[0] + i), a1 = ld4c(S[1] + i), a2 = ld4c(S[2] + i), a3 = ld4c(S[3] + i);
        v4c b0, b1, b2, b3, r0, r1, r2, r3;
        TR4C(a0, a1, a2, a3, b0, b1, b2, b3);
        r0 = (b0 + ((((E6 * U6 + E5 * U5) + E4 * U4) + E3 * U3) + E2 * U2)) + E1 * U1;
        if (__builtin_expect(srcI == i, 0))
            r0 = r0 + SRC;
        r1 = (b1 + ((((E6 * U5 + E5 * U4) + E4 * U3) + E3 * U2) + E2 * U1)) + E1 * r0;
        if (__builtin_expect(srcI == i + 1, 0))
            r1 = r1 + SRC;
        r2 = (b2 + ((((E6 * U4 + E5 * U3) + E4 * U2) + E3 * U1) + E2 * r0)) + E1 * r1;
        if (__builtin_expect(srcI == i + 2, 0))
            r2 = r2 + SRC;
        r3 = (b3 + ((((E6 * U3 + E5 * U2) + E4 * U1) + E3 * r0) + E2 * r1)) + E1 * r2;
        if (__builtin_expect(srcI == i + 3, 0))
            r3 = r3 + SRC;
        U6 = U2;
        U5 = U1;
        U4 = r0;
        U3 = r1;
        U2 = r2;
        U1 = r3;
        TR4C(r0, r1, r2, r3, a0, a1, a2, a3);
        st4c(out[0] + i, a0);
        st4c(out[1] + i, a1);
        st4c(out[2] + i, a2);
        st4c(out[3] + i, a3);
    }
    for (; i < L; i++) {
        v4c Sv, v;
        for (l = 0; l < 4; l++)
            Sv[l] = S[l][i];
        v = (Sv + ((((E6 * U6 + E5 * U5) + E4 * U4) + E3 * U3) + E2 * U2)) + E1 * U1;
        if (i == srcI)
            v = v + SRC;
        for (l = 0; l < 4; l++)
            out[l][i] = v[l];
        U6 = U5;
        U5 = U4;
        U4 = U3;
        U3 = U2;
        U2 = U1;
        U1 = v;
    }
    for (l = 0; l < 4; l++) {
        if (!busy[l])
            continue;
        carry[l * 6 + 0] = U1[l];
        carry[l * 6 + 1] = U2[l];
        carry[l * 6 + 2] = U3[l];
        carry[l * 6 + 3] = U4[l];
        carry[l * 6 + 4] = U5[l];
        carry[l * 6 + 5] = U6[l];
    }
}

static inline __attribute__((always_inline)) void chain4_trm(const stGS* c, const float* const* S0, float* const* out0,
    const int* len, int srcI, int srcLane, float sv, float* carry, const int* busy, const float* zero, float* sink)
{
    const v4c zv = { 0, 0, 0, 0 };
    const v4c E1 = zv + c->e[1], E2 = zv + c->e[2], E3 = zv + c->e[3];
    const v4c E4 = zv + c->e[4], E5 = zv + c->e[5], E6 = zv + c->e[6];
    v4c U1, U2, U3, U4, U5, U6;
    v4c SRC = zv - 0.0f;
    int l, i = 0, act[4];
    const float* S[4];
    float* out[4];
    if (srcLane >= 0)
        SRC[srcLane] = sv;
    for (l = 0; l < 4; l++) {
        S[l] = S0[l];
        out[l] = out0[l];
        act[l] = busy[l];
        U1[l] = busy[l] ? carry[l * 6 + 0] : 0.0f;
        U2[l] = busy[l] ? carry[l * 6 + 1] : 0.0f;
        U3[l] = busy[l] ? carry[l * 6 + 2] : 0.0f;
        U4[l] = busy[l] ? carry[l * 6 + 3] : 0.0f;
        U5[l] = busy[l] ? carry[l * 6 + 4] : 0.0f;
        U6[l] = busy[l] ? carry[l * 6 + 5] : 0.0f;
    }
    for (;;) {
    int L = 1 << 30;
    for (l = 0; l < 4; l++)
        if (act[l] && len[l] < L)
            L = len[l];
    if (L == 1 << 30)
        break;
    for (; i + 4 <= L; i += 4) {
        v4c a0 = ld4c(S[0] + i), a1 = ld4c(S[1] + i), a2 = ld4c(S[2] + i), a3 = ld4c(S[3] + i);
        v4c b0, b1, b2, b3, r0, r1, r2, r3;
        TR4C(a0, a1, a2, a3, b0, b1, b2, b3);
        r0 = (b0 + ((((E6 * U6 + E5 * U5) + E4 * U4) + E3 * U3) + E2 * U2)) + E1 * U1;
        if (__builtin_expect(srcI == i, 0))
            r0 = r0 + SRC;
        r1 = (b1 + ((((E6 * U5 + E5 * U4) + E4 * U3) + E3 * U2) + E2 * U1)) + E1 * r0;
        if (__builtin_expect(srcI == i + 1, 0))
            r1 = r1 + SRC;
        r2 = (b2 + ((((E6 * U4 + E5 * U3) + E4 * U2) + E3 * U1) + E2 * r0)) + E1 * r1;
        if (__builtin_expect(srcI == i + 2, 0))
            r2 = r2 + SRC;
        r3 = (b3 + ((((E6 * U3 + E5 * U2) + E4 * U1) + E3 * r0) + E2 * r1)) + E1 * r2;
        if (__builtin_expect(srcI == i + 3, 0))
            r3 = r3 + SRC;
        U6 = U2;
        U5 = U1;
        U4 = r0;
        U3 = r1;
        U2 = r2;
        U1 = r3;
        TR4C(r0, r1, r2, r3, a0, a1, a2, a3);
        st4c(out[0] + i, a0);
        st4c(out[1] + i, a1);
        st4c(out[2] + i, a2);
        st4c(out[3] + i, a3);
    }
    for (; i < L; i++) {
        v4c Sv, v;
        for (l = 0; l < 4; l++)
            Sv[l] = S[l][i];
        v = (Sv + ((((E6 * U6 + E5 * U5) + E4 * U4) + E3 * U3) + E2 * U2)) + E1 * U1;
        if (i == srcI)
            v = v + SRC;
        for (l = 0; l < 4; l++)
            out[l][i] = v[l];
        U6 = U5;
        U5 = U4;
        U4 = U3;
        U3 = U2;
        U2 = U1;
        U1 = v;
    }
    for (l = 0; l < 4; l++) {
        if (!act[l] || len[l] != L)
            continue;
        carry[l * 6 + 0] = U1[l];
        carry[l * 6 + 1] = U2[l];
        carry[l * 6 + 2] = U3[l];
        carry[l * 6 + 3] = U4[l];
        carry[l * 6 + 4] = U5[l];
        carry[l * 6 + 5] = U6[l];
        act[l] = 0;
        S[l] = zero;
        out[l] = sink;
    }
    }
}

#define DEF_GS_PLANE(NAME, LN, ATTR, RHSFN, CH4FN)                                                           \
ATTR static void NAME(const stGS* c, const float* Q, float* X, int j0, int j1, int k,          \
    float* ws, long wstride, float sv)                                                         \
{                                                                                              \
    typedef float vl_t __attribute__((vector_size(LN * sizeof(float))));                       \
    const int m = c->hi[0] - c->lo[0];                                                         \
    const int C = ((m + LN - 1) / LN + 3) & ~3;                                                \
    const int nc = (m + C - 1) / C;                                                            \
    const int nl = j1 - j0;                                                                    \
    const int srcline = k == c->src[2] ? c->src[1] - j0 : -1;                                  \
    const int srcx = c->src[0] - c->lo[0];                                                     \
    float* zero = ws + LN * wstride;                                                           \
    float* sink = zero + C;                                                                    \
    float carry[LN][6];                                                                        \
    const vl_t zv = { 0 };                                                                     \
    const vl_t E1 = zv + c->e[1], E2 = zv + c->e[2], E3 = zv + c->e[3];                        \
    const vl_t E4 = zv + c->e[4], E5 = zv + c->e[5], E6 = zv + c->e[6];                        \
    int s, l, n, i, a;                                                                         \
                                                                                               \
    memset(zero, 0, C * sizeof(float));                                                        \
                                                                                               \
    for (s = 0; s < nl + nc - 1; s++) {                                                        \
        const float* S[LN];                                                                    \
        float* out[LN];                                                                        \
        int len[LN], busy[LN];                                                                 \
        int n0 = s - nc + 1 > 0 ? s - nc + 1 : 0, n1 = s < nl - 1 ? s : nl - 1;                \
        int L = C, srcI = -1, srcLane = -1;                                                    \
                                                                                               \
        for (l = 0; l < LN; l++) {                                                             \
            S[l] = zero;                                                                       \
            out[l] = sink;                                                                     \
            len[l] = C;                                                                        \
            busy[l] = 0;                                                                       \
        }                                                                                      \
        for (n = n0; n <= n1; n++) {                                                           \
            int b = (s - n) * C, e = b + C < m ? b + C : m;                                    \
            long p0 = k * c->slab + (j0 + n) * c->pitch + c->lo[0];                            \
            float* Sl;                                                                         \
                                                                                               \
            l = n % LN;                                                                        \
            Sl = ws + l * wstride;                                                             \
            if (b == 0)                                                                        \
                for (a = 0; a < 6; a++)                                                        \
                    carry[l][a] = X[p0 - 1 - a];                                               \
            RHSFN(c, Q, X, p0, b, e, Sl);                                                        \
            S[l] = Sl + b;                                                                     \
            out[l] = X + p0 + b;                                                               \
            len[l] = e - b;                                                                    \
            busy[l] = 1;                                                                       \
            if (len[l] < L)                                                                    \
                L = len[l];                                                                    \
            if (n == srcline && srcx >= b && srcx < e) {                                       \
                srcI = srcx - b;                                                               \
                srcLane = l;                                                                   \
            }                                                                                  \
        }                                                                                      \
                                                                                               \
        if (g_pf)                                                                              \
            pf_stage(c, Q, X, j0, nl, nc, C, m, k, s + 1);                                     \
        if (LN == 4 && g_tch) {                                                                \
            if (g_maskc)                                                                       \
                chain4_trm(c, (const float* const*)S, out, len, srcI, srcLane, sv, &carry[0][0], busy, zero, sink);\
            else                                                                               \
                CH4FN(c, (const float* const*)S, out, L, srcI, srcLane, sv, &carry[0][0], busy);\
        } else {                                                                               \
            vl_t U1, U2, U3, U4, U5, U6;                                                       \
            for (l = 0; l < LN; l++) {                                                         \
                U1[l] = busy[l] ? carry[l][0] : 0.0f;                                          \
                U2[l] = busy[l] ? carry[l][1] : 0.0f;                                          \
                U3[l] = busy[l] ? carry[l][2] : 0.0f;                                          \
                U4[l] = busy[l] ? carry[l][3] : 0.0f;                                          \
                U5[l] = busy[l] ? carry[l][4] : 0.0f;                                          \
                U6[l] = busy[l] ? carry[l][5] : 0.0f;                                          \
            }                                                                                  \
            for (i = 0; i < L; i++) {                                                          \
                vl_t Sv, v;                                                                    \
                for (l = 0; l < LN; l++)                                                       \
                    Sv[l] = S[l][i];                                                           \
                v = (Sv + ((((E6 * U6 + E5 * U5) + E4 * U4) + E3 * U3) + E2 * U2)) + E1 * U1;  \
                if (i == srcI)                                                                 \
                    v[srcLane] = v[srcLane] + sv;                                              \
                for (l = 0; l < LN; l++)                                                       \
                    out[l][i] = v[l];                                                          \
                U6 = U5;                                                                       \
                U5 = U4;                                                                       \
                U4 = U3;                                                                       \
                U3 = U2;                                                                       \
                U2 = U1;                                                                       \
                U1 = v;                                                                        \
            }                                                                                  \
            for (l = 0; l < LN; l++) {                                                         \
                if (!busy[l])                                                                  \
                    continue;                                                                  \
                carry[l][0] = U1[l];                                                           \
                carry[l][1] = U2[l];                                                           \
                carry[l][2] = U3[l];                                                           \
                carry[l][3] = U4[l];                                                           \
                carry[l][4] = U5[l];                                                           \
                carry[l][5] = U6[l];                                                           \
            }                                                                                  \
        }                                                                                      \
        for (l = 0; l < LN && !(LN == 4 && g_tch && g_maskc); l++)                             \
            if (busy[l] && len[l] > L)                                                         \
                gs_chain(c, S[l], out[l], L, len[l], carry[l], l == srcLane ? srcI : -1, sv);  \
    }                                                                                          \
}

#if defined(__x86_64__) || defined(__i386__)
#ifndef AVX2_TARGET
#define AVX2_TARGET "avx2,prfchw"
#endif
#define T_AVX2 __attribute__((target(AVX2_TARGET)))
#define T_AVX512 __attribute__((target("avx512f")))
#define CPU_HAS(x) __builtin_cpu_supports(x)
#else
#define T_AVX2
#define T_AVX512
#define CPU_HAS(x) 0
#endif
DEF_GS_PLANE(gs_plane4_sse, 4, , gs_rhs, chain4_tr)
DEF_GS_PLANE(gs_plane8_sse, 8, , gs_rhs, chain4_tr)
DEF_GS_PLANE(gs_plane4_avx2, 4, T_AVX2, RHS_AVX2, chain4_tr)
DEF_GS_PLANE(gs_plane8_avx2, 8, T_AVX2, RHS_AVX2, chain4_tr)
DEF_GS_PLANE(gs_plane16_avx2, 16, T_AVX2, RHS_AVX2, chain4_tr)
DEF_GS_PLANE(gs_plane4_avx512, 4, T_AVX512, gs_rhs, chain4_tr)
DEF_GS_PLANE(gs_plane8_avx512, 8, T_AVX512, gs_rhs, chain4_tr)
DEF_GS_PLANE(gs_plane16_avx512, 16, T_AVX512, gs_rhs, chain4_tr)
#if defined(__x86_64__)
float pulse_kc[16 * 8] __attribute__((aligned(64), visibility("hidden")));
extern void pulse_rhs8_avx2(const float* q, const float* x, float* S, long n8, long N, long N2)
    __attribute__((visibility("hidden")));
extern void pulse_chain4_avx2(const float* const* S, float* const* out, long n4, float* U)
    __attribute__((visibility("hidden")));
static inline __attribute__((always_inline)) void rhs_asm(const stGS* c, const float* Q, const float* X, long p0,
    int i0, int i1, float* S)
{
    const int n8 = (i1 - i0) >> 3;
    if (n8 > 0)
        pulse_rhs8_avx2(Q + p0 + i0, X + p0 + i0, S + i0, n8, c->pitch, c->slab);
    if (i0 + 8 * n8 < i1)
        gs_rhs(c, Q, X, p0, i0 + 8 * n8, i1, S);
}
static inline __attribute__((always_inline)) void chain4_asm(const stGS* c, const float* const* S, float* const* out,
    int L, int srcI, int srcLane, float sv, float* carry, const int* busy)
{
    float Uv[24] __attribute__((aligned(16)));
    const int n4 = L >> 2;
    int l, a;
    if (srcI >= 0 || n4 == 0) {
        chain4_tr(c, S, out, L, srcI, srcLane, sv, carry, busy);
        return;
    }
    for (l = 0; l < 4; l++)
        for (a = 0; a < 6; a++)
            Uv[a * 4 + l] = busy[l] ? carry[l * 6 + a] : 0.0f;
    pulse_chain4_avx2(S, out, n4, Uv);
    if (4 * n4 < L) {
        const float* S2[4];
        float* out2[4];
        int busy2[4];
        for (l = 0; l < 4; l++) {
            S2[l] = S[l] + 4 * n4;
            out2[l] = out[l] + 4 * n4;
            busy2[l] = 1;
        }
        {
            float cr[24];
            for (l = 0; l < 4; l++)
                for (a = 0; a < 6; a++)
                    cr[l * 6 + a] = Uv[a * 4 + l];
            chain4_tr(c, S2, out2, L - 4 * n4, -1, -1, sv, cr, busy2);
            for (l = 0; l < 4; l++)
                for (a = 0; a < 6; a++)
                    Uv[a * 4 + l] = cr[l * 6 + a];
        }
    }
    for (l = 0; l < 4; l++)
        if (busy[l])
            for (a = 0; a < 6; a++)
                carry[l * 6 + a] = Uv[a * 4 + l];
}
DEF_GS_PLANE(gs_plane4_asm, 4, T_AVX2, rhs_asm, chain4_asm)

T_AVX2 static void gs_plane4_sep(const stGS* c, const float* Q, float* X, int j0, int j1, int k,
    float* ws, long wstride, float sv)
{
    const int m = c->hi[0] - c->lo[0];
    const int C = ((m + 3) / 4 + 3) & ~3;
    const int nc = (m + C - 1) / C;
    const int nl = j1 - j0;
    const int srcline = k == c->src[2] ? c->src[1] - j0 : -1;
    const int srcx = c->src[0] - c->lo[0];
    float* zero = ws + 4 * wstride;
    float* sink = zero + wstride;
    float* lsink = sink + wstride;
    float Uv[24] __attribute__((aligned(16)));
    int s, l, n, a;

    memset(zero, 0, wstride * sizeof(float));
    memset(Uv, 0, sizeof Uv);
    for (s = 0; s < nl + nc - 1; s++) {
        const int n0 = s - nc + 1 > 0 ? s - nc + 1 : 0, n1 = s < nl - 1 ? s : nl - 1;
        const float* S[4];
        float* out[4];
        float* out2[4];
        int len[4], busy[4], srcI = -1, srcLane = -1, maxlen = 0, ok = 1;
        for (l = 0; l < 4; l++) {
            S[l] = zero;
            out[l] = out2[l] = sink;
            len[l] = 0;
            busy[l] = 0;
        }
        for (n = n0; n <= n1; n++) {
            const int b = (s - n) * C, e = b + C < m ? b + C : m;
            const long p0 = k * c->slab + (j0 + n) * c->pitch + c->lo[0];
            float* Sl;
            int n8 = (e - b + 7) >> 3;
            l = n % 4;
            Sl = ws + l * wstride;
            if (b + 8 * n8 > wstride)
                n8 = (e - b) >> 3;
            if (b == 0)
                for (a = 0; a < 6; a++)
                    Uv[a * 4 + l] = X[p0 - 1 - a];
            if (n8 > 0)
                pulse_rhs8_avx2(Q + p0 + b, X + p0 + b, Sl + b, n8, c->pitch, c->slab);
            if (b + 8 * n8 < e)
                gs_rhs(c, Q, X, p0, b + 8 * n8, e, Sl);
            S[l] = Sl + b;
            out[l] = out2[l] = X + p0 + b;
            len[l] = e - b;
            busy[l] = 1;
            if (e - b > maxlen)
                maxlen = e - b;
            if (b + C > wstride)
                ok = 0;
            if (n == srcline && srcx >= b && srcx < e) {
                srcI = srcx - b;
                srcLane = l;
            }
        }
        for (l = 0; l < 4; l++)
            if (!busy[l])
                for (a = 0; a < 6; a++)
                    Uv[a * 4 + l] = 0.0f;
        if (srcI < 0 && ok && !(maxlen & 3)) {
            int sl = -1;
            for (l = 0; l < 4; l++)
                if (busy[l] && len[l] < maxlen) {
                    sl = l;
                    out2[l] = lsink;
                }
            pulse_chain4_avx2(S, out2, maxlen >> 2, Uv);
            if (sl >= 0)
                memcpy(out[sl], lsink, len[sl] * sizeof(float));
        } else {
            float carry[24];
            int L = C;
            for (l = 0; l < 4; l++) {
                if (busy[l] && len[l] < L)
                    L = len[l];
                if (!busy[l])
                    len[l] = C;
                for (a = 0; a < 6; a++)
                    carry[l * 6 + a] = Uv[a * 4 + l];
            }
            chain4_tr(c, S, out, L, srcI, srcLane, sv, carry, busy);
            for (l = 0; l < 4; l++)
                if (busy[l] && len[l] > L)
                    gs_chain(c, S[l], out[l], L, len[l], &carry[l * 6], l == srcLane ? srcI : -1, sv);
            for (l = 0; l < 4; l++)
                for (a = 0; a < 6; a++)
                    Uv[a * 4 + l] = carry[l * 6 + a];
        }
    }
}

typedef struct {
    const float* q[4];
    float* x[4];
    long nv[4];
    long nblk;
    long N, N2;
    long pad_;
    float U[24];
} fz_args;
extern void pulse_fused4_avx2(fz_args* A) __attribute__((visibility("hidden")));
T_AVX2 static void gs_plane4_fz(const stGS* c, const float* Q, float* X, int j0, int j1, int k,
    float* ws, long wstride, float sv)
{
    const int m = c->hi[0] - c->lo[0];
    const int C = ((m + 3) / 4 + 3) & ~3;
    const int nc = (m + C - 1) / C;
    const int nl = j1 - j0;
    const int srcline = k == c->src[2] ? c->src[1] - j0 : -1;
    const int srcx = c->src[0] - c->lo[0];
    float* zero = ws + 4 * wstride;
    float* sink = zero + C;
    fz_args A __attribute__((aligned(32)));
    int s, l, n, a;

    if (C & 7) {
        gs_plane4_asm(c, Q, X, j0, j1, k, ws, wstride, sv);
        return;
    }
    memset(zero, 0, C * sizeof(float));
    memset(&A, 0, sizeof A);
    A.N = c->pitch;
    A.N2 = c->slab;
    for (s = 0; s < nl + nc - 1; s++) {
        const int n0 = s - nc + 1 > 0 ? s - nc + 1 : 0, n1 = s < nl - 1 ? s : nl - 1;
        int srcI = -1, srcLane = -1, maxlen = 0, busy[4] = { 0, 0, 0, 0 }, bs[4] = { 0, 0, 0, 0 }, es[4] = { 0, 0, 0, 0 };
        long p0s[4] = { 0, 0, 0, 0 };
        for (l = 0; l < 4; l++)
            A.nv[l] = 0;
        for (n = n0; n <= n1; n++) {
            const int b = (s - n) * C, e = b + C < m ? b + C : m;
            const long p0 = k * c->slab + (j0 + n) * c->pitch + c->lo[0];
            l = n % 4;
            if (b == 0)
                for (a = 0; a < 6; a++)
                    A.U[a * 4 + l] = X[p0 - 1 - a];
            A.q[l] = Q + p0 + b;
            A.x[l] = X + p0 + b;
            A.nv[l] = e - b;
            busy[l] = 1;
            p0s[l] = p0;
            bs[l] = b;
            es[l] = e;
            if (e - b > maxlen)
                maxlen = e - b;
            if (n == srcline && srcx >= b && srcx < e) {
                srcI = srcx - b;
                srcLane = l;
            }
        }
        for (l = 0; l < 4; l++)
            if (!busy[l])
                for (a = 0; a < 6; a++)
                    A.U[a * 4 + l] = 0.0f;
        if (srcI < 0) {
            A.nblk = (maxlen + 7) >> 3;
            pulse_fused4_avx2(&A);
        } else {
            const float* S[4];
            float* out[4];
            int len[4], L = C;
            float carry[24];
            for (l = 0; l < 4; l++) {
                S[l] = zero;
                out[l] = sink;
                len[l] = C;
                if (busy[l]) {
                    float* Sl = ws + l * wstride;
                    rhs_asm(c, Q, X, p0s[l], bs[l], es[l], Sl);
                    S[l] = Sl + bs[l];
                    out[l] = X + p0s[l] + bs[l];
                    len[l] = es[l] - bs[l];
                    if (len[l] < L)
                        L = len[l];
                }
                for (a = 0; a < 6; a++)
                    carry[l * 6 + a] = A.U[a * 4 + l];
            }
            chain4_tr(c, S, out, L, srcI, srcLane, sv, carry, busy);
            for (l = 0; l < 4; l++)
                if (busy[l] && len[l] > L)
                    gs_chain(c, S[l], out[l], L, len[l], &carry[l * 6], l == srcLane ? srcI : -1, sv);
            for (l = 0; l < 4; l++)
                for (a = 0; a < 6; a++)
                    A.U[a * 4 + l] = carry[l * 6 + a];
        }
    }
}
#endif
typedef void (*gs_plane_fn)(const stGS*, const float*, float*, int, int, int, float*, long, float);

#define DLG 2
#define DL2 (4 * DLG)
typedef float v4f __attribute__((vector_size(16)));
typedef int v4i __attribute__((vector_size(16)));
static inline v4f ld4(const float* p)
{
    v4f v;
    memcpy(&v, p, 16);
    return v;
}
static inline void st4(float* p, v4f v) { memcpy(p, &v, 16); }
#define TR4(a0, a1, a2, a3, b0, b1, b2, b3)                         \
    do {                                                            \
        v4f t0_ = __builtin_shuffle(a0, a1, (v4i) { 0, 4, 1, 5 }); \
        v4f t1_ = __builtin_shuffle(a0, a1, (v4i) { 2, 6, 3, 7 }); \
        v4f t2_ = __builtin_shuffle(a2, a3, (v4i) { 0, 4, 1, 5 }); \
        v4f t3_ = __builtin_shuffle(a2, a3, (v4i) { 2, 6, 3, 7 }); \
        b0 = __builtin_shuffle(t0_, t2_, (v4i) { 0, 1, 4, 5 });    \
        b1 = __builtin_shuffle(t0_, t2_, (v4i) { 2, 3, 6, 7 });    \
        b2 = __builtin_shuffle(t1_, t3_, (v4i) { 0, 1, 4, 5 });    \
        b3 = __builtin_shuffle(t1_, t3_, (v4i) { 2, 3, 6, 7 });    \
    } while (0)

static inline __attribute__((always_inline)) void dl2_stage(const stGS* c, const float* Q, float* X, int j0, int nlines, int s,
    float* ws, long wstride, float sv)
{
    const int m = c->hi[0] - c->lo[0];
    const int m4 = m & ~3;
    const int lo2 = c->lo[2], hi2 = c->hi[2];
    const v4f zv = { 0, 0, 0, 0 };
    const v4f E1 = zv + c->e[1], E2 = zv + c->e[2], E3 = zv + c->e[3];
    const v4f E4 = zv + c->e[4], E5 = zv + c->e[5], E6 = zv + c->e[6];
    const float* Sp[DL2];
    float* Op[DL2];
    float* zero = ws + (long)DL2 * wstride;
    float* sink = zero + wstride;
    float* tail = sink + wstride;
    int busy[DL2];
    int l, g, i, srcI = -1, srcL = -1;

    for (l = 0; l < DL2; l++) {
        int k = lo2 + s - l;
        busy[l] = (l < nlines && k >= lo2 && k < hi2);
        if (!busy[l]) {
            Sp[l] = zero;
            Op[l] = sink;
            continue;
        }
        long p0 = k * c->slab + (j0 + l) * c->pitch + c->lo[0];
        float* Sl = ws + (long)l * wstride;
        gs_rhs(c, Q, X, p0, 0, m, Sl);
        for (i = m; i < m4 + 4; i++)
            Sl[i] = 0.0f;
        Sp[l] = Sl;
        Op[l] = X + p0;
        if (k == c->src[2] && j0 + l == c->src[1]) {
            srcI = c->src[0] - c->lo[0];
            srcL = l;
        }
    }
    {
        int any = 0;
        for (l = 0; l < DL2; l++)
            any |= busy[l];
        if (!any)
            return;
    }
    {
        const long sl = c->slab, pt = c->pitch;
        for (l = 0; l < DL2 && l < nlines; l++) {
            int k1 = lo2 + s + 1 - l;
            if (k1 < lo2 || k1 >= hi2)
                continue;
            const float* rq = Q + (k1 + 6) * sl + (j0 + l) * pt;
            const float* rx = X + k1 * sl + (j0 + l) * pt;
            const float* ra = Q + k1 * sl + (j0 + l + 6) * pt;
            for (i = 0; i < c->n; i += 16) {
                __builtin_prefetch(rq + i, 0, 3);
                __builtin_prefetch(rx + i, 1, 3);
                __builtin_prefetch(ra + i, 0, 3);
            }
            if (l == 0)
                for (int a = 1; a <= 6; a++) {
                    const float* bq = Q + k1 * sl + (j0 - a) * pt;
                    const float* bx = X + k1 * sl + (j0 - a) * pt;
                    for (i = 0; i < c->n; i += 16) {
                        __builtin_prefetch(bq + i, 0, 3);
                        __builtin_prefetch(bx + i, 0, 3);
                    }
                }
        }
    }
    const int srcB = srcI >= 0 ? srcI & ~3 : -1;
    v4f U1[DLG], U2[DLG], U3[DLG], U4[DLG], U5[DLG], U6[DLG];
    for (g = 0; g < DLG; g++)
        U1[g] = U2[g] = U3[g] = U4[g] = U5[g] = U6[g] = zv;

    for (i = 0; i < m; i += 4) {
        const int full = i + 4 <= m;
#pragma GCC unroll 4
        for (g = 0; g < DLG; g++) {
            v4f a0 = ld4(Sp[4 * g + 0] + i), a1 = ld4(Sp[4 * g + 1] + i);
            v4f a2 = ld4(Sp[4 * g + 2] + i), a3 = ld4(Sp[4 * g + 3] + i);
            v4f b0, b1, b2, b3, r0, r1, r2, r3;
            TR4(a0, a1, a2, a3, b0, b1, b2, b3);
            r0 = (b0 + ((((E6 * U6[g] + E5 * U5[g]) + E4 * U4[g]) + E3 * U3[g]) + E2 * U2[g])) + E1 * U1[g];
            if (__builtin_expect(i == srcB, 0) && srcI == i + 0 && (srcL >> 2) == g)
                r0[srcL & 3] = r0[srcL & 3] + sv;
            r1 = (b1 + ((((E6 * U5[g] + E5 * U4[g]) + E4 * U3[g]) + E3 * U2[g]) + E2 * U1[g])) + E1 * r0;
            if (__builtin_expect(i == srcB, 0) && srcI == i + 1 && (srcL >> 2) == g)
                r1[srcL & 3] = r1[srcL & 3] + sv;
            r2 = (b2 + ((((E6 * U4[g] + E5 * U3[g]) + E4 * U2[g]) + E3 * U1[g]) + E2 * r0)) + E1 * r1;
            if (__builtin_expect(i == srcB, 0) && srcI == i + 2 && (srcL >> 2) == g)
                r2[srcL & 3] = r2[srcL & 3] + sv;
            r3 = (b3 + ((((E6 * U3[g] + E5 * U2[g]) + E4 * U1[g]) + E3 * r0) + E2 * r1)) + E1 * r2;
            if (__builtin_expect(i == srcB, 0) && srcI == i + 3 && (srcL >> 2) == g)
                r3[srcL & 3] = r3[srcL & 3] + sv;
            U6[g] = U2[g];
            U5[g] = U1[g];
            U4[g] = r0;
            U3[g] = r1;
            U2[g] = r2;
            U1[g] = r3;
            TR4(r0, r1, r2, r3, a0, a1, a2, a3);
            if (full) {
                st4(Op[4 * g + 0] + i, a0);
                st4(Op[4 * g + 1] + i, a1);
                st4(Op[4 * g + 2] + i, a2);
                st4(Op[4 * g + 3] + i, a3);
            } else {
                st4(tail + (4 * g + 0) * 4, a0);
                st4(tail + (4 * g + 1) * 4, a1);
                st4(tail + (4 * g + 2) * 4, a2);
                st4(tail + (4 * g + 3) * 4, a3);
            }
        }
        if (!full) {
            for (l = 0; l < DL2; l++)
                if (busy[l])
                    for (int r = 0; r < m - i; r++)
                        Op[l][i + r] = tail[l * 4 + r];
            break;
        }
    }
}
#define DEF_DL2(NAME, ATTR) ATTR static void NAME(const stGS* c, const float* Q, float* X, int j0, int nlines, int s, \
    float* ws, long wstride, float sv) { dl2_stage(c, Q, X, j0, nlines, s, ws, wstride, sv); }
DEF_DL2(dl2_sse, )
DEF_DL2(dl2_avx2, T_AVX2)
DEF_DL2(dl2_avx512, T_AVX512)
typedef void (*dl2_fn)(const stGS*, const float*, float*, int, int, int, float*, long, float);

#ifndef MADV_POPULATE_WRITE
#define MADV_POPULATE_WRITE 23
#endif

static float noise(long p)
{
    unsigned h = (unsigned)p * 2654435761u;
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return ((float)(h >> 8) * (1.0f / 16777216.0f) * 2.0f - 1.0f) * NOISE_AMP;
}

static float pulse(float tau, float peak)
{
    float a = 3.14159265f * peak * tau;
    float a2 = a * a;
    return (1.0f - 2.0f * a2) * expf(-a2);
}

static long pad_stride(long n)
{
    long s = (n + 15) & ~15L;
    if (((s >> 4) & 1) == 0)
        s += 16;
    return s;
}

static float* alloc_zero(size_t bytes)
{
    void* p = NULL;
    if (posix_memalign(&p, 64, bytes ? bytes : 1)) {
        fprintf(stderr, "out of memory\n");
        exit(EXIT_FAILURE);
    }
    memset(p, 0, bytes);
    return (float*)p;
}

typedef struct {
    atomic_int v;
    char pad[128 - sizeof(atomic_int)];
} flag_t;

#ifdef __linux__
#define CUR_CPU() sched_getcpu()
#else
#define CUR_CPU() (-1)
#endif
static int cpu_package(int cpu)
{
    char path[96];
    int id = 0;
    FILE* f;
    if (cpu < 0)
        return 0;
    snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", cpu);
    f = fopen(path, "r");
    if (f) {
        if (fscanf(f, "%d", &id) != 1)
            id = 0;
        fclose(f);
    }
    return id;
}

static int cpu_node(int cpu)
{
    char path[96];
    int d;
    if (cpu < 0)
        return -1;
    for (d = 0; d < 1024; d++) {
        snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/node%d", cpu, d);
        if (access(path, F_OK) == 0)
            return d;
    }
    return -1;
}

static int cpu_l3(int cpu)
{
    char path[112];
    int id = -1;
    FILE* f;
    if (cpu < 0)
        return -1;
    snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cache/index3/id", cpu);
    f = fopen(path, "r");
    if (f) {
        if (fscanf(f, "%d", &id) != 1)
            id = -1;
        fclose(f);
    }
    if (id < 0) {
        snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cache/index3/shared_cpu_list", cpu);
        f = fopen(path, "r");
        if (f) {
            if (fscanf(f, "%d", &id) != 1)
                id = -1;
            fclose(f);
        }
    }
    return id;
}

static int read_small(const char* path, char* buf, int cap)
{
    int fd = open(path, O_RDONLY), n;
    if (fd < 0)
        return -1;
    n = (int)read(fd, buf, cap - 1);
    close(fd);
    if (n < 0)
        return -1;
    buf[n] = 0;
    return n;
}

static void topo_fill(int cpu, int ncpu, int* pkof, int* l3of)
{
    char path[112], buf[4096];
    const int p = cpu_package(cpu), l = cpu_l3(cpu);
    int done = 0;
    snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cache/index3/shared_cpu_list", cpu);
    if (read_small(path, buf, sizeof buf) > 0) {
        const char* q = buf;
        while (*q >= '0' && *q <= '9') {
            char* e;
            long a = strtol(q, &e, 10), b = a, v;
            if (*e == '-')
                b = strtol(e + 1, &e, 10);
            for (v = a; v <= b && v < ncpu; v++)
                if (v >= 0) {
                    pkof[v] = p;
                    l3of[v] = l;
                    done |= v == cpu;
                }
            q = *e == ',' ? e + 1 : e;
        }
    }
    if (!done) {
        pkof[cpu] = p;
        l3of[cpu] = l;
    }
}

static int cmp_int(const void* a, const void* b)
{
    int x = *(const int*)a, y = *(const int*)b;
    return (x > y) - (x < y);
}

static inline void cpu_relax(void)
{
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__)
    __asm__ __volatile__("yield" ::: "memory");
#endif
}

static inline void wait_ge(atomic_int* f, int v)
{
    int s = 0;
    while (atomic_load_explicit(f, memory_order_acquire) < v) {
        cpu_relax();
        if (++s > 2000) {
            sched_yield();
            s = 0;
        }
    }
}

int main(void)
{
    int N, T, t, b, g;
    const float dx = 12.5f, dt = 0.001f, vp = 2500.0f, peak = 25.0f;
    stGS c;

    if (scanf("%d %d", &N, &T) != 2)
        return EXIT_FAILURE;
    const int init_env = getenv("PULSE_INIT") ? atoi(getenv("PULSE_INIT")) : 2;
    if (!getenv("OMP_NUM_THREADS") && omp_get_num_places() > 0)
        omp_set_num_threads(omp_get_num_places());

    long pitch = pad_stride(N), slab = pad_stride(N * pitch);
    size_t cells = (size_t)slab * N;
    float* wavelet = alloc_zero((T > 0 ? T : 1) * sizeof(float));

    init_constants(&c, dx, dt, vp);
    c.n = N;
    c.pitch = pitch;
    c.slab = slab;
    c.lo[0] = c.lo[1] = c.lo[2] = BORDER;
    c.hi[0] = c.hi[1] = c.hi[2] = N - BORDER;
    c.src[0] = 40;
    c.src[1] = 48;
    c.src[2] = 56;

    float delay = 1.2f / peak;
    for (t = 0; t < T; t++)
        wavelet[t] = 400.0f * pulse(t * dt - delay, peak);

    const char* ei = getenv("PULSE_ISA");
    const char* el = getenv("PULSE_LANES");
#if defined(__x86_64__) || defined(__i386__)
    __builtin_cpu_init();
#endif
    int isa = CPU_HAS("avx512f") ? 2 : CPU_HAS("avx2") ? 1 : 0;
    if (ei) {
        int want = !strcmp(ei, "sse") ? 0 : !strcmp(ei, "avx2") ? 1 : 2;
        if (want < isa)
            isa = want;
    }
    int lanes = el ? atoi(el) : (isa == 2 ? DEF_LANES_AVX512 : isa == 1 ? DEF_LANES_AVX2 : 4);
    gs_plane_fn gsp = gs_plane4_sse;
    dl2_fn dlf = isa == 2 ? dl2_avx512 : isa == 1 ? dl2_avx2 : dl2_sse;
    if (isa == 0)
        gsp = lanes == 8 ? gs_plane8_sse : gs_plane4_sse;
    else if (isa == 1)
        gsp = lanes == 16 ? gs_plane16_avx2 : lanes == 8 ? gs_plane8_avx2 : gs_plane4_avx2;
    else
        gsp = lanes == 16 ? gs_plane16_avx512 : lanes == 8 ? gs_plane8_avx512 : gs_plane4_avx512;
#if defined(__x86_64__)
    {
        const char* ea = getenv("PULSE_ASM");
        int a, v;
        const int am = ea ? atoi(ea) : 1;
        if (isa >= 1 && lanes == 4 && CPU_HAS("avx2") && am > 0)
            gsp = am == 2 ? gs_plane4_fz : am == 3 ? gs_plane4_asm : gs_plane4_sep;
        for (v = 0; v < 8; v++) {
            for (a = 0; a <= 6; a++)
                pulse_kc[a * 8 + v] = c.W[a];
            for (a = 1; a <= 6; a++)
                pulse_kc[(6 + a) * 8 + v] = c.e[a];
            pulse_kc[13 * 8 + v] = c.k;
            pulse_kc[14 * 8 + v] = c.g;
            pulse_kc[15 * 8 + v] = 2.0f;
        }
    }
#endif

    {
        const char* ep = getenv("PULSE_PF");
        g_pf = ep ? atoi(ep) : 2;
    }

    {
        const char* et = getenv("PULSE_TCH");
        g_tch = et ? atoi(et) : 1;
        et = getenv("PULSE_MASKC");
        g_maskc = et ? atoi(et) : 0;
        et = getenv("PULSE_PFW");
        g_pfw = et ? atoi(et) : 1;
    }
    const int ptime = getenv("PULSE_TIME") != NULL;
    double tm0 = omp_get_wtime(), tm1 = 0, tm2 = 0, tm3 = 0;

    const int P = omp_get_max_threads();
    int G = 1, mode = 4;
    int* gid = calloc(P, sizeof(int));
    int* grank = calloc(P, sizeof(int));
    int* gcnt = calloc(P + 1, sizeof(int));
    int* pk = malloc(P * sizeof(int));
    int* nd = malloc(P * sizeof(int));
    int* l3 = malloc(P * sizeof(int));
    int nthr = 0;
    {
        const char* eg = getenv("PULSE_GROUPS");
        int ng = eg ? atoi(eg) : 0;
        mode = !eg || !strcmp(eg, "l3") ? 4 : !strcmp(eg, "socket") ? 1 : !strcmp(eg, "numa") ? 0 : ng > 0 ? 2 : 3;
        if (P > 1 && mode != 3) {
            const int topo = getenv("PULSE_TOPO") ? atoi(getenv("PULSE_TOPO")) : 1;
            int* cpus = malloc(P * sizeof(int));
#pragma omp parallel num_threads(P)
            {
                int tid = omp_get_thread_num(), cpu = CUR_CPU();
                if (tid == 0)
                    nthr = omp_get_num_threads();
                cpus[tid] = cpu;
                nd[tid] = mode == 0 ? cpu_node(cpu) : 0;
                if (!topo) {
                    pk[tid] = mode == 2 ? (int)((long)tid * ng / P) : cpu_package(cpu);
                    {
                        int l = mode == 4 ? cpu_l3(cpu) : 0;
                        l3[tid] = l < 0 ? -1 : pk[tid] * 65536 + l;
                    }
                }
            }
            if (topo) {
                int i, ncpu = 0;
                for (i = 0; i < P; i++)
                    if (cpus[i] + 1 > ncpu)
                        ncpu = cpus[i] + 1;
                int* pkof = malloc((ncpu + 1) * sizeof(int));
                int* l3of = malloc((ncpu + 1) * sizeof(int));
                for (i = 0; i <= ncpu; i++)
                    pkof[i] = l3of[i] = -2;
                for (i = 0; i < P; i++) {
                    const int cpu = cpus[i];
                    if (mode == 2)
                        pk[i] = (int)((long)i * ng / P);
                    else if (cpu < 0)
                        pk[i] = 0;
                    else {
                        if (pkof[cpu] == -2)
                            topo_fill(cpu, ncpu, pkof, l3of);
                        pk[i] = pkof[cpu];
                    }
                    {
                        int l = mode == 4 ? (cpu < 0 ? -1 : l3of[cpu]) : 0;
                        l3[i] = l < 0 ? -1 : pk[i] * 65536 + l;
                    }
                }
                free(pkof);
                free(l3of);
            }
            free(cpus);
            if (nthr != P)
                mode = 3;
        } else
            mode = 3;
    }
    const int J = c.hi[1] - c.lo[1], K = c.hi[2] - c.lo[2];
    const char* ek = getenv("PULSE_KER");
    const int KER = ek ? atoi(ek) : DEFAULT_KER;
    int nb = 0, BL = 0, H = 0;
    int* bj0 = malloc((J + 2) * sizeof(int));
    int* QB = malloc((P + 2) * sizeof(int));
    for (;;) {
        int ok = 1;
        {
            int i, u, v;
            int* key = mode == 0 ? nd : mode == 4 ? l3 : pk;
            G = 1;
            memset(gcnt, 0, (P + 1) * sizeof(int));
            if (mode != 3) {
                int* vals = malloc(P * sizeof(int));
                memcpy(vals, key, P * sizeof(int));
                qsort(vals, P, sizeof(int), cmp_int);
                for (i = 0, u = 0; i < P; i++)
                    if (i == 0 || vals[i] != vals[u - 1])
                        vals[u++] = vals[i];
                G = u;
                if ((mode == 0 || mode == 4) && vals[0] < 0)
                    G = 0;
                for (i = 0; i < P && G > 0; i++) {
                    for (v = 0; vals[v] != key[i]; v++)
                        ;
                    gid[i] = v;
                    grank[i] = gcnt[v]++;
                }
                free(vals);
                if (G <= 1 && (mode == 0 || mode == 4)) {
                    mode = 1;
                    continue;
                }
            }
            if (G <= 1) {
                G = 1;
                mode = 3;
                gcnt[0] = P;
                for (i = 0; i < P; i++) {
                    gid[i] = 0;
                    grank[i] = i;
                }
            }
        }
        if (KER == 2) {
            BL = DL2;
            nb = (J + BL - 1) / BL;
            for (b = 0; b <= nb; b++)
                bj0[b] = c.lo[1] + b * BL < c.hi[1] ? c.lo[1] + b * BL : c.hi[1];
            for (g = 0, t = 0; g <= G; g++) {
                QB[g] = (int)((long)nb * t / P);
                if (g < G)
                    t += gcnt[g];
            }
            for (g = 0; g < G; g++)
                ok &= QB[g + 1] > QB[g];
        } else {
            const char* eh = getenv("PULSE_H");
            H = eh ? atoi(eh) : (J / BLOCK_LINES >= 8 ? BLOCK_LINES : J / 8);
            if (H < 6)
                H = 6;
            nb = 0;
            bj0[0] = c.lo[1];
            for (g = 0, t = 0; g < G; g++) {
                int jl = c.lo[1] + (int)((long)J * t / P), jh, nbg, i;
                t += gcnt[g];
                jh = c.lo[1] + (int)((long)J * t / P);
                if (jh - jl < (G > 1 ? (mode == 4 ? 24 : 6) : 1))
                    ok = 0;
                nbg = (jh - jl) / H > 0 ? (jh - jl) / H : 1;
                QB[g] = nb;
                for (i = 1; i <= nbg; i++)
                    bj0[nb + i] = jl + (int)((long)(jh - jl) * i / nbg);
                nb += nbg;
            }
            QB[G] = nb;
        }
        if (ok || G == 1)
            break;
        mode = mode == 0 || mode == 4 ? 1 : 3;
    }
    int* bup = malloc(nb * sizeof(int));
    for (b = 0; b < nb; b++) {
        int jl = bj0[b + 1] + 5 < c.hi[1] - 1 ? bj0[b + 1] + 5 : c.hi[1] - 1, u = b;
        while (u + 1 < nb && bj0[u + 1] <= jl)
            u++;
        bup[b] = u;
    }
    const int NS = BL ? K + BL - 1 : K;
    const int OFF = BL ? BL - 1 : 0;
    if (getenv("PULSE_VERBOSE")) {
        fprintf(stderr, "isa=%s lanes=%d KER=%d H=%d nb=%d P=%d groups=%d (%s):", isa == 2 ? "avx512f" : isa == 1 ? "avx2" : "sse2",
            lanes, KER, H, nb, P, G, mode == 0 ? "numa" : mode == 1 ? "socket" : mode == 2 ? "blocks" : mode == 4 ? "l3" : "single");
        for (g = 0; g < G; g++)
            fprintf(stderr, " [thr=%d bands %d..%d]", gcnt[g], QB[g], QB[g + 1] - 1);
        fprintf(stderr, "\n");
    }

    const size_t bytes = cells * sizeof(float);
    float* U[2];
    float* Ubase[2];
    void* Umap[2];
    size_t Usz[2];
    int init_mode = init_env;
    int aoff = 2;
    {
        const char* ea = getenv("PULSE_AOFF");
        if (ea)
            aoff = atoi(ea);
        if (aoff < 0 || aoff > 15)
            aoff = 0;
    }
    {
        const char* et = getenv("PULSE_THP");
        const int nothp = et ? !atoi(et) : G > 1;
        (void)nothp;
        int x;
        long xoff = 512;
        {
            const char* ex = getenv("PULSE_XOFF");
            if (ex)
                xoff = atol(ex);
            if (xoff < 0 || xoff > 4096)
                xoff = 0;
        }
        for (x = 0; x < 2; x++) {
            void* p = NULL;
            Usz[x] = bytes + 64 + 4 * xoff + (1 << 21);
            Umap[x] = mmap(NULL, Usz[x], PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
            if (Umap[x] == MAP_FAILED) {
                fprintf(stderr, "out of memory\n");
                exit(EXIT_FAILURE);
            }
            p = (void*)(((uintptr_t)Umap[x] + ((uintptr_t)1 << 21) - 1) & ~(((uintptr_t)1 << 21) - 1));
#ifdef MADV_NOHUGEPAGE
            if (nothp)
                madvise(p, bytes + 4 * xoff, MADV_NOHUGEPAGE);
#endif
            Ubase[x] = (float*)p;
            if (x == 0 && init_mode == 2 && madvise(Umap[0], 4096, MADV_POPULATE_WRITE) != 0)
                init_mode = 0;
            U[x] = (float*)p + aoff + (x == 1 ? xoff : 0);
        }
    }
#pragma omp parallel num_threads(P)
    {
        const int tid = omp_get_thread_num(), nt = omp_get_num_threads();
        const int grouped = G > 1 && nt == P;
        const int gg = grouped ? gid[tid] : 0, rk = grouped ? grank[tid] : tid, ng = grouped ? gcnt[gg] : nt;
        const long rl = grouped && gg > 0 ? bj0[QB[gg]] : 0, rh = grouped && gg < G - 1 ? bj0[QB[gg + 1]] : N;
        const int last = grouped ? gg == G - 1 : 1;
        long k, j, i;
        for (k = rk; k < N; k += ng) {
            int x;
            if (init_mode == 0)
                for (x = 0; x < 2; x++) {
                    memset(U[x] + k * slab + rl * pitch, 0, (size_t)(rh - rl) * pitch * sizeof(float));
                    if (last)
                        memset(U[x] + k * slab + (long)N * pitch, 0, (size_t)(slab - (long)N * pitch) * sizeof(float));
                }
            if (k < c.lo[2] || k >= c.hi[2])
                continue;
            if (init_mode == 2) {
                const long ja = rl > c.lo[1] ? rl : c.lo[1], jb = rh < c.hi[1] ? rh : c.hi[1];
                if (ja < jb)
                    for (x = 0; x < 2; x++) {
                        const uintptr_t a0 = (uintptr_t)(U[x] + k * c.slab + ja * c.pitch) & ~(uintptr_t)4095;
                        const uintptr_t a1 = ((uintptr_t)(U[x] + k * c.slab + (jb - 1) * c.pitch + c.hi[0]) + 4095) & ~(uintptr_t)4095;
                        madvise((void*)a0, a1 - a0, MADV_POPULATE_WRITE);
                    }
            }
            for (j = rl > c.lo[1] ? rl : c.lo[1]; j < (rh < c.hi[1] ? rh : c.hi[1]); j++)
                for (i = c.lo[0]; i < c.hi[0]; i++) {
                    long p = k * c.slab + j * c.pitch + i;
                    U[0][p] = U[1][p] = noise((k * c.n + j) * c.n + i);
                }
        }
    }

    tm1 = omp_get_wtime();
    const long wstride = (N + 64) & ~15L;
    const long wsz = (DL2 + 3 > 16 + 2 ? DL2 + 3 : 16 + 2) * wstride + 64;
    const int ntask = T > 1 ? (T - 1) * nb : 0;
    flag_t* prog = NULL;
    flag_t* nxt = NULL;
    if (posix_memalign((void**)&prog, 128, (size_t)(ntask > 0 ? ntask : 1) * sizeof(flag_t))
        || posix_memalign((void**)&nxt, 128, (size_t)(G + 1) * sizeof(flag_t)))
        return EXIT_FAILURE;
    for (int q = 0; q < ntask; q++)
        atomic_init(&prog[q].v, 0);
    for (g = 0; g <= G; g++)
        atomic_init(&nxt[g].v, 0);
#define CLS(x) ((x) < NS ? (x) : NS)
#define WAITT(f, v)                                \
    do {                                           \
        if (ptime) {                               \
            const double t0_ = omp_get_wtime();    \
            wait_ge(f, v);                         \
            wsum += omp_get_wtime() - t0_;         \
        } else                                     \
            wait_ge(f, v);                         \
    } while (0)
    double* wtime = calloc(P, sizeof(double));
    const int wsm = getenv("PULSE_WSM") ? atoi(getenv("PULSE_WSM")) : 1;
    const size_t wsb = ((size_t)wsz * sizeof(float) + 4095) & ~(size_t)4095;
    char* wsall = NULL;
    if (wsm) {
        wsall = mmap(NULL, (size_t)P * wsb, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (wsall == MAP_FAILED)
            wsall = NULL;
#ifdef MADV_NOHUGEPAGE
        else
            madvise(wsall, (size_t)P * wsb, MADV_NOHUGEPAGE);
#endif
    }

#pragma omp parallel num_threads(P)
    {
        const int tid = omp_get_thread_num(), nt = omp_get_num_threads();
        const int grouped = G > 1 && nt == P;
        const int gg = grouped ? gid[tid] : G;
        const int b0 = grouped ? QB[gg] : 0, nbg = grouped ? QB[gg + 1] - QB[gg] : nb;
        const int ntg = T > 1 ? (T - 1) * nbg : 0;
        float* ws = wsall ? (float*)(wsall + (size_t)tid * wsb) : alloc_zero(wsz * sizeof(float));
        if (wsall)
            memset(ws, 0, wsz * sizeof(float));
        double wsum = 0.0;
        int r;
        while ((r = atomic_fetch_add_explicit(&nxt[gg].v, 1, memory_order_relaxed)) < ntg) {
            const int tt = r / nbg + 1, bb = b0 + r % nbg, q = (tt - 1) * nb + bb;
            const float sv = c.r * wavelet[tt];
            const float* Qp = U[(tt - 1) & 1];
            float* Xp = U[tt & 1];
            const int j0 = bj0[bb], j1 = bj0[bb + 1];
            for (int s = 0; s < NS; s++) {
                if (bb > 0)
                    WAITT(&prog[q - 1].v, CLS(s + 1 + OFF));
                if (tt > 1) {
                    WAITT(&prog[q - nb].v, CLS((s + 7 < K ? s + 7 : K) + OFF));
                    if (bup[bb] > bb)
                        WAITT(&prog[(tt - 2) * nb + bup[bb]].v, CLS(s + 1 + OFF));
                }
                if (BL)
                    dlf(&c, Qp, Xp, j0, j1 - j0, s, ws, wstride, sv);
                else
                    gsp(&c, Qp, Xp, j0, j1, c.lo[2] + s, ws, N, sv);
                atomic_store_explicit(&prog[q].v, s + 1, memory_order_release);
            }
        }
        if (!wsall)
            free(ws);
        if (ptime)
            wtime[omp_get_thread_num()] = wsum;
    }

    tm2 = omp_get_wtime();
    const float* res = U[(T - 1) & 1];
    size_t i, p0 = (size_t)c.src[2] * slab + (size_t)c.src[1] * pitch;
    for (i = p0; i < p0 + (size_t)N; i++)
        if (fabs(res[i]) >= PRINT_MIN && fabs(res[i]) <= PRINT_MAX)
            printf("%.6f\n", res[i]);

    double total = 0.0;
    double* ps = malloc(N * sizeof(double));
    long kz;
#pragma omp parallel for schedule(static)
    for (kz = 0; kz < N; kz++) {
        double plane = 0.0;
        long jy, ix;
        for (jy = 0; jy < N; jy++)
            for (ix = 0; ix < N; ix++)
                plane += res[kz * slab + jy * pitch + ix];
        ps[kz] = plane;
    }
    for (kz = 0; kz < N; kz++)
        total += ps[kz];
    printf("%.17g\n", total);
    tm3 = omp_get_wtime();
    if (ptime)
    {
        double ws_ = 0, wm_ = 0;
        int ti;
        for (ti = 0; ti < P; ti++) {
            ws_ += wtime[ti];
            if (wtime[ti] > wm_)
                wm_ = wtime[ti];
        }
        fprintf(stderr, "phases: setup+touch+init=%.4f sweep=%.4f out+checksum=%.4f wait_mean=%.4f wait_max=%.4f\n", tm1 - tm0,
            tm2 - tm1, tm3 - tm2, ws_ / P, wm_);
    }

    {
        const char* er = getenv("PULSE_REL");
        if (er ? atoi(er) : 1) {
#ifdef MADV_DONTNEED
            const size_t pg = 4096, nb2 = bytes & ~(pg - 1);
#pragma omp parallel num_threads(P)
            {
                const int tid = omp_get_thread_num(), nt = omp_get_num_threads();
                const size_t per = ((nb2 / pg + nt - 1) / nt) * pg;
                const size_t lo = (size_t)tid * per, hi = lo + per < nb2 ? lo + per : nb2;
                int x;
                if (lo < hi)
                    for (x = 0; x < 2; x++)
                        madvise((char*)Ubase[x] + lo, hi - lo, MADV_DONTNEED);
            }
#endif
        }
    }
    if (ptime)
        fprintf(stderr, "release=%.4f\n", omp_get_wtime() - tm3);
    free(ps);
    free(prog);
    free(nxt);
    free(bup);
    free(bj0);
    free(QB);
    free(gid);
    free(grank);
    free(gcnt);
    free(pk);
    free(nd);
    free(l3);
    free(wavelet);
    munmap(Umap[0], Usz[0]);
    munmap(Umap[1], Usz[1]);
    return 0;
}
