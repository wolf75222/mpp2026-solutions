#ifdef A_PART_DTM
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <emmintrin.h>
#include "fastio.h"
#ifndef SMALL_MAX
#define SMALL_MAX 4000000L
#endif
#ifndef CPT
#define CPT 18000L
#endif
#ifndef TMAXP
#define TMAXP 32
#endif
#ifndef OUT_CAP
#define OUT_CAP (64 * 1024)
#endif
#ifndef POPULATE
#define POPULATE 1
#endif
#ifndef PREFAULT
#define PREFAULT 1
#endif
#ifndef HUGE
#define HUGE 0
#endif
#ifndef HUGE_MIN
#define HUGE_MIN ((size_t)1 << 20)
#endif
#ifndef PROF
#define PROF 0
#endif
#ifndef G6_EXP
#define G6_EXP 1
#endif
#ifndef REL_END
#define REL_END 1
#endif
#if PROF
static double ptime() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec * 1e-6;
}
static double g_pt[32];
static const char *g_pn[32];
static int g_np;
#define PM(name)                                                                                                       \
    do {                                                                                                               \
        g_pt[g_np] = ptime();                                                                                          \
        g_pn[g_np++] = name;                                                                                           \
    } while (0)
#else
#define PM(name)                                                                                                       \
    do {                                                                                                               \
    } while (0)
