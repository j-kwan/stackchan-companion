#pragma once
// =============================================================================
// ServoMotion.h — StackChan-Companion (behavior, #ifdef SCE_USE_SERVO)
// =============================================================================
// Servo trajectory generator (ROADMAP §3.4) — replaces a plain fire-and-forget
// moveXY:
//
//   - 50 Hz task (core 0, prio 3 — map §3.5): ease-in-out profile from the
//     current pose to the target, positions written EVERY tick as raw commands
//     (WritePos, time=period). WARNING: StackchanSERVO::moveXY(x,y,ms) is
//     BLOCKING for SCS servos (internal loop with vTaskDelay — read in the
//     library source): unusable for trajectories → direct bus access through
//     inheritance instead.
//   - cmdVelDegS(): COMMANDED angular velocity in screen axes — the VOR's
//     efference copy (§2.2). Mapping VALIDATED ON HARDWARE (PLAYBOOK-HW §1.7):
//     +x = head toward the observer's right (yaw increasing), +y = head tilting
//     up (pitch decreasing) — consistent with ImuReader.
//   - Torque: releaseTorque()/engageTorque() are thread-safe (pickup "dangling
//     feet" pose, optional auto-release after idling).
//
// Thread-safety: the Brain (core 1) writes targets into 32-bit atomics; the
// servo task (core 0) consumes them. No lock.
//
// Safety: every target is clamped to the K151 limits (Units.h) — yaw 166±130°,
// pitch 19..99° (official spec 5~85°: never sit on the end stops).
// =============================================================================
#ifdef SCE_USE_SERVO

#include <Arduino.h>
#include <atomic>
#include <Stackchan_servo.h>
#include "../engine/Clock.h"
#include "../engine/Units.h"
#include "../engine/Tuning.h"

namespace sce {

// Subclass exposing the protected SCS bus (torque + raw position write).
// K151 IDs: X=1, Y=2.
class RawScsServo : public StackchanSERVO {
public:
    void torque(bool on) {
        _sc.EnableTorque(1, on ? 1 : 0);
        _sc.EnableTorque(2, on ? 1 : 0);
    }
    // deg 0-300 → SCS0009 position (inverted mapping: map(0,300 → 1023,0))
    void writeDeg(uint8_t id, float deg, uint16_t timeMs) {
        if (deg < 0) deg = 0; if (deg > 300) deg = 300;
        uint16_t pos = (uint16_t)(1023.0f - deg * (1023.0f / 300.0f));
        _sc.WritePos(id, pos, timeMs);
    }
    // THE BUS IS WRITE-ONLY IN OPERATION. Reading the SCS0009 position over the
    // SHARED half-duplex serial bus corrupts the WritePos commands that follow
    // (servos go mute) and makes the eyes jitter through the VOR efference copy
    // — tried 2026-07-21, REVERTED. Nothing in the control path may call this.
    //
    // The ONE exception, added 08-02 for a measurement session: when the servos
    // are DISABLED (`tuning.servos = 0`) the motion task issues no WritePos at
    // all — `_curYaw` is frozen, the efference copy is zero, the bus is idle.
    // A one-shot read then has nothing to corrupt, and it is the only way to
    // learn where the head ACTUALLY is: the firmware otherwise reports the
    // COMMANDED pose and can never confirm the servo arrived.
    // Called ONLY from the servo task itself (see measuredPose): two tasks
    // cannot share a half-duplex bus by both agreeing to be careful.
    // Returns -1 if the servo does not answer.
    float readDeg(uint8_t id) {
        const int pos = _sc.ReadPos(id);
        if (pos < 0) return -1.0f;
        return (1023.0f - (float)pos) * (300.0f / 1023.0f);   // inverse writeDeg
    }
};

class ServoMotion {
public:
    // tuning: servo_idle_release_ms (P4c — auto-release after idling,
    // 0 = disabled; torque is re-engaged automatically on the next moveTo)
    ServoMotion(const Clock& clock, const Tuning& tuning)
        : _clock(clock), _tuning(tuning) {}

