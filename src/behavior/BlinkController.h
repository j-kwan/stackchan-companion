#pragma once
// =============================================================================
// BlinkController.h — StackChan-Companion (behavior)
// =============================================================================
// THE single owner of the eyelids (ROADMAP §3.3). Nobody "suspends" it from
// the outside: it applies a POLICY per expression plus internal suppression
// windows bounded in time — the whole class of leaking suspend/restore flag
// bugs disappears by construction.
//
// Outputs: openL()/openR() in [0..1], copied into FaceState every tick.
// The closing is rendered as EyeRig's scaleY (the openRatio path, validated
// on winks).
//
// Policies (§3.3):
//   - default    : log-uniform interval (median 3.5 s), blink 60/40/100 ms
//   - bounded    : Surprised/Scared/Awe → 2-4 s of wide-open eyes THEN slow
//     freeze       blinks resume (a gaze that never blinks again looks dead);
//                  Frozen/Scary/Nervous/Contempt/Excited → heavily reduced
//                  rate; Dead → the only total block
//   - Focused    : rate /2; Angry/Furious/Excited: x1.5, snappier
//   - Sleepy     : "fighting off sleep", ONE state machine PER EYE
//                  (ASYNCHRONOUS closings — user request 2026-07-16): slow
//                  droop → fast fall → laborious reopening with
//                  micro-dips, ceiling ~70 %, FLOOR SL_FLOOR
//                  (the eye never quite closes — it is fighting),
//                  startle ~1 cycle in 4 — each eye at its own pace
//
// Natural couplings: notifySaccade(amp) raises blink probability during large
// saccades; notifyEmotionChanged(strong) often triggers a blink on strong
// transitions. Refractory period 300 ms.
//
// Asymmetry (§3.0): the right eye follows the left with a slight delay
// (ring buffer, tuning.blink_lag_ms — default 30 ms, 0 = synchronous;
// 80 ms turned out to be too visible in practice).
//
// PURITY: Clock + Rng injected, no Arduino dependency → test_behavior.
// =============================================================================

#include <cstdint>
#include <cmath>
#include "../engine/Clock.h"
#include "../engine/Rng.h"
#include "../engine/Emotions.h"
#include "../engine/Tuning.h"
#include "../engine/Blender.h"

namespace sce {

class BlinkController {
public:
    BlinkController(const Clock& clock, Rng& rng, const Tuning& tuning)
        : _clock(clock), _rng(rng), _tuning(tuning),
          _outBlender(clock, 1.0f), _outBlenderR(clock, 1.0f) {
        for (int i = 0; i < HIST_SIZE; i++) _history[i] = 1.0f;
        scheduleNext(_clock.ms());
    }

    // ------------------------------------------------------------------
    // Expression change → new policy. `strong`: intense (STRONG) transition
    // → a punctuation blink is likely.
    // ------------------------------------------------------------------
    void setEmotion(eEmotions e, bool strong) {
        if (e == _emotion) return;
        _emotion = e;
        uint32_t now = _clock.ms();

        if (isFrozenStart(e)) {
            // Bounded freeze: wide open 2-4 s, then slow blinks resume
            _freezeUntil = now + _rng.range(2000, 4000);
        } else {
            _freezeUntil = 0;
        }
        if (e == Sleepy) {
            // TWO independent machines (user request 2026-07-16: each eye
            // closes asynchronously) — each draws its own durations, so the
            // droop/fall/reopen cycles diverge naturally.
            _slActive = true;
            _slL.reset(now, _open, _rng);
            _slR.reset(now, _open, _rng);
        } else if (_slActive) {
            // Leaving Sleepy: the normal machine restarts at "open", and the
            // ChannelBlenders (§3.7) crossfade each eye from ITS OWN current
            // opening — no eyelid snap
            _slActive = false;
            _open     = 1.0f;
            _outBlender.blendTo(3 * blend::LID_MS);    // 360 ms: gentle wake-up
            _outBlenderR.blendTo(3 * blend::LID_MS);
        }
        // Punctuation blink on a strong transition (except freeze/Dead/Sleepy)
        if (strong && e != Dead && e != Sleepy && !_freezeUntil &&
            _rng.chance(0.6f)) {
            triggerBlink(now);
        }
        scheduleNext(now);
    }

