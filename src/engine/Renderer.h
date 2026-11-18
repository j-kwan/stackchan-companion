#pragma once
// =============================================================================
// Renderer.h — StackChan-Companion (engine)
// =============================================================================
// THE rendering task (ROADMAP §3.1/§3.5): consumes FaceState snapshots
// (triple buffer, never blocking), drives both EyeRig, applies the
// interpolated emotion color + the CRT effect, pushes the eye-zone canvas.
//
// Architecture settled by the P-1 spike + the P0/P1a validations:
//   - eye-zone canvas 320×160 8-bit in internal SRAM (degraded PSRAM fallback)
//   - LCD bus 40 MHz (80 = panel artifacts) — configured by hal/Board
//   - 30 Hz cadence: budget measured in P1a = 21.4 ms avg / 26.1 ms max, + CRT
//     ~7 ms → the 33 ms period holds (anti-starvation rule §3.5, guard included)
//
// "ONE SINGLE OWNER OF THE SCREEN" RULE (P1b lesson, on hardware): EVERY access
// to M5.Display goes through the renderer task. In the initial P1b, loop() drew
// the status band while the renderer was pushing the sprite → preemption in the
// middle of an SPI transaction → LCD controller address pointer shifted
// (repeated/wrapped image), and the flicker meant 30 setBrightness/s = PMIC I2C
// colliding with M5.update() → screen cut off. Hence:
//   - setStatus(text): the app POSTS its band, the renderer DRAWS it
//   - CrtEffect never touches M5.Display again (drops done through a LUT)
//
// pause()/resume() (P5 launcher, §3.6): pause() requests the stop and WAITS
// until the frame in flight has been pushed (the launcher can then draw
// directly on M5.Display without tearing); resume() restarts it.
//
// DIRTY-BAND PUSH (08-03, diagnosis in ROADMAP §A5 "Frame budget: the eyes").
// A full push of the eye zone is 320×160 px × 2 B on a 40 MHz SPI2 bus =
// 102 400 bytes = 20,5 ms of PURE WIRE TIME, arithmetic, while ALL the drawing
// code together is ~3,5 ms. So the only lever of the right order is the NUMBER
// OF PIXELS PUSHED. The renderer therefore keeps a GHOST copy (PSRAM, 51 200 B)
// of what the panel is showing, compares it to the canvas AFTER the drawing,
// and pushes only the row bands that differ (engine/DirtyBands.h, tested
// natively — `test_dirtybands`).
//   - identical pixels, by construction: the comparison is on PIXELS ALREADY
//     RENDERED, never on a channel value. Nothing is skipped, nothing is
//     smoothed — A2.15 (the Brain is the only source of smoothing) is not even
//     touched. It is a TRANSPORT optimisation, not a visual one.
//   - no PSRAM → `_ghost` is null → full `pushSprite`, i.e. exactly today's
//     behaviour. The fallback is the old code, not a crash.
//   - THE TRAP is the ghost describing a screen that no longer exists. Any
//     third party painting into y < 160 must invalidate it. In this firmware
//     they ALL go through `pause()` — that is rule A2.1/A2.16, and the only
//     sanctioned way to hand the screen over (Launcher, SD-Updater flash
//     screen, remote /api/bins/launch). So the invalidation sits in the SAME
//     place as `_bandInvalidate`: the post-pause resume path, unconditionally.
//     Conservative on purpose — the camera pauses without painting and pays a
//     full push, which is never WORSE than today. `invalidateScreen()` is
//     public for any future painter that would not pause.
//   - `setBusy` needs nothing special: the waiting screen is drawn by the
//     renderer itself, through the same push, so the ghost stays truthful.
//
// Scene (original "witchcraft" logic, in the viewer-centric convention):
//   - gaze → pixels via units::pxFromGaze (the only conversion point)
//   - the eye NEAR the gaze direction widens, the OPPOSITE eye closes
//     slightly (esp32-eyes LookAssistant): gaze.x > 0 (viewer right) →
//     RIGHT eye is near. V component: both close when looking up/down.
//   - per-eye openRatio multiplies scaleY (clamped 0.02 → thin line visible)
// =============================================================================

#include <Arduino.h>
#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <atomic>
#include "../hal/I2cBus.h"
#include "Clock.h"
#include "Units.h"
#include "Emotions.h"
#include "EyeRig.h"
#include "EyeEffects.h"
#include "FaceState.h"
#include "SoundFrame.h"
#include "CrtEffect.h"
#include "DirtyBands.h"
#include "FieldStore.h"

namespace sce {

// Status band modes (bottom of screen). Extensible: adding a mode = one value
// here + one case in drawStatusBand (future: descriptors read from SD).
enum StatusBarMode : int {
    BAND_OFF    = 0,   // dynamic area left black
    // (1 = former DEBUG, removed: the emotion·ip info is now a SEPARATE
    //  OPTION — `band_debug` — displayed CENTERED in the icon row)
    BAND_SOUND  = 2,   // triggered scope, three skins (TripleBuffer<SoundFrame>)
    BAND_GAUGES = 3,   // 3 gauges (fields g0..g2 + labels/right text)
    BAND_TIMER  = 4,   // countdown timer (fields tmr/tmr_st — BandTimer.h)
    BAND_POMO   = 5,   // pomodoro (same fields, same drawing)
};

class Renderer {
public:
    Renderer(const Clock& clock, TripleBuffer<FaceState>& bus)
        : _clock(clock), _bus(bus), _canvas(&M5.Display) {
        // ---- The emotion palette, memoised (08-03, review) ----
        // `emotionToRgb` stopped being a table of hex values the day the
        // palette became a SYSTEM (Emotions.h, 08-03): most of the 30 entries
        // are now `blendRgb888` calls — a saturation restore, a renormalisation
        // and a clamp, i.e. a chain of float divisions each. And drawFrame
        // evaluates it TWICE PER FRAME (the emotion being left and the one
        // being entered) for a value that depends on NOTHING but a 30-value
        // enum. Sixty of those per second, forever, for two constants.
        // Computed here, ONCE, THROUGH THE SAME FUNCTION: the values are
        // identical bit for bit — this is a cache, not a change of rendering —
        // and the palette keeps its single home in Emotions.h, which the
        // renderer only remembers. Done in the constructor rather than
        // begin() so no call path can ever observe an unfilled table.
        for (int i = 0; i < EMOTIONS_COUNT; i++)
            _emoRgb[i] = emotionToRgb((eEmotions)i);
    }

    // ------------------------------------------------------------------
    // Allocates the canvas + the EyeRig + the CRT. Call after Board::begin.
    // ------------------------------------------------------------------
    bool begin() {
        _eyeL = new EyeRig(_clock, /*isLeft=*/true,  units::EYE_L_CX, units::EYE_CY);
        _eyeR = new EyeRig(_clock, /*isLeft=*/false, units::EYE_R_CX, units::EYE_CY);

        _canvas.setColorDepth(8);
        _canvas.setPsram(false);                       // SRAM = throughput (P-1)
        if (!_canvas.createSprite(units::SCREEN_W, units::EYEZONE_H)) {
            _canvas.setPsram(true);                    // degraded fallback
            if (!_canvas.createSprite(units::SCREEN_W, units::EYEZONE_H)) return false;
            Serial.printf("[renderer] zone en PSRAM — fps dégradés\n");
        }
        _crt.begin(units::SCREEN_W, units::EYEZONE_H);

        // ---- Ghost of the eye zone (dirty-band push) ----
        // PSRAM ONLY, and the refusal is deliberate (rule 18's discipline): a
        // 51 200 B block taken from the INTERNAL heap would be taken from
        // WiFi/AsyncTCP, and losing the network stack to save 15 ms of SPI is
        // a bad trade. Without the ghost the renderer simply pushes the whole
        // sprite, which is what it did before this existed.
        _ghost = (uint8_t*)heap_caps_malloc(
                     (size_t)units::SCREEN_W * units::EYEZONE_H,
                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!_ghost)
            Serial.printf("[renderer] pas de PSRAM pour le fantome — "
                          "poussee pleine trame (20,5 ms/frame)\n");
        return true;
    }

    // ------------------------------------------------------------------
    // Starts the task (threading map §3.5: core 1, prio 3 by default)
    // ------------------------------------------------------------------
    void start(uint32_t stack = 8192, UBaseType_t prio = 3, BaseType_t core = 1) {
        xTaskCreatePinnedToCore(taskEntry, "renderer", stack, this, prio, &_task, core);
    }

    // Remaining stack headroom (words) — P7 endurance, read by the heartbeat
    uint32_t stackFreeWords() const {
        return _task ? (uint32_t)uxTaskGetStackHighWaterMark(_task) : 0;
    }

    // ------------------------------------------------------------------
    // pause: requests + waits for the end-of-frame ack (defensive 500 ms
    // timeout). Once it returns, the screen belongs to the caller (launcher).
    // ------------------------------------------------------------------
    // REFCOUNT under a SPINLOCK: several tasks pause concurrently (loop()
    // around SD writes AND the camera task around fb_get, on different
    // cores). The (counter, _pauseReq) pair must mutate ATOMICALLY: with two
    // separate atomics, a resume() preempted between its fetch_sub and its
    // store(false) let a concurrent pause() believe the renderer was paused,
    // then the late store woke it up during the SD write (review 07-21 —
    // exactly the A2.16 SPI2 contention this mechanism is meant to prevent).
    // The counter is also CLAMPED at 0: a defensive double-resume no longer
    // makes every subsequent pause() silently ineffective.
    // A PAUSER SAYS WHETHER IT PAINTS, and that is not a detail since 08-03:
    // resume() invalidates the ghost buffer, so a pauser that touches nothing
    // used to cost the next frame a FULL push (102 400 B of wire) plus a
    // 51 200 B memcpy that did not exist before. The camera brackets every
    // `fb_get` this way at ~10 fps, which nullified the whole dirty-band gain
    // on roughly one frame in three, and forced the three gauge sprites to
    // re-push at every tick as well (review 08-03).
    // The DEFAULT stays `true` — a painter that forgets leaves menu leftovers
    // frozen under the eyes, which is far worse than a wasted push, so the
    // safe answer is the one you get by saying nothing.
    void pause(bool willPaint = true) {
        portENTER_CRITICAL(&_pauseMux);
        _pauseCnt++;
        if (willPaint) _pausePaints = true;   // sticky until the resume clears it
        _pauseReq.store(true);
        portEXIT_CRITICAL(&_pauseMux);
        uint32_t t0 = _clock.ms();
        while (!_paused.load() && _clock.ms() - t0 < 500) vTaskDelay(1);
    }
    void resume() {
        portENTER_CRITICAL(&_pauseMux);
        if (_pauseCnt > 0 && --_pauseCnt == 0) {
            // A PAINTER wiped the panel: BOTH memories — the band's change
            // detection and the eye-zone ghost — are invalidated here, under
            // the same lock, BEFORE the pause is released (review 08-04).
            // They used to travel through a relay flag stored after portEXIT:
            // the renderer could wake on _pauseReq=false, pass its
            // consumption point, and MISS the wipe — menu leftovers frozen
            // under the eyes until the next pause cycle, minutes away with
            // the camera off. Writing the two real flags directly also
            // removes the relay's split-entry trap: a painter calling only
            // the public invalidateScreen() refreshed the ghost but never
            // the band. pause() being the only sanctioned way to take the
            // panel (A2.1/A2.16), this line covers every painter there is;
            // non-painting pausers (camera fb_get, loop SD writes) declared
            // themselves via pause(false) and skip it.
            if (_pausePaints) {
                _bandInvalidate.store(true);
                _pushInvalidate.store(true);
            }
            _pausePaints = false;
            _pauseReq.store(false);
        }
        portEXIT_CRITICAL(&_pauseMux);
    }
    bool isPaused() const { return _paused.load(); }

    // ------------------------------------------------------------------
    // "What is on the panel is no longer what I remember": forces the NEXT
    // frame to push the eye zone in full (dirty-band push, see the header
    // note). Called automatically on every resume() — which covers every
    // third-party painter in this firmware, since pause() is the ONLY way to
    // take the screen (A2.1/A2.16). Public for the painter that would not:
    // call it AFTER painting into y < 160, and the ghost tells the truth
    // again on the next frame. Idempotent, lock-free, callable from any task.
    // ------------------------------------------------------------------
    void invalidateScreen() { _pushInvalidate.store(true); }

    // ------------------------------------------------------------------
    // "busy" mode (user 2026-07-16): a "…" waiting screen — three centered
    // rounded squares, MOTIONLESS. Use it when StackChan must be frozen in a
    // VISIBLE way (OTA, upload, long operations): the renderer keeps the
    // screen (rule 1) and shows a state instead of letting the face stutter.
    //
    // Because the waiting screen is static, it is drawn ONCE and then the
    // renderer stops touching the bus: `setBusy` therefore ALSO suits
    // operations that fight the LCD for SPI2, SD writes included (it was the
    // opposite as long as the dots were animated). One ONE-frame window
    // remains at arming time — the current frame may still be in flight —
    // negligible against 30 frames/s over the whole duration of a transfer.
    //
    // It still does NOT replace pause(), which keeps a distinct role:
    // pause() WAITS for the renderer's acknowledgement and guarantees it no
    // longer draws, the only way to HAND the screen over to a third party
    // (Launcher, SD-Updater — rule A2.16). busy only freezes our own display.
    // ------------------------------------------------------------------
    void setBusy(bool on) { _busy.store(on); }
    bool isBusy() const { return _busy.load(); }

