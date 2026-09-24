#pragma once
// =============================================================================
// Fft.h — StackChan-Companion (engine)
// =============================================================================
// A radix-2 FFT and the band grouping the status band's spectrum needs.
//
// PURE, so it is tested natively (`test_fft`) like the rest of `engine/`: no
// Arduino, no M5, no float-to-pixel. A spectrum that is wrong is wrong in a way
// nobody can see by looking at a moving bargraph — every bar still moves — so
// it has to be pinned against signals whose answer is known before the code
// runs (a sine lands in one bin, DC lands in bin 0, silence is silent).
//
// WHY NOT esp-dsp. The ESP-IDF ships `dsps_fft2r_fc32`, it is already on this
// build's link line, and on an ESP32-S3 it takes the vector-assembly path — so
// it is several times faster than what follows. It is also code that ONLY THE
// TARGET CAN RUN: none of it can be exercised by the native suite, and a
// spectrum is precisely the kind of output whose wrongness is invisible on a
// moving bargraph. A few hundred microseconds of a 240 MHz core, thirty times
// a second, buys the ability to pin this against known signals on a PC. That
// is the trade, and it is the same one `sgp4.h` and `astro.h` already make.
//
// SIZE. 512 real samples per block is what the microphone already delivers
// (32 ms at 16 kHz), which fixes the resolution at 31.25 Hz per bin and the
// span at 0–8 kHz. Both are right for a visualiser: finer would resolve
// nothing the eye can use, coarser would merge the bass into one bar.
// =============================================================================

#include <math.h>
#include <stddef.h>
#include <stdint.h>

