#pragma once
// =============================================================================
// ImuReader.h — StackChan-Companion (hal)
// =============================================================================
// Reads the BMI270 (through M5.Imu, the M5Unified API — NEVER getAccel())
// and maps sensor axes → viewer-centric SCREEN AXES (CONVENTIONS §3).
// The VestibularSystem receives °/s already expressed in the right frame:
// all mapping/signs live HERE and nowhere else.
//
// ACCELEROMETER MAPPING (verified on hardware):
//   accX → left/right screen tilt; accY → front/back (carries gravity at
//   rest). BOTH carry a rest-pose baseline, calibrated at boot over QUIET
//   ticks and thereafter tracked only NEAR that pose (see below).
//   The PHYSICAL direction of the two in-plane axes was measured 2026-08-22
//   (hardware/PERIPHERALS.md): +X is the observer's right, +Y the top of the
//   screen, so gravity in screen pixels is (-accel.x, +accel.y). Until then
//   only accel.z had ever had its sign read directly.
//
// GYROSCOPE MAPPING (VOR v3) — LIVE-TUNABLE, defaults VALIDATED ON HW
// (2026-07-11, PLAYBOOK-HW §1.5: counter-rotation + tilt hold both OK):
//   yaw = gyro.y (+), pitch = gyro.x (+). The first draft mapped
//   yaw <- gyro.z: wrong (z is the screen normal = roll) — imu_test proves
//   that gravity at rest sits on sensor Y → world vertical axis = Y.
//   Axes/signs live in the Tuning registry (gyro_yaw_axis/sign,
//   gyro_pitch_axis/sign): recalibration via POST /api/tuning, persisted to
//   SD, no reflash. gyroRawDegS() exposes the 3 raw axes for telemetry.
//
// THE REST-POSE BASELINE, and the two ways it has been wrong (2026-08-22):
//   v1 tracked continuously (α=0.005, τ≈2 s) and erased a held tilt within
//   seconds. v2 froze it after boot and tracked at α=0.0002 while "quiet" —
//   but τ was still 50 s and a robot HELD on a slope is quiet, so the same
//   erasure came back, just slowly enough that the 2026-07-11 validation over
//   a few seconds could not see it. v3 keeps the slow tracking but gates it on
//   being NEAR the baseline: thermal drift wanders around the rest pose, a
//   held tilt leaves it, so only the first is followed. Boot calibration now
//   counts QUIET ticks, so booting in a hand no longer calibrates the hand.
// =============================================================================

#include <Arduino.h>
#include <M5Unified.h>
#include "../engine/Units.h"
#include "../engine/Tuning.h"

namespace sce {

class ImuReader {
public:
    explicit ImuReader(const Tuning& tuning) : _tuning(tuning) {}