    // Saccade coupling: a large saccade → likely blink (it masks the motion)
    void notifySaccade(float amplitude) {
        if (_emotion == Dead || _emotion == Sleepy) return;
        uint32_t now = _clock.ms();
        if (now - _lastBlinkAt < REFRACTORY_MS) return;
        if (amplitude > 0.25f && _rng.chance(0.5f)) triggerBlink(now);
    }

    // Reflex preemption (shake/pickup — user rule 2026-07-07: whatever is
    // running is cut IMMEDIATELY): cancels any in-flight wink and blink, then
    // reopens smoothly through the blender — the reflex expression that
    // follows (Scared/Curious) starts from a clean eyelid state.
    void preempt() {
        _winkActive = false;
        if (_phase != BK_IDLE) {
            _phase = BK_IDLE;
            _open  = 1.0f;
            _outBlender.blendTo(blend::LID_MS);
        }
    }

    // External commands (API/interactions) — they only honour Dead.
    // TOP PRIORITY (user 2026-07-11: "interactions come first", a tap during
    // an ongoing blink/dance must respond): restarts the closing even if a
    // phase is already in flight.
    void requestBlink() {
        if (_emotion == Dead) return;
        uint32_t now = _clock.ms();
        _phase       = BK_CLOSING;
        _phaseAt     = now;
        _lastBlinkAt = now;
        _closeFrom   = _open;   // resume from the current opening (no snap)
    }
    void requestWink(bool left) {
        if (_emotion == Dead) return;
        _winkLeft   = left;
        _winkAt     = _clock.ms();
        _winkActive = true;   // dedicated flag — _winkAt==0 is a valid instant
    }

    // ------------------------------------------------------------------
    // Tick (100 Hz) — advances the active machine and the lag history
    // ------------------------------------------------------------------
    void update() {
        uint32_t now = _clock.ms();

        if (_emotion == Sleepy) {
            _slL.update(now, _rng);
            _slR.update(now, _rng);
            _open = _slL.open;      // reference (Sleepy exit, lag history)
        } else {
            updateNormal(now);
        }

        // Wink: dedicated per-eye envelope (close 80 ms, hold 220 ms, reopen
        // 150 ms) layered on top of the current state
        float winkL = 1.0f, winkR = 1.0f;
        if (_winkActive) {
            uint32_t e = now - _winkAt;
            float v;
            if      (e < 80)        v = 1.0f - (float)e / 80.0f;
            else if (e < 300)       v = 0.0f;
            else if (e < 450)       v = ((float)e - 300.0f) / 150.0f;
            else                  { v = 1.0f; _winkActive = false; }
            (_winkLeft ? winkL : winkR) = v;
        }

        // ChannelBlenders: they absorb the discontinuities (leaving Sleepy) —
        // in the normal regime they are a pure pass-through
        float openBase = _outBlender.track(_open);

        // Right-eye lag (asymmetry §3.0) — TUNABLE: tuning.blink_lag_ms
        // (default 30 ms; 80 ms turned out to be too visible in practice).
        // 16-tick ring buffer (0-150 ms at 100 Hz), 0 = perfectly synchronous.
        _history[_histIdx] = openBase;
        int lagTicks = (int)(_tuning.blink_lag_ms / 10.0f);
        if (lagTicks < 0) lagTicks = 0;
        if (lagTicks > HIST_SIZE - 1) lagTicks = HIST_SIZE - 1;
        float openLagged = _history[(_histIdx + HIST_SIZE - lagTicks) % HIST_SIZE];
        _histIdx = (_histIdx + 1) % HIST_SIZE;

        // Right eye: in Sleepy, ITS OWN machine (genuinely asynchronous
        // closing, user 2026-07-16); otherwise the delayed copy of the left
        // one. The right blender crossfades the Sleepy → normal handover.
        float baseR = _outBlenderR.track(
            _emotion == Sleepy ? _slR.open : openLagged);

        _openL = openBase * winkL;
        _openR = baseR    * winkR;
    }

    float openL() const { return _openL; }
    float openR() const { return _openR; }

private:
    // ---- Normal blink machine ----
    enum BlinkPhase { BK_IDLE, BK_CLOSING, BK_CLOSED, BK_OPENING };
    // ---- Sleepy "fighting off sleep" machine ----
    enum SleepyState { SL_NONE, SL_DROOP, SL_FALL, SL_REOPEN, SL_STARTLE_HOLD };

