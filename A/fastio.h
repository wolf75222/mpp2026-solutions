#ifndef FASTIO_H
#define FASTIO_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifndef FIO_G6V2
#define FIO_G6V2 1
#endif
namespace fio {
static const float P10F[11] = {1e0f, 1e1f, 1e2f, 1e3f, 1e4f, 1e5f, 1e6f, 1e7f, 1e8f, 1e9f, 1e10f};
static const double P10D[10] = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9};
static inline bool is_ws(unsigned char c) { return c <= ' '; }
static inline bool is_dig(unsigned char c) { return (unsigned)(c - '0') < 10u; }
static inline const char* parse_float(const char* p, float* out) {
const char* s = p;
bool neg = false;
if (*p == '-') { neg = true; ++p; }
else if (*p == '+') ++p;
uint64_t m = 0;
int nd = 0, e10 = 0;
bool any = false, trunc = false;
while (is_dig(*p)) {
any = true;
unsigned d = (unsigned)(*p - '0');
if (nd < 19) { m = m * 10 + d; if (m) nd++; }
else { e10++; if (d) trunc = true; }
++p;
}
if (*p == '.') {
++p;
while (is_dig(*p)) {
any = true;
unsigned d = (unsigned)(*p - '0');
if (nd < 19) { m = m * 10 + d; if (m) nd++; e10--; }
else if (d) trunc = true;
++p;
}
}
if (!any) goto slow;
if (*p == 'e' || *p == 'E') {
const char* q = p + 1;
bool eneg = false;
if (*q == '-') { eneg = true; ++q; } else if (*q == '+') ++q;
if (!is_dig(*q)) goto slow;
int ex = 0;
while (is_dig(*q)) { if (ex < 100000) ex = ex * 10 + (*q - '0'); ++q; }
e10 += eneg ? -ex : ex;
p = q;
}
if (!is_ws((unsigned char)*p)) goto slow;
if (!trunc) {
if (m == 0) { *out = neg ? -0.0f : 0.0f; return p; }
if (m < (1u << 24) && e10 >= -10 && e10 <= 10) {
float f = (float)m;
f = e10 < 0 ? f / P10F[-e10] : f * P10F[e10];
*out = neg ? -f : f;
return p;
}
}
slow: {
char* end;
*out = strtof(s, &end);
if (end == s) {
while (!is_ws((unsigned char)*end)) ++end;
}
return end;
}
}
static const char DIG2[201] =
"00010203040506070809101112131415161718192021222324252627282930313233343536373839"
"40414243444546474849505152535455565758596061626364656667686970717273747576777879"
"8081828384858687888990919293949596979899";
static inline int ndig_u32(uint32_t v) {
return v < 10 ? 1 : v < 100 ? 2 : v < 1000 ? 3 : v < 10000 ? 4 : v < 100000 ? 5
: v < 1000000 ? 6 : v < 10000000 ? 7 : v < 100000000 ? 8 : v < 1000000000 ? 9 : 10;
}
static inline char* put_u32(char* p, uint32_t v) {
int n = ndig_u32(v);
char* e = p + n;
char* q = e;
while (v >= 100) { uint32_t r = v % 100; v /= 100; q -= 2; memcpy(q, DIG2 + 2 * r, 2); }
if (v >= 10) { q -= 2; memcpy(q, DIG2 + 2 * v, 2); }
else *--q = (char)('0' + v);
return e;
}
static inline char* put_i32(char* p, int v) {
if (v < 0) { *p++ = '-'; return put_u32(p, 0u - (uint32_t)v); }
return put_u32(p, (uint32_t)v);
}
static inline char* put_g6_slow(char* p, float f) {
char t[48];
int n = snprintf(t, sizeof t, "%g", (double)f);
memcpy(p, t, (size_t)n);
return p + n;
}
static inline char* put_g6(char* p, float f) {
double v = (double)f;
double a = fabs(v);
if (!(a < 1e6)) return put_g6_slow(p, f);
if (signbit(v)) *p++ = '-';
if (a == 0.0) { *p++ = '0'; return p; }
uint32_t ia = (uint32_t)a;
if ((double)ia == a) return put_u32(p, ia);
int X;
double s;
if (a >= 1.0) {
X = a >= 1e5 ? 5 : a >= 1e4 ? 4 : a >= 1e3 ? 3 : a >= 1e2 ? 2 : a >= 1e1 ? 1 : 0;
s = a * P10D[5 - X];
} else {
X = -1;
s = a * P10D[6];
while (s < 1e5) {
if (X == -4) { if (v < 0) --p; return put_g6_slow(p, f); }
--X; s *= 10.0;
}
}
double fl = floor(s);
uint32_t r = (uint32_t)fl;
double fr = s - fl;
if (fr > 0.5 || (fr == 0.5 && (r & 1u))) r++;
if (r >= 1000000u) { r = 100000u; X++; if (X >= 6) { if (v < 0) --p; return put_g6_slow(p, f); } }
#if FIO_G6V2
uint64_t w;
{
uint16_t a2, b2, c2;
memcpy(&a2, DIG2 + 2 * (r / 10000), 2);
memcpy(&b2, DIG2 + 2 * ((r / 100) % 100), 2);
memcpy(&c2, DIG2 + 2 * (r % 100), 2);
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
} else p += X + 1;
} else {
memcpy(p, "0.000000", 8);
memcpy(p + 1 - X, &w, 8);
p += 2 - X + last;
}
#else
char d[6];
memcpy(d, DIG2 + 2 * (r / 10000), 2);
memcpy(d + 2, DIG2 + 2 * ((r / 100) % 100), 2);
memcpy(d + 4, DIG2 + 2 * (r % 100), 2);
int last = 5;
while (last > 0 && d[last] == '0') last--;
if (X >= 0) {
memcpy(p, d, (size_t)(X + 1)); p += X + 1;
if (last > X) { *p++ = '.'; memcpy(p, d + X + 1, (size_t)(last - X)); p += last - X; }
} else {
*p++ = '0'; *p++ = '.';
for (int k = -1; k > X; k--) *p++ = '0';
memcpy(p, d, (size_t)(last + 1)); p += last + 1;
}
#endif
return p;
}
}
#endif
