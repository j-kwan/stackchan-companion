#pragma once
// =============================================================================
// SceGuest.h — StackChan-Companion (guest — TO BE EMBEDDED IN YOUR OWN .BIN)
// =============================================================================
// "Guest" stub (ROADMAP §3.6): makes a binary started by the Launcher
// REMOTELY STOPPABLE. Without it, a third-party .bin can only be left by
// holding BtnA at boot (stock SD-Updater behaviour).
//
// This header is NOT part of the companion firmware — you copy it into the
// guest .bin project. Dependencies of the guest project:
//   lib_deps: m5stack/M5Unified, tobozo/M5Stack-SD-Updater
//   (WebServer.h and SD.h: provided by the Arduino-ESP32 core)
//   NO other header of this repository is required — see the I18n block
//   below, which keeps that promise.
//
// Usage in the guest .bin:
//   #include "SceGuest.h"
//   sce::SceGuest guest;
//   void setup() {
//     ... your app init (M5.begin, SD.begin on CS=4, etc.) ...
//     guest.begin();   // reads wifi.client_ssid/client_password from
//                       // /stackchan-companion/config.yaml if the SD is mounted
//   }
//   void loop() { guest.update(); ... }
//
// WiFi AND Basic Auth: reuses directly what is already configured for the
// companion (same file, same SD card — a single place to keep up to date;
// the guest settings page is protected by the console password, no more no
// less). If the file is missing, if its wifi: section is empty, or if the
// SD is not mounted, `begin()` accepts fallback credentials as parameters;
// failing that it falls back to the `SCE-Guest`/`goodlife` AP.
//
// Endpoints exposed by the guest:
//   GET  /              : info page + "back to companion" button
//   GET  /config        : settings form, generated from the fields declared
//                         through addSetting() (absent if the app did not
//                         supply settingGet)
//   POST /config        : settings save (anti-CSRF token)
//   POST /api/bins/stop : reflash /companion.bin + reboot (the "stop"
//                         called by the user or by another agent)
//
// Technical choice: the core's synchronous WebServer (not AsyncWebServer) —
// zero extra dependency, plenty for these few endpoints.
// =============================================================================

#include <Arduino.h>
#include <stdint.h>   // uint8_t — needed by the vendored I18n block below
#include <string.h>   // memcpy — needed by the vendored FirmwareInfo block
#include <stdarg.h>   // va_list — needed by the vendored Trace block
#include <stdio.h>    // vsnprintf — needed by the vendored Trace block
#include <esp_ota_ops.h>  // running partition + app ELF sha256
#include <esp_system.h>   // esp_reset_reason
                      // when this header is copied ALONE (contract in
                      // docs/guests/README.md). In-repo Arduino.h already
                      // drags it in, which is why its absence went unseen.
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>       // captive portal in the AP fallback
#include <Preferences.h>     // WiFi credentials typed on the device (NVS)
#include <SD.h>
#include <functional>
#include <M5Unified.h>       // M5.Touch/M5.Display (exit gesture)
#include <M5StackUpdater.h>

// =============================================================================
// BILINGUAL UI (EN/FR) — `sce::T(en, fr)` picks a form at draw time, English
// is the default AND the fallback. The mechanism belongs to
// firmware/common/I18n.h, which explains the design (no key table: the two
// forms ARE the call, so a language cannot silently go missing).
//
// But this header is documented ABOVE as standalone-copyable: M5Unified +
// SD-Updater and nothing else. `#include "../../firmware/common/I18n.h"`
// would turn that contract into a lie for the third party who drops
// SceGuest.h into their own project.
//
// So: we include the real header WHEN IT IS THERE, and vendor its ten lines
// only when it is not. Inside this repository the `#else` NEVER fires — there
// is exactly ONE definition of `sce::T`, shared with the companion, so the
// companion and its guests cannot end up on two different languages. Outside
// it, the copied file compiles alone.
//
// The `#else` body is a DELIBERATE twin of I18n.h in the sense of A2.23
// (like SdConfig::load / yamlForEach): a copy that is NAMED as a copy, whose
// original is pointed at, and which any change to I18n.h must be carried
// into. It is ten lines with no state beyond one enum, which is what makes
// the duplication affordable — a key table would not have been.
//
// ACCENTS: the on-screen strings below are drawn with the 6x8 bitmap face,
// which renders UTF-8 as "??" — their French carries NO accent ("Reglages").
// The HTML pages are UTF-8 and take proper French.
// =============================================================================
// The ONE YAML line decoder (rule 17). Same discipline as I18n below: shared
// in-repo so the companion and the guests cannot drift, vendored when this
// header is copied ALONE into a third-party project — the contract
// `docs/guests/README.md` states. `scripts/gates/check-vendored.py` asserts the two
// stay identical, so the fallback cannot rot unnoticed.
#if defined(__has_include) && __has_include("../../firmware/common/Yaml.h")
#  include "../../firmware/common/Yaml.h"
#else
// >>> VENDORED FROM firmware/common/Yaml.h — DO NOT EDIT HERE <<<
namespace sce {
namespace yaml {
struct Line { bool ok; bool indented; const char* key; const char* val; };
namespace detail {
inline bool isSpace(char c) { return c == ' ' || c == '\t'; }
inline void rtrim(char* s, char* end) {
    while (end > s && isSpace(end[-1])) end--;
    *end = '\0';
}
}  // namespace detail
inline char* scalar(char* v) {
    while (detail::isSpace(*v)) v++;
    char* end = v;
    while (*end) end++;
    detail::rtrim(v, end);
    if (*v == '"' || *v == '\'') {
        const char q = *v;
        char* r = v + 1;
        char* w = v + 1;
        while (*r && *r != q) {
            if (q == '"' && *r == '\\' && r[1]) r++;
            *w++ = *r++;
        }
        *w = '\0';
        return v + 1;
    }
    for (char* p = v; *p; p++) {
        if (*p == '#') { detail::rtrim(v, p); break; }
    }
    return v;
}
inline Line decodeLine(char* buf) {
    Line out{ false, false, "", "" };
    if (!buf) return out;
    {
        char* w = buf;
        for (char* r = buf; *r; r++) if (*r != '\r') *w++ = *r;
        *w = '\0';
    }
    out.indented = detail::isSpace(buf[0]);
    char* t = buf;
    while (detail::isSpace(*t)) t++;
    if (*t == '\0' || *t == '#') return out;
    char* colon = t;
    while (*colon && *colon != ':') colon++;
    if (*colon != ':') return out;
    char* v = colon + 1;
    detail::rtrim(t, colon);
    out.key = t;
    out.val = scalar(v);
    out.ok  = true;
    return out;
}
}  // namespace yaml
}  // namespace sce
// >>> END VENDORED <<<
#endif

#if defined(__has_include) && __has_include("../../firmware/common/I18n.h")
#  include "../../firmware/common/I18n.h"
#else
// >>> VENDORED FROM firmware/common/I18n.h — DO NOT EDIT HERE <<<
// NEEDS: <stdint.h>
namespace sce {

enum class Lang : uint8_t { EN = 0, FR = 1 };

// Not `static`: this header is included by several translation units and they
// must share ONE language, not one each.
inline Lang g_lang = Lang::EN;

// Accepts what a config file actually contains: "fr", "FR", "fr-FR",
// "francais", "français". Anything else — including an empty or missing
// value — is English, deliberately: the fallback must never depend on
// recognising the string.
inline void setLang(const char* s) {
    g_lang = (s && (s[0] == 'f' || s[0] == 'F')) ? Lang::FR : Lang::EN;
}

inline const char* langCode() { return g_lang == Lang::FR ? "fr" : "en"; }

// The whole API.
inline const char* T(const char* en, const char* fr) {
    return g_lang == Lang::FR ? fr : en;
}


// THE ROBOT'S LANGUAGE, read from the COMPANION's own top-level `lang:` key in
// /stackchan-companion/config.yaml. It is deliberately NOT a per-bin setting: a
// copy in each guest's yaml would be a second thing to keep in step, and two
// language settings that can disagree are not a setting — one robot, one
// language. flight-radar has read it this way since the beginning; extracted
// here on 08-04 when the space bin was found keeping its own copy.
//
// `sectioned = false` ON PURPOSE: in sectioned mode a NON-indented line is
// read as a section header, so a top-level key never reaches the callback.
// Absent or unreadable card: English, which is setLang's own fallback.
//
// Declared as a template on the guest type so this header keeps its ONE
// dependency rule (no include of SceGuest.h, which includes it in turn).
template <typename Guest>
inline void loadCompanionLang() {
    Guest::yamlForEach(Guest::CONFIG_PATH, false,
                       [](void*, const char*, const char* k, const char* v) {
        if (!strcmp(k, "lang")) setLang(v);
    }, nullptr);
}

// ---- THE TIME ZONE, AND WHO GETS THE LAST WORD -----------------------------
// Reported by the user, 08-05: set the UTC offset, go to a guest bin, and the
// clock loses it. Each guest read `tz_offset_h` from its OWN yaml and the
// companion keeps its copy in `config.yaml`; nothing joined the two.
//
// THE OBVIOUS FIX IS THE WRONG ONE, and it was measured before shipping: on
// this robot the companion's stored value is 0.00 (nobody ever turned that
// dial) while both guest yamls say 4.0. "The companion always wins" would
// therefore have dragged two correctly configured bins to UTC — turning a
// missing setting into a broken one.
//
// So the rule is: THE GUEST'S OWN KEY WINS WHEN IT HAS ONE, and the companion
// fills the gap when it does not. A bin whose card never mentioned the offset
// inherits the robot's instead of silently sitting on UTC, and nothing that
// already works changes. `lang` can be companion-only because the guests
// deleted their key; the offset cannot, because theirs is populated.
//
// Returns true when the companion stated an offset; the caller decides.
template <typename Guest>
inline bool companionTzOffsetH(float& out) {
    struct Ctx { float v; bool found; } c{ 0.0f, false };
    Guest::yamlForEach(Guest::CONFIG_PATH, false,
                       [](void* ctx, const char*, const char* k, const char* v) {
        if (!strcmp(k, "tz_offset_h")) {
            Ctx* c = (Ctx*)ctx;
            c->v = (float)atof(v);
            c->found = true;
        }
    }, &c);
    // Clamped here rather than at the call sites. Chatham is +12.75, Baker
    // Island -12; the +/-14 bound is the real one (Kiribati).
    if (c.found) {
        if (c.v < -14.0f) c.v = -14.0f;
        if (c.v >  14.0f) c.v =  14.0f;
        out = c.v;
    }
    return c.found;
}

// ---- TRUNCATION THAT SAYS SO ------------------------------------------------
// Copies `src` into `dst` and, when it did not fit, replaces the last kept
// glyph with a '.' so the cut is VISIBLE. That mark is a project discipline,
// not a detail: a silently truncated name reads as a complete one, and the
// screen then states something false with full confidence. It was written five
// times across the bins with three different marks and two with none at all
// (review 08-04) — this is the one place it lives now.
//
// `maxGlyphs` counts CHARACTERS, which for the 6 px Font0 is the usable pixel
// width divided by six. `dst` must hold maxGlyphs + 1 bytes.
inline void fitGlyphs(char* dst, size_t cap, const char* src, size_t maxGlyphs) {
    if (!dst || cap == 0) return;
    if (maxGlyphs > cap - 1) maxGlyphs = cap - 1;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n <= maxGlyphs) { memcpy(dst, src, n + 1); return; }
    if (maxGlyphs == 0) { dst[0] = '\0'; return; }
    memcpy(dst, src, maxGlyphs);
    dst[maxGlyphs - 1] = '.';            // the cut, made visible
    dst[maxGlyphs] = '\0';
}

// ---- LOCAL WALL CLOCK FROM A UTC INSTANT ------------------------------------
// One conversion, because there were four: a time_t shifted and taken modulo
// 86400, modular-minute arithmetic, the same thing again in Julian days, and
// libc's own TZ via configTime. The last is the odd one out and is why the two
// guest bins disagreed about what `tz_offset_h` means.
//
// THE POLICY, stated once: everything internal is UTC, and the offset is
// applied HERE, at display time, exactly once. Decimal hours cover every real
// zone (India 5.5, Nepal 5.75, Chatham 12.75).
inline void localHhmm(time_t utc, float offsetH, char* out, size_t n) {
    if (!out || n < 6) return;
    const time_t lt = utc + (time_t)lround(offsetH * 3600.0f);
    struct tm t;
    gmtime_r(&lt, &t);                    // gmtime on an ALREADY shifted instant
    snprintf(out, n, "%02d:%02d", t.tm_hour, t.tm_min);
}

