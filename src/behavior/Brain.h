#pragma once
// =============================================================================
// Brain.h — StackChan-Companion (behavior)
// =============================================================================
// THE behaviour task (ROADMAP §3.5): 100 Hz, core 1, prio 4 — the SOLE writer
// of FaceState. Every external mutation (API, touch, sensors) comes in through
// the CommandQueue (FreeRTOS xQueue): this avoids ad hoc atomics/portMUX
// scattered all over the calling code.
//
// What each tick does, in order:
//   1. drain the CommandQueue (API emotions with a duration, blinks/winks,
//      CRT options, tuning)
//   2. EmotionRoulette (locked out during an override — the tick is still
//      consumed, so the 6-12 s cadence never drifts)
//   3. IdleBehavior: fixation → saccade → fixation (a saccade event drives
//      the blink coupling §3.3; head-follow arrives in P4)
//   4. BlinkController: eyelids (policies + Sleepy "fighting to stay awake")
//   5. squash & stretch (§3.0-2): squeeze/stretch during saccades,
//      proportional to amplitude — procedural, zero keyframes
//   6. breath + snapshot publication (TripleBuffer, never blocking)
//
// Modules 2-4 are PURE (Clock/Rng injected, tested natively); only this class
// touches FreeRTOS.
// =============================================================================

#include <Arduino.h>
#include <atomic>
#include "../engine/Clock.h"
#include "../engine/Rng.h"
#include "../engine/Emotions.h"
#include "../engine/FaceState.h"
#include "../engine/Tuning.h"
#include "Command.h"
#include "IdleBehavior.h"
#include "BlinkController.h"
#include "EmotionRoulette.h"
#include "Personalities.h"   // clampTransitionScale/clampPitchBiasScale/scaledDurationMs
#include "VestibularSystem.h"
#include "PickupDetector.h"
#include "Sequencer.h"
#include "Dances.h"
#include "../engine/Blender.h"
#include "../hal/ImuReader.h"
#include "../hal/I2cBus.h"
#ifdef SCE_USE_SERVO
#include "ServoMotion.h"
#endif

namespace sce {

// Command / CmdType — the one and only mutation channel — were split out into
// Command.h (included above) so PURE modules (RuleEngine) can share them.

class Brain {
public:
    Brain(const Clock& clock, TripleBuffer<FaceState>& bus, Tuning& tuning,
          uint32_t rngSeed)
        : _clock(clock), _bus(bus), _tuning(tuning), _rng(rngSeed),
          _idle(clock, _rng, tuning), _blink(clock, _rng, tuning),
          _roulette(clock, _rng), _vor(clock, tuning),
          _pickup(clock, tuning), _seq(clock), _gazeBlender(clock) {}

    // Optional IMU (P3): the Brain reads the BMI270 from ITS OWN task
    // (M5Unified guards In_I2C with a mutex — coexists with loop()'s
    // M5.update()).
    void attachImu(ImuReader* imu) { _imu = imu; }

#ifdef SCE_USE_SERVO
    // Optional servo (P4): VOR efference copy + head-follow + pickup posture
    void attachServo(ServoMotion* s) { _servo = s; }
#endif

    // ------------------------------------------------------------------
    void begin() { _queue = xQueueCreate(16, sizeof(Command)); }

    void start(uint32_t stack = 6144, UBaseType_t prio = 4, BaseType_t core = 1) {
        xTaskCreatePinnedToCore(taskEntry, "brain", stack, this, prio, &_task, core);
    }

    // Remaining stack headroom (words) — P7 endurance runs
    uint32_t stackFreeWords() const {
        return _task ? (uint32_t)uxTaskGetStackHighWaterMark(_task) : 0;
    }

    // Post a command (thread-safe, from any task).
    // Non blocking: returns false if the queue is full (the caller may retry).
    bool post(const Command& cmd) {
        return _queue && xQueueSend(_queue, &cmd, 0) == pdTRUE;
    }

    // Read side for the app (status bar, status API) — atomic
    eEmotions currentEmotion() const { return (eEmotions)_shownEmotion.load(); }

    // EXCLUSIVE LAUNCHER MODE (user 07-26): suspends ALL behaviour —
    // animations, reflexes, dances, servo, sounds (which are triggered by
    // emotion changes) — while the Launcher owns the screen.
    // Commands received while suspended are DROPPED (the API is deliberately
    // inert in launcher mode). Resuming is done with setSuspended(false) plus
    // a wake-up command posted by main.cpp.
    void setSuspended(bool on) { _suspended.store(on); }
    bool      crtOn()          const { return _crtOnAtomic.load(); }

    // TRANSIENT head-follow hold (review 08-04) — the boot wake used to
    // express "hold the neck while the head stands up" by zeroing
    // `tuning.head_follow` and restoring a saved copy: a durable settings
    // channel carrying a transient need. The window now reaches past
    // webApi->begin(), so a POST /api/tuning?head_follow landing inside it
    // was silently overwritten by the restore. The hold is its OWN bit, on
    // the mechanism that owns the behaviour: the user setting is never
    // touched, and both can be true at once without erasing each other.
    void setHeadFollowHold(bool on) { _headFollowHold.store(on); }

    // ---- Personality dials on FEEL (Personalities.h §transitionScale/
    // pitchBiasScale) — the CLAMP is pure code in Personalities.h, natively
    // tested (`test_personality_feel`), because Brain itself cannot be: its
    // constructor path opens a FreeRTOS queue and pins a task, so no native
    // test can even construct one. This method is a thin caller, not a second
    // place the bound is written. Atomic: written from loop() (main.cpp, on a
    // personality change), read from the Brain's own tick — cross-task like
    // every other dial here (`_suspended`, `_headFollowHold`).
    void setTransitionScale(float s) {
        _transitionScale.store(clampTransitionScale(s));
    }
    void setPitchBiasScale(float s) {
        _pitchBiasScale.store(clampPitchBiasScale(s));
    }