    // Call on every brain tick AFTER M5.Imu.update()
    void update() {
        auto d = M5.Imu.getImuData();
        _gyroRaw  = { d.gyro.x, d.gyro.y, d.gyro.z };
        _accelRaw = { d.accel.x, d.accel.y, d.accel.z };

        // ---- BMM150 magnetometer (9-axis, µT): absolute heading for the
        // telemetry (a drift-free yaw reference). M5.Imu integrates the
        // BMM150 when present; a null mag means no magnetometer → heading -1.
        // NB: the VOR fusion path EXISTS (VestibularSystem mag-gated yaw
        // correction, 08-04) but ships OFF (`vor_mag_alpha` = 0): the
        // hard-iron calibration (the servos/magnets bias the field) and the
        // sign convention against the gyro yaw are still to be validated on
        // hardware before turning it on.
        _magRaw = { d.mag.x, d.mag.y, d.mag.z };
        if (d.mag.x != 0.0f || d.mag.y != 0.0f) {
            float h = atan2f(d.mag.y, d.mag.x) * 57.2957795f;
            _heading = h < 0.0f ? h + 360.0f : h;   // 0..360°
        }

        // ---- Head angular rate, screen axes (°/s) — Tuning mapping ----
        float axes[3] = { d.gyro.x, d.gyro.y, d.gyro.z };
        int   ya = clampAxis(_tuning.gyro_yaw_axis);
        int   pa = clampAxis(_tuning.gyro_pitch_axis);
        _headVel.x = axes[ya] * (_tuning.gyro_yaw_sign   < 0.0f ? -1.0f : 1.0f);
        _headVel.y = axes[pa] * (_tuning.gyro_pitch_sign < 0.0f ? -1.0f : 1.0f);

        // ---- Magnitudes (pickup + 3-axis shake, mapping-independent) ----
        _accelMag = sqrtf(d.accel.x * d.accel.x + d.accel.y * d.accel.y +
                          d.accel.z * d.accel.z);
        _gyroMag  = sqrtf(d.gyro.x * d.gyro.x + d.gyro.y * d.gyro.y +
                          d.gyro.z * d.gyro.z);

        // ---- Software double-tap: brief |accel| peak, short refractory ----
        uint32_t nowMs = millis();
        if (_accelMag > TAP_THR_G && nowMs - _lastTapMs > TAP_REFRACTORY_MS) {
            if (nowMs - _lastTapMs <= TAP_WINDOW_MS) _doubleTap = true;
            _lastTapMs = nowMs;
        }

        // ---- Face up/down orientation: raw accel.z, simple hysteresis ----
        if (d.accel.z > FACE_THR_G)       _faceOrient = FaceOrient::FaceUp;
        else if (d.accel.z < -FACE_THR_G) _faceOrient = FaceOrient::FaceDown;
        else if (fabsf(d.accel.z) < FACE_THR_G * 0.6f) _faceOrient = FaceOrient::Normal;

        // ---- THE REST POSE, on BOTH in-plane axes -------------------------
        // The baseline is what the robot reads when it is sitting still where
        // it lives. Tilt is the DEPARTURE from that, so a baseline is needed on
        // each axis that can carry one — and X used to have none. At rest
        // upright accel.x is nominally zero, which is exactly why its absence
        // was invisible: any mounting or mechanical bias in X went straight
        // into the gaze as a permanent off-centre offset that no calibration
        // could ever remove.
        bool quiet = _gyroMag < 3.0f && fabsf(_accelMag - 1.0f) < 0.05f;

        // Boot calibration counts QUIET ticks, not the first ones that arrive.
        // Measuring the rest pose during the first second regardless of what
        // the robot was doing meant a boot in somebody's hand calibrated the
        // hand: the zero was wrong for the whole session, and only the runtime
        // drift below ever repaired it — which is precisely the drift that had
        // to be reined in.
        //
        // AND IT ALWAYS FINISHES. Counting only quiet ticks removed a guarantee
        // the unconditional version had for free: `quiet` demands |accel| within
        // 0.05 g of one, and a unit whose accelerometer scale sits a few percent
        // off — or one that simply never stops being handled — would never
        // satisfy it. The early return below leaves `_tilt` at zero, so the
        // whole balance channel would stay dead for the session, with nothing
        // logged and nothing to read that said why. A quiet failure of a sensor
        // is worse than a noisy one, so the wait is BOUNDED: after twenty
        // seconds the best average available is accepted — the quiet ticks if
        // any arrived, otherwise every tick seen — and the fallback is recorded
        // rather than hidden, because a robot calibrated while moving is a robot
        // whose rest pose is a guess and somebody has to be able to find that
        // out. `/api/sensors` publishes both.
        if (!_calDone) {
            _calSeen++;
            _anySumX += d.accel.x;
            _anySumY += d.accel.y;
            if (quiet) {
                _calSumX += d.accel.x;
                _calSumY += d.accel.y;
                _calTicks++;
            }
            if (_calTicks >= CAL_TICKS) {
                _baseX   = _calSumX / (float)_calTicks;
                _baseY   = _calSumY / (float)_calTicks;
                _calDone = true;
            } else if (_calSeen >= CAL_DEADLINE_TICKS) {
                const bool haveQuiet = _calTicks > 0;
                const float n = haveQuiet ? (float)_calTicks : (float)_calSeen;
                _baseX      = (haveQuiet ? _calSumX : _anySumX) / n;
                _baseY      = (haveQuiet ? _calSumY : _anySumY) / n;
                _calDone    = true;
                _calFellBack = true;
            }
            if (!_calDone) {
                _tilt = {};                   // no tilt while calibrating
                return;
            }
        }

        // ---- Drift tracking, and ONLY near the rest pose ------------------
        // This used to run wherever the robot was held, at 0.0002 per tick.
        // At the brain's 100 Hz that is a fifty-second time constant, and a
        // robot held still on a slope IS quiet — gyro at zero, |accel| at one
        // g. So a genuine sustained tilt was absorbed into its own baseline:
        // 63 % gone after fifty seconds, ninety after two minutes, and the
        // eyes drifted back to centre. `VestibularSystem` states the opposite
        // in as many words ("At rest on a slope the eyes HOLD that target —
        // not zero"), and VALIDATION.md's "static tilt held" was checked over
        // seconds, which is the one timescale where both are true.
        //
        // The comment called it thermal drift, and that is the honest fix:
        // thermal drift is a slow wander AROUND the rest pose, so adapt only
        // while the reading is still NEAR the baseline, and slowly enough to
        // be thermal (~8 min). Held on a slope the departure is far outside
        // the band, the baseline stays put, and the target holds for as long
        // as the tilt does. A robot that RESTS on a slope still calibrates to
        // it — that slope is its rest pose, which is the correct answer.
        if (quiet && fabsf(d.accel.x - _baseX) < DRIFT_BAND_G)
            _baseX += DRIFT_ALPHA * (d.accel.x - _baseX);
        if (quiet && fabsf(d.accel.y - _baseY) < DRIFT_BAND_G)
            _baseY += DRIFT_ALPHA * (d.accel.y - _baseY);

        // ---- Tilt (VOR balance target), gaze units [-1..1] ----------------
        // COUNTER-ROTATION on both axes, which is what the otolith organs do
        // and what this class's own documentation claims: the gaze goes to the
        // HIGH side, so the eyes stay level with the world rather than riding
        // the robot. The two axes used to disagree — X counter-rotated while Y
        // followed the fall — and nothing caught it because the physical
        // direction of these two axes had never been read. `led-fluid` read it
        // by watching which way a liquid falls (2026-08-22, hardware/
        // PERIPHERALS.md): +X is the observer's right, +Y the top of the
        // screen, so gravity in screen pixels is (-accel.x, +accel.y).
        //
        // Tipped BACK, accel.y falls below its baseline and the gaze now goes
        // DOWN with it, the way an eye does when a head tips back. It used to
        // go up.
        _tilt.x = clampVal((d.accel.x - _baseX) * TILT_SENS_X, -1.0f, 1.0f);
        _tilt.y = clampVal((d.accel.y - _baseY) * TILT_SENS_Y, -1.0f, 1.0f);
    }