inline void localDayHhmm(time_t utc, float offsetH, char* out, size_t n) {
    if (!out || n < 12) return;
    const time_t lt = utc + (time_t)lround(offsetH * 3600.0f);
    struct tm t;
    gmtime_r(&lt, &t);
    snprintf(out, n, "%02d/%02d %02d:%02d",
             t.tm_mday, t.tm_mon + 1, t.tm_hour, t.tm_min);
}

}  // namespace sce
// >>> END VENDORED <<<
#endif

// Which build IS this guest, and why did the board last start. Same discipline
// as the two blocks above: shared in-repo so companion and guests cannot drift,
// vendored when this header is copied ALONE into a third-party project.
//
// A guest needs this MORE than the companion does, not less: `/api/bins/stop`
// reflashes /companion.bin from the SD card, so a guest is one button away from
// replacing the firmware someone just installed over USB. Saying which build is
// running turns that from invisible into checkable.
#if defined(__has_include) && __has_include("../../firmware/common/FirmwareInfo.h")
#  include "../../firmware/common/FirmwareInfo.h"
#else
// >>> VENDORED FROM firmware/common/FirmwareInfo.h — DO NOT EDIT HERE <<<
// NEEDS: <esp_ota_ops.h>
// NEEDS: <esp_system.h>
// NEEDS: <string.h>
namespace sce {
namespace fw {

// Why the chip last started. Printed at boot AND published, because the serial
// line is not an option after the fact: opening it on native USB RESETS the
// board, which destroys the very evidence you opened it to read.
inline const char* resetReasonName() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   return "poweron";    // plug / battery
        case ESP_RST_EXT:       return "ext";        // reset pin
        case ESP_RST_SW:        return "sw";         // ESP.restart: OTA, launcher, /api/reboot
        case ESP_RST_PANIC:     return "panic";      // exception - a CRASH
        case ESP_RST_INT_WDT:   return "int_wdt";    // interrupt watchdog
        case ESP_RST_TASK_WDT:  return "task_wdt";   // a task starved the IDLE task
        case ESP_RST_WDT:       return "wdt";        // other watchdog
        case ESP_RST_DEEPSLEEP: return "deepsleep";
        case ESP_RST_BROWNOUT:  return "brownout";   // supply sagged (servos!)
        case ESP_RST_SDIO:      return "sdio";
        default:                return "unknown";
    }
}

// The OTA slot actually running ("app0" / "app1"). THE decisive field: a flash
// that went to the other slot is invisible in every other way.
inline const char* slot() {
    const esp_partition_t* p = esp_ota_get_running_partition();
    return p ? p->label : "?";
}

// First 8 hex of the app ELF sha256 - a fingerprint of THIS build, and the one
// value that cannot be confused with a neighbouring version.
//
// REPRODUCIBLE FROM A WORKING COPY, which is what makes it useful rather than
// merely unique: this is the sha256 of the .elf file itself, so
//     sha256sum .pio/build/<env>/firmware.elf   (first 8 hex)
// prints the same thing. Verified on target 2026-08-10.
//
// There is deliberately NO build date next to it. The obvious source,
// `esp_ota_get_app_description()->date`, is the date the PRECOMPILED ARDUINO
// LIBRARIES were built - it answered "Mar 5 2024" on a firmware compiled
// minutes earlier. A field that looks authoritative and is wrong is worse than
// no field, and it is the exact failure this whole header exists to end.
inline const char* sha8() {
    static char buf[9] = {0};
    if (!buf[0]) {
        char full[65] = {0};
        // Writes a NUL-terminated hex string, so `size` counts the NUL.
        esp_ota_get_app_elf_sha256(full, sizeof(full));
        memcpy(buf, full, 8);
        buf[8] = '\0';
    }
    return buf;
}

}  // namespace fw
}  // namespace sce
// >>> END VENDORED <<<
#endif

// The debug trace. Same discipline as the three blocks above: shared in-repo,
// vendored when this header travels alone. The guests need it MORE than the
// companion: a standalone Fire in the field has no console but its serial
// port, and the steps worth tracing (join, fetch, fallback) are theirs.
#if defined(__has_include) && __has_include("../../firmware/common/Trace.h")
#  include "../../firmware/common/Trace.h"
#else
// >>> VENDORED FROM firmware/common/Trace.h — DO NOT EDIT HERE <<<
// NEEDS: <stdarg.h>
// NEEDS: <stdio.h>
namespace sce {
namespace trace {

// The switch. Written from HTTP callbacks and read from tasks: volatile is
// enough for a monotonic diagnostic flag - a torn read costs one log line.
inline volatile bool on = false;

// One line: `[dbg][tag] +uptime_ms text`. The tag names the subsystem (net,
// cfg, http, sd, ui, task) so a capture greps clean. 160 bytes is a line
// budget, not a limit to honour: overlong lines truncate, they do not crash.
inline void log(const char* tag, const char* fmt, ...) {
    if (!on) return;
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Serial.printf("[dbg][%s] +%lu %s\n", tag, (unsigned long)millis(), buf);
}

// ---- THE SAME LINE, WHEN THE CALLER MUST NOT BLOCK ------------------------
// `log()` ends in `Serial.printf`, which BLOCKS once the 128-byte UART FIFO
// fills — about 4 ms per line at 115200. That is fine from a task that owns
// its own time and wrong from two places this project has both of:
//
//   - an AsyncTCP callback, where blocking stalls every other connection the
//     same task is serving (rule A2.6);
//   - a section holding a mutex another task waits on, where the UART's
//     latency becomes that task's latency.
//
// Both had grown their own answer, which is how one problem becomes two
// mechanisms. `defer()` records the line and returns; `drain()` prints
// whatever is waiting, and belongs in loop().
//
// A FULL RING DROPS. The trace is a diagnostic: it must never be the reason
// the network is late, and a diagnostic that blocks to avoid losing a line
// has misunderstood which of the two matters. `_dropped` counts what was lost
// so the capture says so instead of quietly missing lines.
//
// The spinlock is taken for a `snprintf` into a fixed buffer and nothing else
// — bounded, no allocation, no I/O — so it is safe from either side.
inline constexpr uint8_t DEFER_N = 12;
inline constexpr size_t  DEFER_LEN = 96;
inline char     _dq[DEFER_N][DEFER_LEN] = {};
inline char     _dqTag[DEFER_N][8] = {};
inline volatile uint8_t _dqHead = 0, _dqTail = 0;
inline volatile uint16_t _dqDropped = 0;
inline portMUX_TYPE _dqMux = portMUX_INITIALIZER_UNLOCKED;

inline void defer(const char* tag, const char* fmt, ...) {
    if (!on) return;
    char buf[DEFER_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    portENTER_CRITICAL(&_dqMux);
    const uint8_t n = (uint8_t)((_dqHead + 1) % DEFER_N);
    if (n == _dqTail) {
        _dqDropped++;                     // full: drop, never block
    } else {
        strlcpy(_dq[_dqHead], buf, DEFER_LEN);
        strlcpy(_dqTag[_dqHead], tag ? tag : "?", sizeof(_dqTag[0]));
        _dqHead = n;
    }
    portEXIT_CRITICAL(&_dqMux);
}

// Called from loop(). Prints outside any lock and outside the network task.
inline void drain() {
    while (_dqTail != _dqHead) {
        log(_dqTag[_dqTail], "%s", _dq[_dqTail]);
        _dqTail = (uint8_t)((_dqTail + 1) % DEFER_N);
    }
    if (_dqDropped) {
        const uint16_t d = _dqDropped;
        _dqDropped = 0;
        log("dbg", "%u ligne(s) de trace perdues (file pleine)", (unsigned)d);
    }
}

}  // namespace trace
}  // namespace sce
// >>> END VENDORED <<<
#endif

// The gesture vocabulary, vendored on the same terms. What this header needs
// from it is ONE number: the distance that means "leave this bin". It used to
// be a literal 100 sitting in the exit test, and every guest bin then chose
// its own swipe threshold in its own file with nothing to compare against —
// which is how the space bin ended up with a 40..99 band where a real drag
// was read as a tap. The constant belongs where the bins can see it.
#if defined(__has_include) && __has_include("../../firmware/common/Gesture.h")
#  include "../../firmware/common/Gesture.h"
#else
// >>> VENDORED FROM firmware/common/Gesture.h — DO NOT EDIT HERE <<<
// NEEDS: <stdint.h>
namespace sce {
namespace gesture {

// ---- THE TWO DISTANCES -----------------------------------------------------
// A bin's own swipe. 60 px on a 320x240 panel is about a fifth of the screen:
// far enough that a shaky tap is not one, short enough that a thumb can do it
// without crossing the panel. It is the radar's value, the one earned on
// hardware first and the one the other bins claimed to be copying.
inline constexpr int SWIPE_PX = 60;

// SceGuest's exit (downward, vertically dominant). NOT the same kind of
// number: it is deliberately far above SWIPE_PX so leaving a bin cannot be
// done by accident, and every bin's own gestures must fit underneath.
inline constexpr int EXIT_PX = 100;

// ---- THE PRESS -------------------------------------------------------------
// A long press is 700 ms everywhere and always was; what diverged is what
// counts as "the finger stayed put" (12 px on the radar, 20 in space) and
// whether anything on screen says the press was SEEN. The radar grew that
// acknowledgement on a user report: with no feedback you lift the finger and
// cancel your own gesture.
inline constexpr uint32_t LONG_MS = 700;
inline constexpr uint32_t LONG_ARM_MS = 250;   // when the screen should answer
inline constexpr int      LONG_SLOP_PX = 12;   // drift still counted as still

// ---- THE CLASSIFICATION ----------------------------------------------------
enum class Dir : uint8_t { None, Up, Down, Left, Right };

inline int absi(int v) { return v < 0 ? -v : v; }

// Did the finger travel far enough to mean anything, whatever it meant?
//
// Asked separately from `classify` on purpose. A caller that only checks the
// DIRECTION gets `None` for two unrelated situations — "barely moved" and "a
// direction this screen does nothing with" — and if it then falls through to
// its tap branch, a real drag is resolved at the point where the finger went
// DOWN. One classification, two questions about it.
inline bool isSwipe(int dx, int dy, int px = SWIPE_PX) {
    return absi(dx) >= px || absi(dy) >= px;
}

// ONE tie-break rule, and it favours the VERTICAL axis. An exact diagonal has
// to go somewhere; vertical is where the primary navigation lives in every bin
// that has one (the view ring), so a 45-degree drag pages views rather than
// doing nothing. The radar used to answer "neither" and the space bin already
// answered "vertical" — the rule was never the disagreement, only its absence.
inline Dir classify(int dx, int dy, int px = SWIPE_PX) {
    if (!isSwipe(dx, dy, px)) return Dir::None;
    if (absi(dy) >= absi(dx)) return dy < 0 ? Dir::Up : Dir::Down;
    return dx < 0 ? Dir::Left : Dir::Right;
}

}  // namespace gesture
}  // namespace sce
// >>> END VENDORED <<<
#endif

namespace sce {

// ---------------------------------------------------------------------------
// CoopStop -- cooperative stop of a bin's background task before a reflash.
//
// All three shipped bins hand-rolled this pair of flags, and the copies
// drifted four ways (window size, warning, fresh-ACK reset, resume after a
// FAILED reflash -- one bin had it, two stayed parked forever). One
// implementation, one behaviour; the per-bin ACK window stays the one
// legitimate variation — and it is a TRADE, not a measurement. See windowMs.
//
// COOPERATIVE and never vTaskSuspend: suspending a task that holds a mutex
// leaves whoever waits on it frozen forever if the flash then fails
// (flight-radar lesson, 07-27). And parking matters at all because the one
// bin that skipped it turned a ~9 s reflash into >10 min of SD/SPI contention
// with HTTP dark (space, 2026-08-10).
//
// Usage (see docs/guests/README.md):
//   static sce::CoopStop netGuard;            // bin-level, next to the task
//   task loop head:    if (netGuard.shouldPark()) continue;
//   HTTP helpers:      if (netGuard.stopping()) return false;
//   setup():           netGuard.windowMs = 25000;
//                      guest.netGuard = &netGuard;
// SceGuest calls requestAndWait() before the reflash and release() if the
// reflash fails, so the guest resumes instead of staying half-dead.
// ---------------------------------------------------------------------------
struct CoopStop {
    // ACK window: by default, the bin's LONGEST single request (connect + read
    // timeouts). A helper that refuses new work once stopping() is up keeps a
    // request CHAIN from outliving it.
    //
    // IT IS A CHOICE BETWEEN TWO COSTS, not a measurement, and the three bins
    // do not choose the same way — so the alternative is named here rather
    // than left looking like one of them got it wrong:
    //
    //   WAIT IT OUT (space 25 s, ha-remote 20 s). The task is guaranteed to be
    //   parked before the flash, so nothing competes for heap or SPI. The
    //   price is up to that long staring at a bin that has already said it is
    //   leaving.
    //
    //   FLASH ANYWAY (flight-radar 3 s). The reflash starts while a GET may
    //   still be in flight: some heap pressure at the worst possible moment,
    //   against a return to the companion that feels immediate. That bin
    //   states the trade at its own `windowMs`.
    //
    // What must NOT happen is neither: a bin with no guard at all turned a ~9 s
    // reflash into >10 minutes of SD/SPI contention with HTTP dark (space,
    // 2026-08-10). check-mirrors.py verifies the guard is WIRED, which is the
    // part that has actually gone missing; the window is the author's call.
    uint32_t windowMs = 15000;
    volatile bool stop   = false;
    volatile bool parked = false;   // ACK: the task is parked, outside any lock

