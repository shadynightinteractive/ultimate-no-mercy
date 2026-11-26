// ri_wmmedia.cpp (phase3 p3wmmedia, 2026-11-19): a WrestleMania 2000 / Badd Blood man's theme and Titantron as folder
// files (ri_wmmedia.h) -- tools\handlers\handlers\baddblood\game.py's theme and video functions, with the numeric library
// code they call (scipy.signal.resample_poly / firwin / windows.kaiser, scipy.special.i0, scipy.ndimage.minimum_filter1d,
// numpy's pairwise sums and median, fractions.Fraction.limit_denominator, Python's round) ported to give the same bits.
//
// Every float step below is float64 in the order the Python library does it (build without fast-math and without FMA
// contraction: -ffp-contract=off); the library functions used (sin, exp, pow, sqrt) are checked against the reference's
// values by tools\romimport\native\wmmedia_check.py, which compares the folder files byte for byte.
#include "ri_wmmedia.h"
#include "ri_themes.h"
#include "ri_vadpcm.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <deque>
#include <numeric>

namespace ri {

// ---- Python / numpy helpers -----------------------------------------------------------------------------------------

// numpy's pairwise summation (numpy/_core/src/umath/loops_utils.h.src @TYPE@_pairwise_sum, PW_BLOCKSIZE 128): what
// np.sum and np.mean add with when they reduce a contiguous axis. Under 8 items: in order; up to 128: eight running sums
// (item i into sum i % 8), combined as ((0+1)+(2+3))+((4+5)+(6+7)), then the rest in order; more: the two halves (the
// first a multiple of 8 long) summed so, then added.
double np_pairwise_sum(const double* a, size_t n) {
    if (n < 8) {
        double res = -0.0;
        for (size_t i = 0; i < n; i++) res += a[i];
        return res;
    }
    if (n <= 128) {
        double r[8];
        for (int j = 0; j < 8; j++) r[j] = a[j];
        size_t i;
        for (i = 8; i < n - (n % 8); i += 8)
            for (int j = 0; j < 8; j++) r[j] += a[i + j];
        double res = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
        for (; i < n; i++) res += a[i];
        return res;
    }
    size_t n2 = n / 2;
    n2 -= n2 % 8;
    return np_pairwise_sum(a, n2) + np_pairwise_sum(a + n2, n - n2);
}
// np.sum of a 1-D float64 array: numpy's reduction runs through its iterator's buffer, 8192 items at a time (the default
// buffer size), so the result is 0 + the pairwise sum of each 8192-item block, block after block (checked against the
// reference: firwin's sum of 32,221 taps). (A row of a 2-D array reduced along axis 1 that is under 8192 items long, as
// loudness's windows are, is one block.)
static double np_sum(const double* a, size_t n) {
    double s = 0.0;
    for (size_t i = 0; i < n; i += 8192) s += np_pairwise_sum(a + i, std::min<size_t>(8192, n - i));
    return s;
}

// Python's round(x, nd) for a float: the decimal correctly rounded (half to even on the exact binary value: dtoa mode 3),
// read back as the nearest double. std::to_chars with a precision gives that same decimal.
static double py_round(double x, int nd) {
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf - 1, x, std::chars_format::fixed, nd);
    *r.ptr = 0;
    return strtod(buf, nullptr);
}

// Python's floor division for the signed lengths below
static int64_t floordiv(int64_t a, int64_t b) { int64_t q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) q--; return q; }

// fractions.Fraction(x).limit_denominator(maxd) for a positive float x (Python 3.10's algorithm: the continued fraction's
// convergents until the next denominator would pass maxd, then the closer of the last convergent and the best semiconvergent
// -- the convergent on a tie). x is first taken exactly (float.as_integer_ratio).
static void limit_denominator(double x, int64_t maxd, int64_t& num, int64_t& den) {
    typedef __int128 I;
    if (!(x > 0) || !std::isfinite(x)) fail("limit_denominator: %g", x);
    // x exactly: mantissa / 2^-e, reduced
    int e;
    double m = std::frexp(x, &e);                 // x = m 2^e, 0.5 <= m < 1
    I N = (I)std::ldexp(m, 53); int E = e - 53;   // x = N 2^E
    I D = 1;
    while (E < 0 && (N & 1) == 0) { N >>= 1; E++; }
    if (E >= 0) { N <<= E; } else { if (-E > 120) fail("limit_denominator: %g too small", x); D = (I)1 << -E; }
    if (D <= maxd) { num = (int64_t)N; den = (int64_t)D; return; }
    I p0 = 0, q0 = 1, p1 = 1, q1 = 0, n = N, d = D;
    for (;;) {
        I a = n / d;
        I q2 = q0 + a * q1;
        if (q2 > maxd) break;
        I np1 = p0 + a * p1;
        p0 = p1; q0 = q1; p1 = np1; q1 = q2;
        I nd = n - a * d; n = d; d = nd;
    }
    I k = (maxd - q0) / q1;
    I b1n = p0 + k * p1, b1d = q0 + k * q1;       // bound1 (a semiconvergent)
    // |p1/q1 - N/D| <= |b1n/b1d - N/D|  <=>  |p1 D - N q1| b1d <= |b1n D - N b1d| q1
    auto absI = [](I v) { return v < 0 ? -v : v; };
    I l = absI(p1 * D - N * q1) * b1d, r = absI(b1n * D - N * b1d) * q1;
    I rn, rd;
    if (l <= r) { rn = p1; rd = q1; } else { rn = b1n; rd = b1d; }
    I a = rn, b = rd;                              // (Fraction() reduces)
    while (b) { I t = a % b; a = b; b = t; }
    num = (int64_t)(rn / a); den = (int64_t)(rd / a);
}

