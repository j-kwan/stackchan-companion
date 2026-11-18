#pragma once
// =============================================================================
// WebApi.h — StackChan-Companion (app)
// =============================================================================
// Full REST API: control + live tuning, no cable needed. WiFi STA
// (config.yaml / POST /api/wifi) with AP fallback, mDNS, captive portal (AP),
// embedded console (WebConsole.h — covers ALL endpoints), Swagger.
//
// Endpoints — reference spec: OPENAPI_JSON (WebConsole.h):
//   System      GET /api/status · POST /api/wifi · POST /api/config/reload
//               POST /api/update (firmware OTA + reboot) · POST /api/reboot
//               POST /api/poweroff · POST /api/security (Basic Auth)
//   Expressions POST /api/emotion
//   Animations  POST /api/animation · GET /api/dances · POST /api/dance
//   SD dances   GET /api/dances/files · POST /api/dances/file (CSV upload)
//               DELETE /api/dances/file · POST /api/dances/reload
//   Servo       POST /api/servo (head remote control)
//   Options     POST /api/config · GET|POST /api/tuning
//   Bins        GET|POST|DELETE /api/bins · POST /api/bins/launch|stop
//
// GOLDEN RULE (A2.6/§3.5): AsyncTCP callbacks NEVER touch state directly —
// only brain->post(Command), the Tuning registry (atomic floats), or FLAGS
// consumed by loop() (SD save/reload, flashing, dance reload).
// Deliberate exception: uploads stream to the SD card straight from AsyncTCP
// (see the [Bins] handler).
//
// LANGUAGE (EN/FR — config.yaml `lang:`, firmware/common/I18n.h) — TWO
// mechanisms, because there are two kinds of text here and they have opposite
// constraints:
//
//   1. The CONSOLE PAGE (WebConsole.h) is HTML. Its English is written in the
//      markup and French is applied over it by JavaScript, exactly like the PC
//      tools (tools/choregraphies/i18n.js: one table, `t(key, vars)`,
//      `data-i18n*` attributes). Doubling a 60 KB page in flash to ship a
//      second language would be absurd when the browser can do it for free,
//      and the English page still reads correctly if that script never runs.
//      The firmware's only job is to say WHICH language the card is set to —
//      the browser cannot read config.yaml. See `sendConsole()`.
//
//   2. The API MESSAGES (the `error`/`note` values, the camera's text/plain
//      replies) are NOT HTML. curl, Swagger and Home Assistant read them with
//      no JavaScript anywhere, so a page-side table cannot reach them: they go
//      through `sce::T(en, fr)`, the mechanism I18n.h exists for. The cost is
//      bounded because the JSON ENVELOPE is factored into sendErr/sendNote —
//      the second language pays for its own words, not for a second copy of
//      forty `{"error":"…"}` literals.
//
// What stays untranslated, deliberately: JSON KEYS, endpoint paths, config
// keys, HTTP headers and the Serial diagnostics. Those are a machine contract
// and a maintainer's log, not a user interface.
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <Update.h>
#include <atomic>
#include <memory>
#include "esp_heap_caps.h"
#include <ESPAsyncWebServer.h>
#include "../engine/Emotions.h"
#include "../behavior/RuleEngine.h"   // GET /api/rules lists the loaded table
#include "../behavior/Dances.h"       // ...and names the dance a rule plays
#include "../behavior/PersonalityYaml.h"  // the characters + their theme names
#include "../engine/Tuning.h"
#include <time.h>                            // NTP-synced wall clock
#include "../../firmware/common/SunClock.h"  // /api/sensors: night + clock
#include "../behavior/Brain.h"
#include "../engine/Renderer.h"
#include "SdConfig.h"
#include "DanceStore.h"
#include "BinLimits.h"      // shared /bins/ capacity + name length (Launcher)
#include "SoundTracker.h"  // mic levels (console status)
#include "../hal/CpuLoad.h" // per-core CPU load (console status)
#include "../hal/Board.h"   // PMIC battery (console status)
#include "../hal/Camera.h"  // GC0308 (Home Assistant / Frigate)
#include "../../firmware/common/I18n.h"  // sce::T / sce::langCode (EN default)
// GZIP of the embedded pages. WebConsole.h — the readable markup — is the
// SOURCE OF TRUTH but is deliberately NOT included here: only the compressed
// arrays are compiled in, so the plain ~77 KB never reaches flash.
// scripts/build/gen_console_gz.py regenerates this header before every build.
#include "WebConsoleGz.h"
#include "../../firmware/common/FirmwareInfo.h"  // GET /api/firmware: which build runs
#include "../../firmware/common/Trace.h"   // debug narration of the WiFi sequence
#include "../../firmware/common/SdRoot.h"  // SD_ROOT: sdPathAllowed derives its offsets from
                                            // this rather than repeating the string's length as
                                            // a literal — the 2026-09-19 rename shipped exactly
                                            // that bug: three magic numbers (16, 16, 30) sized
                                            // for the old, shorter "/stackchan-eyes" prefix were
                                            // left behind when the path literals were renamed
                                            // around them, silently denying every SD read/write
                                            // under the new root until a live-robot check caught it.

namespace sce {

class WebApi {
public:
    // apiCreds: reference to SdConfig::api — POST /api/security writes
    // straight into it (like WifiCreds), persisted by the normal save().
    WebApi(Brain& brain, Renderer& renderer, Tuning& tuning, WifiCreds& creds,
           ApiCreds& apiCreds, DanceStore& dances)
        : _server(80), _brain(brain), _renderer(renderer), _tuning(tuning),
          _creds(creds), _apiCreds(apiCreds), _dances(dances) {}

    // Persistence: callbacks RAISE a flag; loop() consumes it and writes to
    // the SD card (multi-step FAT access must stay out of AsyncTCP — A2.6).
    bool consumeSaveRequest() { return _saveReq.exchange(false); }
    // POST /api/config?lang= only STAGED the language. Returns 0 = English,
    // 1 = French, -1 = nothing pending. The caller applies it (`sce::setLang`)
    // and writes it to the card: both are things an AsyncTCP callback must not
    // do — one is a global every task reads, the other is an SD write (A2.6).
    int consumeLangRequest() { return _langReq.exchange(-1); }
    // Arms SD persistence (consumed by loop()) — used outside AsyncTCP,
    // e.g. cycling the status bar mode with a touch swipe (main.cpp).
    void requestSave() { _saveReq.store(true); }

    // Consumed by loop(): true once per staged personality write. `out` gets a
    // COPY under the lock, so the applier never reads a struct a second request
    // is rewriting.
    bool consumePersonalityWrite(sce::Personality& out, bool& del) {
        if (!_persoReq.exchange(false)) return false;
        portENTER_CRITICAL(&_persoMux);
        out = _persoPending;
        del = _persoDelete;
        portEXIT_CRITICAL(&_persoMux);
        return true;
    }


    // The staged trace lines, printed from loop(). POST /api/tuning narrated
    // each real change with `sce::trace::log`, which ends in a BLOCKING UART
    // write — on the AsyncTCP task, in a file that spells out four lines above
    // why staging exists (A2.6). The queue itself lives in
    // `firmware/common/Trace.h`, because the same need exists wherever a
    // caller must not block; see the comment there.
    void drainTuningTrace() { sce::trace::drain(); }

    // SD state (shown in /api/status + console) — set by main at boot
    void setSdPresent(bool sd) { _sdPresent = sd; }

    // True while a client is streaming an upload (dance CSV, bin, OTA). The
    // SD hot-swap probe in loop() must NOT SD.end() a filesystem under an
    // open _uploadFile (review 08-04): the AsyncTCP task keeps writing into
    // the torn-down mount — crash or FAT corruption. Same 10 s dead-owner
    // window as uploadOwns(), so an aborted upload cannot pin the probe.
    bool uploadBusy() {
        portENTER_CRITICAL(&_upMux);
        const bool busy = _uploadReq != nullptr &&
                          millis() - _uploadLastMs <= 10000;
        portEXIT_CRITICAL(&_upMux);
        return busy;
    }

    // Mic levels in /api/status (console) — set by main at boot
    void attachSoundTracker(const SoundTracker* st) { _sound = st; }

    // Per-core CPU load in /api/status — set by main at boot
    void attachCpuLoad(const CpuLoad* cl) { _cpu = cl; }

    // Battery (PMIC) in /api/status — set by main at boot
    void attachBoard(const Board* b) { _board = b; }
    void attachFields(FieldStore* f) { _fields = f; }

    // GC0308 camera (/api/camera/*) — set by main at boot
    void attachCamera(Camera* c) { _camera = c; }

    // MEASURED servo pose. The SERVO TASK samples it (it owns the half-duplex
    // bus); loop() copies it here; this class only reads. Nothing in an
    // AsyncTCP callback ever touches the serial bus (rule A2.6), and no second
    // task reaches for it (review 08-02).
    // Plain struct rather than three atomics, deliberately: it is written by
    // loop() and read by the web callback, so a torn read can pair one axis
    // with the other's timestamp. On a diagnostic that refreshes at 5 Hz that
    // is one odd sample which the next poll corrects — the alternative is a
    // dependency from WebApi to `ServoMotion`, which lives behind
    // `#ifdef SCE_USE_SERVO` and would drag the servo build flag into the web
    // layer for a value nothing acts on.
    struct ServoPose {
        float    yaw   = -1.0f;
        float    pitch = -1.0f;
        uint32_t stampMs = 0;     // 0 = never sampled
    };
    void attachServoPose(const ServoPose* p) { _pose = p; }

    // WHAT THE WALL CLOCK ACTUALLY KNOWS, published by main() the same way as
    // the servo pose: read here, never derived here.
    //
    // `ntpOk` is NOT "the epoch looks plausible". `M5.begin()` restores the
    // system clock from the BM8563, so a robot that has never seen a single
    // NTP packet still reports a believable time — which is exactly how the
    // old sync guard convinced itself for weeks that NTP was working. Only
    // the SNTP callback sets this.
    struct ClockState {
        bool ntpOk   = false;    // a packet REALLY landed since boot
        bool pending = false;    // a sync is armed and waiting for one
    };
    void attachClockState(const ClockState* p) { _clk = p; }

    // Forced NTP resync requested by the API — consumed in loop(). It cannot
    // happen in the callback: configTime tears down and restarts the SNTP
    // client, and the RTC write that follows is I2C on the shared 11/12 bus
    // (A2.6, A2.15's bus guard).
    bool consumeClockSyncRequest() { return _clockSyncReq.exchange(false); }

    // AP fallback vs joined an existing network. main() asks before starting
    // NTP: in AP mode the robot IS the network and there is no route out, so
    // an SNTP client would retry forever against an unreachable pool.
    bool isApMode() const { return _apMode; }

    // config.yaml reload requested by the API — consumed in loop()
    // (multi-step FAT read must stay out of AsyncTCP, rule A2.6)
    bool consumeReloadRequest() { return _reloadReq.exchange(false); }

    // Reload of the SD choreographies (/dances/*.csv) — consumed by loop()
    bool consumeDancesReloadRequest() { return _dancesReloadReq.exchange(false); }
    bool consumeRulesReloadRequest()  { return _rulesReloadReq.exchange(false); }

    // The rule table, for `GET /api/rules`. `builtins` is how many of them are
    // COMPILED IN rather than read from the card - the console greys the two
    // apart, and a user who deletes rules.txt should not be told the robot has
    // forgotten how to sleep in the dark.
    void attachRules(const RuleEngine* eng, int builtins) {
        _ruleEng = eng; _ruleBuiltins = builtins;
    }

    // ------------------------------------------------------------------
    // DEFERRED SD WORK — the single entry point loop() calls, renderer
    // PAUSED, to do on the card what an AsyncTCP callback must not do.
    //
    // Two jobs share it because they share the constraint (A2.6 + A2.16) and
    // the caller's pause/resume bracket:
    //   1. the /bins/ NAME CACHE, which POST /api/bins/launch validates
    //      against instead of calling SD.exists() from a callback;
    //   2. one PENDING LISTING (GET /api/bins, GET /api/sd/list), whose
    //      request has been PAUSED by its handler and is answered from here.
    // Returns false when there was nothing to do.
    // ------------------------------------------------------------------
    // ------------------------------------------------------------------
    // The band timer's command slot (see the /api/timer route). loop() calls
    // takeTimerCmd() once per pass; it returns 0 when there is nothing to do.
    // ------------------------------------------------------------------
    int takeTimerCmd(int& m, int& s) {
        const int c = _timerCmd.exchange(0);
        // BOTH set commands carry the duration — 3 (set) and 4 (set and
        // start). Loading it for 3 alone left the presets arming a 00:00
        // timer, which `tap()` correctly refuses to start: the endpoint
        // answered 200 and nothing happened. Found by testing the effect on
        // target rather than the status code (08-04).
        if (c == 3 || c == 4) { m = _timerSetM.load(); s = _timerSetS.load(); }
        return c;
    }

    bool serviceSdWork() {
        bool did = refreshBinsCache();
        return serviceSdListing() || did;
    }

    // ASK BEFORE PAUSING THE RENDERER. loop() used to pause, call the worker
    // — which returns immediately when there is nothing to do — and resume, on
    // EVERY pass. pause() is not a flag: it WAITS for the frame in flight to be
    // pushed (engine/Renderer.h), so at loop rate it stopped the renderer
    // perhaps a hundred times a second. The face stuttered and the status band
    // flickered without pause (user, 08-03), and /api/status showed it plainly:
    // frame avg 41 ms / max 60 ms against a 33 ms period.
    // The other deferred SD blocks in loop() all test their flag BEFORE pausing
    // (consumeReloadRequest, consumeRulesReloadRequest…). This one has to too —
    // hence a peek that costs one bool read and one atomic load.
    bool sdWorkPending() const {
        return _binsDirty.load(std::memory_order_acquire) ||
               _jobState.load(std::memory_order_acquire) == JOB_ARMED;
    }

    // Marks the cache stale. Safe from a callback: it writes one flag.
    void binsChanged() { _binsDirty.store(true, std::memory_order_release); }

    // Launch of a .bin requested by the API — consumed in loop() (flashing
    // CANNOT happen from AsyncTCP). Returns true + copies the path out.
    bool consumeLaunchRequest(char* path, size_t len) {
        if (!_launchReq.exchange(false)) return false;
        strncpy(path, _launchPath, len - 1);
        path[len - 1] = '\0';
        return true;
    }

    // ------------------------------------------------------------------
    // Starts WiFi + the server. STA mode if client_ssid is configured
    // (config.yaml / POST /api/wifi), AP fallback otherwise or on failure
    // (10 s timeout). Returns the IP (empty string if everything failed).
    // ------------------------------------------------------------------
    String begin() {
        bool sta = false;
        if (_creds.clientSsid.length()) {
            WiFi.mode(WIFI_STA);
            WiFi.setHostname(_creds.hostname.c_str());
            sce::trace::log("net", "STA join '%s'...",
                            _creds.clientSsid.c_str());
            WiFi.begin(_creds.clientSsid.c_str(), _creds.clientPass.c_str());
            uint32_t t0 = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) {
                delay(250);
            }
            sta = WiFi.status() == WL_CONNECTED;
            sce::trace::log("net", "STA %s en %lu ms (status %d)",
                            sta ? "OK" : "ECHEC",
                            (unsigned long)(millis() - t0),
                            (int)WiFi.status());
            if (!sta) {
                Serial.printf("[api] STA '%s' echec -> repli AP\n",
                              _creds.clientSsid.c_str());
                WiFi.disconnect(true);
            }
        }
        if (!sta) {
            WiFi.mode(WIFI_AP);
            if (!WiFi.softAP(_creds.apSsid.c_str(), _creds.apPass.c_str()))
                return String();
            // Captive portal: every DNS query → us. Operating systems
            // detect the portal and open the console automatically.
            _apMode = true;
            _dns.start(53, "*", WiFi.softAPIP());
            sce::trace::log("net", "repli AP '%s' ip=%s",
                            _creds.apSsid.c_str(),
                            WiFi.softAPIP().toString().c_str());
        }
        // Modem-sleep OFF (STA): the default power save (WIFI_PS_MIN_MODEM)
        // puts the radio to sleep between beacons → +100-300 ms of latency on
        // EVERY HTTP request (and worse under camera load) — unacceptable for
        // a remote-controlled robot (click→action lag, diagnosed 2026-07-20).
        // Cost: ~40-60 mA extra WiFi draw (traded for responsiveness).
        WiFi.setSleep(false);
        // TX power EXPLICITLY maxed (19.5 dBm): measured RSSI -74/-78 dBm
        // (weak signal, retransmissions) — do not rely on a config default.
        WiFi.setTxPower(WIFI_POWER_19_5dBm);

        // mDNS → http://<hostname>.local (mostly useful in STA mode)
        if (MDNS.begin(_creds.hostname.c_str())) {
            MDNS.addService("http", "tcp", 80);
        }

        // ---- Basic Auth protection (config.yaml `api:`, POST /api/security
        // at runtime) — GLOBAL (console + every /api/*). A single enforcement
        // point (server-level middleware, ALWAYS attached) rather than a guard
        // inside each handler — that way none can be forgotten (lesson from
        // A2.19: every route is independent). NONE/BASIC toggles on the fly
        // through applyAuth() (POST /api/security needs no reboot).
        // HAND-ROLLED Basic Auth (not AsyncAuthenticationMiddleware): the lib
        // stores user/password in Strings that a runtime change reallocates
        // while a request is comparing them (use-after-free). Here we compare
        // our own STABLE char[64] (never reallocated) + an atomic flag — a byte
        // read mid-rewrite at worst fails the compare (401, fail-CLOSED), never
        // crashes. Applies to ALL routes + the console (as before).
        _server.addMiddleware([this](AsyncWebServerRequest* req,
                                     ArMiddlewareNext next) {
            // BEFORE anything else, including the auth check: this is the only
            // hook on this server that every request passes through, and it is
            // where a listing parked by a loop() that never came back gets its
            // 503 (see reapStaleListing). It reaps OUR OWN earlier — already
            // authenticated — request and never looks at this one, so running
            // it ahead of the password comparison leaks nothing. The cost on
            // the normal path is a single atomic load.
            reapStaleListing();
            if (_authEnabled.load() &&
                !req->authenticate(_apiCreds.username, _apiCreds.password)) {
                req->requestAuthentication(AsyncAuthType::AUTH_BASIC);
                return;
            }
            next();
        });
        applyAuth();