    // Head pose (status API / servo remote control) — 0 if there is no servo
    float headYawDeg() const {
#ifdef SCE_USE_SERVO
        if (_servo) return _servo->yawDeg();
#endif
        return 0.0f;
    }
    float headPitchDeg() const {
#ifdef SCE_USE_SERVO
        if (_servo) return _servo->pitchDeg();
#endif
        return 0.0f;
    }

    // BMM150 magnetic heading (0..360°, -1 if no magnetometer) — telemetry.
    float imuHeadingDeg() const { return _imu ? _imu->headingDeg() : -1.0f; }

    // BMM150 raw field in µT (zeros if no magnetometer) — /api/sensors, so
    // the SENSOR can be judged without opening the serial port (which resets
    // the board over native USB, learned 08-04). Three plain float reads
    // across tasks, same tolerance as the heading above: telemetry, where a
    // torn read costs one odd sample, not a decision.
    ImuReader::Raw3 imuMagUT() const {
        return _imu ? _imu->magRawUT() : ImuReader::Raw3{ 0.0f, 0.0f, 0.0f };
    }

    // Rest-pose calibration state — /api/sensors. 0 = still calibrating (the
    // tilt channel reads zero and the eyes do not answer a slope), 1 = done,
    // 2 = done ON THE DEADLINE, i.e. the baseline was averaged over ticks that
    // were never quiet. The third value is the one worth publishing: without it
    // a robot whose accelerometer cannot satisfy the quiet test is indis-
    // tinguishable from one sitting perfectly level.
    int imuCalState() const {
        if (!_imu || !_imu->isCalibrated()) return 0;
        return _imu->calibrationFellBack() ? 2 : 1;
    }

    // Tuning access (P2b: mapped onto /api/tuning)
    EmotionRoulette& roulette() { return _roulette; }

private:
    const Clock&             _clock;
    TripleBuffer<FaceState>& _bus;
    Tuning&                  _tuning;
    Rng                      _rng;
    IdleBehavior             _idle;
    BlinkController          _blink;
    EmotionRoulette          _roulette;
    VestibularSystem         _vor;
    PickupDetector           _pickup;
    Sequencer                _seq;
    Vec2Blender              _gazeBlender;
    bool                     _danceWasActive = false;
    float                    _danceYawSign   = 1.0f;  // mirror of current dance
    // Target of the current keyframe — the gaze aims at it ahead of the servo
    // ("eyes lead")
    float _danceTgtYaw   = (float)units::YAW_CENTER;
    float _danceTgtPitch = (float)units::PITCH_NEUTRAL;
    ImuReader*               _imu = nullptr;
    uint32_t                 _lastShakeMs = 0;
    QueueHandle_t            _queue = nullptr;
    TaskHandle_t             _task  = nullptr;
#ifdef SCE_USE_SERVO
    ServoMotion*             _servo = nullptr;
    uint32_t                 _pickupReleaseAtMs = 0;
    uint32_t                 _lastHeadFollowMs  = 0;
#endif

    // Emotional state
    eEmotions _emotion       = Normal;
    eEmotions _preOverride   = Normal;   // restored when the override ends
    uint32_t  _overrideUntil = 0;        // 0 = no override (ex-TimedEmotion)
    bool      _asymMirror    = false;    // random mirroring of asymmetries
    float     _pitchBias     = 0.0f;     // per-emotion head posture (deg)
    float     _soundPendingStep = 0.0f;  // turn toward the sound after `shocked`
    uint32_t  _lastSoundEvtMs   = 0;     // last sound event
    uint32_t  _lastServoMoveMs  = 0;     // last servo trajectory in flight
    uint32_t  _lastHeadActivityMs = 0;   // last head activity (home return)
    uint32_t  _faceDownSinceMs  = 0;     // face-down orientation (IMU, sleep gesture)
    bool      _faceDownActive   = false;
    std::atomic<int>  _shownEmotion{Normal};
    std::atomic<bool> _suspended{false};   // exclusive launcher mode (07-26)
    std::atomic<bool> _crtOnAtomic{false};   // CRT state (console toggle)
    std::atomic<bool> _headFollowHold{false}; // boot wake holds the neck (08-04)
    std::atomic<float> _transitionScale{1.0f};  // see setTransitionScale()
    std::atomic<float> _pitchBiasScale{1.0f};   // see setPitchBiasScale()

    // Rendering / options
    TransitionConfig _transition = DefaultTransitions::NORMAL;
    CrtOptions       _crt{};
    uint32_t         _lastTelemetryMs = 0;

    // Squash & stretch: smoothed state (elastic return after the saccade)
    float _squashX = 1.0f, _squashY = 1.0f;

    static void taskEntry(void* self) { ((Brain*)self)->run(); }

    void run() {
        TickType_t lastWake = xTaskGetTickCount();
        const TickType_t period = pdMS_TO_TICKS(10);   // 100 Hz (§3.5)

        for (;;) {
            tick();
            // Starvation guard (P0 lesson) — the tick is short (<1 ms) but the
            // rule is applied systematically
            TickType_t before = xTaskGetTickCount();
            vTaskDelayUntil(&lastWake, period);
            if (xTaskGetTickCount() == before) vTaskDelay(1);
        }
    }

