#pragma once
// =============================================================================
// Camera.h — StackChan-Companion (hal)
// =============================================================================
// CoreS3 GC0308 camera (VGA) through the esp32-camera component (pre-built
// into the Arduino-ESP32 core, libesp32-camera.a). Exposed as JPEG for
// Home Assistant / Frigate (WebApi: /api/camera/*).
//
// TWO hardware obstacles solved (diagnosis 2026-07-17/18):
//
// 1. SCCB BUS SHARED WITH THE IMU (pins 11/12). M5Unified drives that bus
//    with its OWN register implementation (`m5gfx::i2c`), NOT the ESP-IDF
//    I2C driver esp32-camera expects for SCCB → timeout (263).
//    FIX: `firmware/companion/sccb_m5.cpp` REDEFINES esp32-camera's SCCB_*
//    functions so the control bus is routed through `M5.In_I2C`
//    (linker: `-Wl,--allow-multiple-definition`). Camera and IMU then share
//    the SAME i2c → serialized, conflict-free, VOR alive during streaming.
//
// 2. NO HARDWARE JPEG. The GC0308 only outputs RGB565/YUV — asking for
//    PIXFORMAT_JPEG returns ESP_ERR_NOT_SUPPORTED (262). FIX: RGB565 capture
//    + software JPEG encoding (`frame2jpg`) at serving time.
//
// `M5.begin()` has already powered the camera rail (ALDO3 = 3.3 V) and set up
// the AW9523 (the board_M5StackChan profile shares M5GFX's CoreS3 bring-up).
//
// ARCHITECTURE (rule A2.6: nothing heavy inside AsyncTCP callbacks):
//   - init (~200 ms) AND capture+encode (~150-300 ms VGA) run inside
//     loop() through service(), NEVER in an AsyncTCP handler (which would
//     freeze the network task). service() captures into a shared JPEG buffer
//     as long as a consumer is active (recent request), at a capped rate.
//   - Handlers (/api/camera/*) only call markWanted() and read the latest
//     JPEG through latestJpeg() (copy under mutex) — never capture directly.
//
// ANTI-BRICK (A2.20): OFF by default (tuning `camera`), init DEFERRED until
// requested (never at boot), failure latched (`_failed`, no retry loop),
// de-init after inactivity (frees PSRAM + leaves the bus idle).
//
// CoreS3 GC0308 pins (DVP) — no conflict with SD (35/36/37/4), servos
// (6/7), I2S mic/speaker (33/34); XCLK=2 = external Port A (Grove), UNUSED.
// =============================================================================

#include <Arduino.h>
#include <M5Unified.h>
#include <atomic>
#include "esp_camera.h"
#include "img_converters.h"   // frame2jpg (GC0308 = no hardware JPEG)
#include "../engine/Tuning.h"
#include "I2cBus.h"           // shared 11/12 bus lock (IMU/touch/SCCB)

namespace sce {

class Camera {
public:
    // Live camera settings (Tuning: cam_fps/quality/brightness...) — wired by
    // main at boot. Applied at init and on the fly when changed (service).
    void attachTuning(const Tuning* t) { _tuning = t; }

    // Renderer pause/resume around the DMA fill of a frame (wired by main:
    // renderer.pause/resume). Without it the display SPI-DMA (30 fps) and the
    // camera GDMA arbitrate badly → camera FIFO overruns mid-frame (real image
    // on top, flat grey below). ~40 ms of frozen display per capture.
    void attachRendererPause(void (*cb)(bool)) { _pauseCb = cb; }

    // DEDICATED camera task — core 0 prio 1 (2026-07-20). JPEG encoding is
    // SOFTWARE (GC0308 has no hardware JPEG) and costs ~150-300 ms per VGA
    // frame: left inside loop() (core 1 prio 1) it saturated the loop as long
    // as a client was watching → stream capped at ~3 fps AND HTTP requests
    // (including POST /api/dance) queued behind the encoding. On its own task
    // at core 0 prio 1: ServoMotion (core 0 prio 3) and async_tcp PREEMPT it
    // (smooth dances, responsive HTTP), and the encoding runs IN PARALLEL with
    // rendering (core 1). The renderer pause around fb_get stays safe
    // (Renderer::pause refcount). loop() no longer calls service().
    void start(uint32_t stack = 8192, UBaseType_t prio = 1, BaseType_t core = 0) {
        xTaskCreatePinnedToCore(taskEntry, "camera", stack, this, prio, &_task, core);
    }

    // Remaining stack headroom (words) — heartbeat/endurance
    uint32_t stackFreeWords() const {
        return _task ? (uint32_t)uxTaskGetStackHighWaterMark(_task) : 0;
    }

    // INHIBITS capture (safety during a flash: firmware OTA, upload/launch of
    // a .bin). The task skips service() BETWEEN two captures (never in the
    // middle of an fb_get → the renderer is never left paused), so no camera
    // GDMA vs flash-SD write contention and no concurrent PSRAM pressure.
    // Cooperative (no vTaskSuspend, which would freeze an fb_get halfway).
    void setInhibited(bool on) { _inhibited.store(on); }
    bool isInhibited() const { return _inhibited.load(); }

