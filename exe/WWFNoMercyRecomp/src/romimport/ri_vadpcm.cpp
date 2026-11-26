// ri_vadpcm.cpp (phase3, 2026-11-19): N64 VADPCM encode and decode in memory (ri_vadpcm.h) -- the C++ twin of
// mod\gen\vadpcm.py, which runs the N64 SDK's audio tools. This file mirrors those tools' algorithms as reconstructed by
// the sm64 decompilation project (tools/sdk-tools):
//   tabledesign  -- the codebook: each 16-sample frame's autocorrelation, solved for a predictor (LU decomposition) and
//                   kept as reflection coefficients; their average (Levinson-Durbin) is the first predictor, then the set
//                   is split in two and refined (nearest-predictor clustering) until there are 2^bits predictors;
//   vadpcm_enc   -- per frame, the predictor with the least squared error, the smallest power-of-two scale that fits the
//                   residuals in 4 bits, then the 4-bit quantisation (decoded as it goes, as the player will decode it);
//   vadpcm_dec   -- the frames back to PCM.
// The arithmetic is the tools' own, step for step (doubles in tabledesign; 32-bit integers and single floats in the
// encoder; the same order of operations, the same truncations and clamps), so the bytes are the same: an encoded theme
// is what the developer's build made.
//
// CREDIT (relcheck 2026-11-19; publishing approved by the user on 2026-11-19: "I looked at your list you needed from me and
// I concur with your recommendations"). Ported from the N64 SDK tools' reconstruction in the Super Mario 64 decompilation,
// n64decomp/sm64 (https://github.com/n64decomp/sm64), tools/sdk-tools/tabledesign (tabledesign.c, estimate.c, codebook.c,
// print.c) and tools/sdk-tools/adpcm (vpredictor.c, vencode.c, quant.c, vdecode.c, sampleio.c) -- released under CC0 1.0
// Universal. EXCEPT the frame's linear solve (lu_decompose / lu_solve below), which is OUR OWN code (relclean 2026-11-19,
// the user's decision of 2026-11-19): estimate.c's two routines for it (lud / lubksb) are marked there as derived from
// Numerical Recipes in C, whose licence restricts redistributing its code, so they were rewritten from the mathematics (an LU
// factorisation with partial pivoting and its substitutions), not from that code -- same results, every theme the same bytes.
// See CREDITS.md at the repository's root.
//
// No floating-point contraction: a fused multiply-add rounds once where the tools round twice (they are built for x86-64
// SSE2, which has no FMA), and the bytes would differ.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif
#include "ri_vadpcm.h"
#include <climits>
#include <cmath>
#include <utility>