    // The "…" indicator, drawn on any LovyanGFX target: the renderer's own
    // canvas (busy mode) OR M5.Display DIRECTLY — only with the renderer
    // PAUSED and from the SAME task as the operation (SD-Updater progress
    // callback: the drawing slots in between SD chunks, never concurrently
    // — A2.16).
    // Clears its own band before redrawing (leftovers from shrinking).
    // Three MOTIONLESS rounded squares. Each used to "breathe" on its own
    // period until 2026-07-30: the animation was REMOVED, and it was the
    // animation that caused the problem. Animating forced the renderer back
    // onto the SPI2 bus every frame; during an upload those frames fought
    // the SD card for the bus (A2.16) and the screen stuttered. In other
    // words, what produced the stutter was the very indicator meant to hide
    // it.
    // Motionless, it draws ONCE and releases the bus — hence a real freeze.
    template <typename GFX>
    static void drawBusyDots(GFX& gfx, int16_t cx, int16_t cy) {
        static constexpr int16_t DOT = 22, SPACING = 40;
        const uint32_t rgb = dimRgb888(CALM_CYAN, 0.80f);
        gfx.fillRect((int16_t)(cx - SPACING - DOT), (int16_t)(cy - DOT),
                     (int16_t)(2 * (SPACING + DOT)), (int16_t)(2 * DOT),
                     TFT_BLACK);
        for (int i = 0; i < 3; i++) {
            int16_t x = (int16_t)(cx + (i - 1) * SPACING);
            gfx.fillRoundRect((int16_t)(x - DOT / 2), (int16_t)(cy - DOT / 2),
                              DOT, DOT, (int16_t)(DOT / 3), rgb);
        }
    }

    // Frame budget telemetry (anti-starvation rule — exposed to heartbeat/API)
    uint32_t frameAvgUs() const { return _frameAvgUs.load(); }
    uint32_t frameMaxUs() const { return _frameMaxUs.load(); }
    // The two stages the frame is made of — eyes (drawFrame) and status band
    // (drawStatusBand). Their sum is the frame minus the bus read.
    uint32_t eyesAvgUs() const { return _eyesAvgUs.load(); }
    uint32_t bandAvgUs() const { return _bandAvgUs.load(); }
    // The PUSH alone, carved out of the eyes stage (08-03): the diff + the
    // memcpy into the ghost + the pushImage bands. This is the number the
    // whole dirty-band change is about — and `pushAvgPx` says WHY it moves:
    // average pixels actually put on the wire per frame, out of 51 200.
    // 51 200 px ≈ 20,5 ms at 40 MHz; the ratio between the two is the check
    // that the model and the hardware agree.
    uint32_t pushAvgUs() const { return _pushAvgUs.load(); }
    uint32_t pushAvgPx() const { return _pushAvgPx.load(); }

    // Eye color ACTUALLY displayed this frame (transition included) — the LEDs
    // synchronize on it (EmotionLeds, user 2026-07-12). Atomic.
    uint32_t eyeColorRgb() const { return _eyeRgb.load(); }

    // Drawn height (SIZE) of each eye, normalized 0 (closed/nothing) → 1
    // (Surprised reference eye) — published as close to pushSprite as possible.
    // Used to align each LED bar's brightness with the size of its eye (left
    // bar ↔ left eye): the bigger the eye, the brighter the bar.
    float eyeHeightL() const { return _heightL.load(); }
    float eyeHeightR() const { return _heightR.load(); }

    // ------------------------------------------------------------------
    // Status band — the app POSTS the text, the renderer DRAWS it (~2 Hz)
    // below the eye zone. Short spinlock: a copy of ≤ 63 characters.
    // ------------------------------------------------------------------
    void setStatus(const char* text) {
        portENTER_CRITICAL(&_statusMux);
        strncpy(_statusText, text, sizeof(_statusText) - 1);
        _statusText[sizeof(_statusText) - 1] = '\0';
        _statusDirty = true;
        portEXIT_CRITICAL(&_statusMux);
    }

    // Screen brightness: the app POSTS a target (0-255), the renderer applies
    // it between two frames — rule A2.1 (only the renderer touches M5.Display)
    // and A2.2 (never per frame: the app only posts on change). -1 = nothing.
    void setBrightness(uint8_t b) { _pendBright.store((int)b); }

    // ------------------------------------------------------------------
    // STATUS BAR (bottom of screen, y 160..240) — driven by FIELDS.
    // The blackboard is the single source: producers (loop/API/BLE…) post
    // fields, the renderer DRAWS them (rule 1). attachFields at boot.
    // ------------------------------------------------------------------
    void attachFields(const FieldStore* f) { _fields = f; }
    // The sound visualiser's own bus. A SECOND TripleBuffer instance, not a
    // second reader of the face bus — A2.5 forbids the latter and this is not
    // it. The analyser publishes on loop(), this task consumes; nothing else
    // touches either end.
    void attachSoundBus(TripleBuffer<SoundFrame>* b) { _sndBus = b; }
    void setStatusBar(int mode) { _bandMode.store(mode); }
    int  statusBar() const { return _bandMode.load(); }

    // Ephemeral notification (POST /api/say): covers the dynamic area for
    // `ms` milliseconds, then hands it back to the current mode.
    void setSay(const char* text, uint32_t ms) {
        portENTER_CRITICAL(&_statusMux);
        copyLocal(_sayText, text, sizeof(_sayText));
        _sayUntil = _clock.ms() + (ms ? ms : 4000);
        portEXIT_CRITICAL(&_statusMux);
    }

    // Icon mask (bits batt=1 wifi=2 cam=4 mic=8 night=16) + size of the
    // dynamic text (say/alert, 1..3). Pushed by main from Tuning.
    void setIconMask(uint32_t m)   { _iconMask.store(m); }
    void setSayTextSize(uint8_t s) { _sayTextSize.store(s < 1 ? 1 : (s > 3 ? 3 : s)); }
    void setScrollSpeed(uint16_t pxs) { _scrollSpeed.store(pxs < 5 ? 5 : pxs); } // px/s
    void setBandDebug(bool on) { _bandDebug.store(on); }
    void setBandClock(bool on) { _bandClock.store(on); }   // emotion·ip (icon row)
    // Identity colour of a personality: 0 = keep the emotion palette. Pushed by
    // loop() like every other band/eye choice. The LEDs need no wiring of their
    // own — `EmotionLeds` reads `eyeColorRgb()`, which is the colour actually
    // DISPLAYED, so the body follows the eyes for free (A2.17). No repaint flag
    // either: the eye colour is resolved from the emotion on every frame, so
    // the next one picks the change up.
    void setEyeRgbOverride(uint32_t rgb) {
        _eyeRgbOverride.store(rgb, std::memory_order_relaxed);
    }
    // TOUCH CROSSHAIRS (debug overlay). Each contact is published as
    // `MARK_LIVE | x << 16 | y`, 0 for "no finger there" — one word per point,
    // written whole, so the renderer can never read half of a position. Pushed
    // by loop() from the panel like every other band choice; the renderer only
    // paints, and paints nothing at all unless the debug row is on.
    static constexpr uint32_t MARK_LIVE = 0x80000000u;
    static constexpr uint32_t markOf(int x, int y) {
        return MARK_LIVE | ((uint32_t)x << 16) | (uint32_t)y;
    }
    void setTouchMarks(uint32_t a, uint32_t b) {
        _touchMark[0].store(a, std::memory_order_relaxed);
        _touchMark[1].store(b, std::memory_order_relaxed);
    }
    // Which of the three sound styles is on the panel. Pushed by main from
    // Tuning like every other band choice; the renderer notices the change on
    // its own and repaints from scratch (the delta memory is style-specific).
    void setSoundStyle(uint8_t s) { _sndStyle.store(s > 2 ? 0 : s); }

private:
    const Clock&             _clock;
    TripleBuffer<FaceState>& _bus;
    M5Canvas                 _canvas;
    CrtEffect                _crt;
    EyeRig*                  _eyeL = nullptr;
    EyeRig*                  _eyeR = nullptr;

    TaskHandle_t          _task = nullptr;
    uint32_t              _statsFromMs = 0;   // stats start (boot ignored)
    std::atomic<bool>     _pauseReq{false};
    std::atomic<bool>     _paused{false};
    int                   _pauseCnt = 0;      // refcount (guarded by _pauseMux)
    portMUX_TYPE          _pauseMux = portMUX_INITIALIZER_UNLOCKED;
    std::atomic<bool>     _busy{false};        // "…" waiting screen
    std::atomic<uint32_t> _frameAvgUs{0}, _frameMaxUs{0};
    // Per-stage split of the frame (08-01): where the 33 ms budget goes.
    std::atomic<uint32_t> _eyesAvgUs{0}, _bandAvgUs{0};
    std::atomic<uint32_t> _pushAvgUs{0}, _pushAvgPx{0};   // the push alone
    std::atomic<int>      _pendBright{-1};     // pending brightness (-1 = none)
    std::atomic<uint32_t> _eyeRgb{CALM_CYAN};  // displayed color (LEDs)
    std::atomic<float>    _heightL{1.0f};      // norm. height, left eye (LEDs)
    std::atomic<float>    _heightR{1.0f};      // norm. height, right eye (LEDs)

    // ALERT band (posted by the app through setStatus — e.g. low battery;
    // takes priority over the dynamic area) + say notification (timed).
    portMUX_TYPE _statusMux = portMUX_INITIALIZER_UNLOCKED;
    char         _statusText[64]  = "";
    bool         _statusDirty     = false;
    uint32_t     _statusDrawnMs   = 0;
    char         _sayText[160]    = "";   // long → scrolls (marquee), not cut
    uint32_t     _sayUntil        = 0;

    // Status bar — fields + refresh state (change-detection so that only what
    // moved is redrawn: near-zero cost in steady state).
    const FieldStore*   _fields = nullptr;
    std::atomic<int>    _bandMode{BAND_OFF};
    std::atomic<int>    _sndStyle{0};
    TripleBuffer<SoundFrame>* _sndBus = nullptr;
    std::atomic<uint32_t> _iconMask{31};        // bits batt/wifi/cam/mic/night
    std::atomic<uint8_t>  _sayTextSize{1};      // say/alert text size (1..3)
    std::atomic<uint16_t> _scrollSpeed{70};     // marquee scrolling (px/s)
    std::atomic<bool>     _bandDebug{true};     // emotion·ip centered (icon row)
    std::atomic<bool>     _bandClock{false};    // mode 0 shows HH:MM (option)
    std::atomic<uint32_t> _touchMark[2]{};      // crosshairs — see setTouchMarks
    std::atomic<uint32_t> _eyeRgbOverride{0};   // 0 = emotion palette (see emotionRgb)
    std::atomic<bool>   _bandInvalidate{false};  // post-pause resume (07-26)
    bool                _pausePaints = false;    // a pauser declared painting
    M5Canvas            _gaugeCv{&M5.Display};   // 320x15 back buffer, 3 rows
    uint32_t            _gaugeSig[3] = {0, 0, 0};// last DRAWN row (change-detect)
    bool                _gaugeFirst  = true;     // force the first pass
    int                 _lastMode  = -1;
    char                _lastDyn[160] = "";     // last dynamic line drawn
    uint8_t             _lastDynSize = 0;       // size of the last text drawn
    uint16_t            _lastDynCol  = 0;        // color of the last text drawn
    int                 _lastDynDx   = 0;        // x-offset of the last text
    bool                _dynScroll  = false;    // marquee running (long text)
    bool                _dynHadText = false;    // previous frame = dynamic text
    uint32_t            _lastIconSig = 0xFFFFFFFF;
    uint32_t            _lastDbgHash = 0;        // debug text hash (icon row)
    uint32_t            _bandDrawnMs = 0;

    static void copyLocal(char* dst, const char* src, int n) {
        int i = 0; for (; i < n - 1 && src[i]; i++) dst[i] = src[i]; dst[i] = '\0';
    }

    // ---- Dirty-band push (see the header note) ----
    // MAX_BANDS bounds the per-band overhead (one window setup each — the SPI
    // transaction is opened ONCE around the whole loop) when the frame is
    // scattered: sparkles and sweat draw in a dozen places at once.
    //
    // MERGE_GAP = 0, RECALIBRATED 08-03 (it was 2) — full arithmetic in the
    // header of DirtyBands.h. Short version: merging costs 128 µs of wire per
    // clean row pushed (640 B at 40 MHz) and saves ≤ 20 µs of band overhead,
    // so the break-even gap is 20/128 < 1 row and an integer threshold can
    // only be 0. The old value 2 spent up to 256 µs to save 20 — the trade was
    // being made in the wrong direction on the one path all 30 expressions go
    // through. 0 still coalesces ADJACENT dirty rows (their gap IS 0): an eye
    // remains one band. It only stops pushing clean rows on purpose.
    static constexpr int MAX_BANDS = 12;
    static constexpr int MERGE_GAP = 0;
    uint8_t*              _ghost = nullptr;    // last image pushed (PSRAM)
    bool                  _ghostValid = false; // renderer task only
    std::atomic<bool>     _pushInvalidate{false};   // "the panel changed under us"
    uint64_t              _pushUs = 0;         // 60-frame accumulators
    uint64_t              _pushPx = 0;
    uint32_t              _pushN  = 0;

    // Memoised palette (filled by the constructor — see the note there).
    uint32_t _emoRgb[EMOTIONS_COUNT] = {0};