    void tick() {
        uint32_t now = _clock.ms();

        // ---- 0. Suspended (exclusive launcher mode): drain the queue while
        //      DROPPING the commands (so it cannot clog up), no behaviour,
        //      no publication, no servo order ----
        if (_suspended.load()) {
            Command drop;
            while (_queue && xQueueReceive(_queue, &drop, 0) == pdTRUE) {}
            return;
        }

        // ---- 1. Commands ----
        Command cmd;
        while (_queue && xQueueReceive(_queue, &cmd, 0) == pdTRUE) handle(cmd, now);

        // ---- Override expired → restore (ex-TimedEmotion::update) ----
        if (_overrideUntil && now >= _overrideUntil) {
            _overrideUntil = 0;
            applyEmotion(_preOverride, scaledTransition(_preOverride));
        }

        // ---- 2. Roulette (locked out during an override OR a dance) ----
        eEmotions next;
        if (_roulette.update(_emotion, _overrideUntil != 0 || _seq.isActive(),
                             next)) {
            applyEmotion(next, scaledTransition(next));
        }

        // ---- 2b. Sequencer (P4b dances): applies the keyframes ----
        if (const DanceKey* k = _seq.update()) {
            if (k->emotion < EMOTIONS_COUNT) {
                applyEmotion(k->emotion, scaledTransition(k->emotion));
            }
            if      (k->lidEvent == 1) _blink.requestBlink();
            else if (k->lidEvent == 2) _blink.requestWink(true);
            else if (k->lidEvent == 3) _blink.requestWink(false);
            // "Eyes lead, head follows" (user correction 2026-07-11): the gaze
            // aims at the keyframe TARGET — a ~80 ms saccade through the
            // blender — while the head travels there over servoMs
            // (300-700 ms). (Before: the gaze was slaved to the instantaneous
            // servo pose → the eyes ARRIVED with the head instead of leading
            // the movement.)
            _danceTgtYaw   = units::YAW_CENTER    + k->yawOff * _danceYawSign;
            _danceTgtPitch = units::PITCH_NEUTRAL + k->pitchOff;
            _gazeBlender.blendTo(blend::GAZE_MS);
#ifdef SCE_USE_SERVO
            if (_servo) {
                // _danceYawSign: random mirror drawn at PlayDance time for
                // symmetric dances (unpredictability — user 2026-07-11)
                _servo->moveTo(_danceTgtYaw, _danceTgtPitch, k->servoMs);
            }
#endif
        }
        // Dance end (falling edge): hand the gaze back to the idle owner
        if (_danceWasActive && !_seq.isActive()) {
            _gazeBlender.blendTo(blend::GAZE_MS);
            // Sound startle: the `shocked` dance is over (neutral pose) — the
            // head now turns toward the sound source (briskly).
            // Same guards as the SoundDir handler, shake included
            // (reflex rule A2.5 — review 2026-07-17)
            if (_soundPendingStep != 0.0f) {
#ifdef SCE_USE_SERVO
                if (_servo && _tuning.sound_track >= 0.5f &&
                    !_pickup.isLifted() && !_vor.shakeDetected()) {
                    _servo->engageTorque();
                    // Reflex envelope, not the choreography one (Units.h).
                    _servo->moveTo(units::clampReflexYaw(
                                       _servo->yawDeg() + _soundPendingStep),
                                   _servo->pitchDeg(),
                                   (uint32_t)clampVal(
                                       _tuning.soundtrack_move_ms * 0.4f,
                                       100.0f, 2000.0f));
                    _lastSoundEvtMs = now;
                }
#endif
                _soundPendingStep = 0.0f;
            }
        } else if (!_danceWasActive && _seq.isActive()) {
            _gazeBlender.blendTo(blend::GAZE_MS);   // entering a dance: same
        }
        _danceWasActive = _seq.isActive();

        // ---- 3. Idle gaze + saccade→blink coupling ----
        if (_idle.update()) {
            _blink.notifySaccade(_idle.lastSaccadeAmplitude());
        }

        // ---- 3b. VOR (P3/v3): offset ADDED on top of the idle gaze (§3.2) ----
        Vec2f vorOffset{};
        if (_imu) {
            // The 11/12 bus is SHARED (IMU, touch, AXP, camera SCCB, PY32
            // LEDs, Si12T) and is not thread-safe: we serialise the ONE
            // hardware transaction (M5.Imu.update) behind a short mutex with
            // priority inheritance. The Brain no longer skips its read (the
            // old advisory gate froze the VOR for ~1.5 s when the camera was
            // switched on) — at worst it blocks for the duration of a
            // concurrent transaction. _imu->update() only reads buffers that
            // are already filled (no I2C) → kept outside the lock.
            { sce::i2cbus::Guard g; M5.Imu.update(); }
            _imu->update();
            // Efference copy: velocity commanded to the servos (P4)
            Vec2f eff{};
            bool selfMotion = _seq.isActive();
#ifdef SCE_USE_SERVO
            if (_servo) {
                eff = _servo->cmdVelDegS();
                selfMotion = selfMotion || _servo->isMoving();
                // Timestamp of servo motion: gates the sound tracking (servo
                // and gear noise picked up by the mics, 07-25)
                if (_servo->isMoving()) _lastServoMoveMs = now;
            }
#endif
            // The magnetic heading rides along (-1 when no BMM150): the VOR
            // only consumes it when `vor_mag_alpha` is turned on — the yaw
            // observable the accelerometer does not have (08-04).
            if (_vor.update(_imu->headVelDegS(), _imu->tiltG(),
                            _imu->accelMagG(), eff,
                            _imu->gyroMagDegS(), selfMotion,
                            _imu->headingDeg())) {
                _blink.notifySaccade(_vor.lastSaccadeAmplitude());
            }
            vorOffset = _vor.offset();

            // Sustained shake → Scared for 2 s (refreshed at most once per
            // 500 ms). REFLEX RULE (user 2026-07-07): preempts IMMEDIATELY
            // whatever is running — dance aborted, wink/blink cut short, and
            // the Scared override overwrites the API/roulette emotion.
            if (_vor.shakeDetected() && now - _lastShakeMs > 500) {
                _lastShakeMs = now;
                if (_seq.isActive()) abortDance(now);
                _blink.preempt();
                handle({ CmdType::SetEmotion, (int32_t)Scared, 2000 }, now);
            }

            // PickupReaction (§2.2) — detection, facial reaction AND posture.
            // This comment used to announce the posture and the torque release
            // as a P4 TODO, with a pitch of -10°. Both have been implemented
            // for a while, right below, and the angle settled at -14° — the
            // comment was describing a plan the code had already outgrown.
            auto pev = _pickup.update(_imu->accelMagG(), _imu->gyroMagDegS());
            if (pev.lifted) {
                Serial.printf("[brain] souleve -> Curious\n");
                if (_seq.isActive()) abortDance(now);   // reflex rule
                _blink.preempt();
                handle({ CmdType::SetEmotion, (int32_t)Curious, 4000 }, now);
#ifdef SCE_USE_SERVO
                // "Dangling feet" (§2.2): head raised (-14° — user feedback:
                // the previous value was not pronounced enough) then torque
                // released as soon as the posture is reached (250 ms —
                // the sooner, the more "relaxed" it looks)
                if (_servo) {
                    _servo->engageTorque();
                    _servo->moveTo(_servo->yawDeg(), _servo->pitchDeg() - 14.0f, 220);
                    _pickupReleaseAtMs = now + 250;
                }
#endif
            } else if (_pickup.isLifted() && _overrideUntil &&
                       _emotion == Curious) {
                _overrideUntil = now + 1500;   // Curious held while carried
            }
            if (pev.putDown) {
                Serial.printf("[brain] pose -> reprise idle\n");
                _blink.requestBlink();         // "that's settled" blink
                if (_emotion == Curious) _overrideUntil = now;  // release it
#ifdef SCE_USE_SERVO
                if (_servo) {                   // re-engage + settling
                    _pickupReleaseAtMs = 0;
                    _servo->engageTorque();
                    _servo->moveTo(_servo->yawDeg(), units::PITCH_NEUTRAL, 500);
                }
#endif
            }
#ifdef SCE_USE_SERVO
            // Deferred torque release ("dangling feet" posture reached)
            if (_pickupReleaseAtMs && now >= _pickupReleaseAtMs) {
                _pickupReleaseAtMs = 0;
                if (_servo && _pickup.isLifted()) _servo->releaseTorque();
            }
#endif

            // Double-tap (2026-07-17, exploratory sensor use): a decisive pat
            // detected in software (ImuReader) → brief Happy + wink.
            // We ALWAYS consume the flag (otherwise it would fire later), but
            // we IGNORE it if a dance is running or if a sound event just
            // crossed the threshold: a HARD knock on the screen makes both a
            // noise (→ `shocked`) and an accel spike (→ tap) — the sound must
            // win (no Happy on top of shocked). Also guarded by the reflex
            // rule (shake/pickup take priority).
            bool tapped = _imu->consumeDoubleTap();
            if (tapped && !_seq.isActive() &&
                now - _lastSoundEvtMs > 500 &&
                !_pickup.isLifted() && !_vor.shakeDetected()) {
                Serial.printf("[brain] double-tap -> Happy\n");
                handle({ CmdType::SetEmotion, (int32_t)Happy, 3000 }, now);
                _blink.requestWink(true);
                _lastHeadActivityMs = now;   // counts as activity (home timer)
            }

            // Sustained face-down orientation (2026-07-17): laid screen-down
            // for ~1.5 s → Sleepy held (a "put me to sleep" gesture), same
            // idiom as Curious/pickup (override refreshed while the condition
            // holds, released on exit). VALIDATED ON HARDWARE (user
            // 2026-07-17: worked on the first pass).
            bool faceDown = _imu->faceOrient() == ImuReader::FaceOrient::FaceDown;
            if (faceDown && !_pickup.isLifted() && !_vor.shakeDetected()) {
                if (!_faceDownSinceMs) _faceDownSinceMs = now;
                if (!_faceDownActive && now - _faceDownSinceMs > 1500) {
                    _faceDownActive = true;
                    Serial.printf("[brain] face-bas -> Sleepy\n");
                    if (_seq.isActive()) abortDance(now);
                    handle({ CmdType::SetEmotion, (int32_t)Sleepy, 4000 }, now);
                } else if (_faceDownActive) {
                    _overrideUntil = now + 4000;   // held while face-down
                }
            } else {
                _faceDownSinceMs = 0;
                if (_faceDownActive) {
                    _faceDownActive = false;
                    if (_emotion == Sleepy) _overrideUntil = now;   // release
                }
            }
        }

#ifdef SCE_USE_SERVO
        // ---- 3c. Head-follow (§3.0-1): "eyes lead, head follows" ----
        // The head follows an off-centre fixation that is held; the VOR
        // (efference copy) re-centres the eyes during the rotation — the
        // Cozmo loop. ON by default since 2026-07-13 (tuning.head_follow = 1):
        // the initial OFF was only a precaution while the gyro mapping was
        // being calibrated, and has been lifted (efference §1.7 + head-follow
        // 4.2 both validated on hardware).
        if (_servo && _tuning.head_follow >= 0.5f &&
            !_headFollowHold.load() &&   // boot wake: neck held, setting intact
            !_pickup.isLifted() &&
            !_vor.shakeDetected() &&   // reflex rule: no following while shaken
            !_seq.isActive() &&        // the dance owns the head
            now - _lastHeadFollowMs > 2500 &&
            _idle.fixationHeld((uint32_t)_tuning.headfollow_hold_ms, 0.15f)) {
            _lastHeadFollowMs = now;
            Vec2f g = _idle.gaze();
            // + emotional posture bias (head low for Sad/Blush..., high for
            // Smug/Excited) — ServoMotion clamps to [PITCH_MIN, PITCH_MAX]
            _servo->moveTo(units::headYawFromGaze(g.x),
                           units::headPitchFromGaze(g.y) + _pitchBias, 600);
        }
#endif

#ifdef SCE_USE_SERVO
        // ---- 3d. GENERALISED RETURN TO HOME (user 2026-07-17 — replaces the
        // calm-down logic that was specific to sound tracking): after
        // head_home_ms with NO head activity at all (dance, pickup,
        // trajectory in flight: sound tracking, head-follow, remote control,
        // posture nudge) AND no sound event, the head ALWAYS returns to home
        // — the emotional posture is kept. A continuous sound (even straight
        // ahead, causing no movement) holds the pose through _lastSoundEvtMs.
        // Stateless: the return only triggers if the head is NOT already at
        // home, and the return movement itself counts as activity (so it
        // cannot retrigger).
        if (_servo) {
            if (_seq.isActive() || _pickup.isLifted() || _servo->isMoving()) {
                _lastHeadActivityMs = now;
            }
            // Target CLAMPED to the reachable servo range: otherwise a bias
            // beyond PITCH_MAX (e.g. Sleepy/Blush) yields a target that
            // ServoMotion clamps, the rest condition below never converges →
            // the servo is re-commanded in a loop (torque never released).
            float homePitch = clampVal(units::PITCH_NEUTRAL + _pitchBias,
                                       (float)units::PITCH_MIN,
                                       (float)units::PITCH_MAX);
            if (_tuning.head_home_ms >= 1.0f &&
                now - _lastHeadActivityMs > (uint32_t)_tuning.head_home_ms &&
                now - _lastSoundEvtMs     > (uint32_t)_tuning.head_home_ms &&
                (fabsf(_servo->yawDeg()   - units::YAW_CENTER) > 1.5f ||
                 fabsf(_servo->pitchDeg() - homePitch)         > 1.5f)) {
                _servo->engageTorque();
                _servo->moveTo(units::YAW_CENTER, homePitch, 900);
            }
        }
#endif

        // ---- 4. Eyelids ----
        _blink.update();

        // ---- 5. Squash & stretch (§3.0-2): during the saccade, horizontal
        //      stretch + vertical squeeze ∝ amplitude; elastic return
        //      (exponential smoothing) afterwards ----
        float targetX = 1.0f, targetY = 1.0f;
        if (_idle.isSaccading()) {
            float k = _idle.lastSaccadeAmplitude() / units::GAZE_MAX_X;
            if (k > 1.0f) k = 1.0f;
            targetX = 1.0f + 0.10f * k;
            targetY = 1.0f - 0.12f * k;
        }
        _squashX += 0.25f * (targetX - _squashX);   // ~40 ms return at 100 Hz
        _squashY += 0.25f * (targetY - _squashY);

        // ---- 5b. Gaze base: owned by idle OR by the dance (§3.2) ----
        // During a dance: the eyes aim at the TARGET of the current keyframe
        // ("eyes lead, head follows" — rather than plainly tracking the servo
        // pose) + the keyframe's vertical bias (NOD/SHY dip with the eyes).
        // The Vec2Blender saccades to each new target (80 ms) and crossfades
        // the idle↔dance handover (§3.7).
        Vec2f gazeBase = _idle.gaze();
        if (_seq.isActive()) {
            gazeBase = units::gazeFromHead(_danceTgtYaw, _danceTgtPitch);
            gazeBase.y += _seq.gazeYBias();          // keyframe bias (normalised)
        }
        gazeBase = _gazeBlender.track(gazeBase);

        // ---- 6. Publication (tunable parameters read from the Tuning
        //      registry: written live by /api/tuning, atomic field by field) ----
        _crt.glowPx  = _tuning.crt_glow_px;
        _crt.glowDim = _tuning.crt_glow_dim;
        FaceState& fs = _bus.beginWrite();
        fs.emotion    = _emotion;
        fs.transition = _transition;
        fs.asymMirror = _asymMirror;
        // DEAD IS IMMOBILE, and that is the entire reading of the face (user
        // 08-03). Everything that keeps the other twenty-nine alive — the gaze,
        // the VOR that holds it against the head, the 4 s breathing sine, the
        // squash the gaze dynamics leave behind — is stopped here, at the ONE
        // place the face state is published. Not in the Renderer: rules 3 and
        // A2.15 put every continuous channel in the Brain's hands, and a
        // Renderer that decided on its own to ignore a channel for one emotion
        // is exactly the bug those rules exist to prevent.
        //
        // This REVERSES a request recorded in validation/VALIDATION.md ("Dead:
        // the cross follows the gaze (it ignored it completely)", still open).
        // Written down rather than quietly dropped: the earlier reading was that
        // a cross nailed to the centre looked like a rendering fault. The answer
        // now is that it is not a fault, it is the point — dead is not looking
        // at anything. That entry has to be closed as SUPERSEDED, not as done.
        //
        // The HEAD keeps its own choreography (the raised, agonising pitch
        // below): what was asked to stop is the eyes.
        // SLEEPY IS PINNED VERTICALLY, and only vertically (user 08-03). The
        // eyes rest on a line 20 px above the floor of the zone and must STAY
        // there for the whole animation; looking left and right is still
        // allowed, because a sleepy face that cannot move at all is a dead one
        // and this table already has that. So the vertical channels are the
        // ones that stop: the y of the gaze, the breathing, and squashY — the
        // last one because a vertical squash moves both edges even when the
        // centre does not, which is the same visible drift under another name.
        const bool dead        = (_emotion == Dead);
        const bool vStill      = dead || (_emotion == Sleepy);
        Vec2f g = units::clampGaze(gazeBase + vorOffset);
        if (dead)        g = Vec2f{ 0.0f, 0.0f };
        else if (vStill) g.y = 0.0f;
        fs.gaze       = g;
        fs.openL      = _blink.openL();
        fs.openR      = _blink.openR();
        fs.breath     = vStill ? 0.0f : sinf(now * (2.0f * PI / 4000.0f));
        fs.colorDim   = _tuning.eye_color_dim;
        fs.depthScale = _tuning.eye_depth_scale;
        fs.eyeSpacing = _tuning.eye_spacing;
        fs.squashX    = dead   ? 1.0f : _squashX;   // Sleepy keeps its horizontal
        fs.squashY    = vStill ? 1.0f : _squashY;
        fs.crt        = _crt;
        _bus.publish();

        // ---- Telemetry (tuning.telemetry=1): serial-plotter format, 10 Hz.
        //      RAW gX/gY/gZ let the gyro mapping be calibrated WITHOUT a
        //      reflash (POST /api/tuning?gyro_yaw_axis=...) — PLAYBOOK-HW §1.
        if (_tuning.telemetry >= 0.5f && now - _lastTelemetryMs >= 100) {
            _lastTelemetryMs = now;
            Vec2f hv = _imu ? _imu->headVelDegS() : Vec2f{};
            Vec2f tg = _imu ? _imu->tiltG()       : Vec2f{};
            ImuReader::Raw3 gr = _imu ? _imu->gyroRawDegS()  : ImuReader::Raw3{};
            ImuReader::Raw3 ar = _imu ? _imu->accelRawG()    : ImuReader::Raw3{};
            // `hdg` RIDES ALONG (08-04): the vor_mag_alpha calibration recipe
            // is "check that heading and gyro yaw move in the same sense" —
            // which was impossible from this line, the one place the recipe
            // pointed at, until the heading was actually printed (user 08-04:
            // ran the whole procedure and had nothing to compare).
            // `mx/my/mz` (µT) joined the same day: the first capture showed a
            // heading BLIND to a >180° hand rotation (gyro integrated ~40°/s
            // for seconds, hdg pinned at ~150±3), and only the raw field can
            // say which classic failure that is — a body-fixed field from the
            // onboard magnets drowning Earth's ~50 µT (hard-iron, components
            // stable in rotation, norm way above 50) or a magnetometer that
            // M5.Imu never actually samples (components frozen).
            ImuReader::Raw3 mr = _imu ? _imu->magRawUT() : ImuReader::Raw3{};
            Serial.printf("gX:%.1f gY:%.1f gZ:%.1f accZ:%.2f headVelX:%.1f headVelY:%.1f "
                          "vorX:%.3f vorY:%.3f tiltX:%.2f tiltY:%.2f "
                          "gazeX:%.3f gazeY:%.3f openL:%.2f hdg:%.1f "
                          "mx:%.1f my:%.1f mz:%.1f\n",
                          gr.x, gr.y, gr.z, ar.z, hv.x, hv.y,
                          vorOffset.x, vorOffset.y, tg.x, tg.y,
                          fs.gaze.x, fs.gaze.y, fs.openL,
                          _imu ? _imu->headingDeg() : -1.0f,
                          mr.x, mr.y, mr.z);
        }
    }