    Vec2f headVelDegS() const { return _headVel; }  // gyro, screen axes
    Vec2f tiltG()       const { return _tilt; }     // accel, gaze units
    float accelMagG()   const { return _accelMag; } // |accel| (pickup §2.2)
    float gyroMagDegS() const { return _gyroMag; }  // |gyro| 3 axes (shake)
    // Raw sensor gyro (°/s) — calibration telemetry (PLAYBOOK-HW §1)
    struct Raw3 { float x, y, z; };
    Raw3  gyroRawDegS()  const { return _gyroRaw; }
    Raw3  accelRawG()    const { return _accelRaw; }  // raw accel (telemetry)
    Raw3  magRawUT()     const { return _magRaw; }    // BMM150 magneto (µT)
    // Magnetic heading 0..360° (-1 if no magnetometer / null field). NOT
    // tilt-compensated: indicative only — the VOR fusion consuming it stays
    // gated off (`vor_mag_alpha` = 0) until the HW calibration session.
    float headingDeg()   const { return _heading; }
    // Rest-pose calibration state, published by /api/sensors. `isCalibrated`
    // says the baseline is in use; `calibrationFellBack` says it was taken on
    // the deadline instead of on quiet ticks — the reading is then a guess made
    // while the robot was moving, and a gaze that sits off-centre has its
    // explanation here rather than in the VOR.
    bool  isCalibrated()       const { return _calDone; }
    bool  calibrationFellBack() const { return _calFellBack; }

