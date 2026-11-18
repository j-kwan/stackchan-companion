// =============================================================================
// companion — StackChan-Companion: MAIN PROGRAM
// =============================================================================
// Full assembly (detailed architecture: docs/ROADMAP.md §A4, docs/architecture/WORKFLOWS.md):
//   hal/Board          ordered K151 init (VM_EN, LCD 40 MHz before SD, probes)
//   engine/Renderer    30 Hz task, core 1, prio 3 — sole screen owner,
//                      CRT dot mask, Cozmo blink line, overlay effects
//   behavior/Brain     100 Hz task, core 1, prio 4 — sole FaceState writer:
//                      roulette, idle saccades, BlinkController (lid anchored
//                      bottom), VOR v3.1 (validated efference), pickup, dance
//                      Sequencer (eyes lead head follows), CommandQueue
//   behavior/ServoMotion  50 Hz task, core 0, prio 3 — trajectories + efference
//   hal/Camera         task on core 0 prio 1 — GC0308 capture + SOFTWARE JPEG
//                      encoding (~150-300 ms/VGA frame) outside loop(): the
//                      stream no longer saturates the loop nor HTTP (preempted
//                      by ServoMotion + async_tcp → smooth dances, snappy HTTP)
//   app/WebApi+Console STA/AP, REST + Swagger + mDNS + captive portal + [Bins]
//   app/EmotionLeds+SoundFx+Launcher+SdConfig  (options + SD)
//   interact/TouchGestures + hal/Si12T          (screen touch + head touch)
//
// loop() stays light: touch/API/launcher polling, instrumented heartbeat
// (internal heapMin + stack margins), deferred SD persistence (rule A2.6).
// =============================================================================

#include <Arduino.h>
#include "../common/I18n.h"   // sce::T - bilingual UI (EN default)
#include "../common/SdRoot.h" // one-time /stackchan-eyes -> /stackchan-companion move
#include <M5Unified.h>
#include "hal/Board.h"
#include "hal/ArduinoClock.h"
#include "engine/Units.h"
#include "engine/Emotions.h"
#include "engine/FaceState.h"
#include "engine/Renderer.h"
#include "engine/Tuning.h"
#include "engine/BandTimer.h"
#include "behavior/Brain.h"
#include "behavior/Personalities.h"   // which character is loaded (selector)
#include "behavior/RuleEngine.h"
#include "app/RuleStore.h"
#include "hal/ImuReader.h"
#include "app/WebApi.h"
#include "../common/FirmwareInfo.h"  // boot log: reset reason + which OTA slot runs
#include "../common/Trace.h"         // debug trace, toggled by the `debug` tuning key
#include "app/EmotionLeds.h"
#include "hal/Si12T.h"
#include "interact/TouchGestures.h"
#include "app/Launcher.h"
#include "app/SoundFx.h"
#include "app/SoundTracker.h"
#include "hal/CpuLoad.h"
#include "hal/Camera.h"
#include "hal/I2cBus.h"
#include "app/SdConfig.h"
#include <esp_sntp.h>        // sntp_set_time_sync_notification_cb
#include "app/DanceStore.h"
#include "app/PersonalityStore.h"  // les personnalites de la carte
#ifdef SCE_USE_SERVO
#include "behavior/ServoMotion.h"
#endif

using namespace sce;
using namespace sce::units;

static Board                   board;
static ArduinoClock            clk;
static TripleBuffer<FaceState> faceBus;
static Tuning                  tuning;
static Renderer                renderer(clk, faceBus);
// The sound visualiser's bus and its analyser. A SECOND TripleBuffer instance,
// not a second reader of the face bus.
static TripleBuffer<SoundFrame> soundBus;
static SoundViz*               soundViz = nullptr;
// Timer / pomodoro of the band (modes 4/5, 08-04). Owned by loop(): touch
// feeds it, loop ticks it and publishes its display through the FieldStore,
// its EVENTS become emotions through the CommandQueue — the machine itself
// is pure (engine/BandTimer.h, test_bandtimer).
static BandTimer               bandTimer(tuning);
static int                     _bandDragSteps    = 0;   // scroll accumulator
static bool                    _bandDragConsumed = false; // swallow release
static FieldStore              fields;         // status band blackboard
static RuleEngine*             rules  = nullptr;   // field→Command (built in setup)
static RuleStore              ruleStore;      // SD rules (reactive plugins)
static int                    builtinRules = 0;   // number of built-in rules

static ImuReader               imu(tuning);   // runtime gyro mapping (VOR v3)
static Brain*                  brain  = nullptr;   // built in setup
static WebApi*                 webApi = nullptr;
static EmotionLeds*            leds   = nullptr;
static Si12T                   headTouch;      // head touch (Wire1, 0x68)
static TouchGestures           screenTouch;    // screen gestures (M5.Touch)
static Launcher                launcher;       // SD .bin picker (T3)
static SoundFx                 soundFx(tuning);// chirps (T4, off by default)
static SoundTracker*           soundTracker = nullptr; // head toward the noise
static CpuLoad                 cpuLoad;        // per-core CPU load (console)
static Camera                  camera;         // GC0308 (HA/Frigate, off by default)
static SdConfig                sdConfig;       // YAML persistence (T5)
static DanceStore              danceStore;     // SD choreographies /dances/*.csv
static PersonalityStore        personalityStore;  // /stackchan-companion/personalities/*.yaml

// Action sink of the rule engine → Brain CommandQueue (whitelist).
//
// PlayDance IS RESOLVED HERE, by name, at FIRE time — the same merged list
// `endOfTimer` walks (built-ins first, then the SD bank). It cannot be done in
// RuleStore: at boot the rules load before `danceStore.reload()`, so the bank is
// empty when a line is parsed; and it must not be done as a stored INDEX,
// because DanceStore is double-banked and an upload renumbers what follows.
// Resolving late costs one name comparison per firing — a rule fires seconds
// apart at best — and buys every SD choreography as a rule action, not just the
// fifteen compiled ones.
static void rulePost(void* ctx, const Command& c) {
    Brain* b = (Brain*)ctx;
    if (c.type == CmdType::PlayDance && c.ptr) {
        const char* name = (const char*)c.ptr;
        const int i = dances::indexOf(name);
        if (i >= 0) { b->post({ CmdType::PlayDance, (int32_t)i }); return; }
        // SD choreography: a STATIC double-bank pointer, the only kind allowed
        // through the queue (A2.17, last paragraph).
        if (const DanceStore::Entry* e = danceStore.find(name)) {
            Command k{ CmdType::PlayCustom };
            k.i = e->count; k.u = 1 /*mirrorable*/; k.ptr = e->keys;
            b->post(k);
            return;
        }
        // NAMED AND ABSENT. Said out loud rather than dropped: the rule exists,
        // it fired, and nothing happened — which looks exactly like a rule that
        // never fires, the hardest shape to diagnose from the outside. The
        // build-time gate is what should have caught it; this is the net below.
        sce::trace::log("[rules] danse '%s' introuvable (ni compilee ni SD)",
                        name);
        return;
    }
    b->post(c);
}
static volatile bool           _launcherRequested = false;
static String                  apiIp;
// Which personality is CURRENTLY applied. Not read back from `tuning` on every
// pass: the point of holding it is to notice the moment it CHANGES, and a value
// compared against itself can never do that.
static int                     _personality = -1;   // -1 = nothing applied yet

// SANITISE the tuning key, exactly as `band_mode` is already sanitised: the key
// is reachable through /api/tuning and config.yaml, neither of which validates,
// so an index nobody implements must resolve to the default robot rather than
// to no robot. Re-persisted so the bad value does not come back at the next
// boot.
static int currentPersonality() {
    int p = (int)(tuning.personality + 0.5f);
    if (p < 0 || p >= sce::personalities::count()) {
        p = 0;
        tuning.personality = 0.0f;
    }
    return p;
}

// Applies everything a personality OWNS — its roulette (cadence + weights) and
// its identity colour. It owns nothing else: no other tuning key is written, so
// brightness, servos, sound and thresholds survive a change of character in
// both directions. That restraint is the design, not an omission — a profile
// that rewrote settings would be the "a value came back changed and nobody
// knows which layer did it" class of bug, already paid for once here.
static void applyPersonality(int idx, bool boot) {
    const sce::Personality& p = sce::personalities::at(idx);
    if (brain) {
        auto& r = brain->roulette();
        // nullptr = keep EmotionRoulette's OWN table. Personality 0 therefore
        // does not restate the historical weights, it declines to touch them —
        // which is what makes "no personality data = today's robot" true by
        // construction instead of by careful copying.
        if (p.hasWeights) {
            r.clearWeights();
            for (int i = 0; i < EMOTIONS_COUNT; i++)
                if (p.weights[i] > 0.0f) r.setWeight((eEmotions)i, p.weights[i]);
        } else {
            r.resetWeights();
        }
        r.setInterval(p.minMs, p.maxMs);
        // FEEL, not another per-emotion table: one dial paces every transition,
        // one dial scales every posture nudge — see Personalities.h for why
        // this stops short of making the ~100 values transitionFor()/
        // pitchBiasFor() hold individually editable.
        brain->setTransitionScale(p.transitionScale);
        brain->setPitchBiasScale(p.pitchBiasScale);
    }
    renderer.setEyeRgbOverride(p.eyeRgb);
    _personality = idx;
    if (!boot) Serial.printf("[app] personnalite: %s\n", p.name);
}

// Loads the active personality's rules. FALLING BACK LOUDLY is the whole point
// of the else branch: a missing file must not leave the robot running with an
// empty rule table, because that looks exactly like a robot whose rules simply
// never fire — the failure would be discovered as a behaviour bug days later,
// not as a missing file now.
static void loadRulesForPersonality() {
    if (!board.hasSD() || !rules) return;
    const sce::Personality& p = sce::personalities::at(_personality);
    int n = ruleStore.load(*rules, builtinRules, p.rulesFile);
    if (n >= 0) {
        Serial.printf("[app] regles SD (%s): %d\n", p.name, n);
        return;
    }
    Serial.printf("[app] regles ABSENTES pour '%s' (%s) - repli sur defaut\n",
                  p.name, p.rulesFile);
    if (_personality != 0) {
        n = ruleStore.load(*rules, builtinRules,
                           sce::personalities::at(0).rulesFile);
        if (n >= 0) Serial.printf("[app] regles SD (repli): %d\n", n);
    }
}
// SET BY THE SNTP CALLBACK, never inferred. `sce::clockSynced(time())` looked
// like a proof that NTP had landed and is not: `M5.begin()` calls
// `M5.Rtc.setSystemTimeFromRtc()`, so on every boot after the first the
// system clock is already plausible BEFORE a single NTP packet. The old
// guard therefore fired on the first loop() pass, rewrote the RTC from its
// own drift and printed "NTP ok" on a captive portal or with UDP/123 blocked
// — real NTP corrected the chip exactly once in its life (review 08-02).
// The status register is no good either: it self-resets after being read.
static volatile bool           sntpSynced = false;      // a packet REALLY landed
static void onSntpSync(struct timeval*) { sntpSynced = true; }
static bool                    rtcSyncPending = false;  // NTP -> BM8563, once
static WebApi::ClockState      clockState;              // published to the API
#ifdef SCE_USE_SERVO
// MEASURED servo pose, for the diagnostic endpoint /api/servo/pos. Sampled
// HERE and not in the AsyncTCP callback: a ReadPos is blocking serial I/O on
// the shared half-duplex bus (rule A2.6). Sampled ONLY while the servos are
// disabled, because that bus is write-only in operation (finding 07-21) and
// a read interleaved with the WritePos stream leaves the servos mute.
static WebApi::ServoPose       servoPose;
#endif

// ARMS (or RE-ARMS) THE SNTP CLIENT. One function because there are two callers
// now — the end of the wake-up sequence and `POST /api/clock/sync` — and two
// copies of "register the callback, then configTime" is exactly the kind of twin
// that drifts. `configTime` stops any running client before starting a new one,
// so calling it a second time IS the resync.
//
// The callback is registered FIRST: a fast server can answer before the next
// line would have run. AP mode returns without arming anything — the robot IS
// the network there, and an SNTP client would retry against an unreachable pool
// for ever.
static void startNtp() {
    if (!webApi || !apiIp.length() || webApi->isApMode()) return;
    sntp_set_time_sync_notification_cb(onSntpSync);
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    rtcSyncPending = true;
}
#ifdef SCE_USE_SERVO
static ServoMotion*            servoMotionPtr = nullptr;  // heartbeat P7

