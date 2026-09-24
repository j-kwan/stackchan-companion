// =============================================================================
// ha-remote — GUEST .bin: Home Assistant remote control for StackChan K151
// =============================================================================
// DIRECTION OF THE RELATION. Here the robot is the CLIENT: it reads the Home
// Assistant entities and calls its services. The OPPOSITE — the companion
// exposed to HA, which then drives the robot — is described in
// docs/integrations/HOMEASSISTANT.md. Both exist, do not mix them up.
//
// ── THE SCREENS ─────────────────────────────────────────────────────────
//   HOME      four cards (covers, lights, switches, cameras), each with its
//             icon and an ACTIVE / TOTAL counter, plus the state of the
//             link. Swiping RIGHT opens the settings, swiping DOWN hands
//             control back to the companion (SceGuest gesture).
//   LIST      one entity at a time, swipe ←/→ to change. The controls follow
//             the domain AND the CAPABILITIES declared by the entity: a
//             cover with no position feedback has no shutter pane, a
//             monochrome lamp has no palette.
//   CAMERA    full-screen image, refreshed on its own.
//
// No "whole house" bulk action: a Home Assistant GROUP already is an
// ordinary `cover.*` entity in the list, and duplicating it on the robot
// side would have ignored the user's own configuration.
//
// ── WHEN THERE ARE MORE ENTITIES THAN SEATS ─────────────────────────────
// The table holds `MAX_ENT` and a real installation serves several hundred.
// Which ones stay is a DECISION (`entsel.h`, pure and tested natively):
// pinned by the user first, then the ones that answer, and equal shares
// between the four categories so that no card is emptied by the order in
// which Home Assistant happened to serve its list. What was left out is
// SAID — home counter, per-card "+n", settings panel and serial log: a cap
// that truncates in silence reads as "everything is here".
//
// ── ARCHITECTURE ────────────────────────────────────────────────────────
// Two tasks, one rule: netTask talks to the network, loop() talks to the
// screen.
//
//   netTask (core 0, 20 KB stack) — SOLE owner of the HTTP sockets, and the
//     only one decoding images. A request blocks for several seconds; run
//     from loop() it would starve the touch input and the SceGuest web
//     server (diagnosed 07-25).
//   loop() — touch, redraw, SD writes. NEVER does HTTP: it POSTS requests
//     (action queue, `pollNow`, `camReq`) and reads the shared state under
//     `gMtx`.
//
// `gMtx` is RECURSIVE: a touch gesture runs from end to end under the lock
// while calling `postAction()`, which takes it too. Without that, netTask
// could rewrite the entity table between the moment the finger designates a
// device and the moment the order leaves — and it was the wrong cover that
// closed.
//
// ── NETWORK PACING ──────────────────────────────────────────────────────
// The full catalog (`/api/states`) only serves to DISCOVER which entities
// exist: it weighs hundreds of KB, takes about fifteen seconds, and is
// therefore read only at startup and on demand. The STATE of the entity
// being watched is re-read one at a time (`/api/states/<id>`), at 1 Hz and
// at 400 ms during the 10 s that follow an action. A user press PREEMPTS a
// discovery in progress: it will be redone.
//
// The JSON documents are filtered and allocated in PSRAM — the internal heap
// is shared with WiFi and TLS, and allocating tens of KB there several times
// per second fragments it until it fails silently.
//
// Configuration: /stackchan-companion/ha-remote.yaml (template in sdcard/),
// editable from the companion console, from the bin's own web page (form
// rendered by SceGuest) or from the on-screen panel.
// Full documentation: docs/guests/HA-REMOTE.md
// =============================================================================

#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>
#include <M5StackUpdater.h>
#include "../common/CfgBool.h"  // ONE definition of "true"
#include "../common/SdPins.h"   // ...and ONE description of the SD wiring
#include "../common/SdRoot.h"   // one-time /stackchan-eyes -> /stackchan-companion move
#include "../common/PsJson.h"   // shared ArduinoJson PSRAM allocator
#include "../common/I18n.h"     // sce::T(en, fr) — bilingual UI, English first
#include "../common/Gesture.h"  // ONE swipe classifier, shared with the others
#include "../common/SdWatch.h"  // the card, watched and not only mounted
#include "../common/Ltr553.h"   // ambient light: ONE calibration, three readers

// The card watcher — see the note in SdWatch.h. This bin keeps host and
// token on the card and nowhere else, so a card that appears mid-run is
// exactly the event it most wants to notice.
static sce::SdWatch gSdWatch;

// Ambient light. PROBED at boot rather than assumed from a build flag:
// `gLtrOk` is what the option is worth on THIS board, so `auto_bright` on a
// unit whose sensor is absent or dead is not a toggle that silently does
// nothing. Same reasoning as the space bin.
static sce::ltr553::Lite gLtr;
static bool gLtrOk = false;
// A settings save may have changed who owns the backlight; clearing the
// hysteresis makes the next sensor pass reapply, so switching auto back on
// restores the sensor level within a second instead of waiting for the room
// to change by more than the dead band.
static volatile bool gAutoBrightReset = false;
#include "entsel.h"             // WHICH entities survive the table (pure, tested)
#include "../../src/guest/SceGuest.h"

// ----------------------------------------------------------------- config
static constexpr const char* CFG_PATH = "/stackchan-companion/ha-remote.yaml";

struct Config {
    char     host[48]  = "";       // Home Assistant IP or hostname
    uint16_t port      = 8123;
    bool     ssl       = false;    // https (certificate NOT verified: LAN)
    char     token[256] = "";      // long-lived access token (HA profile)
    uint32_t pollS     = 0;        // periodic re-DISCOVERY, 0 = never
                                   // (startup + REFRESH, in the settings
                                   // panel reached by swiping →);
                                   // the state of the displayed entity has
                                   // its own tracking, that one permanent
    uint8_t  bright    = 100;      // backlight (10..255)
    // OFF by default, where flight-radar and space have it ON. Not an
    // oversight and not a copy that drifted: it is NEW here, on a screen
    // held in the hand, and a remote that starts dimming by itself the day
    // it is updated is a change nobody asked for. One checkbox turns it on.
    uint8_t  autoBright = 0;       // follow the LTR-553, if the board has one
    // Entities the user wants KEPT no matter what, comma-separated. Only ever
    // matters when Home Assistant serves more than the table holds — see
    // `entsel.h`. A token is matched as a PREFIX, so "cover." means "all my
    // covers" without listing them one by one.
    char     pins[192] = "";
};
static Config cfg;

// -------------------------------------------------------------- entities
// Controllable categories. The order drives the home screen.
enum Cat { CAT_COVER = 0, CAT_LIGHT, CAT_SWITCH, CAT_CAMERA, CAT_N };
static const char* CAT_DOMAIN[CAT_N] = { "cover.", "light.", "switch.", "camera." };
// Category labels. A FUNCTION and not a table: the language is only known
// once the configuration is read, long after a static table would have been
// initialized. Drawn with Font0 in the header, hence French WITHOUT accents.
static const char* catName(int cat) {
    switch (cat) {
    case CAT_COVER:  return sce::T("COVERS",   "VOLETS");
    case CAT_LIGHT:  return sce::T("LIGHTS",   "LUMIERES");
    case CAT_SWITCH: return sce::T("SWITCHES", "PRISES");
    default:         return sce::T("CAMERAS",  "CAMERAS");
    }
}
// Category of an identifier, -1 for a domain this bin does not handle. ONE
// call site: the discovery walks the catalogue twice (demand, then admission)
// and the two passes must classify identically or the quotas mean nothing.
static int catOfId(const char* id) {
    for (int c = 0; c < CAT_N; c++)
        if (!strncmp(id, CAT_DOMAIN[c], strlen(CAT_DOMAIN[c]))) return c;
    return -1;
}

struct Entity {
    char    id[48]   = "";     // "cover.salon"
    char    name[32] = "";     // friendly_name UTF-8 (accents kept)
    char    state[14]= "";     // open/closed/on/off/unavailable…
    uint8_t cat      = 0;
    // CONTROLLABLE attributes (-1 = not published by the entity: not every
    // cover knows its position, not every lamp can change color — the UI
    // only shows what actually exists).
    int16_t pos     = -1;      // cover: current_position 0-100 %
    int16_t bright  = -1;      // light: brightness 0-255
    int16_t kelvin  = -1;      // light: color_temp_kelvin
    int16_t kMin    = 2000, kMax = 6500;
    int16_t rgb[3]  = { -1, -1, -1 };
    // CAPABILITIES declared by `supported_color_modes`. They are read from
    // HA, NEVER inferred from a value: a lamp that is off no longer
    // publishes its brightness, and inferring "no value = no control" made
    // the slider vanish as soon as the light went off — while inferring the
    // opposite offered a palette to a monochrome lamp (review 07-28).
    bool    canDim   = false;  // anything other than onoff
    bool    canTemp  = false;  // color_temp
    bool    canColor = false;  // hs / rgb* / xy
};
static constexpr int MAX_ENT = 64;      // ~5.5 KB — accepted memory bound
static Entity  ents[MAX_ENT];
static int     entCount = 0;
// What the truncation COST, kept alongside what it produced. A cap that
// silences what it removed reads as "everything is here": the home screen
// counted 0 covers with the same confidence it counts a real 0, and the number
// was simply the one the table had room for (backlog A5, `entsel.h`).
static int     entSeen  = 0;            // handled entities Home Assistant served
static int     entDrop  = 0;            // ... minus the ones that fit
static int     entDropCat[CAT_N] = { 0 };   // and where they were missing from

// ----------------------------------------------------------------- state
enum Screen { SC_HOME = 0, SC_LIST };
static Screen  screen  = SC_HOME;
static int     curCat  = CAT_COVER;
static int     curIdx  = 0;             // index WITHIN the current category
// The TARGETED entity, remembered by its identifier. `curIdx` is a rank:
// re-reading the catalog can reorder ents[] (Home Assistant guarantees
// nothing) and the rank would then designate ANOTHER entity - we would have
// ended up commanding the wrong device (max review 07-28).
static char    curId[48] = "";
static String  toast;                   // brief message under the screen
static uint32_t toastMs = 0;