// ---- scipy.signal: the Kaiser window, firwin, resample_poly ---------------------------------------------------------

// scipy.special.i0 = xsf/cephes/i0.h with chbevl.h: exp(x) times a Chebyshev series (30 terms for x <= 8, 25 past it)
static const double I0_A[30] = {
    -4.41534164647933937950E-18, 3.33079451882223809783E-17,  -2.43127984654795469359E-16,
    1.71539128555513303061E-15,  -1.16853328779934516808E-14, 7.67618549860493561688E-14,
    -4.85644678311192946090E-13, 2.95505266312963983461E-12,  -1.72682629144155570723E-11,
    9.67580903537323691224E-11,  -5.18979560163526290666E-10, 2.65982372468238665035E-9,
    -1.30002500998624804212E-8,  6.04699502254191894932E-8,   -2.67079385394061173391E-7,
    1.11738753912010371815E-6,   -4.41673835845875056359E-6,  1.64484480707288970893E-5,
    -5.75419501008210370398E-5,  1.88502885095841655729E-4,   -5.76375574538582365885E-4,
    1.63947561694133579842E-3,   -4.32430999505057594430E-3,  1.05464603945949983183E-2,
    -2.37374148058994688156E-2,  4.93052842396707084878E-2,   -9.49010970480476444210E-2,
    1.71620901522208775349E-1,   -3.04682672343198398683E-1,  6.76795274409476084995E-1 };
static const double I0_B[25] = {
    -7.23318048787475395456E-18, -4.83050448594418207126E-18, 4.46562142029675999901E-17,
    3.46122286769746109310E-17,  -2.82762398051658348494E-16, -3.42548561967721913462E-16,
    1.77256013305652638360E-15,  3.81168066935262242075E-15,  -9.55484669882830764870E-15,
    -4.15056934728722208663E-14, 1.54008621752140982691E-14,  3.85277838274214270114E-13,
    7.18012445138366623367E-13,  -1.79417853150680611778E-12, -1.32158118404477131188E-11,
    -3.14991652796324136454E-11, 1.18891471078464383424E-11,  4.94060238822496958910E-10,
    3.39623202570838634515E-9,   2.26666899049817806459E-8,   2.04891858946906374183E-7,
    2.89137052083475648297E-6,   6.88975834691682398426E-5,   3.36911647825569408990E-3,
    8.04490411014108831608E-1 };