    // ---- Double-tap (2026-07-17) — the BMI270 through M5Unified does NOT
    // expose the hardware tap interrupt (config.inl maps no tap-engine
    // feature): SOFTWARE detection from a brief |accel| peak (clearly
    // distinct from the ShakeDetector shakes — shorter, sharper, and
    // consumed once). Thresholds NOT EXPOSED as tuning keys (exploratory
    // feature, to be validated in the field before promoting them to
    // parameters, §REFLEX RULE).
    bool consumeDoubleTap() {
        bool v = _doubleTap;
        _doubleTap = false;
        return v;
    }

    // ---- Face up/down orientation (2026-07-17) — RAW accel.z (the axis
    // normal to the screen, cf. CONVENTIONS §3: gyro.z = roll). Sign
    // VALIDATED ON HW (user 2026-07-17: face-down gesture → Sleepy confirmed
    // working).
    enum class FaceOrient : uint8_t { Normal, FaceUp, FaceDown };
    FaceOrient faceOrient() const { return _faceOrient; }

private:
    static constexpr int CAL_TICKS = 100;   // ~1 s of QUIET ticks at 100 Hz
    // The deadline, in ticks SEEN: ~20 s at 100 Hz. Long enough that a robot
    // being carried to its shelf still calibrates properly once it is put down,
    // short enough that a unit which can never satisfy `quiet` is not left
    // without a balance channel for the session.
    static constexpr int CAL_DEADLINE_TICKS = 2000;
    // Drift tracking: a band around the rest pose, and a thermal time
    // constant. 0.00002 at 100 Hz is ~500 s — slow enough to be what it says
    // it is. The band is what stops a held tilt from becoming the new zero:
    // 0.05 g is about 3 degrees, well under any tilt a person means.
    static constexpr float DRIFT_ALPHA  = 0.00002f;
    static constexpr float DRIFT_BAND_G = 0.05f;
    // Tilt→gaze sensitivities (1 g ≈ 0.7)
    static constexpr float TILT_SENS_X = 0.67f;
    static constexpr float TILT_SENS_Y = 0.86f;
    // Double-tap: brief peak above this threshold, re-armed after a
    // refractory delay (mechanical ringing of the impact); a 2nd peak inside
    // the window = tap
    static constexpr float    TAP_THR_G          = 1.6f;
    static constexpr uint32_t TAP_REFRACTORY_MS  = 80;
    static constexpr uint32_t TAP_WINDOW_MS      = 500;
    // Orientation: simple hysteresis (avoids Normal/Face* flicker at the edge)
    static constexpr float FACE_THR_G = 0.75f;

    static int clampAxis(float v) {
        int a = (int)v;
        return a < 0 ? 0 : (a > 2 ? 2 : a);
    }

    const Tuning& _tuning;
    Vec2f _headVel{};
    Vec2f _tilt{};
    Raw3  _gyroRaw{};
    Raw3  _accelRaw{};
    Raw3  _magRaw{};
    float _heading  = -1.0f;   // magnetic heading (-1 = no fix)
    float _baseX    = 0.0f;
    float _baseY    = 0.0f;
    float _calSumX  = 0.0f;
    float _calSumY  = 0.0f;
    float _anySumX  = 0.0f;    // every tick seen, for the deadline fallback
    float _anySumY  = 0.0f;
    int   _calTicks = 0;       // QUIET ticks accumulated
    int   _calSeen  = 0;       // ticks seen, quiet or not
    bool  _calDone  = false;
    bool  _calFellBack = false;
    float _accelMag = 1.0f;
    float _gyroMag  = 0.0f;
    uint32_t    _lastTapMs   = 0;
    bool        _doubleTap   = false;
    FaceOrient  _faceOrient  = FaceOrient::Normal;
};

} // namespace sce
