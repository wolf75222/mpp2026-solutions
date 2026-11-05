#ifdef QF
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <emmintrin.h>
#include <immintrin.h>

#define SYS_read 0
#define SYS_write 1
#define SYS_open 2
#define SYS_close 3
#define SYS_lseek 8
#define SYS_mmap 9
#define SYS_madvise 28
#define SYS_dup2 33
#define SYS_getpid 39
#define SYS_clone 56
#define SYS_execve 59
#define SYS_readlink 89
#define SYS_getrusage 98
#define SYS_gettid 186
#define SYS_sched_setaffinity 203
#define SYS_sched_getaffinity 204
#define SYS_exit_group 231
#define SYS_tgkill 234
#define SYS_pipe2 293
#define SYS_memfd_create 319
#define E_INTR 4

static inline long sys3(long n, long a, long b, long c) {
    long r; __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory"); return r;
}
static inline long sys6(long n, long a, long b, long c, long d, long e, long f) {
    long r; register long r10 __asm__("r10") = d; register long r8 __asm__("r8") = e; register long r9 __asm__("r9") = f;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory"); return r;
}
__attribute__((noreturn)) static void qexit(int c) { for (;;) sys3(SYS_exit_group, c, 0, 0); }

void *memcpy(void *d, const void *s, size_t n) {
    char *dc = d; const char *sc = s;
    if (n <= 16) {
        if (n >= 8) { uint64_t a, b; __builtin_memcpy(&a, sc, 8); __builtin_memcpy(&b, sc + n - 8, 8); __builtin_memcpy(dc, &a, 8); __builtin_memcpy(dc + n - 8, &b, 8); }
        else if (n >= 4) { uint32_t a, b; __builtin_memcpy(&a, sc, 4); __builtin_memcpy(&b, sc + n - 4, 4); __builtin_memcpy(dc, &a, 4); __builtin_memcpy(dc + n - 4, &b, 4); }
        else if (n) { char a = sc[0], b = sc[n >> 1], c = sc[n - 1]; dc[0] = a; dc[n >> 1] = b; dc[n - 1] = c; }
        return d;
    }
    if (n <= 32) { __m128i a = _mm_loadu_si128((const __m128i *)sc), b = _mm_loadu_si128((const __m128i *)(sc + n - 16)); _mm_storeu_si128((__m128i *)dc, a); _mm_storeu_si128((__m128i *)(dc + n - 16), b); return d; }
    __asm__ volatile("rep movsb" : "+D"(dc), "+S"(sc), "+c"(n) : : "memory");
    return d;
}
void *memset(void *d, int c, size_t n) { void *r = d; __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory"); return r; }
__attribute__((optimize("no-tree-loop-distribute-patterns"))) void *memmove(void *d, const void *s, size_t n) {
    if ((uintptr_t)d - (uintptr_t)s >= n) return memcpy(d, s, n);
    char *dc = d; const char *sc = s;
    while (n >= 16) { n -= 16; _mm_storeu_si128((__m128i *)(dc + n), _mm_loadu_si128((const __m128i *)(sc + n))); }
    while (n) { n--; dc[n] = sc[n]; }
    return d;
}
int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *p = a, *q = b;
    for (size_t i = 0; i < n; i++) if (p[i] != q[i]) return (int)p[i] - (int)q[i];
    return 0;
}

__asm__(".pushsection .text\n.globl _start\n.type _start,@function\n_start:\n\txor %ebp,%ebp\n\tmov %rsp,%rdi\n\tand $-16,%rsp\n\tcall qf_start\n\thlt\n"
        ".globl qf_clone\n.type qf_clone,@function\nqf_clone:\n\tand $-16,%rsi\n\tsub $16,%rsi\n\tmov %rdx,(%rsi)\n\tmov %rcx,8(%rsi)\n"
        "\tmov $56,%eax\n\txor %edx,%edx\n\txor %r10d,%r10d\n\txor %r8d,%r8d\n\tsyscall\n\ttest %rax,%rax\n\tjnz 1f\n"
        "\txor %ebp,%ebp\n\tpop %rax\n\tpop %rdi\n\tcall *%rax\n\tmov $60,%eax\n\txor %edi,%edi\n\tsyscall\n\thlt\n1:\tret\n.popsection\n");
long qf_clone(unsigned long flags, void *stack_top, void (*fn)(void *), void *arg);
#define CLONE_THR 0x50F00UL

static char **g_argv, **g_envp;
static char g_inbuf[65536], *g_in = g_inbuf;
static size_t g_inlen = 0, g_incap = sizeof g_inbuf;
static long g_in_off0 = -1;
static int g_touched = 0;
static _Atomic int g_ho = 0;

static void *map_anon(size_t sz) {
    long p = sys6(SYS_mmap, 0, (long)sz, 3, 0x22, -1, 0);
    return (p < 0 && p > -4096) ? NULL : (void *)p;
}
static int write_fd(int fd, const char *p, size_t len) {
    while (len) {
        long w = sys3(SYS_write, fd, (long)p, (long)len);
        if (w == -E_INTR) continue;
        if (w <= 0) return 0;
        p += w; len -= (size_t)w;
    }
    return 1;
}
__attribute__((noreturn)) static void handoff(void) {
    int z = 0;
    if (!atomic_compare_exchange_strong(&g_ho, &z, 1)) for (;;) __builtin_ia32_pause();
    char path[4200];
    long r = sys3(SYS_readlink, (long)"/proc/self/exe", (long)path, 4096);
    size_t k = 0;
    if (r > 0 && r < 4096) { k = (size_t)r; while (k > 0 && path[k - 1] != '/') k--; }
    if (k == 0) { path[0] = '.'; path[1] = '/'; k = 2; }
    path[k] = 'q'; path[k + 1] = 't'; path[k + 2] = 'd'; path[k + 3] = 0;
    int ok = !g_touched || (g_in_off0 >= 0 && sys3(SYS_lseek, 0, g_in_off0, 0) == g_in_off0);
    if (!ok) {
        long fd = sys3(SYS_memfd_create, (long)"qt", 0, 0);
        if (fd >= 0) {
            if (write_fd((int)fd, g_in, g_inlen) && sys3(SYS_lseek, fd, 0, 0) == 0 && (fd == 0 || sys3(SYS_dup2, fd, 0, 0) == 0)) ok = 1;
            if (fd != 0) sys3(SYS_close, fd, 0, 0);
        }
    }
    if (!ok && g_inlen <= 65536) {
        int pf[2];
        if (sys3(SYS_pipe2, (long)pf, 0, 0) == 0) {
            if (write_fd(pf[1], g_in, g_inlen) && sys3(SYS_dup2, pf[0], 0, 0) == 0) ok = 1;
            sys3(SYS_close, pf[1], 0, 0); if (pf[0] != 0) sys3(SYS_close, pf[0], 0, 0);
        }
    }
    if (ok) sys3(SYS_execve, (long)path, (long)g_argv, (long)g_envp);
    write_fd(2, "qt: hand-off failed\n", 20);
    qexit(127);
}

#define ALIGNUP(x, a) (((x) + (a) - 1) & ~(size_t)((a) - 1))
static char g_small[1 << 19] __attribute__((aligned(64)));
static _Atomic size_t g_small_used = 0;
static void *xmalloc(size_t sz) {
    size_t need = ALIGNUP(sz, 64) + 64;
    if (need <= (size_t)1 << 16) {
        size_t off = atomic_fetch_add_explicit(&g_small_used, need, memory_order_relaxed);
        if (off + need <= sizeof g_small) { char *p = g_small + off; *(size_t *)p = sz; return p + 64; }
    }
    char *p = map_anon(ALIGNUP(need, 4096));
    if (!p) handoff();
    *(size_t *)p = sz;
    return p + 64;
}
static inline void *xcalloc(size_t n, size_t s) { return xmalloc(n * s); }
static void *xrealloc(void *p, size_t sz) {
    size_t old = *(size_t *)((char *)p - 64);
    if (sz <= old) return p;
    void *q = xmalloc(sz); memcpy(q, p, old); return q;
}
#define malloc(s) xmalloc(s)
#define calloc(n, s) xcalloc((n), (s))
#define realloc(p, s) xrealloc((p), (s))
#define aligned_alloc(a, s) xmalloc(s)
#define free(p) ((void)(p))

static inline double qt_sqrt(double x) { return _mm_cvtsd_f64(_mm_sqrt_sd(_mm_set_sd(x), _mm_set_sd(x))); }
static inline double dmax(double a, double b) { return a > b ? a : b; }
#define fabs(x) __builtin_fabs(x)
#define fabsf(x) __builtin_fabsf((float)(x))
static int g_p1k = 3;
#else
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/resource.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#if defined(__x86_64__) || defined(__SSE2__)
#include <emmintrin.h>
static inline double qt_sqrt(double x) { return _mm_cvtsd_f64(_mm_sqrt_sd(_mm_set_sd(x), _mm_set_sd(x))); }
#else
static inline double qt_sqrt(double x) { return __builtin_sqrt(x); }
#endif
static inline double dmax(double a, double b) { return a > b ? a : b; }
static int g_fastsetup = 1, g_p1k = 3;
static int g_madv = 1, g_earlyfree = -1;
static double g_tfmin = -1;

#endif
#define MAX_CHILDREN 4
#define MAX_ERROR  1E-10
#define MAX_NORMA  1000000
#ifndef QF
#define STRING_SIZE 1024*64
#endif

typedef struct { double x; double y; } tpPoint;
typedef struct { double x_min; double x_max; double y_min; double y_max; } tpRectangle;
typedef struct { tpRectangle map_boundaries; tpRectangle sub_boundaries; uint64_t n_points; } tpConfig;

static bool point_in(tpRectangle r, tpPoint p) {
    if (p.x < r.x_min) return false;
    if (p.x > r.x_max) return false;
    if (p.y < r.y_min) return false;
    if (p.y > r.y_max) return false;
    return true;
}
static bool retangle_in(tpRectangle r2, tpRectangle r1) {
    tpPoint p; bool ret = false;
    p.x = r2.x_min; p.y = r2.y_min; if (point_in(r1, p)) ret = true;
    p.x = r2.x_min; p.y = r2.y_max; if (point_in(r1, p)) ret = true;
    p.x = r2.x_max; p.y = r2.y_min; if (point_in(r1, p)) ret = true;
    p.x = r2.x_max; p.y = r2.y_max; if (point_in(r1, p)) ret = true;
    return ret;
}
static bool intercept(tpRectangle r1, tpRectangle r2) {
    bool r2_in_r1 = retangle_in(r2, r1);
    bool r1_in_r2 = retangle_in(r1, r2);
    return (r2_in_r1 || r1_in_r2);
}
#ifdef QF
__attribute__((noreturn)) static void die_sig(const char *msg, size_t len, int sig) {
    write_fd(2, msg, len);
    long pid = sys3(SYS_getpid, 0, 0, 0), tid = sys3(SYS_gettid, 0, 0, 0);
    sys3(SYS_tgkill, pid, tid, sig);
    sys3(SYS_tgkill, pid, tid, 9);
    qexit(128 + sig);
}
static const char MSG_ASSERT[] = "qt: qt.c: insert_point_quadtree: Assertion `ret >= 0' failed.\n";
static const char MSG_DEPTH[] = "quadtree: unbounded subdivision (reference overflows its stack)\n";
#else
static int g_fastsetup_early = 1;
static void trim(char *dest, char *source) {
    uint64_t len = strlen(source), i = 0, j = 0;
    if (!g_fastsetup_early) memset(dest, 0, STRING_SIZE);
    while (i < len) { if ((source[i] != ' ') && (source[i] != '\n')) dest[j++] = source[i]; i++; }
    dest[j] = 0;
}
static tpConfig load_config_ini(void) {
    tpConfig config;
    static char line[STRING_SIZE], param[STRING_SIZE], value[STRING_SIZE];
    memset(&config, 0, sizeof config);
    while (fgets(line, STRING_SIZE, stdin)) {
        char *token = strtok(line, "=");
        trim(param, token);
        if ((strlen(param) > 0) && (param[0] != '#')) {
            token = strtok(NULL, "=");
            trim(value, token);
            if (strcmp(param, "x_min") == 0) config.map_boundaries.x_min = atof(value);
            else if (strcmp(param, "x_max") == 0) config.map_boundaries.x_max = atof(value);
            else if (strcmp(param, "y_min") == 0) config.map_boundaries.y_min = atof(value);
            else if (strcmp(param, "y_max") == 0) config.map_boundaries.y_max = atof(value);
            else if (strcmp(param, "sub_x_min") == 0) config.sub_boundaries.x_min = atof(value);
            else if (strcmp(param, "sub_x_max") == 0) config.sub_boundaries.x_max = atof(value);
            else if (strcmp(param, "sub_y_min") == 0) config.sub_boundaries.y_min = atof(value);
            else if (strcmp(param, "sub_y_max") == 0) config.sub_boundaries.y_max = atof(value);
            else if (strcmp(param, "num_points") == 0) config.n_points = (uint64_t) atoi(value);
        }
    }
    return config;
}
#endif
static void check_subdivision(const tpRectangle *b) {
    int ret = 1;
    double d = fabsf(b->x_max - b->x_min);
    if (d < MAX_ERROR) ret = -1;
    d = fabsf(b->y_max - b->y_min);
    if (d < MAX_ERROR) ret = -1;
#ifdef QF
    if (ret < 0) die_sig(MSG_ASSERT, sizeof MSG_ASSERT - 1, 6);
#else
    if (ret < 0) {
        fprintf(stderr, "qt: qt.c: insert_point_quadtree: Assertion `ret >= 0' failed.\n");
        abort();
    }
#endif
}
static inline tpRectangle child_rect(const tpRectangle *b, int c, double x_ave, double y_ave) {
    tpRectangle r;
    if (c & 1) { r.x_min = x_ave; r.x_max = b->x_max; } else { r.x_min = b->x_min; r.x_max = x_ave; }
    if (c & 2) { r.y_min = b->y_min; r.y_max = y_ave; } else { r.y_min = y_ave; r.y_max = b->y_max; }
    return r;
}

/* re-implementation of glibc srandom_r/random_r TYPE_3 (glibc stdlib/random_r.c, LGPL-2.1) to reproduce rand() exactly */
static uint32_t g_B[31];
static void rng_base_window(unsigned seed, uint32_t B[31]) {
    int32_t r[34];
    if (seed == 0) seed = 1;
    r[0] = (int32_t)seed; int32_t word = (int32_t)seed;
    for (int i = 1; i < 31; i++) {
        long hi = word / 127773, lo = word % 127773;
        word = (int32_t)(16807 * lo - 2836 * hi);
        if (word < 0) word += 2147483647;
        r[i] = word;
    }
    for (int i = 31; i < 34; i++) r[i] = r[i - 31];
    for (int i = 0; i < 31; i++) B[i] = (uint32_t)r[3 + i];
}
static void poly_mulmod(const uint32_t *a, const uint32_t *b, uint32_t *out) {
    uint32_t t[61];
    memset(t, 0, sizeof t);
    for (int i = 0; i < 31; i++) { uint32_t ai = a[i]; if (!ai) continue; for (int j = 0; j < 31; j++) t[i + j] += ai * b[j]; }
    for (int k = 60; k >= 31; k--) { uint32_t v = t[k]; t[k - 31] += v; t[k - 3] += v; }
    memcpy(out, t, 31 * sizeof(uint32_t));
}
#ifdef QF