// THE LAST THING THE COMPANION DOES BEFORE HANDING THE MACHINE OVER.
//
// Torque is a PHYSICAL state that outlives our process: the SCS0009 keep it in
// their own registers across our reboot. A guest bin that does not drive the
// neck never touches the bus, so it inherits whatever we left — and a head left
// locked stays locked, warm and unmovable by hand, with nothing on the robot
// able to explain why (user 08-02). There is no timer left to save us either:
// the 15 s auto-release needs the companion to still be running.
//
// ACKNOWLEDGED, not fired and forgotten. `releaseTorque()` only posts a
// request; the ServoMotion task applies it on its next tick. Both call sites
// happen to have seconds of work before the actual flash, so today it lands —
// but that is an accident of timing, not a guarantee, and the failure it hides
// is permanent. So we WAIT for the state to be true.
//
// BOUNDED, and it proceeds anyway on timeout: a hand-over that never happens
// would be worse than the risk we are removing. Same shape as the netTask
// parking ACK in `onBeforeStop`, which waits 3 s and flashes regardless.
static void releaseServosForHandover(const char* who) {
    if (!servoMotionPtr) return;
    servoMotionPtr->releaseTorque();
    const uint32_t t0 = millis();
    while (!servoMotionPtr->torqueReleased() && millis() - t0 < 400) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    // The RAIL is cut separately, by cutServoRailForFlash(), at the last
    // instant before the flash — see there for why it cannot happen here.
    // Ordered torque first, rail second: cutting power under a driven servo is
    // how you get the jolt we spent the whole day removing.
    Serial.printf("[companion] servos relaches avant %s : %s (%lu ms)\n", who,
                  servoMotionPtr->torqueReleased() ? "ok" : "NON CONFIRME",
                  (unsigned long)(millis() - t0));
}

// CUTTING THE RAIL IS THE POINT OF NO RETURN, so it happens at the last
// possible instant - immediately before the flash, once every refusal has been
// ruled out.
//
// It used to sit beside the torque release, BEFORE the checks, and that was a
// trap of my own making (review 08-03): a missing OTA partition, an unreadable
// file, a size of zero, an oversized image or a failed updateFromFS all return
// with the COMPANION STILL RUNNING and the servo supply cut. The neck is then
// dead for the rest of the session, silently, and a later engageTorque() drives
// servos that have no power. Whoever does not flash must not cut.
static void cutServoRailForFlash() {
    sce::i2cbus::Guard g;
    Serial.printf("[companion] rail VM_EN %s\n",
                  board.py32().disableServoPower() ? "coupe" : "NON COUPE");
}
#else
static inline void cutServoRailForFlash() {}
// No servo build: the hand-over has nothing to release. Declared all the same
// so the two call sites read identically in both configurations — a call that
// exists only under an #ifdef is a call someone forgets to add to the next door.
static inline void releaseServosForHandover(const char*) {}
#endif

// Cycles the status band mode among {0=none, 2=sound, 3=gauges, 4=timer,
// 5=pomodoro}.
// dir>0 = next, dir<0 = previous. Writes ONLY tuning.band_mode: loop() is the
// SOLE applier (sanitize + setStatusBar every iteration — a single
// Tuning→renderer pipe, no second writer). SD persistence is deferred and
// COALESCED by loop() (a burst of swipes = a single write).
static void cycleBandMode(int dir) {
    static const int MODES[5] = { 0, 2, 3, 4, 5 };
    constexpr int N = 5;
    int cur = (int)(tuning.band_mode + 0.5f), idx = 0;
    for (int i = 0; i < N; ++i) if (MODES[i] == cur) idx = i;
    tuning.band_mode = (float)MODES[(idx + N + (dir > 0 ? 1 : -1)) % N];
    if (webApi) webApi->requestSave();    // SD persistence (debounced in loop())
}

// Next/previous emotion (swipe over the eye zone). AbortDance FIRST — otherwise
// the keyframes of the running dance re-apply their own emotion on every tick
// and overwrite the requested one (manual interaction wins, A2.5).
static void swipeEmotion(int dir) {
    int e = ((int)brain->currentEmotion() + EMOTIONS_COUNT + dir) % EMOTIONS_COUNT;
    brain->post({ CmdType::AbortDance });
    brain->post({ CmdType::SetEmotion, e, 8000 });
}