    // ------------------------------------------------------------------
    // Init: VM_EN must already be on (Board::begin did it). Blocks ~600 ms
    // (library begin + moving into position) — call from setup().
    // ------------------------------------------------------------------
    bool begin(int startYaw = units::YAW_CENTER,
               int startPitch = units::PITCH_NEUTRAL) {
        _servo.begin(SERVO_PIN_X, startYaw, 0,
                     SERVO_PIN_Y, startPitch, 0, ServoType::SCS);
        delay(300);   // let the servo reach its initial position

        // WAKE UP LIMP, THEN STAND UP SLOWLY (user 08-03: "released motors,
        // sleepy, then a slow move home, before a normal return").
        //
        // The head is left wherever the idle release dropped it, which is
        // usually sagged onto the LOWER STOP - nothing holds it once the torque
        // is gone. Every companion boot then re-engaged torque against that
        // stop and pushed: the bang the user hears. It is never a guest bin,
        // because a guest writes nothing to this bus.
        //
        // Two things fix it, and both matter:
        //   1. TORQUE OFF the instant the bus exists. The library's attach has
        //      already written its start position by then, but a servo with no
        //      torque cannot fight anything, so the command lands and nothing
        //      forces.
        //   2. START FROM WHERE THE HEAD ACTUALLY IS. `_curYaw`/`_curPitch`
        //      used to be ASSUMED equal to the start values; every later
        //      trajectory therefore interpolated from a fiction, and the first
        //      one jumped the difference. `readDeg` is legal exactly here - the
        //      motion task is not running yet, so the half-duplex bus is idle
        //      and there is nothing to corrupt (see the note on readDeg).
        // The caller then raises the head with `homeSlowly()`.
        // Torque off + measurement now live in settleAfterPower(): the rail is
        // DOWN while this runs, so nothing here can talk to a servo.
        return true;
    }

    // CALLED ONCE THE RAIL IS BACK UP, and it must be the first thing that
    // happens on the live bus: release the torque, then learn where the head
    // actually is. Both were inside begin() until 08-03, where they ran against
    // an unpowered servo and did nothing at all.
    void settleAfterPower() {
        // THE SERVO BOOTS SLOWER THAN THE RAIL. `enableServoPower()` already
        // waits 300 ms for the supply to settle, but the SCS0009 needs its own
        // time before it answers on the bus: at 120 ms the read came back
        // empty and the code silently fell back to the ASSUMED pose - which is
        // exactly the fiction this whole change exists to remove, reinstated
        // by a timeout (measured 08-03, "lecture KO" on target).
        // RETRIED rather than lengthened blindly: three attempts 150 ms apart
        // cost nothing when the first works, and the failure is still NAMED if
        // all three miss.
        delay(150);
        _servo.torque(false);
        _torqueOn.store(false);
        _torqueReq.store(0);
        float my = -1.0f, mp = -1.0f;
        for (int i = 0; i < 3 && (my < 0.0f || mp < 0.0f); i++) {
            if (i) delay(150);
            if (my < 0.0f) my = _servo.readDeg(1);
            if (mp < 0.0f) mp = _servo.readDeg(2);
        }
        const int startYaw = units::YAW_CENTER, startPitch = units::PITCH_NEUTRAL;
        _poseKnown.store(my >= 0.0f && mp >= 0.0f);
        _curYaw   = (my >= 0.0f) ? my : (float)startYaw;
        _curPitch = (mp >= 0.0f) ? mp : (float)startPitch;
        // The TARGETS follow the measurement too, or the first tick would
        // command a jump to a pose nobody asked for.
        _tgtYaw.store(_curYaw);
        _tgtPitch.store(_curPitch);
        // BOTH published atomics, not just yaw (review 08-04): pitchDeg() read
        // between here and the task's first tick paired the measured yaw with
        // the constructor's pitch — the assumed-pose fiction, one field wide.
        _curYawAtomic.store(_curYaw);
        _curPitchAtomic.store(_curPitch);
        Serial.printf("[servo] reveil : couple relache, pose mesuree %.0f/%.0f%s\n",
                      (double)_curYaw, (double)_curPitch,
                      (my < 0.0f || mp < 0.0f) ? " (lecture KO, valeurs par defaut)" : "");
    }