static double chbevl(double x, const double* p, int n) {
    double b0 = *p++, b1 = 0.0, b2;
    int i = n - 1;
    do { b2 = b1; b1 = b0; b0 = x * b1 - b2 + *p++; } while (--i);
    return 0.5 * (b0 - b2);
}
// ---- the exp scipy.special calls: MinGW-w64's x87 exp, emulated in software --------------------------------------------
// The reference's scipy (a MinGW-w64 GCC build for Windows) links its special functions with MinGW-w64's own exp
// (mingw-w64-crt math/exp.def.h __expl_internal: x87 code), not the C runtime's: 2^(x log2 e) as an integer power of two
// times 2^f, f the fraction, by the x87's F2XM1. Run inside the reference's Python, the x87 rounds every add / multiply to
// 53 bits (the Windows x64 default control word 0x027F; read back from the reference's values: every one of 132,221 i0
// values matches only with it). So the steps are: t = x L2E (the x87's 64-bit log2 e, the product rounded to 53 bits),
// i = trunc(t), xi = trunc(x), f = ((c0 xi - i) + c0 (x - xi)) + c1 x (c0 + c1 = log2 e split: c0 1.44268798828125,
// c1 a 64-bit constant, each product and sum rounded to 53 bits), then 2^f - 1 to 64 bits (F2XM1: not touched by the
// precision control), plus 1 rounded to 53 bits, times 2^i.
// On x86-64 (with a GCC / clang compiler: the game's clang-cl too) the same x87 instructions run, the control word set to
// 0x027F around them and put back: the reference's own arithmetic, bit for bit. Elsewhere (ARM: no x87) mingw_exp_soft
// does the steps in software: exact integer products and a double-double 2^f - 1 rounded to 64 bits. The x87's F2XM1 is
// not always correctly rounded (1 unit in its last of 64 bits off in about 7 % of arguments on the reference's processor),
// which survives the rounding to 53 bits in about 5 in a million arguments (2 of the 32,221 Kaiser window values for
// the ratio 1611/1076): so the software path is near, not exact, and the x87 path is the one the checks prove.
namespace {
struct U128 { uint64_t hi = 0, lo = 0; };
U128 mul64(uint64_t a, uint64_t b) {                  // the full 128-bit product
    uint64_t a0 = (uint32_t)a, a1 = a >> 32, b0 = (uint32_t)b, b1 = b >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t mid = (p00 >> 32) + (uint32_t)p01 + (uint32_t)p10;
    U128 r;
    r.lo = (mid << 32) | (uint32_t)p00;
    r.hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
    return r;
}
int bitlen(const U128& v) {
    uint64_t w = v.hi ? v.hi : v.lo;
    int n = 0;
    while (w) { n++; w >>= 1; }
    return v.hi ? n + 64 : n;
}
bool bit(const U128& v, int i) { return i >= 64 ? (v.hi >> (i - 64)) & 1 : (v.lo >> i) & 1; }
U128 shr(const U128& v, int s) {
    if (s <= 0) return v;
    U128 r;
    if (s >= 128) return r;
    if (s >= 64) { r.lo = v.hi >> (s - 64); return r; }
    r.lo = (v.lo >> s) | (v.hi << (64 - s)); r.hi = v.hi >> s;
    return r;
}
bool low_bits_nonzero(const U128& v, int n) {         // any of bits 0 .. n-1 set
    if (n <= 0) return false;
    if (n >= 128) return v.hi || v.lo;
    if (n >= 64) return v.lo || (n > 64 && (v.hi << (128 - n)));
    return (v.lo << (64 - n)) != 0;
}
// sign * v 2^e rounded to 53 bits, half to even (the x87 with precision control 53 and rounding to nearest)
double round53(bool neg, const U128& v, int e) {
    int n = bitlen(v);
    if (n == 0) return neg ? -0.0 : 0.0;
    uint64_t q; int sh = n - 53;
    if (sh <= 0) q = v.lo;                          // (fits: exact)
    else {
        q = shr(v, sh).lo;
        bool half = bit(v, sh - 1), rest = low_bits_nonzero(v, sh - 1);
        if (half && (rest || (q & 1))) q++;
        if (q == (1ull << 53)) { q >>= 1; sh++; }
    }
    double r = std::ldexp((double)q, e + std::max(sh, 0));
    return neg ? -r : r;
}
// x (a double) times m 2^e (a 64-bit constant), rounded to 53 bits
double mul_r53(double x, uint64_t m, int e) {
    if (x == 0) return 0.0;
    int ex; double fr = std::frexp(std::fabs(x), &ex);
    uint64_t mx = (uint64_t)std::ldexp(fr, 53);
    return round53(x < 0, mul64(mx, m), ex - 53 + e);
}
// double-double arithmetic (Dekker / Knuth: exact sums and products of doubles, no FMA)
struct DD { double hi, lo; };
DD two_sum(double a, double b) { double s = a + b, bb = s - a; return { s, (a - (s - bb)) + (b - bb) }; }
DD quick_two_sum(double a, double b) { double s = a + b; return { s, b - (s - a) }; }
void split(double a, double& h, double& l) { double c = 134217729.0 * a; h = c - (c - a); l = a - h; }
DD two_prod(double a, double b) {
    double p = a * b, ah, al, bh, bl;
    split(a, ah, al); split(b, bh, bl);
    return { p, ((ah * bh - p) + ah * bl + al * bh) + al * bl };
}
DD dd_add(DD a, DD b) { DD s = two_sum(a.hi, b.hi); s.lo += a.lo + b.lo; return quick_two_sum(s.hi, s.lo); }
DD dd_mul(DD a, DD b) { DD p = two_prod(a.hi, b.hi); p.lo += a.hi * b.lo + a.lo * b.hi; return quick_two_sum(p.hi, p.lo); }
DD dd_div_int(DD a, double n) {                      // a / n for a small integer n
    double q1 = a.hi / n;
    DD p = two_prod(q1, n);
    double q2 = ((a.hi - p.hi) - p.lo + a.lo) / n;
    return quick_two_sum(q1, q2);
}
// F2XM1: 2^f - 1 for |f| < 1, as the x87's 64-bit result: -> sign, mantissa (2^63 <= m < 2^64) and its unit's exponent
bool f2xm1_64(double f, uint64_t& m, int& e) {
    if (f == 0) { m = 0; e = 0; return false; }
    const DD LN2 = { 6.93147180559945286227e-01, 2.31904681384629955842e-17 };
    DD y = dd_mul({ f, 0.0 }, LN2);                 // f ln 2, |y| < 0.7
    DD term = y, sum = y;                           // expm1(y) = y + y^2/2! + ...
    for (int n = 2; n < 60; n++) {
        term = dd_div_int(dd_mul(term, y), n);
        sum = dd_add(sum, term);
        if (std::fabs(term.hi) < std::fabs(sum.hi) * 1e-40) break;
    }
    bool neg = sum.hi < 0;
    double hi = std::fabs(sum.hi), lo = neg ? -sum.lo : sum.lo;
    int eh; std::frexp(hi, &eh);                    // hi = [0.5, 1) 2^eh
    for (int k = 0; k < 2; k++) {
        int u = eh - 64 - k;                         // the unit: 64 bits below hi's top
        uint64_t mh = (uint64_t)std::ldexp(hi, -u);
        double ml = std::nearbyint(std::ldexp(lo, -u));
        uint64_t mm = ml >= 0 ? mh + (uint64_t)ml : mh - (uint64_t)(-ml);
        if (mm < (1ull << 63) && k == 0) continue;   // (lo took it under a power of two: one more bit)
        m = mm; e = u;
        if (ml >= 0 && mm < mh) { m = 1ull << 63; e = u + 1; }  // (carried to 2^64: exactly 2^63 of twice the unit)
        return neg;
    }
    fail("f2xm1: no 64-bit mantissa");
}
}  // namespace

