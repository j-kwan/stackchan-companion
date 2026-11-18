// =============================================================================
// space — StackChan-Companion GUEST .bin: the desk space instrument
// =============================================================================
// Five views on one ring: where the ISS is NOW, when it comes back over YOUR
// sky, the Moon, the naked-eye planets, and what leaves the ground next.
// Specification and rationale: docs/guests/SPACE.md. User guide:
// docs/guests/SPACE.md.
//
// WHY THIS BIN EXISTS BESIDES flight-radar: it is the SECOND consumer of the
// SceGuest contract, which is how that contract stops being a description of
// one program and becomes an interface. Everything structural here is
// deliberately the radar's shape — ring of views, UiEvent vocabulary, yaml on
// the card, /config web page, dock mode — and everything astronomical is in
// two PURE headers with native tests beside them.
//
// ── THE RING ────────────────────────────────────────────────────────────
//        ISS       ↑ up: PASSES        ↓ down (long): back to the companion
//        PASSES    ↑ up: MOON          ↓ down (long): back to the companion
//        MOON      ↑ up: SKY           ↓ down (long): back to the companion
//        SKY       ↑ up: LAUNCHES      ↓ down (long): back to the companion
//        LAUNCHES  ↑ up: round to ISS  ↓ down (long): back to the companion
//
//   swipe LEFT/RIGHT   previous / next item (a pass, a launch)
//   tap a pass row     its polar sky chart (modal; tap anywhere to leave)
//   long press 700 ms  force a refresh of the current view's source
//   swipe DOWN         SceGuest's exit — never ours, on any view
//
// ── WHERE THE DATA COMES FROM, AND WHAT IT COSTS ────────────────────────
// Orbit: ONE TLE from Celestrak, fetched AT MOST once a day and cached on the
// card (/stackchan-companion/space-tle.txt). Their etiquette is explicit and they ban abusers;
// the cache is read first at boot, and the network is only touched when it is
// stale. Everything else about the ISS — position, ground track, passes — is
// computed on board from that one element set (sgp4.h), so the bin works for
// days with no network at all.
// Launches: RocketLaunch.Live's free endpoint FIRST (no key, no published
// limit, and it names the vehicle, the pad and the pad's weather in fields of
// their own), Launch Library 2 as the fallback — free but THROTTLED to 15
// requests an hour. The poll floor is therefore still 15 minutes and the
// default 30; the countdown ticks locally between polls, and a cache older
// than an hour says so ("as of hh:mm") rather than pretending a T-0 that has
// since slipped. RocketLaunch.Live's terms ask for a visible credit: the view
// carries it, and it names whichever source actually served.
// Time: NTP. Every view is gated on a synchronised clock — an astronomy
// display with a wrong clock is not degraded, it is wrong.
// =============================================================================

#include <Arduino.h>
#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// -----------------------------------------------------------------------------
// BOARD PROFILE — named after what the board HAS, never after a board name.
// Defaults are the CoreS3/K151 the bin ships on; a future Fire profile flips
// them from platformio.ini exactly as flight-radar-fire does.
// -----------------------------------------------------------------------------
#ifndef SCE_INPUT_BUTTONS
#define SCE_INPUT_BUTTONS 0        // three physical buttons instead of touch
#endif
// NO `SCE_HAS_LTR553` here, unlike the radar, and that is a decision rather
// than an omission: the ambient light sensor is PROBED at boot (`gLtrOk`) and
// the result is reported on /config. That already covers a board which has no
// sensor — and also a board whose sensor is dead, which a build flag never
// would. Adding the flag would be a second mechanism answering the same
// question, and two answers to one question is how they come to disagree.
#ifndef SCE_COMPANION
#define SCE_COMPANION 1            // a companion firmware to return to
#endif
#ifndef SCE_HAS_SERVO
#define SCE_HAS_SERVO 1            // SCS0009 neck through the PY32 expander
#endif
// The wiring lives in firmware/common/SdPins.h — FOUR guards, one per
// pin. This file used to guard all four behind the first, which is the
// documented trap: a profile overriding only CS got it put back to 4.
#include "../common/SdPins.h"
#include "../common/SdRoot.h"          // one-time /stackchan-eyes -> /stackchan-companion move
// ONE GUARD PER MACRO, for the reason SdPins.h states three lines above: a
// profile that supplies only SCE_WIFI_PASS would fall through this test and
// have its password silently redefined to "" — the shape of the 07-25 bug,
// behind a warning that scrolls past.
#ifndef SCE_WIFI_SSID
#define SCE_WIFI_SSID ""
#endif
#ifndef SCE_WIFI_PASS
#define SCE_WIFI_PASS ""
#endif

#include "../common/CfgBool.h"     // ONE definition of "true"
#include "../common/PsJson.h"
#include "../common/SunClock.h"        // sce::clockSynced — the shared test
#include "../common/CellText.h"        // deferred text, ONE draw call site (A2.22)
#include "../common/Ltr553.h"           // ambient light: ONE calibration, two readers
#include "../common/Gesture.h"          // ONE swipe classifier and press budget
#include "../common/SdWatch.h"          // the card, watched and not only mounted
#include "../common/headtrack.h"        // PURE: sky point -> servo pose (test_astro)
#if SCE_HAS_SERVO
#include "../common/HeadServo.h"        // the neck, shared with flight-radar
#endif

// The card watcher. Without it the boot mount stood for the whole run: a
// card inserted afterwards was never seen (settings silently not persisted
// for the session) and one pulled out was never noticed.
static sce::SdWatch gSdWatch;
#include "guest/SceGuest.h"
#include "astro.h"
#include "input.h"
#include "worldmap.h"        // GENERATED by tools/generators/gen-worldmap.py — see its header

using namespace spc;
using spa::UiEvent;
using spa::View;

// =============================================================================
// Theme
// =============================================================================
// Dark by default — it IS space — plus an amber Night palette switched by the
// same SunClock rule the radar uses. Both palettes go through ONE pointer so
// no drawing code ever names a colour directly.
struct Theme {
    uint16_t bg, panelBg, txtMain, txt1, txt2, hint, accent, accent2, alert, sep;
    // The world map: two background states (lit / dark side of the Earth) and
    // two ink states for the coastline drawn over them. In the theme rather
    // than in the drawing code because the Night palette must not put a cyan
    // shoreline on an amber screen — a map is part of the identity, not a
    // decoration. Named after what they ARE now: the land is no longer filled,
    // so `landDay` would have been a lie about the fill it no longer does.
    uint16_t bgDay, bgNight, coastDay, coastNight;
    // The sky strip's three tints, and they answer ONE question: what would it
    // take to see this right now. Bright = your eyes; mid = something else
    // (an instrument, or a darker sky); dim = it is not up at all. `stripOff`
    // is deliberately NOT the background: an object below the horizon still
    // has to be findable in the row, or the strip loses its place-keeping.
    uint16_t stripEye, stripAid, stripOff;
};
static const Theme THEMES[2] = {
    // 0 — Deep (default): a dim blue day side, near-black night, a muted
    // slate shoreline over both.
    { 0x0000, 0x10A2, 0xFFFF, 0xC618, 0x8410, 0x7C0F, 0x05FF, 0xFD20, 0xF800, 0x3186,
      0x1127, 0x0841, 0x5BF1, 0x2B2E,
      0xFFFF, 0x8410, 0x52EA },
    // 1 — Night (amber, for a bedside instrument: no blue after dark)
    { 0x0000, 0x1000, 0xFD20, 0xFC00, 0xD9E0, 0xBAE0, 0xFD20, 0xFEA0, 0xF800, 0x3000,
      0x1880, 0x0840, 0x8B44, 0x72C3,
      0xFDA0, 0x9240, 0x72A0 },
};
static const Theme* TH = &THEMES[0];

// =============================================================================
// Configuration (/stackchan-companion/space.yaml on the card — flat, canonical parser)
// =============================================================================
struct Config {
    float    lat = 48.8566f, lon = 2.3522f;   // observer
    float    altKm = 0.035f;
    // HOURS, decimal — the same key and unit flight-radar already uses
    // (`tz_offset_h`). Three guest bins with two units for the same idea was a
    // divergence with no upside (user 08-04). Decimal hours express every real
    // zone exactly: India is 5.5, Nepal 5.75, Chatham 12.75 — the half-hour
    // zones are why the naive "whole hours" field would have been wrong, not
    // a reason to count in minutes.
    float    tzOffsetH = 0.0f;
    uint32_t norad = 25544;                   // ISS (ZARYA)
    uint16_t launchPollMin = 30;                 // FLOOR 15 — the API's throttle
    uint8_t  theme = 0;
    uint8_t  autoNight = 1;
    // Auto brightness from the LTR-553, like flight-radar. It was missing here
    // and the omission showed: the same robot dimmed itself under one guest bin
    // and stayed at full brightness under another, in the same dark room.
    uint8_t  autoBright = 1;
    uint8_t  metric = 1;
    uint8_t  bright = 110;
    uint16_t dockS = 0;                       // 0 = no auto-cycle
    float    minPassEl = 10.0f;               // a pass below this is roof line
    // The head follows the satellite while it is above the horizon. OFF by
    // default, like every intrusive automation of the guest bins: a head that
    // starts moving on its own at 3 a.m. is a surprise, not a feature.
    uint8_t  servo = 0;
    // The compass bearing the robot's FACE points at when the head is
    // centred. Without it a bearing means nothing to the servo: 0 assumes the
    // robot looks north.
    float    servoAz = 0.0f;
};
// The card's convention: every guest keeps its yaml in /stackchan-companion/,
// beside the companion's config.yaml and the radar's flightradar.yaml. The
// root would have "worked" too — and left this bin the only one whose settings
// nobody could find. Same for the TLE cache: one directory, one place to look.
static constexpr const char* CFG_PATH = "/stackchan-companion/space.yaml";
static constexpr const char* TLE_PATH = "/stackchan-companion/space-tle.txt";
static constexpr const char* LNC_PATH = "/stackchan-companion/space-launch.json";

static Config cfg;

// UNITS. `metric` was a settings toggle the user could flip, watch persist
// across a reboot, and see change nothing — every unit string was hard-coded
// (review 08-04). Same shape as the radar's dD/uD: the converter and the
// suffix sit together, so a call site cannot convert without relabelling.
// Astronomical units and degrees are NOT converted: there is no imperial
// astronomical unit, and an angle is an angle.
static float dKm(float km) { return cfg.metric ? km : km * 0.621371f; }
static const char* uKm()   { return cfg.metric ? "km" : "mi"; }
static float dKms(float k) { return cfg.metric ? k : k * 0.621371f; }
static const char* uKms()  { return cfg.metric ? "km/s" : "mi/s"; }
// Pad weather arrives imperial whatever the reader's taste — the source is
// American — so it converts INWARDS here rather than at the parser: the stored
// value stays the one the API published, and the screen is the single place
// the choice applies.
static int dTempF(int f)    { return cfg.metric ? (int)lroundf((f - 32) / 1.8f) : f; }
static const char* uTemp()  { return cfg.metric ? "C" : "F"; }
static int dMph(int mph)    { return cfg.metric ? (int)lroundf(mph * 1.60934f) : mph; }
static const char* uWind()  { return cfg.metric ? "km/h" : "mph"; }

static bool   sdOk = false;
static String ipStr;

// =============================================================================
// Shared state (netTask ⇄ loop, one mutex — the radar's discipline)
// =============================================================================
static SemaphoreHandle_t gMtx = nullptr;
// Guard parking space-net before a companion reflash (sce::CoopStop --
// the ONE shared implementation, see SceGuest.h). This bin missing it is
// what turned a ~9 s reflash into >10 min of SD/SPI contention (2026-08-10).
static sce::CoopStop netGuard;
struct Guard {
    Guard()  { if (gMtx) xSemaphoreTake(gMtx, portMAX_DELAY); }
    ~Guard() { if (gMtx) xSemaphoreGive(gMtx); }
};

// THE LIVE ELEMENT SET IS LOOP()'s, and only loop()'s. netTask parses into a
// STAGING slot and raises a flag; loop() swaps it in between frames. The first
// version had netTask assign `gProp` directly under a mutex that the drawing
// code never took — and `gProp = p` is a ~350-byte memberwise copy of doubles
// on a genuinely parallel core (netTask is pinned to 0, the Arduino loop to 1).
// A daily Celestrak refresh landing mid-frame produced a chimeric element set:
// no crash — propagate() rejects divergent cases and LovyanGFX clips — but a
// ground track in the wrong ocean, and worse, PassFinder caches `&gProp` and
// steps it forty times per loop pass for SECONDS, so a torn read mixes two
// orbits into a 48 h pass list that is then displayed as fact.
// This is the same shape as the band timer's command slot (A2.6): the thread
// that owns the state is the only one that mutates it.
static Tle   gTle;
static Sgp4  gProp;
static bool  gTleOk = false;
static char  gTleName[25] = "";
static Tle   gTleStage;
static Sgp4  gPropStage;
static char  gTleNameStage[25] = "";
static volatile bool gTleStaged = false;

// DEFERRED SD WRITES. netTask wrote the caches straight from core 0 while
// loop() pushed a 150 KB sprite from core 1 — and SD and the LCD share SPI2,
// which is the whole reason the companion has rule A2.16. The mutex guarded
// the parsed structs, never the BUS. netTask now parks the body in PSRAM and
// loop() writes it between frames, which is the one moment nothing is on the
// bus. Same shape as the band timer's command slot: the owner writes, the
// visitor asks.
struct PendingWrite {
    char*  buf = nullptr;
    size_t len = 0;
    const char* path = nullptr;
};
static PendingWrite gPend[2];              // one TLE slot, one launch slot
static volatile bool gPendReady = false;

static void queueSdWrite(int slot, const char* path, const char* body,
                         const char* prefix) {
    // Both refusals below used to be MUTE, and a cache that silently never
    // writes looks exactly like a cache that works until the next reboot.
    if (!sdOk || !body || !*body || slot < 0 || slot > 1) {
        if (path) sce::trace::log("sd", "ecriture %s refusee (carte=%d)",
                                  path, (int)sdOk);
        return;
    }
    Guard g;
    if (gPend[slot].buf) { free(gPend[slot].buf); gPend[slot].buf = nullptr; }
    const size_t pl = prefix ? strlen(prefix) : 0;
    const size_t n  = pl + strlen(body);
    char* b = (char*)ps_malloc(n + 1);
    if (!b) {                              // no cache is better than a stall
        sce::trace::log("sd", "ecriture %s : psram refusee (%u o)",
                        path, (unsigned)n);
        return;
    }
    if (pl) memcpy(b, prefix, pl);
    memcpy(b + pl, body, n - pl + 1);
    gPend[slot].buf = b; gPend[slot].len = n; gPend[slot].path = path;
    gPendReady = true;
}

// Called from loop() ONLY, between frames.
static void serviceSdWrites() {
    if (!gPendReady) return;
    for (int i = 0; i < 2; i++) {
        char* b = nullptr; const char* path = nullptr; size_t len = 0;
        { Guard g;
          b = gPend[i].buf; path = gPend[i].path; len = gPend[i].len;
          gPend[i].buf = nullptr; }
        if (!b) continue;
        File f = SD.open(path, FILE_WRITE);
        // The failed open was MUTE: a write-protected or full card made the
        // cache silently stale, which reads as a network bug two reboots later.
        if (f) { f.print(b); f.close();
                 sce::trace::log("sd", "ecriture %s ok (%u o)",
                                 path, (unsigned)len); }
        else   { sce::trace::log("sd", "ecriture %s ECHEC (open)", path); }
        free(b);
    }
    gPendReady = false;
}
static double gTleFetchedJd = 0.0;      // when WE fetched it (cache freshness)
// Where the CURRENT element set came from. Only the debug overlay reads it:
// "cache" with a three-week age and "net" with the same age are two different
// diagnoses (no network vs a broken staleness rule), and without this flag
// they are indistinguishable on the screen that exists to tell them apart.
static bool gTleFromNet = false;
// ONE LINE OF STATE, and it EXPIRES. It used to be written once and drawn for
// ever at the same (4, 228) every view's own footer uses, so a Settings URL or
// a "refreshing..." sat on top of "tap a row for its sky chart" and both
// became unreadable — and neither was ever cleared except by a SUCCESSFUL
// fetch, which on a bin with no network never comes (review 08-04).
// A transient hint lives ten seconds, an error two minutes: long enough to be
// read, short enough that the view gets its own footer back.
static char     gNetMsg[64] = "";
static uint32_t gNetMsgUntil = 0;

// Writes the state line with a lifetime. `errorish` is the caller's judgement,
// not a guess from the text: an error deserves to outlive a glance, a hint
// does not.
static void setNetMsg(const char* s, bool errorish) {
    // GUARDED. netTask (core 0) and applyEvent (core 1) both write this, and
    // drawFrame reads it — the two cores are genuinely parallel here, so a
    // long press posting "refreshing..." while httpGet posts "HTTP 429" could
    // put the interleaved remains of both on screen. Every other shared field
    // in this bin already takes this mutex; this one was simply missed.
    Guard g;
    strlcpy(gNetMsg, s ? s : "", sizeof(gNetMsg));
    gNetMsgUntil = gNetMsg[0] ? millis() + (errorish ? 120000u : 10000u) : 0;
}

// Copies the state line out under the same lock, so the frame draws a whole
// message or none — never half of one being overwritten.
static void takeNetMsg(char* out, size_t n) {
    Guard g;
    if (gNetMsg[0] && (int32_t)(millis() - gNetMsgUntil) >= 0) gNetMsg[0] = '\0';
    strlcpy(out, gNetMsg, n);
}

// What the LAST launch fetch actually did, step by step. Read through the
// /config diagnostic: guessing at a fetch that returns nothing and says
// nothing costs a reflash per hypothesis.
static char  gLaunchDiag[110] = "jamais";

struct Launch {
    char name[40] = "";                 // mission / launch designation
    char provider[22] = "";             // who is flying it
    char rocket[26] = "";               // vehicle configuration ("Falcon 9 Block 5")
    char pad[26] = "";                  // pad, then its site
    char orbit[14] = "";                // where the payload is going (LL2 only)
    char tag[16] = "";                  // booster serial / series (RLL only) —
                                        // "B1077" tells a Falcon watcher which
                                        // core is flying, which is exactly the
                                        // sort of thing this screen is for.
    char status[12] = "";               // LL2 status.abbrev: Go / TBC / TBD /
                                        // Hold / In Flight. EIGHT bytes truncated
                                        // "In Flight" to "In Flig", so the branch
                                        // colouring a flying vehicle never matched
                                        // and it drew grey, like TBD.
    time_t netUtc = 0;                  // T-0
    // PAD WEATHER, imperial as the source gives it — converted at DISPLAY time
    // exactly once, like every other unit in this bin. -999 = not published;
    // most non-US pads have none, and an invented 0 C would be a lie.
    char    wxCond[14] = "";
    int16_t wxTempF = -999;
    int16_t wxWindMph = -1;
};
static constexpr int MAX_LAUNCH = 5;
static Launch gLaunch[MAX_LAUNCH];
static int    gLaunchN = 0;
static time_t gLaunchFetched = 0;

// WHICH SOURCE served the list on screen. Not decoration: RocketLaunch.Live
// asks, in its terms of use, that the data be credited wherever it is shown —
// so the footer has to know which of the two answered. It doubles as the
// honest label a failover owes the reader.
enum class LaunchSrc : uint8_t { None, Rll, Ll2 };
static LaunchSrc gLaunchSrc = LaunchSrc::None;

// =============================================================================
// UI state
// =============================================================================
static M5Canvas canvas(&M5.Display);
// SIX views used to write out the A2.22 workaround by hand — six Cell structs,
// six counters, six lambdas, six flush loops. One shared table now, drawn from
// the single `noinline` call site in CellText::flush. Only one view draws at a
// time (everything here runs from loop()), so one instance is enough, and its
// 2 KB leaves loopTask's 8 KB stack instead of eating into it.
static sce::ui::CellText gCells;
// Ambient light. `gLtrOk` is the PROBE result and not the option: a board
// without the part must not have the option silently do nothing, and /config
// reports both (`diag`).
static sce::ltr553::Lite gLtr;
static bool gLtrOk = false;
// Raised by a settings save, consumed by the sensor loop: forget the
// brightness hysteresis so the ruling source reasserts itself at once.
static volatile bool gAutoBrightReset = false;
static sce::SceGuest guest;
#if SCE_HAS_SERVO
static sce::HeadServo gHead;               // tracking the satellite (option)
static volatile bool  gHeadInitReq = false; // begin() requested from /config
#endif
static View     gView = View::Iss;
static int      gCursor = 0;            // item cursor in the two list views
// Modals. An ENUM and not two booleans: the polar chart and the orbital view
// are mutually exclusive, and two flags would let a bug show both.
enum class Modal : uint8_t { None, PolarChart, SkyDome };
static Modal    gModal = Modal::None;
static int      gSkySel = 2;            // Mars: something worth looking at
static uint32_t dockLastMs = 0;
static bool     gRedraw = true;

// Pass search — sliced from loop() (SPACE.md trap 2).
static PassFinder gPasses;
static Pass  gPassList[PassFinder::MAX_PASSES];
static int   gPassN = 0;
static bool  gPassSearching = false;
static double gPassSearchedJd = 0.0;

// The first pass that has NOT already happened. Nothing dropped elapsed
// passes from the list and the re-search only fired every six hours, so at
// 01:00 the ISS view still announced a pass that rose at 20:12 — and the
// remaining-time arithmetic rendered it as "in -4h-48" (review 08-04). A list
// of future events must never contain the past.
static int firstUpcomingPass(double jd) {
    for (int i = 0; i < gPassN; i++)
        if (gPassList[i].setJd > jd) return i;
    return -1;
}


// =============================================================================
// Clock helpers
// =============================================================================
// Everything here is gated on a SYNCHRONISED clock. `clockSynced` is the
// project-wide test (a plausible epoch, not merely "non-zero").
static inline bool clockOk() { return sce::clockSynced(time(nullptr)); }

static double nowJd() {
    const time_t t = time(nullptr);
    struct tm g;
    gmtime_r(&t, &g);
    return jdFromUtc(g.tm_year + 1900, g.tm_mon + 1, g.tm_mday,
                     g.tm_hour, g.tm_min, (double)g.tm_sec);
}

// A Julian date, printed in the user's LOCAL time. The bin reads its own
// tz_offset_h rather than the system zone: a guest bin owns its config.
static void jdToLocalHhmm(double jd, char* out, size_t n) {
    // A Julian date, not a time_t, so it keeps its own conversion — but the
    // OFFSET RULE is the shared one: everything internal is UTC, the offset is
    // applied at display time exactly once (sce::localHhmm states it).
    const CalDate c = calFromJd(jd + cfg.tzOffsetH / 24.0);
    snprintf(out, n, "%02d:%02d", c.hour, c.minute);
}
static void jdToLocalDayHhmm(double jd, char* out, size_t n) {
    const CalDate c = calFromJd(jd + cfg.tzOffsetH / 24.0);
    snprintf(out, n, "%02d/%02d %02d:%02d", c.day, c.month, c.hour, c.minute);
}

// =============================================================================
// Networking
// =============================================================================
// ONE fetch helper for both sources (the radar's fetchJson lesson: four
// near-identical HTTPS blocks had drifted apart). 15 s STREAM timeout — a
// slow body over weak WiFi was being truncated at 8 s and reported as a parse
// error, which sent everyone looking at the parser.
static bool httpGet(const char* url, sce::PsSink& sink, size_t cap) {
    // Refused during a stop request so the task PARKS mid-sequence too:
    // tleFetch chains several GETs, and the guard's ACK window is sized for
    // ONE longest request, not for a whole chain.
    // The two mute refusals below are TRACED (radar's fetchJson lesson): the
    // reflash guard and a dropped WiFi both silence a whole fetch chain with
    // no visible cause, and a capture that shows requests simply stopping
    // reads as a crash rather than a parking.
    if (netGuard.stopping()) {
        sce::trace::log("http", "netguard stoppe : requete annulee");
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        sce::trace::log("http", "WiFi absent : requete annulee");
        return false;
    }
    // Host+path only, never the query string (the radar's rule): callers put
    // identifiers in it, and the trace must stay safe to paste into a report.
    const char* q = strchr(url, '?');
    const int urlLen = q ? (int)(q - url) : (int)strlen(url);
    WiFiClientSecure client;
    client.setInsecure();               // no CA bundle on board; see SPACE.md
    HTTPClient http;
    http.setTimeout(15000);
    http.setConnectTimeout(8000);
    if (!http.begin(client, url)) {
        // A begin() refusal (bad URL, no memory for TLS) is otherwise mute and
        // indistinguishable from a server that never answered.
        sce::trace::log("http", "begin KO %.*s", urlLen, url);
        return false;
    }
    http.addHeader("User-Agent", "stackchan-space/1.0");
    // THE canonical line of the debug trace: every request in this bin funnels
    // through here, so one line per attempt reconstructs the whole network
    // timeline — including how long a timeout actually took, and any 429.
    const uint32_t t0 = millis();
    const int code = http.GET();
    sce::trace::log("http", "GET %.*s -> %d en %lu ms taille=%d",
                    urlLen, url, code,
                    (unsigned long)(millis() - t0), http.getSize());
    if (code != 200) {
        char e[24]; snprintf(e, sizeof(e), "HTTP %d", code); setNetMsg(e, true);
        http.end();
        return false;
    }
    const int len = http.getSize();
    if (len > 0 && (size_t)len > cap) {
        char e[40]; snprintf(e, sizeof(e), "reponse %d o > %u", len, (unsigned)cap); setNetMsg(e, true);
        http.end();
        return false;
    }
    // writeToStream() IS THE POINT. HTTPClient only DECHUNKS through it;
    // getStream()/getStreamPtr() hand back the raw socket, chunk-size lines
    // and all. Reading that raw stream is what emptied this view: Launch
    // Library answers `Transfer-Encoding: chunked`, the body arrived as
    // "13ea\r\n{...", ArduinoJson parsed the 13 as a number, stopped, and
    // returned Ok on a document with no `results` — a blank list with no
    // error anywhere. flight-radar hit the identical trap on autorouter and
    // solved it with PsSink; that class now lives in firmware/common/PsJson.h
    // precisely so the second bin did not have to rediscover it (08-04).
    const bool ok = http.writeToStream(&sink) > 0 && !sink.overflowed();
    if (sink.overflowed()) {
        char e[32];
        snprintf(e, sizeof(e), "reponse > %u o", (unsigned)cap);
        setNetMsg(e, true);
    }
    http.end();
    return ok;
}