        // ---- /api/status — full state (console: top panel).
        //      mic: state NAME (off|warmup|standby|active — single source
        //      SoundTracker::stateName, review 2026-07-17); micWait =
        //      remaining warm-up seconds; RMS levels are only valid in the
        //      "active" state.
        route("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
            SoundTracker::MicState st = _sound ? _sound->state()
                                               : SoundTracker::MIC_OFF;
            bool mic = (st == SoundTracker::MIC_ACTIVE);
            // Same pair /api/sensors publishes: the wall clock and what
            // SoundFx actually decides from it. 0 = never synced, which is
            // itself the answer to "why is the night volume not applying".
            const time_t nowUtc = sce::clockSynced(time(nullptr)) ? time(nullptr) : 0;
            const bool isNight = nowUtc &&
                                 sce::isNight(_tuning.lat, _tuning.lon, nowUtc);
            char body[720];
            snprintf(body, sizeof(body),
                     "{\"emotion\":\"%s\",\"frameAvgUs\":%lu,\"frameMaxUs\":%lu,"
                     "\"uptimeS\":%lu,\"heap\":%lu,\"rssi\":%d,"
                     "\"mode\":\"%s\",\"ip\":\"%s\",\"crt\":%d,\"sd\":%d,"
                     "\"yaw\":%.1f,\"pitch\":%.1f,"
                     "\"mic\":\"%s\",\"micWait\":%lu,"
                     "\"micL\":%.0f,\"micR\":%.0f,"
                     "\"micAmb\":%.0f,\"micEvt\":%lu,"
                     "\"load0\":%d,\"load1\":%d,"
                     "\"batt\":%d,\"chg\":%d,"
                     "\"inaV\":%.2f,\"light\":%d,\"heading\":%.0f,"
                     "\"authOn\":%d,\"authUser\":\"%s\","
                     "\"cam\":\"%s\",\"camErr\":%d,\"statusbar\":%d,"
                     // clock/night are ALSO in /api/sensors. Duplicated on
                     // purpose and with a precedent (light and heading already
                     // are): the console header polls /api/status alone, and a
                     // second request per second, for two integers, to keep a
                     // chip fresh is a worse trade than two more fields here.
                     "\"clock\":%ld,\"night\":%d}",
                     emotionName(_brain.currentEmotion()),
                     (unsigned long)_renderer.frameAvgUs(),
                     (unsigned long)_renderer.frameMaxUs(),
                     (unsigned long)(millis() / 1000),
                     (unsigned long)ESP.getFreeHeap(),
                     _apMode ? 0 : (int)WiFi.RSSI(),
                     _apMode ? "AP" : "STA",
                     _apMode ? WiFi.softAPIP().toString().c_str()
                             : WiFi.localIP().toString().c_str(),
                     _brain.crtOn() ? 1 : 0,
                     _sdPresent ? 1 : 0,
                     _brain.headYawDeg(), _brain.headPitchDeg(),
                     SoundTracker::stateName(st),
                     (unsigned long)(_sound ? _sound->warmupRemainS() : 0),
                     mic ? _sound->rmsL() : 0.0f,
                     mic ? _sound->rmsR() : 0.0f,
                     mic ? _sound->ambient() : 0.0f,
                     (unsigned long)(_sound ? _sound->events() : 0),
                     _cpu ? _cpu->load0() : 0,
                     _cpu ? _cpu->load1() : 0,
                     _board ? (int)_board->cachedBattery() : -1,
                     (_board && _board->cachedCharging()) ? 1 : 0,
                     _board ? _board->cachedInaV() : -1.0f,
                     _board ? _board->cachedLight() : -1,
                     _brain.imuHeadingDeg(),
                     _authEnabled.load() ? 1 : 0,
                     _apiCreds.username,
                     (_tuning.camera < 0.5f || !_camera) ? "off"
                                                         : _camera->stateName(),
                     _camera ? _camera->lastErr() : 0,
                     _renderer.statusBar(),
                     nowUtc > 0 ? (long)nowUtc : 0L,
                     isNight ? 1 : 0);
            req->send(200, "application/json", body);
        });

        // ---- GET /api/sensors: telemetry of the extra K151 sensors —
        //      INA226 gauge (bus/shunt voltage), LTR-553 ambient light, BMM150
        //      magnetic heading. CACHED values (refreshed by loop() — no
        //      blocking I2C in this callback, A2.6); heading is a float cached
        //      by the Brain (100 Hz). Fields are -1 when the sensor is absent. ----
        route("/api/sensors", HTTP_GET, [this](AsyncWebServerRequest* req) {
            // `clock` / `night` (08-01): the night chirp volume is driven by
            // the SUN, which makes it invisible until 3 am — exactly the kind
            // of feature that sits broken in a backlog for weeks (it did:
            // T9, "never observed at night"). Published so it can be READ at
            // noon: `clock` is the NTP-synced UTC epoch (0 = never synced,
            // which is itself the answer to "why is it still loud"), `night`
            // is what SoundFx actually decides from it and from lat/lon.
            const time_t nowUtc = time(nullptr);
            const bool   synced = sce::clockSynced(nowUtc);
            // mag_* (08-04): the raw BMM150 field, in µT, so the sensor can
            // be exercised over HTTP (scripts/dev/test-mag.ps1) — opening the
            // serial port resets the board over native USB, which is how the
            // first magnetometer session lost half its rotations.
            const ImuReader::Raw3 mg = _brain.imuMagUT();
            char body[448];
            snprintf(body, sizeof(body),
                     "{\"batt_pct\":%d,\"charging\":%d,\"ina_v\":%.3f,"
                     "\"ina_shunt_mv\":%.3f,\"light_pct\":%d,\"light_raw\":%d,"
                     "\"heading\":%.1f,"
                     "\"mag_x\":%.1f,\"mag_y\":%.1f,\"mag_z\":%.1f,"
                     "\"imu_cal\":%d,"
                     "\"clock\":%ld,\"night\":%d}",
                     _board ? (int)_board->cachedBattery() : -1,
                     (_board && _board->cachedCharging()) ? 1 : 0,
                     _board ? _board->cachedInaV() : -1.0f,
                     _board ? _board->cachedInaShunt() : 0.0f,
                     _board ? _board->cachedLight() : -1,
                     _board ? _board->cachedLightRaw() : -1,
                     _brain.imuHeadingDeg(),
                     mg.x, mg.y, mg.z,
                     _brain.imuCalState(),
                     synced ? (long)nowUtc : 0L,
                     (synced && sce::isNight(_tuning.lat, _tuning.lon, nowUtc))
                         ? 1 : 0);
            req->send(200, "application/json", body);
        });

        // ---- GET /api/firmware: WHICH BUILD IS RUNNING, and why it last
        //      started. Its own route rather than four more fields in
        //      /api/status, which the console polls every 2 s: all of this is
        //      FIXED for the whole boot, so putting it there would pay for it
        //      thousands of times to read it once.
        //
        //      `slot` is the field the 2026-08-10 incident turned on: a USB
        //      flash writes ONE OTA slot and never touches `otadata`, so the
        //      chip can boot the other one and every outward sign - a verified
        //      esptool hash, a robot back on WiFi - still says success.
        //      Both fingerprints are REPRODUCIBLE from a working copy, which is
        //      the whole point: `sha` is `sha256sum firmware.elf` (first 8 hex)
        //      and `console` is what `gen_console_gz.py --check` prints — so a
        //      running robot can be compared to the repository without a flash
        //      dump, which is how the incident had to be diagnosed.
        //      Everything below is a mapped-flash read: no I2C, no SD (A2.6). ----
        route("/api/firmware", HTTP_GET, [](AsyncWebServerRequest* req) {
            char body[192];
            snprintf(body, sizeof(body),
                     "{\"slot\":\"%s\",\"sha\":\"%s\",\"console\":\"%s\","
                     "\"reset\":\"%s\"}",
                     sce::fw::slot(), sce::fw::sha8(),
                     sce::CONSOLE_SRC_SHA, sce::fw::resetReasonName());
            req->send(200, "application/json", body);
        });

        // ---- POST /api/reboot: clean restart, DEFERRED (same mechanism as
        //      the OTA: the HTTP response goes out first, the restart is
        //      consumed by loop()) ----
        // ---- THE WALL CLOCK ----
        // `/api/clock/sync` is registered BEFORE `/api/clock`: the router runs
        // in BackwardCompatible mode and a prefix registered first swallows its
        // own sub-paths (A2.19). `checkRouteOrder()` denounces the mistake at
        // boot, but the order is the fix.
        route("/api/clock/sync", HTTP_POST, [this](AsyncWebServerRequest* req) {
            // AP MODE IS A REFUSAL, NOT A PENDING. The robot IS the network;
            // there is no route to a time server, so an SNTP client would
            // retry against an unreachable pool for ever while the console
            // showed "syncing". Say what is true.
            if (_apMode) {
                sendErr(req, 409, "no route out in AP mode",
                                  "aucune route sortante en mode AP"); return; }
            _clockSyncReq.store(true);
            sendNote(req, 202,
                     "NTP client restarted; the RTC follows the first packet",
                     "client NTP redemarre ; la RTC suit le premier paquet");
        });

        route("/api/clock", HTTP_GET, [this](AsyncWebServerRequest* req) {
            // NO I2C HERE. The BM8563 sits on the shared 11/12 bus and this is
            // an AsyncTCP callback (A2.6). Everything below is a variable.
            const time_t t = time(nullptr);
            char body[160];
            snprintf(body, sizeof(body),
                     "{\"epoch\":%ld,\"plausible\":%d,\"ntp\":%d,"
                     "\"pending\":%d,\"ap\":%d}",
                     (long)t, sce::clockSynced(t) ? 1 : 0,
                     (_clk && _clk->ntpOk) ? 1 : 0,
                     (_clk && _clk->pending) ? 1 : 0,
                     _apMode ? 1 : 0);
            req->send(200, "application/json", body);
        });

        route("/api/reboot", HTTP_POST, [this](AsyncWebServerRequest* req) {
            _rebootAtMs.store(millis() + 800);
            sendNote(req, 202, "rebooting in ~1 s",
                               "redémarrage dans ~1 s");
        });

        // ---- POST /api/poweroff: full shutdown (AXP2101 PMIC),
        //      DEFERRED (same mechanism as /api/reboot) ----
        route("/api/poweroff", HTTP_POST, [this](AsyncWebServerRequest* req) {
            _poweroffAtMs.store(millis() + 800);
            sendNote(req, 202, "shutting down in ~1 s",
                               "extinction dans ~1 s");
        });

        // ---- /api/emotion?name=X[&ms=N] ----
        route("/api/emotion", HTTP_POST, [this](AsyncWebServerRequest* req) {
            if (!req->hasParam("name")) {
                sendErr(req, 400, "the name parameter is required",
                                  "paramètre name requis"); return; }
            eEmotions e = emotionFromName(req->getParam("name")->value().c_str());
            if (e >= EMOTIONS_COUNT) {
                sendErr(req, 404, "unknown emotion",
                                  "émotion inconnue"); return; }
            uint32_t ms = req->hasParam("ms")
                        ? (uint32_t)req->getParam("ms")->value().toInt() : 0;
            // An emotion asked for explicitly (console/API) wins: abort the
            // running dance, otherwise its keyframes re-apply their own
            // emotion and overwrite this one (same rule as screen swipes).
            _brain.post({ CmdType::AbortDance });
            _brain.post({ CmdType::SetEmotion, (int32_t)e, ms });
            char body[64];
            snprintf(body, sizeof(body), "{\"emotion\":\"%s\",\"ms\":%lu}",
                     emotionName(e), (unsigned long)(ms ? ms : 10000));
            req->send(200, "application/json", body);
        });

        // ---- /api/animation?name=blink|winkLeft|winkRight ----
        route("/api/animation", HTTP_POST, [this](AsyncWebServerRequest* req) {
            String n = req->hasParam("name") ? req->getParam("name")->value() : "";
            CmdType t;
            if      (n.equalsIgnoreCase("blink"))     t = CmdType::Blink;
            else if (n.equalsIgnoreCase("winkLeft"))  t = CmdType::WinkLeft;
            else if (n.equalsIgnoreCase("winkRight")) t = CmdType::WinkRight;
            else { sendErr(req, 404, "unknown animation",
                                    "animation inconnue"); return; }
            _brain.post({ t });
            req->send(200, "application/json", "{\"ok\":true}");
        });

        // ---- /api/config/reload: re-read config.yaml from the SD card ----
        // (user request 2026-07-12) Tuning is applied live by loop(); reloaded
        // WiFi credentials only take effect at the next restart.
        // ⚠ CRITICAL ORDER (bug found 2026-07-12): ESPAsyncWebServer routes
        // `/api/config` in "BackwardCompatible" mode — it ALSO matches every
        // `/api/config/*` (WebServer.cpp: `path.startsWith(_value+"/")`).
        // The FIRST registered handler that matches wins: `/api/config/reload`
        // MUST be registered BEFORE `/api/config`, otherwise it is swallowed
        // silently (it used to return "aucun param" from the wrong handler).
        // Same risk everywhere else: ALWAYS register the most specific path
        // before its prefix (see /api/dances/files and /api/bins/launch|stop
        // further down).
        route("/api/config/reload", HTTP_POST,
                   [this](AsyncWebServerRequest* req) {
            if (!_sdPresent) {
                sendErr(req, 503, "no SD card",
                                  "pas de carte SD"); return; }
            _reloadReq.store(true);
            sendNote(req, 202,
                     "re-read in ~1 s (wifi: at the next restart)",
                     "relecture dans ~1 s (wifi : au prochain redémarrage)");
        });

        // ---- /api/config?crt=0|1 ----
        route("/api/config", HTTP_POST, [this](AsyncWebServerRequest* req) {
            bool changed = false;
            if (req->hasParam("crt")) {
                _brain.post({ CmdType::SetCrt,
                              req->getParam("crt")->value().toInt() ? 1 : 0 });
                changed = true;
            }
            // LANGUAGE, at runtime. It used to be settable only by editing
            // `lang:` on the card and rebooting — which meant the one setting
            // that decides whether the user can READ the console was the one
            // setting the console could not change.
            //
            // Applied immediately (the whole firmware reads `sce::T` live) and
            // staged for the card, so it survives the reboot. The page picks it
            // up from the `sce_lang` cookie on its next load; the console also
            // re-translates itself on the spot, so nothing waits.
            if (req->hasParam("lang")) {
                const String v = req->getParam("lang")->value();
                if (v != "en" && v != "fr") {
                    sendErr(req, 400, "lang must be en or fr",
                                      "lang doit valoir en ou fr"); return; }
                // STAGED, not applied here. `sce::g_lang` is a global that
                // every task reads through `sce::T`, so writing it from an
                // AsyncTCP callback is exactly what A2.6 forbids — the fact
                // that a one-byte enum happens not to tear is luck, not a
                // design. loop() applies it, one pass later, and also writes
                // it to the card: the same shape as the pending WiFi and
                // security credentials next door.
                _langReq.store(v == "fr" ? 1 : 0);
                changed = true;
            }
            if (!changed) {
                sendErr(req, 400, "no known parameter",
                                  "aucun paramètre connu"); return; }
            req->send(200, "application/json", "{\"ok\":true}");
        });

        // ---- /api/tuning: GET = everything, POST ?key=val&... = write ----
        // ---- GET /api/personalities: the live table, as the robot holds it.
        //      Serves the console's Personalities tab AND doubles as the only
        //      way to see from outside whether a card file was picked up — the
        //      loader otherwise reports on the serial line, which cannot be
        //      opened without resetting the board over native USB.
        // ---- POST /api/personalities: create or edit ONE character.
        //      The whole character travels in the query string, so the console
        //      never has to send a partial one and the robot never has to merge
        //      two half-updates. Weights come as `w=Normal:1,Happy:.6` — a
        //      declaration, so clearing an emotion is simply leaving it out.
        //
        //      A2.6: this callback touches NO card. It fills a staging slot and
        //      raises a flag; loop() writes the file and reloads the table under
        //      renderer.pause(). An SD write from AsyncTCP is the freeze this
        //      rule exists to prevent.
        route("/api/personalities", HTTP_POST, [this](AsyncWebServerRequest* req) {
            if (!req->hasParam("name")) {
                sendErr(req, 400, "the name parameter is required",
                                  "le parametre name est requis"); return;
            }
            String nm = req->getParam("name")->value();
            if (!validPersoName(nm)) {
                sendErr(req, 400,
                        "invalid name: 1-15 characters, a-z 0-9 _ - only",
                        "nom invalide : 1-15 caracteres, a-z 0-9 _ - seulement");
                return;
            }
            // The PENDING slot starts from the character being edited, so a
            // form that omits a field leaves it alone rather than zeroing it.
            sce::Personality p;
            const int known = sce::personalities::indexOf(nm.c_str());
            if (known >= 0) p = sce::personalities::at(known);
            else {
                char rules[sce::PERSO_PATH_MAX];
                snprintf(rules, sizeof(rules),
                         "/stackchan-companion/rules.%s.txt", nm.c_str());
                sce::personalities::fill(p, nm.c_str(), rules, 0u,
                                         sce::ConsoleTheme::Default, 6000, 12000);
            }
            if (req->hasParam("rules")) {
                strlcpy(p.rulesFile, req->getParam("rules")->value().c_str(),
                        sizeof(p.rulesFile));
            }
            if (req->hasParam("color"))
                p.eyeRgb = (uint32_t)strtoul(
                    req->getParam("color")->value().c_str(), nullptr, 0) & 0xFFFFFFu;
            if (req->hasParam("theme")) {
                sce::ConsoleTheme th;
                if (!sce::PersonalityYaml::themeFromName(
                        req->getParam("theme")->value().c_str(), th)) {
                    sendErr(req, 400, "unknown theme", "theme inconnu"); return;
                }
                p.theme = th;
            }
            if (req->hasParam("roulette"))
                p.roulette = req->getParam("roulette")->value().toInt() != 0;
            if (req->hasParam("min_ms"))
                p.minMs = (uint32_t)req->getParam("min_ms")->value().toInt();
            if (req->hasParam("max_ms"))
                p.maxMs = (uint32_t)req->getParam("max_ms")->value().toInt();
            if (p.maxMs < p.minMs) p.maxMs = p.minMs;
            // A CADENCE HAS TO STAY A CADENCE. Zero would make the roulette
            // draw on every Brain tick (100 Hz) — not a lively character, a
            // strobe. Clamped rather than refused: the console's slider cannot
            // reach here, but /api/tuning-style hand calls can.
            if (p.minMs < 1000) p.minMs = 1000;
            if (p.maxMs < p.minMs) p.maxMs = p.minMs;
            // Same two bounds as Brain::setTransitionScale/setPitchBiasScale —
            // clamped HERE too so what gets SAVED to the card already matches
            // what the robot will actually carry out, the same discipline as
            // the cadence floor just above. Brain's own clamp is what protects
            // a hand-edited yaml file bypassing this route entirely.
            if (req->hasParam("transition_scale"))
                p.transitionScale = clampVal(
                    req->getParam("transition_scale")->value().toFloat(), 0.3f, 3.0f);
            if (req->hasParam("pitch_bias_scale"))
                p.pitchBiasScale = clampVal(
                    req->getParam("pitch_bias_scale")->value().toFloat(), 0.0f, 2.0f);
            if (req->hasParam("w")) {
                const String w = req->getParam("w")->value();
                p.clearWeights();
                int i = 0;
                while (i < (int)w.length()) {
                    int comma = w.indexOf(',', i);
                    if (comma < 0) comma = w.length();
                    const int colon = w.indexOf(':', i);
                    if (colon > i && colon < comma) {
                        const String en = w.substring(i, colon);
                        const float v = w.substring(colon + 1, comma).toFloat();
                        const eEmotions e = emotionFromName(en.c_str());
                        // An unknown emotion is REFUSED, never skipped: a typo
                        // that silently drops a weight would give a character
                        // that quietly stops doing something.
                        if (e >= EMOTIONS_COUNT) {
                            sendErr(req, 400, "unknown emotion", "emotion inconnue"); return;
                        }
                        if (v > 0.0f) p.setWeight(e, v);
                        else          p.hasWeights = true;   // declared, empty
                    }
                    i = comma + 1;
                }
            }
            portENTER_CRITICAL(&_persoMux);
            _persoPending = p;
            _persoDelete  = false;
            portEXIT_CRITICAL(&_persoMux);
            _persoReq.store(true);
            req->send(200, "application/json", "{\"ok\":true}");
        });

        // ---- DELETE /api/personalities?name=…
        //      Personality 0 is not deletable and neither is the ACTIVE one:
        //      switch first. Removing the character the robot is wearing would
        //      leave `personality` pointing at a slot that no longer exists, and
        //      the sanitiser would silently drop it back to 0 — a deletion with
        //      a side effect nobody asked for.
        route("/api/personalities", HTTP_DELETE, [this](AsyncWebServerRequest* req) {
            if (!req->hasParam("name")) {
                sendErr(req, 400, "the name parameter is required",
                        "le parametre name est requis"); return;
            }
            const String nm = req->getParam("name")->value();
            const int idx = sce::personalities::indexOf(nm.c_str());
            if (idx < 0) { sendErr(req, 404, "unknown personality", "personnalite inconnue"); return; }
            if (!sce::personalities::deletable(idx)) {
                sendErr(req, 400,
                        "the default personality cannot be deleted",
                        "la personnalite par defaut n'est pas supprimable");
                return;
            }
            if (idx == (int)(_tuning.personality + 0.5f)) {
                sendErr(req, 400,
                        "active personality: switch away before deleting it",
                        "personnalite active : en changer avant de la supprimer");
                return;
            }
            portENTER_CRITICAL(&_persoMux);
            strlcpy(_persoPending.name, nm.c_str(), sizeof(_persoPending.name));
            _persoDelete = true;
            portEXIT_CRITICAL(&_persoMux);
            _persoReq.store(true);
            req->send(200, "application/json", "{\"ok\":true}");
        });