#endif
static const unsigned NOD = 15;
#ifndef STEPMODE
#define STEPMODE 2
#endif
struct Step {
    const long *off;
    unsigned k;
    long o;
    explicit Step(const long *t) : off(t), k(0), o(0) {}
    inline long next(long c, unsigned kk);
};
static inline long step(long c, unsigned k, long M) {
    switch (k) {
    case 1:
        return c - M;
    case 2:
        return c - M + 1;
    case 3:
        return c + 1;
    case 4:
        return c + M + 1;
    case 5:
        return c + M;
    case 6:
        return c + M - 1;
    case 7:
        return c - 1;
    default:
        return c - M - 1;
    }
}
inline long Step::next(long c, unsigned kk) {
#if STEPMODE == 1
    return step(c, kk, off[5]);
#elif STEPMODE == 2
    if (kk != k) {
        k = kk;
        o = off[kk];
    }
    return c + o;
#else
    return c + off[kk];
#endif
}
static char g_ob[OUT_CAP + 512] __attribute__((aligned(64)));
static void write_all(int fd, const char *b, size_t len) {
    while (len) {
        ssize_t w = write(fd, b, len);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            perror("write");
            exit(1);
        }
        b += w;
        len -= (size_t)w;
    }
}
static void writev_all(struct iovec *iov, int cnt) {
    int i = 0;
    while (i < cnt) {
        const int c = cnt - i > 1024 ? 1024 : cnt - i;
        ssize_t w = writev(1, iov + i, c);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            perror("write");
            exit(1);
        }
        size_t left = (size_t)w;
        while (i < cnt && left >= iov[i].iov_len) {
            left -= iov[i].iov_len;
            ++i;
        }
        if (i < cnt && left) {
            iov[i].iov_base = (char *)iov[i].iov_base + left;
            iov[i].iov_len -= left;
        }
    }
}
template <bool S> static inline char *ensure(char *q, size_t need) {
    if (S && (size_t)(q - g_ob) + need > (size_t)OUT_CAP) {
        write_all(1, g_ob, (size_t)(q - g_ob));
        q = g_ob;
    }
    return q;
}
template <bool S> static char *put_str(char *q, const char *s) {
    const size_t l = strlen(s);
    q = ensure<S>(q, l);
    memcpy(q, s, l);
    return q + l;
}
template <bool S> static char *put_ghost(char *q, const char *tok, long M) {
    const size_t tl = strlen(tok);
    for (long j = 0; j < M;) {
        const long b = M - j < 4096 ? M - j : 4096;
        q = ensure<S>(q, (size_t)b * 3 + 1);
        if (tl == 2)
            for (long k = 0; k < b; k++) {
                memcpy(q, tok, 2);
                q += 2;
            }
        else
            for (long k = 0; k < b; k++) {
                memcpy(q, tok, 3);
                q += 3;
            }
        j += b;
    }
    q = ensure<S>(q, 1);
    *q++ = '\n';
    return q;
}
static inline const char *ptok(const char *p, float *out) {
    const char *const s = p;
    const unsigned neg = *p == '-';
    p += neg;
    uint32_t m = (uint32_t)(unsigned char)*p - '0';
    if (m > 9)
        return fio::parse_float(s, out);
    const char *const d0 = p;
    ++p;
    uint32_t d;
    while ((d = (uint32_t)(unsigned char)*p - '0') <= 9) {
        m = m * 10 + d;
        ++p;
    }
    long nd = p - d0;
    long e10 = 0;
    if (*p == '.') {
        ++p;
        const char *const f0 = p;
        while ((d = (uint32_t)(unsigned char)*p - '0') <= 9) {
            m = m * 10 + d;
            ++p;
        }
        e10 = f0 - p;
        nd -= e10;
    }
    if (nd > 9 || (unsigned char)*p > ' ' || m >= (1u << 24))
        return fio::parse_float(s, out);
    float f = (float)m;
    if (e10)
        f /= fio::P10F[-e10];
    *out = neg ? -f : f;
    return p;
}
static inline char *put4(char *p, uint32_t v) {
    if (v < 100) {
        if (v < 10) {
            *p = (char)('0' + v);
            return p + 1;
        }
        memcpy(p, fio::DIG2 + 2 * v, 2);
        return p + 2;
    }
    const uint32_t h = v / 100, l = v - 100 * h;
    if (v < 1000) {
        p[0] = (char)('0' + h);
        memcpy(p + 1, fio::DIG2 + 2 * l, 2);
        return p + 3;
    }
    memcpy(p, fio::DIG2 + 2 * h, 2);
    memcpy(p + 2, fio::DIG2 + 2 * l, 2);
    return p + 4;
}
static inline char *pu32(char *p, uint32_t v) {
    if (v < 10000)
        return put4(p, v);
    if (v < 100000000) {
        const uint32_t h = v / 10000, l = v - 10000 * h;
        p = put4(p, h);
        const uint32_t a = l / 100, b = l - 100 * a;
        memcpy(p, fio::DIG2 + 2 * a, 2);
        memcpy(p + 2, fio::DIG2 + 2 * b, 2);
        return p + 4;
    }
    return fio::put_u32(p, v);
}
static inline char *g6_slow(char *p, float f) {
    char t[48];
    const int n = snprintf(t, sizeof t, "%g", (double)f);
    memcpy(p, t, (size_t)n);
    return p + n;
}
static inline char *g6_exp(char *p, double v, double a) {
    if (v < 0)
        *p++ = '-';
    static const double P10E[11] = {1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15};
    int X = 6;
    while (X < 14 && a >= P10E[X - 4])
        X++;
    const double d = fio::P10D[X - 5];
    uint64_t q = (uint64_t)(a / d);
    double r = a - (double)q * d;
    if (r < 0) {
        q--;
        r += d;
    } else if (r >= d) {
        q++;
        r -= d;
    }
    if (2.0 * r > d || (2.0 * r == d && (q & 1u)))
        q++;
    if (q >= 1000000u) {
        q = 100000u;
        X++;
    }
    const uint32_t u = (uint32_t)q;
    char dg[6];
    {
        uint32_t x = u;
        for (int i = 5; i >= 0; i--) {
            dg[i] = (char)('0' + x % 10);
            x /= 10;
        }
    }
    int last = 5;
    while (last > 0 && dg[last] == '0')
        last--;
    *p++ = dg[0];
    if (last > 0) {
        *p++ = '.';
        for (int i = 1; i <= last; i++)
            *p++ = dg[i];
    }
    p[0] = 'e';
    p[1] = '+';
    p[2] = (char)('0' + X / 10);
    p[3] = (char)('0' + X % 10);
    return p + 4;
}
static inline char *g6(char *p, float f) {
    const double v = (double)f;
    const double a = __builtin_fabs(v);
#if G6_EXP
    if (!(a < 1e6))
        return a < 1e15 ? g6_exp(p, v, a) : g6_slow(p, f);
#else
    if (!(a < 1e6))
        return g6_slow(p, f);
#endif
    if (__builtin_signbit(v))
        *p++ = '-';
    if (a == 0.0) {
        *p++ = '0';
        return p;
    }
    const uint32_t ia = (uint32_t)a;
    if ((double)ia == a)
        return pu32(p, ia);
    int X;
    double s;
    if (a >= 1.0) {
        X = a >= 1e5 ? 5 : a >= 1e4 ? 4 : a >= 1e3 ? 3 : a >= 1e2 ? 2 : a >= 1e1 ? 1 : 0;
        s = a * fio::P10D[5 - X];
    } else {
        X = -1;
        s = a * fio::P10D[6];
        while (s < 1e5) {
            if (X == -4) {
                if (v < 0)
                    --p;
                return g6_slow(p, f);
            }
            --X;
            s *= 10.0;
        }
    }
    uint32_t r = (uint32_t)s;
    const double fr = s - (double)r;
    if (fr > 0.5 || (fr == 0.5 && (r & 1u)))
        r++;
    if (r >= 1000000u) {
        r = 100000u;
        X++;
        if (X >= 6) {
            if (v < 0)
                --p;
            return g6_slow(p, f);
        }
    }
    uint64_t w;
    {
        uint16_t a2, b2, c2;
        memcpy(&a2, fio::DIG2 + 2 * (r / 10000), 2);
        memcpy(&b2, fio::DIG2 + 2 * ((r / 100) % 100), 2);
        memcpy(&c2, fio::DIG2 + 2 * (r % 100), 2);
        w = (uint64_t)a2 | ((uint64_t)b2 << 16) | ((uint64_t)c2 << 32);
    }
    const uint64_t z = w ^ 0x0000303030303030ULL;
    const uint64_t nz = (((z & 0x7f7f7f7f7f7f7f7fULL) + 0x7f7f7f7f7f7f7f7fULL) | z) & 0x0000808080808080ULL;
    const int last = (63 - __builtin_clzll(nz)) >> 3;
    if (X >= 0) {
        memcpy(p, &w, 8);
        if (last > X) {
            p[X + 1] = '.';
            const uint64_t fw = w >> (8 * (X + 1));
            memcpy(p + X + 2, &fw, 8);
            p += last + 2;
        } else
            p += X + 1;
    } else {
        memcpy(p, "0.000000", 8);
        memcpy(p + 1 - X, &w, 8);
        p += 2 - X + last;
    }
    return p;
}
static long count_tokens(const char *s, const char *e) {
    long c = 0;
    const char *q = s;
    const __m128i sp = _mm_set1_epi8(' ');
    const __m128i zero = _mm_setzero_si128();
    while (e - q >= 16) {
        long it = (e - q) / 16;
        if (it > 255)
            it = 255;
        __m128i acc = zero;
        for (long k = 0; k < it; k++, q += 16) {
            const __m128i cur = _mm_loadu_si128((const __m128i *)q);
            const __m128i prv = _mm_loadu_si128((const __m128i *)(q - 1));
            const __m128i wc = _mm_cmpeq_epi8(_mm_max_epu8(cur, sp), sp);
            const __m128i wp = _mm_cmpeq_epi8(_mm_max_epu8(prv, sp), sp);
            acc = _mm_sub_epi8(acc, _mm_andnot_si128(wc, wp));
        }
        const __m128i sad = _mm_sad_epu8(acc, zero);
        c += _mm_cvtsi128_si64(sad) + _mm_cvtsi128_si64(_mm_unpackhi_epi64(sad, sad));
    }
    for (; q < e; ++q)
        c += ((unsigned char)q[0] > ' ') & ((unsigned char)q[-1] <= ' ');
    return c;
}
static const char *skip_tokens(const char *s, const char *e, long k) {
    const char *q = s;
    const __m128i sp = _mm_set1_epi8(' ');
    while (e - q >= 16) {
        const __m128i cur = _mm_loadu_si128((const __m128i *)q);
        const __m128i prv = _mm_loadu_si128((const __m128i *)(q - 1));
        unsigned msk = (unsigned)_mm_movemask_epi8(
            _mm_andnot_si128(_mm_cmpeq_epi8(_mm_max_epu8(cur, sp), sp), _mm_cmpeq_epi8(_mm_max_epu8(prv, sp), sp)));
        const long c = __builtin_popcount(msk);
        if (c > k) {
            while (k--)
                msk &= msk - 1;
            return q + __builtin_ctz(msk);
        }
        k -= c;
        q += 16;
    }
    for (; q < e; ++q)
        if (((unsigned char)q[0] > ' ') & ((unsigned char)q[-1] <= ' ')) {
            if (k == 0)
                return q;
            --k;
        }
    return e;
}
static const char *parse_row(const char *p, const char *end, float *r, float *e, long M, float nv, int *bad) {
    for (long j = 0; j < M; j++) {
        while (p < end && (unsigned char)*p <= ' ')
            ++p;
        if (p >= end) {
            for (; j < M; j++)
                r[j] = 0.0f;
            break;
        }
        p = ptok(p, &r[j]);
        if ((unsigned char)*p > ' ')
            *bad = 1;
    }
    const __m128 vnv = _mm_set1_ps(nv), inf = _mm_set1_ps(__builtin_inff());
    long j = 0;
    for (; j + 4 <= M; j += 4) {
        const __m128 x = _mm_loadu_ps(r + j);
        const __m128 c = _mm_cmpeq_ps(x, vnv);
        _mm_storeu_ps(e + j, _mm_or_ps(_mm_and_ps(c, inf), _mm_andnot_ps(c, x)));
    }
    for (; j < M; j++)
        e[j] = r[j] == nv ? __builtin_inff() : r[j];
    e[-1] = __builtin_inff();
    e[M] = __builtin_inff();
    return p;
}
static void dir_row(const float *up, const float *e, const float *dn, const float *r, uint8_t *d, long M, float nv) {
    long j = 0;
    const long J = M - 1;
    const __m128 vnv = _mm_set1_ps(nv);
    const __m128i k15 = _mm_set1_epi32((int)NOD);
    for (; j + 16 <= J; j += 16) {
        __m128i c4[4];
        for (int u = 0; u < 4; u++) {
            const long q = j + 4 * u;
            __m128 mn = _mm_loadu_ps(e + q);
            __m128i code = _mm_setzero_si128();
#define SD(PTR, K)                                                                                                     \
    {                                                                                                                  \
        const __m128 x = _mm_loadu_ps(PTR);                                                                            \
        const __m128 c = _mm_cmplt_ps(x, mn);                                                                          \
        mn = _mm_min_ps(x, mn);                                                                                        \
        code = _mm_max_epi16(code, _mm_and_si128(_mm_castps_si128(c), _mm_set1_epi32(K)));                             \
    }
            SD(up + q, 1)
            SD(up + q + 1, 2) SD(e + q + 1, 3) SD(dn + q + 1, 4) SD(dn + q, 5) SD(dn + q - 1, 6) SD(e + q - 1, 7)
                SD(up + q - 1, 8)
#undef SD
                    const __m128i valid = _mm_castps_si128(_mm_cmpneq_ps(_mm_loadu_ps(r + q), vnv));
            c4[u] = _mm_or_si128(_mm_and_si128(valid, code), _mm_andnot_si128(valid, k15));
        }
        const __m128i a = _mm_packs_epi32(c4[0], c4[1]), b = _mm_packs_epi32(c4[2], c4[3]);
        _mm_storeu_si128((__m128i *)(d + j), _mm_packus_epi16(a, b));
    }
#define STEPD(x, kk)                                                                                                   \
    {                                                                                                                  \
        const float xx = (x);                                                                                          \
        const bool c = xx < mn;                                                                                        \
        mn = c ? xx : mn;                                                                                              \
        code = c ? (kk) : code;                                                                                        \
    }
    for (; j < J; j++) {
        float mn = e[j];
        int code = 0;
        STEPD(up[j], 1)
        STEPD(up[j + 1], 2) STEPD(e[j + 1], 3) STEPD(dn[j + 1], 4) STEPD(dn[j], 5) STEPD(dn[j - 1], 6)
            STEPD(e[j - 1], 7) STEPD(up[j - 1], 8) d[j] = r[j] != nv ? (uint8_t)code : (uint8_t)NOD;
    }
    {
        float mn = e[J];
        int code = 0;
        STEPD(dn[J], 5)
        STEPD(dn[J - 1], 6) STEPD(e[J - 1], 7) STEPD(up[J - 1], 8) STEPD(up[J], 1) d[J] =
            r[J] != nv ? (uint8_t)code : (uint8_t)NOD;
    }
#undef STEPD
}
static void indeg_row(uint8_t *D, long c0, long M) {
    long j = 0;
    const __m128i m0f = _mm_set1_epi8(0x0F);
    for (; j + 16 <= M; j += 16) {
        const long c = c0 + j;
        __m128i cnt = _mm_setzero_si128();
#define IG(OFF, K)                                                                                                     \
    cnt = _mm_sub_epi8(                                                                                                \
        cnt, _mm_cmpeq_epi8(_mm_and_si128(_mm_loadu_si128((const __m128i *)(D + c + (OFF))), m0f), _mm_set1_epi8(K)));
        IG(-M, 5)
        IG(-M + 1, 6) IG(1, 7) IG(M + 1, 8) IG(M, 1) IG(M - 1, 2) IG(-1, 3) IG(-M - 1, 4)
#undef IG
            const __m128i cur = _mm_loadu_si128((const __m128i *)(D + c));
        _mm_storeu_si128((__m128i *)(D + c), _mm_or_si128(cur, _mm_slli_epi16(cnt, 4)));
    }
    for (; j < M; j++) {
        const long c = c0 + j;
        const int k = ((D[c - M] & 15) == 5) + ((D[c - M + 1] & 15) == 6) + ((D[c + 1] & 15) == 7) +
                      ((D[c + M + 1] & 15) == 8) + ((D[c + M] & 15) == 1) + ((D[c + M - 1] & 15) == 2) +
                      ((D[c - 1] & 15) == 3) + ((D[c - M - 1] & 15) == 4);
        D[c] = (uint8_t)(D[c] | (k << 4));
    }
}
static inline unsigned dir_at(const uint8_t *D, long x) { return __atomic_load_n(&D[x], __ATOMIC_RELAXED) & 15u; }
static void indeg_row_edge(uint8_t *D, long c0, long M) {
    for (long j = 0; j < M; j++) {
        const long c = c0 + j;
        const unsigned k = (dir_at(D, c - M) == 5) + (dir_at(D, c - M + 1) == 6) + (dir_at(D, c + 1) == 7) +
                           (dir_at(D, c + M + 1) == 8) + (dir_at(D, c + M) == 1) + (dir_at(D, c + M - 1) == 2) +
                           (dir_at(D, c - 1) == 3) + (dir_at(D, c - M - 1) == 4);
        __atomic_store_n(&D[c], (uint8_t)(__atomic_load_n(&D[c], __ATOMIC_RELAXED) | (k << 4)), __ATOMIC_RELAXED);
    }
}
static inline void walk(uint8_t *D, uint32_t *A, long c, const long *off) {
    unsigned k = D[c] & 15u;
    uint32_t a = 1;
    Step st(off);
    while (k != 0) {
        const long x = st.next(c, k);
        const uint32_t ax = A[x] + a;
        A[x] = ax;
        const unsigned bx = D[x];
        if (bx >= 0x20u) {
            D[x] = (uint8_t)(bx - 0x10u);
            return;
        }
        D[x] = (uint8_t)(bx | 0xF0u);
        a = ax + 1;
        c = x;
        k = bx & 15u;
    }
}
static void walk_all(uint8_t *D, uint32_t *A, long V, const long *off) {
    const __m128i mf0 = _mm_set1_epi8((char)0xF0), m0f = _mm_set1_epi8(0x0F), k15 = _mm_set1_epi8((char)NOD);
    const __m128i zero = _mm_setzero_si128();
    long c0 = 0;
    for (; c0 + 16 <= V; c0 += 16) {
        const __m128i v = _mm_loadu_si128((const __m128i *)(D + c0));
        const __m128i src =
            _mm_andnot_si128(_mm_cmpeq_epi8(_mm_and_si128(v, m0f), k15), _mm_cmpeq_epi8(_mm_and_si128(v, mf0), zero));
        unsigned msk = (unsigned)_mm_movemask_epi8(src);
        while (msk) {
            const int b = __builtin_ctz(msk);
            msk &= msk - 1;
            walk(D, A, c0 + b, off);
        }
    }
    for (; c0 < V; c0++) {
        const unsigned b = D[c0];
        if ((b & 0xF0u) == 0 && (b & 15u) != NOD)
            walk(D, A, c0, off);
    }
}
static inline uint32_t label_of(const uint8_t *D, uint32_t *A, long c, const long *off, uint32_t *num) {
    const unsigned k = D[c] & 15u;
    if (k == 0) {
        const uint32_t L = ++*num;
        A[c] = L;
        return L;
    }
    Step st(off);
    uint32_t L = A[st.next(c, k)];
    if (L) {
        A[c] = L;
        return L;
    }
    long x = c;
    for (;;) {
        L = A[x];
        if (L)
            break;
        const unsigned kx = D[x] & 15u;
        if (kx == 0) {
            L = ++*num;
            A[x] = L;
            break;
        }
        x = st.next(x, kx);
    }
    x = c;
    while (A[x] == 0) {
        A[x] = L;
        x = st.next(x, D[x] & 15u);
    }
    return L;
}
template <bool S> static char *fmt_terrain_row(char *q, const float *r, long M) {
    for (long j = 0; j < M;) {
        const long b = M - j < 2048 ? M - j : 2048;
        q = ensure<S>(q, (size_t)b * 16 + 32);
        for (const long e = j + b; j < e; j++) {
            q = g6(q, r[j]);
            *q++ = ' ';
        }
    }
    q = ensure<S>(q, 1);
    *q++ = '\n';
    return q;
}
template <bool S> static char *fmt_dir_row(char *q, const uint8_t *r, long M) {
    const __m128i m0f = _mm_set1_epi8(0x0F), k15 = _mm_set1_epi8((char)NOD);
    const __m128i z0 = _mm_set1_epi8('0'), spc = _mm_set1_epi8(' ');
    for (long j = 0; j < M;) {
        const long b = M - j < 2048 ? M - j : 2048;
        q = ensure<S>(q, (size_t)b * 3 + 32);
        const long e = j + b;
        for (; j + 16 <= e; j += 16) {
            const __m128i v = _mm_and_si128(_mm_loadu_si128((const __m128i *)(r + j)), m0f);
            if (_mm_movemask_epi8(_mm_cmpeq_epi8(v, k15))) {
                for (int k = 0; k < 16; k++) {
                    const unsigned d = r[j + k] & 15u;
                    if (d != NOD) {
                        q[0] = (char)('0' + d);
                        q[1] = ' ';
                        q += 2;
                    } else {
                        q[0] = '-';
                        q[1] = '9';
                        q[2] = ' ';
                        q += 3;
                    }
                }
                continue;
            }
            const __m128i c = _mm_add_epi8(v, z0);
            _mm_storeu_si128((__m128i *)q, _mm_unpacklo_epi8(c, spc));
            _mm_storeu_si128((__m128i *)(q + 16), _mm_unpackhi_epi8(c, spc));
            q += 32;
        }
        for (; j < e; j++) {
            const unsigned d = r[j] & 15u;
            if (d != NOD) {
                q[0] = (char)('0' + d);
                q[1] = ' ';
                q += 2;
            } else {
                q[0] = '-';
                q[1] = '9';
                q[2] = ' ';
                q += 3;
            }
        }
    }
    q = ensure<S>(q, 1);
    *q++ = '\n';
    return q;
}
template <bool S> static char *fmt_acc_row(char *q, const uint8_t *d, uint32_t *a, long M) {
    for (long j = 0; j < M;) {
        const long b = M - j < 2048 ? M - j : 2048;
        q = ensure<S>(q, (size_t)b * 12 + 32);
        for (const long e = j + b; j < e; j++) {
            if ((d[j] & 15u) == NOD) {
                q[0] = '-';
                q[1] = '1';
                q[2] = ' ';
                q += 3;
            } else {
                q = pu32(q, a[j] + 1);
                *q++ = ' ';
            }
            a[j] = 0;
        }
    }
    q = ensure<S>(q, 1);
    *q++ = '\n';
    return q;
}
struct LabCache {
    uint32_t llab;
    int llen;
    char lbuf[16];
};
template <bool S, bool RES>
static char *fmt_ws_row(char *q, const uint8_t *D, uint32_t *A, long r0, long M, const long *off, uint32_t *num,
                        LabCache *lc) {
    uint32_t llab = lc->llab;
    int llen = lc->llen;
    for (long j = 0; j < M;) {
        const long b = M - j < 2048 ? M - j : 2048;
        q = ensure<S>(q, (size_t)b * 12 + 32);
        for (const long e = j + b; j < e; j++) {
            const long c = r0 + j;
            if ((D[c] & 15u) == NOD) {
                q[0] = '-';
                q[1] = '1';
                q[2] = ' ';
                q += 3;
                continue;
            }
            uint32_t L = A[c];
            if (RES && !L)
                L = label_of(D, A, c, off, num);
            if (L != llab) {
                llab = L;
                llen = (int)(pu32(lc->lbuf, L) - lc->lbuf);
                lc->lbuf[llen] = ' ';
            }
            memcpy(q, lc->lbuf, 16);
            q += llen + 1;
        }
    }
    lc->llab = llab;
    lc->llen = llen;
    q = ensure<S>(q, 1);
    *q++ = '\n';
    return q;
}
struct Grid {
    const char *text;
    const char *end;
    int n;
    long M, V;
    float nv;
    uint8_t *D;
    uint32_t *A;
    float *rows;
    long off[9];
};
static Grid g;
static float *g_trows[TMAXP];
static float *row_slot(int t, int s) { return (g_trows[t] ? g_trows[t] : g.rows) + (size_t)s * ((size_t)g.M + 8) + 4; }
static float *ghost_e(int t) {
    float *e = row_slot(t, 7);
    for (long j = -1; j <= g.M; j++)
        e[j] = __builtin_inff();
    return e;
}
static void run_serial() {
    const long M = g.M, V = g.V;
    const int n = g.n;
    const float nv = g.nv;
    uint8_t *const D = g.D;
    uint32_t *const A = g.A;
    const char *p = g.text;
    const char *const end = g.end;
    int bad = 0;
    float *r[3] = {row_slot(0, 0), row_slot(0, 1), row_slot(0, 2)};
    float *e[3] = {row_slot(0, 3), row_slot(0, 4), row_slot(0, 5)};
    float *const ge = ghost_e(0);
    char *q = g_ob;
    q = put_str<true>(q, "the terrain\n");
    {
        const float *pe = ge;
        int cur = 0;
        p = parse_row(p, end, r[0], e[0], M, nv, &bad);
        q = fmt_terrain_row<true>(q, r[0], M);
        for (long i = 0; i < n; i++) {
            const int nx = cur == 2 ? 0 : cur + 1;
            const float *ne;
            if (i + 1 < n) {
                p = parse_row(p, end, r[nx], e[nx], M, nv, &bad);
                q = fmt_terrain_row<true>(q, r[nx], M);
                ne = e[nx];
            } else
                ne = ge;
            dir_row(pe, e[cur], ne, r[cur], D + i * M, M, nv);
            if (i >= 1)
                indeg_row(D, (i - 1) * M, M);
            pe = e[cur];
            cur = nx;
        }
        indeg_row(D, (long)(n - 1) * M, M);
    }
    PM("p1");
    q = put_str<true>(q, "the directions\n");
    q = put_ghost<true>(q, "1 ", M);
    for (long i = 0; i < n; i++)
        q = fmt_dir_row<true>(q, D + i * M, M);
    q = put_ghost<true>(q, "5 ", M);
    PM("fmtD");
    walk_all(D, A, V, g.off);
    PM("walk");
    q = put_str<true>(q, "flow accumulation \n");
    q = put_ghost<true>(q, "0 ", M);
    for (long i = 0; i < n; i++)
        q = fmt_acc_row<true>(q, D + i * M, A + i * M, M);
    q = put_ghost<true>(q, "0 ", M);
    PM("fmtA");
    q = put_str<true>(q, "watersheds\n");
    q = put_ghost<true>(q, "-1 ", M);
    {
        uint32_t num = 100;
        LabCache lc;
        lc.llab = 0;
        lc.llen = 0;
        memset(lc.lbuf, 0, sizeof lc.lbuf);
        for (long i = 0; i < n; i++)
            q = fmt_ws_row<true, true>(q, D, A, i * M, M, g.off, &num, &lc);
    }
    q = put_ghost<true>(q, "-1 ", M);
    write_all(1, g_ob, (size_t)(q - g_ob));
    PM("fmtW");
}
static int g_T;
struct alignas(64) Slot {
    char *buf;
    size_t len;
    int ready;
    int written;
};
static Slot g_slot[TMAXP];
static long g_cnt[TMAXP];
static const char *g_cut[TMAXP + 1];
static int g_bad;
struct alignas(64) Bar {
    int cnt;
    char pad[60];
    int gen;
};
static Bar g_bar;
static pthread_t g_th[TMAXP];
static char *g_gh[2];
#ifndef SPIN
#define SPIN 4000
#endif
static int g_sleep;
static inline void futex_wait(const int *a, int v) { syscall(SYS_futex, a, 128, v, 0, 0, 0); }
static inline void wake_all(const int *a) {
    if (__atomic_load_n(&g_sleep, __ATOMIC_SEQ_CST))
        syscall(SYS_futex, a, 129, 0x7fffffff, 0, 0, 0);
}
static void spin_until_ge(const int *x, int v) {
    for (unsigned s = 0; __atomic_load_n(x, __ATOMIC_ACQUIRE) < v; s++) {
        _mm_pause();
        if (s >= SPIN) {
            __atomic_add_fetch(&g_sleep, 1, __ATOMIC_SEQ_CST);
            for (int c; (c = __atomic_load_n(x, __ATOMIC_SEQ_CST)) < v;)
                futex_wait(x, c);
            __atomic_sub_fetch(&g_sleep, 1, __ATOMIC_SEQ_CST);
            return;
        }
    }
}
static inline void store_wake(int *x, int v) {
    __atomic_store_n(x, v, __ATOMIC_SEQ_CST);
    wake_all(x);
}
static void barrier() {
    const int gen = __atomic_load_n(&g_bar.gen, __ATOMIC_ACQUIRE);
    if (__atomic_add_fetch(&g_bar.cnt, 1, __ATOMIC_ACQ_REL) == g_T) {
        __atomic_store_n(&g_bar.cnt, 0, __ATOMIC_RELAXED);
        store_wake(&g_bar.gen, gen + 1);
        return;
    }
    spin_until_ge(&g_bar.gen, gen + 1);
}
static void exec_par(const char *data, size_t len, bool from_file);
static const char *g_in;
static size_t g_inlen;
static bool g_from_file;
static char *g_mem;
static size_t g_memlen;
static char *g_inmap;
static size_t g_inmaplen;
static void band_ready(Slot &s, char *q) {
    s.len = (size_t)(q - s.buf);
    store_wake(&s.ready, s.ready + 1);
}
static void write_section(int sec, const char *hdr, const char *gtop, size_t gtl, const char *gbot, size_t gbl) {
    struct iovec iov[TMAXP + 3];
    int c = 0;
    iov[c].iov_base = (void *)hdr;
    iov[c].iov_len = strlen(hdr);
    c++;
    if (gtop) {
        iov[c].iov_base = (void *)gtop;
        iov[c].iov_len = gtl;
        c++;
    }
    for (int t = 0; t < g_T; t++) {
        spin_until_ge(&g_slot[t].ready, sec + 1);
        iov[c].iov_base = g_slot[t].buf;
        iov[c].iov_len = g_slot[t].len;
        c++;
    }
    if (gbot) {
        iov[c].iov_base = (void *)gbot;
        iov[c].iov_len = gbl;
        c++;
    }
    writev_all(iov, c);
    for (int t = 0; t < g_T; t++)
        store_wake(&g_slot[t].written, sec + 1);
}
static size_t ghost_rows(int k, const char *tok, long M) {
    const size_t need = (size_t)M * 3 + 2;
    if (!g_gh[k])
        g_gh[k] = 2 * need <= (size_t)OUT_CAP ? g_ob + (size_t)k * need : (char *)malloc(need);
    if (!g_gh[k]) {
        perror("malloc");
        exit(1);
    }
    return (size_t)(put_ghost<false>(g_gh[k], tok, M) - g_gh[k]);
}
static inline void walk_par(uint8_t *D, uint32_t *A, long c, const long *off) {
    unsigned k = D[c] & 15u;
    uint32_t a = 1;
    Step st(off);
    while (k != 0) {
        const long x = st.next(c, k);
        unsigned cur = __atomic_load_n(&D[x], __ATOMIC_ACQUIRE);
        if ((cur >> 4) == 1) {
            const uint32_t ax = __atomic_load_n(&A[x], __ATOMIC_RELAXED) + a;
            __atomic_store_n(&A[x], ax, __ATOMIC_RELAXED);
            __atomic_store_n(&D[x], (uint8_t)(cur | 0xF0u), __ATOMIC_RELAXED);
            a = ax + 1;
            c = x;
            k = cur & 15u;
            continue;
        }
        __atomic_fetch_add(&A[x], a, __ATOMIC_RELAXED);
        for (;;) {
            uint8_t ex = (uint8_t)cur;
            const uint8_t nw = (uint8_t)((cur >> 4) == 1 ? (cur | 0xF0u) : cur - 0x10u);
            if (__atomic_compare_exchange_n(&D[x], &ex, nw, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                break;
            cur = ex;
        }
        if ((cur >> 4) != 1)
            return;
        a = __atomic_load_n(&A[x], __ATOMIC_RELAXED) + 1;
        c = x;
        k = cur & 15u;
    }
}
static long g_nsrc[TMAXP];
static inline unsigned src_mask16(const uint8_t *p) {
    const __m128i v = _mm_loadu_si128((const __m128i *)p);
    const __m128i src =
        _mm_andnot_si128(_mm_cmpeq_epi8(_mm_and_si128(v, _mm_set1_epi8(0x0F)), _mm_set1_epi8((char)NOD)),
                         _mm_cmpeq_epi8(_mm_and_si128(v, _mm_set1_epi8((char)0xF0)), _mm_setzero_si128()));
    return (unsigned)_mm_movemask_epi8(src);
}
static uint64_t *g_srcb[TMAXP];
static long src_bits(const uint8_t *D, long c0, long c1, uint64_t *bits) {
    const long L = c1 - c0;
    long k = 0, i = 0, w = 0;
    for (; i + 64 <= L; i += 64, w++) {
        const uint8_t *d = D + c0 + i;
        const uint64_t x = (uint64_t)src_mask16(d) | ((uint64_t)src_mask16(d + 16) << 16) |
                           ((uint64_t)src_mask16(d + 32) << 32) | ((uint64_t)src_mask16(d + 48) << 48);
        bits[w] = x;
        k += __builtin_popcountll(x);
    }
    if (i < L) {
        uint64_t x = 0;
        for (long j = 0; i + j < L; j++) {
            const unsigned d = D[c0 + i + j];
            if ((d & 0xF0u) == 0 && (d & 15u) != NOD)
                x |= (uint64_t)1 << j;
        }
        bits[w] = x;
        k += __builtin_popcountll(x);
    }
    return k;
}
static void walk_share(int t) {
    uint8_t *const D = g.D;
    uint32_t *const A = g.A;
    const long *const off = g.off;
    const int T = g_T;
    const int n = g.n;
    const long M = g.M;
    long S = 0;
    for (int k = 0; k < T; k++)
        S += g_nsrc[k];
    const long k0 = S * t / T, k1 = S * (t + 1) / T;
    if (k0 >= k1)
        return;
    int b = 0;
    long pre = 0;
    while (b < T - 1 && pre + g_nsrc[b] <= k0) {
        pre += g_nsrc[b];
        b++;
    }
    long skip = k0 - pre, left = k1 - k0;
    for (; b < T && left > 0; b++) {
        const long c0 = (long)n * b / T * M, c1 = (long)n * (b + 1) / T * M, nw = (c1 - c0 + 63) / 64;
        const uint64_t *const bits = g_srcb[b];
        for (long w = 0; w < nw && left > 0; w++) {
            uint64_t x = bits[w];
            const long pc = __builtin_popcountll(x);
            if (pc <= skip) {
                skip -= pc;
                continue;
            }
            while (skip > 0) {
                x &= x - 1;
                skip--;
            }
            while (x && left > 0) {
                const int i = __builtin_ctzll(x);
                x &= x - 1;
                walk_par(D, A, c0 + w * 64 + i, off);
                left--;
            }
        }
    }
}
static const uint32_t HIGH = 0x80000000u;
static long *g_exits[TMAXP];
static char *g_reg;
static size_t g_regsz, g_rowsz, g_exitsz, g_fminsz, g_srcbsz;
#if REL_END
static void release_share(int t) {
    const int T = g_T;
    const size_t pg = 4096;
    if (g_mem) {
        const size_t a = g_memlen * t / T / pg * pg, b = t + 1 == T ? g_memlen : g_memlen * (t + 1) / T / pg * pg;
        if (b > a)
            madvise(g_mem + a, b - a, MADV_DONTNEED);
    }
    madvise(g_reg + (size_t)t * g_regsz, g_regsz, MADV_DONTNEED);
    if (g_inmap) {
        const size_t a = g_inmaplen * t / T / pg * pg, b = t + 1 == T ? g_inmaplen : g_inmaplen * (t + 1) / T / pg * pg;
        if (b > a)
            madvise(g_inmap + a, b - a, MADV_DONTNEED);
    }
}
#endif
static long g_nexit[TMAXP], g_nsink[TMAXP], g_nfirst[TMAXP];
static uint32_t *g_fminb[TMAXP];
static inline uint32_t *fmin_of(uint32_t s) { return g_fminb[s >> 25] + (s & 0x1FFFFFFu); }
static void local_roots(int t, long c0, long c1) {
    uint8_t *const D = g.D;
    uint32_t *const A = g.A;
    Step st(g.off);
    long ne = 0, ns = 0;
    long *ex = g_exits[t];
    for (long c = c0; c < c1; c++) {
        const unsigned kc = D[c] & 15u;
        if (kc == NOD)
            continue;
        if (kc == 0) {
            ns++;
            A[c] = (uint32_t)c + 1;
            continue;
        }
        if (A[c])
            continue;
        long x = c;
        uint32_t R;
        for (;;) {
            if (x < c0 || x >= c1) {
                R = (uint32_t)x + 1;
                ex[ne++] = x;
                break;
            }
            const uint32_t ax = A[x];
            if (ax) {
                R = ax;
                break;
            }
            const unsigned kx = D[x] & 15u;
            if (kx == 0) {
                R = (uint32_t)x + 1;
                A[x] = R;
                break;
            }
            x = st.next(x, kx);
        }
        for (long y = c; y >= c0 && y < c1 && A[y] == 0; y = st.next(y, D[y] & 15u))
            A[y] = R;
    }
    g_nexit[t] = ne;
    g_nsink[t] = ns;
}
static void resolve_exits() {
    uint8_t *const D = g.D;
    uint32_t *const A = g.A;
    for (int t = 0; t < g_T; t++)
        for (long i = 0; i < g_nexit[t]; i++) {
            const long x = g_exits[t][i];
            if ((D[x] & 15u) == 0)
                continue;
            long y = x;
            while ((D[y] & 15u) != 0)
                y = (long)A[y] - 1;
            A[x] = (uint32_t)y + 1;
        }
}
static const uint32_t DIRECT = 0x40000000u;
static void number_sinks(int t, long c0, long c1) {
    uint8_t *const D = g.D;
    uint32_t *const A = g.A;
    uint32_t s = (uint32_t)t << 25;
    for (long c = c0; c < c1; c++)
        if ((D[c] & 15u) == 0)
            __atomic_store_n(&A[c], D[c] == 0 ? HIGH | DIRECT : HIGH | s++, __ATOMIC_RELAXED);
}
static inline uint32_t sink_number(const uint32_t *A, long c) {
    uint32_t x = __atomic_load_n(&A[c], __ATOMIC_RELAXED);
    while (!(x & HIGH))
        x = __atomic_load_n(&A[x - 1], __ATOMIC_RELAXED);
    return x & ~HIGH;
}
static void basin_min(long c0, long c1) {
    uint8_t *const D = g.D;
    uint32_t *const A = g.A;
    const uint32_t V = (uint32_t)g.V;
    uint32_t last = HIGH;
    for (long c = c0; c < c1; c++) {
        const unsigned d = D[c];
        if ((d & 15u) == NOD || d == 0)
            continue;
        const uint32_t s = sink_number(A, c);
        __atomic_store_n(&A[c], HIGH | s, __ATOMIC_RELAXED);
        if (s == last)
            continue;
        last = s;
        const uint32_t v = V - (uint32_t)c;
        uint32_t *const f = fmin_of(s);
        uint32_t cur = __atomic_load_n(f, __ATOMIC_RELAXED);
        while (v > cur && !__atomic_compare_exchange_n(f, &cur, v, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
        }
    }
}
static void count_firsts(int t, long c0, long c1) {
    uint8_t *const D = g.D;
    const uint32_t *const A = g.A;
    const uint32_t V = (uint32_t)g.V;
    long f = 0;
    for (long c = c0; c < c1; c++) {
        const unsigned d = D[c];
        if ((d & 15u) == NOD)
            continue;
        f += d == 0 || *fmin_of(A[c] & ~HIGH) == V - (uint32_t)c;
    }
    g_nfirst[t] = f;
}
static void assign_labels(int t, long c0, long c1) {
    uint8_t *const D = g.D;
    uint32_t *const A = g.A;
    const uint32_t V = (uint32_t)g.V;
    uint32_t lab = 100;
    for (int k = 0; k < t; k++)
        lab += (uint32_t)g_nfirst[k];
    for (long c = c0; c < c1; c++) {
        const unsigned d = D[c];
        if ((d & 15u) == NOD)
            continue;
        if (d == 0) {
            A[c] = HIGH | DIRECT | ++lab;
            continue;
        }
        uint32_t *const fp = fmin_of(A[c] & ~HIGH);
        if (__atomic_load_n(fp, __ATOMIC_RELAXED) == V - (uint32_t)c)
            __atomic_store_n(fp, HIGH | ++lab, __ATOMIC_RELAXED);
    }
}
static char *fmt_ws_row_par(char *q, const uint8_t *D, const uint32_t *A, long r0, long M, LabCache *lc) {
    uint32_t llab = lc->llab;
    int llen = lc->llen;
    for (long j = 0; j < M; j++) {
        const long c = r0 + j;
        if ((D[c] & 15u) == NOD) {
            q[0] = '-';
            q[1] = '1';
            q[2] = ' ';
            q += 3;
            continue;
        }
        const uint32_t x = A[c];
        const uint32_t L = (x & DIRECT) ? x & ~(HIGH | DIRECT) : *fmin_of(x & ~HIGH) & ~HIGH;
        if (L != llab) {
            llab = L;
            llen = (int)(pu32(lc->lbuf, L) - lc->lbuf);
            lc->lbuf[llen] = ' ';
        }
        memcpy(q, lc->lbuf, 16);
        q += llen + 1;
    }
    lc->llab = llab;
    lc->llen = llen;
    *q++ = '\n';
    return q;
}
static void *band_main(void *arg);
#ifndef PIN
#define PIN 1
#endif
#ifndef STACK
#define STACK (256 * 1024)
#endif
#if PIN
static int g_cpu[TMAXP];
#endif
static void spawn_children(int t) {
    for (int c = 2 * t + 1; c <= 2 * t + 2 && c < g_T; c++) {
        pthread_attr_t at;
        pthread_attr_init(&at);
        pthread_attr_setstacksize(&at, (size_t)STACK);
#if PIN
        cpu_set_t cs;
        CPU_ZERO(&cs);
        CPU_SET(g_cpu[c], &cs);
        pthread_attr_setaffinity_np(&at, sizeof cs, &cs);
#endif
        if (pthread_create(&g_th[c], &at, band_main, (void *)(intptr_t)c) != 0) {
            perror("pthread_create");
            exit(1);
        }
        pthread_attr_destroy(&at);
    }
}
#if PROF
static double g_tt[TMAXP][32];
static int g_ntt[TMAXP];
#define TT()                                                                                                           \
    do {                                                                                                               \
        tt[ntt++] = ptime();                                                                                           \
    } while (0)
#else
#define TT()                                                                                                           \
    do {                                                                                                               \
    } while (0)
#endif
static void band_work(int t) {
#if PROF
    double tt[32];
    int ntt = 0;
#endif
    TT();
    spawn_children(t);
    TT();
    const int T = g_T, n = g.n;
    const long M = g.M;
    const float nv = g.nv;
    uint8_t *const D = g.D;
    uint32_t *const A = g.A;
    Slot &sl = g_slot[t];
    const long r0 = (long)n * t / T, r1 = (long)n * (t + 1) / T;
    {
        char *rg = g_reg + (size_t)t * g_regsz;
        g_trows[t] = (float *)rg;
        rg += g_rowsz;
        g_exits[t] = (long *)rg;
        rg += g_exitsz;
        g_fminb[t] = (uint32_t *)rg;
        rg += g_fminsz;
        g_srcb[t] = (uint64_t *)rg;
        rg += g_srcbsz;
        sl.buf = rg;
    }
    g_cnt[t] = count_tokens(g_cut[t], g_cut[t + 1]);
    TT();
    barrier();
    TT();
    const char *p = g.end;
    {
        const long want = (r0 > 0 ? r0 - 1 : 0) * M;
        long pre = 0;
        for (int k = 0; k < T; k++) {
            if (want < pre + g_cnt[k]) {
                p = skip_tokens(g_cut[k], g_cut[k + 1], want - pre);
                break;
            }
            pre += g_cnt[k];
        }
    }
    {
        int bad = 0;
        float *r[3] = {row_slot(t, 0), row_slot(t, 1), row_slot(t, 2)};
        float *e[3] = {row_slot(t, 3), row_slot(t, 4), row_slot(t, 5)};
        float *const ge = ghost_e(t);
        char *q = sl.buf;
        const float *pe = ge;
        int cur = 0;
        if (r0 > 0) {
            p = parse_row(p, g.end, r[2], e[2], M, nv, &bad);
            pe = e[2];
        }
        p = parse_row(p, g.end, r[0], e[0], M, nv, &bad);
        q = fmt_terrain_row<false>(q, r[0], M);
        for (long i = r0; i < r1; i++) {
            const int nx = cur == 2 ? 0 : cur + 1;
            const float *ne;
            if (i + 1 < n) {
                p = parse_row(p, g.end, r[nx], e[nx], M, nv, &bad);
                if (i + 1 < r1)
                    q = fmt_terrain_row<false>(q, r[nx], M);
                ne = e[nx];
            } else
                ne = ge;
            dir_row(pe, e[cur], ne, r[cur], D + i * M, M, nv);
            pe = e[cur];
            cur = nx;
        }
        if (bad)
            __atomic_store_n(&g_bad, 1, __ATOMIC_RELAXED);
        band_ready(sl, q);
    }
    TT();
    if (t == 0)
        PM("p1");
    barrier();
    TT();
    if (t == 0) {
        if (__atomic_load_n(&g_bad, __ATOMIC_RELAXED))
            exec_par(g_in, g_inlen, g_from_file);
        write_section(0, "the terrain\n", 0, 0, 0, 0);
        PM("wT");
    }
    for (long i = r0; i < r1; i++) {
        if (i == r0 || i == r1 - 1)
            indeg_row_edge(D, i * M, M);
        else
            indeg_row(D, i * M, M);
    }
    g_nsrc[t] = src_bits(D, r0 * M, r1 * M, g_srcb[t]);
#if PREFAULT
    {
        const uintptr_t lo = (uintptr_t)(A + r0 * M), hi = (uintptr_t)(A + r1 * M);
        for (uintptr_t a = (lo + 4095) & ~(uintptr_t)4095; a < hi; a += 4096)
            *(volatile uint32_t *)a = 0;
    }
#endif
    TT();
    barrier();
    TT();
    if (t == 0)
        PM("indeg");
    walk_share(t);
    TT();
    barrier();
    TT();
    if (t == 0)
        PM("walk");
    {
        char *q = sl.buf;
        for (long i = r0; i < r1; i++)
            q = fmt_dir_row<false>(q, D + i * M, M);
        band_ready(sl, q);
    }
    if (t == 0) {
        const size_t g1 = ghost_rows(0, "1 ", M), g5 = ghost_rows(1, "5 ", M);
        write_section(1, "the directions\n", g_gh[0], g1, g_gh[1], g5);
        PM("wD");
    }
    {
        spin_until_ge(&sl.written, 2);
        char *q = sl.buf;
        for (long i = r0; i < r1; i++)
            q = fmt_acc_row<false>(q, D + i * M, A + i * M, M);
        band_ready(sl, q);
    }
    TT();
    local_roots(t, r0 * M, r1 * M);
    TT();
    barrier();
    if (t == 0) {
        PM("fmtA+L1");
        const size_t g0 = ghost_rows(0, "0 ", M);
        write_section(2, "flow accumulation \n", g_gh[0], g0, g_gh[0], g0);
        PM("wA");
        resolve_exits();
        PM("L2");
    }
    TT();
    number_sinks(t, r0 * M, r1 * M);
    TT();
    barrier();
    TT();
    basin_min(r0 * M, r1 * M);
    TT();
    barrier();
    TT();
    count_firsts(t, r0 * M, r1 * M);
    TT();
    barrier();
    TT();
    assign_labels(t, r0 * M, r1 * M);
    TT();
    barrier();
    TT();
    if (t == 0)
        PM("lab");
    {
        spin_until_ge(&sl.written, 3);
        char *q = sl.buf;
        LabCache lc;
        lc.llab = 0;
        lc.llen = 0;
        memset(lc.lbuf, 0, sizeof lc.lbuf);
        for (long i = r0; i < r1; i++)
            q = fmt_ws_row_par(q, D, A, i * M, M, &lc);
        TT();
#if PROF
        memcpy(g_tt[t], tt, sizeof tt);
        g_ntt[t] = ntt;
#endif
        band_ready(sl, q);
    }
    if (t == 0) {
        const size_t gm = ghost_rows(0, "-1 ", M);
        write_section(3, "watersheds\n", g_gh[0], gm, g_gh[0], gm);
        PM("wW");
    }
#if REL_END
    barrier();
    release_share(t);
    barrier();
    if (t == 0)
        PM("rel");
#endif
}
static void *band_main(void *arg) {
    band_work((int)(intptr_t)arg);
    return 0;
}
static char **g_argv;
static cpu_set_t g_mask0;
static bool g_mask0_ok;
static void exec_par(const char *data, size_t len, bool from_file) {
    char path[4096];
    const char *a0 = g_argv && g_argv[0] ? g_argv[0] : "";
    const char *sl = strrchr(a0, '/');
    size_t dl = sl ? (size_t)(sl - a0 + 1) : 0;
    if (dl + 8 >= sizeof path)
        dl = 0;
    if (dl)
        memcpy(path, a0, dl);
    else {
        memcpy(path, "./", 2);
        dl = 2;
    }
    memcpy(path + dl, "dtm_par", 8);
    if (access(path, X_OK) != 0)
        return;
    if (!from_file) {
        const int fd = (int)syscall(SYS_memfd_create, "dtm_in", 0);
        if (fd < 0)
            return;
        write_all(fd, data, len);
        if (lseek(fd, 0, SEEK_SET) != 0 || dup2(fd, 0) < 0) {
            close(fd);
            return;
        }
        close(fd);
    }
    static const char *const unset_vars[] = {"OMP_NUM_THREADS",
                                             "OMP_THREAD_LIMIT",
                                             "OMP_DYNAMIC",
                                             "OMP_PROC_BIND",
                                             "OMP_PLACES",
                                             "OMP_WAIT_POLICY",
                                             "OMP_STACKSIZE",
                                             "OMP_SCHEDULE",
                                             "OMP_MAX_ACTIVE_LEVELS",
                                             "OMP_NESTED",
                                             "OMP_CANCELLATION",
                                             "OMP_DEFAULT_DEVICE",
                                             "OMP_MAX_TASK_PRIORITY",
                                             "OMP_TARGET_OFFLOAD",
                                             "OMP_DISPLAY_ENV",
                                             "OMP_DISPLAY_AFFINITY",
                                             "OMP_AFFINITY_FORMAT",
                                             "GOMP_CPU_AFFINITY",
                                             "GOMP_SPINCOUNT",
                                             "GOMP_STACKSIZE",
                                             "GOMP_DEBUG",
                                             "GOMP_RTEMS_THREAD_POOLS"};
    for (size_t k = 0; k < sizeof unset_vars / sizeof unset_vars[0]; k++)
        unsetenv(unset_vars[k]);
    if (g_mask0_ok)
        sched_setaffinity(0, sizeof g_mask0, &g_mask0);
    execv(path, g_argv);
}
int main(int argc, char **argv) {
    (void)argc;
    g_argv = argv;
    g_mask0_ok = sched_getaffinity(0, sizeof g_mask0, &g_mask0) == 0;
    PM("start");
    const char *buf = 0;
    size_t len = 0;
    bool from_file = false;
    {
        struct stat st;
        if (fstat(0, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
            const off_t cur = lseek(0, 0, SEEK_CUR);
            if (cur >= 0 && cur < st.st_size) {
                const size_t fs = (size_t)st.st_size;
                const size_t pg = 4096;
                const size_t rl = (fs + pg - 1) / pg * pg + pg;
                void *r = mmap(0, rl, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                if (r != MAP_FAILED) {
                    void *f = mmap(
                        r, fs, PROT_READ,
                        MAP_PRIVATE | MAP_FIXED | ((POPULATE && fs <= ((size_t)256 << 10)) ? MAP_POPULATE : 0), 0, 0);
                    if (f == r) {
                        buf = (const char *)f + cur;
                        len = fs - (size_t)cur;
                        from_file = true;
                        g_inmap = (char *)r;
                        g_inmaplen = rl;
                    } else
                        munmap(r, rl);
                }
            }
        }
        if (!buf) {
            size_t cap = (size_t)1 << 16, nb = 0;
            char *o = (char *)malloc(cap + 1);
            if (!o) {
                perror("malloc");
                exit(1);
            }
            for (;;) {
                if (nb == cap) {
                    cap *= 2;
                    o = (char *)realloc(o, cap + 1);
                    if (!o) {
                        perror("realloc");
                        exit(1);
                    }
                }
                const ssize_t rr = read(0, o + nb, cap - nb);
                if (rr < 0) {
                    if (errno == EINTR)
                        continue;
                    perror("read");
                    exit(1);
                }
                if (rr == 0)
                    break;
                nb += (size_t)rr;
            }
            o[nb] = '\0';
            buf = o;
            len = nb;
        }
    }
    g_in = buf;
    g_inlen = len;
    g_from_file = from_file;
    PM("read");
    const char *p = buf;
    const char *const end = buf + len;
    long hv[5];
    for (int k = 0; k < 5; k++) {
        while (p < end && fio::is_ws((unsigned char)*p))
            ++p;
        char *e;
        hv[k] = strtol(p, &e, 10);
        p = e;
    }
    const int n = (int)hv[0], m = (int)hv[1];
    float nv;
    while (p < end && fio::is_ws((unsigned char)*p))
        ++p;
    p = fio::parse_float(p, &nv);
    const long small_max = SMALL_MAX, cpt = CPT;
    const long V = (long)n * m;
    if (n < 1 || m < 2 || V > small_max) {
        exec_par(buf, len, from_file);
        if (n < 1 || m < 2)
            return 1;
    }
    const long M = m;
    long tl = (V + cpt - 1) / cpt;
    if (tl > TMAXP)
        tl = TMAXP;
    if (tl > n)
        tl = n;
    int T = tl < 1 ? 1 : (int)tl;
    if (T > 1) {
        cpu_set_t cs;
        if (sched_getaffinity(0, sizeof cs, &cs) == 0) {
            const int c = CPU_COUNT(&cs);
            if (T > c)
                T = c;
#if PIN
            const int me = sched_getcpu();
            int k = 0;
            for (int pass = 0; pass < 2 && k < T; pass++)
                for (int cpu = 0; cpu < CPU_SETSIZE && k < T; cpu++)
                    if (CPU_ISSET(cpu, &cs) && (pass == 0 ? cpu >= me : cpu < me))
                        g_cpu[k++] = cpu;
            if (k < T)
                T = k;
            cpu_set_t one;
            CPU_ZERO(&one);
            CPU_SET(g_cpu[0], &one);
            sched_setaffinity(0, sizeof one, &one);
#endif
        } else
            T = 1;
        if (T < 1)
            T = 1;
    }
    g_T = T;
    PM("hdr");
    const size_t dpad = (size_t)M + 64;
    const size_t dbytes = ((size_t)V + 2 * dpad + 4095) & ~(size_t)4095;
    const size_t abytes = ((size_t)V * 4 + 4095) & ~(size_t)4095;
    const size_t rbytes = T == 1 ? ((size_t)8 * ((size_t)M + 8) * 4 + 4095) & ~(size_t)4095 : 0;
    const size_t total = dbytes + abytes + rbytes;
    char *mem;
    if (HUGE && T > 1 && total >= (size_t)HUGE_MIN) {
        const size_t H = (size_t)2 << 20, tl2 = (total + H - 1) & ~(H - 1);
        char *r = (char *)mmap(0, tl2 + H, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (r == MAP_FAILED) {
            perror("mmap");
            exit(1);
        }
        mem = (char *)(((uintptr_t)r + H - 1) & ~(uintptr_t)(H - 1));
#ifdef MADV_HUGEPAGE
        madvise(mem, tl2, MADV_HUGEPAGE);
#endif
    } else {
        mem = (char *)mmap(0, total, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS | ((POPULATE && T == 1) ? MAP_POPULATE : MAP_NORESERVE), -1, 0);
        if (mem == MAP_FAILED) {
            perror("mmap");
            exit(1);
        }
#ifdef MADV_NOHUGEPAGE
        if (T > 1)
            madvise(mem, total, MADV_NOHUGEPAGE);
#endif
    }
    g_mem = mem;
    g_memlen = total;
    g.text = p;
    g.end = end;
    g.n = n;
    g.M = M;
    g.V = V;
    g.nv = nv;
    g.D = (uint8_t *)mem + dpad;
    g.A = (uint32_t *)(mem + dbytes);
    g.rows = (float *)(mem + dbytes + abytes);
    const long off[9] = {0, -M, -M + 1, 1, M + 1, M, M - 1, -1, -M - 1};
    memcpy(g.off, off, sizeof off);
    memset(g.D - dpad, (int)NOD, dpad);
    memset(g.D + V, (int)NOD, dpad);
    PM("alloc");
    if (T == 1)
        run_serial();
    else {
        const size_t rows_max = (size_t)((n + T - 1) / T) + 1, cells_max = rows_max * (size_t)M;
        const size_t H = (size_t)2 << 20;
        g_rowsz = ((size_t)8 * ((size_t)M + 8) * 4 + 63) & ~(size_t)63;
        g_exitsz = ((size_t)(2 * M + 16) * sizeof(long) + 63) & ~(size_t)63;
        g_fminsz = ((cells_max + 1) * 4 + 63) & ~(size_t)63;
        g_srcbsz = ((cells_max + 63) / 64 * 8 + 63) & ~(size_t)63;
        g_regsz = (g_rowsz + g_exitsz + g_fminsz + g_srcbsz + cells_max * 16 + rows_max + 4096 + H - 1) & ~(H - 1);
        char *rr = (char *)mmap(0, (size_t)T * g_regsz + H, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (rr == MAP_FAILED) {
            perror("mmap");
            exit(1);
        }
        g_reg = (char *)(((uintptr_t)rr + H - 1) & ~(uintptr_t)(H - 1));
#ifdef MADV_NOHUGEPAGE
        madvise(g_reg, (size_t)T * g_regsz, MADV_NOHUGEPAGE);
#endif
        g_cut[0] = p;
        g_cut[T] = end;
        for (int k = 1; k < T; k++) {
            const char *b = p + (end - p) * k / T;
            if (b < g_cut[k - 1])
                b = g_cut[k - 1];
            while (b < end && (unsigned char)*b > ' ')
                ++b;
            g_cut[k] = b;
        }
        band_work(0);
    }
#if PROF
    fprintf(stderr, "SMALL n=%d m=%d T=%d", n, m, T);
    for (int k = 1; k < g_np; k++)
        fprintf(stderr, " %s=%.3f", g_pn[k], g_pt[k] - g_pt[k - 1]);
    fprintf(stderr, " total=%.3f", g_pt[g_np - 1] - g_pt[0]);
    if (T > 1) {
        double st = 0;
        for (int t = 1; t < T; t++)
            if (g_tt[t][0] - g_tt[0][0] > st)
                st = g_tt[t][0] - g_tt[0][0];
        fprintf(stderr, " laststart=%.3f", st);
        for (int k = 1; k < g_ntt[0]; k++) {
            double mx = 0, sm = 0;
            for (int t = 0; t < T; t++) {
                const double d = g_tt[t][k] - g_tt[t][k - 1];
                sm += d;
                if (d > mx)
                    mx = d;
            }
            fprintf(stderr, " s%d=%.3f/%.3f", k, mx, sm / T);
        }
    }
    fprintf(stderr, "\n");
#endif
    return 0;
}
#else
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <emmintrin.h>
#include <vector>
#include <string>
#include <stdexcept>
#include <limits>
#include "fastio.h"
#ifndef CPT
#define CPT 18000L
#endif
#ifndef TSMALL
#define TSMALL 32
#endif
#ifndef BIG_CELLS
#define BIG_CELLS 4000000L
#endif
#ifndef CPT_BIG
#define CPT_BIG 98304L
#endif
#ifndef TMID
#define TMID 64
#endif
#ifndef TBIG
#define TBIG 128
#endif
#ifndef TMAXP
#define TMAXP 256
#endif
#ifndef OUT_CAP
#define OUT_CAP (64 * 1024)
#endif
#ifndef POPULATE
#define POPULATE 1
#endif
#ifndef PREFAULT
#define PREFAULT 1
#endif
#ifndef HUGE_MEM
#define HUGE_MEM 1
#endif
#ifndef HUGE_REG
#define HUGE_REG 1
#endif
#ifndef PROF
#define PROF 0
#endif
#ifndef G6_EXP
#define G6_EXP 1
#endif
#ifndef WALK_K
#define WALK_K 4096
#endif
#ifndef RUL_PER_T
#define RUL_PER_T 64L
#endif
#ifndef FANOUT
#define FANOUT 8
#endif
#ifndef FMIN_INIT
#define FMIN_INIT 1
#endif
#ifndef NODE_FIRST
#define NODE_FIRST 1
#endif
#ifndef CGRAB
#define CGRAB 32
#endif
#ifndef PREFAULT0
#define PREFAULT0 1
#endif
#ifndef EMIT
#define EMIT 1
#endif
#ifndef RBITS_MIN
#define RBITS_MIN 6
#endif
#ifndef NSUBMAX
#define NSUBMAX 64
#endif
#ifndef SUBBYTES
#define SUBBYTES (64 * 1024)
#endif
#if PROF
static double ptime() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec * 1e-6;
}
static double g_pt[64];
static const char *g_pn[64];
static int g_np;
#define PM(name)                                                                                                       \
    do {                                                                                                               \
        g_pt[g_np] = ptime();                                                                                          \
        g_pn[g_np++] = name;                                                                                           \
    } while (0)
#else
#define PM(name)                                                                                                       \
    do {                                                                                                               \
    } while (0)
#endif
static const unsigned NOD = 15;
#ifndef STEPMODE
#define STEPMODE 2
#endif
struct Step {
    const long *off;
    unsigned k;
    long o;
    explicit Step(const long *t) : off(t), k(0), o(0) {}
    inline long next(long c, unsigned kk);
};
static inline long step(long c, unsigned k, long M) {
    switch (k) {
    case 1:
        return c - M;
    case 2:
        return c - M + 1;
    case 3:
        return c + 1;
    case 4:
        return c + M + 1;
    case 5:
        return c + M;
    case 6:
        return c + M - 1;
    case 7:
        return c - 1;
    default:
        return c - M - 1;
    }
}
inline long Step::next(long c, unsigned kk) {
#if STEPMODE == 1
    return step(c, kk, off[5]);
#elif STEPMODE == 2
    if (kk != k) {
        k = kk;
        o = off[kk];
    }
    return c + o;
#else
    return c + off[kk];
#endif
}
static char g_ob[OUT_CAP + 512] __attribute__((aligned(64)));
static void write_all(int fd, const char *b, size_t len) {
    while (len) {
        ssize_t w = write(fd, b, len);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            perror("write");
            exit(1);
        }
        b += w;
        len -= (size_t)w;
    }
}
static void writev_all(struct iovec *iov, int cnt) {
    int i = 0;
    while (i < cnt) {
        const int c = cnt - i > 1024 ? 1024 : cnt - i;
        ssize_t w = writev(1, iov + i, c);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            perror("write");
            exit(1);
        }
        size_t left = (size_t)w;
        while (i < cnt && left >= iov[i].iov_len) {
            left -= iov[i].iov_len;
            ++i;
        }
        if (i < cnt && left) {
            iov[i].iov_base = (char *)iov[i].iov_base + left;
            iov[i].iov_len -= left;
        }
    }
}
template <bool S> static inline char *ensure(char *q, size_t need) {
    if (S && (size_t)(q - g_ob) + need > (size_t)OUT_CAP) {
        write_all(1, g_ob, (size_t)(q - g_ob));
        q = g_ob;
    }
    return q;
}
template <bool S> static char *put_str(char *q, const char *s) {
    const size_t l = strlen(s);
    q = ensure<S>(q, l);
    memcpy(q, s, l);
    return q + l;
}
template <bool S> static char *put_ghost(char *q, const char *tok, long M) {
    const size_t tl = strlen(tok);
    for (long j = 0; j < M;) {
        const long b = M - j < 4096 ? M - j : 4096;
        q = ensure<S>(q, (size_t)b * 3 + 1);
        if (tl == 2)
            for (long k = 0; k < b; k++) {
                memcpy(q, tok, 2);
                q += 2;
            }
        else
            for (long k = 0; k < b; k++) {
                memcpy(q, tok, 3);
                q += 3;
            }
        j += b;
    }
    q = ensure<S>(q, 1);
    *q++ = '\n';
    return q;
}
static inline const char *ptok(const char *p, float *out) {
    const char *const s = p;
    const unsigned neg = *p == '-';
    p += neg;
    uint32_t m = (uint32_t)(unsigned char)*p - '0';
    if (m > 9)
        return fio::parse_float(s, out);
    const char *const d0 = p;
    ++p;
    uint32_t d;
    while ((d = (uint32_t)(unsigned char)*p - '0') <= 9) {
        m = m * 10 + d;
        ++p;
    }
    long nd = p - d0;
    long e10 = 0;
    if (*p == '.') {
        ++p;
        const char *const f0 = p;
        while ((d = (uint32_t)(unsigned char)*p - '0') <= 9) {
            m = m * 10 + d;
            ++p;
        }
        e10 = f0 - p;
        nd -= e10;
    }
    if (nd > 9 || (unsigned char)*p > ' ' || m >= (1u << 24))
        return fio::parse_float(s, out);
    float f = (float)m;
    if (e10)
        f /= fio::P10F[-e10];
    *out = neg ? -f : f;
    return p;
}
static inline char *put4(char *p, uint32_t v) {
    if (v < 100) {
        if (v < 10) {
            *p = (char)('0' + v);
            return p + 1;
        }
        memcpy(p, fio::DIG2 + 2 * v, 2);
        return p + 2;
    }
    const uint32_t h = v / 100, l = v - 100 * h;
    if (v < 1000) {
        p[0] = (char)('0' + h);
        memcpy(p + 1, fio::DIG2 + 2 * l, 2);
        return p + 3;
    }
    memcpy(p, fio::DIG2 + 2 * h, 2);
    memcpy(p + 2, fio::DIG2 + 2 * l, 2);
    return p + 4;
}
static inline char *pu32(char *p, uint32_t v) {
    if (v < 10000)
        return put4(p, v);
    if (v < 100000000) {
        const uint32_t h = v / 10000, l = v - 10000 * h;
        p = put4(p, h);
        const uint32_t a = l / 100, b = l - 100 * a;
        memcpy(p, fio::DIG2 + 2 * a, 2);
        memcpy(p + 2, fio::DIG2 + 2 * b, 2);
        return p + 4;
    }
    return fio::put_u32(p, v);
}
static inline char *g6_slow(char *p, float f) {
    char t[48];
    const int n = snprintf(t, sizeof t, "%g", (double)f);
    memcpy(p, t, (size_t)n);
    return p + n;
}
static inline char *g6_exp(char *p, double v, double a) {
    if (v < 0)
        *p++ = '-';
    static const double P10E[11] = {1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15};
    int X = 6;
    while (X < 14 && a >= P10E[X - 4])
        X++;
    const double d = fio::P10D[X - 5];
    uint64_t q = (uint64_t)(a / d);
    double r = a - (double)q * d;
    if (r < 0) {
        q--;
        r += d;
    } else if (r >= d) {
        q++;
        r -= d;
    }
    if (2.0 * r > d || (2.0 * r == d && (q & 1u)))
        q++;
    if (q >= 1000000u) {
        q = 100000u;
        X++;
    }
    const uint32_t u = (uint32_t)q;
    char dg[6];
    {
        uint32_t x = u;
        for (int i = 5; i >= 0; i--) {
            dg[i] = (char)('0' + x % 10);
            x /= 10;
        }
    }
    int last = 5;
    while (last > 0 && dg[last] == '0')
        last--;
    *p++ = dg[0];
    if (last > 0) {
        *p++ = '.';
        for (int i = 1; i <= last; i++)
            *p++ = dg[i];
    }
    p[0] = 'e';
    p[1] = '+';
    p[2] = (char)('0' + X / 10);
    p[3] = (char)('0' + X % 10);
    return p + 4;
}
static inline char *g6(char *p, float f) {
    const double v = (double)f;
    const double a = __builtin_fabs(v);
#if G6_EXP
    if (!(a < 1e6))
        return a < 1e15 ? g6_exp(p, v, a) : g6_slow(p, f);
#else
    if (!(a < 1e6))
        return g6_slow(p, f);
#endif
    if (__builtin_signbit(v))
        *p++ = '-';
    if (a == 0.0) {
        *p++ = '0';
        return p;
    }
    const uint32_t ia = (uint32_t)a;
    if ((double)ia == a)
        return pu32(p, ia);
    int X;
    double s;
    if (a >= 1.0) {
        X = a >= 1e5 ? 5 : a >= 1e4 ? 4 : a >= 1e3 ? 3 : a >= 1e2 ? 2 : a >= 1e1 ? 1 : 0;
        s = a * fio::P10D[5 - X];
    } else {
        X = -1;
        s = a * fio::P10D[6];
        while (s < 1e5) {
            if (X == -4) {
                if (v < 0)
                    --p;
                return g6_slow(p, f);
            }
            --X;
            s *= 10.0;
        }
    }
    uint32_t r = (uint32_t)s;
    const double fr = s - (double)r;
    if (fr > 0.5 || (fr == 0.5 && (r & 1u)))
        r++;
    if (r >= 1000000u) {
        r = 100000u;
        X++;
        if (X >= 6) {
            if (v < 0)
                --p;
            return g6_slow(p, f);
        }
    }
    uint64_t w;
    {
        uint16_t a2, b2, c2;
        memcpy(&a2, fio::DIG2 + 2 * (r / 10000), 2);
        memcpy(&b2, fio::DIG2 + 2 * ((r / 100) % 100), 2);
        memcpy(&c2, fio::DIG2 + 2 * (r % 100), 2);
        w = (uint64_t)a2 | ((uint64_t)b2 << 16) | ((uint64_t)c2 << 32);
    }
    const uint64_t z = w ^ 0x0000303030303030ULL;
    const uint64_t nz = (((z & 0x7f7f7f7f7f7f7f7fULL) + 0x7f7f7f7f7f7f7f7fULL) | z) & 0x0000808080808080ULL;
    const int last = (63 - __builtin_clzll(nz)) >> 3;
    if (X >= 0) {
        memcpy(p, &w, 8);
        if (last > X) {
            p[X + 1] = '.';
            const uint64_t fw = w >> (8 * (X + 1));
            memcpy(p + X + 2, &fw, 8);
            p += last + 2;
        } else
            p += X + 1;
    } else {
        memcpy(p, "0.000000", 8);
        memcpy(p + 1 - X, &w, 8);
        p += 2 - X + last;
    }
    return p;
}
static long count_tokens(const char *s, const char *e) {
    long c = 0;
    const char *q = s;
    const __m128i sp = _mm_set1_epi8(' ');
    const __m128i zero = _mm_setzero_si128();
    while (e - q >= 16) {
        long it = (e - q) / 16;
        if (it > 255)
            it = 255;
        __m128i acc = zero;
        for (long k = 0; k < it; k++, q += 16) {
            const __m128i cur = _mm_loadu_si128((const __m128i *)q);
            const __m128i prv = _mm_loadu_si128((const __m128i *)(q - 1));
            const __m128i wc = _mm_cmpeq_epi8(_mm_max_epu8(cur, sp), sp);
            const __m128i wp = _mm_cmpeq_epi8(_mm_max_epu8(prv, sp), sp);
            acc = _mm_sub_epi8(acc, _mm_andnot_si128(wc, wp));
        }
        const __m128i sad = _mm_sad_epu8(acc, zero);
        c += _mm_cvtsi128_si64(sad) + _mm_cvtsi128_si64(_mm_unpackhi_epi64(sad, sad));
    }
    for (; q < e; ++q)
        c += ((unsigned char)q[0] > ' ') & ((unsigned char)q[-1] <= ' ');
    return c;
}
static const char *skip_tokens(const char *s, const char *e, long k) {
    const char *q = s;
    const __m128i sp = _mm_set1_epi8(' ');
    while (e - q >= 16) {
        const __m128i cur = _mm_loadu_si128((const __m128i *)q);
        const __m128i prv = _mm_loadu_si128((const __m128i *)(q - 1));
        unsigned msk = (unsigned)_mm_movemask_epi8(
            _mm_andnot_si128(_mm_cmpeq_epi8(_mm_max_epu8(cur, sp), sp), _mm_cmpeq_epi8(_mm_max_epu8(prv, sp), sp)));
        const long c = __builtin_popcount(msk);
        if (c > k) {
            while (k--)
                msk &= msk - 1;
            return q + __builtin_ctz(msk);
        }
        k -= c;
        q += 16;
    }
    for (; q < e; ++q)
        if (((unsigned char)q[0] > ' ') & ((unsigned char)q[-1] <= ' ')) {
            if (k == 0)
                return q;
            --k;
        }
    return e;
}
static const char *parse_row(const char *p, const char *end, float *r, float *e, long M, float nv, int *bad) {
    for (long j = 0; j < M; j++) {
        while (p < end && (unsigned char)*p <= ' ')
            ++p;
        if (p >= end) {
            for (; j < M; j++)
                r[j] = 0.0f;
            break;
        }
        p = ptok(p, &r[j]);
        if ((unsigned char)*p > ' ')
            *bad = 1;
    }
    const __m128 vnv = _mm_set1_ps(nv), inf = _mm_set1_ps(__builtin_inff());
    long j = 0;
    for (; j + 4 <= M; j += 4) {
        const __m128 x = _mm_loadu_ps(r + j);
        const __m128 c = _mm_cmpeq_ps(x, vnv);
        _mm_storeu_ps(e + j, _mm_or_ps(_mm_and_ps(c, inf), _mm_andnot_ps(c, x)));
    }
    for (; j < M; j++)
        e[j] = r[j] == nv ? __builtin_inff() : r[j];
    e[-1] = __builtin_inff();
    e[M] = __builtin_inff();
    return p;
}
static void dir_row(const float *up, const float *e, const float *dn, const float *r, uint8_t *d, long M, float nv) {
    long j = 0;
    const long J = M - 1;
    const __m128 vnv = _mm_set1_ps(nv);
    const __m128i k15 = _mm_set1_epi32((int)NOD);
    for (; j + 16 <= J; j += 16) {
        __m128i c4[4];
        for (int u = 0; u < 4; u++) {
            const long q = j + 4 * u;
            __m128 mn = _mm_loadu_ps(e + q);
            __m128i code = _mm_setzero_si128();
#define SD(PTR, K)                                                                                                     \
    {                                                                                                                  \
        const __m128 x = _mm_loadu_ps(PTR);                                                                            \
        const __m128 c = _mm_cmplt_ps(x, mn);                                                                          \
        mn = _mm_min_ps(x, mn);                                                                                        \
        code = _mm_max_epi16(code, _mm_and_si128(_mm_castps_si128(c), _mm_set1_epi32(K)));                             \
    }
            SD(up + q, 1)
            SD(up + q + 1, 2) SD(e + q + 1, 3) SD(dn + q + 1, 4) SD(dn + q, 5) SD(dn + q - 1, 6) SD(e + q - 1, 7)
                SD(up + q - 1, 8)
#undef SD
                    const __m128i valid = _mm_castps_si128(_mm_cmpneq_ps(_mm_loadu_ps(r + q), vnv));
            c4[u] = _mm_or_si128(_mm_and_si128(valid, code), _mm_andnot_si128(valid, k15));
        }
        const __m128i a = _mm_packs_epi32(c4[0], c4[1]), b = _mm_packs_epi32(c4[2], c4[3]);
        _mm_storeu_si128((__m128i *)(d + j), _mm_packus_epi16(a, b));
    }
#define STEPD(x, kk)                                                                                                   \
    {                                                                                                                  \
        const float xx = (x);                                                                                          \
        const bool c = xx < mn;                                                                                        \
        mn = c ? xx : mn;                                                                                              \
        code = c ? (kk) : code;                                                                                        \
    }
    for (; j < J; j++) {
        float mn = e[j];
        int code = 0;
        STEPD(up[j], 1)
        STEPD(up[j + 1], 2) STEPD(e[j + 1], 3) STEPD(dn[j + 1], 4) STEPD(dn[j], 5) STEPD(dn[j - 1], 6)
            STEPD(e[j - 1], 7) STEPD(up[j - 1], 8) d[j] = r[j] != nv ? (uint8_t)code : (uint8_t)NOD;
    }
    {
        float mn = e[J];
        int code = 0;
        STEPD(dn[J], 5)
        STEPD(dn[J - 1], 6) STEPD(e[J - 1], 7) STEPD(up[J - 1], 8) STEPD(up[J], 1) d[J] =
            r[J] != nv ? (uint8_t)code : (uint8_t)NOD;
    }
#undef STEPD
}
static void indeg_row(uint8_t *D, long c0, long M) {
    long j = 0;
    const __m128i m0f = _mm_set1_epi8(0x0F);
    for (; j + 16 <= M; j += 16) {
        const long c = c0 + j;
        __m128i cnt = _mm_setzero_si128();
#define IG(OFF, K)                                                                                                     \
    cnt = _mm_sub_epi8(                                                                                                \
        cnt, _mm_cmpeq_epi8(_mm_and_si128(_mm_loadu_si128((const __m128i *)(D + c + (OFF))), m0f), _mm_set1_epi8(K)));
        IG(-M, 5)
        IG(-M + 1, 6) IG(1, 7) IG(M + 1, 8) IG(M, 1) IG(M - 1, 2) IG(-1, 3) IG(-M - 1, 4)
#undef IG
            const __m128i cur = _mm_loadu_si128((const __m128i *)(D + c));
        _mm_storeu_si128((__m128i *)(D + c), _mm_or_si128(cur, _mm_slli_epi16(cnt, 4)));
    }
    for (; j < M; j++) {
        const long c = c0 + j;
        const int k = ((D[c - M] & 15) == 5) + ((D[c - M + 1] & 15) == 6) + ((D[c + 1] & 15) == 7) +
                      ((D[c + M + 1] & 15) == 8) + ((D[c + M] & 15) == 1) + ((D[c + M - 1] & 15) == 2) +
                      ((D[c - 1] & 15) == 3) + ((D[c - M - 1] & 15) == 4);
        D[c] = (uint8_t)(D[c] | (k << 4));
    }
}
static inline void walk(uint8_t *D, uint32_t *A, long c, const long *off) {
    unsigned k = D[c] & 15u;
    uint32_t a = 1;
    Step st(off);
    while (k != 0) {
        const long x = st.next(c, k);
        const uint32_t ax = A[x] + a;
        A[x] = ax;
        const unsigned bx = D[x];
        if (bx >= 0x20u) {
            D[x] = (uint8_t)(bx - 0x10u);
            return;
        }
        D[x] = (uint8_t)(bx | 0xF0u);
        a = ax + 1;
        c = x;
        k = bx & 15u;
    }
}
static void walk_all(uint8_t *D, uint32_t *A, long V, const long *off) {
    const __m128i mf0 = _mm_set1_epi8((char)0xF0), m0f = _mm_set1_epi8(0x0F), k15 = _mm_set1_epi8((char)NOD);
    const __m128i zero = _mm_setzero_si128();
    long c0 = 0;
    for (; c0 + 16 <= V; c0 += 16) {
        const __m128i v = _mm_loadu_si128((const __m128i *)(D + c0));
        const __m128i src =
            _mm_andnot_si128(_mm_cmpeq_epi8(_mm_and_si128(v, m0f), k15), _mm_cmpeq_epi8(_mm_and_si128(v, mf0), zero));
        unsigned msk = (unsigned)_mm_movemask_epi8(src);
        while (msk) {
            const int b = __builtin_ctz(msk);
            msk &= msk - 1;
            walk(D, A, c0 + b, off);
        }
    }
    for (; c0 < V; c0++) {
        const unsigned b = D[c0];
        if ((b & 0xF0u) == 0 && (b & 15u) != NOD)
            walk(D, A, c0, off);
    }
}
static inline uint32_t label_of(const uint8_t *D, uint32_t *A, long c, const long *off, uint32_t *num) {
    const unsigned k = D[c] & 15u;
    if (k == 0) {
        const uint32_t L = ++*num;
        A[c] = L;
        return L;
    }
    Step st(off);
    uint32_t L = A[st.next(c, k)];
    if (L) {
        A[c] = L;
        return L;
    }
    long x = c;
    for (;;) {
        L = A[x];
        if (L)
            break;
        const unsigned kx = D[x] & 15u;
        if (kx == 0) {
            L = ++*num;
            A[x] = L;
            break;
        }
        x = st.next(x, kx);
    }
    x = c;
    while (A[x] == 0) {
        A[x] = L;
        x = st.next(x, D[x] & 15u);
    }
    return L;
}
template <bool S> static char *fmt_terrain_row(char *q, const float *r, long M) {
    for (long j = 0; j < M;) {
        const long b = M - j < 2048 ? M - j : 2048;
        q = ensure<S>(q, (size_t)b * 16 + 32);
        for (const long e = j + b; j < e; j++) {
            q = g6(q, r[j]);
            *q++ = ' ';
        }
    }
    q = ensure<S>(q, 1);
    *q++ = '\n';
    return q;
}
template <bool S> static char *fmt_dir_row(char *q, const uint8_t *r, long M) {
    const __m128i m0f = _mm_set1_epi8(0x0F), k15 = _mm_set1_epi8((char)NOD);
    const __m128i z0 = _mm_set1_epi8('0'), spc = _mm_set1_epi8(' ');
    for (long j = 0; j < M;) {
        const long b = M - j < 2048 ? M - j : 2048;
        q = ensure<S>(q, (size_t)b * 3 + 32);
        const long e = j + b;
        for (; j + 16 <= e; j += 16) {
            const __m128i v = _mm_and_si128(_mm_loadu_si128((const __m128i *)(r + j)), m0f);
            if (_mm_movemask_epi8(_mm_cmpeq_epi8(v, k15))) {
                for (int k = 0; k < 16; k++) {
                    const unsigned d = r[j + k] & 15u;
                    if (d != NOD) {
                        q[0] = (char)('0' + d);
                        q[1] = ' ';
                        q += 2;
                    } else {
                        q[0] = '-';
                        q[1] = '9';
                        q[2] = ' ';
                        q += 3;
                    }
                }
                continue;
            }
            const __m128i c = _mm_add_epi8(v, z0);
            _mm_storeu_si128((__m128i *)q, _mm_unpacklo_epi8(c, spc));
            _mm_storeu_si128((__m128i *)(q + 16), _mm_unpackhi_epi8(c, spc));
            q += 32;
        }
        for (; j < e; j++) {
            const unsigned d = r[j] & 15u;
            if (d != NOD) {
                q[0] = (char)('0' + d);
                q[1] = ' ';
                q += 2;
            } else {
                q[0] = '-';
                q[1] = '9';
                q[2] = ' ';
                q += 3;
            }
        }
    }
    q = ensure<S>(q, 1);
    *q++ = '\n';
    return q;
}
template <bool S> static char *fmt_acc_row(char *q, const uint8_t *d, uint32_t *a, long M) {
    for (long j = 0; j < M;) {
        const long b = M - j < 2048 ? M - j : 2048;
        q = ensure<S>(q, (size_t)b * 12 + 32);
        for (const long e = j + b; j < e; j++) {
            if ((d[j] & 15u) == NOD) {
                q[0] = '-';
                q[1] = '1';
                q[2] = ' ';
                q += 3;
            } else {
                q = pu32(q, a[j] + 1);
                *q++ = ' ';
            }
            a[j] = 0;
        }
    }
    q = ensure<S>(q, 1);
    *q++ = '\n';
    return q;
}
struct LabCache {
    uint32_t llab;
    int llen;
    char lbuf[16];
};
template <bool S, bool RES>
static char *fmt_ws_row(char *q, const uint8_t *D, uint32_t *A, long r0, long M, const long *off, uint32_t *num,
                        LabCache *lc) {
    uint32_t llab = lc->llab;
    int llen = lc->llen;
    for (long j = 0; j < M;) {
        const long b = M - j < 2048 ? M - j : 2048;
        q = ensure<S>(q, (size_t)b * 12 + 32);
        for (const long e = j + b; j < e; j++) {
            const long c = r0 + j;
            if ((D[c] & 15u) == NOD) {
                q[0] = '-';
                q[1] = '1';
                q[2] = ' ';
                q += 3;
                continue;
            }
            uint32_t L = A[c];
            if (RES && !L)
                L = label_of(D, A, c, off, num);
            if (L != llab) {
                llab = L;
                llen = (int)(pu32(lc->lbuf, L) - lc->lbuf);
                lc->lbuf[llen] = ' ';
            }
            memcpy(q, lc->lbuf, 16);
            q += llen + 1;
        }
    }
    lc->llab = llab;
    lc->llen = llen;
    q = ensure<S>(q, 1);
    *q++ = '\n';
    return q;
}
struct Grid {
    const char *text;
    const char *end;
    int n;
    long M, V;
    float nv;
    uint8_t *D;
    uint32_t *A;
    float *rows;
    long off[9];
};
static Grid g;
static float *g_trows[TMAXP];
static float *row_slot(int t, int s) { return (g_trows[t] ? g_trows[t] : g.rows) + (size_t)s * ((size_t)g.M + 8) + 4; }
static float *ghost_e(int t) {
    float *e = row_slot(t, 7);
    for (long j = -1; j <= g.M; j++)
        e[j] = __builtin_inff();
    return e;
}
static void run_serial() {
    const long M = g.M, V = g.V;
    const int n = g.n;
    const float nv = g.nv;
    uint8_t *const D = g.D;
    uint32_t *const A = g.A;
    const char *p = g.text;
    const char *const end = g.end;
    int bad = 0;
    float *r[3] = {row_slot(0, 0), row_slot(0, 1), row_slot(0, 2)};
    float *e[3] = {row_slot(0, 3), row_slot(0, 4), row_slot(0, 5)};
    float *const ge = ghost_e(0);
    char *q = g_ob;
    q = put_str<true>(q, "the terrain\n");
    {
        const float *pe = ge;
        int cur = 0;
        p = parse_row(p, end, r[0], e[0], M, nv, &bad);
        q = fmt_terrain_row<true>(q, r[0], M);
        for (long i = 0; i < n; i++) {
            const int nx = cur == 2 ? 0 : cur + 1;
            const float *ne;
            if (i + 1 < n) {
                p = parse_row(p, end, r[nx], e[nx], M, nv, &bad);
                q = fmt_terrain_row<true>(q, r[nx], M);
                ne = e[nx];
            } else
                ne = ge;
            dir_row(pe, e[cur], ne, r[cur], D + i * M, M, nv);
            if (i >= 1)
                indeg_row(D, (i - 1) * M, M);
            pe = e[cur];
            cur = nx;
        }
        indeg_row(D, (long)(n - 1) * M, M);
    }
    PM("p1");
    q = put_str<true>(q, "the directions\n");
    q = put_ghost<true>(q, "1 ", M);
    for (long i = 0; i < n; i++)
        q = fmt_dir_row<true>(q, D + i * M, M);
    q = put_ghost<true>(q, "5 ", M);
    PM("fmtD");
    walk_all(D, A, V, g.off);
    PM("walk");
    q = put_str<true>(q, "flow accumulation \n");
    q = put_ghost<true>(q, "0 ", M);
    for (long i = 0; i < n; i++)
        q = fmt_acc_row<true>(q, D + i * M, A + i * M, M);
    q = put_ghost<true>(q, "0 ", M);
    PM("fmtA");
    q = put_str<true>(q, "watersheds\n");
    q = put_ghost<true>(q, "-1 ", M);
    {
        uint32_t num = 100;
        LabCache lc;
        lc.llab = 0;
        lc.llen = 0;
        memset(lc.lbuf, 0, sizeof lc.lbuf);
        for (long i = 0; i < n; i++)
            q = fmt_ws_row<true, true>(q, D, A, i * M, M, g.off, &num, &lc);
    }
    q = put_ghost<true>(q, "-1 ", M);
    write_all(1, g_ob, (size_t)(q - g_ob));
    PM("fmtW");
}
static inline const char *ptok_emit(const char *p, float *out, char **qq) {
    const char *const s = p;
    const unsigned neg = *p == '-';
    p += neg;
    uint32_t m = (uint32_t)(unsigned char)*p - '0';
    if (m > 9) {
        p = fio::parse_float(s, out);
        *qq = g6(*qq, *out);
        return p;
    }
    const char *const d0 = p;
    ++p;
    uint32_t d;
    while ((d = (uint32_t)(unsigned char)*p - '0') <= 9) {
        m = m * 10 + d;
        ++p;
    }
    const long il = p - d0;
    long nd = il;
    long e10 = 0;
    const char *f0 = p;
    if (*p == '.') {
        ++p;
        f0 = p;
        while ((d = (uint32_t)(unsigned char)*p - '0') <= 9) {
            m = m * 10 + d;
            ++p;
        }
        e10 = f0 - p;
        nd -= e10;
    }
    if (nd > 9 || (unsigned char)*p > ' ' || m >= (1u << 24)) {
        p = fio::parse_float(s, out);
        *qq = g6(*qq, *out);
        return p;
    }
    float f = (float)m;
    if (e10)
        f /= fio::P10F[-e10];
    *out = neg ? -f : f;
    char *q = *qq;
    long fl = -e10;
    while (fl > 0 && f0[fl - 1] == '0')
        fl--;
    long z = 0, sig;
    if (*d0 != '0') {
        if (il > 6) {
            *qq = g6(q, *out);
            return p;
        }
        sig = fl ? il + fl : 0;
    } else if (il == 1) {
        if (fl == 0) {
            memcpy(q, s, 2);
            *qq = q + neg + 1;
            return p;
        }
        while (f0[z] == '0')
            z++;
        if (z > 3) {
            *qq = g6(q, *out);
            return p;
        }
        sig = fl - z;
    } else {
        *qq = g6(q, *out);
        return p;
    }
    if (sig <= 6) {
        const long len = (long)neg + il + (fl ? 1 + fl : 0);
        memcpy(q, s, 16);
        *qq = q + len;
        return p;
    }
    if (sig > 7 || f0[fl - 1] == '5') {
        *qq = g6(q, *out);
        return p;
    }
    char dg[8];
    {
        long k = 0;
        for (const char *x = d0; x < d0 + il; x++)
            if (k || *x != '0')
                dg[k++] = *x;
        for (long i = (*d0 != '0') ? 0 : z; i < fl; i++)
            dg[k++] = f0[i];
    }
    uint32_t r6 = 0;
    for (int i = 0; i < 6; i++)
        r6 = r6 * 10 + (uint32_t)(dg[i] - '0');
    if (dg[6] > '5') {
        r6++;
        if (r6 >= 1000000u) {
            *qq = g6(q, *out);
            return p;
        }
        for (int i = 5; i >= 0; i--) {
            dg[i] = (char)('0' + r6 % 10);
            r6 /= 10;
        }
    }
    int last = 5;
    while (last > 0 && dg[last] == '0')
        last--;
    if (neg)
        *q++ = '-';
    if (*d0 != '0') {
        for (long i = 0; i < il; i++)
            *q++ = dg[i];
        if (last >= il) {
            *q++ = '.';
            for (long i = il; i <= last; i++)
                *q++ = dg[i];
        }
    } else {
        *q++ = '0';
        *q++ = '.';
        for (long i = 0; i < z; i++)
            *q++ = '0';
        for (long i = 0; i <= last; i++)
            *q++ = dg[i];
    }
    *qq = q;
    return p;
}
static const char *parse_row_emit(const char *p, const char *end, float *r, float *e, long M, float nv, int *bad,
                                  char **qq) {
    char *q = *qq;
    for (long j = 0; j < M; j++) {
        while (p < end && (unsigned char)*p <= ' ')
            ++p;
        if (p >= end) {
            for (; j < M; j++) {
                r[j] = 0.0f;
                q[0] = '0';
                q[1] = ' ';
                q += 2;
            }
            break;
        }
#if EMIT
        p = ptok_emit(p, &r[j], &q);
#else
        p = ptok(p, &r[j]);
        q = g6(q, r[j]);
#endif
        *q++ = ' ';
        if ((unsigned char)*p > ' ')
            *bad = 1;
    }
    *q++ = '\n';
    *qq = q;
    const __m128 vnv = _mm_set1_ps(nv), inf = _mm_set1_ps(__builtin_inff());
    long j = 0;
    for (; j + 4 <= M; j += 4) {
        const __m128 x = _mm_loadu_ps(r + j);
        const __m128 c = _mm_cmpeq_ps(x, vnv);
        _mm_storeu_ps(e + j, _mm_or_ps(_mm_and_ps(c, inf), _mm_andnot_ps(c, x)));
    }
    for (; j < M; j++)
        e[j] = r[j] == nv ? __builtin_inff() : r[j];
    e[-1] = __builtin_inff();
    e[M] = __builtin_inff();
    return p;
}
static void dir_run(const float *up, const float *e, const float *dn, const float *r, uint8_t *d, long L, bool edge,
                    float nv) {
    long j = 0;
    const long J = edge ? L - 1 : L;
    const __m128 vnv = _mm_set1_ps(nv);
    const __m128i k15 = _mm_set1_epi32((int)NOD);
    for (; j + 16 <= J; j += 16) {
        __m128i c4[4];
        for (int u = 0; u < 4; u++) {
            const long q = j + 4 * u;
            __m128 mn = _mm_loadu_ps(e + q);
            __m128i code = _mm_setzero_si128();
#define SD(PTR, K)                                                                                                     \
    {                                                                                                                  \
        const __m128 x = _mm_loadu_ps(PTR);                                                                            \
        const __m128 c = _mm_cmplt_ps(x, mn);                                                                          \
        mn = _mm_min_ps(x, mn);                                                                                        \
        code = _mm_max_epi16(code, _mm_and_si128(_mm_castps_si128(c), _mm_set1_epi32(K)));                             \
    }
            SD(up + q, 1)
            SD(up + q + 1, 2) SD(e + q + 1, 3) SD(dn + q + 1, 4) SD(dn + q, 5) SD(dn + q - 1, 6) SD(e + q - 1, 7)
                SD(up + q - 1, 8)
#undef SD
                    const __m128i valid = _mm_castps_si128(_mm_cmpneq_ps(_mm_loadu_ps(r + q), vnv));
            c4[u] = _mm_or_si128(_mm_and_si128(valid, code), _mm_andnot_si128(valid, k15));
        }
        const __m128i a = _mm_packs_epi32(c4[0], c4[1]), b = _mm_packs_epi32(c4[2], c4[3]);
        _mm_storeu_si128((__m128i *)(d + j), _mm_packus_epi16(a, b));
    }
#define STEPD(x, kk)                                                                                                   \
    {                                                                                                                  \
        const float xx = (x);                                                                                          \
        const bool c = xx < mn;                                                                                        \
        mn = c ? xx : mn;                                                                                              \
        code = c ? (kk) : code;                                                                                        \
    }
    for (; j < J; j++) {
        float mn = e[j];
        int code = 0;
        STEPD(up[j], 1)
        STEPD(up[j + 1], 2) STEPD(e[j + 1], 3) STEPD(dn[j + 1], 4) STEPD(dn[j], 5) STEPD(dn[j - 1], 6)
            STEPD(e[j - 1], 7) STEPD(up[j - 1], 8) d[j] = r[j] != nv ? (uint8_t)code : (uint8_t)NOD;
    }
    if (edge) {
        float mn = e[J];
        int code = 0;
        STEPD(dn[J], 5)
        STEPD(dn[J - 1], 6) STEPD(e[J - 1], 7) STEPD(up[J - 1], 8) STEPD(up[J], 1) d[J] =
            r[J] != nv ? (uint8_t)code : (uint8_t)NOD;
    }
#undef STEPD
}
static void dir_thin(const float *E, uint8_t *D, long i, long ja, long jb, long M) {
    const float inf = __builtin_inff();
    const float *e = E + i * M;
    const float *up = e - M;
    const float *dn = e + M;
    uint8_t *d = D + i * M;
#define STEPD(x, kk)                                                                                                   \
    {                                                                                                                  \
        const float xx = (x);                                                                                          \
        const bool c = xx < mn;                                                                                        \
        mn = c ? xx : mn;                                                                                              \
        code = c ? (kk) : code;                                                                                        \
    }
    long j = ja;
    if (j == 0) {
        float mn = e[0];
        int code = 0;
        STEPD(up[0], 1)
        STEPD(up[1], 2) STEPD(e[1], 3) STEPD(dn[1], 4) STEPD(dn[0], 5) STEPD(inf, 6) STEPD(inf, 7) STEPD(inf, 8) d[0] =
            d[0] == 0xFF ? (uint8_t)NOD : (uint8_t)code;
        j = 1;
    }
    const bool edge = jb == M;
    const long J = edge ? M - 1 : jb;
    const __m128i k15 = _mm_set1_epi8((char)NOD), ff = _mm_set1_epi8((char)0xFF);
    for (; j + 16 <= J; j += 16) {
        __m128i c4[4];
        for (int u = 0; u < 4; u++) {
            const long q = j + 4 * u;
            __m128 mn = _mm_loadu_ps(e + q);
            __m128i code = _mm_setzero_si128();
#define SD(PTR, K)                                                                                                     \
    {                                                                                                                  \
        const __m128 x = _mm_loadu_ps(PTR);                                                                            \
        const __m128 c = _mm_cmplt_ps(x, mn);                                                                          \
        mn = _mm_min_ps(x, mn);                                                                                        \
        code = _mm_max_epi16(code, _mm_and_si128(_mm_castps_si128(c), _mm_set1_epi32(K)));                             \
    }
            SD(up + q, 1)
            SD(up + q + 1, 2) SD(e + q + 1, 3) SD(dn + q + 1, 4) SD(dn + q, 5) SD(dn + q - 1, 6) SD(e + q - 1, 7)
                SD(up + q - 1, 8)
#undef SD
                    c4[u] = code;
        }
        const __m128i a = _mm_packs_epi32(c4[0], c4[1]), b = _mm_packs_epi32(c4[2], c4[3]);
        const __m128i codes = _mm_packus_epi16(a, b);
        const __m128i nd = _mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)(d + j)), ff);
        _mm_storeu_si128((__m128i *)(d + j), _mm_or_si128(_mm_and_si128(nd, k15), _mm_andnot_si128(nd, codes)));
    }
    for (; j < J; j++) {
        float mn = e[j];
        int code = 0;
        STEPD(up[j], 1)
        STEPD(up[j + 1], 2) STEPD(e[j + 1], 3) STEPD(dn[j + 1], 4) STEPD(dn[j], 5) STEPD(dn[j - 1], 6)
            STEPD(e[j - 1], 7) STEPD(up[j - 1], 8) d[j] = d[j] == 0xFF ? (uint8_t)NOD : (uint8_t)code;
    }
    if (edge && J >= ja && J >= 1) {
        float mn = e[J];
        int code = 0;
        STEPD(dn[J], 5)
        STEPD(dn[J - 1], 6) STEPD(e[J - 1], 7) STEPD(up[J - 1], 8) STEPD(up[J], 1) d[J] =
            d[J] == 0xFF ? (uint8_t)NOD : (uint8_t)code;
    }
#undef STEPD
}
static void indeg_mid(uint8_t *D, long c0, long L, long M) {
    long j = 0;
    const __m128i m0f = _mm_set1_epi8(0x0F);
    for (; j + 16 <= L; j += 16) {
        const long c = c0 + j;
        __m128i cnt = _mm_setzero_si128();
#define IG(OFF, K)                                                                                                     \
    cnt = _mm_sub_epi8(                                                                                                \
        cnt, _mm_cmpeq_epi8(_mm_and_si128(_mm_loadu_si128((const __m128i *)(D + c + (OFF))), m0f), _mm_set1_epi8(K)));
        IG(-M, 5)
        IG(-M + 1, 6) IG(1, 7) IG(M + 1, 8) IG(M, 1) IG(M - 1, 2) IG(-1, 3) IG(-M - 1, 4)
#undef IG
            const __m128i cur = _mm_loadu_si128((const __m128i *)(D + c));
        _mm_storeu_si128((__m128i *)(D + c), _mm_or_si128(cur, _mm_slli_epi16(cnt, 4)));
    }
    for (; j < L; j++) {
        const long c = c0 + j;
        const int k = ((D[c - M] & 15) == 5) + ((D[c - M + 1] & 15) == 6) + ((D[c + 1] & 15) == 7) +
                      ((D[c + M + 1] & 15) == 8) + ((D[c + M] & 15) == 1) + ((D[c + M - 1] & 15) == 2) +
                      ((D[c - 1] & 15) == 3) + ((D[c - M - 1] & 15) == 4);
        D[c] = (uint8_t)(D[c] | (k << 4));
    }
}
static inline unsigned dir_at(const uint8_t *D, long x) { return __atomic_load_n(&D[x], __ATOMIC_RELAXED) & 15u; }
static void indeg_edge(uint8_t *D, long c0, long c1, long M) {
    for (long c = c0; c < c1; c++) {
        const unsigned k = (dir_at(D, c - M) == 5) + (dir_at(D, c - M + 1) == 6) + (dir_at(D, c + 1) == 7) +
                           (dir_at(D, c + M + 1) == 8) + (dir_at(D, c + M) == 1) + (dir_at(D, c + M - 1) == 2) +
                           (dir_at(D, c - 1) == 3) + (dir_at(D, c - M - 1) == 4);
        __atomic_store_n(&D[c], (uint8_t)(__atomic_load_n(&D[c], __ATOMIC_RELAXED) | (k << 4)), __ATOMIC_RELAXED);
    }
}
static int g_T;
static bool g_thin;
static long g_bc0[TMAXP + 1];
struct alignas(64) Slot {
    char *buf;
    size_t len;
    int ready;
    int written;
};
static Slot g_slot[TMAXP];
static int g_bad;
struct alignas(64) Bar {
    int cnt;
    char pad[60];
    int gen;
};
static Bar g_bar;
static pthread_t g_th[TMAXP];
#ifndef SPIN
#define SPIN 4000
#endif
static int g_sleep;
static inline void futex_wait(const int *a, int v) { syscall(SYS_futex, a, 128, v, 0, 0, 0); }
static inline void wake_all(const int *a) {
    if (__atomic_load_n(&g_sleep, __ATOMIC_SEQ_CST))
        syscall(SYS_futex, a, 129, 0x7fffffff, 0, 0, 0);
}
static void spin_until_ge(const int *x, int v) {
    for (unsigned s = 0; __atomic_load_n(x, __ATOMIC_ACQUIRE) < v; s++) {
        _mm_pause();
        if (s >= SPIN) {
            __atomic_add_fetch(&g_sleep, 1, __ATOMIC_SEQ_CST);
            for (int c; (c = __atomic_load_n(x, __ATOMIC_SEQ_CST)) < v;)
                futex_wait(x, c);
            __atomic_sub_fetch(&g_sleep, 1, __ATOMIC_SEQ_CST);
            return;
        }
    }
}
static inline void store_wake(int *x, int v) {
    __atomic_store_n(x, v, __ATOMIC_SEQ_CST);
    wake_all(x);
}
static void barrier() {
    const int gen = __atomic_load_n(&g_bar.gen, __ATOMIC_ACQUIRE);
    if (__atomic_add_fetch(&g_bar.cnt, 1, __ATOMIC_ACQ_REL) == g_T) {
        __atomic_store_n(&g_bar.cnt, 0, __ATOMIC_RELAXED);
        store_wake(&g_bar.gen, gen + 1);
        return;
    }
    spin_until_ge(&g_bar.gen, gen + 1);
}
static char *g_mem;
static size_t g_memlen;
static char *g_inmap;
static size_t g_inmaplen;
static char *g_reg;
static size_t g_regsz;
static uint32_t *g_fmin;
static float *g_E;
static long g_rbits;
static size_t g_textsz;
static bool g_huge;
static long g_dpad;
struct Rul {
    uint32_t cell, last, S, L, a0, ain, asg, pad;
};
struct alignas(64) TS {
    float *rows;
    long *exits;
    long nexit;
    uint64_t *srcb;
    long nsrc;
    uint32_t *park;
    long npark, parkcap;
    Rul *rul;
    long nrul, rulcap;
    long nsink, nfirst;
    long pbase;
    long rbase;
};
static TS g_ts[TMAXP];
static long g_rowsz, g_exitsz, g_srcbsz, g_parksz;
static int g_rnext;
static long g_nc;
static int g_cgrab;
static int g_cnext;
static long g_scnt[TMAXP * NSUBMAX];
static inline const char *sub_at(long k) { return g.text + (g.end - g.text) * k / g_nc; }
static void count_sub() {
    for (;;) {
        const long k0 = (long)__atomic_fetch_add(&g_cnext, g_cgrab, __ATOMIC_RELAXED);
        if (k0 >= g_nc)
            break;
        for (long k = k0; k < k0 + g_cgrab && k < g_nc; k++)
            g_scnt[k] = count_tokens(sub_at(k), sub_at(k + 1));
    }
}
static const char *seek_tok(long k) {
    long pre = 0;
    for (long j = 0; j < g_nc; j++) {
        const long c = g_scnt[j];
        if (k < pre + c)
            return skip_tokens(sub_at(j), sub_at(j + 1), k - pre);
        pre += c;
    }
    return g.end;
}
static void band_ready(Slot &s, char *q) {
    s.len = (size_t)(q - s.buf);
    store_wake(&s.ready, s.ready + 1);
}
#define PATN 21845
static char g_pat[4][3 * PATN];
static const char *const g_gtok[4] = {"1 ", "5 ", "0 ", "-1 "};
static void make_pattern(int k) {
    const size_t tl = strlen(g_gtok[k]);
    for (long i = 0; i < PATN; i++)
        memcpy(g_pat[k] + i * tl, g_gtok[k], tl);
}
static int ghost_iov(int k, long M, struct iovec *iov) {
    const size_t tl = strlen(g_gtok[k]);
    int c = 0;
    for (long j = 0; j < M;) {
        const long b = M - j < PATN ? M - j : PATN;
        iov[c].iov_base = g_pat[k];
        iov[c].iov_len = (size_t)b * tl;
        c++;
        j += b;
    }
    iov[c].iov_base = (void *)"\n";
    iov[c].iov_len = 1;
    c++;
    return c;
}
static void write_section(int sec, const char *hdr, int gtop, int gbot) {
    const long M = g.M;
    const long ng = M / PATN + 2;
    struct iovec *iov = (struct iovec *)malloc(sizeof(struct iovec) * (size_t)(g_T + 1 + 2 * ng));
    if (!iov) {
        perror("malloc");
        exit(1);
    }
    int c = 0;
    iov[c].iov_base = (void *)hdr;
    iov[c].iov_len = strlen(hdr);
    c++;
    if (gtop >= 0)
        c += ghost_iov(gtop, M, iov + c);
    for (int t = 0; t < g_T; t++) {
        spin_until_ge(&g_slot[t].ready, sec + 1);
        iov[c].iov_base = g_slot[t].buf;
        iov[c].iov_len = g_slot[t].len;
        c++;
    }
    if (gbot >= 0)
        c += ghost_iov(gbot, M, iov + c);
    writev_all(iov, c);
    free(iov);
    for (int t = 0; t < g_T; t++)
        store_wake(&g_slot[t].written, sec + 1);
}
static inline void park_walk(TS &ts, long c, uint32_t a) {
    if (ts.npark < ts.parkcap) {
        ts.park[2 * ts.npark] = (uint32_t)c;
        ts.park[2 * ts.npark + 1] = a;
        ts.npark++;
    }
}
static Rul *g_rulall;
template <bool W2>
static inline long walk_go(uint8_t *D, uint32_t *A, long c, uint32_t a, const long *off, long budget, TS &ts) {
    unsigned k = __atomic_load_n(&D[c], __ATOMIC_RELAXED) & 15u;
    Step st(off);
    long steps = 0;
    while (k != 0) {
        if (!W2 && steps >= budget) {
            park_walk(ts, c, a);
            return steps;
        }
        const long x = st.next(c, k);
        unsigned cur = __atomic_load_n(&D[x], __ATOMIC_ACQUIRE);
        const unsigned pend = cur >> 4;
        steps++;
        if (pend == 1) {
            const uint32_t ax = __atomic_load_n(&A[x], __ATOMIC_RELAXED) + a;
            __atomic_store_n(&A[x], ax, __ATOMIC_RELAXED);
            if (!W2)
                __atomic_store_n(&D[x], (uint8_t)(cur | 0xF0u), __ATOMIC_RELAXED);
            a = ax + 1;
            c = x;
            k = cur & 15u;
            continue;
        }
        if (W2 && pend == 9) {
            const uint32_t ri = __atomic_load_n(&A[x], __ATOMIC_RELAXED);
            Rul &r = g_rulall[ri];
            r.ain = a;
            r.asg = 1;
            a = a + r.S + r.L;
            c = r.last;
            k = __atomic_load_n(&D[c], __ATOMIC_RELAXED) & 15u;
            steps += r.L;
            continue;
        }
        __atomic_fetch_add(&A[x], a, __ATOMIC_RELAXED);
        for (;;) {
            uint8_t ex = (uint8_t)cur;
            const uint8_t nw = (uint8_t)((cur >> 4) == 1 ? (cur | 0xF0u) : cur - 0x10u);
            if (__atomic_compare_exchange_n(&D[x], &ex, nw, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                break;
            cur = ex;
        }
        if ((cur >> 4) != 1)
            return steps;
        a = __atomic_load_n(&A[x], __ATOMIC_RELAXED) + 1;
        c = x;
        k = cur & 15u;
    }
    return steps;
}
static inline unsigned src_mask16(const uint8_t *p) {
    const __m128i v = _mm_loadu_si128((const __m128i *)p);
    const __m128i src =
        _mm_andnot_si128(_mm_cmpeq_epi8(_mm_and_si128(v, _mm_set1_epi8(0x0F)), _mm_set1_epi8((char)NOD)),
                         _mm_cmpeq_epi8(_mm_and_si128(v, _mm_set1_epi8((char)0xF0)), _mm_setzero_si128()));
    return (unsigned)_mm_movemask_epi8(src);
}
static long src_bits(const uint8_t *D, long c0, long c1, uint64_t *bits) {
    const long L = c1 - c0;
    long k = 0, i = 0, w = 0;
    for (; i + 64 <= L; i += 64, w++) {
        const uint8_t *d = D + c0 + i;
        const uint64_t x = (uint64_t)src_mask16(d) | ((uint64_t)src_mask16(d + 16) << 16) |
                           ((uint64_t)src_mask16(d + 32) << 32) | ((uint64_t)src_mask16(d + 48) << 48);
        bits[w] = x;
        k += __builtin_popcountll(x);
    }
    if (i < L) {
        uint64_t x = 0;
        for (long j = 0; i + j < L; j++) {
            const unsigned d = D[c0 + i + j];
            if ((d & 0xF0u) == 0 && (d & 15u) != NOD)
                x |= (uint64_t)1 << j;
        }
        bits[w] = x;
        k += __builtin_popcountll(x);
    }
    return k;
}
static void walk_share(int t) {
    uint8_t *const D = g.D;
    uint32_t *const A = g.A;
    const long *const off = g.off;
    const int T = g_T;
    TS &ts = g_ts[t];
    long S = 0;
    for (int k = 0; k < T; k++)
        S += g_ts[k].nsrc;
    const long k0 = S * t / T, k1 = S * (t + 1) / T;
    ts.npark = 0;
    if (k0 >= k1)
        return;
    int b = 0;
    long pre = 0;
    while (b < T - 1 && pre + g_ts[b].nsrc <= k0) {
        pre += g_ts[b].nsrc;
        b++;
    }
    long skip = k0 - pre, left = k1 - k0;
    for (; b < T && left > 0; b++) {
        const long c0 = g_bc0[b], c1 = g_bc0[b + 1], nw = (c1 - c0 + 63) / 64;
        const uint64_t *const bits = g_ts[b].srcb;
        for (long w = 0; w < nw && left > 0; w++) {
            uint64_t x = bits[w];
            const long pc = __builtin_popcountll(x);
            if (pc <= skip) {
                skip -= pc;
                continue;
            }
            while (skip > 0) {
                x &= x - 1;
                skip--;
            }
            while (x && left > 0) {
                const int i = __builtin_ctzll(x);
                x &= x - 1;
                walk_go<false>(D, A, c0 + w * 64 + i, 1, off, WALK_K, ts);
                left--;
            }
        }
    }
}
static inline bool hashed(long x) { return ((uint32_t)x * 2654435761u) >> (32 - g_rbits) == 0; }
static void make_rulers(int t) {
    uint8_t *const D = g.D;
    uint32_t *const A = g.A;
    TS &ts = g_ts[t];
    const long c0 = g_bc0[t], c1 = g_bc0[t + 1];
    Step st(g.off);
    Rul *const R = g_rulall + ts.rbase;
    long i = 0;
    for (long x = c0; x < c1 && i < ts.nrul; x++) {
        if (!hashed(x) || (__atomic_load_n(&D[x], __ATOMIC_RELAXED) >> 4) != 1)
            continue;
        uint32_t S = __atomic_load_n(&A[x], __ATOMIC_RELAXED), L = 1;
        long last = x;
        for (;;) {
            const unsigned kl = __atomic_load_n(&D[last], __ATOMIC_RELAXED) & 15u;
            if (kl == 0)
                break;
            const long y = st.next(last, kl);
            if (hashed(y) || (__atomic_load_n(&D[y], __ATOMIC_RELAXED) >> 4) != 1)
                break;
            S += __atomic_load_n(&A[y], __ATOMIC_RELAXED);
            L++;
            last = y;
        }
        Rul &r = R[i];
        r.cell = (uint32_t)x;
        r.last = (uint32_t)last;
        r.S = S;
        r.L = L;
        r.a0 = __atomic_load_n(&A[x], __ATOMIC_RELAXED);
        r.ain = 0;
        r.asg = 0;
        __atomic_store_n(&A[x], (uint32_t)(ts.rbase + i), __ATOMIC_RELAXED);
        __atomic_store_n(&D[x], (uint8_t)((__atomic_load_n(&D[x], __ATOMIC_RELAXED) & 15u) | 0x90u), __ATOMIC_RELAXED);
        i++;
    }
    ts.nrul = i;
}
static void resume_parked(int t) {
    uint8_t *const D = g.D;
    uint32_t *const A = g.A;
    const long *const off = g.off;
    const int T = g_T;
    long NP = 0;
    for (int k = 0; k < T; k++)
        NP += g_ts[k].npark;
    const long i0 = NP * t / T, i1 = NP * (t + 1) / T;
    for (long i = i0; i < i1; i++) {
        int lo = 0, hi = T - 1;
        while (lo < hi) {
            const int md = (lo + hi + 1) / 2;
            if (g_ts[md].pbase <= i)
                lo = md;
            else
                hi = md - 1;
        }
        int b = lo;
        while (i >= g_ts[b].pbase + g_ts[b].npark)
            b++;
        const uint32_t *p = g_ts[b].park + 2 * (i - g_ts[b].pbase);
        walk_go<true>(D, A, (long)p[0], p[1], off, 0, g_ts[b]);
    }
}
static void fill_rulers() {
    uint8_t *const D = g.D;
    uint32_t *const A = g.A;
    const int T = g_T;
    long NR = 0;
    for (int k = 0; k < T; k++)
        NR = g_ts[k].rbase + g_ts[k].nrul > NR ? g_ts[k].rbase + g_ts[k].nrul : NR;
    Step st(g.off);
    for (;;) {
        const long i0 = __atomic_fetch_add(&g_rnext, 8, __ATOMIC_RELAXED);
        if (i0 >= NR)
            break;
        for (long i = i0; i < i0 + 8 && i < NR; i++) {
            int lo = 0, hi = T - 1;
            while (lo < hi) {
                const int md = (lo + hi + 1) / 2;
                if (g_ts[md].rbase <= i)
                    lo = md;
                else
                    hi = md - 1;
            }
            const int b = lo;
            if (i >= g_ts[b].rbase + g_ts[b].nrul)
                continue;
            const Rul &r = g_rulall[i];
            long x = (long)r.cell;
            A[x] = r.a0;
            uint32_t v = r.ain;
            for (uint32_t k = 0; k < r.L; k++) {
                const uint32_t ax = A[x] + v;
                A[x] = ax;
                v = ax + 1;
                const unsigned dx = D[x];
                D[x] = (uint8_t)(dx | 0xF0u);
                if (k + 1 < r.L)
                    x = st.next(x, dx & 15u);
            }
        }
    }
}
static const uint32_t HIGH = 0x80000000u;
static const uint32_t DIRECT = 0x40000000u;
static inline uint32_t *fmin_of(uint32_t s) { return g_fmin + s; }
static void local_roots(int t, long c0, long c1) {
    uint8_t *const D = g.D;
    uint32_t *const A = g.A;
    Step st(g.off);
    long ne = 0, ns = 0;
    long *ex = g_ts[t].exits;
    for (long c = c0; c < c1; c++) {
        const unsigned kc = D[c] & 15u;
        if (kc == NOD)
            continue;
        if (kc == 0) {
            ns += D[c] != 0;
            A[c] = (uint32_t)c + 1;
            continue;
        }
        if (A[c])
            continue;
        long x = c;
        uint32_t R;
        for (;;) {
            if (x < c0 || x >= c1) {
                R = (uint32_t)x + 1;
                ex[ne++] = x;
                break;
            }
            const uint32_t ax = A[x];
            if (ax) {
                R = ax;
                break;
            }
            const unsigned kx = D[x] & 15u;
            if (kx == 0) {
                R = (uint32_t)x + 1;
                A[x] = R;
                break;
            }
            x = st.next(x, kx);
        }
        for (long y = c; y >= c0 && y < c1 && A[y] == 0; y = st.next(y, D[y] & 15u))
            A[y] = R;
    }
    g_ts[t].nexit = ne;
    g_ts[t].nsink = ns;
}
static void resolve_exits(int t) {
    const uint8_t *const D = g.D;
    uint32_t *const A = g.A;
    const TS &ts = g_ts[t];
    for (long i = 0; i < ts.nexit; i++) {
        const long x = ts.exits[i];
        if ((D[x] & 15u) == 0)
            continue;
        long y = x, root;
        for (;;) {
            const long z = (long)__atomic_load_n(&A[y], __ATOMIC_RELAXED) - 1;
            if ((D[z] & 15u) == 0) {
                root = z;
                break;
            }
            const long zz = (long)__atomic_load_n(&A[z], __ATOMIC_RELAXED) - 1;
            __atomic_store_n(&A[y], (uint32_t)zz + 1, __ATOMIC_RELAXED);
            if ((D[zz] & 15u) == 0) {
                root = zz;
                break;
            }
            y = zz;
        }
        __atomic_store_n(&A[x], (uint32_t)root + 1, __ATOMIC_RELAXED);
    }
}
static void number_sinks(int t, long c0, long c1) {
    uint8_t *const D = g.D;
    uint32_t *const A = g.A;
    uint32_t s = 0;
    for (int k = 0; k < t; k++)
        s += (uint32_t)g_ts[k].nsink;
    const uint32_t V = (uint32_t)g.V;
    for (long c = c0; c < c1; c++)
        if ((D[c] & 15u) == 0) {
            if (D[c] == 0) {
                __atomic_store_n(&A[c], HIGH | DIRECT, __ATOMIC_RELAXED);
                continue;
            }
#if FMIN_INIT
            __atomic_store_n(&g_fmin[s], V - (uint32_t)c, __ATOMIC_RELAXED);
#endif
            __atomic_store_n(&A[c], HIGH | s++, __ATOMIC_RELAXED);
        }
    (void)V;
}
static inline uint32_t sink_number(const uint32_t *A, long c) {
    uint32_t x = __atomic_load_n(&A[c], __ATOMIC_RELAXED);
    while (!(x & HIGH))
        x = __atomic_load_n(&A[x - 1], __ATOMIC_RELAXED);
    return x & ~HIGH;
}
static void basin_min(long c0, long c1) {
    uint8_t *const D = g.D;
    uint32_t *const A = g.A;
    const uint32_t V = (uint32_t)g.V;
    uint32_t last = HIGH;
    for (long c = c0; c < c1; c++) {
        const unsigned d = D[c];
        if ((d & 15u) == NOD || d == 0)
            continue;
        const uint32_t s = sink_number(A, c);
        __atomic_store_n(&A[c], HIGH | s, __ATOMIC_RELAXED);
        if (s == last)
            continue;
        last = s;
        const uint32_t v = V - (uint32_t)c;
        uint32_t *const f = fmin_of(s);
        uint32_t cur = __atomic_load_n(f, __ATOMIC_RELAXED);
        while (v > cur && !__atomic_compare_exchange_n(f, &cur, v, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
        }
    }
}
static void count_firsts(int t, long c0, long c1) {
    uint8_t *const D = g.D;
    const uint32_t *const A = g.A;
    const uint32_t V = (uint32_t)g.V;
    long f = 0;
    for (long c = c0; c < c1; c++) {
        const unsigned d = D[c];
        if ((d & 15u) == NOD)
            continue;
        f += d == 0 || *fmin_of(A[c] & ~HIGH) == V - (uint32_t)c;
    }
    g_ts[t].nfirst = f;
}
static void assign_labels(int t, long c0, long c1) {
    uint8_t *const D = g.D;
    uint32_t *const A = g.A;
    const uint32_t V = (uint32_t)g.V;
    uint32_t lab = 100;
    for (int k = 0; k < t; k++)
        lab += (uint32_t)g_ts[k].nfirst;
    for (long c = c0; c < c1; c++) {
        const unsigned d = D[c];
        if ((d & 15u) == NOD)
            continue;
        if (d == 0) {
            A[c] = HIGH | DIRECT | ++lab;
            continue;
        }
        uint32_t *const fp = fmin_of(A[c] & ~HIGH);
        if (__atomic_load_n(fp, __ATOMIC_RELAXED) == V - (uint32_t)c)
            __atomic_store_n(fp, HIGH | ++lab, __ATOMIC_RELAXED);
    }
}
static char *fmt_dir_seg(char *q, const uint8_t *r, long L) {
    const __m128i m0f = _mm_set1_epi8(0x0F), k15 = _mm_set1_epi8((char)NOD);
    const __m128i z0 = _mm_set1_epi8('0'), spc = _mm_set1_epi8(' ');
    long j = 0;
    for (; j + 16 <= L; j += 16) {
        const __m128i v = _mm_and_si128(_mm_loadu_si128((const __m128i *)(r + j)), m0f);
        if (_mm_movemask_epi8(_mm_cmpeq_epi8(v, k15))) {
            for (int k = 0; k < 16; k++) {
                const unsigned d = r[j + k] & 15u;
                if (d != NOD) {
                    q[0] = (char)('0' + d);
                    q[1] = ' ';
                    q += 2;
                } else {
                    q[0] = '-';
                    q[1] = '9';
                    q[2] = ' ';
                    q += 3;
                }
            }
            continue;
        }
        const __m128i c = _mm_add_epi8(v, z0);
        _mm_storeu_si128((__m128i *)q, _mm_unpacklo_epi8(c, spc));
        _mm_storeu_si128((__m128i *)(q + 16), _mm_unpackhi_epi8(c, spc));
        q += 32;
    }
    for (; j < L; j++) {
        const unsigned d = r[j] & 15u;
        if (d != NOD) {
            q[0] = (char)('0' + d);
            q[1] = ' ';
            q += 2;
        } else {
            q[0] = '-';
            q[1] = '9';
            q[2] = ' ';
            q += 3;
        }
    }
    return q;
}
static char *fmt_acc_seg(char *q, const uint8_t *d, uint32_t *a, long L) {
    for (long j = 0; j < L; j++) {
        if ((d[j] & 15u) == NOD) {
            q[0] = '-';
            q[1] = '1';
            q[2] = ' ';
            q += 3;
        } else {
            q = pu32(q, a[j] + 1);
            *q++ = ' ';
        }
        a[j] = 0;
    }
    return q;
}
static char *fmt_ws_seg(char *q, const uint8_t *D, const uint32_t *A, long c0, long L, LabCache *lc) {
    uint32_t llab = lc->llab;
    int llen = lc->llen;
    for (long j = 0; j < L; j++) {
        const long c = c0 + j;
        if ((D[c] & 15u) == NOD) {
            q[0] = '-';
            q[1] = '1';
            q[2] = ' ';
            q += 3;
            continue;
        }
        const uint32_t x = A[c];
        const uint32_t Lb = (x & DIRECT) ? x & ~(HIGH | DIRECT) : *fmin_of(x & ~HIGH) & ~HIGH;
        if (Lb != llab) {
            llab = Lb;
            llen = (int)(pu32(lc->lbuf, Lb) - lc->lbuf);
            lc->lbuf[llen] = ' ';
        }
        memcpy(q, lc->lbuf, 16);
        q += llen + 1;
    }
    lc->llab = llab;
    lc->llen = llen;
    return q;
}
template <class F> static inline void for_pieces(long c0, long c1, long M, F f) {
    for (long c = c0; c < c1;) {
        const long j = c % M, L = (M - j < c1 - c) ? M - j : c1 - c;
        f(c, L, j + L == M);
        c += L;
    }
}
static char *stream_thick(int t, char *q) {
    const int n = g.n;
    const long M = g.M;
    const float nv = g.nv;
    uint8_t *const D = g.D;
    const long r0 = g_bc0[t] / M, r1 = g_bc0[t + 1] / M;
    if (r0 >= r1)
        return q;
    float *const rows = g_ts[t].rows;
    const size_t rs = (size_t)M + 8;
    float *r[3] = {rows + 4, rows + rs + 4, rows + 2 * rs + 4};
    float *e[3] = {rows + 3 * rs + 4, rows + 4 * rs + 4, rows + 5 * rs + 4};
    float *const ge = rows + 6 * rs + 4;
    for (long j = -1; j <= M; j++)
        ge[j] = __builtin_inff();
    int bad = 0;
    const char *p = seek_tok((r0 > 0 ? r0 - 1 : 0) * M);
    const float *pe = ge;
    int cur = 0;
    if (r0 > 0) {
        p = parse_row(p, g.end, r[2], e[2], M, nv, &bad);
        pe = e[2];
    }
    p = parse_row_emit(p, g.end, r[0], e[0], M, nv, &bad, &q);
    for (long i = r0; i < r1; i++) {
        const int nx = cur == 2 ? 0 : cur + 1;
        const float *ne;
        if (i + 1 < n) {
            if (i + 1 < r1)
                p = parse_row_emit(p, g.end, r[nx], e[nx], M, nv, &bad, &q);
            else
                p = parse_row(p, g.end, r[nx], e[nx], M, nv, &bad);
            ne = e[nx];
        } else
            ne = ge;
        dir_run(pe, e[cur], ne, r[cur], D + i * M, M, true, nv);
        pe = e[cur];
        cur = nx;
    }
    if (bad)
        __atomic_store_n(&g_bad, 1, __ATOMIC_RELAXED);
    return q;
}
static char *stream_thin1(int t, char *q) {
    const long M = g.M;
    const float nv = g.nv;
    uint8_t *const D = g.D;
    float *const E = g_E;
    const long c0 = g_bc0[t], c1 = g_bc0[t + 1];
    if (c0 >= c1)
        return q;
    const char *p = seek_tok(c0);
    const char *const end = g.end;
    int bad = 0;
    for_pieces(c0, c1, M, [&](long c, long L, bool eol) {
        for (long j = 0; j < L; j++) {
            float x;
            while (p < end && (unsigned char)*p <= ' ')
                ++p;
            if (p >= end) {
                x = 0.0f;
                q[0] = '0';
                q += 1;
            } else {
#if EMIT
                p = ptok_emit(p, &x, &q);
#else
p = ptok(p, &x);
q = g6(q, x);
#endif
                if ((unsigned char)*p > ' ')
                    bad = 1;
            }
            *q++ = ' ';
            const bool nd = x == nv;
            E[c + j] = nd ? __builtin_inff() : x;
            D[c + j] = nd ? 0xFF : 0;
        }
        if (eol)
            *q++ = '\n';
    });
    if (bad)
        __atomic_store_n(&g_bad, 1, __ATOMIC_RELAXED);
    return q;
}
static void stream_thin2(int t) {
    const long M = g.M;
    const long c0 = g_bc0[t], c1 = g_bc0[t + 1];
    for_pieces(c0, c1, M, [&](long c, long L, bool) {
        const long i = c / M, j = c % M;
        dir_thin(g_E, g.D, i, j, j + L, M);
    });
}
#if PROF
static double g_tt[TMAXP][48];
static int g_ntt[TMAXP];
#define TT()                                                                                                           \
    do {                                                                                                               \
        tt[ntt++] = ptime();                                                                                           \
    } while (0)
#else
#define TT()                                                                                                           \
    do {                                                                                                               \
    } while (0)
#endif
static void *band_main(void *arg);
#ifndef STACK
#define STACK (256 * 1024)
#endif
static int g_cpu[TMAXP];
static char *g_stk;
static void spawn_children(int t) {
    for (int c = FANOUT * t + 1; c <= FANOUT * t + FANOUT && c < g_T; c++) {
        pthread_attr_t at;
        pthread_attr_init(&at);
        pthread_attr_setstack(&at, g_stk + (size_t)c * STACK, (size_t)STACK);
        cpu_set_t cs;
        CPU_ZERO(&cs);
        CPU_SET(g_cpu[c], &cs);
        pthread_attr_setaffinity_np(&at, sizeof cs, &cs);
        if (pthread_create(&g_th[c], &at, band_main, (void *)(intptr_t)c) != 0) {
            perror("pthread_create");
            exit(1);
        }
        pthread_attr_destroy(&at);
    }
}
static void release_share(int t) {
    const int T = g_T;
    const size_t pg = 4096;
    const size_t mg = g_huge ? (size_t)2 << 20 : pg;
    if (g_mem) {
        const size_t a = g_memlen * t / T / mg * mg, b = t + 1 == T ? g_memlen : g_memlen * (t + 1) / T / mg * mg;
        if (b > a)
            madvise(g_mem + a, b - a, MADV_DONTNEED);
    }
    madvise(g_reg + (size_t)t * g_regsz, g_regsz, MADV_DONTNEED);
    if (g_inmap) {
        const size_t a = g_inmaplen * t / T / pg * pg, b = t + 1 == T ? g_inmaplen : g_inmaplen * (t + 1) / T / pg * pg;
        if (b > a)
            madvise(g_inmap + a, b - a, MADV_DONTNEED);
    }
}
static void run_serial_fallback();
static void band_work(int t) {
#if PROF
    double tt[48];
    int ntt = 0;
#endif
    TT();
    spawn_children(t);
    TT();
    const int T = g_T;
    const long M = g.M;
    uint8_t *const D = g.D;
    uint32_t *const A = g.A;
    Slot &sl = g_slot[t];
    TS &ts = g_ts[t];
    const long c0 = g_bc0[t], c1 = g_bc0[t + 1];
    {
        char *rg = g_reg + (size_t)t * g_regsz;
        ts.rows = (float *)rg;
        rg += g_rowsz;
        ts.exits = (long *)rg;
        rg += g_exitsz;
        ts.srcb = (uint64_t *)rg;
        rg += g_srcbsz;
        ts.park = (uint32_t *)rg;
        rg += g_parksz;
        ts.parkcap = (long)(g_parksz / 8);
        sl.buf = rg;
    }
    {
        const long P = g_dpad, a = P * t / T, b = P * (t + 1) / T;
        memset(D - P + a, (int)NOD, (size_t)(b - a));
        memset(D + g.V + a, (int)NOD, (size_t)(b - a));
        if (g_thin) {
            const long Q = M + 16, qa = Q * t / T, qb = Q * (t + 1) / T;
            for (long j = qa; j < qb; j++) {
                g_E[j - Q] = __builtin_inff();
                g_E[g.V + j] = __builtin_inff();
            }
        }
    }
    if (t < 4)
        make_pattern(t);
    if (t == 0)
        for (int k = T; k < 4; k++)
            make_pattern(k);
#if PREFAULT0
    {
        volatile uint8_t *const vd = D;
        for (long c = c0 & ~4095L; c < c1; c += 4096)
            if (c >= c0)
                vd[c] = 0;
        const uintptr_t lo = (uintptr_t)(A + c0), hi = (uintptr_t)(A + c1);
        for (uintptr_t a = (lo + 4095) & ~(uintptr_t)4095; a < hi; a += 4096)
            *(volatile uint32_t *)a = 0;
        if (g_thin) {
            const uintptr_t el = (uintptr_t)(g_E + c0), eh = (uintptr_t)(g_E + c1);
            for (uintptr_t a = (el + 4095) & ~(uintptr_t)4095; a < eh; a += 4096)
                *(volatile float *)a = 0;
        }
        const size_t tb = (size_t)((double)(g.end - g.text) / (double)g.V * (double)(c1 - c0) * 1.1) + 65536;
        for (size_t o = 0; o < tb && o < g_textsz; o += 4096)
            ((volatile char *)sl.buf)[o] = 0;
    }
#endif
    count_sub();
    TT();
    barrier();
    TT();
    {
        char *q = sl.buf;
        if (!g_thin)
            q = stream_thick(t, q);
        else
            q = stream_thin1(t, q);
        TT();
        if (g_thin) {
            barrier();
            stream_thin2(t);
        }
        band_ready(sl, q);
    }
    TT();
    if (t == 0)
        PM("p1");
    barrier();
    TT();
    if (__atomic_load_n(&g_bad, __ATOMIC_RELAXED)) {
        if (t == 0)
            run_serial_fallback();
        return;
    }
    if (t == 0) {
        write_section(0, "the terrain\n", -1, -1);
        PM("wT");
    }
    {
        const long e0 = c0 + M + 1 < c1 ? c0 + M + 1 : c1;
        const long e1 = c1 - M - 1 > e0 ? c1 - M - 1 : e0;
        indeg_edge(D, c0, e0, M);
        if (e1 > e0)
            indeg_mid(D, e0, e1 - e0, M);
        indeg_edge(D, e1, c1, M);
    }
    ts.nsrc = src_bits(D, c0, c1, ts.srcb);
#if PREFAULT && !PREFAULT0
    {
        const uintptr_t lo = (uintptr_t)(A + c0), hi = (uintptr_t)(A + c1);
        for (uintptr_t a = (lo + 4095) & ~(uintptr_t)4095; a < hi; a += 4096)
            *(volatile uint32_t *)a = 0;
    }
#endif
    TT();
    barrier();
    TT();
    if (t == 0)
        PM("indeg");
    walk_share(t);
    TT();
    barrier();
    TT();
    if (t == 0)
        PM("walk1");
    {
        long NP = 0, pb = 0;
        for (int k = 0; k < T; k++) {
            if (k == t)
                pb = NP;
            NP += g_ts[k].npark;
        }
        ts.pbase = pb;
        if (NP > 0) {
            ts.rbase = (c0 >> g_rbits) + 2L * t;
            ts.nrul = ((c1 - c0) >> g_rbits) + 2;
            if (t + 1 < T) {
                const long nb = (g_bc0[t + 1] >> g_rbits) + 2L * (t + 1);
                if (ts.nrul > nb - ts.rbase)
                    ts.nrul = nb - ts.rbase;
            }
            make_rulers(t);
            TT();
            barrier();
            TT();
            resume_parked(t);
            TT();
            barrier();
            TT();
            fill_rulers();
            TT();
            barrier();
            TT();
        }
    }
    if (t == 0)
        PM("walk2");
    {
        char *q = sl.buf;
        for_pieces(c0, c1, M, [&](long c, long L, bool eol) {
            q = fmt_dir_seg(q, D + c, L);
            if (eol)
                *q++ = '\n';
        });
        band_ready(sl, q);
    }
    if (t == 0) {
        write_section(1, "the directions\n", 0, 1);
        PM("wD");
    }
    {
        spin_until_ge(&sl.written, 2);
        char *q = sl.buf;
        for_pieces(c0, c1, M, [&](long c, long L, bool eol) {
            q = fmt_acc_seg(q, D + c, A + c, L);
            if (eol)
                *q++ = '\n';
        });
        band_ready(sl, q);
    }
    TT();
    local_roots(t, c0, c1);
    TT();
    barrier();
    TT();
    if (t == 0) {
        PM("fmtA+L1");
        write_section(2, "flow accumulation \n", 2, 2);
        PM("wA");
    }
    resolve_exits(t);
    number_sinks(t, c0, c1);
    TT();
    barrier();
    TT();
    basin_min(c0, c1);
    TT();
    barrier();
    TT();
    count_firsts(t, c0, c1);
    TT();
    barrier();
    TT();
    assign_labels(t, c0, c1);
    TT();
    barrier();
    TT();
    if (t == 0)
        PM("lab");
    {
        spin_until_ge(&sl.written, 3);
        char *q = sl.buf;
        LabCache lc;
        lc.llab = 0;
        lc.llen = 0;
        memset(lc.lbuf, 0, sizeof lc.lbuf);
        for_pieces(c0, c1, M, [&](long c, long L, bool eol) {
            q = fmt_ws_seg(q, D, A, c, L, &lc);
            if (eol)
                *q++ = '\n';
        });
        TT();
#if PROF
        memcpy(g_tt[t], tt, sizeof tt);
        g_ntt[t] = ntt;
#endif
        band_ready(sl, q);
    }
    if (t == 0) {
        write_section(3, "watersheds\n", 3, 3);
        PM("wW");
    }
    barrier();
    release_share(t);
    barrier();
    if (t == 0)
        PM("rel");
}
static void *band_main(void *arg) {
    band_work((int)(intptr_t)arg);
    return 0;
}
static void write_all(const char *b, size_t len) { write_all(1, b, len); }
namespace refp {
const int no_dir_value = -9, no_acc_value = -1, no_watershed_value = -1, first_label = 100;
struct Text {
    std::vector<char> b;
    void s(const char *x) { b.insert(b.end(), x, x + strlen(x)); }
    void i(int v) {
        char t[16];
        const int l = snprintf(t, sizeof t, "%d", v);
        b.insert(b.end(), t, t + l);
    }
    void f(float v) {
        char t[32];
        char *e = fio::put_g6(t, v);
        b.insert(b.end(), t, e);
    }
    void nl() { b.push_back('\n'); }
};
static int f_edge1(float ref, float *neighbors, float no_value) {
    float min = ref;
    int code = 0;
    for (int i = 0; i < 5; i++)
        if (neighbors[i] != no_value) {
            if (neighbors[i] < min) {
                min = neighbors[i];
                code = i + 1;
            }
        }
    return code;
}
static int f_edge2(float ref, float *neighbors, float no_value) {
    float min = ref;
    int code = -1;
    for (int i = 0; i < 5; i++)
        if (neighbors[i] != no_value) {
            if (neighbors[i] < min) {
                min = neighbors[i];
                code = i;
            }
        }
    if (code == 4)
        return 1;
    else if (code == -1)
        return 0;
    else
        return code + 5;
}
static int f(float ref, float *neighbors, float no_value) {
    float min = ref;
    int code = 0;
    for (int i = 0; i < 8; i++)
        if (neighbors[i] != no_value) {
            if (neighbors[i] < min) {
                min = neighbors[i];
                code = i + 1;
            }
        }
    return code;
}
static int f_acc(int *neighbors, int no_value, int *edge_directions, int l) {
    int val = 0, inflow_count = 0, calculated_inflow_count = 0;
    for (int i = 0; i < l; i++)
        if (neighbors[l + i] != no_value)
            if (neighbors[l + i] == edge_directions[i]) {
                inflow_count++;
                if (neighbors[i] != -1) {
                    val += neighbors[i];
                    calculated_inflow_count++;
                }
            }
    if (inflow_count == calculated_inflow_count)
        val++;
    else
        val = no_acc_value;
    return val;
}
static int f_watershed(int *watershed, int *dir, int row_count, int column_count, int i, int j, int *num) {
    const int dr[9] = {0, -1, -1, 0, 1, 1, 1, 0, -1};
    const int dc[9] = {0, 0, 1, 1, 1, 0, -1, -1, -1};
    std::vector<int> path;
    int label;
    while (true) {
        if (i < 0 || i >= row_count || j < 0 || j >= column_count)
            throw std::runtime_error("Watershed flow leaves the terrain");
        const int index = i * column_count + j;
        const int code = dir[index];
        if (code < 0 || code > 8)
            throw std::runtime_error("Invalid watershed direction or no-data target");
        if (watershed[index] == -2)
            throw std::runtime_error("Cycle in watershed directions");
        if (watershed[index] != no_watershed_value) {
            label = watershed[index];
            break;
        }
        path.push_back(index);
        watershed[index] = -2;
        if (code == 0) {
            if (*num == std::numeric_limits<int>::max())
                throw std::runtime_error("Too many watershed labels");
            label = ++(*num);
            break;
        }
        i += dr[code];
        j += dc[code];
    }
    for (size_t k = 0; k < path.size(); k++)
        watershed[path[k]] = label;
    return label;
}
static int run(int row_count, int column_count, float no_value, const char *p, const char *end) {
    Text o;
    {
        const long long r = row_count > 0 ? (long long)row_count + 2 : 2, c = column_count > 0 ? column_count : 0;
        long long est = 64 + 4 * r + 4 * r * c * 12;
        if (est > (1LL << 28))
            est = 1LL << 28;
        try {
            o.b.reserve((size_t)est);
        } catch (...) {
        }
    }
    int rc = 0;
    const char *err = 0;
    std::string what;
    float *terrain = 0;
    int *direction = 0;
    int *accumulation = 0;
    int *watershed = 0;
    try {
        terrain = new float[(row_count + 2) * (column_count)];
        bool eof = false;
        for (int i = 0; i < row_count; i++)
            for (int j = 0; j < column_count; j++) {
                float v = 0.0f;
                if (!eof) {
                    while (p < end && fio::is_ws((unsigned char)*p))
                        ++p;
                    if (p >= end)
                        eof = true;
                    else
                        p = fio::parse_float(p, &v);
                }
                terrain[(i + 1) * (column_count) + j] = v;
            }
        for (int j = 0; j < column_count; j++) {
            terrain[j] = no_value;
            terrain[(row_count + 1) * (column_count) + j] = no_value;
        }
        o.s("the terrain");
        o.nl();
        for (int i = 0; i < row_count; i++) {
            for (int j = 0; j < column_count; j++) {
                o.f(terrain[(i + 1) * column_count + j]);
                o.s(" ");
            }
            o.nl();
        }
        direction = new int[(row_count + 2) * column_count];
        for (int i = 0; i < column_count; i++) {
            direction[i] = 1;
            direction[(row_count + 1) * column_count + i] = 5;
        }
        for (int i = 0; i < row_count; i++)
            for (int j = 0; j < column_count; j++)
                direction[(i + 1) * column_count + j] = no_dir_value;
        {
            int x, y, x1, y1, x2, y2;
            float neighbors[8];
            float edge_neighbors[5];
            for (int i = 0; i < row_count; i++) {
                x = i + 1;
                x1 = x - 1;
                x2 = x + 1;
                for (int j = 0; j < column_count; j++) {
                    y = j;
                    y1 = y - 1;
                    y2 = y + 1;
                    float val = terrain[x * column_count + y];
                    if (val != no_value) {
                        if (j == 0) {
                            edge_neighbors[0] = terrain[x1 * column_count + y];
                            edge_neighbors[1] = terrain[x1 * column_count + y2];
                            edge_neighbors[2] = terrain[x * column_count + y2];
                            edge_neighbors[3] = terrain[x2 * column_count + y2];
                            edge_neighbors[4] = terrain[x2 * column_count + y];
                            direction[(i + 1) * column_count + j] = f_edge1(val, edge_neighbors, no_value);
                        } else if (j == (column_count - 1)) {
                            edge_neighbors[0] = terrain[x2 * column_count + y];
                            edge_neighbors[1] = terrain[x2 * column_count + y1];
                            edge_neighbors[2] = terrain[x * column_count + y1];
                            edge_neighbors[3] = terrain[x1 * column_count + y1];
                            edge_neighbors[4] = terrain[x1 * column_count + y];
                            direction[(i + 1) * column_count + j] = f_edge2(val, edge_neighbors, no_value);
                        } else {
                            neighbors[0] = terrain[x1 * column_count + y];
                            neighbors[1] = terrain[x1 * column_count + y2];
                            neighbors[2] = terrain[x * column_count + y2];
                            neighbors[3] = terrain[x2 * column_count + y2];
                            neighbors[4] = terrain[x2 * column_count + y];
                            neighbors[5] = terrain[x2 * column_count + y1];
                            neighbors[6] = terrain[x * column_count + y1];
                            neighbors[7] = terrain[x1 * column_count + y1];
                            direction[(i + 1) * column_count + j] = f(val, neighbors, no_value);
                        }
                    }
                }
            }
        }
        o.s("the directions");
        o.nl();
        for (int i = 0; i < row_count + 2; i++) {
            for (int j = 0; j < column_count; j++) {
                o.i(direction[i * column_count + j]);
                o.s(" ");
            }
            o.nl();
        }
        accumulation = new int[(row_count + 2) * column_count];
        for (int i = 0; i < column_count; i++) {
            accumulation[i] = 0;
            accumulation[(row_count + 1) * column_count + i] = 0;
        }
        for (int i = 0; i < row_count; i++)
            for (int j = 0; j < column_count; j++)
                accumulation[(i + 1) * column_count + j] = no_acc_value;
        {
            int edge_directions1[5] = {5, 6, 7, 8, 1};
            int edge_directions2[5] = {1, 2, 3, 4, 5};
            int neighbor_directions[8] = {5, 6, 7, 8, 1, 2, 3, 4};
            unsigned stop = 0;
            int pending_count = 0;
            int x, y, x1, y1, x2, y2;
            int neighbors[16];
            int edge_neighbors[10];
            while (stop == 0) {
                for (int i = 0; i < row_count; i++) {
                    x = i + 1;
                    x1 = x - 1;
                    x2 = x + 1;
                    for (int j = 0; j < column_count; j++) {
                        y = j;
                        y1 = y - 1;
                        y2 = y + 1;
                        int d = direction[x * column_count + y];
                        if (d != no_dir_value) {
                            if (accumulation[x * column_count + y] == -1) {
                                if (j == 0) {
                                    edge_neighbors[0] = accumulation[x1 * column_count + y];
                                    edge_neighbors[1] = accumulation[x1 * column_count + y2];
                                    edge_neighbors[2] = accumulation[x * column_count + y2];
                                    edge_neighbors[3] = accumulation[x2 * column_count + y2];
                                    edge_neighbors[4] = accumulation[x2 * column_count + y];
                                    edge_neighbors[5] = direction[x1 * column_count + y];
                                    edge_neighbors[6] = direction[x1 * column_count + y2];
                                    edge_neighbors[7] = direction[x * column_count + y2];
                                    edge_neighbors[8] = direction[x2 * column_count + y2];
                                    edge_neighbors[9] = direction[x2 * column_count + y];
                                    int res = f_acc(edge_neighbors, no_dir_value, edge_directions1, 5);
                                    if (res != -1)
                                        accumulation[x * column_count + j] = res;
                                    else
                                        pending_count++;
                                } else if (j == (column_count - 1)) {
                                    edge_neighbors[0] = accumulation[x2 * column_count + y];
                                    edge_neighbors[1] = accumulation[x2 * column_count + y1];
                                    edge_neighbors[2] = accumulation[x * column_count + y1];
                                    edge_neighbors[3] = accumulation[x1 * column_count + y1];
                                    edge_neighbors[4] = accumulation[x1 * column_count + y];
                                    edge_neighbors[5] = direction[x2 * column_count + y];
                                    edge_neighbors[6] = direction[x2 * column_count + y1];
                                    edge_neighbors[7] = direction[x * column_count + y1];
                                    edge_neighbors[8] = direction[x1 * column_count + y1];
                                    edge_neighbors[9] = direction[x1 * column_count + y];
                                    int res = f_acc(edge_neighbors, no_dir_value, edge_directions2, 5);
                                    if (res != -1)
                                        accumulation[x * column_count + j] = res;
                                    else
                                        pending_count++;
                                } else {
                                    neighbors[0] = accumulation[x1 * column_count + y];
                                    neighbors[1] = accumulation[x1 * column_count + y2];
                                    neighbors[2] = accumulation[x * column_count + y2];
                                    neighbors[3] = accumulation[x2 * column_count + y2];
                                    neighbors[4] = accumulation[x2 * column_count + y];
                                    neighbors[5] = accumulation[x2 * column_count + y1];
                                    neighbors[6] = accumulation[x * column_count + y1];
                                    neighbors[7] = accumulation[x1 * column_count + y1];
                                    neighbors[8] = direction[x1 * column_count + y];
                                    neighbors[9] = direction[x1 * column_count + y2];
                                    neighbors[10] = direction[x * column_count + y2];
                                    neighbors[11] = direction[x2 * column_count + y2];
                                    neighbors[12] = direction[x2 * column_count + y];
                                    neighbors[13] = direction[x2 * column_count + y1];
                                    neighbors[14] = direction[x * column_count + y1];
                                    neighbors[15] = direction[x1 * column_count + y1];
                                    int res = f_acc(neighbors, no_dir_value, neighbor_directions, 8);
                                    if (res != -1)
                                        accumulation[x * column_count + j] = res;
                                    else
                                        pending_count++;
                                }
                            }
                        }
                    }
                }
                if (pending_count == 0)
                    stop = 1;
                else
                    pending_count = 0;
            }
        }
        o.s("flow accumulation ");
        o.nl();
        for (int i = 0; i < row_count + 2; i++) {
            for (int j = 0; j < column_count; j++) {
                o.i(accumulation[i * column_count + j]);
                o.s(" ");
            }
            o.nl();
        }
        watershed = new int[(row_count + 2) * column_count];
        for (int i = 0; i < row_count + 2; i++)
            for (int j = 0; j < column_count; j++)
                watershed[i * column_count + j] = no_watershed_value;
        {
            int num = first_label;
            int *ptr_direction = direction + column_count;
            for (int i = 0; i < row_count; i++)
                for (int j = 0; j < column_count; j++) {
                    if (ptr_direction[i * column_count + j] != no_dir_value) {
                        if (watershed[(i + 1) * column_count + j] == -1)
                            watershed[(i + 1) * column_count + j] = f_watershed(watershed + column_count, ptr_direction,
                                                                                row_count, column_count, i, j, &num);
                    } else
                        watershed[(i + 1) * column_count + j] = no_watershed_value;
                }
        }
        o.s("watersheds");
        o.nl();
        for (int i = 0; i < row_count + 2; i++) {
            for (int j = 0; j < column_count; j++) {
                o.i(watershed[i * column_count + j]);
                o.s(" ");
            }
            o.nl();
        }
    } catch (const std::exception &error) {
        what = error.what();
        err = what.c_str();
        rc = 1;
    }
    if (!o.b.empty())
        write_all(o.b.data(), o.b.size());
    if (err) {
        std::string m = std::string("Cannot process terrain: ") + err + "\n";
        if (write(2, m.data(), m.size()) < 0) {
        }
    }
    delete[] terrain;
    delete[] direction;
    delete[] accumulation;
    delete[] watershed;
    return rc;
}
} // namespace refp
static void run_serial_fallback() {
    const long M = g.M;
    const size_t rb = ((size_t)8 * ((size_t)M + 8) * 4 + 4095) & ~(size_t)4095;
    float *rows = (float *)mmap(0, rb, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (rows == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    g.rows = rows;
    run_serial();
    _exit(0);
}
#ifndef RMIN
#define RMIN 4
#endif
#ifndef MTHICK
#define MTHICK 32768L
#endif
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    PM("start");
    const char *buf = 0;
    size_t len = 0;
    {
        struct stat st;
        if (fstat(0, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
            const off_t cur = lseek(0, 0, SEEK_CUR);
            if (cur >= 0 && cur < st.st_size) {
                const size_t fs = (size_t)st.st_size;
                const size_t pg = 4096;
                const size_t rl = (fs + pg - 1) / pg * pg + pg;
                void *r = mmap(0, rl, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                if (r != MAP_FAILED) {
                    void *f = mmap(
                        r, fs, PROT_READ,
                        MAP_PRIVATE | MAP_FIXED | ((POPULATE && fs <= ((size_t)256 << 10)) ? MAP_POPULATE : 0), 0, 0);
                    if (f == r) {
                        buf = (const char *)f + cur;
                        len = fs - (size_t)cur;
                        g_inmap = (char *)r;
                        g_inmaplen = rl;
                    } else
                        munmap(r, rl);
                }
            }
        }
        if (!buf) {
            size_t cap = (size_t)1 << 16, nb = 0;
            char *o = (char *)malloc(cap + 1);
            if (!o) {
                perror("malloc");
                exit(1);
            }
            for (;;) {
                if (nb == cap) {
                    cap *= 2;
                    o = (char *)realloc(o, cap + 1);
                    if (!o) {
                        perror("realloc");
                        exit(1);
                    }
                }
                const ssize_t rr = read(0, o + nb, cap - nb);
                if (rr < 0) {
                    if (errno == EINTR)
                        continue;
                    perror("read");
                    exit(1);
                }
                if (rr == 0)
                    break;
                nb += (size_t)rr;
            }
            o = (char *)realloc(o, nb + 64);
            if (!o) {
                perror("realloc");
                exit(1);
            }
            memset(o + nb, 0, 64);
            buf = o;
            len = nb;
        }
    }
    PM("read");
    const char *p = buf;
    const char *const end = buf + len;
    long hv[5];
    float nv;
    for (int k = 0; k < 5; k++) {
        while (p < end && fio::is_ws((unsigned char)*p))
            ++p;
        char *e;
        hv[k] = strtol(p, &e, 10);
        p = e;
    }
    while (p < end && fio::is_ws((unsigned char)*p))
        ++p;
    p = fio::parse_float(p, &nv);
    const int n = (int)hv[0], m = (int)hv[1];
    if (n < 1 || m < 2)
        return refp::run(n, m, nv, p, end);
    const long V = (long)n * m;
    const long M = m;
    long tl;
    if (V <= BIG_CELLS) {
        tl = (V + CPT - 1) / CPT;
        if (tl > TSMALL)
            tl = TSMALL;
    } else {
        tl = (V + CPT_BIG - 1) / CPT_BIG;
        if (tl < TMID)
            tl = TMID;
        if (tl > TBIG)
            tl = TBIG;
    }
    if (tl > TMAXP)
        tl = TMAXP;
    if (V >= (1L << 30))
        tl = 1;
    if (p < end && (unsigned char)*p > ' ')
        tl = 1;
    int T = tl < 1 ? 1 : (int)tl;
    if (T > 1) {
        cpu_set_t cs;
        if (sched_getaffinity(0, sizeof cs, &cs) == 0) {
            const int c = CPU_COUNT(&cs);
            if (T > c)
                T = c;
            const int me = sched_getcpu();
            int k = 0;
#if NODE_FIRST
            {
                unsigned gc = 0, nd = 0;
                if (me >= 0 && syscall(SYS_getcpu, &gc, &nd, (void *)0) == 0 && nd < 4096) {
                    char path[64], b[256];
                    snprintf(path, sizeof path, "/sys/devices/system/node/node%u/cpulist", nd);
                    const int fd = open(path, O_RDONLY);
                    ssize_t r = fd >= 0 ? read(fd, b, sizeof b - 1) : -1;
                    if (fd >= 0)
                        close(fd);
                    long lo = -1, hi = -1;
                    if (r > 0) {
                        b[r] = 0;
                        for (char *q = b; *q >= '0' && *q <= '9';) {
                            char *e;
                            const long a = strtol(q, &e, 10);
                            long z = a;
                            if (*e == '-')
                                z = strtol(e + 1, &e, 10);
                            if (me >= a && me <= z) {
                                lo = a;
                                hi = z;
                                break;
                            }
                            if (*e != ',')
                                break;
                            q = e + 1;
                        }
                    }
                    if (lo >= 0) {
                        int lst[CPU_SETSIZE], cnt = 0, idx = -1;
                        for (long cpu = lo; cpu <= hi && cpu < CPU_SETSIZE; cpu++)
                            if (CPU_ISSET(cpu, &cs)) {
                                if (cpu == me)
                                    idx = cnt;
                                lst[cnt++] = (int)cpu;
                            }
                        if (idx >= 0 && cnt >= T) {
                            int st = idx < cnt - T ? idx : cnt - T;
                            g_cpu[k++] = me;
                            for (int i = st; i < st + T; i++)
                                if (i != idx)
                                    g_cpu[k++] = lst[i];
                        }
                    }
                }
            }
#endif
            for (int pass = 0; pass < 2 && k < T; pass++)
                for (int cpu = 0; cpu < CPU_SETSIZE && k < T; cpu++)
                    if (CPU_ISSET(cpu, &cs) && (pass == 0 ? cpu >= me : cpu < me))
                        g_cpu[k++] = cpu;
            if (k < T)
                T = k;
            if (T > 1) {
                cpu_set_t one;
                CPU_ZERO(&one);
                CPU_SET(g_cpu[0], &one);
                sched_setaffinity(0, sizeof one, &one);
            }
        } else
            T = 1;
        if (T < 1)
            T = 1;
    }
    g_T = T;
    PM("hdr");
    g.text = p;
    g.end = end;
    g.n = n;
    g.M = M;
    g.V = V;
    g.nv = nv;
    {
        const long off[9] = {0, -M, -M + 1, 1, M + 1, M, M - 1, -1, -M - 1};
        memcpy(g.off, off, sizeof off);
    }
    const size_t dpad = (size_t)M + 64;
    if (T == 1) {
        const size_t dbytes = ((size_t)V + 2 * dpad + 4095) & ~(size_t)4095;
        const size_t abytes = ((size_t)V * 4 + 4095) & ~(size_t)4095;
        const size_t rbytes = ((size_t)8 * ((size_t)M + 8) * 4 + 4095) & ~(size_t)4095;
        const size_t total = dbytes + abytes + rbytes;
        char *mem = (char *)mmap(0, total, PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS |
                                     ((POPULATE && total <= ((size_t)2 << 20)) ? MAP_POPULATE : MAP_NORESERVE),
                                 -1, 0);
        if (mem == MAP_FAILED) {
            perror("mmap");
            exit(1);
        }
        g.D = (uint8_t *)mem + dpad;
        g.A = (uint32_t *)(mem + dbytes);
        g.rows = (float *)(mem + dbytes + abytes);
        memset(g.D - dpad, (int)NOD, dpad);
        memset(g.D + V, (int)NOD, dpad);
        run_serial();
        return 0;
    }
    g_thin = !((long)n >= (long)RMIN * T && M <= MTHICK);
    for (int t = 0; t <= T; t++)
        g_bc0[t] = g_thin ? V * t / T : (long)n * t / T * M;
    long B = 0;
    for (int t = 0; t < T; t++)
        if (g_bc0[t + 1] - g_bc0[t] > B)
            B = g_bc0[t + 1] - g_bc0[t];
    {
        long bits = RBITS_MIN;
        while (bits < 24 && (V >> (bits + 1)) >= RUL_PER_T * T)
            bits++;
        g_rbits = bits;
    }
    const size_t H = (size_t)2 << 20;
    const size_t dbytes = ((size_t)V + 2 * dpad + 4095) & ~(size_t)4095;
    const size_t abytes = ((size_t)V * 4 + 4095) & ~(size_t)4095;
    const size_t fbytes = ((size_t)(V + 1) * 4 + 4095) & ~(size_t)4095;
    const size_t rlbytes = (((size_t)(V >> g_rbits) + 2 * TMAXP + 16) * sizeof(Rul) + 4095) & ~(size_t)4095;
    const size_t ebytes = g_thin ? (((size_t)V + 2 * (size_t)M + 32) * 4 + 4095) & ~(size_t)4095 : 0;
    const size_t total = dbytes + abytes + fbytes + rlbytes + ebytes;
    char *mem = (char *)mmap(0, total + H, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (mem == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    mem = (char *)(((uintptr_t)mem + H - 1) & ~(uintptr_t)(H - 1));
    g_huge = HUGE_MEM && V > BIG_CELLS;
#if defined(MADV_HUGEPAGE) && defined(MADV_NOHUGEPAGE)
    madvise(mem, total, g_huge ? MADV_HUGEPAGE : MADV_NOHUGEPAGE);
#endif
    g_mem = mem;
    g_memlen = total;
    g.D = (uint8_t *)mem + dpad;
    g.A = (uint32_t *)(mem + dbytes);
    g_fmin = (uint32_t *)(mem + dbytes + abytes);
    g_rulall = (Rul *)(mem + dbytes + abytes + fbytes);
    g_E = g_thin ? (float *)(mem + dbytes + abytes + fbytes + rlbytes) + M + 16 : 0;
    g_rowsz = g_thin ? 64 : (long)((((size_t)7 * ((size_t)M + 8) * 4) + 63) & ~(size_t)63);
    g_exitsz = (long)((((size_t)((B < 2 * M + 2 ? B : 2 * M + 2) + 16)) * sizeof(long) + 63) & ~(size_t)63);
    g_srcbsz = (long)((((size_t)(B + 63) / 64 + 2) * 8 + 63) & ~(size_t)63);
    g_parksz = (long)((((size_t)((V + T - 1) / T) + 4) * 8 + 63) & ~(size_t)63);
    const size_t textsz = (size_t)B * 16 + (size_t)(B / M + 4) + 4096;
    g_textsz = textsz;
    g_regsz = ((size_t)g_rowsz + g_exitsz + g_srcbsz + g_parksz + textsz + H - 1) & ~(H - 1);
    char *rr = (char *)mmap(0, (size_t)T * g_regsz + H, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (rr == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    g_reg = (char *)(((uintptr_t)rr + H - 1) & ~(uintptr_t)(H - 1));
#if defined(MADV_HUGEPAGE) && defined(MADV_NOHUGEPAGE)
    madvise(g_reg, (size_t)T * g_regsz, HUGE_REG && V > BIG_CELLS ? MADV_HUGEPAGE : MADV_NOHUGEPAGE);
#endif
    g_stk = (char *)mmap(0, (size_t)T * STACK, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_STACK, -1, 0);
    if (g_stk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    g_dpad = (long)dpad;
    {
        const long tb = (long)(end - p);
        long ns = tb / T / SUBBYTES;
        if (ns < 1)
            ns = 1;
        if (ns > NSUBMAX)
            ns = NSUBMAX;
        g_nc = ns * T;
        long gr = g_nc / (4L * T);
        if (gr > CGRAB)
            gr = CGRAB;
        g_cgrab = gr < 1 ? 1 : (int)gr;
    }
    PM("alloc");
    band_work(0);
#if PROF
    fprintf(stderr, "PAR n=%d m=%d T=%d thin=%d rbits=%ld", n, m, T, (int)g_thin, g_rbits);
    for (int k = 1; k < g_np; k++)
        fprintf(stderr, " %s=%.3f", g_pn[k], g_pt[k] - g_pt[k - 1]);
    fprintf(stderr, " total=%.3f", g_pt[g_np - 1] - g_pt[0]);
    {
        double st = 0;
        for (int t = 1; t < T; t++)
            if (g_tt[t][0] - g_tt[0][0] > st)
                st = g_tt[t][0] - g_tt[0][0];
        fprintf(stderr, " laststart=%.3f", st);
        long np = 0, nr = 0;
        for (int t = 0; t < T; t++) {
            np += g_ts[t].npark;
            nr += g_ts[t].nrul;
        }
        fprintf(stderr, " npark=%ld nrul=%ld", np, nr);
        for (int k = 1; k < g_ntt[0]; k++) {
            double mx = 0, sm = 0;
            for (int t = 0; t < T; t++) {
                const double d = g_tt[t][k] - g_tt[t][k - 1];
                sm += d;
                if (d > mx)
                    mx = d;
            }
            fprintf(stderr, " s%d=%.3f/%.3f", k, mx, sm / T);
        }
    }
    fprintf(stderr, "\n");
#endif
    _exit(0);
}
#endif