double mingw_exp_soft(double x) {
    if (x == 0) return 1.0;
    if (!(std::fabs(x) < 700)) fail("mingw_exp: %g is outside the range the importer uses", x);
    const uint64_t L2E = 0xB8AA3B295C17F0BCull;     // the x87's log2 e (FLDL2E, rounded to nearest): L2E 2^-63
    const double C0 = 1.44268798828125;              // log2 e's high part (exact as a double)
    const uint64_t C1 = 0xECA5705FC2EEFA20ull;       // its low part: C1 2^-81 (7.0526e-6)
    double t = mul_r53(x, L2E, -63);
    double i = std::trunc(t), xi = std::trunc(x);
    double f = C0 * xi - i;
    f = f + C0 * (x - xi);
    f = f + mul_r53(x, C1, -81);
    uint64_t m; int e;
    bool neg = f2xm1_64(f, m, e);
    double s;
    // 1 + (2^f - 1), rounded to 53 bits: in units of 2^e (2^f - 1 under 2^-60: 1 + it rounds to 1)
    if (m == 0 || e + 64 < -60) s = 1.0;
    else {
        U128 one = { 0, 0 };
        int k = -e;                                  // 1 = 2^k units (k <= 124)
        if (k >= 64) one.hi = 1ull << (k - 64); else one.lo = 1ull << k;
        U128 v;
        if (!neg) { v.lo = one.lo + m; v.hi = one.hi + (v.lo < m ? 1 : 0); }
        else { v.lo = one.lo - m; v.hi = one.hi - (one.lo < m ? 1 : 0); }
        s = round53(false, v, e);
    }
    return std::ldexp(s, (int)i);                    // (FSCALE by i, then stored as a double: exact here)
}

double mingw_exp(double x) {
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    if (x == 0) return 1.0;
    if (!(std::fabs(x) < 700)) fail("mingw_exp: %g is outside the range the importer uses", x);
    // mingw-w64-crt math/exp.def.h __expl_internal, x86-64 path, verbatim; c0 / c1 as its 80-bit constants
    static const unsigned char c0[10] = { 0, 0, 0, 0, 0, 0, 0xAA, 0xB8, 0xFF, 0x3F };                   // 1.44268798828125
    static const unsigned char c1[10] = { 0x20, 0xFA, 0xEE, 0xC2, 0x5F, 0x70, 0xA5, 0xEC, 0xED, 0x3F };  // 7.0526e-6
    const uint16_t cw = 0x027F;          // (precision 53 bits, round to nearest, all exceptions masked)
    uint16_t old;
    double res;
    __asm__ volatile(
        "fnstcw %[old]\n\t"
        "fldcw %[cw]\n\t"
        "fldl %[x]\n\t"
        "fldl2e\n\t"                     // log2(e)
        "fmul %%st(1),%%st\n\t"          // x log2(e)
        "subq $8, %%rsp\n\t"
        "fnstcw 4(%%rsp)\n\t"
        "movzwl 4(%%rsp), %%eax\n\t"
        "orb $12, %%ah\n\t"              // (round toward zero for the two FRNDINTs)
        "movw %%ax, (%%rsp)\n\t"
        "fldcw (%%rsp)\n\t"
        "frndint\n\t"                    // i
        "fld %%st(1)\n\t"                // x
        "frndint\n\t"                    // xi
        "fldcw 4(%%rsp)\n\t"
        "addq $8, %%rsp\n\t"
        "fld %%st(1)\n\t"                // i
        "fldt %[c0]\n\t"                 // c0
        "fld %%st(2)\n\t"                // xi
        "fmul %%st(1),%%st\n\t"          // c0 xi
        "fsubp %%st,%%st(2)\n\t"         // f = c0 xi - i
        "fld %%st(4)\n\t"                // x
        "fsub %%st(3),%%st\n\t"          // xf = x - xi
        "fmulp %%st,%%st(1)\n\t"         // c0 xf
        "faddp %%st,%%st(1)\n\t"         // f = f + c0 xf
        "fldt %[c1]\n\t"
        "fmul %%st(4),%%st\n\t"          // c1 x
        "faddp %%st,%%st(1)\n\t"         // f = f + c1 x
        "f2xm1\n\t"                      // 2^f - 1
        "fld1\n\t"
        "faddp\n\t"                      // 2^f
        "fstp %%st(1)\n\t"
        "fscale\n\t"                     // times 2^i
        "fstp %%st(1)\n\t"
        "fstp %%st(1)\n\t"
        "fstpl %[res]\n\t"               // (as a double)
        "fldcw %[old]\n\t"
        : [res] "=m"(res), [old] "=m"(old)
        : [x] "m"(x), [cw] "m"(cw), [c0] "m"(c0), [c1] "m"(c1)
        : "rax", "memory", "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)");
    return res;
#else
    return mingw_exp_soft(x);
#endif
}