// -----------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------
void setup() {
    // ---- THE SERIAL LINE MUST NEVER BLOCK loop() (fix 08-25) --------------
    // On the CoreS3 `Serial` IS the USB CDC (ARDUINO_USB_CDC_ON_BOOT), and
    // since the Arduino 3.x core `HWCDC::write` waits on its ring buffer with
    // a 100 ms timeout, retried until a progress cap — so a cable plugged into
    // a host that never opens the port blocks the WRITER for SECONDS. That is
    // the normal state of this robot: powered over USB, no monitor attached.
    //
    // MEASURED, not feared: with the port closed, the 5 s heartbeat's own
    // printf took 4.01 s, the whole loop() pass 4.08 s, the microphone drain
    // fell from 156 blocks to 33, and the status band held the same picture
    // for 121 consecutive frames. Open a monitor and the same firmware reads
    // 1.8 ms, 156 blocks, one still frame. The "sound analyser freezes
    // periodically" report was this, and so was the earlier "it stutters":
    // nothing was ever wrong with the audio path.
    //
    // Zero means DROP rather than wait. Diagnostics are worth exactly what
    // they cost, and a log line that can freeze the face for four seconds
    // costs more than it tells. With a monitor attached nothing is lost:
    // the host drains, the ring never fills, every line still arrives.
    Serial.setTxTimeoutMs(0);

    // Lock for the shared 11/12 I2C bus (IMU/touch/AXP/SCCB/LEDs/Si12T): created
    // BEFORE any access and BEFORE the Brain/Renderer/Servo tasks start.
    sce::i2cbus::init();
    board.begin(/*brightness=*/76);
    // Reset reason: tells apart power-cycle (poweron), crash (panic/task_wdt/
    // int_wdt), brownout, OTA (sw) — diagnosing the unexpected reboots seen
    // during camera testing (2026-07-20). NAMED, not the raw enum: the number
    // meant a trip to the IDF headers every time.
    // The build identity goes out on the SAME line, because the one question
    // this log could never answer is "is this even the firmware I just
    // flashed?" — a USB flash writes one OTA slot and `otadata` picks the slot,
    // so `slot` is what tells a successful flash from an ignored one.
    // All of it is also served by GET /api/firmware: opening this serial port
    // on native USB RESETS the board, so anything only printed here is lost the
    // moment you go looking for it.
    Serial.printf("[app] setup suite (reset: %s, slot: %s, sha: %s)\n",
                  sce::fw::resetReasonName(), sce::fw::slot(),
                  sce::fw::sha8());

    // One-time move of the whole config directory, BEFORE anything opens a
    // path under either name (see firmware/common/SdRoot.h).
    if (board.hasSD()) sce::migrateSdRoot();

    // SD config (T5): reloads tuning + persisted WiFi credentials.
    // BEFORE the modules start (they read the register live).
    if (board.hasSD() && sdConfig.load(tuning)) {
        Serial.printf("[app] config.yaml chargée (ssid:'%s')\n",
                      sdConfig.wifi.clientSsid.c_str());
    }
    // The trace flag must be live BEFORE api.begin(): the WiFi join is the
    // first thing worth narrating, and loop() — which keeps the flag in step
    // afterwards — has not run yet. Without this line a persisted debug=1
    // missed the one sequence it was most probably set to watch.
    sce::trace::on = tuning.debug > 0.5f;

    if (!renderer.begin()) {
        Serial.printf("[app] ERREUR: renderer.begin()\n");
    }

    brain = new Brain(clk, faceBus, tuning, esp_random());
    soundTracker = new SoundTracker(*brain, tuning);   // sound_track option
    // The status band's sound visualiser: the analyser runs on loop(), in the
    // same pass that already consumes a microphone block, and publishes on its
    // OWN TripleBuffer. The renderer is the only consumer.
    soundViz = new SoundViz(soundBus);
    soundTracker->attachViz(soundViz);
    renderer.attachSoundBus(&soundBus);
    brain->begin();
    // VOR v3: gyro→counter-rotation + catch-up saccades + Scared.
    // Default gyro mapping (yaw=Y+, pitch=X+) VALIDATED ON HW 2026-07-11;
    // re-tunable at runtime if needed (tuning gyro_*, PLAYBOOK-HW §1).
    brain->attachImu(&imu);

#ifdef SCE_USE_SERVO
    // K151 servos (P4a): 50 Hz trajectories + VOR efference copy + pickup
    // posture. Head-follow OFF by default (tuning.head_follow).
    static ServoMotion servoMotion(clk, tuning);
    // THE RAIL IS CUT ACROSS THE LIBRARY'S ATTACH (user 08-03: "the servos go
    // back to home before the screen shows anything"). That movement is not
    // ours: `attachServos()` opens Serial2 and then writes the start position
    // with a 1000 ms transition, while the servo is powered and its torque is
    // on from the power-up. Our own `torque(false)` runs AFTER that write, so
    // it was always one beat too late - and if the head happened to rest
    // against a stop, that commanded sweep is the bang.
    // With the rail down the write reaches an unpowered servo and moves
    // nothing; power comes back afterwards, and the FIRST thing we do on the
    // live bus is release the torque. The servo then holds whatever pose it
    // physically has, which is what the measurement below reads.
    board.py32().disableServoPower();
    servoMotion.begin();       // Serial2 + start write, into a dead rail
    board.py32().enableServoPower();
    servoMotion.settleAfterPower();   // torque OFF first, then measure
    servoMotion.start();       // core 0, prio 3
    brain->attachServo(&servoMotion);
    servoMotionPtr = &servoMotion;
    Serial.printf("[app] servo: trajectoires 50 Hz actives\n");
#endif

    // THE ROBOT WAKES UP RATHER THAN SNAPPING TO ATTENTION (user 08-03).
    // Order matters and it is the whole fix: the servos are already limp when
    // we get here (ServoMotion::begin leaves them so), the face goes to Sleepy
    // first, and only THEN does the head rise to HOME on a slow ramp - from the
    // pose that was measured, not from one we assumed. Normal behaviour follows.
    // Before this, boot re-engaged the torque against the lower stop the head
    // had sagged onto, and pushed.
    // Startup order: renderer first (consumer ready), then brain
#ifdef SCE_USE_SERVO
    // THE ORDER IS THE FIX, and the first version had it wrong (user 08-03:
    // "I see Normal before Sleepy, and the servos engage before I even see
    // Normal"). Both faults came from starting the Brain first:
    //   · its very first tick publishes the DEFAULT face, so Normal was on
    //     screen before the Sleepy posted a line later ever arrived;
    //   · and head-follow commanded the neck on that same tick, which
    //     re-engages the torque - so the head repositioned itself before the
    //     wake-up sequence had begun, which is precisely the bang we are here
    //     to remove.
    // So: the emotion is QUEUED BEFORE the Brain runs (the CommandQueue is
    // drained on the first tick, so frame one is already Sleepy), and
    // head-follow is HELD until the head has walked up to HOME — via the
    // Brain's own hold bit, never by touching the user's setting.
    // The hold is the Brain's OWN bit (review 08-04), not a zeroed-then-
    // restored `tuning.head_follow`: the restore used to overwrite a POST
    // /api/tuning?head_follow that landed inside the wake window, silently.
    brain->setHeadFollowHold(true);
#endif
    // THE ROBOT SLEEPS UNTIL IT IS REACHABLE (user 08-04): Sleepy is held
    // through the WHOLE boot — head rise, SD loads, WiFi association — and
    // Normal is only posted once an IP exists (STA or AP fallback), right
    // after webApi->begin() below. Eyes that open before the robot can be
    // talked to promise a readiness that is not there; a sleepy face over a
    // long association is the honest picture, and it makes "no network"
    // visible from across the room.
    // The 120 s hold is the CAP, not the duration: Normal normally arrives
    // within seconds, and if the network takes longer than two minutes the
    // override expires back to Normal by itself — a robot without WiFi still
    // has to wake up eventually.
    brain->post({ CmdType::SetEmotion, (int32_t)Sleepy, 120000 });
    // THE BRAIN GOES FIRST (user 08-03: "I see Normal eyes for an instant, then
    // it starts Sleepy"). The renderer draws whatever the TripleBuffer holds,
    // and before the Brain has published anything that is the DEFAULT state -
    // Normal. Queueing Sleepy ahead of the Brain was not enough: the flash
    // happens between the renderer's first frame and the Brain's first tick.
    // Starting the Brain first costs nothing - the buffer is a buffer, it does
    // not need its consumer to exist - and the 60 ms below let the first
    // published face be Sleepy before a single frame is drawn.
    brain->start();         // core 1, prio 4
    vTaskDelay(pdMS_TO_TICKS(60));
    renderer.start();       // core 1, prio 3

#ifdef SCE_USE_SERVO
    // ---- THE WAKE-UP SEQUENCE (user 08-03: "make it look like it is waking
    // up"). Four beats, and each one is doing a job, not decoration:
    //   1. EYES SHUT. Sleepy is posted first and given a moment: the face is
    //      the only thing the user can see while the neck is still limp, and a
    //      robot that boots with its eyes already open looks like it never
    //      slept - which is exactly what the old snap-to-attention looked like.
    //   2. A BLINK. One slow open-and-shut, so the waking reads as waking and
    //      not as a screen that took a second to draw.
    //   3. THE HEAD RISES, slowly, from the pose that was MEASURED at boot -
    //      1.8 s, against the ~400 ms of an ordinary move. This is the beat
    //      that removes the bang: the torque is engaged AT the measured pose,
    //      so nothing is forced at the instant it comes back, and the head then
    //      walks up off the stop instead of being thrown at it.
    //   4. EYES OPEN, and normal life resumes.
    // Blocking, in setup(), on purpose: nothing else has anything to do yet,
    // and the alternative - a state machine in loop() for an event that happens
    // once per boot - is more moving parts than the thing is worth.
    vTaskDelay(pdMS_TO_TICKS(900));           // the eyelids fall, neck still limp
    servoMotion.homeSlowly(1800);             // torque back AT the measured pose
    // The ramp FINISHES before Normal is posted (review 08-03): applyEmotion
    // issues its own 700 ms moveTo on every emotion change, and posting Normal
    // mid-ramp restarted the trajectory at 2/5 of the rise.
    // WAITED OUT ON ITS OWN SIGNAL, capped (review 08-04): a fixed 2000 here
    // restated homeSlowly's 1800 five lines up, and whoever slows the rise
    // for a heavier head would not know the echo exists. isMoving() covers
    // the blind stand-up too (the servo-side ramp reports itself). The
    // 200 ms head start lets the request travel through the motion task; the
    // 4 s cap means a dead servo cannot hold the boot hostage.
    vTaskDelay(pdMS_TO_TICKS(200));
    for (uint32_t t0 = millis();
         servoMotion.isMoving() && millis() - t0 < 4000; )
        vTaskDelay(pdMS_TO_TICKS(50));
    Serial.println("[app] reveil : tete a HOME, yeux fermes jusqu'a l'IP");
#endif
    // The wake does NOT end here (user 08-04): the eyes stay shut through the
    // rest of setup() — SD loads, WiFi association — and open at the ONE
    // moment the robot becomes reachable, right after webApi->begin(). This
    // also settles the old debt of the wake PRECEDING the STA connect instead
    // of overlapping it: the sleepy seconds now ARE the association seconds.

    // ---- Rule engine (field→Command) — the REACTIVE half of the contract.
    // dark_sleepy is EXPRESSED here (no more ad-hoc state machine in loop):
    // darkness held 6 s → night mode; light back → day; option turned off →
    // cleanup. The sources (loop) publish light/dark_sleepy; the engine posts
    // AmbientDark through the CommandQueue (reflexes have priority, A2.5).
    // New behaviours = new RULES (soon loadable from the SD card), never
    // hard-wired plumbing. ----
    rules = new RuleEngine(fields, rulePost, brain);
    {   using R = RuleEngine;
        R::Rule sleep; sleep.enableKey = "dark_sleepy"; sleep.field = "light";
        sleep.op = R::LE; sleep.value = 1.0f; sleep.sustainMs = 6000;
        sleep.cooldownMs = 2000;
        sleep.cmd.type = CmdType::AmbientDark; sleep.cmd.i = 1;
        rules->add(sleep);
        R::Rule wake; wake.enableKey = "dark_sleepy"; wake.field = "light";
        wake.op = R::GT; wake.value = 10.0f;
        wake.cmd.type = CmdType::AmbientDark; wake.cmd.i = 0;
        rules->add(wake);
        // Option turned off during the night → leave night mode (idempotent).
        R::Rule off; off.field = "dark_sleepy"; off.op = R::LT; off.value = 0.5f;
        off.cmd.type = CmdType::AmbientDark; off.cmd.i = 0;
        rules->add(off);
    }
    builtinRules = rules->count();
    // Community SD rules (/stackchan-companion/rules.txt) — appended AFTER the
    // built-in ones; hot-reload (POST /api/rules/reload) truncates back to
    // builtinRules.
    // THE PERSONALITY decides which rule file is read, so it is settled before
    // the first load rather than corrected after one.
    // LES PERSONNALITES DE LA CARTE, avant de choisir laquelle appliquer : un
    // caractere ajoute par la console doit exister au moment ou `personality`
    // est resolu, sinon l'index designerait un slot vide et retomberait sur 0.
    // Une carte SANS repertoire ne change rien (-1) — c'est le critere
    // d'acceptation, pas un cas d'erreur.
    if (board.hasSD()) {
        renderer.pause(/*willPaint=*/false);   // SD only, no pixels (A2.16)
        int bad = 0;
        const int np = personalityStore.loadAll(bad);
        renderer.resume();
        if (np > 0) Serial.printf("[perso] %d personnalite(s) de la carte%s\n",
                                  np, bad ? " (avec des problemes, voir plus haut)" : "");
    }
    applyPersonality(currentPersonality(), /*boot=*/true);
    if (board.hasSD()) {
        // creates a template rules.txt if missing (visible/downloadable/
        // editable from the console). SD write → under renderer.pause (SPI2
        // contention, A2.16). Only ever the DEFAULT file: a personality's own
        // rules are authored, not generated, and writing a stub for a missing
        // one would turn "I forgot to copy it" into "it exists and does
        // nothing" — the silent failure the fallback below is there to avoid.
        renderer.pause(/*willPaint=*/false);   // SD only, no pixels
        ruleStore.writeDefaultIfAbsent();
        renderer.resume();
        loadRulesForPersonality();
    }

    // Leftovers from interrupted uploads (atomic sd/put). ⚠ DO NOT delete
    // blindly: after a power cut BETWEEN the `remove` and the `rename`, the
    // leftover is the ONLY surviving copy — the old blanket purge wiped the
    // fallback image (/companion.old) and the WiFi credentials
    // (config.yaml.tmp), exactly what the .tmp scheme was meant to prevent
    // (max review 07-27). We RESTORE when the target is missing, purge
    // otherwise. NB: the .tmp of /companion.bin is named "/companion.bin.tmp"
    // (the old "/companion.tmp" never existed).
    if (board.hasSD()) {
        auto recover = [](const char* tmp, const char* target, bool checkImg) {
            if (!SD.exists(tmp)) return;
            if (SD.exists(target)) { SD.remove(tmp); return; }
            if (checkImg) {           // OTA image: only promote a PLAUSIBLE
                File f = SD.open(tmp, FILE_READ);   // binary (same thresholds
                uint8_t magic = 0; size_t sz = 0;   // as the launcher)
                if (f) { sz = f.size(); f.read(&magic, 1); f.close(); }
                if (magic != 0xE9 || sz < 262144) { SD.remove(tmp); return; }
            }
            if (SD.rename(tmp, target))
                Serial.printf("[app] %s restaure depuis %s\n", target, tmp);
        };
        // An upload .tmp is NEVER promoted: its completeness cannot be
        // verified after a POWER cut (the "bytes received == bytes written"
        // check lives in WebApi, not on disk), and a truncated file starting
        // with 0xE9 and larger than 256 KB would pass validation → an
        // unbootable safety net (review 07-27b).
        // The .old, on the other hand, is the OLD complete image renamed: safe.
        SD.remove("/companion.bin.tmp");
        recover("/companion.old", "/companion.bin", true);
        recover("/stackchan-companion/config.yaml.tmp",
                "/stackchan-companion/config.yaml", false);
        recover("/stackchan-companion/rules.txt.tmp",
                "/stackchan-companion/rules.txt", false);
    }

    // ---- REST API — STA if configured (config.yaml/API), AP fallback ----
    // SD choreographies (/dances/*.csv) — loaded BEFORE the API (lookups)
    if (board.hasSD()) danceStore.reload();

    webApi = new WebApi(*brain, renderer, tuning, sdConfig.wifi, sdConfig.api, danceStore);
    webApi->setSdPresent(board.hasSD());
    webApi->attachSoundTracker(soundTracker);   // mic levels (status)
    cpuLoad.begin();
    webApi->attachCpuLoad(&cpuLoad);            // CPU load (status)
    webApi->attachBoard(&board);                // PMIC battery (status)
    camera.attachTuning(&tuning);               // camera settings, hot-tunable
    // The camera PAINTS NOTHING: it pauses the renderer only to keep the DMA
    // off SPI2 (~40 ms/frame, A2.16). Declaring that (08-03) is what stops it
    // from invalidating the ghost buffer ten times a second — before this, one
    // renderer frame in three paid a full 102 400 B push plus a 51 200 B
    // memcpy, which is the whole dirty-band gain given back.
    camera.attachRendererPause([](bool p) {
        if (p) renderer.pause(/*willPaint=*/false); else renderer.resume();
    });
    camera.start();                             // dedicated task, core 0 prio 1
    webApi->attachCamera(&camera);              // GC0308 (HA/Frigate, off by default)
    renderer.attachFields(&fields);             // status band blackboard
    webApi->attachFields(&fields);              // /api/field | /api/statusbar | /api/say
#ifdef SCE_USE_SERVO
    webApi->attachServoPose(&servoPose);        // /api/servo/pos (diagnostic)
#endif
    // The rule table, for GET /api/rules. Handed over AFTER the built-ins
    // were counted and the card was read, so the console's list matches the
    // engine from the first request rather than filling in a second later.
    webApi->attachRules(rules, builtinRules);
    apiIp  = webApi->begin();
    webApi->attachClockState(&clockState);
    Serial.printf("[app] API: %s\n", apiIp.length() ? apiIp.c_str() : "ECHEC WiFi");

    // ---- THE EYES OPEN — the robot has an IP (user 08-04: "keep sleepy
    // until StackChan acquires an IP"). begin() has just associated (STA) or
    // stood up the fallback AP, both of which end in an address someone can
    // type; either way there is now something to wake up FOR. The one path
    // that reaches here with no IP at all (radio failure) wakes too: staying
    // asleep forever would read as a dead robot, and the serial line above
    // already named the fault.
    brain->post({ CmdType::SetEmotion, (int32_t)Normal, 1200 });
#ifdef SCE_USE_SERVO
    vTaskDelay(pdMS_TO_TICKS(700));           // the eyes open at HOME
    brain->setHeadFollowHold(false);          // the neck rejoins the gaze
#endif
    Serial.printf("[app] reveil termine : %s\n",
                  apiIp.length() ? apiIp.c_str() : "sans reseau");

    // ---- WALL CLOCK (08-01). The companion never had one, and two features
    // quietly depended on having one: the night chirp volume (backlog T9,
    // "never observed at night") and anything that wants to know the date.
    // UTC, no timezone, deliberately: the only consumer is `sce::isNight`,
    // which takes a UTC instant and the robot's own lat/lon and answers from
    // the SUN. A timezone would be a second thing to configure and a second
    // thing to get wrong — the sun does not observe daylight saving.
    // ASYNCHRONOUS: configTime returns immediately and the SNTP client syncs
    // in the background. Nothing here waits on it; every consumer already
    // guards on `sce::clockSynced()`, so an unsynced clock simply
    // means "not night" until the first packet lands.
    // AP mode gets no NTP and cannot: there is no route out.
    startNtp();

    // NB: BLE (control channel #2) was ATTEMPTED then REVERTED on 2026-07-21 —
    // NimBLEDevice::init crashed at boot while coexisting with WiFi (boot-loop,
    // manual recovery through download mode). The design is kept
    // (docs/reference/PLUGINS.md §BLE), to be picked up again in a dedicated
    // session with physical access.

    if (board.py32().isDetected()) {
        leds = new EmotionLeds(board.py32(), tuning);
        // ASSERT the ring, do not merely react to it. `update()` blacks the
        // LEDs only when IT had lit them (`_wasOn`), which starts false — so a
        // companion booting with `leds` off never writes them at all, and
        // whatever the previous firmware left burning stays burning. A WS2812
        // latches: it keeps its last colour across a reboot AND across a
        // reflash. That is how a guest bin left twelve LEDs stuck white on the
        // robot (08-18), and it is not the guest's bug alone to fix — a guest
        // can be a third-party binary, or one the watchdog killed before any
        // cleanup of its own could run. The firmware that owns the hardware is
        // the one that must state its condition at startup.
        leds->forceOff();
    }

    // ---- P5b: touch interactions ----
    // Head touch (Si12T): a stroke → Happy 3 s + left wink; a forward slide →
    // right wink. Everything goes through the CommandQueue (rule A2.7).
    if (board.hasSi12T() && headTouch.begin(Wire1, 3)) {
        // A head stroke is a manual interaction → it WINS (rule A2.5):
        // AbortDance FIRST, otherwise the keyframes of the running dance
        // re-apply their own emotion on every tick and overwrite the reaction.
        headTouch.onPress = []() {
            brain->post({ CmdType::AbortDance });
            brain->post({ CmdType::SetEmotion, (int32_t)Happy, 3000 });
            // RANDOM winking eye (user: no longer always the same on press).
            brain->post({ (esp_random() & 1) ? CmdType::WinkLeft
                                             : CmdType::WinkRight });
            // ...AND THE BLACKBOARD, which is the part that was missing. Until
            // this line the stroke went STRAIGHT to the CommandQueue and left
            // no trace anywhere a rule could see, so the one genuinely intimate
            // interaction this robot has was the only one `rules.txt` could not
            // react to — and the reply above could not differ from one
            // personality to the next, which is precisely what a personality is
            // for. Published from loop() like every other field (A2.6: the
            // Si12T poll lives there).
            //
            // A STATE, not an event: 1 while a hand is on the head, 0 when it
            // leaves. That is what the engine can work with — `sustainMs` then
            // expresses "held for a moment" on its own, and the return to 0 is
            // what re-arms the rule, so one stroke fires once however long it
            // lasts. An event counter would need every rule to know its own
            // last value.
            fields.set("head", 1.0f);
        };
        headTouch.onRelease = []() { fields.set("head", 0.0f); };
        headTouch.onSwipeBackward = []() {   // stroke front → back
            brain->post({ CmdType::AbortDance });
            brain->post({ CmdType::SetEmotion, (int32_t)Happy, 3000 });
        };
        headTouch.onSwipeForward = []() {
            brain->post({ CmdType::AbortDance });
            brain->post({ CmdType::SetEmotion, (int32_t)Glee, 2000 });
            brain->post({ CmdType::WinkRight });
        };
        Serial.printf("[app] Si12T actif (caresse tête)\n");
    }

    // Screen gestures: taps → blink/short dances; L/R swipes → emotion ±1;
    // swipe UP → random dance; swipe DOWN → reserved for the Launcher (T3).
    screenTouch.onTapCenter = []() { brain->post({ CmdType::Blink }); };
    screenTouch.onTapLeft   = []() { brain->post({ CmdType::WinkLeft }); };
    screenTouch.onTapRight  = []() { brain->post({ CmdType::WinkRight }); };
    // Horizontal swipes: band area (y >= 160) → cycle the modes; eye area →
    // emotion ±1 (swipeEmotion: AbortDance + SetEmotion, A2.5).
    screenTouch.onSwipeLeft = []() {
        if (screenTouch.startY() >= units::EYEZONE_H) cycleBandMode(+1);
        else                                          swipeEmotion(+1);
    };
    screenTouch.onSwipeRight = []() {
        if (screenTouch.startY() >= units::EYEZONE_H) cycleBandMode(-1);
        else                                          swipeEmotion(-1);
    };
    // Vertical swipes: EYE AREA = dance / launcher; BAND AREA = the timer's
    // setting gesture (08-04: swipe up/down on the HH half or the MM half).
    // The split at x=160 is deliberately COARSE — two generous halves beat
    // four precise digit hitboxes on a 40 px tall strip. Before the timer
    // modes existed, band-area vertical swipes were inert (review 07-25);
    // they still are in every other mode.
    auto bandAdjust = [](int dir) {
        // A drag that already scrolled ends in a swipe-shaped release:
        // swallowed, or the last flick would add one unit nobody asked for.
        if (_bandDragConsumed) { _bandDragConsumed = false; return true; }
        const int m = (int)(tuning.band_mode + 0.5f);
        // The SOUND band no longer answers a vertical swipe either (user
        // 08-25): a TAP anywhere on it cycles the three skins — see
        // onTapBand, next to the pomodoro's, since both are now taps.
        if (m == 2) return false;
        // THE POMODORO'S SHAPE, on the gesture that fits how rarely it is
        // changed (user 08-25, third pass — the design question was asked
        // properly this time). Ranked by frequency, start/pause happens many
        // times a session and the shape maybe once a fortnight; binding the
        // rare act to the big obvious target and making the common one require
        // aim was backwards. So the TAP went back to being the primary action
        // everywhere (onTapBand) and the shape moved here.
        //
        // IDLE OR DONE ONLY, and inert otherwise: a swipe on a 40 px strip is
        // awkward enough that it must never be able to disturb a running
        // session, and between sessions is the one moment aim does not matter.
        // The four shapes carry work, break, cycles AND hydration
        // (BandTimer::preset) — the console remains the real home for all
        // four, with labels and room to explain them.
        if (m == 5) {
            using P = BandTimer::Phase;
            const P ph = bandTimer.phase();
            if (ph != P::Idle && ph != P::Done) return true;   // consumed, inert
            const BandTimer::Preset& p = BandTimer::preset(
                BandTimer::nextPreset(tuning.pomo_work_min,
                                      tuning.pomo_break_min,
                                      tuning.pomo_cycles,
                                      tuning.pomo_hydra_min, dir));
            tuning.pomo_work_min  = (float)p.workMin;
            tuning.pomo_break_min = (float)p.breakMin;
            tuning.pomo_cycles    = (float)p.cycles;
            tuning.pomo_hydra_min = (float)p.hydraMin;
            bandTimer.reset();          // Done -> Idle; from Idle a no-op
            if (webApi) webApi->requestSave();
            return true;
        }
        if (m != 4) return false;            // only the TIMER is settable
        if (screenTouch.startX() < units::SCREEN_W / 2)
            bandTimer.addMinutes(dir);
        else
            bandTimer.addSeconds(dir);
        return true;
    };
    screenTouch.onSwipeUp = [bandAdjust]() {
        if (screenTouch.startY() >= units::EYEZONE_H) { bandAdjust(+1); return; }
        brain->post({ CmdType::PlayDance,
                      (int32_t)(esp_random() % dances::count()) });
    };
    screenTouch.onSwipeDown = [bandAdjust]() {
        if (screenTouch.startY() >= units::EYEZONE_H) { bandAdjust(-1); return; }
        _launcherRequested = true;
    };
    // Band taps. ALARM-CLOCK PATTERN (user 08-04: the swipe-only setting was
    // tedious): while the TIMER is editable, the band is a keypad — left
    // third = hours, right third = minutes, tap ABOVE the digits = +1,
    // BELOW = −1, and the centre keeps the primary action. Everywhere else
    // (running, ringing, pomodoro) the WHOLE band stays the primary action:
    // an alarm must stop on the first tap, not on a well-aimed one.
    screenTouch.onTapBand = []() {
        // A drag that already scrolled must not ALSO tap on release.
        if (_bandDragConsumed) { _bandDragConsumed = false; return; }
        const int m = (int)(tuning.band_mode + 0.5f);
        // THE SOUND BAND: a tap cycles its three skins (wave, columns,
        // matrix) — user 08-25, replacing the vertical swipe. The whole band
        // is the target because the visualiser has no parts to aim at: it is
        // one picture, and "tap the picture to redress it" needs no rule.
        //
        // Written into `tuning` and therefore PERSISTED, deliberately: it is
        // the same value the console slider writes, so a skin chosen with a
        // finger survives the reboot exactly as one chosen in a browser. The
        // SD write is debounced by loop() (A2.16) — a finger cycles three
        // styles faster than the card can be written, and the renderer must
        // not be paused once per tap.
        if (m == 2) {
            const int n = 3;
            tuning.band_sound = (float)(((int)(tuning.band_sound + 0.5f) + 1) % n);
            if (webApi) webApi->requestSave();
            return;
        }
        if (m != 4 && m != 5) return;
        // bandTimer.settable(), not a second copy of "Idle or Paused":
        // the machine refuses to edit anything but Idle, so routing Paused
        // into the keypad here made a press outside the centre third do
        // nothing at all instead of resuming (fix 08-25).
        if (m == 4 && bandTimer.settable()) {
            const int x = screenTouch.startX();
            const int d = (screenTouch.startY() < units::EYEZONE_H + 40)
                        ? +1 : -1;
            if (x < 130) { bandTimer.addMinutes(d); return; }
            if (x > 190) { bandTimer.addSeconds(d); return; }
        }
        // TAP = THE PRIMARY ACTION, ANYWHERE ON THE BAND (user 08-25, third
        // pass). Start, pause, resume, acknowledge — the thing you do many
        // times a session needs no aim at all, which is the rule this band
        // already stated for the alarm: it must stop on the FIRST tap, not on
        // a well-aimed one. The shape moved to a vertical swipe (bandAdjust).
        //
        // ONE EXCEPTION, and it earns its place: a tap ON THE PHASE ICON, to
        // the left of the digits, SKIPS the current phase. That is the verb
        // this band never had — "this block is done early", "I do not want
        // this break" — and until now its only expression was a reset, which
        // throws the session away. A small deliberate target is right here
        // for the same reason it was wrong for start/pause: skipping by
        // accident is the failure that matters.
        //
        // The boundary comes from the layout itself (units §7), so it follows
        // the digits as they slide, and everything left of the first digit
        // counts — the stamp is 18 px, the target is the whole left margin.
        if (m == 5) {
            using P = BandTimer::Phase;
            const P ph = bandTimer.phase();
            const bool running = (ph == P::Work || ph == P::Break ||
                                  ph == P::Hydrate);
            char cur[24] = "";
            fields.getS("tmr", cur, sizeof(cur));
            const int split = cur[0] ? units::bandTextX0((int)strlen(cur)) : 0;
            if (running && screenTouch.startX() < split) {
                bandTimer.skip(millis());   // event staged, loop() reacts
                return;
            }
        }
        bandTimer.tap(millis());
    };
    // HOLD (0.7-3 s, still, released in place) = RESET (user 08-04): back
    // to Idle from any phase — countdown emptied, setting kept. The 3 s
    // ceiling and the stillness test are TouchGestures' own guard: a
    // resting thumb or a carried robot holds longer or drifts, and stays
    // swallowed as before.
    // Every long contact that started in the band clears the scroll flag,
    // including the ones that produce neither a tap nor a swipe (review
    // 08-04: it latched and ate the next tap).
    // TWO FINGERS, THREE SECONDS, ON THE FACE: show or hide the debug row.
    // The gesture exists for the case where the console is out of reach —
    // the robot is on its access point and you need its address, or it is on
    // a network and you want to read the address off the glass rather than
    // hunt for it. It writes the SETTING (persisted, debounced) rather than a
    // runtime flag, so the answer survives the reboot; the AP override still
    // sits on top, which is why hiding it while on the AP does nothing
    // visible. That is honest — on the AP the address is not optional.
    screenTouch.onTwoFingerHoldEyes = []() {
        tuning.band_debug = (tuning.band_debug >= 0.5f) ? 0.0f : 1.0f;
        if (webApi) webApi->requestSave();
    };
    screenTouch.onBandDragEnd = []() { _bandDragConsumed = false; };
    screenTouch.onHoldBand = []() {
        const int m = (int)(tuning.band_mode + 0.5f);
        if (m == 4 || m == 5) { bandTimer.reset(); _bandDragConsumed = false; }
    };
    // THE SCROLL (user 08-04, third revision of this gesture and the good
    // one: "hold and slide — up increases, down decreases"). One unit per
    // 20 px, applied LIVE while the finger moves (the band redraws each
    // change: the value follows the finger), accumulator reset by the
    // press frame (dyTotal == 0). The finger may leave the band upward for
    // long runs — the routing keys on where the gesture STARTED. A drag
    // that applied at least one unit swallows the release's tap/swipe.
    screenTouch.onBandDrag = [](int x, int dyTot) {
        if (dyTot == 0) { _bandDragSteps = 0; return; }   // new touch
        const int m = (int)(tuning.band_mode + 0.5f);
        if (m != 4) return;
        // Same single predicate as the keypad above. Accepting Paused here
        // set `_bandDragConsumed` while the setters refused the edit, so a
        // brush on a paused countdown swallowed the tap meant to resume it.
        if (!bandTimer.settable()) return;
        const int steps = -dyTot / 20;          // up = increase
        const int d = steps - _bandDragSteps;
        if (!d) return;
        _bandDragSteps    = steps;
        _bandDragConsumed = true;
        if (x < units::SCREEN_W / 2) bandTimer.addMinutes(d);
        else                         bandTimer.addSeconds(d);
    };

    board.pollSlowSensors();   // warm sensor cache before the 1st /api/status

    Serial.printf("[app] companion prêt — cerveau autonome + API + touch\n");
}

