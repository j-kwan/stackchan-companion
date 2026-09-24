#pragma once
// =============================================================================
// SoundTracker.h — StackChan-Companion (app)
// =============================================================================
// Sound tracking (user 2026-07-16): stereo capture from the two microphones
// (M5.Mic, ES7210) → SoundDirection (pure) → a SoundDir command to the Brain,
// which turns the head towards the noise until the levels balance out.
//
// Option OFF by default: POST /api/tuning?sound_track=1 (+ console).
// Settings: soundtrack_thr (RMS threshold), soundtrack_sign (L/R direction —
// HW-VALIDATED at -1, user 2026-07-30; full history in Tuning.h, do not
// "correct" it from reasoning alone).
//
// I2S bus SHARED with M5.Speaker (chirps, sound=1) on the CoreS3 — arbitrated
// on the fly: the speaker wins during a chirp (the mic releases the bus and
// takes it back 300 ms after the end); symmetrically SoundFx takes the bus
// away from the mic before playing.
//
// Thread: loop() only (same pattern as EmotionLeds/SoundFx) — the capture is
// non-blocking (double buffer, asynchronous M5.Mic.record).
// =============================================================================

#include <Arduino.h>
#include <M5Unified.h>
#include "../engine/Tuning.h"
#include "../behavior/Brain.h"
#include "../behavior/SoundDirection.h"
#include "SoundViz.h"

namespace sce {

class SoundTracker {
public:
    SoundTracker(Brain& brain, const Tuning& tuning)
        : _brain(brain), _tuning(tuning) {}

    // The status band's visualiser, fed from the SAME block this class already
    // consumes. Optional: nothing here changes if it is absent, and nothing
    // else ever gets a handle on the sample ring — loop() stays its one reader.
    void attachViz(SoundViz* v) { _viz = v; }

    // Microphone state for the API/console (user 2026-07-16: tell the boot
    // unavailability apart from the rest). Written by loop(), read by
    // AsyncTCP (an aligned 8/32-bit write is atomic on Xtensa, Tuning model).
    enum MicState : uint8_t {
        MIC_OFF     = 0,   // sound_track option disabled
        MIC_WARMUP  = 1,   // boot guard (WARMUP_MS, A2.20)
        MIC_STANDBY = 2,   // speaker has priority (chirp ± 300 ms)
        MIC_ACTIVE  = 3,   // capture running — levels are valid
    };
    // Anti-brick guard (A2.20) — SINGLE SOURCE: the countdown shown by the
    // console comes from warmupRemainS(), never from a copy of that 20.
    static constexpr uint32_t WARMUP_MS = 20000;

    // Canonical name of the state — travels WITH the enum (2026-07-17
    // review: the console indexed its labels on the VALUES, so reordering
    // the enum silently shifted everything). The API publishes this name and
    // the JS only knows names.
    static const char* stateName(MicState s) {
        switch (s) {
            case MIC_OFF:     return "off";
            case MIC_WARMUP:  return "warmup";
            case MIC_STANDBY: return "standby";
            case MIC_ACTIVE:  return "active";   // English like its three siblings
        }
        return "?";
    }

    MicState state()     const { return _state; }
    // Seconds left in the warm-up (0 when not in MIC_WARMUP)
    uint32_t warmupRemainS() const {
        uint32_t now = millis();
        return (_state == MIC_WARMUP && now < WARMUP_MS)
             ? (WARMUP_MS - now + 999) / 1000 : 0;
    }
    float    rmsL()      const { return _dir.rmsL(); }
    float    rmsR()      const { return _dir.rmsR(); }
    float    ambient()   const { return _dir.ambient(); }
    uint32_t events()    const { return _events; }   // clear noises detected