// RECURSIVE lock: the touch path must read ents[] under the lock from end to
// end of a gesture (netTask rewrites the whole table meanwhile), while also
// calling postAction() which takes it too. A plain mutex would have
// self-deadlocked (review 07-28).
static SemaphoreHandle_t gMtx = nullptr;
struct Lock {                        // RAII: handleTouch has ~12 exits
    Lock()  { xSemaphoreTakeRecursive(gMtx, portMAX_DELAY); }
    ~Lock() { xSemaphoreGiveRecursive(gMtx); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
};
static TaskHandle_t netTaskHandle = nullptr;
static volatile bool pollNow   = true;
static volatile bool uiDirty   = true;
static volatile bool cfgDirty  = false;
// The entity roster changed and the card has not been told. Raised by netTask
// after a discovery, consumed by loop() — SD writes belong to one task.
static volatile bool rosterDirty = false;
// Guard parking haNet before a companion reflash (sce::CoopStop -- the ONE
// shared implementation, see SceGuest.h).
static sce::CoopStop netGuard;
static volatile bool pollBusy  = false;
// CLOSE tracking of the displayed entity: /api/states/<id> is tiny (a single
// object) where /api/states weighs hundreds of KB. We query it at ~1 Hz —
// and at 400 ms during the 8 s that follow an action, the time for the cover
// to travel its course — without ever hammering the full API.
static volatile uint32_t liveUntil = 0;
static uint32_t pollDurMs  = 0;      // duration of the last FULL poll
static long     lastBodyLen = 0;     // announced body size
static int     httpStatus = 0;
static uint32_t lastOkMs  = 0;
static bool    sdOk = false;
static String  ipStr;

// Action requested by the UI, executed by netTask (never any HTTP in loop)
struct Action {
    char domain[12]  = "";
    char service[24] = "";   // "set_cover_position" is 18 characters:
                             // 16 truncated it to "set_cover_posit" -> 404
    char entity[48]  = "";
    char extra[72]   = "";               // service parameters (JSON)
};
// A QUEUE of actions, not a single slot: netTask can be busy for several
// seconds on a poll, and each new press then OVERWROTE the previous one —
// commands disappeared without a trace (user report 07-28).
// Producer: loop(). Consumer: netTask.
static constexpr int ACT_Q = 8;
static Action  actQ[ACT_Q];
// volatile: produced by loop() (core 1), consumed by netTask (core 0).
// Without it the compiler could keep actHead in a register inside the
// reader's wait loop and ignore presses for a whole discovery cycle
// (max review 07-28).
static volatile uint8_t actHead = 0, actTail = 0;
static inline bool actPending() { return actHead != actTail; }

// Camera: JPEG in PSRAM (the internal heap cannot hold an image)
// (camBuf/camLen removed: the raw buffer now lives in camNext, and it is
//  camDecode() that frees it — a single owner.)
static char     camErr[40] = "";         // why the image is missing
// The image is DECODED ONCE, not on every redraw: a full-screen PNG takes
// hundreds of ms to several seconds, and redoing it on every frame — under
// the lock — froze the touch input badly enough to look like a crash (user
// report 07-28). The result lives in a dedicated canvas.
static M5Canvas camImg(&M5.Display);
// volatile: WRITTEN by netTask (camDecode) and READ by loop() (drawList).
// While it is false, loop() does not display — this flag is what prevents
// pushing a half-filled sprite (review 07-28).
static volatile bool camImgOk = false;
static char     camImgFor[48] = "";
// Handover: netTask DROPS OFF the raw buffer, loop() PICKS it UP, decodes it
// OUTSIDE THE LOCK then frees it. A single owner for the free, hence no
// use-after-free during decoding.
static uint8_t* camNext = nullptr;
static size_t   camNextLen = 0;
static char     camNextFor[48] = "";
static volatile bool camReq = false;

static M5Canvas canvas(&M5.Display);
static sce::SceGuest guest;

// ------------------------------------------------------------- palette
// Same visual language as the launcher and the console (Liquid Glass): the
// robot must not change identity between its screens.
static constexpr uint16_t C_BG   = 0x0000;
static constexpr uint16_t C_ACC  = 0x269D;   // cyan   #22d3ee
static constexpr uint16_t C_ACC2 = 0x847F;   // indigo #818cf8
static constexpr uint16_t C_CARD = 0x10C5;   // card   #131a2b
static constexpr uint16_t C_BORD = 0x29AA;   // border
static constexpr uint16_t C_TXT  = 0xFFFF;
static constexpr uint16_t C_MUT  = 0xA534;   // secondary (RGAA ≥ 4.5:1,
                                             // measured by check-contrast.py)
static constexpr uint16_t C_DIM  = 0x738E;   // off
static constexpr uint16_t C_OK   = 0x34C8;   // green  #34d399
static constexpr uint16_t C_KO   = 0xFB8E;   // red    #f87171

// ===========================================================================
// SD configuration
// ===========================================================================
// The DECODING (quotes, comments) is the one from `SceGuest::yamlForEach` —
// the project's single parser. What matters here: the HA token contains
// dots, dashes and may end with "=", so a "#" only cuts the line OUTSIDE
// quotes. That is exactly what the canonical parser guarantees, for this bin
// as for the others.
// `false` = FLAT file: no line opens a section.
static void loadConfig() {
    // Idempotent and cheap (two SD.exists()) — called here rather than at
    // each mount call site so a card inserted mid-session (retry, SdWatch
    // reinsertion) is covered the same as the initial boot mount.
    sce::migrateSdRoot();
    // Counters ride through the ctx pointer: the callback is a captureless
    // lambda (function pointer), so there is no other channel. An unknown key
    // used to fall through a silent final else — a typo in the yaml looked
    // exactly like an absent key, hence the count traced at the end.
    struct CfgStat { int known; int unknown; } st = { 0, 0 };
    const bool found = sce::SceGuest::yamlForEach(CFG_PATH, false,
                               [](void* ctx, const char*, const char* k,
                                  const char* v) {
        CfgStat& s = *(CfgStat*)ctx;
        const String key(k), val(v);
        if      (key == "host")   strlcpy(cfg.host, val.c_str(), sizeof(cfg.host));
        else if (key == "port")   cfg.port = (uint16_t)val.toInt();
        else if (key == "ssl")    cfg.ssl  = val.toInt() != 0;
        else if (key == "token")  strlcpy(cfg.token, val.c_str(), sizeof(cfg.token));
        else if (key == "poll_s") cfg.pollS = constrain(val.toInt(), 0, 3600);
        else if (key == "brightness") cfg.bright = constrain(val.toInt(), 10, 255);
        else if (key == "auto_bright") cfg.autoBright = sce::webBool(val.c_str());
        else if (key == "entities") strlcpy(cfg.pins, val.c_str(), sizeof(cfg.pins));
        else { s.unknown++; return; }        // key names only, NEVER values:
        s.known++;                           // the token travels in this file
    }, &st);
    if (found)
        sce::trace::log("cfg", "%s lu: %d cles reconnues, %d inconnues ignorees",
                        CFG_PATH, st.known, st.unknown);
    else
        sce::trace::log("cfg", "%s absent: valeurs par defaut", CFG_PATH);
    // UI language: the companion's SHARED key (`lang:` at the top level of
    // config.yaml), never a per-bin copy — one robot, one language, and a
    // second copy is a second thing to keep in step. Which is exactly why the
    // READING of it is shared too: this bin had the eight lines written out,
    // the same eight the helper was extracted from `space` to retire. A guard
    // added to the helper reaches the three bins or it reaches two.
    sce::loadCompanionLang<sce::SceGuest>();
}

static bool saveConfigSd() {
    Config c;
    xSemaphoreTakeRecursive(gMtx, portMAX_DELAY);
    c = cfg;
    xSemaphoreGiveRecursive(gMtx);
    File f = SD.open(CFG_PATH, FILE_WRITE);      // truncates
    if (!f) { Serial.printf("[ha] ECHEC ecriture %s\n", CFG_PATH);
              sce::trace::log("sd", "echec ouverture %s en ecriture", CFG_PATH);
              return false; }
    f.printf("# ha-remote.yaml - telecommande Home Assistant (docs/guests/HA-REMOTE.md)\n"
             "# (reecrit par le bin : les commentaires manuels ne survivent pas)\n");
    f.printf("host: %s\nport: %u\nssl: %u\n", c.host, (unsigned)c.port,
             (unsigned)(c.ssl ? 1 : 0));
    f.printf("token: \"%s\"\n", c.token);        // quoted: free-form chars
    f.printf("poll_s: %lu\nbrightness: %u\nauto_bright: %u\n",
             (unsigned long)c.pollS, (unsigned)c.bright,
             (unsigned)c.autoBright);
    // Quoted like the token: the list carries commas and dots, and an unquoted
    // value would have to survive every future tightening of the parser.
    f.printf("entities: \"%s\"\n", c.pins);
    bool bad = f.getWriteError() != 0;
    // position() = bytes written so far on this sequential handle: the size
    // the file will have, read BEFORE close() invalidates the handle.
    const unsigned written = (unsigned)f.position();
    f.close();
    // A full or write-protected SD still lets SD.open succeed and TRUNCATE
    // the file: returning `true` without checking lost host and token while
    // reporting success, and the web page displayed "Saved"
    // (max review 07-28).
    if (bad) Serial.println("[ha] ECHEC ecriture config (SD pleine ?)");
    if (bad) sce::trace::log("sd", "%s: getWriteError, contenu perdu (SD pleine ?)",
                             CFG_PATH);
    else     sce::trace::log("sd", "%s ecrit (%u o)", CFG_PATH, written);
    return !bad;
}

// ===========================================================================
// Network — netTask is the ONLY one to open sockets
// ===========================================================================
// ATOMIC copy of the configuration. The web form rewrites `cfg` field by
// field under gMtx; netTask used to read it without the lock and could build
// a request with the NEW host and a half-copied token — that is, send a
// truncated credential to an unexpected machine (review 07-28).
static Config cfgSnapshot() {
    Config c;
    xSemaphoreTakeRecursive(gMtx, portMAX_DELAY);
    c = cfg;
    xSemaphoreGiveRecursive(gMtx);
    return c;
}

// Client holder: the TLS context is allocated ONLY if the link is encrypted.
// `WiFiClientSecure` reserves ~1.5 to 2 KB of INTERNAL heap as soon as it is
// constructed, even unused — and that was on every request, on a heap
// already contended by WiFi (max review 07-28).
struct HaClient {
    WiFiClient        plain;
    WiFiClientSecure* tls = nullptr;
    ~HaClient() { delete tls; }
};

// Prepares an authenticated request. The holder must live as long as `http`:
// it is supplied by the caller (explicit lifetime).
static bool haBegin(HTTPClient& http, HaClient& cl, const String& path) {
    const Config c = cfgSnapshot();
    // Every silent exit below is NAMED: unattributed, they all read as "Home
    // Assistant unreachable" while the causes call for different fixes.
    // The path is logged (no query string in this bin, entity ids are not
    // secrets); the token NEVER is — it only travels in the header.
    if (!c.host[0] || !c.token[0]) {
        sce::trace::log("http", "%s: abandon, hote ou jeton non configure",
                        path.c_str());
        return false;
    }
    String url = String(c.ssl ? "https://" : "http://") + c.host + ":" +
                 String(c.port) + path;
    bool ok;
    if (c.ssl) {
        cl.tls = new WiFiClientSecure();
        if (!cl.tls) {
            sce::trace::log("http", "%s: alloc TLS impossible (tas interne)",
                            path.c_str());
            return false;
        }
        cl.tls->setInsecure();
        ok = http.begin(*cl.tls, url);
    } else {
        ok = http.begin(cl.plain, url);
    }
    if (!ok) {
        sce::trace::log("http", "%s: echec http.begin (url invalide ?)",
                        path.c_str());
        return false;
    }
    // The connection must fail fast (host powered off); it BLOCKS inside
    // lwIP, so the IDLE task keeps running meanwhile — 3 s are harmless.
    // Without this header collection, `header("Transfer-Encoding")` always
    // returns empty and de-chunking would never kick in.
    static const char* WANTED[] = { "Transfer-Encoding" };
    http.collectHeaders(WANTED, 1);
    http.setConnectTimeout(3000);
    // 4 s and no more: waiting for the HEADERS loops on `delay(0)` inside
    // HTTPClient, which does NOT yield to the IDLE task of core 0. Beyond
    // the watchdog window (5 s), a slow Home Assistant was enough to bring
    // the firmware down. The BODY is no longer concerned: YieldingReader has
    // its own bound and sleeps.
    http.setTimeout(4000);
    http.addHeader("Authorization", String("Bearer ") + c.token);
    http.addHeader("Content-Type", "application/json");
    return true;
}

// Cleans up a name for the screen. UTF-8 is PRESERVED — Home Assistant
// entity names are written in French here ("Volet sejour") and the older
// version, which kept ASCII only, stripped their accents (user report
// 07-28). Rendering goes through `efontJA_12`, the only embedded Unicode
// font. We NEVER cut in the middle of a multi-byte sequence: the font would
// display a replacement character.
static void cleanName(char* s, size_t maxBytes) {
    char* w = s;
    for (char* r = s; *r; ) {
        uint8_t c = (uint8_t)*r;
        if (c < 0x20) { r++; continue; }               // control: skipped
        size_t n = (c < 0x80) ? 1 : (c >= 0xF0) ? 4
                 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
        if ((size_t)(w - s) + n > maxBytes) break;     // bound WITHOUT cutting
        size_t k = 0;
        while (k < n && r[k]) { w[k] = r[k]; k++; }
        // We advance by what was ACTUALLY copied: `r += n` jumped over the
        // NUL on a truncated sequence (max review 07-28).
        w += k; r += k;
        if (k < n) break;
    }
    *w = '\0';
}

// ---------------------------------------------------------------------------
// BUFFERED reader that yields — a necessity, not an optimization.
//
// ArduinoJson consumes the stream BYTE BY BYTE (`Reader::read()` calls
// `Stream::readBytes(&c, 1)`), and `Stream::timedRead()` waits in a loop with
// `yield()`, which only yields to tasks of EQUAL priority: the IDLE task of
// core 0 stops running. Over the hundreds of KB of `/api/states`, the
// watchdog brought the firmware down after 5 s — boot-loop reproduced on
// target on 07-28, backtrace pointing inside `timedRead` under `parseObject`.
//
// So we read in BLOCKS from a buffer, sleeping one tick on each refill: the
// IDLE task can breathe, and we save one virtual call per byte along the
// way. The announced length bounds the read — without it, ArduinoJson's last
// request (end-of-input check) waited on a connection Home Assistant keeps
// open.
// ---------------------------------------------------------------------------
class YieldingReader : public Stream {
public:
    // `chunked`: the body arrives in blocks preceded by their hex size.
    // `HTTPClient::getStream()` returns the RAW socket — de-chunking only
    // exists inside writeToStream()/getString(). Without doing it here, the
    // chunk headers went straight into the JSON parser or the image decoder:
    // unreadable body behind any reverse proxy (max review 07-28).
    YieldingReader(WiFiClient& src, long contentLen, bool chunked)
        : _s(src), _limit(chunked ? -1 : contentLen), _chunked(chunked) {}

