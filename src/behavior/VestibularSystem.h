#pragma once
// =============================================================================
// VestibularSystem.h — StackChan-Companion (behavior)
// =============================================================================
// Vestibulo-Ocular Reflex (ROADMAP §2.2) — goal #1 of the whole project.
// Replaces an accelerometer-based tracker + low-pass filter + spring
// (~150-300 ms of lag) with the biological model:
//
//   SLOW PHASE (VOR)    : IMMEDIATE counter-rotation driven by the GYROSCOPE —
//                         eyeOffset -= headVel × dt × gain. NO smoothing:
//                         latency IS the quality of the reflex.
//   DRIFT               : integrating the gyro drifts → very slow
//                         complementary correction towards the accelerometer
//                         tilt target (alpha ≈ 0.02 — invisible to the eye).
//   FAST PHASE          : when the offset saturates (> threshold × max) OR the
//   (catch-up saccade)    head is steady with a residual offset, a recentring
//                         SACCADE (60-100 ms, hard ease-out) — nystagmus.
//                         The event is exposed (blink coupling §3.3).
//   EFFERENCE COPY      : update() receives cmdVel (angular velocity COMMANDED
//                         to the servos, P4) and subtracts it from the gyro —
//                         self-generated motion triggers neither VOR nor Scared.
//
// Shake detection: |headVel| sustained > threshold for > 500 ms →
// shakeDetected() (the Brain fires Scared). Efference copy made the old
// danceActive guard unnecessary.
//
// IMPROVEMENTS (2026-07-07, replacing the first version):
//   GYRO BIAS    : the BMI270 has a DC offset (0.5-2 °/s typical) → without
//                  estimating it the eye drifts permanently and lives off
//                  catch-up saccades. The bias is learned while the head is
//                  STILL (calm gyro + |a| ≈ 1 g, both sustained): fast EMA
//                  for the first 2 seconds (boot capture), very slow after
//                  that (thermal tracking). Subtracted before any integration.
//   DEADBAND     : after bias removal, |vel| < 0.8 °/s = noise → no
//                  integration. The eyes are PERFECTLY still at rest.
//   GATED DRIFT  : the accelerometer measures gravity + linear acceleration —
//                  during motion the "tilt target" is wrong. The
//                  complementary correction is applied only when the head is
//                  calm AND |a| ≈ 1 g (gated complementary filter, standard
//                  IMU fusion practice).
//
// Inputs in °/s and g, signed viewer-centric SCREEN AXES (CONVENTIONS §3) —
// the sensor→screen mapping is hal/ImuReader's business, NOT this file's.
//
// PURITY: injected Clock, no Arduino dependency — tested natively with
// synthetic gyro profiles (test_vor).
// =============================================================================

#include <cstdint>
#include <cmath>
#include "../engine/Clock.h"
#include "../engine/Units.h"
#include "../engine/Tuning.h"

namespace sce {

class VestibularSystem {
public:
    VestibularSystem(const Clock& clock, const Tuning& tuning)
        : _clock(clock), _tuning(tuning), _lastMs(clock.ms()) {}