    // Call from loop() (~50 Hz)
    void update() {
        uint32_t now = millis();

        // TWO SETTINGS, one microphone. `mic_enable` runs the capture;
        // `sound_track` additionally lets it move the head, and IMPLIES the
        // capture so that a card written before `mic_enable` existed behaves
        // exactly as it always did.
        const bool wantMic = (_tuning.mic_enable  >= 0.5f) ||
                             (_tuning.sound_track >= 0.5f);

        // SAFETY RAIL (boot-loop lesson 2026-07-16): NEVER turn the mic on
        // before 20 s of uptime — if the activation crashes (I2S bus,
        // driver), every boot cycle still leaves a 20 s window during which
        // the API answers → mic_enable=0 / sound_track=0 can always be posted
        // back (persisted to SD) remotely. A crash no longer bricks the robot.
        if (!wantMic || now < WARMUP_MS) {
            _state = !wantMic ? MIC_OFF : MIC_WARMUP;
            if (_micOn) stopMic();
            if (_viz) _viz->idle();
            return;
        }

        // ---- I2S1 bus arbitration (SHARED between the AW88298 speaker and
        //      the ES7210 mic on the CoreS3, same BCLK/WS): use the REAL
        //      speaker state — isRunning(), NOT isEnabled() which only
        //      reflects the pin CONFIG (always true on the CoreS3, an
        //      M5Unified trap). The speaker has priority; the mic takes over
        //      again 300 ms after the end of a chirp. ----
        if (M5.Speaker.isRunning()) {
            _spkBusyMs = now;
            _state = MIC_STANDBY;
            if (_micOn) stopMic();
            if (_viz) _viz->idle();
            return;
        }
        if (now - _spkBusyMs < 300) {
            _state = MIC_STANDBY;
            if (_viz) _viz->idle();
            return;
        }
        _state = MIC_ACTIVE;

        // ---- CONTINUOUS capture: an M5 queue of 2 blocks + a ring of 3
        //      buffers. NO explicit M5.Mic.begin(): record() performs the
        //      init at the RIGHT sample rate — a prior begin() (internal
        //      rate still 0) triggered an internal end()/begin() cycle on
        //      the first record(), during which mic_task called i2s_read on
        //      an uninstalled driver → Guru LoadProhibited (HW boot-loop,
        //      A2.20). MEMBER buffers, persistent: record() is ASYNCHRONOUS,
        //      mic_task writes into them after we return.
        //      2 queued blocks of 32 ms = 64 ms of depth, far above the
        //      loop() period (10 ms since 2026-07-16, previously 50) → ZERO
        //      listening gaps, and a block is processed ≤ 10 ms after it
        //      ends (originally: 1 block of 16 ms per 50 ms round = deaf
        //      ~70 % of the time, "StackChan does not move"). ----
        if (!_micOn) {
            _micOn = true;
            _head = _tail = _inFlight = 0;
        }

        // Finished blocks (FIFO: the oldest one in the queue first)
        uint8_t pending = (uint8_t)M5.Mic.isRecording();
        while (_inFlight > pending) {
            // The analyser runs ONLY when something consumes it: the
            // sound band is mode 2, and everything else showing on the band
            // made ~31 FFTs a second for a display nobody could see.
            if (_viz && (int)(_tuning.band_mode + 0.5f) == 2)
                _viz->feed(_buf[_tail]);         // scope + spectrum, same block
            auto r = _dir.feed(_buf[_tail], FRAMES, _tuning.soundtrack_thr);
            _tail  = (uint8_t)((_tail + 1) % NBUF);
            _inFlight--;
            if (r.event) {
                _events++;               // a fact about the ROOM: counted even
                                         // when nothing acts on it, because it
                                         // is what tells you the mic is alive
            }
            // THE HEAD only moves when asked to. With `mic_enable` alone the
            // robot listens and stays still — the band's visualiser runs, the
            // event counter climbs, and nothing turns.
            if (r.event && _tuning.sound_track >= 0.5f) {
                // EVERY event is posted (rate-limited to 1/300 ms) — the
                // Brain decides: turn if lateralised (|imb| > 0.08), startle
                // on a shock, and in every case the event re-arms the
                // "back to calm" timer (a continuous sound straight ahead
                // holds the pose without moving the head).
                if (now - _lastPostMs > 300) {
                    _lastPostMs = now;
                    _brain.post({ CmdType::SoundDir, 0, 0,
                                  r.imbalance, r.level });
                }
            }
        }
        // Refill the queue back to 2 blocks.
        //
        // A REFUSED record() MUST NOT BE COUNTED (fix 08-25). record() returns
        // false when the driver cannot take the block — the I2S queue is full,
        // the mic is mid-init, the speaker just grabbed the bus. Counting it
        // anyway makes `_inFlight` say two while the driver holds one, and
        // that lie never heals: every later pass then finds `_inFlight >
        // isRecording()`, hands the visualiser a buffer NOBODY EVER FILLED —
        // the previous block's samples, or worse the one being written right
        // now — and steps `_tail` past `_head`. The scope replays and jumps
        // instead of scrolling, which is exactly what a stutter looks like.
        // Breaking out instead leaves the queue one short for a single pass,
        // and the next pass tops it back up.
        while (_inFlight < 2) {
            if (!M5.Mic.record(_buf[_head], FRAMES * 2, RATE, /*stereo=*/true))
                break;
            _head = (uint8_t)((_head + 1) % NBUF);
            _inFlight++;
        }
    }

private:
    static constexpr size_t   FRAMES = 512;    // L/R pairs (32 ms at 16 kHz)
    static constexpr uint32_t RATE   = 16000;
    static constexpr int      NBUF   = 3;      // 2 queued + 1 being processed

    void stopMic() {
        if (M5.Mic.isRunning()) M5.Mic.end();
        _micOn    = false;
        _inFlight = 0;
    }

    Brain&         _brain;
    const Tuning&  _tuning;
    SoundDirection _dir;
    SoundViz*      _viz = nullptr;
    int16_t        _buf[NBUF][FRAMES * 2];     // 6 KB — stereo ring
    uint8_t        _head = 0, _tail = 0, _inFlight = 0;
    MicState       _state     = MIC_OFF;
    bool           _micOn     = false;
    uint32_t       _spkBusyMs = 0;
    uint32_t       _lastPostMs = 0;
    uint32_t       _events    = 0;
};

} // namespace sce
