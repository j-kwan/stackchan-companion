#pragma once
// =============================================================================
// EmotionRoulette.h — StackChan-Companion (behavior)
// =============================================================================
// Weighted emotion roulette (ported from esp32-eyes FaceBehavior — draw
// logic under AGPL-3.0, Aitchison/Llamas).
//
// PURE module (Clock + Rng injected), with no task of its own — the Brain
// ticks it. The lock is a plain arbitration boolean passed in by the Brain:
// the roulette consumes its tick even while locked (the 6-12 s cycle never
// drifts).
//
// Every emotion carries its default TransitionConfig (validated table).
// =============================================================================

#include <cstdint>
#include "../engine/Clock.h"
#include "../engine/Rng.h"
#include "../engine/Emotions.h"
#include "../engine/Transitions.h"

namespace sce {

class EmotionRoulette {
public:
    EmotionRoulette(const Clock& clock, Rng& rng)
        : _clock(clock), _rng(rng) {
        resetWeights();
        scheduleNext(_clock.ms());
    }

    void setWeight(eEmotions e, float w) {
        if (e < EMOTIONS_COUNT) _weights[e] = w < 0.0f ? 0.0f : w;
    }
    void setInterval(uint32_t minMs, uint32_t maxMs) {
        _minMs = minMs; _maxMs = maxMs > minMs ? maxMs : minMs;
    }

    // ---- The default table: ONE home ------------------------------------
    // The constructor and `resetWeights()` both come here rather than each
    // holding a copy. The old comment claimed these were "overridden by the
    // companion" — the companion never did, and nothing else will now: a
    // personality that wants this table asks for it by NOT supplying one
    // (Personalities.h, `weights == nullptr`), so the numbers stay in a single
    // place instead of being restated as a "default personality".
    void resetWeights() {
        clearWeights();
        _weights[Normal]      = 0.8f;
        _weights[Happy]       = 0.4f;
        _weights[Focused]     = 0.3f;
        _weights[Glee]        = 0.2f;
        _weights[Worried]     = 0.10f;
        _weights[Sleepy]      = 0.10f;
        _weights[Sad]         = 0.08f;
        _weights[Surprised]   = 0.08f;
        _weights[Angry]       = 0.06f;
        _weights[Annoyed]     = 0.06f;
        _weights[Curious]     = 0.06f;
        _weights[Excited]     = 0.04f;
        _weights[Questioning] = 0.04f;
        _weights[Awe]         = 0.03f;
        _weights[Skeptic]     = 0.03f;
        _weights[Furious]     = 0.02f;
        _weights[Blush]       = 0.04f;   // occasional fond embarrassment
    }
    void clearWeights() {
        for (int i = 0; i < EMOTIONS_COUNT; i++) _weights[i] = 0.0f;
    }

    // ---- The roulette can be SWITCHED OFF (personality selector) ---------
    // Off, the face stops changing on its own — which is the point: a
    // character's expression has to MEAN something, and it cannot while a draw
    // every 6-12 s overwrites whatever a rule just said with noise.
    //
    // Off is not "frozen", and the distinction is load-bearing. The Brain
    // restores `_preOverride` when a timed emotion expires and SetEmotion
    // carries a default duration, so the face still returns to rest by itself;
    // saccades, blinks, breathing and the VOR are driven elsewhere and keep
    // running. What stops is only the RANDOM CHOOSING.
    void setEnabled(bool on) { _enabled = on; }
    bool enabled() const { return _enabled; }

    // NIGHT mode (light sensor, dark_sleepy option): in the dark, Sleepy
    // TAKES THE PLACE of Normal in the draw (Normal weight → 0) and gets a
    // dominant weight (~66 % of draws) — the robot dozes, with the odd other
    // emotion (still alive), never an awake Normal. Reflexes/dances/API keep
    // priority (the roulette is already subordinate to overrides — no
    // overwrite fight).
    void setDarkMode(bool on) { _dark = on; }
    bool darkMode() const { return _dark; }

    // Default transition of an emotion (STRONG for intense ones, SOFT for
    // slow ones, CALM for the happy ones)
    static TransitionConfig transitionFor(eEmotions e) {
        switch (e) {
            case Furious: case Angry: case Surprised: case Scared:
            case Awe: case Excited: case Dead:
            case Nervous:  // SHARP entry (nervous squint, user 07-12)
                return DefaultTransitions::STRONG;
            case Sleepy: case Sad: case Frozen:
                return DefaultTransitions::SOFT;
            case Happy: case Glee: case Smug: case Blush:
                return DefaultTransitions::CALM;
            default:
                return DefaultTransitions::NORMAL;
        }
    }

    // ------------------------------------------------------------------
    // Tick. `locked`: an override (API/dance) holds the floor — the tick is
    // CONSUMED anyway (stable cadence) but nothing is drawn.
    // Returns true when `out` holds a new emotion (≠ current).
    // ------------------------------------------------------------------
    bool update(eEmotions current, bool locked, eEmotions& out) {
        uint32_t now = _clock.ms();
        if (now < _nextAt) return false;
        // THE TICK IS CONSUMED BEFORE EITHER TEST, and that is deliberate for
        // both of them. A roulette that only rescheduled when it actually drew
        // would fire the instant an override ended, or the instant it was
        // switched back on — the cadence would be measured from the unlock
        // rather than from the clock, and the robot would react to being freed
        // instead of living at its own pace.
        scheduleNext(now);
        if (locked || !_enabled) return false;

        eEmotions pick = draw();
        if (pick == current) return false;
        out = pick;
        return true;
    }

private:
    const Clock& _clock;
    Rng&         _rng;
    float        _weights[EMOTIONS_COUNT] = {};   // zero by default
    uint32_t     _minMs = 6000, _maxMs = 12000;   // default interval
    uint32_t     _nextAt = 0;
    bool         _dark   = false;                 // night mode (dark_sleepy)
    bool         _enabled = true;                 // see setEnabled()

    // Dominant Sleepy weight in night mode: ~66 % of draws
    // (3.0 / (3.0 + 1.54, the sum of the default weights excluding
    // Normal/Sleepy)).
    static constexpr float DARK_SLEEPY_WEIGHT = 3.0f;

    void scheduleNext(uint32_t now) { _nextAt = now + _rng.range(_minMs, _maxMs); }

    // EFFECTIVE weight (night mode: Normal → 0, Sleepy → dominant)
    float wOf(int i) const {
        if (_dark) {
            if (i == (int)Normal) return 0.0f;
            if (i == (int)Sleepy) return DARK_SLEEPY_WEIGHT;
        }
        return _weights[i];
    }

    // Weighted draw — port of FaceBehavior::GetRandomEmotion (AGPL)
    eEmotions draw() {
        float sum = 0.0f;
        for (int i = 0; i < EMOTIONS_COUNT; i++) sum += wOf(i);
        if (sum <= 0.0f) return _dark ? Sleepy : Normal;
        float r = _rng.rangef(0.0f, sum), acc = 0.0f;
        for (int i = 0; i < EMOTIONS_COUNT; i++) {
            float w = wOf(i);
            if (w <= 0.0f) continue;
            acc += w;
            if (r <= acc) return (eEmotions)i;
        }
        return _dark ? Sleepy : Normal;
    }
};

} // namespace sce