// ---- TLE: the cache is the primary source, the network the exception -------
static bool tleParseAndArm(const char* l0, const char* l1, const char* l2) {
    Tle t;
    if (!parseTle(l1, l2, t, l0)) return false;
    if (t.isDeepSpace()) {              // SGP4 alone cannot: say so
        setNetMsg(sce::T("high orbit unsupported", "orbite haute non geree"), true);
        return false;
    }
    Sgp4 p;
    if (!p.init(t)) return false;
    Guard g;
    gTleStage = t; gPropStage = p;
    strlcpy(gTleNameStage, t.name[0] ? t.name : "SAT", sizeof(gTleNameStage));
    gTleStaged = true;                  // loop() swaps it in between frames
    return true;
}

// Called from loop() ONLY. Returns true when a new element set was adopted.
static bool tleAdoptStaged() {
    if (!gTleStaged) return false;
    Guard g;
    gTle = gTleStage; gProp = gPropStage; gTleOk = true;
    strlcpy(gTleName, gTleNameStage, sizeof(gTleName));
    gTleStaged = false;
    return true;
}

static bool tleLoadFromSd() {
    if (!sdOk) return false;
    File f = SD.open(TLE_PATH, FILE_READ);
    if (!f) return false;
    char l0[32] = "", l1[80] = "", l2[80] = "";
    f.readBytesUntil('\n', l0, sizeof(l0) - 1);
    f.readBytesUntil('\n', l1, sizeof(l1) - 1);
    f.readBytesUntil('\n', l2, sizeof(l2) - 1);
    // The fetch date rides in the file's own first token so the cache knows
    // its own age without a second file to keep in step.
    f.close();
    for (char* p : { l0, l1, l2 })
        for (char* c = p; *c; c++) if (*c == '\r') *c = '\0';
    if (!tleParseAndArm(l0, l1, l2)) return false;
    // KEYED TO THE SATELLITE. The staleness test never mentioned `norad`, so
    // changing the tracked object in the settings left the PREVIOUS one
    // propagating under the new number — and a reboot re-adopted it from this
    // single unkeyed file with its own epoch as the freshness stamp, which
    // could hold the wrong object for ever. The catalogue number is already
    // parsed; it just had to be compared.
    if (gTleStage.norad != cfg.norad) {
        gTleStaged = false;
        Serial.printf("[space] cache TLE = %lu, demande %lu : ignore\n",
                      (unsigned long)gTleStage.norad, (unsigned long)cfg.norad);
        return false;
    }
    // STAMP THE CACHE AS OLD-BUT-KNOWN. Left at 0, the staleness test
    // `gTleFetchedJd > 0 && ...` was false for ever, so a bin that booted from
    // the card NEVER fetched again: with a three-week-old file it drew the ISS
    // tens of kilometres off and said so permanently, without once trying to
    // fix it. The epoch is what we actually know about this element set, so it
    // is what the freshness test should stand on.
    gTleFetchedJd = gTleStage.epochJd;
    return true;
}

static void tleSaveToSd(const String& body) {
    queueSdWrite(0, TLE_PATH, body.c_str(), nullptr);
}

static void tleFetch() {
    char url[128];
    snprintf(url, sizeof(url),
             "https://celestrak.org/NORAD/elements/gp.php?CATNR=%lu&FORMAT=tle",
             (unsigned long)cfg.norad);
    // The catalogue number is in the query string, which the http trace cuts
    // off on purpose — so it is said here, where the fetch is decided.
    sce::trace::log("net", "TLE : recuperation norad=%lu",
                    (unsigned long)cfg.norad);
    sce::PsSink sink;
    if (!httpGet(url, sink, 4096)) return;
    const String body(sink.data());
    // Celestrak answers a 3-line set; anything else (an error page, an
    // unknown catalogue number) must NOT overwrite a good cache.
    int a = body.indexOf('\n');
    int b = body.indexOf('\n', a + 1);
    if (a < 0 || b < 0) {
        // An error page or an unknown catalogue number: say what CAME, the
        // size is the one clue that separates "empty answer" from "HTML page".
        sce::trace::log("net", "TLE illisible (%u o)",
                        (unsigned)strlen(sink.data()));
        setNetMsg(sce::T("TLE unreadable", "TLE illisible"), true);
        return;
    }
    char l0[32] = "", l1[80] = "", l2[80] = "";
    strlcpy(l0, body.substring(0, a).c_str(), sizeof(l0));
    strlcpy(l1, body.substring(a + 1, b).c_str(), sizeof(l1));
    strlcpy(l2, body.substring(b + 1, b + 1 + 69).c_str(), sizeof(l2));
    for (char* p : { l0, l1, l2 })
        for (char* c = p; *c; c++) if (*c == '\r') *c = '\0';
    if (tleParseAndArm(l0, l1, l2)) {
        tleSaveToSd(body);
        gTleFetchedJd = nowJd();
        gTleFromNet   = true;          // the overlay's "net", vs the SD cache
        sce::trace::log("net", "TLE recu : %s epoque %.3f",
                        gTleNameStage, gTleStage.epochJd);
        { Guard g; gNetMsg[0] = '\0'; gNetMsgUntil = 0; }
    } else {
        // parseTle refused a body that LOOKED like three lines — the case a
        // silent return turns into "the fetch never happened".
        sce::trace::log("net", "TLE : analyse refusee");
    }
}

// ---- the launch feed, two sources ------------------------------------------
// PRIMARY: RocketLaunch.Live's free shortcut. FALLBACK: Launch Library 2.
// The order is not a preference, it is what each one costs and carries:
//
//   · LL2 allows FIFTEEN requests an hour and answers a flat 429 past that —
//     the ceiling this bin actually hit while its own view was being written.
//     RLL publishes no limit on `/json/launches/next/N` and needs no key.
//   · LL2 packs the vehicle and the mission into ONE `name` field joined by
//     " | ", so the vehicle — the thing the silhouette is chosen from — had to
//     be recovered by splitting a string. RLL gives `vehicle.name` outright.
//   · RLL publishes PAD WEATHER, which nothing else here had and which is the
//     other half of "will it go".
//
// What LL2 has and RLL does not is the target ORBIT; RLL has the booster
// serial instead. Neither is worth failing over for, so the view shows
// whichever arrived (`orbit` or `tag`) and the footer names the source.
//
// The two answers are told apart by SHAPE, not by a flag stored beside them:
// LL2 nests everything under `results`, RLL under `result`. That is what lets
// the SD cache stay a verbatim copy of whatever body worked — a cache that
// needed its own format field would be a third thing to keep in step.
static bool launchCommit(const Launch* tmp, int n, LaunchSrc src) {
    snprintf(gLaunchDiag + strlen(gLaunchDiag),
             sizeof(gLaunchDiag) - strlen(gLaunchDiag), " n=%d", n);
    if (n <= 0) return false;
    Guard g;
    for (int i = 0; i < n; i++) gLaunch[i] = tmp[i];
    gLaunchN = n;
    gLaunchSrc = src;
    return true;
}

// ISO-8601 as the two sources write it. RLL omits the seconds ("2026-08-05T
// 02:35Z"), so a %2d:%2d:%2d scan of it fails and every T-0 lands at zero —
// which is why this accepts a missing seconds field instead of demanding six.
static time_t parseIsoUtc(const char* s) {
    if (!s || !s[0]) return 0;
    struct tm tmv{};
    const int got = sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d",
                           &tmv.tm_year, &tmv.tm_mon, &tmv.tm_mday,
                           &tmv.tm_hour, &tmv.tm_min, &tmv.tm_sec);
    if (got < 5) return 0;
    tmv.tm_year -= 1900; tmv.tm_mon -= 1; tmv.tm_isdst = 0;
    return mktime(&tmv);                  // configTime(0,0,..): already UTC
}

// RocketLaunch.Live. Every field is its own key, so nothing is split out of a
// display string — the failure mode LL2's `name` forced on us.
static bool launchParseRll(const char* body) {
    sce::psAlloc.reset();
    JsonDocument doc(&sce::psAlloc);
    JsonDocument filter;
    // The SAME idiom as the LL2 filter below, deliberately: `filter[k][0][...]`
    // is what ArduinoJson reads as "every element of this array", and building
    // the element with .to<JsonObject>() first is the variant that has bitten
    // this bin once already (an empty list, no error).
    filter["result"][0]["name"] = true;
    filter["result"][0]["sort_date"] = true;      // unix epoch, ALWAYS present
    filter["result"][0]["t0"] = true;
    filter["result"][0]["win_open"] = true;
    filter["result"][0]["est_date"] = true;
    filter["result"][0]["provider"]["name"] = true;
    filter["result"][0]["vehicle"]["name"] = true;
    filter["result"][0]["pad"]["name"] = true;
    filter["result"][0]["pad"]["location"]["name"] = true;
    filter["result"][0]["tags"][0]["text"] = true;
    filter["result"][0]["weather_condition"] = true;
    filter["result"][0]["weather_temp"] = true;
    filter["result"][0]["weather_wind_mph"] = true;
    DeserializationError err = deserializeJson(doc, body,
                                               DeserializationOption::Filter(filter));
    if (err) {
        snprintf(gLaunchDiag + strlen(gLaunchDiag),
                 sizeof(gLaunchDiag) - strlen(gLaunchDiag), " json %s", err.c_str());
        return false;
    }
    Launch tmp[MAX_LAUNCH];
    int n = 0;
    for (JsonObject r : doc["result"].as<JsonArray>()) {
        if (n >= MAX_LAUNCH) break;
        strlcpy(tmp[n].name,     r["name"] | "",              sizeof(tmp[n].name));
        strlcpy(tmp[n].rocket,   r["vehicle"]["name"] | "",   sizeof(tmp[n].rocket));
        strlcpy(tmp[n].provider, r["provider"]["name"] | "",  sizeof(tmp[n].provider));
        // Pad, then its site — "SLC-40, Cape Canaveral SFS". The site alone
        // loses which of a dozen pads it is; the pad alone means nothing.
        const char* padN = r["pad"]["name"] | "";
        const char* site = r["pad"]["location"]["name"] | "";
        if (padN[0] && site[0]) snprintf(tmp[n].pad, sizeof(tmp[n].pad), "%s, %s", padN, site);
        else                    strlcpy(tmp[n].pad, padN[0] ? padN : site, sizeof(tmp[n].pad));
        strlcpy(tmp[n].tag, r["tags"][0]["text"] | "", sizeof(tmp[n].tag));

        // TIME. `sort_date` is a unix epoch and is filled even when the date is
        // an estimate, so it is what the countdown stands on; t0/win_open are
        // read only to judge HOW FIRM that time is.
        const char* t0  = r["t0"] | "";
        const char* win = r["win_open"] | "";
        tmp[n].netUtc = (time_t)strtoul(r["sort_date"] | "0", nullptr, 10);
        if (tmp[n].netUtc == 0) tmp[n].netUtc = parseIsoUtc(t0[0] ? t0 : win);

        // STATUS, derived — RLL has no `status.abbrev`, but it says plainly
        // enough how much it believes its own date. `est_date` carries a year
        // ONLY when the entry is an estimate, and that outranks a t0: entries
        // arrive with both a precise-looking t0 and "estimated" in their own
        // summary. So: estimated -> TBD, exact time -> Go, window only -> TBC.
        JsonObject est = r["est_date"];
        const bool estimated = !est.isNull() && !est["year"].isNull();
        strlcpy(tmp[n].status, estimated ? "TBD" : (t0[0] ? "Go" : (win[0] ? "TBC" : "TBD")),
                sizeof(tmp[n].status));

        strlcpy(tmp[n].wxCond, r["weather_condition"] | "", sizeof(tmp[n].wxCond));
        if (!r["weather_temp"].isNull())
            tmp[n].wxTempF = (int16_t)lroundf(r["weather_temp"].as<float>());
        if (!r["weather_wind_mph"].isNull())
            tmp[n].wxWindMph = (int16_t)lroundf(r["weather_wind_mph"].as<float>());
        n++;
    }
    return launchCommit(tmp, n, LaunchSrc::Rll);
}

// Launch Library 2, kept as the fallback.
static bool launchParseLl2(const char* body) {
    sce::psAlloc.reset();
    JsonDocument doc(&sce::psAlloc);
    JsonDocument filter;
    filter["results"][0]["name"] = true;
    filter["results"][0]["net"] = true;
    filter["results"][0]["status"]["abbrev"] = true;
    filter["results"][0]["lsp_name"] = true;
    filter["results"][0]["pad"] = true;
    filter["results"][0]["location"] = true;
    filter["results"][0]["orbit"] = true;
    DeserializationError err = deserializeJson(doc, body,
                                               DeserializationOption::Filter(filter));
    if (err) {
        snprintf(gLaunchDiag + strlen(gLaunchDiag),
                 sizeof(gLaunchDiag) - strlen(gLaunchDiag), " json %s", err.c_str());
        return false;
    }
    Launch tmp[MAX_LAUNCH];
    int n = 0;
    for (JsonObject r : doc["results"].as<JsonArray>()) {
        if (n >= MAX_LAUNCH) break;
        const char* nm  = r["name"] | "";
        const char* net = r["net"] | "";
        const char* bar = strstr(nm, " | ");
        if (bar) {
            size_t rl = (size_t)(bar - nm);
            if (rl >= sizeof(tmp[n].rocket)) rl = sizeof(tmp[n].rocket) - 1;
            memcpy(tmp[n].rocket, nm, rl);
            tmp[n].rocket[rl] = '\0';
            strlcpy(tmp[n].name, bar + 3, sizeof(tmp[n].name));
        } else {
            strlcpy(tmp[n].name, nm, sizeof(tmp[n].name));
            tmp[n].rocket[0] = '\0';
        }
        strlcpy(tmp[n].status,   r["status"]["abbrev"] | "", sizeof(tmp[n].status));
        strlcpy(tmp[n].provider, r["lsp_name"] | "",         sizeof(tmp[n].provider));
        strlcpy(tmp[n].orbit,    r["orbit"] | "",            sizeof(tmp[n].orbit));
        const char* site = r["location"] | "";
        const char* padN = r["pad"] | "";
        strlcpy(tmp[n].pad, site[0] ? site : padN, sizeof(tmp[n].pad));
        tmp[n].netUtc = parseIsoUtc(net);
        n++;
    }
    return launchCommit(tmp, n, LaunchSrc::Ll2);
}

// The ONE entry point, for the network fetch and for the SD cache alike: the
// cache exists precisely so the two paths are equivalent, and a second decoder
// on one side of it would be exactly the "assumed twin" A2.23 forbids.
// Sniffed on `"results"` FIRST — "result" is a prefix of it, and RLL also uses
// a per-entry `"result"` key for the flight outcome, so testing the shorter
// one first would read an LL2 body as an RLL one.
static bool launchParse(const char* body) {
    if (strstr(body, "\"results\"")) return launchParseLl2(body);
    if (strstr(body, "\"result\""))  return launchParseRll(body);
    snprintf(gLaunchDiag, sizeof(gLaunchDiag), "forme inconnue");
    return false;
}

// EVERY BOOT USED TO COST A REQUEST, and Launch Library allows fifteen an
// hour. Cycling the bin a dozen times while working on this view earned a
// flat HTTP 429 and an empty list (08-04) — the etiquette was written into
// the poll period and not into the restart, which is the one a developer
// exercises most. The list is now cached on the card exactly as the TLE is:
// read at boot, shown immediately, and refreshed on the normal schedule.
// THE CACHE CARRIES ITS OWN FETCH TIME, on a first line before the JSON. Two
// things depended on it and neither worked without (review 08-04):
//   · the "as of hh:mm" footer is gated on gLaunchFetched, so a list read from
//     the card was shown as LIVE however old it was — the exact lie that
//     footer exists to prevent;
//   · the first netTask pass fired on `lastLaunch == 0`, i.e. on every boot,
//     so the cache only made the list appear FASTER and the request count per
//     reboot was unchanged. That is the whole reason the cache was added, and
//     it did not close it.
// A line of decimal epoch costs eleven bytes and closes both.
static void launchLoadFromSd() {
    if (!sdOk) return;
    File f = SD.open(LNC_PATH, FILE_READ);
    if (!f) return;
    const size_t sz = f.size();
    if (sz == 0 || sz > 96 * 1024) { f.close(); return; }
    char* buf = (char*)sce::psAlloc.allocate(sz + 1);
    if (!buf) { f.close(); return; }
    const size_t got = f.read((uint8_t*)buf, sz);
    buf[got] = '\0';
    f.close();
    const char* json = buf;
    time_t stamp = 0;
    if (buf[0] != '{') {                    // a stamped cache: epoch, then JSON
        stamp = (time_t)strtoul(buf, nullptr, 10);
        const char* nl = strchr(buf, '\n');
        json = nl ? nl + 1 : buf;
    }
    snprintf(gLaunchDiag, sizeof(gLaunchDiag), "cache %uo", (unsigned)got);
    if (launchParse(json)) gLaunchFetched = stamp;   // 0 = unknown, stays honest
    sce::psAlloc.deallocate(buf);
}

static void launchSaveToSd(const char* body, time_t stamp) {
    char pre[16];
    snprintf(pre, sizeof(pre), "%lu\n", (unsigned long)stamp);
    queueSdWrite(1, LNC_PATH, body, pre);
}

// One source, end to end. Returns true only when a list was actually adopted:
// an HTTP 200 that decodes to nothing must fall through to the other source,
// not leave the screen empty with a green light. (`mode=list` is what keeps
// LL2 within reach at all: the full serialisation of five launches is ~300 KB,
// the list mode ~15 KB.)
static bool launchTry(const char* url, const char* label, bool keepPrev) {
    sce::PsSink sink;
    // THE FALLBACK APPENDS, the primary overwrites. Wiping the diagnosis on the
    // way to the second source destroyed the only remote evidence that the
    // first one had stopped serving — which is precisely what this field was
    // added for, and precisely the situation the failover creates.
    char* d = gLaunchDiag;
    size_t cap = sizeof(gLaunchDiag);
    if (keepPrev) {
        const size_t used = strlen(gLaunchDiag);
        if (used + 8 < cap) {
            d = gLaunchDiag + used; cap -= used;
            snprintf(d, cap, " -> "); d += 4; cap -= 4;
        }
    }
    snprintf(d, cap, "%s get...", label);
    sce::trace::log("net", "lancements : tentative %s", label);
    if (!httpGet(url, sink, 96 * 1024)) {
        char msg[sizeof(gNetMsg)];
        takeNetMsg(msg, sizeof(msg));       // UNDER THE LOCK, like every reader:
        snprintf(d, cap, "%s KO %s", label, msg);   // core 1 writes it too
        // The HTTP code (429 included) is already on the http trace line; this
        // one says which SOURCE the failure belongs to.
        sce::trace::log("net", "lancements %s KO : %s", label, msg);
        return false;
    }
    // The size VERBATIM: the robot and the PC do not necessarily get the same
    // answer from a CDN, and "0 results" is consistent with both a filter bug
    // and a body nobody looked at.
    const char* body = sink.data();
    snprintf(d, cap, "%s %uo", label, (unsigned)strlen(body));
    if (!launchParse(body)) {
        // A 200 that decodes to nothing: gLaunchDiag holds the step-by-step,
        // and the trace is where it becomes readable without opening /config.
        sce::trace::log("net", "lancements %s : analyse KO (%s)",
                        label, gLaunchDiag);
        return false;
    }
    sce::trace::log("net", "lancements %s : %d adopte(s)", label, gLaunchN);
    { Guard g; gLaunchFetched = time(nullptr); }   // read under lock by the view
    { Guard g; gNetMsg[0] = '\0'; gNetMsgUntil = 0; }
    launchSaveToSd(body, gLaunchFetched);      // a reboot must not cost a request
    return true;
}

static void launchFetch() {
    // FAILOVER, in the radar's discipline: try the cheaper source, fall through
    // on any failure — refused, unreachable, or decoded to nothing — and stop
    // at the first that serves. The fallback is NOT tried on a good answer, so
    // the normal cycle stays one request.
    if (launchTry("https://fdo.rocketlaunch.live/json/launches/next/5",
                  "rll", false)) return;
    launchTry("https://ll.thespacedevs.com/2.2.0/launch/upcoming/"
              "?limit=5&mode=list&hide_recent_previous=true", "ll2", true);
}