    // THE SLOW STAND-UP. Engages the torque AT the measured pose - so nothing
    // moves at that instant - then walks the target to HOME over `ms`, which
    // the trajectory task turns into a ramp. Non-blocking: the caller shows a
    // Sleepy face while it happens and normal behaviour resumes after.
    // Through moveTo, NOT raw target stores (review 08-03): the task only
    // starts a trajectory when _tgtSeq changes, so bare stores of the targets
    // were never picked up - the "slow" rise did not exist, and the head only
    // moved because the Brain's next SetEmotion issued its own 700 ms moveTo.
    // BLIND boot (review 08-04): when the wake measurement failed, the
    // software ramp is a lie — it interpolates from an ASSUMED pose, and
    // assuming HOME degenerates the rise into one full-speed write while the
    // head lies on the stop. The task then delegates the ramp to the SERVO
    // (WritePos with a time argument interpolates from its REAL position).
    void homeSlowly(uint32_t ms = 1800) {
        if (!_poseKnown.load()) _blindHomeMs.store(ms);
        engageTorque();
        moveTo((float)units::YAW_CENTER, (float)units::PITCH_NEUTRAL, ms);
    }

    // Trajectory task — core 0 prio 3 (map §3.5)
    void start(uint32_t stack = 4096, UBaseType_t prio = 3, BaseType_t core = 0) {
        xTaskCreatePinnedToCore(taskEntry, "servo", stack, this, prio, &_task, core);
    }

    // Remaining stack headroom (words) — P7 endurance runs
    uint32_t stackFreeWords() const {
        return _task ? (uint32_t)uxTaskGetStackHighWaterMark(_task) : 0;
    }

    // ------------------------------------------------------------------
    // Brain-facing API (thread-safe, non blocking)
    // ------------------------------------------------------------------
    void moveTo(float yawDeg, float pitchDeg, uint32_t durMs) {
        _tgtYaw.store(clampVal(yawDeg,
                               (float)(units::YAW_CENTER - units::YAW_RANGE),
                               (float)(units::YAW_CENTER + units::YAW_RANGE)));
        _tgtPitch.store(clampVal(pitchDeg, (float)units::PITCH_MIN,
                                            (float)units::PITCH_MAX));
        _tgtDurMs.store(durMs ? durMs : 1);
        _tgtSeq.fetch_add(1);   // publish — picked up by the task
    }

    void releaseTorque() { _torqueReq.store(0); }
    void engageTorque()  { _torqueReq.store(1); }

    // Hand-over that did not happen (launcher dismissed, flash refused): the
    // head sagged while limp, so re-engaging on the commanded pose would jerk.
    // Asks the motion task — the bus's only owner while it runs — to RE-LEARN
    // the pose, then re-engage the torque from it. One-shot, non-blocking.
    void rebaseAndEngage() { _rebaseReq.store(true); }

    bool torqueReleased() const { return !_torqueOn.load(); }
    // Trajectory in flight — selfMotion guard of the VOR v3 (Brain)
    bool isMoving()       const { return _movingAtomic.load(); }
    float yawDeg()   const { return _curYawAtomic.load(); }
    float pitchDeg() const { return _curPitchAtomic.load(); }