    static constexpr uint32_t REFRACTORY_MS = 300;
    static constexpr int      HIST_SIZE     = 16;   // 150 ms max at 100 Hz

    const Clock&  _clock;
    Rng&          _rng;
    const Tuning& _tuning;

    eEmotions _emotion = Normal;
    float     _open    = 1.0f;   // left eye (reference); right = lagged copy
    float     _openL   = 1.0f, _openR = 1.0f;

    BlinkPhase _phase       = BK_IDLE;
    float      _closeFrom   = 1.0f;   // opening at the start of the closing
    uint32_t   _phaseAt     = 0;
    uint32_t   _nextBlinkAt = 0;
    uint32_t   _lastBlinkAt = 0;
    uint32_t   _freezeUntil = 0;

    // ------------------------------------------------------------------
    // Sleepy — spec §3.3, ONE machine PER EYE (user 2026-07-16: asynchronous
    // closing). Each instance draws its own durations/ceilings: the two eyes
    // fall asleep and fight back at their own pace.
    // FIGHT FLOOR: the falls stop at SL_FLOOR (≈ 1-2 px on the 24-32 px
    // Sleepy presets) — sleep NEVER quite wins. 0.08 stays > 0.06, so the
    // Renderer's "both eyes closed" line does not kick in (it is reserved
    // for real blinks).
    // ------------------------------------------------------------------
    static constexpr float SL_FLOOR = 0.08f;

    struct SleepyMachine {
        SleepyState state   = SL_NONE;
        uint32_t    phaseAt = 0;
        uint32_t    dur     = 0;
        float       from    = 1.0f;
        float       ceil    = 0.70f;   // reopening ceiling
        int         dips    = 0;       // remaining micro-dips
        float       open    = 1.0f;    // output [SL_FLOOR..1]

        void reset(uint32_t now, float openNow, Rng& rng) {
            state   = SL_DROOP;
            phaseAt = now;
            from    = open = openNow;
            dur     = rng.range(2000, 4000);
        }

        void update(uint32_t now, Rng& rng) {
            uint32_t e = now - phaseAt;
            switch (state) {
            case SL_NONE: reset(now, open, rng); break;

            case SL_DROOP: {     // slow drift toward 0.35 — he is dozing off
                float t = (float)e / (float)dur;
                if (t >= 1.0f || (t > 0.4f && rng.chance(0.003f))) {
                    state = SL_FALL; phaseAt = now; from = open;
                } else {
                    float s = t * t * (3.0f - 2.0f * t);   // gentle ease
                    open = from + (0.35f - from) * s;
                }
                break;
            }
            case SL_FALL: {      // fast ~100 ms fall — sleep ALMOST wins
                float t = (float)e / 100.0f;
                if (t >= 1.0f) {
                    open = SL_FLOOR;                       // never shut (fighting)
                    bool startle = rng.chance(0.25f);      // startle 1 in 4
                    state   = startle ? SL_STARTLE_HOLD : SL_REOPEN;
                    phaseAt = now;
                    dur     = startle ? rng.range(160, 240)     // brisk rise
                                      : rng.range(500, 900);    // laborious one
                    ceil    = startle ? 0.95f : rng.rangef(0.60f, 0.75f);
                    dips    = startle ? 0 : rng.range(1, 2);
                } else {
                    open = from * (1.0f - t) + SL_FLOOR * t;
                }
                break;
            }
            case SL_REOPEN: {    // slow reopening + micro-dips (the effort)
                float t = (float)e / (float)dur;
                if (t >= 1.0f) {
                    open = ceil;
                    state = SL_DROOP; phaseAt = now; from = open;
                    dur = rng.range(2000, 4000);
                } else {
                    float base = SL_FLOOR + (ceil - SL_FLOOR)
                               * (1.0f - (1.0f - t) * (1.0f - t));   // ease-out
                    // micro-dips: sinusoidal troughs at 1/3 and 2/3
                    float dip = 0.0f;
                    if (dips >= 1 && t > 0.25f && t < 0.45f)
                        dip = 0.12f * sinf((t - 0.25f) / 0.20f * 3.14159f);
                    if (dips >= 2 && t > 0.55f && t < 0.75f)
                        dip = 0.10f * sinf((t - 0.55f) / 0.20f * 3.14159f);
                    open = base - dip;
                }
                break;
            }
            case SL_STARTLE_HOLD: {   // startle: brisk rise, held ~1 s
                if (e < dur) {
                    float t = (float)e / (float)dur;
                    // slight overshoot: goes 5 % past, then settles at the ceiling
                    float ov = 1.0f - (1.0f - t) * (1.0f - t);
                    open = SL_FLOOR + (ceil * 1.05f - SL_FLOOR) * ov;
                    if (open > 1.0f) open = 1.0f;
                } else if (e < dur + 1000) {
                    open = ceil;                           // held ~1 s
                } else {
                    state = SL_DROOP; phaseAt = now; from = open;
                    dur = rng.range(1200, 2200);           // faster re-droop
                }
                break;
            }
            }
        }
    };