    // ------------------------------------------------------------------
    // Tick (100 Hz).
    //   headVelRawDegS : RAW head rotation (bias included), °/s, screen axes
    //                    (x = viewer yaw, y = pitch) — mapped by ImuReader
    //   tiltG          : accelerometer tilt, in gaze units [-1..1]
    //   accelMagG      : |accel| in g (drift gate: 1 g = trustworthy)
    //   cmdVelDegS     : servo efference copy (0,0 as long as P4 is missing)
    //   gyroMag3DegS   : |gyro| over 3 axes (shake independent of the
    //                    mapping); < 0 → fall back on the mapped |vel|
    //                    (native tests)
    //   selfMotion     : dance / servo trajectory in flight — inhibits ONLY
    //                    shake detection (v3.1: efference copy being validated
    //                    in §1.7, integration stays active during self-motion —
    //                    fixes "Scared at the end of Laugh")
    //   magHeadingDeg  : BMM150 magnetic heading 0..360° (< 0 = no reading).
    //                    Consumed only when `vor_mag_alpha` > 0 — see the
    //                    yaw-correction block for what it observes that the
    //                    accelerometer cannot.
    // Returns true if a catch-up saccade has JUST started.
    // ------------------------------------------------------------------
    bool update(Vec2f headVelRawDegS, Vec2f tiltG, float accelMagG = 1.0f,
                Vec2f cmdVelDegS = {}, float gyroMag3DegS = -1.0f,
                bool selfMotion = false, float magHeadingDeg = -1.0f) {
        uint32_t now = _clock.ms();
        float dt = (now - _lastMs) / 1000.0f;
        _lastMs = now;
        if (dt <= 0.0f || dt > 0.1f) dt = 0.01f;   // first tick / gap: clamp it

        // ---- Gyro bias estimation (2026-07-07) ----
        // "Still" conditions: low debiased velocity AND pure gravity.
        Vec2f debiased = headVelRawDegS - _bias;
        bool quiet = debiased.length() < QUIET_DEGS &&
                     fabsf(accelMagG - 1.0f) < QUIET_ACC_G;
        if (quiet) {
            if (!_quietSinceMs) _quietSinceMs = now;
            // Fast capture at boot (first 2 s), very slow tracking afterwards
            float aB = (now < 2000) ? 0.05f : 0.002f;
            if (now - _quietSinceMs > 300) {
                _bias += (headVelRawDegS - _bias) * aB;
            }
        } else {
            _quietSinceMs = 0;
        }

        // ---- Efference copy + anti-noise deadband ----
        Vec2f vel = headVelRawDegS - _bias - cmdVelDegS;
        if (fabsf(vel.x) < DEADBAND_DEGS) vel.x = 0.0f;
        if (fabsf(vel.y) < DEADBAND_DEGS) vel.y = 0.0f;

        // ---- Shake detection (sustained > threshold for 500 ms) ----
        // VOR v3: gyro magnitude over 3 AXES (mapping-free — a shake can be on
        // any axis); inhibited during self-motion (the 2-axis efference copy
        // is not yet calibrated to cancel it out).
        float mag      = vel.length();
        float shakeMag = gyroMag3DegS >= 0.0f ? gyroMag3DegS : mag;
        if (!selfMotion && shakeMag > _tuning.shake_gyro_thr) {
            if (!_shakeStartMs) _shakeStartMs = now;
            _shakeDetected = (now - _shakeStartMs > 500);
        } else {
            _shakeStartMs  = 0;
            _shakeDetected = false;
        }

        // Equilibrium target: tilt compensation (the otolith organs).
        // At rest on a slope the eyes HOLD that target — not zero.
        Vec2f target = { tiltG.x * units::GAZE_MAX_X,
                         tiltG.y * units::GAZE_MAX_Y };

        bool saccadeStarted = false;
        switch (_state) {
        case STABILIZE: {
            // v3.1 (2026-07-11): efference copy VALIDATED on hardware
            // (TESTS §1.7 OK) → integration stays ACTIVE during self-motion —
            // `vel` already has cmdVel removed, so the VOR only reacts to
            // EXTERNAL rotations even mid-dance. Only shake detection stays
            // inhibited by selfMotion (a 3-axis magnitude cannot be corrected
            // by a 2-axis efference copy).
            // Slow phase: direct counter-rotation (the HEART of the VOR)
            _offset.x -= vel.x * dt * units::DEG2GAZE_X * _tuning.vor_gain;
            _offset.y -= vel.y * dt * units::DEG2GAZE_Y * _tuning.vor_gain;

            // GATED drift correction (2026-07-07): calm head and pure gravity
            // only — during motion the accelerometer lies (gravity + linear
            // acceleration) and would corrupt the stabilisation
            if (quiet) {
                float a = _tuning.vor_drift_alpha;
                _offset += (target - _offset) * a;
            }

            // ---- MAG-GATED yaw correction (08-04) — the observable the
            // accelerometer does not have. Gravity says nothing about
            // rotation AROUND itself, so yaw drift is only ever caught here
            // by the quiet-gated pull and the catch-up saccades. The BMM150
            // heading is an ABSOLUTE yaw: if the eyes hold a world-fixed
            // direction, offset.x must equal target.x − (heading − ref)·K —
            // one anchored reference, one complementary pull toward it.
            // Division of labour with the block above, not competition: the
            // accel corrector owns the QUIET regime (and re-anchors us), the
            // magnetometer corrects DURING MOTION, exactly where the
            // accelerometer lies. Gated off during selfMotion — the servo
            // currents distort the very field being read — and re-anchored
            // whenever the reading disappears or a saccade rewrites the
            // offset, because a stale reference would "correct" toward a
            // direction nobody is holding.
            // `vor_mag_alpha` DEFAULTS TO 0 (off): the heading is not
            // tilt-compensated and its SIGN convention against the gyro yaw
            // is unvalidated on hardware — with the wrong sign this loop
            // doubles the drift instead of removing it. The HW calibration
            // session turns it on (CONFIG `vor_mag_alpha`).
            {
                // CLAMPED at 0.2 (user tried 0.5 on 08-04): this is an EMA at
                // 100 Hz — 0.5 corrects half the error per tick, which stops
                // being a drift corrector and starts being a second, nervous
                // controller fighting the integration. 0.2 is already brisk.
                float aMag = _tuning.vor_mag_alpha;
                if (aMag > 0.2f) aMag = 0.2f;
                const float K    = units::DEG2GAZE_X * _tuning.vor_gain;
                if (aMag > 0.0f && magHeadingDeg >= 0.0f && !selfMotion &&
                    K > 1e-6f) {
                    if (quiet || _magRef < 0.0f) {
                        _magRef = wrap360(magHeadingDeg
                                          - (target.x - _offset.x) / K);
                    } else {
                        float expX = target.x
                                   - wrapDeg(magHeadingDeg - _magRef) * K;
                        if (expX >  units::GAZE_MAX_X) expX =  units::GAZE_MAX_X;
                        if (expX < -units::GAZE_MAX_X) expX = -units::GAZE_MAX_X;
                        _offset.x += (expX - _offset.x) * aMag;
                    }
                } else {
                    _magRef = -1.0f;   // re-anchor on the next valid reading
                }
            }

            // Fast-phase (nystagmus) triggers:
            //  a) saturation: the eye hits its limit while the head keeps turning
            //  b) head steady > 300 ms with a residual gap vs the target
            bool saturated = fabsf(_offset.x) > _tuning.saccade_recentre * units::GAZE_MAX_X
                          || fabsf(_offset.y) > _tuning.saccade_recentre * units::GAZE_MAX_Y;
            bool headStill = mag < HEAD_STILL_DEGS;
            if (headStill) { if (!_stillSinceMs) _stillSinceMs = now; }
            else           _stillSinceMs = 0;
            bool residual  = _stillSinceMs && (now - _stillSinceMs > 300) &&
                             (_offset - target).length() > 0.15f * units::GAZE_MAX_X;

            if (saturated || residual) {
                _saccadeFrom   = _offset;
                _saccadeTo     = target;
                _magRef        = -1.0f;   // the saccade rewrites the offset:
                                          // the mag anchor is stale with it
                _saccadeAt     = now;
                _saccadeDur    = (uint32_t)_tuning.saccade_ms;
                if (_saccadeDur < 40) _saccadeDur = 40;
                _lastAmplitude = (_offset - target).length();
                _state         = SACCADE;
                saccadeStarted = true;
            }
            break;
        }
        case SACCADE: {
            // Fast phase: crisp catch-up (cubic ease-out) TOWARDS THE TARGET —
            // during the saccade the VOR is suspended (saccadic suppression)
            float t = (float)(now - _saccadeAt) / (float)_saccadeDur;
            if (t >= 1.0f) {
                _offset = _saccadeTo;
                _state  = STABILIZE;
                _stillSinceMs = 0;
            } else {
                float s = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
                _offset = _saccadeFrom + (_saccadeTo - _saccadeFrom) * s;
            }
            break;
        }
        }

        // Defensive clamp — the offset is ADDED to the idle gaze and the
        // arbiter re-clamps the total, but bound it here too (no build-up)
        _offset = units::clampGaze(_offset);
        return saccadeStarted;
    }