    // MEASURED pose, straight off the servos — as opposed to yawDeg()/pitchDeg()
    // above, which report what was COMMANDED and can never confirm the servo
    // arrived. Diagnostic ONLY (08-02).
    //
    // THE READ ITSELF HAPPENS ON THE SERVO TASK, never here. This accessor only
    // publishes what that task last measured.
    // The first version did the read inline, from loop(), guarded by
    // `tuning.servos == 0`. That guard is not enough and the gap is the exact
    // failure it exists to prevent (review 08-02): between the test and the
    // serial transaction, a `POST /api/tuning?servos=1` lets the servo task
    // resume its WritePos stream, and a read interleaved with it is what left
    // the servos mute on 2026-07-21. Two tasks cannot share a half-duplex bus
    // by both agreeing to be careful — the one that OWNS it does the reading.
    //
    // Returns false while no measurement is available: servos enabled (the task
    // does not sample then), or no servo answered yet.
    // ON DEMAND. Ask here, read the answer on a later call — the sampling
    // itself happens on the servo task (see run()).
    // NOT free-running, and that is the correction of a real hazard rather
    // than a preference (review 08-02): `SCSerial::readSCS` is a `while(1)`
    // busy-wait with NO yield, `IOTimeOut = 100 ms`, and its timer RESTARTS on
    // every byte received. A servo that does not answer — no servos attached,
    // VM_EN never raised, wrong ID, a noisy line — costs 100 ms of pure spin
    // PER READ, so 200 ms per sample, at priority 3 on core 0. Sampling that
    // every 200 ms for as long as `servos = 0` (a perfectly normal persisted
    // state: it is what leaves the head limp) is a permanent hog that starves
    // the camera task and IDLE0 — the family of the recorded
    // "ArduinoJson starves IDLE0 -> watchdog -> boot-loop".
    // On demand, the cost is bounded to someone actually looking.
    void requestPose() { _measReq.store(true); }

    bool measuredPose(float* yawOut, float* pitchOut, uint32_t* stampMs) const {
        // Angles BEFORE the stamp — the inverse of the publication order, so
        // the flag that validates the data is loaded last (review 08-02).
        // Re-checked after, seqlock-style: if the stamp moved between the two
        // loads the pair may straddle two samples, so we decline rather than
        // report a yaw from one and a pitch from the next.
        const uint32_t before = _measMs.load();
        if (!before) return false;
        const float y = _measYaw.load();
        const float p = _measPitch.load();
        if (_measMs.load() != before) return false;
        if (yawOut)   *yawOut   = y;
        if (pitchOut) *pitchOut = p;
        if (stampMs)  *stampMs  = before;
        return true;
    }

    // Efference copy (°/s, screen axes — see header). Read by the Brain.
    Vec2f cmdVelDegS() const {
        return { _cmdVelX.load(), _cmdVelY.load() };
    }

private:
    // K151 Serial2 pins (from the official firmware source)
    static constexpr int SERVO_PIN_X = 7;   // rx (G7 = Servo_TX K151)
    static constexpr int SERVO_PIN_Y = 6;   // tx (G6 = Servo_RX K151)
    static constexpr uint32_t PERIOD_MS = 20;   // 50 Hz

    const Clock&  _clock;
    const Tuning& _tuning;
    RawScsServo   _servo;
    TaskHandle_t  _task = nullptr;
    uint32_t      _lastActivityMs = 0;   // last motion (P4c auto-release)

    // Targets published by the Brain (atomics — no strongly consistent pair:
    // a target read half-updated is corrected on the next tick)
    std::atomic<float>    _tgtYaw{(float)units::YAW_CENTER};
    std::atomic<float>    _tgtPitch{(float)units::PITCH_NEUTRAL};
    std::atomic<uint32_t> _tgtDurMs{1};
    std::atomic<uint32_t> _tgtSeq{0};
    std::atomic<int>      _torqueReq{1};   // 1 = engaged
    std::atomic<bool>     _torqueOn{true};
    std::atomic<bool>     _rebaseReq{false}; // one-shot: re-measure then engage
    std::atomic<bool>     _poseKnown{false}; // settleAfterPower read succeeded
    std::atomic<uint32_t> _blindHomeMs{0};   // one-shot: servo-side slow rise

    // Outputs (read by the Brain)
    std::atomic<float> _curYawAtomic{(float)units::YAW_CENTER};
    std::atomic<float> _curPitchAtomic{(float)units::PITCH_NEUTRAL};
    std::atomic<float> _cmdVelX{0.0f}, _cmdVelY{0.0f};
    std::atomic<bool>  _movingAtomic{false};