    SleepyMachine _slL, _slR;
    bool          _slActive = false;

    // Wink
    uint32_t _winkAt     = 0;
    bool     _winkActive = false;
    bool     _winkLeft   = true;

    // Right-eye lag
    float _history[HIST_SIZE];
    int   _histIdx = 0;

    // Discontinuity smoothing (§3.7) — leaving Sleepy, future handovers.
    // One blender PER EYE: in Sleepy each eye has its own machine, so the
    // Sleepy → normal handover must crossfade each one from ITS OWN value.
    FloatBlender _outBlender;    // left eye (reference channel)
    FloatBlender _outBlenderR;   // right eye

    // ------------------------------------------------------------------
    static bool isFrozenStart(eEmotions e) {
        return e == Surprised || e == Scared || e == Awe;
    }

    // Per-expression interval multiplier (×1 = 3.5 s median)
    float rateDivider() const {
        switch (_emotion) {
            case Focused: case Squint:                return 2.0f;   // /2
            case Angry: case Furious: case Excited:   return 0.66f;  // x1.5
            case Frozen: case Scary: case Nervous:
            case Contempt:                            return 4.0f;   // very rare
            case Surprised: case Scared: case Awe:    return 3.0f;   // slow, post-freeze
            default:                                  return 1.0f;
        }
    }

    void scheduleNext(uint32_t now) {
        // Log-uniform interval [median/2, median×2] — breaks the metronomic
        // feel (§3.3), modulated by the expression's policy.
        // The median is tunable at runtime (tuning.blink_median_ms).
        float med = _tuning.blink_median_ms * rateDivider();
        float f   = _rng.rangef(-1.0f, 1.0f);
        _nextBlinkAt = now + (uint32_t)(med * (f < 0 ? 1.0f / (1.0f - f) : 1.0f + f));
    }

    void triggerBlink(uint32_t now) {
        if (_phase != BK_IDLE) return;
        _phase       = BK_CLOSING;
        _phaseAt     = now;
        _lastBlinkAt = now;
        _closeFrom   = _open;
    }

    void updateNormal(uint32_t now) {
        // Scheduled autoblink — except Dead (never) and during an active freeze
        if (_phase == BK_IDLE && _emotion != Dead &&
            now >= _nextBlinkAt && now >= _freezeUntil) {
            triggerBlink(now);
            scheduleNext(now);
        }

        switch (_phase) {
        case BK_IDLE:
            _open = 1.0f;
            break;
        case BK_CLOSING: {   // 60 ms — fast closing (t²) starting at _closeFrom
            float t = (now - _phaseAt) / 60.0f;
            if (t >= 1.0f) { _phase = BK_CLOSED; _phaseAt = now; _open = 0.02f; }
            else           { _open = _closeFrom + (0.02f - _closeFrom) * t * t; }
            break;
        }
        case BK_CLOSED:      // 40 ms held shut
            if (now - _phaseAt >= 40) { _phase = BK_OPENING; _phaseAt = now; }
            break;
        case BK_OPENING: {   // 100 ms — ease-out reopening
            float t = (now - _phaseAt) / 100.0f;
            if (t >= 1.0f) { _phase = BK_IDLE; _open = 1.0f; }
            else           { _open = 0.02f + (1.0f - (1.0f - t) * (1.0f - t)) * 0.98f; }
            break;
        }
        }
    }

};

} // namespace sce