static double sp_i0(double x) {
    if (x < 0) x = -x;
    if (x <= 8.0) return mingw_exp(x) * chbevl((x / 2.0) - 2.0, I0_A, 30);
    return mingw_exp(x) * chbevl(32.0 / x - 2.0, I0_B, 25) / std::sqrt(x);
}

// scipy.signal.windows.kaiser(M, beta, sym=True) (firwin asks get_window(..., fftbins=False): symmetric):
// w[n] = i0(beta sqrt(1 - ((n - alpha) / alpha)^2)) / i0(beta), alpha = (M - 1) / 2
static std::vector<double> sp_kaiser(int M, double beta) {
    if (M <= 1) return std::vector<double>((size_t)std::max(M, 0), 1.0);
    double alpha = (M - 1) / 2.0, den = sp_i0(beta);
    std::vector<double> w((size_t)M);
    for (int n = 0; n < M; n++) {
        double r = ((double)n - alpha) / alpha;
        w[(size_t)n] = sp_i0(beta * std::sqrt(1 - r * r)) / den;    // (** 2.0 is numpy's square: r * r)
    }
    return w;
}

// scipy.signal.firwin(numtaps, cutoff, window=("kaiser", beta)), a low-pass (pass_zero True, fs 2 so nyq 1.0, scale True):
// the ideal filter right sinc(right m) (minus left sinc(left m) with left 0: nothing), m = n - (numtaps - 1) / 2, times the
// window, then scaled to a gain of 1 at 0 (divided by its sum: cos(pi m 0) is 1 for every tap).
std::vector<double> sp_firwin_kaiser(int numtaps, double cutoff, double beta) {
    const double PI = 3.141592653589793;              // (numpy's pi)
    double right = cutoff / 1.0;                      // (cutoff / nyq)
    double alpha = 0.5 * (numtaps - 1);
    std::vector<double> win = sp_kaiser(numtaps, beta), h((size_t)numtaps);
    for (int i = 0; i < numtaps; i++) {
        double m = (double)i - alpha;
        double x = right * m;
        double y = PI * (x == 0 ? 1.0e-20 : x);       // np.sinc: sin(pi x) / (pi x), x 0 taken as 1e-20
        double s = std::sin(y) / y;
        h[(size_t)i] = (0 + right * s) - 0.0 * 1.0;   // (h = 0; h += right sinc; h -= left sinc(left m): 0 sinc(0) = 0)
        h[(size_t)i] *= win[(size_t)i];
    }
    double s = np_sum(h.data(), h.size());
    for (auto& v : h) v /= s;
    return h;
}

// scipy.signal.resample_poly(x, up, down, window=("kaiser", beta)), padtype "constant" (zeros beyond the ends):
// up / down reduced; firwin(2 half_len + 1, 1 / max(up, down)) times up (the zero-stuffing's gain), zeros put before it so
// the output samples fall on the input's (n_pre_pad) and after it until the output is long enough (n_post_pad); then
// upfirdn (scipy/signal/_upfirdn_apply.pyx _apply_impl: output i is phase t = i down % up of the filter -- its taps taken
// transposed and flipped (_upfirdn.py _pad_h) -- over the inputs up to i down / up, summed from the oldest input on, from
// 0.0) and the n_out outputs from n_pre_remove kept.
std::vector<double> sp_resample_poly_kaiser(const std::vector<double>& x, int64_t up, int64_t down, double beta) {
    if (up < 1 || down < 1) fail("resample_poly: up and down must be >= 1");
    int64_t g = std::gcd(up, down); up /= g; down /= g;
    if (up == 1 && down == 1) return x;
    const int64_t n_in = (int64_t)x.size();
    int64_t n_out = n_in * up;
    n_out = n_out / down + (n_out % down ? 1 : 0);
    const int64_t max_rate = std::max(up, down), half_len = 10 * max_rate;
    std::vector<double> h0 = sp_firwin_kaiser((int)(2 * half_len + 1), 1. / (double)max_rate, beta);
    for (auto& v : h0) v *= (double)up;
    auto output_len = [&](int64_t len_h) { return (((n_in - 1) * up + len_h) - 1) / down + 1; };   // (_upfirdn_apply _output_len)
    const int64_t n_pre_pad = down - half_len % down;
    const int64_t n_pre_remove = (half_len + n_pre_pad) / down;
    int64_t n_post_pad = 0;
    while (output_len((int64_t)h0.size() + n_pre_pad + n_post_pad) < n_out + n_pre_remove) n_post_pad++;
    std::vector<double> h((size_t)n_pre_pad, 0.0);
    h.insert(h.end(), h0.begin(), h0.end());
    h.resize(h.size() + (size_t)n_post_pad, 0.0);
    // _pad_h: zero-padded to a multiple of up, then phase c's taps reversed: htf[c hpp + j] = h[(hpp - 1 - j) up + c]
    const int64_t L = (int64_t)h.size(), Lp = L + ((up - L % up) % up), hpp = Lp / up;
    h.resize((size_t)Lp, 0.0);
    std::vector<double> htf((size_t)Lp);
    for (int64_t c = 0; c < up; c++)
        for (int64_t j = 0; j < hpp; j++) htf[(size_t)(c * hpp + j)] = h[(size_t)((hpp - 1 - j) * up + c)];
    const int64_t len_out = output_len(L), padded_len = n_in + hpp - 1;
    std::vector<double> y((size_t)n_out, 0.0);
    for (int64_t k = 0; k < n_out; k++) {
        int64_t yi = n_pre_remove + k;              // its index in upfirdn's whole output
        if (yi >= len_out) break;
        int64_t pos = yi * down, xi = pos / up, t = pos % up;
        if (xi >= padded_len) continue;              // (past the flush loop's end: upfirdn leaves 0)
        int64_t hi = t * hpp, xc = xi - hpp + 1;
        if (xc < 0) { hi -= xc; xc = 0; }            // (zero padding before the start: those taps skipped)
        double acc = 0.0;
        for (; xc <= xi; xc++, hi++) {
            if (xc >= n_in) continue;                // (zeros after the end add +-0: nothing)
            acc = acc + x[(size_t)xc] * htf[(size_t)hi];
        }
        y[(size_t)k] = acc;
    }
    return y;
}