        route("/api/personalities", HTTP_GET, [this](AsyncWebServerRequest* req) {
            String out = "{\"active\":";
            out += String((int)(_tuning.personality + 0.5f));
            out += ",\"items\":[";
            const int n = sce::personalities::count();
            for (int i = 0; i < n; i++) {
                const sce::Personality& p = sce::personalities::at(i);
                if (i) out += ',';
                out += "{\"i\":" + String(i);
                out += ",\"name\":\"" + jesc(p.name) + "\"";
                out += ",\"rules\":\"" + jesc(p.rulesFile) + "\"";
                out += ",\"color\":" + String((unsigned long)p.eyeRgb);
                out += ",\"theme\":\"";
                out += sce::PersonalityYaml::themeName(p.theme);
                out += "\",\"roulette\":" + String(p.roulette ? 1 : 0);
                out += ",\"min_ms\":" + String((unsigned long)p.minMs);
                out += ",\"max_ms\":" + String((unsigned long)p.maxMs);
                out += ",\"tscale\":" + String(p.transitionScale, 2);
                out += ",\"pbscale\":" + String(p.pitchBiasScale, 2);
                out += ",\"del\":" + String(sce::personalities::deletable(i) ? 1 : 0);
                out += ",\"w\":{";
                bool first = true;
                if (p.hasWeights)
                    for (int e = 0; e < EMOTIONS_COUNT; e++) {
                        if (p.weights[e] <= 0.0f) continue;
                        if (!first) out += ',';
                        first = false;
                        out += "\"" + String(emotionName((eEmotions)e)) + "\":";
                        out += String(p.weights[e], 2);
                    }
                out += "}}";
            }
            out += "]}";
            req->send(200, "application/json", out);
        });

        route("/api/tuning", HTTP_GET, [this](AsyncWebServerRequest* req) {
            String s; s.reserve(2560);   // ~69 keys x ~30 chars, one alloc
            s += '{';
            for (const Tuning::Entry* e = Tuning::table(); e->key; e++) {
                if (s.length() > 1) s += ',';
                s += '"'; s += e->key; s += "\":";
                s += String(_tuning.*(e->field), 4);
            }
            s += '}';
            req->send(200, "application/json", s);
        });
        route("/api/tuning", HTTP_POST, [this](AsyncWebServerRequest* req) {
            int applied = 0;
            bool changed = false;   // only arm the SD SAVE on a REAL change: a
                                    // POST re-posting the same value (a toggle
                                    // clicked again, a probe, HA resyncing)
                                    // used to trigger config.yaml +
                                    // renderer.pause() EVERY time — SD wear and
                                    // free render freezes (diag 07-20).
            for (size_t i = 0; i < req->params(); i++) {
                auto* p = req->getParam(i);
                // Resolved through fieldFor(), which also knows the OLD names
                // (Tuning::aliases) — those are accepted here but never
                // enumerated back by the GET above.
                float Tuning::* f = Tuning::fieldFor(p->name().c_str());
                if (f) {
                    float nv = p->value().toFloat();
                    if (_tuning.*f != nv) {
                        _tuning.*f = nv;
                        changed = true;
                        // Narrates REMOTE pokes only on real changes: with
                        // several clients (console, HA, scripts) rewriting
                        // the register, "who set what, when" is the first
                        // question of every tuning mystery. DEFERRED, never
                        // printed here — see drainTuningTrace().
                        sce::trace::defer("cfg", "tuning %s=%s",
                                          p->name().c_str(),
                                          p->value().c_str());
                    }
                    applied++;
                }
            }
            if (changed) _saveReq.store(true);   // SD persistence via loop()
            char body[48];
            snprintf(body, sizeof(body), "{\"applied\":%d}", applied);
            req->send(applied ? 200 : 400, "application/json", body);
        });

        // ================= STATUS BAND (bottom of the screen) ==============
        // "sources → fields → widgets" contract. These callbacks ONLY set a
        // field (FieldStore, short lock) or a renderer flag — nothing heavy
        // (rule A2.6). The RENDERER draws (rule 1).

        // Mode of the dynamic zone: 0=none 2=sound 3=gauges. (The former
        // mode 1, debug, is gone: emotion·ip info is now the band_debug option.)
        route("/api/statusbar", HTTP_POST, [this](AsyncWebServerRequest* req) {
            if (!req->hasParam("mode")) {
                sendErr(req, 400,
                        "the mode parameter is required (0, 2, 3, 4 or 5)",
                        "paramètre mode requis (0, 2, 3, 4 ou 5)"); return; }
            int m = req->getParam("mode")->value().toInt();
            if (m != 0 && m != 2 && m != 3 && m != 4 && m != 5) {
                sendErr(req, 400,
                        "invalid mode (0=default, 2=sound, 3=gauges, "
                        "4=timer, 5=pomodoro)",
                        "mode invalide (0=défaut, 2=vumètre, 3=jauges, "
                        "4=minuteur, 5=pomodoro)"); return; }
            // NO direct renderer call: loop() is the ONLY applier (it
            // sanitizes and pushes band_mode→setStatusBar on every iteration,
            // ~10 ms — writing the Tuning field is enough, rule 4).
            _tuning.band_mode = (float)m;       // PERSISTED (survives a reboot)
            _saveReq.store(true);
            req->send(200, "application/json", "{\"ok\":true}");
        });

        // ------------------------------------------------------------------
        // TIMER / POMODORO — the band's two interactive modes, driven from
        // the console as well as from the robot (user 08-04). Everything the
        // finger can do on the band is here: set a duration, start, pause,
        // acknowledge a ring, reset.
        //
        // A2.6: this callback touches NOTHING. It fills a small command slot
        // that loop() drains — BandTimer is loop()'s, and an AsyncTCP thread
        // mutating a state machine that loop() is stepping is exactly the
        // race this rule exists to forbid. One slot is enough: these are
        // human-paced actions, and a second press arriving inside the same
        // 10 ms tick would be a double-click nobody made.
        // ------------------------------------------------------------------
        route("/api/timer", HTTP_POST, [this](AsyncWebServerRequest* req) {
            if (!req->hasParam("action")) {
                sendErr(req, 400,
                        "the action parameter is required "
                        "(tap, reset or set)",
                        "paramètre action requis (tap, reset ou set)"); return; }
            const String a = req->getParam("action")->value();
            if (a == "tap")        _timerCmd.store(1);
            else if (a == "reset") _timerCmd.store(2);
            else if (a == "set") {
                // A duration is set in MINUTES and SECONDS, both bounded the
                // same way the band's own gesture bounds them (0-99, 0-59):
                // one definition of what a valid timer is, not two.
                int m = req->hasParam("m") ? req->getParam("m")->value().toInt() : -1;
                int s = req->hasParam("s") ? req->getParam("s")->value().toInt() : -1;
                if (m < 0 || m > 99 || s < 0 || s > 59) {
                    sendErr(req, 400,
                            "set needs m (0-99) and s (0-59)",
                            "set demande m (0-99) et s (0-59)"); return; }
                _timerSetM.store(m);
                _timerSetS.store(s);
                // `start=1` sets AND starts in ONE command, and that is a
                // correctness requirement rather than a convenience: the slot
                // holds a single command, so two rapid POSTs could see the
                // second overwrite the first before loop() drained it. The
                // console's preset buttons are exactly that pattern.
                const bool go = req->hasParam("start") &&
                                req->getParam("start")->value() != "0";
                _timerCmd.store(go ? 4 : 3);
            } else {
                sendErr(req, 400, "unknown action",
                                  "action inconnue"); return; }
            req->send(200, "application/json", "{\"ok\":true}");
        });

        // Writes blackboard FIELDS: /api/field?ctx=62&g0l=SCE&...
        // a numeric value → float; a non-numeric one → string. `<key>_s`
        // forces the string form (e.g. g0r_s=1h24). Any source (script, HA, BLE).
        route("/api/field", HTTP_POST, [this](AsyncWebServerRequest* req) {
            if (!_fields) {
                sendErr(req, 503, "no fieldstore",
                                  "fieldstore absent"); return; }
            int n = 0;
            for (size_t i = 0; i < req->params(); i++) {
                auto* p = req->getParam(i);
                String key = p->name(), val = p->value();
                bool forceStr = key.endsWith("_s");
                if (forceStr) key = key.substring(0, key.length() - 2);
                char* end = nullptr;
                float f = strtof(val.c_str(), &end);
                bool numeric = !forceStr && end && *end == '\0' && end != val.c_str();
                _fields->set(key.c_str(), numeric ? f : 0.0f,
                             numeric ? nullptr : val.c_str());
                n++;
            }
            char body[32]; snprintf(body, sizeof(body), "{\"set\":%d}", n);
            req->send(200, "application/json", body);
        });

        // Ephemeral notification: /api/say?text=Bonjour&ms=4000 (covers the
        // dynamic zone, then hands back to the current mode).
        route("/api/say", HTTP_POST, [this](AsyncWebServerRequest* req) {
            if (!req->hasParam("text")) {
                sendErr(req, 400, "the text parameter is required",
                                  "paramètre text requis"); return; }
            uint32_t ms = req->hasParam("ms")
                ? (uint32_t)req->getParam("ms")->value().toInt() : 4000;
            _renderer.setSay(req->getParam("text")->value().c_str(), ms);
            req->send(200, "application/json", "{\"ok\":true}");
        });

        // ---- /api/wifi?ssid=X&pass=Y: STA credentials (restart required) ----
        route("/api/wifi", HTTP_POST, [this](AsyncWebServerRequest* req) {
            if (!req->hasParam("ssid")) {
                sendErr(req, 400, "the ssid parameter is required",
                                  "paramètre ssid requis"); return; }
            // STAGE (fixed buffers): applied by loop() (applyPendingWifi),
            // never mutated from this AsyncTCP callback. Sanitize outside the
            // lock, copy into the staging area under a spinlock (against two
            // POSTs arriving back to back).
            char s[64] = "", p[96] = "";
            // `quotable`: a WPA passphrase may hold " and \ — the card escapes
            // them, and these never appear in any JSON response.
            sanitizeCred(req->getParam("ssid")->value(), s, sizeof(s), true);
            sanitizeCred(req->hasParam("pass") ? req->getParam("pass")->value()
                                               : String(""), p, sizeof(p), true);
            portENTER_CRITICAL(&_credMux);
            strlcpy(_pendSsid, s, sizeof(_pendSsid));
            strlcpy(_pendWifiPass, p, sizeof(_pendWifiPass));
            portEXIT_CRITICAL(&_credMux);
            _wifiReq.store(true);
            sendNote(req, 200, "restart to apply",
                               "redémarrer pour appliquer");
        });

        // ---- /api/security?username=X&password=Y: Basic Auth at runtime.
        //      An empty password DISABLES the protection (same as config.yaml).
        //      Already behind the SAME global middleware: if a protection is
        //      active, reaching this route proves you know the CURRENT
        //      password (no bypass); if the API is open, setting a first
        //      password is equivalent to editing the SD card. ----
        route("/api/security", HTTP_POST, [this](AsyncWebServerRequest* req) {
            // STAGE only: mutating _apiCreds + applyAuth + persistence are
            // deferred to loop() (applyPendingSecurity). Sanitize outside the
            // lock, copy into the staging area under a spinlock (against two
            // POSTs arriving back to back).
            bool uset = req->hasParam("username") &&
                        req->getParam("username")->value().length();
            char u[64] = "", p[64] = "";
            if (uset) sanitizeCred(req->getParam("username")->value(), u, sizeof(u));
            sanitizeCred(req->hasParam("password") ? req->getParam("password")->value()
                                                   : String(""), p, sizeof(p));
            portENTER_CRITICAL(&_credMux);
            _pendUserSet = uset;
            strlcpy(_pendUser, u, sizeof(_pendUser));
            strlcpy(_pendPass, p, sizeof(_pendPass));
            portEXIT_CRITICAL(&_credMux);
            _secReq.store(true);
            sendNote(req, 200, "applied immediately",
                               "appliqué immédiatement");
        });

        // ---- /api/dance?name=happy|...|stop  +  GET /api/dances ----
        route("/api/dance", HTTP_POST, [this](AsyncWebServerRequest* req) {
            String n = req->hasParam("name") ? req->getParam("name")->value() : "";
            if (n.equalsIgnoreCase("stop")) {
                _brain.post({ CmdType::AbortDance });
                req->send(200, "application/json", "{\"ok\":true}");
                return;
            }
            int idx = dances::indexOf(n.c_str());
            if (idx >= 0) {
                _brain.post({ CmdType::PlayDance, idx });
                req->send(200, "application/json", "{\"ok\":true}");
                return;
            }
            // SD choreographies (DanceStore) — static double-bank pointer,
            // safe to carry through the CommandQueue
            if (const DanceStore::Entry* d = _dances.find(n.c_str())) {
                Command c{ CmdType::PlayCustom };
                c.i = d->count; c.u = 1 /*mirrorable*/; c.ptr = d->keys;
                _brain.post(c);
                req->send(200, "application/json", "{\"ok\":true,\"custom\":true}");
                return;
            }
            sendErr(req, 404, "unknown dance (GET /api/dances)",
                              "danse inconnue (GET /api/dances)");
        });
        // ======== SD choreographies (/dances/*.csv — CHOREGRAPHIES.md §6) ===
        // List of the custom files (the console tells them apart from the
        // built-in ones).
        // ⚠ Registered BEFORE `/api/dances` (GET) — same prefix trap as
        // /api/config/reload above (`/api/dances` also matches
        // `/api/dances/*` in BackwardCompatible mode).
        route("/api/dances/files", HTTP_GET, [this](AsyncWebServerRequest* req) {
            String s = "[";
            for (int i = 0; i < _dances.count(); i++) {
                if (i) s += ',';
                s += "{\"name\":\""; s += jesc(_dances.get(i)->name);
                s += "\",\"keys\":" + String(_dances.get(i)->count) + "}";
            }
            s += "]";
            req->send(200, "application/json", s);
        });
        // MERGED list: built-in dances + SD choreographies
        route("/api/dances", HTTP_GET, [this](AsyncWebServerRequest* req) {
            String s = "[";
            const dances::Entry* t = dances::table();
            for (int i = 0; t[i].name; i++) {
                if (i) s += ',';
                s += '"'; s += t[i].name; s += '"';
            }
            for (int i = 0; i < _dances.count(); i++) {
                s += ",\""; s += jesc(_dances.get(i)->name); s += '"';
            }
            s += "]";
            req->send(200, "application/json", s);
        });
        // Upload/edit of a CSV (multipart — same A2.6 trade-off as the bins).
        // Re-uploading the same name means EDITING it. Then reload.
        route("/api/dances/file", HTTP_POST,
            [this](AsyncWebServerRequest* req) {
                uploadRelease(req);                     // end of request → release
                bool ok = _uploadOk;
                _uploadOk = false;
                if (ok) _dancesReloadReq.store(true);   // reloaded by loop()
                if (ok) sendNote(req, 200, "reloaded in ~1 s",
                                           "rechargement dans ~1 s");
                else    sendErr(req, 500, "upload failed",
                                          "échec de l'upload");
            },
            [this](AsyncWebServerRequest* req, String filename, size_t index,
                   uint8_t* data, size_t len, bool final) {
                if (!uploadOwns(req, index)) return;   // only one active upload
                if (index == 0) {
                    _uploadDenied = !uploadAuthed(req);   // auth before SD write
                    if (_uploadDenied) return;
                    if (_uploadFile) _uploadFile.close();
                    if (!filename.endsWith(".csv") || filename.indexOf("..") >= 0) return;
                    SD.mkdir(DanceStore::DIR);
                    _uploadFile = SD.open(String(DanceStore::DIR) + "/" + filename,
                                          FILE_WRITE);
                    _uploadOk   = false;
                }
                if (_uploadDenied) return;
                if (_uploadFile) {
                    _uploadFile.write(data, len);
                    if (final) { _uploadFile.close(); _uploadOk = true; }
                }
            });
        // Deletion of a choreography
        route("/api/dances/file", HTTP_DELETE, [this](AsyncWebServerRequest* req) {
            if (!req->hasParam("name")) {
                sendErr(req, 400, "the name parameter is required",
                                  "paramètre name requis"); return; }
            String n = req->getParam("name")->value();
            if (n.indexOf("..") >= 0) {
                sendErr(req, 400, "invalid name",
                                  "nom invalide"); return; }
            if (!n.endsWith(".csv")) n += ".csv";
            bool ok = SD.remove(String(DanceStore::DIR) + "/" + n);
            if (ok) _dancesReloadReq.store(true);
            if (!ok) { sendErr(req, 404, "not found", "introuvable"); return; }
            req->send(200, "application/json", "{\"ok\":true}");
        });
        // Re-read of /dances/ (deferred to loop() — rule A2.6)
        route("/api/dances/reload", HTTP_POST, [this](AsyncWebServerRequest* req) {
            if (!_sdPresent) {
                sendErr(req, 503, "no SD card",
                                  "pas de carte SD"); return; }
            _dancesReloadReq.store(true);
            sendNote(req, 202, "reloaded in ~1 s",
                               "rechargement dans ~1 s");
        });

        // Reloads the SD rules (/stackchan-companion/rules.txt) — reactive plugins.
        route("/api/rules/reload", HTTP_POST, [this](AsyncWebServerRequest* req) {
            if (!_sdPresent) {
                sendErr(req, 503, "no SD card",
                                  "pas de carte SD"); return; }
            _rulesReloadReq.store(true);
            sendNote(req, 202, "reloaded in ~1 s",
                               "rechargement dans ~1 s");
        });