// ---- the network task ------------------------------------------------------
// Core 0, 16 KB of stack: one TLS session plus ArduinoJson overflows the 8 KB
// of loopTask (the radar paid for that discovery with a Guru Meditation).
static volatile bool netForce = false;
static void netTask(void*) {
    uint32_t lastLaunch = 0, lastTle = 0;
    for (;;) {
        if (netGuard.shouldPark()) continue;   // reflash: park (ACK inside)
        const uint32_t now = millis();
        const bool force = netForce;
        netForce = false;

        if (clockOk() && WiFi.status() == WL_CONNECTED) {
            // TLE: at most once a day (Celestrak etiquette), or when the cache
            // is missing/too old to be trusted, or on an explicit refresh.
            const bool stale = !gTleOk ||
                               (gTleFetchedJd > 0 && nowJd() - gTleFetchedJd > 1.0);
            if ((stale && (lastTle == 0 || now - lastTle > 3600000UL)) ||
                (force && now - lastTle > 60000UL)) {
                lastTle = now;
                tleFetch();
            }
            // Launches: the poll floor is the API's throttle, not a taste.
            // DUE BY THE AGE OF THE DATA, not by the age of the boot. Testing
            // `lastLaunch == 0` fetched on every start, which is what earned a
            // flat HTTP 429 while working on this view: the etiquette was in
            // the poll period and not in the restart, and the restart is the
            // one a developer — and a dock-mode robot — exercises most.
            const uint32_t period = (uint32_t)cfg.launchPollMin * 60000UL;
            const time_t nowT = time(nullptr);
            const bool cacheDue = (gLaunchN == 0) || (gLaunchFetched == 0) ||
                                  (nowT - gLaunchFetched >=
                                   (time_t)cfg.launchPollMin * 60);
            if ((lastLaunch == 0 && cacheDue) || now - lastLaunch > period ||
                (force && now - lastLaunch > 900000UL)) {
                lastLaunch = now;
                launchFetch();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// =============================================================================
// Drawing — shared chrome
// =============================================================================
static constexpr int SCR_W = 320, SCR_H = 240;
static constexpr int HDR_H = 18, FTR_Y = 228;

// THE TITLE THE USER READS, which is not the same string as the view's NAME.
// `spa::viewName` lives in the pure input header — no Arduino, no `sce::T` —
// and it is an IDENTIFIER: it is what the native tests assert on and what the
// serial log prints. Translating it there would have dragged the i18n table
// into a file that exists precisely to have no dependencies.
//
// So the header title is localised HERE, where the display lives. It was
// simply missing: every view announced itself in English on a French robot,
// which is the one string on the screen that is always visible.
static const char* viewTitle(spa::View v) {
    switch (v) {
        case spa::View::Iss:      return "ISS";
        case spa::View::Passes:   return sce::T("PASSES",   "PASSAGES");
        case spa::View::Moon:     return sce::T("MOON",     "LUNE");
        case spa::View::Sky:      return sce::T("SKY",      "CIEL");
        default:                  return sce::T("LAUNCHES", "LANCEMENTS");
    }
}

static void drawHeader() {
    canvas.fillRect(0, 0, SCR_W, HDR_H, TH->panelBg);
    canvas.setTextSize(1);
    canvas.setTextColor(TH->accent);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.drawString(viewTitle(gView), 4, 5);
    if (clockOk()) {
        const time_t t = time(nullptr);
        struct tm g; gmtime_r(&t, &g);
        char buf[32], loc[8];
        sce::localHhmm(t, cfg.tzOffsetH, loc, sizeof(loc));
        snprintf(buf, sizeof(buf), "%02d:%02dZ  %s", g.tm_hour, g.tm_min, loc);
        canvas.setTextColor(TH->txt2);
        canvas.setTextDatum(textdatum_t::top_right);
        canvas.drawString(buf, SCR_W - 4, 5);
        canvas.setTextDatum(textdatum_t::top_left);
    }
    // Ring position: five pips, the current one filled. Cheap orientation on
    // a bin with no visible menu (A2.22: one call site per shape).
    for (int i = 0; i < (int)View::COUNT; i++) {
        const int x = SCR_W / 2 - 20 + i * 9;
        const bool on = (i == (int)gView);
        canvas.fillRect(x, 8, on ? 5 : 3, on ? 5 : 3, on ? TH->accent : TH->sep);
    }
}

// The one place a STATE is written. Data zones are never overwritten by a
// status line — the radar's rule, and the reason its errors are readable.
static void drawFooter(const char* msg, uint16_t col) {
    if (!msg || !msg[0]) return;
    canvas.setTextSize(1);
    canvas.setTextColor(col);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.drawString(msg, 4, FTR_Y);
}

// TLE staleness (SPACE.md trap 3): past 14 days SGP4 has drifted by tens
// of kilometres. The view says so instead of drawing a confident wrong dot.
static bool tleStale(double jd) { return gTleOk && (jd - gTle.epochJd) > 14.0; }

static void drawWaitingClock() {
    canvas.setTextSize(2);
    canvas.setTextColor(TH->hint);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.drawString(sce::T("waiting for clock", "attente horloge"),
                      SCR_W / 2, SCR_H / 2);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextSize(1);
}

// =============================================================================
// View 1 — ISS: the world map
// =============================================================================
// EDGE TO EDGE (user 08-04). The map had a 10 px margin all round, which on a
// 320 px screen is 6 % of the only thing this view is about. It now runs from
// x = 0 to 320 and from just under the header to the data rows, and the mask
// was regenerated at 320x160 to match — the blit is 1:1, and resampling a
// coastline to fit a different rectangle is how a shoreline stops lining up
// with the ground track drawn over it.
//
// COASTLINE, NOT LANDMASS (user 08-04: "more discreet, outline only?"). A
// filled continent turned the day/night shading into a second variable inside
// the same block of colour — the eye had to separate "land or sea" from "lit
// or dark" at once. Now the fill says exactly one thing (where the Sun is) and
// the outline says the other (where the coasts are).
static constexpr int MAP_X = 0, MAP_Y = 18;
static constexpr int MAP_W = spa::WORLD_W, MAP_H = spa::WORLD_H;   // 320 x 160
static constexpr int MAP_B = MAP_Y + MAP_H;                        // 178

static inline int mapLonX(double lon) { return MAP_X + (int)((lon + 180.0) / 360.0 * MAP_W); }
static inline int mapLatY(double lat) { return MAP_Y + (int)((90.0 - lat) / 180.0 * MAP_H); }

// Night row range per column, recomputed once per frame. Static rather than on
// the stack: loopTask has 8 KB and this is 1.3 KB of it.
static int16_t sNightY0[MAP_W], sNightY1[MAP_W];

// THE GROUND TRACK, CACHED. Recomputing it every frame ran 101 SGP4
// propagations AND 101 iterative geodetic conversions — all in software
// `double` — inside the same body that then fills 320 columns and plots the
// coastline. `sgp4.h` states the rule in its own header ("propagate ON DEMAND
// ... NEVER once per frame"), and the pass search was sliced for exactly this
// reason while this was not (review 08-04).
//
// REBUILT EVERY 20 SECONDS, and that is not a compromise: the view's own
// comment computes that the ISS moves half a pixel in 500 ms, so 20 s is
// twenty pixels — one rebuild for forty frames, and the curve is redrawn from
// the cache in between. The MARKER still moves every frame; it costs one
// propagation, which is the one the view genuinely needs live.
static constexpr int TRK_N = 101;                 // -50..+50 minutes
static int16_t sTrkX[TRK_N], sTrkY[TRK_N];
static bool    sTrkOk[TRK_N];
static double  sTrkJd = 0.0;                      // instant it was built for

// The "no element set" state, in its OWN function with `noinline`. Not style:
// left inline it was a second drawString in the same body as the figures loop
// — two similar calls, the A2.22 shape — and inlining would have folded the
// split straight back out (check-a222.py caught exactly that on drawSkyNames).
static __attribute__((noinline)) void drawIssNotice() {
    canvas.setTextColor(TH->hint);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.drawString(sce::T("no element set", "pas de jeu d'elements"),
                      MAP_W / 2, MAP_Y + MAP_H / 2);
    canvas.setTextDatum(textdatum_t::top_left);
}
// EVERY VIEW FUNCTION IS `noinline`, and that is a gate requirement rather
// than a taste: each is called from exactly one place, so GCC folds them into
// drawFrame and their symbols vanish — at which point check-a222.py cannot see
// the single-call-site discipline at all and reports "symbole absent" (it did,
// on this very change). One call per frame at 2 Hz is not a cost.
static __attribute__((noinline)) void drawIssView(double jd) {
    // --- Where night falls, column by column. The terminator is the great
    // circle 90 degrees from the subsolar point; at a given longitude it sits
    // at latitude atan(-cos(dLon)/tan(subLat)).
    const Equatorial sun = sunPosition(jd);
    const double subLat = sun.decDeg;
    const double subLon = wrap180(sun.raDeg - gmstRad(jd) * RAD2DEG_D);
    const double tanDec = tan(subLat * DEG2RAD_D);
    // WHICH SIDE IS DARK follows from the Sun's hemisphere alone: a point is
    // in daylight when sin(lat)sin(subLat) + cos(lat)cos(subLat)cos(dLon) > 0,
    // so with the Sun NORTH the lit half is the northern one and night lies
    // SOUTH of the terminator — and vice versa. (The first version folded the
    // longitude into this test and shaded the lit half at some longitudes.)
    const bool nightAbove = subLat < 0.0;
    for (int px = 0; px < MAP_W; px++) {
        const double lon = -180.0 + (px + 0.5) * 360.0 / MAP_W;
        const double c = cos((lon - subLon) * DEG2RAD_D);
        if (fabs(tanDec) < 1e-4) {
            // Equinox: the terminator IS a meridian, so a column is entirely
            // day or entirely night. Dividing by tan(subLat) here would give
            // an infinite latitude.
            sNightY0[px] = 0;
            sNightY1[px] = (c > 0.0) ? 0 : (int16_t)MAP_H;
            continue;
        }
        const double termLat = atan(-c / tanDec) * RAD2DEG_D;
        int ty = (int)((90.0 - termLat) / 180.0 * MAP_H);
        if (ty < 0) ty = 0;
        if (ty > MAP_H) ty = MAP_H;
        if (nightAbove) { sNightY0[px] = 0;            sNightY1[px] = (int16_t)ty; }
        else            { sNightY0[px] = (int16_t)ty;  sNightY1[px] = (int16_t)MAP_H; }
    }

    // --- The lit and dark halves, one column at a time: at most two vertical
    // runs per column, emptied through ONE drawFastVLine call site (A2.22).
    for (int px = 0; px < MAP_W; px++) {
        const int n0 = sNightY0[px], n1 = sNightY1[px];
        int      segY[3], segH[3];
        uint16_t segC[3];
        int n = 0;
        if (n0 > 0)      { segY[n] = 0;  segH[n] = n0;         segC[n] = TH->bgDay;   n++; }
        if (n1 > n0)     { segY[n] = n0; segH[n] = n1 - n0;    segC[n] = TH->bgNight; n++; }
        if (n1 < MAP_H)  { segY[n] = n1; segH[n] = MAP_H - n1; segC[n] = TH->bgDay;   n++; }
        for (int i = 0; i < n; i++)
            canvas.drawFastVLine(MAP_X + px, MAP_Y + segY[i], segH[i], segC[i]);
    }

    // --- The coastline. Whole BYTES are skipped when empty, so the 51 200
    // pixels cost 6 400 reads and about 3 200 plots — the shoreline is 6 % of
    // the map. ONE drawPixel call site.
    for (int py = 0; py < MAP_H; py++) {
        for (int byte = 0; byte < spa::WORLD_ROW_BYTES; byte++) {
            uint8_t bits = pgm_read_byte(&spa::WORLD_MASK[py * spa::WORLD_ROW_BYTES + byte]);
            if (!bits) continue;
            for (int b = 0; b < 8; b++) {
                if (!(bits & (0x80 >> b))) continue;
                const int px = byte * 8 + b;
                if (px >= MAP_W) break;
                const bool night = py >= sNightY0[px] && py < sNightY1[px];
                canvas.drawPixel(MAP_X + px, MAP_Y + py,
                                 night ? TH->coastNight : TH->coastDay);
            }
        }
    }

    // --- Graticule, kept DELIBERATELY faint now that there are coastlines:
    // the equator and the prime meridian are the two lines that still say
    // something a shoreline does not.
    for (int x = MAP_X; x < MAP_X + MAP_W; x += 10)
        canvas.drawFastHLine(x, mapLatY(0), 3, TH->sep);
    for (int y = MAP_Y; y < MAP_B; y += 10)
        canvas.drawFastVLine(mapLonX(0), y, 3, TH->sep);

    // --- The observer.
    const int ox = mapLonX(cfg.lon), oy = mapLatY(cfg.lat);
    canvas.drawFastHLine(ox - 3, oy, 7, TH->accent2);
    canvas.drawFastVLine(ox, oy - 3, 7, TH->accent2);

    if (!gTleOk) { drawIssNotice(); return; }

    // --- Ground track: a CONTINUOUS curve, one orbit's worth. Two details
    // make a polyline work here where dots did not:
    //  · the DATE LINE. Consecutive samples straddling +/-180 are adjacent on
    //    the globe and at opposite edges of the screen; joining them would
    //    draw a bar across the whole map. A jump wider than half the map is a
    //    wrap, and the segment is skipped.
    //  · PAST versus FUTURE. Same path, two weights: where it has BEEN is
    //    faint, where it is GOING is the accent colour, so the direction of
    //    travel reads at a glance.
    const bool stale = tleStale(jd);
    const uint16_t pastCol = TH->sep;
    const uint16_t nextCol = stale ? TH->hint : TH->accent2;
    if (sTrkJd == 0.0 || fabs(jd - sTrkJd) > 20.0 / 86400.0) {
        for (int i = 0; i < TRK_N; i++) {
            const double t = jd + (i - 50) / 1440.0;
            const StateVector sv = gProp.propagateJd(t);
            sTrkOk[i] = sv.ok;
            if (!sv.ok) continue;
            const Geodetic g = eciToGeodetic(sv.x, sv.y, sv.z, t);
            sTrkX[i] = (int16_t)mapLonX(g.lonDeg);
            sTrkY[i] = (int16_t)mapLatY(g.latDeg);
        }
        sTrkJd = jd;
    }
    int prevI = -1;
    for (int i = 0; i < TRK_N; i++) {
        if (!sTrkOk[i]) { prevI = -1; continue; }
        if (prevI >= 0 && abs(sTrkX[i] - sTrkX[prevI]) < MAP_W / 2)
            canvas.drawLine(sTrkX[prevI], sTrkY[prevI], sTrkX[i], sTrkY[i],
                            i <= 50 ? pastCol : nextCol);
        prevI = i;
    }

    // --- The station.
    const StateVector now = gProp.propagateJd(jd);
    if (!now.ok) return;
    const Geodetic g = eciToGeodetic(now.x, now.y, now.z, jd);
    const bool lit = isSunlit(now.x, now.y, now.z, jd);
    const int sx = mapLonX(g.lonDeg), sy = mapLatY(g.latDeg);
    const uint16_t satCol = stale ? TH->hint : (lit ? TH->accent : TH->accent2);
    canvas.fillRect(sx - 2, sy - 2, 5, 5, satCol);
    canvas.drawRect(sx - 4, sy - 4, 9, 9, satCol);

    // --- The figures, under the map. ONE DATUM PER ROW (the radar's
    // trip-zone lesson), through ONE drawString call site.
    gCells.reset();
    auto put = [&](int x, int y, uint16_t col, const char* t) { gCells.put(x, y, col, t); };
    char buf[40];
    canvas.setTextSize(1);
    // THE NAME IS FITTED, THE COORDINATES ARE NOT NEGOTIABLE. `gTleName` holds
    // up to 24 characters (the TLE line-0 field), and 24 + "  -90.00  -180.000"
    // is 42 into a 39-character buffer: snprintf would cut the LONGITUDE, in
    // silence, leaving "-180.0" reading as a different place. Fitting the name
    // first puts the loss where it is visible and where it costs least — a
    // satellite name with a mark on it is still the satellite you chose.
    char nm[26];
    sce::fitGlyphs(nm, sizeof(nm), gTleName, 20);
    snprintf(buf, sizeof(buf), "%s  %+.2f  %+.3f", nm, g.latDeg, g.lonDeg);
    put(4, 184, TH->txtMain, buf);
    const double vel = sqrt(now.vx * now.vx + now.vy * now.vy + now.vz * now.vz);
    snprintf(buf, sizeof(buf), "%.0f %s   %.2f %s   %s",
             dKm(g.altKm), uKm(), dKms(vel), uKms(),
             lit ? sce::T("sunlit", "au soleil") : sce::T("eclipse", "eclipse"));
    put(4, 198, TH->txt1, buf);

    const int up = firstUpcomingPass(jd);
    if (up >= 0) {
        char hhmm[24];
        jdToLocalDayHhmm(gPassList[up].riseJd, hhmm, sizeof(hhmm));
        const double inH = (gPassList[up].riseJd - jd) * 24.0;
        snprintf(buf, sizeof(buf), "%s %s (%s %dh%02d)",
                 sce::T("next pass", "prochain passage"), hhmm,
                 sce::T("in", "dans"), (int)inH, (int)((inH - (int)inH) * 60));
        put(4, 212, TH->txt2, buf);
    } else if (gPassSearching) {
        snprintf(buf, sizeof(buf), "%s %d%%",
                 sce::T("searching passes", "recherche des passages"),
                 (int)(gPasses.progress() * 100));
        put(4, 212, TH->txt2, buf);
    }
    gCells.flush(canvas);            // THE one call site (A2.22)

    if (stale) {
        snprintf(buf, sizeof(buf), "%s %.0f j",
                 sce::T("STALE element set", "jeu d'elements PERIME"),
                 jd - gTle.epochJd);
        drawFooter(buf, TH->alert);
    }
}

// =============================================================================
// View 2 — PASSES
// =============================================================================
// The view's three EMPTY/INTERIM states, in their own function. Not a stylistic
// split: leaving them in drawPassesView put three status messages and the
// table's cell loop in ONE body, which is four similar drawString calls — the
// exact shape A2.22 exists for, and the binary came back with three. One body,
// one call site, on both sides of the split.
static void drawPassesNotice(const char* msg, uint16_t col,
                             bool showBar, float pct) {
    canvas.setTextSize(1);
    canvas.setTextColor(col);
    canvas.drawString(msg, 8, 34);
    if (showBar) {
        canvas.drawRect(8, 50, 300, 10, TH->sep);
        canvas.fillRect(8, 50, (int)(300 * pct), 10, TH->accent);
    }
}

static __attribute__((noinline)) void drawPassesView(double jd) {
    canvas.setTextSize(1);
    if (!gTleOk) {
        drawPassesNotice(sce::T("no element set", "pas de jeu d'elements"),
                         TH->hint, false, 0);
        return;
    }
    if (gPassSearching) {
        drawPassesNotice(sce::T("searching...", "recherche..."),
                         TH->txt1, true, (float)gPasses.progress());
        return;
    }
    if (gPassN == 0) {
        drawPassesNotice(sce::T("no pass above the threshold in 48 h",
                                "aucun passage au-dessus du seuil en 48 h"),
                         TH->hint, false, 0);
        return;
    }
    // ---- THE TABLE ---------------------------------------------------------
    // Header and values share ONE set of column origins (user 08-04: the
    // header did not line up). The first version spaced the header with blanks
    // inside a single string and placed the values with two independent x
    // coordinates, so the two were only ever aligned by luck — and Font0's
    // 6 px cell makes every miscount visible. Now a column is a NUMBER used
    // by both, and nothing can drift.
    //
    // Every cell also goes through ONE drawString call site (A2.22): a table
    // written as one call per field is precisely the shape GCC 8.4 Xtensa is
    // entitled to thin out, and a silently missing column in a timetable is
    // unfalsifiable by eye.
    static constexpr int COL_WHEN = 8;     // "04/08 21:47"  11 ch -> ends 74
    static constexpr int COL_RISE = 92;    // "21:47"         5 ch -> ends 122
    static constexpr int COL_AZ   = 134;   // "NNE"           3 ch -> ends 152
    static constexpr int COL_MAX  = 172;   // "42"            2 ch -> ends 184
    static constexpr int COL_SET  = 206;   // "22:01"         5 ch -> ends 236

    // 7 cells per pass row (when, rise, az, max, set, chip, duration) plus the
    // SIX header cells: 6 x 7 + 6 = 48, against the table's 56. This line said
    // five headers and it was the miscount CellText.h records — the table it
    // sized was 47, so the last cell of the sixth row had been dropped all
    // along. State the arithmetic, not a size that happens to hold.
    // A full table no longer loses a column in silence (`CellText::overflowed`,
    // and the flush says so once on the console); the count is stated anyway,
    // because being TOLD a timetable lost its last row is a poor second to it
    // fitting.
    gCells.reset();
    auto put = [&](int x, int y, uint16_t col, const char* t) { gCells.put(x, y, col, t); };

    const int HDR_Y = 26;
    put(COL_WHEN, HDR_Y, TH->txt2, sce::T("WHEN", "QUAND"));
    put(COL_RISE, HDR_Y, TH->txt2, sce::T("RISE", "LEVER"));
    put(COL_AZ,   HDR_Y, TH->txt2, "AZ");
    put(COL_MAX,  HDR_Y, TH->txt2, sce::T("MAX", "MAX"));
    put(COL_SET,  HDR_Y, TH->txt2, sce::T("SET", "COUCH"));
    put(248,      HDR_Y, TH->txt2, sce::T("PROFILE", "PROFIL"));

    for (int i = 0; i < gPassN && i < 6; i++) {
        const Pass& p = gPassList[i];
        const int y = 42 + i * 30;
        if (i == gCursor) canvas.fillRect(2, y - 3, 316, 27, TH->panelBg);
        const uint16_t main = (i == gCursor) ? TH->txtMain : TH->txt1;
        char b[24];
        jdToLocalDayHhmm(p.riseJd, b, sizeof(b));  put(COL_WHEN, y, main, b);
        jdToLocalHhmm(p.riseJd, b, sizeof(b));     put(COL_RISE, y, main, b);
        put(COL_AZ, y, main, compass16(p.riseAz));
        snprintf(b, sizeof(b), "%.0f", p.maxEl);   put(COL_MAX, y, main, b);
        jdToLocalHhmm(p.setJd, b, sizeof(b));      put(COL_SET, y, main, b);
        // Second line of the row: the chip that is the point of the whole
        // view — a pass you can SEE is a different event from one only a
        // radio hears — then the duration and where it goes.
        // Both words are spelled the same in French; they go through T()
        // anyway, so "was this translated?" has an answer in the source
        // instead of needing the question asked again next time.
        put(COL_WHEN, y + 12, p.visible ? TH->accent : TH->hint,
            p.visible ? sce::T("VISIBLE", "VISIBLE") : sce::T("radio", "radio"));
        snprintf(b, sizeof(b), "%.0f min  %s %s", (p.setJd - p.riseJd) * 1440.0,
                 sce::T("to", "vers"), compass16(p.setAz));
        put(COL_RISE, y + 12, TH->txt2, b);
    }

    gCells.flush(canvas);            // THE one call site (A2.22)
    canvas.drawFastHLine(4, 36, 312, TH->sep);

    // ---- THE PASS PROFILE, in the seventy pixels the table left empty.
    // The radar's lesson, applied: let a GRAPHIC carry the number. A column
    // reading "42" tells you the maximum elevation; an arc drawn to 42/90 of
    // the dome's height tells you it at a glance AND compares it with the row
    // above without reading either. The arc's colour repeats the VISIBLE/radio
    // verdict, so the two facts that decide whether you go outside — how high,
    // and whether you could see it — are one shape.
    //
    // Sampled as a parabola through (rise, horizon), (max, peak), (set,
    // horizon): the real curve is a great circle seen in elevation, and at
    // 60 px wide the difference is under a pixel. ONE drawPixel call site.
    static constexpr int PROF_X = 248, PROF_W = 64, PROF_H = 20;
    for (int i = 0; i < gPassN && i < 6; i++) {
        const Pass& p = gPassList[i];
        const int base = 42 + i * 30 + PROF_H;
        const int peak = (int)(PROF_H * (p.maxEl / 90.0));
        const uint16_t c = p.visible ? TH->accent : TH->hint;
        canvas.drawFastHLine(PROF_X, base + 1, PROF_W, TH->sep);   // the horizon
        for (int k = 0; k <= PROF_W; k++) {
            const double u = (double)k / PROF_W;          // 0..1 across the pass
            const int y = base - (int)(4.0 * peak * u * (1.0 - u));
            canvas.drawPixel(PROF_X + k, y, c);
        }
    }
    drawFooter(sce::T("tap a row for its sky chart",
                      "touchez une ligne pour la carte du ciel"), TH->hint);
}

// ---- the polar sky chart (modal) -------------------------------------------
// North UP, East RIGHT: this is the sky seen by someone LOOKING UP, which is
// the mirror of a ground map and the single most common way to get a polar
// chart wrong.
static __attribute__((noinline)) void drawPolarModal(double jd) {
    canvas.fillScreen(TH->bg);
    const int cx = 160, cy = 128, R = 92;
    canvas.drawCircle(cx, cy, R, TH->sep);
    canvas.drawCircle(cx, cy, R * 2 / 3, TH->sep);   // 30 deg
    canvas.drawCircle(cx, cy, R / 3, TH->sep);       // 60 deg
    canvas.setTextSize(1);
    canvas.setTextColor(TH->txt2);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.drawString("N", cx, cy - R - 8);
    canvas.drawString("S", cx, cy + R + 8);
    canvas.drawString("E", cx + R + 8, cy);
    canvas.drawString("W", cx - R - 8, cy);

    if (gCursor < gPassN && gTleOk) {
        const Pass& p = gPassList[gCursor];
        // Sample the pass and plot it. az/el → screen: r scales with the
        // zenith distance, and EAST is +x.
        auto plot = [&](double t, int size, uint16_t col) {
            const StateVector sv = gProp.propagateJd(t);
            if (!sv.ok) return;
            const LookAngle la = lookAngle(sv.x, sv.y, sv.z, t,
                                           cfg.lat, cfg.lon, cfg.altKm);
            if (la.elDeg < 0) return;
            const double rr = R * (90.0 - la.elDeg) / 90.0;
            const int x = cx + (int)(rr * sin(la.azDeg * DEG2RAD_D));
            const int y = cy - (int)(rr * cos(la.azDeg * DEG2RAD_D));
            canvas.fillRect(x - size / 2, y - size / 2, size, size, col);
        };
        const double step = (p.setJd - p.riseJd) / 60.0;
        for (int i = 0; i <= 60; i++) plot(p.riseJd + i * step, 2, TH->sep);
        plot(p.riseJd, 5, TH->accent2);
        plot(p.maxJd,  6, TH->accent);
        plot(p.setJd,  5, TH->accent2);

        char buf[64], rise[8], set[8];
        jdToLocalHhmm(p.riseJd, rise, sizeof(rise));
        jdToLocalHhmm(p.setJd,  set,  sizeof(set));
        canvas.setTextDatum(textdatum_t::top_left);
        canvas.setTextColor(TH->txtMain);
        snprintf(buf, sizeof(buf), "%s %s -> %s  max %.0f deg",
                 p.visible ? "VISIBLE" : "radio", rise, set, p.maxEl);
        canvas.drawString(buf, 6, 6);
        canvas.setTextColor(TH->txt2);
        snprintf(buf, sizeof(buf), "%s %s   %s %s",
                 sce::T("from", "depuis"), compass16(p.riseAz),
                 sce::T("to", "vers"), compass16(p.setAz));
        canvas.drawString(buf, 6, 220);
    }
    canvas.setTextDatum(textdatum_t::top_left);
}

// =============================================================================
// View 3 — MOON
// =============================================================================
static const char* moonPhaseName(MoonPhase p) {
    switch (p) {
        case MoonPhase::New:            return sce::T("New moon",        "Nouvelle lune");
        case MoonPhase::WaxingCrescent: return sce::T("Waxing crescent", "Premier croissant");
        case MoonPhase::FirstQuarter:   return sce::T("First quarter",   "Premier quartier");
        case MoonPhase::WaxingGibbous:  return sce::T("Waxing gibbous",  "Gibbeuse croissante");
        case MoonPhase::Full:           return sce::T("Full moon",       "Pleine lune");
        case MoonPhase::WaningGibbous:  return sce::T("Waning gibbous",  "Gibbeuse decroissante");
        case MoonPhase::LastQuarter:    return sce::T("Last quarter",    "Dernier quartier");
        default:                        return sce::T("Waning crescent", "Dernier croissant");
    }
}

// ---- one moon, any size -----------------------------------------------------
// Used by the phase strip's eight thumbnails. ONE scanline loop draws BOTH
// halves: the first version filled a circle then painted the shadow over it,
// and left a bright one-pixel rim on the dark limb that read as an eclipse
// ring (user 08-04). fillCircle rasterises its edge its own way, while the
// shadow span truncated its sqrt toward zero, so the two disagreed by up to a
// pixel. Deriving the lit AND dark spans from the SAME half-width makes a rim
// arithmetically impossible rather than merely unlikely.
//
// Geometry: the terminator is the ellipse x = s*c, with s = 1 - 2k for a
// waxing moon (k = lit fraction) and its opposite when waning. That single
// signed number covers crescent (s > 0) and gibbous (s < 0) alike — which is
// what makes a gibbous moon look gibbous instead of bitten.
//
// `waxing` here is really "LIT ON THE RIGHT", which is not the same question:
// south of the equator the same waxing Moon is seen lit on the left. The
// conversion happens at the two call sites through `moonLitOnRight`, so the
// observer's latitude is applied once per drawing and cannot be forgotten by
// only one of them.
static __attribute__((noinline)) void drawMoonDisc(int cx, int cy, int R, double k, bool litRight,
                         uint16_t lit, uint16_t dark) {
    const bool waxing = litRight;      // the geometry below is written for it
    const double s = waxing ? (1.0 - 2.0 * k) : (2.0 * k - 1.0);
    for (int dy = -R; dy <= R; dy++) {
        // ROUNDED, not truncated: the limb is then symmetric top to bottom.
        const int c = (int)(sqrt((double)(R * R - dy * dy)) + 0.5);
        if (c <= 0) continue;
        int xt = (int)lround(s * c);
        if (xt < -c) xt = -c;
        if (xt >  c) xt =  c;
        int      segX[2], segW[2];
        uint16_t segC[2];
        int n = 0;
        if (xt > -c) { segX[n] = cx - c;  segW[n] = xt + c;
                       segC[n] = waxing ? dark : lit; n++; }
        if (c > xt)  { segX[n] = cx + xt; segW[n] = c - xt;
                       segC[n] = waxing ? lit : dark; n++; }
        for (int i = 0; i < n; i++)                // ONE call site (A2.22)
            canvas.drawFastHLine(segX[i], cy + dy, segW[i], segC[i]);
    }
}

// A body in the DIAGRAM, lit on the side that faces the Sun. This is the
// difference between a schematic and a picture: on a top-down figure the Moon
// does not show us a phase, it shows a hemisphere pointed at the Sun, and
// seeing that is the whole explanation.
//
// The split is the line perpendicular to the Sun direction through the centre,
// so it TILTS as the Moon goes round — drawing it as a fixed vertical split
// would be right only at the quarters.
static __attribute__((noinline)) void drawLitBody(int cx, int cy, int R, double ux, double uy,
                        uint16_t lit, uint16_t dark) {
    for (int dy = -R; dy <= R; dy++) {
        const int c = (int)(sqrt((double)(R * R - dy * dy)) + 0.5);
        if (c <= 0) continue;
        // Lit where (p - centre) . u > 0. Solved for x at this row; when the
        // Sun is almost straight above or below, ux vanishes and the whole
        // row is on one side.
        int xt;
        if (fabs(ux) < 1e-3) xt = (dy * uy > 0) ? -c : c;
        else                 xt = (int)lround(-(dy * uy) / ux);
        if (xt < -c) xt = -c;
        if (xt >  c) xt =  c;
        int      segX[2], segW[2];
        uint16_t segC[2];
        int n = 0;
        // ux > 0 means the Sun is to the RIGHT, so the right span is lit.
        const bool rightLit = ux > 0;
        if (xt > -c) { segX[n] = cx - c;  segW[n] = xt + c;
                       segC[n] = rightLit ? dark : lit; n++; }
        if (c > xt)  { segX[n] = cx + xt; segW[n] = c - xt;
                       segC[n] = rightLit ? lit : dark; n++; }
        for (int i = 0; i < n; i++)                // ONE call site (A2.22)
            canvas.drawFastHLine(segX[i], cy + dy, segW[i], segC[i]);
    }
}

// ---- the Sun-Earth-Moon diagram, to the user's sketch (08-04) ---------------
// Half a Sun flush against the LEFT EDGE, vertically centred; an arc of
// Earth's orbit bulging right; Earth as a disc on that arc, navy on its night
// side and blue on its day side; the Moon on its own small orbit at its true
// elongation, lit on the side facing the Sun. Text down the right, phases
// along the bottom.
//
// WHY THE HALF SUN: a whole one would need a margin, and the margin would say
// the Sun is a nearby object of that size. Running it off the edge says the
// opposite — that it continues beyond the frame — which is the one true thing
// a diagram at this scale can say about it. Nothing here is to scale and
// nothing needs to be: the ANGLE is the only claim, and the angle is exact.
//
// No sun rays. They were decoration in the first version and they crowded the
// only region where the Moon's orbit and the Earth's arc have to stay legible.
// Blood-moon copper for an umbral eclipse — theme-independent on purpose:
// this is the physics (sunlight refracted through Earth's atmosphere), not
// the chrome, and it must read as "not the normal Moon" under every theme.
static constexpr uint16_t ECL_COPPER = 0xB1E4;   // ~RGB 180,60,32
static constexpr uint16_t ECL_DARK   = 0x30A1;   // umbral shadow side
static constexpr int SUN_X = 0, SUN_Y = 100, SUN_R = 36;
static constexpr int EORB_R = 118;                 // Earth's orbit, about the Sun
static constexpr int EARTH_X = SUN_X + EORB_R, EARTH_Y = SUN_Y;
// SIZES ARE A LEGIBILITY DECISION, not a scale (user 08-04). Earth is drawn
// clearly larger than the Moon so the two are never confused at a glance, and
// the lunar orbit is wide enough that the Moon never touches Earth's limb —
// at true scale the radii would differ by 3.7x and the orbit would be sixty
// Earth-radii across, which no 190 px zone can hold. What the figure claims
// is the ANGLE, and the angle is exact.
static constexpr int EARTH_R = 20;
static constexpr int MORB_R  = 52;                 // the Moon's orbit, about Earth
static constexpr int MOON_R  = 11;
// 190, and the separator keeps its OWN offset below: the longest phase name
// ("Gibbeuse decroissante", 21 glyphs = 126 px) has always overflowed the
// panel by 2 px from 196. The Moon's rightmost point is x=181 (Earth 118 +
// orbit 52 + disc 11), the rule sits at 184, so the text can start at 190 —
// six pixels of air on each side of the rule, and the whole vocabulary fits.
static constexpr int TXT_L = 190;                  // the text column

static void drawSunEarthMoon(double elongDeg, uint8_t eclipse) {
    // The Sun: a full circle whose left half falls off the canvas. Letting the
    // clip do the cutting is exact and free; drawing a semicircle by hand
    // would need its own flat edge and would show a seam.
    canvas.fillCircle(SUN_X, SUN_Y, SUN_R, 0xFE06);
    canvas.drawCircle(SUN_X, SUN_Y, SUN_R + 4, 0x8200);

    // Earth's orbit: plotted point by point rather than as a circle, because a
    // circle of this radius would run out of the diagram and into the phase
    // strip below. Only the arc that belongs in the zone is drawn — ONE
    // drawPixel call site.
    for (int a = -42; a <= 42; a++) {
        const int x = SUN_X + (int)(EORB_R * cosD(a));
        const int y = SUN_Y - (int)(EORB_R * sinD(a));
        canvas.drawPixel(x, y, TH->sep);
    }

    // Earth on the arc. The vector towards the Sun is (-1, 0) from here, so
    // the DAY side is the left half and the night side the right — which is
    // also why the Moon is "new" when it sits between the two.
    drawLitBody(EARTH_X, EARTH_Y, EARTH_R, -1.0, 0.0, 0x4CFC, 0x1108);
    canvas.drawCircle(EARTH_X, EARTH_Y, EARTH_R, TH->sep);

    // The Moon's orbit, and the Moon on it.
    canvas.drawCircle(EARTH_X, EARTH_Y, MORB_R, TH->sep);
    // Elongation 0 = new = between us and the Sun, so on the LEFT of Earth;
    // 180 = full = behind us, on the right. Hence the sign on the cosine.
    const int mx = EARTH_X - (int)(MORB_R * cosD(elongDeg));
    const int my = EARTH_Y - (int)(MORB_R * sinD(elongDeg));
    // Unit vector from the Moon TOWARDS the Sun. It genuinely tilts around the
    // orbit, and drawing a fixed vertical split instead would be right only at
    // the quarters.
    //
    // HALF LIT, exactly like the Earth beside it. An earlier version drew the
    // phase as seen from Earth here (a 67 % gibbous disc on the orbit), so the
    // figure matched the % beside it — but seen from above, the Moon is always
    // half lit, the hemisphere facing the Sun, and a diagram whose one claim
    // is the ANGLE cannot take liberties with the lighting that angle causes
    // (user 08-15). The appearance keeps its two homes: the phase strip below
    // and the big percentage.
    double ux = SUN_X - mx, uy = SUN_Y - my;
    const double un = sqrt(ux * ux + uy * uy);
    ux /= un; uy /= un;
    // During a REAL umbral eclipse the diagram is finally allowed to darken
    // the Moon behind Earth — moonInfo tested the ecliptic latitude, so this
    // fires two or three nights a year, not at every full moon. Total: the
    // whole disc goes copper. Partial: the geometry stays, the ring says it.
    // SAME call sites either way (A2.22): only the colours change hands.
    const uint16_t litC  = (eclipse == 2) ? ECL_COPPER : TH->txtMain;
    const uint16_t darkC = (eclipse == 2) ? ECL_DARK   : TH->panelBg;
    drawLitBody(mx, my, MOON_R, ux, uy, litC, darkC);
    canvas.drawCircle(mx, my, MOON_R, TH->sep);
    canvas.drawCircle(mx, my, MOON_R + 4,
                      eclipse ? ECL_COPPER : TH->accent);  // the one you watch
    // NO rays, and no Earth-to-Moon line either (user 08-04). Both were
    // drawing LIGHT, and the lighting is already stated where it belongs — on
    // the bodies themselves, each lit on the side facing the Sun. A line
    // saying the same thing a second time only crowds the one region where
    // the two orbits have to stay legible.
}

static __attribute__((noinline)) void drawMoonView(double jd) {
    const MoonInfo m = moonInfo(jd);
    canvas.setTextSize(1);

    drawSunEarthMoon(m.elongDeg, m.eclipse);
    canvas.drawFastVLine(TXT_L - 6, 24, 162, TH->sep);

    // ---- the figures, down the right. COLLECTED FIRST, MEASURED, THEN DRAWN
    // (user 08-04: "centre the whole text block to balance it"). Building the
    // rows before placing any of them is what lets the block know its own
    // height: a Moon that does not rise today is one row fewer, and a layout
    // with a fixed origin cannot know that — it just hangs off-centre.
    //
    // Typography: a heading (the phase), then the ONE number anyone came for
    // at double size, then label/value pairs on a single tab stop. Labels in
    // the quiet colour, values in the bright one, so the eye runs down the
    // values and only reads a label when it needs to.
    struct Row { char label[8]; char value[14]; uint16_t col; };
    Row rows[5];
    int nRows = 0;
    auto add = [&](const char* l, const char* v, uint16_t c) {
        if (nRows >= (int)(sizeof(rows) / sizeof(rows[0]))) return;
        strlcpy(rows[nRows].label, l, sizeof(rows[0].label));
        strlcpy(rows[nRows].value, v, sizeof(rows[0].value));
        rows[nRows].col = c; nRows++;
    };
    // 28 and not 24: the countdown's longest string is
    // "nouvelle lune cette nuit" - 24 characters, which a 24-byte buffer
    // TRUNCATES to lose its final letter. snprintf cuts in silence, and a
    // French speaker reading "cette nui" files it as a typo, not a bug.
    char buf[28];
    // The eclipse leads the column when there is one: it is the one fact
    // that outranks everything else on this page for the next few hours.
    if (m.eclipse)
        add(sce::T("eclipse", "eclipse"),
            m.eclipse == 2 ? sce::T("total", "totale")
                           : sce::T("partial", "partielle"), ECL_COPPER);
    snprintf(buf, sizeof(buf), "%.1f j", m.ageDays);
    add(sce::T("age",  "age"),   buf, TH->txt1);
    // Thousands separated by a thin space: "384 400 km" is read at a glance,
    // "384400 km" has to be counted.
    {
        const long km = lround(dKm(m.distKm));
        snprintf(buf, sizeof(buf), "%ld %03ld %s", km / 1000, km % 1000, uKm());
        add(sce::T("dist", "dist"), buf, TH->txt1);
    }

    auto moonAlt = [&](double tt) {
        const MoonInfo mm = moonInfo(tt);
        return altAzFromRaDec(mm.raDeg, mm.decDeg, tt, cfg.lat, cfg.lon).elDeg;
    };
    // ONCE A DAY, not once a frame. `findRiseSet` evaluates moonInfo about 75
    // times for the scan plus 28 per bisected crossing — some 130 full lunar
    // series in software double, twice a second, for two times that change by
    // under an hour a day (review 08-04). The key is the LOCAL day, so the
    // answer changes exactly when the row it feeds should.
    static RiseSet sRs;
    static long    sRsDay = -1;
    const long today = (long)floor(jd + cfg.tzOffsetH / 24.0);
    if (today != sRsDay) { sRs = findRiseSet(moonAlt, jd, 25.0, -0.833);
                           sRsDay = today; }
    const RiseSet& rs = sRs;
    if (rs.hasRise || rs.hasSet) {
        char rise[8] = "--:--", set[8] = "--:--";
        if (rs.hasRise) jdToLocalHhmm(rs.riseJd, rise, sizeof(rise));
        if (rs.hasSet)  jdToLocalHhmm(rs.setJd,  set,  sizeof(set));
        snprintf(buf, sizeof(buf), "%s-%s", rise, set);
        add(sce::T("up", "leve"), buf, TH->accent2);
    }
    const LookAngle la = altAzFromRaDec(m.raDeg, m.decDeg, jd, cfg.lat, cfg.lon);
    if (la.elDeg > 0) {
        snprintf(buf, sizeof(buf), "%s %+.0f %s", compass16(la.azDeg), la.elDeg,
                 sce::T("deg", "deg"));
        add(sce::T("now", "ici"), buf, TH->accent);
    } else {
        add(sce::T("now", "ici"), sce::T("below", "couchee"), TH->hint);
    }

    // ---- the column, measured then placed. The area is 124 x 162 (x 196..320,
    // y 24..186) and the old block used two thirds of it; the rest is spent on
    // AIR and on the one fact the page never answered — see below.
    //
    //   phase name        the heading, quiet
    //   illumination %    THE number, triple size now that the room exists
    //   gradient bar      the companion's cyan->indigo identity, 3 px
    //   next milestone    "full moon ~ 3 d" — what a moon page is ASKED
    //   facts             age / dist / up-window / position, 17 px pitch
    //
    // The countdown is computed from the elongation at the mean synodic rate
    // (moonDaysToElong): whole days, so the mean-vs-true error is invisible,
    // and no extra series evaluation. Waxing runs to full, waning to new —
    // whichever milestone is actually NEXT.
    constexpr int H_HEAD = 12, H_BIG = 28, H_BAR = 11, H_NEXT = 24, H_ROW = 17;
    const int total = H_HEAD + 4 + H_BIG + H_BAR + H_NEXT + nRows * H_ROW;
    int y = (24 + 186) / 2 - total / 2;             // centred in the COLUMN,
    if (y < 24) y = 24;                             // not on Earth's row: the
                                                    // column is what it fills
    gCells.reset();
    auto put = [&](int x, int yy, uint8_t size, uint16_t col, const char* s) {
        gCells.put(x, yy, col, s, size);
    };
    put(TXT_L, y, 1, TH->txt2, moonPhaseName(moonPhaseOf(m.elongDeg)));
    y += H_HEAD + 4;
    snprintf(buf, sizeof(buf), "%.0f%%", m.illum * 100.0);
    put(TXT_L, y, 3, TH->txtMain, buf);
    y += H_BIG;
    // The identity bar, cyan into indigo — the same two-segment ramp the
    // launcher, the lobby and the flash screen wear. ONE fillRect call site
    // (A2.22, pinned) — and the asm is what MAKES it one. A two-iteration
    // loop with constant bounds is fully unrolled by GCC 8.4 back into the
    // two similar calls the rule exists to forbid (`#pragma GCC unroll 1`
    // did not stop it — verified in the binary, check-a222 counted 2). The
    // empty asm makes the bound opaque, so the loop cannot be peeled; it
    // emits no instruction.
    int nSeg = 2;
    asm volatile("" : "+r"(nSeg));
    for (int sgm = 0; sgm < nSeg; sgm++)
        canvas.fillRect(TXT_L + sgm * 40, y, 40, 3,
                        sgm ? TH->accent2 : TH->accent);
    y += H_BAR;
    {
        // <=, not <: at exactly 180 the Moon IS full, and a strict test
        // would announce the next NEW moon, a fortnight out, at the very
        // instant of the event. Unreachable for a sampled double in
        // practice; reachable for anything that plugs 180.0 in directly.
        const bool toFull = (m.elongDeg <= 180.0);
        const double dd = moonDaysToElong(m.elongDeg, toFull ? 180.0 : 0.0);
        const char* what = toFull ? sce::T("full moon", "pleine lune")
                                  : sce::T("new moon", "nouvelle lune");
        // "ce soir", not "cette nuit": with the phase word in front the
        // night form runs to 24 glyphs — 144 px on a 130 px column, off the
        // panel edge. 21 glyphs is the column's exact capacity.
        if (dd < 0.75) snprintf(buf, sizeof(buf), "%s %s", what,
                                sce::T("tonight", "ce soir"));
        else           snprintf(buf, sizeof(buf), "%s ~ %.0f %s", what, dd,
                                sce::T("d", "j"));
        put(TXT_L, y, 1, TH->accent, buf);
    }
    y += H_NEXT;
    for (int i = 0; i < nRows; i++) {
        put(TXT_L,      y, 1, TH->txt2,     rows[i].label);
        put(TXT_L + 40, y, 1, rows[i].col,  rows[i].value);
        y += H_ROW;
    }
    gCells.flush(canvas);            // THE one call site (A2.22)

    // ---- the phase strip, under everything: where tonight sits in the cycle.
    // Eight thumbnails at 45 degrees of elongation each, the current one
    // ringed, drawn by the SAME routine as the Moon in the diagram. A name
    // tells you what tonight is called; the strip tells you what comes next.
    canvas.drawFastHLine(0, 194, 320, TH->sep);
    const int stripY = 216, r = 11;
    for (int i = 0; i < 8; i++) {
        const double e = i * 45.0;
        const int sx = 26 + i * 38;
        // Illumination at that elongation, from the same relation the real
        // phase uses rather than tabulated.
        drawMoonDisc(sx, stripY, r, (1.0 - cosD(e)) * 0.5,
                     moonLitOnRight(e < 180.0, cfg.lat), TH->txt1, TH->panelBg);
        canvas.drawCircle(sx, stripY, r, TH->sep);
    }
    // The marker LAST, or a later thumbnail would paint over it.
    {
        const int cur = (int)(wrap360(m.elongDeg) / 45.0 + 0.5) % 8;
        canvas.drawCircle(26 + cur * 38, stripY, r + 3, TH->accent);
    }
}

// =============================================================================
// View 4 — SKY, and its orbital modal
// =============================================================================
// THE ASTRONOMICAL SYMBOLS ARE PIXEL ART, and they have to be: Font0 is pure
// ASCII, and the one Unicode face in this binary (efontJA_12) is a Japanese
// font with no guarantee of carrying U+263F..U+2646. A missing glyph renders
// as a blank or a tofu box — which on a symbol-keyed chart means the legend
// silently stops working. Drawn ourselves, they cannot go missing.
//
// 7 x 11 cells, one string per row. Order MUST match `spc::Planet`, with Earth
// appended: the orbital view draws Earth too, because the geometry it exists
// to show — why Mars is bright, why Mercury is never far from the Sun — is
// unreadable without our own position in it.
static const char* const GLYPH[8][11] = {
    { "#     #"," #   # ","  ###  "," #   # "," #   # "," #   # ","  ###  ",
      "   #   "," ##### ","   #   ","       " },                       // Mercury
    { "  ###  "," #   # "," #   # "," #   # ","  ###  ","   #   ","   #   ",
      " ##### ","   #   ","       ","       " },                       // Venus
    { "   ####","     ##","    # #","   #   "," ###   ","#   #  ","#   #  ",
      "#   #  "," ###   ","       ","       " },                       // Mars
    { "##     ","  #    ","  #    "," #     ","#####  ","  #    ","  #    ",
      "  #    ","  #  # ","   ##  ","       " },                       // Jupiter
    { "  #    ","  #    ","#####  ","  #    ","  # ## ","  ##  #","  #   #",
      "      #","     # ","   ##  ","       " },                       // Saturn
    { "#  #  #","#  #  #","#######","#  #  #","   #   ","   #   ","  ###  ",
      " #   # "," #   # ","  ###  ","       " },                       // Uranus
    { "#  #  #","#  #  #","#  #  #"," # # # ","  ###  ","   #   ","   #   ",
      " ##### ","   #   ","   #   ","       " },                       // Neptune
    { "       ","  ###  "," #   # "," # # # "," ##### "," # # # "," #   # ",
      "  ###  ","       ","       ","       " },                       // Earth
};
static constexpr int GLYPH_EARTH = 7;

// Its OWN function: the stamp loop's fillRect would otherwise sit in the same
// body as the orbital view's planet dots — two similar calls, the A2.22 shape.
static void drawGlyph(int idx, int x, int y, int cell, uint16_t col) {
    if (idx < 0 || idx > GLYPH_EARTH) return;
    for (int r = 0; r < 11; r++)
        for (int c = 0; c < 7; c++)
            if (GLYPH[idx][r][c] == '#')
                canvas.fillRect(x + c * cell, y + r * cell, cell, cell, col);
}

// ---- SKY: one view, an orrery ----------------------------------------------
// THE DESIGN DECISION, made after building both (user 08-04 left the call):
// ONE view, not a table with an orbital view behind it.
//
// The table was accurate and told you nothing you did not already have to know
// to read it — "Venus, WSW 248, +37" is an answer to a question you can only
// ask if you already know where Venus is. The orrery answers "where is
// everything, and why": why Venus is only ever a morning or evening object,
// why Mars is bright some years. Keeping both would have put two levels
// behind one subject, and the thing the table did better — SCANNING the lot at
// once — is recovered in twenty pixels by the symbol strip at the bottom
// rather than in a hundred and seventy by a table.
//
// So: the chart explains, the strip scans, the panel details, and everything
// is one tap away on one screen.
//
// THE RADIAL SCALE IS LOGARITHMIC, and that is the other real decision.
// Linear, Neptune's 30 AU would crush Mercury, Venus, Earth and Mars into four
// pixels around the Sun — the exact region the view exists to explain. Log
// spacing gives every orbit a visible ring; the price is that distances can no
// longer be compared by eye, which is why the real ones are printed as figures
// in the panel.
//
// LAYOUT: the reference image is a ROUND screen and ours is not, so the disc
// does not grow to fill 320 px — it takes the square it needs on the left, and
// the freed 110 px on the right become the panel. Stretching the orbits into
// ellipses to "use the space" would have made a picture that lies about
// circles.
// LAYOUT, second cut (user 08-04: "use the space better, and make the orrery
// a bit bigger"). The scan strip used to run along the BOTTOM, full width,
// which cost thirty pixels of height for seven symbols that occupy a hundred.
// Folded into the panel as a 4+3 grid it costs nothing extra — the panel was
// already there — and the chart gets the whole left half: radius 84 to 100,
// which is 42 % more area for the thing the view is about.
// CLEAR OF THE SEPARATOR, at the largest radius that is (user 08-04). The
// arithmetic, since the numbers are the whole point: the panel's rule sits at
// PANEL_L - 8 = 204, and the old cx=104 / r=100 put the outer orbit at exactly
// 204 — the ring ran ALONG the rule and read as touching it. Shifting alone
// would push the disc off the left edge; trimming alone would leave a one
// pixel gap that still reads as a collision. Five pixels left and three
// smaller gives 2..196: eight pixels of air before the rule, two after the
// screen edge, and a chart three pixels shy of the biggest that ever fitted.
static constexpr int ORR_CX = 99, ORR_CY = 124;
static constexpr int ORR_RIN = 16, ORR_ROUT = 97;
static constexpr int PANEL_L = 212;          // left edge of the data panel
static constexpr int GRID_Y = 24;            // the scan grid, inside the panel
static constexpr int GRID_C = 26;            // one cell, 4 across then 3

// Per-planet colours: the eye keys on hue long before it reads a label, and
// these are the conventional ones (rust Mars, cream Venus, tan Jupiter, ice
// blue for the outer two). Earth is deliberately the odd one out — on this
// chart it is "us", not another object.
static const uint16_t PL_COL[8] = {
    0x9CF3,   // Mercury: grey
    0xF6D4,   // Venus:   cream
    0xE308,   // Mars:    rust
    0xDD0C,   // Jupiter: tan
    0xEE91,   // Saturn:  pale gold
    0x7EDC,   // Uranus:  ice cyan
    0x431C,   // Neptune: deep blue
    0x4CFC,   // Earth:   blue-green
};

// Label mode, cycled by TAPPING THE SUN (the reference's own affordance):
// clean → names → symbols → both. UI state, not config: it is a way of
// looking, changed in the moment, not something anyone would want persisted.
static uint8_t gSkyLabels = 2;               // symbols by default

static int orrRing(double aAu) {
    static const double aMin = planetSemiMajorAu(Planet::Mercury);
    static const double aMax = planetSemiMajorAu(Planet::Neptune);
    const double t = log(aAu / aMin) / log(aMax / aMin);
    return ORR_RIN + (int)((ORR_ROUT - ORR_RIN) * (t < 0 ? 0 : (t > 1 ? 1 : t)));
}

// Where a body sits right now. ONE function, used by the drawing AND by the
// tap test — a hit test computed separately from the render is how a chart
// ends up with dots you cannot press. Longitude 0 to the right, increasing
// anticlockwise: the view from ecliptic north, the way every solar system
// diagram since Copernicus is drawn.
static void orrBodyXY(int idx, double jd, int& x, int& y) {
    const bool earth = (idx == (int)Planet::COUNT);
    const Helio h = earth ? earthHelio(jd) : planetHelio((Planet)idx, jd);
    const int rr = orrRing(earth ? 1.0 : planetSemiMajorAu((Planet)idx));
    x = ORR_CX + (int)(rr * cosD(h.lonDeg));
    y = ORR_CY - (int)(rr * sinD(h.lonDeg));
}

// 0 below the horizon, 1 up but the sky is too bright, 2 genuinely observable.
// THREE values on purpose: a two-state chip would call Jupiter "up" at noon.
static uint8_t planetState(Planet p, double jd, bool daylight) {
    const Equatorial q = planetPosition(p, jd);
    if (altAzFromRaDec(q.raDeg, q.decDeg, jd, cfg.lat, cfg.lon).elDeg <= 0)
        return 0;
    return daylight ? 1 : 2;
}

// The starfield. DETERMINISTIC (fixed-seed LCG, recomputed identically every
// frame) so the sky does not shimmer: fresh random stars at 2 Hz would flicker
// and read as a fault. Kept out of the disc's middle, where a stray point
// would be taken for an object.
static void drawStarfield() {
    uint32_t s = 0x5EED1234;
    for (int i = 0; i < 70; i++) {
        s = s * 1664525u + 1013904223u;
        const int x = 2 + (int)((s >> 16) % (PANEL_L - 6));
        s = s * 1664525u + 1013904223u;
        const int y = 22 + (int)((s >> 16) % 204);
        const int dx = x - ORR_CX, dy = y - ORR_CY;
        if (dx * dx + dy * dy < (ORR_ROUT - 6) * (ORR_ROUT - 6)) continue;
        canvas.drawPixel(x, y, ((s >> 8) & 3) ? TH->sep : TH->txt2);
    }
}

// The three-letter chart labels, in their OWN function. Not a stylistic split:
// left inline, this loop and the panel's cell loop were two similar drawString
// calls in one body — the A2.22 shape. One body, one call site, on both sides.
//
// `noinline` IS THE POINT, not a micro-optimisation. Called from one place,
// GCC folded it straight back into drawSkyView and the two loops were in one
// body again — the split existed in the source and not in the binary, which is
// exactly the class of illusion check-a222.py exists to expose (it caught this
// one). The cost is one call per frame at 2 Hz.
static __attribute__((noinline)) void drawSkyNames(double jd) {
    struct Lbl { int16_t x, y; uint16_t col; char t[4]; };
    Lbl ls[8];
    int n = 0;
    for (int i = 0; i <= (int)Planet::COUNT; i++) {
        const bool earth = (i == (int)Planet::COUNT);
        int x, y; orrBodyXY(i, jd, x, y);
        const char* nm = earth ? sce::T("Ear", "Ter") : planetName((Planet)i);
        ls[n].x = (int16_t)(x > ORR_CX ? x - 24 : x + 7);
        ls[n].y = (int16_t)(y - 3);
        ls[n].col = PL_COL[earth ? 7 : i];
        ls[n].t[0] = nm[0]; ls[n].t[1] = nm[1];
        ls[n].t[2] = nm[2]; ls[n].t[3] = '\0';
        n++;
    }
    for (int i = 0; i < n; i++) {                  // ONE call site
        canvas.setTextColor(ls[i].col);
        canvas.drawString(ls[i].t, ls[i].x, ls[i].y);
    }
}

// ---- SKY's secondary screen: WHERE TO LOOK ----------------------------------
// The orrery says where the planets ARE; this says where to point your face
// (user 08-04). Same instrument as the passes' polar chart, and deliberately
// so — one dome convention in the bin, learned once.
//
// NORTH UP, EAST ON THE RIGHT. This is the sky seen by someone LOOKING UP,
// which is the mirror image of a ground map and the single most common way to
// get a sky chart wrong. The centre is the zenith, the rim the horizon, and
// the two inner rings are 30 and 60 degrees of altitude: an object halfway out
// from the centre is halfway up the sky.
static __attribute__((noinline)) void drawSkyDome(double jd) {
    canvas.fillScreen(TH->bg);
    canvas.setTextSize(1);
    const int cx = 128, cy = 122, R = 96;

    // Altitude rings — ONE drawCircle call site (A2.22).
    for (int i = 3; i >= 1; i--)
        canvas.drawCircle(cx, cy, R * i / 3, i == 3 ? TH->txt2 : TH->sep);

    // Cardinal points, and the four that matter most read as words rather
    // than as tick marks: you turn your body by them.
    // 20, not 16: "N haut, E droite" is sixteen characters plus its
    // terminator, so the French orientation legend lost its last letter
    // on every frame (review 08-04).
    gCells.reset();
    auto put = [&](int x, int y, uint16_t col, const char* t) { gCells.put(x, y, col, t); };
    put(cx - 3, cy - R - 12, TH->txt2, "N");
    put(cx - 3, cy + R + 4,  TH->txt2, "S");
    put(cx + R + 6, cy - 4,  TH->txt2, "E");
    put(cx - R - 12, cy - 4, TH->txt2, "W");

    const bool daylight = sunAltDeg(jd, cfg.lat, cfg.lon) > -6.0;

    // Every body above the horizon, at its own az/el. Screen: azimuth is
    // measured from north CLOCKWISE through east, and east must land on the
    // right — hence +sin for x and -cos for y.
    auto place = [&](double az, double el, int& x, int& y) {
        const double rr = R * (90.0 - el) / 90.0;
        x = cx + (int)(rr * sinD(az));
        y = cy - (int)(rr * cosD(az));
    };

    int selX = -1, selY = -1;
    for (int i = 0; i < (int)Planet::COUNT; i++) {
        const Equatorial q = planetPosition((Planet)i, jd);
        const LookAngle la = altAzFromRaDec(q.raDeg, q.decDeg, jd, cfg.lat, cfg.lon);
        if (la.elDeg <= 0) continue;               // below the horizon: absent
        int x, y; place(la.azDeg, la.elDeg, x, y);
        // Same three tints as the scan strip, same meaning: what it would
        // take to see it. A chart that drew Neptune as brightly as Jupiter
        // would send you outside expecting to find it.
        const bool eye = !daylight && planetAid((Planet)i) == 0;
        drawGlyph(i, x - 7, y - 11, 2,
                  daylight ? TH->stripAid : (eye ? TH->stripEye : TH->stripAid));
        if (i == gSkySel) { selX = x; selY = y; }
    }
    // The Moon, drawn as its actual phase — it is the one object you can
    // identify without a chart, so it doubles as your bearing.
    const MoonInfo mn = moonInfo(jd);
    const LookAngle ml = altAzFromRaDec(mn.raDeg, mn.decDeg, jd, cfg.lat, cfg.lon);
    if (ml.elDeg > 0) {
        int x, y; place(ml.azDeg, ml.elDeg, x, y);
        drawMoonDisc(x, y, 8, mn.illum, moonLitOnRight(mn.waxing, cfg.lat),
                     TH->txtMain, TH->panelBg);
        canvas.drawCircle(x, y, 8, TH->sep);
    }
    // The selection marker LAST, and a RECTANGLE rather than a circle: the
    // rings above already own drawCircle in this body, and two similar calls
    // in one body is the A2.22 shape.
    if (selX >= 0) canvas.drawRect(selX - 11, selY - 13, 22, 26, TH->accent);

    // ---- the caption, right of the dome.
    const Planet p = (Planet)gSkySel;
    const Equatorial q = planetPosition(p, jd);
    const LookAngle la = altAzFromRaDec(q.raDeg, q.decDeg, jd, cfg.lat, cfg.lon);
    char b[24];
    put(236, 30, TH->txtMain, planetName(p));
    if (la.elDeg > 0) {
        snprintf(b, sizeof(b), "%s", compass16(la.azDeg));
        put(236, 50, TH->accent, b);
        snprintf(b, sizeof(b), "%.0f%s", la.azDeg, sce::T(" deg", " deg"));
        put(236, 62, TH->txt2, b);
        // "deg" here too. The line ABOVE is an azimuth reading "45 deg";
        // this one is the same unit and used to read "+34 up", so the panel
        // showed two degree values, one labelled and one not, four pixels
        // apart. The word stays: it says which way, the unit says how much.
        snprintf(b, sizeof(b), "%+.0f %s %s", la.elDeg, sce::T("deg", "deg"),
                 sce::T("up", "haut"));
        put(236, 80, TH->txt1, b);
        // A hand at arm's length is about 20 degrees across, and a fist about
        // 10 — the field measure anyone can use without an instrument.
        snprintf(b, sizeof(b), "~%.0f %s", la.elDeg / 10.0,
                 sce::T("fists", "poings"));
        put(236, 92, TH->hint, b);
    } else {
        put(236, 50, TH->hint, sce::T("below", "sous"));
        put(236, 62, TH->hint, sce::T("horizon", "l'horizon"));
    }
    // y=136 and not 120: the "E" cardinal sits at (230, 118) and Font0 is 6x8,
    // so it ends at x=235 on rows 118-125 — flush against a caption starting at
    // x=236 on rows 120-127, with not one pixel between them. The two read as a
    // single word, and they collided every daylit hour, which is most of the
    // hours this screen is opened.
    if (daylight)
        put(236, 136, TH->accent2, sce::T("daylight", "de jour"));

    put(236, 196, TH->hint, sce::T("N up, E right", "N haut, E droite"));
    put(236, 208, TH->hint, sce::T("centre =", "centre ="));
    put(236, 218, TH->hint, sce::T("zenith", "zenith"));

    gCells.flush(canvas);            // THE one call site (A2.22)
}

static __attribute__((noinline)) void drawSkyView(double jd) {
    const double sunAlt  = sunAltDeg(jd, cfg.lat, cfg.lon);
    const bool   daylight = sunAlt > -6.0;
    const int    sel     = gSkySel;
    canvas.setTextSize(1);
    drawStarfield();

    // Orbit rings — ONE drawCircle call site (A2.22).
    for (int i = 0; i < (int)Planet::COUNT; i++)
        canvas.drawCircle(ORR_CX, ORR_CY, orrRing(planetSemiMajorAu((Planet)i)),
                          i == sel ? TH->txt2 : TH->sep);

    // The Sun: a filled core inside a corona ring, so it reads as a SOURCE and
    // not as one more planet. It is also the tap target for the label mode.
    canvas.drawCircle(ORR_CX, ORR_CY, 8, 0x8200);
    canvas.fillCircle(ORR_CX, ORR_CY, 5, 0xFE06);

    // Bodies, Earth included (index COUNT), so there is ONE fillCircle call
    // site for every dot on the chart.
    for (int i = 0; i <= (int)Planet::COUNT; i++) {
        const bool earth = (i == (int)Planet::COUNT);
        int x, y; orrBodyXY(i, jd, x, y);
        canvas.fillCircle(x, y, (!earth && i == sel) ? 4 : (earth ? 3 : 2),
                          PL_COL[earth ? 7 : i]);
    }
    // The selection ring goes on AFTER every dot, or a planet drawn later
    // would paint over it.
    {
        int x, y; orrBodyXY(sel, jd, x, y);
        canvas.drawCircle(x, y, 7, TH->accent);
    }

    // Labels on the chart: names, symbols, both, or a clean picture. Placed on
    // the side of the dot that faces the centre, so nothing runs into the
    // panel.
    if (gSkyLabels == 1 || gSkyLabels == 3) drawSkyNames(jd);
    if (gSkyLabels == 2 || gSkyLabels == 3) {
        for (int i = 0; i <= (int)Planet::COUNT; i++) {
            const bool earth = (i == (int)Planet::COUNT);
            int x, y; orrBodyXY(i, jd, x, y);
            drawGlyph(earth ? GLYPH_EARTH : i,
                      x > ORR_CX ? x - 15 : x + 8, y - 5, 1,
                      PL_COL[earth ? 7 : i]);
        }
    }

    // ---- the panel: every figure the old table carried, about the one object
    // you asked about, plus the two things a table row could not fit — the
    // symbol and what it takes to actually see it.
    const Planet p = (Planet)sel;
    const Equatorial q  = planetPosition(p, jd);
    const Helio      hp = planetHelio(p, jd);
    const LookAngle  la = altAzFromRaDec(q.raDeg, q.decDeg, jd, cfg.lat, cfg.lon);
    const Equatorial su = sunPosition(jd);
    const double elong  = fabs(wrap180(q.raDeg - su.raDeg));
    const bool   up     = la.elDeg > 0.0;

    canvas.drawFastVLine(PANEL_L - 8, 22, 204, TH->sep);
    // The big symbol sits at the RIGHT end of the panel, clear of the text
    // rows: at 3 px per cell it is 21x33, and placed at the panel's left
    // edge it would have covered the name and the state under it.
    drawGlyph(sel, 292, 88, 3, PL_COL[sel]);

    gCells.reset();
    auto put = [&](int x, int y, uint16_t col, const char* t) { gCells.put(x, y, col, t); };
    char b[20];
    put(PANEL_L, 90, TH->txtMain, planetName(p));

    const char* st; uint16_t stc;
    if (!up)           { st = sce::T("below",  "sous horiz"); stc = TH->hint; }
    else if (daylight) { st = sce::T("daylit", "de jour");    stc = TH->txt2; }
    else               { st = sce::T("UP NOW", "LEVE");       stc = TH->accent; }
    put(PANEL_L, 106, stc, st);

    // Same convention as the dome's caption: an elevation carries its unit.
    snprintf(b, sizeof(b), "%s %+.0f %s", compass16(la.azDeg), la.elDeg,
             sce::T("deg", "deg"));
    put(PANEL_L, 124, up ? TH->txt1 : TH->hint, b);
    snprintf(b, sizeof(b), "%.2f %s", hp.rAu, sce::T("au sun", "ua sol"));
    put(PANEL_L, 166, TH->txt2, b);
    snprintf(b, sizeof(b), "%.2f %s", q.distAu, sce::T("au us", "ua nous"));
    put(PANEL_L, 154, TH->txt2, b);
    // Elongation is what EXPLAINS an inner planet: Venus can never be more
    // than 47 degrees from the Sun, which is why it is only ever a morning or
    // evening star. For the outer ones it says how well placed they are.
    snprintf(b, sizeof(b), "%.0f %s", elong, sce::T("fr sun", "du sol"));
    put(PANEL_L, 142, TH->txt2, b);

    const uint8_t aid = planetAid(p);
    put(PANEL_L, 186, aid ? TH->accent2 : TH->hint,
        aid == 1 ? sce::T("binoculars", "jumelles")
      : aid == 2 ? sce::T("telescope",  "lunette")
                 : sce::T("naked eye",  "oeil nu"));

    put(PANEL_L, 202, TH->hint, sce::T("tap a dot", "tap un point"));
    put(PANEL_L, 212, TH->accent2, sce::T("tap here:", "tap ici :"));
    put(PANEL_L, 222, TH->accent2, sce::T("sky map", "carte ciel"));

    gCells.flush(canvas);            // THE one call site (A2.22)

    // ---- the SCAN STRIP: seven symbols, one per planet, tinted by whether
    // you could actually see it right now. This is what the table was for —
    // taking in the whole sky at a glance — in one row instead of eight, and
    // it doubles as the selector. The chart cannot do this job: two planets
    // can sit at the same longitude and tell you nothing about their altitude.
    for (int i = 0; i < (int)Planet::COUNT; i++) {
        const int sx = PANEL_L + (i % 4) * GRID_C;
        const int sy = GRID_Y + (i / 4) * GRID_C;
        const uint8_t s = planetState((Planet)i, jd, daylight);
        // THREE TINTS, and each says what it would take to see the thing
        // (user 08-04). Bright: up, sky dark, no instrument - go and look.
        // Mid: up, but it needs either an instrument (Uranus, Neptune) or a
        // darker sky (daylight) - the two cases are the same answer, "not
        // with your eyes, not right now". Dim: below the horizon, and dim
        // rather than absent so the row keeps its order.
        const bool eye = (s == 2) && (planetAid((Planet)i) == 0);
        const uint16_t col = (s == 0) ? TH->stripOff
                           : eye      ? TH->stripEye
                                      : TH->stripAid;
        if (i == sel) canvas.drawRect(sx, sy, GRID_C - 2, GRID_C - 2, TH->accent);
        drawGlyph(i, sx + 5, sy + 2, 2, col);
    }
    canvas.drawFastHLine(PANEL_L, GRID_Y + 2 * GRID_C + 2, 104, TH->sep);

    // Footer: how many are up in a dark sky. It counts the two telescope
    // planets, because the question it answers is "is there anything to point
    // at", not "anything to see unaided".
    int visible = 0;
    if (!daylight)
        for (int i = 0; i < (int)Planet::COUNT; i++)
            if (planetState((Planet)i, jd, daylight) == 2) visible++;
    // The daylight line used to read "daylight  sun +4", which states a number
    // without saying what it measures (user 08-04 asked what it meant). It now
    // names BOTH the regime and the direction: above the horizon the Sun is
    // "up", between 0 and -6 it is below and the sky is still too bright, and
    // "twilight" is the word for that.
    char line[64];
    if (daylight)
        snprintf(line, sizeof(line), sunAlt >= 0
                   ? sce::T("daylight - sun %.0f above horizon",
                            "jour - soleil %.0f au-dessus de l'horizon")
                   : sce::T("twilight - sun %.0f below horizon",
                            "crepuscule - soleil %.0f sous l'horizon"),
                 fabs(sunAlt));
    else if (visible == 0)
        snprintf(line, sizeof(line), "%s", sce::T("nothing up tonight",
                                                  "rien au-dessus ce soir"));
    else
        snprintf(line, sizeof(line), "%d %s", visible,
                 sce::T("planets up now", "planetes levees"));
    drawFooter(line, TH->txt2);
}

// Tap routing for the sky, kept beside the drawing it mirrors. Returns true
// when the tap was consumed. Radius 12 px around a dot: bigger than the 4 px
// marker, because a finger is not a cursor and the outer planets sit close
// together on a log scale.
static bool skyTap(int tx, int ty, double jd) {
    // The panel is the door to the sky map: the orrery says where the planets
    // ARE, the dome says where to point your face, and the panel is what you
    // are already reading when that question comes up.
    if (tx >= PANEL_L - 8) {
        // The grid sits at the TOP of the panel; everything under it opens
        // the sky map. Two targets, one region, split where the rule is
        // drawn — so the boundary the finger meets is the one the eye sees.
        if (ty < GRID_Y + 2 * GRID_C + 2) {
            // BOTH indices bounded before use. Integer division truncates
            // TOWARDS ZERO, so a tap ABOVE the grid (the header clock sits at
            // y = 5) gave row 0 rather than -1 and silently reselected a
            // planet; x in the last four pixels gave column 4, which is a cell
            // drawn on the row below.
            const int ry = ty - GRID_Y, rx = tx - PANEL_L;
            if (ry < 0 || rx < 0) return false;
            const int row = ry / GRID_C, col = rx / GRID_C;
            if (row > 1 || col > 3) return false;
            const int i = row * 4 + col;
            if (i < (int)Planet::COUNT) { gSkySel = i; return true; }
            return false;
        }
        gModal = Modal::SkyDome;
        return true;
    }
    const int sdx = tx - ORR_CX, sdy = ty - ORR_CY;
    if (sdx * sdx + sdy * sdy <= 14 * 14) {        // the Sun: cycle labels
        gSkyLabels = (uint8_t)((gSkyLabels + 1) & 3);
        return true;
    }
    for (int i = 0; i < (int)Planet::COUNT; i++) {
        int x, y; orrBodyXY(i, jd, x, y);
        const int dx = tx - x, dy = ty - y;
        if (dx * dx + dy * dy <= 12 * 12) { gSkySel = i; return true; }
    }
    return false;
}

// =============================================================================
// View 5 — LAUNCHES
// =============================================================================
static uint16_t launchStatusColour(const char* st) {
    if (!strcmp(st, "Go"))   return TH->accent;
    if (!strcmp(st, "Hold")) return TH->accent2;
    if (!strcmp(st, "Success") || !strcmp(st, "In Flight")) return TH->accent;
    return TH->hint;                              // TBC, TBD, anything new
}

// ---- the rocket silhouettes -------------------------------------------------
// PROFILES OF REAL VEHICLES, not one generic rocket restyled (user 08-04).
// Each entry carries the proportions that actually distinguish the family on a
// diagram — the ratios below are taken from published dimensions and rounded to
// what a 150 px silhouette can express:
//
//   Falcon 9      70.0 m / 3.7 m core, fairing 5.2 m  -> fairing WIDER than the
//                 core, a long uniform first stage, four landing legs.
//   Falcon Heavy  same core x3 side by side, side boosters ~2 m shorter, nose
//                 cones on the sides and a fairing in the middle.
//   Electron      18.0 m / 1.2 m -> the slimmest thing flying, 15:1, no legs,
//                 a small ogive fairing barely wider than the body.
//   Soyuz-2       46.1 m, four CONICAL strap-ons tapering to points at about
//                 half height — the one silhouette nobody confuses.
//   Ariane 6 /    core plus two (or four) slim solid boosters running two
//   Vulcan/H3     thirds of the way up, fairing wider than the core.
//   SLS           core with two boosters nearly as TALL as it, and a narrow
//                 crew stack on top instead of a fairing.
//   Starship      121 m, 9 m throughout: two fat stages, no fairing, four
//                 flaps (two forward on the ship, two aft) and grid fins.
//   New Glenn     98 m / 7 m: fat, fairing continuous with the body, strakes.
//
// A SHAPE and not a bitmap, still: one asset per vehicle would be stale the day
// a new rocket flies, and these proportions cover the ones that do.
//   Long March 5  57 m, core plus FOUR 3.35 m liquid boosters — the Chinese
//                 heavy-lift signature, shared by CZ-2F, CZ-3B and CZ-7.
//   PSLV          44 m with SIX small solid strap-ons clustered low around a
//                 slim core: nothing else in service looks like it.
//   GSLV Mk II    four liquid strap-ons; LVM3 is the OTHER Indian shape, two
//                 fat S200 solids, which is why they map to different profiles.
//   UNKNOWN       not a shape: the SOURCE says it does not know. Drawing a
//                 definite silhouette there would be the display asserting a
//                 vehicle nobody has announced.
enum RkFam : uint8_t {
    RK_F9, RK_HEAVY, RK_ELECTRON, RK_SOYUZ, RK_STRAP, RK_SLS, RK_SHIP,
    RK_GLENN, RK_QUAD, RK_PSLV, RK_UNKNOWN
};

static uint8_t rocketFamily(const char* rocket, const char* fallback) {
    char n[80];
    strlcpy(n, rocket[0] ? rocket : fallback, sizeof(n));
    for (char* c = n; *c; c++) *c = (char)tolower((unsigned char)*c);
    // FIRST, because the source itself declines to say. RocketLaunch.Live
    // returns "Unconfirmed Vehicle" for a launch whose vehicle has not been
    // announced — common for CASC missions, where the Long March variant is
    // confirmed only days ahead. That string contains none of the family words
    // below, so without this it falls through to the single-core default and
    // the screen draws a confident rocket for something nobody has announced.
    // The text was already honest; the picture was not.
    if (!n[0] || strstr(n, "unconfirmed") || strstr(n, "unknown") ||
        strstr(n, "to be determined") || !strcmp(n, "tbd"))    return RK_UNKNOWN;
    // Order matters: "falcon heavy" contains "falcon".
    if (strstr(n, "heavy"))                                   return RK_HEAVY;
    if (strstr(n, "starship") || strstr(n, "super heavy"))    return RK_SHIP;
    if (strstr(n, "new glenn"))                               return RK_GLENN;
    if (strstr(n, "soyuz") || strstr(n, "soiouz"))            return RK_SOYUZ;
    if (strstr(n, "sls") || strstr(n, "space launch system")) return RK_SLS;
    // SIX strap-ons — PSLV only, and it is worth its own profile because no
    // other vehicle in service carries that cluster.
    if (strstr(n, "pslv"))                                    return RK_PSLV;
    // FOUR strap-ons. The Chinese heavy family was the gap here (user 08-04):
    // only "long march 5" was matched, so CZ-2F, CZ-3B and CZ-7 — four
    // boosters each — fell through to the single-core default. GSLV Mk II has
    // four liquid strap-ons and was likewise drawn with two.
    if (strstr(n, "long march 2f") || strstr(n, "long march 3b") ||
        strstr(n, "long march 3c") || strstr(n, "long march 5")  ||
        strstr(n, "long march 7")  || strstr(n, "long march 6a") ||
        strstr(n, "gslv")          || strstr(n, "angara a5")     ||
        strstr(n, "proton"))                                  return RK_QUAD;
    // Slim light-lift. Vega is a four-stage solid stack, closer to this than
    // to anything with boosters.
    if (strstr(n, "electron")  || strstr(n, "alpha")    ||
        strstr(n, "rs1")       || strstr(n, "launcher") ||
        strstr(n, "vega")      || strstr(n, "epsilon")  ||
        strstr(n, "sslv")      || strstr(n, "ceres")    ||
        strstr(n, "kuaizhou")  || strstr(n, "hyperbola")||
        strstr(n, "jielong")   || strstr(n, "kinetica") ||
        strstr(n, "lijian"))                                  return RK_ELECTRON;
    // TWO boosters. LVM3's pair of S200 solids is the other Indian shape.
    if (strstr(n, "ariane")  || strstr(n, "atlas")  || strstr(n, "vulcan") ||
        strstr(n, "h3")      || strstr(n, "h-iia")  || strstr(n, "h-iib")  ||
        strstr(n, "lvm3")    || strstr(n, "mk iii") || strstr(n, "delta")  ||
        strstr(n, "long march 8"))                            return RK_STRAP;
    // Single core: Falcon 9, Neutron, Zhuque, Tianlong, Gravity, and the
    // Long March variants that genuinely fly without strap-ons (2C, 2D, 4B,
    // 4C, 6, 11, 12).
    return RK_F9;
}

static const char* rocketFamilyName(uint8_t f) {
    switch (f) {
        case RK_UNKNOWN:  return "not announced";
        case RK_HEAVY:    return "3x core";
        case RK_ELECTRON: return "light";
        case RK_SOYUZ:    return "4 strap-on";
        case RK_STRAP:    return "2 boosters";
        case RK_SLS:      return "crew stack";
        case RK_SHIP:     return "2 fat stages";
        case RK_GLENN:    return "wide body";
        case RK_QUAD:     return "4 boosters";
        case RK_PSLV:     return "6 strap-on";
        default:          return "single core";
    }
}

// Drawn from TABLES emptied through one call site per primitive (A2.22):
// written as literal calls, GCC 8.4 Xtensa is entitled to drop some, and a
// rocket that silently loses a booster is unfalsifiable by eye.
static __attribute__((noinline)) void drawRocket(int cx, int baseY, int h,
                                                 uint8_t fam, uint16_t col,
                                                 uint16_t accent, uint16_t dark) {
    struct R { int16_t x, y, w, hh; uint16_t c; };
    struct T { int16_t x1, y1, x2, y2, x3, y3; uint16_t c; };
    R rects[26]; T tris[16];
    int nr = 0, nt = 0;
    auto rect = [&](int x, int y, int w, int hh, uint16_t c) {
        if (nr < 26 && w > 0 && hh > 0)
            rects[nr++] = { (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)hh, c };
    };
    auto tri = [&](int x1, int y1, int x2, int y2, int x3, int y3, uint16_t c) {
        if (nt < 16) tris[nt++] = { (int16_t)x1, (int16_t)y1, (int16_t)x2,
                                    (int16_t)y2, (int16_t)x3, (int16_t)y3, c };
    };

    // Core width as a FRACTION OF HEIGHT, straight from each vehicle's real
    // fineness ratio: Electron is 15:1, Falcon 9 is 19:1, Starship 13:1 but
    // drawn fatter because 9 m over 121 m still reads as a tube.
    int cw;
    switch (fam) {
        case RK_ELECTRON: cw = h / 15; break;
        case RK_SHIP:     cw = h / 6;  break;
        case RK_GLENN:    cw = h / 8;  break;
        default:          cw = h / 12; break;
    }
    if (cw < 4) cw = 4;
    const int top = baseY - h;

    // ---- strap-ons FIRST, so the core overlaps them and reads as in front.
    if (fam == RK_SOYUZ) {
        // The conical boosters: a tapered body that comes to a point at about
        // half height. Four of them in reality; two are visible in profile,
        // and a third pair is hinted narrower behind.
        const int bh = h * 45 / 100, bw = cw * 4 / 5;
        for (int s = -1; s <= 1; s += 2) {
            const int bx = cx + s * (cw / 2 + bw / 2);
            tri(bx - bw / 2, baseY, bx + bw / 2, baseY, bx, baseY - bh, col);
            rect(bx - bw / 2, baseY - 3, bw, 3, dark);
        }
        for (int s = -1; s <= 1; s += 2) {          // the pair behind
            const int bx = cx + s * (cw / 2 + bw + 1);
            tri(bx - bw / 3, baseY, bx + bw / 3, baseY, bx, baseY - bh * 4 / 5, dark);
        }
    } else if (fam == RK_QUAD) {
        // Four cylindrical boosters with conical noses. In profile you see two
        // clearly and two behind them, so the back pair is drawn narrower and
        // in the shadow colour — depth without a second vanishing point.
        const int bw = cw * 3 / 5, bh = h * 52 / 100, bt = baseY - bh;
        for (int s = -1; s <= 1; s += 2) {           // the pair behind
            const int bx = cx + s * (cw / 2 + bw);
            tri(bx - bw / 3, bt + 4, bx + bw / 3, bt + 4, bx, bt - bw, dark);
            rect(bx - bw / 3, bt + 4, bw * 2 / 3, bh - 4, dark);
        }
        for (int s = -1; s <= 1; s += 2) {           // the pair in front
            const int bx = cx + s * (cw / 2 + bw / 2 + 1);
            tri(bx - bw / 2, bt, bx + bw / 2, bt, bx, bt - bw * 2, col);
            rect(bx - bw / 2, bt, bw, bh, col);
            rect(bx - bw / 2, bt + bh / 5, bw, 1, dark);
            rect(bx - bw / 2 - 1, baseY - 3, bw + 2, 3, dark);
        }
    } else if (fam == RK_PSLV) {
        // Six SHORT solid strap-ons clustered low: they are about a quarter of
        // the vehicle's height, which is what makes the cluster read as a
        // skirt rather than as a set of boosters.
        const int bw = cw * 2 / 5, bh = h * 26 / 100, bt = baseY - bh;
        for (int s = -1; s <= 1; s += 2) {
            for (int k = 0; k < 2; k++) {            // two visible each side
                const int bx = cx + s * (cw / 2 + bw / 2 + 1 + k * bw);
                const uint16_t c = k ? dark : col;
                tri(bx - bw / 2, bt, bx + bw / 2, bt, bx, bt - bw * 2, c);
                rect(bx - bw / 2, bt, bw, bh - 2, c);
            }
        }
    } else if (fam == RK_STRAP || fam == RK_SLS || fam == RK_HEAVY) {
        const bool sls   = (fam == RK_SLS);
        const bool heavy = (fam == RK_HEAVY);
        const int bw = heavy ? cw : (sls ? cw * 2 / 3 : cw / 2);
        const int bh = heavy ? h * 62 / 100 : (sls ? h * 78 / 100 : h * 66 / 100);
        const int bt = baseY - bh;
        for (int s = -1; s <= 1; s += 2) {
            const int bx = cx + s * (cw / 2 + bw / 2 + 1);
            tri(bx - bw / 2, bt, bx + bw / 2, bt, bx, bt - bw * 2, col);
            rect(bx - bw / 2, bt, bw, bh, col);
            rect(bx - bw / 2, bt + bh / 5, bw, 1, dark);
            rect(bx - bw / 2 - 1, baseY - 3, bw + 2, 3, dark);
        }
    }

    // ---- the core: fairing or crew stack, body, bands, engine section.
    if (fam == RK_UNKNOWN) {
        // NOT a silhouette — a DASHED OUTLINE. The vehicle has not been
        // announced, so the drawing states the shape of a launcher and refuses
        // to state which one. A solid body here would look exactly as certain
        // as a Falcon 9 beside it, which is the whole failure mode: the text
        // said "Unconfirmed Vehicle" while the picture said "this one".
        // Same tables, same single call site per primitive (A2.22).
        const int w     = cw * 7 / 5;                  // a generic body width
        const int pitch = (h / 6) < 4 ? 4 : (h / 6);   // dash + gap
        const int dash  = (pitch / 2) < 2 ? 2 : pitch / 2;
        for (int y = top; y < baseY - 3; y += pitch) {
            int hh = dash;
            if (y + hh > baseY - 3) hh = baseY - 3 - y;
            rect(cx - w / 2,     y, 1, hh, col);
            rect(cx + w / 2 - 1, y, 1, hh, col);
        }
        rect(cx - w / 2, top,       w, 1, col);        // the two ends, closed:
        rect(cx - w / 2, baseY - 3, w, 3, dark);       // a tube, not a ladder
    } else if (fam == RK_SHIP) {
        // Two fat stages of equal width, a pointed ship nose, forward flaps at
        // the top and aft flaps at the stage joint. No fairing: the payload
        // rides inside.
        const int split = top + h * 46 / 100;
        tri(cx, top, cx - cw / 2, top + cw, cx + cw / 2, top + cw, col);
        rect(cx - cw / 2, top + cw, cw, split - top - cw, col);
        rect(cx - cw / 2, split, cw, baseY - split - 3, col);
        rect(cx - cw / 2, split - 2, cw, 2, accent);        // the stage joint
        for (int s = -1; s <= 1; s += 2) {                  // forward flaps
            const int fx = cx + s * cw / 2;
            tri(fx, top + cw * 2, fx, top + cw * 4, fx + s * cw / 2, top + cw * 4, col);
        }
        for (int s = -1; s <= 1; s += 2) {                  // aft flaps
            const int fx = cx + s * cw / 2;
            tri(fx, split - cw, fx, split, fx + s * cw * 2 / 3, split, col);
        }
    } else if (fam == RK_SLS) {
        // A narrow crew stack on top, not a fairing: the tower and the capsule
        // are what make SLS unmistakable in profile.
        const int sw = cw / 3;
        rect(cx - 1, top, 2, h / 14, col);                  // launch abort tower
        tri(cx, top + h / 14, cx - sw, top + h / 8, cx + sw, top + h / 8, col);
        rect(cx - sw, top + h / 8, sw * 2, h / 12, col);    // the capsule
        const int neck = top + h / 8 + h / 12;
        tri(cx, neck, cx - cw / 2, neck + cw, cx + cw / 2, neck + cw, col);
        rect(cx - cw / 2, neck + cw, cw, baseY - neck - cw - 3, col);
    } else {
        // Everything else has a FAIRING, and on most modern launchers it is
        // WIDER than the core — Falcon 9 is 5.2 m over 3.7. Drawing it flush
        // with the body is the commonest way a rocket sketch looks wrong.
        const int fw = (fam == RK_ELECTRON) ? cw + 1
                     : (fam == RK_GLENN)    ? cw
                                            : cw * 7 / 5;
        const int fh = h * (fam == RK_ELECTRON ? 14 : 18) / 100;
        tri(cx, top, cx - fw / 2, top + fh * 3 / 5, cx + fw / 2, top + fh * 3 / 5, col);
        rect(cx - fw / 2, top + fh * 3 / 5, fw, fh * 2 / 5, col);
        // The shoulder back down to the core.
        tri(cx - fw / 2, top + fh, cx + fw / 2, top + fh, cx, top + fh + fw / 3, col);
        const int neck = top + fh + fw / 6;
        rect(cx - cw / 2, neck, cw, baseY - neck - 3, col);
        rect(cx - cw / 2, neck + (baseY - neck) / 3, cw, 2, accent);   // interstage
        if (fam == RK_F9 || fam == RK_HEAVY) {
            // Grid fins near the top of the first stage, and landing legs at
            // the base — the two features that say "this one comes back".
            const int gy = neck + (baseY - neck) / 3 + 5;
            for (int s = -1; s <= 1; s += 2)
                rect(cx + s * (cw / 2 + 1) - (s < 0 ? 2 : 0), gy, 3, 5, col);
            for (int s = -1; s <= 1; s += 2)
                tri(cx + s * cw / 2, baseY - h / 9, cx + s * cw / 2, baseY - 3,
                    cx + s * (cw / 2 + cw / 2), baseY - 3, col);
        }
    }

    // ---- engine section: a slight flare and the bells. Bell COUNT is the
    // vehicle's real one — nine on a Falcon 9, one on an Electron stage seen
    // in profile, six on a Starship booster row.
    const int ew = (fam == RK_SHIP) ? cw : cw + cw / 3;
    rect(cx - ew / 2, baseY - 6, ew, 3, col);
    const int bells = (fam == RK_ELECTRON) ? 2 : (fam == RK_SHIP ? 5 : 3);
    for (int i = 0; i < bells; i++) {
        const int bw2 = ew / bells;
        rect(cx - ew / 2 + i * bw2, baseY - 3, bw2 - 1, 3, dark);
    }

    for (int i = 0; i < nr; i++)                  // ONE call site
        canvas.fillRect(rects[i].x, rects[i].y, rects[i].w, rects[i].hh, rects[i].c);
    for (int i = 0; i < nt; i++)                  // ONE call site
        canvas.fillTriangle(tris[i].x1, tris[i].y1, tris[i].x2, tris[i].y2,
                            tris[i].x3, tris[i].y3, tris[i].c);
}

// The service tower beside it. Pure scene-setting, and it earns its place:
// it gives the silhouette a ground and a scale, so a stubby Starship and a
// slim Electron read as different sizes rather than different drawings.
static __attribute__((noinline)) void drawTower(int x, int topY, int baseY,
                                                uint16_t col) {
    canvas.drawFastVLine(x, topY, baseY - topY, col);
    canvas.drawFastVLine(x + 7, topY + 10, baseY - topY - 10, col);
    for (int y = topY + 10; y < baseY; y += 12)
        canvas.drawLine(x, y, x + 7, y + 6, col);
}

// The empty state, in its own `noinline` function: inline it was a second
// drawString in the same body as the cell loop — the A2.22 shape.
static __attribute__((noinline)) void drawLaunchesNotice() {
    canvas.setTextSize(1);
    canvas.setTextColor(TH->hint);
    canvas.drawString(WiFi.status() == WL_CONNECTED
                      ? sce::T("waiting for the launch feed",
                               "attente du flux de lancements")
                      : sce::T("no network", "pas de reseau"), 8, 40);
}

// ---- LAUNCHES, the pad view -------------------------------------------------
// WHAT SOMEBODY ACTUALLY WANTS FROM THIS SCREEN, in the order they want it:
// when is the next one, is it going to happen, what is flying, and from where.
// The layout answers those four in four places, and lets a SHAPE answer
// wherever a shape can — a column of five dated text rows is something you
// READ and then compare in your head, five silhouettes standing on a time axis
// is something you SEE.
//
// FOUR ZONES.
//   The BANNER (y 20..42) carries identity and verdict: the vehicle at double
//   size, and the status as a filled CHIP rather than a coloured word. A chip
//   has an edge, so "Go" reads at arm's length; a coloured word only reads if
//   you already know the colour code.
//   The VEHICLE (x 0..94) keeps the left edge, because tall and narrow is what
//   a rocket IS and a silhouette wants height, not width.
//   The BRIEF (x 100..316) is the countdown at the size that says it is the
//   point of the view, the pad weather as an ICON, and four facts hung off a
//   spine that groups them as one block instead of four loose rows.
//   The HORIZON (y 166..218) replaces the five-row text queue. That queue
//   answered "when is each of them" by making the reader parse five dates and
//   subtract them; the strip places the five ON a time axis, so the SPACING is
//   the answer, and draws each as its own silhouette, so "what is flying" is
//   answered for all five at once instead of only for the selected one.
//
// WHAT WAS DROPPED, and why the graphic says it better. The per-row date, the
// per-row vehicle NAME and the per-row status word are gone from the queue:
// position on the axis carries the date, the silhouette carries the vehicle,
// and a status pip under each marker carries the verdict. What the rows had
// that the strip does not is the exact minute of the four launches you did not
// select — one swipe away, and the strip is what tells you whether that swipe
// is worth making.
//
// WHY THERE IS NO COUNTDOWN DIAL. An arc filling as T-0 approaches draws ONE
// launch's distance in time; the horizon strip draws five of them, on a scale
// with named ticks. The dial would have been the same fact drawn twice, and
// the second drawing costs exactly the space the pad name needs.
static constexpr int STRIP_SEP  = 166;   // the rule above the horizon
static constexpr int STRIP_BASE = 202;   // the time axis itself
static constexpr int STRIP_X0   = 14, STRIP_X1 = 306;
// No two silhouettes may overlap: two rockets drawn on top of each other convey
// strictly less than one. 18 px is the widest thumbnail (a four-booster core at
// h=30) plus a margin.
static constexpr int STRIP_MINSEP = 18;
// Half-width of the dead zone around the SELECTED marker: a tap inside it
// steps nowhere. Half the minimum separation, so the zone can never swallow
// a neighbour's side of the axis.
static constexpr int STRIP_TAP_DEAD = STRIP_MINSEP / 2;

// WHERE EACH MARKER LANDED, kept for the tap router. The strip is the only
// selectable thing on this screen and its markers are placed by DATA, not by a
// fixed grid, so the hit test cannot be arithmetic the way a row list's is — it
// has to read back the positions the frame actually drew. Drawing runs before
// touch handling in loop(), so these are always the ones under the finger.
static int16_t gStripX[MAX_LAUNCH] = { -1000, -1000, -1000, -1000, -1000 };

// TIME IS LOGARITHMIC ON THIS AXIS, and it has to be. The next launch is often
// hours away and the fifth is three weeks out; on a linear axis four markers
// pile into the first centimetre and 90 % of the strip draws nothing. log10(1 +
// hours) gives hours, days and weeks comparable stretches of screen, which is
// how the eye reads "soon" against "not for a fortnight" — the same reasoning
// the pass profile uses when it draws an elevation instead of printing it.
static int stripXForHours(double h, double hMax) {
    if (h < 0) h = 0;                       // a launch already past sits at NOW
    const double d = log10(1.0 + hMax);
    const double u = (d > 0.0) ? log10(1.0 + h) / d : 0.0;
    int x = STRIP_X0 + (int)lround(u * (STRIP_X1 - STRIP_X0));
    if (x < STRIP_X0) x = STRIP_X0;
    if (x > STRIP_X1) x = STRIP_X1;
    return x;
}

// Weather reads as a STATE before it reads as a word: clear is a go, overcast
// is a shrug, a storm is why a launch slips. Three colours, keyed on what the
// source actually publishes.
static uint16_t weatherColour(const char* cond) {
    char n[16];
    strlcpy(n, cond, sizeof(n));
    for (char* c = n; *c; c++) *c = (char)tolower((unsigned char)*c);
    if (strstr(n, "clear") || strstr(n, "sunny"))                 return TH->accent;
    if (strstr(n, "rain")  || strstr(n, "storm") || strstr(n, "snow") ||
        strstr(n, "thunder") || strstr(n, "fog"))                 return TH->alert;
    return TH->txt2;
}

// ...and the same state as a SHAPE. "Overcast" spelled out costs eight glyphs
// of a row that has better uses; a cloud costs 24 px square and is recognised
// without being read, which matters more here than the nuance between "Mostly
// Cloudy" and "Overcast" — nobody scrubs a launch over that distinction.
// FIVE kinds and not two: partly-cloudy is the commonest reading at a Florida
// pad and folding it into "cloud" would have made the icon nearly constant.
enum WxKind : uint8_t { WX_NONE, WX_CLEAR, WX_PART, WX_CLOUD, WX_FOG, WX_RAIN };

static uint8_t weatherKind(const char* cond) {
    if (!cond || !cond[0]) return WX_NONE;
    char n[16];
    strlcpy(n, cond, sizeof(n));
    for (char* c = n; *c; c++) *c = (char)tolower((unsigned char)*c);
    // Order matters the way it does in rocketFamily(): "Partly Cloudy" carries
    // no "clear", but "Mostly Clear" does, so the qualified readings are tested
    // before the plain ones.
    if (strstr(n, "rain")  || strstr(n, "storm")   || strstr(n, "thunder") ||
        strstr(n, "snow")  || strstr(n, "shower")  || strstr(n, "drizzle"))
        return WX_RAIN;
    if (strstr(n, "fog")   || strstr(n, "mist")    || strstr(n, "haze"))
        return WX_FOG;
    if (strstr(n, "part")  || strstr(n, "few")     || strstr(n, "scatter"))
        return WX_PART;
    if (strstr(n, "clear") || strstr(n, "sunny")   || strstr(n, "fair"))
        return WX_CLEAR;
    return WX_CLOUD;
}

// Drawn from TABLES emptied through one call site per primitive, and `noinline`
// so the split exists in the BINARY and not only in the source (A2.22): a sun
// is a disc plus eight strokes, which is the exact shape GCC 8.4 Xtensa is
// entitled to thin out, and an icon that quietly loses half its rays still
// looks like an icon.
static __attribute__((noinline)) void drawWxIcon(int cx, int cy, uint8_t kind,
                                                 uint16_t col) {
    struct C { int16_t x, y, r; };
    struct R { int16_t x, y, w, h; };
    C cs[6]; R rs[14];
    int nc = 0, nr = 0;
    auto circ = [&](int x, int y, int r) {
        if (nc < 6 && r > 0) cs[nc++] = { (int16_t)x, (int16_t)y, (int16_t)r };
    };
    auto rct = [&](int x, int y, int w, int h) {
        if (nr < 14 && w > 0 && h > 0)
            rs[nr++] = { (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h };
    };

    const bool sun   = (kind == WX_CLEAR || kind == WX_PART);
    const bool cloud = (kind != WX_CLEAR);
    if (sun) {
        // Full disc when the sky is clear; tucked up and to the left, smaller,
        // when a cloud shares the frame — which is what "partly" looks like.
        const bool alone = (kind == WX_CLEAR);
        const int sx = alone ? cx : cx - 6;
        const int sy = alone ? cy : cy - 6;
        const int r  = alone ? 7 : 5;
        circ(sx, sy, r);
        // Four orthogonal rays and four diagonal pips. Squares stand in for the
        // diagonals because a rectangle cannot be drawn at 45 degrees and an
        // anti-aliased line is forbidden in the per-frame path (A2.22).
        rct(sx - 1,     sy - r - 4, 2, 3);
        rct(sx - 1,     sy + r + 1, 2, 3);
        rct(sx - r - 4, sy - 1,     3, 2);
        rct(sx + r + 1, sy - 1,     3, 2);
        const int d = (r * 7) / 10 + 2;
        rct(sx - d - 1, sy - d - 1, 2, 2);
        rct(sx + d,     sy - d - 1, 2, 2);
        rct(sx - d - 1, sy + d,     2, 2);
        rct(sx + d,     sy + d,     2, 2);
    }
    if (cloud) {
        // Three discs and a flat base: the cheapest arrangement that still
        // reads as a cloud rather than as a blob.
        const bool with_sun = (kind == WX_PART);
        const int bx = with_sun ? cx + 3 : cx;
        const int by = with_sun ? cy + 4 : cy;
        circ(bx - 6, by + 1, 5);
        circ(bx,     by - 2, 7);
        circ(bx + 6, by + 1, 5);
        rct(bx - 11, by + 1, 22, 5);
        // Rain falls, fog lies flat. Two motions, and they are what separates
        // "it is grey" from "the pad is under a cell".
        if (kind == WX_RAIN)
            for (int k = -1; k <= 1; k++) rct(bx + k * 6 - 1, by + 8, 2, 5);
        if (kind == WX_FOG)
            for (int k = 0; k < 2; k++)   rct(bx - 9 + k * 3, by + 8 + k * 4, 18, 2);
    }

    for (int i = 0; i < nc; i++)                 // ONE call site
        canvas.fillCircle(cs[i].x, cs[i].y, cs[i].r, col);
    for (int i = 0; i < nr; i++)                 // ONE call site
        canvas.fillRect(rs[i].x, rs[i].y, rs[i].w, rs[i].h, col);
}

static __attribute__((noinline)) void drawLaunchesView() {
    canvas.setTextSize(1);
    Guard g;
    if (gLaunchN == 0) { drawLaunchesNotice(); return; }
    const time_t now = time(nullptr);
    const bool haveClock = clockOk();
    if (gCursor >= gLaunchN) gCursor = 0;
    const Launch& L = gLaunch[gCursor];
    const uint8_t fam = rocketFamily(L.rocket, L.name);
    const uint16_t stCol = launchStatusColour(L.status);
    const int n = (gLaunchN < MAX_LAUNCH) ? gLaunchN : MAX_LAUNCH;

    gCells.reset();
    auto put = [&](int x, int y, uint8_t size, uint16_t col, const char* s) {
        gCells.put(x, y, col, s, size);
    };
    auto putR = [&](int right, int y, uint8_t size, uint16_t col, const char* s) {
        gCells.putR(right, y, col, s, size);
    };
    char b[40];

    // Every filled shape on this screen goes through THREE tables and three
    // call sites (A2.22). The chip, the spine, the selection plate and the five
    // status pips are all rectangles; written as literal calls they are a dozen
    // similar fillRects in one body, and the one GCC decides to drop is the one
    // nobody can prove is missing.
    struct R { int16_t x, y, w, h; uint16_t c; };
    struct H { int16_t x, y, w; uint16_t c; };
    struct V { int16_t x, y, h; uint16_t c; };
    R rects[14]; H hls[6]; V vls[6];
    int nr = 0, nh = 0, nv = 0;
    auto rect = [&](int x, int y, int w, int h, uint16_t c) {
        if (nr < 14 && w > 0 && h > 0)
            rects[nr++] = { (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, c };
    };
    auto hline = [&](int x, int y, int w, uint16_t c) {
        if (nh < 6 && w > 0) hls[nh++] = { (int16_t)x, (int16_t)y, (int16_t)w, c };
    };
    auto vline = [&](int x, int y, int h, uint16_t c) {
        if (nv < 6 && h > 0) vls[nv++] = { (int16_t)x, (int16_t)y, (int16_t)h, c };
    };

    // ---- THE BANNER. The vehicle is the headline; the status is the one word
    // that decides whether the rest matters, so it sits as a chip at the far
    // right of the same line, where the eye ends up anyway.
    //
    // THE CHIP IS BOUNDED, and the arithmetic is not left to a comment. The
    // headline is twenty glyphs at double size, ending at x = 4 + 20*12 = 244.
    // The old note claimed the widest status was "In Flight" and therefore
    // started at 250 — but `status` holds ELEVEN characters and Launch Library
    // copies `status.abbrev` in unfiltered, so "Partial Failure" arrives as
    // "Partial Fai" and the chip starts at 238: six pixels INTO the vehicle
    // name. A fact about a feed does not belong in a comment when it can be a
    // clamp, so the chip is capped at the nine glyphs the layout can hold.
    sce::fitGlyphs(b, sizeof(b), L.rocket[0] ? L.rocket : L.name, 20);
    put(4, 22, 2, TH->txtMain, b);
    char stBuf[12];
    sce::fitGlyphs(stBuf, sizeof(stBuf), L.status[0] ? L.status : "?", 9);
    const char* st = stBuf;
    const int chipW = (int)strlen(st) * 6 + 12;
    const int chipX = 316 - chipW;
    // FILLED when the launch is locked in, OUTLINED when it is not, and the
    // shape carries that on its own — which is what lets the "TBD" grey stay
    // grey. A solid chip in that grey with dark ink on it measures 2.8:1 and is
    // unreadable, and brightening the grey to fix the ink would have destroyed
    // the very distinction the colour exists to make. So: solid = a date you
    // can plan around, outline = pencilled in.
    const bool firm = (stCol != TH->hint);
    if (firm) {
        rect(chipX, 20, chipW, 18, stCol);
    } else {
        rect(chipX,             20, chipW,  1, stCol);   // the four edges, from
        rect(chipX,             37, chipW,  1, stCol);   // the same table as
        rect(chipX,             20,     1, 18, stCol);   // every other filled
        rect(chipX + chipW - 1, 20,     1, 18, stCol);   // shape on the screen
    }
    put(chipX + 6, 25, 1, firm ? TH->bg : TH->txt1, st);
    hline(0, 42, 320, TH->sep);

    // ---- THE VEHICLE, left edge, standing on its own ground line with the
    // tower beside it for scale. The architecture is NAMED under the drawing:
    // a silhouette teaches nothing if you cannot say what you are looking at.
    // 157 and not 155 — Font0 inks rows y..y+7 and the ground rule is at 154.
    put(4, 157, 1, TH->hint, rocketFamilyName(fam));
    hline(4, 154, 88, TH->txt2);

    // ---- THE COUNTDOWN, the reason anyone opens this view, at the size that
    // says so. It ticks LOCALLY between polls, which is why the poll can be as
    // slow as the API demands without the screen looking frozen.
    char big[12] = "--", small[16] = "";
    if (L.netUtc > 0 && haveClock) {
        long d = (long)(L.netUtc - now);
        const bool past = d < 0;
        if (past) d = -d;
        snprintf(big, sizeof(big), "%c%ldd", past ? '+' : '-', d / 86400);
        snprintf(small, sizeof(small), "%02ld:%02ld:%02ld",
                 (d % 86400) / 3600, (d % 3600) / 60, d % 60);
    }
    put(100, 50, 1, TH->txt2, sce::T("T MINUS", "T MOINS"));
    put(100, 60, 3, TH->accent, big);
    put(100, 88, 2, TH->txt1, small);

    // ---- PAD WEATHER, as an icon with its two numbers right-aligned under it.
    // Wind is parsed independently of the other two, so gating the block on
    // condition and temperature alone silently dropped a pad that published
    // only wind.
    const uint8_t wxk = weatherKind(L.wxCond);
    const uint16_t wxc = weatherColour(L.wxCond);
    if (wxk != WX_NONE || L.wxTempF > -999 || L.wxWindMph >= 0) {
        if (L.wxTempF > -999) {
            snprintf(b, sizeof(b), "%d%s", dTempF(L.wxTempF), uTemp());
            putR(316, 80, 1, TH->txtMain, b);
        }
        if (L.wxWindMph >= 0) {
            snprintf(b, sizeof(b), "%d %s", dMph(L.wxWindMph), uWind());
            putR(316, 92, 1, wxc, b);
        }
    }

    // ---- THE FACTS, on a spine. Four rows that each answer a different
    // question — what is being flown, by whom, from where, and on what date —
    // and a 2 px vertical rule to their left that says they are ONE block and
    // not four unrelated lines. The mission's segment of the spine takes the
    // accent, so the row that names the flight is the row the eye finds first.
    rect(96, 110, 2, 10, TH->accent2);
    rect(96, 122, 2, 34, TH->sep);
    sce::fitGlyphs(b, sizeof(b), L.name, 35);
    put(104, 112, 1, TH->accent2, b);
    // Provider shares its row with whichever second fact the serving source
    // carries: the target orbit (LL2) or the booster serial (RLL). Both are
    // short, and either reads as part of the same sentence — who is flying it,
    // and the one detail that distinguishes this flight from the last.
    const char* second = L.orbit[0] ? L.orbit : L.tag;
    char row[48];
    if (second[0]) snprintf(row, sizeof(row), "%s  -  %s", L.provider, second);
    else           snprintf(row, sizeof(row), "%s", L.provider);
    sce::fitGlyphs(b, sizeof(b), row, 35);
    put(104, 124, 1, TH->txt2, b);
    if (L.pad[0]) {
        sce::fitGlyphs(b, sizeof(b), L.pad, 35);
        put(104, 136, 1, TH->txt2, b);
    }
    if (L.netUtc > 0) {
        sce::localDayHhmm(L.netUtc, cfg.tzOffsetH, b, sizeof(b));
        put(104, 148, 1, TH->txtMain, b);
    }

    // ---- THE HORIZON. Marker positions first, because the tap router reads
    // them back and because the collision pass has to run before anything is
    // drawn from them.
    double hMax = 24.0;              // never shorter than a day: a single
    for (int i = 0; i < n; i++)      // launch tonight must not stretch the axis
        if (gLaunch[i].netUtc > 0) {
            const double h = (double)(gLaunch[i].netUtc - now) / 3600.0;
            if (h > hMax) hMax = h;
        }
    int mx[MAX_LAUNCH];
    for (int i = 0; i < n; i++) {
        if (haveClock && gLaunch[i].netUtc > 0)
            mx[i] = stripXForHours((double)(gLaunch[i].netUtc - now) / 3600.0, hMax);
        else
            // No clock, no axis: evenly spaced, which at least keeps the five
            // selectable and in order. An invented position would be a lie the
            // countdown has already admitted it cannot tell.
            mx[i] = STRIP_X0 + (STRIP_X1 - STRIP_X0) * i / (n > 1 ? n - 1 : 1);
    }
    // Two launches on the same morning would draw one on top of the other, so
    // the second is pushed right until it clears. The strip is a sense of
    // SPACING, not a ruler: a pushed marker is at most a few pixels from where
    // the axis says it belongs, and the named ticks below are the true scale.
    // Five markers need at most 4x18 = 72 px of the 292 available, so the pass
    // always resolves and the final shift back can never re-create an overlap.
    for (int i = 1; i < n; i++)
        if (mx[i] < mx[i - 1] + STRIP_MINSEP) mx[i] = mx[i - 1] + STRIP_MINSEP;
    const int over = mx[n - 1] - STRIP_X1;
    if (over > 0)
        for (int i = 0; i < n; i++) mx[i] -= over;
    for (int i = 0; i < n; i++) gStripX[i] = (int16_t)mx[i];
    for (int i = n; i < MAX_LAUNCH; i++) gStripX[i] = -1000;

    hline(0, STRIP_SEP, 320, TH->sep);
    hline(10, STRIP_BASE, 300, TH->sep);
    // The selection is a PLATE behind the chosen silhouette rather than an
    // outline around it: the queue rows used the same panel fill, and reusing
    // it keeps "the one you are looking at" spelled one way in this bin.
    rect(mx[gCursor] - 11, 168, 22, 34, TH->panelBg);
    // NOW, then whichever of a day, a week and a month the horizon reaches.
    // Ticks are what turn a strip of dots into an axis: without them the eye
    // can compare two markers but cannot say whether the gap is a night or a
    // fortnight.
    // 196..204 and not down to the pips at 205: the tables are emptied
    // rectangles first, lines second, so a tick reaching that far would be
    // drawn THROUGH the status pip sharing its column.
    vline(STRIP_X0, 196, 9, TH->sep);
    put(2, 210, 1, TH->hint, sce::T("NOW", "MAINT"));
    if (haveClock) {
        static const struct { double h; const char* en; const char* fr; } TICK[3] = {
            {  24.0, "1d", "1j"   },
            { 168.0, "1w", "1sem" },
            { 720.0, "1M", "1M"   },
        };
        for (int k = 0; k < 3; k++) {
            if (TICK[k].h > hMax) continue;
            const int x = stripXForHours(TICK[k].h, hMax);
            vline(x, 196, 9, TH->sep);
            // Centred on the tick, then pushed back inside the screen: the
            // last tick can sit at x=306 and the French "1sem" is 24 px wide,
            // so an uncorrected label loses its tail off the right edge.
            const char* lb = sce::T(TICK[k].en, TICK[k].fr);
            const int lw = (int)strlen(lb) * 6;
            int lx = x - lw / 2;
            if (lx > 318 - lw) lx = 318 - lw;
            if (lx < 0)        lx = 0;
            put(lx, 210, 1, TH->hint, lb);
        }
    }
    // A status PIP under every marker. The queue said "Go" or "TBD" in words
    // five times over; three pixels of the same colour say it five times in the
    // width of a fingernail, and the chip in the banner is where the word lives
    // for the one launch actually being read.
    for (int i = 0; i < n; i++)
        rect(mx[i] - 3, 205, 7, 3, launchStatusColour(gLaunch[i].status));

    // ---- EMPTY THE TABLES. Before the silhouettes, deliberately: the
    // selection plate is a BACKGROUND, and a table flushed after the rockets
    // would paint it over the very thing it is meant to single out.
    for (int i = 0; i < nr; i++)                 // ONE call site
        canvas.fillRect(rects[i].x, rects[i].y, rects[i].w, rects[i].h, rects[i].c);
    for (int i = 0; i < nh; i++)                 // ONE call site
        canvas.drawFastHLine(hls[i].x, hls[i].y, hls[i].w, hls[i].c);
    for (int i = 0; i < nv; i++)                 // ONE call site
        canvas.drawFastVLine(vls[i].x, vls[i].y, vls[i].h, vls[i].c);

    drawTower(8, 50, 154, TH->sep);
    drawRocket(48, 154, 100, fam, TH->txt1, stCol, TH->panelBg);
    if (wxk != WX_NONE) drawWxIcon(290, 62, wxk, wxc);

    // The five thumbnails, standing on the axis. The selected one is taller and
    // drawn in the main ink; the others recede to txt2, which is enough to rank
    // them without making four of the five unreadable — a queue you cannot read
    // is a queue you may as well not draw.
    for (int i = 0; i < n; i++) {
        const bool on = (i == gCursor);
        drawRocket(mx[i], STRIP_BASE - 2, on ? 30 : 22,
                   rocketFamily(gLaunch[i].rocket, gLaunch[i].name),
                   on ? TH->txtMain : TH->txt2,
                   launchStatusColour(gLaunch[i].status), TH->sep);
    }

    // ---- ATTRIBUTION. RocketLaunch.Live asks, in the terms that come with
    // the free endpoint, that the data be credited wherever it is displayed.
    // It rides the cell list rather than a drawString of its own: a second
    // similar draw call in this body is precisely the shape GCC 8.4 Xtensa is
    // entitled to delete (A2.22), and a credit that silently disappears from
    // the binary is worse than none. It doubles as the honest label a failover
    // owes the reader — which of the two sources is on screen right now.
    if (gLaunchSrc != LaunchSrc::None)
        putR(316, FTR_Y, 1, TH->hint,
             gLaunchSrc == LaunchSrc::Rll ? "Data by RocketLaunch.Live"
                                          : "Data by Launch Library 2");

    gCells.flush(canvas);            // THE one call site (A2.22)

    // "as of hh:mm" past an hour (SPACE.md trap 6): a T-0 slips, and a
    // stale countdown presented as live is the one lie this view can tell.
    // It OUTRANKS the gesture hint below: a stale countdown is a fact about
    // what is on screen, the hint is only about what you could do next.
    if (gLaunchFetched > 0 && now - gLaunchFetched > 3600) {
        const time_t lt = gLaunchFetched + (time_t)lround(cfg.tzOffsetH * 3600.0f);
        struct tm t; gmtime_r(&lt, &t);
        snprintf(b, sizeof(b), "%s %02d:%02d",
                 sce::T("as of", "au"), t.tm_hour, t.tm_min);
        drawFooter(b, TH->accent2);
    } else if (n > 1) {
        // PASSES tells you its rows are tappable; this view did not, and its
        // gesture is less guessable — nothing about a row of silhouettes says
        // that tapping beside one steps to it. An action nobody can discover
        // is an action that does not exist.
        drawFooter(sce::T("tap left / right of the marker to step",
                          "tapez a gauche / droite du marqueur"), TH->hint);
    }
}

// `applyEvent` is defined below (it consumes what every producer emits);
// declared here so the strip's tap router can be read beside the screen it
// belongs to rather than moved away from it.
static void applyEvent(UiEvent e);

// Tap routing on the horizon strip: a tap to the LEFT of the selected marker
// steps to the previous launch, a tap to its RIGHT to the next.
//
// It STEPS, it does not aim. Selecting the nearest marker sounds friendlier and
// is worse in the hand: the markers are placed by data, so they cluster — 18 px
// apart at the minimum separation — and picking one means looking first, then
// aiming at a 10 px silhouette. Stepping needs neither. The direction still
// means what it looks like, because the step goes the way you tapped, and
// repeating the tap keeps walking that way.
//
// It produces the SAME events as the swipes and the Fire's buttons rather than
// moving the cursor itself: one consumer (`applyEvent`), so paging can never
// come to mean two different things depending on how it was asked for.
//
// The dead zone is what keeps a tap ON the selection from stepping anywhere:
// without it, the half-pixel side of the marker you just chose would decide.
// Above the strip nothing is selectable at all — a brush against the brief must
// not change what is displayed.
static bool launchTap(int tx, int ty) {
    if (ty < STRIP_SEP) return false;
    if (gLaunchN <= 1) return false;                  // nothing to step through
    const int cur = (gCursor >= 0 && gCursor < MAX_LAUNCH) ? gStripX[gCursor] : -1;
    if (cur < 0) return false;                        // selection not on screen
    if (tx > cur + STRIP_TAP_DEAD)      applyEvent(UiEvent::NextItem);
    else if (tx < cur - STRIP_TAP_DEAD) applyEvent(UiEvent::PrevItem);
    else return false;
    return true;
}

#if SCE_INPUT_BUTTONS
// ---------------------------------------------------------- DEBUG overlay
// Held with A+C — the radar's diagnostic, brought over with the button bank.
// Everything a touch board reads on /config's diag line, on a board that has
// no touch panel: network, clock, what each source last did, and which build
// is running.
//
// A HELD overlay rather than a step of the view ring, deliberately: it is a
// diagnostic, not a destination. You cannot end up stranded in it, it cannot
// come up by accident on the ring, and letting go is the whole exit.
//
// A2.22: 13 rows, ONE drawString call site in ONE loop over a table. Written
// as thirteen calls, GCC 8.4 Xtensa is entitled to drop some of them, and a
// diagnostic screen missing a line it never mentions is worse than no
// diagnostic at all. `noinline` so `scripts/gates/check-a222.py` can watch it
// (pinned on space-fire, the env where the symbol exists).
static bool dbgHeld = false;

static void __attribute__((noinline)) drawDebug() {
    canvas.fillScreen(TH->bg);
    canvas.setTextSize(1);
    canvas.setTextDatum(textdatum_t::top_left);

    char v[14][44];
    int n = 0;

    // BILINGUAL LIKE EVERY OTHER SCREEN, and the radar's column discipline:
    // labels are SEVEN characters plus a space in both languages, because the
    // columns are what make this readable at 6x8. Section headings stay
    // ACCENT-FREE and uppercase — the row-vs-heading test below reads the
    // first two characters, and Font0 has no accented glyphs. `ip`, `config`,
    // `ssid`, `tle`, `lanc`, `passes`, `heap`, `build` and the SOURCES
    // heading are spelled identically in both languages: one literal, not an
    // sce::T() with two equal halves, which would only invite drift.
    snprintf(v[n++], 44, sce::T("NETWORK %s  rssi %d dBm",
                                "RESEAU  %s  rssi %d dBm"),
             WiFi.status() == WL_CONNECTED ? "STA" : "AP", (int)WiFi.RSSI());
    snprintf(v[n++], 44, "ip      %s", ipStr.c_str());
    // THE ADDRESS TO TYPE, spelled out: on this profile everything
    // configurable lives behind that URL, and you retype what you read.
    snprintf(v[n++], 44, "config  http://%s/config", ipStr.c_str());
    snprintf(v[n++], 44, "ssid    %s", WiFi.SSID().c_str());
    {
        // The clock AND its validity in one row: every view of this bin is
        // gated on clockOk(), so "NO SYNC" here explains five empty screens.
        char h[8] = "";
        if (clockOk()) {
            const time_t t = time(nullptr);
            struct tm g; gmtime_r(&t, &g);
            snprintf(h, sizeof(h), "%02d:%02dZ", g.tm_hour, g.tm_min);
        }
        snprintf(v[n++], 44, sce::T("clock   %s", "horloge %s"),
                 h[0] ? h : sce::T("NO SYNC", "NON SYNC"));
    }
    snprintf(v[n++], 44, "SOURCES");
    // Name, AGE and origin: "cache" with a three-week age and "net" with the
    // same age are two different diagnoses (no network vs Celestrak serving a
    // stale element set) — gTleFromNet exists for this row.
    //
    // THE AGE IS THE ELEMENT SET'S, not the download's. It used to be
    // `nowJd() - gTleFetchedJd`, and that variable holds two different things
    // by design: the fetch instant after a network fetch, the epoch after a
    // cache load (see tleLoadFromSd, where the substitution is what makes the
    // staleness test work at all). So the row read 0.0 d for EVERY fresh
    // fetch, including one that returned a three-week-old set — the single
    // case the row was added to expose. `gTle.epochJd` is the same quantity
    // in both branches, and it is the one accuracy depends on.
    if (gTleOk)
        snprintf(v[n++], 44, sce::T("tle     %s  %.1f d  %s",
                                    "tle     %s  %.1f j  %s"),
                 gTleName, nowJd() - gTle.epochJd,
                 gTleFromNet ? "net" : "cache");
    else
        snprintf(v[n++], 44, "tle     -");
    snprintf(v[n++], 44, "lanc    n=%d  %s", gLaunchN, gLaunchDiag);
    snprintf(v[n++], 44, sce::T("passes  %d found", "passes  %d trouve(s)"),
             gPassN);
    snprintf(v[n++], 44, sce::T("light   %s", "lumiere %s"),
             gLtrOk ? "ok" : "absent");
    snprintf(v[n++], 44, sce::T("RESOURCES    up %lus",
                                "RESSOURCES   up %lus"),
             (unsigned long)(millis() / 1000));
    snprintf(v[n++], 44, "heap    %u  min %u",
             (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
    // WHICH BUILD IS RUNNING — the 08-10 lesson: a flash that succeeded says
    // nothing about the binary that actually boots.
    snprintf(v[n++], 44, "build   %s  slot %s", sce::fw::sha8(), sce::fw::slot());

    // Section headings are the rows whose label starts with two capitals;
    // they get the accent so the eye finds the groups at once.
    constexpr int Y0 = 4, PITCH = 16;
    for (int i = 0; i < n; i++) {
        const bool head = (v[i][0] >= 'A' && v[i][0] <= 'Z' &&
                           v[i][1] >= 'A' && v[i][1] <= 'Z');
        canvas.setTextColor(head ? TH->accent : TH->txt1);
        canvas.drawString(v[i], 6, Y0 + i * PITCH);
    }
    canvas.setTextColor(TH->hint);
    canvas.drawString(sce::T("release A+C to go back",
                             "relacher A+C pour revenir"), 6, Y0 + n * PITCH + 2);
    canvas.pushSprite(0, 0);
}
#endif  // SCE_INPUT_BUTTONS

// =============================================================================
// Frame
// =============================================================================
static void drawFrame() {
#if SCE_INPUT_BUTTONS
    // Before everything: the overlay owns the screen while the chord is held.
    if (dbgHeld) { drawDebug(); return; }
#endif
    const double jd = nowJd();
    canvas.fillScreen(TH->bg);
    if (gModal != Modal::None) {
        if (gModal == Modal::PolarChart) drawPolarModal(jd);
        else                             drawSkyDome(jd);
        canvas.pushSprite(0, 0);
        return;
    }
    drawHeader();
    canvas.setTextDatum(textdatum_t::top_left);
    if (!clockOk()) {
        drawWaitingClock();
    } else {
        switch (gView) {
            case View::Iss:      drawIssView(jd);    break;
            case View::Passes:   drawPassesView(jd); break;
            case View::Moon:     drawMoonView(jd);   break;
            case View::Sky:      drawSkyView(jd);    break;
            default:             drawLaunchesView(); break;
        }
    }
    char msg[64];
    takeNetMsg(msg, sizeof(msg));      // expires it, and copies it, under lock
    if (msg[0]) drawFooter(msg, TH->accent2);
    canvas.pushSprite(0, 0);
}

// =============================================================================
// Events — ONE consumer, three producers (touch, buttons, dock)
// =============================================================================
static void startPassSearch(double jd) {
    if (!gTleOk) return;
    gPasses.begin(&gProp, jd, 48.0, cfg.lat, cfg.lon, cfg.altKm,
                  cfg.minPassEl, 30.0);
    gPassSearching = true;
    gPassN = 0;
    gPassSearchedJd = jd;
}

static void applyEvent(UiEvent e) {
    if (e == UiEvent::None) return;
    // The numeric id, not a name table: the trace is a developer's line and
    // spa::UiEvent is small and stable; the view NAME is the context that
    // makes a capture readable without the screen in front of you.
    sce::trace::log("ui", "evenement %d, vue %s, modal %d",
                    (int)e, spa::viewName(gView), (int)gModal);
    dockLastMs = millis();               // any manual action re-arms the dock
    switch (e) {
        case UiEvent::NextView:
            if (gModal != Modal::None) { gModal = Modal::None; break; }
            gView = spa::nextView(gView);
            gCursor = 0;
            break;
        // In the orbital modal the item cursor selects the PLANET: the same
        // gesture keeps the same meaning ("move along the list in front of
        // you") instead of the modal inventing a vocabulary of its own.
        case UiEvent::NextItem:
            if (gView == View::Sky) gSkySel = (gSkySel + 1) % (int)Planet::COUNT;
            else if (gView == View::Passes && gPassN > 0) gCursor = (gCursor + 1) % gPassN;
            else if (gView == View::Launches && gLaunchN > 0) gCursor = (gCursor + 1) % gLaunchN;
            break;
        case UiEvent::PrevItem:
            if (gView == View::Sky)
                gSkySel = (gSkySel + (int)Planet::COUNT - 1) % (int)Planet::COUNT;
            else if (gView == View::Passes && gPassN > 0) gCursor = (gCursor + gPassN - 1) % gPassN;
            else if (gView == View::Launches && gLaunchN > 0) gCursor = (gCursor + gLaunchN - 1) % gLaunchN;
            break;
        case UiEvent::Select:
            if (gView == View::Passes && gPassN > 0) gModal = Modal::PolarChart;
            break;
        case UiEvent::Back:
            gModal = Modal::None;
            break;
        case UiEvent::Refresh:
            netForce = true;
            if (gView == View::Passes || gView == View::Iss) startPassSearch(nowJd());
            setNetMsg(sce::T("refreshing...", "rafraichissement..."), false);
            break;
        case UiEvent::Settings:
            {   char u[64];
                snprintf(u, sizeof(u), "http://%s/config", ipStr.c_str());
                setNetMsg(u, false);   }
            break;
        default: break;
    }
    gRedraw = true;
}

// ---- touch -----------------------------------------------------------------
static int  touchStartX = 0, touchStartY = 0;
static bool touchDown = false;
static uint32_t touchMs = 0;
static bool touchLongFired = false;

static void handleTouch() {
    const auto t = M5.Touch.getDetail();
    const uint32_t now = millis();
    if (t.isPressed() && !touchDown) {
        touchDown = true; touchLongFired = false;
        touchStartX = t.x; touchStartY = t.y; touchMs = now;
        return;
    }
    if (t.isPressed() && touchDown && !touchLongFired &&
        now - touchMs >= sce::gesture::LONG_MS) {
        // Long press = forced refresh, and only if the finger stayed put.
        // The tolerance is the shared one: this bin allowed 20 px of drift
        // where the radar allows 12, which is a different gesture wearing the
        // same name.
        if (abs(t.x - touchStartX) < sce::gesture::LONG_SLOP_PX &&
            abs(t.y - touchStartY) < sce::gesture::LONG_SLOP_PX) {
            touchLongFired = true;
            applyEvent(UiEvent::Refresh);
        }
        return;
    }
    if (!t.isPressed() && touchDown) {
        touchDown = false;
        if (touchLongFired) return;
        const spa::Swipe s{ t.x - touchStartX, t.y - touchStartY };
        const UiEvent e = spa::swipeEvent(s);
        if (e != UiEvent::None) { applyEvent(e); return; }
        // A SWIPE THAT MEANS NOTHING HERE IS STILL A SWIPE. Downward maps to
        // no event on purpose — SceGuest owns that gesture — but SceGuest only
        // consumes it past 100 px, so a shorter drag used to fall through and
        // be read as a tap at the finger's ORIGIN: a half-hearted exit opened
        // a pass or picked a planet.
        if (spa::isSwipe(s)) return;
        // A TAP, routed by view. In a modal it leaves; on the pass list it
        // opens the row under the finger; on the sky it hits a planet, the
        // Sun or the scan strip. Anywhere else it does nothing at all — a
        // brush against the screen must not change what is displayed.
        if (gModal != Modal::None) { applyEvent(UiEvent::Back); return; }
        if (gView == View::Passes && gPassN > 0) {
            // Bounded on BOTH sides, and against the rows actually DRAWN.
            // Integer division truncates towards zero, so a tap on the column
            // header at y=26 gave row 0; and the list draws at most six rows
            // while MAX_PASSES is eight, so a tap on empty screen could open a
            // pass that is nowhere in front of the user (review 08-04).
            const int ry = touchStartY - 42;
            const int shown = gPassN < 6 ? gPassN : 6;
            if (ry >= 0) {
                const int row = ry / 30;
                if (row < shown) { gCursor = row; applyEvent(UiEvent::Select); }
            }
        } else if (gView == View::Launches) {
            // No re-arming or redraw here: launchTap goes through
            // applyEvent, which already does both for every producer.
            launchTap(touchStartX, touchStartY);
        } else if (gView == View::Sky) {
            // The hit test uses the SAME positions the frame drew, so a dot
            // is pressable exactly where it appears.
            if (skyTap(touchStartX, touchStartY, nowJd())) {
                dockLastMs = millis();
                gRedraw = true;
            }
        }
    }
}

#if SCE_INPUT_BUTTONS
// ---- buttons (Fire) ---------------------------------------------------------
// The whole navigation on a board with no touchscreen. Everything hard lives
// in sce::ButtonFsm (firmware/common/ButtonFsm.h), which is pure and natively
// tested; this is the glue that reads M5's button objects and hands the
// result to the shared consumer. ONE bank, not three machines: three
// independent FSMs is what used to fire TWO long actions on A+C, and none of
// them had the boot priming.
static sce::ButtonFsm btnFsm;

static void handleButtons() {
    const uint32_t now = millis();
    const bool lvl[sce::BTN_COUNT] = {
        M5.BtnA.isPressed(), M5.BtnB.isPressed(), M5.BtnC.isPressed()
    };
    // Any finger on any button restarts the dock clock — including holds and
    // chords that never become a UiEvent, so the ring cannot cycle away from
    // under a held overlay.
    if (lvl[0] || lvl[1] || lvl[2]) dockLastMs = now;
    // A+C = the debug overlay, for as long as it is held. Polled BEFORE
    // update() so the chord suppresses both buttons' own actions in the very
    // tick it is detected — otherwise letting go would also step the item
    // cursor, and holding would fire A's long press underneath the overlay.
    const bool chord = btnFsm.chord(sce::BTN_A, sce::BTN_C);
    if (chord != dbgHeld) { dbgHeld = chord; gRedraw = true; }
    if (dbgHeld) {
        // Live figures, THROTTLED (the radar's 08-04 lesson): a redraw on
        // every 10 ms pass meant a full-screen pushSprite plus a WiFi.RSSI()
        // IPC call at ~100 Hz for numbers that change at most once a second —
        // the screen opened to diagnose slowness was itself pinning the SPI
        // bus. 250 ms; enter/exit stays instant above.
        static uint32_t dbgDrawMs = 0;
        if (now - dbgDrawMs >= 250) { dbgDrawMs = now; gRedraw = true; }
        btnFsm.update(now, lvl, nullptr);   // keep debouncing, emit nothing
        return;
    }
    sce::BtnEvent ev{};
    if (btnFsm.update(now, lvl, &ev))
        applyEvent(spa::buttonEvent(ev));
}
#endif  // SCE_INPUT_BUTTONS

// =============================================================================
// Config: read, write, and the /config web page
// =============================================================================
// A CHECKBOX IS NOT ALWAYS "1". A bare atoi() works only because SceGuest
// happens to emit value='1' today; a form that sent "on", "true" or "yes" —
// the browser default for a valueless checkbox — would read every toggle as
// FALSE, silently. flight-radar records that exact outage (27 July). Accept
// what a web form can actually send.
// One answer for the whole project (firmware/common/CfgBool.h). The version
// that lived here tested the FIRST CHARACTER, so `off` read as TRUE — it
// starts like `on`. Nothing sent `off` from the form, so it never fired; a
// hand-edited yaml would have.
static uint8_t webBool(const char* v) { return sce::webBool(v) ? 1 : 0; }

// Raised by our OWN yaml parser when it sees `tz_offset_h`, so the companion
// fallback below knows to stay out of the way. Reset at the top of loadConfig:
// a reload after a card insertion has to re-decide, not inherit the last run's
// verdict.
static bool tzFromOwnYaml = false;

// True while loadConfig replays the yaml: cfgApply is shared between the web
// form and the boot replay, and cross-key SIDE EFFECTS (a hand-set brightness
// disabling the auto) only make sense for a HAND. A replayed file is not a
// hand.
static bool gCfgLoading = false;

// Returns whether the key was RECOGNISED — not whether the value was good.
// loadConfig tallies the answers into one trace line: a yaml full of unknown
// keys (a typo, a stale schema) used to be indistinguishable from a yaml that
// loaded, because every unmatched key fell off the end of this chain silently.
static bool cfgApply(const char* k, const char* v) {
    bool known = true;
    if      (!strcmp(k, "lat"))           cfg.lat = atof(v);
    else if (!strcmp(k, "lon"))           cfg.lon = atof(v);
    else if (!strcmp(k, "alt_m"))         cfg.altKm = atof(v) / 1000.0f;
    else if (!strcmp(k, "tz_offset_h"))   { tzFromOwnYaml = true; cfg.tzOffsetH = atof(v); }
    else if (!strcmp(k, "norad")) {
        const uint32_t was = cfg.norad;
        cfg.norad = strtoul(v, nullptr, 10);
        if (cfg.norad != was) {              // a NEW object: nothing we hold
            gTleOk = false;                  // applies to it any more
            gTleFetchedJd = 0.0;
            gPassN = 0; gPassSearching = false; gPassSearchedJd = 0.0;
            netForce = true;
        }
    }
    // ACCEPTS THE OLD KEY. The setting was named after Launch Library when
    // that was the only source; renaming it without an alias would have made
    // every card already in the field fall back to the default in silence.
    else if (!strcmp(k, "launch_poll_min") ||
             !strcmp(k, "ll2_poll_min"))  cfg.launchPollMin = atoi(v);
    else if (!strcmp(k, "theme"))         cfg.theme = webBool(v);
    else if (!strcmp(k, "auto_night"))    cfg.autoNight = webBool(v);
    else if (!strcmp(k, "auto_bright"))   cfg.autoBright = webBool(v);
    else if (!strcmp(k, "metric"))        cfg.metric = webBool(v);
    else if (!strcmp(k, "bright")) {
        // Setting it BY HAND disables the auto — without this the slider was
        // overwritten within the second by the sensor. BY HAND ONLY: this
        // same function replays the yaml at boot, where `bright` still holds
        // the compiled default, so the card's own `bright:` line differed and
        // KILLED the `auto_bright: 1` loaded two lines earlier. Auto
        // brightness silently turned itself off on every reboot while the
        // yaml said on.
        if (!gCfgLoading && cfg.bright != (uint8_t)atoi(v)) cfg.autoBright = 0;
        cfg.bright = atoi(v);
    }
    else if (!strcmp(k, "dock_s"))        cfg.dockS = atoi(v);
    else if (!strcmp(k, "min_pass_el"))   cfg.minPassEl = atof(v);
    else if (!strcmp(k, "servo"))         cfg.servo = webBool(v);
    else if (!strcmp(k, "servo_az"))      cfg.servoAz = atof(v);
    else                                  known = false;
    // CLAMPS applied at the point of entry, so no other code has to wonder.
    // launch_poll_min's floor is the SLOWEST source's published throttle
    // (Launch Library: 15 requests an hour): letting a user set 1 would get
    // the bin rate-limited the moment the primary source failed over, and the
    // symptom — an empty launch list — looks like a bug in the parser.
    if (cfg.launchPollMin < 15)  cfg.launchPollMin = 15;
    if (cfg.launchPollMin > 240) cfg.launchPollMin = 240;
    if (cfg.dockS > 120)      cfg.dockS = 120;
    if (cfg.tzOffsetH < -14.0f) cfg.tzOffsetH = -14.0f;
    if (cfg.tzOffsetH >  14.0f) cfg.tzOffsetH =  14.0f;
    if (cfg.minPassEl < 0)  cfg.minPassEl = 0;
    if (cfg.minPassEl > 60) cfg.minPassEl = 60;
    if (cfg.servoAz < 0)    cfg.servoAz = 0;
    if (cfg.servoAz > 359)  cfg.servoAz = 359;
    if (cfg.bright < 10)  cfg.bright = 10;
    if (cfg.bright > 255) cfg.bright = 255;
    if (cfg.lat < -90)  cfg.lat = -90;
    if (cfg.lat >  90)  cfg.lat =  90;
    if (cfg.lon < -180) cfg.lon = -180;
    if (cfg.lon >  180) cfg.lon =  180;
    return known;
}

static void loadConfig() {
    // Idempotent and cheap (two SD.exists()) — called here rather than at
    // each mount call site so a card inserted mid-session (retry, SdWatch
    // reinsertion) is covered the same as the initial boot mount.
    sce::migrateSdRoot();
    tzFromOwnYaml = false;
    gCfgLoading = true;
    // The CANONICAL parser (A2.23): one decoder for the whole project.
    // Tallied so the trace can say in ONE line whether the yaml matched the
    // schema — each unknown key is also named as it passes, because "1
    // inconnue" without the name still costs a reflash to find the typo.
    struct Tally { int known = 0, unknown = 0; } tally;
    sce::SceGuest::yamlForEach(CFG_PATH, false,
        [](void* u, const char* /*section*/, const char* key, const char* val) {
            Tally* t = static_cast<Tally*>(u);
            if (cfgApply(key, val)) t->known++;
            else { t->unknown++; sce::trace::log("cfg", "cle inconnue : %s", key); }
        }, &tally);
    sce::trace::log("cfg", "yaml %s : %d cle(s) reconnue(s), %d inconnue(s)",
                    CFG_PATH, tally.known, tally.unknown);
    gCfgLoading = false;
    // THE LANGUAGE IS THE COMPANION'S, not ours. This bin kept its own `lang`
    // key, which flight-radar's config comment explicitly argues against: two
    // language settings that can disagree are not a setting. One robot, one
    // language (08-04).
    sce::loadCompanionLang<sce::SceGuest>();
    // ...AND THE TIME ZONE, but only as a FALLBACK (user 08-05: "set the UTC
    // offset, go to a guest bin, and the clock loses it"). Our own key wins
    // when we have one: measured on the robot, the companion holds 0.00 —
    // nobody has ever turned that dial — while this bin's yaml says 4.0, so
    // "the companion always wins" would have replaced a missing setting with a
    // broken one. This way a card that never mentioned the offset inherits the
    // robot's instead of sitting on UTC, and nothing that already works moves.
    if (!tzFromOwnYaml) sce::companionTzOffsetH<sce::SceGuest>(cfg.tzOffsetH);
}

static bool saveConfig() {
    // Every exit is traced: /config answers "save failed" without saying WHY,
    // and the three reasons (no card, open refused, write error) have three
    // different remedies.
    if (!sdOk) { sce::trace::log("cfg", "sauvegarde ECHEC : pas de carte"); return false; }
    File f = SD.open(CFG_PATH, FILE_WRITE);
    if (!f) { sce::trace::log("cfg", "sauvegarde ECHEC : open %s", CFG_PATH); return false; }
    f.printf("# space — StackChan-Companion guest bin (docs/guests/SPACE.md)\n");
    f.printf("lat: %.4f\n",           cfg.lat);
    f.printf("lon: %.4f\n",           cfg.lon);
    f.printf("alt_m: %.0f\n",         cfg.altKm * 1000.0f);
    // Only when the key is the bin's own (yaml or /config): unconditional,
    // this SEALED a companion-inherited offset into the yaml on the first
    // unrelated save — the "bin key wins" precedence defeated by its own
    // persistence, and the robot stuck on the old zone with nothing to say why.
    if (tzFromOwnYaml) f.printf("tz_offset_h: %.2f\n", cfg.tzOffsetH);
    f.printf("norad: %lu\n",          (unsigned long)cfg.norad);
    f.printf("launch_poll_min: %u\n", cfg.launchPollMin);
    f.printf("min_pass_el: %.0f\n",   cfg.minPassEl);
    f.printf("theme: %u\n",           cfg.theme);
    f.printf("auto_night: %u\n",      cfg.autoNight);
    // saveConfig() TRUNCATES the file and rewrites it from this list, so a key
    // missing HERE is a key DESTROYED: auto_bright could not be persisted at
    // all, and a hand-edited `auto_bright: 0` was wiped by the first save from
    // the web page (review 08-05).
    f.printf("auto_bright: %u\n",     cfg.autoBright);
    f.printf("metric: %u\n",          cfg.metric);
    f.printf("bright: %u\n",          cfg.bright);
    f.printf("dock_s: %u\n",          cfg.dockS);
    f.printf("servo: %u\n",           cfg.servo);
    f.printf("servo_az: %.0f\n",      cfg.servoAz);
    const bool bad = f.getWriteError();
    f.close();
    // A FULL OR WRITE-PROTECTED CARD lets SD.open(FILE_WRITE) truncate the file
    // and succeed: without this test the page reports "Saved" over settings
    // that no longer exist. ha-remote paid for that on 07-28.
    sce::trace::log("cfg", "sauvegarde %s : %s", CFG_PATH,
                    bad ? "ECHEC ecriture" : "ok");
    return !bad;
}

static String settingGet(const char* k) {
    char b[24];
    if (!strcmp(k, "lat"))           { snprintf(b, sizeof(b), "%.4f", cfg.lat);   return b; }
    if (!strcmp(k, "lon"))           { snprintf(b, sizeof(b), "%.4f", cfg.lon);   return b; }
    if (!strcmp(k, "alt_m"))         { snprintf(b, sizeof(b), "%.0f", cfg.altKm * 1000); return b; }
    if (!strcmp(k, "tz_offset_h"))   { snprintf(b, sizeof(b), "%.2f", cfg.tzOffsetH); return b; }
    if (!strcmp(k, "norad"))         return String((unsigned long)cfg.norad);
    if (!strcmp(k, "launch_poll_min")) return String(cfg.launchPollMin);
    if (!strcmp(k, "min_pass_el"))   { snprintf(b, sizeof(b), "%.0f", cfg.minPassEl); return b; }
    if (!strcmp(k, "theme"))         return String(cfg.theme);
    if (!strcmp(k, "auto_night"))    return String(cfg.autoNight);
    if (!strcmp(k, "auto_bright"))   return String(cfg.autoBright);
    if (!strcmp(k, "metric"))        return String(cfg.metric);
    if (!strcmp(k, "bright"))        return String(cfg.bright);
    if (!strcmp(k, "dock_s"))        return String(cfg.dockS);
    if (!strcmp(k, "servo"))         return String(cfg.servo);
    if (!strcmp(k, "servo_az"))      { snprintf(b, sizeof(b), "%.0f", cfg.servoAz); return b; }
    // READ-ONLY DIAGNOSTIC. This bin has no telemetry endpoint, so when the
    // launch list stayed empty the only place the reason existed was a footer
    // line on the robot's own screen — unreadable from anywhere else. One
    // string on /config costs nothing and makes the next failure answerable
    // without a reflash (the radar earned its RESEAU tab the same way).
    if (!strcmp(k, "diag")) {
        char d[80];
        // `ltr` is reported because `auto_bright` on a board with no sensor is
        // a toggle that legitimately does nothing, and a setting that does
        // nothing without saying so is the kind of thing people reflash over.
        snprintf(d, sizeof(d), "tle=%s ltr=%s lanc=%d | %s",
                 gTleOk ? "ok" : "-", gLtrOk ? "ok" : "-", gLaunchN, gLaunchDiag);
        return String(d);
    }
    return String();
}

static void setupSettingsPage() {
    guest.addSetting("lat",           sce::T("Latitude", "Latitude"),                sce::SceGuest::Num, -90, 90);
    guest.addSetting("lon",           sce::T("Longitude", "Longitude"),               sce::SceGuest::Num, -180, 180);
    guest.addSetting("alt_m",         sce::T("Altitude (m)", "Altitude (m)"),            sce::SceGuest::Num, 0, 5000);
    guest.addSetting("tz_offset_h",   sce::T("UTC offset (h)", "Décalage UTC (h)"),        sce::SceGuest::Num, -14, 14);
    guest.addSetting("norad",         sce::T("Satellite (NORAD)", "Satellite (NORAD)"),       sce::SceGuest::Num, 1, 99999);
    guest.addSetting("min_pass_el",   sce::T("Min pass elevation", "Élévation mini passage"),  sce::SceGuest::Num, 0, 60);
    guest.addSetting("launch_poll_min", sce::T("Launch poll (min)", "Période lancements (min)"), sce::SceGuest::Num, 15, 240);
    guest.addSetting("dock_s",        sce::T("Auto cycle (s, 0=off)", "Cycle auto (s, 0=off)"),   sce::SceGuest::Num, 0, 120);
    guest.addSetting("bright",        sce::T("Brightness", "Luminosité"),              sce::SceGuest::Num, 10, 255);
    guest.addSetting("theme",         sce::T("Night theme", "Thème nuit"),              sce::SceGuest::Bool);
    guest.addSetting("auto_night",    sce::T("Auto theme at dusk", "Thème auto au coucher"),   sce::SceGuest::Bool);
    guest.addSetting("auto_bright",   sce::T("Auto brightness", "Luminosité auto"),       sce::SceGuest::Bool);
    guest.addSetting("metric",        sce::T("Metric units", "Unités métriques"),        sce::SceGuest::Bool);
#if SCE_HAS_SERVO
    guest.addSetting("servo",         sce::T("Head follows the satellite", "La tête suit le satellite"), sce::SceGuest::Bool);
    guest.addSetting("servo_az",      sce::T("Robot faces (deg, 0=N)", "Le robot regarde (deg, 0=N)"), sce::SceGuest::Num, 0, 359);
#endif
    guest.addSetting("diag",          sce::T("Diagnostic (read-only)", "Diagnostic (lecture)"),    sce::SceGuest::Text);
    guest.settingGet = settingGet;
    guest.settingSet = [](const char* k, const String& v) {
        if (!strcmp(k, "diag")) return;      // read-only: never written back
        cfgApply(k, v.c_str());
    };
    guest.onSettingsSaved = []() -> bool {
        // One line marks the save itself; saveConfig() below traces its own
        // outcome, so the pair brackets the whole persist.
        sce::trace::log("cfg", "reglages recus via /config");
        TH = &THEMES[cfg.theme ? 1 : 0];
        // Only when the sensor is NOT in charge: applying the manual value
        // under auto dimmed the screen, and the sensor loop's own hysteresis
        // (reapply on a >6 change) never restored it — the computed target
        // had not changed. The loop's `lastB` is reset below when auto
        // toggles, so the sensor level comes back on the next second.
        if (!cfg.autoBright) M5.Display.setBrightness(cfg.bright);
        gAutoBrightReset = true;
        gPassSearchedJd = 0.0;          // the observer may have moved
        gRedraw = true;
#if SCE_HAS_SERVO
        // begin() BLOCKS ~1.8 s: never in this HTTP handler, loop() does it.
        if (cfg.servo) gHeadInitReq = true;
#endif
        return saveConfig();
    };
}

// =============================================================================
// setup / loop
// =============================================================================
void setup() {
    auto mcfg = M5.config();
    M5.begin(mcfg);
    Serial.begin(115200);
    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE);

    SPI.begin(SCE_SD_SCK, SCE_SD_MISO, SCE_SD_MOSI, SCE_SD_CS);
    sdOk = SD.begin(SCE_SD_CS, SPI, SCE_SD_HZ);
    loadConfig();                       // before the first pixel: it carries `lang`
    M5.Display.drawString(sce::T("space: starting...", "space : demarrage..."), 10, 10);

#if SCE_COMPANION
    if (sdOk) {
        sce::SceGuest::applyLobbyTheme("space");
        checkSDUpdater(SD, String("/companion.bin"), 2500, 4);
    }
#endif
    Serial.printf("[space] SD %s\n", sdOk ? "montee" : "ECHEC montage");
    // Said BEFORE the instrument draws anything: without a card the observer
    // silently falls back to the compiled position, and a sky for the wrong
    // place looks exactly like a sky for the right one. A card inserted HERE
    // has to be read from scratch — `loadConfig()` already ran against no card
    // and loaded nothing, so its lat/lon/tz/lang would stay at the defaults
    // until the next boot, which would make the retry a lie.
    // The screen is SceGuest's; what a missing card costs THIS bin is ours.
    // The position is formatted from the live config and never written out as
    // text: a hard-coded "Paris" would keep claiming Paris the day the
    // compiled default changes, and this screen exists to be believed.
    //
    // `%+.4g` and not `%+.2g` for the zone: two significant digits round
    // UTC+12.75 to 13, and a screen whose whole job is to be believed cannot
    // invent a time zone.
    if (!sdOk) {
        char pos[56];
        snprintf(pos, sizeof(pos), "   %.4f%c  %.4f%c   UTC%+.4g",
                 cfg.lat < 0 ? -cfg.lat : cfg.lat, cfg.lat < 0 ? 'S' : 'N',
                 cfg.lon < 0 ? -cfg.lon : cfg.lon, cfg.lon < 0 ? 'W' : 'E',
                 (double)cfg.tzOffsetH);
        const char* why[] = {
            sce::T("The observer stays at the COMPILED position:",
                   "L'observateur reste a la position COMPILEE :"),
            pos,
            sce::T("Passes, sky and Moon are computed for THAT place",
                   "Passages, ciel et Lune sont calcules pour CE lieu"),
            sce::T("- not for where the robot actually is.",
                   "- pas pour l'endroit ou est le robot."),
            sce::T("No cache either: the ISS needs the network first.",
                   "Pas de cache non plus : l'ISS attend le reseau."),
        };
        // A card found here has to be read from SCRATCH: loadConfig() already
        // ran against no card and loaded nothing, so its lat/lon/tz/lang would
        // stay at the defaults until the next boot — which would make the
        // retry a lie.
        if (guest.noSdNotice([] { SD.end();
                                  sdOk = SD.begin(SCE_SD_CS, SPI, SCE_SD_HZ);
                                  return sdOk; },
                             why, (int)(sizeof(why) / sizeof(why[0]))))
            loadConfig();
    }

    // Seeded HERE and not at the mount above: the notice offers a RETRY
    // that mounts the card, and a watcher seeded before it would keep
    // the pre-retry answer -- then unmount a working card to "discover"
    // it (firmware/common/SdWatch.h).
    gSdWatch.begin(sdOk, SCE_SD_CS, SCE_SD_HZ);

    guest.appName = "space";
    // The exit gesture goes back to the companion. With no companion there is
    // nothing to go back TO, so the swipe would open a confirmation whose
    // "yes" leads nowhere. It is also dead weight on a board with no touch
    // panel, which polls it every pass to learn nothing.
    guest.setSwipeExit(SCE_COMPANION != 0);
    setupSettingsPage();
    ipStr = guest.begin(SCE_WIFI_SSID, SCE_WIFI_PASS);
    Serial.printf("[space] WiFi %s ip=%s\n",
                  WiFi.status() == WL_CONNECTED ? "STA" : "AP/echec", ipStr.c_str());
    // UTC everywhere internally; the local offset is applied at DISPLAY time
    // only (cfg.tzOffsetH). Feeding the offset to configTime instead would
    // put local time into every astronomical routine — the classic way to be
    // wrong by a whole time zone in a Julian date.
    configTime(0, 0, "pool.ntp.org");

    M5.Display.setBrightness(cfg.bright);
    // Probed once. `auto_bright` on a board that has no sensor must not look
    // like a setting that does nothing: gLtrOk is what the loop and /config
    // both read.
    gLtrOk = gLtr.begin();
    Serial.printf("[space] LTR-553 %s\n", gLtrOk ? "present" : "absent");
#if SCE_HAS_SERVO
    // Lazy: the neck is powered only when the option is on. After the LTR
    // probe, so the internal bus has been used once before Wire1 opens on
    // the same pins (the radar's order).
    if (cfg.servo) gHead.begin();
    Serial.printf("[space] servo %s, face au %.0f deg\n",
                  cfg.servo ? "ON" : "off", cfg.servoAz);
#endif
    TH = &THEMES[cfg.theme];
    canvas.setColorDepth(16);
    canvas.setPsram(true);
    canvas.createSprite(SCR_W, SCR_H);

    // The cache before the network: the bin must be useful the instant it
    // starts, and on a card with a fresh TLE it is.
    launchLoadFromSd();             // show the last known list at once
    if (tleLoadFromSd()) {
        tleAdoptStaged();               // no task is running yet: adopt now
        Serial.printf("[space] TLE cache: %s epoque %.3f\n", gTleName, gTle.epochJd);
    }

    gMtx = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(netTask, "space-net", 16384, nullptr, 1, nullptr, 0);
    // Window = ONE longest request (8 s connect + 15 s read, cf. httpGet);
    // httpGet refuses new requests once the guard is up, so a fetch CHAIN
    // cannot outlive it. SceGuest parks/releases around the reflash.
    netGuard.windowMs = 25000;
    guest.netGuard = &netGuard;
    dockLastMs = millis();
}

void loop() {
    M5.update();
    guest.update();                     // POST /api/bins/stop → companion

    // THE CARD, WATCHED. Until this call the boot mount stood for the whole
    // run: a card inserted afterwards was never seen, so settings silently did
    // not persist for the session while /config went on offering to save them.
    // On insertion the config is re-read from scratch, for the same reason the
    // boot notice's retry does it — loadConfig() ran against no card and
    // loaded nothing, so lat/lon/tz/lang would sit at the compiled defaults
    // until the next reboot, and this instrument is only worth believing if it
    // knows where it is.
    if (gSdWatch.update([] { loadConfig(); })) {
        sdOk = gSdWatch.mounted();
        Serial.printf("[space] SD %s\n",
                      sdOk ? "INSEREE : configuration relue"
                           : "RETIREE : reglages non persistes");
    }

#if SCE_INPUT_BUTTONS
    handleButtons();   // no touchscreen: the three buttons ARE the navigation
#else
    // A frame eaten by SceGuest's exit modal must also RESET our own gesture
    // state: skipping handleTouch left `touchDown` true with a press origin
    // from before the modal, so the next release was decoded as a swipe that
    // nobody made and silently moved a cursor (review 08-04).
    if (guest.consumedTouch()) touchDown = false;
    else                       handleTouch();
#endif

    serviceSdWrites();              // the bus is free HERE, and only here

    // --- Adopt a freshly fetched element set, BETWEEN frames and between
    // search slices: the one instant at which nothing is reading it.
    if (tleAdoptStaged()) {
        gPassSearchedJd = 0.0;          // the orbit moved: redo the passes
        sTrkJd = 0.0;                   // ...and the ground track with it
        gPassSearching = false;
        gPassN = 0;
        // ...and a polar chart open on a pass that no longer exists would draw
        // its rings and nothing else, with no explanation. Close it: the list
        // behind it is about to be recomputed.
        if (gModal == Modal::PolarChart) gModal = Modal::None;
        gRedraw = true;
        Serial.printf("[space] TLE adopte : %s epoque %.3f\n",
                      gTleName, gTle.epochJd);
    }

    // --- The pass search, SLICED. 40 SGP4 samples per pass through loop()
    // keeps each visit near a millisecond, so the frame budget and the touch
    // polling never notice a search running (SPACE.md trap 2).
    if (gPassSearching) {
        if (gPasses.step(40)) {
            gPassSearching = false;
            gPassN = gPasses.count();
            for (int i = 0; i < gPassN; i++) gPassList[i] = gPasses.at(i);
            gRedraw = true;
            Serial.printf("[space] %d passage(s) trouve(s)\n", gPassN);
        }
    } else if (gTleOk && clockOk()) {
        // Re-run once every 6 h, or when the element set / observer changed.
        const double jd = nowJd();
        // Re-search every six hours OR as soon as the list has been spent:
        // the second condition is what stops a finished pass being displayed
        // as the next one for the rest of the window.
        if (gPassSearchedJd == 0.0 || jd - gPassSearchedJd > 0.25 ||
            (gPassN > 0 && firstUpcomingPass(jd) < 0))
            startPassSearch(jd);
    }

    // --- Dock: the third producer of NextView, after touch and buttons.
    if (cfg.dockS > 0 && gModal == Modal::None) {
        const uint32_t n = millis();
        if (n - dockLastMs >= (uint32_t)cfg.dockS * 1000u) applyEvent(UiEvent::NextView);
    }

    // --- Auto brightness (LTR-553), the same curve the radar uses: the
    // calibration lives in firmware/common/Ltr553.h, not in three copies.
    // ONCE A SECOND, and that is a timer and not a comment: this block sits
    // before the draw throttle, so it ran on every loop() pass — about a
    // hundred reads a second, on the bus that also carries the touch panel and
    // the PMIC, for a part whose conversion rate is 500 ms (review 08-05).
    // The hysteresis on top is A2.2: setBrightness is a PMIC transaction.
    static uint32_t ltrMs = 0;
    static int      ltrLastB = -1;
    // A settings save may have changed who is in charge of the backlight;
    // forgetting the hysteresis forces the next sensor pass to reapply, so
    // toggling auto back on restores the sensor level within a second instead
    // of waiting for the room to change by more than the dead band.
    if (gAutoBrightReset) { gAutoBrightReset = false; ltrLastB = -1000; }
    if (cfg.autoBright && gLtrOk && millis() - ltrMs >= 1000) {
        ltrMs = millis();
        const int32_t v = gLtr.visible();
        if (v >= 0) {
            const int b = sce::ltr553::brightnessFrom(v);
            if (abs(b - ltrLastB) > 6) { ltrLastB = b; M5.Display.setBrightness((uint8_t)b); }
        }
    }

#if SCE_HAS_SERVO
    // --- The head follows the satellite while it is above the horizon
    // (option `servo`). ONCE A SECOND: the ISS crosses at most a degree or
    // two of sky a second even overhead, and each setpoint lasts 900 ms, so a
    // faster loop would only add bus traffic. Below the horizon, with a stale
    // or missing element set, or with no clock, the head goes home and its
    // torque is released — it never points at a position it cannot vouch for
    // (SPACE.md trap 3). Turning the option off also sends it home.
    if (gHeadInitReq) { gHeadInitReq = false; gHead.begin(); }
    static uint32_t headMs = 0;
    if (gHead.ready() && millis() - headMs >= 1000) {
        headMs = millis();
        bool up = false;
        spc::headtrack::Pose pose = spc::headtrack::home();
        if (cfg.servo && gTleOk && clockOk()) {
            const double jd = nowJd();
            if (!tleStale(jd)) {
                const StateVector sv = gProp.propagateJd(jd);
                if (sv.ok) {
                    const LookAngle la = lookAngle(sv.x, sv.y, sv.z, jd,
                                                   cfg.lat, cfg.lon, cfg.altKm);
                    if (la.elDeg > 0.0) {
                        pose = spc::headtrack::aim((float)la.azDeg, (float)la.elDeg,
                                                   cfg.servoAz);
                        up = true;
                    }
                }
            }
        }
        static bool wasUp = false;
        if (up != wasUp) {
            wasUp = up;
            Serial.printf("[space] tete : %s\n", up ? "suit le satellite" : "au repos");
        }
        gHead.moveTo(pose.yaw, pose.pitch, 900);
        gHead.service(millis(), true);   // released 1.5 s after the last move
    }
#endif

    // --- Auto night theme, on the observer's real sunset (not a fixed hour).
    if (cfg.autoNight && clockOk()) {
        static uint32_t lastCheck = 0;
        if (millis() - lastCheck > 60000UL) {
            lastCheck = millis();
            // `sce::isNight` is the project's ONE definition of night (NOAA,
            // -0.833 deg, natively tested) — this bin had its own -6 deg civil
            // twilight test from a different header, so the same robot could
            // disagree with itself about dusk depending on which bin was up.
            const bool night = sce::isNight(cfg.lat, cfg.lon, time(nullptr));
            // A MANUALLY CHOSEN night theme SURVIVES daylight. Assigning
            // THEMES[night] outright overrode cfg.theme, so a user who picked
            // Night watched it revert every morning and fought the 60 s poll.
            // Same semantics as the radar's `theme | 1`.
            const Theme* want = &THEMES[(cfg.theme || night) ? 1 : 0];
            if (want != TH) { TH = want; gRedraw = true; }
        }
    }

    // --- 2 Hz is the tempo of this bin: the ISS moves 15 km in that time,
    // which is half a pixel on the map, and the launch countdown needs a
    // second's resolution. Nothing here justifies 30 fps.
    static uint32_t lastDraw = 0;
    if (gRedraw || millis() - lastDraw > 500) {
        lastDraw = millis();
        gRedraw = false;
        drawFrame();
    }
    delay(10);
}