// ---- mod\gen\mp3theme.py: the level -------------------------------------------------------------------------------
static const double TARGET_MED = 8800.0, CEIL = 31000.0;

// mp3theme.loudness(y, R)[0]: the median of the 0.4 s windows' RMS (whole windows only) over those above 200
static double loudness_median(const std::vector<double>& y, int R) {
    const size_t W = (size_t)(int64_t)(0.4 * R), k = y.size() / W;
    std::vector<double> r, sq(W);
    for (size_t i = 0; i < k; i++) {
        for (size_t j = 0; j < W; j++) { double v = y[i * W + j]; sq[j] = v * v; }
        double rms = std::sqrt(np_sum(sq.data(), W) / (double)W);
        if (rms > 200) r.push_back(rms);
    }
    if (r.empty()) fail("theme: no window above the loudness floor");
    // np.median: the middle item, or the mean of the middle two (their sum / 2)
    size_t n = r.size(), h = n / 2;
    std::nth_element(r.begin(), r.begin() + h, r.end());
    double hi = r[h];
    if (n % 2) return hi;
    double lo = *std::max_element(r.begin(), r.begin() + h);
    return (-0.0 + lo + hi) / 2.0;
}

// mp3theme.limit(y, ceil, R): the gain each sample needs (ceil / |y|, at most 1), its minimum over +-5 ms
// (ndimage.minimum_filter1d, size 2 la + 1, mode "reflect": the ends mirrored, edge sample repeated), applied with an
// instant attack and a release over 120 ms
static std::vector<double> limit(const std::vector<double>& y, int R) {
    const size_t n = y.size();
    std::vector<double> need(n);
    for (size_t i = 0; i < n; i++) need[i] = std::min(1.0, CEIL / std::max(std::fabs(y[i]), 1e-9));
    const int64_t la = std::max<int64_t>(1, (int64_t)(0.005 * R));
    // the reflected line: index i of the extended run is input index i - la, mirrored at both ends (period 2n)
    auto at = [&](int64_t i) -> double {
        int64_t p = 2 * (int64_t)n, j = ((i % p) + p) % p;
        return need[(size_t)(j < (int64_t)n ? j : p - 1 - j)];
    };
    std::vector<double> g(n);
    std::deque<int64_t> dq;                      // indexes of the extended run, their values increasing
    const int64_t total = (int64_t)n + 2 * la;
    for (int64_t i = 0; i < total; i++) {
        double v = at(i - la);
        while (!dq.empty() && at(dq.back() - la) >= v) dq.pop_back();
        dq.push_back(i);
        if (dq.front() <= i - (2 * la + 1)) dq.pop_front();
        if (i >= 2 * la) g[(size_t)(i - 2 * la)] = at(dq.front() - la);
    }
    const double rel = std::exp(-1.0 / (0.120 * R));
    std::vector<double> out(n);
    double cur = 1.0;
    for (size_t i = 0; i < n; i++) {
        cur = g[i] < cur ? g[i] : g[i] + (cur - g[i]) * rel;
        out[i] = y[i] * cur;
    }
    return out;
}

// ---- baddblood\game.py: the theme ---------------------------------------------------------------------------------
static uint32_t ram32(const Bytes& ram, uint32_t a) { a &= 0x7FFFFF; if (a + 4 > ram.size()) fail("RAM read past the image at %X", a); return be32(ram, a); }
static uint32_t ram16(const Bytes& ram, uint32_t a) { a &= 0x7FFFFF; if (a + 2 > ram.size()) fail("RAM read past the image at %X", a); return be16(ram, a); }
static void ram_copy(uint8_t* dst, const Bytes& ram, uint32_t a, size_t n) {
    a &= 0x7FFFFF;
    if (a + n > ram.size()) fail("RAM read past the image at %X", a);
    memcpy(dst, ram.data() + a, n);
}

static const uint32_t ENTRANCES = 0x8010206C;   // the entrance table: slot k at + 12 k; its music bank sound at + 8 (s16)
static const int THEME_RATE = 10787;            // the PC player's rate for every theme.bin
static const double OUT_RATE = 28818.9;         // the game's output rate for a tuning of -48 (game.py's measurement)

int wm_theme_sound(const Bytes& sel_ram, int slot) {
    return sx16(ram16(sel_ram, ENTRANCES + 12 * (uint32_t)slot + 8));
}

