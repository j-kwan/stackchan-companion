#pragma once
// =============================================================================
// EmotionLeds.h — StackChan-Companion (app)
// =============================================================================
// Emotional emphasis on the 12 WS2812C LED bars (ROADMAP §3.9).
// PY32 protocol VALIDATED on hardware during P0 (Py32Expander::setAllLeds —
// GPIO 13 + count 0x24 + RGB565 LE RAM 0x30 + refresh bit 6, R/G/B verdicts
// given by the user).
//
// Principles (§3.9):
//   - EMPHASIS, not lighting: the colour is the one the Renderer DISPLAYS
//     (Renderer::eyeColorRgb — transition and dimming included) → LEDs stay
//     in sync with the screen. Brightness ∝ emotion intensity, ramp speed
//     ∝ intensity (snappy on Furious, slow on Sleepy).
//   - Slow breathing when idle (follows the FaceState breath channel).
//   - PULSE on strong emotion changes (brief flash, then back down).
//   - Sleepy → very low brightness; Dead → steady, very dim red.
//   - OFF by default (tuning.leds = 0) — enable with POST /api/tuning?leds=1.
//
// Rate: called from loop() (~50 Hz) but only writes I2C when the colour or a
// bar brightness actually changed; brightness-only changes are further capped
// at 40 Hz, colour changes go out immediately (screen↔LED sync). The PY32 bus
// runs at 100 kHz — 25 bytes per frame, so useless traffic is avoided.
//
// Thread: loop() only (Wire1 already lives there — Si12T/PY32).
// =============================================================================

#include <Arduino.h>
#include "../hal/Py32Expander.h"
#include "../engine/Emotions.h"
#include "../engine/Tuning.h"
#include "../engine/LedBars.h"        // the depth distribution (§10), pure
#include "../../firmware/common/Py32Leds.h"  // scale565: ONE rounding, shared

namespace sce {

class EmotionLeds {
public:
    EmotionLeds(Py32Expander& py32, const Tuning& tuning)
        : _py32(py32), _tuning(tuning) {}

    // IMMEDIATE shutdown (exclusive launcher mode: loop() is blocked during
    // run(), so the LEDs would stay frozen lit on their last state, 07-26).
    void forceOff() { _py32.ledsOff(); _wasOn = false; }

