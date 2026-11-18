#pragma once
// =============================================================================
// Rng.h — StackChan-Companion (engine)
// =============================================================================
// Minimal pseudo-random generator (xorshift32), SEEDABLE and deterministic.
//
// Why not Arduino's random(): the behavioural state machines (IdleBehavior,
// BlinkController, EmotionRoulette) must be testable natively in a
// REPRODUCIBLE way — a test that fails one run in twenty is worthless. The
// Rng is INJECTED (the Brain owns ONE and lends it by reference to its state
// machines — they take `Rng&`); the tests fix the seed, production seeds it
// with esp_random() at boot (firmware/companion/main.cpp).
//
// PURITY: no Arduino dependency.
// =============================================================================

#include <cstdint>

namespace sce {

class Rng {
public:
    explicit Rng(uint32_t seed = 0xC0FFEE42) : _s(seed ? seed : 1) {}

    void seed(uint32_t s) { _s = s ? s : 1; }

    // xorshift32 — period 2^32-1, far more than behaviour needs
    uint32_t next() {
        uint32_t x = _s;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return _s = x;
    }

    // Uniform integer in [lo, hi] (bounds INCLUDED — avoids the classic
    // off-by-one of the exclusive random(min, max))
    uint32_t range(uint32_t lo, uint32_t hi) {
        if (hi <= lo) return lo;
        return lo + next() % (hi - lo + 1);
    }

    // Uniform float in [lo, hi]
    float rangef(float lo, float hi) {
        return lo + (hi - lo) * (next() / 4294967296.0f);
    }

    // True with probability p (0..1)
    bool chance(float p) { return rangef(0.0f, 1.0f) < p; }

private:
    uint32_t _s;
};

} // namespace sce