    // Task loop head. True = the task must `continue` (the ACK and the pacing
    // delay already happened here); false = normal pass, ACK cleared.
    bool shouldPark() {
        if (!stop) { parked = false; return false; }
        parked = true;                       // OUTSIDE of any lock
        vTaskDelay(pdMS_TO_TICKS(50));       // pace the parked spin
        return true;
    }

    // For HTTP helpers: refuse to OPEN anything new during a stop.
    bool stopping() const { return stop; }

    // Raise the stop and wait -- bounded -- for the ACK. If the task never
    // parks we flash anyway: heap pressure is a risk, losing the only road
    // back to the companion is worse (adversarial check 07-27).
    void requestAndWait() {
        parked = false;                      // FRESH ack, not a leftover
        stop = true;
        uint32_t t0 = millis();
        while (!parked && millis() - t0 < windowMs) delay(20);
        trace::log("task", "garde reseau: %s en %lu ms",
                   parked ? "garee" : "PAS GAREE",
                   (unsigned long)(millis() - t0));
        if (!parked)
            Serial.println("[guest] ATTENTION : tache reseau non garee, "
                           "reflash risque");
    }

    // The reflash FAILED and the guest lives on: resume the task. Explicit
    // and deterministic -- this replaces the radar's old 15 s self-healing
    // timer, which the other two bins never copied.
    void release() { stop = false; }
};



class SceGuest {
public:
    static constexpr const char* COMPANION_PATH = "/companion.bin";
    static constexpr const char* CONFIG_PATH    = "/stackchan-companion/config.yaml";

    // ------------------------------------------------------------------
    // WEB CONFIGURATION (optional) — a guest bin DECLARES its settings,
    // SceGuest renders the form and handles the save. Motivation: tuning an
    // app with a finger on 320 px is painful, and otherwise every bin
    // rewrote its own server + its own HTML.
    //
    //   guest.addSetting("radius_nm", "Rayon", Num, 10, 500);
    //   guest.addSetting("api", "Source", Choice, 0, 0,
    //                    "airplanes.live|adsb.lol|adsb.fi");
    //   guest.addSetting("servo", "Tete pointee vers le vol", Bool);
    //   guest.addSetting("token", "Jeton", Secret);   // never displayed back
    //   guest.settingGet = [](const char* k) -> String { ... };
    //   guest.settingSet = [](const char* k, const String& v) { ... };
    //
    // The app keeps ownership of its storage: SceGuest only reads/writes
    // through these two lambdas. Without `settingGet` the page is not
    // exposed at all.
    // ------------------------------------------------------------------
    // `Secret` = SENSITIVE value (API token, password). It is NEVER
    // displayed back: the field starts empty and an empty submission means
    // "unchanged". A home-automation token opens the WHOLE installation, and
    // this page runs on the local network, sometimes without a password —
    // echoing it in the field value was enough to leak it (review finding
    // 07-28).
    enum Kind { Num, Bool, Text, Choice, Secret };
    // REVOCATION sentinel for a `Secret`: entering it clears the value.
    // Without it, "empty = unchanged" made clearing impossible from the UI —
    // you had to pull the SD card out to revoke a compromised token (review
    // finding 07-28).
    static constexpr const char* SECRET_CLEAR = "-";
    // 24, and the history of this number is the argument for the margin: it
    // was 16 until flight-radar's `metar_rwy` took the last slot, then 20 —
    // and on 2026-08-01 `notam_fir` filled the twentieth exactly. Each time the
    // bound was reached by the feature that needed it, which is the worst
    // moment to discover a cap whose only symptom is a SERIAL warning: the
    // field simply is not on the form, and nobody is reading the log.
    // Cost of the margin: 4 x sizeof(SettingDef) (~100 bytes of static RAM),
    // paid once per guest bin.
    static constexpr int MAX_SETTINGS = 24;

    void addSetting(const char* key, const char* label, Kind kind,
                    float lo = 0, float hi = 0, const char* choices = nullptr) {
        if (_nSet >= MAX_SETTINGS) {   // silent = field lost without a trace
            Serial.printf("[sceguest] reglage '%s' IGNORE (max %d)\n",
                          key ? key : "?", MAX_SETTINGS);
            return;
        }
        // A leading underscore is RESERVED for SceGuest's own form fields
        // (`_sce`, `_wifi_*`). Silently accepting one would let an app setting
        // collide with the network form, and the symptom would be a robot that
        // changes network when you save an unrelated option.
        if (key && key[0] == '_') {
            Serial.printf("[sceguest] reglage '%s' REFUSE : prefixe '_' reserve\n",
                          key);
            return;
        }
        _set[_nSet++] = { key, label, kind, lo, hi, choices };
    }
    // Read/write of one setting — supplied by the app.
    std::function<String(const char*)>               settingGet;
    std::function<void(const char*, const String&)>  settingSet;
    // Bracket the application of a submission: the app can take ITS lock
    // ONCE there (otherwise every field was a separate critical section and
    // a concurrent task could read a half-applied config).
    std::function<void()>                            onSettingsBegin;
    // Called AFTER all fields have been applied (that is where the app
    // persists: SceGuest knows neither its format nor its medium).
    // Returning false signals a FAILURE: the page shows it instead of a
    // "Saved" message.
    std::function<bool()>                            onSettingsSaved;

    // HTTP Basic authentication for the settings page (optional, same
    // spirit as the companion API). Without credentials the page is open on
    // the local network — acceptable for a toy, NOT for exposing the robot:
    // the settings drive the head.
    void setAuth(const char* user, const char* pass) {
        _authUser = user ? user : "";
        _authPass = pass ? pass : "";
    }

    // STA connection — credentials read from the wifi: section of
    // /stackchan-companion/config.yaml (same file as the companion) when the SD
    // is mounted: client_ssid/client_password for STA, ap_ssid/ap_password
    // for the AP fallback (each used only if present and non-empty,
    // independently of the other). Failing that: fallbackSsid/fallbackPass
    // for STA, "SCE-Guest"/"goodlife" for the AP.
    // STA timeout 10 s. Returns the IP that was obtained.
    String begin(const char* fallbackSsid = "", const char* fallbackPass = "",
                 const char* hostname = "sce-guest") {
        String ssid = fallbackSsid;
        String pass = fallbackPass;
        String apSsid = "SCE-Guest";
        String apPass = "goodlife";
        String apiUser, apiPass;
        loadLang();                  // UI language, same config.yaml
        readSdCreds(ssid, pass, apSsid, apPass, apiUser, apiPass);
        sce::trace::log("net", "creds carte: ssid='%s' ap='%s'",
                        ssid.c_str(), apSsid.c_str());
        // CREDENTIALS TYPED ON THE DEVICE WIN. A bin running standalone — no
        // companion, and often no card worth editing — has exactly one way to
        // be told about a network: somebody types it into /config while the
        // bin is serving its own access point. That is a deliberate act on
        // this unit, so it outranks whatever the card happens to carry;
        // anything else and the field would silently do nothing on a machine
        // whose config.yaml still names the old network. The card stays the
        // PROVISIONING source (write the file once, flash ten robots), and
        // "Forget" on the page clears the override to fall back to it.
        {
            Preferences p;
            if (p.begin(NVS_NS, true)) {         // read-only
                // The debug trace lives in NVS for the same reason the network
                // override does: it is a property of THIS unit, set while
                // somebody was debugging THIS unit, and a reflash or a swapped
                // card must not silence it mid-hunt.
                sce::trace::on = p.getBool("dbg", false);
                String ns = p.getString("ssid", "");
                if (ns.length() > 0) {
                    ssid = ns;
                    pass = p.getString("pass", "");
                    sce::trace::log("net", "override NVS: ssid='%s'", ssid.c_str());
                }
                p.end();
            }
        }
        // Companion console credentials, unless the app forced other ones
        // through setAuth() BEFORE begin().
        if (_authUser.length() == 0 && apiPass.length() > 0) {
            _authUser = apiUser;
            _authPass = apiPass;
        }

        WiFi.mode(WIFI_STA);
        WiFi.setHostname(hostname);
        sce::trace::log("net", "STA join '%s'...", ssid.c_str());
        WiFi.begin(ssid.c_str(), pass.c_str());
        uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) delay(250);
        sce::trace::log("net", "STA %s en %lu ms (status %d, rssi %d)",
                        WiFi.status() == WL_CONNECTED ? "OK" : "ECHEC",
                        (unsigned long)(millis() - t0), (int)WiFi.status(),
                        (int)WiFi.RSSI());

        // Anti-CSRF token, drawn AFTER the radio is up: `esp_random()` is a
        // true generator only once Wi-Fi/BT is active — before that it
        // returns a WEAK sequence, reproducible from one boot to the next
        // and from one unit to the next. A predictable token protects
        // nothing, which cancelled exactly the guard we wanted (max review
        // 07-28).
        _csrf = "";
        for (int i = 0; i < 4; i++) {
            char h[9];
            snprintf(h, sizeof(h), "%08x", (unsigned)esp_random());
            _csrf += h;
        }

        String ip;
        if (WiFi.status() == WL_CONNECTED) {
            ip = WiFi.localIP().toString();
        } else {
            WiFi.mode(WIFI_AP);
            WiFi.softAP(apSsid.c_str(), apPass.c_str());
            ip = WiFi.softAPIP().toString();
            sce::trace::log("net", "repli AP '%s' ip=%s", apSsid.c_str(),
                            ip.c_str());
            _apMode = true;
            _apSsid = apSsid;
            // CAPTIVE PORTAL. The access point is the ONLY way back for a bin
            // that cannot reach the network, and reaching it means knowing an
            // address nothing has displayed on a phone. Answering every name
            // with our own address turns "join the network" into a page that
            // opens by itself. Cheap: one UDP socket, and only in the branch
            // where nothing else is happening anyway.
            _dns.setErrorReplyCode(DNSReplyCode::NoError);
            _dnsUp = _dns.start(53, "*", WiFi.softAPIP());
        }

