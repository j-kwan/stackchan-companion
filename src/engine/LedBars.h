#pragma once
// =============================================================================
// LedBars.h — the DEPTH distribution of a bar's light (ROADMAP §10)
// =============================================================================
// The twelve WS2812 are two bars of six, and the bars run PERPENDICULAR to the
// display: their LEDs are staggered front to back. The firmware has always
// written all six of a bar to one value, so a bar carried exactly one number.
// This adds a second dimension without spending the first: the brightness
// `EmotionLeds` already computes stays the bar's AMPLITUDE, and what is decided
// here is only how that amplitude is DISTRIBUTED along the depth.
//
// THE DEGENERATE CASE IS THE EXISTING BEHAVIOUR, and that is the acceptance
// criterion rather than a hope: at rest every weight is exactly 1.0f, so
// amplitude × weight is the amplitude, and the payload is byte-identical to the
// one the bars receive today. `test_ledbars` asserts it with an exact
// comparison, because "approximately identical" would let a rounding change
// slip in under the claim that nothing was lost.
//
// THE REDISTRIBUTION SUBTRACTS, IT NEVER ADDS — and that is forced, not a
// taste. `EmotionLeds` clamps its brightness at 255 and a bar can legitimately
// sit there. Massing light toward the near end at constant total would need
// those LEDs to exceed the bar's own brightness, so at the ceiling they would
// clip and the total would quietly collapse — in the very case the effect
// matters most, since brightness peaks when the emotion is intense. A weight
// above one is therefore forbidden: a turn DARKENS THE FAR END rather than
// brightening the near one. The bar loses light while it turns, which is honest
// (nothing is invented) and legible (the eye reads the dark end moving), and the
// brightness ceiling keeps meaning exactly what it means today.
//
// COMMANDED motion, never the gyro. The rate handed in is what the firmware
// ASKED the servos for. The gyro also fires when a human turns the robot by
// hand, and the bars would then indicate a turn the robot never made — an
// indicator that lies about who is driving.
//
// PURE: no Arduino, no M5. Tested natively (test/test_ledbars) like the rest of
// engine/ (rule 7).
// =============================================================================

#include <stdint.h>

namespace sce {
namespace ledbars {

inline constexpr int PER_BAR = 6;

// The commanded yaw rate that spends the whole effect, in °/s. PROVISIONAL: the
// curve from rate to displacement is a looks-right decision and §10 says so, so
// this is the number the hardware session is expected to move. It is one
// constant in one place precisely so that moving it is a one-line argument.
inline constexpr float FULL_RATE_DEG_S = 120.0f;

inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// How far the light has moved along the bar: 0 at rest, ±1 when the effect is
// spent, and the SIGN says which end darkens.
//
// SIGNED, and that is not decoration. `EmotionLeds` quantises this to decide
// whether the picture CHANGED, and its write-suppression watches nothing else
// about the distribution — so an unsigned magnitude makes a left turn and a
// right turn of the same speed compare equal, and the bar keeps whichever one
// it drew first until the colour or the eye height happens to move. Reporting
// only the magnitude is the same class of bug as reporting nothing at all: it
// is a value that cannot distinguish the two pictures it exists to distinguish.
inline float displacement(float rateDegS, float gainPct) {
    if (gainPct <= 0.0f || FULL_RATE_DEG_S <= 0.0f) return 0.0f;
    const float mag = rateDegS < 0.0f ? -rateDegS : rateDegS;
    const float d = clamp01(mag * (gainPct / 100.0f) / FULL_RATE_DEG_S);
    return rateDegS < 0.0f ? -d : d;
}

// rateDegS : commanded yaw rate, signed. Its SIGN chooses which end darkens.
// gainPct  : 0 disables the effect exactly (every weight 1.0f).
// frontIsZero : does index 0 of a bar sit at the FRONT? Nobody has measured it
//               yet — §10 lists it as the open question, and it exists here for
//               the same reason `led_swap` exists for the left/right wiring:
//               without it, light massing at the wrong end is indistinguishable
//               from arithmetic that is simply wrong.
// out[PER_BAR] : per-LED weights, each in [0, 1], to MULTIPLY the bar's
//                brightness by. Never above one — see the header.
inline void weights(float rateDegS, float gainPct, bool frontIsZero,
                    float out[PER_BAR]) {
    const float signed_ = displacement(rateDegS, gainPct);
    const float d = signed_ < 0.0f ? -signed_ : signed_;

    // Which physical end keeps its light: a turn masses the light toward the
    // FRONT, so the front end is the one that stays lit and the back darkens.
    // Reverse that for a negative rate, and reverse it again if index 0 is at
    // the back rather than the front — the two flips compose.
    const bool nearIsIndexZero = (signed_ >= 0.0f) == frontIsZero;

    for (int i = 0; i < PER_BAR; i++) {
        // Normalised distance from the end that keeps its light: 0 there, 1 at
        // the far end. Written from a division by PER_BAR - 1 so the two ends
        // land exactly on 0 and 1 — the rest-case identity depends on the
        // arithmetic being exact at d = 0, and it is, because d multiplies it.
        const float t = (float)i / (float)(PER_BAR - 1);
        const float u = nearIsIndexZero ? t : 1.0f - t;
        out[i] = 1.0f - d * u;
    }
}

} // namespace ledbars
} // namespace sce