namespace ri {
namespace {

// vadpcm.py runs "tabledesign -s 2": order 2, 2 bits (4 predictors); the tool's defaults for the rest.
constexpr int ORDER = 2;
constexpr int BITS = 2;
constexpr int NPRED = 1 << BITS;
constexpr int REFINE_ITERS = 2;
constexpr int FRAME = 16;
constexpr double THRESH = 10.0;

// ---- how the tools' C converts and overflows on x86-64 (gcc), spelled out ------------------------------------------
// (int) of a double: truncation; NaN or out of range gives INT_MIN (the cvttsd2si instruction's answer).
inline int32_t cvt_i32(double x) {
    if (!(x > -2147483649.0 && x < 2147483648.0)) return INT32_MIN;
    return (int32_t)x;
}
// (s16) of an int: its low 16 bits.
inline int16_t low16(int32_t v) { return (int16_t)(uint16_t)(uint32_t)v; }
// 32-bit integer sums and products wrap around (what the tools' int arithmetic does on the machine).
inline int32_t wadd(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }
inline int32_t wsub(int32_t a, int32_t b) { return (int32_t)((uint32_t)a - (uint32_t)b); }
inline int32_t wmul(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }

// =====================================================================================================================
// tabledesign (tabledesign.c, estimate.c, codebook.c, print.c). Vectors are 1-indexed as the tool's are: [0..ORDER].
// =====================================================================================================================
typedef double Vec[ORDER + 1];
typedef double Mat[ORDER + 1][ORDER + 1];

// estimate.c acvect: the frame's autocorrelation vector, negated (in[-ORDER..-1] is the previous frame's end).
void acvect(const int16_t* in, int n, int m, double* out) {
    for (int i = 0; i <= n; i++) {
        out[i] = 0.0;
        for (int j = 0; j < m; j++) out[i] -= in[j - i] * in[j];   // (an int product, then the double subtraction)
    }
}

// estimate.c acmat: the frame's autocorrelation matrix (1-indexed).
void acmat(const int16_t* in, int n, int m, Mat out) {
    for (int i = 1; i <= n; i++)
        for (int j = 1; j <= n; j++) {
            out[i][j] = 0.0;
            for (int k = 0; k < m; k++) out[i][j] += in[k - i] * in[k - j];
        }
}

// ---- the frame's linear solve: A x = b for the ORDER x ORDER autocorrelation matrix -----------------------------------
// OUR OWN CODE (relclean, 2026-11-19), written from the mathematics, not from any earlier program: an LU factorisation with
// partial pivoting, then forward and back substitution. With P A = L U (L unit lower triangular, U upper triangular), the
// factorisation goes column by column; for column j:
//   - the entries of U above the diagonal:   u(i,j) = a(i,j) - sum over k < i of l(i,k) u(k,j)          (i < j)
//   - the candidates for the diagonal:       c(i)   = a(i,j) - sum over k < j of l(i,k) u(k,j)          (i >= j)
//   - the pivot: the candidate largest relative to its row's size in the original matrix (|c(i)| / max_k |a(i,k)|); on a
//     tie the later row; that row is exchanged with row j (its whole row: the l's already made and the a's still to come);
//   - below the diagonal:                    l(i,j) = c(i) * (1 / u(j,j))                                (i > j)
// Every sum is taken left to right from a(i,j), k increasing, each product subtracted as it comes; the l's are a
// multiplication by the pivot's reciprocal. That fixes the rounding, so the predictors -- and the encoded themes -- come out
// the same bits as the developer's SDK tools made them (the test: tools\romimport\native\vadpcm_check.py, every case).
// The matrix is refused (1) when a row is all zeros, when a pivot is zero, or when it is too badly conditioned to trust:
// the smallest |u(i,i)| under 1e-10 of the largest.
// Arrays are 1-indexed ([1..n]), as the rest of tabledesign here. perm[j]: the row exchanged with row j at column j.
int lu_decompose(Mat a, int n, int* perm, int* parity) {
    double row_scale[ORDER + 1];
    for (int i = 1; i <= n; i++) {
        double biggest = 0.0;
        for (int k = 1; k <= n; k++) biggest = std::fmax(biggest, std::fabs(a[i][k]));
        if (biggest == 0.0) return 1;                              // (a row of zeros: no solution)
        row_scale[i] = 1.0 / biggest;
    }
    *parity = 1;
    for (int j = 1; j <= n; j++) {
        // U above the diagonal
        for (int i = 1; i < j; i++) {
            double u = a[i][j];
            for (int k = 1; k < i; k++) u -= a[i][k] * a[k][j];
            a[i][j] = u;
        }
        // the diagonal's candidates, and the pivot among them
        int pivot_row = j;
        double best = -1.0;
        for (int i = j; i <= n; i++) {
            double c = a[i][j];
            for (int k = 1; k < j; k++) c -= a[i][k] * a[k][j];
            a[i][j] = c;
            const double weight = row_scale[i] * std::fabs(c);
            if (!(weight < best)) { best = weight; pivot_row = i; }   // (a tie goes to the later row)
        }
        if (pivot_row != j) {
            for (int k = 1; k <= n; k++) std::swap(a[pivot_row][k], a[j][k]);
            row_scale[pivot_row] = row_scale[j];
            *parity = -*parity;
        }
        perm[j] = pivot_row;
        if (a[j][j] == 0.0) return 1;                              // (singular)
        if (j < n) {
            const double inv = 1.0 / a[j][j];
            for (int i = j + 1; i <= n; i++) a[i][j] *= inv;
        }
    }
    // conditioning: the diagonal of U, smallest against largest
    double lo = std::fabs(a[1][1]), hi = lo;
    for (int i = 2; i <= n; i++) { lo = std::fmin(lo, std::fabs(a[i][i])); hi = std::fmax(hi, std::fabs(a[i][i])); }
    return lo / hi < 1e-10 ? 1 : 0;
}

// Solves A x = b with lu_decompose's L, U and row exchanges (a and perm as it left them); b becomes x. The row exchanges are
// applied to b in the order they were made, then L y = P b forward (y(i) = b(i) - sum over k < i of l(i,k) y(k)), then
// U x = y backward (x(i) = (y(i) - sum over k > i of u(i,k) x(k)) / u(i,i)); sums left to right, k increasing.
void lu_solve(Mat a, int n, const int* perm, double* b) {
    for (int j = 1; j <= n; j++) std::swap(b[j], b[perm[j]]);
    for (int i = 2; i <= n; i++) {
        double y = b[i];
        for (int k = 1; k < i; k++) y -= a[i][k] * b[k];
        b[i] = y;
    }
    for (int i = n; i >= 1; i--) {
        double x = b[i];
        for (int k = i + 1; k <= n; k++) x -= a[i][k] * b[k];
        b[i] = x / a[i][i];
    }
}

// estimate.c kfroma: predictor coefficients -> reflection coefficients (step-down recursion; 'in' is consumed).
// Nonzero when one is outside [-1, 1] (or the recursion divides by zero).
int kfroma(double* in, double* out, int n) {
    int ret = 0;
    Vec next;
    out[n] = in[n];
    for (int i = n - 1; i >= 1; i--) {
        for (int j = 0; j <= i; j++) {
            double temp = out[i + 1];
            double div = 1.0 - (temp * temp);
            if (div == 0.0) return 1;
            next[j] = (in[j] - in[i + 1 - j] * temp) / div;
        }
        for (int j = 0; j <= i; j++) in[j] = next[j];
        out[i] = next[i];
        if (std::fabs(out[i]) > 1.0) ret++;
    }
    return ret;
}

// estimate.c afromk: reflection coefficients -> predictor coefficients (step-up recursion).
void afromk(const double* in, double* out, int n) {
    out[0] = 1.0;
    for (int i = 1; i <= n; i++) {
        out[i] = in[i];
        for (int j = 1; j <= i - 1; j++) out[j] += out[i - j] * out[i];
    }
}

// estimate.c rfroma: predictor coefficients -> the normalised autocorrelation they model.
void rfroma(const double* arg0, int n, double* arg2) {
    double mat[ORDER + 1][ORDER + 1];     // the tool's rows mat[i] (i + 1 wide; row n is n + 1 wide)
    mat[n][0] = 1.0;
    for (int i = 1; i <= n; i++) mat[n][i] = -arg0[i];
    for (int i = n; i >= 1; i--) {
        double div = 1.0 - mat[i][i] * mat[i][i];
        for (int j = 1; j <= i - 1; j++) mat[i - 1][j] = (mat[i][i - j] * mat[i][i] + mat[i][j]) / div;
    }
    arg2[0] = 1.0;
    for (int i = 1; i <= n; i++) {
        arg2[i] = 0.0;
        for (int j = 1; j <= i; j++) arg2[i] += mat[i][j] * arg2[i - j];
    }
}

// estimate.c durbin: Levinson-Durbin -- an autocorrelation -> predictor (arg3) and reflection (arg2) coefficients.
int durbin(const double* arg0, int n, double* arg2, double* arg3, double* outSomething) {
    int ret = 0;
    arg3[0] = 1.0;
    double div = arg0[0];
    for (int i = 1; i <= n; i++) {
        double sum = 0.0;
        for (int j = 1; j <= i - 1; j++) sum += arg3[j] * arg0[i - j];
        arg3[i] = (div > 0.0 ? -(arg0[i] + sum) / div : 0.0);
        arg2[i] = arg3[i];
        if (std::fabs(arg2[i]) > 1.0) ret++;
        for (int j = 1; j < i; j++) arg3[j] += arg3[i - j] * arg3[i];
        div *= 1.0 - arg3[i] * arg3[i];
    }
    *outSomething = div;
    return ret;
}

// estimate.c model_dist: how badly predictor arg0 fits the frame whose predictor is arg1 (the clustering's distance).
double model_dist(const double* arg0, const double* arg1, int n) {
    Vec sp3C, sp38;
    rfroma(arg1, n, sp3C);
    for (int i = 0; i <= n; i++) {
        sp38[i] = 0.0;
        for (int j = 0; j <= n - i; j++) sp38[i] += arg0[j] * arg0[i + j];
    }
    double ret = sp38[0] * sp3C[0];
    for (int i = 1; i <= n; i++) ret += 2 * sp3C[i] * sp38[i];
    return ret;
}

// The tool's reflection-coefficient clamp (used after each kfroma / durbin): strictly inside (-1, 1).
void clamp_k(double* k, int n) {
    for (int j = 1; j <= n; j++) {
        if (k[j] >= 1.0) k[j] = 0.9999999999;
        if (k[j] <= -1.0) k[j] = -0.9999999999;
    }
}

// codebook.c split: each predictor gets a twin, nudged by delta * scale.
void split(Vec* table, const double* delta, int order, int npredictors, double scale) {
    for (int i = 0; i < npredictors; i++)
        for (int j = 0; j <= order; j++) table[i + npredictors][j] = table[i][j] + delta[j] * scale;
}

// codebook.c refine: k-means -- each frame to its nearest predictor; each predictor re-made from its frames' average
// autocorrelation.
void refine(Vec* table, int order, int npredictors, const std::vector<double>& data, int dataSize, int refineIters) {
    Vec rsums[NPRED];
    int counts[NPRED];
    Vec temp_s7;
    double dummy;
    for (int iter = 0; iter < refineIters; iter++) {
        for (int i = 0; i < npredictors; i++) {
            counts[i] = 0;
            for (int j = 0; j <= order; j++) rsums[i][j] = 0.0;
        }
        for (int i = 0; i < dataSize; i++) {
            const double* row = &data[(size_t)i * (order + 1)];
            double bestValue = 1e30;
            int bestIndex = 0;
            for (int j = 0; j < npredictors; j++) {
                double dist = model_dist(table[j], row, order);
                if (dist < bestValue) {
                    bestValue = dist;
                    bestIndex = j;
                }
            }
            counts[bestIndex]++;
            rfroma(row, order, temp_s7);
            for (int j = 0; j <= order; j++) rsums[bestIndex][j] += temp_s7[j];
        }
        for (int i = 0; i < npredictors; i++)
            if (counts[i] > 0)
                for (int j = 0; j <= order; j++) rsums[i][j] /= counts[i];
        for (int i = 0; i < npredictors; i++) {
            durbin(rsums[i], order, temp_s7, table[i], &dummy);
            clamp_k(temp_s7, order);
            afromk(temp_s7, table[i], order);
        }
    }
}

// print.c print_entry: a predictor as its 8 x order table of 4.11 fixed-point coefficients -- the numbers the tool prints
// (and vadpcm_enc reads back with fscanf "%d"): out[i * 8 + j] is printed row i, column j.
void print_entry(const double* row, int order, int32_t* out) {
    double table[8][ORDER];
    for (int i = 0; i < order; i++) {
        for (int j = 0; j < i; j++) table[i][j] = 0.0;
        for (int j = i; j < order; j++) table[i][j] = -row[order - j + i];
    }
    for (int i = order; i < 8; i++)
        for (int j = 0; j < order; j++) table[i][j] = 0.0;
    for (int i = 1; i < 8; i++)
        for (int j = 1; j <= order; j++)
            if (i - j >= 0)
                for (int k = 0; k < order; k++) table[i][k] -= row[j] * table[i - j][k];
    for (int i = 0; i < order; i++)
        for (int j = 0; j < 8; j++) {
            double fval = table[j][i] * 2048.0;
            out[i * 8 + j] = fval < 0.0 ? cvt_i32(fval - 0.5) : cvt_i32(fval + 0.5);   // (overflow only warned of)
        }
}

// tabledesign.c main: the codebook for the whole sound -> per predictor, the printed numbers (order x 8).
std::vector<int32_t> table_design(const int16_t* pcm, size_t n) {
    // Each full 16-sample frame (the tool stops at the first short read: a last partial frame is not used) whose energy
    // is over the threshold gives one predictor, kept as reflection coefficients clamped inside (-1, 1).
    std::vector<double> data;            // dataSize rows of ORDER + 1
    int dataSize = 0;
    int16_t buf[FRAME * 2] = {};          // the previous frame, then this one (acvect / acmat look back ORDER samples)
    Vec vec, spF4;
    Mat mat;
    int perm[ORDER + 1], permDet;
    for (size_t at = 0; at + FRAME <= n; at += FRAME) {
        for (int i = 0; i < FRAME; i++) buf[FRAME + i] = pcm[at + i];
        acvect(buf + FRAME, ORDER, FRAME, vec);
        if (std::fabs(vec[0]) > THRESH) {
            acmat(buf + FRAME, ORDER, FRAME, mat);
            if (lu_decompose(mat, ORDER, perm, &permDet) == 0) {
                lu_solve(mat, ORDER, perm, vec);
                vec[0] = 1.0;
                if (kfroma(vec, spF4, ORDER) == 0) {
                    data.resize((size_t)(dataSize + 1) * (ORDER + 1));
                    double* row = &data[(size_t)dataSize * (ORDER + 1)];
                    row[0] = 1.0;
                    clamp_k(spF4, ORDER);
                    afromk(spF4, row, ORDER);
                    dataSize++;
                }
            }
        }
        for (int i = 0; i < FRAME; i++) buf[i] = buf[i + FRAME];
    }

    // The first predictor: the frames' average autocorrelation through Levinson-Durbin. (No frames: 0 / 0, NaN, as in
    // the tool; the refinement below turns such a predictor into zeros.)
    Vec table[NPRED];
    double dummy;
    vec[0] = 1.0;
    for (int j = 1; j <= ORDER; j++) vec[j] = 0.0;
    for (int i = 0; i < dataSize; i++) {
        rfroma(&data[(size_t)i * (ORDER + 1)], ORDER, table[0]);
        for (int j = 1; j <= ORDER; j++) vec[j] += table[0][j];
    }
    for (int j = 1; j <= ORDER; j++) vec[j] /= dataSize;
    durbin(vec, ORDER, spF4, table[0], &dummy);
    clamp_k(spF4, ORDER);
    afromk(spF4, table[0], ORDER);

    // Split and refine until there are 2^BITS predictors.
    Vec splitDelta;
    int curBits = 0;
    while (curBits < BITS) {
        for (int i = 0; i <= ORDER; i++) splitDelta[i] = 0.0;
        splitDelta[ORDER - 1] = -1.0;
        split(table, splitDelta, ORDER, 1 << curBits, 0.01);
        curBits++;
        refine(table, ORDER, 1 << curBits, data, dataSize, REFINE_ITERS);
    }

    std::vector<int32_t> printed((size_t)NPRED * ORDER * 8);
    for (int i = 0; i < NPRED; i++) print_entry(table[i], ORDER, &printed[(size_t)i * ORDER * 8]);
    return printed;
}

// =====================================================================================================================
// vadpcm_enc / vadpcm_dec (vpredictor.c, vencode.c, quant.c, vdecode.c, sampleio.c)
// =====================================================================================================================
constexpr int ROW = ORDER + 8;                  // a codebook row: ORDER coefficients, then the 8 for earlier residuals
struct Predictor {                               // coefTable[k] (the tools' s32 ***coefTable): 8 rows of ROW
    int32_t rows[8][ROW];
    int32_t* operator[](int i) { return rows[i]; }
    const int32_t* operator[](int i) const { return rows[i]; }
};

// vpredictor.c readcodebook / readaifccodebook: a predictor's order x 8 numbers -> its 8 rows; column ORDER is the
// impulse response's first tap (2048), the columns after it the response shifted down a step at a time.
void expand_predictor(const int32_t* nums, Predictor& t) {
    for (int j = 0; j < ORDER; j++)
        for (int k = 0; k < 8; k++) t[k][j] = nums[j * 8 + k];
    for (int k = 1; k < 8; k++) t[k][ORDER] = t[k - 1][ORDER - 1];
    t[0][ORDER] = 1 << 11;
    for (int k = 1; k < 8; k++) {
        int j = 0;
        for (; j < k; j++) t[j][k + ORDER] = 0;
        for (; j < 8; j++) t[j][k + ORDER] = t[j - k][ORDER];
    }
}

// vpredictor.c inner_product: a prediction, the sum / 2^11 rounded down.
int32_t inner_product(int length, const int32_t* v1, const int32_t* v2) {
    int32_t out = 0;
    for (int j = 0; j < length; j++) out = wadd(out, wmul(v1[j], v2[j]));
    int32_t dout = out / (1 << 11);
    int32_t fiout = dout * (1 << 11);
    return out - fiout < 0 ? dout - 1 : dout;
}

// quant.c qsample: x / scale to the nearest integer (halves, give or take 1e-7, toward zero). The division is single
// precision, the rounding offset double; the result the low 16 bits of the truncated int.
int16_t qsample(float x, int32_t scale) {
    if (x > 0.0f) return low16(cvt_i32((double)(x / (float)scale) + 0.4999999));
    return low16(cvt_i32((double)(x / (float)scale) - 0.4999999));
}

// quant.c clamp: the residuals to 16-bit integers (clamped, then rounded half away from zero).
void clamp16(float* e, int32_t* ie) {
    const float llevel = -(float)(1 << 15);
    const float ulevel = -llevel - 1.0f;
    for (int i = 0; i < 16; i++) {
        if (e[i] > ulevel) e[i] = ulevel;
        if (e[i] < llevel) e[i] = llevel;
        ie[i] = e[i] > 0.0f ? cvt_i32((double)e[i] + 0.5) : cvt_i32((double)e[i] - 0.5);
    }
}

// quant.c clip
int32_t clip(int32_t ix, int32_t llevel, int32_t ulevel) {
    if (ix < llevel) return llevel;
    if (ix > ulevel) return ulevel;
    return ix;
}

// The 16 residuals of a frame for predictor p, from the state (the previous decoded frame) -- vencodeframe's search and
// its "same thing again, for real" pass. inVector ends holding the second half's values.
void residuals(const int16_t* in, const int32_t* state, const Predictor& p, int32_t* prediction, int32_t* inVector,
               float* e) {
    for (int i = 0; i < ORDER; i++) inVector[i] = state[16 - ORDER + i];
    for (int i = 0; i < 8; i++) {
        prediction[i] = inner_product(ORDER + i, p[i], inVector);
        inVector[i + ORDER] = wsub(in[i], prediction[i]);
        e[i] = (float)inVector[i + ORDER];
    }
    for (int i = 0; i < ORDER; i++) inVector[i] = wadd(prediction[8 - ORDER + i], inVector[8 + i]);
    for (int i = 0; i < 8; i++) {
        prediction[8 + i] = inner_product(ORDER + i, p[i], inVector);
        inVector[i + ORDER] = wsub(in[8 + i], prediction[8 + i]);
        e[8 + i] = (float)inVector[i + ORDER];
    }
}

// vencode.c vencodeframe: one frame (nsam samples, zero-padded to 16) -> 9 bytes; 'state' becomes its decoded samples.
void encode_frame(const int16_t* samples, int nsam, int32_t* state, const Predictor* coefTable, int npredictors,
                  uint8_t* out) {
    int16_t inBuffer[16];
    for (int i = 0; i < 16; i++) inBuffer[i] = i < nsam ? samples[i] : 0;
    int16_t ix[16];
    int32_t prediction[16], inVector[16], saveState[16], ie[16];
    float e[16];
    const int32_t llevel = -8, ulevel = -llevel - 1;

    // The predictor whose residuals have the least sum of squares (single-precision sum; the first of equals).
    float min = (float)1e30;
    int optimalp = 0;
    for (int k = 0; k < npredictors; k++) {
        residuals(inBuffer, state, coefTable[k], prediction, inVector, e);
        float se = 0.0f;
        for (int j = 0; j < 16; j++) se += e[j] * e[j];
        if (se < min) {
            min = se;
            optimalp = k;
        }
    }
    residuals(inBuffer, state, coefTable[optimalp], prediction, inVector, e);
    clamp16(e, ie);

    // The scale: halve the largest residual (the first of the largest magnitude, as the tool finds it) until it fits in
    // 4 bits.
    int32_t max = 0;
    for (int i = 0; i < 16; i++)
        if (std::fabs((double)ie[i]) > std::fabs((double)max)) max = ie[i];
    int scale;
    for (scale = 0; scale <= 12; scale++) {
        if (max <= ulevel && max >= llevel) break;
        max /= 2;
    }

    // Quantise, decoding as the player will (each prediction from the quantised past); if a residual had to be clipped
    // by 2 or more, once more with the next scale.
    for (int i = 0; i < 16; i++) saveState[i] = state[i];
    scale--;
    int nIter = 0, maxClip;
    do {
        nIter++;
        maxClip = 0;
        scale++;
        if (scale > 12) scale = 12;
        for (int i = 0; i < ORDER; i++) inVector[i] = saveState[16 - ORDER + i];
        for (int h = 0; h < 2; h++) {            // the frame's two halves of 8
            if (h == 1)
                for (int i = 0; i < ORDER; i++) inVector[i] = state[8 - ORDER + i];
            for (int i = 0; i < 8; i++) {
                int s = h * 8 + i;
                prediction[s] = inner_product(ORDER + i, coefTable[optimalp][i], inVector);
                float se = (float)inBuffer[s] - (float)prediction[s];
                ix[s] = qsample(se, 1 << scale);
                int32_t cV = (int16_t)clip(ix[s], llevel, ulevel) - ix[s];
                if (maxClip < std::abs(cV)) maxClip = std::abs(cV);
                ix[s] = (int16_t)(ix[s] + cV);
                inVector[i + ORDER] = wmul(ix[s], 1 << scale);
                state[s] = wadd(prediction[s], inVector[i + ORDER]);
            }
        }
    } while (maxClip >= 2 && nIter < 2);

    out[0] = (uint8_t)((scale << 4) | (optimalp & 0xf));
    for (int i = 0; i < 16; i += 2) out[1 + i / 2] = (uint8_t)((ix[i] << 4) | (ix[i + 1] & 0xf));
}

// vdecode.c vdecodeframe: 9 bytes -> 16 samples into outp (which holds the previous frame's: the predictor's history).
void decode_frame(const uint8_t* f, int32_t* outp, const Predictor* coefTable) {
    const int32_t maxlevel = 7;
    int32_t scale = 1 << (f[0] >> 4);
    int optimalp = f[0] & 0xf;
    int32_t ix[16], in_vec[16];
    for (int i = 0; i < 16; i += 2) {
        uint8_t c = f[1 + i / 2];
        ix[i] = c >> 4;
        ix[i + 1] = c & 0xf;
        ix[i] = ix[i] <= maxlevel ? ix[i] * scale : (-0x10 - -ix[i]) * scale;
        ix[i + 1] = ix[i + 1] <= maxlevel ? ix[i + 1] * scale : (-0x10 - -ix[i + 1]) * scale;
    }
    for (int j = 0; j < 2; j++) {
        for (int i = 0; i < 8; i++) in_vec[i + ORDER] = ix[j * 8 + i];
        for (int i = 0; i < ORDER; i++) in_vec[i] = j == 0 ? outp[16 - ORDER + i] : outp[j * 8 - ORDER + i];
        for (int i = 0; i < 8; i++) outp[i + j * 8] = inner_product(ORDER + 8, coefTable[optimalp][i], in_vec);
    }
}

}  // namespace

// vadpcm.encode: tabledesign's codebook, then vadpcm_enc with it; the book and the SSND data as the .aifc holds them.
VadpcmOut vadpcm_encode(const int16_t* pcm, size_t n, int rate) {
    (void)rate;
    if (n > 0x7FFFFFFF) fail("vadpcm: %zu samples is more than the tools take", n);
    std::vector<int32_t> printed = table_design(pcm, n);

    // The book (the APPL "VADPCMCODES" chunk): the printed numbers as s16 (their low 16 bits, as vadpcm_enc writes them
    // -- while it encodes with the full ints it read).
    VadpcmOut o;
    o.book.resize(8 + printed.size() * 2);
    put_be32(&o.book[0], ORDER);
    put_be32(&o.book[4], NPRED);
    for (size_t i = 0; i < printed.size(); i++) put_be16(&o.book[8 + 2 * i], (uint16_t)low16(printed[i]));

    Predictor coefTable[NPRED];
    for (int k = 0; k < NPRED; k++) expand_predictor(&printed[(size_t)k * ORDER * 8], coefTable[k]);

    // vadpcm_enc main: frames of 16 (the last one short, zero-padded) from a zero state; an odd byte count gets a zero.
    size_t frames = (n + 15) / 16;
    o.data.assign(frames * 9 + (frames & 1), 0);
    int32_t state[16] = {};
    for (size_t f = 0; f < frames; f++) {
        size_t at = f * 16;
        int nsam = n - at < 16 ? (int)(n - at) : 16;
        encode_frame(pcm + at, nsam, state, coefTable, NPRED, &o.data[f * 9]);
    }
    o.samples = (uint32_t)n;
    return o;
}

// vadpcm.decode: vadpcm_dec on the frames with the book -> whole frames of samples, clamped as sampleio.c writes them
// (to -0x7FFF..0x7FFF), the first n kept.
std::vector<int16_t> vadpcm_decode(const Bytes& data, const Bytes& book, size_t n) {
    if (book.size() < 8) fail("vadpcm: a book of %zu bytes", book.size());
    int32_t order = s32(be32(book, 0)), npred = s32(be32(book, 4));
    if (order != ORDER || npred < 1 || npred > 16 || book.size() < 8 + (size_t)npred * ORDER * 8 * 2)
        fail("vadpcm: a book of order %d with %d predictors (this decoder: order %d)", order, npred, ORDER);
    std::vector<Predictor> coefTable((size_t)npred);
    for (int k = 0; k < npred; k++) {
        int32_t nums[ORDER * 8];
        for (int i = 0; i < ORDER * 8; i++) nums[i] = sx16(be16(book, 8 + ((size_t)k * ORDER * 8 + i) * 2));
        expand_predictor(nums, coefTable[k]);
    }
    size_t frames = (n + 15) / 16;
    if (data.size() < frames * 9) fail("vadpcm: %zu samples need %zu bytes of frames, not %zu", n, frames * 9, data.size());
    std::vector<int16_t> pcm(frames * 16);
    int32_t outp[16] = {};
    for (size_t f = 0; f < frames; f++) {
        const uint8_t* fr = &data[f * 9];
        if ((fr[0] & 0xf) >= npred) fail("vadpcm: frame %zu names predictor %d of %d", f, fr[0] & 0xf, npred);
        decode_frame(fr, outp, coefTable.data());
        for (int i = 0; i < 16; i++) {
            int32_t c = outp[i];
            if (c < -0x7fff) c = -0x7fff;
            if (c >= 0x8000) c = 0x7fff;
            pcm[f * 16 + i] = (int16_t)c;
        }
    }
    pcm.resize(n);
    return pcm;
}

}  // namespace ri