        // Home page styled with the companion console THEME (Liquid Glass —
        // same CSS variables as WebConsole.h, to be kept in sync BY HAND:
        // SceGuest is a STANDALONE stub copied into third-party bins, no
        // shared include). Glass card centred H+V (user 07-27).
        _server.on("/", HTTP_GET, [this]() {
            // NOTHING TO GO BACK TO -> go where the user actually needs to be.
            // This page exists to offer ONE action: reflash the companion from
            // the SD card. Without a valid /companion.bin that button cannot
            // work, and the card's text ("a guest is running in place of the
            // companion firmware") describes a situation that does not exist.
            // It was not merely useless, it was misleading, and the failure
            // only surfaced on the click.
            //
            // The test is at RUNTIME and not on a build flag, deliberately: a
            // CoreS3 whose SD card is missing or dead is in exactly the same
            // position as a Fire that never had one, and a compile-time flag
            // would have covered only the second. Same rule as everywhere else
            // here - the reason shown must come from the same source as the
            // decision it describes.
            if (!companionImageOk()) {
                _server.sendHeader("Location", "/config");
                _server.send(302);
                return;
            }
            // Built as a String and not sent as one literal: the page is
            // bilingual, so the text chunks come from sce::T. The chrome
            // (CSS) stays in flash through F().
            String p;
            p.reserve(2400);
            p += F("<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>");
            p += shownName();
            p += F(" - ");
            p += sce::T("guest bin", "bin invit&eacute;");
            p += F("</title><style>"
                ":root{--txt:#e8eefb;--mut:#93a1bd;--acc:#22d3ee;--acc2:#818cf8;"
                "--glass:rgba(255,255,255,.055);--bord:rgba(255,255,255,.12)}"
                "*{box-sizing:border-box;margin:0}"
                "body{font-family:system-ui;background:#0a0d16;color:var(--txt);"
                "min-height:100vh;display:flex;align-items:center;justify-content:center}"
                "main{background:var(--glass);backdrop-filter:blur(22px) saturate(1.5);"
                "border:1px solid var(--bord);border-radius:6px;padding:36px 44px;"
                "text-align:center;box-shadow:0 18px 50px rgba(0,0,0,.45)}"
                "h2{font-size:1.15rem;font-weight:600;letter-spacing:.4px}"
                "h2 b{background:linear-gradient(120deg,var(--acc),var(--acc2));"
                "-webkit-background-clip:text;background-clip:text;color:transparent}"
                "p{color:var(--mut);font-size:.85rem;margin:8px 0 22px}"
                "button{background:linear-gradient(120deg,var(--acc),var(--acc2));"
                "color:#06121f;border:0;border-radius:3px;padding:12px 22px;"
                "font:600 .95rem system-ui;cursor:pointer;"
                "box-shadow:0 0 14px rgba(34,211,238,.35)}"
                "button:hover{filter:brightness(1.1)}"
                "</style></head><body><main><h2>");
            p += sce::T("Guest bin", "Bin invit&eacute;");
            p += F(" <b>"); p += shownName(); p += F("</b></h2><p>");
            p += sce::T(
                "A guest application is currently running on your StackChan "
                "in place of the companion firmware.<br>"
                "The button below reflashes the companion from the SD card "
                "then reboots the robot (about 30 seconds).<br>"
                "A long swipe down on the robot screen does the same.",
                "Une application invit&eacute;e s'ex&eacute;cute actuellement "
                "sur votre StackChan &agrave; la place du firmware companion.<br>"
                "Le bouton ci-dessous reflashe le companion depuis la carte SD "
                "puis red&eacute;marre le robot (environ 30 secondes).<br>"
                "Un glissement long vers le bas sur l'&eacute;cran du robot "
                "produit le m&ecirc;me retour.");
            p += F("</p><form method='POST' action='/api/bins/stop'><button>");
            p += sce::T("Back to companion", "Retour au companion");
            p += F("</button></form>"
                "<p style='margin:18px 0 0'><a href='/config' "
                "style='color:#22d3ee'>");
            p += sce::T("Settings", "R&eacute;glages");
            p += F("</a></p></main></body></html>");
            _server.send(200, "text/html", p);
        });
        // This endpoint reflashes the companion and reboots the robot: it is
        // STRICTLY more destructive than the settings form, so it must be
        // protected too. But NOT by the form token — it is called by the
        // companion console, by scripts and by other agents, none of which
        // can know it: requiring it broke remote control, the whole point of
        // the stub (found while deploying, 07-28).
        // So we filter on the ORIGIN: a browser always sends `Origin` on a
        // cross-origin POST, an API client does not. Missing origin = this is
        // not a booby-trapped web page; origin present and foreign = refused.
        _server.on("/api/bins/stop", HTTP_POST, [this]() {
            if (!authOk()) return;
            if (!originOk()) {
                _server.send(403, "text/plain",
                             sce::T("origin refused", "origine refusee"));
                return;
            }
            if (!companionImageOk()) {        // missing OR truncated/invalid
                _server.send(404, "application/json",
                    "{\"error\":\"/companion.bin absent ou invalide\"}");
                return;
            }
            _server.send(200, "application/json",
                         "{\"ok\":true,\"note\":\"reflash companion + reboot\"}");
            delay(300);                       // let the response go out
            stopToCompanion();
        });
        // Settings page (only if the app declared some settings)
        _server.on("/config", HTTP_GET,  [this]() {
            if (authOk()) sendConfigPage();
        });
        _server.on("/config", HTTP_POST, [this]() {
            if (!authOk()) return;
            // ANTI-CSRF TOKEN, drawn at boot. The previous constant marker
            // (`_sce=1`) did block the empty POST, but any web page opened by
            // the user could reproduce it in a cross-origin form. The stake
            // is not theoretical: by rewriting `host`, the attacker makes the
            // robot send the home-automation token to the server of his
            // choice — a token he does not even need to know, since a
            // `Secret` left unsupplied stays unchanged (review finding
            // 07-28). An unpredictable secret in the body cannot be forged
            // blind.
            if (_server.arg("_sce") != _csrf) {
                _server.send(403, "text/plain",
                             sce::T("invalid form token - reload /config",
                                    "jeton de formulaire invalide - rechargez /config"));
                return;
            }
            // The lock taken in onSettingsBegin has only ONE release point:
            // onSettingsSaved. Declaring the first without the second would
            // freeze the robot on the very first save, with no way out. We
            // refuse the incomplete pair instead of suffering it — this
            // header is copied as-is into third-party bins.
            if (onSettingsBegin && !onSettingsSaved) {
                Serial.println("[sceguest] onSettingsBegin SANS onSettingsSaved "
                               ": enregistrement REFUSE (verrou non liberable)");
                _server.send(500, "text/plain",
                             sce::T("misconfigured bin: onSettingsSaved missing",
                                    "bin mal configure : onSettingsSaved manquante"));
                return;
            }
            // THE NETWORK IS SAVED FIRST, and outside the app's lock: it is
            // not the app's setting, it lives in NVS rather than on the card,
            // and it must survive a bin whose own save fails. A user who has
            // just typed a network on a stranded robot must not lose it
            // because an unrelated write to a missing SD card returned false.
            applyWifiForm();
            applyDebugForm();

            if (onSettingsBegin) onSettingsBegin();
            for (int i = 0; i < _nSet; i++) {
                if (!_server.hasArg(_set[i].key) || !settingSet) continue;
                String v = _server.arg(_set[i].key);
                if (_set[i].kind == Secret) {
                    // Empty = "unchanged": the page never displays a secret
                    // back, so a save must not wipe it. It must still be
                    // POSSIBLE to revoke it without pulling the SD card out —
                    // hence the explicit sentinel.
                    if (v.length() == 0) continue;
                    if (v == SECRET_CLEAR) v = "";
                }
                settingSet(_set[i].key, v);
            }
            for (int i = 0; i < _nSet; i++)   // unchecked box = absent = 0
                if (_set[i].kind == Bool && !_server.hasArg(_set[i].key) &&
                    settingSet)
                    settingSet(_set[i].key, "0");
            bool ok = onSettingsSaved ? onSettingsSaved() : true;
            _server.sendHeader("Location", ok ? "/config?ok=1" : "/config?ko=1");
            _server.send(303);
        });
        // ---- THE OTHER HALF OF THE CAPTIVE PORTAL --------------------------
        // Wildcard DNS alone does not open anything. It only guarantees that
        // whatever name the phone asks for resolves HERE; what decides whether
        // a portal appears is the ANSWER to the probe that follows, and every
        // operating system probes a URL of its own:
        //
        //   Android  /generate_204, /gen_204      expects 204 No Content
        //   Apple    /hotspot-detect.html         expects a body saying Success
        //   Windows  /connecttest.txt, /ncsi.txt  expects a fixed string
        //   Firefox  /canonical.html              expects a known body
        //
        // None of those is a route here, so they all fell through to the
        // WebServer's built-in 404. A 404 does tell the OS that the network is
        // not what it expected — Android and Windows would eventually raise a
        // "sign in" notice — but Apple SHOWS the returned page in its portal
        // sheet, so what a user got was the words "Not found" where the
        // configuration form should have been. The DNS half was written and
        // the documentation described the whole thing as working.
        //
        // Answering with a REDIRECT settles all four at once: it is not the
        // expected body for anybody, so every OS concludes there is a portal,
        // and the address it points at is the page the user actually needs.
        //
        // ONLY IN AP MODE, and that limit is load-bearing. On a joined network
        // this same handler would turn every typo and every stale bookmark
        // into a silent redirect to the settings page — a missing route has to
        // stay a missing route, or the next 404 anybody looks for is a page
        // that quietly answers 302 instead.
        //
        // The target is spelled with the AP's IP rather than a name: the whole
        // situation is that no name resolves. `Location` therefore carries the
        // one address that is true here, which is also the address printed on
        // the no-network screen — a user who ignores the pop-up and types it by
        // hand ends up in exactly the same place.
        if (_apMode) {
            const String portal = "http://" + WiFi.softAPIP().toString() + "/config";
            _server.onNotFound([this, portal]() {
                _server.sendHeader("Location", portal);
                // 302 and not 303: a 303 rewrites the method to GET, which is
                // right for a form but wrong here, where the probe may be a
                // HEAD and some clients treat the change as a protocol error.
                // The body is what Apple's sheet shows if it declines to
                // follow the header, so it is a link and not an empty string.
                _server.send(302, "text/html",
                             "<meta http-equiv='refresh' content='0;url=" + portal +
                             "'><a href='" + portal + "'>" + portal + "</a>");
            });
        }

        // Without this collection, `header("Origin")` always returns empty
        // and the origin filter would let everything through.
        static const char* HDRS[] = { "Origin" };
        _server.collectHeaders(HDRS, 1);
        _server.begin();
        return ip;
    }

    // Call this from loop(). Serves the HTTP AND (by default) the exit
    // gesture: SWIPE DOWN (top→bottom, ≥ 100 px, vertically dominant) →
    // on-screen confirmation → reflash of /companion.bin. Symmetric with the
    // companion (swipe down = launcher). Requires the app to call M5.update()
    // in its loop() (standard M5) — otherwise the gesture is simply inert.
    void update() {
        if (_dnsUp) _dns.processNextRequest();
        _server.handleClient();
        if (_swipeExit) pollSwipeExit();
    }

    // True when the STA connection failed and the bin is serving its own
    // access point — the state in which a bin has NO network and the only
    // useful thing it can say on screen is which network to join.
    bool isAp() const { return _apMode; }
    const String& apSsid() const { return _apSsid; }

    // ------------------------------------------------------------------
    // NO CARD — one screen, for every guest bin
    // ------------------------------------------------------------------
    // Two bins had written this by hand and the second was a copy of the
    // first, which is rule 17's exact failure mode: the twins had already
    // started to differ (the radar drew its title on a red bar, `space` on
    // another row; one said "check FAT32", the other did not).
    //
    // SceGuest owns the MECHANISM — layout, the retry loop, the input
    // routing, the single `drawString` call site (A2.22), the wording of what
    // is true for EVERY bin. The bin supplies only its own CONSEQUENCES,
    // which is the part that is genuinely its own and the reason the screen
    // is worth showing at all: "settings are lost" is generic, "the observer
    // falls back to Paris" is not.
    //
    // The bin also supplies the REMOUNT: SceGuest must not learn anybody's SD
    // pins, and the caller already has them.
    //
    // THE CONSEQUENCES ARE OPTIONAL. `guest.noSdNotice(remount)` alone gives a
    // complete, honest screen — everything the generic rows state is true of
    // every bin that uses this header, so a new bin gets a working notice on
    // day one and adds its own lines when it has something to add. A screen
    // that only works once you have written five strings is a screen new bins
    // will not have.
    //
    // Returns TRUE when a card was mounted on a retry — the caller must then
    // re-read whatever it read before, because all of that ran against no
    // card and loaded nothing.
    // ROWS, and the arithmetic spelled out because it is a stack array and I
    // already got it wrong once: the text area holds TEN rows (they start at
    // y=66 on a 16 px pitch; the tenth ends at 222, where the failed-retry
    // BANNER sits — that alert is a red band drawn with the title's loop, not
    // a row of this table). Five rows are fixed — three generic, two actions
    // — which leaves five for the bin.
    static constexpr int NOSD_FIXED   = 5;
    static constexpr int NOSD_MAX_WHY = 5;
    static constexpr int NOSD_ROWS    = NOSD_MAX_WHY + NOSD_FIXED;   // 10