    // The ONLY reader of that table. Out of range falls back on the function
    // itself, which answers CALM_CYAN through its `default` — the behaviour
    // before the cache, to the bit.
    uint32_t emotionRgb(eEmotions e) const {
        // IDENTITY COLOUR (personality). Posed HERE, at the table's only
        // reader, and never written INTO the table: `_emoRgb[]` is filled once
        // in the constructor and has never needed invalidating — pushing the
        // override into it would turn a cache that cannot go stale into one
        // that can. Since both the emotion being left and the one being entered
        // come through here, a transition interpolates green to green, i.e.
        // does not flicker.
        //
        // What this COSTS is worth naming: the Inside-Out-derived palette stops
        // saying which feeling is on the face. That is the accepted trade of a
        // one-colour character, not a side effect — a Haro is green, and that
        // is most of what makes it legible as a Haro.
        const uint32_t ov = _eyeRgbOverride.load(std::memory_order_relaxed);
        if (ov) return ov;
        return ((unsigned)e < (unsigned)EMOTIONS_COUNT) ? _emoRgb[e]
                                                        : emotionToRgb(e);
    }

    // Tracking state carried between frames
    eEmotions _prevEmotion  = Normal;   // for the color interpolation
    eEmotions _shownEmotion = Normal;   // last emotion applied to the rigs
    bool      _shownMirror  = false;    // last asym mirror applied
    bool      _firstFace    = true;     // first consumed frame → applied INSTANTLY

    static void taskEntry(void* self) { ((Renderer*)self)->run(); }

    void run() {
        TickType_t lastWake = xTaskGetTickCount();
        const TickType_t period = pdMS_TO_TICKS(33);   // 30 Hz (P1a budget)
        uint64_t accUs = 0; uint32_t accN = 0;
        uint32_t eyesUs = 0, bandUs = 0, stageN = 0;   // per-stage split
        // The waiting screen is STATIC: once laid down there is nothing left
        // to redraw. This flag is what makes the freeze truly silent on SPI2 —
        // without it, "busy" repainted an identical image 30 times a second,
        // competing with the SD card.
        bool busyDrawn = false;

        for (;;) {
            // ---- Cooperative pause (launcher §3.6) ----
            if (_pauseReq.load()) {
                _paused.store(true);
                while (_pauseReq.load()) vTaskDelay(pdMS_TO_TICKS(20));
                _paused.store(false);
                lastWake = xTaskGetTickCount();   // restart without catch-up
                // RESUMING after a pause: the band/ghost invalidation for a
                // PAINTING pauser is done by resume() itself, under the pause
                // lock, before this loop can wake — see resume() (08-04).
                // …and the waiting screen must be LAID DOWN AGAIN: a third
                // party may have painted over it during the pause. Outside the
                // block on purpose: cheap, and a wrong `busyDrawn` shows a
                // frozen face rather than a missing pixel.
                busyDrawn = false;
                // RE-CHECK before drawing: a pause() that landed between the
                // exit of the while and the store(false) read _paused as still
                // true (stale ack) and believes it owns the screen — without
                // this continue we would draw ONE frame in the middle of its
                // SD write (07-21).
                continue;
            }

            // ---- Auto brightness: applied HERE (the renderer is the only
            //      owner of M5.Display, A2.1); under the bus lock because the
            //      CoreS3 backlight goes through the AXP2101 (shared In_I2C,
            //      see I2cBus.h).
            int pb = _pendBright.exchange(-1);
            if (pb >= 0) { sce::i2cbus::Guard g; M5.Display.setBrightness((uint8_t)pb); }

            int64_t f0 = esp_timer_get_time();
            // NOTHING PUBLISHED YET → NOTHING DRAWN (review 08-04). Before
            // the Brain's first publish, read() can only hand back the
            // default-constructed slot — a Normal face nobody produced. The
            // boot-order fix (Brain first, 60 ms head start) made drawing it
            // unlikely; this makes it impossible, whatever the scheduling.
            // The panel stays black for those few milliseconds, which is what
            // a robot that has not decided its face yet should show.
            if (!_bus.hasEverPublished()) {
                vTaskDelayUntil(&lastWake, period);
                continue;
            }
            const FaceState& fs = _bus.read();
            if (_busy.load()) {
                // VISIBLE freeze: image laid down once, then NO bus access at
                // all for as long as the freeze lasts. The status band is
                // frozen along with the rest — repeating it every frame would
                // bring back exactly the contention we are trying to remove.
                if (!busyDrawn) {
                    drawBusyFrame();
                    drawStatusBand(fs);
                    busyDrawn = true;
                }
            } else {
                busyDrawn = false;
                // SPLIT TIMING (08-01). The frame budget is 33 ms and the
                // measured average sat at ~27.5 ms with peaks at 42 — over
                // budget, and A2.22 says an over-budget frame starves loop()'s
                // touch polling on the same core. "27.5 ms" alone does not say
                // WHERE, and optimising a two-stage frame without splitting it
                // is guessing. Two stages, two numbers, published in the
                // heartbeat: the eyes and the status band.
                const int64_t e0 = esp_timer_get_time();
                drawFrame(fs);
                const int64_t e1 = esp_timer_get_time();
                drawStatusBand(fs);   // same task → serialized SPI (rule P1b)
                eyesUs += (uint32_t)(e1 - e0);
                bandUs += (uint32_t)(esp_timer_get_time() - e1);
                stageN++;
            }

            // ---- Frame budget — the first 3 s are IGNORED: boot (WiFi STA
            //      connection, SD init) causes an isolated spike that would
            //      skew frameMaxUs for the whole endurance run ----
            uint32_t dt = (uint32_t)(esp_timer_get_time() - f0);
            if (_statsFromMs == 0) _statsFromMs = _clock.ms() + 3000;
            if (_clock.ms() >= _statsFromMs) {
                accUs += dt; accN++;
                if (dt > _frameMaxUs.load()) _frameMaxUs.store(dt);
                if (accN >= 60) { _frameAvgUs.store((uint32_t)(accUs / accN)); accUs = 0; accN = 0; }
                // The split has its OWN 60-frame counter: `accN` also counts
                // the frozen frames of the busy path, which draw nothing and
                // would dilute the two stage averages towards zero.
                if (stageN >= 60) {
                    _eyesAvgUs.store(eyesUs / stageN);
                    _bandAvgUs.store(bandUs / stageN);
                    eyesUs = bandUs = 0; stageN = 0;
                }
            }

            // ---- Cadence + anti-starvation guard (P0 lesson, hardened
            // 07-25) ----
            // Frame > the 33 ms budget → vTaskDelayUntil does not sleep →
            // without a forced gap this task (prio 3) STARVES loop() (prio 1,
            // SAME core 1: M5.update touch polling): symptom "no swipe at all
            // during emotion X" (Dead/drawWideLine, diagnosed 07-22). 3
            // guaranteed ticks ≈ a real slot for loop() even at 57 ms/frame —
            // protects the CLASS of bug, not just the one rendering we fixed.
            TickType_t before = xTaskGetTickCount();
            vTaskDelayUntil(&lastWake, period);
            if (xTaskGetTickCount() == before) vTaskDelay(3);
        }
    }

    // =====================================================================
    // THE push of the eye zone — the single exit point of every rendering
    // path (face, blink line, "…" waiting screen). Everything that draws into
    // the canvas ends HERE, which is what keeps the ghost truthful: there is
    // no second way to reach the panel.
    //
    // IDENTICAL PIXELS, and here is why it is not a hope but a property.
    // `pushSprite` builds `pixelcopy_t(_img, dstDepth, rgb332_1Byte, false,
    // nullptr, NON_TRANSP)` and hands the whole 320×160 to
    // `Panel::writeImage`. `pushImage(x, y, w, h, const rgb332_t*)` builds the
    // SAME pixelcopy_t (`create_pc`, same constructor, same defaults — the
    // 8 bpp canvas has NO palette, `setColorDepth(8)` means rgb332 and not
    // palette_8bit) over a slice of the same buffer. With w = the full canvas
    // width the source stride LovyanGFX derives (`src_bitwidth = w`) matches
    // the canvas, so the slice is read exactly as the full push would have
    // read those rows. The conversion path ignores `use_dma` (it goes through
    // `_bus->writePixels`), so pushImage vs pushImageDMA changes nothing but
    // the name. Same bytes, fewer of them.
    // =====================================================================
    // ---- TOUCH CROSSHAIRS (debug overlay) ------------------------------
    // One vertical and one horizontal line per contact, crossing where the
    // finger is, a colour per point. It answers a question the numbers could
    // not: the panel MERGES two fingers placed close together into a single
    // contact, and until you can see one crosshair where you put two fingers
    // that reads as "the gesture is broken" rather than "move them apart".
    //
    // Drawn HERE — at the single exit point of every rendering path — rather
    // than in the face routine: the blink line and the "…" screen take their
    // own way out, and an overlay that vanished on a blink would look like the
    // touch had been lost. AFTER the CRT post-process on purpose, too; a
    // measuring instrument that is itself smeared measures nothing.
    //
    // A2.22: up to four segments, ONE call site. Written as four fillRect
    // calls in one body this is exactly the group the Xtensa backend may thin,
    // and the symptom would be a crosshair permanently missing an arm.
    // `noinline` so the symbol survives for the gate to count.
    __attribute__((noinline)) void drawTouchMarks() {
        if (!_bandDebug.load()) return;
        // Not the eye cyan, and not each other: the whole point is to tell two
        // contacts apart at a glance, on a face that is already cyan.
        static constexpr uint32_t INK[2] = { 0xFF2A2Au, 0x2AFF6Au };
        struct Seg { int16_t x, y, w, h; uint32_t rgb; };
        Seg seg[4];
        int n = 0;
        for (int i = 0; i < 2; ++i) {
            const uint32_t p = _touchMark[i].load(std::memory_order_relaxed);
            if (!(p & MARK_LIVE)) continue;
            const int x = (int)((p >> 16) & 0x1FFu);
            const int y = (int)(p & 0x1FFu);
            if (x < units::SCREEN_W)
                seg[n++] = { (int16_t)x, 0, 1,
                             (int16_t)units::EYEZONE_H, INK[i] };
            // A finger on the status band keeps its VERTICAL line and loses
            // the horizontal one — the canvas stops at the eye zone, and a
            // line clamped to the last row would claim a position the finger
            // does not have.
            if (y < units::EYEZONE_H)
                seg[n++] = { 0, (int16_t)y,
                             (int16_t)units::SCREEN_W, 1, INK[i] };
        }
        for (int i = 0; i < n; ++i)
            _canvas.fillRect(seg[i].x, seg[i].y, seg[i].w, seg[i].h, seg[i].rgb);
    }

    void pushEyeZone() {
        drawTouchMarks();
        uint8_t* buf = (uint8_t*)_canvas.getBuffer();
        const int   W = units::SCREEN_W, H = units::EYEZONE_H;
        const size_t N = (size_t)W * H;
        if (_pushInvalidate.exchange(false)) _ghostValid = false;

        const int64_t t0 = esp_timer_get_time();
        int32_t rows = H;
        if (!_ghost || !buf || !_ghostValid) {
            _canvas.pushSprite(0, 0);                  // the pre-08-03 path
            if (_ghost && buf) { memcpy(_ghost, buf, N); _ghostValid = true; }
        } else {
            dirty::Band bands[MAX_BANDS];
            const int n = dirty::diffBands(buf, _ghost, W, H,
                                           bands, MAX_BANDS, MERGE_GAP);
            rows = dirty::bandRows(bands, n);
            // ---- ONE transaction for the whole frame (08-03, review) ----
            // Unbracketed, each `pushImage` opens and closes its OWN SPI
            // transaction: up to MAX_BANDS bus acquisitions, CS toggles and
            // end-of-transaction flushes to push one frame. Bracketed, each
            // band still emits its window setup (that is what addresses it),
            // but the bus is taken once.
            //
            // LICIT WITH RESPECT TO A2.16 (SPI2 shared with the SD), and the
            // reason is an upper bound, not an opinion: the longest this can
            // hold the bus is the push of the ENTIRE zone, which is exactly
            // what `pushSprite` held every single frame before the dirty bands
            // existed (one startWrite over 102 400 B = 20,5 ms) — and still
            // does on the no-PSRAM path just above. So the bracket restores a
            // pre-existing worst case as its ceiling while the measured typical
            // is 0,6-5 ms: it cannot open a window that was not already there.
            // Nothing can slip in between the bands either — this task is the
            // ONLY one allowed to touch M5.Display (A2.1), and pause() is
            // acknowledged BETWEEN frames, never inside a push. The real remedy
            // for LCD/SD contention is unchanged and lives elsewhere: deferred
            // SD accesses bracket themselves with pause()/resume(), which stops
            // the renderer entirely rather than interleaving with it.
            //
            // ONE call site for the push, one for the ghost update (A2.22: two
            // similar drawing calls in the same body and GCC 8.4 Xtensa is
            // entitled to emit only one — here the loop IS the shape).
            M5.Display.startWrite();
            for (int i = 0; i < n; i++)
                M5.Display.pushImage(0, bands[i].y, W, bands[i].h,
                                     (const lgfx::rgb332_t*)
                                         (buf + (size_t)bands[i].y * W));
            M5.Display.endWrite();
            // The ghost catches up AFTER the transaction is closed. Inside it,
            // every one of these PSRAM copies would be held bus time for no
            // reason at all — the copy touches no peripheral. `buf` cannot move
            // in between: the renderer task owns the canvas.
            for (int i = 0; i < n; i++) {
                const size_t off = (size_t)bands[i].y * W;
                memcpy(_ghost + off, buf + off, (size_t)bands[i].h * W);
            }
        }
        _pushUs += (uint64_t)(esp_timer_get_time() - t0);
        _pushPx += (uint64_t)rows * W;
        if (++_pushN >= 60) {
            _pushAvgUs.store((uint32_t)(_pushUs / _pushN));
            _pushAvgPx.store((uint32_t)(_pushPx / _pushN));
            _pushUs = _pushPx = 0; _pushN = 0;
        }
    }