    // Current eye offset (ADDED to the base gaze — GazeArbiter §3.2)
    Vec2f offset() const { return _offset; }

    bool  shakeDetected()        const { return _shakeDetected; }
    bool  isSaccading()          const { return _state == SACCADE; }
    float lastSaccadeAmplitude() const { return _lastAmplitude; }
    Vec2f gyroBias()             const { return _bias; }   // telemetry / debug

private:
    enum State { STABILIZE, SACCADE };

    static constexpr float HEAD_STILL_DEGS = 3.0f;  // "head steady" (°/s)
    static constexpr float QUIET_DEGS      = 3.0f;  // bias/drift gate (°/s)
    static constexpr float QUIET_ACC_G     = 0.05f; // "pure" gravity (±g)
    static constexpr float DEADBAND_DEGS   = 0.8f;  // gyro noise after debias

    const Clock&  _clock;
    const Tuning& _tuning;

    State    _state  = STABILIZE;
    Vec2f    _offset{};
    Vec2f    _saccadeFrom{}, _saccadeTo{};
    uint32_t _lastMs;
    uint32_t _saccadeAt = 0, _saccadeDur = 80;
    uint32_t _stillSinceMs = 0;
    uint32_t _shakeStartMs = 0;
    uint32_t _quietSinceMs = 0;
    Vec2f    _bias{};               // estimated gyro bias (learned while still)
    float    _magRef = -1.0f;       // mag heading anchor (< 0 = re-anchor)
    bool     _shakeDetected = false;
    float    _lastAmplitude = 0.0f;

    // Heading arithmetic on a circle: differences in [-180, 180), absolute
    // values in [0, 360).
    static float wrapDeg(float a) {
        a = fmodf(a + 180.0f, 360.0f);
        if (a < 0.0f) a += 360.0f;
        return a - 180.0f;
    }
    static float wrap360(float a) {
        a = fmodf(a, 360.0f);
        return a < 0.0f ? a + 360.0f : a;
    }
};

} // namespace sce