    bool noSdNotice(std::function<bool()> remount,
                    const char* const* why = nullptr, int nWhy = 0) {
        if (nWhy > NOSD_MAX_WHY) nWhy = NOSD_MAX_WHY;
        bool failed = false, dirty = true;
        for (;;) {
            if (dirty) { paintNoSd(why, nWhy, failed); dirty = false; }
            M5.update();
            bool retry = false, go = false;
            // BOTH input paths, unconditionally: a board has a touch panel or
            // it has buttons, and the one it lacks simply never fires. Only
            // the TEXT has to choose, and it asks the panel itself rather
            // than a build flag — the same binary then says the right thing
            // on either board.
            if (M5.Touch.getCount() > 0) {
                const auto t = M5.Touch.getDetail(0);
                if (t.wasPressed()) { if (t.x < 160) retry = true; else go = true; }
            }
            if (M5.BtnA.wasPressed())                         retry = true;
            if (M5.BtnB.wasPressed() || M5.BtnC.wasPressed()) go    = true;

            if (retry) {
                const bool ok = remount ? remount() : false;
                Serial.printf("[sceguest] SD reessai : %s\n",
                              ok ? "montee" : "rien");
                if (ok) { M5.Display.fillScreen(TFT_BLACK); return true; }
                failed = true;
                dirty  = true;
            }
            if (go) break;
            delay(20);
        }
        M5.Display.fillScreen(TFT_BLACK);
        return false;
    }

    // true ONCE if a modal (exit confirmation) has just consumed the touch
    // frame: the app must then SKIP its own tap handling, otherwise the "No"
    // is replayed as a tap in its own interface (flight-radar: tracking lost,
    // max review 07-27).
    bool consumedTouch() { bool e = _touchEaten; _touchEaten = false; return e; }

    // Gesture exit is ON BY DEFAULT — can be disabled in code if the bin
    // wants to keep every swipe for itself: guest.setSwipeExit(false).
    void setSwipeExit(bool on) { _swipeExit = on; }

    // ZONE limit of the exit gesture: only swipes STARTING at
    // x < swipeExitMaxX trigger the exit (default: the whole screen).
    // Lets the app reserve a column for its own swipes
    // (e.g. flight-radar: right panel = cycle through the flights, 07-26).
    int swipeExitMaxX = 9999;

    // NAME OF THE GUEST APPLICATION, as shown in the browser tab and on the
    // home card — "flight-radar", "ha-remote". Set it BEFORE begin().
    //
    // The pages used to say "StackChan", which names the HOST and not the
    // thing you are configuring: with two guest bins installed, two browser
    // tabs and two bookmarks were indistinguishable. The bin name is the same
    // one the launcher and the SD card use, so there is nothing new to learn.
    //
    // Empty keeps the old wording, on purpose: this header is copied
    // STANDALONE into third-party bins, and a new field must not change what
    // an existing copy renders.
    String appName;

    // The name to PRINT: the app's own when it set one, the historical
    // "StackChan" otherwise. One place, so the three pages cannot disagree.
    String shownName() const { return appName.length() ? appName : String("StackChan"); }

    // Called just BEFORE the companion reflash (HTTP stop or swipe): the
    // guest app can suspend its tasks there (network/TLS/SD) — an SD→OTA
    // flash competing with TLS allocations can fail for lack of heap (07-26).
    std::function<void()> onBeforeStop;

    // The bin's background-task guard (optional -- REQUIRED by the contract
    // as soon as the bin creates a task, docs/guests/README.md). SceGuest
    // parks it before the reflash and releases it if the reflash fails;
    // onBeforeStop stays for anything ELSE the app must quiesce.
    CoopStop* netGuard = nullptr;

    // ------------------------------------------------------------------
    // Theme of the SD-Updater LOBBY (the wait screen at boot: [Companion] /
    // [Continuer] / [SauvFW]). Call it BEFORE checkSDUpdater(). Aligns the
    // lobby with the companion launcher (`src/app/Launcher.h`) and with the
    // web console: black background, cyan title + gradient rule, glass card,
    // rounded buttons — a single visual language from boot to app.
    //
    // ⚠ The palette is DUPLICATED from Launcher.h / WebConsole.h: this header
    // is copied STANDALONE into third-party bins, no factoring possible
    // (same contract as companionImageOk).
    // ------------------------------------------------------------------
    // `appName` null = the generic name, resolved AFTER loadLang() (a default
    // argument would have been evaluated at the call site, i.e. before the
    // language is known).
    static void applyLobbyTheme(const char* appName = nullptr) {
        loadLang();                         // the lobby comes BEFORE begin()
        _lobbyApp = appName ? appName : T("Guest bin", "Bin invite");
        SDUCfg.setLabelMenu("Companion");   // loads /companion.bin
        SDUCfg.setLabelRollback("Companion");
        SDUCfg.setLabelSkip(T("Continue", "Continuer"));  // stays on the guest
        SDUCfg.setSplashPageCb(lobbySplash);
        // ⚠ NOT setButtonDrawCb: on CoreS3 (touch screen) the library takes
        // the TOUCH path, which draws its own buttons (y 96-144, full width)
        // WITHOUT ever calling that callback — they were painting over the
        // splash card (max review 07-27). So we replace the WHOLE WAIT, the
        // only way to control buttons AND touch zones.
        SDUCfg.setWaitForActionCb(lobbyWait);
        // The FLASH SCREEN too: SD-Updater draws it, with its own identity
        // (grey background, green bar). Dressing up the lobby while leaving
        // that one raw broke the visual continuity at the most visible moment
        // of all — the return to the companion (user 07-28).
        SDUCfg.setProgressCb(flashProgress);
        SDUCfg.setMessageCb(flashMessage);
        SDUCfg.setErrorCb(flashError);
    }

    // The no-card screen, painted. Same visual language as the lobby and the
    // flash screen — one identity from boot to app, which is the argument the
    // lobby already made.
    //
    // ONE `drawString` call site for every row (A2.22): the rows are resolved
    // into a table first, and only the colour is set per row before the single
    // call. Written as one call per row, GCC 8.4 Xtensa is entitled to drop
    // some — and the row it dropped would be a warning nobody would ever see
    // missing. The title uses `print`, a different primitive, so it costs no
    // second site.
    __attribute__((noinline))
    void paintNoSd(const char* const* why, int nWhy, bool failed) {
        auto& d = M5.Display;
        const bool touch = M5.Touch.isEnabled();

        const char* row[NOSD_ROWS];
        uint16_t    col[NOSD_ROWS];
        int n = 0;
        // Bounded at the WRITE, not only at the caller: `nWhy` is clamped on
        // entry, but a row added here later would otherwise walk off a stack
        // array with nothing to stop it.
        auto add = [&](const char* s, uint16_t c) {
            if (n >= NOSD_ROWS) return;
            row[n] = s; col[n++] = c;
        };

        // THE GENERIC ROWS, true of every bin that includes this header — so a
        // bin that supplies nothing still gets a screen worth reading. The
        // network is called out because it is the ONE thing a card-less board
        // keeps: it lives in NVS, which survives both a missing card and a
        // reflash, and without saying so the screen would read as "you are
        // stuck on the access point for ever".
        add(T("Nothing can be saved to the card: what you set on",
              "Rien ne peut aller sur la carte : ce qui est regle"), L_MUT);
        add(T("/config applies now and is LOST at the next boot.",
              "sur /config s'applique et sera PERDU au redemarrage."), L_MUT);
        add(T("The network is the exception - it is kept in NVS.",
              "Le reseau fait exception - il est garde en NVS."), L_MUT);
        for (int i = 0; i < nWhy && why; i++) add(why[i], TFT_WHITE);   // bin's own
        add(touch ? T("LEFT half = I inserted a card, retry",
                      "Moitie GAUCHE = j'ai insere une carte, reessayer")
                  : T("A = I inserted a card, retry",
                      "A = j'ai insere une carte, reessayer"), L_ACC);
        add(touch ? T("RIGHT half = continue without one",
                      "Moitie DROITE = continuer sans carte")
                  : T("B or C = continue without one",
                      "B ou C = continuer sans carte"), L_ACC);

        d.fillScreen(TFT_BLACK);
        // ALERTS ARE SOLID RED BANNERS, full width, text centred (user
        // 08-09) — the idiom the original no-card screen used, restored for
        // every alert-class message this header draws: a fault must not wear
        // the launcher's cyan identity. Black ink on the red fill measures
        // 5.25:1, the black-on-alert convention the radar's notification
        // banner already follows.
        //
        // BOTH banners — the title, and the failed-retry line when a retry
        // found nothing — come out of ONE fillRect and ONE print call site,
        // in a loop whose count depends on `failed` at runtime: a constant
        // two-iteration loop is exactly what GCC 8.4 unrolls back into the
        // similar-call pair A2.22 forbids (proven on drawMoonView). Centred
        // with print() + a computed cursor, NOT a second drawString: that
        // site is pinned at ONE for the rows below.
        struct Ban { int16_t y, h; uint8_t size; const char* txt; };
        Ban ban[2];
        int nBan = 0;
        ban[nBan++] = { 22, 24, 2, T("NO CARD INSERTED",
                                     "PAS DE CARTE INSEREE") };
        if (failed)
            ban[nBan++] = { 222, 16, 1,
                            T("Still no card - check FAT32 and seating",
                              "Toujours pas de carte - verifier FAT32") };
        // The bound is made OPAQUE: GCC sees nBan is 1 or 2 and PEELS the
        // loop into two similar fillRect calls — the drawMoonView lesson,
        // in its peeling variant. The empty asm emits no instruction.
        asm volatile("" : "+r"(nBan));
        for (int i = 0; i < nBan; i++) {
            d.setTextSize(ban[i].size);
            d.fillRect(0, ban[i].y, 320, ban[i].h, TFT_RED);
            d.setTextColor(TFT_BLACK, TFT_RED);
            d.setCursor((320 - d.textWidth(ban[i].txt)) / 2,
                        ban[i].y + (ban[i].h - 8 * ban[i].size) / 2);
            d.print(ban[i].txt);
        }
        d.setTextSize(2);

        // THE DEFAULT FONT, and that is a size decision. Drawing this in
        // `efontJA_12` costs **312 KB of flash** in every bin that includes
        // this header — measured: `space` went from 1.33 to 1.64 MB the moment
        // this screen linked it, for one screen shown when a card is missing.
        // A shared header must be cheap to include, or bins will stop
        // including it.
        //
        // Which is why every string above is ASCII BY CONSTRUCTION — French
        // included, written without accents. Font0 has no accented glyphs, and
        // a missing one renders as a blank: add "réessayer" here and the line
        // silently loses a letter. Keep them unaccented.
        d.setTextSize(1);
        d.setTextDatum(textdatum_t::top_left);
        for (int i = 0; i < n; i++) {
            d.setTextColor(col[i], TFT_BLACK);
            d.drawString(row[i], 16, 66 + i * 16);      // ONE call site (A2.22)
        }
    }