    // RENDEZ-VOUS after setInhibited(true): waits (bounded) for an ALREADY
    // started capture to finish — the inhibit flag is only tested at the top
    // of service(), so an in-flight capture (fb_get GDMA + encode, ~350 ms
    // max) used to overlap the first OTA/bins flash writes (review 07-21).
    // Call BEFORE Update.begin()/updateFromFS; free when no capture is in
    // flight.
    bool waitCaptureIdle(uint32_t maxMs = 500) {
        uint32_t t0 = millis();
        while (_capturing.load()) {
            if (millis() - t0 > maxMs) return false;
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        return true;
    }

    // Signals that a client wants images (AsyncTCP handler — does NOT block):
    // arms the init if needed and keeps capture alive. The actual capture is
    // done by service() in the camera task.
    void markWanted() { _initReq = true; _lastUseMs = millis(); }

    // Copies the latest captured JPEG (from service/loop) — callable from an
    // AsyncTCP callback (never blocks on the camera). Returns a buffer the
    // caller MUST FREE, or false if no image is ready.
    bool latestJpeg(uint8_t** out, size_t* outLen) {
        if (!_mtx) return false;
        bool ok = false;
        if (xSemaphoreTake(_mtx, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (_jpg && _jpgLen) {
                uint8_t* cp = (uint8_t*)heap_caps_malloc(_jpgLen, MALLOC_CAP_SPIRAM);
                if (cp) { memcpy(cp, _jpg, _jpgLen); *out = cp; *outLen = _jpgLen; ok = true; }
                // Marks the frame as consumed (capture pacing — service()
                // only encodes the next one after this). Monotonic
                // cross-task hint: a slightly stale value costs at worst one
                // capture more/less, never a crash.
                _lastServedVer = _frameVer;
            }
            xSemaphoreGive(_mtx);
        }
        return ok;
    }

    // Version number of the latest frame (incremented on every capture) — the
    // MJPEG stream uses it to serve each image only ONCE (pacing).
    uint32_t frameVersion() const { return _frameVer; }

    // Is an image already available? (the MJPEG stream only STARTS if yes —
    // a filler that answers "try again" before the 1st frame drains the
    // AsyncTCP send credit and kills the connection silently)
    bool hasFrame() const { return _jpg && _jpgLen; }

    // Camera disabled by the option (Tuning.camera)? The MJPEG stream checks
    // this to CLOSE ITSELF (return 0) when camera=0 — otherwise the connection
    // would stay open answering "try again" forever (a lingering tab/Frigate)
    // AND its markWanted() would re-arm the init in a loop against the user's
    // choice.
    bool disabled() const { return !_tuning || _tuning->camera < 0.5f; }

    // ---- FULL QUALITY on-demand still (/api/camera/still.jpg?full=1)
    // The stream/preview run at LIVE quality (compressed); the still wants
    // full quality (cam_quality). requestSnapshot() arms a single FULL capture
    // performed by the task (never in the AsyncTCP callback); the handler
    // serves the shot if snapAgeMs() <= 1.5 s, else 503 (client retries). ----
    // Arms a full capture WITHOUT invalidating the existing one. (The 1st
    // design invalidated on every call: a client RETRYING the same endpoint
    // destroyed the fresh snapshot before reading it → endless 503, 07-20.)
    void requestSnapshot() {
        if (!_snapReq.load()) _snapReq.store(true);   // no burst of captures
        markWanted();
    }
    // Age of the latest snapshot (ms) — UINT32_MAX if there is none.
    uint32_t snapAgeMs() const {
        uint32_t t = _snapAtMs.load();
        return t ? (uint32_t)(millis() - t) : UINT32_MAX;
    }
    bool latestSnapshot(uint8_t** out, size_t* outLen) {
        if (!_mtx) return false;
        bool ok = false;
        if (xSemaphoreTake(_mtx, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (_snap && _snapLen) {
                uint8_t* cp = (uint8_t*)heap_caps_malloc(_snapLen, MALLOC_CAP_SPIRAM);
                if (cp) { memcpy(cp, _snap, _snapLen); *out = cp; *outLen = _snapLen; ok = true; }
            }
            xSemaphoreGive(_mtx);
        }
        return ok;
    }

    // Called from the camera task: creates the mutex (1st time), performs the
    // deferred init, captures+encodes a JPEG while a consumer is active
    // (capped rate), de-inits after inactivity.
    void service() {
        if (!_mtx) _mtx = xSemaphoreCreateMutex();
        // Inhibited (OTA flash / .bin in progress): do nothing — we are
        // BETWEEN two captures (renderer already resumed), capture will
        // restart when the inhibit is lifted.
        if (_inhibited.load()) return;
        uint32_t now = millis();

        // Camera disabled (Tuning): stop ALL capture and de-init, even if an
        // MJPEG stream keeps re-arming _lastUseMs — otherwise the stream would
        // keep running capture + renderer pause until the client disconnects.
        if (_tuning && _tuning->camera < 0.5f) {
            _initReq = false;
            // camera=0 = EXPLICIT user gesture: also un-latch _failed —
            // otherwise a single failed init (transient SCCB glitch) killed
            // the camera until reboot, and the user's 0→1 retry did NOTHING
            // (review 07-21). This is not an anti-brick retry loop: retrying
            // only happens on that deliberate gesture.
            _failed = false;
            if (_ready) deinit();
            return;
        }

        if (_initReq && !_ready && !_failed) {
            _initReq = false;
            // init() holds the bus lock EXCLUSIVELY over the whole SCCB burst
            // (~300 registers): the Brain CANNOT interleave its IMU reads →
            // VOR frozen ~1 s when the camera powers up. DELIBERATE tradeoff
            // (see init()): an exclusive burst is the only way not to corrupt
            // the sensor config (regression of the per-transaction lock,
            // 07-19). Bounded, once per camera=0→1.
            init();
        }

        if (_ready) applySensorIfChanged();   // live settings (Tuning)

        // Full-quality still requested (?full=1): a SINGLE FULL capture,
        // independent of the stream pacing. After publishing, PURGE the
        // re-requests that arrived DURING the capture (the client's 300 ms
        // retries re-armed _snapReq → a 2nd full capture nobody consumed,
        // with a free renderer pause — review 07-21): they are satisfied by
        // the shot just published.
        if (_ready && _snapReq.exchange(false)) {
            captureSnapshot();
            _snapReq.store(false);
        }

        bool wanted = (int32_t)(now - _lastUseMs) < (int32_t)CONSUMER_MS;
        // Never encode faster than the consumption: the NEXT frame is only
        // captured once the previous one has been served at least once. A lone
        // still.jpg request = ONE capture (instead of ~10/s of fb_get +
        // frame2jpg wasted in loop(), ~40 ms of renderer pause each).
        // The MJPEG stream consumes every frame → capture at cam_fps as usual.
        bool consumed = (_frameVer == 0) || (_lastServedVer == _frameVer);
        if (_ready && wanted && consumed &&
            (int32_t)(now - _lastCapMs) >= (int32_t)capIntervalMs()) {
            _lastCapMs = now;
            captureStep();
        }

        // AUTO-OFF after IDLE_MS without a consumer (re-enabled 07-20 with a
        // LONG threshold): frees ~40 KB of internal heap (driver DMA) + the
        // sensor rail when camera=1 lingers with nobody watching.
        // The old 15 s auto-deinit had been removed because re-init was
        // unstable — since then the ALDO3 power-cycle + frame-gate reset make
        // the deinit→init cycle RELIABLE (validated on HW). 60 s >> the HA
        // polls (10 s) → no parasitic cycling; the next access re-inits
        // (~1.5 s, the "camera starting..." UX already retries).
        if (_ready && (int32_t)(now - _lastUseMs) > (int32_t)IDLE_MS) deinit();
    }

    bool isReady()   const { return _ready; }
    bool hasFailed() const { return _failed; }

    // Exposed state (/api/status, console): "failed" (init KO, latched),
    // "ready" (powered), "idle" (never inited / de-inited after inactivity).
    const char* stateName() const {
        if (_failed) return "failed";
        if (_ready)  return "ready";
        return "idle";
    }
    int lastErr() const { return (int)_lastErr; }

private:
    static constexpr uint32_t IDLE_MS     = 60000; // auto-off with no consumer
                                                   // (>> HA polls, 10 s)
    static constexpr uint32_t CONSUMER_MS = 3000;  // capture if request < 3 s

    // Dedicated task loop: service() (deferred init / de-init / self-paced
    // capture) + a short yield. The internal gate in service() caps capture at
    // capIntervalMs; the yield hands the CPU back to tasks of prio >= 1.
    static void taskEntry(void* p) { static_cast<Camera*>(p)->taskLoop(); }
    void taskLoop() {
        for (;;) { service(); vTaskDelay(pdMS_TO_TICKS(5)); }
    }
    TaskHandle_t _task = nullptr;
    std::atomic<bool> _inhibited{false};   // flash/upload in progress (safety)
    std::atomic<bool> _capturing{false};   // capture in flight (waitCaptureIdle)

    const Tuning* _tuning = nullptr;
    void (*_pauseCb)(bool) = nullptr;   // renderer pause/resume (main)
    bool _ready   = false;
    bool _failed  = false;
    bool _initReq = false;
    int  _lastErr = 0;
    uint32_t _lastUseMs = 0;
    uint32_t _lastCapMs = 0;
    uint32_t _lastServedVer = 0;   // last frameVersion served (pacing)

    // Defaults when no Tuning is attached (brightness pushed up).
    // cam_quality follows the OV convention (1..63, LOW = better image) but
    // the software encoder frame2jpg (jpge) expects 1..100 where HIGH = better:
    // so we INVERT (otherwise default 12 = near-worst image, slider backwards).
    uint8_t quality() const {   // full-quality STILL (?full=1)
        float q = _tuning ? clampf(_tuning->cam_quality, 1, 63) : 12;
        return (uint8_t)(100.0f - q);   // 1 -> 99 (sharp), 63 -> 37 (compressed)
    }
    uint8_t liveQuality() const {   // STREAM/preview: more compressed (light/fast)
        float q = _tuning ? clampf(_tuning->cam_stream_quality, 1, 63) : 45;
        return (uint8_t)(100.0f - q);   // 45 -> 55 (~4x lighter than 88)
    }
    bool streamQvga() const {       // stream/preview downsampled to 320x240
        return !_tuning || _tuning->cam_stream_qvga >= 0.5f;
    }
public:
    // RE-SERVE interval of the MJPEG stream when no fresh frame is available:
    // a CONSTANT 300 ms — this is a KEEP-ALIVE requirement of the AsyncTCP
    // credit machinery, not a frame-rate setting. Verified in the library
    // (WebResponses.cpp): a LIFETIME budget of 2 filler calls on poll
    // (++credit on ack, --credit unconditionally, polls every 500 ms) — acks
    // are neutral, only SENDS spaced <=~300 ms keep the chain alive. The
    // max(cam_fps) attempt of 07-20 killed the stream in ~2-3 s at
    // cam_fps <= 3 (poll-only window > budget). Re-serving 2-4 KB 3x/s is
    // negligible next to a dead stream.
    uint32_t reServeMs() const { return 300; }
private:
    uint32_t capIntervalMs() const {
        float fps = _tuning ? clampf(_tuning->cam_fps, 1, 15) : 10;
        return (uint32_t)(1000.0f / fps);
    }
    static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    // SCCB access (GC0308 registers @0x21): EVERY transaction under the bus
    // lock (shared with IMU/touch/AXP) — never as a block, never across a
    // delay() — so the Brain can interleave its IMU reads (VOR stays alive).
    static void    wr(uint8_t reg, uint8_t val) {
        sce::i2cbus::Guard g; M5.In_I2C.writeRegister8(0x21, reg, val, 100000);
    }
    static uint8_t rd(uint8_t reg) {
        sce::i2cbus::Guard g; return M5.In_I2C.readRegister8(0x21, reg, 100000);
    }
    static void    aldo3(int mv) {   // sensor rail (AXP2101, same bus)
        if (M5.Power.getType() != m5::Power_Class::pmic_axp2101) return;
        sce::i2cbus::Guard g; M5.Power.Axp2101.setALDO3(mv);
    }

    // Cache of the sensor settings already applied (hot-apply on change).
    // _apCbar starts at 0 (not 1e9): NEVER write set_colorbar(0) on the 1st
    // apply — only on a real 0↔1 toggle.
    float _apBright = 1e9f, _apContr = 1e9f, _apSat = 1e9f,
          _apVflip = 1e9f, _apHmir = 1e9f, _apCbar = 0.0f,
          _apLowlight = 1e9f;

    SemaphoreHandle_t _mtx = nullptr;
    uint8_t* _jpg = nullptr;   // latest live JPEG (PSRAM), protected by _mtx
    size_t   _jpgLen = 0;
    volatile uint32_t _frameVer = 0;   // incremented on every published capture
    // On-demand full-quality still (?full=1) — dedicated PSRAM buffer
    std::atomic<bool>     _snapReq{false};
    uint8_t*              _snap = nullptr;
    size_t                _snapLen = 0;
    std::atomic<uint32_t> _snapAtMs{0};   // millis() of the shot (0 = never)
    uint8_t*              _qvga = nullptr;   // 320x240 downscale buffer (PSRAM,
                                             // reused between captures)

    // _capturing flag held true for the duration of a capture (RAII: covers the
    // early returns) — waitCaptureIdle synchronizes on it before a flash.
    struct CapGuard {
        std::atomic<bool>& f;
        explicit CapGuard(std::atomic<bool>& b) : f(b) { f.store(true); }
        ~CapGuard() { f.store(false); }
    };

    // Incremental PSRAM writer for fmt2jpg_cb: the JPEG is streamed DIRECTLY
    // into PSRAM. The old frame2jpg path allocated its output buffer on the
    // INTERNAL heap (~10 Hz spikes while streaming that pushed heapMin down
    // against the AsyncTCP buffers) then memcpy'd to PSRAM + free (review
    // 07-21) — NO internal allocation is left in the image pipeline.
    struct JpgPsramWriter {
        uint8_t* buf = nullptr; size_t cap = 0; size_t len = 0;
        static size_t cb(void* arg, size_t index, const void* data, size_t n) {
            JpgPsramWriter* w = (JpgPsramWriter*)arg;
            size_t need = index + n;
            if (need > w->cap) {
                size_t ncap = w->cap ? w->cap * 2 : 16384;
                while (ncap < need) ncap *= 2;
                uint8_t* nb = (uint8_t*)heap_caps_realloc(w->buf, ncap,
                                                          MALLOC_CAP_SPIRAM);
                if (!nb) return 0;              // PSRAM full → encode fails
                w->buf = nb; w->cap = ncap;
            }
            if (n) memcpy(w->buf + index, data, n);
            if (need > w->len) w->len = need;
            return n;
        }
    };

    // Encodes a YUV422 buffer → JPEG streamed into PSRAM (*out must be freed
    // with heap_caps_free). SINGLE path for the three encodings (live QVGA,
    // live VGA, full snapshot) — no more duplicated pipelines.
    static bool encodeToPsram(const uint8_t* src, size_t srcLen, int w, int h,
                              uint8_t q, uint8_t** out, size_t* outLen) {
        JpgPsramWriter wr;
        bool ok = fmt2jpg_cb((uint8_t*)src, srcLen, (uint16_t)w, (uint16_t)h,
                             PIXFORMAT_YUV422, q, JpgPsramWriter::cb, &wr) &&
                  wr.len > 0;
        if (!ok) { if (wr.buf) heap_caps_free(wr.buf); return false; }
        *out = wr.buf; *outLen = wr.len;
        return true;
    }

    // Publishes a JPEG (PSRAM) into (dst,dstLen) under _mtx — frees on failure.
    // true = published (the caller then bumps its version counter).
    bool publishJpeg(uint8_t* buf, size_t len, uint8_t*& dst, size_t& dstLen) {
        if (xSemaphoreTake(_mtx, pdMS_TO_TICKS(50)) != pdTRUE) {
            heap_caps_free(buf);   // could not publish — no leak
            return false;
        }
        if (dst) heap_caps_free(dst);
        dst = buf; dstLen = len;
        return true;               // the caller finishes THEN gives _mtx back
    }

    // Captures a frame + encodes JPEG, publishes into _jpg (camera task).
    // The renderer is suspended ONLY during the DMA fill (fb_get, ~40 ms) —
    // JPEG encoding reads a complete buffer, so it does not need the pause.
    void captureStep() {
        CapGuard cg(_capturing);
        if (_pauseCb) _pauseCb(true);
        camera_fb_t* fb = esp_camera_fb_get();
        if (_pauseCb) _pauseCb(false);
        if (!fb) return;
        uint8_t* jbuf = nullptr; size_t jlen = 0;
        // Stream/preview: optional QVGA downsampling (~4x fewer bytes over the
        // air + ~4x less jpge CPU) then compressed encoding.
        bool ok = streamQvga()
            ? qvgaToJpeg(fb, &jbuf, &jlen)
            : encodeToPsram(fb->buf, fb->len, fb->width, fb->height,
                            liveQuality(), &jbuf, &jlen);
        esp_camera_fb_return(fb);
        if (!ok) return;
        if (publishJpeg(jbuf, jlen, _jpg, _jpgLen)) {
            _frameVer++;
            xSemaphoreGive(_mtx);
        }
    }

    // Downsamples the YUV422 frame (packed YUYV) VGA → QVGA (every other pixel,
    // every other line) into a reused PSRAM buffer, then encodes (streamed to
    // PSRAM). Each destination pair (4 bytes Y U Y V) samples pixels 4k and
    // 4k+2 of the source line (chroma from the 1st pair — a half-pixel shift,
    // invisible at this size). Falls back to VGA if PSRAM is unavailable.
    bool qvgaToJpeg(camera_fb_t* fb, uint8_t** out, size_t* outLen) {
        const int sw = fb->width, sh = fb->height;   // 640x480 (VGA)
        const int dw = sw / 2,    dh = sh / 2;       // 320x240
        if (!_qvga) _qvga = (uint8_t*)heap_caps_malloc((size_t)dw * dh * 2,
                                                       MALLOC_CAP_SPIRAM);
        if (!_qvga) return encodeToPsram(fb->buf, fb->len, sw, sh,
                                         liveQuality(), out, outLen);
        uint8_t* d = _qvga;
        for (int y = 0; y < dh; y++) {
            const uint8_t* row = fb->buf + (size_t)(y * 2) * sw * 2;
            for (int k = 0; k < dw / 2; k++) {       // k = dest pair (2 px)
                const uint8_t* p = row + (size_t)k * 8;  // 2 source pairs
                d[0] = p[0];   // Y (px 4k)
                d[1] = p[1];   // U
                d[2] = p[4];   // Y (px 4k+2)
                d[3] = p[3];   // V
                d += 4;
            }
        }
        return encodeToPsram(_qvga, (size_t)dw * dh * 2, dw, dh,
                             liveQuality(), out, outLen);
    }

    // SINGLE full-quality capture (?full=1) → _snap. Same pipeline as
    // captureStep but quality() (full) and a dedicated buffer: the handler
    // judges freshness through snapAgeMs() and re-serves a shot younger than
    // 1.5 s. Camera task.
    void captureSnapshot() {
        CapGuard cg(_capturing);
        if (_pauseCb) _pauseCb(true);
        camera_fb_t* fb = esp_camera_fb_get();
        if (_pauseCb) _pauseCb(false);
        if (!fb) return;
        uint8_t* ps = nullptr; size_t jlen = 0;
        bool ok = encodeToPsram(fb->buf, fb->len, fb->width, fb->height,
                                quality(), &ps, &jlen);   // FULL quality
        esp_camera_fb_return(fb);
        if (!ok) return;
        if (publishJpeg(ps, jlen, _snap, _snapLen)) {
            _snapAtMs.store(millis() ? millis() : 1);   // 0 means "never"
            xSemaphoreGive(_mtx);
        }
    }

    // OFFICIAL GC0308 init table (espressif/esp-video-components,
    // esp_cam_sensor gc0308: 8-bit DVP, 20 MHz crystal, 640x480 YUV422
    // YUYV 16 fps — Apache-2.0). The esp32-camera driver pre-built into the
    // Arduino core writes a defective table (broken AWB/chroma -> grey veil
    // + pink window); we REWRITE the whole config on top of it through our
    // own SCCB (M5.In_I2C). Starts with a SOFT RESET (0xfe,0x80), ends by
    // enabling the output (0x25,0x0f).
    static const uint8_t* initTable(size_t& n) {
        static const uint8_t T[] = {
    0xfe,0x80,0xfe,0x00,0x25,0x00,0xd2,0x10,0x22,0x55,0x03,0x01,0x04,0x2c,0x5a,0x56,
    0x5b,0x40,0x5c,0x4a,0x22,0x57,0x01,0x6a,0x02,0x37,0x0f,0x10,0xe2,0x00,0xe3,0x7d,
    0xe4,0x02,0xe5,0x71,0xe6,0x02,0xe7,0x71,0xe8,0x02,0xe9,0x71,0xea,0x02,0xeb,0x71,
    0xec,0x00,0x05,0x00,0x06,0x00,0x07,0x00,0x08,0x00,0x09,0x01,0x0a,0xe8,0x0b,0x02,
    0x0c,0x88,0x46,0x80,0x47,0x00,0x48,0x00,0x49,0x01,0x4a,0xe0,0x4b,0x02,0x4c,0x80,
    0x0d,0x02,0x0e,0x02,0x10,0x22,0x11,0xfd,0x12,0x2a,0x13,0x00,0x15,0x0a,0x16,0x05,
    0x17,0x01,0x18,0x44,0x19,0x44,0x1a,0x1e,0x1b,0x00,0x1c,0xc1,0x1d,0x08,0x1e,0x60,
    0x1f,0x16,0x20,0xff,0x21,0xf8,0x22,0x57,0x26,0x03,0x2f,0x01,0x30,0xf7,0x31,0x50,
    0x32,0x00,0x39,0x04,0x3a,0x18,0x3b,0x20,0x3c,0x00,0x3d,0x00,0x3e,0x00,0x3f,0x00,
    0x50,0x10,0x53,0x82,0x54,0x80,0x55,0x80,0x56,0x82,0x8b,0x40,0x8c,0x40,0x8d,0x40,
    0x8e,0x2e,0x8f,0x2e,0x90,0x2e,0x91,0x3c,0x92,0x50,0x5d,0x12,0x5e,0x1a,0x5f,0x24,
    0x60,0x07,0x61,0x15,0x62,0x08,0x64,0x03,0x66,0xe8,0x67,0x86,0x68,0xa2,0x69,0x18,
    0x6a,0x0f,0x6b,0x00,0x6c,0x5f,0x6d,0x8f,0x6e,0x55,0x6f,0x38,0x70,0x15,0x71,0x33,
    0x72,0xdc,0x73,0x80,0x74,0x02,0x75,0x3f,0x76,0x02,0x77,0x36,0x78,0x88,0x79,0x81,
    0x7a,0x81,0x7b,0x22,0x7c,0xff,0x93,0x48,0x94,0x00,0x95,0x05,0x96,0xe8,0x97,0x40,
    0x98,0xf0,0xb1,0x38,0xb2,0x38,0xbd,0x38,0xbe,0x36,0xd0,0xc9,0xd1,0x10,0xd3,0x80,
    0xd5,0xf2,0xd6,0x16,0xdb,0x92,0xdc,0xa5,0xdf,0x23,0xd9,0x00,0xda,0x00,0xe0,0x09,
    0xed,0x04,0xee,0xa0,0xef,0x40,0x80,0x03,0x80,0x03,0x9f,0x10,0xa0,0x20,0xa1,0x38,
    0xa2,0x4e,0xa3,0x63,0xa4,0x76,0xa5,0x87,0xa6,0xa2,0xa7,0xb8,0xa8,0xca,0xa9,0xd8,
    0xaa,0xe3,0xab,0xeb,0xac,0xf0,0xad,0xf8,0xae,0xfd,0xaf,0xff,0xc0,0x00,0xc1,0x10,
    0xc2,0x1c,0xc3,0x30,0xc4,0x43,0xc5,0x54,0xc6,0x65,0xc7,0x75,0xc8,0x93,0xc9,0xb0,
    0xca,0xcb,0xcb,0xe6,0xcc,0xff,0xf0,0x02,0xf1,0x01,0xf2,0x01,0xf3,0x30,0xf9,0x9f,
    0xfa,0x78,0xfe,0x01,0x00,0xf5,0x02,0x1a,0x0a,0xa0,0x0b,0x60,0x0c,0x08,0x0e,0x4c,
    0x0f,0x39,0x11,0x3f,0x12,0x72,0x13,0x13,0x14,0x42,0x15,0x43,0x16,0xc2,0x17,0xa8,
    0x18,0x18,0x19,0x40,0x1a,0xd0,0x1b,0xf5,0x70,0x40,0x71,0x58,0x72,0x30,0x73,0x48,
    0x74,0x20,0x75,0x60,0x77,0x20,0x78,0x32,0x30,0x03,0x31,0x40,0x32,0xe0,0x33,0xe0,
    0x34,0xe0,0x35,0xb0,0x36,0xc0,0x37,0xc0,0x38,0x04,0x39,0x09,0x3a,0x12,0x3b,0x1c,
    0x3c,0x28,0x3d,0x31,0x3e,0x44,0x3f,0x57,0x40,0x6c,0x41,0x81,0x42,0x94,0x43,0xa7,
    0x44,0xb8,0x45,0xd6,0x46,0xee,0x47,0x0d,0xfe,0x00,0xd2,0x90,0xfe,0x00,0x10,0x26,
    0x11,0x0d,0x1a,0x2a,0x1c,0x49,0x1d,0x9a,0x1e,0x61,0x3a,0x20,0x50,0x14,0x53,0x80,
    0x56,0x80,0x8b,0x20,0x8c,0x20,0x8d,0x20,0x8e,0x14,0x8f,0x10,0x90,0x14,0x94,0x02,
    0x95,0x07,0x96,0xe0,0xb1,0x40,0xb2,0x40,0xb3,0x40,0xb6,0xe0,0xd0,0xcb,0xd3,0x48,
    0xf2,0x02,0xf7,0x12,0xf8,0x0a,0xfe,0x01,0x02,0x20,0x04,0x10,0x05,0x08,0x06,0x20,
    0x08,0x0a,0x0e,0x44,0x0f,0x32,0x10,0x41,0x11,0x37,0x12,0x22,0x13,0x19,0x14,0x44,
    0x15,0x44,0x19,0x50,0x1a,0xd8,0x32,0x10,0x35,0x00,0x36,0x80,0x37,0x00,0xfe,0x00,
    0x9f,0x0e,0xa0,0x1c,0xa1,0x34,0xa2,0x48,0xa3,0x5a,0xa4,0x6b,0xa5,0x7b,0xa6,0x95,
    0xa7,0xab,0xa8,0xbf,0xa9,0xce,0xaa,0xd9,0xab,0xe4,0xac,0xec,0xad,0xf7,0xae,0xfd,
    0xaf,0xff,0x14,0x10,0x24,0xa2,
    0x25,0x0f,
        };
        n = sizeof(T) / 2;
        return T;
    }

    bool init() {
        // Sensor POWER-CYCLE (mandatory): the CoreS3 GC0308 has NEITHER a
        // RESET nor a PWDN pin wired — its register state PERSISTS across
        // reboots and reflashes as long as the ALDO3 rail stays powered.
        // Cutting/restoring ALDO3 (the camera's dedicated rail, cf. M5GFX) is
        // the only real reset. Done OUTSIDE the lock (aldo3 locks per
        // transaction): these ~650 ms of delays carry NO SCCB traffic → the
        // Brain interleaves its IMU reads and the VOR stays alive during the
        // power-cycle (07-20 — the exclusive freeze now only covers the SCCB
        // burst, ~0.5 s).
        aldo3(0);                             // cut the rail (internal lock)
        delay(350);                           // long OFF: caps fully discharge
                                              // → RELIABLE reset of a stuck
                                              // sensor (150 ms was not always
                                              // enough, missing frames 07-19)
        aldo3(3300);                          // power back up
        delay(300);                           // sensor startup before SCCB

        // EXCLUSIVE LOCK over the whole SCCB CONFIG (recursive mutex → wr/rd
        // re-take it): esp_camera_init (driver probe/config) + rewriting the
        // table (~300 registers). CRUCIAL: if the Brain slips an IMU read
        // BETWEEN two SCCB writes, the sensor config is corrupted and fb_get
        // delivers NO frame at all (still.jpg 503, 2026-07-19 regression of the
        // per-transaction lock). The lock is held across the small delay()s of
        // the burst (bounded ~0.5 s, once per power-up) — a deliberate
        // exception to rule 15.
        sce::i2cbus::Guard initGuard;

        camera_config_t c = {};
        c.pin_pwdn     = -1;
        c.pin_reset    = -1;
        c.pin_xclk     = 2;
        // SCCB routed through M5.In_I2C by firmware/companion/sccb_m5.cpp
        // (overrides esp32-camera's SCCB_* symbols). The pin/port fields below
        // are therefore no longer decisive, kept only for reuse.
        c.pin_sccb_sda = -1;
        c.pin_sccb_scl = -1;
        c.sccb_i2c_port = (int)M5.In_I2C.getPort();
        c.pin_d7 = 47; c.pin_d6 = 48; c.pin_d5 = 16; c.pin_d4 = 15;
        c.pin_d3 = 42; c.pin_d2 = 41; c.pin_d1 = 40; c.pin_d0 = 39;
        c.pin_vsync = 46; c.pin_href = 38; c.pin_pclk = 45;
        // The sensor's real XCLK comes from an external 20 MHz crystal (the pin
        // is not wired): xclk_freq_hz only configures the driver. 16 MHz would
        // enable the S3 EDMA but produces interlaced/corrupted frames here.
        c.xclk_freq_hz = 20000000;
        c.ledc_timer   = LEDC_TIMER_1;
        c.ledc_channel = LEDC_CHANNEL_2;
        // YUV422 (NOT RGB565): the GC0308 outputs YUV — when asked for RGB565,
        // the YUV data was interpreted as RGB → pink/magenta window, blue
        // fringes and a GREY VEIL (chroma read as luma, contrast crushed —
        // diagnosed from a user image 2026-07-18). frame2jpg converts YUV422
        // correctly. Same bandwidth (2 bytes/pixel).
        c.pixel_format = PIXFORMAT_YUV422;
        // VGA: the only size the GC0308 driver exposes correctly (its QVGA
        // windowing comes out washed out). The partial frames (real image on
        // top, flat grey below — flashlight diagnosis 2026-07-18) came from the
        // display SPI-DMA vs camera GDMA arbitration → fixed by the renderer
        // pause around fb_get (captureStep), not by the resolution.
        c.frame_size   = FRAMESIZE_VGA;
        c.jpeg_quality = 12;                     // (ignored in RGB565)
        c.fb_count     = 1;                      // 1 fb: controlled DMA window
                                                 // (WHEN_EMPTY: fills only
                                                 // during fb_get)
        c.fb_location  = CAMERA_FB_IN_PSRAM;
        c.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;

        esp_err_t e = esp_camera_init(&c);
        if (e != ESP_OK) {           // 1 retry (shared-bus timing randomness)
            esp_camera_deinit();
            delay(100);
            e = esp_camera_init(&c);
        }
        if (e != ESP_OK) {
            _failed = true;
            _lastErr = (int)e;
            // Do NOT leave the sensor rail (ALDO3, raised at the top of init)
            // powered on failure: a permanent current leak until reboot, and it
            // contradicts "camera off when idle". service() will not go through
            // deinit() again (the _failed guard), so we cut it here.
            aldo3(0);
            Serial.printf("[cam] init ECHEC (0x%x) — camera desactivee\n", (int)e);
            return false;
        }
        // HYBRID REWRITE of the sensor config: the OFFICIAL table
        // (esp_cam_sensor) repairs the colors/AWB/gamma that the pre-built
        // driver leaves broken, BUT its windowing/timing overruns the DMA
        // sized by the arduino driver (EV-EOF-OVF). So we SAVE the interface
        // registers set by esp_camera_init, write the table (soft reset
        // included), then RESTORE the interface.
        {
            // Interface/timing registers to preserve (page 0): hb/vb + current
            // exposure (01-04), window (05-0c), hb/vb high (0f),
            // sync/polarities (26), PLL/div (28). NOT 0xe2-0xed: those are the
            // AEC EXPOSURE STEPS — the arduino driver's ones (different clock)
            // capped the exposure → a lit room rendered black (observed on HW
            // 2026-07-18); the official table's ones are calibrated for the
            // real 20 MHz crystal.
            static const uint8_t KEEP[] = {
                0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,
                0x0f,0x26,0x28 };
            uint8_t saved[sizeof(KEEP)];
            wr(0xFE, 0x00);
            for (size_t i = 0; i < sizeof(KEEP); i++)
                saved[i] = rd(KEEP[i]);

            size_t n = 0;
            const uint8_t* t = initTable(n);
            for (size_t i = 0; i < n; i++) {
                wr(t[2*i], t[2*i + 1]);   // wr re-takes the recursive mutex
                                          // (already held by initGuard → stays
                                          // exclusive)
                if (i == 0) delay(2);     // settle after the soft reset (the
                                          // initGuard lock is STILL held — see
                                          // init())
            }

            wr(0xFE, 0x00);
            for (size_t i = 0; i < sizeof(KEEP); i++)
                wr(KEEP[i], saved[i]);
            delay(10);
        }
        // User settings (cam_*) applied on top of the base table.
        // NO set_gain_ctrl/set_exposure_ctrl from the pre-built driver:
        // AEC/AWB are already configured (and calibrated) by the table.
        _apBright = _apContr = _apSat = _apVflip = _apHmir = _apLowlight = 1e9f;
        _apCbar = 0.0f;
        applySensorIfChanged();
        _ready = true;
        _lastUseMs = millis();
        Serial.printf("[cam] init OK (GC0308 VGA YUV422, SCCB via M5.In_I2C)\n");
        return true;
    }

    // Applies the Tuning sensor settings when they change — ALL through DIRECT
    // register writes (page 0, SCCB via M5.In_I2C): the pre-built driver's
    // setters (set_brightness/contrast/saturation...) BREAK the color config
    // laid down by the official table (the official gc0308.c has NO such
    // control at all — everything comes from the table).
    // Calibrated baselines from the table: 0xb1/0xb2 (Cb/Cr sat) = 0x40,
    // 0xb3 (luma contrast) = 0x40, 0xd5 (luma offset) = 0xf2, 0x14 = 0x10.
    void applySensorIfChanged() {
        if (!_tuning) return;
        if (!esp_camera_sensor_get()) return;
        float b = clampf(_tuning->cam_brightness, -2, 2);
        float c = clampf(_tuning->cam_contrast, -2, 2);
        float sa = clampf(_tuning->cam_saturation, -2, 2);
        float vf = _tuning->cam_vflip >= 0.5f ? 1 : 0;
        float hm = _tuning->cam_hmirror >= 0.5f ? 1 : 0;
        float cb = _tuning->cam_colorbar >= 0.5f ? 1 : 0;
        float ll = _tuning->cam_lowlight >= 0.5f ? 1 : 0;
        bool dirty = b != _apBright || c != _apContr || sa != _apSat ||
                     vf != _apVflip || hm != _apHmir ||
                     cb != _apCbar || ll != _apLowlight;
        if (!dirty) return;
        auto W = [](uint8_t reg, uint8_t val) { wr(reg, val); };  // lock + write
        W(0xFE, 0x00);                                    // page 0
        if (b != _apBright) {                             // AEC TARGET (real...
            // Calibrated base 0x48, slope 22: ±2 → 0x1C..0x74 (brighter at the
            // top end, user said "still a bit dark"). HARD ceiling 0x78: beyond
            // it the AEC response INVERTS (darker image). NB: the camera lockup
            // of 2026-07-19 came from the non-exclusive SCCB init, NOT from
            // this value (init fixed). To go further: cam_lowlight.
            int v = 0x48 + (int)(22.0f * b);
            if (v > 0x78) v = 0x78;
            if (v < 0x10) v = 0x10;
            W(0xd3, (uint8_t)v);
            _apBright = b;
        }
        if (c != _apContr) {                              // luma contrast
            W(0xb3, (uint8_t)(0x40 + (int)(16.0f * c))); _apContr = c;
        }
        if (sa != _apSat) {                               // Cb/Cr saturation
            uint8_t v = (uint8_t)(0x40 + (int)(16.0f * sa));
            W(0xb1, v); W(0xb2, v);                       _apSat = sa;
        }
        if (vf != _apVflip || hm != _apHmir) {            // mirrors (0x14)
            W(0x14, (uint8_t)(0x10 | ((int)hm) | ((int)vf << 1)));
            _apVflip = vf; _apHmir = hm;
        }
        if (ll != _apLowlight) {                          // low-light mode
            // GC0308 datasheet §AEC: 0xec = max allowed exposure levels
            // ([5:4]); 0xee/0xef = the AEC's post/pre digital gain ceilings.
            // Lifting those ceilings lets the AEC brighten far more in a dark
            // room (at the cost of noise).
            if (ll >= 0.5f) { W(0xec, 0x30); W(0xee, 0xff); W(0xef, 0xa0); }
            else            { W(0xec, 0x20); W(0xee, 0xc0); W(0xef, 0x60); }
            _apLowlight = ll;
        }
        if (cb != _apCbar) {                              // test pattern
            sensor_t* s = esp_camera_sensor_get();
            if (s) s->set_colorbar(s, (int)cb);           // only setter kept
            _apCbar = cb;                                 // (proven to work)
        }
    }

    void deinit() {
        esp_camera_deinit();
        // Cut the sensor rail (ALDO3): the re-init then starts from an
        // UNPOWERED state, like a fresh boot (where it works reliably) —
        // without it the sensor stayed powered between deinit and re-init and
        // could get stuck (missing frames after a camera off/on cycle 07-19).
        aldo3(0);
        _ready = false;
        // Flush the LATCHED init request: while watching, every request arms
        // _initReq, which service() only consumes in the !_ready branch —
        // without this reset the 60 s auto-off was immediately followed by a
        // full PHANTOM RE-INIT (VOR freeze ~0.5-1 s with no client at all) and
        // the camera stayed powered for another 60 s (review 07-21). A client
        // that is still there will post markWanted() again → legitimate
        // re-init.
        _initReq = false;
        // Flush the snapshot state: a stale _snapAtMs made the ?full=1 handler
        // believe a fresh shot existed right after a camera=0→1 cycle → 503 in
        // a loop, never arming the capture (07-21).
        _snapReq.store(false);
        _snapAtMs.store(0);
        // Re-apply ALL the settings on the next init (the sensor loses its
        // config when powered down) — except colorbar (restarts from 0)
        _apBright = _apContr = _apSat = _apVflip = _apHmir
                  = _apLowlight = 1e9f;
        _apCbar = 0.0f;
        // RESET the pacing counters: otherwise a camera=0→1 cycle left with
        // _lastServedVer < _frameVer (last captured frame never served, e.g.
        // client disconnected) freezes the consumed=false gate at re-init → no
        // capture at all, permanent 503 until reboot (review 07-20).
        _frameVer = _lastServedVer = 0;
        if (_mtx && xSemaphoreTake(_mtx, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (_jpg)  { heap_caps_free(_jpg);  _jpg = nullptr;  _jpgLen = 0; }
            if (_snap) { heap_caps_free(_snap); _snap = nullptr; _snapLen = 0; }
            xSemaphoreGive(_mtx);
        }
        if (_qvga) { heap_caps_free(_qvga); _qvga = nullptr; }   // camera task
                                                                 // is the only toucher
        Serial.printf("[cam] deinit (inactivite)\n");
    }
};

} // namespace sce