// game.py theme(rom, ram, sound): the music bank's sound -> (its VADPCM data, the folder-style wave record + level, loops,
// tune), and the about dict's first keys
struct RawTheme { Bytes data, meta; int instrument; uint32_t base, len; bool loops; };
static RawTheme raw_theme(const Bytes& rom, const Bytes& ram, int sound) {
    uint32_t WB = ram32(ram, 0x800571EC), WC = ram32(ram, WB + 0x10);
    uint32_t wp = ram32(ram, WB + 0x18 + 8 * (uint32_t)sound);
    uint8_t sc[24]; ram_copy(sc, ram, wp, 24);
    int at = -1;
    for (int i = 0; i < 24; i++) if (sc[i] == 0x81) { at = i; break; }
    if (at < 0 || at + 1 >= 24) fail("sound %d: its script has no instrument (0x81)", sound);
    RawTheme t;
    t.instrument = (int)ram16(ram, ram32(ram, WB + 0x14) + 2 * sc[at + 1]);
    uint32_t ni = (uint32_t)t.instrument;
    uint32_t e = ram32(ram, ram32(ram, WC + 0x2C) + 4 * ni);
    t.base = ram32(ram, e); t.len = ram32(ram, e + 4);
    Bytes w(0xD0, 0);
    ram_copy(&w[0], ram, e, 0x18);
    // (the record's ROM offset and its pointers mean nothing in a folder: 0, as tools/wrestler_theme.py writes them)
    put_be32(&w[0], 0); memset(&w[0x0C], 0, 0x0C);
    ram_copy(&w[0x18], ram, ram32(ram, e + 0x10), 0x88);                 // the codebook
    uint32_t lp = ram32(ram, e + 0x0C);
    if (lp) ram_copy(&w[0xA0], ram, lp, 0x2C);                          // the loop
    uint8_t level = ram[(ram32(ram, WC + 0x24) & 0x7FFFFF) + ni];
    uint32_t tune = ram32(ram, ram32(ram, WC + 0x28) + 4 * ni);
    t.loops = lp != 0;
    size_t b0 = std::min<size_t>(t.base, rom.size()), b1 = std::min<size_t>((size_t)t.base + t.len, rom.size());
    t.data.assign(rom.begin() + b0, rom.begin() + std::max(b0, b1));   // (rom[base:base + ln])
    t.meta = w;
    uint8_t tail[8] = { level, (uint8_t)(t.loops ? 1 : 0), 0, 0 };
    put_be32(tail + 4, tune);
    t.meta.insert(t.meta.end(), tail, tail + 8);
    return t;
}