    void handle(const Command& cmd, uint32_t now) {
        switch (cmd.type) {
        case CmdType::SetEmotion: {
            eEmotions e = (eEmotions)cmd.i;
            if (e >= EMOTIONS_COUNT) break;
            // Capture the emotion to restore — do not re-capture if an
            // override is already active
            if (!_overrideUntil) _preOverride = _emotion;
            // Dead: ~2 s HELD UP (400 ms of crisp rise + 2 s hold, user
            // 07-21/07-25) — this extreme posture must not be held for 10 s.
            uint32_t defMs = (e == Dead) ? 2400 : 10000;
            _overrideUntil = now + (cmd.u ? cmd.u : defMs);
            applyEmotion(e, scaledTransition(e));
            break;
        }
        case CmdType::Blink:       _blink.requestBlink();      break;
        case CmdType::WinkLeft:    _blink.requestWink(true);   break;
        case CmdType::WinkRight:   _blink.requestWink(false);  break;
        case CmdType::SetCrt:
            _crt.enabled = (cmd.i != 0);
            _crtOnAtomic.store(_crt.enabled);   // mirror for the status API
            break;
        case CmdType::SetColorDim: _tuning.eye_color_dim = cmd.f; break;
        case CmdType::PlayDance: {
            const dances::Entry* t = dances::table();
            int n = 0; while (t[n].name) n++;
            if (cmd.i >= 0 && cmd.i < n && !_pickup.isLifted()) {
                _blink.preempt();                    // clean start
                _soundPendingStep = 0.0f;   // a memorised sound turn does not
                                            // survive a NEW dance
                                            // (review 2026-07-17)
                // Random mirror (symmetric dances): unpredictable left/right
                // starting direction — user 2026-07-11
                _danceYawSign = (t[cmd.i].mirrorable && _rng.chance(0.5f))
                              ? -1.0f : 1.0f;
                // Emotion↔dance sync (user 2026-07-16): the asymmetry of the
                // current expression flips to the side of the movement
                _asymMirror = (_danceYawSign < 0.0f);
                _seq.play(t[cmd.i].keys, t[cmd.i].count);
                Serial.printf("[brain] danse: %s (sens %+.0f)\n",
                              t[cmd.i].name, _danceYawSign);
            }
            break;
        }
        case CmdType::PlayCustom: {
            // SD choreography (DanceStore: static double-bank memory — the
            // pointer stays valid even if a reload happens meanwhile)
            if (cmd.ptr && cmd.i > 0 && !_pickup.isLifted()) {
                _blink.preempt();
                _soundPendingStep = 0.0f;   // same as PlayDance (review 2026-07-17)
                _danceYawSign = (cmd.u && _rng.chance(0.5f)) ? -1.0f : 1.0f;
                _asymMirror   = (_danceYawSign < 0.0f);   // emotion↔dance sync
                _seq.play((const DanceKey*)cmd.ptr, cmd.i);
                Serial.printf("[brain] danse SD (%ld keyframes)\n", (long)cmd.i);
            }
            break;
        }
        case CmdType::AbortDance:
            if (_seq.isActive()) abortDance(now);
            break;
        case CmdType::AmbientDark: {
            bool dark = (cmd.i != 0);
            if (dark == _roulette.darkMode()) break;   // transitions only
            _roulette.setDarkMode(dark);
            // IMMEDIATE switch (without waiting for the next 6-12 s draw),
            // only if the roulette is in charge (no active override, no dance
            // — reflexes/API stay in command, rule 5):
            // night + Normal face → falls asleep; day + Sleepy face →
            // wakes up (Normal + blink).
            bool locked = (_overrideUntil != 0) || _seq.isActive() ||
                          _pickup.isLifted();
            if (!locked) {
                if (dark && _emotion == Normal) {
                    applyEmotion(Sleepy, scaledTransition(Sleepy));
                } else if (!dark && _emotion == Sleepy) {
                    applyEmotion(Normal, scaledTransition(Normal));
                    _blink.requestBlink();   // waking up
                }
            }
            break;
        }
        case CmdType::SoundDir: {
#ifdef SCE_USE_SERVO
            // Head toward the noise (sound_track, user 2026-07-16): a
            // RELATIVE yaw step ∝ the imbalance between the two mics —
            // successive events converge until the levels balance out
            // (SoundTracker stops posting below |imb| 0.12).
            // Guards: the dance owns the head, being carried = dangling feet,
            // reflexes take priority (rule A2.5). The left/right sign is
            // tunable live (soundtrack_sign, HW validation like gyro_*).
            // + MUTED WHILE MOVING (user 07-25): the mics pick up the
            // servo/gear noise → without this gate the head chases itself
            // (turns → noise → "sound" → turns again) and the bogus event
            // rearms the return-to-home timer. We ignore every event during
            // a trajectory in flight plus a 350 ms tail (the ~300 ms RMS
            // window is polluted, plus the mechanical ring-down).
            if (_servo && _tuning.sound_track >= 0.5f && !_seq.isActive() &&
                !_pickup.isLifted() && !_vor.shakeDetected() &&
                now - _lastServoMoveMs > 350) {
                _lastSoundEvtMs = now;   // rearm the "calm down" timer
                // NOTE from the 2026-07-17 review: SoundTracker posts EVERY
                // event (300 ms cadence) — everything is decided HERE: turn
                // if |imb| > 0.08, startle on a shock, and the event alone
                // holds the pose (return-to-home timer).
                float sign = _tuning.soundtrack_sign < 0.0f ? -1.0f : 1.0f;
                float imb  = clampVal(cmd.f, -1.0f, 1.0f);
                // Intensity 0..1 (user 2026-07-16: NATURAL movement): half
                // from lateralisation (|imb|), half from the level above the
                // threshold — a loud, clearly lateralised sound = a wide AND
                // brisk step; a faint sound nearly straight ahead = a small,
                // soft step.
                float thr  = _tuning.soundtrack_thr < 1.0f
                           ? 1.0f : _tuning.soundtrack_thr;
                float kLvl = clampVal((cmd.f2 / thr - 1.0f) / 3.0f, 0.0f, 1.0f);
                float k    = clampVal(0.5f * fabsf(imb) + 0.5f * kLvl,
                                      0.0f, 1.0f);
                // √ curve (responsiveness, user 2026-07-16: "not very
                // responsive"): MEDIUM imbalances (0.3-0.6, the typical case
                // of a clap off to one side) already turn decisively —
                // √0.35 ≈ 0.6 of the maximum step
                float mag  = sqrtf(fabsf(imb));
                float step = (imb < 0 ? -mag : mag) * sign
                           * _tuning.soundtrack_step_deg;
                // STARTLE (user 2026-07-16, 2nd verdict): a sound above the
                // shock threshold → the `shocked` DANCE (crisp recoil +
                // blinks), THEN, once the animation ends, the head turns
                // toward the source (the turn is memorised and applied on the
                // dance's falling edge in tick()).
                if (_tuning.soundtrack_shock_thr >= 1.0f &&
                    cmd.f2 >= _tuning.soundtrack_shock_thr) {
                    int di = dances::indexOf("shocked");
                    if (di >= 0) {
                        const dances::Entry* t = dances::table();
                        _blink.preempt();
                        _danceYawSign     = 1.0f;
                        _asymMirror       = false;   // consistent with sign +1
                        _soundPendingStep = step;    // turn after the dance
                        _seq.play(t[di].keys, t[di].count);
                        Serial.printf("[brain] sursaut sonore (RMS %.0f) -> "
                                      "shocked puis virage %+.1f\n",
                                      cmd.f2, step);
                    }
                    break;
                }
                // Nearly straight ahead: no movement (but the event did rearm
                // the timer — the pose is HELD as long as someone is talking)
                if (fabsf(imb) <= 0.08f) break;
                // Angle ∝ imbalance; speed ∝ intensity (×1.4 soft → ×0.4
                // brisk around the base duration)
                float ms = _tuning.soundtrack_move_ms * (1.4f - k);
                _servo->engageTorque();
                // Reflex envelope, not the choreography one (Units.h).
                _servo->moveTo(units::clampReflexYaw(_servo->yawDeg() + step),
                               _servo->pitchDeg(),
                               (uint32_t)clampVal(ms, 100.0f, 2000.0f));
            }
#endif
            break;
        }
        case CmdType::MoveHead: {
#ifdef SCE_USE_SERVO
            // Servo remote control (§4d) — ignored while carried (dangling
            // feet) or during a dance (the timeline owns the head)
            if (_servo && !_pickup.isLifted() && !_seq.isActive()) {
                float y = cmd.f, p = cmd.f2;
                if (cmd.i) { y += _servo->yawDeg(); p += _servo->pitchDeg(); }
                _servo->moveTo(y, p, cmd.u ? cmd.u : 400);
            }
#endif
            break;
        }
        }
    }