namespace sce {
namespace dsp {

// ---- the transform ---------------------------------------------------------
// In-place, decimation in time, `n` a POWER OF TWO. Anything else is refused
// rather than approximated: a half-done butterfly pass produces a spectrum
// that looks plausible and is meaningless.
inline bool fft(float* re, float* im, int n) {
    if (!re || !im || n < 2 || (n & (n - 1)) != 0) return false;

    // Bit-reversal permutation.
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            const float tr = re[i]; re[i] = re[j]; re[j] = tr;
            const float ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }
    // Butterflies. The twiddle is advanced by recurrence inside a stage rather
    // than recomputed: a cosf/sinf pair per butterfly is most of the cost of a
    // transform this size.
    for (int len = 2; len <= n; len <<= 1) {
        const float ang = -6.2831853071795864769f / (float)len;
        const float wr = cosf(ang), wi = sinf(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1.0f, ci = 0.0f;
            for (int k = 0; k < len / 2; k++) {
                const int a = i + k, b = i + k + len / 2;
                const float xr = re[b] * cr - im[b] * ci;
                const float xi = re[b] * ci + im[b] * cr;
                re[b] = re[a] - xr; im[b] = im[a] - xi;
                re[a] += xr;        im[a] += xi;
                const float nr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = nr;
            }
        }
    }
    return true;
}

// ---- the window ------------------------------------------------------------
// Hann. Without one, a tone that does not land exactly on a bin smears across
// the whole spectrum (spectral leakage) and every band lights up at once —
// which on a bargraph reads as "loud everywhere" rather than as a wrong
// analysis.
inline float hann(int i, int n) {
    return 0.5f * (1.0f - cosf(6.2831853071795864769f * (float)i / (float)(n - 1)));
}

// ---- real magnitude spectrum ----------------------------------------------
// `x` holds `n` real samples. `mag` receives n/2 magnitudes, bin k centred on
// k * rate / n. `im` is scratch of n floats, supplied by the caller so this
// allocates nothing.
//
// THE MEAN IS REMOVED BEFORE WINDOWING, and the order is the whole point. A
// microphone sits on a DC offset that dwarfs the signal, and zeroing bin 0
// afterwards does NOT get rid of it: the window has a spectrum of its own, so a
// constant multiplied by a Hann window lands in bins 0, 1 AND 2. Subtract
// first and those bins are empty; subtract after, or not at all, and the two
// lowest bars sit at the ceiling for ever whatever the room is doing.
inline bool magnitudes(float* x, float* im, float* mag, int n, bool window = true) {
    if (!x || !im || !mag || n < 4 || (n & (n - 1)) != 0) return false;
    float mean = 0.0f;
    for (int i = 0; i < n; i++) mean += x[i];
    mean /= (float)n;
    for (int i = 0; i < n; i++) x[i] -= mean;
    if (window) for (int i = 0; i < n; i++) x[i] *= hann(i, n);
    for (int i = 0; i < n; i++) im[i] = 0.0f;
    if (!fft(x, im, n)) return false;
    // 2/n for the two-sided spectrum, divided by the window's COHERENT GAIN
    // (0.5 for Hann). Without that second factor a full-scale tone reads 6 dB
    // low, and every threshold expressed in dBFS is quietly wrong.
    const float norm = (2.0f / (float)n) / (window ? 0.5f : 1.0f);
    for (int k = 0; k < n / 2; k++)
        mag[k] = sqrtf(x[k] * x[k] + im[k] * im[k]) * norm;
    mag[0] = 0.0f;                      // DC: the microphone's own offset
    return true;
}

// ---- TWO REAL SIGNALS, ONE TRANSFORM ---------------------------------------
// The band shows both microphones, so it needs two spectra — and a second FFT
// is a second FFT. Packing the left channel into the real part and the right
// into the imaginary one gets both out of a SINGLE transform, because the
// spectrum of a real signal is conjugate-symmetric and the two therefore
// separate cleanly afterwards:
//
//     L[k] = ( Z[k] + conj(Z[n-k]) ) / 2
//     R[k] = ( Z[k] - conj(Z[n-k]) ) / 2i
//
// Exact, not an approximation — and it halves the cost of the one part of this
// pipeline that is not free.
//
// `l` and `r` hold n real samples each and are DESTROYED. `magL`/`magR`
// receive n/2 magnitudes.
inline bool magnitudes2(float* l, float* r, float* magL, float* magR, int n,
                        bool window = true) {
    if (!l || !r || !magL || !magR || n < 4 || (n & (n - 1)) != 0) return false;
    float ml = 0.0f, mr = 0.0f;              // the mean FIRST, see above
    for (int i = 0; i < n; i++) { ml += l[i]; mr += r[i]; }
    ml /= (float)n; mr /= (float)n;
    for (int i = 0; i < n; i++) { l[i] -= ml; r[i] -= mr; }
    if (window)
        for (int i = 0; i < n; i++) { const float w = hann(i, n); l[i] *= w; r[i] *= w; }
    if (!fft(l, r, n)) return false;                 // l = Re(Z), r = Im(Z)
    const float norm = (2.0f / (float)n) / (window ? 0.5f : 1.0f);
    for (int k = 0; k < n / 2; k++) {
        const int m = (k == 0) ? 0 : n - k;
        const float lr = 0.5f * (l[k] + l[m]);       // Re L
        const float li = 0.5f * (r[k] - r[m]);       // Im L
        const float rr = 0.5f * (r[k] + r[m]);       // Re R
        const float ri = 0.5f * (l[m] - l[k]);       // Im R
        magL[k] = sqrtf(lr * lr + li * li) * norm;
        magR[k] = sqrtf(rr * rr + ri * ri) * norm;
    }
    magL[0] = magR[0] = 0.0f;           // DC: the microphones' own offset
    return true;
}

// ---- band grouping ---------------------------------------------------------
// LOGARITHMIC edges, because hearing is. Linear bands spend three quarters of
// the display on 4–8 kHz, where music and speech have almost nothing, and
// squeeze every vowel and every bass note into the first two bars.
//
// `edge` receives `bands + 1` bin indices: band b covers [edge[b], edge[b+1]).
// Edges are forced to be strictly increasing, so a low band can never end up
// empty and read as permanent silence.
inline void bandEdges(int* edge, int bands, int n, float rate,
                      float fLo = 60.0f, float fHi = 7000.0f) {
    if (!edge || bands < 1) return;
    const int half = n / 2;
    const float lr = logf(fHi / fLo);
    for (int b = 0; b <= bands; b++) {
        const float f = fLo * expf(lr * (float)b / (float)bands);
        int k = (int)(f * (float)n / rate + 0.5f);
        if (k < 1) k = 1;
        if (k > half) k = half;
        edge[b] = k;
    }
    for (int b = 1; b <= bands; b++)
        if (edge[b] <= edge[b - 1]) edge[b] = edge[b - 1] + 1;
    // The forward pass above can push the top edges past the last bin, so they
    // are clamped — and the clamp can flatten two neighbours back onto the same
    // index, which is the very emptiness the forward pass existed to prevent.
    // A band whose edges are equal reads as PERMANENT SILENCE and looks exactly
    // like a quiet room, so the monotonicity is re-established DOWNWARD, from
    // the ceiling back. Floor at bin 1 (never DC): if `bands` is so large that
    // the floor is reached, the caller asked for more bands than there are
    // bins, and no arrangement can give each one a bin of its own.
    if (edge[bands] > half) edge[bands] = half;
    for (int b = bands - 1; b >= 0; b--) {
        if (edge[b] >= edge[b + 1]) edge[b] = edge[b + 1] - 1;
        if (edge[b] < 1) edge[b] = 1;
    }
}

// The value of one band: the PEAK of its bins, not their mean. A band that
// averages is a band that hides a tone among its neighbours, and a spectrum
// display exists precisely to show the tone.
inline float bandValue(const float* mag, const int* edge, int b) {
    float m = 0.0f;
    for (int k = edge[b]; k < edge[b + 1]; k++) if (mag[k] > m) m = mag[k];
    return m;
}

// ---- what the eye sees -----------------------------------------------------
// Amplitude to a 0..1 height, on a DECIBEL scale over `range` dB below full
// scale. Linear height is unusable: normal speech sits in the bottom tenth of
// the scale and the bars barely leave the floor.
inline float dbNorm(float amp, float full = 32768.0f, float range = 48.0f) {
    if (amp <= 0.0f) return 0.0f;
    const float db = 20.0f * log10f(amp / full);      // <= 0
    const float v = 1.0f + db / range;                // 0 at -range dB
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

} // namespace dsp
} // namespace sce