FolderTheme wm_theme_folder(const Bytes& rom, const Bytes& sel_ram, int slot, WmThemeSteps* steps) {
    int sound = wm_theme_sound(sel_ram, slot);
    if (sound < 0) fail("music slot %d plays no sound", slot);
    RawTheme T = raw_theme(rom, sel_ram, sound);
    const Bytes& meta = T.meta;
    // the rate the game plays it at: OUT_RATE 2^((tuning + 48) / 12), the tuning a float in the record's last word
    uint32_t tb = be32(meta, 0xD4); float tf; memcpy(&tf, &tb, 4);
    double tune = tf;
    double src_rate = OUT_RATE * std::pow(2.0, (tune + 48.0) / 12.0);
    // decoded (its loop end the sound's end when it loops)
    int32_t order = s32(be32(meta, 0x18)), npred = s32(be32(meta, 0x1C));
    Bytes book(meta.begin() + 0x18, meta.begin() + 0x18 + 8 + order * npred * 16);
    // (the Python asks for len(data) * 16 // 9 samples; a sound whose bytes aren't whole 9-byte frames -- Badd Blood's Vader:
    //  73432 bytes, 130545 samples asked, 8159 whole frames -- gets its last sample from a frame vadpcm_dec reads past the
    //  data. Decoded here: the whole frames; the samples past them must be cut by the loop end below, else it is said.)
    size_t n_ask = T.data.size() * 16 / 9, n_whole = T.data.size() / 9 * 16;
    std::vector<int16_t> dec = vadpcm_decode(T.data, book, std::min(n_ask, n_whole));
    std::vector<double> pcm(dec.begin(), dec.end());
    uint32_t loop0 = be32(meta, 0xA0), loop1 = be32(meta, 0xA4);
    if (n_ask > n_whole) {
        if (!(T.loops && loop1 && loop1 <= n_whole))
            fail("music sound %d: %zu samples asked of %zu bytes, and its loop end doesn't cut the part past the whole frames",
                 sound, n_ask, T.data.size());
        pcm.resize(n_ask, 0.0);      // (cut at the loop end next: never used)
    }
    if (T.loops && loop1 && loop1 < pcm.size()) pcm.resize(loop1);
    // resampled to THEME_RATE by the ratio as a fraction of denominator <= 4000
    int64_t up, down;
    limit_denominator((double)THEME_RATE / src_rate, 4000, up, down);
    std::vector<double> y = sp_resample_poly_kaiser(pcm, up, down, 8.0);
    // the loop start moved with it, both ends whole 16-sample frames: ls = int(round(loop0 * fr)) // 16 * 16
    int64_t ls = 0;
    if (T.loops) {
        __int128 p = (__int128)loop0 * up; int64_t q = down;
        int64_t fl = (int64_t)(p / q), rem = (int64_t)(p - (__int128)fl * q);
        int64_t r = 2 * rem < q ? fl : 2 * rem > q ? fl + 1 : (fl % 2 == 0 ? fl : fl + 1);   // (round half to even)
        ls = floordiv(r, 16) * 16;
    }
    int64_t n = floordiv((int64_t)y.size() - ls, 16) * 16 + ls;
    if (n < 0) n = std::max<int64_t>(0, (int64_t)y.size() + n);     // (Python's y[:n])
    if (n < (int64_t)y.size()) y.resize((size_t)n);
    // levelled: brought to the game's themes' median loudness, peaks held under the ceiling
    double med = loudness_median(y, THEME_RATE);
    std::vector<double> ys(y.size());
    double gain = TARGET_MED / med;
    for (size_t i = 0; i < y.size(); i++) ys[i] = y[i] * gain;
    std::vector<double> y2 = limit(ys, THEME_RATE);
    std::vector<int16_t> out(y2.size());
    for (size_t i = 0; i < y2.size(); i++) {
        double v = std::nearbyint(y2[i]);                            // (np.round: half to even)
        out[i] = (int16_t)std::min(32767.0, std::max(-32768.0, v));
    }
    // encoded again, its own codebook (theme.py encode's files: the same wave record, level 0, tune -48)
    ThemeFiles F = theme_encode(out, ls, THEME_RATE);
    FolderTheme r;
    r.bin = F.bin; r.meta = F.meta;
    ojson& a = r.about;
    a["sound"] = sound;
    a["instrument"] = T.instrument;
    a["rom_offset"] = strf("0x%X", T.base);
    a["bytes"] = T.len;
    a["loops"] = T.loops;
    a["slot"] = slot;
    a["tuning"] = py_round(tune, 3);
    a["source_rate"] = py_round(src_rate, 1);
    a["rate"] = THEME_RATE;
    a["seconds"] = py_round((double)F.samples / THEME_RATE, 1);
    a["loop_from_s"] = py_round((double)ls / THEME_RATE, 1);
    std::string frs = down == 1 ? strf("%lld", (long long)up) : strf("%lld/%lld", (long long)up, (long long)down);
    a["made"] = "decoded, resampled " + frs + ", levelled, VADPCM again";
    if (steps) {
        steps->pcm = pcm; steps->y = y; steps->y2 = y2;
        int64_t mr = std::max(up, down);
        steps->h = sp_firwin_kaiser((int)(20 * mr + 1), 1. / (double)mr, 8.0);
        steps->src_rate = src_rate; steps->med = med; steps->up = (int)up; steps->down = (int)down; steps->ls = (int)ls;
    }
    return r;
}

std::string wm_theme_none(int slot) {
    return strf("none: his record's music slot %d plays no sound in WrestleMania 2000 (its sound is -1)", slot);
}

// ---- baddblood\game.py: the video ---------------------------------------------------------------------------------
static const size_t FRAME_BYTES = 16 + 512 + 64 * 64;

FolderVideo wm_video(const RomFiles& files, const Bytes& ram, int v) {
    // video_list: its frame list [(frame file, time)], 0xFFFF-ended, from the entrance table at 0x80102070 + 12 v
    std::vector<std::pair<uint32_t, uint32_t>> lst;
    uint32_t a = ram32(ram, 0x80102070 + 12 * (uint32_t)v);
    while (ram16(ram, a) != 0xFFFF) {
        lst.push_back({ ram16(ram, a), ram16(ram, a + 2) }); a += 4;
        if (lst.size() >= 512) fail("video %d: its frame list has no end", v);
    }
    for (auto& f : lst) {
        const Bytes& b = files(f.first);
        if (b.size() != FRAME_BYTES || memcmp(b.data(), "TEX", 3)) fail("frame file 0x%X isn't a 64 x 64 CI8 frame", f.first);
    }
    // each entry stays up (time & 0x7FFF) ticks of 1/30 s; the folder's video is sampled every 4 ticks (7.5 a second)
    std::vector<uint32_t> ends; uint32_t t = 0;
    for (auto& f : lst) { t += f.second & 0x7FFF; ends.push_back(t); }
    FolderVideo r;
    size_t shown = 0, i = 0;
    for (uint32_t tick = 0; tick < t; tick += 4) {
        while (!(tick < ends[i])) i++;                        // (the first entry still up at this tick; ticks rise)
        const Bytes& b = files(lst[i].first);
        r.bin.insert(r.bin.end(), b.begin(), b.end());
        shown++;
    }
    std::vector<uint32_t> uniq;
    for (auto& f : lst) uniq.push_back(f.first);
    std::sort(uniq.begin(), uniq.end()); uniq.erase(std::unique(uniq.begin(), uniq.end()), uniq.end());
    ojson ff = ojson::array();
    for (auto f : uniq) ff.push_back(strf("0x%X", f));
    r.about["video"] = v;
    r.about["entries"] = lst.size();
    r.about["ticks"] = t;
    r.about["frame_files"] = ff;
    r.about["frames_written"] = shown;
    return r;
}

std::string wm_video_none() { return "none: his record's video is 0 (the game shows none for him)"; }

}  // namespace ri