    // Task-internal state
    float    _curYaw = 0, _curPitch = 0;     // current commanded pose
    // `_trajFrom*` IS the trajectory start pose. A pair `_cmdYaw`/`_cmdPitch`
    // used to sit here claiming that role: written once in begin(), read
    // nowhere, and its comment sent the reader looking for a mechanism that
    // lives in `_trajFrom*` instead. Removed (review 2026-07-30).
    float    _trajFromYaw = 0, _trajFromPitch = 0;
    float    _trajToYaw = 0, _trajToPitch = 0;
    uint32_t _trajStartMs = 0, _trajDurMs = 0;
    uint32_t _seenSeq = 0;
    uint32_t _blindUntilMs = 0;   // servo-side ramp in flight (blind stand-up)
    bool     _trajActive   = false;
    bool     _autoReleased = false;   // release caused by idling (≠ pickup)
    // Measured pose (diagnostic). Written by the task, read by loop()/the web
    // callback — hence atomics rather than a plain struct: a torn read of a
    // three-field pose would pair one axis with the other axis' timestamp.
    std::atomic<float>    _measYaw{-1.0f}, _measPitch{-1.0f};
    std::atomic<uint32_t> _measMs{0};       // 0 = nothing to report
    std::atomic<bool>     _measReq{false};  // one-shot request from a reader
    uint32_t              _measFailMs = 0;  // last failed read (task-local)
    // A servo that does not answer costs ~100 ms of busy-wait per read. After
    // a failure we stop trying for a while rather than let every poll pay it.
    static constexpr uint32_t FAIL_COOLDOWN_MS = 5000;

    static void taskEntry(void* self) { ((ServoMotion*)self)->run(); }