    int  available() override { return (int)(_len - _pos) + _s.available(); }
    int  peek() override      { return ensure() ? (uint8_t)_buf[_pos] : -1; }
    int  read() override {
        if (!ensure()) return -1;
        int c = (uint8_t)_buf[_pos++];
        if (_chunked && _chunkLeft) _chunkLeft--;
        return c;
    }
    size_t write(uint8_t) override { return 0; }          // read-only
    size_t readBytes(char* out, size_t n) override {
        size_t got = 0;
        while (got < n && ensure()) {
            size_t take = _len - _pos;
            if (take > n - got) take = n - got;
            if (_chunked && take > _chunkLeft) take = _chunkLeft;
            if (!take) break;
            memcpy(out + got, _buf + _pos, take);
            _pos += take; got += take;
            if (_chunked) _chunkLeft -= (uint32_t)take;
        }
        return got;
    }
    // true = end of body REACHED cleanly. false = we stopped earlier
    // (timeout, requested stop, user press) — the content is INCOMPLETE and
    // the caller must not take it as good.
    bool clean() const { return _clean; }

private:
    int rawByte() {
        if (_pos >= _len && !rawFill()) return -1;
        return (uint8_t)_buf[_pos++];
    }
    // Line "size[;options]CRLF". Size 0 = last chunk.
    bool nextChunk() {
        if (_afterFirst) { rawByte(); rawByte(); }   // CRLF of previous chunk
        _afterFirst = true;
        long sz = 0; bool any = false; int c;
        while ((c = rawByte()) >= 0) {
            if (c == 13) { rawByte(); break; }       // CR then LF
            if (c == ';') { while ((c = rawByte()) >= 0 && c != 10) {} break; }
            int v = (c >= 48 && c <= 57) ? c - 48
                  : (c >= 97 && c <= 102) ? c - 87
                  : (c >= 65 && c <= 70)  ? c - 55 : -1;
            if (v < 0) break;
            sz = sz * 16 + v; any = true;
        }
        if (!any) return false;                      // broken framing
        if (sz <= 0) { _clean = true; return false; }// last chunk: CLEAN end
        _chunkLeft = (uint32_t)sz;
        return true;
    }
    bool ensure() {
        if (_chunked && _chunkLeft == 0 && !nextChunk()) return false;
        if (_pos < _len) return true;
        return rawFill();
    }
    bool rawFill() {
        if (!_chunked && _limit >= 0 && _got >= (uint32_t)_limit) {
            _clean = true; return false;             // length reached
        }
        _pos = _len = 0;
        uint32_t t0 = millis();
        for (;;) {
            int a = _s.available();
            if (a > 0) {
                size_t want = (size_t)a > sizeof(_buf) ? sizeof(_buf) : (size_t)a;
                if (!_chunked && _limit >= 0) {
                    uint32_t rest = (uint32_t)_limit - _got;
                    if (want > rest) want = rest;
                }
                int n = _s.read((uint8_t*)_buf, want);
                if (n > 0) {
                    _len  = (size_t)n;
                    _got += (uint32_t)n;
                    // CLEAN end the moment the announced length is reached.
                    // Marking it only on the NEXT refill did not work: the
                    // caller leaves its loop as soon as it has its count and
                    // never calls back — every complete image was declared
                    // incomplete (user report 07-28).
                    if (!_chunked && _limit >= 0 &&
                        _got >= (uint32_t)_limit) _clean = true;
                    // Yield every 4 KB, not on every block: the receive
                    // buffer only returns about 1 KB at a time, and one sleep
                    // per block added seconds on a body of several hundred
                    // KB. 4 KB arrive in far less than the watchdog's 5 s,
                    // even on weak WiFi.
                    _sinceYield += (uint32_t)n;
                    if (_sinceYield >= 4096) { _sinceYield = 0; vTaskDelay(1); }
                    return true;
                }
            }
            // Requested STOP (return to the companion): without this exit,
            // the parking window expired with a TLS session still alive and
            // the reflash — the only way back — competed with its buffers
            // (max review 07-28).
            if (netGuard.stopping()) return false;
            // A user press PREEMPTS discovery: the catalog takes about
            // fifteen seconds, and making the user wait made the remote
            // unusable. It will be redone.
            if (actPending()) return false;
            if (!_s.connected() && _s.available() <= 0) {
                if (!_chunked && _limit < 0) _clean = true;  // end by close
                return false;
            }
            if (millis() - t0 > 8000) return false;          // hard bound
            vTaskDelay(pdMS_TO_TICKS(2));     // wait WITHOUT starving
        }
    }
    WiFiClient& _s;
    long        _limit;                       // Content-Length, or -1
    bool        _chunked;
    bool        _clean = false, _afterFirst = false;
    uint32_t    _chunkLeft = 0;
    uint32_t    _got = 0, _sinceYield = 0;
    char        _buf[1024];
    size_t      _pos = 0, _len = 0;
};

// Does the server announce a chunked body? Assumes haBegin() asked for that
// header to be collected (HTTPClient does not keep them by default).
static bool isChunked(HTTPClient& http) {
    String te = http.header("Transfer-Encoding");
    te.toLowerCase();
    return te.indexOf("chunked") >= 0;
}

// Home Assistant nests deeply (attributes made of arrays of objects) and the
// filter does NOT exempt us from walking down those levels: ArduinoJson's
// default limit (10) returned `TooDeep`, hence zero entities, on a real
// installation (HW report 07-28).
static constexpr uint8_t JSON_DEPTH = 24;

// PSRAM allocator: `sce::psAlloc` (../common/PsJson.h), shared with
// flight-radar. The INTERNAL heap is the one WiFi/TLS uses — allocating tens
// of KB there several times per second fragments it until NoMemory, and the
// entity then stops refreshing WITHOUT reporting anything.

// KEPT fields, for both polls. A single entity can publish enormous blocks
// (`effect_list` of a WLED lamp, `entity_id` of every member of a group):
// close tracking without a filter allocated tens of KB every 400 ms
// (review 07-28).
static void fillFilter(JsonObject fe) {
    fe["entity_id"] = true;
    fe["state"]     = true;
    JsonObject fa = fe["attributes"].to<JsonObject>();
    fa["friendly_name"]         = true;
    fa["current_position"]      = true;   // covers
    fa["brightness"]            = true;   // lights
    fa["color_temp_kelvin"]     = true;
    fa["min_color_temp_kelvin"] = true;
    fa["max_color_temp_kelvin"] = true;
    fa["rgb_color"]             = true;
    fa["supported_color_modes"] = true;   // CAPABILITIES
}

// Reading the controllable attributes — ONE single source of truth, shared
// by the FULL poll and by the tracking of ONE entity. Two divergent readers
// left a stale RGB triplet displayed after a lamp switched to dynamic white
// (review 07-28).
static void applyAttrs(Entity& t, JsonObject a) {
    t.pos    = a["current_position"].is<int>()  ? (int16_t)a["current_position"]  : -1;
    t.bright = a["brightness"].is<int>()        ? (int16_t)a["brightness"]        : -1;
    t.kelvin = a["color_temp_kelvin"].is<int>() ? (int16_t)a["color_temp_kelvin"] : -1;
    if (a["min_color_temp_kelvin"].is<int>()) t.kMin = a["min_color_temp_kelvin"];
    if (a["max_color_temp_kelvin"].is<int>()) t.kMax = a["max_color_temp_kelvin"];
    JsonArray rc = a["rgb_color"];
    for (int k = 0; k < 3; k++) t.rgb[k] = (rc.size() == 3) ? (int16_t)rc[k] : -1;
    t.canDim = t.canTemp = t.canColor = false;
    for (JsonVariant m : a["supported_color_modes"].as<JsonArray>()) {
        const char* mode = m.as<const char*>();
        if (!mode || !strcmp(mode, "onoff") || !strcmp(mode, "unknown")) continue;
        t.canDim = true;                              // everything else varies
        if      (!strcmp(mode, "color_temp")) t.canTemp = true;
        else if (strcmp(mode, "brightness") && strcmp(mode, "white"))
            t.canColor = true;                        // hs / rgb* / xy
    }
}

// GET /api/states → fills ents[].
static bool fetchStates() {
    if (netGuard.stopping() || WiFi.status() != WL_CONNECTED) {
        sce::trace::log("http", "/api/states: abandon (%s)",
                        netGuard.stopping() ? "arret demande" : "WiFi absent");
        return false;
    }
    HaClient cl; HTTPClient http;
    if (!haBegin(http, cl, "/api/states")) { httpStatus = -1; return false; }
    // Discovery is the ~15 s request: its start is traced so a silence in the
    // log reads as "still downloading", not "stuck".
    sce::trace::log("http", "GET /api/states (decouverte du catalogue)...");
    const uint32_t t0 = millis();
    int code = http.GET();
    if (code != 200) {
        sce::trace::log("http", "GET /api/states -> %d en %lu ms", code,
                        (unsigned long)(millis() - t0));
        httpStatus = code; http.end(); return false;
    }

    JsonDocument filter;                     // root = ARRAY of entities
    fillFilter(filter.add<JsonObject>());
    sce::psAlloc.reset();
    JsonDocument doc(&sce::psAlloc);
    lastBodyLen = http.getSize();
    YieldingReader rd(http.getStream(), lastBodyLen, isChunked(http));
    DeserializationError err = deserializeJson(doc, rd,
                                               DeserializationOption::Filter(filter),
                                               DeserializationOption::NestingLimit(JSON_DEPTH));
    http.end();
    if (err) {
        if (actPending()) {         // INTENTIONAL interruption, not a failure
            Serial.println("[ha] sondage interrompu (action utilisateur)");
            sce::trace::log("http", "GET /api/states interrompu par une action "
                            "apres %lu ms", (unsigned long)(millis() - t0));
            return false;
        }
        Serial.printf("[ha] json err %s (len=%d heap=%u psram=%u)\n", err.c_str(),
                      (int)http.getSize(),
                      (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
        sce::trace::log("http", "GET /api/states -> 200 mais json '%s' "
                        "(%ld o annonces) en %lu ms", err.c_str(), lastBodyLen,
                        (unsigned long)(millis() - t0));
        httpStatus = -2;
        return false;
    }
    sce::trace::log("http", "GET /api/states -> 200, %ld o en %lu ms",
                    lastBodyLen, (unsigned long)(millis() - t0));

    xSemaphoreTakeRecursive(gMtx, portMAX_DELAY);
    // ---- WHO gets a seat when there are more entities than seats ----------
    // Filling in arrival order is not a rule: what you lose is whatever
    // `/api/states` happened to serve last. The criterion lives in `entsel.h`
    // (pinned > answering > unavailable, then equal shares per category) and
    // is tested natively; here we only feed it.
    //
    // Two walks over a document that is ALREADY fully parsed in PSRAM: the
    // quotas need the totals, and the totals are only known at the end of the
    // first walk. Re-iterating a JsonArray allocates nothing and costs
    // microseconds next to the fifteen seconds of download.
    //
    // The pin list is snapshotted BEFORE the first walk: read again between
    // the two, a concurrent /config submit would classify the same entity as
    // pinned in one pass and ordinary in the other, and the quotas would no
    // longer match what gets admitted.
    char pins[sizeof(cfg.pins)];
    strlcpy(pins, cfg.pins, sizeof(pins));
    char watched[sizeof(curId)];
    strlcpy(watched, curId, sizeof(watched));
    // The entity currently on screen is pinned too: it must not disappear from
    // under the finger because a discovery landed while it was being watched.
    //
    // Compared over `sizeof(curId) - 1` and NOT with strcmp: `curId` is a copy
    // of `Entity::id`, which is a TRUNCATION of the identifier Home Assistant
    // serves. A `light.` entity whose identifier passes 47 characters would
    // never have matched itself, so the one entity on screen was the one the
    // rule could not protect — the exact failure the pin exists to prevent.
    auto tierOf = [&](const char* id, const char* state) {
        if (ha::isPinned(pins, id) ||
            (watched[0] && !strncmp(watched, id, sizeof(curId) - 1)))
            return ha::TIER_PINNED;
        return ha::isStale(state) ? ha::TIER_STALE : ha::TIER_LIVE;
    };
    static ha::Selector sel;          // static: 100 bytes kept off a 20 KB stack
    sel.reset(MAX_ENT, CAT_N);
    for (JsonObject e : doc.as<JsonArray>()) {
        const char* id = e["entity_id"] | "";
        if (!id[0]) continue;
        int cat = catOfId(id);
        if (cat < 0) continue;                       // domain not handled
        sel.offer(cat, tierOf(id, e["state"] | ""));
    }
    sel.plan();
    entSeen = sel.seen();
    entDrop = sel.dropped();
    for (int c = 0; c < CAT_N; c++) entDropCat[c] = sel.droppedIn(c);

    entCount = 0;
    for (JsonObject e : doc.as<JsonArray>()) {
        const char* id = e["entity_id"] | "";
        if (!id[0]) continue;
        int cat = catOfId(id);
        if (cat < 0) continue;                       // domain not handled
        if (!sel.admit(cat, tierOf(id, e["state"] | ""))) continue;
        // NOT a quiet cap (review 08-04): the Selector plans its quotas
        // against MAX_ENT, so admitting more than fits would mean a Selector
        // arithmetic bug — and silently dropping the tail here would
        // reinstate the exact invisible truncation entsel.h exists to
        // remove, with entSeen/entDrop still reporting the (now wrong)
        // arithmetic as truth. Say it, once, then stop.
        if (entCount >= MAX_ENT) {
            Serial.println("[ha] BUG selecteur : admis au-dela de MAX_ENT");
            break;
        }
        Entity& t = ents[entCount++];
        t = Entity{};
        strlcpy(t.id, id, sizeof(t.id));
        strlcpy(t.state, e["state"] | "", sizeof(t.state));
        const char* fn = e["attributes"]["friendly_name"] | "";
        strlcpy(t.name, fn[0] ? fn : id + strlen(CAT_DOMAIN[cat]), sizeof(t.name));
        cleanName(t.name, sizeof(t.name) - 1);
        t.cat = (uint8_t)cat;
        applyAttrs(t, e["attributes"]);
    }
    // Realign the rank on the targeted entity: if it moved, or vanished, we
    // find it again or fall back cleanly onto the first one.
    if (curId[0]) {
        int seen = 0, found = -1;
        for (int i = 0; i < entCount; i++)
            if (ents[i].cat == curCat) {
                if (!strcmp(ents[i].id, curId)) { found = seen; break; }
                seen++;
            }
        curIdx = (found >= 0) ? found : 0;
        if (found < 0) curId[0] = '\0';
    }
    lastOkMs = millis();
    httpStatus = 200;
    uiDirty = true;
    // The card is told only when the PERSISTED tuple changed. This used to be
    // an unconditional `rosterDirty = true`, which with poll_s=30 truncated
    // and rewrote ha-entities.tsv ~2900 times a day with IDENTICAL bytes —
    // pure SD wear, and every truncate window was a chance for a power cut to
    // leave the empty roster this cache exists to prevent. The signature
    // hashes exactly the fields rosterSave() writes, nothing else: states
    // change every poll and are deliberately not part of it.
    {
        uint32_t sig = 2166136261u;                  // FNV-1a
        auto mix = [&sig](const char* str) {
            while (*str) { sig ^= (uint8_t)*str++; sig *= 16777619u; }
            sig ^= 0xFF; sig *= 16777619u;           // field separator
        };
        for (int i = 0; i < entCount; i++) {
            const Entity& e = ents[i];
            char t[24];
            snprintf(t, sizeof(t), "%u%d%d%d%d%d", (unsigned)e.cat,
                     e.canDim ? 1 : 0, e.canTemp ? 1 : 0, e.canColor ? 1 : 0,
                     (int)e.kMin, (int)e.kMax);
            mix(e.id); mix(e.name); mix(t);
        }
        static uint32_t lastSig = 0;
        static int      lastN   = -1;
        if (sig != lastSig || entCount != lastN) {
            lastSig = sig; lastN = entCount;
            rosterDirty = true;  // loop() writes it: netTask never touches the card
        }
    }
    xSemaphoreGiveRecursive(gMtx);
    // Discovery outcome, parse and admission included — the HTTP line above
    // only covers the transfer.
    sce::trace::log("http", "decouverte ok: %d entites gardees / %d vues "
                    "(%d ecartees) en %lu ms", entCount, entSeen, entDrop,
                    (unsigned long)(millis() - t0));
    return true;
}

// ---- THE ROSTER CACHE ------------------------------------------------------
// Discovery costs about fifteen seconds and hundreds of kilobytes, and until
// it lands the home screen counts zero of everything — every boot, on a device
// whose whole point is to be grabbed and pressed. Yet WHAT it discovers barely
// moves: identifiers, friendly names, categories and capabilities change when
// you add a lamp, not between two mornings.
//
// So the durable half is written to the card and read back at boot. The table
// is on screen in a second, with the states BLANK — and blank is the honest
// word: a remembered "on" from yesterday shown as current would be the one lie
// this bin must not tell, since it is also the thing a user acts on. The
// per-entity polling fills them within the second, and the full discovery
// still runs behind it and replaces the table wholesale, so an entity that has
// genuinely disappeared corrects itself within the usual fifteen seconds.
//
// TAB-separated, because a friendly name is free UTF-8 text a user typed and
// may well contain a semicolon or a comma; a tab it cannot contain, since
// `cleanName` has already run.
static constexpr const char* ROSTER_PATH = "/stackchan-companion/ha-entities.tsv";

static bool rosterSave() {
    if (!sdOk) return false;
    File f = SD.open(ROSTER_PATH, FILE_WRITE);      // truncates
    if (!f) {
        sce::trace::log("sd", "echec ouverture %s en ecriture", ROSTER_PATH);
        return false;
    }
    xSemaphoreTakeRecursive(gMtx, portMAX_DELAY);
    int n = 0;
    for (int i = 0; i < entCount; i++) {
        const Entity& e = ents[i];
        f.printf("%s\t%s\t%u\t%d%d%d\t%d\t%d\n",
                 e.id, e.name, (unsigned)e.cat,
                 e.canDim ? 1 : 0, e.canTemp ? 1 : 0, e.canColor ? 1 : 0,
                 (int)e.kMin, (int)e.kMax);
        n++;
    }
    xSemaphoreGiveRecursive(gMtx);
    // getWriteError is READ for the trace only: the return value keeps its
    // historical contract (the roster is a cache, a lost write self-heals at
    // the next discovery — unlike the config, where it loses the token).
    const bool bad = f.getWriteError() != 0;
    const unsigned written = (unsigned)f.position();
    f.close();
    if (bad) sce::trace::log("sd", "%s: getWriteError (cache peut-etre tronque)",
                             ROSTER_PATH);
    else     sce::trace::log("sd", "%s ecrit: %d entites, %u o",
                             ROSTER_PATH, n, written);
    return true;
}

static void rosterLoad() {
    if (!sdOk) return;
    File f = SD.open(ROSTER_PATH, FILE_READ);
    if (!f) return;
    char line[160];                                  // bounded, A2.23
    int n = 0;
    while (f.available() && n < MAX_ENT) {
        const size_t got = f.readBytesUntil('\n', line, sizeof(line) - 1);
        line[got] = '\0';
        if (got == 0) continue;
        // "Bounded" was only half true: the buffer was bounded, the LINE
        // was not. A record longer than the buffer left its tail in the
        // stream, where the next iteration parsed it as a fresh entity —
        // a fragment with a valid domain prefix seats a phantom record
        // with shifted capability columns. A too-long record is DISCARDED
        // whole: one entity lost beats one invented.
        if (got == sizeof(line) - 1) {
            while (f.available() && f.read() != '\n') {}
            continue;
        }
        char* p = line;
        auto field = [&p]() -> char* {
            char* s = p;
            char* t = strchr(p, '\t');
            if (t) { *t = '\0'; p = t + 1; } else p = s + strlen(s);
            return s;
        };
        char* id   = field();
        char* name = field();
        char* cat  = field();
        char* caps = field();
        char* kmin = field();
        char* kmax = field();
        for (char* c = kmax; *c; c++) if (*c == '\r') *c = '\0';
        // The identifier is re-validated against the CURRENT domain table, not
        // trusted: a card carried over from a build that handled other domains
        // would otherwise seat an entity nothing here can command.
        const int c = catOfId(id);
        if (c < 0 || !name[0]) continue;
        Entity& t = ents[n++];
        t = Entity{};
        strlcpy(t.id, id, sizeof(t.id));
        strlcpy(t.name, name, sizeof(t.name));
        t.cat = (uint8_t)c;
        (void)cat;                       // the identifier is the authority
        if (strlen(caps) >= 3) {
            t.canDim   = caps[0] == '1';
            t.canTemp  = caps[1] == '1';
            t.canColor = caps[2] == '1';
        }
        if (kmin[0]) t.kMin = (int16_t)atoi(kmin);
        if (kmax[0]) t.kMax = (int16_t)atoi(kmax);
        // state, position, brightness: DELIBERATELY left empty. See above.
    }
    f.close();
    entCount = n;
    entSeen  = n;                        // what we know we knew; the discovery
    entDrop  = 0;                        // that follows publishes the real count
    if (n) {
        uiDirty = true;
        Serial.printf("[ha] roster relu de la carte : %d entites (etats vides)\n", n);
    }
}

// POST /api/services/<domain>/<service>. `entity` can be a single identifier
// or, for a bulk action, a JSON ARRAY of identifiers — one single call rather
// than one per cover (HA processes them in parallel).
static bool callService(const char* domain, const char* service,
                        const String& entityJson, const char* extra = "") {
    if (netGuard.stopping() || WiFi.status() != WL_CONNECTED) {
        // A user action that dies here is the worst silent exit of the bin:
        // the press was accepted, queued... and nothing moves.
        sce::trace::log("http", "/api/services/%s/%s: abandon (%s)", domain,
                        service, netGuard.stopping() ? "arret demande"
                                                     : "WiFi absent");
        return false;
    }
    HaClient cl; HTTPClient http;
    String path = String("/api/services/") + domain + "/" + service;
    if (!haBegin(http, cl, path)) return false;
    // `extra` = already-formed JSON fragment (",\"position\":50"): HA
    // services take their parameters in the SAME object as the entity_id.
    String body = String("{\"entity_id\":") + entityJson + (extra ? extra : "") + "}";
    const uint32_t t0 = millis();
    int code = http.POST(body);
    http.end();
    Serial.printf("[ha] %s.%s -> %d\n", domain, service, code);
    sce::trace::log("http", "POST %s -> %d en %lu ms", path.c_str(), code,
                    (unsigned long)(millis() - t0));
    return code == 200 || code == 201;
}

// Image format, recognized by its MAGIC NUMBER. Not every Home Assistant
// `camera.*` entity serves JPEG: a still image wrapped into a camera comes
// out as PNG, and `drawJpg` then rendered a BLACK rectangle without
// reporting anything (user report 07-28).
enum ImgFmt { IMG_NONE = 0, IMG_JPG, IMG_PNG };
static ImgFmt imgFmt(const uint8_t* d, size_t n) {
    if (n > 3 && d[0] == 0xFF && d[1] == 0xD8) return IMG_JPG;
    if (n > 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G')
        return IMG_PNG;
    return IMG_NONE;
}
// PNG dimensions: IHDR chunk, always first, big-endian.
static bool pngSize(const uint8_t* d, size_t n, int& w, int& h) {
    if (n < 24) return false;
    w = ((int)d[16] << 24) | ((int)d[17] << 16) | ((int)d[18] << 8) | d[19];
    h = ((int)d[20] << 24) | ((int)d[21] << 16) | ((int)d[22] << 8) | d[23];
    return w > 0 && h > 0;
}

// JPEG dimensions, read from the SOFn marker. Without them the downscale
// factor cannot be computed, and a modern camera snapshot (1080p, often
// > 200 KB) exceeded the memory bound: the bin refused to display it instead
// of shrinking it (user report 07-28).
static bool jpegSize(const uint8_t* d, size_t n, int& w, int& h) {
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return false;
    size_t i = 2;
    while (i + 9 < n) {
        if (d[i] != 0xFF) { i++; continue; }
        uint8_t m = d[i + 1];
        if (m == 0xFF) { i++; continue; }
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { i += 2; continue; }
        if (m == 0xDA) break;                       // start of scan data
        size_t len = ((size_t)d[i + 2] << 8) | d[i + 3];
        if (len < 2) return false;
        if ((m >= 0xC0 && m <= 0xCF) && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            h = ((int)d[i + 5] << 8) | d[i + 6];
            w = ((int)d[i + 7] << 8) | d[i + 8];
            return w > 0 && h > 0;
        }
        i += 2 + len;
    }
    return false;
}

// Camera thumbnail: JPEG downloaded into PSRAM (drawJpg reads a buffer).
static bool fetchCamera(const char* entityId) {
    if (netGuard.stopping() || WiFi.status() != WL_CONNECTED) {
        sce::trace::log("http", "/api/camera_proxy/%s: abandon (%s)", entityId,
                        netGuard.stopping() ? "arret demande" : "WiFi absent");
        return false;
    }
    HaClient cl; HTTPClient http;
    if (!haBegin(http, cl, String("/api/camera_proxy/") + entityId))
        return false;
    const uint32_t t0 = millis();
    int code = http.GET();
    if (code != 200) {
        sce::trace::log("http", "GET /api/camera_proxy/%s -> %d en %lu ms",
                        entityId, code, (unsigned long)(millis() - t0));
        // camErr is DISPLAYED (drawList): every message below is bilingual,
        // Font0 — French without accents.
        snprintf(camErr, sizeof(camErr),
                 sce::T("Home Assistant: error %d",
                        "Home Assistant : erreur %d"), code);
        http.end(); return false;
    }
    // The shared reader brings chunk de-framing, periodic yielding and above
    // all the distinction between a CLEAN end and an interruption.
    const size_t MAXJ = 1500000;             // accepted bound (PSRAM: 8 MB)
    long len = http.getSize();
    if (len > (long)MAXJ) {
        snprintf(camErr, sizeof(camErr),
                 sce::T("image too large (%ld kB)",
                        "image trop lourde (%ld ko)"), len / 1024);
        http.end(); return false;
    }
    // Unknown length: start small and GROW, rather than reserving 1.5 MB
    // every three seconds.
    size_t cap = (len > 0) ? (size_t)len : 131072;
    uint8_t* buf = (uint8_t*)ps_malloc(cap);
    if (!buf) { strlcpy(camErr, sce::T("not enough memory",
                                       "memoire insuffisante"), sizeof(camErr));
                http.end(); return false; }
    YieldingReader rd(http.getStream(), len, isChunked(http));
    size_t got = 0;
    for (;;) {
        if (got == cap) {
            if (len > 0 || cap >= MAXJ) break;
            size_t ncap = cap * 2;
            if (ncap > MAXJ) ncap = MAXJ;
            uint8_t* nb = (uint8_t*)ps_realloc(buf, ncap);
            if (!nb) break;
            buf = nb; cap = ncap;
        }
        size_t n = rd.readBytes((char*)buf + got, cap - got);
        if (!n) break;
        got += n;
    }
    http.end();
    // One line for the whole transfer: size actually received, clean end or
    // not — an INCOMPLET here explains the "image incomplete" on screen.
    sce::trace::log("http", "GET /api/camera_proxy/%s -> 200, %u o%s en %lu ms",
                    entityId, (unsigned)got, rd.clean() ? "" : " (INCOMPLET)",
                    (unsigned long)(millis() - t0));
    // An INTERRUPTED download is not an image: the older version published
    // the truncated blob while CLEARING the error message, and the view
    // redisplayed the top third every 3 s without reporting anything
    // (max review 07-28).
    if (!rd.clean()) {
        snprintf(camErr, sizeof(camErr),
                 sce::T("incomplete image (%u kB)", "image incomplete (%u ko)"),
                 (unsigned)(got / 1024));
        free(buf); return false;
    }
    if (got < 128) {
        snprintf(camErr, sizeof(camErr),
                 sce::T("empty image (%u B)", "image vide (%u o)"),
                 (unsigned)got);
        free(buf); return false;
    }
    camErr[0] = '\0';
    xSemaphoreTakeRecursive(gMtx, portMAX_DELAY);
    if (camNext) free(camNext);          // previous drop-off never consumed
    camNext = buf; camNextLen = got;
    strlcpy(camNextFor, entityId, sizeof(camNextFor));
    xSemaphoreGiveRecursive(gMtx);
    return true;
}

// GET /api/states/<entity_id>: refreshes ONE entity in place.
static bool fetchOneState(const char* entityId) {
    if (netGuard.stopping() || WiFi.status() != WL_CONNECTED) {
        sce::trace::log("http", "/api/states/%s: abandon (%s)", entityId,
                        netGuard.stopping() ? "arret demande" : "WiFi absent");
        return false;
    }
    HaClient cl; HTTPClient http;
    if (!haBegin(http, cl, String("/api/states/") + entityId))
        return false;
    const uint32_t t0 = millis();
    // The code is CAPTURED for the trace — this poll runs at 1 Hz (400 ms
    // after an action) and a failing iteration used to vanish without a line,
    // only the on-screen state quietly going stale.
    const int code = http.GET();
    if (code != 200) {
        sce::trace::log("http", "GET /api/states/%s -> %d en %lu ms", entityId,
                        code, (unsigned long)(millis() - t0));
        http.end(); return false;
    }
    JsonDocument filter;                    // root = ONE object
    fillFilter(filter.to<JsonObject>());
    sce::psAlloc.reset();
    JsonDocument doc(&sce::psAlloc);
    YieldingReader rd(http.getStream(), http.getSize(), isChunked(http));
    DeserializationError err = deserializeJson(doc, rd,
                                               DeserializationOption::Filter(filter),
                                               DeserializationOption::NestingLimit(JSON_DEPTH));
    http.end();
    if (err) {
        sce::trace::log("http", "GET /api/states/%s -> 200 mais json '%s' "
                        "en %lu ms", entityId, err.c_str(),
                        (unsigned long)(millis() - t0));
        return false;
    }
    sce::trace::log("http", "GET /api/states/%s -> 200 en %lu ms", entityId,
                    (unsigned long)(millis() - t0));
    xSemaphoreTakeRecursive(gMtx, portMAX_DELAY);
    for (int i = 0; i < entCount; i++) {
        if (strcmp(ents[i].id, entityId)) continue;
        Entity& t = ents[i];
        strlcpy(t.state, doc["state"] | "", sizeof(t.state));
        applyAttrs(t, doc["attributes"]);
        uiDirty = true;
        break;
    }
    xSemaphoreGiveRecursive(gMtx);
    return true;
}

// Thumbnail decoding. OWNED BY netTask: done from loop(), it froze
// M5.update(), the touch input and the SceGuest server for several hundred
// ms every 3 s — swipes swallowed, including the exit one (max review 07-28,
// rule A2.22 on the frame budget). netTask, on its side, has nothing
// interactive to serve.
static void camDecode() {
    uint8_t* raw; size_t rawLen; char forWho[48];
    xSemaphoreTakeRecursive(gMtx, portMAX_DELAY);
    raw = camNext; rawLen = camNextLen;
    strlcpy(forWho, camNextFor, sizeof(forWho));
    camNext = nullptr; camNextLen = 0;
    xSemaphoreGiveRecursive(gMtx);
    if (!raw) return;
    if (!camImg.width()) {               // 320x240x16 bits = 150 KB of PSRAM
        camImg.setColorDepth(16);
        camImg.setPsram(true);
        camImg.createSprite(320, 240);
    }
    camImgOk = false;                    // loop() must not display while writing
    if (camImg.width()) {
        camImg.fillSprite(C_BG);
        int iw = 0, ih = 0;
        ImgFmt f = imgFmt(raw, rawLen);
        bool haveSize = (f == IMG_JPG) ? jpegSize(raw, rawLen, iw, ih)
                      : (f == IMG_PNG) ? pngSize (raw, rawLen, iw, ih) : false;
        float sc = 1.0f;
        int dx = 0, dy = 0, dw = 320, dh = 240;
        if (haveSize) {
            float sx = 320.0f / (float)iw, sy = 240.0f / (float)ih;
            sc = (sx < sy) ? sx : sy;
            dw = (int)((float)iw * sc + 0.5f);
            dh = (int)((float)ih * sc + 0.5f);
            if (dw > 320) dw = 320;
            if (dh > 240) dh = 240;
            dx = (320 - dw) / 2;
            dy = (240 - dh) / 2;
        }
        bool okDraw = false;
        if      (f == IMG_JPG) okDraw = camImg.drawJpg(raw, rawLen, dx, dy, dw, dh, 0, 0, sc, sc);
        else if (f == IMG_PNG) okDraw = camImg.drawPng(raw, rawLen, dx, dy, dw, dh, 0, 0, sc, sc);
        else strlcpy(camErr, sce::T("unknown image format",
                                    "format d'image non reconnu"),
                     sizeof(camErr));
        if (okDraw) {
            strlcpy(camImgFor, forWho, sizeof(camImgFor));
            camErr[0] = '\0';
            camImgOk = true;
        } else if (!camErr[0]) {
            strlcpy(camErr, sce::T("unreadable image", "image illisible"),
                    sizeof(camErr));
        }
    }
    free(raw);
    uiDirty = true;
}

static void netTask(void*) {
    uint32_t lastPollMs = 0;
    for (;;) {
        if (netGuard.shouldPark()) continue;   // reflash: park (ACK inside)
        if (WiFi.status() == WL_CONNECTED) {
            // ---- action requested by the UI ----
            // The queue is DRAINED entirely, before anything else: the user's
            // press comes before entity discovery.
            while (actPending()) {
                Action a;
                xSemaphoreTakeRecursive(gMtx, portMAX_DELAY);
                a = actQ[actTail];
                actTail = (uint8_t)((actTail + 1) % ACT_Q);
                xSemaphoreGiveRecursive(gMtx);
                // One single entity per call: bulk actions go through a Home
                // Assistant GROUP, which is itself an ordinary entity of the
                // list.
                if (a.entity[0])
                    callService(a.domain, a.service,
                                String("\"") + a.entity + "\"", a.extra);
                // NO full poll here: it takes about fifteen seconds on a real
                // installation and blocked netTask, hence the next action AND
                // the position refresh (user report 07-28: "big lag", frozen
                // position). The close tracking of the displayed entity is
                // enough.
                liveUntil = millis() + 10000;
            }
            // ---- camera thumbnail ----
            if (camReq) {
                camReq = false;
                char id[48] = "";
                xSemaphoreTakeRecursive(gMtx, portMAX_DELAY);
                int seen = 0;
                for (int i = 0; i < entCount; i++)
                    if (ents[i].cat == CAT_CAMERA) {
                        if (seen == curIdx) { strlcpy(id, ents[i].id, sizeof(id)); break; }
                        seen++;
                    }
                xSemaphoreGiveRecursive(gMtx);
                // The error message belongs to the CURRENT camera: kept as
                // is, it was attributed to the next one.
                if (id[0] && strcmp(camImgFor, id)) camErr[0] = '\0';
                if (id[0] && fetchCamera(id)) camDecode();
            }
            // ---- close tracking of the DISPLAYED entity ----
            uint32_t now = millis();
            static uint32_t liveMs = 0;
            bool fast = (int32_t)(liveUntil - now) > 0;
            // Two SCALARS under a short lock rather than a copy of the whole
            // Config (316 bytes, token included) on every loop iteration.
            bool     ssl;
            uint32_t pollS;
            {
                Lock lk;
                ssl   = cfg.ssl;
                pollS = cfg.pollS;
            }
            // Over HTTPS every request redoes a TLS handshake (~40 KB of
            // internal heap, 300-800 ms): at 2.5 Hz the heap collapses and
            // the full poll fails in turn. So we space the tracking out when
            // `ssl` is active — smoothness is not worth stability.
            uint32_t period = fast ? (ssl ? 1200u : 400u)
                                   : (ssl ? 3000u : 1000u);
            if (now - liveMs >= period) {
                liveMs = now;
                char id[48] = "";
                // `screen`, `curCat` and `curIdx` are read TOGETHER under the
                // same lock: otherwise we could poll the entity of a screen
                // we had just left.
                xSemaphoreTakeRecursive(gMtx, portMAX_DELAY);
                int idx = -1, seen = 0;
                if (screen == SC_LIST && curCat != CAT_CAMERA)
                    for (int i = 0; i < entCount; i++)
                        if (ents[i].cat == curCat) {
                            if (seen == curIdx) { idx = i; break; }
                            seen++;
                        }
                if (idx >= 0) strlcpy(id, ents[idx].id, sizeof(id));
                xSemaphoreGiveRecursive(gMtx);
                if (id[0]) fetchOneState(id);
            }
            // ---- camera thumbnail: refreshed AUTOMATICALLY (user 07-28, the
            // REFRESH button is gone). Slow rate: a JPEG image weighs tens of
            // KB, hammering it would saturate the link.
            static uint32_t camMs = 0;
            if (screen == SC_LIST && curCat == CAT_CAMERA &&
                now - camMs >= 3000) {
                camMs = now;
                camReq = true;
            }
            // ---- FULL refresh (entity discovery) ----
            // The entire catalog of a real installation takes about fifteen
            // seconds to download and parse, and DURING that time the
            // tracking of the displayed entity is stopped — netTask is
            // alone. So the interval adapts to what discovery REALLY costs:
            // never more than a fifth of the time. Entities do not appear
            // every ten seconds; their STATE goes through the close tracking
            // (measured on HW 07-28).
            // The full catalog takes about fifteen seconds on a real
            // installation: repeating it in the background blocked netTask,
            // hence the commands AND the position refresh. Entities do not
            // appear every minute — once known, only the state of the one
            // being WATCHED is re-read (user 07-28).
            // `poll_s = 0` (default): no periodicity, discovery at startup
            // and via the REFRESH button of the settings panel.
            // A discovery that FAILS must be RETRIED: without periodicity
            // (poll_s = 0), a first attempt failing on weak WiFi left the bin
            // at zero entities forever, with no recourse other than the
            // REFRESH button (defect introduced then observed on 07-28,
            // RSSI -85). Retry with a growing backoff, capped at 60 s.
            static uint32_t retryAt = 0, backoff = 0;
            bool due = pollNow || !lastPollMs;
            if (!due && retryAt && (int32_t)(now - retryAt) >= 0) due = true;
            if (!due && pollS > 0) {
                uint32_t gap = pollS * 1000UL;
                if (pollDurMs * 5 > gap) gap = pollDurMs * 5;
                due = (now - lastPollMs >= gap);
            }
            if (due) {
                pollNow = false;
                retryAt = 0;
                pollBusy = true;  uiDirty = true;
                uint32_t t0 = millis();
                bool ok = fetchStates();
                lastPollMs = millis();       // measured AFTER: see pollGapMs
                pollDurMs = lastPollMs - t0;
                pollBusy = false; uiDirty = true;
                if (ok) {
                    backoff = 0;
                } else if (!actPending()) {   // an INTENTIONAL interruption
                    backoff = backoff ? (backoff * 2) : 5000;  // does not count
                    if (backoff > 60000) backoff = 60000;
                    retryAt = millis() + backoff;
                    Serial.printf("[ha] nouvelle tentative dans %lus\n",
                                  (unsigned long)(backoff / 1000));
                    sce::trace::log("http", "decouverte echouee (http=%d), "
                                    "backoff %lu s", httpStatus,
                                    (unsigned long)(backoff / 1000));
                } else {
                    retryAt = millis() + 1000;   // action has priority: we
                }                                // resume right after
                Serial.printf("[ha] states %s http=%d entites=%d/%d %ld o en %lums\n",
                              ok ? "ok" : "echec", httpStatus, entCount, entSeen,
                              lastBodyLen, (unsigned long)pollDurMs);
                // A truncation is NAMED, and named per category: "64 entities"
                // alone reads as a complete list, and the screen has no room
                // for the detail of which ones are missing.
                if (ok && entDrop)
                    Serial.printf("[ha] TRONQUE : %d entites ecartees "
                                  "(volets:%d lumieres:%d prises:%d cameras:%d) "
                                  "- epingler celles qui comptent avec la cle "
                                  "'entities' (docs/guests/HA-REMOTE.md)\n",
                                  entDrop, entDropCat[CAT_COVER],
                                  entDropCat[CAT_LIGHT], entDropCat[CAT_SWITCH],
                                  entDropCat[CAT_CAMERA]);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

// ===========================================================================
// Display helpers
// ===========================================================================
static int catCount(int cat) {
    int n = 0;
    for (int i = 0; i < entCount; i++) if (ents[i].cat == cat) n++;
    return n;
}
// How many are ACTIVE in the category: covers open, lamps and switches on. A
// plain count says nothing about the state of the house — yet that is the
// question one asks upon arriving (user 07-28).
static bool isOn(const Entity& e);
static int catOn(int cat) {
    int n = 0;
    for (int i = 0; i < entCount; i++)
        if (ents[i].cat == cat && isOn(ents[i])) n++;
    return n;
}

// ABSOLUTE index of the n-th entity of the category (-1 if absent)
static int catNth(int cat, int nth) {
    int seen = 0;
    for (int i = 0; i < entCount; i++)
        if (ents[i].cat == cat) { if (seen == nth) return i; seen++; }
    return -1;
}
static void say(const String& m) { toast = m; toastMs = millis(); uiDirty = true; }

// "Active" state of an entity, for the colored dot
static bool isOn(const Entity& e) {
    return !strcmp(e.state, "on") || !strcmp(e.state, "open") ||
           !strcmp(e.state, "opening");
}
// Readable label for the state (Home Assistant reports raw English states).
// Drawn with efontJA_12 (see drawList), the only Unicode face: the French
// here KEEPS its accents.
static const char* stateLabel(const Entity& e) {
    if (!strcmp(e.state, "open"))        return sce::T("open",        "ouvert");
    if (!strcmp(e.state, "closed"))      return sce::T("closed",      "fermé");
    if (!strcmp(e.state, "opening"))     return sce::T("opening...",  "ouverture...");
    if (!strcmp(e.state, "closing"))     return sce::T("closing...",  "fermeture...");
    if (!strcmp(e.state, "on"))          return sce::T("on",          "allumé");
    if (!strcmp(e.state, "off"))         return sce::T("off",         "éteint");
    if (!strcmp(e.state, "unavailable")) return sce::T("unavailable", "indisponible");
    if (!strcmp(e.state, "idle"))        return sce::T("idle",        "au repos");
    return e.state;                      // unmapped: shown as HA sends it
}

// Rounded button, the project's single geometry (h 30, r 3)
static void button(int x, int w, int y, const char* label, uint16_t col,
                   bool fill) {
    if (fill) { canvas.fillRoundRect(x, y, w, 30, 3, col);
                canvas.setTextColor(TFT_BLACK, col); }
    else      { canvas.fillRoundRect(x, y, w, 30, 3, C_CARD);
                canvas.drawRoundRect(x, y, w, 30, 3, col);
                canvas.setTextColor(col, C_CARD); }
    canvas.setTextSize(1);
    canvas.setCursor(x + (w - (int)strlen(label) * 6) / 2, y + 11);
    canvas.print(label);
}

// ---- VERTICAL geometry of the list screen. ONE table, read by the drawing
// code AND by the touch hit test — and tuned so that the bottom status line
// NEVER covers the buttons: "Home Assistant : erreur 401" used to print
// across ALLUMER / ETEINDRE, that is precisely when the user needs them
// (review 07-28).
static constexpr int Y_NAME  = 38;    // name, size 2    → 38..54
static constexpr int Y_STATE = 62;    // state, size 1   → 62..70
// (Y_SLD0/Y_CAM/H_CAM/Y_PAL removed: the lamp lays itself out via
//  lightRows() and the camera takes the whole screen. Keeping them invited
//  reuse as if they were authoritative — the drawing/touch divorce that
//  lightRows() has just eliminated.)
static constexpr int Y_RAIL  = 178;   // position rail
static constexpr int Y_BTN   = 188;   // buttons h 30    → 188..218
static constexpr int H_BTN   = 30;
// ---- COVER: the control mirrors the REAL MOVEMENT. A cover goes up and
// down; its position is therefore read and set VERTICALLY, and the three
// commands stack in the order of the gesture — OPEN at the top, CLOSE at the
// bottom. The horizontal slider and the row of buttons forced a mental
// translation (user 07-28).
// The whole usable height, from below the state down to above the status
// line. The three commands are CONTIGUOUS, separated by a single rule: the
// 6 px gap between buttons only served to shrink targets aimed at with a
// finger (user 07-28).
static constexpr int VOL_X = 22, VOL_Y = 76, VOL_W = 64, VOL_H = 144;
static constexpr int VB_X = 172, VB_W = 130, VB_Y = 76, VB_H = 48;
static inline int vbY(int i) { return VB_Y + i * VB_H; }
// HOME: one single table, read by the drawing code AND the touch hit test.
static constexpr int HOME_X0 = 8,  HOME_Y0 = 36, HOME_W = 148, HOME_H = 76;
static constexpr int HOME_DX = 156, HOME_DY = 82;   // 36..112 then 118..194
static constexpr int HOME_STAT = 204;               // status banner
static constexpr int Y_STAT  = 226;   // status line     → 226..234

// ---- Sliders: the value is changed LOCALLY during the drag and is only
// sent on RELEASE — one HA service call per frame would saturate the
// installation (lesson from the zoom that wrote to the SD card on every
// frame, 07-27).
static constexpr int SLD_X0 = 96, SLD_X1 = 300;
enum Drag { DRAG_NONE = 0, DRAG_POS, DRAG_BRIGHT, DRAG_TEMP };
// Action the power button ANNOUNCES, frozen at draw time.
static bool  btnTurnOn = true;
static Drag  dragKind = DRAG_NONE;
static int   dragVal  = 0;          // value being dragged (preview)
// Entity GRABBED on first contact. Without it, an interrupted drag (exit
// modal eating the release frame, entity gone from a poll) left `dragKind`
// armed: the next tap, on ANOTHER entity, sent the value of the previous
// drag — a cover moved without anyone touching it (review 07-28).
static char  dragEnt[48] = "";
static void  cancelDrag() {
    dragKind = DRAG_NONE; dragEnt[0] = '\0';
    guest.setSwipeExit(true);          // always given back — see drag grab
}

// Step of 32 and not 30: the value of a track prints below it (y+18..y+26)
// and the touch band of the NEXT one overlapped it by 3 px — a finger placed
// on the "42 %" of the "Puissance" (brightness) row armed the "Temperature"
// drag (review 07-28).
// Disjoint bands: 76..96 / 108..128.

// `shown` is a char*: the drawing path runs at the rate of loop() during a
// drag, and temporary String objects allocated on the INTERNAL heap — the
// very one PsAlloc works to spare (review 07-28).
static void sliderAt(int y, const char* label, int val, int lo, int hi,
                     const char* shown, uint16_t col) {
    canvas.setTextSize(1);
    canvas.setTextColor(C_MUT);
    canvas.drawString(label, 8, y + 2);
    int span = (hi > lo) ? (hi - lo) : 1;
    int hx = SLD_X0 + (int)((int64_t)(val - lo) * (SLD_X1 - SLD_X0) / span);
    if (hx < SLD_X0) hx = SLD_X0;
    if (hx > SLD_X1) hx = SLD_X1;
    // A2.22: two SIMILAR drawing calls in the same body and GCC 8.4 Xtensa
    // drops the second one (X Dead lesson 07-25) — the track would have
    // stayed empty, value invisible. Hollow track then filled track, knob
    // then its center: ONE single call site, in a loop.
    const int      trkW[2] = { SLD_X1 - SLD_X0, hx - SLD_X0 };
    const uint16_t trkC[2] = { C_CARD, col };
    for (int i = 0; i < 2; i++)
        canvas.fillRoundRect(SLD_X0, y + 2, trkW[i], 8, 3, trkC[i]);
    const int      dotR[2] = { 9, 3 };
    const uint16_t dotC[2] = { col, (uint16_t)TFT_BLACK };
    for (int i = 0; i < 2; i++)
        canvas.fillCircle(hx, y + 6, dotR[i], dotC[i]);
    canvas.setTextColor(col);
    canvas.setTextDatum(textdatum_t::top_right);
    canvas.drawString(shown, 312, y + 18);
    canvas.setTextDatum(textdatum_t::top_left);
}


// ---- LAMP: ADAPTIVE layout. A plain lamp, a dimmable one and an RGB one do
// not have the same controls; fixed positions left large voids on the first
// ones and crammed the last ones. The blocks therefore stack according to
// what the entity CAN do, and this table is read by the drawing code AND by
// the touch hit test — that is the only way they do not diverge (user
// 07-28).
struct LightRows { int yDim, yTemp, yPal, yBtn; };
static LightRows lightRows(const Entity& e) {
    LightRows r { -1, -1, -1, 0 };
    int y = 74;
    if (e.canDim)   { r.yDim  = y; y += 42; }
    if (e.canTemp)  { r.yTemp = y; y += 42; }
    if (e.canColor) { r.yPal  = y; y += 44; }
    // The buttons sit AT THE BOTTOM whatever happens: their place must not
    // depend on the lamp, it is the most frequent gesture.
    r.yBtn = 192;
    return r;
}
// Color swatches: 8 cells of 34x30 (instead of 22x22 — a 22 px target is
// missed by a finger).
static constexpr int PAL_W = 34, PAL_H = 30, PAL_STEP = 38, PAL_X0 = 8;

// Color palette: 8 tappable swatches
static constexpr int PAL_N = 8;
static const uint8_t PALETTE[PAL_N][3] = {
    { 255, 147,  41 },   // warm white
    { 255, 255, 255 },   // white
    { 255,  60,  60 },   // red
    { 255, 150,  40 },   // orange
    { 255, 230,  80 },   // yellow
    {  70, 220, 110 },   // green
    {  60, 170, 255 },   // blue
    { 190,  90, 255 },   // purple
};
static uint16_t rgb565(const uint8_t c[3]) {
    return (uint16_t)(((c[0] & 0xF8) << 8) | ((c[1] & 0xFC) << 3) | (c[2] >> 3));
}

static void header(const char* title, const char* right) {
    canvas.setTextSize(2);
    canvas.setTextColor(C_ACC);
    canvas.drawString(title, 8, 6);
    if (right) {
        canvas.setTextSize(1);
        canvas.setTextColor(C_MUT);
        canvas.setTextDatum(textdatum_t::top_right);
        canvas.drawString(right, 312, 12);
        canvas.setTextDatum(textdatum_t::top_left);
    }
    // A2.22: two mirrored fillRect — one single call site.
    const uint16_t hdrC[2] = { C_ACC, C_ACC2 };   // cyan -> indigo rule
    for (int i = 0; i < 2; i++)
        canvas.fillRect(i * 160, 28, 160, 2, hdrC[i]);
}

// ===========================================================================
// Rendering
// ===========================================================================
// Category icons, drawn with primitives: a bare word forces you to READ, a
// shape is recognized at a glance — that is what makes the difference on a
// screen glanced at in passing (user 07-28).
static __attribute__((noinline))
void catIcon(int cat, int x, int y, uint16_t col) {
    switch (cat) {
    case CAT_COVER:                                   // slatted window
        canvas.drawRoundRect(x, y, 22, 20, 2, col);
        for (int i = 0; i < 3; i++)
            canvas.drawFastHLine(x + 3, y + 5 + i * 5, 16, col);
        break;
    case CAT_LIGHT:                                   // bulb + rays
    {
        canvas.fillCircle(x + 11, y + 8, 6, col);
        canvas.fillRect(x + 8, y + 14, 7, 5, col);
        canvas.drawFastHLine(x + 7, y + 20, 9, col);
        canvas.drawFastVLine(x + 11, y - 1, 3, col);
        const int rayX[2] = { x - 1, x + 21 };   // A2.22: mirrored pair
        for (int k = 0; k < 2; k++)
            canvas.drawFastHLine(rayX[k], y + 8, 3, col);
        break;
    }
    case CAT_SWITCH:                                  // power on/off symbol
        canvas.drawCircle(x + 11, y + 11, 9, col);
        canvas.fillRect(x + 10, y + 1, 3, 10, C_CARD);
        canvas.drawFastVLine(x + 11, y + 2, 9, col);
        break;
    default:                                          // body + lens
        canvas.drawRoundRect(x, y + 4, 22, 15, 2, col);
        canvas.fillRect(x + 5, y + 1, 7, 4, col);
        canvas.drawCircle(x + 11, y + 11, 4, col);
        break;
    }
}

static void drawHome() {
    char n[28];
    // "64 of 213" and not "64 entities": the second is a true sentence about a
    // list that is not the house. The old form appended a "+" — a mark nobody
    // reads as "a hundred and forty devices are not on this screen".
    if (entDrop) snprintf(n, sizeof(n), sce::T("%d of %d", "%d sur %d"),
                          entCount, entSeen);
    else         snprintf(n, sizeof(n), sce::T("%d entities", "%d entites"),
                          entCount);
    header(sce::T("HOME", "MAISON"), n);
    // Four 2x2 cards. Each carries its ICON, its name and its count: the
    // number is the information one comes looking for, so it is the biggest
    // and right-aligned, where the eye finds it again from card to card.
    for (int c = 0; c < CAT_N; c++) {
        int x = HOME_X0 + (c % 2) * HOME_DX;
        int y = HOME_Y0 + (c / 2) * HOME_DY;
        int cnt = catCount(c);
        bool empty = (cnt == 0);
        uint16_t line = empty ? C_BORD : C_ACC;
        uint16_t txt  = empty ? C_DIM  : C_TXT;
        canvas.fillRoundRect(x, y, HOME_W, HOME_H, 3, C_CARD);
        canvas.drawRoundRect(x, y, HOME_W, HOME_H, 3, line);
        catIcon(c, x + 12, y + 12, line);
        canvas.setFont(&fonts::efontJA_12);
        canvas.setTextSize(1);
        canvas.setTextColor(txt, C_CARD);
        canvas.drawString(catName(c), x + 12, y + HOME_H - 20);
        canvas.setFont(&fonts::Font0);
        // "3/6": active out of total. Cameras have no on state, they only
        // show their count.
        canvas.setTextDatum(textdatum_t::top_right);
        if (c == CAT_CAMERA || empty) {
            canvas.setTextSize(2);
            canvas.setTextColor(empty ? C_DIM : C_ACC, C_CARD);
            canvas.drawString(String(cnt), x + HOME_W - 12, y + HOME_H - 26);
        } else {
            int on = catOn(c);
            char tot[8];
            snprintf(tot, sizeof(tot), "/%d", cnt);
            // The total small and gray: what the eye looks for is the number
            // of ACTIVE ones.
            canvas.setTextSize(1);
            canvas.setTextColor(C_MUT, C_CARD);
            canvas.drawString(tot, x + HOME_W - 12, y + HOME_H - 18);
            int tw = (int)strlen(tot) * 6;
            canvas.setTextSize(2);
            canvas.setTextColor(on ? C_OK : C_DIM, C_CARD);
            canvas.drawString(String(on), x + HOME_W - 14 - tw, y + HOME_H - 26);
        }
        // "+9" in the top-right corner: this category has entities the table
        // could not hold. It belongs HERE and not only in a global counter —
        // the number just drawn underneath is the one the user will read as
        // "how many covers I own", and it is not that number. `empty` is
        // precisely the case that used to lie best: a category truncated down
        // to zero looked exactly like a category one does not own.
        if (entDropCat[c]) {
            char more[8];
            snprintf(more, sizeof(more), "+%d", entDropCat[c]);
            canvas.setTextSize(1);
            canvas.setTextColor(C_KO, C_CARD);
            canvas.drawString(more, x + HOME_W - 8, y + 8);
        }
        canvas.setTextDatum(textdatum_t::top_left);
    }
    // Status banner: the health of the link BEFORE the gestures — that is
    // the first question one asks when a command does not go through.
    bool up = (httpStatus == 200 && lastOkMs);
    canvas.fillCircle(14, HOME_STAT + 4, 4, up ? C_OK : C_KO);
    canvas.setTextSize(1);
    canvas.setTextColor(up ? C_OK : C_KO);
    canvas.drawString(pollBusy ? sce::T("reading the list...",
                                        "lecture de la liste...")
                     : up      ? sce::T("Home Assistant connected",
                                        "Home Assistant connecte")
                               : sce::T("Home Assistant unreachable",
                                        "Home Assistant injoignable"),
                      26, HOME_STAT);
    canvas.setTextColor(C_DIM);
    canvas.setTextDatum(textdatum_t::top_center);
    // Widths, drawn centred on x = 160 in Font0 at text size 1, so 6 px per
    // character and the string spans 160 +/- width/2 on a 320 px screen:
    //   EN  48 chars = 288 px -> 16 .. 304        fits, 16 px of margin
    //   FR  55 chars = 330 px -> -5 .. 325        ran off BOTH edges
    //   FR  43 chars = 258 px -> 31 .. 289        fits (this line)
    // The French form lost the repeated "glisser" on the second gesture, not
    // the gesture itself: the verb is stated once and carries over.
    canvas.drawString(sce::T("swipe right : settings    -    swipe down : quit",
                             "glisser droite : reglages  -  bas : quitter"),
                      160, HOME_STAT + 14);
    canvas.setTextDatum(textdatum_t::top_left);
}

// Defined further down (Actions section): drawList fixes up the rank when
// the list shrinks, and curId must follow.
static void rememberTarget();

static __attribute__((noinline)) void drawList() {
    int cnt = catCount(curCat);
    // Clamp BEFORE displaying the rank: a shrinking catalog left "5/3" for
    // one frame, contradicting the entity drawn underneath.
    if (curIdx >= cnt) { curIdx = cnt ? cnt - 1 : 0; rememberTarget(); }
    char pos[20];
    // "3/12 +9": the rank, the length of the list, and the fact that the list
    // is not the house. Swiping to the last card is exactly the moment one
    // concludes "that is all of them".
    if (entDropCat[curCat])
        snprintf(pos, sizeof(pos), "%d/%d +%d", cnt ? curIdx + 1 : 0, cnt,
                 entDropCat[curCat]);
    else
        snprintf(pos, sizeof(pos), "%d/%d", cnt ? curIdx + 1 : 0, cnt);
    header(catName(curCat), pos);
    // Cameras: the name is displayed IN the header, the image takes the rest.
    if (curCat == CAT_CAMERA && cnt) {
        int ci = catNth(curCat, curIdx);
        if (ci >= 0) {
            canvas.setFont(&fonts::efontJA_12);
            canvas.setTextColor(C_TXT);
            canvas.drawString(ents[ci].name, 108, 8);
            canvas.setFont(&fonts::Font0);
        }
    }

    if (!cnt) {
        canvas.setTextSize(1);
        canvas.setTextColor(C_MUT);
        canvas.setTextDatum(textdatum_t::top_center);
        // An EMPTY category that was truncated to zero must not be shown as
        // "you have none of these": that is the one sentence the cap made the
        // screen say without knowing it.
        if (entDropCat[curCat]) {
            char msg[64];
            snprintf(msg, sizeof(msg),
                     sce::T("%d not loaded - list too long (see settings)",
                            "%d non chargees - liste trop longue (reglages)"),
                     entDropCat[curCat]);
            canvas.setTextColor(C_KO);
            canvas.drawString(msg, 160, 110);
        } else {
            canvas.drawString(entCount
                              ? sce::T("no entity in this category",
                                       "aucune entite dans cette categorie")
                              : sce::T("waiting for Home Assistant...",
                                       "en attente de Home Assistant..."),
                              160, 110);
        }
        canvas.setTextDatum(textdatum_t::top_left);
        button(8, 140, Y_BTN, sce::T("HOME", "ACCUEIL"), C_MUT, false);
        return;
    }
    int idx = catNth(curCat, curIdx);
    if (idx < 0) { curIdx = 0; idx = catNth(curCat, 0); rememberTarget(); }
    const Entity& e = ents[idx];

    if (curCat == CAT_CAMERA) {
        // FULL SCREEN: a camera view has no reason to live in a thumbnail.
        // The image occupies the 320x240 and the text goes OVER it, on a dark
        // banner (user 07-28).
        // Plain copy. Decoding happens in netTask (camDecode); `camImgOk` is
        // false while the sprite is being written, so we never display a
        // half-decoded image.
        bool shown = camImgOk && camImg.width() && !strcmp(camImgFor, e.id);
        if (shown) camImg.pushSprite(&canvas, 0, 0);
        if (!shown) {
            canvas.setTextSize(1);
            canvas.setTextColor(C_MUT);
            canvas.setTextDatum(textdatum_t::middle_center);
            canvas.drawString(camErr[0] ? camErr
                                        : sce::T("loading the image...",
                                                 "chargement de l'image..."),
                              160, 120);
            canvas.setTextDatum(textdatum_t::top_left);
        }
        // Banner: name + rank, READABLE whatever the image underneath.
        canvas.fillRect(0, 0, 320, 20, C_BG);
        canvas.setFont(&fonts::efontJA_12);
        canvas.setTextColor(C_TXT, C_BG);
        canvas.drawString(e.name, 6, 4);
        canvas.setFont(&fonts::Font0);
        canvas.setTextColor(C_MUT, C_BG);
        canvas.setTextDatum(textdatum_t::top_right);
        canvas.drawString(pos, 314, 6);
        canvas.setTextDatum(textdatum_t::top_left);
        if (cnt > 1 && cnt <= 24) {
            int w = cnt * 10, x0 = 160 - w / 2;
            canvas.fillRect(x0 - 6, 228, w + 12, 12, C_BG);
            for (int i = 0; i < cnt; i++)
                canvas.fillCircle(x0 + i * 10 + 4, 234, i == curIdx ? 4 : 2,
                                  i == curIdx ? C_ACC : C_BORD);
        }
        return;                       // no rail, no buttons, no status
    } else {
        // Name + state: the essentials read in one second
        canvas.setTextSize(2);
        canvas.setTextColor(C_TXT);
        // Unicode font for the NAME and the STATE: Home Assistant entities
        // are named in French here ("Volet sejour") and the 6x8 Font0
        // rendered the accents as "??" (user report 07-28). The rest of the
        // UI keeps Font0, whose fixed metrics align the buttons.
        canvas.setFont(&fonts::efontJA_12);
        canvas.setTextSize(1);
        canvas.drawString(e.name, 8, Y_NAME);
        canvas.fillCircle(14, Y_STATE + 4, 6, isOn(e) ? C_OK : C_DIM);
        canvas.setTextColor(isOn(e) ? C_OK : C_MUT);
        canvas.drawString(stateLabel(e), 28, Y_STATE - 2);
        canvas.setFont(&fonts::Font0);

        // ---- Continuous controls: we display ONLY the CAPABILITIES declared
        // by the entity, and never a control inferred from a missing value.
        char sv[16];
        if (curCat == CAT_COVER && e.pos >= 0) {
            int v = (dragKind == DRAG_POS) ? dragVal : e.pos;
            // Window seen from the front: the curtain comes down FROM THE
            // TOP. 100 % = open (nothing masked), 0 % = closed (all masked).
            int shut = VOL_H * (100 - v) / 100;
            canvas.fillRoundRect(VOL_X, VOL_Y, VOL_W, VOL_H, 3, C_BG);
            if (shut > 0)
                canvas.fillRoundRect(VOL_X, VOL_Y, VOL_W, shut, 3, C_ACC);
            // Slats: without them the curtain is just a solid rectangle
            for (int ly = VOL_Y + 6; ly < VOL_Y + shut - 1; ly += 7)
                canvas.drawFastHLine(VOL_X + 3, ly, VOL_W - 6, C_BG);
            canvas.drawRoundRect(VOL_X, VOL_Y, VOL_W, VOL_H, 3, C_BORD);
            canvas.drawFastHLine(VOL_X - 4, VOL_Y + shut, VOL_W + 8, C_TXT);
            canvas.setTextSize(2);
            canvas.setTextColor(C_ACC);
            snprintf(sv, sizeof(sv), "%d %%", v);
            canvas.drawString(sv, VOL_X + VOL_W + 12, VOL_Y + VOL_H / 2 - 8);
            canvas.setTextSize(1);
            canvas.setTextColor(C_MUT);
            canvas.drawString(sce::T("drag", "glisser"),
                              VOL_X + VOL_W + 12, VOL_Y + VOL_H / 2 + 12);
        }
        if (curCat == CAT_LIGHT) {
            LightRows lr = lightRows(e);
            if (lr.yDim >= 0) {
                // Lamp off: HA no longer publishes `brightness`. The slider
                // stays, at zero — making it vanish when the light goes off
                // was precisely what prevented turning it back on gently.
                int cur = (e.bright >= 0) ? e.bright : 0;
                int v = (dragKind == DRAG_BRIGHT) ? dragVal : cur;
                snprintf(sv, sizeof(sv), "%d %%", (v * 100 + 127) / 255);
                sliderAt(lr.yDim, sce::T("Brightness", "Puissance"),
                         v, 0, 255, sv, C_ACC);
            }
            if (lr.yTemp >= 0) {
                int cur = (e.kelvin >= 0) ? e.kelvin : (e.kMin + e.kMax) / 2;
                int v = (dragKind == DRAG_TEMP) ? dragVal : cur;
                snprintf(sv, sizeof(sv), "%d K", v);
                // "Temperature" is spelled the same in both languages —
                // no pair to write, and none to keep in step.
                sliderAt(lr.yTemp, "Temperature", v, e.kMin, e.kMax, sv, C_ACC2);
            }
            if (lr.yPal >= 0) {
                canvas.setTextSize(1);
                canvas.setTextColor(C_MUT);
                canvas.drawString(sce::T("Color", "Couleur"),
                                  PAL_X0, lr.yPal - 10);
                for (int i = 0; i < PAL_N; i++) {
                    int x = PAL_X0 + i * PAL_STEP;
                    canvas.fillRoundRect(x, lr.yPal, PAL_W, PAL_H, 3,
                                         rgb565(PALETTE[i]));
                    bool sel = (e.rgb[0] == PALETTE[i][0] &&
                                e.rgb[1] == PALETTE[i][1] &&
                                e.rgb[2] == PALETTE[i][2]);
                    // WHITE inner ring: an outer border got lost against the
                    // neighboring swatches.
                    // A2.22: two nearly identical drawRoundRect back to back.
                    // Loop with a single call site.
                    if (sel) {
                        const int      off[2] = { 2, 3 };
                        const uint16_t rc[2]  = { (uint16_t)TFT_BLACK, C_TXT };
                        for (int k = 0; k < 2; k++)
                            canvas.drawRoundRect(x + off[k], lr.yPal + off[k],
                                                 PAL_W - 2 * off[k],
                                                 PAL_H - 2 * off[k], 3, rc[k]);
                    }
                }
            }
        }
    }

    // Rail: one dot per entity of the category. No rail for covers — the
    // command stack takes the whole height and the header already shows the
    // rank ("2/5").
    // ⚠ SKIP THE RAIL, NOT WHAT FOLLOWS: a `return` placed here also hid the
    // buttons drawn right after (user report 07-28).
    // A lamp with a palette occupies the rail area (swatches 158..188 against
    // a rail at 174..182): the rank is already readable in the header, as for
    // covers (review 07-28).
    bool railHidden = (curCat == CAT_COVER) ||
                      (curCat == CAT_LIGHT && cnt && lightRows(ents[catNth(curCat, curIdx) >= 0
                          ? catNth(curCat, curIdx) : 0]).yPal >= 0);
    if (!railHidden && cnt > 1 && cnt <= 24) {
        int w = cnt * 10, x0 = 160 - w / 2;
        int ry = Y_RAIL;   // the camera returns earlier, with its own rail
        for (int i = 0; i < cnt; i++)
            canvas.fillCircle(x0 + i * 10 + 4, ry, i == curIdx ? 4 : 2,
                              i == curIdx ? C_ACC : C_BORD);
    }

    // Actions: depend on the domain
    if (curCat == CAT_COVER) {
        // CONTINUOUS STACK, in the order of the gesture: up, stop, down.
        // Not `static`: a static local is initialized on the FIRST call, and
        // the language must be free to be read after that.
        const char* VLBL[3] = { sce::T("OPEN", "OUVRIR"), "STOP",
                                sce::T("CLOSE", "FERMER") };
        canvas.fillRoundRect(VB_X, VB_Y, VB_W, VB_H * 3, 3, C_CARD);
        canvas.drawRoundRect(VB_X, VB_Y, VB_W, VB_H * 3, 3, C_ACC);
        for (int i = 0; i < 3; i++) {
            int y  = vbY(i);
            int ty = y + VB_H / 2 - 4;
            uint16_t c = (i == 1) ? C_MUT : C_ACC;
            // A separating rule, no gap: the target stays full-size.
            if (i) canvas.drawFastHLine(VB_X + 8, y, VB_W - 16, C_BORD);
            // The ARROW gives the direction before one even reads the word.
            // A2.22: the two arrows used to be two MIRRORED fillTriangle in
            // the same body — exactly the pattern proven faulty on 07-25. One
            // single call site, the apex parameterized by the direction.
            if (i == 1) {
                canvas.fillRect(VB_X + 18, ty - 3, 12, 10, c);
            } else {
                int apex = (i == 0) ? ty - 5 : ty + 6;
                int base = (i == 0) ? ty + 6 : ty - 5;
                canvas.fillTriangle(VB_X + 24, apex, VB_X + 16, base,
                                    VB_X + 32, base, c);
            }
            canvas.setTextSize(1);
            canvas.setTextColor(c, C_CARD);
            canvas.setCursor(VB_X + 46, ty);
            canvas.print(VLBL[i]);
        }
    } else if (curCat == CAT_CAMERA) {
        // No button: automatic refresh, back out by swiping up.
    } else {
        int by = (curCat == CAT_LIGHT) ? lightRows(e).yBtn : Y_BTN;
        // ONE SINGLE button, offering the only USEFUL action: offering
        // "turn on" to an already-lit lamp is just noise, and the full width
        // doubles the surface to aim at (user 07-28).
        // The INTENT is frozen at draw time: the state is re-read every
        // 400 ms, and deciding again at tap time could invert the action
        // relative to the word displayed — a reflex second press turned off
        // what had just been turned on (review 07-28).
        btnTurnOn = !isOn(e);
        button(6, 308, by, btnTurnOn ? sce::T("TURN ON",  "ALLUMER")
                                     : sce::T("TURN OFF", "ETEINDRE"),
               btnTurnOn ? C_ACC : C_MUT, btnTurnOn);
    }
}

static void draw() {
    xSemaphoreTakeRecursive(gMtx, portMAX_DELAY);
    canvas.fillSprite(C_BG);
    if (screen == SC_HOME) drawHome(); else drawList();

    // No "whole house" bulk action: Home Assistant already has the notion of
    // a GROUP (a `cover.*` entity that drives the others). It appears
    // naturally in the list and is operated like any cover — duplicating that
    // mechanism on the robot side would have ignored the user's own
    // configuration (user 07-28).

    // Brief message (result of an action) or diagnostic
    canvas.setTextSize(1);
    canvas.setTextDatum(textdatum_t::top_center);
    if (toast.length() && millis() - toastMs < 2500) {
        canvas.setTextColor(C_OK);
        canvas.drawString(toast, 160, Y_STAT);
    } else if (WiFi.status() != WL_CONNECTED) {
        canvas.setTextColor(C_KO);
        canvas.drawString(sce::T("WiFi disconnected", "WiFi deconnecte"),
                          160, Y_STAT);
    } else if (!cfg.host[0] || !cfg.token[0]) {
        // The ADDRESS, not just the instruction: this is the guaranteed state
        // on the very first launch, and without it one had to plug the USB
        // back in and read the serial console to know where to go (07-28).
        canvas.setTextColor(C_KO);
        char m[64];
        snprintf(m, sizeof(m), sce::T("to be set up: http://%s/config",
                                      "a configurer : http://%s/config"),
                 ipStr.c_str());
        canvas.drawString(m, 160, Y_STAT);
    } else if (!lastOkMs) {
        canvas.setTextColor(C_MUT);
        canvas.drawString(pollBusy ? sce::T("connecting to Home Assistant...",
                                            "connexion a Home Assistant...")
                                   : sce::T("waiting...", "en attente..."),
                          160, Y_STAT);
    } else if (httpStatus && httpStatus != 200) {
        canvas.setTextColor(C_KO);
        char em[40];
        snprintf(em, sizeof(em), sce::T("Home Assistant: error %d",
                                        "Home Assistant : erreur %d"),
                 httpStatus);
        canvas.drawString(em, 160, Y_STAT);
    }
    canvas.setTextDatum(textdatum_t::top_left);
    xSemaphoreGiveRecursive(gMtx);
    canvas.pushSprite(0, 0);
}

// ===========================================================================
// Settings panel — SWIPE RIGHT from the home screen
// ===========================================================================
// Same gesture as `flight-radar` (60 px threshold, horizontal dominant): a
// guest bin must not reinvent its gestures, otherwise each one has to be
// learned separately.
//
// Only the settings that are FINGER-ADJUSTABLE are here. The host and the
// token are not — a Home Assistant token is 180 characters long — and remain
// the business of the web page, whose address is recalled on screen.
static volatile bool settingsReq = false;

static void settingsPanel() {
    guest.setSwipeExit(false);              // the panel keeps every gesture
    // Snapshot UNDER THE LOCK: the web form writes cfg from another task.
    // Starting from half-read values, then REWRITING them on OK, silently
    // cancelled an entry made on /config (review 07-28).
    int lum, pol, ssl;
    char host[48]; unsigned port;
    {
        Lock lk;
        lum = (int)cfg.bright;
        pol = (int)cfg.pollS;
        ssl = (int)cfg.ssl;
        strlcpy(host, cfg.host, sizeof(host));
        port = (unsigned)cfg.port;
    }
    const int lum0 = lum, pol0 = pol, ssl0 = ssl;
    bool done = false, ok = false;

    // Single GEOMETRY, read by the drawing code AND the touch hit test.
    static constexpr int TRK0 = 28, TRK1 = 292;
    struct Row { int y; const char* lbl; int* v; int lo, hi, step; };
    Row rows[2] = {
        { 40, sce::T("Brightness", "Luminosite"),            &lum,  10,  255,  5 },
        { 86, sce::T("Auto re-discovery (s)",
                     "Re-decouverte auto (s)"),              &pol,   0, 3600, 60 },
    };
    auto band = [](const Row& r, int ty) {
        return ty >= r.y + 20 && ty <= r.y + 42;    // track at r.y + 26
    };
    // ACTIONS row: reloading the list is the MANUAL counterpart of the
    // automatic re-discovery just above — the two go together.
    static constexpr int ACT_X = 10,  ACT_Y = 142, ACT_W = 140, ACT_H = 26;
    static constexpr int SSL_X = 162, SSL_Y = 142, SSL_W = 140, SSL_H = 26;
    static constexpr int BTN_Y = 202, BTN_H = 30,  BTN_W = 130;
    static constexpr int CAN_X = 20,  OK_X  = 170;

    auto draw = [&]() {
        canvas.fillSprite(C_BG);
        canvas.setTextSize(2);
        canvas.setTextColor(C_ACC);
        canvas.drawString(sce::T("Settings", "Reglages"), 10, 6);
        for (int i = 0; i < 2; i++) {
            const Row& r = rows[i];
            canvas.setTextSize(1);
            canvas.setTextColor(C_MUT);
            canvas.drawString(r.lbl, 10, r.y);
            char v[12];
            if (r.v == &pol && pol == 0)
                strlcpy(v, sce::T("never", "jamais"), sizeof(v));
            else snprintf(v, sizeof(v), "%d", *r.v);
            canvas.setTextSize(2);
            canvas.setTextColor(C_ACC);
            canvas.setTextDatum(textdatum_t::top_right);
            canvas.drawString(v, 312, r.y - 4);
            canvas.setTextDatum(textdatum_t::top_left);
            int ty = r.y + 26;
            int hx = TRK0 + (*r.v - r.lo) * (TRK1 - TRK0) / (r.hi - r.lo);
            if (hx > TRK1) hx = TRK1;
            // A2.22: three pairs of mirrored drawing calls used to live here
            // (arrows, track, knob). The remedy was ported into flight-radar
            // pretending it came from here — it was not there. ONE single
            // call site per shape, in a loop.
            const int arrX[2] = { 20, 300 }, arrT[2] = { 8, 312 };
            for (int i = 0; i < 2; i++)
                canvas.fillTriangle(arrX[i], ty - 4, arrX[i], ty + 12,
                                    arrT[i], ty + 4, C_MUT);
            const int      trkW[2] = { TRK1 - TRK0, hx - TRK0 };
            const uint16_t trkC[2] = { C_CARD, C_ACC };
            for (int i = 0; i < 2; i++)
                canvas.fillRoundRect(TRK0, ty, trkW[i], 8, 3, trkC[i]);
            const int      dotR[2] = { 10, 3 };
            const uint16_t dotC[2] = { C_ACC, (uint16_t)TFT_BLACK };
            for (int i = 0; i < 2; i++)
                canvas.fillCircle(hx, ty + 4, dotR[i], dotC[i]);
        }
        canvas.setTextSize(1);
        canvas.setTextColor(C_MUT);
        canvas.drawString(sce::T("Entities", "Entites"), ACT_X, 130);
        canvas.drawString(sce::T("Link",     "Liaison"), SSL_X, 130);
        // IMMEDIATE action: reloading does not wait for OK, this is not a
        // value to validate but a command.
        int n, tot, drop;
        { Lock lk; n = entCount; tot = entSeen; drop = entDrop; }
        char al[26];
        if (pollBusy) strlcpy(al, sce::T("READING...", "LECTURE..."), sizeof(al));
        else if (drop) snprintf(al, sizeof(al),
                                sce::T("REFRESH (%d/%d)", "ACTUALISER (%d/%d)"),
                                n, tot);
        else snprintf(al, sizeof(al), sce::T("REFRESH (%d)", "ACTUALISER (%d)"), n);
        // The shortfall stated next to the label, in the panel one opens
        // precisely to ask "is my list complete?". The remedy — the `entities`
        // key — is edited on the /config page whose address is recalled below.
        if (drop) {
            char d[24];
            snprintf(d, sizeof(d), sce::T("%d dropped", "%d ecartees"), drop);
            canvas.setTextColor(C_KO, C_BG);
            canvas.setTextDatum(textdatum_t::top_right);
            canvas.drawString(d, SSL_X - 6, 130);
            canvas.setTextDatum(textdatum_t::top_left);
            canvas.setTextColor(C_MUT, C_BG);
        }
        canvas.fillRoundRect(ACT_X, ACT_Y, ACT_W, ACT_H, 3, C_CARD);
        canvas.drawRoundRect(ACT_X, ACT_Y, ACT_W, ACT_H, 3,
                             pollBusy ? C_MUT : C_ACC);
        canvas.setTextColor(pollBusy ? C_MUT : C_ACC, C_CARD);
        canvas.setCursor(ACT_X + (ACT_W - (int)strlen(al) * 6) / 2, ACT_Y + 9);
        canvas.print(al);
        canvas.fillRoundRect(SSL_X, SSL_Y, SSL_W, SSL_H, 3,
                             ssl ? C_ACC : C_CARD);
        if (!ssl) canvas.drawRoundRect(SSL_X, SSL_Y, SSL_W, SSL_H, 3, C_BORD);
        canvas.setTextColor(ssl ? TFT_BLACK : C_TXT, ssl ? C_ACC : C_CARD);
        // Centering computed from the label ACTUALLY drawn: the two French
        // forms happened to be the same 11 characters, the hard-coded 66 px
        // they stood for does not survive a second language.
        const char* sslLbl = ssl ? sce::T("HTTPS on",   "HTTPS actif")
                                 : sce::T("plain HTTP", "HTTP simple");
        canvas.setCursor(SSL_X + (SSL_W - (int)strlen(sslLbl) * 6) / 2, SSL_Y + 9);
        canvas.print(sslLbl);
        // Host and token are typed on a keyboard, not with a finger: we
        // SHOW where to do it rather than pretending to offer it here.
        canvas.setTextColor(C_MUT, C_BG);
        char l1[42];
        snprintf(l1, sizeof(l1), "%.24s:%u",
                 host[0] ? host : sce::T("(no host)", "(hote absent)"), port);
        canvas.drawString(l1, ACT_X, 176);
        canvas.setTextColor(C_DIM, C_BG);
        char l2[56];
        snprintf(l2, sizeof(l2), sce::T("host + token: http://%s/config",
                                        "hote + jeton : http://%s/config"),
                 ipStr.c_str());
        canvas.drawString(l2, ACT_X, 188);
        button(CAN_X, BTN_W, BTN_Y, sce::T("CANCEL", "ANNULER"), C_MUT, false);
        button(OK_X,  BTN_W, BTN_Y, "OK",  C_ACC, true);   // same in both
        canvas.pushSprite(0, 0);
    };

    draw();
    uint32_t t0 = millis();
    while (!done && millis() - t0 < 90000) {
        M5.update();
        guest.update();                       // the HTTP server stays alive
        auto t = M5.Touch.getDetail();
        bool handled = false;
        if (t.isPressed() && t.x >= TRK0 && t.x <= TRK1) {
            for (int i = 0; i < 2 && !handled; i++) {
                Row& r = rows[i];
                if (!band(r, t.y)) continue;
                t0 = millis();
                int v = r.lo + (t.x - TRK0) * (r.hi - r.lo) / (TRK1 - TRK0);
                v = (v / r.step) * r.step;
                if (v < r.lo) v = r.lo;
                if (v > r.hi) v = r.hi;
                if (v != *r.v) {
                    *r.v = v;
                    if (r.v == &lum) {        // A2.2: PMIC I2C, 10 Hz preview
                        static uint32_t last = 0;
                        if (millis() - last > 100) {
                            last = millis();
                            M5.Display.setBrightness((uint8_t)lum);
                        }
                    }
                    draw();
                }
                handled = true;
            }
        }
        if (!handled && t.wasClicked()) {
            t0 = millis();
            for (int i = 0; i < 2 && !handled; i++) {   // arrows ◄ ►
                Row& r = rows[i];
                if (!band(r, t.y)) continue;
                int v = *r.v;
                if      (t.x < TRK0) v -= r.step;
                else if (t.x > TRK1) v += r.step;
                else break;
                if (v < r.lo) v = r.lo;
                if (v > r.hi) v = r.hi;
                if (v != *r.v) {
                    *r.v = v;
                    if (r.v == &lum) M5.Display.setBrightness((uint8_t)lum);
                    draw();
                }
                handled = true;
            }
            if (handled) {
                // already handled
            } else if (t.y >= ACT_Y && t.y <= ACT_Y + ACT_H &&
                       t.x >= ACT_X && t.x <= ACT_X + ACT_W) {
                if (!pollBusy) {
                    sce::trace::log("ui", "reglages: redecouverte demandee");
                    pollNow = true;                // netTask picks it up
                }
                draw();
            } else if (t.y >= SSL_Y && t.y <= SSL_Y + SSL_H &&
                       t.x >= SSL_X && t.x <= SSL_X + SSL_W) {
                ssl = !ssl; draw();
            } else if (t.y >= BTN_Y && t.y <= BTN_Y + BTN_H) {
                if      (t.x >= OK_X  && t.x <= OK_X  + BTN_W) { done = ok = true; }
                else if (t.x >= CAN_X && t.x <= CAN_X + BTN_W) { done = true; }
            }
        }
        // Tick: the poll state and the entity counter change WITHOUT any
        // gesture; without a periodic redraw the button stayed on
        // "LECTURE..." forever.
        static uint32_t tick = 0;
        static bool wasBusy = false;
        if (millis() - tick > 400) {
            tick = millis();
            if (pollBusy != wasBusy) { wasBusy = pollBusy; draw(); }
        }
        delay(15);
    }
    // FINAL value applied in every case: the last step of the drag may have
    // been throttled out by the 10 Hz preview.
    M5.Display.setBrightness((uint8_t)(ok ? lum : lum0));
    if (ok) {
        // We rewrite ONLY what the user touched. Rewriting all three
        // fields cancelled an entry made meanwhile on the /config page —
        // which is precisely what the snapshot under lock claimed to
        // prevent, without managing it (review 07-29).
        Lock lk;
        bool changed = false;
        if (lum != lum0) { cfg.bright = (uint8_t)lum;  changed = true; }
        if (pol != pol0) { cfg.pollS  = (uint32_t)pol; changed = true; }
        if (ssl != ssl0) { cfg.ssl    = (ssl != 0);    changed = true; }
        if (changed) {
            cfgDirty = true;
            sce::trace::log("cfg", "panneau applique: lum=%d poll=%d s ssl=%d",
                            lum, pol, ssl);
        }
    }
    sce::trace::log("ui", "reglages fermes: %s", ok ? "OK" : "annules");
    uiDirty = true;
    guest.setSwipeExit(true);
}

// ===========================================================================
// Actions (posted to netTask)
// ===========================================================================
static void postAction(const char* domain, const char* service,
                       const char* entity, const char* extra = "") {
    // The ONE choke point every user command passes through: naming the
    // action here covers taps, drags and palette alike, without a trace per
    // touch branch. `extra` is our own JSON fragment (position, color) — no
    // secret in it.
    sce::trace::defer("ui", "action %s.%s sur %s%s%s", domain, service,
                    entity ? entity : "(aucune)",
                    (extra && extra[0]) ? " params " : "",
                    (extra && extra[0]) ? extra : "");
    xSemaphoreTakeRecursive(gMtx, portMAX_DELAY);
    uint8_t nxt = (uint8_t)((actHead + 1) % ACT_Q);
    if (nxt == actTail) {                 // queue full: we drop the OLDEST
        actTail = (uint8_t)((actTail + 1) % ACT_Q);   // one, the most recent
    }                                     // is the most relevant
    Action& a = actQ[actHead];
    strlcpy(a.domain,  domain,  sizeof(a.domain));
    strlcpy(a.service, service, sizeof(a.service));
    strlcpy(a.entity,  entity ? entity : "", sizeof(a.entity));
    strlcpy(a.extra,   extra ? extra : "", sizeof(a.extra));
    actHead = nxt;
    xSemaphoreGiveRecursive(gMtx);
}

// Records the TARGETED entity. Called on EVERY path that changes target:
// relying on the side effect of current() only held because loop() runs
// fast — one slightly long block, and an interleaved poll brought the
// selection back to the previous entity (review finding 07-28).
static void rememberTarget() {
    int idx = catNth(curCat, curIdx);
    if (idx >= 0) strlcpy(curId, ents[idx].id, sizeof(curId));
    else          curId[0] = '\0';
}

static const Entity* current() {
    int idx = catNth(curCat, curIdx);
    if (idx >= 0) strlcpy(curId, ents[idx].id, sizeof(curId));
    return idx >= 0 ? &ents[idx] : nullptr;
}

// ===========================================================================
// Touch
// ===========================================================================
static int  _sx = 0, _sy = 0;
static bool _tsActive = false;

// The WHOLE gesture runs under lock. netTask rewrites ents[] in full on every
// poll (`entCount = 0` then refill): reading the entity, then posting the
// action without the lock, made the command act on whichever had taken its
// place in the table meanwhile — pressing CLOSE for the living-room cover
// closed the bedroom one (review finding 07-28).
static void handleTouchLocked() {
    auto t = M5.Touch.getDetail();
    const Entity* cur = (screen == SC_LIST) ? current() : nullptr;

    // ---- SLIDERS: grabbing a track NEUTRALIZES the swipe (dragging on a
    // slider must not change entity), the value is tracked locally, and the
    // service is called ONLY ON RELEASE.
    // Cover: VERTICAL grab on the shutter itself.
    if (t.wasPressed() && cur && curCat == CAT_COVER && cur->pos >= 0 &&
        t.x >= VOL_X - 14 && t.x <= VOL_X + VOL_W + 14 &&
        t.y >= VOL_Y - 6  && t.y <= VOL_Y + VOL_H) {   // not down to the
                                                       // status line
        dragKind = DRAG_POS;
        // dragEnt/dragVal MUST be set HERE: the block that used to set them
        // is guarded by `dragKind == DRAG_NONE`, hence skipped as soon as the
        // cover arms the drag — and the identity check just after then
        // cancelled the gesture on the very frame it started. The shutter
        // never followed the finger (max review 07-28).
        strlcpy(dragEnt, cur->id, sizeof(dragEnt));
        dragVal = cur->pos;
        uiDirty = true;
        // The shutter is 144 px tall: a full-travel drag EXCEEDS the 100 px
        // threshold of the SceGuest down swipe and would have opened the
        // exit confirmation in the middle of a closing (user finding
        // 07-28). We take the gesture away from it for the drag.
        guest.setSwipeExit(false);
    }
    if (t.wasPressed() && cur && dragKind == DRAG_NONE &&
        t.x >= SLD_X0 - 12 && t.x <= SLD_X1 + 12) {
        // Band = the track and its knob (y-3..y+15), NOT the value text
        // that follows: two neighbouring tracks must never overlap each
        // other.
        // SAME table as the drawing: cf. lightRows().
        LightRows lr = lightRows(*cur);
        auto inRow = [&](int y) { return y >= 0 && t.y >= y - 4 && t.y <= y + 16; };
        if      (curCat == CAT_LIGHT && inRow(lr.yDim))  dragKind = DRAG_BRIGHT;
        else if (curCat == CAT_LIGHT && inRow(lr.yTemp)) dragKind = DRAG_TEMP;
        if (dragKind != DRAG_NONE) {
            strlcpy(dragEnt, cur->id, sizeof(dragEnt));
            // Start value = the DISPLAYED value: without it the slider
            // jumped to zero for the length of one render, before the
            // finger had moved at all.
            dragVal = (dragKind == DRAG_POS)    ? cur->pos
                    : (dragKind == DRAG_BRIGHT) ? (cur->bright >= 0 ? cur->bright : 0)
                    : (cur->kelvin >= 0 ? cur->kelvin : (cur->kMin + cur->kMax) / 2);
            _tsActive = false;
            uiDirty   = true;
        }
    }
    if (dragKind != DRAG_NONE) {
        // The GRABBED entity must always be the one under the finger: an
        // interrupted drag must never apply to the next one.
        if (!cur || strcmp(cur->id, dragEnt)) { cancelDrag(); uiDirty = true; return; }
        int lo = 0, hi = 100;
        if (dragKind == DRAG_BRIGHT) { lo = 0; hi = 255; }
        if (dragKind == DRAG_TEMP)   { lo = cur->kMin; hi = cur->kMax; }
        if (t.isPressed()) {
            int v;
            if (dragKind == DRAG_POS) {         // VERTICAL: up = open
                int y = t.y;
                if (y < VOL_Y) y = VOL_Y;
                if (y > VOL_Y + VOL_H) y = VOL_Y + VOL_H;
                v = 100 - (y - VOL_Y) * 100 / VOL_H;
            } else {
                int x = t.x;
                if (x < SLD_X0) x = SLD_X0;
                if (x > SLD_X1) x = SLD_X1;
                v = lo + (x - SLD_X0) * (hi - lo) / (SLD_X1 - SLD_X0);
            }
            if (v != dragVal) { dragVal = v; uiDirty = true; }
            return;
        }
        // Release: ONE single service call carrying the final value
        char extra[72];
        if (dragKind == DRAG_POS) {
            snprintf(extra, sizeof(extra), ",\"position\":%d", dragVal);
            postAction("cover", "set_cover_position", cur->id, extra);
            // "position" is spelled the same in both languages.
            char m[24]; snprintf(m, sizeof(m), "position %d %%", dragVal);
            say(m);
        } else if (dragKind == DRAG_BRIGHT) {
            snprintf(extra, sizeof(extra), ",\"brightness\":%d", dragVal);
            postAction("light", "turn_on", cur->id, extra);
            char m[24];
            snprintf(m, sizeof(m), sce::T("brightness %d %%", "puissance %d %%"),
                     (dragVal * 100 + 127) / 255);
            say(m);
        } else {
            snprintf(extra, sizeof(extra), ",\"color_temp_kelvin\":%d", dragVal);
            postAction("light", "turn_on", cur->id, extra);
            char m[16]; snprintf(m, sizeof(m), "%d K", dragVal);
            say(m);
        }
        cancelDrag();
        return;
    }

    if (t.wasPressed()) { _sx = t.x; _sy = t.y; _tsActive = true; }

    if (_tsActive && t.wasReleased()) {
        _tsActive = false;
        const int dx = t.x - _sx, dy = t.y - _sy;
        // ONE classification for the three gestures below
        // (firmware/common/Gesture.h). They used to be three hand-written
        // conditions with TWO thresholds, and the middle one contradicted the
        // comment sitting above it.
        const auto dir = sce::gesture::classify(dx, dy);
        // Swipe UP: back to the category home (swipe DOWN is reserved for
        // SceGuest — back to the companion).
        if (dir == sce::gesture::Dir::Up) {
            sce::trace::defer("ui", "geste: retour accueil");
            screen = SC_HOME; uiDirty = true;
            return;
        }
        // Swipe RIGHT on the home screen: settings panel. Same gesture and
        // same threshold as flight-radar — a guest does not reinvent its
        // gestures (user 07-28), and since the threshold is now shared the
        // sentence is enforced rather than promised. The panel is BLOCKING:
        // we cannot open it here, we hold the lock. loop() takes care of it.
        if (screen == SC_HOME && dir == sce::gesture::Dir::Right) {
            sce::trace::defer("ui", "geste: ouverture des reglages");
            settingsReq = true;
            return;
        }
        // Swipe ←/→: previous / next entity WITHIN the category. This
        // line said 50, six lines below the promise above it: the claim was
        // true of one site and not of its neighbour.
        if (screen == SC_LIST && (dir == sce::gesture::Dir::Left ||
                                  dir == sce::gesture::Dir::Right)) {
            int cnt = catCount(curCat);
            if (cnt > 0) {
                curIdx = (curIdx + (dx < 0 ? 1 : cnt - 1)) % cnt;
                rememberTarget();     // the invariant must not depend on
                                      // a side effect of current()
                sce::trace::defer("ui", "geste: entite %d/%d (%s)", curIdx + 1,
                                cnt, curId[0] ? curId : "?");
                if (curCat == CAT_CAMERA) camReq = true;   // load the image
                uiDirty = true;
            }
            return;
        }
    }
    if (!t.wasClicked()) return;
    int x = t.x, y = t.y;

    if (screen == SC_HOME) {
        // 2×2 cards — hit zones = the DRAWN rectangles only (a tap in a
        // gutter must trigger nothing).
        for (int c = 0; c < CAT_N; c++) {
            int cx = HOME_X0 + (c % 2) * HOME_DX;
            int cy = HOME_Y0 + (c / 2) * HOME_DY;
            if (x >= cx && x <= cx + HOME_W && y >= cy && y <= cy + HOME_H) {
                if (!catCount(c)) { say(sce::T("no entity", "aucune entite")); return; }
                curCat = c; curIdx = 0; screen = SC_LIST;
                rememberTarget();
                // CAT_DOMAIN, not catName(): the trace vocabulary must not
                // change with the UI language.
                sce::trace::defer("ui", "carte %s ouverte (%d entites)",
                                CAT_DOMAIN[c], catCount(c));
                if (c == CAT_CAMERA) camReq = true;
                uiDirty = true;
                return;
            }
        }
        return;
    }

    // ---- list screen ----
    const Entity* e = current();
    // The HOME button of the empty screen must stay REACHABLE. A category can
    // empty out AFTER entering it (HA restart, entity renamed, WiFi down):
    // the button was then drawn but inert, and all that was left was the up
    // swipe, which is written down nowhere (review finding 07-28).
    if (!e) {
        if (y >= Y_BTN && y <= Y_BTN + H_BTN && x >= 8 && x <= 148) {
            screen = SC_HOME; uiDirty = true;
        }
        return;
    }
    if (curCat == CAT_COVER) {
        if (x >= VB_X && x <= VB_X + VB_W)
            for (int i = 0; i < 3; i++)
                if (y >= vbY(i) && y < vbY(i) + VB_H) {   // half-open:
                                                        // no shared
                                                        // pixel
                    static const char* SVC[3] = { "open_cover", "stop_cover",
                                                  "close_cover" };
                    // Not `static`: initialized on the first call only, it
                    // would freeze the language of that first call.
                    const char* MSG[3] = { sce::T("opening",  "ouverture"),
                                           sce::T("stopping", "arret"),
                                           sce::T("closing",  "fermeture") };
                    postAction("cover", SVC[i], e->id);
                    say(MSG[i]);
                    break;
                }
        return;
    }
    if (curCat == CAT_CAMERA) return;   // no button: swipe up = home
    // Color palette — hit zones = the DRAWN swatches, and only if the lamp
    // KNOWS how to change color (otherwise HA rejects the service).
    LightRows lr = lightRows(*e);
    if (curCat == CAT_LIGHT && lr.yPal >= 0 &&
        y >= lr.yPal && y <= lr.yPal + PAL_H && x >= PAL_X0) {
        int i = (x - PAL_X0) / PAL_STEP;
        if (i >= 0 && i < PAL_N && (x - PAL_X0) % PAL_STEP <= PAL_W) {
            char extra[72];
            snprintf(extra, sizeof(extra), ",\"rgb_color\":[%u,%u,%u]",
                     PALETTE[i][0], PALETTE[i][1], PALETTE[i][2]);
            postAction("light", "turn_on", e->id, extra);
            say(sce::T("color", "couleur"));
            return;
        }
    }
    // lights / switches: turn on / turn off
    int by = (curCat == CAT_LIGHT) ? lr.yBtn : Y_BTN;
    if (y >= by && y <= by + H_BTN && x >= 6 && x <= 314) {
        const char* dom = (curCat == CAT_LIGHT) ? "light" : "switch";
        postAction(dom, btnTurnOn ? "turn_on" : "turn_off", e->id);
        say(btnTurnOn ? sce::T("turning on",  "allumage")
                      : sce::T("turning off", "extinction"));
    }
}

// Entry point: ONE lock for the whole gesture (cf. handleTouchLocked).
// THE LOCK IS HELD FOR THE WHOLE GESTURE, so nothing inside may block:
// netTask waits on this same recursive mutex in fetchOneState. That is
// why the traces below it use `sce::trace::defer` and not `log` -- a
// ~100-byte line is ~9 ms of blocking UART at 115200, and debug mode is
// switched on precisely when the network is what is being diagnosed.
static void handleTouch() { Lock lk; handleTouchLocked(); }

// ===========================================================================
// setup / loop
// ===========================================================================
void setup() {
    auto mcfg = M5.config();
    M5.begin(mcfg);
    Serial.begin(115200);
    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE);
    // English only, and on purpose: the SD card is not mounted yet, so the
    // `lang` key has not been read — there is no language to obey here.
    M5.Display.drawString("ha-remote : starting...", 10, 10);

    // SPI2 shared LCD/SD: the PINS must be set BEFORE SD.begin (without
    // this SPI.begin the mount fails silently, bug 07-25).
    SPI.begin(SCE_SD_SCK, SCE_SD_MISO, SCE_SD_MOSI, SCE_SD_CS);
    sdOk = SD.begin(SCE_SD_CS, SPI, SCE_SD_HZ);
    if (sdOk) {
        // BEFORE the lobby: applyLobbyTheme() reads config.yaml (loadLang()).
        sce::migrateSdRoot();
        // Lobby: escape hatch independent of this code and of the network
        sce::SceGuest::applyLobbyTheme("ha-remote");
        checkSDUpdater(SD, String("/companion.bin"), 2500, 4);
    }
    Serial.printf("[ha] SD %s\n", sdOk ? "montee" : "ECHEC montage");
    // Without a card this bin has NO Home Assistant to talk to: the host and
    // the long-lived token live in the yaml, and there is no compiled default
    // for either. It would come up, discover nothing, and show an empty home
    // screen that looks exactly like a Home Assistant with no entities.
    if (!sdOk) {
        const char* why[] = {
            sce::T("No Home Assistant: the host and the long-lived",
                   "Pas de Home Assistant : l'hote et le jeton longue"),
            sce::T("token live on the card, with no compiled default.",
                   "duree vivent sur la carte, sans defaut compile."),
            sce::T("Enter them on /config - they work until reboot.",
                   "Saisissez-les sur /config - valables jusqu'au reboot."),
            sce::T("The entity roster cache is gone too: discovery",
                   "Le cache des entites est perdu aussi : la decouverte"),
            sce::T("runs again, fifteen seconds of empty home screen.",
                   "recommence, quinze secondes d'accueil vide."),
        };
        if (guest.noSdNotice([] { SD.end();
                                  sdOk = SD.begin(SCE_SD_CS, SPI, SCE_SD_HZ);
                                  return sdOk; },
                             why, (int)(sizeof(why) / sizeof(why[0]))))
            Serial.println("[ha] carte inseree : configuration relue");
    }

    // Seeded HERE and not at the mount above: the notice offers a RETRY
    // that mounts the card, and a watcher seeded before it would keep
    // the pre-retry answer -- then unmount a working card to "discover"
    // it (firmware/common/SdWatch.h).
    gSdWatch.begin(sdOk, SCE_SD_CS, SCE_SD_HZ);
    // Probed once, and the answer is reported: `auto_bright` on a board with
    // no sensor must not look like a setting that does nothing.
    gLtrOk = gLtr.begin();
    Serial.printf("[ha] LTR-553 %s\n", gLtrOk ? "present" : "absent");
    loadConfig();
    // The table from the last run, so the home screen is usable in a second
    // instead of after a fifteen-second discovery. States stay blank.
    rosterLoad();
    Serial.printf("[ha] cfg host=%s:%u ssl=%u poll=%lus token=%s entities=%s\n",
                  cfg.host, (unsigned)cfg.port, (unsigned)cfg.ssl,
                  (unsigned long)cfg.pollS, cfg.token[0] ? "oui" : "NON",
                  cfg.pins[0] ? cfg.pins : "(aucune epinglee)");

    gMtx = xSemaphoreCreateRecursiveMutex();
    M5.Display.setBrightness(cfg.bright);
    canvas.setColorDepth(16);
    canvas.setPsram(true);
    canvas.createSprite(320, 240);

    guest.appName = "ha-remote";   // names the tab and the home card
    ipStr = guest.begin("", "", "ha-remote");
    Serial.printf("[ha] WiFi %s ip=%s\n",
                  WiFi.status() == WL_CONNECTED ? "STA" : "AP", ipStr.c_str());

    // ---- Settings exposed on http://<ip>/config (SceGuest form) ----
    // These labels go into HTML, which is UTF-8: the French keeps its
    // accents here, unlike everything drawn with Font0.
    guest.addSetting("host",  sce::T("Home Assistant host", "Hôte Home Assistant"),
                     sce::SceGuest::Text);
    guest.addSetting("port",  "Port",                sce::SceGuest::Num, 1, 65535);
    guest.addSetting("ssl",   "HTTPS",               sce::SceGuest::Bool);
    guest.addSetting("token", sce::T("Long-lived token", "Jeton longue durée"),
                     sce::SceGuest::Secret);
    guest.addSetting("poll_s", sce::T("Auto re-discovery (s, 0 = never)",
                                      "Re-découverte auto (s, 0 = jamais)"),
                     sce::SceGuest::Num, 0, 3600);
    guest.addSetting("brightness", sce::T("Brightness", "Luminosité"),
                     sce::SceGuest::Num, 10, 255);
    guest.addSetting("auto_bright", sce::T("Auto brightness", "Luminosité auto"),
                     sce::SceGuest::Bool);
    // Only ever matters above 64 entities, and that is exactly when the user
    // arrives on this page — the screen has just told them how many were
    // dropped. A prefix pins a whole domain ("cover.").
    guest.addSetting("entities",
                     sce::T("Entities to keep first (comma-separated, "
                            "\"cover.\" = whole domain)",
                            "Entités à garder d'abord (séparées par des "
                            "virgules, « cover. » = domaine entier)"),
                     sce::SceGuest::Text);
    guest.settingGet = [](const char* k) -> String {
        if (!strcmp(k, "host"))       return cfg.host;
        if (!strcmp(k, "port"))       return String(cfg.port);
        if (!strcmp(k, "ssl"))        return String((int)cfg.ssl);
        if (!strcmp(k, "token"))      return cfg.token;
        if (!strcmp(k, "poll_s"))     return String((int)cfg.pollS);
        if (!strcmp(k, "brightness")) return String((int)cfg.bright);
        if (!strcmp(k, "auto_bright")) return String((int)cfg.autoBright);
        if (!strcmp(k, "entities"))   return cfg.pins;
        return "";
    };
    guest.onSettingsBegin = []() { xSemaphoreTakeRecursive(gMtx, portMAX_DELAY); };
    guest.settingSet = [](const char* k, const String& v) {
        // The project's ONE definition (firmware/common/CfgBool.h).
        auto boolOf = [](const String& s) { return sce::webBool(s.c_str()); };
        if      (!strcmp(k, "host"))  strlcpy(cfg.host, v.c_str(), sizeof(cfg.host));
        else if (!strcmp(k, "port"))  cfg.port = (uint16_t)constrain(v.toInt(), 1, 65535);
        else if (!strcmp(k, "ssl"))   cfg.ssl  = boolOf(v);
        else if (!strcmp(k, "token")) strlcpy(cfg.token, v.c_str(), sizeof(cfg.token));
        else if (!strcmp(k, "poll_s"))cfg.pollS = constrain(v.toInt(), 0, 3600);
        else if (!strcmp(k, "brightness")) {
            cfg.bright = constrain(v.toInt(), 10, 255);
            // Moving the SLIDER means taking the backlight back by hand.
            // Without this, the setting appeared to do nothing under auto:
            // the sensor reapplied its own level a second later and the page
            // looked broken (the space bin paid for this on 08-05).
            //
            // No "am I loading the yaml?" guard, unlike that bin: here the
            // card is read by loadConfig's own key chain and never through
            // settingSet, so this runs on a web submit and nowhere else.
            cfg.autoBright = 0;
        }
        else if (!strcmp(k, "auto_bright")) cfg.autoBright = boolOf(v);
        else if (!strcmp(k, "entities")) strlcpy(cfg.pins, v.c_str(), sizeof(cfg.pins));
    };
    guest.onSettingsSaved = []() -> bool {
        // Traced while the submit lock is still held: cfg cannot move under
        // us. Presence of the token only — its VALUE never reaches a trace.
        sce::trace::log("cfg", "submit web applique: hote=%s:%u ssl=%u "
                        "poll=%lus jeton=%s epingles=%s",
                        cfg.host, (unsigned)cfg.port, (unsigned)cfg.ssl,
                        (unsigned long)cfg.pollS,
                        cfg.token[0] ? "present" : "ABSENT",
                        cfg.pins[0] ? cfg.pins : "(aucune)");
        xSemaphoreGiveRecursive(gMtx);                     // end of the submit
        // Only when the SENSOR is not in charge: applying the manual value
        // under auto dimmed the screen, and the sensor's own hysteresis never
        // restored it because its computed target had not changed.
        if (!cfg.autoBright) M5.Display.setBrightness(cfg.bright);
        gAutoBrightReset = true;
        cfgDirty = true;                          // persisted by loop()
        pollNow  = true;                          // re-read with the new
        uiDirty  = true;                          // configuration
        return true;
    };

    // The window must cover the LONGEST request (3 s connect + 8 s read,
    // cf. haBegin), otherwise the reflash starts while a TLS session still
    // holds its buffers: the OTA runs short of internal heap and it is the
    // ONLY way out towards the companion that closes (review finding 07-28).
    // `setTimeout` is a PER-READ delay, not a request budget: a response
    // arriving in dribbles (weak WiFi, busy hub) rearms it on every block.
    // Hence the wide window — waiting 20 s is nothing next to the risk of
    // missing the reflash. SceGuest parks/releases around the reflash.
    netGuard.windowMs = 20000;
    guest.netGuard = &netGuard;

    // 20 KB: JSON parsing is recursive (JSON_DEPTH levels) and the buffered
    // reader puts 1 KB more on that stack.
    xTaskCreatePinnedToCore(netTask, "haNet", 20480, nullptr, 1,
                            &netTaskHandle, 0);
}

void loop() {
    M5.update();
    guest.update();
    // The exit dialog is blocking and pumps M5.update(): the frame of the
    // "No" tap would come back here as a release (flight-radar lesson).
    if (guest.consumedTouch()) { _tsActive = false; cancelDrag(); }
    else                       handleTouch();

    // Settings panel: called HERE, outside the lock. Opened from
    // handleTouch it would have frozen netTask for the modal's 90 s.
    if (settingsReq) { settingsReq = false; settingsPanel(); }
    // The gesture traces, printed HERE and not where they were decided: they
    // are recorded under the lock and drained outside it (see handleTouch).
    sce::trace::drain();

    // --- Auto brightness (LTR-553), the same curve the other two bins use:
    // the calibration lives in firmware/common/Ltr553.h and not in three
    // copies. ONCE A SECOND, and that is a timer rather than a comment: the
    // part converts every 500 ms, and the bus it sits on also carries the
    // touch panel and the PMIC. The hysteresis on top is A2.2 -- setBrightness
    // is a PMIC transaction, never a per-frame call.
    static uint32_t ltrMs = 0;
    static int      ltrLastB = -1;
    if (gAutoBrightReset) { gAutoBrightReset = false; ltrLastB = -1000; }
    if (cfg.autoBright && gLtrOk && millis() - ltrMs >= 1000) {
        ltrMs = millis();
        const int32_t v = gLtr.visible();
        if (v >= 0) {
            const int b = sce::ltr553::brightnessFrom(v);
            if (abs(b - ltrLastB) > 6) {
                ltrLastB = b;
                M5.Display.setBrightness((uint8_t)b);
            }
        }
    }

    // THE CARD, WATCHED. This bin keeps the host and the long-lived token on
    // the card and NOWHERE else, with no compiled default — so a card that
    // appears mid-run is the event it most wants to notice, and until now the
    // boot answer stood for the whole session. On insertion, both the config
    // and the entity roster are read again: that is exactly what the boot
    // notice's retry does, and there is no reason the same card appearing
    // thirty seconds later should be worth less.
    if (gSdWatch.update([] { loadConfig(); rosterLoad(); })) {
        sdOk = gSdWatch.mounted();
        uiDirty = true;
        Serial.printf("[ha] SD %s\n",
                      sdOk ? "INSEREE : configuration relue"
                           : "RETIREE : reglages non persistes");
    }

    // A failed write is RETRIED, but not on every iteration: without a
    // backoff, loop() hammered the SPI2 bus shared with the display.
    //
    // NO CARD AT ALL is not a failed write, and the distinction was missing
    // HERE while the roster eight lines below already made it: `cfgDirty`
    // never clears without a card, so the backoff turned an unbounded busy
    // loop into one attempt every 5 seconds — quieter, and still forever, on
    // the SPI2 bus the display shares. The radar states the same rule and
    // says so ONCE; a setting that will not survive the reboot is worth a
    // line on the console.
    static uint32_t cfgRetry = 0;
    if (cfgDirty) {
        if (!sdOk) {
            static bool said = false;
            if (!said) { said = true;
                Serial.println("[ha] pas de SD : reglages non persistes "
                               "(valables jusqu'au redemarrage)"); }
            cfgDirty = false;
        } else if ((int32_t)(millis() - cfgRetry) >= 0) {
            if (saveConfigSd()) cfgDirty = false;
            else cfgRetry = millis() + 5000;
        }
    }
    // The roster, on the same rule and by the same writer. No card is not a
    // transient failure: drop the flag rather than reopen a file on an
    // unmounted filesystem every pass (the radar paid for that lesson).
    if (rosterDirty) {
        if (!sdOk || rosterSave()) rosterDirty = false;
    }

    static uint32_t lastDraw = 0;
    uint32_t now = millis();
    if (uiDirty || now - lastDraw > 1000) {       // tick 1 s: "il y a Xs"
        uiDirty = false;
        lastDraw = now;
        draw();
    }
    static uint32_t statsMs = 0;
    if (now - statsMs >= 10000) {
        statsMs = now;
        Serial.printf("[ha] stats up:%lus heap:%u psram:%u entites:%d/%d rssi:%d\n",
                      (unsigned long)(now / 1000), (unsigned)ESP.getFreeHeap(),
                      (unsigned)ESP.getFreePsram(), entCount, entSeen,
                      WiFi.RSSI());
    }
    delay(10);
}
