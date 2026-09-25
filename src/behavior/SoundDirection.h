#pragma once
// =============================================================================
// SoundDirection.h — StackChan-Companion (behavior)
// =============================================================================
// Left/right direction of a noise from BOTH microphones (CoreS3: ES7210 +
// 2 MEMS, interleaved stereo capture). Principle requested by the user
// (2026-07-16): compare the LEVEL of the two channels — the head turns toward
// the louder side until the levels balance out.
//
//   - per-channel RMS on every block (~16 ms)
//   - gate: level > threshold (tuning.soundtrack_thr) AND > 2× the ambient
//     floor (slow EMA: a constant background — a fan, background music —
//     does NOT trigger; only sharp noises emerge)
//   - imbalance in [-1, +1], smoothed: -1 = fully left, +1 = fully right
//     (in terms of the CAPTURED CHANNELS — mapping to the robot's PHYSICAL
//     left/right goes through tuning.soundtrack_sign, validated on hardware
//     like the gyro_* settings)
//
// PURITY: no Arduino dependency — testable natively on synthetic buffers.
// =============================================================================

#include <cstdint>
#include <cstddef>
#include <cmath>

namespace sce {

class SoundDirection {
public:
    struct Result {
        bool  event     = false;   // sharp noise detected on this block
        float imbalance = 0.0f;    // -1 (channel L) .. +1 (channel R), smoothed
        float level     = 0.0f;    // mean RMS of the block
    };

    // `lr`: INTERLEAVED stereo frames (L,R,L,R...), `frames` = number of pairs.
    // `thr`: absolute RMS threshold (tuning.soundtrack_thr, int16 scale).
    Result feed(const int16_t* lr, size_t frames, float thr) {
        Result r;
        if (!lr || frames == 0) return r;

        // CHANNEL MAPPING VALIDATED ON HARDWARE (user 2026-07-17): on the
        // K151, the FIRST interleaved ES7210 sample is the RIGHT microphone
        // (viewer-centric), the second one the LEFT. The original labelling
        // was swapped — that is what had forced soundtrack_sign=-1 as
        // compensation. ⚠ This mapping fix PREDICTED sign=+1, but the robot
        // said otherwise: -1 is the HW-verified value (user 2026-07-30, v4
        // migration) — one link of the channel→yaw chain is still inverted
        // somewhere. Full post-mortem in Tuning.h at soundtrack_sign.
        float s1 = 0.0f, s2 = 0.0f;
        for (size_t i = 0; i < frames; i++) {
            float a = (float)lr[2 * i];        // channel 1 = RIGHT mic
            float b = (float)lr[2 * i + 1];    // channel 2 = LEFT mic
            s1 += a * a;
            s2 += b * b;
        }
        float rmsR = sqrtf(s1 / (float)frames);
        float rmsL = sqrtf(s2 / (float)frames);
        _rmsL = rmsL;
        _rmsR = rmsR;
        r.level = 0.5f * (rmsL + rmsR);

        // Gate: absolute threshold AND emergence over the ambient floor. The
        // ambient tracks silence FAST but noise SLOWLY — a continuous noise
        // eventually gets absorbed, a sharp one stands out. Coefficients ×2 on
        // 2026-07-17 (review): blocks went from 16 to 32 ms, so feed() runs
        // half as often — 0.10/0.004 per block restore the ORIGINAL REAL time
        // constants.
        bool loud = r.level > thr && r.level > ambient() * 2.0f;
        float a = loud ? 0.004f : 0.10f;
        _ambL += a * (rmsL - _ambL);
        _ambR += a * (rmsR - _ambR);

        if (loud) {
            // Direction computed on levels NORMALIZED by the PER-CHANNEL
            // ambient: this cancels the gain bias between the two MEMS
            // (measured on hardware 2026-07-16: L ≈ 2.4 × R at rest — the raw
            // imbalance was structurally pulled to the left).
            float nl  = rmsL / (_ambL > 1.0f ? _ambL : 1.0f);
            float nr  = rmsR / (_ambR > 1.0f ? _ambR : 1.0f);
            float imb = (nr - nl) / (nl + nr + 1e-3f);
            _imb += 0.7f * (imb - _imb);   // snappy smoothing: ~2 blocks (reactivity)
            r.event = true;
        } else {
            _imb *= 0.90f;                 // gentle return to 0 during silence
        }
        r.imbalance = _imb < -1.0f ? -1.0f : (_imb > 1.0f ? 1.0f : _imb);
        return r;
    }

    float ambient() const { return 0.5f * (_ambL + _ambR); }
    // Last per-channel RMS (console telemetry — valid after a feed())
    float rmsL() const { return _rmsL; }
    float rmsR() const { return _rmsR; }

private:
    float _ambL = 200.0f;      // PER-CHANNEL ambient floor (gain bias)
    float _ambR = 200.0f;
    float _imb  = 0.0f;        // smoothed imbalance
    float _rmsL = 0.0f;        // last per-channel levels (console)
    float _rmsR = 0.0f;
};

} // namespace sce