// -----------------------------------------------------------------------
// loop — NO animation logic left at all: heartbeat, status band (posted),
// P0 LED test (→ EmotionLeds P5), and the demo CRT toggle (35 s) posted as a
// Command, exactly the way the API will do it.
// -----------------------------------------------------------------------
void loop() {
    uint32_t now = millis();
    // The trace follows the tuning key EVERY tick: `POST /api/tuning?debug=1`
    // is then live within a frame, no reboot, no dedicated plumbing.
    sce::trace::on = tuning.debug > 0.5f;

    // M5.update() (buttons + the FT6336 SCREEN TOUCH over I2C): rate capped at
    // 30 ms — M5Unified's internal touch throttle is only 4 ms (review
    // 2026-07-17: the 10 ms loop pushed the touch I2C polling from 20 to
    // 100 Hz on the shared bus, exactly the contention the Si12T throttle
    // avoids).
    static uint32_t lastM5 = 0;
    bool m5Tick = (now - lastM5 >= 30);
    // M5.update() reads the FT6336 touch on the 11/12 bus shared with the IMU
    // (Brain, core 1) → under the bus lock.
    if (m5Tick) { lastM5 = now; sce::i2cbus::Guard g; M5.update(); }

    // Touch interactions (P5b) — light polling, callbacks → CommandQueue.
    // Si12T: the HISTORICAL 50 ms rate is kept despite the 10 ms loop
    // (2026-07-16) — its I2C bus (Wire1) is SHARED with M5Unified's In_I2C
    // (IMU at 100 Hz on the Brain side): no point quintupling the traffic and
    // the collisions (Wire 263) just for head touch.
    static uint32_t lastHeadTouch = 0;
    if (now - lastHeadTouch >= 50) {
        lastHeadTouch = now;
        headTouch.update();
    }
    // screenTouch consumes the M5.Touch state: same rate as M5.update()
    if (m5Tick) screenTouch.update();
    // TOUCH CROSSHAIRS: loop() reads the panel, the renderer paints (rule 1).
    // `getTouchPointRaw` CLAMPS an index it cannot serve back onto point zero,
    // so the count is the guard — without it a single finger would be drawn
    // twice and the overlay would claim a second contact that is not there.
    if (m5Tick) {
        const uint8_t np = M5.Touch.getCount();
        uint32_t mk[2] = { 0, 0 };
        for (uint8_t i = 0; i < 2 && i < np; ++i) {
            auto& r = M5.Touch.getTouchPointRaw(i);
            if (r.x >= 0 && r.y >= 0)
                mk[i] = sce::Renderer::markOf(r.x, r.y);
        }
        renderer.setTouchMarks(mk[0], mk[1]);
    }
    // Captive portal (P6): AP DNS → embedded console
    webApi->update();

    // ---- Launcher (T3, §3.6): swipe down → blocking UI ----
    // Sequence: eyes closing (staging) → renderer.pause() (the Launcher
    // becomes the owner of the screen) → UI → resume + wake-up.
    if (_launcherRequested) {
        _launcherRequested = false;
        if (board.hasSD()) {
            // EXCLUSIVE LAUNCHER MODE (user 07-26): only what serves the
            // launcher stays alive (touch, SD, screen). Everything else is
            // suspended: Brain (animations/reflexes/dances/sounds — commands
            // are DROPPED), servos (torque released = mechanical silence),
            // LEDs (off — a blocked loop would freeze them lit), camera
            // (inhibited: the SD SPI is monopolized by the menu/flash).
            brain->post({ CmdType::AbortDance });
            brain->post({ CmdType::SetEmotion, (int32_t)Sleepy, 1500 });
            vTaskDelay(pdMS_TO_TICKS(400));   // let the eyelids fall
            brain->setSuspended(true);
            // ACKNOWLEDGED: the menu can flash a guest, and once it does
            // there is no companion left to release anything.
            releaseServosForHandover("launcher");
            // AND THE RAIL, which this path had simply never cut (review
            // 08-03). Torque is a register INSIDE the SCS0009 and they power
            // up with it ENABLED, so a release a reboot erases is no release:
            // the menu can flash a guest, and that guest would inherit a
            // locked, warm, hand-immovable neck - the verbatim 08-02 symptom
            // this whole mechanism exists to remove. The rail was cut on the
            // API path only, i.e. NOT on the path users actually take.
            // Cut HERE and not deeper: launcher.run() blocks until it either
            // flashes (no return) or is dismissed, and the enableServoPower()
            // below already restores it in the second case - the two halves
            // were written expecting a cut that was never there.
            cutServoRailForFlash();
            if (leds) leds->forceOff();
            camera.setInhibited(true);
            camera.waitCaptureIdle();
            renderer.pause();
            launcher.run(&renderer);          // blocking; restarts if flashed;
                                              // self-heals if the pause is stolen
            renderer.resume();
            camera.setInhibited(false);
#ifdef SCE_USE_SERVO
            // run() RETURNED, so nothing was flashed and we are still alive:
            // give the servos their supply back before asking for torque.
            // Engaging into a cut rail is a silent no-op that leaves the neck
            // dead for the session (review 08-03).
            // NO Guard here (review 08-03): setBit/clearBit take the bus
            // lock per transaction, and enableServoPower ends in delay(300).
            // Holding the I2C lock across a delay is the one thing A2.21
            // forbids outright — it stalled the Brain's 100 Hz IMU read for
            // 300 ms on every launcher exit, which is exactly the VOR freeze
            // the per-transaction lock exists to prevent. Board.h's boot call
            // site has always called it bare.
            board.py32().enableServoPower();
            // Re-LEARN the pose before re-engaging (review 08-04): the head
            // sagged during the menu — minutes of limp time — and engaging on
            // the pre-launcher COMMANDED pose made the Brain's next move jump
            // the whole gap in one write: the boot bang, on every launcher
            // exit. The Brain is still suspended here, so no moveTo can race
            // the rebase.
            if (servoMotionPtr) servoMotionPtr->rebaseAndEngage();
#endif
            brain->setSuspended(false);
            brain->post({ CmdType::SetEmotion, (int32_t)Normal, 0 });
            brain->post({ CmdType::Blink });  // wake-up
            Serial.printf("[app] launcher: sortie\n");
        } else {
            Serial.printf("[app] launcher: pas de SD\n");
        }
    }

    // Slow sensors (AXP2101 battery, INA226, LTR-553): refreshed at a low rate
    // INSIDE loop() (blocking I2C, shared bus) and cached in Board — so
    // /api/status and /api/sensors serve the cache without touching the bus
    // from their AsyncTCP callbacks. Low-charge alert: band below 15 %.
    static uint32_t lastSensPoll = 0;
    if (now - lastSensPoll >= 5000) {
        lastSensPoll = now;
        board.pollSlowSensors();
    }
    int8_t battPctCached = board.cachedBattery();
    bool   battChgCached = board.cachedCharging();
    bool battLow = battPctCached >= 0 && battPctCached <= 15 && !battChgCached;

    // SCREEN BRIGHTNESS — ONE decision, two sources. Either the light sensor
    // drives the backlight (auto_brightness) or `screen_bright` does; the
    // target is computed in one place and POSTED to the renderer, which is the
    // only task allowed to touch the panel (A2.1), and only on a noticeable
    // change (A2.2: setBrightness is a PMIC I2C transaction, never per frame).
    //
    // `screen_bright` is the PANEL. `eye_color_dim` is the eye palette and
    // nothing else — it leaves the status band, the launcher and every guest
    // bin at full blast, which is why a screen-wide knob had to exist too.
    //
    // TWO CLOCKS, one decision. The SENSOR is read on its own slow cadence —
    // its EMA is calibrated on a 2 s step and a faster poll would change the
    // time constant. A MANUAL value is applied as soon as it changes: a slider
    // whose panel answers two seconds later reads as a broken control. Either
    // way the PMIC transaction only happens on a noticeable step.
    static uint32_t lastBrMs   = 0;
    static int      lastBright = -1;
    static float    lightEma   = -1.0f;
    const bool autoBright = (tuning.auto_brightness >= 0.5f && board.hasLtr553());
    if (!autoBright || now - lastBrMs >= 2000) {
        lastBrMs = now;
        int target;
        if (autoBright) {
            // Light read from the CACHE (refreshed by pollSlowSensors) — no
            // duplicate I2C read here. cachedLight = 0..100. SMOOTHED (EMA
            // a=0.4) so one isolated abnormal reading does not make the screen
            // jump. The floor of 24 keeps the panel readable in a dim room.
            const float lt = (float)board.cachedLight();
            lightEma = (lightEma < 0.0f) ? lt : lightEma + 0.4f * (lt - lightEma);
            target = 24 + (int)(lightEma * (255 - 24) / 100.0f);
        } else {
            lightEma = -1.0f;                 // re-seed when the sensor returns
            target = (int)(tuning.screen_bright + 0.5f);
        }
        if (target < 10)  target = 10;        // never black: the only control
        if (target > 255) target = 255;       // that could hide itself
        if (lastBright < 0 || abs(target - lastBright) > 6) {
            lastBright = target;
            renderer.setBrightness((uint8_t)target);
        }
    }

    // ---- Sensor night → FIELDS (light, dark_sleepy). The LOGIC (thresholds,
    // 6 s debounce, hysteresis, AmbientDark) lives in the RULE ENGINE
    // (behavior/RuleEngine.h, wired at boot). 07-21 migration: loop() is now
    // only a SOURCE of fields — no more ad-hoc state machine (the deletion
    // itself proves the "sources → fields → rules" contract holds). ----
    bool darkOpt = board.hasLtr553() && tuning.dark_sleepy >= 0.5f;
    fields.set("dark_sleepy", darkOpt ? 1.0f : 0.0f);
    if (board.hasLtr553()) fields.set("light", (float)board.cachedLight());
    if (rules) rules->update(now);

    // ---- PERSONALITY (behavior/Personalities.h) --------------------------
    // loop() is the ONLY applier, like every other persisted choice — the
    // AsyncTCP callback that received /api/tuning wrote a float and nothing
    // else (A2.6), and the SD read below has to happen here anyway.
    //
    // The dance is aborted and the face returned to Normal BEFORE the rules are
    // swapped: a timeline started by the outgoing character has no business
    // finishing under the incoming one, and its keyframes would keep reapplying
    // their own emotions over the new personality's first choices.
    {
        const int want = currentPersonality();
        if (want != _personality) {
            applyPersonality(want, /*boot=*/false);
            brain->post({ CmdType::AbortDance });
            brain->post({ CmdType::SetEmotion, (int32_t)Normal, 1200 });
            loadRulesForPersonality();   // SD read, already in loop()
            if (webApi) webApi->requestSave();
        }
    }
    // The roulette switch is pushed every pass like the other toggles: one
    // atomic-ish bool store, and it keeps working when the key is changed by
    // any route (API, console, config.yaml reload) without each of them having
    // to remember to notify anyone.
    brain->roulette().setEnabled(tuning.roulette >= 0.5f);

    // Status band config (Tuning → renderer, hot): icon mask + size of the
    // say/alert text. Atomic stores, negligible cost.
    // Clamp BEFORE the cast: /api/tuning accepts arbitrary floats, and a
    // negative float -> unsigned integer is UNDEFINED behaviour in C++.
    float im = clampVal(tuning.icon_mask,       0.0f,  31.0f);
    float bt = clampVal(tuning.band_text_size,  1.0f,   3.0f);
    float bs = clampVal(tuning.band_scroll_speed, 5.0f, 400.0f);
    // band_mode: SANITIZED to {0,2,3,4,5} — /api/statusbar validates, but
    // /api/tuning?band_mode=… and config.yaml do NOT go through that filter
    // (e.g. band_mode=1, the removed ex-DEBUG mode: silent band + an
    // inconsistent state persisted). Unknown value → 0 (none), re-persisted.
    int bm = (int)(tuning.band_mode + 0.5f);
    if (bm != 0 && bm != 2 && bm != 3 && bm != 4 && bm != 5) {
        bm = 0; tuning.band_mode = 0.0f;
    }
    renderer.setStatusBar(bm);                               // persisted mode
    renderer.setIconMask((uint32_t)(im + 0.5f));
    renderer.setSayTextSize((uint8_t)(bt + 0.5f));
    renderer.setScrollSpeed((uint16_t)(bs + 0.5f));
    // THE ACCESS POINT FORCES IT ON, and this is the one override in the
    // whole band. A robot on its fallback AP is a robot nobody can reach:
    // there is no name to resolve, the console is the only way to give it a
    // network, and the address of that console exists in exactly two places —
    // the serial line, which needs a cable, and this row of pixels. Leaving
    // it behind a setting means the one situation where the IP is
    // indispensable is also the one where it may be switched off.
    //
    // It OVERRIDES rather than WRITES: `tuning.band_debug` is untouched, so
    // nothing is persisted and the moment the robot joins a real network the
    // display goes back to whatever the user chose. An override that saved
    // itself would silently rewrite a preference because the WiFi was down
    // once (the class of bug the `imu_*` rename was about: a value that comes
    // back changed and nobody knows which layer did it).
    const bool apForcesDebug = webApi && webApi->isApMode();
    renderer.setBandDebug(apForcesDebug || tuning.band_debug >= 0.5f);
    renderer.setBandClock(tuning.band_clock >= 0.5f);
    float ss = clampVal(tuning.band_sound, 0.0f, 2.0f);
    renderer.setSoundStyle((uint8_t)(ss + 0.5f));
    // The analyser lives on THIS task, so this is a plain member write and
    // not a queued command. It bounds the value itself — see setGain().
    if (soundViz) soundViz->setGain(tuning.band_sound_gain);

    // ---- Status band: we PUSH FIELDS (blackboard), the RENDERER draws
    // (rule 1: a single owner of the screen). Single contract — those same
    // fields feed the widgets and (soon) the rules. ----
    // Mics: every iteration (responsive VU). Plain linear normalization.
    bool micOn = soundTracker && soundTracker->state() == SoundTracker::MIC_ACTIVE;
    // ATTACK INSTANT, RELEASE SLOW — and it lives HERE, at the source, not in
    // the renderer. A2.15 is explicit: the Brain (app side) is the sole place
    // continuous channels are smoothed, never Renderer/EyeRig. The band's mouth
    // needs the classic VU envelope to look alive — a raw RMS at 100 Hz reads
    // as noise, and a symmetric filter swallows the consonants that give speech
    // its shape — so the envelope is applied where the field is PUBLISHED and
    // the renderer keeps drawing exactly what it is handed.
    // 0.86 per 10 ms pass is about 70 ms to fall by half: fast enough to close
    // between two syllables, slow enough not to strobe.
    static float micEnvL = 0.0f, micEnvR = 0.0f;
    if (micOn) {
        const float SC = 2000.0f;   // full VU ~ RMS 2000 (adjustable)
        float l = soundTracker->rmsL() / SC, r = soundTracker->rmsR() / SC;
        if (l < 0) l = 0; if (l > 1) l = 1;
        if (r < 0) r = 0; if (r > 1) r = 1;
        micEnvL = (l > micEnvL) ? l : micEnvL * 0.86f;
        micEnvR = (r > micEnvR) ? r : micEnvR * 0.86f;
        fields.set("micL", micEnvL);
        fields.set("micR", micEnvR);
    } else {
        micEnvL = micEnvR = 0.0f;
        fields.set("micL", 0.0f); fields.set("micR", 0.0f);
    }

    // ---- Timer / pomodoro band (modes 4/5, 08-04): tick the machine,
    // publish its display through the SAME blackboard as every other band
    // widget, and turn its EVENTS into emotions — ordinary posts, so the
    // reflex rule (A2.5) still preempts an alarm face like anything else.
    {
        const int m = (int)(tuning.band_mode + 0.5f);
        bandTimer.setKind(m == 5 ? BandTimer::Kind::Pomodoro
                                 : BandTimer::Kind::Timer);
        // Console commands (POST /api/timer) are drained HERE, in the one
        // task that owns the machine — the AsyncTCP callback only filled a
        // slot (A2.6). Drained BEFORE the tick so a "tap" and the tick that
        // follows it see the same instant, the way a finger press does.
        if (webApi) {
            int sm = 0, ss = 0;
            switch (webApi->takeTimerCmd(sm, ss)) {
            case 1: bandTimer.tap(now); break;
            case 2: bandTimer.reset();  break;
            case 3:
                // addMinutes/addSeconds are the ONLY way in, so the console
                // goes through the same wrap and the same edit lock as the
                // gesture: a running countdown is not silently rewritten.
                bandTimer.addMinutes(sm - bandTimer.setM());
                bandTimer.addSeconds(ss - bandTimer.setS());
                break;
            case 4:
                // A PRESET: "start this, now". It resets first, and that is
                // the one place the edit lock is deliberately stepped over —
                // the lock exists so a stray brush cannot rewrite a running
                // timer, and pressing "10 min" is not a stray brush. Without
                // the reset a preset would do nothing while a countdown ran,
                // which is the least explicable behaviour a button can have.
                bandTimer.reset();
                bandTimer.addMinutes(sm - bandTimer.setM());
                bandTimer.addSeconds(ss - bandTimer.setS());
                bandTimer.tap(now);
                break;
            default: break;
            }
        }
        // A DANCE AT THE BELL, and only at the bell. `timer_dance` names a
        // dance by its 1-based index (0 = none); it fires on the two events
        // that mean "this is over" — Ring for a countdown, AllDone for a
        // pomodoro — and never on a phase change. Work/break/hydrate happen
        // every few minutes, and a robot that stands up and dances every few
        // minutes is a robot you unplug. Posted AFTER the emotion so the
        // dance's own gaze work is the last word, and through the queue like
        // every other mutation (A2.4).
        //
        // THE INDEX IS INTO `/api/dances`, WHICH IS THE MERGED LIST — built-in
        // dances first, then the SD choreographies — because that is the list
        // the console fills its selector from. Posting it straight to
        // `PlayDance` resolved only the built-in prefix: pick a `.csv` from
        // the card and the last block of a pomodoro ended in silence, with no
        // error anywhere, because `Brain` bounds-checks the index and simply
        // does nothing (found in review 08-25). The two halves resolve here,
        // in the same order the endpoint concatenates them, so the console's
        // promise that its list cannot drift from the robot's is now true.
        //
        // An index still follows its list: adding a file to /dances renumbers
        // what comes after it. That is visible rather than silent — the
        // console shows the name the saved index resolves to TODAY — and the
        // built-in prefix, which is what most people pick, never moves.
        auto endOfTimer = [&]() {
            const int d = (int)(tuning.timer_dance + 0.5f);
            if (d <= 0) return;                       // 0 = no dance
            const dances::Entry* t = dances::table();
            int n = 0; while (t[n].name) n++;
            const int i = d - 1;
            if (i < n) { brain->post({ CmdType::PlayDance, (int32_t)i }); return; }
            // SD choreography: a STATIC double-bank pointer, which is the only
            // kind allowed through the queue (A2.17, last paragraph).
            if (const DanceStore::Entry* e = danceStore.get(i - n)) {
                Command c{ CmdType::PlayCustom };
                c.i = e->count; c.u = 1 /*mirrorable*/; c.ptr = e->keys;
                brain->post(c);
            }
        };
        switch (bandTimer.tick(now)) {
        case BandTimer::Event::Ring:       // the alarm face: star eyes
            brain->post({ CmdType::AbortDance });
            brain->post({ CmdType::SetEmotion, (int32_t)Excited, 10000 });
            endOfTimer();
            break;
        case BandTimer::Event::WorkStart:
            brain->post({ CmdType::AbortDance });
            brain->post({ CmdType::SetEmotion, (int32_t)Focused, 5000 });
            break;
        case BandTimer::Event::HydrateStart:
            // Curious, not Happy: the drink prompt has to READ as a different
            // moment from the break that follows it, or the two phases are one
            // phase wearing two labels.
            brain->post({ CmdType::AbortDance });
            brain->post({ CmdType::SetEmotion, (int32_t)Curious, 5000 });
            break;
        case BandTimer::Event::BreakStart:
            brain->post({ CmdType::AbortDance });
            brain->post({ CmdType::SetEmotion, (int32_t)Happy, 5000 });
            break;
        case BandTimer::Event::AllDone:
            brain->post({ CmdType::AbortDance });
            brain->post({ CmdType::SetEmotion, (int32_t)Glee, 8000 });
            endOfTimer();
            break;
        default: break;
        }
        if (m == 4 || m == 5) {
            // MM:SS in every phase, minutes up to 99 (user 08-04, the final
            // form): one format, one meaning, seconds always visible — a
            // countdown whose digits change meaning with the value reads as
            // two different clocks.
            auto fmtMS = [](char* out, size_t n, uint32_t s) {
                snprintf(out, n, "%02lu:%02lu",
                         (unsigned long)(s / 60), (unsigned long)(s % 60));
            };
            using P = BandTimer::Phase;
            const uint32_t rs = bandTimer.remainingS(now);
            char t[24] = "", c[12];
            float st = 0.0f;
            switch (bandTimer.phase()) {
            case P::Idle:
                if (m == 4) snprintf(t, sizeof(t), "%02d:%02d",
                                     bandTimer.setM(), bandTimer.setS());
                else { fmtMS(c, sizeof(c), rs);
                       snprintf(t, sizeof(t), "1/%d %s",
                                bandTimer.cycles(), c); }
                break;
            case P::Run:    fmtMS(t, sizeof(t), rs); st = 1.0f; break;
            case P::Paused: fmtMS(t, sizeof(t), rs); st = 2.0f; break;
            case P::Work:
            case P::Break:
            case P::Hydrate:
                fmtMS(c, sizeof(c), rs);
                snprintf(t, sizeof(t), "%d/%d %s", bandTimer.cycle(),
                         bandTimer.cycles(), c);
                // Hydrate borrows the BREAK colour: it is a pause, and giving
                // it a fourth colour would say "new kind of thing" when the
                // icon already says everything that differs.
                st = bandTimer.phase() == P::Work ? 1.0f : 4.0f;
                break;
            case P::Ring:
                // The blink IS this alternation: drawDynText's change
                // detection redraws on every text change, wipes on empty.
                if ((now / 500) & 1) strlcpy(t, "00:00", sizeof(t));
                st = 3.0f;
                break;
            case P::Done:
                snprintf(t, sizeof(t), "%d/%d %s", bandTimer.cycles(),
                         bandTimer.cycles(), sce::T("DONE", "FINI"));
                st = 4.0f;
                break;
            }
            fields.set("tmr", 0.0f, t);
            fields.set("tmr_st", st);
            // THE PHASE, published beside the colour and not folded into it.
            // `tmr_st` is a COLOUR code: work and run share 1, break and done
            // share 4, and hydrate now borrows 4 as well — three phases the
            // renderer has to tell apart to pick an icon. Deriving the icon
            // from the colour would mean the drink prompt and the break wear
            // the same drawing, which is the one thing the icons exist to
            // prevent. Same blackboard, one more field (A2.5).
            fields.set("tmr_ph", (float)(int)bandTimer.phase());
            // PROGRESS 0..1 of the current phase, for the bar under the
            // digits. Computed HERE because it is a fact about the machine,
            // not about pixels: the renderer paints what it is handed (A2.15).
            // A phase with no duration (Idle, Ring, Done) reports 0 and the
            // bar reads empty rather than guessing.
            const uint32_t tot = bandTimer.phaseTotalS();
            const float pg = tot ? (float)(tot - (rs > tot ? tot : rs)) / (float)tot
                                 : 0.0f;
            fields.set("tmr_pg", pg);
        }
    }

    static uint32_t lastFields = 0;
    if (now - lastFields > 1000) {                 // slow fields at 1 Hz
        lastFields = now;
        fields.set("batt",  (float)battPctCached);
        fields.set("chg",   battChgCached ? 1.0f : 0.0f);
        fields.set("rssi",  (float)WiFi.RSSI());   // 0 in AP/disconnected
        fields.set("cam",   tuning.camera     >= 0.5f ? 1.0f : 0.0f);
        fields.set("night", brain->roulette().darkMode() ? 1.0f : 0.0f);
        fields.set("mic",   micOn ? 1.0f : 0.0f);
        fields.set("ip", 0.0f, apiIp.c_str());
        // Local wall clock for the band_clock option (mode 0). Empty until
        // NTP has spoken — an unsynced clock shows NOTHING, never 1970. The
        // offset is display-only (the robot's own night stays sun-driven).
        {
            char ck[8] = "";
            const time_t utc = time(nullptr);
            if (sce::clockSynced(utc)) {
                const float ofs = clampVal(tuning.tz_offset_h,
                                           -14.0f, 14.0f);
                uint32_t s = (uint32_t)((utc + (long)(ofs * 3600.0f)) % 86400);
                unsigned long hh = (unsigned long)(s / 3600);
                const unsigned long mm = (unsigned long)((s % 3600) / 60);
                if (tuning.clock_24h < 0.5f) {
                    // 12-hour: midnight and noon are BOTH "12", which is the
                    // whole trap of the format — 0 h is 12 AM, 12 h is 12 PM.
                    const char sfx = hh < 12 ? 'a' : 'p';
                    hh = hh % 12; if (hh == 0) hh = 12;
                    snprintf(ck, sizeof(ck), "%lu:%02lu%c", hh, mm, sfx);
                } else {
                    snprintf(ck, sizeof(ck), "%02lu:%02lu", hh, mm);
                }
            }
            fields.set("clk", 0.0f, ck);
        }
        // Low-battery alert = top-priority RED overlay (setStatus); cleared
        // when the battery is OK again (on change only).
        static int lastAlert = -999;
        int ap = battLow ? (int)battPctCached : -1;
        if (ap != lastAlert) {
            lastAlert = ap;
            if (battLow) {
                char a[32];
                snprintf(a, sizeof(a), "BATTERIE FAIBLE %d%%", (int)battPctCached);
                renderer.setStatus(a);
            } else renderer.setStatus("");
        }
    }

    // Serial heartbeat (5 s) — instrumented for the P7 endurance run:
    // heapMin = heap floor since boot (a leak = a continuous decrease),
    // stk* = remaining stack headroom in WORDS per task (danger below 128)
#ifdef SCE_USE_SERVO
    // ---- MEASURED servo pose, 5 Hz, ONLY when the servos are off ----
    // The whole point is to compare where the head IS with where it was told
    // to go: yawDeg()/pitchDeg() report the COMMANDED pose and can never
    // confirm the servo arrived. readActualDeg refuses on its own if the
    // servos are enabled; the period here just keeps the idle bus quiet.
    // The SAMPLING is done by the servo task (it owns the bus); loop() only
    // copies the published measurement into the struct the web layer reads.
    // Cleared when the task reports nothing, so /api/servo/pos says "no
    // measurement" instead of ageing a stale number while the head moves.
    // ASK at 5 Hz, and only while the servos are off: the read is a busy-wait
    // on the servo task and must not run unless someone is looking. Publishing
    // is separate — measuredPose() only reads atomics.
    if (servoMotionPtr) {
        static uint32_t poseAskMs = 0;
        if (tuning.servos < 0.5f && now - poseAskMs >= 200) {
            poseAskMs = now;
            servoMotionPtr->requestPose();
        }
        float y = 0, p = 0; uint32_t st = 0;
        if (servoMotionPtr->measuredPose(&y, &p, &st)) {
            servoPose.yaw = y; servoPose.pitch = p; servoPose.stampMs = st;
        } else {
            servoPose.stampMs = 0;
        }
    }
#endif

    // ---- RTC set FROM NTP, once (08-01). The BM8563 is on the shared 11/12
    // bus, so this is a guarded write and it happens exactly once per sync —
    // hence the flag rather than a periodic refresh.
    // Why bother when `sce::isNight` reads NTP directly: because the chip was
    // documented as "nothing in this project ever sets it", which made every
    // reading of it a trap for the next person. Now it holds UTC that is
    // actually true, and it survives a reboot — a robot that reboots at 3 am
    // out of WiFi range keeps a plausible clock instead of returning to 1970.
    if (rtcSyncPending && sntpSynced) {
        const time_t t = time(nullptr);
        if (sce::clockSynced(t)) {
            struct tm g;
            gmtime_r(&t, &g);
            {
                sce::i2cbus::Guard guard;   // BM8563 shares 11/12 with the IMU
                M5.Rtc.setDateTime({ { (int16_t)(g.tm_year + 1900),
                                       (int8_t)(g.tm_mon + 1), (int8_t)g.tm_mday },
                                     { (int8_t)g.tm_hour, (int8_t)g.tm_min,
                                       (int8_t)g.tm_sec } });
            }
            rtcSyncPending = false;
            Serial.printf("[app] NTP ok, RTC regle sur %04d-%02d-%02d %02d:%02d:%02dZ\n",
                          g.tm_year + 1900, g.tm_mon + 1, g.tm_mday,
                          g.tm_hour, g.tm_min, g.tm_sec);
        }
    }

    // ---- FORCED RESYNC (`POST /api/clock/sync`). Consumed HERE and not in the
    // callback: configTime tears the SNTP client down and back up, and the RTC
    // write that follows is I2C on the shared 11/12 bus (A2.6).
    //
    // Clearing `sntpSynced` is the point of the whole thing. Leaving it set
    // would let the block above fire on the NEXT loop pass with the old,
    // drifted `time()` and call it a sync — the exact self-deception the
    // callback was introduced to end. Cleared, the RTC is rewritten only when a
    // packet has really landed again.
    //
    // It is also the only recovery when the network arrives AFTER the wake-up
    // sequence: the boot-time arming is guarded on already having an IP.
    if (webApi && webApi->consumeClockSyncRequest()) {
        sntpSynced = false;
        startNtp();
        Serial.println("[app] resync NTP demandee par l API");
    }
    if (webApi) {
        clockState.ntpOk   = sntpSynced;
        clockState.pending = rtcSyncPending;
    }

    static uint32_t lastBeat = 0;
    if (now - lastBeat >= 5000) {
        lastBeat = now;
        uint32_t stkServo = 0;
#ifdef SCE_USE_SERVO
        if (servoMotionPtr) stkServo = servoMotionPtr->stackFreeWords();
#endif
        Serial.printf("companion alive - emotion:%s ip:%s rssi:%d sd:%d heap:%lu "
                      "heapMin:%lu psram:%lu psramMin:%lu stkBrain:%lu stkRend:%lu "
                      "stkServo:%lu stkLoop:%lu stkCam:%lu frame avg %luus max %luus "
                      "(eyes %luus band %luus push %luus %lupx) uptime:%lus\n",
                      emotionName(brain->currentEmotion()), apiIp.c_str(),
                      // STA RSSI (dBm, 0 in AP): the latency spikes coming in
                      // BURSTS in every camera state (diag 07-20) point at the
                      // radio — correlate rssi with the lag.
                      (int)WiFi.RSSI(),
                      board.hasSD() ? 1 : 0,
                      (unsigned long)ESP.getFreeHeap(),
                      // INTERNAL heap only (esp_get_minimum_free_heap_size
                      // aggregates the 8 MB PSRAM → would hide an SRAM leak)
                      (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                      // Free PSRAM + min: the JPEG copies (still/stream) live
                      // there — a leak here kills the camera/web without ever
                      // touching the internal heap (camera slowdown diagnosis
                      // 2026-07-20).
                      (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                      (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
                      (unsigned long)brain->stackFreeWords(),
                      (unsigned long)renderer.stackFreeWords(),
                      (unsigned long)stkServo,
                      (unsigned long)uxTaskGetStackHighWaterMark(nullptr),
                      (unsigned long)camera.stackFreeWords(),
                      (unsigned long)renderer.frameAvgUs(),
                      (unsigned long)renderer.frameMaxUs(),
                      (unsigned long)renderer.eyesAvgUs(),
                      (unsigned long)renderer.bandAvgUs(),
                      // The PUSH alone, carved out of the eyes stage (08-03,
                      // dirty-band push): microseconds AND pixels actually put
                      // on the wire, out of 51 200 for a full frame. A settled
                      // face should read near zero on both; if the pixels drop
                      // and the microseconds do not, the bottleneck was never
                      // the wire.
                      (unsigned long)renderer.pushAvgUs(),
                      (unsigned long)renderer.pushAvgPx(),
                      (unsigned long)(now / 1000));
    }

    // EmotionLeds + SoundFx (P5, §3.9) — off by default:
    // POST /api/tuning?leds=1 / ?sound=1 to enable them.
    if (leds) leds->update(brain->currentEmotion(), renderer.eyeColorRgb(),
                           renderer.eyeHeightL(),    // left bar ∝ left eye size
                           renderer.eyeHeightR(),    // right bar ∝ right eye size
                           // COMMANDED yaw rate, for the depth channel (§10) —
                           // what the firmware ASKED for, never what the gyro
                           // felt: a hand turning the robot must not make the
                           // bars claim the robot turned itself.
                           servoMotionPtr ? servoMotionPtr->cmdVelDegS().x : 0.0f);
    soundFx.update(brain->currentEmotion());
    if (soundTracker) soundTracker->update();   // head toward the noise (option)
    cpuLoad.update();                           // ~1 s window (console)
    // camera: capture+encoding (software VGA JPEG, ~150-300 ms) now runs on
    // its OWN DEDICATED TASK (core 0 prio 1, camera.start() above) — no longer
    // in loop(): the stream no longer saturates the loop nor HTTP.

    // Deferred persistence (T5): tuning/wifi changed through the API → SD here
    // (FAT access outside the AsyncTCP callbacks, rule A2.6).
    // renderer.pause()/resume() (rule A2.16, ROADMAP.md): SPI2 is SHARED with
    // the LCD — an SD write that coincides with a renderer push makes the card
    // protocol fail (the SD library's "no token received"/"Card Failed"
    // retries) AND freezes the display for the duration of the retries
    // (~0.7 s measured; it persists even at 15 MHz: the cause is bus
    // CONTENTION, not the frequency). The Launcher already paused the renderer
    // for its SD accesses (§3.6) — same guard here, generalized to EVERY
    // deferred SD write/read.
    // Pending credentials (POST /api/security /api/wifi): applied HERE (loop)
    // so that the shared Strings are never mutated from an AsyncTCP callback
    // while save() is reading them. Each one arms _saveReq → persistence below.
    webApi->applyPendingSecurity();
    webApi->applyPendingWifi();

    // DEBOUNCED SD persistence (~2 s after the LAST request): every save() is
    // a full YAML rewrite + a renderer pause/resume (A2.16, a brief visible
    // freeze) — a burst of mode swipes or of successive POST /api/tuning must
    // produce only ONE write (SD wear + smoothness).
    static uint32_t pendingSaveAtMs = 0;
    // The actual write — called when the debounce expires AND FORCED before
    // any reboot/power-off: a setting acknowledged with a 200 (e.g. POST
    // /api/wifi then "Restart" from the console) used to go straight to the
    // bin, and the robot came back on the OLD SSID (max review 07-27).
    auto flushConfigSave = [&]() {
        if (!pendingSaveAtMs) return;
        pendingSaveAtMs = 0;
        if (!board.hasSD()) return;
        renderer.pause(/*willPaint=*/false);   // SD only, no pixels
        bool ok = sdConfig.save(tuning);
        renderer.resume();
        Serial.printf("[app] config.yaml %s\n", ok ? "sauvee" : "ECHEC ecriture");
    };
    // The language was staged by POST /api/config?lang=. APPLIED here, not in
    // the callback: `sce::g_lang` is read by every task through `sce::T`, and
    // `sdConfig.lang` is read by save() — neither belongs to an AsyncTCP
    // callback (A2.6). One loop pass later is imperceptible; the console waits
    // for the 200 before it re-translates itself.
    const int wantLang = webApi->consumeLangRequest();
    if (wantLang >= 0) {
        sce::setLang(wantLang ? "fr" : "en");
        sdConfig.lang = sce::langCode();
        webApi->requestSave();
    }
    // The tuning trace, staged the same way and for the same reason: writing
    // it is a BLOCKING UART call, and the task that used to make it is the one
    // serving every other connection (A2.6).
    webApi->drainTuningTrace();
    if (webApi->consumeSaveRequest()) pendingSaveAtMs = millis() + 2000;
    if (pendingSaveAtMs && (int32_t)(millis() - pendingSaveAtMs) >= 0)
        flushConfigSave();

    // Deferred reboot (POST /api/update after a successful flash, or POST
    // /api/reboot): the HTTP response is already out — we restart cleanly.
    if (webApi->rebootDue()) {
        flushConfigSave();          // pending setting → SD before leaving
        Serial.printf("[app] reboot demande (OTA ou /api/reboot)\n");
        delay(100);
        ESP.restart();
    }

    // Power-off requested (POST /api/poweroff): cuts the power through the
    // AXP2101 PMIC (M5.Power.powerOff never returns).
    if (webApi->poweroffDue()) {
        flushConfigSave();          // same: do not lose the last setting
        Serial.printf("[app] extinction demandee (/api/poweroff)\n");
        webApi->clearPoweroff();   // one-shot: powerOff() DOES return on USB
        delay(100);
        { sce::i2cbus::Guard g; M5.Power.powerOff(); }   // AXP2101 = shared bus
        // On USB we land here: the request has been disarmed, so loop() keeps
        // running normally instead of spinning on powerOff (unplug the USB to
        // actually power down).
    }

    // config.yaml re-read requested by POST /api/config/reload
    if (webApi->consumeReloadRequest() && board.hasSD()) {
        renderer.pause(/*willPaint=*/false);   // SD only, no pixels
        bool ok = sdConfig.load(tuning);
        renderer.resume();
        Serial.printf("[app] config.yaml %s (wifi : au restart)\n",
                      ok ? "relue" : "ECHEC relecture");
    }

    // SD choreography reload (upload / delete / reload API)
    if (webApi->consumeDancesReloadRequest() && board.hasSD()) {
        renderer.pause(/*willPaint=*/false);   // SD only, no pixels
        danceStore.reload();
        renderer.resume();
    }
    // ---- PERSONALITY WRITE (console): create, edit or delete a character.
    // The AsyncTCP callback only staged it (A2.6); the card write happens HERE,
    // under renderer.pause() like every other deferred SD access (A2.16).
    //
    // The table is RELOADED from the directory afterwards rather than patched in
    // place: the loader is idempotent and already knows the edit-or-create rule,
    // so re-reading is both shorter and the only version of that logic. Patching
    // would be a second implementation of it, free to disagree.
    {
        sce::Personality wp;
        bool del = false;
        if (webApi->consumePersonalityWrite(wp, del) && board.hasSD()) {
            // The table's slots are reassigned by DIRECTORY ORDER on every
            // reload (PersonalityStore.h), so `tuning.personality` — a raw
            // ordinal — can point at a DIFFERENT character once a create or
            // delete reshuffles the directory. Carry the ACTIVE character's
            // NAME across the reload instead and re-resolve its new index,
            // rather than trusting the old ordinal blindly.
            char activeName[PERSO_NAME_MAX] = {0};
            if (_personality >= 0 && _personality < sce::personalities::count())
                strncpy(activeName, sce::personalities::at(_personality).name,
                        sizeof(activeName) - 1);
            renderer.pause(/*willPaint=*/false);   // SD only, no pixels
            const bool ok = del ? PersonalityStore::remove(wp.name)
                                : PersonalityStore::save(wp);
            int bad = 0;
            personalityStore.loadAll(bad);
            renderer.resume();
            Serial.printf("[perso] %s '%s' %s\n", del ? "suppression" : "ecriture",
                          wp.name, ok ? "ok" : "ECHEC");
            // Re-resolve by NAME: found -> same character, wherever its slot
            // landed. Not found (it was the one just deleted) -> fall back to
            // the compiled default rather than whichever character now
            // happens to sit at the old ordinal.
            const int idx = activeName[0] ? sce::personalities::indexOf(activeName) : -1;
            tuning.personality = (idx >= 0) ? (float)idx : 0.0f;
            // The active character may have just changed shape — re-apply it so
            // the new colour, cadence and weights take effect without a reboot,
            // and re-read its rule file in case the path moved.
            applyPersonality(currentPersonality(), /*boot=*/false);
            loadRulesForPersonality();
        }
    }

    if (webApi->consumeRulesReloadRequest() && board.hasSD()) {
        // Reloads the ACTIVE personality's file, not the default one: after a
        // switch to Haro, "reload the rules" from the console has to mean the
        // rules the robot is actually running, or the button would quietly
        // reinstate the default character's behaviour.
        renderer.pause(/*willPaint=*/false);   // SD only, no pixels
        loadRulesForPersonality();             // reset to built-ins + SD
        renderer.resume();
    }

    // /bins/ name cache, rebuilt HERE because it reads the card: the launch
    // route validates a name against it instead of calling SD.exists() from an
    // AsyncTCP callback (A2.6). Paused like every other deferred SD access —
    // SD and LCD share SPI2 (A2.16).
    // The dirty flag is tested BEFORE the pause, never inside it: pause() waits
    // for the frame in flight, so pausing on every pass to ask "is there
    // anything to do" throttles the renderer permanently (flickering status
    // band, frame avg 41 ms — user 08-03). Same shape as the blocks above.
    // `willPaint=false`: this reads the card, it never touches the panel.
    if (webApi && webApi->sdWorkPending()) {
        renderer.pause(/*willPaint=*/false);   // SD only, no pixels
        webApi->serviceSdWork();
        renderer.resume();
        Serial.println("[app] travail SD differe servi");
    }

    // IS THE CARD STILL THERE? It used to be asked once, at boot, and the
    // answer believed for the rest of the run — so pulling the card left
    // `/api/status` reporting `sd:1`, the console offering files, and a 1.7 MB
    // upload writing into a stale mount (measured 08-03: a 3.4 s renderer
    // freeze on the timeout, and an "import failed" that reads like a card
    // fault rather than an absent card).
    //
    // EVERY 3 s, not every pass: it touches SPI2 and the renderer has to be
    // paused around it (A2.16). Three seconds is chosen against what the answer
    // is FOR — a human swapping a card and then asking the console — not
    // against how fast a card can be pulled.
    // While the card is absent the same tick attempts a REMOUNT, because a flag
    // that only ever goes false would make the first removal permanent until
    // the next boot. `end()` + `begin()` and not a re-probe: the driver holds a
    // mount that no longer describes anything.
    // TWO CADENCES, not one (review 08-03). The cheap probe is a directory
    // open and is worth doing often; the REMOUNT is `SD.end()` + `SD.begin()`
    // and, on a board that simply has no card — a Fire, a StackChan someone
    // runs bare — it times out against an empty slot EVERY time, with the
    // renderer paused, for the whole session. Board.h's own comment already
    // said "polling a remount on a board that simply has no card would pay for
    // it forever", and the first version of this block did exactly that.
    // So: probe at 3 s while the card is there, retry the mount at 10 s while
    // it is not. Inserting a card is a human act; ten seconds is not a wait.
    {
        static uint32_t lastSdProbe = 0;
        // NEVER-SEEN BACKOFF (review 08-04): a StackChan deliberately run
        // without a card paid a failed SD.begin() — bus init plus CMD0
        // retries, renderer paused around it — every 10 s for the whole
        // session, a periodic visible hitch answering a question whose
        // answer is almost always no. While NO card has ever been seen this
        // session the retry interval doubles, 10 s up to 60 s; the first
        // successful mount pins it back to 10 s forever, because from then
        // on an absence really is a removal.
        static uint32_t absentEveryMs = 10000;
        static bool     sdEverSeen    = false;
        const uint32_t nowMs   = millis();
        const bool     present = board.hasSD();
        if (present) sdEverSeen = true;
        const uint32_t every   = present ? 3000
                               : sdEverSeen ? 10000 : absentEveryMs;
        // NOT UNDER AN ACTIVE UPLOAD (review 08-04): remountSD() is
        // SD.end() + SD.begin(), and the AsyncTCP task may hold _uploadFile
        // OPEN mid-stream — ending the filesystem under it is a write into a
        // torn-down mount. The probe also stays out: a transient open("/")
        // failure under heavy concurrent I/O would read as a removal. The
        // card question can wait ten seconds; the upload cannot be replayed.
        if (nowMs - lastSdProbe > every && !(webApi && webApi->uploadBusy())) {
            lastSdProbe = nowMs;
            renderer.pause(/*willPaint=*/false);   // SD only, no pixels
            const bool changed = present ? board.probeSD() : board.remountSD();
            renderer.resume();
            if (!present && !changed && !sdEverSeen && absentEveryMs < 60000)
                absentEveryMs *= 2;
            if (changed && webApi) {
                webApi->setSdPresent(board.hasSD());
                // A card that has just arrived carries a /bins/ we have never
                // listed, and one that has just left leaves a cache promising
                // files that are gone — either way the list is a lie now.
                webApi->binsChanged();
                // INSIDE its own bracket, not after the resume() seven lines
                // up (review 08-03): reload() opens the directory and parses
                // every CSV, and SD shares SPI2 with the LCD (A2.16). The
                // correct shape is 55 lines above, in this same file.
                if (board.hasSD()) {
                    sdEverSeen = true;
                    renderer.pause(/*willPaint=*/false);   // SD only, no pixels
                    danceStore.reload();
                    int n = ruleStore.load(*rules, builtinRules);
                    renderer.resume();
                    // THE NEW CARD IS NOT THE OLD CARD (review 08-04). The
                    // dances and the /bins cache were reloaded, but the rule
                    // engine kept running the PREVIOUS card's rules — hence
                    // the load() above — and a pending tuning autosave would
                    // have written the OLD card's config.yaml over the file
                    // the user just prepared on the PC: the debounce is
                    // cancelled instead. Tuning itself is NOT re-read: live
                    // settings the user changed by API must not be silently
                    // reverted by an insertion; /api/config/reload exists
                    // for that intent.
                    pendingSaveAtMs = 0;
                    Serial.printf("[app] carte remplacee : regles rechargees "
                                  "(%d), autosave annulee\n", n);
                }
            }
        }
    }

    // .bin launch requested by the API (T5, §3.6): flashing outside AsyncTCP.
    // Size guard against the OTA partition, like the touch launcher.
    vTaskDelay(pdMS_TO_TICKS(10));
}