    // ---- FLASH screen (SD-Updater callbacks) --------------------------
    // Same language as the lobby: black background, cyan title, gradient
    // rule, rounded cyan→indigo bar. Drawn DIRECTLY on M5.Display — by then
    // the guest app is already stopped, nobody else is drawing.
    static void flashChrome(const char* title) {
        auto& d = M5.Display;
        d.fillScreen(TFT_BLACK);
        d.setTextSize(2);
        d.setTextColor(L_ACC, TFT_BLACK);
        d.setCursor(16, 40);
        d.print(title);
        d.fillRect(16, 66, 144, 2, L_ACC);
        d.fillRect(160, 66, 144, 2, L_ACC2);
    }
    static void flashMessage(const String& label) {
        _flashMsg = label;
        flashChrome(T("BACK TO COMPANION", "RETOUR AU COMPANION"));
        auto& d = M5.Display;
        d.setTextSize(1);
        d.setTextColor(L_MUT, TFT_BLACK);
        d.setCursor(16, 92);
        d.print(label);
        _flashPct = -1;                   // forces the bar to be redrawn
    }
    static void flashProgress(int state, int size) {
        int pct = (size > 0) ? (state * 100 / size) : 0;
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        if (pct == _flashPct) return;     // 1 redraw per percent, no more
        _flashPct = pct;
        auto& d = M5.Display;
        d.fillRoundRect(16, 120, 288, 18, 4, L_CARD);
        int w = 288 * pct / 100;
        if (w > 0) d.fillRoundRect(16, 120, w, 18, 4, pct < 50 ? L_ACC : L_ACC2);
        d.setTextSize(1);
        d.setTextColor(L_MUT, TFT_BLACK);
        d.fillRect(140, 148, 48, 10, TFT_BLACK);
        d.setCursor(146, 148);
        d.printf("%d %%", pct);
        // A flash must NOT be interrupted: saying so stops people unplugging.
        d.setTextColor(L_DIM, TFT_BLACK);
        d.setCursor(16, 176);
        d.print(T("do not power off", "ne pas eteindre"));
    }
    // A failed flash is an ALERT, so it wears the red banner like the other
    // alerts of this header — it used to borrow the cyan chrome of the flash
    // screen, which dressed the one message that must alarm in the livery of
    // routine. The detail line stays white on black below: SD-Updater's
    // messages can be long, and long text belongs on the quiet ground.
    static void flashError(const String& message, unsigned long ms) {
        auto& d = M5.Display;
        d.fillScreen(TFT_BLACK);
        d.setTextSize(2);
        const char* title = T("FLASH FAILED", "ECHEC DU FLASH");
        d.fillRect(0, 40, 320, 24, TFT_RED);
        d.setTextColor(TFT_BLACK, TFT_RED);
        d.setCursor((320 - d.textWidth(title)) / 2, 44);
        d.print(title);
        d.setTextSize(1);
        d.setTextColor(TFT_WHITE, TFT_BLACK);
        d.setCursor(16, 100);
        d.print(message);
        delay(ms ? ms : 2000);
    }

    // /companion.bin present AND plausible (ESP32 magic 0xE9 + minimum size
    // — an interrupted upload must not be flashed: this binary is the ONLY
    // safety net back, review 07-26).
    // ⚠ KEEP IN SYNC with the upload validation in WebApi.h (sd/put
    // /companion.bin): same 0xE9/256 KB thresholds — this header is copied
    // STANDALONE into third-party bins, no factoring possible.
    bool companionImageOk() {
        File f = SD.open(COMPANION_PATH, FILE_READ);
        if (!f) return false;
        uint8_t magic = 0;
        size_t  sz = f.size();
        f.read(&magic, 1);
        f.close();
        return magic == 0xE9 && sz >= 262144;
    }

    // Reflash /companion.bin + reboot. Does not return on success.
    // If updateFromFS FAILS (temperamental SD), we do NOT call ESP.restart:
    // rebooting into the same guest after answering ok:true would lie to the
    // caller (max review 07-26) — the failure is shown on screen instead.
    bool stopToCompanion() {
        if (!companionImageOk()) return false;
        // BREADCRUMBS ON SERIAL, one per stage. While updateFromFS runs, the
        // synchronous web server is dead and ping still answers - from the
        // network that window is indistinguishable from a crash, and on
        // 2026-08-10 it lasted >10 min (space-net was never parked and fought
        // the reflash for the SD bus). The serial line is the only narrator
        // this window has; give it the stages and the sizes.
        Serial.printf("[guest] retour companion : arret des taches...\n");
        uint32_t t0 = millis();
        if (netGuard) netGuard->requestAndWait();  // parks the net task
        if (onBeforeStop) onBeforeStop();  // anything ELSE the app suspends
        File f = SD.open(COMPANION_PATH, FILE_READ);
        size_t sz = f ? f.size() : 0;
        if (f) f.close();
        Serial.printf("[guest] taches arretees en %lu ms ; reflash de %s "
                      "(%u octets) - HTTP muet jusqu'au redemarrage\n",
                      (unsigned long)(millis() - t0), COMPANION_PATH,
                      (unsigned)sz);
        t0 = millis();
        updateFromFS(SD, COMPANION_PATH); // SD-Updater: flash + restart
        // updateFromFS RETURNED: that means the flash failed.
        Serial.printf("[guest] ECHEC updateFromFS apres %lu ms\n",
                      (unsigned long)(millis() - t0));
        if (netGuard) netGuard->release();  // the guest lives on: resume
        flashError(T("Companion reflash failed - SD card?",
                     "Echec du reflash companion - carte SD ?"), 2500);
        return false;
    }

    // ==================================================================
    // YAML READING — the project's CANONICAL parser
    // ==================================================================
    // The format of the files on the card (the companion's config.yaml, the
    // guests' <bin>.yaml) is the same everywhere: `section:` then indented
    // `  key: value` lines, `#` comments, values possibly quoted. It was
    // nevertheless re-written FOUR times — here, in SdConfig.h, and in each
    // guest bin — with four different behaviours. Two outages already came
    // out of it:
    //
    //   07-25: a parser that was not quote-aware kept the quotes inside the
    //          SSID -> STA always failed -> silent fallback to AP;
    //   07-29: flight-radar cut at the "#" BEFORE looking at the quotes and
    //          never stripped them, so `api: "adsb.fi"` matched no known
    //          source and the bin switched to another API without a word.
    //
    // So there is only ONE implementation left, and it lives here because
    // this header is the ONLY file already embedded in all three binaries:
    // sharing it does not cost one extra byte of flash, duplicating it cost
    // four times that.
    //
    // Callback by FUNCTION POINTER + context, not std::function: a single
    // copy of the code whatever the number of callers, and zero allocation
    // (std::function allocates as soon as the capture exceeds its internal
    // reserve). A lambda WITHOUT capture converts to it directly.
    //
    // `section` is "" for a FLAT file (the guest-bin case).
    using YamlFn = void (*)(void* ctx, const char* section,
                            const char* key, const char* val);

    // Maximum length of a config line, STACK buffer included. A legitimate
    // line (an SSID, a password, an ADS-B source URL) fits in it with room to
    // spare; beyond that the file is not a config. Any change here must be
    // mirrored on the twin `SdConfig::load` (rule A2.23).
    static constexpr size_t MAX_LINE = 512;

    // Scalar decoding moved to `sce::yaml::scalar` (firmware/common/Yaml.h,
    // included at the top) on 08-01 — quoted content is literal, unquoted is
    // cut at the comment then trimmed. It is the same code the companion runs
    // and the same code test_yaml exercises; it is no longer written twice.

    // Walks `path` and calls `fn` for every key/value pair.
    // Returns false if the file is missing (the caller keeps its defaults).
    //
    // `sectioned` is DECLARED by the caller, never guessed:
    //   true  — sectioned file (config.yaml): a NON-indented line opens a
    //           section, only indented lines are keys;
    //   false — FLAT file (<bin>.yaml): every line is a key.
    // Deducing one from the other would need a heuristic like "empty value =
    // section", and an empty `host:` — the normal case of a ha-remote not
    // configured yet — would then become a section, swallowing the key.
    static bool yamlForEach(const char* path, bool sectioned,
                            YamlFn fn, void* ctx) {
        File f = SD.open(path, FILE_READ);
        if (!f) return false;
        String section;
        // Guard against a corrupted or binary file: the ceiling is enforced
        // DURING the read, into a fixed-size STACK buffer. Testing it AFTER a
        // readStringUntil() protected nothing — the String had already grown
        // to the end of the "line", so a damaged card (or a .bin renamed to
        // .yaml: not a single '\n' for megabytes) exhausted the heap BEFORE
        // reaching the test.
        char buf[MAX_LINE + 1];
        while (f.available()) {
            size_t n = f.readBytesUntil('\n', buf, MAX_LINE);
            buf[n] = '\0';
            // Buffer full without having seen the newline = line too long: we
            // THROW AWAY the rest up to the next '\n'. Without that the
            // leftover would be re-read as a line of its own and come back as
            // a FALSE key/value pair — exactly the 96-byte truncation of the
            // old flight-radar parser.
            if (n == MAX_LINE && f.available() && f.peek() != '\n') {
                while (f.available() && f.read() != '\n') { }
                continue;
            }
            // ONE decoder, shared with the companion's SdConfig (08-01). What
            // a line MEANS — indentation, quotes, end-of-line comment — used to
            // be written twice as "assumed twins", and they had already
            // diverged once. It now lives in firmware/common/Yaml.h, natively
            // tested. Only the bounded READ above stays per-caller: that is
            // I/O, and it is not where the twins disagreed.
            const sce::yaml::Line ln = sce::yaml::decodeLine(buf);
            if (!ln.ok) continue;
            if (sectioned && !ln.indented) { section = ln.key; continue; }
            fn(ctx, section.c_str(), ln.key, ln.val);
        }
        f.close();
        return true;
    }

private:
    struct SettingDef { const char* key; const char* label; Kind kind;
                        float lo, hi; const char* choices; };
    SettingDef _set[MAX_SETTINGS];
    int        _nSet = 0;

    static inline String _flashMsg;      // last label from SD-Updater
    static inline int    _flashPct = -1;  // avoids redrawing the bar
    // Acceptable origin: header absent (API client, script) or pointing at
    // OUR host. A browser cannot lie about this header.
    bool originOk() {
        if (!_server.hasHeader("Origin")) return true;
        String o = _server.header("Origin");
        String h = _server.hostHeader();
        if (!h.length()) return false;
        o.toLowerCase(); h.toLowerCase();      // "StackChan.local" = the same
        int p = o.indexOf("://");              // we compare host AND port
        if (p >= 0) o = o.substring(p + 3);
        // STRICT equality: a plain suffix test let through any host ENDING
        // with ours ("evilstackchan.local" against "stackchan.local") — the
        // filter was bypassable by construction (review finding 07-28).
        return o == h;
    }

    String _csrf;                 // anti-CSRF token, drawn at every boot
    String _authUser, _authPass;
    bool authOk() {
        if (_authUser.length() == 0) return true;         // no auth
        if (_server.authenticate(_authUser.c_str(), _authPass.c_str()))
            return true;
        _server.requestAuthentication();
        return false;
    }

    // HTML escaping: a value containing an apostrophe broke out of the
    // attribute and injected markup — and it can be PERSISTED (hence
    // replayed on every display). This header is copied into third-party
    // bins whose Text fields are arbitrarily long.
    // A bound as an HTML attribute: two decimals, trailing zeros and a
    // trailing point removed. `String(float)` alone gives "90.00" for a
    // latitude bound and "-14.00" for a time zone — noise in a field the user
    // reads while typing.
    static String numAttr(float v) {
        String s(v, 2);
        while (s.length() && s[s.length() - 1] == '0') s.remove(s.length() - 1);
        if (s.length() && s[s.length() - 1] == '.')    s.remove(s.length() - 1);
        return s;
    }

    static String esc(const String& v) {
        String o;
        o.reserve(v.length() + 8);
        for (size_t i = 0; i < v.length(); i++) {
            char c = v[i];
            if      (c == '&')  o += "&amp;";
            else if (c == '<')  o += "&lt;";
            else if (c == '>')  o += "&gt;";
            else if (c == '"')  o += "&quot;";
            else if (c == '\'') o += "&#39;";
            else                o += c;
        }
        return o;
    }

    // The network fields of the settings form → NVS. Separate from the app's
    // settings on purpose: different owner, different medium, different
    // lifetime (a reflash of the bin keeps them, a new SD card does not).
    // The Debug checkbox → NVS + applied LIVE: the moment that needs the
    // trace is now, not after a reboot the robot may not be reachable for.
    void applyDebugForm() {
        if (_server.arg("_dbg_p") != "1") return;   // form never carried it
        const bool want = _server.arg("_dbg") == "1";
        if (want != sce::trace::on) {
            sce::trace::on = want;
            Serial.printf("[sceguest] trace debug %s\n",
                          want ? "ACTIVE" : "coupee");
        }
        Preferences p;
        if (p.begin(NVS_NS, false)) { p.putBool("dbg", want); p.end(); }
    }

    void applyWifiForm() {
        if (!_server.hasArg("_wifi_ssid") && !_server.hasArg("_wifi_forget")) return;
        Preferences p;
        if (!p.begin(NVS_NS, false)) {          // read-write
            Serial.println("[sceguest] NVS indisponible : reseau NON enregistre");
            return;
        }
        if (_server.arg("_wifi_forget") == "1") {
            // Clearing BOTH: a passphrase left behind under no SSID is a
            // secret kept for nothing.
            p.remove("ssid");
            p.remove("pass");
        } else {
            String s = _server.arg("_wifi_ssid");
            s.trim();
            if (s.length() == 0) {
                // An SSID emptied by hand means the same thing as Forget, and
                // storing an empty name would shadow the card with nothing.
                p.remove("ssid");
                p.remove("pass");
            } else {
                const String old = p.getString("ssid", "");
                p.putString("ssid", s);
                const String pw = _server.arg("_wifi_pass");
                // Empty = unchanged, EXCEPT on a different network: keeping
                // the old passphrase for a new SSID guarantees a failure that
                // looks like a typo in the name.
                if (pw.length() > 0)   p.putString("pass", pw);
                else if (s != old)     p.putString("pass", "");
            }
        }
        p.end();
    }