    void drawFrame(const FaceState& fs) {
        uint32_t now = _clock.ms();

        // ---- Adjustable eye spacing (tuning.eye_spacing — semantics settled
        //      2026-07-13, user): EDGE-TO-EDGE gap in px for the WIDEST
        //      preset. 0 = the edges TOUCH (centers at MID ∓ EYE_HALF_W_MAX);
        //      default 14 = the historical position (centers 90/230). The
        //      GAP-1 clamp (floor 0) only prevents OVERLAP, no longer contact.
        int16_t sp   = (int16_t)clampVal(fs.eyeSpacing, 0.0f, 44.0f);
        int16_t half = (int16_t)(units::EYE_HALF_W_MAX + sp);
        _eyeL->CenterX = (int16_t)(units::SCREEN_W / 2 - half);
        _eyeR->CenterX = (int16_t)(units::SCREEN_W / 2 + half);

        // ---- Emotion change OR asym-mirror change → transition on both rigs
        //      (the mirror alone flips at the start of a dance: the asymmetry
        //      swings to the side of the movement, user 2026-07-16) ----
        // THE FIRST FRAME IS APPLIED INSTANTLY (user 08-03: "I still see Normal
        // first"). The rigs are BORN in the Normal preset, so the first
        // published emotion used to arrive as a Normal→X morph — replaying on
        // screen a face that was never published. There is nothing to morph
        // FROM before the first frame: no previous on-screen state exists, and
        // interpolating from the constructor's default is a fiction. Shape
        // (zero-duration transition) and color (_prevEmotion) both snap.
        if (fs.emotion != _shownEmotion || fs.asymMirror != _shownMirror
            || _firstFace) {
            _prevEmotion  = _firstFace ? fs.emotion : _shownEmotion;
            _shownEmotion = fs.emotion;
            _shownMirror  = fs.asymMirror;
            const TransitionConfig tc = _firstFace
                ? TransitionConfig(EASE_IN_OUT, 0) : fs.transition;
            _eyeL->setEmotion(fs.emotion, tc, fs.asymMirror);
            _eyeR->setEmotion(fs.emotion, tc, fs.asymMirror);
        }
        _firstFace = false;

        // ---- Color: interpolated ease-in-out in sync with the shape
        //      (COLOR-1), then dimmed (tuning.eye_color_dim) ----
        float raw = _eyeL->transitionProgress();
        float t   = raw * raw * (3.0f - 2.0f * raw);
        uint32_t rgb = dimRgb888(
            lerpRgb888(emotionRgb(_prevEmotion), emotionRgb(fs.emotion), t),
            fs.colorDim);
        // (color/blink/wink are published to the LEDs by publishLedState()
        //  RIGHT before each pushSprite — the LED state matches the frame
        //  actually VISIBLE, not the one being drawn)

        // ---- Gaze → pixels + OPTIONAL near/far "depth" effect
        //      (fs.depthScale, default 0: pure offset — user 2026-07-11: the
        //      eye shrunk by the effect made the VOR harder to read)
        //      + procedural squash & stretch (§3.0-2).
        //      openL/R goes through the SEPARATE `lid` channel (closure
        //      anchored at the bottom — drooping eyelid, user 2026-07-12);
        //      scale = centered.
        Vec2f g  = units::clampGaze(fs.gaze);
        Vec2f px = units::pxFromGaze(g);
        // gaze.x > 0 = viewer right → RIGHT eye near (widens), left one far
        float ds = clampVal(fs.depthScale, 0.0f, 1.0f);
        float lidL = clampVal(fs.openL, 0.02f, 1.0f);
        float lidR = clampVal(fs.openR, 0.02f, 1.0f);
        float scaleYL = (1.0f - g.x * 0.20f * ds) * (1.0f - fabsf(g.y) * 0.40f * ds)
                      * fs.squashY;
        float scaleYR = (1.0f + g.x * 0.20f * ds) * (1.0f - fabsf(g.y) * 0.40f * ds)
                      * fs.squashY;
        _eyeL->lookAtPx(px.x, -px.y, scaleYL, fs.squashX, lidL);  // +MoveY = up
        _eyeR->lookAtPx(px.x, -px.y, scaleYR, fs.squashX, lidR);

        // ---- Drawing ----
        int16_t breathPx = (int16_t)(fs.breath * 3.0f);
        int16_t glowPx   = (fs.crt.enabled && fs.crt.glow)
                         ? (int16_t)fs.crt.glowPx : 0;
        uint32_t glowRgb = dimRgb888(rgb, fs.crt.glowDim);

        _canvas.fillSprite(TFT_BLACK);

        // ---- "Cozmo" blink (see docs/assets/cozmo.jpg): BOTH eyes fully
        //      closed = a single thin full-width line in the eye color — the
        //      Anki signature. Laid on the BOTTOM edge of the eyes (the anchor
        //      of the `lid` channel: the line sits exactly where the eyelid
        //      finishes falling — slit → line continuity; on flat-bottomed
        //      presets such as Happy/Glee/Blush it coincides with the first
        //      pixel of the bottom of the eye). A wink keeps the per-eye
        //      rendering. The rigs keep advancing (tick): no frozen morph, the
        //      line follows the transitions.
        const bool closedL = fs.openL <= units::EYE_CLOSED,
                   closedR = fs.openR <= units::EYE_CLOSED;
        const bool closed  = closedL && closedR;
        const bool wink    = (closedL != closedR);
        if (closed) {
            _eyeL->tick();
            _eyeR->tick();
            int16_t lineY = (int16_t)((_eyeL->bottomEdgeY(breathPx)
                                     + _eyeR->bottomEdgeY(breathPx)) / 2);
            _canvas.drawFastHLine(0, lineY, units::SCREEN_W, rgb);
            _crt.apply((uint8_t*)_canvas.getBuffer(), fs.crt, now);
            publishLedState(rgb, 0.0f, 0.0f);   // both eyes closed → bars off
            pushEyeZone();
            return;
        }

        _eyeL->draw(&_canvas, rgb, breathPx, glowPx, glowRgb);
        _eyeR->draw(&_canvas, rgb, breathPx, glowPx, glowRgb);

        // ---- Emotion overlays (blush/sparkles/sweat — T7): after the eyes,
        //      before the CRT (so the effects get the post-process too). The
        //      blush sits a few px BELOW the REAL bottom edge of the eyes
        //      (average of both) and follows the horizontal gaze (px.x) →
        //      cheeks move with the face (gaze + squash; bottom edge
        //      invariant under blink for Blush/Glee, A2.17). ----
        // +25: max half-height of the cluster ~9 px → the TOP of the strokes
        //      stays ~16 px BELOW the bottom edge of the eyes (well clear).
        const int16_t eyeBottom = (int16_t)((_eyeL->bottomEdgeY(breathPx)
                                           + _eyeR->bottomEdgeY(breathPx)) / 2);
        effects::drawForEmotion(&_canvas, now, fs.emotion, t, fs.colorDim,
                                px.x, (float)eyeBottom + 25.0f);

        // ---- CRT post-process + push ----
        _crt.apply((uint8_t*)_canvas.getBuffer(), fs.crt, now);
        // Bar brightness ∝ the drawn SIZE of each eye (after draw():
        // _variation2.Output is up to date), normalized on the reference eye.
        publishLedState(rgb, heightNorm(_eyeL->currentHeight()),
                             heightNorm(_eyeR->currentHeight()));
        pushEyeZone();
    }

    // ------------------------------------------------------------------
    // "…" waiting screen (busy mode, renderer task): black background +
    // drawBusyDots on the canvas. Color = the dimmed identity cyan.
    // LEDs: off during the freeze (publishLedState closed).
    // ------------------------------------------------------------------
    void drawBusyFrame() {
        _canvas.fillSprite(TFT_BLACK);
        drawBusyDots(_canvas,
                     (int16_t)(units::SCREEN_W / 2),
                     (int16_t)(units::EYEZONE_H / 2));
        publishLedState(dimRgb888(CALM_CYAN, 0.80f),
                        0.0f, 0.0f);   // "…" freeze → bars off
        // Through the SAME push: the waiting screen is OURS (setBusy does not
        // hand the panel over), so the ghost must record it — otherwise the
        // first frame after the freeze would believe the dots are still there.
        pushEyeZone();
    }

    // Eye height (px) → LED bar brightness factor [0..1], normalized on the
    // reference eye (Surprised) then CURVED (gamma 0.4). The curve lifts the
    // short but OPEN "smile" eyes (Happy H25, Glee H20 → ~0.55/0.51 instead of
    // 0.22/0.18, far too dark in linear) while keeping 0 at zero (blink → off)
    // and the gap at the top (Surprised 1.0 vs Curious small eye ~0.81) → the
    // asymmetry stays readable.
    static float heightNorm(int16_t h) {
        float n = (float)h / units::EYE_HEIGHT_MAX;
        if (n <= 0.0f) return 0.0f;
        if (n >= 1.0f) return 1.0f;
        return powf(n, 0.4f);
    }

    // Publishes the state read by the LEDs (from loop()) as close to the
    // pushSprite as possible: drawing a frame takes ~20-30 ms — publishing at
    // the start of the frame put the LEDs ONE frame AHEAD of the screen (user
    // 2026-07-16). heightL/R = normalized height [0..1] of each eye
    // (0 = closed/nothing).
    void publishLedState(uint32_t rgb, float heightL, float heightR) {
        _eyeRgb.store(rgb);
        _heightL.store(heightL);
        _heightR.store(heightR);
    }

    // =====================================================================
    // STATUS BAR (y 160..240) — drawn DIRECTLY on M5.Display, from the
    // renderer task (rule 1). Two regions: dynamic area (top) + icon row
    // (bottom). Per-region change-detection → near-zero cost in steady state;
    // the VU meter redraws its segments IN PLACE (no fillRect).
    // Dynamic-area priority: alert (setStatus) > say (timed) > mode.
    // =====================================================================
    static constexpr int BAND_TOP = units::EYEZONE_H;          // 160
    // units:: owns it: the touch routing needs the same number to tell the
    // phase icon from the digits, and two spellings of 168 is one too many.
    static constexpr int DYN_Y    = units::BAND_DYN_Y;         // 168
    // units:: owns it: the text row's position is derived from this one, so
    // a second spelling of 225 would let the two drift apart.
    static constexpr int ICON_Y   = units::BAND_STATUS_Y;      // 225