#ifdef VADPCM_MAIN
// The test driver for tools\romimport\native\vadpcm_check.py (built with ri_core.cpp):
//   vadpcm enc <pcm, s16 little-endian> <out.book> <out.data>      prints "samples N ms T" (T: the encode's time)
//   vadpcm dec <data> <book> <n> <out pcm, s16 little-endian>
#include <chrono>
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    try {
        std::string cmd = argc > 1 ? argv[1] : "";
        if (cmd == "enc" && argc == 5) {
            ri::Bytes in = ri::read_file(argv[2]);
            std::vector<int16_t> pcm(in.size() / 2);
            for (size_t i = 0; i < pcm.size(); i++) pcm[i] = (int16_t)(in[2 * i] | in[2 * i + 1] << 8);
            auto t0 = std::chrono::steady_clock::now();
            ri::VadpcmOut o = ri::vadpcm_encode(pcm.data(), pcm.size(), 22050);
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            ri::write_file(argv[3], o.book);
            ri::write_file(argv[4], o.data);
            std::printf("samples %u ms %.3f\n", o.samples, ms);
            return 0;
        }
        if (cmd == "dec" && argc == 6) {
            std::vector<int16_t> pcm = ri::vadpcm_decode(ri::read_file(argv[2]), ri::read_file(argv[3]),
                                                         (size_t)std::strtoull(argv[4], nullptr, 10));
            ri::Bytes out(pcm.size() * 2);
            for (size_t i = 0; i < pcm.size(); i++) {
                out[2 * i] = (uint8_t)pcm[i];
                out[2 * i + 1] = (uint8_t)((uint16_t)pcm[i] >> 8);
            }
            ri::write_file(argv[5], out);
            return 0;
        }
        std::fprintf(stderr, "vadpcm enc <pcm> <out.book> <out.data> | vadpcm dec <data> <book> <n> <out.pcm>\n");
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "vadpcm: %s\n", e.what());
        return 1;
    }
}
#endif