        // ---- /api/servo: head remote control (§4d, user 2026-07-11) ----
        // Absolute: ?yaw=166&pitch=90; relative: ?dyaw=-10&dpitch=5;
        // duration: &ms=400. Clamped to the K151 limits by ServoMotion;
        // ignored during a dance/pickup (the Brain arbitrates).
        // ---- GET /api/servo/pos: the MEASURED pose, not the commanded one.
        //      Registered before /api/servo out of A2.19 DISCIPLINE, but be
        //      precise about what that buys here: the swallowing A2.19 describes
        //      is per-METHOD, and this pair is GET against POST, so the router
        //      would not confuse them either way — and `checkRouteOrder()`
        //      skips the pair for exactly that reason (`methodsOverlap`). The
        //      first version of this comment claimed the boot guard verified
        //      the order; it never looked at it (review 08-02). Keep the order
        //      anyway: the day a POST /api/servo/pos is added, it matters.
        //      Only samples while `servos = 0` (the bus is write-only in
        //      operation, finding 07-21), so `valid` is false the rest of the
        //      time and says why rather than returning a stale number. ----
        // ---- GET /api/rules: what is ACTUALLY loaded ----
        // Registered AFTER `/api/rules/reload` (A2.19: the router runs in
        // BackwardCompatible mode and a prefix registered first swallows its
        // own sub-paths). `checkRouteOrder()` denounces the mistake at boot,
        // but the order is the fix.
        //
        // WHY IT EXISTS: reload answered 202 and said nothing about the
        // result. A line that failed to parse is simply ABSENT - no error, no
        // log - and the only way to find out was to watch the robot not react.
        // This lists what survived parsing, so the file and the engine can be
        // compared by looking.
        route("/api/rules", HTTP_GET, [this](AsyncWebServerRequest* req) {
            if (!_ruleEng) { req->send(200, "application/json",
                                       "{\"builtins\":0,\"rules\":[]}"); return; }
            // CHUNKED, because the table is up to 24 rules of ~120 bytes and a
            // stack buffer for the worst case would be 3 kB inside an AsyncTCP
            // callback. The response is built one rule per call.
            const int n = _ruleEng->count();
            // THE CURSOR BELONGS TO THE RESPONSE, not to the server. As a
            // member it was shared by every client: two browsers asking at once
            // - one polling console and one curl is enough - would advance the
            // same counter and each would receive half a list, interleaved into
            // JSON neither could parse. A `shared_ptr` captured by the lambda
            // lives and dies with the response object, which is what "per
            // request state" means here.
            auto cur = std::make_shared<int>(-1);
            auto* resp = req->beginChunkedResponse("application/json",
                [this, n, cur](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
                    // `index` is the byte offset already sent, which says
                    // nothing about WHICH rule comes next - hence the cursor.
                    // It is reset on the first call (index == 0) so a retried
                    // response does not resume half way.
                    if (index == 0) *cur = -1;
                    // 448 and not 224: `snprintf` returns the length it
                    // WOULD have written, and the copy below trusted it. Once
                    // rules carried a description the payload outgrew the
                    // buffer, `w` came back at ~300 with only 223 bytes in
                    // `tmp`, and the memcpy read off the end of the stack —
                    // the server stopped answering at all.
                    //
                    // Re-derived when the other three strings started being
                    // escaped too, since escaping can DOUBLE a length and the
                    // old sum counted two of them raw. Worst case now, and it
                    // is worth being able to check: 70 of literal punctuation,
                    // 63 + 63 for the escaped `en` and `f`, 95 for the escaped
                    // action, 95 for the escaped description, 2 for `op`, 11
                    // for `%.3f`, 10 + 10 for the two `%lu`, 5 + 5 for the two
                    // booleans, 1 leading comma and 2 of tail = 432, plus the
                    // NUL = 433. Fifteen bytes spare, and every term above is
                    // a buffer size on the next line rather than a guess.
                    char tmp[448];
                    int  w = 0;
                    if (*cur < 0) { w = snprintf(tmp, sizeof(tmp),
                            "{\"builtins\":%d,\"rules\":[", _ruleBuiltins); }
                    else if (*cur >= n) { return 0; }
                    else {
                        const RuleEngine::Rule& r = _ruleEng->at(*cur);
                        // EVERY STRING HERE COMES FROM rules.txt, so every one
                        // of them is escaped. `d` alone used to be, and the
                        // other three were emitted raw — a discipline applied
                        // to one member of a set it had to cover completely.
                        // `en` and `f` are interned VERBATIM from the line's
                        // `|`-separated tokens (RuleStore::parseLine trims and
                        // interns; nothing strips a quote), and `act` embeds
                        // `setKey`, interned the same way. One `"` in a field
                        // name therefore closed the string early and made the
                        // WHOLE document unparseable — so the console's rules
                        // table rendered empty and a typo in one rule silently
                        // blanked all of them, the exact failure the comment on
                        // jsonSafe() describes for control characters.
                        //
                        // Escaping and not validating, deliberately: field
                        // names are resolved at RUNTIME against FieldStore
                        // (`getF(name, default)`), sources register as they
                        // come up, and a rule may legitimately name a field
                        // published later. A list of "valid" names checked here
                        // would be a second list drifting from the real
                        // sources, which is the assumed-twin A2.23 forbids.
                        // The serialiser cannot rely on an upstream invariant
                        // it cannot see; it can always make its own output
                        // well-formed.
                        //
                        // FOUR BUFFERS AND NOT ONE. The evaluation order of
                        // function arguments is unspecified in C++, so a shared
                        // scratch buffer would have these four calls clobber
                        // each other in whatever order the compiler chose —
                        // a bug that reads as "the wrong name in the wrong
                        // column" and moves when anything nearby changes.
                        char act[40], dsc[96], enb[64], fld[64], actEsc[96];
                        ruleActionName(r, act, sizeof(act));
                        w = snprintf(tmp, sizeof(tmp),
                            "%s{\"en\":\"%s\",\"f\":\"%s\",\"op\":\"%s\","
                            "\"v\":%.3f,\"sus\":%lu,\"cd\":%lu,"
                            "\"act\":\"%s\",\"on\":%s,\"sd\":%s,"
                            "\"d\":\"%s\"}%s",
                            *cur ? "," : "",
                            jsonSafe(r.enableKey, enb, sizeof(enb)),
                            jsonSafe(r.field,     fld, sizeof(fld)),
                            opName(r.op), r.value,
                            (unsigned long)r.sustainMs, (unsigned long)r.cooldownMs,
                            jsonSafe(act, actEsc, sizeof(actEsc)),
                            _ruleEng->gateOpen(*cur) ? "true" : "false",
                            (*cur >= _ruleBuiltins) ? "true" : "false",
                            jsonSafe(r.desc, dsc, sizeof(dsc)),
                            (*cur == n - 1) ? "]}" : "");
                    }
                    (*cur)++;
                    if (w < 0) return 0;
                    // CLAMP TO WHAT IS ACTUALLY IN `tmp`, first and always.
                    // Truncation here would emit invalid JSON, which is why
                    // the buffer is sized so it cannot happen — but a silent
                    // out-of-bounds read is a worse way to find that out than
                    // a short response.
                    if (w >= (int)sizeof(tmp)) w = (int)sizeof(tmp) - 1;
                    if ((size_t)w > maxLen)    w = (int)maxLen;
                    memcpy(buf, tmp, (size_t)w);
                    return (size_t)w;
                });
            req->send(resp);
        });
        route("/api/servo/pos", HTTP_GET, [this](AsyncWebServerRequest* req) {
            // ONE read of the stamp, and everything derived from that copy.
            // Reading `_pose->stampMs` three times across a `millis()` call let
            // loop() zero it in between — the endpoint then answered
            // `"valid":true,"ageMs":412563`, i.e. the whole uptime, from the one
            // field whose entire purpose is stating freshness (review 08-02).
            // The angles are also emitted only when the stamp is good: leaving
            // the last pair in the JSON while `valid` says false contradicted
            // this handler's own comment.
            const uint32_t st  = _pose ? _pose->stampMs : 0;
            const bool     off = _tuning.servos < 0.5f;
            const bool     ok  = st && off;
            char body[176];
            snprintf(body, sizeof(body),
                     "{\"valid\":%s,\"yaw\":%.1f,\"pitch\":%.1f,"
                     "\"ageMs\":%lu,\"servosOff\":%s,"
                     "\"cmdYaw\":%.1f,\"cmdPitch\":%.1f}",
                     ok ? "true" : "false",
                     ok ? _pose->yaw   : -1.0f,
                     ok ? _pose->pitch : -1.0f,
                     (unsigned long)(st ? millis() - st : 0),
                     off ? "true" : "false",
                     _brain.headYawDeg(), _brain.headPitchDeg());
            req->send(200, "application/json", body);
        });

        route("/api/servo", HTTP_POST, [this](AsyncWebServerRequest* req) {
            bool rel = req->hasParam("dyaw") || req->hasParam("dpitch");
            Command c{ CmdType::MoveHead };
            c.i = rel ? 1 : 0;
            c.u = req->hasParam("ms")
                ? (uint32_t)req->getParam("ms")->value().toInt() : 400;
            if (rel) {
                c.f  = req->hasParam("dyaw")
                     ? req->getParam("dyaw")->value().toFloat() : 0.0f;
                c.f2 = req->hasParam("dpitch")
                     ? req->getParam("dpitch")->value().toFloat() : 0.0f;
            } else {
                c.f  = req->hasParam("yaw")
                     ? req->getParam("yaw")->value().toFloat()
                     : _brain.headYawDeg();
                c.f2 = req->hasParam("pitch")
                     ? req->getParam("pitch")->value().toFloat()
                     : _brain.headPitchDeg();
            }
            _brain.post(c);
            char body[96];
            snprintf(body, sizeof(body),
                     "{\"ok\":true,\"yaw\":%.1f,\"pitch\":%.1f}",
                     _brain.headYawDeg(), _brain.headPitchDeg());
            req->send(200, "application/json", body);
        });

        // ================= API [Bins] (§3.6) =================
        // FLASHING is deferred to loop(). So, since 08-03, are the two
        // LISTINGS — see the block below, which is the reference explanation
        // for GET /api/bins AND GET /api/sd/list.
        //
        // ---------------------------------------------------------------
        // HOW A LISTING GETS OFF THE AsyncTCP TASK (audit 08-03)
        // ---------------------------------------------------------------
        // These routes cannot be deferred the way a WRITE is (raise a flag,
        // answer 202, let loop() do it): the card's content IS their response
        // body, so something has to answer AFTER loop() has read the card. And
        // they cannot borrow the remedy used everywhere else either —
        // `renderer.pause()` WAITS for the end-of-frame ack (up to 500 ms of
        // vTaskDelay, engine/Renderer.h), and blocking the AsyncTCP task for a
        // third of a second is a worse bug than the SPI2 contention it would
        // be protecting against.
        //
        // THE MECHANISM USED: request continuation (`request->pause()`,
        // ESPAsyncWebServer 3.11, examples RequestContinuation*). The handler
        // does NO card access at all: it claims one job slot, keeps the weak
        // pointer `pause()` hands back and returns. loop() — where the
        // renderer IS paused, A2.16 — reads the card, builds the body in
        // PSRAM (rule 18) and calls `send()` on the resumed request. What
        // stays in the AsyncTCP task afterwards is the response filler, and it
        // is a `memcpy` out of that buffer, nothing else.
        //
        // WHY NOT `beginChunkedResponse` + RESPONSE_TRY_AGAIN, the shape one
        // reaches for first: the FILLER RUNS ON THE AsyncTCP TASK TOO (it is
        // called from `_onAck`/`_onPoll` → `AsyncAbstractResponse::_ack`), so
        // reading the card there would move the fault, not fix it. And a
        // filler that answers "retry" while waiting for loop() DIES: the
        // library grants a response 2 in-flight credits and gives one back
        // only for ACKED BYTES — polls give none — so after ~2 empty rounds
        // `write_send_buffs` returns 0 for good and the connection hangs
        // delivering nothing. That is the same trap already documented on
        // /api/camera/stream, which is why that route refuses to open before
        // it holds a frame.
        //
        // ONE SLOT, and a second caller gets 503 (see beginSdListing): two
        // listings in flight would need two PSRAM buffers and two paused
        // requests to keep straight, for a console that asks one at a time.
        // The slot is normally held for ONE loop() pass — but "normally" was
        // being read as "always", and it is not: `launcher.run()` and the
        // deferred flash block loop() for as long as a human takes. What
        // bounds the slot is therefore explicit, not assumed — a loop()
        // heartbeat that forbids arming, a deadline reaped by the AsyncTCP
        // task, and a disconnect that frees the slot itself. All three are
        // documented at the slot's declaration (audit 08-03).
        //
        // CLIENT GONE IN THE MIDDLE — two cases, both closed:
        //   · before we answer → `onDisconnect` sets the job's `dead` flag and
        //     the weak pointer expires; loop() drops the job and frees the
        //     slot without ever touching the card's data. Nothing is leaked:
        //     the PSRAM buffer is not allocated until we are about to send.
        //   · while the body is going out → the buffer belongs to the response
        //     through a `shared_ptr`, and the library destroys the response
        //     with the request. Same ownership as the camera snapshot.
        //
        // What was NOT structural, and was fixed first: the launch route's
        // 404, which called SD.exists() here for a question — "does this name
        // exist" — that never needed the card, only a list the card produced
        // earlier. See refreshBinsCache().

        // ---- GET /api/bins: lists /bins/*.bin ----
        // No card access here: paused, answered by loop(). See above.
        route("/api/bins", HTTP_GET, [this](AsyncWebServerRequest* req) {
            beginSdListing(req, SdJobKind::Bins);
        });

        // ---- DELETE /api/bins?name=X ----
        route("/api/bins", HTTP_DELETE, [this](AsyncWebServerRequest* req) {
            if (!req->hasParam("name")) {
                sendErr(req, 400, "the name parameter is required",
                                  "paramètre name requis"); return; }
            String path = "/bins/" + req->getParam("name")->value();
            if (path.indexOf("..") >= 0) {
                sendErr(req, 400, "invalid name", "nom invalide"); return; }
            bool ok = SD.remove(path);
            if (ok) binsChanged();   // the name cache is stale now
            if (!ok) { sendErr(req, 404, "not found", "introuvable"); return; }
            req->send(200, "application/json", "{\"ok\":true}");
        });

        // ---- POST /api/bins/launch and /api/bins/stop — registered BEFORE
        //      POST /api/bins (upload): same prefix trap as
        //      /api/config/reload (see above) — otherwise /api/bins/launch
        //      and /api/bins/stop are swallowed by the upload handler and
        //      always answer 500 "upload" (bug found 2026-07-12; these two
        //      routes had NEVER answered correctly). ----

        // ---- POST /api/bins/launch?name=X: deferred flash (202) ----
        route("/api/bins/launch", HTTP_POST, [this](AsyncWebServerRequest* req) {
            if (!req->hasParam("name")) {
                sendErr(req, 400, "the name parameter is required",
                                  "paramètre name requis"); return; }
            String n = req->getParam("name")->value();
            if (n.indexOf("..") >= 0) {
                sendErr(req, 400, "invalid name",
                                  "nom invalide"); return; }
            // A NAME THE CACHE CANNOT HOLD IS SAID SO, not answered "not
            // found": the file may well be on the card, and 404 on a name the
            // console just listed sends the user hunting for the wrong fault.
            if (n.length() >= BIN_NAME_MAX) {
                Serial.printf("[api] launch refuse : '%s' fait %u car., le "
                              "lanceur API en accepte %u\n", n.c_str(),
                              (unsigned)n.length(), (unsigned)(BIN_NAME_MAX - 1));
                sendErr(req, 400, "name too long for the launcher",
                                  "nom trop long pour le lanceur"); return; }
            // NAME LOOKUP, no card. See _binBank/refreshBinsCache: this used to be
            // an SD.exists() in an AsyncTCP callback (A2.6 + A2.16). The bank
            // index is read ONCE, with an acquire load paired with the release
            // store in refreshBinsCache() — the table cannot change under this
            // loop, and it is never the table loop() is rewriting.
            const BinBank& bank = _binBank[_binBankIdx.load(std::memory_order_acquire)];
            bool known = false;
            for (int i = 0; i < bank.n && !known; i++)
                known = (n == bank.name[i]);
            if (!known) {
                Serial.printf("[api] launch : '%s' absent du cache /bins "
                              "(%d nom(s) en cache)\n", n.c_str(), bank.n);
                sendErr(req, 404, "not found", "introuvable"); return; }
            snprintf(_launchPath, sizeof(_launchPath), "/bins/%s", n.c_str());
            _launchReq.store(true);
            sendNote(req, 202, "flash + reboot in ~2 s",
                               "flash + redémarrage dans ~2 s");
        });

        // ---- POST /api/bins/stop: documented no-op (§3.6 — remotely stopping
        // a third-party .bin requires the SceGuest stub; here we are ALREADY
        // running the companion) ----
        route("/api/bins/stop", HTTP_POST, [](AsyncWebServerRequest* req) {
            sendNote(req, 200, "already running the companion",
                               "déjà sur le companion");
        });

        // ---- POST /api/bins (multipart upload): adds /bins/<name>.bin ----
        // Streamed to the SD card chunk by chunk from the AsyncWebServer
        // handler.
        // ⚠ DELIBERATE EXCEPTION to rule A2.6 (review 2026-07-12): writing to
        // the SD card from AsyncTCP is safe bus-wise (spi_bus_lock serializes
        // with the LCD) but can SLOW DOWN the renderer during the upload — a
        // rare and explicit operation, and streaming through loop() would cost
        // a queue of chunks.
        route("/api/bins", HTTP_POST,
            [this](AsyncWebServerRequest* req) {           // final response
                uploadRelease(req);                        // end of request → release
                bool ok = _uploadOk;
                _uploadOk = false;
                if (!ok) { sendErr(req, 500, "upload failed",
                                             "échec de l'upload"); return; }
                req->send(200, "application/json", "{\"ok\":true}");
            },
            [this](AsyncWebServerRequest* req, String filename, size_t index,
                   uint8_t* data, size_t len, bool final) {
                if (!uploadOwns(req, index)) return;        // only one active upload
                if (index == 0) {                          // start
                    _uploadDenied = !uploadAuthed(req);     // auth before SD write
                    if (_uploadDenied) return;
                    // Previous upload interrupted (connection dropped before
                    // `final`): close the leftover handle (review 07-12)
                    if (_uploadFile) _uploadFile.close();
                    if (!filename.endsWith(".bin") || filename.indexOf("..") >= 0) return;
                    SD.mkdir("/bins");
                    // ATOMIC (.tmp + validation + rename), like sd/put: an
                    // interrupted upload used to leave a TRUNCATED .bin in
                    // /bins, listed and launchable — and the launcher's guard
                    // is "size <= partition", which a truncated file passes
                    // ALL THE MORE EASILY: a dead image gets flashed and the
                    // OTA partition no longer boots (max review 07-27).
                    _uploadTarget = "/bins/" + filename;
                    SD.remove(_uploadTarget + ".tmp");
                    _uploadFile = SD.open(_uploadTarget + ".tmp", FILE_WRITE);
                    _uploadOk   = false;
                }
                if (_uploadDenied) return;
                if (_uploadFile) {
                    _uploadFile.write(data, len);
                    if (final) {
                        _uploadFile.close();
                        String tmp = _uploadTarget + ".tmp";
                        size_t expect = index + len;
                        File t = SD.open(tmp, FILE_READ);
                        uint8_t magic = 0; size_t sz = 0;
                        if (t) { sz = t.size(); t.read(&magic, 1); t.close(); }
                        // Bytes received == bytes written AND a plausible
                        // ESP32 image (same thresholds as
                        // SceGuest::companionImageOk)
                        if (sz == expect && sz >= 262144 && magic == 0xE9) {
                            SD.remove(_uploadTarget);
                            _uploadOk = SD.rename(tmp, _uploadTarget);
                            if (_uploadOk) binsChanged();
                        } else {
                            SD.remove(tmp);
                            _uploadOk = false;
                        }
                    }
                }
            });

        // ================= Import / Export SD ================================
        // Export = direct download (spi_bus_lock serializes with the LCD, same
        // A2.6 exception as the Bins upload). Import = multipart upload to a
        // WHITELISTED path (config.yaml, rules.txt, /dances/*.csv,
        // /bins/*.bin). config/rules → automatic reload after the import.
        // ⚠ config.yaml can hold the WiFi credentials (exporting = a potential
        //   leak if the API is not protected — see Basic Auth).
        // /api/sd/list belonged to the STRUCTURAL class of the 08-03 audit and
        // is now off the AsyncTCP task: it is PAUSED here and answered by
        // loop(). The whole mechanism — why not a chunked filler, one slot,
        // what happens when the client vanishes — is documented once, above
        // the [Bins] section. This is the heavier of the two listings (four
        // directories walked, so dozens of FAT reads in a row), and the one
        // that motivated the design.
        route("/api/sd/list", HTTP_GET, [this](AsyncWebServerRequest* req) {
            beginSdListing(req, SdJobKind::SdList);
        });