    uint16_t accent() const {
        uint32_t c = _eyeRgb.load();
        return M5.Display.color565((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    }

    // 0 idle (gray)  1 run/work (eye colour)  2 paused (amber)
    // 3 ring (red)   4 break/done (green)
    // ONE table, two readers: the digits and the progress rule are the same
    // state said twice, and a second copy of this switch is how they start
    // disagreeing about what colour "paused" is.
    uint16_t bandStateColor(int st) const {
        switch (st) {
            case 1:  return accent();
            case 2:  return M5.Display.color565(0xf2, 0xb5, 0x44);
            case 3:  return TFT_RED;
            case 4:  return M5.Display.color565(0x4f, 0xc0, 0x8a);
            default: return M5.Display.color565(0x85, 0x93, 0xab);
        }
    }

    // ---- THE PROGRESS RULE, ALONG THE TOP EDGE OF THE BAND ----------------
    // Two rows at the very top of the band, and it doubles as the separator
    // between the face and the status area — the whole reason it sits there
    // rather than under the digits, where it read as a second widget arguing
    // with them (user 08-25).
    //
    // IT GROWS FROM THE CENTRE OUTWARD, staying centred, and the unspent part
    // is never drawn. So an empty phase shows NOTHING, a finished one shows a
    // full-width line, and in between a mark that opens symmetrically about
    // the middle of the screen — under the middle of the digits, which are
    // centred too. There is no track: the extent is the screen.
    //
    // IT IS DRAWN ONLY BY THE MODES THAT HAVE PROGRESS TO REPORT. A permanent
    // divider was an earlier shape of this and it was one line too many: the
    // band is short of air, not of furniture, and a rule that is always lit
    // says nothing on the four modes that have nothing to say.
    //
    // `tmr_pg` is not consulted outside those modes: it outlives the mode that
    // wrote it, so a pomodoro abandoned at 70 % would otherwise leave a stripe
    // burnt across the top of the sound visualiser.
    //
    // THREE SEGMENTS, ONE CALL SITE (A2.22), and the two black ones are not
    // decoration: they erase the flanks the lit part no longer covers, which
    // is what lets the mark CLOSE again at a phase change. Erasing the whole
    // width first and repainting over it would flicker the lit part every
    // frame; erasing only the flanks leaves the lit pixels untouched. Written
    // as three fillRect calls in one body it is exactly the group GCC 8.4
    // Xtensa may thin, and the symptom would be a mark that never closes on
    // one side.
    void drawBandRule(int mode) {
        if (mode != BAND_TIMER && mode != BAND_POMO) return;
        float pg = 0.0f;
        int   st = 0;
        if (_fields) {
            pg = _fields->getF("tmr_pg", 0.0f);
            st = (int)_fields->getF("tmr_st", 0.0f);
        }
        const int BW = units::SCREEN_W;
        int w = (int)(pg * (float)BW + 0.5f);
        if (w < 0)  w = 0;
        if (w > BW) w = BW;
        const int x0 = (BW - w) / 2;            // centred, always
        const struct { int x, w; uint16_t c; } seg[3] = {
            { 0,      x0,           TFT_BLACK },          // left flank
            { x0,     w,            bandStateColor(st) }, // the mark
            { x0 + w, BW - x0 - w,  TFT_BLACK },          // right flank
        };
        for (int i = 0; i < 3; i++)
            if (seg[i].w > 0)
                M5.Display.fillRect(seg[i].x, units::BAND_BAR_Y, seg[i].w,
                                    units::BAND_BAR_H, seg[i].c);
    }

    void drawStatusBand(const FaceState& fs) {
        uint32_t now = _clock.ms();
        int mode = _bandMode.load();
        M5.Display.setTextWrap(false);           // band: never wrap lines
        // Resume after a pause (launcher/SD-Updater): full wipe of the area +
        // reset of the change-detection memos (menu leftovers, 07-26)
        if (_bandInvalidate.exchange(false)) {
            _lastMode = -1;                      // forces the wipe below
            _lastDbgHash = 0;
            // THE GAUGE MEMOS GO WITH IT. Something else has just painted over
            // the band (launcher, SD-Updater, busy screen), so a row whose
            // value has not changed is no longer on screen even though its
            // signature says it is — exactly the same reason `_lastMode` and
            // `_lastDbgHash` are reset here. Change detection is only ever as
            // good as its list of the things that invalidate it.
            _gaugeFirst = true;
        }
        if (mode != _lastMode) {                 // mode change: wipe
            M5.Display.fillRect(0, BAND_TOP, units::SCREEN_W,
                                units::SCREEN_H - BAND_TOP, TFT_BLACK);
            bandWiped();                     // includes the gauge memos
            _lastMode = mode; _dynScroll = false;
        }

        // ---- dynamic area: alert > say > mode ----
        char over[160]; bool alert = false, say = false;
        portENTER_CRITICAL(&_statusMux);
        if (_statusText[0]) { copyLocal(over, _statusText, sizeof(over)); alert = true; }
        else if ((int32_t)(_sayUntil - now) > 0) { copyLocal(over, _sayText, sizeof(over)); say = true; }
        portEXIT_CRITICAL(&_statusMux);

        // Leaving text (say expired / alert cleared) → WIPE the leftovers
        // before handing back to the mode (fix: lingering text fragments).
        const bool isText = alert || say;
        if (!isText && _dynHadText) { clearDyn(); _lastDyn[0] = '\0'; _dynScroll = false; }
        // ENTERING text over the visualiser → wipe ITS zone, not the text's.
        // The sound band is taller than the text layout it used to borrow its
        // forty rows from, so the per-frame text wipe no longer covers it and
        // bars would survive above and below the line. Once, on the transition:
        // widening the scrolling wipe to fifty-nine rows would pay for this
        // every frame instead.
        if (isText && !_dynHadText && mode == BAND_SOUND) sndRest();
        _dynHadText = isText;

        const uint8_t ts = _sayTextSize.load();
        // Alert: ALWAYS readable (min size 2), whatever band_text_size says.
        if (alert)     drawDynText(over, TFT_RED, ts < 2 ? 2 : ts);
        else if (say)  drawDynText(over, accent(), ts);
        else switch (mode) {
            case BAND_SOUND:     drawSound();     break;
            case BAND_GAUGES: drawGauges(now); break;
            case BAND_TIMER:
            case BAND_POMO:   drawBandClock(); break;
            default: /* OFF */
                // The `band_clock` OPTION (user 08-04): mode 0 can carry the
                // wall clock instead of black. Same pipe as everything else:
                // loop() publishes `clk` (local HH:MM, empty until NTP) and
                // drawDynText's memo repaints once a minute. Discreet gray
                // on purpose — it replaces "nothing", not a widget.
                if (_bandClock.load()) {
                    char c[8] = "";
                    if (_fields) _fields->getS("clk", c, sizeof(c));
                    if (c[0]) {
                        // Same full-size face as the timer (user 08-04) —
                        // which is exactly why the timer wears an hourglass.
                        drawDynText(c, M5.Display.color565(0x85, 0x93, 0xab), 4);
                        break;
                    }
                }
                if (_lastDyn[0]) { clearDyn(); _lastDyn[0] = '\0'; }
        }

        drawBandRule(mode);
        drawIcons(fs);
    }

    // EVERY WIPE OF THE BAND FORGETS THE ROWS IT ERASED. The gauge change
    // detection was armed by `_bandInvalidate` alone, and TWO other paths in
    // the same function black those rows out: this one (an alert or a /api/say
    // erases y166-206, i.e. gauge rows 0, 1 and the top of 2) and the
    // mode-change wipe. When the text cleared, every row recomputed the same
    // signature, matched, and was skipped — the band stayed black until a value
    // moved by a whole percent, possibly never (review 08-03). Before the
    // change detection existed the rows redrew unconditionally, so it healed
    // itself; that is exactly the class of regression change detection adds.
    // ONE function, called from all three sites: the list of things that
    // invalidate a memo is the memo's real definition.
    void bandWiped() {
        _lastMode    = -1;
        _lastDbgHash = 0;
        _lastIconSig = 0xFFFFFFFF;
        _gaugeFirst  = true;
        // The sound visualiser draws DELTAS against what it believes is on
        // screen. Once something else has painted over the band that belief is
        // false, and a widget that keeps differencing against it stays stuck
        // half open.
        _sndFirst  = true;
        _lastDyn[0]  = '\0';
    }

    void clearDyn() {
        M5.Display.fillRect(0, DYN_Y - 2, units::SCREEN_W, 40, TFT_BLACK);
        _gaugeFirst = true;                  // those rows are gone: repaint them
        _sndFirst = true;                  // and so is the visualiser's memory
    }

    // Timer / pomodoro band (modes 4/5, 08-04). The MACHINE lives app-side
    // (engine/BandTimer.h owned by loop, which also owns the touch wiring):
    // this draws exactly what the blackboard says — `tmr` is the composed
    // text ("01:30", "2/4 24:59", blank during the ring's off-beat: loop
    // alternates it, and drawDynText's change detection turns that into the
    // blink for free) and `tmr_st` picks the colour. All the drawing —
    // centring, memoisation, wipe — is drawDynText's existing machinery;
    // this function owns nothing but the colour table.
    void drawBandClock() {
        char txt[24] = "";
        if (_fields) _fields->getS("tmr", txt, sizeof(txt));
        const int st = _fields ? (int)_fields->getF("tmr_st", 0.0f) : 0;
        if (!txt[0]) {                       // ring off-beat / no state yet
            if (_lastDyn[0]) { clearDyn(); _lastDyn[0] = '\0'; }
            return;
        }
        const uint16_t col = bandStateColor(st);
        // Full-size digits (user 08-04, after the hold-and-slide scroll
        // landed: the +/- affordance rows of the brief tap-pattern era went
        // with it — the scroll needs no on-screen hint, and the size the
        // hints had cost went back to the digits).
        // Shifted right by half the room the stamp occupies, so that
        // icon + gap + digits reads as ONE centred object (units §7).
        drawDynText(txt, col, 4, units::BAND_TEXT_DX);
        // THE HOURGLASS (user 08-04): the wall clock now shares the same
        // full-size face, so the TIMER wears a pixel-art hourglass on its
        // left — the icon is what tells them apart at a glance (the
        // pomodoro's cycle prefix already does that job for mode 5). Tinted
        // with the state colour, redrawn every frame (an 18x24 opaque stamp:
        // identical pixels, no flicker, negligible cost) and it blinks with
        // the ring's off-beat since the empty-text return above skips it.
        {
            // 9x12 grid, 2 px cells -> an 18x24 stamp modelled on the user's
            // pixel-art reference: thick bars, glass
            // walls meeting in an X, a sand sparkle in the top chamber, a
            // mound in the bottom one. VERTICALLY CENTRED ON THE INK of the
            // digits (user 08-04, twice — the first attempt centred on the
            // CELL and still sat low). The built-in font is 5x7 glyphs in a
            // 6x8 cell: the eighth row is spacing, so at size 4 the cursor at
            // DYN_Y+6 puts 28 px of ink from +6 to +33, whose middle is
            // +19.5 — not the +22 the 32 px cell suggests. A 24 px icon
            // therefore starts at DYN_Y + 8, not +10.
            // ONE STAMP PER PHASE, and ONE call site for all of them (A2.22).
            // The tables are DATA; the loop below is the only thing that
            // draws. Written as an if-chain of six little loops it would be
            // six similar fillRect bodies in one function, which is precisely
            // the shape GCC 8.4 Xtensa is entitled to thin out — and the
            // symptom would be an icon that silently never appears for one
            // phase. Adding a seventh phase is adding a table.
            static const char* HG[12] = {          // hourglass: countdown
                "#########",
                "#########",
                " #     # ",
                " # # # # ",
                "  # # #  ",
                "   ###   ",
                "    #    ",
                "   # #   ",
                "  # # #  ",
                " # ### # ",
                "#########",
                "#########" };
            static const char* BRAIN[12] = {       // work: a head, thinking
                "  #####  ",
                " ##   ## ",
                "# ## ## #",
                "# # # # #",
                "# ## ## #",
                "# # # # #",
                "# ## ## #",
                " ##   ## ",
                "  #####  ",
                "   # #   ",
                "  #####  ",
                "   ###   " };
            static const char* DROP[12] = {        // hydrate: a water drop
                "    #    ",
                "    #    ",
                "   ###   ",
                "   ###   ",
                "  #####  ",
                "  #####  ",
                " ####### ",
                " ## #### ",
                " ## #### ",
                " ####### ",
                "  #####  ",
                "   ###   " };
            static const char* CUP[12] = {         // break: a mug, steaming
                "  #   #  ",
                "   # #   ",
                "  #   #  ",
                "         ",
                "#######  ",
                "#     ###",
                "#     # #",
                "#     ###",
                "#     #  ",
                " #####   ",
                "  ###    ",
                "         " };
            static const char* BELL[12] = {        // ring: the alarm
                "   ###   ",
                "  #   #  ",
                "  #   #  ",
                " #     # ",
                " #     # ",
                " #     # ",
                "#       #",
                "#       #",
                "#########",
                "#########",
                "   ###   ",
                "   ###   " };
            static const char* FLAG[12] = {        // done: the session is over
                " #       ",
                " ####### ",
                " #     # ",
                " # ### # ",
                " #     # ",
                " ####### ",
                " #       ",
                " #       ",
                " #       ",
                " #       ",
                "####     ",
                "####     " };
            // Phase codes are BandTimer::Phase, published as `tmr_ph`:
            // 0 Idle 1 Run 2 Paused 3 Ring 4 Work 5 Break 6 Done 7 Hydrate.
            // An unknown value falls back to the hourglass rather than drawing
            // nothing — a missing icon reads as a rendering fault, and the
            // hourglass is true of anything that counts down.
            const int ph = _fields ? (int)_fields->getF("tmr_ph", 0.0f) : 0;
            const char* const* G = HG;
            switch (ph) {
                case 4:  G = BRAIN; break;
                case 7:  G = DROP;  break;
                case 5:  G = CUP;   break;
                case 3:  G = BELL;  break;
                case 6:  G = FLAG;  break;
                default: G = HG;    break;
            }
            // HORIZONTAL GAP, matched to the one around the COLON (user
            // 08-04). Naively the answer is the 4 px of cell padding between
            // two digits — but the colon's ink is only columns 1-2 of the
            // 5-wide glyph, so at size 4 it floats with 8 px of clear air on
            // one side and 12 on the other, and THAT is the space the eye
            // reads as "the separator's gap". The icon is set 12 px clear of
            // the first digit to sit at the same visual distance; 24 (the old
            // value) left it 6 px away, visibly tighter than the colon.
            // FROM units::, not from a formula spelled out here: the finger
            // has to find this stamp too (mode 5 taps it to cycle the
            // session's shape), and a hit box that recomputes the layout is a
            // hit box that stops agreeing with the paint the first time
            // either is touched. One definition, two readers (units §7).
            const int x0 = units::bandIconX0((int)strlen(txt));
            const int y0 = units::BAND_ICON_Y;
            M5.Display.fillRect(x0 - 1, y0 - 1,
                                units::BAND_ICON_W + 2,
                                units::BAND_ICON_H + 2, TFT_BLACK);
            for (int r = 0; r < 12; r++)
                for (int cx = 0; cx < 9; cx++)
                    if (G[r][cx] == '#')
                        M5.Display.fillRect(x0 + cx * 2, y0 + r * 2, 2, 2, col);
        }
    }

    // Dynamic area: short text CENTERED (redrawn only when it changes); text
    // too long → SCROLLS (leftwards marquee, continuous wrap). The opaque text
    // background + a clear of the line avoid smearing trails.
    // `dx` shifts the STATIC (centred) layout right. It exists for the band
    // clock, whose icon+gap+digits assembly is centred as ONE object while
    // this function only ever sees the digits (units::BAND_TEXT_DX). Every
    // other caller passes 0 and keeps the plain centring. It takes part in the
    // memo below: a change of offset must repaint, or the digits would stay
    // where the previous caller left them.
    void drawDynText(const char* s, uint16_t col, uint8_t size, int dx = 0) {
        const int  w      = (int)strlen(s) * 6 * size;
        const int  MARGIN = 4;
        const int  avail  = units::SCREEN_W - 2 * MARGIN;
        const int  lineH  = 8 * size + 6;

        if (w <= avail) {                        // ---- STATIC (centered) ----
            // Memo on (text, size, color): a size/hue change at constant text
            // MUST redraw (band_text_size, eye color).
            if (!_dynScroll && size == _lastDynSize && col == _lastDynCol
                && dx == _lastDynDx
                && strncmp(s, _lastDyn, sizeof(_lastDyn)) == 0) return;
            _dynScroll = false;
            _lastDynSize = size; _lastDynCol = col; _lastDynDx = dx;
            copyLocal(_lastDyn, s, sizeof(_lastDyn));
            clearDyn();
            M5.Display.setTextSize(size);
            M5.Display.setTextColor(col, TFT_BLACK);
            int x = (units::SCREEN_W - w) / 2 + dx;
            if (x + w > units::SCREEN_W - MARGIN)      // never off the right
                x = units::SCREEN_W - MARGIN - w;
            if (x < MARGIN) x = MARGIN;
            M5.Display.setCursor(x, DYN_Y + 6);
            M5.Display.print(s);
            M5.Display.setTextSize(1);
            return;
        }

        // ---- MARQUEE (long text): redrawn every frame ----
        if (!_dynScroll) { clearDyn(); _dynScroll = true; }  // 1st pass: wipe
        copyLocal(_lastDyn, s, sizeof(_lastDyn));
        const int GAP    = 28;                   // hole between two copies
        const int period = w + GAP;
        // offset = pixels travelled = time × speed (px/s), modulo the period
        const uint32_t spd = _scrollSpeed.load();
        const int off = (int)(((uint64_t)_clock.ms() * spd / 1000u) % (uint32_t)period);
        M5.Display.fillRect(0, DYN_Y + 2, units::SCREEN_W, lineH, TFT_BLACK);
        M5.Display.setTextSize(size);
        M5.Display.setTextColor(col, TFT_BLACK);
        const int y = DYN_Y + 6;
        M5.Display.setCursor(MARGIN - off, y);            M5.Display.print(s);
        M5.Display.setCursor(MARGIN - off + period, y);   M5.Display.print(s);
        M5.Display.setTextSize(1);
    }

    // ===================== THE SOUND VISUALISER ==========================
    // Band mode 2. Three SKINS OF ONE PICTURE — the triggered oscilloscope
    // window, time across the width — computed on loop() by `SoundViz` and
    // handed over in a `SoundFrame`. Nothing is measured, smoothed or scaled
    // here: A2.15 puts all of that on the producer side, including the
    // envelope the bar skins draw and the peak-hold matrix marks, and this
    // stays a painter. Switching skins changes the clothing, never the
    // statement.
    //
    // BOTH MICROPHONES, AND NO FORCED SYMMETRY. The robot has a left and a
    // right microphone; an early design mirrored one level about a centre
    // line, which drew the same number twice. Here the two channels are two
    // signals: `wave` plots them as two traces, and the bar skins take the
    // LOUDER of the two per column — a silhouette has one height, and a mix
    // would let a channel hide inside the other. What a channel does, it does
    // alone.
    //
    //   0 WAVE     two continuous traces, one per microphone, plotted one
    //              pixel per column. The producer TRIGGERS each trace on a
    //              rising zero crossing, which is what stops the picture
    //              sliding sideways when a steady tone is playing. The ink
    //              follows the agitation: nearly black at rest, whitening
    //              through the ramp as the curve moves.
    //   1 COLUMNS  the same window as thin strokes, well apart: each is the
    //              envelope of its five samples. Monochrome.
    //   2 MATRIX   the same envelope quantised into stacks of square blocks —
    //              an LED analyser — with a pale peak marker detached beyond
    //              each end.
    //
    // The band SPECTRUM stays computed and published in `SoundFrame`: nothing
    // draws it today, it is the assistant mouth's food (ROADMAP §8).
    //
    // COLOUR is the launcher's cyan-to-indigo ramp (`sndGrad`), the same one
    // the lobby, the flash screen and the web console sweep between. Deliberately
    // not the eye colour: that rule governs what the FACE wears, and this is
    // chrome.
    //
    // THE BAND IS PAINTED STRAIGHT TO THE PANEL, no back buffer, and the eyes
    // already spend most of the 33 ms frame. So: DELTAS ONLY. Every column
    // remembers what is actually on the glass and repaints just what moved,
    // and every rectangle goes through ONE fillRect call site (A2.22).
    enum : uint8_t { SND_WAVE = 0, SND_COLUMNS = 1, SND_MATRIX = 2 };

    // THE ZONE IS THE WHOLE BAND, not the text layout's forty rows. It used to
    // be `DYN_Y - 2` to `+39`, which is where a scrolling line of 8-pixel type
    // wants to sit — but a visualiser is not type. Between the eye zone's last
    // row (159) and the icon row's own wipe (223) there are sixty-three rows,
    // and it was using forty; the trace was a thin ribbon in a wide black
    // strip.
    //
    // NOT all sixty-three, though. The margins are DELIBERATELY unequal: three
    // rows under the eyes, but ten above the icons. The icon row is a line of
    // small glyphs, and a full-scale bar landing two rows under it read as
    // touching it — two unrelated things fused into one crowded strip. Above,
    // the eye zone is mostly black at the bottom, so the trace has air there
    // whether or not the constant says so.
    static constexpr int SND_TOP  = BAND_TOP + 3;      // 163, the zone
    static constexpr int SND_BOT  = ICON_Y - 10;       // 215, its last row
    // THE AXIS, and everything is measured from it. It sits at the MIDDLE of
    // the zone rather than 3 px above it, because every style grows both
    // ways from here: an axis off centre would give a display more room
    // downwards than upwards and the mirror would be visibly lopsided. Moving
    // it also stopped the scope clipping — at 182 with a half-height of 18 the
    // trace reached row 164, two rows above the zone, and mSeg quietly cut it.
    static constexpr int SND_CY   = (SND_TOP + SND_BOT) / 2;   // 189
    // The shorter of the two sides, so a full-scale display reaches the edge in
    // BOTH directions and neither is clipped.
    static constexpr int SND_ROOM = (SND_CY - SND_TOP) < (SND_BOT - SND_CY)
                                  ? (SND_CY - SND_TOP) : (SND_BOT - SND_CY);
    // ...ROUNDED DOWN TO 4m+3, which is matrix's own arithmetic and not a taste.
    // Its outermost mark is the peak block at k = HALF/4, whose top row is
    // CY - 4*(HALF/4) - 3; that row is inside the zone exactly when HALF is
    // 4m+3. At 26 the peak block of a full-scale column would have sat three
    // rows below the zone and `mSeg` would have quietly trimmed it — the same
    // silent clipping the axis was moved to cure, back by another door.
    static constexpr int SND_HALF = ((SND_ROOM - 3) / 4) * 4 + 3;   // 26 -> 23
    // The scope uses the SAME half-height: three skins of one waveform must
    // agree about how tall full scale is, or switching skins would rescale the
    // picture.
    static constexpr int SND_AMP  = SND_HALF;
    // ...and the compiler checks it, because `mSeg` clips SILENTLY: a marker
    // one row outside the zone does not crash, it just stops being drawn at
    // full scale, which is the moment the display is looked at. That is how the
    // scope was clipped for weeks before anyone noticed.
    static_assert(SND_CY - (SND_HALF / 4) * 4 - 3 >= SND_TOP,
                  "matrix peak block clipped above the zone");
    static_assert(SND_CY + (SND_HALF / 4) * 4 + 3 <= SND_BOT,
                  "matrix peak block clipped below the zone");
    static_assert(SND_CY - SND_AMP >= SND_TOP && SND_CY + SND_AMP <= SND_BOT,
                  "scope trace clipped by the zone");

    // ---- geometry per style ---------------------------------------------
    // WAVE plots one column per sample and the columns touch, so the trace is
    // a line and not a row of dashes. The two bar skins quantise the same
    // window into 32 columns, time across the width — no halves, no mirror.
    // ALL THREE SPAN THE FULL 320: the band is the
    // robot's mouth-height strip and a margin around a signal display reads
    // as a window onto something, not as the thing itself.
    static constexpr int SND_W_X0 = (units::SCREEN_W - SoundFrame::WAVE_N * 2) / 2;
    static constexpr int SND_W_PITCH = 2;              // 160 * 2 = 320, edge to edge

    static constexpr int SND_BARS  = SoundFrame::BAND_N * 2;   // 32 across
    static constexpr int SND_B_PITCH = 10;             // 32 * 10 = 320, edge to edge
    static constexpr int SND_B_X0 = (units::SCREEN_W - SND_BARS * SND_B_PITCH) / 2;

    // What is on the panel, so a frame paints only the difference.
    int8_t  _sndPrevL[SoundFrame::WAVE_N] = { 0 };
    int8_t  _sndPrevR[SoundFrame::WAVE_N] = { 0 };
    uint8_t _sndPrevBar[SND_BARS]  = { 0 };
    uint8_t _sndPrevPeak[SND_BARS] = { 0 };   // matrix's peak marker
    uint8_t _sndShown  = 0xFF;         // style currently on the panel
    bool    _sndFirst  = true;
    bool    _sndLive   = false;

    struct MSeg { int16_t x, y, w, h; uint16_t c; };
    // Sized against the WORST CASE, which is WAVE: 160 columns x 2 channels x
    // (one erase + one draw) = 640 — raised from 560 the day WAVE_N grew to
    // span the full panel, exactly the drift its old comment warned about.
    // MATRIX peaks near 384 (32 stacks x (4 block rows + a peak) x 2 sides).
    // Past this cap `mSeg` DROPS the segment silently rather than overrun, so
    // the margin is what keeps paint from vanishing — widen it if WAVE_N,
    // BAND_N or SND_HALF ever grow again.
    static constexpr int SND_SEGS = 704;
    static_assert(SND_SEGS >= SoundFrame::WAVE_N * 2 * 2,
                  "the segment table cannot hold a full WAVE frame");
    static_assert(SND_SEGS >= SND_BARS * ((SND_HALF / 4) * 2 + 4),
                  "the segment table cannot hold a full MATRIX frame");
    MSeg _mSeg[SND_SEGS];
    int  _mNs = 0;
    void mSeg(int x, int y, int w, int h, uint16_t c) {
        if (w <= 0 || h <= 0 || _mNs >= SND_SEGS) return;
        if (y < SND_TOP) { h -= (SND_TOP - y); y = SND_TOP; }
        if (y + h > SND_BOT + 1) h = SND_BOT + 1 - y;
        if (h <= 0) return;
        _mSeg[_mNs++] = { (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, c };
    }

    // THE COMPANION IDENTITY: cyan -> indigo, the ramp the launcher, the lobby,
    // the flash screen and the web console all use (`--acc-grad`, and
    // `L_ACC`/`L_ACC2` in SceGuest.h). `t` runs across the width, `k` is
    // lightness. Saturation is said by going PALE, towards the console's text
    // colour, rather than by borrowing an amber from another palette.
    // `hot` is an AMOUNT, not a flag: wave whitens PROGRESSIVELY with the
    // agitation of the curve, and a boolean can only snap. `true` still
    // converts to 1.0f, so every earlier call site keeps its meaning.
    // In RGB888, so callers that need to keep MIXING (the peak marker walks on
    // towards white) are not forced to re-derive the ramp from scratch.
    static uint32_t sndGradRgb(float t, float k, float hot = 0.0f) {
        if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
        if (k < 0.0f) k = 0.0f; if (k > 1.0f) k = 1.0f;
        if (hot < 0.0f) hot = 0.0f; if (hot > 1.0f) hot = 1.0f;
        // Composed from the SAME two helpers the eye-colour transition uses
        // (Emotions.h): a two-stop lerp and a lightness scale. Hand-rolling the
        // arithmetic here would be a second blending convention to keep in step
        // with the first.
        uint32_t c = lerpRgb888(0x22d3ee, 0x818cf8, t);   // the launcher's ramp
        if (hot > 0.0f) c = lerpRgb888(c, 0xe8eefb, 0.75f * hot);   // pale, not amber
        return dimRgb888(c, k);
    }
    static uint16_t rgb565(uint32_t c) {
        return M5.Display.color565((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    }
    static uint16_t sndGrad(float t, float k, float hot = 0.0f) {
        return rgb565(sndGradRgb(t, k, hot));
    }

    // THE PEAK MARKER'S COLOUR SAYS HOW HIGH IT IS. `u` is its distance from
    // the axis over the zone's half-height: on the ramp and dim near the
    // centre, white at the edge.
    //
    // A marker of one fixed colour made every transient look alike — a clap
    // that reached full scale and a door closing three blocks up were the same
    // mark in two places, and the eye had to read the POSITION to learn which.
    // Colour is the faster channel, so it now carries the same fact the height
    // does, and the two agree by construction.
    //
    // The whitening is QUADRATIC on purpose. Linear, everything above the
    // middle read as roughly white and the top of the scale stopped being
    // special; squared, the marker stays on the palette for most of its travel
    // and only the peaks that actually reach the edge go white — which is also
    // what keeps a busy room from being a row of white specks.
    static uint16_t sndPeakCol(float t, float u) {
        if (u < 0.0f) u = 0.0f; if (u > 1.0f) u = 1.0f;
        const uint32_t c = sndGradRgb(t, 0.55f + 0.45f * u);
        return rgb565(lerpRgb888(c, 0xffffff, u * u));
    }

    // The wipe, and nothing else. No rule is laid under the visualiser: what
    // is on the glass is what the sound put there, so silence is a blank band.
    // Its own function — called once on entering the mode, it has no business
    // being inlined into the per-frame body.
    __attribute__((noinline)) void sndRest() {
        M5.Display.fillRect(0, SND_TOP, units::SCREEN_W,
                            SND_BOT - SND_TOP + 1, TFT_BLACK);
    }

    // ---- the painters. They only FILL THE TABLE; nothing here draws, which
    // is what keeps the single call site single however many styles exist.


    // A bar-skin column: a thin stroke growing BOTH WAYS from the axis, the
    // shape the reference visualisers use — the band reads as one waveform
    // seen edge-on rather than as a row of towers standing on a floor.
    //
    // NO PEAK MARKER. It was a bright pixel detached above each bar, and at
    // this width it read as a speck of dirt rather than as a memory of a
    // transient. Matrix keeps its, where a detached BLOCK is legible; here the
    // level alone carries the meaning, which is also what the reference shows.
    //
    // NO ROUNDED CAP EITHER. At three pixels wide a "round end" is one inset
    // pixel, invisible — and keeping it correct across growth and shrink is
    // exactly the bookkeeping that produced the notch bug on the old peak
    // marker. A shape that cannot be seen is not worth a special case.
    //
    // `h` is a HALF-height: the stroke covers rows [CY-h, CY+h], so h = 0 is
    // one blank column and not a one-pixel stub. Silence draws nothing.
    void sndBar(int x, int w, int prevH, int curH, uint16_t col) {
        if (prevH == curH) return;
        // DELTAS, and the two ends are symmetric so each case is one pair of
        // slivers. The from-nothing and to-nothing cases are separate because
        // the centre row belongs to neither sliver: [CY-c, CY-p-1] and
        // [CY+p+1, CY+c] skip row CY itself, which is right only when the
        // column was already occupying it.
        if (prevH == 0) {
            mSeg(x, SND_CY - curH, w, curH * 2 + 1, col);
        } else if (curH == 0) {
            mSeg(x, SND_CY - prevH, w, prevH * 2 + 1, TFT_BLACK);
        } else if (curH > prevH) {
            const int d = curH - prevH;
            mSeg(x, SND_CY - curH,      w, d, col);
            mSeg(x, SND_CY + prevH + 1, w, d, col);
        } else {
            const int d = prevH - curH;
            mSeg(x, SND_CY - prevH,     w, d, TFT_BLACK);
            mSeg(x, SND_CY + curH + 1,  w, d, TFT_BLACK);
        }
    }

    // HOW MANY BLOCKS A LEVEL IS WORTH — and a non-zero level is always worth
    // at least one. Plain `h / 4` meant anything under a sixth of full scale
    // drew NOTHING, while `columns` — the very same numbers, the other skin —
    // drew it: two skins of one picture disagreeing about whether there is any
    // sound at all, which is the one thing they may never disagree about. The
    // quantisation still governs the LEVEL; it no longer governs PRESENCE.
    //
    // The ceiling is untouched, so the geometry holds: `blocks(SND_HALF)` is
    // still `SND_HALF / 4`, and the peak marker still lands one block beyond
    // the tallest stack block, inside the zone (see the static_asserts above).
    static int blocks(int h) { return h <= 0 ? 0 : (h < 4 ? 1 : h / 4); }

    // A matrix column: SQUARE blocks (3 x 3 — the caller passes the width, and
    // it matches the height on purpose) on a four-pixel pitch, stacked from the
    // axis BOTH WAYS, the outer ones lighter, and a WHITE peak marker detached
    // beyond each end.
    //
    // The marker is legitimate here and not a renderer invention: `curP` comes
    // from `SoundFrame::envPeak`, held and decayed by SoundViz on the producer
    // side (A2.15). White rather than a pale tint of the ramp so it reads as a
    // different KIND of mark — a memory, not a level.
    //
    // Block k of a half occupies rows [CY-3-4k, CY-1-4k] above and
    // [CY+1+4k, CY+3+4k] below: a ONE-ROW seam on the axis, so the two halves
    // read as a mirror instead of fusing into one tall column — and so the
    // outermost block at k=4 still fits the zone whole on both sides.
    void sndBlocks(int x, int w, int prevH, int prevP, int curH, int curP,
                   float t) {
        // The gradient's span is DERIVED from the stack's own maximum, not
        // written as a number. It used to be a literal 8 — the block count of
        // the old full-height bar — and when the bar became a half-height one
        // the top block stopped reaching 0.94 lightness and stalled at 0.69,
        // dull beside a peak marker still drawn at full brightness. The
        // brightest block is the one you see at full scale, which is when the
        // display is looked at.
        static constexpr float NB_MAX = (float)(SND_HALF / 4);
        const int nb = blocks(curH), nbPrev = blocks(prevH);
        if (nb != nbPrev) {
            const int from = (nb > nbPrev ? nbPrev : nb);
            const int to   = (nb > nbPrev ? nb : nbPrev) - 1;
            for (int k = from; k <= to; k++) {
                const uint16_t c = (k < nb)
                    ? sndGrad(t, 0.5f + 0.5f * (float)k / NB_MAX) : TFT_BLACK;
                mSeg(x, SND_CY - k * 4 - 3, w, 3, c);     // above the axis
                mSeg(x, SND_CY + k * 4 + 1, w, 3, c);     // and below it
            }
        }
        const int np = blocks(curP), npPrev = blocks(prevP);
        if (np != npPrev) {
            // Erasing the old marker is not always a black block: on a
            // transient the stack grows past it, and the square it used to
            // hold now belongs to the stack. Black there punches a gap
            // straight through the column.
            if (npPrev > 0) {
                const uint16_t c =
                    (npPrev < nb) ? sndGrad(t, 0.5f + 0.5f * (float)npPrev / NB_MAX)
                                  : TFT_BLACK;
                mSeg(x, SND_CY - npPrev * 4 - 3, w, 3, c);
                mSeg(x, SND_CY + npPrev * 4 + 1, w, 3, c);
            }
            if (np > 0) {
                // Its colour rises with it: palette near the axis, white at the
                // edge of the zone (see sndPeakCol). A single fixed white was
                // tried first and out-shouted the stack it is there to
                // annotate; a single fixed dim one told the eye nothing the
                // position was not already telling it.
                const uint16_t c = sndPeakCol(t, (float)np / NB_MAX);
                mSeg(x, SND_CY - np * 4 - 3, w, 3, c);
                mSeg(x, SND_CY + np * 4 + 1, w, 3, c);
            }
        }
    }

    // The state the band shows when the microphone is not listening — the
    // speaker owns the shared I2S bus, or the boot warm-up is not over. Its own
    // `noinline` function so the per-frame body keeps one drawString site.
    __attribute__((noinline)) void sndAsleep() {
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(M5.Display.color565(0x39, 0x42, 0x4f), TFT_BLACK);
        M5.Display.setTextDatum(textdatum_t::middle_center);
        M5.Display.drawString(sce::T("mic idle", "micro au repos"),
                              units::SCREEN_W / 2, SND_CY);
        M5.Display.setTextDatum(textdatum_t::top_left);
    }

    void drawSound() {
        _lastDyn[0] = '\0';                       // (text mode will redraw)
        int st = _sndStyle.load();
        if (st < 0 || st > 2) st = 0;
        if ((uint8_t)st != _sndShown) { _sndFirst = true; _sndShown = (uint8_t)st; }
        if (!_sndBus || !_sndBus->hasEverPublished()) return;
        const SoundFrame& f = _sndBus->read();

        // NOT LISTENING. Said once, then left alone: a widget repainting an
        // unchanged nothing thirty times a second is pure cost.
        if (!f.live) {
            if (_sndLive || _sndFirst) {
                sndRest(); sndAsleep();
                _sndLive = false; _sndFirst = false;
                for (int i = 0; i < SoundFrame::WAVE_N; i++)
                    _sndPrevL[i] = _sndPrevR[i] = -128;
                for (int i = 0; i < SND_BARS; i++)
                    _sndPrevBar[i] = _sndPrevPeak[i] = 0;
            }
            return;
        }
        const bool firstPass = _sndFirst || !_sndLive;
        if (firstPass) {
            sndRest();
            for (int i = 0; i < SoundFrame::WAVE_N; i++)
                _sndPrevL[i] = _sndPrevR[i] = -128;
            for (int i = 0; i < SND_BARS; i++)
                _sndPrevBar[i] = _sndPrevPeak[i] = 0;
            _sndFirst = false;
        }
        _sndLive = true;

        _mNs = 0;
        if (st == SND_WAVE) {
            // TWO TRACES, one per microphone, in the two ends of the ramp so
            // they are told apart without a legend. The right channel is drawn
            // second: where they overlap, one of them has to win, and it may as
            // well be a stable choice rather than whichever moved last.
            //
            // A COLUMN IS REPAINTED WHOLE, OR NOT AT ALL. The per-channel
            // deltas each ignored the other's paint: R erasing its old stroke
            // blacked the pixels where it crossed a STATIC L trace, and L —
            // seeing its own span unchanged — never repainted them, so holes
            // accumulated in a steady tone's trace as long as the other mic
            // heard movement. Erasing both old strokes and redrawing both
            // whenever EITHER moved makes the crossing self-healing; a column
            // where neither moved was never erased, so it needs nothing.
            for (int i = 0; i < SoundFrame::WAVE_N; i++) {
                const int x = SND_W_X0 + i * SND_W_PITCH;
                const int n = (i + 1 < SoundFrame::WAVE_N) ? i + 1 : i;
                const int aL = f.waveL[i] * SND_AMP / 127;
                const int bL = f.waveL[n] * SND_AMP / 127;
                const int aR = f.waveR[i] * SND_AMP / 127;
                const int bR = f.waveR[n] * SND_AMP / 127;
                const int pL = _sndPrevL[i], qL = _sndPrevL[n];
                const int pR = _sndPrevR[i], qR = _sndPrevR[n];
                if (pL == aL && qL == bL && pR == aR && qR == bR) continue;
                const bool had = (pL > -128);   // rest wiped: nothing on glass
                auto span = [](int a, int b, int& lo, int& hi) {
                    lo = a < b ? a : b;
                    hi = a > b ? a : b;
                };
                // THE INK FOLLOWS THE AGITATION (user 08-10). A calm trace is
                // nearly black — present, barely — and whitens through the
                // launcher's ramp as the curve moves. A line of constant
                // brightness makes silence and speech look equally eventful;
                // this way the band is dark when the room is, and the eye is
                // only called when something happened.
                auto ink = [](int a, int b, float t) {
                    const int m = (a < 0 ? -a : a) > (b < 0 ? -b : b)
                                ? (a < 0 ? -a : a) : (b < 0 ? -b : b);
                    const float u = (float)m / (float)SND_AMP;   // 0..1
                    const float hot = (u - 0.55f) / 0.45f;       // white at the top
                    return sndGrad(t, 0.12f + 0.88f * u, hot);
                };
                int lo, hi;
                if (had) {
                    span(pL, qL, lo, hi);
                    mSeg(x, SND_CY - hi, SND_W_PITCH, hi - lo + 1, TFT_BLACK);
                    span(pR, qR, lo, hi);
                    mSeg(x, SND_CY - hi, SND_W_PITCH, hi - lo + 1, TFT_BLACK);
                }
                span(aL, bL, lo, hi);
                mSeg(x, SND_CY - hi, SND_W_PITCH, hi - lo + 1, ink(aL, bL, 0.0f));
                span(aR, bR, lo, hi);
                mSeg(x, SND_CY - hi, SND_W_PITCH, hi - lo + 1, ink(aR, bR, 1.0f));
            }
            for (int i = 0; i < SoundFrame::WAVE_N; i++) {
                _sndPrevL[i] = (int8_t)(f.waveL[i] * SND_AMP / 127);
                _sndPrevR[i] = (int8_t)(f.waveR[i] * SND_AMP / 127);
            }
        } else {
            // SKINS OF THE WAVE, not spectrums (user 08-09) — which is what
            // the three reference images actually showed: the same waveform
            // silhouette, dressed as a curve, as thin strokes, as blocks. The
            // width is TIME, the same fixed 256-sample window the wave skin
            // plots, so the three styles are three views of one picture and
            // switching them never changes what is being said.
            //
            // Each column takes the ENVELOPE of its five trace samples — the
            // louder of the two microphones, since a silhouette has one height
            // and the mix must not hide a channel. No mirror layout, so the
            // left-right symmetry that used to puzzle is gone: what you see is
            // the signal's own shape in time. The bands stay computed and
            // published (SoundFrame): nothing draws them today, they are the
            // §8 mouth's food tomorrow.
            //
            // No peak marker anywhere: a peak-hold on the DISPLAY column would
            // be smoothing born in the renderer, which A2.15 forbids — and the
            // reference silhouettes carry none.
            for (int i = 0; i < SND_BARS; i++) {
                // The envelope and its peak-hold are PUBLISHED (SoundViz):
                // deriving them here would put a memory in the painter, which
                // is what A2.15 forbids.
                const int h  = (int)f.env[i]     * SND_HALF / 255;
                const int ph = (int)f.envPeak[i] * SND_HALF / 255;
                const int x  = SND_B_X0 + i * SND_B_PITCH;
                const float t = (float)i / (float)(SND_BARS - 1);
                if (st == SND_MATRIX)
                    sndBlocks(x, 3, _sndPrevBar[i], _sndPrevPeak[i], h, ph, t);
                else
                    // MONOCHROME (user 08-10): one colour for every stroke.
                    // The across-the-width ramp made a row of thin strokes
                    // read as a gradient chart — a second thing said by the
                    // colour while the height was already saying the only
                    // thing this skin has to say.
                    sndBar(x, 3, _sndPrevBar[i], h, sndGrad(0.0f, 1.0f));
                _sndPrevBar[i]  = (uint8_t)h;
                _sndPrevPeak[i] = (uint8_t)ph;
            }
        }
        // ONE SPI transaction for the whole table: up to ~512 segments a
        // frame, and each unbracketed fillRect pays its own transaction and
        // address-window setup (~25-35 us) — 10-15 ms of pure overhead at
        // full music, the A2.22 class of budget failure. The eye push batches
        // for the same reason.
        M5.Display.startWrite();
        for (int k = 0; k < _mNs; k++)               // ONE call site (A2.22)
            M5.Display.fillRect(_mSeg[k].x, _mSeg[k].y, _mSeg[k].w, _mSeg[k].h,
                                _mSeg[k].c);
        M5.Display.endWrite();
    }

    // ONE ROW OF THE GAUGE BAND, drawn onto whatever surface is given. It
    // exists so the sprite path and the fallback path are the SAME code: two
    // copies of a drawing body is how A2.22 bites (GCC drops the second of two
    // similar calls) and how a fallback rots unnoticed.
    // `y` is the top of the row ON THE TARGET: 0 for the sprite, the screen
    // row for the direct fallback.
    void drawGaugeRow(LovyanGFX& g, int y, int i, const char* k, float pct,
                      const char* lbl, const char* rgt) {
        g.fillRect(0, y, units::SCREEN_W, 15, TFT_BLACK);
        if (pct < 0) return;                     // empty slot: cleared, no more
        if (pct > 100) pct = 100;
        g.setTextSize(1);
        g.setTextColor(M5.Display.color565(0x85, 0x93, 0xab), TFT_BLACK);
        g.setCursor(6, y + 3); g.print(lbl[0] ? lbl : k);
        // Base color INDEPENDENT of the eye color, distinct per gauge (before:
        // accent() -> the bars followed the eyes). Alert thresholds kept
        // (amber >= 70 %, red >= 85 %).
        static const uint16_t GBASE[3] = {
            M5.Display.color565(0x35, 0xb8, 0xc8),   // g0 cyan
            M5.Display.color565(0x8a, 0x8c, 0xf0),   // g1 indigo
            M5.Display.color565(0x4f, 0xc0, 0x8a),   // g2 green
        };
        uint16_t bar = pct >= 85 ? TFT_RED
                     : pct >= 70 ? M5.Display.color565(0xf2, 0xb5, 0x44) : GBASE[i];
        const int bx = 46, bw = 170;
        g.drawRect(bx, y + 1, bw, 11, M5.Display.color565(0x39, 0x42, 0x4f));
        g.fillRect(bx + 2, y + 3, (int)((bw - 4) * pct / 100.0f), 7, bar);
        char p[6]; snprintf(p, sizeof(p), "%d%%", (int)(pct + 0.5f));
        g.setTextColor(M5.Display.color565(0xcf, 0xe9, 0xef), TFT_BLACK);
        g.setCursor(bx + bw + 8, y + 3); g.print(p);
        if (rgt[0]) {
            g.setTextColor(M5.Display.color565(0x85, 0x93, 0xab), TFT_BLACK);
            g.setCursor(258, y + 3); g.print(rgt);
        }
    }

    // 3 gauges (g0..g2): left label, bar, %, right text (eta/label).
    //
    // THE FLICKER, AND WHY IT WAS THERE (user 08-03: "the status bar shimmers,
    // sometimes blinks"). Two defects, and the comment that used to sit here
    // claimed neither existed - it read "redraw throttled + only when a value
    // has changed", and there was NO change detection at all. A comment that
    // asserts a guard nobody wrote is worse than no comment: it stops the next
    // reader from looking.
    //   1. Every row was WIPED TO BLACK and redrawn straight onto the panel,
    //      five times a second. The eye zone never flickers because it renders
    //      into a canvas and pushes once; the band had no back buffer, so the
    //      black frame was on screen for real, every time.
    //   2. All three rows redrew whether or not anything had moved.
    // Now each row is rendered into a 320x15 sprite and pushed in one go - the
    // wipe happens off-screen - and a row whose value, label and right-hand
    // text are unchanged is not touched at all.
    //
    // The sprite is 9 600 bytes, allocated ONCE and reused by the three rows.
    // If it cannot be had, the direct path still works and the flicker comes
    // back: a degraded band beats a blank one, and it is the same drawing code
    // either way (drawGaugeRow).
    void drawGauges(uint32_t now) {
        _lastDyn[0] = '\0';
        if (now - _bandDrawnMs < 200) return;
        _bandDrawnMs = now;                      // (wrap OFF: set by drawStatusBand)
        if (!_gaugeCv.getBuffer()) {             // lazy, once
            _gaugeCv.setColorDepth(16);
            _gaugeCv.setPsram(true);             // never the internal heap
            _gaugeCv.createSprite(units::SCREEN_W, 15);
        }
        for (int i = 0; i < 3; i++) {
            char k[4] = { 'g', (char)('0' + i), 0 };
            char lk[6]; snprintf(lk, sizeof(lk), "g%dl", i);
            char rk[6]; snprintf(rk, sizeof(rk), "g%dr", i);
            float pct = _fields ? _fields->getF(k, -1.0f) : -1.0f;
            char lbl[8] = ""; char rgt[16] = "";
            if (_fields) { _fields->getS(lk, lbl, sizeof(lbl)); _fields->getS(rk, rgt, sizeof(rgt)); }
            // Change detection on WHAT IS DRAWN, not on the raw float — which
            // is only true if the DRAWING is quantised too (review 08-04):
            // the signature rounded to a percent while the bar width came
            // from the raw float (1.66 px per percent), so a value creeping
            // inside one percent was skipped with its bar up to ~1.6 px
            // stale. `pct` is therefore snapped to the signed-off percent
            // BEFORE drawing: now identical signature ⇒ identical pixels.
            uint32_t sig = 2166136261u;
            const int pq = (pct < 0) ? -1 : (int)(pct + 0.5f);
            if (pct >= 0) pct = (float)pq;
            sig = (sig ^ (uint32_t)(pq + 1)) * 16777619u;
            for (const char* q = lbl; *q; q++) sig = (sig ^ (uint8_t)*q) * 16777619u;
            for (const char* q = rgt; *q; q++) sig = (sig ^ (uint8_t)*q) * 16777619u;
            if (sig == _gaugeSig[i] && !_gaugeFirst) continue;
            _gaugeSig[i] = sig;
            const int y = BAND_TOP + 6 + i * 18;
            // ONE call site for the row (A2.22): the sprite path and the
            // direct fallback pick their SURFACE, not their call — two
            // similar draw calls in one body is the exact shape GCC 8.4
            // Xtensa has dropped before (X Dead, 07-25).
            const bool ps = _gaugeCv.getBuffer() != nullptr;
            drawGaugeRow(ps ? (LovyanGFX&)_gaugeCv : (LovyanGFX&)M5.Display,
                         ps ? 0 : y, i, k, pct, lbl, rgt);
            if (ps) _gaugeCv.pushSprite(&M5.Display, 0, y);
        }
        _gaugeFirst = false;
    }

    // Icon row (bottom): battery (left edge), wifi (right edge), + camera ●,
    // mic, night ☾ (contextual). Redrawn only when a state changes.
    void drawIcons(const FaceState& fs) {
        int batt = _fields ? (int)_fields->getF("batt", -1) : -1;
        bool chg   = _fields && _fields->getF("chg") >= 0.5f;
        int rssi   = _fields ? (int)_fields->getF("rssi", 0) : 0;
        bool cam   = _fields && _fields->getF("cam") >= 0.5f;
        bool night = _fields && _fields->getF("night") >= 0.5f;
        bool mic   = _fields && _fields->getF("mic") >= 0.5f;
        // Visibility mask (bits batt=1 wifi=2 cam=4 mic=8 night=16).
        const uint32_t mask = _iconMask.load();
        const bool showBatt = mask & 1, showWifi = mask & 2;
        cam = cam && (mask & 4); mic = mic && (mask & 8); night = night && (mask & 16);
        if (!showBatt) batt = -1;                // hidden → treated as absent
        // Debug info (emotion·ip): a SEPARATE option, CENTERED in the row.
        const bool dbg = _bandDebug.load();
        char dline[40] = "";
        if (dbg) {
            char ip[24] = ""; if (_fields) _fields->getS("ip", ip, sizeof(ip));
            if (ip[0]) snprintf(dline, sizeof(dline), "%s  %s", emotionName(fs.emotion), ip);
            else       snprintf(dline, sizeof(dline), "%s", emotionName(fs.emotion));
        }
        uint32_t dh = 2166136261u;               // FNV-1a of the debug text (change-detect)
        for (const char* p = dline; *p; p++) dh = (dh ^ (uint8_t)*p) * 16777619u;
        // compact signature for the change-detection (mask + debug included)
        uint32_t sig = ((uint32_t)((batt + 1) & 0x7F)) | (chg << 7)
                     | (((rssi < -90 ? 0 : rssi > -55 ? 3 : (rssi + 90) / 12) & 3) << 8)
                     | (cam << 10) | (night << 11) | (mic << 12)
                     | (showWifi << 13) | ((mask & 0x1F) << 14);
        // Two SEPARATE keys (icons + debug text hash): a single XOR could
        // cancel itself out and miss a legitimate redraw.
        if (sig == _lastIconSig && dh == _lastDbgHash) return;
        _lastIconSig = sig; _lastDbgHash = dh;
        M5.Display.fillRect(0, ICON_Y - 2, units::SCREEN_W, 14, TFT_BLACK);
        uint16_t mut = M5.Display.color565(0x85, 0x93, 0xab);
        uint16_t dim = M5.Display.color565(0x39, 0x42, 0x4f);
        int y = ICON_Y;
        // battery — LEFT edge
        if (batt >= 0) {
            bool low = batt <= 15 && !chg;
            // Color INDEPENDENT of the eyes: red when low, green when
            // charging (before: accent() while charging → followed the eyes).
            uint16_t bc = low ? TFT_RED
                        : chg ? M5.Display.color565(0x3a, 0xd0, 0x7a)   // charging green
                        : mut;
            M5.Display.drawRect(6, y, 20, 10, bc);
            M5.Display.fillRect(26, y + 3, 2, 4, bc);
            int w = (int)(16 * batt / 100.0f); if (w < 1) w = 1;
            M5.Display.fillRect(8, y + 2, w, 6, bc);
        }
        // wifi — RIGHT edge (4 bars)
        if (showWifi) {
            int bars = rssi == 0 ? 0 : rssi > -60 ? 4 : rssi > -70 ? 3 : rssi > -80 ? 2 : 1;
            for (int i = 0; i < 4; i++) {
                int hh = 3 + i * 2, xx = 300 + i * 4;
                M5.Display.fillRect(xx, y + 9 - hh, 3, hh, i < bars ? mut : dim);
            }
        }
        // contextual ones — from the right inwards, SPACED OUT (18 px step)
        // with a margin before the wifi bars (right edge at x=300).
        int x = 280;
        if (cam)   { M5.Display.fillCircle(x + 4, y + 5, 4, TFT_RED); x -= 18; }
        if (mic)   { M5.Display.fillRoundRect(x + 3, y, 5, 8, 2, mut); x -= 18; }
        if (night) { M5.Display.fillCircle(x + 5, y + 5, 4, mut);
                     M5.Display.fillCircle(x + 7, y + 3, 4, TFT_BLACK); x -= 18; }
        // Debug info (emotion·ip) CENTERED between the battery (left) and the
        // icons (right), TRUNCATED so it never spills over the icons (a long
        // ip can be pushed through /api/field).
        if (dbg && dline[0]) {
            const int ctxCount = (cam ? 1 : 0) + (mic ? 1 : 0) + (night ? 1 : 0);
            const int rlimit = 280 - 18 * ctxCount;   // left edge of the contextual icons
            const int lx = 36;                          // just after the battery
            const int maxW = rlimit - lx;
            // Cut index clamped to the buffer size: with no contextual icons
            // maxW/6 == 40 == sizeof(dline) → a write one past the end (today
            // unbreakable because strlen ≤ 39, but that is a sizing
            // coincidence, not an invariant).
            int cut = maxW / 6;
            if (cut > (int)sizeof(dline) - 1) cut = (int)sizeof(dline) - 1;
            if (cut > 0 && (int)strlen(dline) > cut) dline[cut] = '\0';
            int tw = (int)strlen(dline) * 6;
            int tx = (units::SCREEN_W - tw) / 2;
            if (tx + tw > rlimit) tx = rlimit - tw;
            if (tx < lx) tx = lx;
            M5.Display.setTextSize(1);
            M5.Display.setTextColor(mut, TFT_BLACK);
            M5.Display.setCursor(tx, y + 1);
            M5.Display.print(dline);
        }
    }
};

} // namespace sce