    // Form themed like the companion console (same CSS variables — kept in
    // sync by hand, cf. applyLobbyTheme).
    void sendConfigPage() {
        // The WiFi block below is ALWAYS there, so the page always is: a bin
        // that exposes no setting of its own is exactly the bin most likely to
        // be stranded on an access point with no way to be told a network.
        const bool appSettings = (_nSet > 0 && settingGet);
        String h = F("<!doctype html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>");
        h += shownName();
        h += F(" - ");
        h += sce::T("settings", "r&eacute;glages");
        h += F("</title><style>"
            ":root{--txt:#e8eefb;--mut:#93a1bd;--acc:#22d3ee;--acc2:#818cf8;"
            "--glass:rgba(255,255,255,.055);--bord:rgba(255,255,255,.12)}"
            "*{box-sizing:border-box}body{font-family:system-ui;margin:0;"
            "background:#0a0d16;color:var(--txt);min-height:100vh;display:flex;"
            "align-items:center;justify-content:center;padding:24px}"
            "main{background:var(--glass);backdrop-filter:blur(22px);"
            "border:1px solid var(--bord);border-radius:6px;padding:28px 32px;"
            "width:min(520px,100%);box-shadow:0 18px 50px rgba(0,0,0,.45)}"
            "h2{margin:0 0 18px;font-size:1.1rem}"
            "h2 b{background:linear-gradient(120deg,var(--acc),var(--acc2));"
            "-webkit-background-clip:text;background-clip:text;color:transparent}"
            "label{display:flex;align-items:center;justify-content:space-between;"
            "gap:16px;padding:9px 0;border-bottom:1px solid var(--bord);"
            "font-size:.9rem;color:var(--mut)}"
            "input,select{background:rgba(8,12,22,.55);color:var(--txt);"
            "border:1px solid var(--bord);border-radius:3px;padding:6px 8px;"
            "font:inherit;min-width:150px}"
            "input[type=checkbox]{min-width:auto;width:18px;height:18px}"
            "button{margin-top:20px;width:100%;background:linear-gradient("
            "120deg,var(--acc),var(--acc2));color:#06121f;border:0;"
            "border-radius:3px;padding:12px;font:600 .95rem system-ui;"
            "cursor:pointer}.ok{color:#34d399;font-size:.85rem;margin:0 0 12px}"
            // Build identity, at the very bottom: read rarely, but the one
            // thing worth having on screen when a guest is about to reflash
            // the companion.
            ".fw{margin:26px 0 0;padding-top:12px;border-top:1px solid "
            "var(--bord);color:var(--mut);font-size:.72rem;text-align:center;"
            "font-family:ui-monospace,Menlo,monospace;word-break:break-all}"
            "</style></head><body><main><h2>");
        h += sce::T("Settings", "R&eacute;glages");
        h += F(" <b>"); h += shownName(); h += F("</b></h2>");
        // HONEST warning: without an API password on the companion side, this
        // page is open to the whole network. Do not block it for all that —
        // it is the only convenient path to ENTER the secret.
        bool hasSecret = false;
        for (int i = 0; i < _nSet; i++)
            if (_set[i].kind == Secret) hasSecret = true;
        if (hasSecret && _authUser.length() == 0) {
            h += F("<p class='ok' style='color:#fbbf24'>");
            h += sce::T("This page is NOT protected: set "
                        "<code>api.username</code> / <code>api.password</code> "
                        "in the companion's <code>config.yaml</code>.",
                        "Page NON prot&eacute;g&eacute;e : d&eacute;finissez "
                        "<code>api.username</code> / <code>api.password</code> "
                        "dans le <code>config.yaml</code> du companion.");
            h += F("</p>");
        }
        if (_server.hasArg("ok")) {
            h += F("<p class='ok'>");
            h += sce::T("Saved.", "Enregistr&eacute;.");
            h += F("</p>");
        }
        if (_server.hasArg("ko")) {
            h += F("<p class='ok' style='color:#f87171'>");
            h += sce::T("SAVE FAILED (SD card?).",
                        "&Eacute;CHEC de l'enregistrement (carte SD ?).");
            h += F("</p>");
        }
        h += "<form method='POST' action='/config'>"
             "<input type='hidden' name='_sce' value='" + _csrf + "'>";

        // ---- NETWORK, always shown ------------------------------------
        // Only the SSID is echoed back. The passphrase follows the same rule
        // as every other Secret here: never displayed, empty means unchanged,
        // and it is cleared by forgetting the network rather than by typing a
        // sentinel — "forget" is the operation a user actually has in mind.
        {
            Preferences p;
            String cur;
            if (p.begin(NVS_NS, true)) { cur = p.getString("ssid", ""); p.end(); }
            h += F("<h3 style='margin:18px 0 6px;font-size:.8rem;"
                   "letter-spacing:.08em;text-transform:uppercase;"
                   "color:var(--mut)'>");
            h += sce::T("Network", "R&eacute;seau");
            h += F("</h3>");
            if (_apMode) {
                h += F("<p class='ok' style='color:#fbbf24'>");
                h += sce::T("No network joined: this page is being served by the "
                            "bin's own access point. Enter a network below, then "
                            "restart.",
                            "Aucun r&eacute;seau rejoint : cette page est servie "
                            "par le point d'acc&egrave;s du bin. Saisissez un "
                            "r&eacute;seau ci-dessous, puis red&eacute;marrez.");
                h += F("</p>");
            }
            h += "<label>" + String(sce::T("Wi-Fi network", "R&eacute;seau Wi-Fi")) +
                 "<input name='_wifi_ssid' value='" + esc(cur) +
                 "' placeholder='" +
                 String(sce::T("from the SD card", "depuis la carte SD")) + "'></label>";
            h += "<label>" + String(sce::T("Passphrase", "Mot de passe")) +
                 "<input type='password' autocomplete='new-password' "
                 "name='_wifi_pass' placeholder='" +
                 String(cur.length() ? sce::T("empty = unchanged", "vide = inchange")
                                     : sce::T("not set", "non defini")) +
                 "'></label>";
            if (cur.length()) {
                h += "<label>" +
                     String(sce::T("Forget it (back to the SD card)",
                                   "L'oublier (retour &agrave; la carte SD)")) +
                     "<input type='checkbox' value='1' name='_wifi_forget'></label>";
            }
            h += F("<p class='ok' style='color:#93a1bd'>");
            h += sce::T("A network change takes effect on the next restart.",
                        "Un changement de r&eacute;seau prend effet au "
                        "prochain red&eacute;marrage.");
            h += F("</p>");
            // ---- DEBUG TRACE — framework-owned like the network above, so
            // every guest bin gets the switch without writing a line. The
            // hidden marker is the classic unchecked-checkbox dance: a box
            // that is off sends NOTHING, and without the marker "absent"
            // would be indistinguishable from "form never carried it".
            h += F("<h3 style='margin:22px 0 6px;font-size:.8rem;"
                   "letter-spacing:.08em;text-transform:uppercase;"
                   "color:var(--mut)'>Debug</h3>"
                   "<input type='hidden' name='_dbg_p' value='1'>");
            h += "<label>" +
                 String(sce::T("Serial trace (every step)",
                               "Trace s&eacute;rie (chaque &eacute;tape)")) +
                 "<input type='checkbox' value='1' name='_dbg'" +
                 String(sce::trace::on ? " checked" : "") + "></label>";
            h += F("<p class='ok' style='color:#93a1bd'>");
            h += sce::T("Applied at once, survives reboots (NVS). Serial "
                        "115200: network, config, every HTTP attempt.",
                        "Appliqu&eacute; imm&eacute;diatement, survit aux "
                        "red&eacute;marrages (NVS). S&eacute;rie 115200 : "
                        "r&eacute;seau, config, chaque tentative HTTP.");
            h += F("</p>");
            if (appSettings) {
                h += F("<h3 style='margin:22px 0 6px;font-size:.8rem;"
                       "letter-spacing:.08em;text-transform:uppercase;"
                       "color:var(--mut)'>");
                h += shownName();
                h += F("</h3>");
            }
        }

        for (int i = 0; appSettings && i < _nSet; i++) {
            const SettingDef& d = _set[i];
            String v = settingGet(d.key);
            h += "<label>" + String(d.label);
            if (d.kind == Bool) {
                // EXPLICIT value='1': without it the browser sends "on",
                // which the apps convert to 0 with toInt() (a toggle turned
                // on applied as zero, seen on HW 07-27d).
                h += "<input type='checkbox' value='1' name='" +
                     String(d.key) + "'";
                if (v.toInt() || v == "on" || v == "true") h += " checked";
                h += ">";
            } else if (d.kind == Choice && d.choices) {
                h += "<select name='" + String(d.key) + "'>";
                String all = d.choices;
                int p = 0;
                while (p <= (int)all.length()) {
                    int e = all.indexOf('|', p);
                    if (e < 0) e = all.length();
                    String o = all.substring(p, e);
                    h += "<option" + String(o == v ? " selected" : "") + ">" +
                         esc(o) + "</option>";
                    p = e + 1;
                }
                h += "</select>";
            } else if (d.kind == Secret) {
                // Neither `value` nor autocompletion: the browser must not
                // put it back either.
                h += "<input type='password' autocomplete='new-password' "
                     "name='" + String(d.key) + "' placeholder='" +
                     String(v.length()
                        ? sce::T("set - empty = unchanged, - = clear",
                                 "defini - vide = inchange, - = effacer")
                        : sce::T("not set", "non defini")) + "'>";
            } else if (d.kind == Num) {
                // `step='any'`, and it is not cosmetic. Without it a number
                // input steps by ONE and the browser REFUSES anything with a
                // decimal point: a latitude of 48.8566, a longitude, a UTC
                // offset of 5.75 — the fields most likely to need decimals
                // were the ones the form rejected, silently, by marking them
                // invalid instead of saying why.
                //
                // The bin decides what it does with the number, as it already
                // does: it parses with atoi or atof to suit itself and clamps
                // its own range. The form's job is to carry what was typed,
                // not to invent an integrality nobody declared.
                //
                // The bounds keep their decimals too — printed with two and
                // trimmed — because an integer cast on a fractional bound
                // would quietly move the limit it exists to state.
                h += "<input type='number' step='any' name='" + String(d.key) +
                     "' value='" + esc(v) + "'";
                if (d.hi > d.lo) h += " min='" + numAttr(d.lo) +
                                      "' max='" + numAttr(d.hi) + "'";
                h += ">";
            } else {
                h += "<input name='" + String(d.key) + "' value='" +
                     esc(v) + "'>";
            }
            h += "</label>";
        }
        h += "<button>" + String(sce::T("Save", "Enregistrer")) + "</button></form>";
        // WHICH BUILD IS THIS GUEST. `/api/bins/stop` on this very bin
        // reflashes /companion.bin from the SD card, so this page is one
        // button away from replacing the firmware someone just installed
        // over USB. `sha` is `sha256sum .pio/build/<env>/firmware.elf`
        // (first 8 hex), `slot` the OTA partition actually booted.
        h += "<p class='fw'>" + String(shownName()) + " &middot; " +
             String(sce::fw::sha8()) + " &middot; " +
             String(sce::fw::slot()) + " &middot; " +
             String(sce::T("start", "d&eacute;marrage")) + " " +
             String(sce::fw::resetReasonName()) + "</p>";
        h += "</main></body></html>";
        _server.send(200, "text/html", h);
    }

    // NVS namespace for the network typed on the device. Short and shared by
    // every guest bin ON PURPOSE: it is a property of the ROBOT, not of the
    // app that happens to be running — swapping flight-radar for space must
    // not lose the network somebody just entered. NVS survives a reflash of
    // the application partition, which is exactly the lifetime wanted.
    static constexpr const char* NVS_NS = "sce-net";

    WebServer _server{80};
    DNSServer _dns;
    bool _dnsUp        = false;
    bool _apMode       = false;
    String _apSsid;
    bool _swipeExit    = true;
    bool _touchEaten   = false;   // a modal consumed the touch frame
    bool _touchActive  = false;
    int  _sx = 0, _sy = 0;