    void run() {
        TickType_t lastWake = xTaskGetTickCount();
        const TickType_t period = pdMS_TO_TICKS(PERIOD_MS);

        for (;;) {
            uint32_t now = _clock.ms();

            // ---- Servos disabled (Tuning.servos = 0 option) ----
            // Torque RELEASED (limp head, movable by hand, zero servo current)
            // through the torque mechanism below, plus no bus write at all →
            // no head motion (VOR/dances/head-follow all inert).
            // Re-enabling: torque is re-engaged, and the off→on torque
            // transition rebases the running trajectory (smooth resume, the
            // head returns to its commanded pose). Eyes animate as usual.
            bool servosOff = _tuning.servos < 0.5f;

            // ---- Torque (forced RELEASED when servos are disabled) ----
            // NOTE: NO ReadPos re-read on re-engagement (tried 07-21,
            // REVERTED) — reading the SCS0009 position over the SHARED
            // half-duplex serial bus corrupts the following WritePos commands
            // (servos stop responding) AND, since the trajectory keeps running
            // in software, the efference copy makes the eyes jitter through the
            // VOR. We rebase on the COMMANDED pose instead: worst case a small
            // jump if the head was moved by hand during a release — acceptable
            // in exchange for a reliable bus.
            bool wantTorque = (_torqueReq.load() != 0) && !servosOff;
            if (wantTorque != _torqueOn.load()) {
                _servo.torque(wantTorque);
                _torqueOn.store(wantTorque);
                if (wantTorque && _trajActive) {
                    // REBASE the running trajectory onto the commanded pose
                    // instead of CANCELLING it.
                    // Fix 2026-07-17: the auto re-engagement triggered BY a
                    // moveTo used to kill that very moveTo — the last keyframe
                    // of a dance following an auto-release (long hold, e.g.
                    // cry 3 s with a short release delay) was lost and the head
                    // stayed frozen away from home.
                    _trajFromYaw   = _curYaw;
                    _trajFromPitch = _curPitch;
                    _trajStartMs   = now;
                }
            }

            // ---- ONE-SHOT REBASE + RE-ENGAGE (hand-over that did not happen)
            // The head sagged while the torque was released (launcher browsed,
            // flash refused); re-engaging on the COMMANDED pose would throw the
            // first trajectory across the gap in one write — the boot bang,
            // reproduced on every return path (review 08-04). So the pose is
            // RE-LEARNED here, on the one task that owns the bus, before the
            // torque request is honoured. A stale trajectory is DROPPED rather
            // than resumed: it was computed from the fiction being corrected.
            // Retried like settleAfterPower(): a servo freshly re-powered
            // answers late (measured 08-03), and a silent fallback to the
            // assumed pose would reinstate the exact fiction this removes.
            if (_rebaseReq.exchange(false)) {
                if (!_torqueOn.load()) {
                    float my = -1.0f, mp = -1.0f;
                    for (int i = 0; i < 3 && (my < 0.0f || mp < 0.0f); i++) {
                        if (i) vTaskDelay(pdMS_TO_TICKS(150));
                        if (my < 0.0f) my = _servo.readDeg(1);
                        if (mp < 0.0f) mp = _servo.readDeg(2);
                    }
                    if (my >= 0.0f) _curYaw   = my;
                    if (mp >= 0.0f) _curPitch = mp;
                    if (_trajActive) {
                        // An in-flight move is RE-AIMED from the true pose,
                        // not dropped — same shape as the off→on torque
                        // rebase below: the DESTINATION was legitimate, only
                        // the origin was fiction. Fresh clock, not `now`: the
                        // read retries above can hold this block ~450 ms, and
                        // a stale start would fast-forward the trajectory.
                        _trajFromYaw   = _curYaw;
                        _trajFromPitch = _curPitch;
                        _trajStartMs   = _clock.ms();
                    } else {
                        // Nothing pending: pin the targets on the measured
                        // pose so the first tick commands no jump. A moveTo
                        // not yet picked up keeps its own seq and starts,
                        // next iteration, from the corrected _curYaw.
                        _tgtYaw.store(_curYaw);
                        _tgtPitch.store(_curPitch);
                    }
                    _curYawAtomic.store(_curYaw);
                    _curPitchAtomic.store(_curPitch);
                    Serial.printf("[servo] rebase : pose mesuree %.0f/%.0f%s\n",
                                  (double)_curYaw, (double)_curPitch,
                                  (my < 0.0f || mp < 0.0f)
                                      ? " (lecture KO, pose commandee gardee)" : "");
                }
                _torqueReq.store(1);
                _autoReleased = false;
            }

            // ---- MEASURED pose, 5 Hz, ONLY while the servos are off ----
            // Here and nowhere else: this is the one context that OWNS the bus
            // and knows it is issuing no WritePos this tick. `servosOff` was
            // read once at the top of the tick, so the decision cannot change
            // underneath the transaction.
            // The stamp is CLEARED as soon as the servos come back, so the
            // diagnostic endpoint reports "no measurement" instead of a value
            // that silently ages while the head moves.
            if (servosOff) {
                // Requested, not scheduled — and refused during the cooldown a
                // FAILED read leaves behind. A silent servo costs ~100 ms of
                // busy-wait per read (readSCS has no yield); answering every
                // request would let a poller hold core 0 indefinitely on a
                // robot whose bus never replies.
                if (_measReq.load() &&
                    (!_measFailMs || now - _measFailMs >= FAIL_COOLDOWN_MS)) {
                    _measReq.store(false);
                    const float my = _servo.readDeg(1);
                    const float mp = _servo.readDeg(2);
                    // Stamped AFTER the reads: stamping before made the pacing
                    // start at the beginning of a transaction that can last
                    // longer than the interval, so the next one was always
                    // already due.
                    if (my >= 0 && mp >= 0) {
                        _measYaw.store(my);
                        _measPitch.store(mp);
                        _measMs.store(_clock.ms() ? _clock.ms() : 1);
                        _measFailMs = 0;
                    } else {
                        _measFailMs = _clock.ms() ? _clock.ms() : 1;
                    }
                }
            } else if (_measMs.load()) {
                _measReq.store(false);
                _measMs.store(0);
            }

            // ---- New target published? ----
            uint32_t seq = _tgtSeq.load();
            if (seq != _seenSeq) {
                _seenSeq       = seq;
                _trajFromYaw   = _curYaw;
                _trajFromPitch = _curPitch;
                _trajToYaw     = _tgtYaw.load();
                _trajToPitch   = _tgtPitch.load();
                _trajStartMs   = now;
                _trajDurMs     = _tgtDurMs.load();
                _trajActive    = true;
                _lastActivityMs = now;
                // BLIND STAND-UP: pose unknown → the ramp belongs to the
                // SERVO, whose WritePos-with-time interpolates from its REAL
                // position — the one thing the software ramp cannot know.
                // One write, trajectory closed; the efference copy stays zero
                // (eyes are shut for the whole rise, nothing consumes it).
                // Torque is already on this tick: the torque section above
                // ran before this one, and homeSlowly() requested both.
                uint32_t blind = _blindHomeMs.exchange(0);
                if (blind && _torqueOn.load()) {
                    _servo.writeDeg(1, _trajToYaw,   (uint16_t)blind);
                    _servo.writeDeg(2, _trajToPitch, (uint16_t)blind);
                    _curYaw     = _trajToYaw;
                    _curPitch   = _trajToPitch;
                    _trajActive = false;
                    // The head IS moving for the whole servo-side ramp, even
                    // though no software trajectory runs: isMoving() must say
                    // so, both for the wake sequence that waits on it and for
                    // the VOR's selfMotion guard.
                    _blindUntilMs = _clock.ms() + blind;
                }
                // Auto re-engagement (P4c): a motion was requested while the
                // torque had been dropped by the auto-release. (The DELIBERATE
                // pickup release is not affected: no moveTo is issued while the
                // robot is being carried — Brain-side guards.)
                if (!_torqueOn.load() && _torqueReq.load() == 0 && _autoReleased) {
                    _torqueReq.store(1);
                    _autoReleased = false;
                }
            }

            // ---- Auto-release (P4c): torque dropped after idling ----
            uint32_t idleMs = (uint32_t)_tuning.servo_idle_release_ms;
            if (idleMs > 0 && _torqueOn.load() && !_trajActive &&
                now - _lastActivityMs > idleMs) {
                _torqueReq.store(0);
                _autoReleased = true;
            }

            // ---- Ease-in-out trajectory + bus write ----
            // (torque is released when servos are disabled → section skipped:
            //  no write, _curYaw frozen, efference copy zero)
            float prevYaw = _curYaw, prevPitch = _curPitch;
            if (_trajActive && _torqueOn.load()) {
                float t = (float)(now - _trajStartMs) / (float)_trajDurMs;
                if (t >= 1.0f) {
                    t = 1.0f;
                    _trajActive = false;
                    // IDLE STARTS WHEN MOTION ENDS, not when it begins. The
                    // stamp was posted at the START of the trajectory, which
                    // lasts up to 900 ms, so "release after X ms of idle"
                    // actually released X minus the travel time. Harmless at
                    // 15 s, a fifth of the window at 4 s (08-02).
                    _lastActivityMs = now;
                }
                float s = t * t * (3.0f - 2.0f * t);
                _curYaw   = _trajFromYaw   + (_trajToYaw   - _trajFromYaw)   * s;
                _curPitch = _trajFromPitch + (_trajToPitch - _trajFromPitch) * s;
                _servo.writeDeg(1, _curYaw,   PERIOD_MS);
                _servo.writeDeg(2, _curPitch, PERIOD_MS);
            }

            // ---- Efference copy: commanded velocity in screen axes ----
            // +x = observer's right = yaw increasing; +y = up = pitch
            // decreasing. This mapping is VALIDATED ON HARDWARE (§1.7,
            // 2026-07-11) — the "provisional, pending calibration" note that sat
            // here contradicted the file header, which had it right.
            float dtS = PERIOD_MS / 1000.0f;
            _cmdVelX.store((_curYaw   - prevYaw)   / dtS);
            _cmdVelY.store(-(_curPitch - prevPitch) / dtS);
            _curYawAtomic.store(_curYaw);
            _curPitchAtomic.store(_curPitch);
            // Torque off (servos=0/release) = NO bus write = no self-motion:
            // publishing true with a frozen trajectory used to inhibit shake
            // detection (VOR selfMotion) → the Scared reflex was DEAD as long
            // as servos=0 (rule 5, review 07-21).
            _movingAtomic.store((_trajActive || now < _blindUntilMs) &&
                                _torqueOn.load());

            // Starvation guard (rule §3.5)
            TickType_t before = xTaskGetTickCount();
            vTaskDelayUntil(&lastWake, period);
            if (xTaskGetTickCount() == before) vTaskDelay(1);
        }
    }
};

} // namespace sce

#endif // SCE_USE_SERVO