    // Cutting a dance short (reflex/API): timeline stopped, head brought back
    // to neutral smoothly, gaze handover crossfaded
    void abortDance(uint32_t now) {
        (void)now;
        _seq.abort();
        _soundPendingStep = 0.0f;   // startle interrupted: no turn
        _gazeBlender.blendTo(blend::GAZE_MS);
#ifdef SCE_USE_SERVO
        if (_servo) _servo->moveTo(units::YAW_CENTER, units::PITCH_NEUTRAL, 600);
#endif
        Serial.printf("[brain] danse abortee\n");
    }

    // ------------------------------------------------------------------
    // Per-emotion head posture (user 2026-07-16, home pitch 93°): the "low"
    // emotions LOWER the head (pitch increasing toward PITCH_MAX 99 =
    // horizon), the proud ones raise it. 0 = home. The bias is applied both
    // on emotion changes AND to the head-follow poses.
    // ------------------------------------------------------------------
    static float pitchBiasFor(eEmotions e) {
        switch (e) {
            // "Head down" family, all the way to the LIMIT (user 2026-07-16:
            // +6 = PITCH_MAX 99 (spec 5~85°) — reads as lowering the head)
            case Sad:      return (float)units::PITCH_DOWN_MAX;  // limit — dejected
            case Sleepy:   return (float)units::PITCH_DOWN_MAX;  // dozing off
            case Blush:    return (float)units::PITCH_DOWN_MAX;  // shy, hiding
            case Worried:  return (float)units::PITCH_DOWN_MAX;
            case Frustrated: case Disgust:   return  4.0f;
            case Unimpressed:                return  3.0f;
            // Dead: head raised to a pitch of ~55 (raw) — reads as agonising,
            // throat exposed, but WITHOUT going all the way to the end stop
            // (user 2026-07-21: 55° instead of the previous near-limit value,
            // which was too extreme). 55 is far from PITCH_MIN (19) → no risk
            // of stalling the servo.
            case Dead:
                return (float)(55 - units::PITCH_NEUTRAL);
            case Smug:                       return -5.0f;  // chin up
            case Excited: case Surprised:    return -4.0f;
            case Awe:                        return -4.0f;
            case Curious: case Questioning:  return -3.0f;
            default:                         return  0.0f;
        }
    }