        // ---- GET /api/sd/get: file download ----------------------------
        // ⚠ THIS ONE STAYS ON THE AsyncTCP TASK, and that is a DECISION, not
        // an oversight. It is the third route of the 08-03 audit and the only
        // one not converted; the reasons, in order of weight:
        //
        //  1. Buffering it whole is not an option. A .bin here is 1.7 MB. Rule
        //     18 sends big buffers to PSRAM, but PSRAM is also where the
        //     camera framebuffers and every JSON parse live, and the ONE file
        //     that must never fail to download is /companion.bin — the only
        //     way back from a guest bin. Making the recovery path depend on a
        //     1.7 MB allocation succeeding is trading a latency defect for a
        //     bricking defect.
        //  2. Block-by-block streaming driven by loop() (double buffer, hand
        //     over at each block) IS the correct answer and it is a real piece
        //     of design: a state machine, two buffers, a restart position, and
        //     a failure mode — a stalled producer — that ends in a half
        //     firmware image. On the file this robot is recovered with, that
        //     is not something to land in the same sitting as the listings.
        //  3. And it is by far the least harmful of the three. The library
        //     reads the file through `AsyncFileResponse::_fillBuffer`, i.e.
        //     ONE sequential `File::read` of at most ASYNC_RESPONCE_BUFF_SIZE
        //     (2×MSS ≈ 2.8 KB) per callback — a bounded read on an already
        //     open handle. The listings, which ARE converted, did something
        //     else entirely: an `SD.open` per directory plus one
        //     `openNextFile` per entry, dozens of FAT metadata seeks inside a
        //     single callback.
        //
        // So: documented, bounded, and left. What IS fixed here is the double
        // card access — `SD.exists()` followed by the library opening the file
        // again. One `SD.open` answers both questions, and a falsy handle is a
        // truthful 404 (it also stops an empty-but-present file from being
        // reported as absent, which the FS overload does: it treats
        // `!available()` as "try <path>.gz" and 404s when that is missing).
        route("/api/sd/get", HTTP_GET, [](AsyncWebServerRequest* req) {
            if (!req->hasParam("path")) {
                sendErr(req, 400, "the path parameter is required",
                                  "paramètre path requis"); return; }
            String p = req->getParam("path")->value();
            if (!sdPathAllowed(p)) {
                sendErr(req, 403, "path not allowed",
                                  "chemin interdit"); return; }
            File f = SD.open(p, FILE_READ);
            if (!f || f.isDirectory()) {
                if (f) f.close();
                sendErr(req, 404, "file not found",
                                  "fichier absent"); return; }
            // The response takes the handle over (it closes it on destruction,
            // including when the client hangs up mid-download).
            req->send(f, p, "application/octet-stream", true); // download=attachment
        });

        route("/api/sd/put", HTTP_POST,
            [this](AsyncWebServerRequest* req) {
                uploadRelease(req);
                bool ok = _uploadOk; _uploadOk = false;
                if (ok) {   // reload the bank concerned (derived from the path)
                    String p = req->hasParam("path") ? req->getParam("path")->value() : String();
                    if      (p == "/stackchan-companion/config.yaml") _reloadReq.store(true);
                    // ANY rule file, not just the default one: a personality
                    // owns its own (rules.haro.txt…), and uploading it from the
                    // console has to take effect the same way rules.txt does.
                    // Matching on the prefix rather than listing the names
                    // keeps this from needing an edit every time a personality
                    // is added — the applier decides which file is live.
                    else if (p.startsWith("/stackchan-companion/rules"))
                        _rulesReloadReq.store(true);
                    else if (p.startsWith("/dances/"))           _dancesReloadReq.store(true);
                    // AND THE /bins NAME CACHE, which this route never
                    // told (review 08-03). sdPathAllowed() lets
                    // /bins/*.bin through here, so a guest uploaded from
                    // the console file manager was LISTED by GET
                    // /api/bins - rebuilt from the card - while
                    // POST /api/bins/launch, which validates against the
                    // cache, answered 404 for a file shown with a Launch
                    // button on the same page.
                    else if (p.startsWith("/bins/"))             binsChanged();
                }
                if (!ok && _uploadDenied) {
                    sendErr(req, 403, "path not allowed, or authentication",
                                      "chemin interdit ou authentification");
                    return;
                }
                if (!ok) { sendErr(req, 500, "import failed",
                                             "échec de l'import"); return; }
                req->send(200, "application/json", "{\"ok\":true}");
            },
            [this](AsyncWebServerRequest* req, String filename, size_t index,
                   uint8_t* data, size_t len, bool final) {
                if (!uploadOwns(req, index)) return;
                if (index == 0) {
                    _uploadDenied = !uploadAuthed(req);
                    if (_uploadDenied) return;
                    if (_uploadFile) _uploadFile.close();
                    String p = req->hasParam("path") ? req->getParam("path")->value() : String();
                    if (!sdPathAllowed(p)) { _uploadDenied = true; return; }
                    // /companion.bin = the ONLY way back from a guest bin:
                    // ATOMIC write through .tmp + validation (magic 0xE9,
                    // minimum size) then rename — an interrupted upload must
                    // never corrupt the existing file (07-26).
                    _uploadCompanion = (p == "/companion.bin");
                    // EVERY sd/put writes a .tmp then renames at the end: an
                    // interrupted upload NEVER leaves a truncated target file
                    // (a truncated config.yaml means WiFi credentials lost
                    // silently — max review 07-26).
                    _uploadTarget = p;
                    p += ".tmp";
                    SD.remove(p);                    // purge an orphan .tmp
                    int sl = p.lastIndexOf('/');
                    if (sl > 0) SD.mkdir(p.substring(0, sl));
                    _uploadFile = SD.open(p, FILE_WRITE);
                    _uploadOk = false;
                }
                if (_uploadDenied) return;
                if (_uploadFile) {
                    _uploadFile.write(data, len);
                    if (final) {
                        _uploadFile.close();
                        String tmp = _uploadTarget + ".tmp";
                        // Size written == bytes received (catches a short
                        // write on a full SD card — a truncated file >256 KB
                        // starting with 0xE9 used to pass validation, max
                        // review 07-26).
                        size_t expect = index + len;
                        File t = SD.open(tmp, FILE_READ);
                        uint8_t magic = 0; size_t sz = 0;
                        if (t) { sz = t.size(); t.read(&magic, 1); t.close(); }
                        bool sane = (sz == expect) && sz > 0;
                        // ⚠ KEEP IN STEP with SceGuest::companionImageOk
                        // (same 0xE9/256 KB thresholds — SceGuest is
                        // standalone, copied into third-party bins: no
                        // factoring out possible)
                        if (sane && _uploadCompanion)
                            sane = (magic == 0xE9 && sz >= 262144);
                        if (!sane) {
                            SD.remove(tmp);
                            _uploadOk = false;
                        } else if (_uploadCompanion) {
                            // .old dance: the old safety net SURVIVES a failed
                            // rename (the former remove→rename left a window
                            // with NO companion.bin at all).
                            SD.remove("/companion.old");
                            SD.rename("/companion.bin", "/companion.old");
                            _uploadOk = SD.rename(tmp, "/companion.bin");
                            if (!_uploadOk)          // restore the old one
                                SD.rename("/companion.old", "/companion.bin");
                            else
                                SD.remove("/companion.old");
                        } else {
                            SD.remove(_uploadTarget);
                            _uploadOk = SD.rename(tmp, _uploadTarget);
                        }
                        _uploadCompanion = false;
                    }
                }
            });

        route("/api/sd/delete", HTTP_DELETE, [this](AsyncWebServerRequest* req) {
            if (!req->hasParam("path")) {
                sendErr(req, 400, "the path parameter is required",
                                  "paramètre path requis"); return; }
            String p = req->getParam("path")->value();
            // /companion.bin: downloadable and REPLACEABLE but NEVER
            // deletable — without it, neither a remote stop nor BtnA-at-boot
            // brings the companion back (robot stuck on the guest, review
            // 07-26).
            if (p == "/companion.bin" || !sdPathAllowed(p)) {
                sendErr(req, 403, "path not allowed",
                                  "chemin interdit"); return; }
            bool ok = SD.exists(p) && SD.remove(p);
            // THE NAME CACHE OUTLIVES THE FILE otherwise (review 08-03):
            // a deleted /bins/x.bin stayed in the name bank, so launch validated
            // it and loop() ran updateFromFS on a file that is gone.
            if (p.startsWith("/bins/")) binsChanged();
            if (!ok) { sendErr(req, 404, "file not found",
                                         "fichier absent"); return; }
            req->send(200, "application/json", "{\"ok\":true}");
        });

        // ---- POST /api/update (multipart upload): OTA of the companion
        //      firmware ITSELF (user 2026-07-16). Writes the next OTA
        //      partition chunk by chunk (Update.h — same mechanism as the SD
        //      launcher), then a DEFERRED REBOOT consumed by loop() (the HTTP
        //      response must go out before the restart).
        //      ⚠ Same A2.6 exception as the [Bins] upload. During the flash
        //      the renderer switches to BUSY mode (a breathing "…" screen —
        //      user 2026-07-16): the OTA does not use the SD card, so there is
        //      no SPI2 contention (A2.16 does not apply) — only brief cache
        //      stalls during the flash writes. Safety net: update() re-arms
        //      the renderer if the upload dies without `final` (10 s). ----
        route("/api/update", HTTP_POST,
            [this](AsyncWebServerRequest* req) {           // final response
                uploadRelease(req);                        // end of request → release
                bool ok = _updateOk;
                _updateOk = false;
                if (ok) {
                    _rebootAtMs.store(millis() + 1200);    // the 200 goes out first
                    sendNote(req, 200, "flash ok - rebooting in ~1 s",
                                       "flash ok - redémarrage dans ~1 s");
                } else {
                    _renderer.setBusy(false);
                    // NOT translated: `Update.errorString()` is the library's
                    // own English text, and a French prefix glued to an English
                    // payload reads worse than a consistently English line.
                    char body[96];
                    snprintf(body, sizeof(body), "{\"error\":\"update: %s\"}",
                             Update.errorString());
                    req->send(500, "application/json", body);
                }
            },
            [this](AsyncWebServerRequest* req, String filename, size_t index,
                   uint8_t* data, size_t len, bool final) {
                if (!uploadOwns(req, index)) return;       // only one active upload
                if (index == 0) {                          // start
                    _updateOk = false;
                    // Auth BEFORE any side effect (the middleware only runs
                    // after the body — otherwise an unauthenticated flash).
                    _uploadDenied = !uploadAuthed(req);
                    if (_uploadDenied) return;
                    if (!filename.endsWith(".bin")) return;
                    // (the waiting screen is already armed by `uploadOwns`,
                    //  which covers the four upload routes from one place)
                    // Rendezvous: wait for a camera capture ALREADY under way
                    // to finish (the inhibit set by uploadOwns only covers
                    // FUTURE captures) before the flash writes (07-21).
                    if (_camera) _camera->waitCaptureIdle();
                    if (Update.isRunning()) Update.abort();
                    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                        Serial.printf("[api] OTA begin: %s\n", Update.errorString());
                        _renderer.setBusy(false);
                        return;
                    }
                    Serial.printf("[api] OTA: %s\n", filename.c_str());
                }
                if (_uploadDenied) return;
                if (!Update.isRunning()) return;
                _otaLastMs = millis();
                if (Update.write(data, len) != len) {
                    Serial.printf("[api] OTA write: %s\n", Update.errorString());
                    Update.abort();
                    return;
                }
                if (final) {
                    _updateOk = Update.end(true);
                    Serial.printf("[api] OTA end: %s (%lu o)\n",
                                  _updateOk ? "ok" : Update.errorString(),
                                  (unsigned long)(index + len));
                }
            });

        // ---- GET /api/camera/still.jpg: JPEG snapshot (GC0308).
        //      Off by default (tuning camera=1); the camera is initialised ON
        //      DEMAND (never at boot, A2.20). The framebuffer is copied into
        //      PSRAM and given back IMMEDIATELY (there is a single fb); the
        //      copy lives inside the response (captured shared_ptr → freed
        //      when the response is destroyed, including if the client
        //      disconnects). ----
        route("/api/camera/still.jpg", HTTP_GET,
                   [this](AsyncWebServerRequest* req) {
            if (_tuning.camera < 0.5f || !_camera) {
                // text/plain, not JSON: this endpoint answers an <img>, so the
                // body is only ever read by a human debugging it. The tuning
                // KEY inside the sentence stays as it is — it is a config key.
                req->send(403, "text/plain",
                          sce::T("camera disabled (POST /api/tuning?camera=1)",
                                 "caméra désactivée (POST /api/tuning?camera=1)"));
                return;
            }
            // The latest JPEG is captured by loop() (Camera::service) — the
            // handler only reads it (never a blocking capture here).
            // ?full=1: full-quality SNAPSHOT (cam_quality) — arms a FULL
            // capture by the task then reads latestSnapshot (503 until it is
            // fresh → the client retries). Otherwise: the latest LIVE frame
            // (compressed, cam_stream_quality) — responsive view/stream.
            // The VALUE is read (not just the presence of the param): ?full=0
            // must serve the live frame, as per the OpenAPI (review 07-21).
            bool full = req->hasParam("full") &&
                        req->getParam("full")->value().toFloat() >= 0.5f;
            uint8_t* jbuf = nullptr; size_t len = 0;
            bool got;
            if (full) {
                // Freshness: serve a snapshot younger than 1.5 s; otherwise
                // 503 + ARM one. The arming happens on ANY failure (not only
                // on age): after a camera=0→1 cycle, a "fresh" _snapAtMs with
                // an emptied buffer left the client in a 503 live-lock,
                // triggering neither a capture nor a re-init (review 07-21).
                // NEVER invalidate here — a retry would destroy the fresh
                // snapshot (07-20).
                got = (_camera->snapAgeMs() <= 1500) &&
                      _camera->latestSnapshot(&jbuf, &len);
                if (!got) _camera->requestSnapshot();   // also arms markWanted
            } else {
                _camera->markWanted();
                got = _camera->latestJpeg(&jbuf, &len);
            }
            if (!got) {
                req->send(503, "text/plain",
                          _camera->hasFailed()
                              ? sce::T("camera init failed",
                                       "échec de l'init caméra")
                              : sce::T("camera starting up - retry",
                                       "caméra en démarrage - réessayer"));
                return;
            }
            // The (PSRAM) buffer is owned by the response through a
            // shared_ptr: freed when the response is destroyed (send finished
            // or client disconnected).
            std::shared_ptr<uint8_t> buf(jbuf,
                [](uint8_t* p) { if (p) heap_caps_free(p); });
            AsyncWebServerResponse* r = req->beginResponse("image/jpeg", len,
                [buf, len](uint8_t* dst, size_t maxLen, size_t index) -> size_t {
                    size_t remaining = len - index;
                    size_t n = remaining < maxLen ? remaining : maxLen;
                    memcpy(dst, buf.get() + index, n);
                    return n;
                });
            r->addHeader("Cache-Control", "no-store");
            req->send(r);
        });

        // ---- GET /api/camera/stream: MJPEG stream (multipart/x-mixed-replace)
        //      for Frigate / a browser. Serves the frames captured by loop()
        //      (Camera::service) — the handler NEVER captures (rule A2.6), it
        //      re-arms markWanted() and broadcasts the latest JPEG. The rate
        //      is that of the loop() capture (~10 fps ceiling) through
        //      frameVersion: each image is served ONCE (RESPONSE_TRY_AGAIN in
        //      between). ----
        route("/api/camera/stream", HTTP_GET,
                   [this](AsyncWebServerRequest* req) {
            if (_tuning.camera < 0.5f || !_camera) {
                req->send(403, "text/plain",
                          sce::T("camera disabled", "caméra désactivée"));
                return;
            }
            _camera->markWanted();
            // Do NOT open the stream before the 1st frame: a filler that
            // answers "retry" in a loop during the init (~2 s) exhausts the
            // AsyncTCP send credit (polls do not give credit back) → a dead
            // connection delivering 0 bytes. The client (console/ffmpeg)
            // retries about 1 s later.
            if (!_camera->hasFrame()) {
                req->send(503, "text/plain",
                          sce::T("camera starting up - retry",
                                 "caméra en démarrage - réessayer"));
                return;
            }
            auto st = std::make_shared<CamStream>();
            st->cam = _camera;
            AsyncWebServerResponse* r = req->beginChunkedResponse(
                "multipart/x-mixed-replace;boundary=frame",
                [st](uint8_t* buf, size_t maxLen, size_t) -> size_t {
                    if (!st->frame) {
                        // Camera turned off (option): end the response (0 =
                        // closes the connection) — frees AsyncTCP and stops
                        // re-arming the init. Done BETWEEN two frames (never
                        // half-way through a send).
                        if (st->cam->disabled()) return 0;
                        st->cam->markWanted();
                        uint32_t now = millis();
                        // BOUNDED pacing: wait for a newer frame, otherwise
                        // re-serve the same one after reServeMs (≥300 ms,
                        // derived from cam_fps — the old fixed 300 re-served
                        // the SAME image 3x/s at a low rate: radio bytes
                        // wasted).
                        bool fresh = st->cam->frameVersion() != st->lastVer;
                        if (!fresh && (uint32_t)(now - st->lastEmitMs) <
                                          st->cam->reServeMs())
                            return RESPONSE_TRY_AGAIN;
                        uint8_t* jb = nullptr; size_t jl = 0;
                        if (!st->cam->latestJpeg(&jb, &jl)) return RESPONSE_TRY_AGAIN;
                        st->frame = jb; st->frameLen = jl;
                        st->lastVer = st->cam->frameVersion();
                        st->lastEmitMs = now;
                        st->head = String("--frame\r\nContent-Type: image/jpeg"
                                          "\r\nContent-Length: ") + jl + "\r\n\r\n";
                        st->sent = 0;
                    }
                    size_t hlen = st->head.length();
                    size_t total = hlen + st->frameLen + 2;
                    size_t out = 0;
                    while (out < maxLen && st->sent < total) {
                        size_t room = maxLen - out, s = st->sent, n;
                        if (s < hlen) {
                            n = hlen - s; if (n > room) n = room;
                            memcpy(buf + out, st->head.c_str() + s, n);
                        } else if (s < hlen + st->frameLen) {
                            size_t fo = s - hlen;
                            n = st->frameLen - fo; if (n > room) n = room;
                            memcpy(buf + out, st->frame + fo, n);
                        } else {
                            static const char crlf[2] = { '\r', '\n' };
                            size_t to = s - hlen - st->frameLen;
                            n = 2 - to; if (n > room) n = room;
                            memcpy(buf + out, crlf + to, n);
                        }
                        out += n; st->sent += n;
                    }
                    if (st->sent >= total) {
                        heap_caps_free(st->frame); st->frame = nullptr;
                    }
                    return out;
                });
            r->addHeader("Cache-Control", "no-store");
            req->send(r);
        });

        // ---- Pages: embedded console (zero CDN), Swagger, OpenAPI ----
        route("/", HTTP_GET, [](AsyncWebServerRequest* req) {
            sendConsole(req);       // page + the `lang:` of config.yaml
        });
        // Both are static and read by a browser (Swagger UI fetches the spec
        // with XHR, which inflates transparently): gzipping them is free.
        route("/swagger", HTTP_GET, [](AsyncWebServerRequest* req) {
            sendGz(req, "text/html", SWAGGER_HTML_GZ, SWAGGER_HTML_GZ_LEN);
        });
        route("/api/openapi.json", HTTP_GET, [](AsyncWebServerRequest* req) {
            sendGz(req, "application/json", OPENAPI_JSON_GZ,
                   OPENAPI_JSON_GZ_LEN);
        });

        // ---- 404 → redirect to the console (captive-portal trigger: OS
        // probes such as generate_204/hotspot-detect get a 302) ----
        _server.onNotFound([](AsyncWebServerRequest* req) {
            req->redirect("/");
        });

        // A2.19: the list has just been filled in by route() —
        // we re-read it BEFORE opening the server.
        checkRouteOrder();
        // CROSS-ORIGIN, once, and only if asked (Tuning::cors). The header list
        // is global to the server and add-only, which is why this is read here
        // and not per request: there is no removing it afterwards, so the
        // decision belongs to the boot that read the config.
        if (_tuning.cors > 0.5f) {
            DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
            // A POST with no body and no custom header is a "simple request"
            // and never preflights, so these two only matter the day something
            // sends JSON. Cheap now, and the alternative is a 405 that names
            // nothing.
            DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods",
                                                 "GET, POST, DELETE, OPTIONS");
            DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers",
                                                 "Content-Type");
            Serial.println("[api] CORS ouvert (cors=1) - toute page du reseau "
                           "peut parler au robot");
        }
        // One heartbeat before the first request can arrive: the rest of
        // setup() still runs between here and the first update(), and a client
        // that connects in that window must not be told the robot is busy.
        _loopBeatMs.store(millis(), std::memory_order_relaxed);
        _server.begin();
        return sta ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    }

    // Has an atomic deadline expired (comparison safe against millis()
    // rollover). Idiom shared by the deferred reboot and the deferred
    // shutdown.
    static bool dueAt(const std::atomic<uint32_t>& at) {
        uint32_t v = at.load();
        return v && (int32_t)(millis() - v) >= 0;
    }

    // Deferred post-OTA reboot (/api/update) — consumed by loop(): true once
    // the HTTP response has gone out and the board must restart.
    bool rebootDue() const { return dueAt(_rebootAtMs); }

    // Deferred shutdown (POST /api/poweroff) — consumed by loop(): true once
    // the HTTP response has gone out and the power must be cut.
    bool poweroffDue() const { return dueAt(_poweroffAtMs); }

    // Disarms the shutdown: M5.Power.powerOff() RETURNS when the device is on
    // USB (the AXP2101 cannot cut a rail fed by USB) — without this reset,
    // poweroffDue() would stay true and loop() would spin at ~10 Hz doing
    // delay+powerOff, starving Brain/servo/renderer.
    void clearPoweroff() { _poweroffAtMs.store(0); }

    // Applies the pending Basic Auth credentials — loop() ONLY (mutates
    // _apiCreds on the loop side, out of any race with save()). true if
    // something was applied (triggers persistence).
    bool applyPendingSecurity() {
        if (!_secReq.exchange(false)) return false;
        char u[64], p[64]; bool uset;
        portENTER_CRITICAL(&_credMux);              // atomic snapshot of the staging
        uset = _pendUserSet;
        strlcpy(u, _pendUser, sizeof(u));
        strlcpy(p, _pendPass, sizeof(p));
        portEXIT_CRITICAL(&_credMux);
        if (uset) strlcpy(_apiCreds.username, u, sizeof(_apiCreds.username));
        strlcpy(_apiCreds.password, p, sizeof(_apiCreds.password));
        applyAuth();
        _saveReq.store(true);
        return true;
    }

    // Same for the STA WiFi credentials (a restart is needed for the radio to
    // pick them up).
    bool applyPendingWifi() {
        if (!_wifiReq.exchange(false)) return false;
        char s[64], p[96];
        portENTER_CRITICAL(&_credMux);
        strlcpy(s, _pendSsid, sizeof(s));
        strlcpy(p, _pendWifiPass, sizeof(p));
        portEXIT_CRITICAL(&_credMux);
        _creds.clientSsid = s;   // _creds (wifi) = String, read only from loop()
        _creds.clientPass = p;
        _saveReq.store(true);
        return true;
    }

    // To be called from loop(): captive-portal DNS (AP mode only) + the OTA
    // safety net (upload interrupted without `final`: abort + screen restored)
    void update() {
        // PROOF OF LIFE for the AsyncTCP task (audit 08-03). loop() calls this
        // on every pass and nothing else does, so a stale stamp means loop() is
        // blocked — inside the touch launcher, or inside a flash. The listing
        // handler reads it and refuses to park a request nobody would answer;
        // see the slot's comment block, `loopStalled()`.
        _loopBeatMs.store(millis(), std::memory_order_relaxed);
        if (_apMode) _dns.processNextRequest();
        if (Update.isRunning() && millis() - _otaLastMs > 10000) {
            Serial.printf("[api] OTA: upload interrompu -> abort\n");
            Update.abort();
            _renderer.setBusy(false);
        }
        // Self-healing: an upload that dies without `final` (dropped
        // connection) leaves _uploadReq (and the camera inhibit) armed —
        // onRequest never ran. After >10 s with no chunk we release the
        // ownership AND the inhibit (otherwise the camera would stay off until
        // the next reboot). Under _upMux (see uploadOwns) with the printf
        // OUTSIDE the critical section — the old printf INSIDE the window
        // widened the race with an upload just starting (review 07-21).
        bool healed = false;
        portENTER_CRITICAL(&_upMux);
        if (_uploadReq && millis() - _uploadLastMs > 10000) {
            _uploadReq = nullptr;
            healed = true;
        }
        portEXIT_CRITICAL(&_upMux);
        if (healed) {
            Serial.printf("[api] upload interrompu -> liberation (camera reprend, "
                          "ecran degele)\n");
            if (_camera) _camera->setInhibited(false);
            // Without this, a connection dropped mid-upload left the robot
            // FROZEN until the next reboot: the freeze is armed by
            // `uploadOwns`, and `uploadRelease` never runs in that case.
            _renderer.setBusy(false);
        }
    }