    // ---- "Liquid Glass" palette (copied from Launcher.h — cf. the note on
    // applyLobbyTheme: this header must stay copyable alone, so the values are
    // restated rather than included). NO LONGER "synced by hand": the three
    // copies — here, Launcher.h and ha-remote — are held to each other by
    // check-mirrors.py, and check-contrast.py measures the readability the
    // comments used to merely claim. ----
    static constexpr uint16_t L_ACC  = 0x269D;   // cyan   #22d3ee
    static constexpr uint16_t L_ACC2 = 0x847F;   // indigo #818cf8
    static constexpr uint16_t L_CARD = 0x10C5;   // card   #131a2b
    static constexpr uint16_t L_BORD = 0x29AA;   // border #2a3550
    static constexpr uint16_t L_MUT  = 0xA534;   // secondary text
    static constexpr uint16_t L_DIM  = 0x738E;   // dimmed text
    static constexpr uint16_t L_KO   = 0xFB8E;   // error  #f87171
    // English default: this initialiser runs before any language is known
    // (applyLobbyTheme overwrites it once loadLang has run).
    inline static const char* _lobbyApp = "Guest bin";

    // Lobby button: geometry and style of Launcher::button (h 28, r 6).
    static void lobbyBtn(int x, int w, const char* label, uint16_t col,
                         bool fill) {
        auto& d = M5.Display;
        if (fill) { d.fillRoundRect(x, 206, w, 28, 6, col);
                    d.setTextColor(TFT_BLACK, col); }
        else      { d.fillRoundRect(x, 206, w, 28, 6, L_CARD);
                    d.drawRoundRect(x, 206, w, 28, 6, col);
                    d.setTextColor(col, L_CARD); }
        d.setTextSize(2);
        int len = label ? (int)strlen(label) : 0;
        d.setCursor(x + (w - len * 12) / 2, 213);
        if (label) d.print(label);
    }

    // Lobby page: same grammar as Launcher::header + a central card.
    static void lobbySplash(const char* msg) {
        auto& d = M5.Display;
        d.fillScreen(TFT_BLACK);
        d.setTextSize(2);
        d.setTextColor(L_ACC, TFT_BLACK);
        d.setCursor(8, 8); d.print("STACKCHAN");
        d.fillRect(0, 30, 160, 2, L_ACC);      // cyan → indigo gradient rule
        d.fillRect(160, 30, 160, 2, L_ACC2);
        d.setTextSize(1);
        d.setTextColor(L_DIM, TFT_BLACK);
        d.setCursor(8, 34);
        d.print(T("guest bin - choose", "bin invite - choisissez"));
        (void)msg;                            // (`msg` = the library's raw
                                              //  own label, discarded)
        d.fillRoundRect(12, 52, 296, 132, 8, L_CARD);
        d.drawRoundRect(12, 52, 296, 132, 8, L_BORD);
        d.setTextColor(L_MUT, L_CARD);
        d.setCursor(28, 66); d.print(T("Starting:", "Demarrage de :"));
        d.setTextSize(2);
        d.setTextColor(TFT_WHITE, L_CARD);
        d.setCursor(28, 84);
        char nm[22]; snprintf(nm, sizeof(nm), "%.21s", _lobbyApp);
        d.print(nm);
        d.setTextSize(1);
        d.setTextColor(L_MUT, L_CARD);
        d.setCursor(28, 118);
        d.print(T("Companion: back to the firmware",
                  "Companion : revenir au firmware"));
        d.setCursor(28, 132);
        d.print(T("Continue: start this bin right now",
                  "Continuer : lancer ce bin tout de suite"));
        d.setTextColor(L_DIM, L_CARD);
        d.setCursor(28, 152);
        d.print(T("No action: the bin starts on its own.",
                  "Sans action, le bin demarre seul."));
    }

    // COMPLETE action wait (buttons + touch zones + countdown).
    // Replaces the library's `actionTriggered`: on a touch screen that one
    // draws non-customizable buttons right in the middle of the splash.
    // Returns: 1 = load /companion.bin, -1 = carry on with the guest.
    // No "Save" button: in a guest bin `binFileName` is null, the action is
    // INERT on the library side (and saving the guest over /companion.bin
    // would destroy the only safety net back).
    static int lobbyWait(char* labelLoad, char* labelSkip, char*,
                         unsigned long waitdelay) {
        lobbyBtn(4,   152, labelLoad ? labelLoad : "Companion", L_ACC, true);
        lobbyBtn(164, 152, labelSkip ? labelSkip : T("Continue", "Continuer"),
                 L_MUT, false);
        auto& d = M5.Display;
        uint32_t t0 = millis(), last = 0;
        while (millis() - t0 < waitdelay) {
            M5.update();
            auto t = M5.Touch.getDetail(0);
            if (t.wasClicked() && t.y >= 206 && t.y <= 233) {   // zones =
                if (t.x >= 4   && t.x <= 156) return 1;         // the DRAWN
                if (t.x >= 164 && t.x <= 316) return -1;        // buttons
            }
            uint32_t el = millis() - t0;                 // countdown: the bar
            uint32_t w  = 296 - 296 * el / waitdelay;    // drains across the
            if (w != last) {                            // width of the card
                last = w;
                d.fillRect(12, 190, 296, 3, L_CARD);
                if (w) d.fillRect(12, 190, (int)w, 3, L_ACC2);
            }
            delay(15);
        }
        return -1;                                      // timeout: we launch
    }

    // ------------------------------------------------------------------
    // Exit gesture: minimal detection (independent of the app's taps —
    // passive read of M5.Touch, a swipe is not a wasClicked).
    // ------------------------------------------------------------------
    void pollSwipeExit() {
        if (!M5.Touch.isEnabled()) return;
        if (M5.Touch.getCount() == 0) { _touchActive = false; return; }
        auto t = M5.Touch.getDetail(0);
        if (t.wasPressed()) { _sx = t.x; _sy = t.y; _touchActive = true; }
        if (_touchActive && t.wasReleased()) {
            _touchActive = false;
            int dx = t.x - _sx, dy = t.y - _sy;
            int adx = dx < 0 ? -dx : dx;
            if (_sx < swipeExitMaxX &&
                dy >= sce::gesture::EXIT_PX &&
                adx < dy) {                    // long swipe down, clearly vertical
                if (confirmExit() && !stopToCompanion()) {
                    // /companion.bin missing: the user has to know
                    auto& d = M5.Display;
                    d.fillRoundRect(30, 100, 260, 40, 3, L_CARD);
                    d.setTextSize(1);
                    d.setTextColor(L_KO, L_CARD);
                    d.setCursor(46, 116);
                    d.print(T("/companion.bin missing or invalid",
                              "/companion.bin absent ou invalide"));
                    delay(1800);
                }
            }
        }
    }

    // BLOCKING confirmation (~8 s max) drawn over the app — the app redraws
    // its screen afterwards (a guest that never redraws will keep the card on
    // screen: accepted, rare case). true = leave.
    bool confirmExit() {
        auto& d = M5.Display;
        d.fillRoundRect(30, 70, 260, 100, 3, L_CARD);   // dark card
        d.drawRoundRect(30, 70, 260, 100, 3, L_ACC);   // cyan border
        d.setTextSize(2);
        d.setTextColor(TFT_WHITE, L_CARD);
        // Two lines, so two pairs: the break falls in a different place in
        // each language and belongs to the translation.
        d.setCursor(52, 84);  d.print(T("Back to the", "Retour au"));
        d.setCursor(52, 104); d.print(T("companion?", "companion ?"));
        // Buttons in the SAME language as the launcher: L_* palette and
        // radius 3 (0x2104/0x8410 were two invented greys, not in the
        // palette).
        d.fillRoundRect(46, 130, 100, 32, 3, L_CARD);
        d.drawRoundRect(46, 130, 100, 32, 3, L_MUT);
        d.setTextColor(L_MUT, L_CARD);
        d.setCursor(78, 139); d.print(T("No", "Non"));
        d.fillRoundRect(174, 130, 100, 32, 3, L_ACC);
        d.setTextColor(TFT_BLACK, L_ACC);
        d.setCursor(206, 139); d.print(T("Yes", "Oui"));
        uint32_t t0 = millis();
        while (millis() - t0 < 8000) {
            M5.update();
            _server.handleClient();            // HTTP served DURING the
                                               // confirmation (a remote stop
                                               // must not time out, 07-26)
            auto t = M5.Touch.getDetail(0);
            // Zones = the DRAWN buttons only (No x46-145, Yes x174-273):
            // `t.x >= 160` accepted the WHOLE width, including 46 px outside
            // the card — an imprecise tap reflashed the companion and
            // destroyed the running app (max review 07-27, symmetric with the
            // hardening of Launcher::confirm).
            if (t.wasClicked() && t.y >= 130 && t.y <= 162) {
                if (t.x >= 174 && t.x <= 273) { _touchEaten = true; return true; }
                if (t.x >= 46  && t.x <= 145) { _touchEaten = true; return false; }
            }
            delay(20);
        }
        _touchEaten = true;                    // the tap frame is
        return false;                          // consumed here (timeout)
    }

    // ------------------------------------------------------------------
    // UI LANGUAGE — read from the SAME config.yaml as the credentials
    // (top-level key `lang: fr`, absent = English). Same reasoning as the
    // WiFi: the robot has ONE setting, the guest reuses it instead of asking
    // for it a second time.
    //
    // FLAT read (`sectioned=false`) although the file IS sectioned: in
    // sectioned mode a non-indented line OPENS a section and its value is
    // dropped, so `lang:` would be swallowed as a section name and never
    // reach the callback. In flat mode every line comes through and we keep
    // the only key we know about. Still ONE parser (rule A2.23) — the point
    // of the rule is not to write another one, and we do not.
    // ------------------------------------------------------------------
    static void loadLang() {
        char got[16] = "";
        yamlForEach(CONFIG_PATH, false, [](void* ctx, const char*,
                                           const char* key, const char* val) {
            if (!strcmp(key, "lang")) strlcpy((char*)ctx, val, 16);
        }, got);
        if (got[0]) setLang(got);
    }

    // ------------------------------------------------------------------
    // Reads the wifi: section AND the api: section of the companion's
    // config.yaml — the robot has only ONE set of credentials, the ones of its
    // console (07-27). So the guest reuses what is ALREADY configured: a
    // single place to keep up to date.
    //
    // The decoding itself is delegated to `yamlForEach`: the quotes, the "#"
    // and the sections are handled there ONCE for the whole project. The
    // home-grown parser that used to live here had already cost the 07-25 bug
    // (quotes kept inside the SSID, STA failing, silent fallback to AP).
    //
    // Each pair (ssid/pass, apSsid/apPass) is applied only if ITS ssid is
    // present and non-empty, independently of the other.
    // ------------------------------------------------------------------
    static void readSdCreds(String& ssid, String& pass,
                            String& apSsid, String& apPass,
                            String& apiUser, String& apiPass) {
        struct Found { String ssid, pass, apSsid, apPass, apiUser, apiPass; } got;
        yamlForEach(CONFIG_PATH, true, [](void* ctx, const char* section,
                                          const char* key, const char* val) {
            Found& g = *(Found*)ctx;
            if (!strcmp(section, "api")) {          // Basic Auth console+API
                if      (!strcmp(key, "username")) g.apiUser = val;
                else if (!strcmp(key, "password")) g.apiPass = val;
                return;
            }
            if (strcmp(section, "wifi")) return;
            if      (!strcmp(key, "client_ssid"))     g.ssid   = val;
            else if (!strcmp(key, "client_password")) g.pass   = val;
            else if (!strcmp(key, "ap_ssid"))         g.apSsid = val;
            else if (!strcmp(key, "ap_password"))     g.apPass = val;
        }, &got);
        if (got.apSsid.length() > 0) { apSsid = got.apSsid; apPass = got.apPass; }
        if (got.ssid.length()   > 0) { ssid   = got.ssid;   pass   = got.pass;   }
        // An EMPTY password = open API on the companion side (default): we
        // keep the same convention, the guest does not harden what the
        // companion leaves open, and does not open up when it protects.
        if (got.apiPass.length() > 0) {
            apiUser = got.apiUser.length() ? got.apiUser : String("admin");
            apiPass = got.apiPass;
        }
    }
};

} // namespace sce