    // The compiled per-emotion table, PACED by the active personality. Duration
    // only — springK/springD stay whatever `transitionFor()` chose, because
    // scaling those changes HOW a spring overshoots, not just how long the
    // whole thing takes, and that reads as a different transition style rather
    // than the same one paced differently (A2.15: the Brain is the one place
    // that smooths anything; this is where that pacing has to live).
    TransitionConfig scaledTransition(eEmotions e) const {
        TransitionConfig tc = EmotionRoulette::transitionFor(e);
        tc.duration = scaledDurationMs(tc.duration, _transitionScale.load());
        return tc;
    }

    void applyEmotion(eEmotions e, const TransitionConfig& tc) {
        // Random mirroring of asymmetries (user 2026-07-16): redrawn on every
        // emotion CHANGE — which eye carries the asymmetry (the small eye of
        // Annoyed, the squint of Smug...) varies from one episode to the next.
        // During a dance: LOCKED to the mirrored direction (_danceYawSign) so
        // that the carrying eye matches the direction of the movement.
        bool changed = (e != _emotion);
        if (changed) {
            _asymMirror = _seq.isActive() ? (_danceYawSign < 0.0f)
                                          : _rng.chance(0.5f);
        }
        _emotion    = e;
        _transition = tc;
        _shownEmotion.store((int)e);
        _blink.setEmotion(e, tc.method == SPRING);
        _pitchBias  = pitchBiasFor(e) * _pitchBiasScale.load();
#ifdef SCE_USE_SERVO
        // Posture: gentle nudge toward the bias of the new emotion — not
        // during a dance (the timeline owns the head) and not during a pickup
        // (dangling feet). The current yaw is kept. Dead: CRISP rise (400 ms
        // — 38° of travel; the standard 700 ms dragged, user 07-25).
        if (changed && _servo && !_seq.isActive() && !_pickup.isLifted()) {
            _servo->moveTo(_servo->yawDeg(),
                           units::PITCH_NEUTRAL + _pitchBias,
                           (e == Dead) ? 400 : 700);
        }
#endif
    }
};

} // namespace sce