static uint32_t g_jx310[31], g_jq[24][31];
static void poly_xpow(uint64_t j, uint32_t out[31]) {
#else
static void rng_window_for_output(uint64_t k, uint32_t W[31]) {
    uint64_t j = k + 310;
#endif
    uint32_t res[31] = {0}, base[31] = {0};
    res[0] = 1; base[1] = 1;
    while (j) { if (j & 1) poly_mulmod(res, base, res); poly_mulmod(base, base, base); j >>= 1; }
#ifdef QF
    memcpy(out, res, sizeof res);
}
static void jump_setup(const uint32_t *x2L, size_t nchunks) {
    poly_xpow(310, g_jx310);
    memcpy(g_jq[0], x2L, sizeof g_jq[0]);
    for (int k = 0; k < 4; k++) poly_mulmod(g_jq[0], g_jq[0], g_jq[0]);
    for (int b = 1; b < 24 && ((size_t)1 << b) < nchunks; b++) poly_mulmod(g_jq[b - 1], g_jq[b - 1], g_jq[b]);
}
static void chunk_window(uint64_t ch, uint32_t W[31]) {
    uint32_t res[31];
    memcpy(res, g_jx310, sizeof res);
    for (int b = 0; ch; b++, ch >>= 1) if (ch & 1) poly_mulmod(res, g_jq[b], res);
#endif
    for (int t = 0; t < 31; t++) {
        uint32_t s = 0;
        for (int i = 0; i < 31; i++) s += res[i] * g_B[i];
        W[t] = s;
        uint32_t carry = res[30];
        for (int i = 30; i > 0; i--) res[i] = res[i - 1];
        res[0] = carry; res[28] += carry;
    }
}
#ifndef QF
static int libc_rand_is_glibc(void) {
#ifdef FORCE_GLIBC_EMU
    return 1;
#else
    uint32_t W[31 + 64];
    if (g_fastsetup_early) {
        uint32_t r[344 + 64];
        for (int i = 0; i < 31; i++) r[3 + i] = g_B[i];
        r[0] = r[31]; r[1] = r[32]; r[2] = r[33];
        for (int i = 34; i < 344 + 64; i++) r[i] = r[i - 31] + r[i - 3];
        for (int i = 0; i < 31 + 64; i++) W[i] = r[313 + i];
    } else {
        rng_window_for_output(0, W);
        for (int i = 31; i < 31 + 64; i++) W[i] = W[i - 31] + W[i - 3];
    }
    srand(42);
    for (int i = 0; i < 64; i++) if (rand() != (int)(W[31 + i] >> 1)) return 0;
    return 1;
#endif
}

#include <time.h>
static int g_prof = 0;
static uint64_t g_ppt = 0;
static double tnow(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + 1e-9 * t.tv_nsec; }
#define PROF(name, t0) do { if (g_prof) { double t1_ = tnow(); fprintf(stderr, "  %-28s %.5f s\n", name, t1_ - (t0)); (t0) = t1_; } } while (0)
#endif
static tpConfig C;
static double Wx, Wy;
#ifndef QF
static int use_emu = 1;
static uint32_t *KX = NULL, *KY = NULL;
#endif

static inline double fx(uint32_t k) { double x = (double)(int)k / (double)MAX_NORMA; x *= Wx; x += C.map_boundaries.x_min; return x; }
static inline double fy(uint32_t k) { double y = (double)(int)k / (double)MAX_NORMA; y *= Wy; y += C.map_boundaries.y_min; return y; }
static inline int kpred(int isx, double t, int gt, long k) {
    if (k >= MAX_NORMA) return 1;
    double v = isx ? fx((uint32_t)k) : fy((uint32_t)k);
    return gt ? (v > t) : (v >= t);
}
static uint32_t kthr(int isx, double t, int gt) {
#ifdef QF
    {
#else
    if (g_fastsetup) {
#endif
        double W = isx ? Wx : Wy, lo0 = isx ? C.map_boundaries.x_min : C.map_boundaries.y_min;
        double gk = (t - lo0) / W * 1e6;
        if (gk > -4.0 && gk < MAX_NORMA + 4.0) {
            long k = (long)gk; if (k < 0) k = 0; if (k > MAX_NORMA) k = MAX_NORMA;
            for (int st = 0; st < 8; st++) {
                if (!kpred(isx, t, gt, k)) { k++; continue; }
                if (k > 0 && kpred(isx, t, gt, k - 1)) { k--; continue; }
                return (uint32_t)k;
            }
        }
    }
    uint32_t lo = 0, hi = MAX_NORMA;
    while (lo < hi) {
        uint32_t mid = (lo + hi) >> 1;
        double v = isx ? fx(mid) : fy(mid);
        bool ok = gt ? (v > t) : (v >= t);
        if (ok) hi = mid; else lo = mid + 1;
    }
    return lo;
}

#ifdef QF
#define CPU_RELAX() __builtin_ia32_pause()
#define ATOMIC_SIZE _Atomic size_t
#define FETCH_ADD(p, v) atomic_fetch_add((p), (v))
#define CPUSET_BYTES 128
typedef struct { uint64_t b[CPUSET_BYTES / 8]; } cpuset_t;
static inline void cs_zero(cpuset_t *s) { memset(s, 0, sizeof *s); }
static inline void cs_set(int c, cpuset_t *s) { if (c >= 0 && c < CPUSET_BYTES * 8) s->b[c >> 6] |= 1ULL << (c & 63); }
static inline int cs_isset(int c, const cpuset_t *s) { return (int)((s->b[c >> 6] >> (c & 63)) & 1); }
static int get_affinity(cpuset_t *s) { cs_zero(s); return sys3(SYS_sched_getaffinity, 0, CPUSET_BYTES, (long)s) > 0 ? 0 : -1; }
static int set_affinity(long tid, const cpuset_t *s) { return sys3(SYS_sched_setaffinity, tid, CPUSET_BYTES, (long)s) == 0 ? 0 : -1; }
static int nthreads(void) {
    cpuset_t cs; int ncpu = 0;
    if (get_affinity(&cs) == 0) for (int c = 0; c < CPUSET_BYTES * 8; c++) ncpu += cs_isset(c, &cs);
    if (ncpu <= 0) handoff();
#else
#define GEN_BLK 2046
static inline void rng_fill(uint32_t *buf) {
    uint32_t a = buf[28], b = buf[29], c = buf[30];
    for (int k = 31; k < 31 + GEN_BLK; k += 3) {
        a += buf[k - 31]; buf[k] = a;
        b += buf[k - 30]; buf[k + 1] = b;
        c += buf[k - 29]; buf[k + 2] = c;
    }
}

typedef void (*job_fn)(int, int, void *);
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#if defined(__x86_64__) || defined(__i386__)
#define CPU_RELAX() __builtin_ia32_pause()
#elif defined(__aarch64__)
#define CPU_RELAX() __asm__ __volatile__("yield" ::: "memory")
#else
#define CPU_RELAX() do { } while (0)
#endif
#ifndef POOL_STACK
#define POOL_STACK (256 * 1024)
#endif
#ifndef POOL_SPIN
#define POOL_SPIN (1L << 20)
#endif
#define POOL_MAX 4096
static int pool_size = 1, pool_target = 1, pool_broken = 0, pool_attr_ok = 0;
static _Atomic int pool_accounted = 0;
static _Atomic unsigned pool_gen = 0;
static _Atomic int pool_done = 0, pool_sleepers = 0;
static job_fn pool_fn; static void *pool_ctx; static int pool_nt;
static pthread_mutex_t pool_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pool_cv = PTHREAD_COND_INITIALIZER;
static pthread_attr_t pool_attr;
static void *pool_worker(void *arg);
static int subtree_size(int c, int T) { int s = 0; long lo = c, hi = c; while (lo < T) { s += (int)((hi < T - 1 ? hi : T - 1) - lo + 1); lo = 2 * lo + 1; hi = 2 * hi + 2; } return s; }
static void pool_spawn(int id, unsigned seen, int tree) {
    pthread_t th;
    uintptr_t a = ((uintptr_t)seen << 17) | ((uintptr_t)tree << 16) | (uintptr_t)id;
    if (pthread_create(&th, pool_attr_ok ? &pool_attr : NULL, pool_worker, (void *)a) != 0) {
        pool_broken = 1;
        atomic_fetch_add(&pool_accounted, tree ? subtree_size(id, pool_target) : 1);
        return;
    }
    pthread_detach(th);
    atomic_fetch_add(&pool_accounted, 1);
}
static job_fn g_ramp_fn; static void *g_ramp_ctx; static int g_ramp_nt;
static _Atomic int g_ramp_state = 0;
static void ramp_cancel(void) { int z = 0; atomic_compare_exchange_strong(&g_ramp_state, &z, 2); }
static void *pool_worker(void *arg) {
    uintptr_t a = (uintptr_t)arg;
    int id = (int)(a & 0xffff), tree = (int)((a >> 16) & 1);
    unsigned seen = (unsigned)(a >> 17);
    if (tree) for (int k = 1; k <= 2; k++) { int c = 2 * id + k; if (c < pool_target) pool_spawn(c, seen, 1); }
    if (tree && seen == 0) {
        int st;
        while ((st = atomic_load(&g_ramp_state)) == 0) CPU_RELAX();
        if (st == 1 && id < g_ramp_nt) g_ramp_fn(id, g_ramp_nt, g_ramp_ctx);
    }
    for (;;) {
        unsigned g; long spins = 0;
        while ((g = atomic_load(&pool_gen)) == seen) {
            CPU_RELAX();
            if (++spins > POOL_SPIN) {
                pthread_mutex_lock(&pool_mx);
                atomic_fetch_add(&pool_sleepers, 1);
                while ((g = atomic_load(&pool_gen)) == seen) pthread_cond_wait(&pool_cv, &pool_mx);
                atomic_fetch_sub(&pool_sleepers, 1);
                pthread_mutex_unlock(&pool_mx);
                break;
            }
        }
        seen = g;
        if (id < pool_nt) pool_fn(id, pool_nt, pool_ctx);
        atomic_fetch_add(&pool_done, 1);
    }
    return NULL;
}
static void pool_attr_init(void) {
    if (!pool_attr_ok && pthread_attr_init(&pool_attr) == 0) { pthread_attr_setstacksize(&pool_attr, POOL_STACK); pool_attr_ok = 1; }
}
__attribute__((unused)) static void pool_start(int T) {
    if (T > POOL_MAX) T = POOL_MAX;
    if (T <= pool_target || pool_target > 1) return;
    pool_attr_init();
    pool_target = T;
    for (int k = 1; k <= 2; k++) if (k < T) pool_spawn(k, atomic_load(&pool_gen), 1);
}
static void pool_ensure(int nt) {
    if (nt > POOL_MAX) nt = POOL_MAX;
    if (pool_target < nt) {
        if (pool_target == 1) { ramp_cancel(); pool_start(nt); }
        else {
            while (atomic_load(&pool_accounted) < pool_target - 1) CPU_RELAX();
            int old = pool_target; pool_target = nt;
            for (int id = old; id < nt; id++) pool_spawn(id, atomic_load(&pool_gen), 0);
        }
    }
    while (atomic_load(&pool_accounted) < pool_target - 1) CPU_RELAX();
    pool_size = pool_target;
}
static void par_run(int nt, job_fn fn, void *ctx) {
    if (nt > POOL_MAX) nt = POOL_MAX;
    if (nt <= 1 || pool_broken) { fn(0, 1, ctx); return; }
    pool_ensure(nt);
    if (pool_broken) { fn(0, 1, ctx); return; }
    pool_fn = fn; pool_ctx = ctx; pool_nt = nt;
    atomic_store(&pool_done, 0);
    atomic_fetch_add(&pool_gen, 1);
    if (atomic_load(&pool_sleepers) > 0) { pthread_mutex_lock(&pool_mx); pthread_cond_broadcast(&pool_cv); pthread_mutex_unlock(&pool_mx); }
    fn(0, nt, ctx);
    while (atomic_load(&pool_done) < pool_size - 1) CPU_RELAX();
}
static int nthreads(void) {
    int ncpu = 0;
#ifdef __linux__
    cpu_set_t cs; if (sched_getaffinity(0, sizeof cs, &cs) == 0) ncpu = CPU_COUNT(&cs);
#endif
    if (ncpu <= 0) { long c = sysconf(_SC_NPROCESSORS_ONLN); ncpu = c > 0 ? (int)c : 1; }
    const char *e = getenv("OMP_NUM_THREADS");
    if (e && atoi(e) > 0 && atoi(e) < ncpu) return atoi(e);
#endif
    return ncpu;
}
#ifndef QF
#define ATOMIC_SIZE _Atomic size_t
#define FETCH_ADD(p, v) atomic_fetch_add((p), (v))
#endif

static void build_node(tpRectangle b, tpPoint *a, tpPoint *tmp, size_t c, tpPoint *out, size_t *r, int emit, int full, int depth) {
    if (emit && !intercept(b, C.sub_boundaries)) emit = 0;
    if (!emit && !full) return;
    if (c <= MAX_CHILDREN) {
        if (emit) for (size_t i = 0; i < c; i++) if (point_in(C.sub_boundaries, a[i])) out[(*r)++] = a[i];
        return;
    }
    check_subdivision(&b);
#ifdef QF
    if (depth > 100000) die_sig(MSG_DEPTH, sizeof MSG_DEPTH - 1, 11);
#else
    if (depth > 100000) { fprintf(stderr, "quadtree: unbounded subdivision (reference overflows its stack)\n"); raise(SIGSEGV); abort(); }
#endif
    double x_ave = (b.x_min + b.x_max) / 2.0;
    double y_ave = (b.y_min + b.y_max) / 2.0;
    size_t cnt[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < c; i++) cnt[(a[i].y < y_ave ? 2 : 0) + (a[i].x > x_ave ? 1 : 0)]++;
    size_t off[4] = {0, cnt[0], cnt[0] + cnt[1], cnt[0] + cnt[1] + cnt[2]}, pos[4];
    memcpy(pos, off, sizeof pos);
    for (size_t i = 0; i < c; i++) tmp[pos[(a[i].y < y_ave ? 2 : 0) + (a[i].x > x_ave ? 1 : 0)]++] = a[i];
    for (int ch = 0; ch < 4; ch++)
        build_node(child_rect(&b, ch, x_ave, y_ave), tmp + off[ch], a + off[ch], cnt[ch], out, r, emit, full, depth + 1);
}

static const char DIG2[201] =
    "00010203040506070809101112131415161718192021222324252627282930313233343536373839"
    "40414243444546474849505152535455565758596061626364656667686970717273747576777879"
    "8081828384858687888990919293949596979899";
#ifndef QF
static char *fmt_slow(char *p, double v) { return p + sprintf(p, "%.1f", v); }
#endif
static inline char *fmt1(char *p, double v) {
    uint64_t bits; memcpy(&bits, &v, 8);
    int ex = (int)((bits >> 52) & 0x7ff);
    uint64_t man = bits & ((1ULL << 52) - 1);
    int e;
#ifdef QF
    if (ex == 0x7ff) __builtin_trap();
#else
    if (ex == 0x7ff) return fmt_slow(p, v);
#endif
    if (ex == 0) e = -1074; else { man |= 1ULL << 52; e = ex - 1075; }
    uint64_t R, t = man * 10;
#ifdef QF
    if (e >= 0) { if (e > 6) __builtin_trap(); R = t << e; }
#else
    if (e >= 0) { if (e > 6) return fmt_slow(p, v); R = t << e; }
#endif
    else {
        int s = -e;
        if (s >= 58) R = 0;
        else {
            uint64_t q = t >> s, rem = t & ((1ULL << s) - 1), half = 1ULL << (s - 1);
            if (rem > half || (rem == half && (q & 1))) q++;
            R = q;
        }
    }
    if (bits >> 63) *p++ = '-';
#ifdef QF
    char tmpd[48]; char *ep = tmpd + 40;
#else
    char tmpd[24]; char *ep = tmpd + 24;
#endif
    while (R >= 100) { uint64_t q = R / 100; unsigned rr = (unsigned)(R - q * 100); ep -= 2; memcpy(ep, DIG2 + 2 * rr, 2); R = q; }
    ep -= 2; memcpy(ep, DIG2 + 2 * R, 2);
#ifdef QF
    if (ep[0] == '0' && ep + 2 < tmpd + 40) ep++;
    size_t nd = (size_t)(tmpd + 40 - ep);
    memcpy(p, ep, 24); p += nd - 1; *p++ = '.'; *p++ = ep[nd - 1];
#else
    if (ep[0] == '0' && ep + 2 < tmpd + 24) ep++;
    size_t nd = (size_t)(tmpd + 24 - ep);
    memcpy(p, ep, nd - 1); p += nd - 1; *p++ = '.'; *p++ = ep[nd - 1];
#endif
    return p;
}
static void write_all(const char *p, size_t len) {
#ifdef QF
    if (!write_fd(1, p, len)) { static const char m[] = "write: Input/output error\n"; write_fd(2, m, sizeof m - 1); qexit(1); }
}
#else
    while (len) { ssize_t w = write(1, p, len); if (w <= 0) { perror("write"); exit(1); } p += w; len -= (size_t)w; }
}
static int g_T1 = 1;
#endif
#ifndef P1_NS
#define P1_NS 1.2
#endif
#ifndef P1_THREAD_US
#define P1_THREAD_US 67.0
#endif
#ifndef TF_MIN_LINES
#define TF_MIN_LINES 1024.0
#endif
#ifndef TS_MIN_ITEMS
#define TS_MIN_ITEMS 1024.0
#endif
#ifndef QF
static double g_tsmin = -1;
static int g_pretouch = 1;
static int threads_for(double work, double ns_per_item, int avail) {
    double t = qt_sqrt(work * ns_per_item * 1e-9 / 37e-6);
    int T = (int)t; if (T < 1) T = 1; if (T > avail) T = avail;
    return T;
}
static double g_p1a = -1, g_p1c = -1;
#endif
#ifndef KEPT_NS
#define KEPT_NS 140.0
#endif
#ifdef QF
static int team_threads(uint64_t n, double kept, int avail) {
    double a = P1_NS * 1e-9, c = P1_THREAD_US * 1e-6;
    double b = KEPT_NS * 1e-9;
#else
static double g_keptns = -1;
static int team_threads(uint64_t n, double kept, int avail) {
    double a = g_p1a > 0 ? g_p1a * 1e-9 : P1_NS * 1e-9, c = g_p1c > 0 ? g_p1c * 1e-6 : P1_THREAD_US * 1e-6;
    double b = g_keptns >= 0 ? g_keptns * 1e-9 : KEPT_NS * 1e-9;
#endif
    double t = qt_sqrt(((double)n * a + kept * b) / c);
    int T = (int)t; if (T < 1) T = 1; if (T > avail) T = avail;
    return T;
}
#ifndef QF
static int reuse_threads(double work, double ns_per_item, double min_items, int avail) {
    int T = threads_for(work, ns_per_item, avail);
    int R = (int)(work / min_items); if (R > g_T1) R = g_T1;
    if (R > T) T = R;
    if (T < 1) T = 1;
    if (T > avail) T = avail;
    return T;
}
#endif
static size_t line_bound(void) {
    double mx = dmax(dmax(fabs(C.map_boundaries.x_min), fabs(C.map_boundaries.x_max)), dmax(fabs(C.map_boundaries.y_min), fabs(C.map_boundaries.y_max)));
    if (!(mx < 1e15)) return 48;
    int k = 1; double p = 10.0; while (p <= mx + 1.0) { p *= 10.0; k++; }
    size_t b = (size_t)(4 + 2 * (k + 3));
    return b < 48 ? b : 48;
}
typedef struct { const tpPoint *res; const size_t *start, *cnt, *pre; size_t nsg, m, per; int T; char *big; size_t *cap0; char **bufs; size_t *lens; } FmtCtx;
static void fmt_job(int tid, int nt, void *vp) {
    FmtCtx *F = (FmtCtx *)vp;
    for (int t = tid; t < F->T; t += nt) {
        size_t a = (size_t)t * F->per, b = a + F->per < F->m ? a + F->per : F->m;
        if (a >= b) continue;
        size_t lo = 0, hi = F->nsg;
        while (hi - lo > 1) { size_t mid = (lo + hi) / 2; if (F->pre[mid] <= a) lo = mid; else hi = mid; }
        size_t sg = lo, off = a - F->pre[lo];
        size_t cap = F->cap0[t], used = 0;
        char *buf = F->bufs[t];
#ifndef QF
        int own = 0;
#endif
        for (size_t g = a; g < b; g++) {
            while (off >= F->cnt[sg]) { sg++; off = 0; }
            const tpPoint *q = F->res + F->start[sg] + off; off++;
            if (cap - used < 1024) {
                size_t nc = 2 * cap + 4096; char *nb = malloc(nc); memcpy(nb, buf, used);
#ifdef QF
                buf = nb; cap = nc;
#else
                if (own) free(buf);
                buf = nb; cap = nc; own = 1;
#endif
            }
            char *p = buf + used;
            *p++ = '(';
            p = fmt1(p, q->x);
            *p++ = ',';
            p = fmt1(p, q->y);
            *p++ = ')'; *p++ = '\n';
            used = (size_t)(p - buf);
        }
        F->bufs[t] = buf; F->lens[t] = used;
#ifdef QF
    }
#else
    }
}
static void print_slices(const tpPoint *res, const size_t *start, const size_t *cnt, size_t nsg, size_t m) {
    fflush(stdout);
    if (m == 0) { write_all("Empty\n", 6); return; }
    int T = reuse_threads((double)m, 40.0, 8192.0, nthreads());
    size_t *pre = malloc(sizeof(size_t) * (nsg + 1));
    pre[0] = 0; for (size_t i = 0; i < nsg; i++) pre[i + 1] = pre[i] + cnt[i];
    size_t per = (m + T - 1) / T;
    size_t one = per * 48 + 1024;
    char *big = malloc(one * (size_t)T);
    char **bufs = malloc(sizeof(char *) * (size_t)T);
    size_t *lens = calloc((size_t)T, sizeof(size_t)), *cap0 = malloc(sizeof(size_t) * ((size_t)T + 1));
    for (int t = 0; t < T; t++) { bufs[t] = big + one * (size_t)t; cap0[t] = one; }
    cap0[T] = 0;
    FmtCtx F = { res, start, cnt, pre, nsg, m, per, T, big, cap0, bufs, lens };
    par_run(T, fmt_job, &F);
    for (int t = 0; t < T; t++) if (lens[t]) write_all(bufs[t], lens[t]);
}
static void print_results(const tpPoint *res, size_t m) { size_t z = 0; print_slices(res, &z, &m, 1, m); }

static void generic_path(void) {
    uint64_t n = C.n_points;
    tpPoint *A = malloc(sizeof(tpPoint) * (n + 1)), *B = malloc(sizeof(tpPoint) * (n + 1)), *res = malloc(sizeof(tpPoint) * (n + 1));
    size_t c = 0;
    tpRectangle root = C.map_boundaries;
    uint32_t W[31 + GEN_BLK]; uint64_t i = 0;
    if (use_emu) rng_window_for_output(0, W);
    while (i < n) {
        if (use_emu) rng_fill(W);
        for (int q = 0; q < GEN_BLK / 2 && i < n; q++, i++) {
            uint32_t kx = use_emu ? (W[31 + 2 * q] >> 1) % MAX_NORMA : KX[i];
            uint32_t ky = use_emu ? (W[32 + 2 * q] >> 1) % MAX_NORMA : KY[i];
            tpPoint p; p.x = fx(kx); p.y = fy(ky);
            if (point_in(root, p)) A[c++] = p;
            else printf("- (% 8.1lf, % 8.1lf) is not inserted ***\n", p.x, p.y);
        }
        if (use_emu) memcpy(W, W + GEN_BLK, 31 * sizeof(uint32_t));
    }
    size_t m = 0;
    build_node(root, A, B, c, res, &m, 1, 1, 0);
    print_results(res, m);
#endif
}

#define DMAX 9
typedef uint32_t v4u __attribute__((vector_size(16)));
#define LANES 16
#define ROWS 256
static int D_;
static uint32_t *TXv, *TYv;
static double *XLO, *XHI, *YLO, *YHI;
static int32_t *relmap;
#define REL_GET(i) (relmap[(i)] - 1)
#define REL_SET(i, v) (relmap[(i)] = (v) + 1)

typedef struct { int8_t l; int32_t xj, yj, parent; } RNode;
static RNode *rn; static size_t nrn, caprn;
static int32_t *cell_x, *cell_y; static size_t ncell;

static inline size_t LO(int l) { return ((size_t)1 << l) - 1; }
static void relevant_rec(int l, int32_t xj, int32_t yj, int32_t parent) {
    tpRectangle r = { XLO[LO(l) + xj], XHI[LO(l) + xj], YLO[LO(l) + yj], YHI[LO(l) + yj] };
    if (!intercept(r, C.sub_boundaries)) return;
    if (nrn == caprn) { caprn *= 2; rn = realloc(rn, caprn * sizeof(RNode)); }
    int32_t me = (int32_t)nrn;
    rn[nrn++] = (RNode){ (int8_t)l, xj, yj, parent };
    if (l == D_) {
        REL_SET(((size_t)yj << D_) | (size_t)xj, (int32_t)ncell);
        cell_x[ncell] = xj; cell_y[ncell] = yj; ncell++;
        return;
    }
    for (int c = 0; c < 4; c++)
        relevant_rec(l + 1, 2 * xj + (c & 1), 2 * yj + ((c & 2) ? 0 : 1), me);
}
#ifndef QF
static int g_full = 0;
static uint8_t *cell_emit = NULL;
static void relevant_rec_full(int l, int32_t xj, int32_t yj, int32_t parent, int emit) {
    tpRectangle r = { XLO[LO(l) + xj], XHI[LO(l) + xj], YLO[LO(l) + yj], YHI[LO(l) + yj] };
    if (emit && !intercept(r, C.sub_boundaries)) emit = 0;
    if (nrn == caprn) { caprn *= 2; rn = realloc(rn, caprn * sizeof(RNode)); }
    int32_t me = (int32_t)nrn;
    rn[nrn++] = (RNode){ (int8_t)l, xj, yj, parent };
    if (l == D_) {
        REL_SET(((size_t)yj << D_) | (size_t)xj, (int32_t)ncell);
        cell_x[ncell] = xj; cell_y[ncell] = yj; cell_emit[ncell] = (uint8_t)emit; ncell++;
        return;
    }
    for (int c = 0; c < 4; c++)
        relevant_rec_full(l + 1, 2 * xj + (c & 1), 2 * yj + ((c & 2) ? 0 : 1), me, emit);
}
#endif
static inline uint32_t xcell(uint32_t kx) { uint32_t j = 0; for (int l = 0; l < D_; l++) j = 2 * j + (kx >= TXv[LO(l) + j]); return j; }
static inline uint32_t ycell(uint32_t ky) { uint32_t j = 0; for (int l = 0; l < D_; l++) j = 2 * j + (ky >= TYv[LO(l) + j]); return j; }

typedef struct { uint64_t *v; size_t n, cap; } KVec;

static void adv_matrix(uint64_t s, uint32_t M[31][31]) {
    uint32_t res[31] = {0}, base[31] = {0};
    res[0] = 1; base[1] = 1;
    while (s) { if (s & 1) poly_mulmod(res, base, res); poly_mulmod(base, base, base); s >>= 1; }
    for (int t = 0; t < 31; t++) {
        memcpy(M[t], res, sizeof res);
        uint32_t carry = res[30];
        for (int i = 30; i > 0; i--) res[i] = res[i - 1];
        res[0] = carry; res[28] += carry;
    }
}

static void *big_alloc(size_t sz) {
#ifdef QF
    if (sz >= ((size_t)2 << 20)) {
        size_t al = (size_t)2 << 20, len = (sz + al - 1) / al * al + al;
        char *p = map_anon(len);
        if (p) { char *q = (char *)(((uintptr_t)p + al - 1) & ~(uintptr_t)(al - 1)); sys3(SYS_madvise, (long)q, (long)(len - (size_t)(q - p)), 14); return q; }
    }
#else
#if defined(__linux__) && defined(MADV_HUGEPAGE)
    if (g_madv && sz >= ((size_t)2 << 20)) {
        size_t al = (size_t)2 << 20, len = (sz + al - 1) / al * al + al;
        char *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p != MAP_FAILED) { char *q = (char *)(((uintptr_t)p + al - 1) & ~(uintptr_t)(al - 1)); madvise(q, len - (size_t)(q - p), MADV_HUGEPAGE); return q; }
    }
#endif
#endif
    return malloc(sz ? sz : 1);
}
typedef struct {
    uint64_t n, L; size_t nchunks; int D; uint32_t bx0, bxw, by0, byw;
    KVec *kv; uint32_t *rings; size_t ringsz; uint8_t *flagsall; uint32_t (*Madv)[31];
    char pad0[64]; ATOMIC_SIZE next; ATOMIC_SIZE finished; char pad1[64];
} P1Ctx;
static inline void p1_cand(P1Ctx *P, const uint32_t *ring, int q, int l, uint64_t blk, uint64_t s0) {
    uint64_t pos = blk * (ROWS / 2) + (uint64_t)q, idx = (s0 + (uint64_t)l) * P->L + pos;
    if (pos >= P->L || idx >= P->n) return;
    uint32_t kx = (ring[(31 + 2 * q) * LANES + l] >> 1) % 1000000u;
    uint32_t ky = (ring[(32 + 2 * q) * LANES + l] >> 1) % 1000000u;
    int32_t rid = REL_GET(((size_t)ycell(ky) << P->D) | xcell(kx));
    if (rid >= 0) {
        KVec *k = &P->kv[s0 + (uint64_t)l];
        if (k->n == k->cap) {
            size_t nc = 2 * k->cap; uint64_t *nv = malloc(nc * sizeof(uint64_t));
            memcpy(nv, k->v, k->n * sizeof(uint64_t));
            k->v = nv; k->cap = nc;
        }
        k->v[k->n++] = ((uint64_t)rid << 40) | ((uint64_t)kx << 20) | ky;
    }
}
#ifdef QF
static int g_avx2 = 0;
__attribute__((target("avx2"))) static inline __m256i mod1e6_avx2(__m256i x) {
    const __m256i M = _mm256_set1_epi64x(1125899907LL);
    __m256i pe = _mm256_mul_epu32(x, M);
    __m256i po = _mm256_mul_epu32(_mm256_srli_epi64(x, 32), M);
    __m256i q = _mm256_or_si256(_mm256_srli_epi64(pe, 50), _mm256_slli_epi64(_mm256_srli_epi64(po, 50), 32));
    return _mm256_sub_epi32(x, _mm256_mullo_epi32(q, _mm256_set1_epi32(1000000)));
}
__attribute__((target("avx2"))) static inline unsigned box_avx2(__m256i x, __m256i y, __m256i BX0, __m256i BXW, __m256i BY0, __m256i BYW) {
    const __m256i MONE = _mm256_set1_epi32(-1);
    __m256i dx = _mm256_sub_epi32(mod1e6_avx2(_mm256_srli_epi32(x, 1)), BX0);
    __m256i dy = _mm256_sub_epi32(mod1e6_avx2(_mm256_srli_epi32(y, 1)), BY0);
    __m256i mx = _mm256_and_si256(_mm256_cmpgt_epi32(BXW, dx), _mm256_cmpgt_epi32(dx, MONE));
    __m256i my = _mm256_and_si256(_mm256_cmpgt_epi32(BYW, dy), _mm256_cmpgt_epi32(dy, MONE));
    return (unsigned)_mm256_movemask_ps(_mm256_castsi256_ps(_mm256_and_si256(mx, my)));
}
#define FRAC_M2 2251799814ULL
__attribute__((target("avx2"))) static inline __m256i frac_avx2(__m256i r) {
    const __m256i M = _mm256_set1_epi64x((long long)FRAC_M2);
    __m256i pe = _mm256_mul_epu32(r, M), po = _mm256_mul_epu32(_mm256_srli_epi64(r, 32), M);
    return _mm256_blend_epi32(_mm256_srli_epi64(_mm256_slli_epi64(pe, 12), 32), _mm256_slli_epi64(po, 12), 0xAA);
}
__attribute__((target("avx2"))) static inline __m256i frac_in(__m256i f, __m256i LO, __m256i WM1) {
    __m256i d = _mm256_sub_epi32(f, LO);
    return _mm256_cmpeq_epi32(_mm256_min_epu32(d, WM1), d);
}
static inline uint32_t frac_lo(uint32_t c) { return c >= MAX_NORMA ? 0 : (uint32_t)(((uint64_t)c << 32) / MAX_NORMA); }
static inline int frac_params(uint32_t a, uint32_t w, uint32_t *lo, uint32_t *wm1) {
    if (w == 0) return 0;
    *lo = frac_lo(a);
    *wm1 = a + w >= MAX_NORMA ? (uint32_t)(0u - *lo) - 1u : frac_lo(a + w) - *lo - 1u;
    return 1;
}
__attribute__((target("avx2"))) static void p1_block_frac(P1Ctx *P, uint32_t *ring, uint64_t blk, uint64_t s0);
extern uint64_t qt_p1_frac_block_avx2(uint32_t *ring, const uint32_t *par, uint64_t *hits);
_Static_assert(ROWS == 256 && LANES == 16, "p1asm.S is written for blocks of 256 rows of 16 lanes");
static void p1_block_asm(P1Ctx *P, uint32_t *ring, uint64_t blk, uint64_t s0) {
    uint32_t par[4]; uint64_t hits[ROWS / 4];
    if (!frac_params(P->bx0, P->bxw, &par[0], &par[1]) || !frac_params(P->by0, P->byw, &par[2], &par[3])) {
        p1_block_frac(P, ring, blk, s0);
        return;
    }
    uint64_t nh = qt_p1_frac_block_avx2(ring, par, hits);
    for (uint64_t h = 0; h < nh; h++) {
        unsigned q = (unsigned)(hits[h] >> 32), m = (unsigned)hits[h];
        while (m) { int l = __builtin_ctz(m); m &= m - 1; p1_cand(P, ring, (int)(q + (unsigned)(l >> 4)), l & 15, blk, s0); }
    }
}
__attribute__((target("avx2"))) static void p1_block_frac(P1Ctx *P, uint32_t *ring, uint64_t blk, uint64_t s0) {
    uint32_t lx = 0, ly = 0, wx = 0, wy = 0;
    int any = frac_params(P->bx0, P->bxw, &lx, &wx) & frac_params(P->by0, P->byw, &ly, &wy);
    const __m256i LX = _mm256_set1_epi32((int)lx), WX = _mm256_set1_epi32((int)wx), LY = _mm256_set1_epi32((int)ly), WY = _mm256_set1_epi32((int)wy);
    __m256i *R = (__m256i *)ring;
    for (int q = 0; q < ROWS / 2; q++) {
        int r = 31 + 2 * q;
        __m256i x0 = _mm256_add_epi32(R[2 * (r - 31)], R[2 * (r - 3)]);
        __m256i x1 = _mm256_add_epi32(R[2 * (r - 31) + 1], R[2 * (r - 3) + 1]);
        __m256i y0 = _mm256_add_epi32(R[2 * (r - 30)], R[2 * (r - 2)]);
        __m256i y1 = _mm256_add_epi32(R[2 * (r - 30) + 1], R[2 * (r - 2) + 1]);
        R[2 * r] = x0; R[2 * r + 1] = x1; R[2 * r + 2] = y0; R[2 * r + 3] = y1;
        __m256i m0 = _mm256_and_si256(frac_in(frac_avx2(x0), LX, WX), frac_in(frac_avx2(y0), LY, WY));
        __m256i m1 = _mm256_and_si256(frac_in(frac_avx2(x1), LX, WX), frac_in(frac_avx2(y1), LY, WY));
        unsigned m = any ? (unsigned)_mm256_movemask_ps(_mm256_castsi256_ps(m0)) | ((unsigned)_mm256_movemask_ps(_mm256_castsi256_ps(m1)) << 8) : 0;
        while (m) { int l = __builtin_ctz(m); m &= m - 1; p1_cand(P, ring, q, l, blk, s0); }
    }
}
__attribute__((target("avx2"))) static void p1_block_avx2(P1Ctx *P, uint32_t *ring, uint64_t blk, uint64_t s0) {
    const __m256i BX0 = _mm256_set1_epi32((int)P->bx0), BXW = _mm256_set1_epi32((int)P->bxw);
    const __m256i BY0 = _mm256_set1_epi32((int)P->by0), BYW = _mm256_set1_epi32((int)P->byw);
    __m256i *R = (__m256i *)ring;
    for (int q = 0; q < ROWS / 2; q++) {
        int r = 31 + 2 * q;
        __m256i x0 = _mm256_add_epi32(R[2 * (r - 31)], R[2 * (r - 3)]);
        __m256i x1 = _mm256_add_epi32(R[2 * (r - 31) + 1], R[2 * (r - 3) + 1]);
        __m256i y0 = _mm256_add_epi32(R[2 * (r - 30)], R[2 * (r - 2)]);
        __m256i y1 = _mm256_add_epi32(R[2 * (r - 30) + 1], R[2 * (r - 2) + 1]);
        R[2 * r] = x0; R[2 * r + 1] = x1; R[2 * r + 2] = y0; R[2 * r + 3] = y1;
        unsigned m = box_avx2(x0, y0, BX0, BXW, BY0, BYW) | (box_avx2(x1, y1, BX0, BXW, BY0, BYW) << 8);
        while (m) { int l = __builtin_ctz(m); m &= m - 1; p1_cand(P, ring, q, l, blk, s0); }
    }
}
#else
#if defined(__x86_64__) && defined(__GNUC__) && !defined(NO_AVX2_PATH)
#include <immintrin.h>
#define HAVE_AVX2_PATH 1
static int g_avx2 = 0;
__attribute__((target("avx2"))) static inline __m256i mod1e6_avx2(__m256i x) {
    const __m256i M = _mm256_set1_epi64x(1125899907LL);
    __m256i pe = _mm256_mul_epu32(x, M);
    __m256i po = _mm256_mul_epu32(_mm256_srli_epi64(x, 32), M);
    __m256i q = _mm256_or_si256(_mm256_srli_epi64(pe, 50), _mm256_slli_epi64(_mm256_srli_epi64(po, 50), 32));
    return _mm256_sub_epi32(x, _mm256_mullo_epi32(q, _mm256_set1_epi32(1000000)));
}
__attribute__((target("avx2"))) static inline unsigned box_avx2(__m256i x, __m256i y, __m256i BX0, __m256i BXW, __m256i BY0, __m256i BYW) {
    const __m256i MONE = _mm256_set1_epi32(-1);
    __m256i dx = _mm256_sub_epi32(mod1e6_avx2(_mm256_srli_epi32(x, 1)), BX0);
    __m256i dy = _mm256_sub_epi32(mod1e6_avx2(_mm256_srli_epi32(y, 1)), BY0);
    __m256i mx = _mm256_and_si256(_mm256_cmpgt_epi32(BXW, dx), _mm256_cmpgt_epi32(dx, MONE));
    __m256i my = _mm256_and_si256(_mm256_cmpgt_epi32(BYW, dy), _mm256_cmpgt_epi32(dy, MONE));
    return (unsigned)_mm256_movemask_ps(_mm256_castsi256_ps(_mm256_and_si256(mx, my)));
}
__attribute__((target("avx2"))) static inline unsigned xbox_avx2(__m256i x, __m256i BX0, __m256i BXW) {
    const __m256i MONE = _mm256_set1_epi32(-1);
    __m256i dx = _mm256_sub_epi32(mod1e6_avx2(_mm256_srli_epi32(x, 1)), BX0);
    return (unsigned)_mm256_movemask_ps(_mm256_castsi256_ps(_mm256_and_si256(_mm256_cmpgt_epi32(BXW, dx), _mm256_cmpgt_epi32(dx, MONE))));
}
static int g_yskip = 0;
#define FRAC_M2 2251799814ULL
__attribute__((target("avx2"))) static inline __m256i frac_avx2(__m256i r) {
    const __m256i M = _mm256_set1_epi64x((long long)FRAC_M2);
    __m256i pe = _mm256_mul_epu32(r, M), po = _mm256_mul_epu32(_mm256_srli_epi64(r, 32), M);
    return _mm256_blend_epi32(_mm256_srli_epi64(_mm256_slli_epi64(pe, 12), 32), _mm256_slli_epi64(po, 12), 0xAA);
}
__attribute__((target("avx2"))) static inline __m256i frac_in(__m256i f, __m256i LO, __m256i WM1) {
    __m256i d = _mm256_sub_epi32(f, LO);
    return _mm256_cmpeq_epi32(_mm256_min_epu32(d, WM1), d);
}
static inline uint32_t frac_lo(uint32_t c) { return c >= MAX_NORMA ? 0 : (uint32_t)(((uint64_t)c << 32) / MAX_NORMA); }
static inline int frac_params(uint32_t a, uint32_t w, uint32_t *lo, uint32_t *wm1) {
    if (w == 0) return 0;
    *lo = frac_lo(a);
    *wm1 = a + w >= MAX_NORMA ? (uint32_t)(0u - *lo) - 1u : frac_lo(a + w) - *lo - 1u;
    return 1;
}
__attribute__((target("avx2"))) static void p1_block_frac(P1Ctx *P, uint32_t *ring, uint64_t blk, uint64_t s0);
extern uint64_t qt_p1_frac_block_avx2(uint32_t *ring, const uint32_t *par, uint64_t *hits);
_Static_assert(ROWS == 256 && LANES == 16, "p1asm.S is written for blocks of 256 rows of 16 lanes");
static void p1_block_asm(P1Ctx *P, uint32_t *ring, uint64_t blk, uint64_t s0) {
    uint32_t par[4]; uint64_t hits[ROWS / 4];
    if (!frac_params(P->bx0, P->bxw, &par[0], &par[1]) || !frac_params(P->by0, P->byw, &par[2], &par[3])) {
        p1_block_frac(P, ring, blk, s0);
        return;
    }
    uint64_t nh = qt_p1_frac_block_avx2(ring, par, hits);
    for (uint64_t h = 0; h < nh; h++) {
        unsigned q = (unsigned)(hits[h] >> 32), m = (unsigned)hits[h];
        while (m) { int l = __builtin_ctz(m); m &= m - 1; p1_cand(P, ring, (int)(q + (unsigned)(l >> 4)), l & 15, blk, s0); }
    }
}
__attribute__((target("avx2"))) static void p1_block_frac(P1Ctx *P, uint32_t *ring, uint64_t blk, uint64_t s0) {
    uint32_t lx = 0, ly = 0, wx = 0, wy = 0;
    int any = frac_params(P->bx0, P->bxw, &lx, &wx) & frac_params(P->by0, P->byw, &ly, &wy);
    const __m256i LX = _mm256_set1_epi32((int)lx), WX = _mm256_set1_epi32((int)wx), LY = _mm256_set1_epi32((int)ly), WY = _mm256_set1_epi32((int)wy);
    __m256i *R = (__m256i *)ring;
    for (int q = 0; q < ROWS / 2; q++) {
        int r = 31 + 2 * q;
        __m256i x0 = _mm256_add_epi32(R[2 * (r - 31)], R[2 * (r - 3)]);
        __m256i x1 = _mm256_add_epi32(R[2 * (r - 31) + 1], R[2 * (r - 3) + 1]);
        __m256i y0 = _mm256_add_epi32(R[2 * (r - 30)], R[2 * (r - 2)]);
        __m256i y1 = _mm256_add_epi32(R[2 * (r - 30) + 1], R[2 * (r - 2) + 1]);
        R[2 * r] = x0; R[2 * r + 1] = x1; R[2 * r + 2] = y0; R[2 * r + 3] = y1;
        __m256i m0 = _mm256_and_si256(frac_in(frac_avx2(x0), LX, WX), frac_in(frac_avx2(y0), LY, WY));
        __m256i m1 = _mm256_and_si256(frac_in(frac_avx2(x1), LX, WX), frac_in(frac_avx2(y1), LY, WY));
        unsigned m = any ? (unsigned)_mm256_movemask_ps(_mm256_castsi256_ps(m0)) | ((unsigned)_mm256_movemask_ps(_mm256_castsi256_ps(m1)) << 8) : 0;
        while (m) { int l = __builtin_ctz(m); m &= m - 1; p1_cand(P, ring, q, l, blk, s0); }
    }
}
__attribute__((target("avx2"))) static void p1_block_avx2(P1Ctx *P, uint32_t *ring, uint64_t blk, uint64_t s0) {
    const __m256i BX0 = _mm256_set1_epi32((int)P->bx0), BXW = _mm256_set1_epi32((int)P->bxw);
    const __m256i BY0 = _mm256_set1_epi32((int)P->by0), BYW = _mm256_set1_epi32((int)P->byw);
    __m256i *R = (__m256i *)ring;
    for (int q = 0; q < ROWS / 2; q++) {
        int r = 31 + 2 * q;
        __m256i x0 = _mm256_add_epi32(R[2 * (r - 31)], R[2 * (r - 3)]);
        __m256i x1 = _mm256_add_epi32(R[2 * (r - 31) + 1], R[2 * (r - 3) + 1]);
        __m256i y0 = _mm256_add_epi32(R[2 * (r - 30)], R[2 * (r - 2)]);
        __m256i y1 = _mm256_add_epi32(R[2 * (r - 30) + 1], R[2 * (r - 2) + 1]);
        R[2 * r] = x0; R[2 * r + 1] = x1; R[2 * r + 2] = y0; R[2 * r + 3] = y1;
        unsigned m;
        if (g_yskip) {
            unsigned m0 = xbox_avx2(x0, BX0, BXW), m1 = xbox_avx2(x1, BX0, BXW);
            if (m0) m0 &= box_avx2(x0, y0, BX0, BXW, BY0, BYW);
            if (m1) m1 &= box_avx2(x1, y1, BX0, BXW, BY0, BYW);
            m = m0 | (m1 << 8);
        } else m = box_avx2(x0, y0, BX0, BXW, BY0, BYW) | (box_avx2(x1, y1, BX0, BXW, BY0, BYW) << 8);
        while (m) { int l = __builtin_ctz(m); m &= m - 1; p1_cand(P, ring, q, l, blk, s0); }
    }
}
#endif
#endif
static void phase1_job(int tid, int nt, void *vp) {
    (void)nt;
    P1Ctx *P = (P1Ctx *)vp;
    const uint64_t n = P->n, L = P->L; const int D = P->D;
    const uint32_t bx0 = P->bx0, bxw = P->bxw, by0 = P->by0, byw = P->byw;
    uint32_t *ring = P->rings + P->ringsz * (size_t)tid;
    uint8_t *flags = P->flagsall + (size_t)(ROWS / 2) * LANES * (size_t)tid;
    for (;;) {
        size_t ch = FETCH_ADD(&P->next, 1);
        if (ch >= P->nchunks) break;
        uint64_t s0 = ch * LANES;
        uint64_t nblk = (L + ROWS / 2 - 1) / (ROWS / 2);
#ifdef QF
        {
            uint32_t w[31], w2[31];
            chunk_window(ch, w);
#else
        if (use_emu) {
            uint32_t w[31], w2[31];
            rng_window_for_output(2 * s0 * L, w);
#endif
            for (int l = 0; l < LANES; l++) {
                for (int t = 0; t < 31; t++) ring[t * LANES + l] = w[t];
                for (int t = 0; t < 31; t++) { uint32_t s = 0; for (int i = 0; i < 31; i++) s += P->Madv[t][i] * w[i]; w2[t] = s; }
                memcpy(w, w2, sizeof w);
            }
        }
#ifdef QF
        if (g_avx2) {
            for (uint64_t blk = 0; blk < nblk; blk++) {
                if (g_p1k == 3) p1_block_asm(P, ring, blk, s0); else
                if (g_p1k == 2) p1_block_frac(P, ring, blk, s0); else
                p1_block_avx2(P, ring, blk, s0);
                memcpy(ring, ring + ROWS * LANES, sizeof(uint32_t) * 31 * LANES);
            }
            FETCH_ADD(&P->finished, 1);
            continue;
        }
        for (uint64_t blk = 0; blk < nblk; blk++) {
            for (int r = 31; r < 31 + ROWS; r++)
                for (int l = 0; l < LANES / 4; l++)
                    ((v4u *)ring)[r * (LANES / 4) + l] = ((v4u *)ring)[(r - 31) * (LANES / 4) + l] + ((v4u *)ring)[(r - 3) * (LANES / 4) + l];
#else
#ifdef HAVE_AVX2_PATH
        if (use_emu && g_avx2) {
            for (uint64_t blk = 0; blk < nblk; blk++) {
                if (g_p1k == 3) p1_block_asm(P, ring, blk, s0); else
                if (g_p1k == 2) p1_block_frac(P, ring, blk, s0); else
                p1_block_avx2(P, ring, blk, s0);
                memcpy(ring, ring + ROWS * LANES, sizeof(uint32_t) * 31 * LANES);
            }
            FETCH_ADD(&P->finished, 1);
            continue;
        }
#endif
        for (uint64_t blk = 0; blk < nblk; blk++) {
            if (use_emu) {
                for (int r = 31; r < 31 + ROWS; r++)
                    for (int l = 0; l < LANES / 4; l++)
                        ((v4u *)ring)[r * (LANES / 4) + l] = ((v4u *)ring)[(r - 31) * (LANES / 4) + l] + ((v4u *)ring)[(r - 3) * (LANES / 4) + l];
            } else {
                for (int q = 0; q < ROWS / 2; q++)
                    for (int l = 0; l < LANES; l++) {
                        uint64_t idx = (s0 + l) * L + blk * (ROWS / 2) + q;
                        uint32_t a = 0, b = 0;
                        if (blk * (ROWS / 2) + q < L && idx < n) { a = KX[idx] << 1; b = KY[idx] << 1; }
                        ring[(31 + 2 * q) * LANES + l] = a; ring[(32 + 2 * q) * LANES + l] = b;
                    }
            }
#endif
            const uint32_t *rg = ring + 31 * LANES;
            for (int q = 0; q < ROWS / 2; q++)
                for (int l = 0; l < LANES; l++) {
                    uint32_t kx = (rg[(2 * q) * LANES + l] >> 1) % 1000000u;
                    uint32_t ky = (rg[(2 * q + 1) * LANES + l] >> 1) % 1000000u;
                    flags[q * LANES + l] = (uint8_t)((kx - bx0 < bxw) & (ky - by0 < byw));
                }
            for (int q = 0; q < ROWS / 2; q++) {
                uint64_t w0, w1; memcpy(&w0, flags + q * LANES, 8); memcpy(&w1, flags + q * LANES + 8, 8);
                if (!(w0 | w1)) continue;
                for (int l = 0; l < LANES; l++) {
                    if (!flags[q * LANES + l]) continue;
                    uint64_t pos = blk * (ROWS / 2) + q, idx = (s0 + l) * L + pos;
                    if (pos >= L || idx >= n) continue;
                    uint32_t kx = (rg[(2 * q) * LANES + l] >> 1) % 1000000u;
                    uint32_t ky = (rg[(2 * q + 1) * LANES + l] >> 1) % 1000000u;
                    int32_t rid = REL_GET(((size_t)ycell(ky) << D) | xcell(kx));
                    if (rid >= 0) {
                        KVec *k = &P->kv[s0 + l];
                        if (k->n == k->cap) {
                            size_t nc = 2 * k->cap; uint64_t *nv = malloc(nc * sizeof(uint64_t));
                            memcpy(nv, k->v, k->n * sizeof(uint64_t));
                            k->v = nv; k->cap = nc;
                        }
                        k->v[k->n++] = ((uint64_t)rid << 40) | ((uint64_t)kx << 20) | ky;
                    }
                }
            }
#ifdef QF
            memcpy(ring, ring + ROWS * LANES, sizeof(uint32_t) * 31 * LANES);
#else
            if (use_emu) memcpy(ring, ring + ROWS * LANES, sizeof(uint32_t) * 31 * LANES);
#endif
        }
        FETCH_ADD(&P->finished, 1);
    }
}
typedef struct { KVec *kv; size_t *sfirst; uint32_t *hist; uint64_t *cellcnt; size_t *base; size_t RD; int TS; tpPoint *A; uint32_t *I; uint64_t L; } SortCtx;
static void hist_job(int tid, int nt, void *vp) {
    SortCtx *S = (SortCtx *)vp;
    for (int t = tid; t < S->TS; t += nt) {
        uint32_t *h = S->hist + (size_t)t * S->RD;
        memset(h, 0, sizeof(uint32_t) * S->RD);
        for (size_t s = S->sfirst[t]; s < S->sfirst[t + 1]; s++) for (size_t i = 0; i < S->kv[s].n; i++) h[S->kv[s].v[i] >> 40]++;
    }
}
static void cellcnt_job(int tid, int nt, void *vp) {
    SortCtx *S = (SortCtx *)vp;
    size_t a = S->RD * (size_t)tid / (size_t)nt, b = S->RD * (size_t)(tid + 1) / (size_t)nt;
    for (size_t c = a; c < b; c++) { uint64_t sum = 0; for (int t2 = 0; t2 < S->TS; t2++) sum += S->hist[(size_t)t2 * S->RD + c]; S->cellcnt[c] = sum; }
}
static void offsets_job(int tid, int nt, void *vp) {
    SortCtx *S = (SortCtx *)vp;
    size_t a = S->RD * (size_t)tid / (size_t)nt, b = S->RD * (size_t)(tid + 1) / (size_t)nt;
    for (size_t c = a; c < b; c++) { uint32_t o = (uint32_t)S->base[c]; for (int t2 = 0; t2 < S->TS; t2++) { uint32_t v = S->hist[(size_t)t2 * S->RD + c]; S->hist[(size_t)t2 * S->RD + c] = o; o += v; } }
}
static void scatter_job(int tid, int nt, void *vp) {
    SortCtx *S = (SortCtx *)vp;
    for (int t = tid; t < S->TS; t += nt) {
        uint32_t *h = S->hist + (size_t)t * S->RD;
        for (size_t s = S->sfirst[t]; s < S->sfirst[t + 1]; s++)
            for (size_t i = 0; i < S->kv[s].n; i++) {
                uint64_t v = S->kv[s].v[i];
                tpPoint p; p.x = fx((uint32_t)((v >> 20) & 0xFFFFF)); p.y = fy((uint32_t)(v & 0xFFFFF));
                uint32_t o = h[v >> 40]++;
                S->A[o] = p;
#ifndef QF
                if (S->I) S->I[o] = (uint32_t)(s * S->L + i);
#endif
            }
    }
}
typedef struct {
#ifdef QF
    tpPoint *A, *tmp; size_t tmpstride; tpPoint *res; size_t *base, *rc; size_t RD; int D;
#else
    tpPoint *A, *tmp; size_t tmpstride; tpPoint *res; size_t *base, *rc; size_t RD; int D; uint32_t *I, *itmp;
#endif
    char pad0[64]; ATOMIC_SIZE next; char pad1[64];
} P2Ctx;
#ifndef QF
static _Atomic uint64_t g_fail = UINT64_MAX;
static void fail_record(uint64_t trig, int type) {
    uint64_t v = (trig << 2) | (uint64_t)type, cur = atomic_load_explicit(&g_fail, memory_order_relaxed);
    while (v < cur && !atomic_compare_exchange_weak_explicit(&g_fail, &cur, v, memory_order_relaxed, memory_order_relaxed)) { }
}
static void build_full(tpRectangle b, tpPoint *a, uint32_t *ia, tpPoint *t, uint32_t *it, size_t c, tpPoint *out, size_t *r, int emit) {
    if (emit && !intercept(b, C.sub_boundaries)) emit = 0;
    if (c <= MAX_CHILDREN) {
        if (emit) for (size_t i = 0; i < c; i++) if (point_in(C.sub_boundaries, a[i])) out[(*r)++] = a[i];
        return;
    }
    uint64_t trig = ia[4];
    if ((trig << 2) >= atomic_load_explicit(&g_fail, memory_order_relaxed)) return;
    double d = fabsf(b.x_max - b.x_min);
    if (d < MAX_ERROR) { fail_record(trig, 1); return; }
    d = fabsf(b.y_max - b.y_min);
    if (d < MAX_ERROR) { fail_record(trig, 1); return; }
    double x_ave = (b.x_min + b.x_max) / 2.0;
    double y_ave = (b.y_min + b.y_max) / 2.0;
    size_t cnt[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < c; i++) cnt[(a[i].y < y_ave ? 2 : 0) + (a[i].x > x_ave ? 1 : 0)]++;
    for (int ch = 0; ch < 4; ch++) if (cnt[ch] == c) {
        tpRectangle cr = child_rect(&b, ch, x_ave, y_ave);
        if (cr.x_min == b.x_min && cr.x_max == b.x_max && cr.y_min == b.y_min && cr.y_max == b.y_max) { fail_record(trig, 2); return; }
    }
    size_t off[4] = {0, cnt[0], cnt[0] + cnt[1], cnt[0] + cnt[1] + cnt[2]}, pos[4];
    memcpy(pos, off, sizeof pos);
    for (size_t i = 0; i < c; i++) { int ch = (a[i].y < y_ave ? 2 : 0) + (a[i].x > x_ave ? 1 : 0); size_t o = pos[ch]++; t[o] = a[i]; it[o] = ia[i]; }
    for (int ch = 0; ch < 4; ch++)
        build_full(child_rect(&b, ch, x_ave, y_ave), t + off[ch], it + off[ch], a + off[ch], ia + off[ch], cnt[ch], out, r, emit);
}
static void reproduce_failure(uint64_t f) {
    fflush(stdout);
    if ((f & 3) == 1) { fprintf(stderr, "qt: qt.c: insert_point_quadtree: Assertion `ret >= 0' failed.\n"); abort(); }
    fprintf(stderr, "quadtree: unbounded subdivision (reference overflows its stack)\n");
    raise(SIGSEGV); abort();
}
#endif
static void phase2_job(int tid, int nt, void *vp) {
    (void)nt;
    P2Ctx *Q = (P2Ctx *)vp;
    for (;;) {
        size_t c = FETCH_ADD(&Q->next, 1);
        if (c >= Q->RD) break;
        size_t px = LO(Q->D) + (size_t)cell_x[c], py = LO(Q->D) + (size_t)cell_y[c];
        tpRectangle b = { XLO[px], XHI[px], YLO[py], YHI[py] };
        size_t r = 0;
        tpPoint *tmp = Q->tmpstride ? Q->tmp + Q->tmpstride * (size_t)tid : Q->tmp + Q->base[c];
#ifndef QF
        if (Q->I) {
            build_full(b, Q->A + Q->base[c], Q->I + Q->base[c], tmp, Q->itmp + Q->tmpstride * (size_t)tid,
                       Q->base[c + 1] - Q->base[c], Q->res + Q->base[c], &r, cell_emit[c]);
            Q->rc[c] = r;
            continue;
        }
#endif
        build_node(b, Q->A + Q->base[c], tmp, Q->base[c + 1] - Q->base[c], Q->res + Q->base[c], &r, 1, 0, Q->D);
        Q->rc[c] = r;
    }
}

#ifdef QF
#ifndef TEAM_STACK
#define TEAM_STACK (512 * 1024)
#endif
#else
#include <sys/syscall.h>
#ifndef TEAM_STACK
#define TEAM_STACK (256 * 1024)
#endif
#endif
typedef struct FusedCtx {
    P1Ctx *P; KVec *kv; size_t nseg, *soff, tot, maxcell, m, *sfirst, *base, *rc, *fpre, RD;
    uint32_t *hist; uint64_t *cellcnt; tpPoint *A, *scratch;
    int TS, ok, TF, T1, D; uint64_t n;
    SortCtx S; P2Ctx Q2; FmtCtx Fm; char *fbig; char **fbufs; size_t *flens, *fcap0;
#ifdef QF
    char *arena; size_t szA, szF, lb;
    uint64_t L;
#else
    char *arena; size_t szA, szF, lb; double tp;
    uint32_t *I, *itmp; uint64_t L, fail;
#endif
    uint64_t *pool; size_t poolsz; int efree; long flt0, flt1, fltpages;
} FusedCtx;
static struct {
    _Atomic unsigned cnt; char p0[60];
    _Atomic unsigned gen; char p1[60];
    _Atomic int ntf; char p2[60];
    _Atomic int done; char p3[60];
    FusedCtx *ctx;
} g_team;
#ifdef QF
static int g_team_ncpu = -1, *g_team_cpus = NULL;
static int *g_smt_cpus = NULL;
static long minflt_thread(void) {
    long ru[18];
    return sys3(SYS_getrusage, 1, (long)ru, 0) == 0 ? ru[8] : 0;
#else
static int g_bind = 2, g_wexit = 1, g_team_ncpu = -1, *g_team_cpus = NULL, g_smt_k = 1, g_smt_s = 0, g_smt_used = 0;
static int *g_smt_cpus = NULL, *g_team_assigned = NULL;
static _Atomic int g_smt_ready = 0;
static size_t g_team_stack = TEAM_STACK;
static long minflt_thread(void) {
#ifdef RUSAGE_THREAD
    struct rusage ru; return getrusage(RUSAGE_THREAD, &ru) == 0 ? ru.ru_minflt : 0;
#else
    return 0;
#endif
#endif
}
static void team_barrier(int nt) {
    unsigned g = atomic_load_explicit(&g_team.gen, memory_order_acquire);
    if (atomic_fetch_add_explicit(&g_team.cnt, 1, memory_order_acq_rel) == (unsigned)nt - 1) {
        atomic_store_explicit(&g_team.cnt, 0, memory_order_relaxed);
        atomic_store_explicit(&g_team.gen, g + 1, memory_order_release);
    } else while (atomic_load_explicit(&g_team.gen, memory_order_acquire) == g) CPU_RELAX();
}
static int team_size(void) {
    int v; while ((v = atomic_load_explicit(&g_team.ntf, memory_order_acquire)) == 0) CPU_RELAX();
    return v;
}
static void fused_body(int tid, FusedCtx *F) {
    phase1_job(tid, F->T1, F->P);
    const int nt = team_size();
    team_barrier(nt);
    if (tid == 0) {
#ifndef QF
        PROF("phase1 gen+filter", F->tp);
        g_T1 = nt;
#endif
        size_t tot = 0, nseg = F->nseg, *soff = F->soff, RD = F->RD;
        for (size_t s = 0; s < nseg; s++) { soff[s] = tot; tot += F->kv[s].n; }
        soff[nseg] = tot; F->tot = tot;
#ifdef QF
        int TS = (int)(tot / TS_MIN_ITEMS);
#else
        int TS = (int)(tot / (g_tsmin > 0 ? g_tsmin : TS_MIN_ITEMS));
#endif
        if (TS > nt) TS = nt;
        if ((size_t)TS * RD > ((size_t)8 << 20)) TS = (int)(((size_t)8 << 20) / RD);
        if (TS < 1) TS = 1;
        F->TS = TS;
        size_t *sfirst = malloc(sizeof(size_t) * (TS + 1));
        { size_t s = 0; for (int t = 0; t <= TS; t++) { size_t target = (size_t)((double)tot * t / TS); while (s < nseg && soff[s + 1] <= target && t < TS) s++; sfirst[t] = (t == TS) ? nseg : s; } sfirst[0] = 0; }
        for (int t = 1; t <= TS; t++) if (sfirst[t] < sfirst[t - 1]) sfirst[t] = sfirst[t - 1];
        F->sfirst = sfirst;
        F->hist = malloc(sizeof(uint32_t) * (size_t)TS * RD);
        F->cellcnt = malloc(sizeof(uint64_t) * RD);
        F->base = malloc(sizeof(size_t) * (RD + 1));
        F->S = (SortCtx){ F->kv, sfirst, F->hist, F->cellcnt, F->base, RD, TS, NULL, NULL, F->L };
        F->lb = line_bound();
        F->szA = (sizeof(tpPoint) * (tot + 1) + 4095) & ~(size_t)4095;
#ifdef QF
        F->szF = tot * F->lb + (size_t)nt * (F->lb + 1024) + 4096;
#else
        if (g_full) {
            if (tot != F->n) F->ok = 0;
            F->szF = (sizeof(uint32_t) * (tot + 1) + 4095) & ~(size_t)4095;
        } else F->szF = tot * F->lb + (size_t)nt * (F->lb + 1024) + 4096;
#endif
        F->arena = big_alloc(F->szA + F->szF);
        F->A = (tpPoint *)F->arena; F->fbig = F->arena + F->szA;
#ifndef QF
        if (g_full) { F->I = (uint32_t *)(F->arena + F->szA); F->S.I = F->I; F->fbig = NULL; }
#endif
    }
    team_barrier(nt);
#ifdef QF
    {
#else
    if (g_pretouch) {
#endif
        size_t tot_sz = F->szA + F->szF, per = ((tot_sz / (size_t)nt) + 4095) & ~(size_t)4095;
        size_t a0 = per * (size_t)tid, a1 = a0 + per < tot_sz ? a0 + per : tot_sz;
#ifdef QF
        long f0 = tid == 0 ? minflt_thread() : 0;
        for (size_t o = a0; o < a1; o += 4096) F->arena[o] = 0;
        if (tid == 0) { F->flt0 = f0; F->flt1 = minflt_thread(); F->fltpages = (long)((a1 > a0 ? a1 - a0 : 0) >> 12); }
#else
        long f0 = (tid == 0 && g_earlyfree < 0) ? minflt_thread() : 0;
        for (size_t o = a0; o < a1; o += 4096) F->arena[o] = 0;
        if (tid == 0 && g_earlyfree < 0) { F->flt0 = f0; F->flt1 = minflt_thread(); F->fltpages = (long)((a1 > a0 ? a1 - a0 : 0) >> 12); }
#endif
    }
    hist_job(tid, nt, &F->S);
    team_barrier(nt);
    cellcnt_job(tid, nt, &F->S);
    team_barrier(nt);
    if (tid == 0) {
#ifndef QF
        PROF("  sort: segments+hist+cellcnt", F->tp);
#endif
        const int D = F->D; size_t RD = F->RD; uint64_t *cellcnt = F->cellcnt;
        uint64_t *rcnt = calloc(nrn, sizeof(uint64_t));
        for (size_t i = 0; i < nrn; i++) if (rn[i].l == D) rcnt[i] = cellcnt[REL_GET(((size_t)rn[i].yj << D) | (size_t)rn[i].xj)];
        for (size_t i = nrn; i-- > 1;) rcnt[rn[i].parent] += rcnt[i];
        for (size_t i = 0; i < nrn; i++) if (rn[i].l < D && rcnt[i] < 5) F->ok = 0;
        if (F->ok) {
            size_t acc = 0, maxcell = 0, *base = F->base;
            for (size_t c = 0; c < RD; c++) { base[c] = acc; acc += cellcnt[c]; if (cellcnt[c] > maxcell) maxcell = cellcnt[c]; } base[RD] = acc;
            F->maxcell = maxcell;
            F->S.A = F->A;
            F->scratch = big_alloc(sizeof(tpPoint) * (maxcell + 1) * (size_t)nt);
            F->rc = calloc(RD, sizeof(size_t));
            F->Q2 = (P2Ctx){ .A = F->A, .tmp = F->scratch, .tmpstride = maxcell + 1, .res = F->A, .base = base, .rc = F->rc, .RD = RD, .D = D };
#ifndef QF
            if (g_full) { F->itmp = big_alloc(sizeof(uint32_t) * (maxcell + 1) * (size_t)nt); F->Q2.I = F->I; F->Q2.itmp = F->itmp; }
#endif
        }
    }
    team_barrier(nt);
    if (!F->ok) return;
#ifndef QF
    if (g_prof) { if (tid == 0) PROF("  sort: validate+alloc", F->tp); team_barrier(nt); }
#endif
    offsets_job(tid, nt, &F->S);
    team_barrier(nt);
#ifndef QF
    if (g_prof) { if (tid == 0) PROF("  sort: offsets", F->tp); team_barrier(nt); }
#endif
    scatter_job(tid, nt, &F->S);
    team_barrier(nt);
#ifndef QF
    if (g_prof) {
        if (tid == 0) {
            PROF("sort kept by cell", F->tp);
            fprintf(stderr, "  kept points: %zu (%.3f%% of n), sort threads=%d of %d\n", F->tot, 100.0 * F->tot / (F->n ? F->n : 1), F->TS, nt);
        }
        team_barrier(nt);
    }
#endif
    phase2_job(tid, nt, &F->Q2);
    team_barrier(nt);
#ifndef QF
    if (g_full) {
        if (tid == 0) F->fail = atomic_load(&g_fail);
        team_barrier(nt);
        if (F->fail != UINT64_MAX) return;
    }
#endif
    if (tid == 0) {
        size_t RD = F->RD, m = 0, *rc = F->rc;
        for (size_t c = 0; c < RD; c++) m += rc[c];
        F->m = m;
#ifdef QF
        int TF = (int)(m / TF_MIN_LINES); if (TF > nt) TF = nt;
#else
        PROF("phase2 build+query", F->tp);
        int TF = (int)(m / (g_tfmin > 0 ? g_tfmin : TF_MIN_LINES)); if (TF > nt) TF = nt;
#endif
        if (TF < 1) TF = 1;
        F->TF = TF;
        if (m) {
            size_t *fpre = malloc(sizeof(size_t) * (RD + 1));
            fpre[0] = 0; for (size_t i = 0; i < RD; i++) fpre[i + 1] = fpre[i] + rc[i];
            size_t per = (m + TF - 1) / TF, one = per * F->lb + 1024;
#ifndef QF
            if (g_full) F->fbig = big_alloc(one * (size_t)TF);
#endif
            char **fbufs = malloc(sizeof(char *) * (size_t)TF);
            size_t *flens = calloc((size_t)TF, sizeof(size_t)), *fcap0 = malloc(sizeof(size_t) * ((size_t)TF + 1));
            for (int t = 0; t < TF; t++) { fbufs[t] = F->fbig + one * (size_t)t; fcap0[t] = one; }
            fcap0[TF] = 0;
            F->fpre = fpre; F->fbufs = fbufs; F->flens = flens; F->fcap0 = fcap0;
            F->Fm = (FmtCtx){ F->A, F->base, rc, fpre, RD, m, per, TF, F->fbig, fcap0, fbufs, flens };
        }
#ifdef QF
        F->efree = nt > 1 && F->fltpages > 0 && F->flt1 - F->flt0 > F->fltpages / 4;
#else
        F->efree = g_earlyfree >= 0 ? g_earlyfree : (nt > 1 && F->fltpages > 0 && F->flt1 - F->flt0 > F->fltpages / 4);
        if (g_prof) fprintf(stderr, "  early release: %d (master first-touch faults %ld for %ld pages)\n", F->efree, F->flt1 - F->flt0, F->fltpages);
#endif
    }
    team_barrier(nt);
    if (F->m) fmt_job(tid, nt, &F->Fm);
#ifdef QF
    if (F->efree) {
        team_barrier(nt);
        char *rg[2] = { F->arena, (char *)F->pool }; size_t ln[2] = { F->szA, F->poolsz };
        for (int k = 0; k < 2; k++) {
            uintptr_t lo = ((uintptr_t)rg[k] + 4095) & ~(uintptr_t)4095, hi = ((uintptr_t)rg[k] + ln[k]) & ~(uintptr_t)4095;
            if (!rg[k] || hi <= lo) continue;
            size_t pg = (hi - lo) >> 12, p0 = pg * (size_t)tid / (size_t)nt, p1 = pg * (size_t)(tid + 1) / (size_t)nt;
            if (p1 > p0) sys3(SYS_madvise, (long)(lo + (p0 << 12)), (long)((p1 - p0) << 12), 4);
        }
    }
}
static void spawn_children(int id);
static void team_worker(void *a) {
    spawn_children((int)(intptr_t)a);
#else
#ifdef __linux__
    if (F->efree) {
        team_barrier(nt);
        char *rg[2] = { F->arena, (char *)F->pool }; size_t ln[2] = { F->szA, F->poolsz };
        for (int k = 0; k < 2; k++) {
            uintptr_t lo = ((uintptr_t)rg[k] + 4095) & ~(uintptr_t)4095, hi = ((uintptr_t)rg[k] + ln[k]) & ~(uintptr_t)4095;
            if (!rg[k] || hi <= lo) continue;
            size_t pg = (hi - lo) >> 12, p0 = pg * (size_t)tid / (size_t)nt, p1 = pg * (size_t)(tid + 1) / (size_t)nt;
            if (p1 > p0) madvise((void *)(lo + (p0 << 12)), (p1 - p0) << 12, MADV_DONTNEED);
        }
    }
#endif
}
static void smt_worker_step(void);
static void *team_worker(void *a) {
    if ((int)(intptr_t)a == 1) smt_worker_step();
#endif
    fused_body((int)(intptr_t)a, g_team.ctx);
    atomic_fetch_add_explicit(&g_team.done, 1, memory_order_release);
#ifdef QF
}
static long my_strtol(const char *s, char **end) {
    const char *p = s; int neg = 0; long v = 0;
    while (*p == ' ' || (*p >= '\t' && *p <= '\r')) p++;
    if (*p == '+' || *p == '-') { neg = *p == '-'; p++; }
    if (!(*p >= '0' && *p <= '9')) { *end = (char *)s; return 0; }
    while (*p >= '0' && *p <= '9') { if (v < 100000000) v = v * 10 + (*p - '0'); p++; }
    *end = (char *)p;
    return neg ? -v : v;
}
static int read_siblings(int cpu, int *out, int maxn) {
    static const char pre[] = "/sys/devices/system/cpu/cpu", suf[] = "/topology/thread_siblings_list";
    char path[128], dg[16]; size_t k = 0; int nd = 0; unsigned u = (unsigned)cpu;
    memcpy(path, pre, sizeof pre - 1); k = sizeof pre - 1;
    do { dg[nd++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (nd) path[k++] = dg[--nd];
    memcpy(path + k, suf, sizeof suf);
    long fd = sys3(SYS_open, (long)path, 02000000, 0); if (fd < 0) return 0;
    char buf[1024]; long r = sys3(SYS_read, fd, (long)buf, sizeof buf - 1); sys3(SYS_close, fd, 0, 0);
    if (r <= 0) return 0;
    buf[r] = 0;
    int kk = 0; char *q = buf;
    while (*q && *q != '\n' && kk < maxn) {
        char *e; long a = my_strtol(q, &e); if (e == q || a < 0) return 0;
        long b = a; if (*e == '-') { q = e + 1; b = my_strtol(q, &e); if (e == q || b < a) return 0; }
        for (long v = a; v <= b && kk < maxn; v++) out[kk++] = (int)v;
        q = e; if (*q == ',') q++;
        else if (*q && *q != '\n') return 0;
    }
    return kk;
}
static int smt_pattern_ok(const int *sb, int k, int s, int cpu) {
    int found = 0;
    for (int i = 0; i < k; i++) { if (sb[i] != sb[0] + i * s || (sb[i] / s) % k != i) return 0; if (sb[i] == cpu) found = 1; }
    return found;
}
static void smt_reorder(void) {
    if (g_team_ncpu < 2) return;
    int sb[64], sb2[64];
    int k = read_siblings(g_team_cpus[0], sb, 64);
    if (k < 2) return;
    int s = sb[1] - sb[0];
    if (s <= 0 || !smt_pattern_ok(sb, k, s, g_team_cpus[0])) return;
    int last = g_team_cpus[g_team_ncpu - 1];
    if (read_siblings(last, sb2, 64) != k || !smt_pattern_ok(sb2, k, s, last)) return;
    int *o = malloc(sizeof(int) * (size_t)g_team_ncpu), n = 0;
    for (int rk = 0; rk < k; rk++) for (int i = 0; i < g_team_ncpu; i++) if ((g_team_cpus[i] / s) % k == rk) o[n++] = g_team_cpus[i];
    if (n == g_team_ncpu) g_smt_cpus = o;
}
#else
#if defined(__linux__) && defined(SYS_exit)
    if (g_wexit) syscall(SYS_exit, 0);
#endif
    return NULL;
}
#ifdef __linux__
#include <fcntl.h>
static int read_siblings(const char *dir, int cpu, int *out, int maxn) {
    char path[512]; snprintf(path, sizeof path, "%s/cpu%d/topology/thread_siblings_list", dir, cpu);
    int fd = open(path, O_RDONLY | O_CLOEXEC); if (fd < 0) return 0;
    char buf[1024]; ssize_t r = read(fd, buf, sizeof buf - 1); close(fd);
    if (r <= 0) return 0;
    buf[r] = 0;
    int k = 0; char *q = buf;
    while (*q && *q != '\n' && k < maxn) {
        char *e; long a = strtol(q, &e, 10); if (e == q || a < 0) return 0;
        long b = a; if (*e == '-') { q = e + 1; b = strtol(q, &e, 10); if (e == q || b < a) return 0; }
        for (long v = a; v <= b && k < maxn; v++) out[k++] = (int)v;
        q = e; if (*q == ',') q++;
        else if (*q && *q != '\n') return 0;
    }
    return k;
}
static int smt_pattern_ok(const int *sb, int k, int s, int cpu) {
    int found = 0;
    for (int i = 0; i < k; i++) { if (sb[i] != sb[0] + i * s || (sb[i] / s) % k != i) return 0; if (sb[i] == cpu) found = 1; }
    return found;
}
static void smt_reorder(void) {
    if (g_team_ncpu < 2) return;
    const char *dir = getenv("QT_SYSFS_CPU"); if (!dir) dir = "/sys/devices/system/cpu";
    int sb[64], sb2[64];
    int k = read_siblings(dir, g_team_cpus[0], sb, 64);
    if (k < 2) return;
    int s = sb[1] - sb[0];
    if (s <= 0 || !smt_pattern_ok(sb, k, s, g_team_cpus[0])) return;
    int last = g_team_cpus[g_team_ncpu - 1];
    if (read_siblings(dir, last, sb2, 64) != k || !smt_pattern_ok(sb2, k, s, last)) return;
    int *o = malloc(sizeof(int) * (size_t)g_team_ncpu), n = 0;
    for (int rk = 0; rk < k; rk++) for (int i = 0; i < g_team_ncpu; i++) if ((g_team_cpus[i] / s) % k == rk) o[n++] = g_team_cpus[i];
    if (n == g_team_ncpu) { g_smt_cpus = o; g_smt_k = k; g_smt_s = s; g_smt_used = 1; }
    else free(o);
}
#endif
#endif
static void team_cpus_init(void) {
    if (g_team_ncpu >= 0) return;
    g_team_ncpu = 0;
#ifdef QF
    cpuset_t cs;
    if (get_affinity(&cs) != 0) return;
    g_team_cpus = malloc(sizeof(int) * CPUSET_BYTES * 8);
    for (int c = 0; c < CPUSET_BYTES * 8; c++) if (cs_isset(c, &cs)) g_team_cpus[g_team_ncpu++] = c;
}
static struct { char *stk; int T; int *cpu; cpuset_t all; } g_sp;
static void spawn_one(int c) {
    long tid = qf_clone(CLONE_THR, g_sp.stk + (size_t)c * TEAM_STACK, team_worker, (void *)(intptr_t)c);
    if (tid <= 0) handoff();
    if (g_sp.cpu && g_sp.cpu[c] >= 0) {
        cpuset_t cs; cs_zero(&cs); cs_set(g_sp.cpu[c], &cs);
        if (set_affinity(tid, &cs) != 0) set_affinity(tid, &g_sp.all);
    }
}
static void spawn_children(int id) {
    int r = id == 0 ? 0 : 32 - __builtin_clz((unsigned)id);
    for (long c = id + (1L << r); c < g_sp.T; r++, c = id + (1L << r)) spawn_one((int)c);
#else
#ifdef __linux__
    cpu_set_t cs; CPU_ZERO(&cs);
    if (sched_getaffinity(0, sizeof cs, &cs) != 0) return;
    g_team_cpus = malloc(sizeof(int) * CPU_SETSIZE);
    for (int c = 0; c < CPU_SETSIZE; c++) if (CPU_ISSET(c, &cs)) g_team_cpus[g_team_ncpu++] = c;
    if (getenv("QT_NOSMT")) atomic_store(&g_smt_ready, 2);
#endif
}
static void smt_worker_step(void) {
#ifdef __linux__
    if (atomic_load_explicit(&g_smt_ready, memory_order_acquire) != 0) return;
    smt_reorder();
    atomic_store_explicit(&g_smt_ready, g_smt_cpus ? 1 : 2, memory_order_release);
#endif
#endif
}
static void team_run(int T, FusedCtx *F) {
    g_team.ctx = F;
    atomic_store(&g_team.cnt, 0); atomic_store(&g_team.done, 0); atomic_store(&g_team.ntf, 0);
    int created = 0;
    if (T > 1) {
        team_cpus_init();
#ifdef QF
        int bind = g_team_ncpu >= T;
        if (bind) {
            cpuset_t cs; cs_zero(&cs); cs_set(g_team_cpus[0], &cs);
            if (get_affinity(&g_sp.all) != 0) bind = 0;
            if (bind && set_affinity(0, &cs) != 0) bind = 0;
        }
        char *stk = map_anon((size_t)T * TEAM_STACK);
        if (!stk) handoff();
        sys3(SYS_madvise, (long)stk, (long)((size_t)T * TEAM_STACK), 15);
        g_sp.stk = stk; g_sp.T = T; g_sp.cpu = NULL;
        if (bind) {
            smt_reorder();
            const int *order = g_smt_cpus ? g_smt_cpus : g_team_cpus;
            int *cp = malloc(sizeof(int) * (size_t)T), k = 0;
            cp[0] = g_team_cpus[0];
            for (int i = 0; i < g_team_ncpu && k + 1 < T; i++) if (order[i] != g_team_cpus[0]) cp[++k] = order[i];
            for (k++; k < T; k++) cp[k] = -1;
            g_sp.cpu = cp;
        }
        created = T - 1;
        atomic_store_explicit(&g_team.ntf, T, memory_order_release);
        spawn_children(0);
#else
        int bind = g_team_ncpu >= T ? g_bind : 0, skip = -1;
#ifndef __linux__
        bind = 0;
#else
        if (bind == 2) {
            cpu_set_t cs; CPU_ZERO(&cs); CPU_SET(g_team_cpus[0], &cs);
            if (sched_setaffinity(0, sizeof cs, &cs) != 0) bind = 0;
        } else if (bind == 1) {
            int c = sched_getcpu(); skip = c;
        }
#endif
        pthread_attr_t at; int attr_ok = pthread_attr_init(&at) == 0;
        if (attr_ok) { pthread_attr_setstacksize(&at, g_team_stack); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED); }
        int next_cpu = 0, *order = g_team_cpus, switched = 0;
        static unsigned char used[4096];
        memset(used, 0, sizeof used);
        if (!bind) atomic_store(&g_smt_ready, 2);
        g_team_assigned = realloc(g_team_assigned, sizeof(int) * ((size_t)T + 1));
        if (bind == 2) used[g_team_cpus[0]] = 1;
        for (int i = 1; i < T; i++) {
            int bound = 0;
#ifdef __linux__
            if (bind && attr_ok) {
                if (!switched && atomic_load_explicit(&g_smt_ready, memory_order_acquire) == 1) { order = g_smt_cpus; next_cpu = 0; switched = 1; }
                while (next_cpu < g_team_ncpu && (used[order[next_cpu]] || order[next_cpu] == skip)) next_cpu++;
                if (next_cpu < g_team_ncpu) {
                    cpu_set_t cs; CPU_ZERO(&cs); CPU_SET(order[next_cpu], &cs);
                    bound = pthread_attr_setaffinity_np(&at, sizeof cs, &cs) == 0;
                }
            }
#endif
            pthread_t th;
            int rc = pthread_create(&th, attr_ok ? &at : NULL, team_worker, (void *)(intptr_t)(created + 1));
            if (rc != 0 && bound) {
#ifdef __linux__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnonnull"
                pthread_attr_setaffinity_np(&at, 0, NULL);
#pragma GCC diagnostic pop
#endif
                bind = 0; bound = 0;
                rc = pthread_create(&th, attr_ok ? &at : NULL, team_worker, (void *)(intptr_t)(created + 1));
            }
            if (rc != 0) break;
            created++;
            if (bound) { used[order[next_cpu]] = 1; if (created <= T) g_team_assigned[created] = order[next_cpu]; next_cpu++; }
            else if (created <= T) g_team_assigned[created] = -1;
        }
        if (attr_ok) pthread_attr_destroy(&at);
        if (g_prof) {
            int ready = atomic_load(&g_smt_ready);
            fprintf(stderr, "  cpu order (%d allowed, SMT %s k=%d s=%d): master %d, ranks 1..:", g_team_ncpu,
                    ready == 1 ? "reordered" : ready == 2 ? "not used" : "not read yet", g_smt_k, g_smt_s, bind == 2 ? g_team_cpus[0] : -1);
            for (int r = 1; r <= created && r <= 8; r++) fprintf(stderr, " %d", g_team_assigned[r]);
            if (created > 8) fprintf(stderr, " ... %d", g_team_assigned[created]);
            fprintf(stderr, "\n");
        }
#endif
    }
    atomic_store_explicit(&g_team.ntf, created + 1, memory_order_release);
    fused_body(0, F);
    while (atomic_load_explicit(&g_team.done, memory_order_acquire) < created) CPU_RELAX();
}
#ifdef QF
static void write_bufs(char **b, const size_t *l, int n) {
    struct { const char *p; size_t n; } iov[64];
    int i = 0;
    while (i < n) {
        int k = 0, u = 0;
        while (i + u < n && k < 64) { if (l[i + u]) { iov[k].p = b[i + u]; iov[k].n = l[i + u]; k++; } u++; }
        int j = 0;
        while (j < k) {
            long w = sys3(20, 1, (long)(iov + j), k - j);
            if (w == -E_INTR) continue;
            if (w <= 0) { static const char m[] = "write: Input/output error\n"; write_fd(2, m, sizeof m - 1); qexit(1); }
            size_t ww = (size_t)w;
            while (j < k && ww >= iov[j].n) { ww -= iov[j].n; j++; }
            if (j < k && ww) { iov[j].p += ww; iov[j].n -= ww; }
        }
        i += u;
    }
}
#endif

static int fast_path(int D) {
#ifndef QF
    double t_fp0 = tnow();
#endif
    uint64_t n = C.n_points;
    D_ = D;
    size_t tot_nodes = ((size_t)1 << (D + 1)) - 1;
    TXv = malloc(sizeof(uint32_t) * tot_nodes); TYv = malloc(sizeof(uint32_t) * tot_nodes);
    XLO = malloc(sizeof(double) * tot_nodes); XHI = malloc(sizeof(double) * tot_nodes);
    YLO = malloc(sizeof(double) * tot_nodes); YHI = malloc(sizeof(double) * tot_nodes);
    uint32_t *KX0 = malloc(sizeof(uint32_t) * tot_nodes), *KX1 = malloc(sizeof(uint32_t) * tot_nodes);
    uint32_t *KY0 = malloc(sizeof(uint32_t) * tot_nodes), *KY1 = malloc(sizeof(uint32_t) * tot_nodes);
    XLO[0] = C.map_boundaries.x_min; XHI[0] = C.map_boundaries.x_max;
    YLO[0] = C.map_boundaries.y_min; YHI[0] = C.map_boundaries.y_max;
    KX0[0] = KY0[0] = 0; KX1[0] = KY1[0] = MAX_NORMA;
    for (int l = 0; l < D; l++)
        for (size_t j = 0; j < ((size_t)1 << l); j++) {
            size_t p = LO(l) + j, c0 = LO(l + 1) + 2 * j, c1 = c0 + 1;
            double xa = (XLO[p] + XHI[p]) / 2.0, ya = (YLO[p] + YHI[p]) / 2.0;
            double d = fabsf(XHI[p] - XLO[p]); if (d < MAX_ERROR) return 0;
            d = fabsf(YHI[p] - YLO[p]); if (d < MAX_ERROR) return 0;
#ifndef QF
            if (g_full && !(xa > XLO[p] && xa < XHI[p] && ya > YLO[p] && ya < YHI[p])) return 0;
#endif
            TXv[p] = kthr(1, xa, 1);
            TYv[p] = kthr(0, ya, 0);
            XLO[c0] = XLO[p]; XHI[c0] = xa; XLO[c1] = xa; XHI[c1] = XHI[p];
            YLO[c0] = YLO[p]; YHI[c0] = ya; YLO[c1] = ya; YHI[c1] = YHI[p];
            if (TXv[p] < KX0[p] || TXv[p] > KX1[p] || TYv[p] < KY0[p] || TYv[p] > KY1[p]) return 0;
            KX0[c0] = KX0[p]; KX1[c0] = TXv[p]; KX0[c1] = TXv[p]; KX1[c1] = KX1[p];
            KY0[c0] = KY0[p]; KY1[c0] = TYv[p]; KY0[c1] = TYv[p]; KY1[c1] = KY1[p];
        }
    size_t ncells_all = (size_t)1 << (2 * D);
    relmap = calloc(ncells_all, sizeof(int32_t));
#ifdef QF
    caprn = (((size_t)1 << (2 * D + 2)) - 1) / 3; nrn = 0; rn = malloc(caprn * sizeof(RNode));
    cell_x = malloc(sizeof(int32_t) * ncells_all); cell_y = malloc(sizeof(int32_t) * ncells_all); ncell = 0;
#else
    caprn = 1024; nrn = 0; rn = malloc(caprn * sizeof(RNode));
    cell_x = malloc(sizeof(int32_t) * ncells_all); cell_y = malloc(sizeof(int32_t) * ncells_all); ncell = 0;
    if (g_full) { cell_emit = malloc(ncells_all); relevant_rec_full(0, 0, 0, -1, 1); }
    else
#endif
    relevant_rec(0, 0, 0, -1);
    size_t RD = ncell;
#ifdef QF
    if (RD == 0) { write_all("Empty\n", 6); return 1; }
#else
    double tp = tnow();
    if (g_prof) fprintf(stderr, "  tables (D=%d)                %.5f s\n", D, tp - t_fp0);
    if (g_prof) fprintf(stderr, "  D=%d relevant nodes=%zu cells=%zu\n", D, nrn, RD);
    if (RD == 0) { ramp_cancel(); print_results(NULL, 0); return 1; }
#endif
    uint32_t bx0 = MAX_NORMA, bx1 = 0, by0 = MAX_NORMA, by1 = 0;
    double karea = 0;
    for (size_t c = 0; c < RD; c++) {
        size_t px = LO(D) + (size_t)cell_x[c], py = LO(D) + (size_t)cell_y[c];
        karea += (double)(KX1[px] - KX0[px]) * (double)(KY1[py] - KY0[py]) * 1e-12;
        if (KX0[px] < bx0) bx0 = KX0[px];
        if (KX1[px] > bx1) bx1 = KX1[px];
        if (KY0[py] < by0) by0 = KY0[py];
        if (KY1[py] > by1) by1 = KY1[py];
    }
    uint32_t bxw = bx1 > bx0 ? bx1 - bx0 : 0, byw = by1 > by0 ? by1 - by0 : 0;

    int T = nthreads();
    int T1 = team_threads(n, (double)n * karea, T);
#ifndef QF
    if (g_ppt) { T1 = (int)(n / g_ppt); if (T1 < 1) T1 = 1; if (T1 > T) T1 = T; }
#endif
    size_t nchunks = n < 200000 ? 1 : (size_t)T1 * 4;
    size_t nseg = nchunks * LANES;
    uint64_t L = (n + nseg - 1) / nseg;
    double fbox = ((double)bxw / MAX_NORMA) * ((double)byw / MAX_NORMA);
    size_t capseg = (size_t)((double)L * fbox * 1.25) + 64;
    if (capseg > L + 64) capseg = L + 64;
    uint64_t *pool = big_alloc(sizeof(uint64_t) * nseg * capseg);
    KVec *kv = calloc(nseg, sizeof(KVec));
    for (size_t s = 0; s < nseg; s++) { kv[s].v = pool + s * capseg; kv[s].cap = capseg; kv[s].n = 0; }
    size_t ringsz = (size_t)(31 + ROWS) * LANES;
    uint32_t *rings = aligned_alloc(64, sizeof(uint32_t) * ringsz * (size_t)T1);
    uint8_t *flagsall = aligned_alloc(64, (size_t)(ROWS / 2) * LANES * (size_t)T1);
    static uint32_t Madv[31][31];
#ifdef QF
    adv_matrix(2 * L, Madv);
    jump_setup(Madv[0], nchunks);
#else
    if (use_emu) adv_matrix(2 * L, Madv);
    if (g_prof) fprintf(stderr, "  phase1 threads=%d chunks=%zu segment=%llu pts cap/seg=%zu\n", T1, nchunks, (unsigned long long)L, capseg);
#endif
    P1Ctx *P = calloc(1, sizeof(P1Ctx));
    *P = (P1Ctx){ .n = n, .L = L, .nchunks = nchunks, .D = D, .bx0 = bx0, .bxw = bxw, .by0 = by0, .byw = byw, .kv = kv, .rings = rings, .ringsz = ringsz, .flagsall = flagsall, .Madv = Madv };
#ifdef QF
    size_t *soff = malloc(sizeof(size_t) * (nseg + 1));
    FusedCtx *F = calloc(1, sizeof *F);
    F->pool = pool; F->poolsz = sizeof(uint64_t) * nseg * capseg;
    F->P = P; F->kv = kv; F->nseg = nseg; F->soff = soff; F->RD = RD; F->ok = 1; F->TF = 1; F->TS = 1;
    F->T1 = T1; F->D = D; F->n = n; F->lb = 48; F->L = L;
    team_run(T1, F);
    if (!F->ok) return fast_path(0);
    if (F->m == 0) write_all("Empty\n", 6);
    else write_bufs(F->fbufs, F->flens, F->TF);
#else
    size_t *soff = malloc(sizeof(size_t) * (nseg + 1)), tot = 0, maxcell = 0, m = 0, *sfirst = NULL, *base = NULL, *rc = NULL, *fpre = NULL;
    uint32_t *hist = NULL; uint64_t *cellcnt = NULL; tpPoint *A = NULL, *scratch = NULL;
    int TS = 1, ok = 1, TF = 1;
    SortCtx S; P2Ctx Q2; FmtCtx Fm; char *fbig = NULL; char **fbufs = NULL; size_t *flens = NULL, *fcap0 = NULL;
    char *arena = NULL; size_t szA = 0, szF = 0, lb = 48;
    memset(&S, 0, sizeof S); memset(&Q2, 0, sizeof Q2); memset(&Fm, 0, sizeof Fm);
    {
        FusedCtx *F = calloc(1, sizeof *F);
        F->pool = pool; F->poolsz = sizeof(uint64_t) * nseg * capseg;
        F->P = P; F->kv = kv; F->nseg = nseg; F->soff = soff; F->RD = RD; F->ok = 1; F->TF = 1; F->TS = 1;
        F->T1 = T1; F->D = D; F->n = n; F->lb = 48; F->tp = tp; F->L = L; F->fail = UINT64_MAX;
        if (g_full) g_team_stack = (size_t)8 << 20;
        team_run(T1, F);
        if (F->fail != UINT64_MAX) reproduce_failure(F->fail);
        tp = F->tp; ok = F->ok; m = F->m; TF = F->TF; fbufs = F->fbufs; flens = F->flens;
        (void)tot; (void)maxcell; (void)sfirst; (void)base; (void)rc; (void)fpre; (void)hist; (void)cellcnt; (void)A; (void)scratch;
        (void)TS; (void)S; (void)Q2; (void)Fm; (void)fbig; (void)fcap0; (void)arena; (void)szA; (void)szF; (void)lb;
        if (g_prof) PROF("format (in region)", tp);
    }
    if (!ok && g_full) return 0;
    if (!ok) return fast_path(0);
    if (g_prof) fprintf(stderr, "  threads: team=%d format buffers=%d (results %zu)\n", g_T1, TF, m);
    fflush(stdout);
    if (m == 0) write_all("Empty\n", 6);
    else for (int t = 0; t < TF; t++) if (flens[t]) write_all(fbufs[t], flens[t]);
    PROF("format+write", tp);
#endif
    return 1;
}

#ifndef QF
static long long g_t_main = 0;
static long long now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (long long)t.tv_sec * 1000000000LL + t.tv_nsec; }
static int finish(void) {
    if (getenv("QT_TS")) {
        struct rusage ru; getrusage(RUSAGE_SELF, &ru);
        fprintf(stderr, "QTTS %lld %lld %ld %ld\n", g_t_main, now_ns(), (long)ru.ru_minflt, (long)ru.ru_majflt);
    }
    return EXIT_SUCCESS;
}

#endif
static int axis_proof(double lo, double hi, double W, int *R, double *wmin_eps, double *T) {
    const double u = 0x1p-53, tiny = 0x1p-1060;
    double M = dmax(fabs(lo), fabs(hi));
    if (!(M < 1e307) || !(W > 0) || !(W < 1e307)) return 0;
    double eps = 1.01 * u * M + tiny;
    double eta = 2.02 * u * W + 1.01 * u * M + tiny;
    double g = W * 1e-6 * (1.0 - 1e-9) - 2.02 * eta;
    double W0hi = W * (1.0 + 4 * u);
    if (!(g > 2.02 * eps)) return 0;
    int d = 0; double w = W0hi;
    while (!(w + 2.02 * eps < g)) { w *= 0.5; if (++d > 4000) return 0; }
    *R = d; *wmin_eps = 2.02 * eps;
    *T = dmax(1.000001e-10, 2.02 * eps);
    return 1;
}
static int axis_ok_at(double W, int d, double weps, double T) {
    double w = W * (1.0 - 4 * 0x1p-53); for (int i = 0; i < d; i++) w *= 0.5;
    return w - weps > T;
}
static int no_failure_proof(void) {
    int Rx, Ry; double ex, ey, Tx, Ty;
    if (!axis_proof(C.map_boundaries.x_min, C.map_boundaries.x_max, Wx, &Rx, &ex, &Tx)) return 0;
    if (!axis_proof(C.map_boundaries.y_min, C.map_boundaries.y_max, Wy, &Ry, &ey, &Ty)) return 0;
    if (C.n_points > 0x7fffffffULL) return 0;
    int R = Rx > Ry ? Rx : Ry;
    if (R == 0) return 1;
    return axis_ok_at(Wx, R - 1, ex, Tx) && axis_ok_at(Wy, R - 1, ey, Ty);
}
#ifdef QF

static void read_input(void) {
    g_in_off0 = sys3(SYS_lseek, 0, 0, 1);
    g_touched = 1;
    for (;;) {
        if (g_inlen == g_incap) {
            size_t nc = g_incap * 4; char *nb = map_anon(nc);
            if (!nb) handoff();
            memcpy(nb, g_in, g_inlen); g_in = nb; g_incap = nc;
        }
        long r = sys3(SYS_read, 0, (long)(g_in + g_inlen), (long)(g_incap - g_inlen));
        if (r == -E_INTR) continue;
        if (r <= 0) { if (r < 0) handoff(); break; }
        g_inlen += (size_t)r;
    }
}
typedef struct { uint32_t w[48]; int n; } Big;
static void big_set(Big *b, uint64_t v) { b->n = 0; while (v) { b->w[b->n++] = (uint32_t)v; v >>= 32; } }
static int big_mul(Big *b, uint32_t m) {
    uint64_t c = 0;
    for (int i = 0; i < b->n; i++) { c += (uint64_t)b->w[i] * m; b->w[i] = (uint32_t)c; c >>= 32; }
    if (c) { if (b->n == 48) return 0; b->w[b->n++] = (uint32_t)c; }
    return 1;
}
static int big_pow10(Big *b, int e) {
    static const uint32_t p9[9] = { 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000 };
    for (; e >= 9; e -= 9) if (!big_mul(b, 1000000000u)) return 0;
    return big_mul(b, p9[e]);
}
static int big_shl(Big *b, int sh) {
    if (!b->n) return 1;
    int q = sh >> 5, r = sh & 31;
    if (b->n + q + 1 > 48) return 0;
    uint32_t t[48]; int n = b->n + q + 1;
    for (int i = 0; i < n; i++) t[i] = 0;
    for (int i = 0; i < b->n; i++) {
        uint64_t v = (uint64_t)b->w[i] << r;
        t[i + q] |= (uint32_t)v; t[i + q + 1] |= (uint32_t)(v >> 32);
    }
    while (n > 0 && !t[n - 1]) n--;
    for (int i = 0; i < n; i++) b->w[i] = t[i];
    b->n = n;
    return 1;
}
static int big_cmp(const Big *a, const Big *b) {
    if (a->n != b->n) return a->n < b->n ? -1 : 1;
    for (int i = a->n - 1; i >= 0; i--) if (a->w[i] != b->w[i]) return a->w[i] < b->w[i] ? -1 : 1;
    return 0;
}
static int cmp_dec_bin(uint64_t M, int e, uint64_t K, int f, int *res) {
    Big A, B; big_set(&A, M); big_set(&B, K);
    if (e >= 0) { if (!big_pow10(&A, e)) return 0; } else if (!big_pow10(&B, -e)) return 0;
    if (f >= 0) { if (!big_shl(&B, f)) return 0; } else if (!big_shl(&A, -f)) return 0;
    *res = big_cmp(&A, &B);
    return 1;
}
static const double P10[23] = { 1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22 };
static int dec_to_double(uint64_t M, int e, double *out) {
    while (e > 22 && M <= ((uint64_t)1 << 53) / 10) { M *= 10; e--; }
    if (M <= ((uint64_t)1 << 53) && e >= -22 && e <= 22) { double v = (double)M; *out = e < 0 ? v / P10[-e] : v * P10[e]; return 1; }
    if (e < -330 || e > 310) return 0;
    long double p = 1.0L, t = 10.0L, x = (long double)M;
    for (int ae = e < 0 ? -e : e; ae; ae >>= 1) { if (ae & 1) p *= t; t *= t; }
    x = e < 0 ? x / p : x * p;
    double c = (double)x;
    if (!(c >= 0x1p-1021 && c <= 0x1.fffffffffffffp1023)) return 0;
    for (int it = 0; it < 64; it++) {
        uint64_t bits; memcpy(&bits, &c, 8);
        uint64_t mc = (bits & ((1ULL << 52) - 1)) | (1ULL << 52); int ec = (int)(bits >> 52) - 1075, r;
        if (!cmp_dec_bin(M, e, 2 * mc + 1, ec - 1, &r)) return 0;
        if (r > 0 || (r == 0 && (mc & 1))) { bits++; memcpy(&c, &bits, 8); if (!(c <= 0x1.fffffffffffffp1023)) return 0; continue; }
        uint64_t lb = bits - 1; double lo; memcpy(&lo, &lb, 8);
        if (!(lo >= 0x1p-1022)) return 0;
        uint64_t lm = (lb & ((1ULL << 52) - 1)) | (1ULL << 52); int le = (int)(lb >> 52) - 1075;
        if (!cmp_dec_bin(M, e, 2 * lm + 1, le - 1, &r)) return 0;
        if (r < 0 || (r == 0 && (mc & 1))) { c = lo; continue; }
        *out = c;
        return 1;
    }
    return 0;
}
static inline int is_ws(char c) { return c == '\t' || c == '\r' || c == '\v' || c == '\f' || c == ' ' || c == '\n'; }
static inline int is_dg(char c) { return c >= '0' && c <= '9'; }
static int exact_atof(const char *s, int len, double *out) {
    int i = 0, neg = 0, any = 0, nd = 0, pz = 0, e10 = 0;
    uint64_t M = 0;
    while (i < len && is_ws(s[i])) i++;
    if (i < len && (s[i] == '+' || s[i] == '-')) { neg = s[i] == '-'; i++; }
    if (i < len && ((s[i] | 0x20) == 'i' || (s[i] | 0x20) == 'n')) return 0;
    if (i + 1 < len && s[i] == '0' && (s[i + 1] | 0x20) == 'x') return 0;
    for (int frac = 0; ; frac = 1) {
        while (i < len && is_dg(s[i])) {
            int d = s[i] - '0';
            any = 1;
            if (frac) e10--;
            if (!d) { if (M) pz++; }
            else {
                if (nd + pz + 1 > 19) return 0;
                for (int z = 0; z < pz; z++) M *= 10;
                M = M * 10 + (uint64_t)d; nd += pz + 1; pz = 0;
            }
            i++;
        }
        if (frac || i >= len || s[i] != '.' || !(any || (i + 1 < len && is_dg(s[i + 1])))) break;
        i++;
    }
    if (!any) return 0;
    if (i < len && (s[i] == 'e' || s[i] == 'E')) {
        int j = i + 1, en = 0, x = 0;
        if (j < len && (s[j] == '+' || s[j] == '-')) { en = s[j] == '-'; j++; }
        if (j < len && is_dg(s[j])) {
            while (j < len && is_dg(s[j])) { if (x < 100000) x = x * 10 + (s[j] - '0'); j++; }
            e10 += en ? -x : x;
        }
    }
    e10 += pz;
    double v = 0.0;
    if (M && !dec_to_double(M, e10, &v)) return 0;
    *out = neg ? -v : v;
    return 1;
}
static int exact_atoi(const char *s, int len, uint64_t *out) {
    int i = 0, neg = 0, nd = 0, ovf = 0; uint64_t v = 0;
    while (i < len && is_ws(s[i])) i++;
    if (i < len && (s[i] == '+' || s[i] == '-')) { neg = s[i] == '-'; i++; }
    while (i < len && is_dg(s[i])) { nd++; if (v > 1000000000000000000ULL) ovf = 1; else v = v * 10 + (uint64_t)(s[i] - '0'); i++; }
    int64_t lv = 0;
    if (nd) {
        if (!neg) lv = (ovf || v > (uint64_t)INT64_MAX) ? INT64_MAX : (int64_t)v;
        else lv = (ovf || v > (uint64_t)INT64_MAX + 1) ? INT64_MIN : (int64_t)(0 - v);
    }
    *out = (uint64_t)(int64_t)(int32_t)(uint32_t)(uint64_t)lv;
    return 1;
}
static const char *const KEYS[9] = { "x_min", "x_max", "y_min", "y_max", "sub_x_min", "sub_x_max", "sub_y_min", "sub_y_max", "num_points" };
static int parse_input(void) {
    double v[8] = {0}; uint64_t np = 0; unsigned seen = 0;
    const char *in = g_in; size_t i = 0, N = g_inlen;
    for (size_t k = 0; k < N; k++) if (!in[k]) return 0;
    while (i < N) {
        size_t s = i;
        while (i < N && in[i] != '\n') i++;
        size_t e = i < N ? i + 1 : i;
        if (e - s > 65535) return 0;
        i = e;
        size_t p = s;
        while (p < e && in[p] == '=') p++;
        if (p == e) return 0;
        size_t t1 = p;
        while (p < e && in[p] != '=') p++;
        size_t t1e = p, save = p < e ? p + 1 : e;
        char param[16]; int pl = 0;
        for (size_t k = t1; k < t1e; k++) if (in[k] != ' ' && in[k] != '\n') { if (pl < 15) param[pl] = in[k]; pl++; }
        if (pl == 0 || param[0] == '#') continue;
        p = save;
        while (p < e && in[p] == '=') p++;
        if (p == e) return 0;
        size_t t2 = p;
        while (p < e && in[p] != '=') p++;
        int key = -1;
        if (pl <= 15)
            for (int q = 0; q < 9; q++) {
                const char *K = KEYS[q]; int kl = 0; while (K[kl]) kl++;
                if (kl == pl && !memcmp(K, param, (size_t)pl)) { key = q; break; }
            }
        if (key < 0) continue;
        char val[64]; int vl = 0;
        for (size_t k = t2; k < p; k++) if (in[k] != ' ' && in[k] != '\n') { if (vl >= 63) return 0; val[vl++] = in[k]; }
        if (key < 8) { if (!exact_atof(val, vl, &v[key])) return 0; }
        else exact_atoi(val, vl, &np);
        seen |= 1u << key;
    }
    if (seen != 0x1ff) return 0;
    C.map_boundaries = (tpRectangle){ v[0], v[1], v[2], v[3] };
    C.sub_boundaries = (tpRectangle){ v[4], v[5], v[6], v[7] };
    C.n_points = np;
    return 1;
}
static int cpu_avx2(void) {
    unsigned a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0), "c"(0));
    if (a < 7) return 0;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    if (!((c >> 27) & 1) || !((c >> 28) & 1)) return 0;
    unsigned xlo, xhi;
    __asm__ volatile("xgetbv" : "=a"(xlo), "=d"(xhi) : "c"(0));
    if ((xlo & 6) != 6) return 0;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(7), "c"(0));
    return (int)((b >> 5) & 1);
}

__attribute__((noreturn, used)) void qf_start(long *sp) {
    int argc = (int)sp[0];
    g_argv = (char **)(sp + 1); g_envp = g_argv + argc + 1;
    for (char **e = g_envp; *e; e++) if ((*e)[0] == 'Q' && (*e)[1] == 'T' && (*e)[2] == '_') handoff();
    read_input();
    if (!parse_input()) handoff();
    g_avx2 = cpu_avx2();
#else
int main(int ac, char **av) {
#ifdef __linux__
    if (getenv("QT_NOTHP")) prctl(PR_SET_THP_DISABLE, 1, 0, 0, 0);
#endif
    (void)ac; (void)av;
    g_t_main = now_ns();
    g_prof = getenv("QT_PROF") != NULL;
    if (getenv("QT_PPT")) { long long v = atoll(getenv("QT_PPT")); if (v > 0) g_ppt = (uint64_t)v; }
    if (getenv("QT_P1A")) g_p1a = atof(getenv("QT_P1A"));
    if (getenv("QT_P1C")) g_p1c = atof(getenv("QT_P1C"));
    if (getenv("QT_TSMIN")) g_tsmin = atof(getenv("QT_TSMIN"));
    if (getenv("QT_NOPRETOUCH")) g_pretouch = 0;
    if (getenv("QT_FASTSETUP")) g_fastsetup = g_fastsetup_early = atoi(getenv("QT_FASTSETUP"));
    if (getenv("QT_NOMADV")) g_madv = 0;
    if (getenv("QT_EARLYFREE")) g_earlyfree = atoi(getenv("QT_EARLYFREE"));
    if (getenv("QT_P1K")) g_p1k = atoi(getenv("QT_P1K"));
    if (getenv("QT_TFMIN")) g_tfmin = atof(getenv("QT_TFMIN"));
    if (getenv("QT_BIND")) g_bind = atoi(getenv("QT_BIND"));
    if (getenv("QT_WEXIT")) g_wexit = atoi(getenv("QT_WEXIT"));
    if (getenv("QT_KEPTNS")) g_keptns = atof(getenv("QT_KEPTNS"));
#ifdef HAVE_AVX2_PATH
    g_avx2 = __builtin_cpu_supports("avx2") && !getenv("QT_NOAVX2");
    if (getenv("QT_YSKIP")) g_yskip = 1;
#endif
    double tm = tnow();
    C = load_config_ini();
#endif
    Wx = (C.map_boundaries.x_max - C.map_boundaries.x_min);
    Wy = (C.map_boundaries.y_max - C.map_boundaries.y_min);
    uint64_t n = C.n_points;
#ifdef QF
    rng_base_window(42, g_B);
    const tpRectangle *M = &C.map_boundaries, *Q = &C.sub_boundaries;
    int valid = 1;
    if (!(__builtin_isfinite(M->x_min) && __builtin_isfinite(M->x_max) && __builtin_isfinite(M->y_min) && __builtin_isfinite(M->y_max))) valid = 0;
    if (!(__builtin_isfinite(Wx) && __builtin_isfinite(Wy) && Wx > 0 && Wy > 0)) valid = 0;
    if (__builtin_isnan(Q->x_min) || __builtin_isnan(Q->x_max) || __builtin_isnan(Q->y_min) || __builtin_isnan(Q->y_max)) valid = 0;
    if (valid && !(fx(0) >= M->x_min && fx(MAX_NORMA - 1) <= M->x_max && fy(0) >= M->y_min && fy(MAX_NORMA - 1) <= M->y_max)) valid = 0;
    if (!valid || !no_failure_proof()) handoff();
    if (!(dmax(dmax(fabs(M->x_min), fabs(M->x_max)), dmax(fabs(M->y_min), fabs(M->y_max))) < 0x1p59)) handoff();
#else
    if (n > 0x7fffffffULL) { fprintf(stderr, "num_points out of range\n"); raise(SIGSEGV); abort(); }
    rng_base_window(42, g_B);
    use_emu = libc_rand_is_glibc();
    if (g_prof) {
#ifdef HAVE_AVX2_PATH
        fprintf(stderr, "  phase-1 kernel: %s\n", g_avx2 ? "AVX2" : "SSE2/generic");
#endif
    }
    if (g_prof) fprintf(stderr, "  rng: %s\n", use_emu ? "built-in glibc emulation (libc rand() checked == glibc)" : "libc rand() is NOT glibc: sequential libc rand()");
    PROF("read input + rng init", tm);
    if (!use_emu) {
        KX = malloc(sizeof(uint32_t) * (n + 1)); KY = malloc(sizeof(uint32_t) * (n + 1));
        srand(42);
        for (uint64_t i = 0; i < n; i++) { KX[i] = (uint32_t)(rand() % MAX_NORMA); KY[i] = (uint32_t)(rand() % MAX_NORMA); }
    }
    const tpRectangle *M = &C.map_boundaries, *Q = &C.sub_boundaries;
    int valid = !getenv("QT_FORCE_GENERIC");
    if (!(isfinite(M->x_min) && isfinite(M->x_max) && isfinite(M->y_min) && isfinite(M->y_max))) valid = 0;
    if (!(isfinite(Wx) && isfinite(Wy) && Wx > 0 && Wy > 0)) valid = 0;
    if (isnan(Q->x_min) || isnan(Q->x_max) || isnan(Q->y_min) || isnan(Q->y_max)) valid = 0;
    if (valid && !(fx(0) >= M->x_min && fx(MAX_NORMA - 1) <= M->x_max && fy(0) >= M->y_min && fy(MAX_NORMA - 1) <= M->y_max)) valid = 0;
    int proof = valid && no_failure_proof();
    if (g_prof) fprintf(stderr, "  path: %s\n", !valid ? "serial generic" : proof ? "fast (no-failure proof holds)" : "full parallel build");
#endif
    int D = 0;
    while (D < DMAX && ((uint64_t)64 << (2 * (D + 1))) <= n) D++;
#ifdef QF
    if (!intercept(*M, *Q)) { write_all("Empty\n", 6); qexit(0); }
    if (!fast_path(D)) handoff();
    qexit(0);
#else
    if (!valid) { generic_path(); return finish(); }
    if (!proof || getenv("QT_FORCE_FULL")) {
        int Df = 0;
        while (Df < DMAX && ((uint64_t)2048 << (2 * (Df + 1))) <= n) Df++;
        if (getenv("QT_FULLD")) { Df = atoi(getenv("QT_FULLD")); if (Df < 0) Df = 0; if (Df > DMAX) Df = DMAX; }
        g_full = 1;
        if (!fast_path(Df)) { g_full = 0; generic_path(); }
        return finish();
    }
    if (!intercept(*M, *Q)) { print_results(NULL, 0); return finish(); }
    if (!fast_path(D)) { ramp_cancel(); generic_path(); }
    return finish();
#endif
}