private:
    // ==================================================================
    // DEFERRED SD LISTINGS — request continuation (audit 08-03)
    // ==================================================================
    // WHY, and why not a chunked filler: documented once, above GET /api/bins
    // in begin(). What follows is only the machinery.
    enum class SdJobKind : uint8_t { Bins, SdList };

    // FOUR slot states, not two. `_jobKind` and `_jobReq` are plain fields
    // written by the AsyncTCP task and read by loop(); CLAIMED is the window
    // where the slot is taken but those fields are not yet readable, and the
    // `store(ARMED, release)` is the barrier that publishes them both. With a
    // single busy flag, loop() could pick up a half-written weak pointer.
    // SERVING is the EXCLUSIVE state (added 08-03): loop() and the AsyncTCP
    // reaper both take the job by `CAS(ARMED -> SERVING)`, so exactly one of
    // them ever owns the paused request — see `reapStaleListing()`.
    static constexpr uint8_t JOB_IDLE = 0, JOB_CLAIMED = 1, JOB_ARMED = 2,
                             JOB_SERVING = 3;
    // Body budget for one listing, in PSRAM. ~100 bytes per entry, so about
    // 120 files — well past what this card is meant to carry, and bounded
    // rather than trusted: the buffer refuses to overflow (see PsBuf).
    static constexpr size_t  LISTING_BUF = 12288;

    // ---- THE PARKED REQUEST MUST NEVER OUTLIVE THE MACHINE (audit 08-03) --
    // The slot was armed on the assumption, written in this file, that "loop()
    // frees it on its very next pass". That assumption is FALSE on two paths
    // that this same firmware takes on purpose: `launcher.run()` blocks loop()
    // until a human dismisses the touch menu, and the deferred flash blocks it
    // until the board reboots. The AsyncTCP task keeps accepting requests
    // throughout — so one listing could take the only slot, park itself, and be
    // served by nobody: its browser waited for its own timeout, EVERY later
    // listing got 503 "another listing is in progress", and if the menu ended
    // in a flash the slot was never freed at all.
    //
    // A deadline alone does not fix it, because the deadline would be checked
    // by the task that is blocked. The fix is therefore in THREE parts, and all
    // three run on the AsyncTCP task — the only one still alive:
    //
    //   1. DO NOT ARM WHEN loop() IS NOT RUNNING. `update()` is called by
    //      loop() on every pass and now stamps `_loopBeatMs`; a listing that
    //      arrives more than LOOP_STALE_MS after the last stamp is refused
    //      IMMEDIATELY with 503. This turns the whole failure class into a fast
    //      honest error: while the launcher is open, listings are declined in
    //      milliseconds and every other endpoint keeps answering normally.
    //   2. DEADLINE + REAPER for the race that remains (armed one instant
    //      before loop() enters the menu): a job still ARMED after
    //      LISTING_DEADLINE_MS is taken over by the next incoming request —
    //      any request, the check sits in the server middleware — which answers
    //      the parked one with a clean 503 and frees the slot. The console
    //      polls /api/status about once a second, so in practice the reaper
    //      fires on the very next poll.
    //   3. DISCONNECT FREES THE SLOT. A client that gives up used to only set
    //      `_jobDead`, leaving the slot armed for a loop() that was not coming
    //      back. `onDisconnect` now reaps it on the spot.
    //
    // What is left open, stated rather than hidden: a client that parks a
    // listing during that one-instant race, then sends nothing else and never
    // times out, waits until loop() comes back (a human closing the menu) or
    // until the flash reboots the board and TCP drops the connection. There is
    // no timer here to do better, and adding one would mean calling into lwIP
    // from the esp_timer task — a far worse trade than a request that ends in a
    // reset the browser reports.
    static constexpr uint32_t LOOP_STALE_MS       = 1500;
    static constexpr uint32_t LISTING_DEADLINE_MS = 4000;

    std::atomic<uint8_t>     _jobState{JOB_IDLE};
    std::atomic<bool>        _jobDead{false};  // client left before we answered
    std::atomic<uint32_t>    _jobGen{0};      // which job a disconnect belongs to
    std::atomic<uint32_t>    _jobArmedMs{0};  // when the slot was armed (deadline)
    std::atomic<uint32_t>    _loopBeatMs{0};  // last update() call = loop() alive
    SdJobKind                _jobKind = SdJobKind::Bins;
    AsyncWebServerRequestPtr _jobReq;          // weak: the library owns the request

    // Has loop() stopped calling update()? Rollover-safe (unsigned wrap).
    bool loopStalled() const {
        return (uint32_t)(millis() - _loopBeatMs.load(std::memory_order_relaxed))
               > LOOP_STALE_MS;
    }

    // ---- Append-only body buffer, PSRAM ONLY (rule 18) -------------------
    // No fallback to the internal heap: 12 KB taken from the 320 KB of SRAM
    // that WiFi, TLS and AsyncTCP live in kills the network stack far from the
    // cause, whereas a refused listing is one 503 the client retries. It
    // TRUNCATES rather than overflows, and truncation is turned into a valid
    // (empty) body by the caller — a listing cut mid-object would be invalid
    // JSON, i.e. an empty file manager with a silent catch (the 07-26 lesson
    // behind jesc()).
    struct PsBuf {
        uint8_t* p = nullptr;
        size_t   cap = 0, len = 0;
        bool     full = false;
        explicit PsBuf(size_t n) {
            p = (uint8_t*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (p) cap = n;
        }
        ~PsBuf() { if (p) heap_caps_free(p); }
        PsBuf(const PsBuf&)            = delete;
        PsBuf& operator=(const PsBuf&) = delete;
        // No `ok()` accessor: the two call sites ask the two questions
        // separately (`buf.p` = did PSRAM answer, `buf.full` = did it fit) and
        // act differently on each — 503 for one, an empty body for the other.
        // A combined predicate had no caller and could only ever hide which of
        // the two happened (audit 08-03).
        void reset()    { len = 0; full = false; }
        void add(const char* s, size_t n) {
            if (!p || full) return;
            if (len + n > cap) { full = true; return; }
            memcpy(p + len, s, n);
            len += n;
        }
        void add(const char* s)   { add(s, strlen(s)); }
        void add(const String& s) { add(s.c_str(), s.length()); }
        void addNum(uint32_t v) {
            char t[12];
            add(t, (size_t)snprintf(t, sizeof(t), "%lu", (unsigned long)v));
        }
        // Hands the block over to the response; the PsBuf no longer owns it.
        uint8_t* release() { uint8_t* q = p; p = nullptr; cap = len = 0; return q; }
    };

    // ---- AsyncTCP task side ---------------------------------------------
    // Touches NO card and no shared state beyond the slot (rule A2.6): claims,
    // records, pauses, returns.
    void beginSdListing(AsyncWebServerRequest* req, SdJobKind kind) {
        // PART 1 of the parking rule (see the slot's comment block): a request
        // is only parked when there is somebody left to un-park it. loop()
        // stamps `_loopBeatMs` on every pass; when that stamp is stale the
        // machine is inside the touch launcher or a flash, and a job armed now
        // would wait for a loop() that is not coming back.
        if (loopStalled()) {
            // The MESSAGE stays generic — the launcher and the flash are the
            // long cases, but a card being remounted stalls loop() for seconds
            // too, and naming a cause we only guessed sends the reader after
            // the wrong one. The serial line carries the measurement instead.
            Serial.printf("[api] listing SD refuse : loop() muette depuis %lu ms "
                          "(lanceur, flash ou acces SD long)\n",
                          (unsigned long)(millis() -
                              _loopBeatMs.load(std::memory_order_relaxed)));
            sendErr(req, 503, "the robot is busy - retry",
                              "le robot est occupé - réessayer");
            return;
        }
        uint8_t expected = JOB_IDLE;
        if (!_jobState.compare_exchange_strong(expected, JOB_CLAIMED)) {
            // ONE listing at a time. A slot held by a job loop() is about to
            // serve is a real collision (two clients, or a console reloading on
            // top of itself), not a queue we are declining to build; a slot
            // held PAST the deadline is a job nobody is going to serve, and the
            // middleware has already reaped it by the time we get here. Either
            // way a clean 503 beats two jobs sharing one buffer.
            sendErr(req, 503, "another listing is in progress - retry",
                              "un listing est déjà en cours - réessayer");
            return;
        }
        // A GENERATION, not a bare flag (review 08-03). This server closes
        // the connection at the end of EVERY response, and AsyncTCP calls
        // the discard callback synchronously from close() - so job A's
        // still-registered lambda fires while job B is already armed, and
        // B was seen as dead, dropped without a response, its request left
        // paused until the browser timed out. The lambda now carries the
        // generation it belongs to and a stale one is ignored.
        const uint32_t gen = _jobGen.fetch_add(1) + 1;
        _jobDead.store(false);
        // Registered BEFORE pausing: from here on, the only thing that can
        // happen to this request without us is that it dies.
        req->onDisconnect([this, gen]() {
            uint32_t cur = _jobGen.load();
            if (cur != gen) return;                 // a previous job's lambda
            _jobDead.store(true);                   // ours, and still current
            // PART 3: AND FREE THE SLOT. Setting a flag for loop() to notice
            // is enough only while loop() is running — the very assumption
            // this audit removed. The client is gone, so there is nothing left
            // to answer: take the job by the same CAS loop() uses (so we never
            // race a pass that is already serving it) and release. Runs on the
            // AsyncTCP task, from the library's own close path.
            uint8_t armed = JOB_ARMED;
            if (_jobState.compare_exchange_strong(armed, JOB_SERVING,
                                                  std::memory_order_acq_rel))
                releaseSdJob();
        });
        _jobKind = kind;
        _jobReq  = req->pause();
        // Stamped BEFORE the publication barrier: a reaper that sees ARMED
        // must see the deadline it belongs to, never a previous job's.
        _jobArmedMs.store(millis(), std::memory_order_relaxed);
        _jobState.store(JOB_ARMED, std::memory_order_release);
    }

    // ---- PART 2: the reaper. AsyncTCP task, called by the server middleware
    //      on EVERY incoming request — the cheap case is one atomic load.
    //
    // Answering a paused request from here is the library's OWN nominal path
    // (`send()` on a paused request clears the pause and flushes it), and doing
    // it from the AsyncTCP task is strictly safer than doing it from loop():
    // the disconnect callback runs on this same task, so the residual
    // lock/send race documented on `serviceSdListing()` cannot happen here.
    void reapStaleListing() {
        if (_jobState.load(std::memory_order_acquire) != JOB_ARMED) return;
        const uint32_t age = millis() - _jobArmedMs.load(std::memory_order_relaxed);
        if (age < LISTING_DEADLINE_MS) return;
        uint8_t armed = JOB_ARMED;                  // loop() may have taken it
        if (!_jobState.compare_exchange_strong(armed, JOB_SERVING,
                                               std::memory_order_acq_rel)) return;
        AsyncWebServerRequestPtr weak = _jobReq;
        std::shared_ptr<AsyncWebServerRequest> req;
        if (!_jobDead.load() && !weak.expired()) req = weak.lock();
        Serial.printf("[api] listing SD arme depuis %lu ms sans service -> "
                      "abandon (%s)\n", (unsigned long)age,
                      req ? "503 au client" : "client deja parti");
        if (req) sendErr(req.get(), 503, "the robot did not answer in time - retry",
                                         "le robot n'a pas répondu à temps - réessayer");
        releaseSdJob();
    }

    // ---- loop() side, renderer PAUSED by the caller ----------------------
    // Reads the card, builds the body in PSRAM, answers, frees the slot.
    // Returns false when there is nothing pending.
    //
    // THE ONE RESIDUAL RACE, stated rather than hidden: `weak.lock()` proves
    // the request was alive at that instant and no more — the library's
    // shared_ptr has a no-op deleter, so holding it does not keep the object
    // alive (ESPAsyncWebServer.h, `pause()`; its own RequestContinuation
    // example does exactly this). A disconnect landing between the lock and
    // the send is a use-after-free. It is left OPEN, deliberately: closing it
    // means holding a mutex that the disconnect callback also takes, i.e.
    // blocking the AsyncTCP task while loop() is inside `send()` — and
    // `send()` reaches lwIP through `tcpip_api_call`, whose thread blocks on
    // AsyncTCP's event queue when that queue is full. That trade swaps a
    // window of a few microseconds ending in a reboot the robot comes back
    // from, for a deadlock that freezes loop() — servos, touch and the SD
    // recovery path — until someone pulls the power. The cheap half of the
    // mitigation IS taken: `_jobDead` catches every disconnect that happened
    // before this pass, which is all of them but the coincidence.
    bool serviceSdListing() {
        // TAKEN, not merely observed: the AsyncTCP reaper competes for the same
        // job (deadline, or the client disconnecting), and whoever wins this
        // CAS is the only one that touches the parked request.
        uint8_t armed = JOB_ARMED;
        if (!_jobState.compare_exchange_strong(armed, JOB_SERVING,
                                               std::memory_order_acq_rel))
            return false;
        const SdJobKind          kind = _jobKind;
        AsyncWebServerRequestPtr weak = _jobReq;
        const bool gone = _jobDead.load() || weak.expired();
        if (gone) {                 // nothing allocated yet: nothing to free
            releaseSdJob();
            return true;
        }

        PsBuf buf(LISTING_BUF);
        if (buf.p) {
            if (kind == SdJobKind::Bins) buildBinsJson(buf);
            else                         buildSdListJson(buf);
            if (buf.full) {         // valid JSON beats a truncated object
                Serial.printf("[api] listing SD tronque (>%u o) - corps vide\n",
                              (unsigned)LISTING_BUF);
                buf.reset();
                buf.add(kind == SdJobKind::Bins ? "[]" : "{\"files\":[]}");
            }
        }

        auto req = weak.lock();
        if (req) {
            if (buf.p) {
                // THE LENGTH IS READ BEFORE THE RELEASE, and it has to be.
                // Written `sendPsBody(req.get(), buf.release(), buf.len)`, the
                // two arguments are INDETERMINATELY SEQUENCED in C++: GCC
                // evaluated `release()` first, which zeroes `len`, so the
                // response was built with a length of 0 — HTTP 200, correct
                // headers, no Content-Length and an EMPTY body. It looked like
                // an SD fault and was a sequencing rule (found on target 08-03,
                // the endpoint answered 200 with nothing in it).
                const size_t bodyLen = buf.len;
                sendPsBody(req.get(), buf.release(), bodyLen);
            } else {
                // Rule 18: the block is REFUSED, traced, and the client is
                // told to retry — never taken from the internal heap.
                Serial.printf("[api] listing SD: PSRAM indisponible (%u o) -> 503\n",
                              (unsigned)LISTING_BUF);
                sendErr(req.get(), 503, "out of memory - retry",
                                        "mémoire insuffisante - réessayer");
            }
        }
        releaseSdJob();
        return true;
    }

    void releaseSdJob() {
        _jobReq.reset();
        _jobState.store(JOB_IDLE, std::memory_order_release);
    }

    // The body goes out from a PSRAM block owned BY THE RESPONSE: the
    // shared_ptr is captured by the filler, and the library destroys the
    // response when the body has gone out OR when the client disconnects — so
    // the block is freed exactly once either way. Same ownership as the camera
    // snapshot. The filler itself runs on the AsyncTCP task and is a memcpy:
    // that is the whole point of the exercise.
    static void sendPsBody(AsyncWebServerRequest* req, uint8_t* body, size_t len) {
        std::shared_ptr<uint8_t> buf(body,
            [](uint8_t* q) { if (q) heap_caps_free(q); });
        AsyncWebServerResponse* r = req->beginResponse("application/json", len,
            [buf, len](uint8_t* dst, size_t maxLen, size_t index) -> size_t {
                size_t remaining = len - index;
                size_t n = remaining < maxLen ? remaining : maxLen;
                memcpy(dst, buf.get() + index, n);
                return n;
            });
        r->addHeader("Cache-Control", "no-store");
        req->send(r);
    }

    // ---- The two bodies. Byte-for-byte what the AsyncTCP callbacks used to
    //      build; only the destination changed (PSRAM instead of a String on
    //      the internal heap) and the caller (loop() instead of AsyncTCP).
    static void buildBinsJson(PsBuf& b) {
        b.add("[");
        bool first = true;
        File dir = SD.open("/bins");
        if (dir && dir.isDirectory()) {
            File f;
            while ((f = dir.openNextFile())) {
                String n = f.name();
                if (!f.isDirectory() && n.endsWith(".bin")) {
                    if (!first) b.add(",");
                    first = false;
                    // jesc: name dropped in by hand (max review 07-26)
                    b.add("{\"name\":\""); b.add(jesc(n));
                    b.add("\",\"size\":");  b.addNum(f.size());
                    b.add("}");
                }
                f.close();
            }
            dir.close();
        }
        b.add("]");
    }

    static void buildSdListJson(PsBuf& b) {
        b.add("{\"files\":[");
        bool first = true;
        auto add = [&](const String& name, const String& path, uint32_t sz,
                       const char* cat) {
            if (!first) b.add(",");
            first = false;
            b.add("{\"name\":\""); b.add(jesc(name));
            b.add("\",\"path\":\""); b.add(jesc(path));
            b.add("\",\"size\":");   b.addNum(sz);
            b.add(",\"cat\":\"");    b.add(cat);
            b.add("\"}");
        };
        // Every .yaml in /stackchan-companion/ (config.yaml + the guest bins'
        // own configs, e.g. flightradar.yaml) — kept in step with
        // sdPathAllowed.
        File cf = SD.open("/stackchan-companion");
        if (cf && cf.isDirectory()) {
            for (File c = cf.openNextFile(); c; c = cf.openNextFile()) {
                if (!c.isDirectory()) {
                    String nm = c.name(); int sl = nm.lastIndexOf('/');
                    if (sl >= 0) nm = nm.substring(sl + 1);
                    // The runway database is listed TOO: it is uploadable
                    // (sdPathAllowed), so not showing it left no way of
                    // telling whether the card actually carries it.
                    if (nm.endsWith(".yaml"))
                        add(nm, "/stackchan-companion/" + nm, c.size(),
                            nm == "config.yaml" ? "config" : "guest");
                    else if (nm == "runways.csv")
                        add(nm, "/stackchan-companion/" + nm, c.size(), "data");
                }
                c.close();
            }
        }
        if (cf) cf.close();
        File f = SD.open("/stackchan-companion/rules.txt");
        if (f) {
            add("rules.txt", "/stackchan-companion/rules.txt", f.size(), "rules");
            f.close();
        }
        const char* dirs[2] = { "/dances", "/bins" };
        const char* cats[2] = { "dance",   "bin"   };
        for (int i = 0; i < 2; i++) {
            File d = SD.open(dirs[i]);
            if (d && d.isDirectory()) {
                for (File c = d.openNextFile(); c; c = d.openNextFile()) {
                    if (!c.isDirectory()) {
                        String nm = c.name(); int sl = nm.lastIndexOf('/');
                        if (sl >= 0) nm = nm.substring(sl + 1);
                        // only expose downloadable files (get whitelist)
                        bool okExt = (i == 0) ? nm.endsWith(".csv")
                                              : nm.endsWith(".bin");
                        if (okExt) add(nm, String(dirs[i]) + "/" + nm,
                                       c.size(), cats[i]);
                    }
                    c.close();
                }
            }
            if (d) d.close();
        }
        b.add("]}");
    }

    // Rebuilds the /bins/ name cache POST /api/bins/launch validates against.
    // loop() ONLY (SD access, renderer paused by the caller).
    // Fills the SPARE bank and publishes it — never the bank a callback may be
    // walking (see BinBank). Anything the card holds and this cache cannot is
    // COUNTED and said out loud: a .bin the console lists and the launch route
    // then refuses is exactly the kind of "it is on the card, why does it not
    // work" that costs an evening.
    bool refreshBinsCache() {
        if (!_binsDirty.load(std::memory_order_acquire)) return false;
        _binsDirty.store(false, std::memory_order_relaxed);
        const uint8_t cur   = _binBankIdx.load(std::memory_order_relaxed);
        BinBank&      spare = _binBank[cur ^ 1];
        int k = 0, tooMany = 0, tooLong = 0;
        File dir = SD.open("/bins");
        if (dir && dir.isDirectory()) {
            File f;
            // The whole directory is walked even once the bank is full: the
            // count of what did NOT fit is the point of the walk. GET
            // /api/bins walks it unbounded already, so this costs nothing new.
            while ((f = dir.openNextFile())) {
                String nm = f.name();
                const int sl = nm.lastIndexOf('/');
                if (sl >= 0) nm = nm.substring(sl + 1);
                if (!f.isDirectory() && nm.endsWith(".bin")) {
                    if (nm.length() >= BIN_NAME_MAX) {
                        // NOT truncated into the bank: a truncated name matches
                        // nothing anyway, and storing it would only make the
                        // table lie about what it holds.
                        tooLong++;
                        Serial.printf("[api] /bins : '%s' (%u car.) depasse %u - "
                                      "NON lancable par l'API\n", nm.c_str(),
                                      (unsigned)nm.length(),
                                      (unsigned)(BIN_NAME_MAX - 1));
                    } else if (k < MAX_BINS) {
                        strlcpy(spare.name[k++], nm.c_str(), BIN_NAME_MAX);
                    } else {
                        tooMany++;
                    }
                }
                f.close();
            }
        }
        if (dir) dir.close();
        spare.n = k;
        // Publication barrier: everything written above is visible to the
        // AsyncTCP task before the index it reads changes.
        _binBankIdx.store(cur ^ 1, std::memory_order_release);
        if (tooMany || tooLong)
            Serial.printf("[api] cache /bins SATURE : %d nom(s) retenu(s) sur %d, "
                          "%d au-dela du plafond %d, %d trop long(s) - ces .bin "
                          "sont LISTES mais PAS lancables par l'API\n",
                          k, k + tooMany + tooLong, tooMany, MAX_BINS, tooLong);
        return true;
    }

    // ==================================================================
    // A2.19 — ROUTE ORDER, verified instead of merely hoped for
    // ==================================================================
    // ESPAsyncWebServer, in BackwardCompatible mode, lets `/api/x/y` be
    // swallowed by the `/api/x` handler if the latter is registered FIRST.
    // The rule "the specific path BEFORE its prefix" has already broken THREE
    // endpoints (config/reload, bins/launch, bins/stop — 2026-07-12), and up
    // to now it rested on nothing but a comment and the position of
    // thirty-seven calls inside a 1400-line function. An endpoint made
    // unreachable does not show: it answers 200 with the body of the wrong
    // handler.
    //
    // So every route goes through `route()`, which registers it AND records
    // it; `checkRouteOrder()` re-reads the list at startup and DENOUNCES, one
    // after another, every badly ordered pair. The cost is a table of 48
    // pointer pairs, once, at boot — next to an endpoint dying in silence,
    // that is cheap.
    // `WebRequestMethodComposite` is an ENCAPSULATED mask: it exposes neither
    // an integer conversion nor an intersection between two composites, only
    // equality and `matches(WebRequestMethod)`. Equality is NOT enough — the
    // router decides by intersection, so a route declared with a combined mask
    // (`HTTP_GET | HTTP_POST`) made the guard silent on that path, precisely
    // where order matters most. Hence `methodsOverlap`, which queries
    // `matches()` bit by bit (found in the 07-29 review).
    struct RouteNote { const char* path; WebRequestMethodComposite method; };
    static constexpr int MAX_ROUTES = 48;
    RouteNote _routes[MAX_ROUTES];
    int       _nRoutes = 0;

    // PERFECT forwarding: `on()` has several overloads (handler alone,
    // + onUpload, + onBody) and they must all stay reachable.
    template <typename... A>
    void route(const char* path, WebRequestMethodComposite method, A&&... h) {
        if (_nRoutes < MAX_ROUTES) _routes[_nRoutes++] = { path, method };
        else Serial.println("[api] ATTENTION : MAX_ROUTES atteint, "
                            "verification d'ordre INCOMPLETE");
        _server.on(path, method, std::forward<A>(h)...);
    }

    // Is `a` a PATH prefix of `b`?
    //
    // The criterion is not a matter of taste: it is EXACTLY the router's own.
    // A plain string passed to `on()` produces a `BackwardCompatible` matcher,
    // whose semantics are `^{uri}(/.*)?$` (ESPAsyncWebServer,
    // AsyncURIMatcher). So "/api/bins" captures "/api/bins/launch", but
    //   - NOT "/api/binsxyz": the optional group requires a "/";
    //   - and "/" captures ONLY "/" — hence the absence of a special case for
    //     the root, which would have made the guard shout about "/swagger"
    //     and "/api/openapi.json" even though they work. A guard that cries
    //     wolf ends up ignored, which is worse than no guard at all.
    static bool pathPrefixOf(const char* a, const char* b) {
        size_t la = strlen(a);
        return !strncmp(a, b, la) && b[la] == '/';
    }

    // Two routes step on each other as soon as they share ONE method: the
    // router tests an INTERSECTION of masks, not an equality. Comparing the
    // composites with `==` therefore made the guard silent on every route
    // declared with a combined mask (`HTTP_GET | HTTP_POST`) — precisely the
    // case where order matters most. `WebRequestMethodComposite` does not
    // publish its mask: querying `matches()` bit by bit is the only way.
    // 24 bits = the methods defined by the library (the HTTP_INVALID sentinel
    // sits at bit 31 and designates no route).
    static bool methodsOverlap(const WebRequestMethodComposite& a,
                               const WebRequestMethodComposite& b) {
        for (int bit = 0; bit < 24; bit++) {
            const auto m = static_cast<WebRequestMethod>(1u << bit);
            if (a.matches(m) && b.matches(m)) return true;
        }
        return false;
    }

    void checkRouteOrder() const {
        for (int i = 0; i < _nRoutes; i++)
            for (int j = i + 1; j < _nRoutes; j++) {
                if (!methodsOverlap(_routes[i].method, _routes[j].method))
                    continue;
                if (pathPrefixOf(_routes[i].path, _routes[j].path))
                    Serial.printf("[api] A2.19 VIOLEE : %s est enregistre AVANT "
                                  "%s et va l'avaler - inverser les deux\n",
                                  _routes[i].path, _routes[j].path);
                // Same path AND a shared method: the second handler will NEVER
                // be reached (the router stops at the first one). Silent, and
                // indistinguishable from a logic bug inside the handler.
                else if (!strcmp(_routes[i].path, _routes[j].path))
                    Serial.printf("[api] route DOUBLON : %s est enregistre deux "
                                  "fois - le second handler est mort\n",
                                  _routes[i].path);
            }
    }

    // ==================================================================
    // Bilingual API messages — EN/FR (firmware/common/I18n.h)
    // ==================================================================
    // ENGLISH FIRST, at the call site, and English is also what an absent or
    // unreadable `lang:` gives: a card that never heard of the setting answers
    // in English rather than in nothing.
    //
    // The JSON ENVELOPE lives HERE and not at the forty call sites. That is
    // what makes the second language affordable: each site used to carry its
    // own `{"error":"…"}` literal, so shipping French would have duplicated the
    // braces, the quotes and the key as well as the words. Factored, French
    // costs its own text and the call sites got shorter.
    //
    // ACCENTS ARE CORRECT HERE, unlike everywhere else in this firmware: these
    // strings travel as `application/json` (UTF-8 by definition, RFC 8259) to a
    // browser or to curl, never to the 6x8 bitmap face that renders UTF-8 as
    // "??" (the caveat at the top of I18n.h is about the FACE, not the header).
    //
    // The buffer is sized for the longest message plus its UTF-8 accents; a
    // longer one truncates rather than overflows, and the HTTP code — which is
    // what a machine reads — is unaffected either way.
    static void sendErr(AsyncWebServerRequest* req, int code,
                        const char* en, const char* fr) {
        char body[224];
        snprintf(body, sizeof(body), "{\"error\":\"%s\"}", sce::T(en, fr));
        req->send(code, "application/json", body);
    }
    static void sendNote(AsyncWebServerRequest* req, int code,
                         const char* en, const char* fr) {
        char body[224];
        snprintf(body, sizeof(body), "{\"ok\":true,\"note\":\"%s\"}",
                 sce::T(en, fr));
        req->send(code, "application/json", body);
    }

    // ---- Serving a page pre-compressed at build time ----------------------
    // The array lives in flash and goes out as-is; the browser inflates. No
    // RAM copy (AsyncProgmemResponse points straight at the bytes), no CPU
    // spent compressing on a chip that has better things to do, and on this
    // robot's radio (RSSI around -77) the bytes not sent are the whole point.
    static void sendGz(AsyncWebServerRequest* req, const char* type,
                       const uint8_t* data, size_t len) {
        AsyncWebServerResponse* r = req->beginResponse(200, type, data, len);
        r->addHeader("Content-Encoding", "gzip");
        req->send(r);
    }

    // ---- The console page + the one thing only the firmware knows ---------
    // The page (WebConsole.h) carries its English in the markup and translates
    // itself in JavaScript. The browser cannot read `lang:` from the SD card,
    // so the firmware has to publish it.
    //
    // IT USED TO BE A TRAILER: one `<script>window.SCE_LANG="fr"…</script>`
    // line appended after `</html>`. That is IMPOSSIBLE on a gzipped body —
    // plain text concatenated to a deflate stream is not a deflate stream, and
    // the multi-member gzip that WOULD decode is not reliably supported by
    // browser content decoders. Two responses is not a thing either.
    //
    // So the language now travels in the SAME response, as a COOKIE. It is the
    // only carrier that is (a) already there when the parser reaches the first
    // inline script — cookies are stored while the headers are processed, so
    // there is NO extra request and NO flash of English before a French pass —
    // and (b) readable by the page itself, which a `Content-Language` header is
    // not. The alternative, a tiny `/api/lang` the page fetches, costs a second
    // round trip on the very link this change exists to spare, and the page
    // would render in English until it lands: exactly the flash we must avoid.
    //
    // The page reads `sce_lang` only as a FALLBACK for `window.SCE_LANG`
    // (WebConsole.h, applyLang), so a caller that sets the variable itself
    // still wins and the old trailer would still work if it ever came back. No
    // cookie, or cookies refused: the page stays English — the intended
    // fallback, unchanged. Max-Age rather than a session cookie so a page
    // restored from the browser's back-forward cache still finds it; every
    // real load rewrites it, so a `lang:` edit takes effect on the next reload.
    static void sendConsole(AsyncWebServerRequest* req) {
        AsyncWebServerResponse* r = req->beginResponse(200, "text/html",
            CONSOLE_HTML_GZ, CONSOLE_HTML_GZ_LEN);
        r->addHeader("Content-Encoding", "gzip");
        char cookie[72];
        snprintf(cookie, sizeof(cookie),
                 "sce_lang=%s; Path=/; Max-Age=31536000; SameSite=Lax",
                 sce::langCode());
        r->addHeader("Set-Cookie", cookie);
        req->send(r);
    }

    AsyncWebServer    _server;
    Brain&            _brain;
    Renderer&         _renderer;
    FieldStore*       _fields = nullptr;   // blackboard (status band)
    Tuning&           _tuning;
    WifiCreds&        _creds;
    ApiCreds&         _apiCreds;             // Basic Auth protection (= SdConfig::api)
    DanceStore&       _dances;
    DNSServer         _dns;
    bool              _apMode    = false;
    bool              _sdPresent = false;   // set by main (board.hasSD())
    const SoundTracker* _sound   = nullptr; // mic levels (status)
    const Board*        _board   = nullptr; // PMIC battery (status)
    Camera*             _camera  = nullptr; // GC0308 (/api/camera/*)
    const ServoPose*    _pose    = nullptr; // measured pose, sampled by loop()

    // State of one MJPEG stream (/api/camera/stream) — one per connection,
    // held by a shared_ptr captured in the filler (freed on disconnect).
    struct CamStream {
        Camera*  cam      = nullptr;
        uint8_t* frame    = nullptr;   // current JPEG (PSRAM), owned here
        size_t   frameLen = 0;
        String   head;                 // multipart header of the current frame
        size_t   sent     = 0;         // bytes emitted of (head+frame+CRLF)
        uint32_t lastVer  = 0;         // last version served (pacing)
        uint32_t lastEmitMs = 0;       // pacing bound (re-serve after 300 ms)
        ~CamStream() { if (frame) heap_caps_free(frame); }
    };
    // The "auth is active" decision: an ATOMIC flag (not strlen on a char[]
    // that loop() may be rewriting) — an uploadAuthed/status reading during a
    // password change sees a clean state (fail-closed if the compare lands on
    // a partial buffer), never a fail-OPEN on a premature NUL.
    std::atomic<bool> _authEnabled{false};
    const CpuLoad*      _cpu     = nullptr; // CPU load (status)
    // ---- Personality write, staged for loop() (A2.6) --------------------
    // The callback fills this under a spinlock and raises `_persoReq`; loop()
    // takes a copy, writes the card and reloads the table. Same shape as the
    // pending WiFi/security credentials, and for the same reason: the shared
    // struct must never be half-written while the applier reads it.
    portMUX_TYPE       _persoMux = portMUX_INITIALIZER_UNLOCKED;
    sce::Personality   _persoPending{};
    bool               _persoDelete = false;
    std::atomic<bool>  _persoReq{false};

    // 1-15 chars of `a-z 0-9 _ -`. The name becomes a FILE NAME on the card
    // (`personalities/<name>.yaml`) and a rules path, so it is checked here
    // rather than trusted: a slash or a `..` would write outside the directory,
    // and the whole point of sdPathAllowed() is that nothing does that.
    static bool validPersoName(const String& s) {
        if (s.length() < 1 || s.length() >= sce::PERSO_NAME_MAX) return false;
        for (size_t i = 0; i < s.length(); i++) {
            const char c = s[i];
            const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                            c == '_' || c == '-';
            if (!ok) return false;
        }
        return true;
    }
    std::atomic<bool> _saveReq{false};
    std::atomic<int>  _langReq{-1};      // see consumeLangRequest()

    // 0 none, 1 tap, 2 reset, 3 set (with _timerSetM/_timerSetS).
    std::atomic<int> _timerCmd{0};
    std::atomic<int> _timerSetM{0};
    std::atomic<int> _timerSetS{0};
    std::atomic<bool> _reloadReq{false};
    std::atomic<bool> _dancesReloadReq{false};
    // Rule listing helpers. They live here and not in RuleEngine because they
    // are a PRESENTATION of the table: the engine has no business knowing that
    // something wants "SetEmotion Worried 4000" as a string.
    // A description comes from a file the user wrote, so it can hold a quote
    // or a backslash — either of which would produce JSON the console fails to
    // parse, and the symptom would be an empty rule table rather than a bad
    // character. Copied out with those two escaped, and nothing else: the
    // template is ASCII and the reader already tolerates the rest.
    static const char* jsonSafe(const char* s, char* out, size_t cap) {
        size_t j = 0;
        for (size_t i = 0; s && s[i] && j + 2 < cap; i++) {
            // A RAW CONTROL CHARACTER IS NOT VALID JSON, and a tab inside a
            // comment survives the parser that captured it. Emitted as-is it
            // makes the whole document unparseable, and the symptom is an
            // EMPTY rules table rather than one odd character - the failure
            // lands nowhere near its cause. Folded to a space; nothing here
            // needs to round-trip.
            const unsigned char c = (unsigned char)s[i];
            if (c < 0x20) { out[j++] = ' '; continue; }
            if (c == '"' || c == '\\') out[j++] = '\\';
            out[j++] = (char)c;
        }
        out[j] = '\0';
        return out;
    }
    static const char* opName(RuleEngine::Op o) {
        switch (o) {
            case RuleEngine::GT: return "gt";
            case RuleEngine::GE: return "ge";
            case RuleEngine::LT: return "lt";
            case RuleEngine::LE: return "le";
            case RuleEngine::EQ: return "eq";
            default:             return "ne";
        }
    }
    static void ruleActionName(const RuleEngine::Rule& r, char* out, size_t cap) {
        if (r.setAction) {
            snprintf(out, cap, "set %s %g", r.setKey ? r.setKey : "?", r.setVal);
            return;
        }
        switch (r.cmd.type) {
            case CmdType::SetEmotion:
                if (r.cmd.u) snprintf(out, cap, "SetEmotion %s %lu",
                                      emotionName((eEmotions)r.cmd.i),
                                      (unsigned long)r.cmd.u);
                else         snprintf(out, cap, "SetEmotion %s",
                                      emotionName((eEmotions)r.cmd.i));
                break;
            case CmdType::PlayDance: {
                // THE NAME the rule was written with, which since the fire-time
                // resolution is what the rule actually carries. Reading the
                // compiled table by index would print "?" for every SD
                // choreography — the console would show a rule that looks
                // broken while it works perfectly.
                if (r.cmd.ptr) {
                    snprintf(out, cap, "PlayDance %s", (const char*)r.cmd.ptr);
                    break;
                }
                const auto* t = dances::table();
                snprintf(out, cap, "PlayDance %s",
                         (r.cmd.i >= 0 && r.cmd.i < dances::count() && t[r.cmd.i].name)
                             ? t[r.cmd.i].name : "?");
                break; }
            case CmdType::Blink:       snprintf(out, cap, "Blink"); break;
            case CmdType::WinkLeft:    snprintf(out, cap, "WinkLeft"); break;
            case CmdType::WinkRight:   snprintf(out, cap, "WinkRight"); break;
            case CmdType::AmbientDark: snprintf(out, cap, "AmbientDark %d", r.cmd.i); break;
            default:                   snprintf(out, cap, "?"); break;
        }
    }

    std::atomic<bool> _rulesReloadReq{false};
    const RuleEngine* _ruleEng = nullptr;
    int               _ruleBuiltins = 0;
    std::atomic<bool> _clockSyncReq{false};
    const ClockState* _clk = nullptr;
    std::atomic<bool> _launchReq{false};
    // main.cpp consumes this into a `char path[56]`, so the buffer stays 56
    // and BIN_NAME_MAX is DERIVED from it below rather than guessed.
    static constexpr size_t LAUNCH_PATH_MAX = 56;
    char              _launchPath[LAUNCH_PATH_MAX] = "";
    // NAMES OF /bins/, refreshed by loop() — see refreshBinsCache().
    // The launch route used to answer 404 by calling SD.exists() INSIDE the
    // AsyncTCP callback: blocking I/O where rule A2.6 allows only post() and
    // Tuning writes, and unpaused SD on the SPI2 bus the LCD shares (A2.16,
    // the documented ~0.7 s freeze). The check is a name lookup, so it does
    // not need the card at all — only a list the card produced earlier.
    //
    // THE CAP IS A CAP, NOT A CEILING NOBODY REACHES (audit 08-03). It used to
    // be 24 names of 39 characters, silently, while `GET /api/bins` listed the
    // whole directory with no limit at all: a card with a 25th .bin, or a name
    // of 39 characters, was OFFERED by the API and by the console and then
    // answered 404 by `POST /api/bins/launch` — a cap that truncates in silence
    // reads as "everything is covered". So: 48 names, 49 characters each —
    // the longest that still fits "/bins/" + name + NUL in `_launchPath`,
    // asserted below rather than left to a comment — and BOTH limits are
    // DENOUNCED on the serial line when the card exceeds them, plus an
    // explicit error on the launch route instead of a bare "not found".
    // The pair is SHARED with the touch launcher (BinLimits.h, review
    // 08-04): two catalogues of one directory, one answer.
    static constexpr int    MAX_BINS     = sce::BINS_MAX;
    static constexpr size_t BIN_NAME_MAX = sce::BIN_NAME_MAX;
    static_assert(BIN_NAME_MAX + 6 <= LAUNCH_PATH_MAX,
                  "a cached name must fit \"/bins/\" + name + NUL in _launchPath");
    // DOUBLE BANK, the project's own idiom for a table published across tasks
    // (DanceStore). `volatile` was doing NONE of the work it looked like it was
    // doing here: it makes neither the array writes atomic nor their order
    // visible, so loop() rewrote `_bins[]` IN PLACE — with SD reads between two
    // entries — while an AsyncTCP callback walked it, and a torn name decides a
    // launch. loop() now fills the SPARE bank and publishes it with one release
    // store; the callback reads the index with an acquire load and never sees a
    // half-written table. Cost: 4.8 KB inside the one WebApi, which main.cpp
    // allocates ONCE at boot (measured: static RAM unchanged, 24.9 %) — not a
    // per-request allocation and not a String in a callback.
    struct BinBank {
        char name[MAX_BINS][BIN_NAME_MAX] = {};
        int  n = 0;
    };
    BinBank              _binBank[2];
    std::atomic<uint8_t> _binBankIdx{0};      // bank PUBLISHED to the callbacks
    std::atomic<bool>    _binsDirty{true};    // loop() refreshes when set
    File              _uploadFile;
    bool              _uploadOk = false;
    bool              _updateOk = false;            // OTA: Update.end() ok
    uint32_t          _otaLastMs = 0;               // OTA: last chunk received
    std::atomic<uint32_t> _rebootAtMs{0};           // OTA: deferred reboot
    std::atomic<uint32_t> _poweroffAtMs{0};         // deferred shutdown

    // Pending credentials: POST /api/security and /api/wifi STAGE into these
    // FIXED buffers (no String mutated on the AsyncTCP task while
    // SdConfig::save() reads it from loop() — race/UAF). loop() applies them
    // through applyPendingSecurity()/applyPendingWifi(). The atomic flag is
    // the barrier; _credMux (a short spinlock) protects the buffers against
    // two back-to-back POSTs rewriting them while loop() copies them.
    std::atomic<bool> _secReq{false};
    bool _pendUserSet = false;
    char _pendUser[64]     = "";
    char _pendPass[64]     = "";
    std::atomic<bool> _wifiReq{false};
    char _pendSsid[64]     = "";
    char _pendWifiPass[96] = "";
    portMUX_TYPE _credMux  = portMUX_INITIALIZER_UNLOCKED;

    // Copies in->out (at most n-1) keeping only printable ASCII except " and \
    // : prevents JSON injection into /api/status AND guarantees the quoted
    // YAML round-trip of SdConfig (yamlQuote does not escape). Truncates.
    // `quotable` = this value may contain `"` and `\`.
    //
    // The two callers genuinely differ, which is why this is a parameter and
    // not a second function. WIFI credentials go to `WiFi.begin()` and to the
    // card, and the card now escapes them (`SdConfig::yamlQuote` +
    // `sce::yaml::scalar`) — so stripping those two characters would only
    // corrupt legal passphrases, WPA-PSK being any 8..63 printable ASCII.
    // API credentials also land in the /api/status JSON (`authUser`, written
    // with a raw %s), where an unescaped quote breaks the whole document and
    // empties the console — so for those, stripping stays.
    static void sanitizeCred(const String& in, char* out, size_t n,
                             bool quotable = false) {
        size_t j = 0;
        for (unsigned i = 0; i < in.length() && j + 1 < n; i++) {
            unsigned char c = (unsigned char)in[i];   // char is signed on Xtensa
            // Strip control chars/DEL; keep bytes >=0x80 (UTF-8: accented
            // WiFi SSID/password).
            if (c < 0x20 || c == 0x7F) continue;
            if (!quotable && (c == '"' || c == '\\')) continue;
            out[j++] = (char)c;
        }
        out[j] = '\0';
    }

    bool _uploadDenied = false;    // current upload refused (missing auth)
    bool _uploadCompanion = false; // current upload = /companion.bin (.old dance)
    String _uploadTarget;          // FINAL path of the current sd/put (.tmp+rename)

    // SD import/export: allowed paths (anti-traversal + whitelist).
    // Minimal JSON escaping for SD FILE NAMES (dropped in by hand: a '"' or a
    // '\' broke the whole JSON → empty file manager/dance list in the console,
    // with a silent catch — review 07-26/max). Allocation-free fast path when
    // there is nothing to escape (which is nearly always the case).
    static String jesc(const String& v) {
        bool clean = true;
        for (char c : v)
            if (c == '"' || c == '\\' || (uint8_t)c < 0x20) { clean = false; break; }
        if (clean) return v;
        String o; o.reserve(v.length() + 4);
        for (char c : v) {
            if (c == '"' || c == '\\') o += '\\';
            if ((uint8_t)c >= 0x20) o += c;
        }
        return o;
    }

    static bool sdPathAllowed(const String& p) {
        if (p.indexOf("..") >= 0) return false;
        // ROOT_LEN is strlen(SD_ROOT) + 1 (the trailing slash) — DERIVED, not
        // repeated as a literal. The three checks below used to hardcode this
        // as 16 (twice) and a directory-length 30, sized for the old, shorter
        // "/stackchan-eyes" root; the 2026-09-19 rename to "/stackchan-companion"
        // updated the path STRINGS but left those numbers behind, silently
        // rejecting every read/write under the new root. Computed once here so
        // the next root name change cannot reintroduce the same bug.
        static const int ROOT_LEN = (int)strlen(SD_ROOT) + 1;
        // THE RULE FILES, as a family. `rules.txt` plus one per personality
        // (`rules.haro.txt`…). A pattern rather than a list of names, and the
        // exception is argued rather than assumed: the runway database below
        // stays named explicitly because it is ONE known file, whereas this
        // family is OPEN BY DESIGN — adding a character means adding a rule
        // file, and a whitelist needing an edit for each one would make the
        // console silently unable to manage the newest personality. Same shape
        // and same guards as the `.yaml` allowance just below: no subdirectory
        // (`indexOf('/', ROOT_LEN) < 0`) and a non-empty name.
        if (p.startsWith("/stackchan-companion/rules") && p.endsWith(".txt") &&
            p.indexOf('/', ROOT_LEN) < 0) return true;
        // SD-Updater restore binary: manageable REMOTELY (without it, stopping
        // a guest bin fails — diag 07-25; uploading avoids the mandatory trip
        // through the launcher's physical [SauverFW] menu entry)
        if (p == "/companion.bin")              return true;
        // Every .yaml in /stackchan-companion/: config.yaml + the GUEST bins' own
        // configs (e.g. flightradar.yaml — radar position editable from the
        // console).
        // indexOf('.') > ROOT_LEN: non-empty name (rejects "/stackchan-companion/.yaml")
        if (p.startsWith("/stackchan-companion/") && p.endsWith(".yaml") &&
            p.indexOf('/', ROOT_LEN) < 0 && p.indexOf('.') > ROOT_LEN) return true;
        // THE PERSONALITIES DIRECTORY — the one subdirectory allowed under
        // /stackchan-companion/, named in full rather than by relaxing the
        // no-subdirectory test above. That test is what keeps this whitelist
        // from becoming a tree walk, so it is left intact and an exception is
        // stated instead: `/stackchan-companion/personalities/<name>.yaml`, one
        // level, no deeper. `indexOf('/', L) < 0` is that "no deeper", L being
        // strlen(PERSO) — DERIVED below, for the same reason ROOT_LEN is.
        {
            static const char* PERSO = "/stackchan-companion/personalities/";
            static const int L = (int)strlen(PERSO);
            if (p.startsWith(PERSO) && p.endsWith(".yaml") &&
                p.indexOf('/', L) < 0 && p.length() > L + 5) return true;
        }
        // Runway database read by the flight-radar guest (generated by
        // tools/generators/make-runways.py from OurAirports, public domain): a
        // FIXED-WIDTH file its METAR view binary-searches. Named EXPLICITLY
        // rather than allowing every .csv under /stackchan-companion/ — this
        // whitelist is a list of known files, and a pattern would quietly
        // widen it every time someone drops a new csv there.
        if (p == "/stackchan-companion/runways.csv") return true;
        // The space guest's orbital element cache, on the same grounds as the
        // runway database above: a data file the bin READS, useful to inspect
        // when a position looks wrong, and useful to SEED by hand on a robot
        // that has no network. Named explicitly, like everything else here —
        // a "*.txt under /stackchan-companion/" pattern would quietly widen the
        // whitelist every time someone drops a note there.
        if (p == "/stackchan-companion/space-tle.txt") return true;
        // The space guest's launch-list cache, same grounds: it is what
        // spares a reboot an API request against a fifteen-per-hour quota,
        // so being able to inspect or seed it matters.
        if (p == "/stackchan-companion/space-launch.json") return true;
        // The radar's aerodrome cache (position, city, tower and ATIS
        // frequencies, field elevation) and the ha-remote entity roster. Both
        // are machine-written caches the bins read at boot, and both are worth
        // inspecting when a bin says it knows something odd about a place or a
        // device — the same grounds as the two above, and named just as
        // explicitly. The NOTAM token file is deliberately NOT here: it is a
        // bearer credential, and nothing that serves it over HTTP is doing
        // anyone a favour.
        if (p == "/stackchan-companion/radar-airports.csv") return true;
        if (p == "/stackchan-companion/ha-entities.tsv")    return true;
        if (p.startsWith("/dances/") && p.endsWith(".csv")) return true;
        if (p.startsWith("/bins/")   && p.endsWith(".bin")) return true;
        return false;
    }

    // Serialization of the uploads: dances/bins/OTA share _uploadFile,
    // _uploadDenied and _uploadOk. AsyncTCP interleaves the chunks of two
    // simultaneous POSTs → a 2nd upload would close/overwrite the handle of
    // the 1st (truncated SD file, aborted OTA). uploadOwns() allows only ONE
    // active upload at a time; the chunks of a concurrent upload are ignored
    // (they touch NO shared state). Automatic recovery if the owner dies
    // without `final` (>10 s with no chunk = dropped connection) — otherwise
    // one aborted upload would block every later one. Ownership is released by
    // the HTTP response (uploadRelease if _uploadReq==req) OR by this timeout.
    // (Review 07-20.)
    // _uploadReq/_uploadLastMs are shared between the AsyncTCP task
    // (uploadOwns/uploadRelease) and loop() (the self-heal in update()): the
    // pair is protected by a short SPINLOCK (_upMux). Without it, the
    // self-heal could disown an upload just STARTING (publish-before-timestamp
    // window) or a live one, between two unordered writes (review 07-21). The
    // timestamp is written BEFORE the owner is published; the side effects
    // (setInhibited, printf) stay OUTSIDE the critical section.
    AsyncWebServerRequest* _uploadReq = nullptr;
    uint32_t               _uploadLastMs = 0;
    portMUX_TYPE           _upMux = portMUX_INITIALIZER_UNLOCKED;
    bool uploadOwns(AsyncWebServerRequest* req, size_t index) {
        uint32_t now = millis();
        bool owns, started = false;
        portENTER_CRITICAL(&_upMux);
        if (index == 0) {
            bool libre = (_uploadReq == nullptr) || (now - _uploadLastMs > 10000);
            if (libre || _uploadReq == req) {
                _uploadLastMs = now;      // BEFORE publishing (ordering, 07-21)
                _uploadReq    = req;
                started       = true;
            }
        }
        owns = (_uploadReq == req);
        if (owns) _uploadLastMs = now;
        portEXIT_CRITICAL(&_upMux);
        // SAFETY: inhibit camera capture for the whole duration of any upload
        // (CSV/bin/OTA) — no GDMA fb_get and no PSRAM load competing with the
        // flash/SD writes. Lifted by uploadRelease or by the self-heal.
        if (started && _camera) _camera->setInhibited(true);
        // SCREEN FROZEN for the whole duration of an upload (user 2026-07-30).
        // The SD writes done by these callbacks fight the LCD for SPI2
        // (A2.16): the face stuttered while a large file was being
        // transferred. Since the waiting screen is static, it is drawn once
        // and the renderer gives the bus back — the SD card gets it all to
        // itself, and the user can see the robot is busy instead of thinking
        // it crashed.
        //
        // HERE, and not in each route: this is the SINGLE entry point of every
        // upload (bins, sd/put, dances, OTA), and its release is already
        // covered on both sides — `uploadRelease` at the end of the request
        // AND the self-healing in `update()` if the connection dies without
        // `final`. A freeze that never lifts would leave the robot blind until
        // the next reboot, so no exit path may be left unhandled.
        if (started) _renderer.setBusy(true);
        return owns;
    }
    void uploadRelease(AsyncWebServerRequest* req) {
        bool was;
        portENTER_CRITICAL(&_upMux);
        was = (_uploadReq == req);                      // the loser does not release
        if (was) _uploadReq = nullptr;
        portEXIT_CRITICAL(&_upMux);
        if (was && _camera) _camera->setInhibited(false);  // capture resumes
        // End of the freeze — EXCEPT if an OTA has just SUCCEEDED: the waiting
        // screen must cover the flash until the restart, otherwise the face
        // comes back for a second only to vanish again. We test `_updateOk`
        // and not `_rebootAtMs`: the OTA route calls `uploadRelease` BEFORE
        // scheduling the reboot, so at that instant `_rebootAtMs` is still 0
        // while `_updateOk` is already set.
        if (was && !_updateOk) _renderer.setBusy(false);
    }

    // Auth for the upload CALLBACKS: the global Basic Auth middleware only
    // runs at PARSE_REQ_END, AFTER handleUpload has already consumed the body
    // (so after Update.write/Update.end and the SD writes). We therefore check
    // the auth OURSELVES at the head of the upload — otherwise an
    // unauthenticated POST flashes the firmware before the 401 goes out.
    // true = allowed.
    bool uploadAuthed(AsyncWebServerRequest* req) const {
        if (!_authEnabled.load()) return true;          // open API
        return req->authenticate(_apiCreds.username, _apiCreds.password);
    }

    // Toggles the auth — called at boot and by POST /api/security (at runtime,
    // no restart). The hand-rolled middleware reads _apiCreds directly; here
    // we only publish the atomic flag. Logs the STATE, NEVER the password.
    void applyAuth() {
        bool on = strlen(_apiCreds.password) > 0;
        _authEnabled.store(on);              // published for the middleware/status
        Serial.printf(on ? "[api] Basic Auth ACTIVE (utilisateur '%s')\n"
                         : "[api] Basic Auth desactivee (API ouverte)\n",
                      _apiCreds.username);
    }
};

} // namespace sce