    // Call from loop(). emotion: Brain::currentEmotion() (atomic);
    // eyeRgb: Renderer::eyeColorRgb() — the colour ACTUALLY displayed
    // (transition included) → LEDs in sync with the screen (user 07-12);
    // heightL/heightR: Renderer::eyeHeightL()/eyeHeightR() — the drawn SIZE of
    // each eye, normalised 0 (closed/nothing) → 1 (the Surprised reference
    // eye). The LEFT bar's brightness follows the left eye, the RIGHT one the
    // right eye: the BIGGER the eye, the brighter the bar (Surprised = max,
    // asymmetric Curious/Questioning); closed (blink/wink) → off.
    // ⚠ NO faceBus.read() here: the TripleBuffer is SINGLE-consumer (the
    // Renderer) — a second read() would corrupt the front/middle swap.
    // yawRateDegS: the COMMANDED yaw rate (ServoMotion::cmdVelDegS().x), used
    // by the depth channel below. Commanded and never the gyro: the gyro also
    // fires when a human turns the robot by hand, and the bars would then
    // report a turn the robot never made — an indicator that lies about who is
    // driving (§10).
    void update(eEmotions emotion, uint32_t eyeRgb, float heightL, float heightR,
                float yawRateDegS = 0.0f) {
        uint32_t now = millis();
        float breath = sinf(now * (2.0f * PI / 4000.0f));
        bool enabled = _tuning.leds >= 0.5f;

        // Disabled: turn the LEDs off exactly once
        if (!enabled) {
            if (_wasOn) { _py32.ledsOff(); _wasOn = false; }
            return;
        }

        // L/R wiring swapped: exchange the two bars (led_swap tuning key)
        if (_tuning.led_swap >= 0.5f) { float t = heightL; heightL = heightR; heightR = t; }

        // ---- Colour: the one the Renderer DISPLAYS this frame (already
        //      interpolated + dimmed) — exact screen↔LED sync ----
        uint32_t rgb = (emotion == Dead) ? 0x400000 : eyeRgb;

        // ---- Pulse on emotion change (120 ms flash) ----
        if (emotion != _lastEmotion) {
            _lastEmotion = emotion;
            _pulseUntil  = now + 120;
        }

        // ---- Emotional intensity (user 2026-07-11: "brighter, gradually, at
        //      a speed ∝ intensity"): every emotion has a target [0..1];
        //      brightness CONVERGES towards it with a short time constant for
        //      strong emotions (~150 ms) and a long one for calm ones
        //      (~800 ms) — a rocketing rise on Furious, a lazy one on Sleepy.
        //      The smoothers step at a FIXED 50 ms RATE (review 2026-07-17:
        //      they used to step PER CALL — moving the loop from 50 to 10 ms
        //      sped them up ×1.7 in steady state and ×5 in transition, drifting
        //      away from the HW-validated calibration). Colour writes stay
        //      immediate, using the latest smoothed values.
        float k = intensityOf(emotion);
        if (now - _lastStepMs >= 50) {
            _lastStepMs = now;
            float alpha = 0.05f + 0.30f * k;          // speed ∝ intensity
            _level += alpha * (k - _level);

            // ---- Brightness: base × smoothed level + breathing ±20 % +
            //      pulse. RISE SPEED ∝ the animation (user 2026-07-12):
            //      intense emotion → snappy ramp (~100 ms), calm one → lazy
            //      (~1 s). The pulse goes through the same smoothing.
            float lumTgt = _tuning.leds_brightness * (0.35f + 0.90f * _level);
            lumTgt *= 1.0f + 0.20f * breath;
            if (emotion == Dead) lumTgt = _tuning.leds_brightness * 0.5f;
            if (now < _pulseUntil) lumTgt *= 2.0f;
            if (lumTgt > 255.0f) lumTgt = 255.0f;
            _lum += (0.08f + 0.42f * k) * (lumTgt - _lum);
        }

        uint8_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
        uint8_t bri = (uint8_t)_lum;

        // ---- PER-BAR brightness ∝ eye SIZE: heightL/R are already the drawn
        //      height normalised to [0..1] (emotion + squash + blink), so a
        //      bigger eye gives a brighter bar and closing it turns the bar
        //      off. No smoothing here (the Brain already smooths the channels;
        //      the height follows the render tick for tick). ----
        uint8_t briL = (uint8_t)(bri * heightL);
        uint8_t briR = (uint8_t)(bri * heightR);

        // ---- DEPTH (§10): how the amplitude is spread along the bar --------
        // The distribution is part of the PICTURE, so it has to take part in
        // the change detection. Watching colour and amplitude alone, a bar
        // whose light had moved along its own depth under an unchanged
        // brightness would simply never be sent — the effect would work only
        // when something else happened to change at the same moment.
        // Quantised to a byte for the same reason the dot keys are: comparing
        // floats would repaint on noise. SIGNED, because the sign is which end
        // of the bar is dark — turning left and turning right at the same rate
        // are two different pictures, and a magnitude would call them equal.
        const int8_t depth = (int8_t)(ledbars::displacement(yawRateDegS,
                                                            _tuning.led_depth) * 127.0f);

        // Only write if the colour, one of the two brightnesses or the
        // distribution changed — otherwise cap changes at 40 Hz (25 ms).
        bool colorChanged = (rgb & 0xFFFFFF) != _lastRgb;
        if (!colorChanged && briL == _lastBriL && briR == _lastBriR &&
            depth == _lastDepth) return;
        if (!colorChanged && now - _lastWriteMs < 25) return;
        _lastRgb     = rgb & 0xFFFFFF;
        _lastBriL    = briL;
        _lastBriR    = briR;
        _lastDepth   = depth;
        _lastWriteMs = now;

        if (_tuning.led_depth < 0.5f) {
            // OFF is the old path, untouched. The distribution at rest is
            // provably the identity (test_ledbars asserts it exactly), so this
            // branch is not needed for correctness — it is here so that a robot
            // which never enables the feature runs the code that was validated
            // on hardware, byte for byte and instruction for instruction.
            _py32.setLedsBars(r, g, b, briL, briR);
        } else {
            float w[ledbars::PER_BAR];
            ledbars::weights(yawRateDegS, _tuning.led_depth,
                             _tuning.led_depth_front >= 0.5f, w);
            uint16_t colors[py32::LED_COUNT];
            for (int i = 0; i < ledbars::PER_BAR; i++) {
                colors[i] = py32::scale565(r, g, b, (uint8_t)(briL * w[i]));
                colors[i + ledbars::PER_BAR] =
                    py32::scale565(r, g, b, (uint8_t)(briR * w[i]));
            }
            _py32.setLedsRaw(colors);
        }
        _wasOn = true;
    }

private:
    // Emotional intensity [0..1] — drives the target brightness AND its rise
    // speed (same families as EmotionRoulette::transitionFor)
    static float intensityOf(eEmotions e) {
        switch (e) {
            case Furious: case Scared: case Scary:                return 1.0f;
            case Angry: case Surprised: case Excited: case Awe:   return 0.9f;
            case Glee: case Frustrated:                           return 0.7f;
            case Happy: case Curious: case Blush:                 return 0.55f;
            case Sad: case Worried: case Sleepy:                  return 0.15f;
            case Dead:                                            return 0.1f;
            default:                                              return 0.35f;
        }
    }

    Py32Expander&  _py32;
    const Tuning&  _tuning;
    float          _level       = 0.35f;   // smoothed level (intensity)
    float          _lum         = 0.0f;    // smoothed brightness
    uint32_t       _lastStepMs  = 0;       // FIXED smoothing step rate (50 ms)
    eEmotions      _lastEmotion = EMOTIONS_COUNT;
    uint32_t       _pulseUntil  = 0;
    uint32_t       _lastWriteMs = 0;
    uint32_t       _lastRgb     = 0xFFFFFFFF;   // forces the 1st write
    uint8_t        _lastBriL    = 0;
    uint8_t        _lastBriR    = 0;
    int8_t         _lastDepth   = 0;    // quantised SIGNED distribution (§10)
    bool           _wasOn       = false;
};

} // namespace sce
