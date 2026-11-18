// =============================================================================
// flight-radar — StackChan-Companion demo GUEST .bin
// =============================================================================
// Real-time aircraft radar on the CoreS3 screen. This is the REFERENCE guest
// bin: it exercises the whole chain described in docs/guests/README.md —
// launch from the SD launcher, remote stop by SceGuest, WiFi and password
// inherited from the companion, config on the SD card and a web page.
//
// ── WHAT IS ON SCREEN ───────────────────────────────────────────────────
// Radar on the LEFT: one blip per aircraft, oriented to its track, its SHAPE
// giving the category (airliner, light, ultralight, glider, helicopter,
// military) and its COLOR the altitude band. A segment ahead of the blip
// shows where it will be in a few minutes.
// Panel on the RIGHT: either the summary and the legend, or — as soon as a
// flight is tracked — its route, cities, progress and times.
// Times carry a "~": they are ESTIMATED (great circle / ground speed), no
// free API publishes the real schedules.
//
// ── GESTURES ────────────────────────────────────────────────────────────
// The four screens are a RING travelled by the swipe UP alone: radar → METAR
// → TAF → NOTAM → radar. Which is why the state is a level (a number), not one
// boolean per view. The ORDER is the meaning: the radar is what is happening,
// the METAR what is measured, the TAF what is forecast, the NOTAM what is out
// of service — each step one remove further from the ground truth.
//
// THE SWIPE DOWN IS NOT OURS, at any step: it always means "back to the
// companion" and belongs to SceGuest (user 07-31). One gesture, one job. The
// earlier design armed that exit at the bottom step only and used down as
// "one step back" above it — it worked, but it put our navigation and the
// guest contract on the same input, and the contract is not ours to bend.
// Now nothing this bin does can break the way out of it.
//
//        NOTAM      ↑ up: round to the RADAR   ↓ down (long): the companion
//        TAF        ↑ up from the METAR        ↓ down (long): the companion
//        METAR      ↑ up from the radar        ↓ down (long): the companion
//        RADAR      ↑ up: the METAR            ↓ down (long): the companion
//
//   touch an aircraft   track that flight (automatic re-acquisition)
//   touch empty space   stop tracking
//   swipe UP            next screen on the ring
//   any TAP above the radar   straight back to the radar
//   swipe LEFT          keyboard: callsign to track, or airport code
//   swipe RIGHT         settings, 3 tabs (persisted on the SD card)
//   swipe DOWN (long)   back to the companion, from ANY step (SceGuest)
//   pinch / double-tap  radius zoom
// Above the radar, any TAP jumps straight back to the radar — the short way
// out of a reading screen, without going round the ring. The stack is driven
// from the RADAR ZONE (x < PANEL_X): vertical swipes STARTING on the right
// panel keep their older job, cycling through the tracked flights.
//
// ── WHERE THE DATA COMES FROM ───────────────────────────────────────────
// Positions: airplanes.live by default (best Indian Ocean coverage measured
// on 2026-07-25); adsb.lol and adsb.fi speak the same v2 format. A FOURTH
// source, safesky (api.safesky.app), adds FLARM/advisory traffic — gliders
// and UAVs that no ADS-B mirror sees — at the price of an API key, of METRES
// and m/s converted at the entry point, and of a 20 km cap on its radius
// that forces the `viewport` mode beyond that.
// Routes: hexdb.io, with adsbdb.com as fallback — both databases are
// community-run and INCOMPLETE, each knows flights the other ignores.
// Weather: aviationweather.gov (METAR, free, no key), on its OWN 10-minute
// period — a report is issued every 30 to 60 minutes.
//
// A flight number has a CANONICAL direction in these databases, but several
// airlines reuse the same callsign for the return leg: if the real track
// opposes the announced destination, the leg is considered reversed and
// origin/destination are swapped. Without this, progress and ETA were wrong
// and the "remaining" distance grew as the flight went on.
//
// ── ARCHITECTURE ────────────────────────────────────────────────────────
// netTask (core 0) is the ONLY one to open TLS sockets: a request blocks for
// several seconds and would otherwise starve the touch input and the web
// server. loop() draws and posts requests; `planes[]` and the selection are
// shared under `gMtx`.
//
// Route results carry a GENERATION: switching flights while a resolution is
// in flight discards the stale result instead of applying it to the wrong
// aircraft.
//
// Configuration: /stackchan-companion/flightradar.yaml (template in sdcard/) —
// center or airport code, radius, source, theme, units and options.
// The UI LANGUAGE is NOT in that file: it is the companion's own top-level
// `lang:` key in /stackchan-companion/config.yaml, read at boot into sce::setLang
// (firmware/common/I18n.h). English is the default and the fallback; every
// user-visible string is a sce::T("en", "fr") pair at its call site.
// Full documentation: docs/guests/FLIGHT-RADAR.md
// =============================================================================

// ── BOARD PROFILE ────────────────────────────────────────────────────────
// This bin runs on TWO machines that share a screen size and nothing else:
//   - M5Stack CoreS3 inside a StackChan K151 — touchscreen, servos, ambient
//     light sensor, and a companion firmware to go back to;
//   - M5Stack Fire — three buttons, no touch, no K151 hardware, standalone.
//
// The differences are declared HERE, as orthogonal capability flags, and
// nowhere else. Not a single `#ifdef FIRE` in the body: a flag named after
// what the board HAS survives the next board, a flag named after a board does
// not. Each one can be overridden from platformio.ini's build_flags.
//
// Defaults describe the CoreS3/K151, so an unflagged build is the historical
// one and the guest bin keeps behaving exactly as it did.
#ifndef SCE_INPUT_BUTTONS
#define SCE_INPUT_BUTTONS 0     // 3 physical buttons (Fire, Core classic)
#endif
#ifndef SCE_INPUT_TOUCH
#define SCE_INPUT_TOUCH   (!SCE_INPUT_BUTTONS)   // capacitive touchscreen
#endif
#ifndef SCE_HAS_SERVO
#define SCE_HAS_SERVO     1     // SCS0009 neck through the PY32 expander
#endif
// THE ONE FLAG THE OTHER TWO BINS DO NOT HAVE, and the argument against it is
// written in space's own board profile: a boot PROBE covers a board with no
// sensor AND a board whose sensor is dead, where a build flag only ever covers
// the first. Both mechanisms answer one question, which is how they come to
// disagree — so say here which side this bin is on and why it has not moved.
//
// It has not moved because on this bin the flag is not only about the sensor:
// `SCE_HAS_LTR553` also strips the auto-brightness OPTION from a build that
// cannot use it, and on the Fire profile that keeps a setting off a page where
// it could never do anything. The probe answers "is there a part?"; the flag
// answers "does this BOARD offer the feature?", and those are two questions
// after all. If the Fire ever gets the part, this becomes one question again
// and the flag should go — that is the condition, stated rather than left for
// someone to rediscover.
#ifndef SCE_HAS_LTR553
#define SCE_HAS_LTR553    1     // ambient light sensor (auto brightness)
#endif
// NOT "is SceGuest compiled in" — it always is, and on every board. SceGuest
// is what brings up WiFi, serves /config and holds the settings model; a
// standalone build needs all of that MORE, not less. This flag is narrower
// still: it guards the BOOT LOBBY, the 2.5 s window that offers a jump back to
// the launcher, which is meaningless with no companion.bin on the card.
// It deliberately does NOT strip the rest of the return path — `POST
// /api/bins/stop` and the swipe-down exit stay registered, because both
// already answer honestly when /companion.bin is absent, and a flag that
// removed them would have to be re-added the day this board gets a card with
// a companion on it.
#ifndef SCE_COMPANION
#define SCE_COMPANION     1     // boot lobby (checkSDUpdater)
#endif
// SD on SPI2, shared with the LCD. CoreS3 pinout by default; the Fire uses
// the classic VSPI trio. Wrong pins = a SILENT mount failure, hence no WiFi
// credentials and no STA at all (bug 07-25) — so they are stated per board
// rather than probed.
// FOUR separate guards, not one covering the group: a board that shares the
// CoreS3 trio but wires CS elsewhere would define only SCE_SD_CS, leave
// SCE_SD_SCK undefined, and have the block below silently redefine CS back to
// 4 — a warning that scrolls past in a PlatformIO build, then a failed mount,
// then no WiFi credentials and no STA. That is bug 07-25 all over again.
// The four guards, and the reason for keeping them separate, now live in
// firmware/common/SdPins.h — this file is where that discipline was
// written, and where it stopped being copied correctly.
#include "../common/SdPins.h"
#include "../common/SdRoot.h" // one-time /stackchan-eyes -> /stackchan-companion move
// WiFi credentials of LAST RESORT, compiled in. Empty by default, and that is
// the intended state: the credentials normally come from the SD card, which
// keeps them out of the binary and out of this repository.
// They exist for a board running with NO CARD AT ALL — a bare Fire — where the
// alternative is the AP fallback, i.e. no internet, i.e. a radar with nothing
// to display. The SD still WINS when a card is present (`readSdCreds`
// overwrites these), so a CoreS3 is unaffected.
// ⚠ NEVER hard-code a value here or in platformio.ini. The Fire env passes
// them through `${sysenv.SCE_WIFI_SSID}`, so the secret lives in the shell
// that runs the build and reaches neither git nor a shared file.
#ifndef SCE_WIFI_SSID
#define SCE_WIFI_SSID ""
#endif
#ifndef SCE_WIFI_PASS
#define SCE_WIFI_PASS ""
#endif

#include <M5Unified.h>
#include <SD.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ctype.h>   // toupper (tracking)

// In-place ASCII uppercase — and ALWAYS through `unsigned char`. `char` is
// SIGNED on xtensa; a UTF-8 high byte is negative and `toupper(-61)` indexes
// newlib's table at __ctype_ptr__[-61]: undefined behaviour that does not
// crash, which is why it survives review (08-03). The cast lived in FIVE
// hand-rolled copies of this loop, each of which had to remember it — the
// 08-03 fix was applied to every copy by hand, which is the divergence
// mechanism rule 17 exists to kill (review 08-04).
static void upAscii(char* s) {
    for (char* c = s; *c; ++c) *c = (char)toupper((unsigned char)*c);
}
#include <time.h>    // time/localtime_r (NTP-based estimated times)
#include "../common/CfgBool.h"  // ONE definition of "true"
#include "../common/PsJson.h"   // shared ArduinoJson allocator in PSRAM
#include "../common/I18n.h"     // sce::T(en, fr) - EN default, FR opt-in
#include "../common/SunClock.h"  // real sunrise/sunset, not a fixed window
#include "../common/Ltr553.h"    // ambient light: ONE calibration, two readers
#include "../common/CellText.h"  // deferred text, ONE draw call site (A2.22)
#include "../common/Gesture.h"   // ONE swipe classifier and press budget
#include "../common/SdWatch.h"   // the card, watched and not only mounted

// The card watcher, one per bin: setup() hands it the boot answer, loop()
// asks it whether that answer still holds.
static sce::SdWatch gSdWatch;

// The deferred text table, ONE instance for the whole bin: the cells are filled
// where each value is decided and drawn from the single `noinline` call site in
// CellText::flush. Only one view draws at a time, all of them from loop().
static sce::ui::CellText gCells;

// The Zulu HH:MMZ stamp, ONE implementation for the two places that print it
// (A+C debug screen, serial heartbeat). The two hand-rolled copies diverged
// in their FIRST commit — one had a gmtime_r-failure branch, the other did
// not (review 08-04). Returns false when the clock is not synced or gmtime_r
// refuses; each caller keeps its own "not synced" wording.
static bool zuluHhmm(char* out, size_t n) {
    const time_t utc = time(nullptr);
    struct tm g;
    if (!sce::clockSynced(utc) || !gmtime_r(&utc, &g)) return false;
    snprintf(out, n, "%02d:%02dZ", g.tm_hour, g.tm_min);
    return true;
}
#include "../../src/guest/SceGuest.h"
#include "geo.h"          // PURE functions (native tests, test_flightgeo)
#include "input.h"        // PURE UiEvent vocabulary + button FSM (test_input)
#include <Wire.h>
#if SCE_HAS_SERVO
#include "../common/HeadServo.h"   // head pointing towards the tracked flight
#include "../common/headtrack.h"   // PURE: bearing -> servo yaw (test_astro)
#endif

// ---------------------------------------------------------------- SD config
static constexpr const char* CFG_PATH = "/stackchan-companion/flightradar.yaml";

struct Config {
    // Roland Garros, Réunion — the exact figures the airport database returns
    // for RUN/FMEE, not a rounding of them. They used to be -20.89/55.53,
    // about a kilometre off, so a successful lookup silently MOVED the centre
    // at boot; now the network changes nothing when it works, and nothing is
    // lost when it does not.
    float    lat      = -20.8871f;
    float    lon      =  55.5103f;
    // 500 nm, not 100. The sweep is what the screen is FOR, and at 100 nm
    // around this aerodrome it is routinely empty — a radar showing nothing is
    // indistinguishable from a radar that is not working, which is exactly how
    // it read on a board with no SD card to widen it (user 08-02). The zoom is
    // one long press away for anyone who wants a closer look.
    float    radiusNm = 500.0f;
    uint32_t pollS    = 10;
    char     api[24]  = "airplanes.live";
    // IATA (RUN) or ICAO (FMEE) code — resolved at boot via hexdb.io and takes
    // PRIORITY over lat/lon. Defaults to RUN so the built-in configuration
    // NAMES the place its coordinates already point at: the code is what the
    // radar prints in its corner, and an unnamed centre reads as "somewhere".
    // Three letters, so `metarStation()` (which wants four) still falls through
    // to `metarIcao` — RUN and FMEE are the IATA and ICAO of one aerodrome, and
    // the pair is the point.
    // Offline it costs nothing: the lookup fails and the coordinates above,
    // which are that aerodrome's, stand unchanged.
    char     airport[8] = "RUN";
    float    tzOffsetH = 4.0f;     // local offset (h vs UTC) — Reunion +4
    // 60, not 100. This screen is read in a room, often at night, and it is
    // mostly dark pixels with thin bright strokes — at 100 the rings and the
    // text bloom. Measured against the four themes, which are built for a
    // scope look rather than for a phone in daylight (user 08-02).
    uint8_t  bright    = 60;       // screen backlight (10..255)
    // 0 Gundam | 1 Gundam night (ochre) | 2 Scope | 3 Scope night (red)
    // GUNDAM is the default (user 08-03). It matters most exactly where it is
    // least visible: a board with no card NEVER reads a yaml, so this constant
    // is the only theme it will ever have. With `autoNight` on — also the
    // default — `nightThemeFor` turns it into `theme | 1`, so the pair actually
    // shipped on a card-less Fire is Gundam / Gundam night, not the astro red
    // of Scope night that a default of 2 produced.
    uint8_t  theme     = 0;
    uint8_t  metric    = 0;        // 0 = aero (nm/kt/ft), 1 = metric
    // Options (settings panel toggles). Servos and sounds OFF by default
    // (user 07-27): a guest bin must not move the head nor make noise
    // unless asked to.
    uint8_t  servo     = 0;        // head pointed at the tracked flight
    // SOUND IS A LEVEL, NOT A SWITCH (user 07-31): 0 = silent, and that is
    // the only "off" there is. A separate on/off flag beside a volume gives
    // two ways to be quiet, and a state where the level reads 60 and nothing
    // comes out. Default 0 - a guest bin must not make noise unless asked.
    uint8_t  volume    = 0;        // 0..100 % (chirps: lock-on, emergency)
    // NOTAM deck: hide the ones a briefing would not print. Measured on FMEE,
    // SEVEN of the thirteen in force were rank M ("miscellaneous, not normally
    // briefed") — more than half the deck was pages you page past. ON by
    // default: the rank already exists, and the default that shows less is the
    // one that shows what you came for.
    uint8_t  notamBrief = 1;       // 1 = drop rank M
    uint8_t  autoLum   = 1;        // auto brightness (LTR-553) — ON because
    uint8_t  autoNight = 1;        // auto Night theme (NTP local time) —
                                   // NON-intrusive automations are enabled,
                                   // the intrusive ones (servo/sound) are not
    uint8_t  showGround = 1;       // show ground traffic
    uint8_t  follow    = 0;        // automatically track the nearest one
    // DOCK MODE (08-04, borrowed from the Aerospace Tracker guide): parked
    // on a desk, the views cycle by themselves every dockS seconds. 0 = off
    // (the default: an instrument must not move under a reader's eyes unless
    // asked to). Any manual action restarts the current view's clock.
    uint8_t  dockS     = 0;        // s per view, 0 = no auto-cycle (0..120)
    // METAR station (up swipe). Defaults to the aerodrome the DEFAULT
    // COORDINATES point at, because anything else is incoherent: lat/lon above
    // are Réunion's, `tzOffsetH` is +4 for Réunion, and leaving this empty made
    // the built-in configuration describe a place while refusing to name its
    // station. `airport` still wins when it holds a 4-letter ICAO code — see
    // metarStation() — so nothing a user configures is overridden.
    // It was empty "on purpose", the purpose being that `airport` would cover
    // it; but `airport` defaults to empty too, so the two defaults together
    // produced no station at all. Invisible on a board whose SD card names one,
    // and the entire experience on a board with no card (user 08-02).
    char     metarIcao[8] = "FMEE";
    // RUNWAY of that station, "12/30" (or a single "12"), for the compass of
    // the METAR view. A METAR never carries it and we own no aerodrome
    // database: it is TYPED IN. Empty = the compass is drawn without a runway,
    // which is still true — better than an invented heading.
    char     metarRwy[12] = "";
    // SafeSky API key (source `safesky`). Kept out of the serial logs and
    // never echoed by the web form (SceGuest::Secret).
    char     skyKey[80]   = "";
    // NOTAM ACCOUNT (top step of the stack), and it IS used: fetchNotam signs
    // in and reads the deck. It stayed unused for two days because no free
    // key-less source answered from here (FAA public search 403,
    // external-api.faa.gov/notamapi/v1/notams 401 without a key, checked
    // 2026-07-29) until autorouter.aero — EUROCONTROL EAD, free — did.
    // NOT a key: autorouter's OAuth 2.0 "client_credentials" grant reuses the
    // ACCOUNT e-mail and password — there is no separate API token to issue.
    // Which is why the password is a Secret the form never echoes back, and
    // why it is worth saying out loud that this pair opens the whole account
    // (it can file flight plans), not just a read-only feed.
    // FIR to ask about ALONGSIDE the aerodrome (08-01). Every NOTAM carries
    // an item A naming what it applies to, and they do not overlap: at
    // Reunion the 27 aerodrome messages are all `[FMEE]` and the 19 area
    // ones all `[FMMM]`. Querying the aerodrome alone therefore hid the
    // whole FIR — the temporary restricted area for UAV, the TMA surveillance
    // upgrade, the wind-farm obstacle group — which is exactly the class of
    // item a radar watching the surrounding traffic should know about.
    // NOT DERIVABLE from the aerodrome code (FMEE -> FMMM is a lookup, not a
    // rule), hence a setting. Empty = aerodrome only, the previous behaviour.
    // Costs no extra request: `itemas` is a JSON ARRAY.
    char     notamFir[8]  = "";
    char     notamUser[64] = "";
    char     notamPass[64] = "";
};
static Config cfg;
static uint8_t themeVer = 0;      // 0 = pre-07-28 numbering
// True once a config file has actually been OPENED and walked. Distinguishes
// "the yaml says nothing about theme_v" (migrate) from "there is no yaml"
// (nothing to migrate) — see migrateTheme().
static bool    cfgFileRead = false;

// Reading the bin's yaml. The DECODING (quotes, comments, over-long lines)
// is the one from `SceGuest::yamlForEach` — the project's single parser.
// The one that used to live here cut at "#" BEFORE looking at quotes and
// never stripped them: `api: "adsb.fi"` arrived with its quotes, matched no
// known source, and the radar silently fell back to another API (found
// 07-29). It also truncated lines at 96 bytes, restarting the remainder as
// a bogus key/value pair.
// `false` = FLAT file: no line opens a section.
// The token is PERSISTED (08-01). It lives SEVEN DAYS and autorouter
// allows only 20 active ones per account, but it used to live in RAM only:
// every reboot minted a fresh one, and a day of deploy cycles walked the
// account into a 403 "toomanytokens". Kept in flightradar.yaml next to the
// password it is derived from - no new exposure, one fewer token burned.
// The deadline is ABSOLUTE (UTC seconds), not a millis() offset: a millis
// deadline means nothing after the reboot that is the whole point here.
// Kept in its OWN file, not in flightradar.yaml (review 08-02). Persisting
// it through the config had a cost nobody asked for: `saveConfigSd()` opens
// CFG_PATH truncating and regenerates it from printf calls — its own header
// says hand-written comments do not survive a save — and `cfgDirty` used to
// be raised only by a DELIBERATE save. Minting a token would therefore have
// silently rewritten the shipped 77-line yaml, destroying the inline schema
// documentation (radius, api, metar_rwy, the runway-vs-heading warning, the
// theme renumbering note) for a user who never touched a setting.
// A token is not configuration, it is a CACHE: machine-written, disposable,
// and losing it costs one login. It belongs in its own file.
// Boot-time SD mount result. Declared HERE, above the first file writer, and
// not further down with the other status flags: everything that touches the
// card has to be able to ask whether there IS one (a Fire runs this bin with
// no card at all).
static bool sdOk = false;

static constexpr const char* TOK_PATH = "/stackchan-companion/notam-token.txt";
static char     notamTok[80] = "";
static uint32_t notamTokExp  = 0;         // UTC seconds, 0 = unknown
// A token minted BEFORE the clock arrived: its remaining life is known, its
// deadline is not. These two hold the monotonic half of the answer until NTP
// lands and `notamTokStamp()` can turn it into an absolute expiry worth
// writing down. See the mint site for why dropping such a token is the wrong
// answer to not knowing the time.
static uint32_t notamTokPendMs  = 0;
static uint32_t notamTokPendTtl = 0;
// (declared above loadConfig, which restores them; the rest of the NOTAM
//  store lives further down with fetchNotam)

// The token cache: two lines, token then absolute expiry. Deliberately NOT
// yaml — nothing here is meant to be read or edited by a human, and keeping
// it out of the config is what stops a machine write from truncating a
// documented file.
// WHY IT IS STORED AT ALL. The token is the scarce resource: twenty active per
// account, seven days each. With no store, EVERY reboot mints a fresh one — and
// a boot-loop would burn the whole allowance in minutes, locking the account out
// of NOTAMs for a week. That is precisely the `toomanytokens` state this account
// reached in development.
//
// NOT the yaml, on either board: `saveConfigSd()` rewrites that file whole and
// drops its comments, the token is a bearer credential for an account that can
// file flight plans (the yaml is what people paste into a ticket), and the two
// change on unrelated rhythms — settings when you decide, the token once a week
// on its own.

// NO NVS FALLBACK, and the reason is worth keeping (08-03). A card-less board
// briefly had one — flash-backed, so it survived both a reboot and a power
// cycle, which looked like the right answer to the boot-loop danger above.
// It was UNREACHABLE. `notam_user`/`notam_pass` are settings, settings live in
// the yaml, and there is no yaml without a card: after any reboot they are
// empty, and `fetchNotam` returns `HTTP_NO_KEY` BEFORE it ever looks at the
// bearer. A stored token that nothing can present is not a cache, it is a
// credential sitting in flash for no one.
// The honest answer to a card-less board is not a partial store, it is to say
// so at boot — see `SceGuest::noSdNotice`. The same reasoning covers the WiFi
// credentials, which come from build flags and cannot be changed without a card
// either: what a missing card costs is the whole configuration, not one token.
// RAISED by whoever changes the token, CONSUMED by loop(). netTask used to
// call notamTokSave() directly (and loop() does too, since notamTokStamp), so
// two tasks could truncate and rewrite the same file at once: the interleaving
// pairs the new token with the old one's deadline, and the next boot presents
// an expired bearer — one more out of the twenty. The file this bin states
// twice is loop()'s alone; now it actually is (review 08-05).
static volatile bool notamTokDirty = false;

static void notamTokSave() {
    // Both silent returns traced: a token that fails to reach the card costs
    // one of the twenty weekly mints at the next reboot, and nothing on
    // screen ever says why. The token itself NEVER goes in a trace.
    if (!sdOk) {
        sce::trace::log("sd", "jeton NOTAM non ecrit : pas de carte");
        return;                              // nothing to write it to
    }
    File f = SD.open(TOK_PATH, FILE_WRITE);
    if (!f) {
        sce::trace::log("sd", "jeton NOTAM non ecrit : open %s KO", TOK_PATH);
        return;
    }
    f.printf("%s\n%lu\n", notamTok, (unsigned long)notamTokExp);
    f.close();
    sce::trace::log("sd", "jeton NOTAM ecrit (exp=%lu)",
                    (unsigned long)notamTokExp);
}

static void notamTokLoad() {
    if (!sdOk) {
        sce::trace::log("sd", "jeton NOTAM non lu : pas de carte");
        return;                              // nothing to read it from
    }
    File f = SD.open(TOK_PATH, FILE_READ);
    if (!f) {
        // Not an error - a board that never minted has no cache file - but
        // the difference matters when counting who spent a weekly token.
        sce::trace::log("sd", "jeton NOTAM : pas de cache (%s)", TOK_PATH);
        return;
    }
    // Bounded reads, like every other file this bin touches (A2.23).
    char buf[96];
    size_t n = f.readBytesUntil('\n', buf, sizeof(buf) - 1);
    buf[n] = '\0';
    strlcpy(notamTok, buf, sizeof(notamTok));
    n = f.readBytesUntil('\n', buf, sizeof(buf) - 1);
    buf[n] = '\0';
    notamTokExp = (uint32_t)strtoul(buf, nullptr, 10);
    f.close();
    if (!notamTok[0] || !notamTokExp) {
        // An invalidated cache is a mint coming: worth one line.
        sce::trace::log("sd", "jeton NOTAM cache invalide, ignore");
        notamTok[0] = '\0'; notamTokExp = 0;
    } else {
        sce::trace::log("sd", "jeton NOTAM recharge (exp=%lu)",
                        (unsigned long)notamTokExp);
    }
}

// Called from loop() once the clock is worth something. Turns the monotonic
// remainder recorded at mint time into the absolute expiry the cache file
// stores, and writes it — a token that survives the reboot is one fewer taken
// out of the twenty. Idempotent and cheap: it does nothing at all unless a
// token is being held without a deadline.
static void notamTokStamp() {
    if (!notamTok[0] || notamTokExp || !notamTokPendTtl) return;
    const time_t nowUtc = time(nullptr);
    if (!sce::clockSynced(nowUtc)) return;
    const uint32_t spent = (millis() - notamTokPendMs) / 1000UL;
    if (spent >= notamTokPendTtl) {          // it expired while we waited
        notamTok[0] = '\0';
        notamTokPendTtl = 0;
        return;
    }
    notamTokExp = (uint32_t)nowUtc + (notamTokPendTtl - spent);
    notamTokPendTtl = 0;
    notamTokDirty = true;
    Serial.println("[radar] jeton NOTAM date apres NTP et ecrit sur la carte");
}

// Raised by our OWN yaml parser when it sees `tz_offset_h`, so the companion
// fallback below knows to stay out of the way. Reset at the top of loadConfig:
// a reload after a card insertion has to re-decide, not inherit the last run's
// verdict.
static bool tzFromOwnYaml = false;

// Counters for the loadConfig summary trace. File-scope because the recognizer
// is a captureless lambda: the final `else` of its chain used to drop a
// misspelled key on the floor with no witness at all, and the summary line is
// how a "why does my setting not apply" report gets answered from a capture.
static uint16_t gCfgKeys = 0, gCfgUnknown = 0;
static char gCfgFirstUnknown[24] = "";

static void loadConfig() {
    // Idempotent and cheap (two SD.exists()) — called here rather than at
    // each mount call site so a card inserted mid-session (noSdNotice retry,
    // SdWatch reinsertion) is covered the same as the initial boot mount.
    sce::migrateSdRoot();
    tzFromOwnYaml = false;
    gCfgKeys = 0; gCfgUnknown = 0; gCfgFirstUnknown[0] = '\0';
    // RE-DERIVED FROM THE FILE ON EVERY READ, and reset FIRST (08-03).
    // `theme_v` is only assigned when the key is present, so the variable
    // survives the read that did not mention it — and migrateTheme() latches it
    // to 1 whenever it runs against no card at all. Boot without a card, insert
    // one carrying a pre-07-28 yaml, and the reload found `themeVer == 1`
    // already: "nothing to migrate", old numbering applied to the new table,
    // permanently. Zero here means what it has always meant — old numbering
    // until the file says otherwise — and it now means it for the SECOND read
    // as much as for the first.
    themeVer = 0;
    // The RETURN VALUE matters here, and it did not use to be read: it is the
    // difference between a yaml that stayed silent on a key and a yaml that was
    // never there (see migrateTheme).
    cfgFileRead = sce::SceGuest::yamlForEach(CFG_PATH, false,
                               [](void*, const char*, const char* k,
                                  const char* v) {
        const String key(k);
        String val(v);                       // `airport` upcases it below
        if      (key == "lat")       cfg.lat      = val.toFloat();
        else if (key == "lon")       cfg.lon      = val.toFloat();
        else if (key == "radius_nm") cfg.radiusNm = constrain(val.toFloat(), 10.0f, 500.0f);
        else if (key == "poll_s")    cfg.pollS    = constrain(val.toInt(), 5, 60);
        else if (key == "api")       { val.toCharArray(cfg.api, sizeof(cfg.api)); }
        else if (key == "airport")   { val.toUpperCase();
                                       val.toCharArray(cfg.airport, sizeof(cfg.airport)); }
        else if (key == "tz_offset_h") { tzFromOwnYaml = true; cfg.tzOffsetH = val.toFloat(); }
        else if (key == "brightness")  cfg.bright = (uint8_t)constrain(val.toInt(), 10, 255);
        else if (key == "theme")       cfg.theme = (uint8_t)constrain(val.toInt(), 0, 3);
        // MIGRATION: before 2026-07-28, 1 = Scope and 2 = Night. An older
        // yaml therefore showed "Gundam night" in broad daylight. The
        // `theme_v` marker tells the two numberings apart; absent = old one.
        else if (key == "theme_v")     themeVer = (uint8_t)val.toInt();
        else if (key == "units")       cfg.metric = (val == "metrique" ||
                                                     val == "metric") ? 1 : 0;
        else if (key == "servo")       cfg.servo      = val.toInt() != 0;
        else if (key == "volume")      cfg.volume = (uint8_t)constrain(val.toInt(), 0, 100);
        else if (key == "notam_brief") cfg.notamBrief = val.toInt() != 0;
        else if (key == "dock_s")      cfg.dockS = (uint8_t)constrain(val.toInt(), 0, 120);
        // Legacy `sound: 0|1`, read ONLY to migrate a card written before the
        // level existed: an owner who had turned the chirps on must not find
        // them silently off after an update. A `volume:` line wins if present,
        // whichever order they appear in is decided by the file.
        else if (key == "sound")       cfg.volume = (val.toInt() != 0) ? 60 : 0;
        else if (key == "auto_bright") cfg.autoLum    = val.toInt() != 0;
        else if (key == "auto_night")  cfg.autoNight  = val.toInt() != 0;
        else if (key == "show_ground") cfg.showGround = val.toInt() != 0;
        else if (key == "follow")      cfg.follow     = val.toInt() != 0;
        else if (key == "metar_icao")  { val.toUpperCase();
                                         val.toCharArray(cfg.metarIcao,
                                                         sizeof(cfg.metarIcao)); }
        else if (key == "metar_rwy")   { val.toUpperCase();
                                         val.toCharArray(cfg.metarRwy,
                                                         sizeof(cfg.metarRwy)); }
        else if (key == "safesky_key") val.toCharArray(cfg.skyKey,
                                                       sizeof(cfg.skyKey));
        else if (key == "notam_fir")   val.toCharArray(cfg.notamFir,
                                                       sizeof(cfg.notamFir));
        else if (key == "notam_user")  val.toCharArray(cfg.notamUser,
                                                       sizeof(cfg.notamUser));
        else if (key == "notam_pass")  val.toCharArray(cfg.notamPass,
                                                       sizeof(cfg.notamPass));
        else {
            // The end of the chain is where a typo'd key silently dies:
            // count it and remember the first one so the summary can name it.
            gCfgUnknown++;
            if (!gCfgFirstUnknown[0])
                strlcpy(gCfgFirstUnknown, k, sizeof(gCfgFirstUnknown));
        }
        gCfgKeys++;
    }, nullptr);
    // One summary line instead of one per key: enough to tell "file missing"
    // from "file read but the key never reached a recognizer".
    sce::trace::log("cfg", "yaml %s : %s, %u cles dont %u inconnues (1ere: %s)",
                    CFG_PATH, cfgFileRead ? "lu" : "absent",
                    (unsigned)gCfgKeys, (unsigned)gCfgUnknown,
                    gCfgUnknown ? gCfgFirstUnknown : "-");
    // UI LANGUAGE — the COMPANION's key, not one of ours. `lang:` sits at the
    // top level of /stackchan-companion/config.yaml and is the single source read
    // by the console, the API and every guest bin: one robot, one language.
    // A copy in flightradar.yaml would be a second thing to keep in step, and
    // two language settings that can disagree are not a setting.
    // `sectioned = false` ON PURPOSE: in sectioned mode a NON-indented line is
    // read as a section header, so a top-level key never reaches the callback.
    // Flat, every line is a key — and config.yaml has no `lang` nested under a
    // section to collide with. Absent or unreadable: English (sce::setLang's
    // own fallback), which is what a card that never heard of the key gives.
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

// ---------------------------------------------------------------- aircraft
struct Plane {
    char     hex[8]    = "";
    char     flight[10] = "";
    float    lat = 0, lon = 0;
    float    altFt = 0, gs = 0, track = 0;
    bool     hasTrack = false;   // `track` MISSING != heading 000
    bool     onGround = false;
    char     cat[3] = "";      // ADS-B ICAO category ("A3", "A7"...)
    char     typ[5] = "";      // aircraft type ("A320", "R44"...)
    bool     mil = false;      // dbFlags bit0 = readsb military database
    int16_t  vrate = 0;        // baro_rate ft/min (vertical speed)
    char     squawk[5] = "";   // transponder code (7700 = emergency)
    uint32_t seenMs = 0;
    // Trail: LOCAL position history (accumulated at every poll — the
    // community APIs do not serve history; the trail fills up while we
    // watch).
    static constexpr int TRAIL = 48;
    float   tLat[TRAIL], tLon[TRAIL];
    uint8_t tCount = 0, tHead = 0;

    void pushTrail() {
        if (tCount && fabsf(tLat[(tHead + TRAIL - 1) % TRAIL] - lat) < 1e-4f &&
                      fabsf(tLon[(tHead + TRAIL - 1) % TRAIL] - lon) < 1e-4f)
            return;                       // stationary → no duplicate point
        tLat[tHead] = lat; tLon[tHead] = lon;
        tHead = (tHead + 1) % TRAIL;
        if (tCount < TRAIL) tCount++;
    }
};
static constexpr int MAX_PLANES = 48;    // RAM bound (~420 B/aircraft with
                                         // trail) — beyond that: "48+"
static Plane  planes[MAX_PLANES];
static int    planeCount   = 0;
static char   selectedHex[8] = "";       // targeted flight (empty = none)
static char   selectedRoute[32] = "";    // "LFPG-FIMP" via hexdb (when
                                         // available; multi-leg possible)
static float  origLat = 0, origLon = 0;  // route airports (when resolved)
static float  destLat = 0, destLon = 0;
static bool   origValid = false, destValid = false;
static char   origCity[20] = "", destCity[20] = "";  // "City/CC" (hexdb/adsbdb)
static char   origIata[8]  = "", destIata[8]  = "";  // SHORT code shown when
                                                     // available (CDG vs LFPG)
static char   airline[20]  = "";                     // "Air France" (adsbdb)
static char   origCtry[4]  = "", destCtry[4]  = "";  // country "FR" (pinned to
                                                     // the right of the code,
                                                     // 07-27)
static uint32_t lastPollMs = 0, lastPollOkMs = 0;

// THE SELECTED SOURCE IS THE PREFERRED ONE, NOT THE ONLY ONE (user 08-03).
// The three ADS-B feeds speak the same v2 format and cover the same sky
// unevenly; picking one used to mean that its outage was the radar's outage,
// with an empty screen and no way to tell "no traffic" from "no server". The
// same afternoon proved the point on another service: hexdb.io kept accepting
// TCP and answering nothing, and the airport search reported the user's
// airport as unknown.
// So: the preferred source is asked FIRST, every time. If it fails at the
// TRANSPORT level, the others are tried in a fixed order, and whichever
// answers serves the cycle. The substitution is STATED on screen - a radar
// showing traffic from a source you did not choose, silently, is worse than
// an empty one.
static char     apiServing[24] = "";   // what actually answered last cycle
static uint32_t apiFellBackMs  = 0;    // when the substitution started, 0 = none

// The order tried after the preferred one. `adsb.fi` and `adsb.lol` are the
// two that speak the same v2 dialect through the same code path; SafeSky is
// NOT here - it is a different account-bound service with its own key, and
// falling back onto something the user has not configured would be a surprise,
// not a rescue.
static const char* const API_ALT[] = { "airplanes.live", "adsb.lol", "adsb.fi" };
static constexpr int     API_ALT_N = 3;
static volatile bool pollBusy  = false; // poll in progress ("searching..."
                                        // banner)
static uint8_t       pollPenalty = 0;    // adaptive backoff: 0/1/2 → x1/x2/x4
                                         // on the period after a 429 or a
                                         // 5xx (the API is rate-limiting us:
                                         // we must SLOW DOWN, not insist)
static volatile bool pollFailed = false; // LAST cycle failed entirely — the
                                         // only condition that blames the API
                                         // (a 404 on a global lookup or 1 tile
                                         // out of 7 does not condemn the
                                         // cycle, adversarial check 07-27)
static bool          planesFullCycle = false; // saturation seen THIS cycle
static volatile bool planesFull = false; // published to the screen → "48+"
                                         // (the API sees more, we keep the
                                         // nearest ones — user 07-27)
static int    httpStatus   = 0;        // status of the poll CYCLE (1st error)
static char   trackQuery[10] = "";     // tracked callsign/fragment (UPPERCASE)

// ------------------------------------------------------------------- METAR
// Official observation of the station (aviationweather.gov, free, NO key).
// Kept in AERONAUTICAL units, exactly as the API serves them (knots, statute
// miles, feet, hPa, deg C): the metric conversion belongs to the display, like
// everywhere else in this bin (dD/dS/dA).
struct Metar {
    char  icao[8]   = "";
    char  name[40]  = "";        // "Reunion/St Denis/Garros Arpt, , RE"
    char  raw[144]  = "";        // rawOb — what a pilot actually reads
    // rawTaf — the FORECAST, and it comes FREE: `taf=1` on the request we
    // already make returns it in the same object, so the TAF view costs zero
    // extra connection. Much longer than a METAR (change groups: BECMG, TEMPO,
    // PROB40...).
    // 512 and no longer 264 (review 08-01). 264 was sized on the FMEE forecast
    // alone (~180 chars) and a busy field's TAF routinely runs past it — the
    // tail is the part furthest into the future, i.e. the part you came for.
    // 512 is not a guess either: the view can display 50 glyphs x 19 rows at
    // size 1, so anything past ~950 could not be shown whatever the buffer, and
    // 512 covers the forecasts actually served while the truncation that is
    // left SAYS SO rather than ending mid-group.
    char  taf[512]  = "";
    bool  tafCut    = false;     // rawTaf was longer than the buffer
    char  cover[12] = "";        // "CAVOK", "BKN"...
    char  cat[6]    = "";        // VFR / MVFR / IFR / LIFR (colour-coded)
    char  visib[12] = "";        // API STRING ("6+"): never parsed blindly
    // Cloud layers kept SPLIT (cover + base in FEET, the API unit) and not
    // pre-formatted: the metric toggle has to convert the base, and a
    // formatted "SCT 2500" would have to be parsed back to do it.
    char  layerCov[2][8] = {};   // 8 and not 6: a layer publishes its TYPE in
                                 // the same token ("///TCU" is SIX characters
                                 // and used to arrive truncated to "///TC")
    int   layerBaseFt[2] = { -1, -1 };
    uint8_t nLayers = 0;
    float temp = 0, dewp = 0;    // deg C (both unit systems)
    int   wdir  = -1;            // degrees, -1 = absent or VRB (a string)
    float wspd  = -1.0f;         // knots, -1 = absent
    float altim = 0;             // QNH hPa
    // PRESENCE flags, because 0 is a LEGAL value for all three (0 C, 0 C dew
    // point, and a missing QNH used to print "0 hPa" as if measured). The
    // relative humidity derived from temp/dewp also needs to know, or a
    // station reporting neither would have shown a confident 100 %.
    bool  hasTemp = false, hasDewp = false, hasAltim = false;
    uint32_t obsTime = 0;        // epoch s (age of the observation)
    bool  valid = false;
};
static Metar metar;                          // netTask writes, draw() reads: gMtx

// ------------------------------------------------------------- VIEW STACK
// The screens are STACKED and a vertical swipe moves ONE step. The state is
// therefore a LEVEL, not one boolean per view: `metarView` (what this used to
// be) could say "METAR or not" but had nowhere to put a third screen, and a
// second boolean would have made two flags able to disagree. Everything else
// — what is drawn, which gestures apply, whether the METAR is refreshed,
// whether the companion exit is armed — is DERIVED from this single number.
// Read by netTask, hence volatile.
// TAF sits between them BECAUSE it is the same fetch and the same station as
// the METAR, one step further from the ground truth: observed now, forecast
// next, then what is out of service. The order IS the meaning.
enum ViewLevel : uint8_t {
    VIEW_RADAR = 0, VIEW_METAR = 1, VIEW_TAF = 2, VIEW_NOTAM = 3
};
static constexpr uint8_t VIEW_TOP = VIEW_NOTAM;   // ceiling of the stack
static volatile uint8_t  viewLevel = VIEW_RADAR;

// A METAR is issued every 30 to 60 minutes: polling it at the radar's pace
// (5-60 s) would be hundreds of pointless TLS sessions per hour on a public
// service. So the fetch has its OWN period, and it only runs while its own
// step of the stack is on screen — entering a view whose data is still fresh
// reloads nothing at all.
static constexpr uint32_t METAR_PERIOD_MS = 10UL * 60UL * 1000UL;
static volatile uint32_t metarOkMs  = 0;     // millis() of the last success
static volatile int      metarHttp  = 0;     // last status (shown on screen)

// ---- WHEN TO ASK AGAIN, DECIDED BY THE DATA -------------------------------
// A fixed ten-minute timer asks a fixed six times an hour for something a
// station publishes twice — and it still shows an observation up to ten
// minutes after it was issued, because the tick and the issue time have no
// relation to each other. The report itself says when it was made (`obsTime`),
// which is enough to do better on both counts at once.
//
// The CYCLE is LEARNED rather than assumed: half-hourly and hourly stations
// both exist, and some publish special reports in between. Two consecutive
// different observation times are one measurement of this station's rhythm;
// it is clamped to the plausible range so a special report cannot teach the
// bin to poll every minute, nor a gap teach it to sleep for a day.
//
// Everything degrades to the old behaviour: no clock, no observation time, or
// a station that just changed, and the next poll is simply ten minutes away.
static constexpr uint32_t METAR_MIN_WAIT_S = 300;    // never faster than 5 min
static constexpr uint32_t METAR_MAX_WAIT_S = 900;    // never slower than 15
static constexpr uint32_t METAR_GRACE_S    = 150;    // publication lag
static uint32_t metarCycleS   = 1800;        // learned; 30 min until measured
static uint32_t metarPrevObs  = 0;           // the previous observation time
static uint32_t metarDueMs    = 0;           // 0 = due now

// Called with the parsed report in hand. `obs` is 0 when the station did not
// publish one, which is the case that has to fall back rather than guess.
static void metarSchedule(uint32_t obs) {
    const time_t nowT = time(nullptr);
    if (obs && metarPrevObs && obs > metarPrevObs) {
        uint32_t d = obs - metarPrevObs;
        if (d < 600)  d = 600;
        if (d > 3600) d = 3600;
        metarCycleS = d;
    }
    if (obs) metarPrevObs = obs;
    if (!obs || !sce::clockSynced(nowT)) {
        metarDueMs = millis() + METAR_PERIOD_MS;
        return;
    }
    const int32_t age = (int32_t)((uint32_t)nowT - obs);
    int32_t wait = (int32_t)(metarCycleS + METAR_GRACE_S) - age;
    if (wait < (int32_t)METAR_MIN_WAIT_S) wait = METAR_MIN_WAIT_S;
    if (wait > (int32_t)METAR_MAX_WAIT_S) wait = METAR_MAX_WAIT_S;
    metarDueMs = millis() + (uint32_t)wait * 1000UL;
}

// Network: netTask (core 0) is the SOLE owner of the TLS sockets — requests
// block for several seconds (in loop() they starved SceGuest's synchronous
// WebServer, diag 07-25; and two concurrent WiFiClientSecure exhaust the
// internal heap, review 07-26). loop() only does UI/touch/guest.update() and
// POSTS requests; sharing under the gMtx mutex.
static SemaphoreHandle_t gMtx = nullptr;
// ROUTE request by generation: loop() increments routeReqGen on every new
// selection (under gMtx, together with routeFlight); netTask captures
// (gen, callsign), resolves, and only APPLIES if gen is still current — a tap
// during an in-flight resolution is neither lost nor misattributed.
static volatile uint32_t routeReqGen = 0;
static char  routeFlight[10] = "";             // callsign to resolve (gMtx)
// Route resolution state, SHOWN on the panel (diagnostic 07-27: a silent
// failure left a "?" with no explanation and no retry).
static volatile int routeStatus = 0;   // 0 n/a  1 searching  2 found
                                       // 3 unknown (hexdb)  4 network failure
// Airport RECENTER request (keyboard) — resolved by netTask
static char  airportReq[8] = "";               // requested code
static volatile int airportState = 0;          // 0 idle 1 pending 2 ok
                                               // 3 unknown code, 4 no answer
static volatile bool pollNow = false;          // immediate re-poll requested
static volatile bool uiDirty = true;           // redraw requested (data)
static TaskHandle_t  netTaskHandle = nullptr;
// Guard parking netTask before a companion reflash (sce::CoopStop -- the
// ONE shared implementation, see SceGuest.h; this bin's hand-rolled pair of
// flags was its model).
static sce::CoopStop netGuard;
// Raised by a settings apply (web or panel), consumed by the sensor loop:
// forget the brightness hysteresis so the ruling source reasserts itself at
// once. The fix space carries (gAutoBrightReset); it was missing here.
static volatile bool gAutoLumReset = false;
static volatile bool cfgDirty = false;         // yaml persistence requested —
                                               // consumed by loop() ONLY
                                               // (never SD from netTask: SPI
                                               // shared with the LCD)

// ---------------------------------------------------------------------
// OPTIONAL StackChan peripherals (all driven by a settings toggle). The
// guest bin talks to the hardware directly: it has neither the companion's
// Board nor its HALs.
// ---------------------------------------------------------------------
// The neck: power, bus, non-blocking writes and torque release live in
// firmware/common/HeadServo.h, shared with the space bin.
#if SCE_HAS_SERVO
static sce::HeadServo head;
static volatile bool servoInitReq = false;  // init requested (handled by
                                            // loop: ~1.8 s of delays, NEVER
                                            // to be done inside an HTTP
                                            // handler)
#endif  // SCE_HAS_SERVO

#if SCE_HAS_LTR553
// LTR-553 (ambient light). The register map, the gain/integration calibration
// and the level curve now live in firmware/common/Ltr553.h, shared with the
// companion's hal/Ltr553.h: this file used to carry its own copy of those
// numbers, kept in step with the companion by a COMMENT (A2.23). The reader
// itself stays guest-side — a bin with no Brain has no bus lock to honour and
// takes the four data bytes in one burst.
static sce::ltr553::Lite ltr;
static bool  ltrOk = false;
static bool  ltrBegin()   { return ltr.begin(); }
static int32_t ltrVisible() { return ltr.visible(); }
#endif  // SCE_HAS_LTR553

// A ticked checkbox is sent as "on" by the browser: toInt() returned 0, so
// EVERY toggle enabled from the web page was applied as zero (seen on HW
// 07-27d). We accept the three usual forms.
// Delegates to the project's ONE definition (firmware/common/CfgBool.h): three
// bins had three answers, and space's disagreed on `off`.
static inline bool webBool(const String& v) { return sce::webBool(v.c_str()); }

static sce::SceGuest guest;
static String  ipStr;
static M5Canvas canvas(&M5.Display);
static bool saveConfigSd();            // defined below (yaml persistence)

// ONE GESTURE PER JOB (user 07-31): the swipe DOWN belongs to SceGuest at
// EVERY step — it always means "back to the companion" — and the whole stack
// travels on the swipe UP, which cycles. The alternative, arming the exit at
// the bottom step only, made the same gesture mean two things depending on
// where you stood: it worked, but it put our navigation and the guest contract
// on the same input, and the contract is not ours to bend. Now nothing this
// bin does can break the way out of it.
// So the exit stays armed unconditionally. The function survives because the
// MODALS (keyboard, settings) suspend the gesture while they own the screen
// and restore it through this one call site — one place to change if the rule
// ever moves again.
// Guarded because the modals that call it are touch-only: on a button board
// nothing suspends the gesture, so nothing has to restore it.
#if SCE_INPUT_TOUCH
static void armSwipeExit() { guest.setSwipeExit(true); }
#endif

// Moves to an absolute step of the stack. The ONE place the level changes, so
// the derived state cannot drift from it.
static void setViewLevel(uint8_t lvl) {
    if (lvl > VIEW_TOP) lvl = VIEW_TOP;
    viewLevel = lvl;
    uiDirty = true;
}
// One step UP, and it CYCLES: past the top comes the radar again. There is no
// "step down" any more — down is the companion's, at every level — so this
// takes no direction and the stack is a ring travelled one way.
static void stepView() {
    setViewLevel(viewLevel >= VIEW_TOP ? VIEW_RADAR : (uint8_t)(viewLevel + 1));
}

// ------------------------------------------------------ geo → screen (radar)
// Radar shifted LEFT; tracked-flight info column on the right (07-26).
static constexpr int PANEL_X = 228;                  // panel left edge
static constexpr int CX = 114, CY = 120, RPX = 100;  // screen centre + radius

// SPEED VECTOR horizon, in minutes. The vector represents a flight DURATION,
// so its on-screen length depends on the ZOOM. At 500 nm, one minute at
// 450 kt is only 1.5 px — below the drawing threshold, so the vector simply
// VANISHED (user report 07-28 on SS636, 453 kt). The horizon therefore grows
// in steps until it becomes legible again; real scopes offer exactly this
// setting (1/2/4/8 min).
static int vectorMin() {
    for (int m = 1; m < 8; m *= 2)
        if (450.0f * (float)m / 60.0f / cfg.radiusNm * RPX >= 10.0f) return m;
    return 8;
}
using fr::NM_PER_DEG_LAT;              // ONE single definition (geo.h, tested)

static void planeToXY(float plat, float plon, int& x, int& y) {
    float dLatNm = (plat - cfg.lat) * NM_PER_DEG_LAT;
    float dLonNm = (plon - cfg.lon) * NM_PER_DEG_LAT * cosf(cfg.lat * DEG_TO_RAD);
    x = CX + (int)(dLonNm / cfg.radiusNm * RPX);
    y = CY - (int)(dLatNm / cfg.radiusNm * RPX);
}
// ---- DISPLAY units: AERO ⇄ METRIC toggle across the WHOLE interface
// (user 07-27). Storage, computations and API requests stay in aeronautical
// units — the ones the ADS-B services speak — ; only the presentation
// changes, no rounding ever propagates to the network.
static inline float       dD(float nm) { return cfg.metric ? nm * 1.852f  : nm; }
static inline const char* uD()         { return cfg.metric ? "km"   : "nm"; }
static inline float       dS(float kt) { return cfg.metric ? kt * 1.852f  : kt; }
static inline const char* uS()         { return cfg.metric ? "km/h" : "kt"; }
static inline float       dA(float ft) { return cfg.metric ? ft * 0.3048f : ft; }
static inline const char* uA()         { return cfg.metric ? "m"    : "ft"; }

// Geodesy: PURE implementations in geo.h (covered by test_flightgeo)
static inline float gcNm(float la1, float lo1, float la2, float lo2) {
    return fr::gcNm(la1, lo1, la2, lo2);
}

static inline float distNmAt(float plat, float plon) {
    return fr::planarNm(cfg.lat, cfg.lon, plat, plon);
}

static float distNm(const Plane& p) {
    float dLat = (p.lat - cfg.lat) * NM_PER_DEG_LAT;
    float dLon = (p.lon - cfg.lon) * NM_PER_DEG_LAT * cosf(cfg.lat * DEG_TO_RAD);
    return sqrtf(dLat * dLat + dLon * dLon);
}

// Clears the ROUTE state (CALL WITH gMtx HELD). SINGLE point of truth: the
// 4 hand-written copies had already DIVERGED — the COUNTRY codes were only
// cleared in selectPlaneLocked, so after a keyboard entry or a tap on empty
// space the country of the PREVIOUS flight stayed pasted next to the "?" of
// the new one (max review 07-27, 4 converging angles).
static void clearRouteLocked() {
    selectedRoute[0] = '\0';
    origCity[0] = destCity[0] = '\0';
    origIata[0] = destIata[0] = '\0';
    origCtry[0] = destCtry[0] = '\0';
    airline[0] = '\0';
    origValid = destValid = false;
}

// Selects planes[i] (CALL WITH gMtx HELD): track + route request.
// SINGLE path shared by the tap, the keyboard and the auto re-lock
// (duplicated in 3 places before the 07-26 review). `setTrack` = remember
// the callsign as a track query (UPPERCASE — matching upcases the aircraft).
// Last emergency squawk THAT ALREADY SOUNDED. Kept at file scope, and reset
// on EVERY selection change: held inside draw(), it stayed armed after a
// deselection and the alarm never sounded again for that squawk on another
// flight (max review 07-28).
static char lastEmerg[5] = "";

static void selectPlaneLocked(int i, bool setTrack) {
    lastEmerg[0] = '\0';                 // new target: alarm REARMED
    strlcpy(selectedHex, planes[i].hex, sizeof(selectedHex));
    // A blip WITHOUT a callsign does not overwrite a track armed from the
    // keyboard (max review 07-26: trackQuery was silently cleared).
    if (setTrack && planes[i].flight[0]) {
        strlcpy(trackQuery, planes[i].flight, sizeof(trackQuery));
        // `unsigned char`: the callsign comes off the wire and a mis-decoded
        // frame can carry a byte >= 0x80, which is NEGATIVE as a plain char.
        upAscii(trackQuery);
    }
    strlcpy(routeFlight, planes[i].flight, sizeof(routeFlight));
    clearRouteLocked();
    routeStatus = planes[i].flight[0] ? 1 : 0;   // hex only: nothing to resolve
    // Gen bumped on EVERY selection mutation (even without a callsign): an
    // in-flight fetchRoute for the PREVIOUS AIRCRAFT becomes stale and its
    // result is dropped (protocol hole, max review 07-26).
    routeReqGen = routeReqGen + 1;   // volatile: no ++
    uiDirty = true;
}

// Track re-lock (CALL WITH gMtx HELD): targets the first aircraft whose
// callsign contains trackQuery — shared by the local poll and the global
// request.
// ---- Sound patterns (option "son", OFF by default) ----------------------
// Gundam identity: short cockpit-HUD sequences rather than an isolated beep
// — the lock-on RISES, the alarm FALLS and repeats. The speaker shares the
// I2S1 bus with the microphones on the companion side, but this bin never
// records: no arbitration needed.
//
// Played STEP BY STEP from loop(): `tone()` does not block, but chaining
// notes needs pacing, and the trigger can come from netTask. Only STATIC
// pointers travel — same discipline as the dances on the companion side
// (A2.17, last bullet).
struct Note { uint16_t hz, ms; uint8_t vol; };
static const Note SEQ_LOCK[]  = { {1175, 45, 160}, {1568, 45, 160}, {2093, 110, 160} };
static const Note SEQ_SEL[]   = { {1568, 35, 140}, {2349,  55, 140} };
// WEATHER WORSENED — the EXACT MIRROR of SEQ_LOCK, note for note (08-01).
// The vocabulary of this bin already says "rising = something acquired"; a
// falling version of the same three notes therefore says "something lost"
// without teaching the ear a new sound. It plays ONCE and does not loop: the
// emergency alarm is the only event allowed to insist, and a cloud base is not
// a squawk 7700.
static const Note SEQ_WX_DOWN[] = { {2093, 45, 160}, {1568, 45, 160}, {1175, 110, 160} };

// Emergency alarm MEASURED from docs/assets/gundam-warning-alarm.mp3
// (PCM decode + FFT + envelope, 07-28):
//   - PURE tone at 1757 Hz, 2nd harmonic at 3515, no other component;
//   - modulated at 10.5 Hz, 95 ms period, 18 periods = 1.71 s;
//   - the low phase does NOT cut the sound: it drops to about 20-30 % — it is
//     ducking, not chopping, hence two volume notes per period;
//   - the DECAY is part of the pattern (175 -> 76, factor 0.43) and mostly
//     affects the last third. It is therefore reproduced, and the whole
//     pattern LOOPS — decay included (user 07-28).
// 1760 Hz = A6, 3 Hz off the measurement: inaudible difference, and it lines
// up with the other patterns.
static const Note SEQ_ALARM[] = {
    { 1760, 45, 175 }, { 1760, 50,  50 },   //  1
    { 1760, 45, 175 }, { 1760, 50,  62 },   //  2
    { 1760, 45, 148 }, { 1760, 50,  54 },   //  3
    { 1760, 45, 140 }, { 1760, 50,  73 },   //  4
    { 1760, 45, 137 }, { 1760, 50,  59 },   //  5
    { 1760, 45, 145 }, { 1760, 50,  68 },   //  6
    { 1760, 45, 133 }, { 1760, 50,  68 },   //  7
    { 1760, 45, 142 }, { 1760, 50,  55 },   //  8
    { 1760, 45, 137 }, { 1760, 50,  57 },   //  9
    { 1760, 45, 133 }, { 1760, 50,  54 },   // 10
    { 1760, 45, 134 }, { 1760, 50,  67 },   // 11
    { 1760, 45, 137 }, { 1760, 50,  62 },   // 12
    { 1760, 45, 123 }, { 1760, 50,  52 },   // 13
    { 1760, 45, 111 }, { 1760, 50,  59 },   // 14
    { 1760, 45,  90 }, { 1760, 50,  50 },   // 15
    { 1760, 45,  86 }, { 1760, 50,  46 },   // 16
    { 1760, 45,  84 }, { 1760, 50,  46 },   // 17
    { 1760, 45,  76 }, { 1760, 50,  39 },   // 18
    // 2 s of breathing room BEFORE the restart: that pause is what makes the
    // pattern read as a cycle instead of a continuous noise (user 07-28).
    { 0, 2000, 0 },
};

// EVERYTHING is volatile: playSeq() runs on netTask (core 0) and soundTick()
// on loop() (core 1). Publishing seqPtr as volatile orders nothing with
// respect to ordinary fields — the reader could see the new pointer with the
// OLD length and read dozens of Note entries past the array (max review
// 07-28).
// Night, as decided by the SAME rule that swaps the theme — the REAL SUN since
// 08-01 (sce::isNight over the radar's own lat/lon), no longer a fixed
// 21 h - 7 h window. Published here because the sound reads it too: one notion
// of "it is night", not two that can disagree.
static volatile bool nightNow = false;

static const Note* volatile seqPtr = nullptr;
static volatile uint8_t  seqLen = 0, seqIdx = 0, seqLoops = 0;
static volatile uint32_t seqNextMs = 0;

// `force` serves the preview from the settings panel: the "son" toggle has
// not been applied to cfg yet when we already want to hear the pattern.
// `loops` = number of EXTRA repetitions of the whole pattern.
template <size_t N>
static void playSeq(const Note (&notes)[N], bool force = false,
                    uint8_t loops = 0) {
    if (!cfg.volume && !force) return;
    seqLen = (uint8_t)N; seqIdx = 0; seqNextMs = 0; seqLoops = loops;
    seqPtr = notes;              // published LAST: read by loop()
}
static void stopSeq() {
    seqPtr = nullptr; seqLoops = 0;
    M5.Speaker.stop();
}

// Called on every loop() pass: advances one note once the previous one has
// finished sounding.
static void soundTick() {
    const Note* p = seqPtr;
    if (!p) return;
    // WAIT first: testing the end BEFORE the deadline cut the LAST note of
    // every pattern one tick after it started — the high note the two chirps
    // are built around never sounded at all (max review 07-28).
    uint32_t now = millis();
    if (seqNextMs && (int32_t)(now - seqNextMs) < 0) return;
    if (seqIdx >= seqLen) {
        if (!seqLoops) { stopSeq(); return; }
        seqLoops--; seqIdx = 0;      // the pattern RESTARTS, decay included
    }
    const Note& n = p[seqIdx++];
    if (n.hz) {
        // The patterns carry a MEASURED envelope (the alarm's ducking and its
        // decay were read off the reference recording): the level scales the
        // whole envelope instead of replacing it, so a quiet alarm is still
        // the same alarm. Halved at night, on the same notion of night that
        // swaps the theme — a chirp that is right in a lit room is a startle
        // at 23 h, and the machine already knows what time it is.
        uint16_t v = (uint16_t)n.vol * cfg.volume / 100;
        if (nightNow) v /= 2;
        M5.Speaker.setChannelVolume(0, (uint8_t)v);
        M5.Speaker.tone(n.hz, n.ms, 0);
    }
    // EXACT duration, no added padding: the pattern is timed to the
    // millimetre (95 ms period) and 10 ms of slack shifted it by 20 %.
    seqNextMs = now + n.ms;
}

// ---- REVERSED leg: the route is given for the OTHER direction ----------
// The databases give the CANONICAL direction of a flight number, and several
// airlines reuse the same designator on the return leg: SS636 is announced
// Marseille → Reunion while it actually flies Reunion → Marseille (user
// report 07-28 — real track 309°, bearing to the announced destination 130°,
// the exact opposite). Without a fix the origin, the destination, the
// progress AND the ETA are wrong, and the "remaining" distance GROWS as the
// flight goes on.
//
// The swap happens AT DISPLAY TIME, never before caching: resolution blocks
// for several seconds on two TLS requests, and if the user changes flight
// meanwhile, the track we read belonged to ANOTHER aircraft — the wrongly
// reversed route was then cached for two hours and replayed on every
// selection (max review 07-28). So the cache keeps the canonical route; the
// swap is recomputed on every draw.
// CALL WITH gMtx HELD.
static void maybeSwapLegLocked(char* res, float& ola, float& olo, bool& ov,
                               float& dla, float& dlo, bool& dv,
                               char* oCity, char* dCity, char* oIata,
                               char* dIata, char* oCc, char* dCc,
                               const char* cs) {
    if (!res[0] || !ov || !dv) return;
    float trk = 0.0f, spd = 0.0f, plat = 0.0f, plon = 0.0f;
    float alt = 99999.0f, vr = 0.0f;
    bool  hasTrk = false, found = false;
    for (int i = 0; i < planeCount; i++)
        if (selectedHex[0] && !strcmp(planes[i].hex, selectedHex)) {
            trk = planes[i].track; spd = planes[i].gs;
            plat = planes[i].lat;  plon = planes[i].lon;
            alt = planes[i].altFt; vr  = (float)planes[i].vrate;
            hasTrk = planes[i].hasTrack;
            found  = true;
            break;
        }
    if (!found) return;
    // The DECISION is fr::legReversed (pure, tested): the cruise track test
    // — whose 150 kt floor guards against transient turns and whose hasTrack
    // flag guards against Mode-S aircraft that publish no track at all — and
    // the phase-of-flight test that catches what the floor hides: an
    // aircraft DESCENDING onto its announced ORIGIN is arriving there at
    // approach speed, under the very threshold the track test needs (user
    // 08-04: watched a landing at RUN under a panel saying RUN→Marseille).
    if (!fr::legReversed(hasTrk, spd, trk,
                         fr::bearingDeg(plat, plon, dla, dlo), alt, vr,
                         fr::gcNm(plat, plon, ola, olo),
                         fr::gcNm(plat, plon, dla, dlo))) return;
    Serial.printf("[route] %s troncon INVERSE (trk %.0f spd %.0f alt %.0f "
                  "vr %.0f)\n", cs, trk, spd, alt, vr);
    auto swapF = [](float& a, float& b) { float t = a; a = b; b = t; };
    auto swapB = [](bool& a, bool& b)   { bool  t = a; a = b; b = t; };
    auto swapS = [](char* a, char* b, size_t n) {
        char t[24]; strlcpy(t, a, sizeof(t));
        strlcpy(a, b, n); strlcpy(b, t, n);
    };
    swapF(ola, dla); swapF(olo, dlo); swapB(ov, dv);
    swapS(oCity, dCity, 20);
    swapS(oIata, dIata, 5);
    swapS(oCc,   dCc,   4);
    // "MRS-RUN" → "RUN-MRS": the label must follow, otherwise it contradicts
    // the DEPARTURE / ARRIVAL blocks right below it.
    char* dash = strchr(res, '-');
    if (dash) {
        char left[16], right[16];
        size_t ln = (size_t)(dash - res);
        if (ln < sizeof(left)) {
            memcpy(left, res, ln); left[ln] = '\0';
            strlcpy(right, dash + 1, sizeof(right));
            snprintf(res, 32, "%s-%s", right, left);
        }
    }
}

static void acquireTrackedLocked() {
    if (!trackQuery[0]) return;
    for (int i = 0; i < planeCount; i++) {
        char up[10]; strlcpy(up, planes[i].flight, sizeof(up));
        upAscii(up);
        if (up[0] && strstr(up, trackQuery)) {
            if (strcmp(selectedHex, planes[i].hex)) {
                selectPlaneLocked(i, false);
                playSeq(SEQ_LOCK);        // flight locked on
            }
            break;
        }
    }
}

// ------------------------------------------------------------------ poll API
// Number of selectable sources — read by the settings chips AND by the
// web `Choice` list, which must stay in step (a chip with no counterpart in
// the browser form is a setting you can only change with a finger).
static constexpr int N_API = 4;

// SafeSky `beacon_type` → ICAO category. The category is what picks the blip
// SHAPE, and the shapes are the radar's vocabulary: a FLARM glider must look
// like a glider whoever reported it. SafeSky's value is the ONLY hint about
// what the aircraft is — it publishes no ADS-B category and no type.
static const char* skyCat(const char* bt) {
    if (!strcmp(bt, "HELICOPTER")) return "A7";   // circle + rotor cross
    if (!strcmp(bt, "GLIDER"))     return "B1";   // 2 px wing
    if (!strcmp(bt, "UAV") || !strcmp(bt, "PARAGLIDER") ||
        !strcmp(bt, "HANGGLIDER") || !strcmp(bt, "BALLOON"))
        return "B4";                              // hollow delta
    if (!strcmp(bt, "JET"))        return "A3";   // airliner triangle
    return "A1";                                  // anything else: light
}

// `api` is a SNAPSHOT taken under mutex at the start of a cycle: an OK from
// the settings panel during a tiling pass cannot mix two sources.
static String apiBase(const char* api) {
    if (!strcmp(api, "adsb.lol")) return "https://api.adsb.lol/v2/point/";
    if (!strcmp(api, "adsb.fi"))  return "";   // different URL format
    return "https://api.airplanes.live/v2/point/";
}

// ---------------------------------------------------------------- HTTPS+JSON
// ONE single way to query a service. The bin's calls (ADS-B tile, SafeSky,
// hexdb airport, hexdb route, adsbdb fallback, METAR) repeated the same dozen
// lines — TLS open, timeout, GET, parse, `end()` — and had drifted apart:
// three of them parsed into the INTERNAL heap instead of PSRAM.
//
// Returns: the HTTP status (200 = success), or a NEGATIVE code for a local
// failure. These codes are shown verbatim on screen ("HTTP -1"): they
// therefore start at -1000 so they cannot be confused with a real status nor
// with HTTPClient's own errors, which occupy exactly -1 and -2
// (HTTPC_ERROR_CONNECTION_REFUSED, HTTPC_ERROR_SEND_HEADER_FAILED) and are
// returned as-is by this function. Two opposite causes used to carry the same
// number on screen, the only trace available without a hardware probe.
static constexpr int HTTP_BEGIN_KO = -1000;   // cannot open
static constexpr int HTTP_PARSE_KO = -1001;   // unreadable body
static constexpr int HTTP_NETSTOP  = -1002;   // reflash imminent: nothing opened
static constexpr int HTTP_NO_STATION = -1003; // METAR: no 4-letter ICAO to ask
// autorouter answers 403 for TWO unrelated things and the remedies are
// opposite: "privileges" needs a support ticket, "toomanytokens" needs nothing
// at all. Given its own code so the screen can stop conflating them (08-01).
// -1005 and NOT -1004: that one was already HTTP_NO_KEY, one line below.
// Two opposite meanings on one integer, in one translation unit, with no
// diagnostic — `notamHttp` could not tell "no credentials configured" from
// "403 toomanytokens", i.e. the two states whose remedies are opposite and
// which this constant was added to separate (review 08-02). It escaped only
// because drawNotamEmpty happens to test `!creds` first.
static constexpr int HTTP_TOOMANYTOK = -1005; // 403 toomanytokens (limit 20)
static constexpr int HTTP_NO_KEY     = -1004; // SafeSky: API key missing

// `hdrName`/`hdrValue`: ONE optional request header — SafeSky authenticates
// with `x-api-key`, and the alternative was a second copy of this function
// (the very drift this one exists to prevent). An empty value adds nothing.
// sce::PsSink now lives in firmware/common/PsJson.h: the space bin met the same
// chunked-transfer trap and two copies of the fix is how they diverge.

static int fetchJson(const String& url, JsonDocument& doc, uint16_t timeoutMs,
                     const JsonDocument* filter = nullptr,
                     const char* hdrName = nullptr,
                     const char* hdrValue = nullptr) {
    if (netGuard.stopping()) {
        // The reflash guard is the one actor that silences a whole fetch chain
        // with no visible cause; without this line a capture shows requests
        // simply stopping, which reads as a crash rather than a parking.
        sce::trace::log("http", "netguard stoppe : requete annulee");
        return HTTP_NETSTOP;  // reflash imminent: open nothing
    }
    WiFiClientSecure tls;
    // No CA anchor. The payloads are public data; what is NOT public is the
    // SafeSky key travelling in a header, so this is an ASSUMED weakness: an
    // attacker able to impersonate the API on the local network could harvest
    // it. Pinning would mean shipping a root bundle that expires and reflashing
    // the bin when it does, for a key that grants read-only access to public
    // traffic. Revocation is one field away (/config, sentinel "-").
    tls.setInsecure();
    HTTPClient http;
    // Host+path only, never the query string: some callers put identifiers in
    // it, and the trace must stay safe to paste whole into a bug report.
    const int urlCut = url.indexOf('?');
    const int urlLen = urlCut >= 0 ? urlCut : (int)url.length();
    if (!http.begin(tls, url)) {
        // A begin() refusal (bad URL, no memory for TLS) used to be mute and
        // indistinguishable from a server that never answered.
        sce::trace::log("http", "begin KO %.*s", urlLen, url.c_str());
        return HTTP_BEGIN_KO;
    }
    if (hdrName && hdrValue && hdrValue[0]) http.addHeader(hdrName, hdrValue);
    // TWO knobs, and this line used to claim there was one. `setTimeout`
    // stores the READ budget then, on connect, pushes it onto the socket
    // (`_client->setTimeout((_tcpTimeout + 500) / 1000)`, HTTPClient.cpp:1168);
    // `setConnectTimeout` is a SEPARATE field handed to `_client->connect(host,
    // port, _connectTimeout)` one line earlier (:1162), and left alone it holds
    // the library's own default of 5 s. So the budget was never `timeoutMs`:
    // it was 5 s of connect plus `timeoutMs` of reading, and the sentence
    // denying the second knob is what kept anyone from adding up the two.
    // Set explicitly at that same 5 s — the behaviour is unchanged, the worst
    // case is now written where it can be read, and the other two guest bins
    // set both knobs for the same reason.
    //
    // A `tls.setTimeout(timeoutMs)` used to sit here and did NOTHING, twice
    // over: `WiFiClientSecure::setTimeout` expects SECONDS (we were handing it
    // 6000 s), and as long as the socket is not open — which is the case
    // before `GET()` — it only sets a field that `HTTPClient::connect()`
    // overwrites right after. The comment therefore promised a read deadline
    // that never existed (review finding 2026-07-29).
    http.setConnectTimeout(5000);
    http.setTimeout(timeoutMs);
    // THE canonical line of the debug trace: every JSON request in this bin
    // funnels through here, so one line per attempt reconstructs the whole
    // network timeline — including how long a timeout actually took.
    const uint32_t t0 = millis();
    int code = http.GET();
    sce::trace::log("http", "GET %.*s -> %d en %lu ms taille=%d",
                    urlLen, url.c_str(), code,
                    (unsigned long)(millis() - t0), http.getSize());
    if (code != 200) { http.end(); return code; }
    sce::psAlloc.reset();
    // getSize() is -1 when the server sends no Content-Length, i.e. when the
    // body is CHUNKED. Parsing the raw stream then feeds ArduinoJson the chunk
    // framing; only writeToStream() strips it. See sce::PsSink above.
    DeserializationError err;
    if (http.getSize() >= 0) {
        err = filter ? deserializeJson(doc, http.getStream(),
                                       DeserializationOption::Filter(*filter))
                     : deserializeJson(doc, http.getStream());
        http.end();
    } else {
        sce::PsSink sink;
        http.writeToStream(&sink);
        http.end();
        if (sink.overflowed()) {
            Serial.printf("[radar] corps chunked trop gros (%s)\n", url.c_str());
            return HTTP_PARSE_KO;
        }
        err = filter ? deserializeJson(doc, sink.data(),
                                       DeserializationOption::Filter(*filter))
                     : deserializeJson(doc, sink.data());
    }
    if (err) {
        Serial.printf("[radar] json err %s (heap=%u psram=%u)\n",
                      err.c_str(), (unsigned)ESP.getFreeHeap(),
                      (unsigned)ESP.getFreePsram());
        return HTTP_PARSE_KO;
    }
    return 200;
}

// Queries ONE point (the v2 APIs cap at 250 nm per request) and merges into
// planes[] (dedup by hex — the trail survives). httpStatus remembers the
// FIRST error of the cycle (otherwise the last tile masked the failures).
// `csGlobal` (user 07-27): WORLDWIDE /v2/callsign/<cs> request instead of the
// point — brings back a tracked flight even outside the radar radius (same
// ac[] format).
static bool fetchPoint(const char* api, float plat, float plon, int radNm,
                       const char* csGlobal = nullptr) {
    String url;
    // SafeSky is a DIFFERENT beast from the three ADS-B mirrors: a key in a
    // header, metres and m/s instead of feet and knots, a flat array instead
    // of `ac[]`. Everything that differs is dealt with HERE, at the entry
    // point; nothing downstream (display, sorting, geometry, metric toggle)
    // learns that a second unit system exists.
    const bool sky = !strcmp(api, "safesky");
    char skyKey[80] = "";
    bool skyGround = true;
    if (sky) {
        xSemaphoreTake(gMtx, portMAX_DELAY);
        strlcpy(skyKey, cfg.skyKey, sizeof(skyKey));
        skyGround = cfg.showGround != 0;
        xSemaphoreGive(gMtx);
        // No key = no request at all. Opening a TLS session to collect a 401
        // would show up as "API en echec (HTTP 401)" — a diagnosis pointing at
        // the service instead of at the missing setting.
        if (!skyKey[0]) {
            if (httpStatus == 0 || httpStatus == 200) httpStatus = HTTP_NO_KEY;
            return false;
        }
        // SafeSky has no callsign endpoint: worldwide tracking is an ADS-B
        // mirror feature only (pollPlanes does not even ask, this is a guard).
        if (csGlobal) return false;
        // `rad` is capped at 20 000 m by the API — 10.8 nm, when `radius_nm`
        // goes up to 500. Past that we switch to `viewport`, the bounding BOX
        // of the same circle: a user who sets 200 nm must get 200 nm, not a
        // silent 10. The box is wider than the circle at the corners (harmless:
        // the radar clips what falls outside anyway) and the API caps a viewport
        // answer at 300 aircraft, which is far above the 48 slots kept here.
        const float radM = (float)radNm * 1852.0f;
        if (radM <= 20000.0f) {
            url = "https://api.safesky.app/v1/uav?lat=" + String(plat, 4) +
                  "&lng=" + String(plon, 4) + "&rad=" + String((int)radM);
        } else {
            float dLat = (float)radNm / NM_PER_DEG_LAT;
            float cosL = cosf(plat * DEG_TO_RAD);
            // Near the poles cos(lat) collapses and the longitude span blows
            // up: clamped so the box stays a box instead of a NaN or a request
            // spanning several turns of the globe.
            if (cosL < 0.02f) cosL = 0.02f;
            float dLon = (float)radNm / (NM_PER_DEG_LAT * cosL);
            if (dLon > 180.0f) dLon = 180.0f;
            url = "https://api.safesky.app/v1/uav?viewport=" +
                  String(plat - dLat, 4) + "," + String(plon - dLon, 4) + "," +
                  String(plat + dLat, 4) + "," + String(plon + dLon, 4);
        }
        url += String("&show_grounded=") + (skyGround ? "true" : "false");
    } else if (csGlobal) {
        if (!strcmp(api, "adsb.fi"))
            url = String("https://opendata.adsb.fi/api/v2/callsign/") + csGlobal;
        else if (!strcmp(api, "adsb.lol"))
            url = String("https://api.adsb.lol/v2/callsign/") + csGlobal;
        else
            url = String("https://api.airplanes.live/v2/callsign/") + csGlobal;
    } else if (!strcmp(api, "adsb.fi"))
        url = "https://opendata.adsb.fi/api/v2/lat/" + String(plat, 4) +
              "/lon/" + String(plon, 4) + "/dist/" + String(radNm);
    else
        url = apiBase(api) + String(plat, 4) + "/" + String(plon, 4) +
              "/" + String(radNm);
    // ArduinoJson filter: keep only the useful fields (the full response near
    // a hub can exceed 100 KB). SafeSky answers a FLAT array, so its filter is
    // an array too — `filter[0]` describes every element.
    JsonDocument filter;
    if (sky) {
        JsonObject fs = filter[0].to<JsonObject>();
        fs["id"] = fs["call_sign"] = fs["latitude"] = fs["longitude"] = true;
        fs["altitude"] = fs["ground_speed"] = fs["course"] = true;
        fs["vertical_rate"] = fs["status"] = fs["beacon_type"] = true;
    } else {
        JsonObject fac = filter["ac"].add<JsonObject>();
        fac["hex"] = fac["flight"] = fac["lat"] = fac["lon"] = true;
        fac["alt_baro"] = fac["gs"] = fac["track"] = true;
        fac["baro_rate"] = fac["squawk"] = true;          // vario + emergency
        fac["category"] = fac["dbFlags"] = fac["t"] = true;   // ICAO category,
                                                              // military flag,
                                                              // aircraft type
    }
    // Doc in PSRAM (8 MB): even FILTERED, a hub such as CDG means hundreds of
    // aircraft → the doc outgrew the internal heap, deserializeJson failed
    // and the radar showed 0 aircraft (diag 07-27: recentre on MRU/ORY/CDG).
    JsonDocument doc(&sce::psAlloc);
    // 15 s: a HUB response (ORY/CDG: hundreds of aircraft, 300-500 KB body)
    // over weak WiFi exceeded the 8 s budget → truncated stream,
    // "json err IncompleteInput", 0 aircraft (serial diag 07-27).
    // ⚠ `x-api-key` is DEPRECATED by SafeSky in favour of a per-request
    // HMAC-SHA256 signature (KID derivation, HKDF, single-use nonce,
    // timestamp within ±5 min) — https://api.safesky.app/doc/authentication.
    // It still works, and the signature would need mbedTLS plus a clock we
    // only get from NTP (which the bin does have, but not before the WiFi is
    // up and not reliably on an AP fallback). KNOWN DEBT: the day the header
    // is retired, SafeSky stops answering and only this source breaks.
    int code = fetchJson(url, doc, 15000, &filter,
                         sky ? "x-api-key" : nullptr, sky ? skyKey : nullptr);
    if (code != 200) {
        if (httpStatus == 0 || httpStatus == 200) httpStatus = code;
        return false;
    }

    uint32_t now = millis();
    xSemaphoreTake(gMtx, portMAX_DELAY);       // merge under mutex (short)
    JsonArray list = sky ? doc.as<JsonArray>() : doc["ac"].as<JsonArray>();
    for (JsonObject a : list) {
        // ---- SOURCE NORMALISATION: everything below this block works on
        // ADS-B units (feet, knots, ft/min) and ADS-B field names. SafeSky
        // serves metres, m/s and its own vocabulary, so it is translated
        // ONCE, here. Propagating two unit systems into the merge, the
        // display and the geometry is how a "why is this glider at 30 000 ft"
        // bug is born.
        char skyId[8] = "";
        const char* hex;
        if (sky) {
            const char* id = a["id"] | "";
            if (!id[0] || !a["latitude"].is<float>()) continue;
            // `hex` is 7 characters + NUL (an ICAO address is 6). A SafeSky id
            // can be longer, so we keep the TAIL: the head of these ids is a
            // constant prefix per transponder type, the entropy is at the end,
            // and two aircraft colliding here would MERGE into one blip.
            size_t n = strlen(id);
            strlcpy(skyId, n > 7 ? id + (n - 7) : id, sizeof(skyId));
            hex = skyId;
        } else {
            hex = a["hex"] | "";
            if (!hex[0] || !a["lat"].is<float>()) continue;
        }
        const float nlat = sky ? (float)(a["latitude"]  | 0.0f)
                               : (float)(a["lat"] | 0.0f);
        const float nlon = sky ? (float)(a["longitude"] | 0.0f)
                               : (float)(a["lon"] | 0.0f);
        // Look for the existing aircraft (trail kept), else a free slot
        Plane* p = nullptr;
        for (int i = 0; i < planeCount; i++)
            if (!strcmp(planes[i].hex, hex)) { p = &planes[i]; break; }
        if (!p) {
            if (planeCount < MAX_PLANES) {
                p = &planes[planeCount++];
                *p = Plane{};             // purges COMPACT the table: a slot
            }                             // keeps the callsign+trail of the
                                          // dead entry — an aircraft with no
                                          // callsign inherited it and the
                                          // track locked onto the WRONG hex
                                          // (max review 07-27)
            else {                        // table full → evict the FARTHEST,
                                          // never the tracked flight. Sorting
                                          // by AGE did not work: every
                                          // aircraft merged within one cycle
                                          // shares the same seenMs, so slot 0
                                          // was always picked and "we keep
                                          // the closest ones" was false
                                          // (max review 07-27).
                planesFullCycle = true;   // → explicit "48+" counter
                Plane* v = nullptr; float worst = -1.0f;
                for (int i = 0; i < MAX_PLANES; i++) {
                    if (selectedHex[0] &&
                        !strcmp(planes[i].hex, selectedHex)) continue;
                    float dd = distNm(planes[i]);
                    if (!v || dd > worst) { v = &planes[i]; worst = dd; }
                }
                // The newcomer gets in ONLY if it is closer than the worst
                // kept entry: without this comparison an aircraft at 480 nm
                // evicted a blip at 200 nm and "we keep the closest ones"
                // stayed false (review 07-27b) — while tiling, the satellite
                // tiles (the farthest, merged last) systematically evicted
                // the centre.
                if (!v || distNmAt(nlat, nlon) >= worst) continue;
                p = v;
                *p = Plane{};
            }
            strlcpy(p->hex, hex, sizeof(p->hex));
        }
        const char* fl = sky ? (a["call_sign"] | "") : (a["flight"] | "");
        if (fl[0]) { strlcpy(p->flight, fl, sizeof(p->flight));
                     char* e = p->flight + strlen(p->flight);
                     while (e > p->flight && e[-1] == ' ') *--e = '\0';
                     // Callsign that arrived AFTER the tap (hex-only blip at
                     // selection time): arm the route now — otherwise it
                     // would NEVER be resolved (diag 07-27).
                     if (p->flight[0] && !routeFlight[0] &&
                         !strcmp(p->hex, selectedHex)) {
                         strlcpy(routeFlight, p->flight, sizeof(routeFlight));
                         routeStatus = 1;
                         routeReqGen = routeReqGen + 1;
                     } }
        p->lat = nlat; p->lon = nlon;
        if (sky) {
            // THE conversion point: metres AMSL → feet, m/s → knots, m/s →
            // ft/min. Past this line the aircraft is indistinguishable from an
            // ADS-B one.
            p->onGround = !strcmp(a["status"] | "", "GROUNDED");
            // -9999 m is SafeSky's "altitude unknown" sentinel (their model
            // definition). Converted blindly it becomes -32 808 ft: a nonsense
            // figure on the tracked-flight card, and a value that then feeds
            // the altitude colour ramp. Unknown collapses to 0, which is how
            // the ADS-B path already treats a missing `alt_baro`.
            const int altM = a["altitude"] | 0;
            p->altFt = (p->onGround || altM <= -1000)
                     ? 0.0f : (float)altM * 3.28084f;
            p->gs    = (float)(a["ground_speed"] | 0.0f) * 1.94384f;
            p->hasTrack = a["course"].is<float>() || a["course"].is<int>();
            p->track = a["course"] | 0.0f;
            p->vrate = (int16_t)((float)(a["vertical_rate"] | 0.0f) * 196.85f);
            const char* bt = a["beacon_type"] | "";
            strlcpy(p->cat, skyCat(bt), sizeof(p->cat));
            // 4 characters is all `typ` holds, and the truncation happens to
            // read well: HELICOPTER → "HELI", GLIDER → "GLID", PARAGLIDER →
            // "PARA". No aircraft TYPE is published by this source.
            strlcpy(p->typ, bt, sizeof(p->typ));
            p->mil = false;            // no military database behind SafeSky
            p->squawk[0] = '\0';       // nor a transponder code: no emergency
        } else {                       // squawk detection on this source
            p->onGround = a["alt_baro"].is<const char*>();      // "ground"
            p->altFt = p->onGround ? 0.0f : (float)(a["alt_baro"] | 0);
            p->gs    = a["gs"]    | 0.0f;
            p->hasTrack = a["track"].is<float>() || a["track"].is<int>();
            p->track = a["track"] | 0.0f;
            strlcpy(p->cat, a["category"] | "", sizeof(p->cat));
            strlcpy(p->typ, a["t"] | "", sizeof(p->typ));
            p->mil   = (((int)(a["dbFlags"] | 0)) & 1) != 0;
            p->vrate = (int16_t)(int)(a["baro_rate"] | 0);
            strlcpy(p->squawk, a["squawk"] | "", sizeof(p->squawk));
        }
        p->seenMs = now;
        p->pushTrail();
    }
    xSemaphoreGive(gMtx);
    return true;
}

// ---- THE AIRPORT CACHE -----------------------------------------------------
// The one lookup in this bin whose answer NEVER changes. An aerodrome's
// coordinates, its city and its country are fixed; the route cache above it
// expires after two hours precisely because a callsign's destination does not
// keep, but "where is FMEE" keeps for ever.
//
// WHAT IT COSTS WITHOUT ONE. Resolving a route is one request for the route
// plus one per waypoint — up to SIX TLS sessions, on the network task, for a
// single tap. The route ring already spares the repeat within two hours, but
// every reboot and every route sharing an airport paid the full price again:
// the same half-dozen airports, over and over, on a domestic radar that mostly
// watches the same handful of aerodromes.
//
// PERSISTED, and that is the point — a RAM-only cache would be re-earned at
// every boot, which is the case that costs the most requests. The file is a
// CSV the bin writes and reads and no human is meant to edit; like the NOTAM
// token, it stays out of the yaml so a machine write cannot truncate a
// documented file.
//
// NO EXPIRY on a hit, therefore, and no ceremony: the entry is only dropped
// when the ring wraps. Negative results are remembered TOO but only in RAM and
// only for codes the database ANSWERED about — an unreached database says
// nothing about the code, and a 404 today may be a record tomorrow, so that
// verdict must not survive a reboot.
// ONE ENTRY PER AERODROME, not one cache per question. The tower frequency,
// the ATIS and the field elevation are exactly as permanent as the latitude,
// they are keyed by the same code, and they were being re-fetched every boot
// from a second endpoint on the same host. A second ring beside this one would
// have been two files, two save paths and two ways to disagree about what
// "FMEE" means (A2.23). `hasPos` and `hasStn` say which halves are filled;
// either one alone is worth a line on the card.
struct ApEntry {
    char  code[6] = "";
    float la = 0, lo = 0;
    char  city[20] = "";
    char  iata[8] = "";
    char  cc[4] = "";
    // THREE STATES, and the first version had only two — which was a bug, not a
    // simplification (review 08-05). `known == false` was read at the lookup as
    // "the database answered and does not know this code", but it was ALSO the
    // default of any slot created by the STATION half: the first METAR success
    // called apStoreStn("FMEE"), and from then on every position lookup for
    // FMEE answered "unknown airport" straight from cache, without asking
    // anyone — for the very aerodrome the radar is centred on. A verdict must
    // be STATED, never inferred from the absence of something else.
    bool  hasPos = false;           // coordinates resolved
    bool  hasStn = false;           // frequencies / elevation resolved
    bool  neg    = false;           // the database ANSWERED: no such code
    char  atis[10] = "";
    char  twr[10]  = "";
    int16_t elevFt = -9999;         // -9999 = unknown, 0 is a LEGAL elevation
};
static constexpr int AP_CACHE_N = 48;
static ApEntry gApCache[AP_CACHE_N];
static int     gApNext = 0;
static volatile bool gApDirty = false;      // loop() alone writes the card
static constexpr const char* AP_PATH = "/stackchan-companion/radar-airports.csv";

// Codes are compared CASE-INSENSITIVELY: hexdb answers "FMEE" whatever case
// went in, and a route field can arrive lower-cased. Two entries for one
// aerodrome would be a cache that dilutes itself.
static ApEntry* apFind(const char* code) {
    for (int i = 0; i < AP_CACHE_N; i++)
        if (gApCache[i].code[0] && !strcasecmp(gApCache[i].code, code))
            return &gApCache[i];
    return nullptr;
}

// The slot for a code, created if needed. Kept apart from the two writers
// below so that filling one half of an entry never clears the other: the
// position and the station data arrive from two endpoints, minutes apart.
static ApEntry* apSlot(const char* code) {
    ApEntry* e = apFind(code);
    if (!e) {
        e = &gApCache[gApNext];
        gApNext = (gApNext + 1) % AP_CACHE_N;
        *e = ApEntry();
        strlcpy(e->code, code, sizeof(e->code));
    }
    return e;
}

static void apStorePos(const char* code, bool known, float la, float lo,
                       const char* city, const char* iata, const char* cc) {
    ApEntry* e = apSlot(code);
    if (!known) {                   // a stated negative carries NO coordinates:
        e->neg = true;              // lookupAirportNet leaves la/lo untouched on
        return;                     // failure, so they are indeterminate here
    }                               // (negatives are never written to the card)
    // THE VALUES FIRST, THE FLAG LAST. loop() reads this array to write the
    // card while netTask fills it; publishing `hasPos` before la/lo let a save
    // catch the entry between the two and put Null Island on the card for a
    // real airport — permanently, since the cache has no expiry (review 08-05).
    e->la = la; e->lo = lo;
    strlcpy(e->city, city ? city : "", sizeof(e->city));
    strlcpy(e->iata, iata ? iata : "", sizeof(e->iata));
    strlcpy(e->cc,   cc   ? cc   : "", sizeof(e->cc));
    e->neg    = false;
    e->hasPos = true;
    gApDirty  = true;
}

static void apStoreStn(const char* code, const char* atis, const char* twr,
                       int elevFt) {
    ApEntry* e = apSlot(code);
    strlcpy(e->atis, atis ? atis : "", sizeof(e->atis));
    strlcpy(e->twr,  twr  ? twr  : "", sizeof(e->twr));
    e->elevFt = (int16_t)elevFt;
    // An aerodrome that publishes a tower frequency EXISTS: a stale negative
    // verdict about the same code cannot also be true, so it goes.
    e->neg    = false;
    e->hasStn = true;               // flag last, same reason as apStorePos
    gApDirty  = true;
}

// Rewritten WHOLE rather than appended: the ring overwrites entries in place,
// so an append-only file would grow without bound and disagree with RAM about
// which forty-eight are current. Forty-eight short lines is one small write.
// Called from loop() ONLY (SD and LCD share SPI2 — A2.16).
static bool apSaveSd() {
    // Traced returns: a cache that silently fails to persist re-spends one
    // TLS lookup per airport at every boot, and looks like a slow network.
    if (!sdOk) {
        sce::trace::log("sd", "cache aeroports non ecrit : pas de carte");
        return false;
    }
    File f = SD.open(AP_PATH, FILE_WRITE);   // truncates
    if (!f) {
        sce::trace::log("sd", "cache aeroports : open %s KO", AP_PATH);
        return false;
    }
    int written = 0;
    for (int i = 0; i < AP_CACHE_N; i++) {
        // A COPY, not a reference. netTask can be filling this very slot while
        // loop() formats it, and a printf that read `code` from the old entry
        // and `la` from the new one would write a line that was never true.
        const ApEntry e = gApCache[i];
        if (!e.code[0] || (!e.hasPos && !e.hasStn)) continue;
        f.printf("%s;%.5f;%.5f;%s;%s;%s;%d;%s;%s;%d\n",
                 e.code, e.la, e.lo, e.city, e.iata, e.cc,
                 e.hasPos ? 1 : 0, e.atis, e.twr, (int)e.elevFt);
        written++;
    }
    f.close();
    sce::trace::log("sd", "cache aeroports ecrit : %d entrees", written);
    return true;
}

static void apLoadSd() {
    // Same rationale as apSaveSd: the mute entry returns are the difference
    // between "cold cache by design" and "cold cache by accident".
    if (!sdOk) {
        sce::trace::log("sd", "cache aeroports non lu : pas de carte");
        return;
    }
    File f = SD.open(AP_PATH, FILE_READ);
    if (!f) {
        sce::trace::log("sd", "cache aeroports : pas de fichier (%s)", AP_PATH);
        return;
    }
    // BOUNDED read, the rest of the line thrown away — the same shape as every
    // other file this bin reads (A2.23). A corrupted long line costs one entry,
    // never a String that grew before anyone measured it.
    char line[96];
    while (f.available()) {
        const size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
        line[n] = '\0';
        if (n == 0) continue;
        // The comment above promises the rest of the line is thrown away;
        // for a while nothing did. A record longer than the buffer left
        // its tail in the stream, parsed as the NEXT record — a 3-4 char
        // fragment passes the code-length check and is stored, cached
        // without expiry and written back by apSaveSd: a permanent
        // phantom airport. A too-long record is discarded whole.
        if (n == sizeof(line) - 1) {
            while (f.available() && f.read() != '\n') {}
            continue;
        }
        char* p = line;
        auto field = [&p]() -> char* {
            char* s = p;
            char* semi = strchr(p, ';');
            if (semi) { *semi = '\0'; p = semi + 1; } else { p = s + strlen(s); }
            return s;
        };
        char* code = field();
        char* la   = field();
        char* lo   = field();
        char* city = field();
        char* iata = field();
        char* cc   = field();
        // The last four arrived with the station half (08-04). A file written
        // before that stops here, and `field()` hands back empty strings rather
        // than walking off the end — an older card degrades to what it knew,
        // which is the whole reason the fields are positional and appended.
        char* pos  = field();
        char* atis = field();
        char* twr  = field();
        char* elev = field();
        for (char* c = elev; *c; c++) if (*c == '\r') *c = '\0';
        if (strlen(code) < 3 || strlen(code) > 4) continue;
        // No `pos` column at all = a file from before the merge, whose every
        // line was a resolved position.
        if (!pos[0] || atoi(pos)) apStorePos(code, true, atof(la), atof(lo), city, iata, cc);
        if (atis[0] || twr[0] || (elev[0] && atoi(elev) != -9999))
            apStoreStn(code, atis, twr, elev[0] ? atoi(elev) : -9999);
    }
    f.close();
    gApDirty = false;               // just read: nothing new to write back
    Serial.printf("[radar] cache aeroports : %d entrees\n",
                  [] { int k = 0; for (const ApEntry& e : gApCache) if (e.code[0]) k++; return k; }());
}

// Airport code resolution (3-letter IATA / 4-letter ICAO) → lat/lon via
// hexdb.io.
static bool lookupAirportNet(const char* code, float* la, float* lo,
                          char* region = nullptr, size_t rsz = 0,
                          char* iata = nullptr, char* cc = nullptr,
                          bool* reached = nullptr) {
    size_t n = strlen(code);
    if (n < 3 || n > 4 || WiFi.status() != WL_CONNECTED) {
        if (reached) *reached = false;          // never even asked
        return false;
    }
    String url = String("https://hexdb.io/api/v1/airport/") +
                 (n == 3 ? "iata/" : "icao/") + code;
    JsonDocument doc(&sce::psAlloc);
    // TWO FAILURES, NOT ONE (user 08-03: "I searched CDG and it says unknown").
    // CDG is a perfectly good IATA code; what happened is that hexdb.io stopped
    // answering - DNS resolves, TCP 443 accepts, and the request then hangs
    // until the timeout, while another aeronautical API answers 200 from the
    // same machine. The lookup reported that as "unknown", i.e. it told the
    // user something FALSE about their airport rather than something true about
    // the database. `reached` carries the difference up: an HTTP 200 that
    // simply has no latitude means the database does not know this code; any
    // other outcome means the database did not speak.
    sce::trace::log("http", "aeroport %s : resolution %s",
                    code, n == 3 ? "iata" : "icao");
    const int http = fetchJson(url, doc, 6000);
    if (reached) *reached = (http == 200);
    if (http != 200 || !doc["latitude"].is<float>()) {
        // The two failures this function exists to tell apart, named: an
        // unknown code and an unreachable database call for different fixes.
        sce::trace::log("http", "aeroport %s : %s (http=%d)", code,
                        http == 200 ? "inconnu de la base"
                                    : "base injoignable", http);
        return false;
    }
    *la = doc["latitude"]; *lo = doc["longitude"];
    sce::trace::log("http", "aeroport %s -> %.4f,%.4f",
                    code, (double)*la, (double)*lo);
    if (region && rsz) {              // readable city/region (user 07-26):
        const char* r = doc["region_name"] | "";   // "Reunion",
        if (!r[0]) r = doc["airport"] | "";        // "Ile-de-France"...
        strlcpy(region, r, rsz);
    }
    if (iata) strlcpy(iata, doc["iata"] | "", 8);         // "CDG"
    if (cc)   strlcpy(cc, doc["country_code"] | "", 4);   // "FR"
    return true;
}

// The one callers use. Cache first, network second, cache written back — so
// the SIX lookups a multi-leg route can need collapse to the ones this radar
// has genuinely never seen.
static bool lookupAirport(const char* code, float* la, float* lo,
                          char* region = nullptr, size_t rsz = 0,
                          char* iata = nullptr, char* cc = nullptr,
                          bool* reached = nullptr) {
    if (const ApEntry* e = apFind(code)) {
        // A slot may exist WITHOUT a position — the station half of the cache
        // creates one for the METAR aerodrome. Only `hasPos` may answer yes,
        // only `neg` may answer no; anything else falls through and asks.
        if (e->hasPos) {
            if (reached) *reached = true;  // we know, from a request that landed
            *la = e->la; *lo = e->lo;
            if (region && rsz) strlcpy(region, e->city, rsz);
            if (iata) strlcpy(iata, e->iata, 8);
            if (cc)   strlcpy(cc,   e->cc,   4);
            return true;
        }
        if (e->neg) { if (reached) *reached = true; return false; }
    }
    char rg[20] = "", ia[8] = "", co[4] = "";
    bool got = false;
    const bool ok = lookupAirportNet(code, la, lo, rg, sizeof(rg), ia, co, &got);
    if (reached) *reached = got;
    // Only what the database ANSWERED is remembered. A lookup that never
    // reached it must stay unknown, or one outage would be cached as fact —
    // the exact failure the `reached` flag was added to tell apart (08-03).
    if (got) apStorePos(code, ok, *la, *lo, rg, ia, co);
    if (ok) {
        if (region && rsz) strlcpy(region, rg, rsz);
        if (iata) strlcpy(iata, ia, 8);
        if (cc)   strlcpy(cc,   co, 4);
    }
    return ok;
}

// ------------------------------------------------------------------- METAR
// Station to ask for. `airport` is REUSED when it holds a 4-letter ICAO code:
// it is the field the radar is already centred on, and asking for the weather
// of the airport you are watching is the only sensible default. It cannot
// always serve: `airport` also accepts a 3-letter IATA code (RUN), and
// aviationweather.gov indexes ICAO ids ONLY — "RUN" returns an empty array,
// which would show up as a mute "no data". Hence `metar_icao`, used whenever
// `airport` is IATA or empty.
// CALL WITH gMtx HELD (reads cfg).
static void metarStation(char* out, size_t n) {
    if (strlen(cfg.airport) == 4) { strlcpy(out, cfg.airport, n); return; }
    strlcpy(out, cfg.metarIcao, n);
}

// "Is there anything to ask about?" — the guard the background refresh needs.
// Same rule as metarStation, without the buffer: netTask now polls the
// observation whether or not its view is open (the weather alert has to watch
// it), and polling an empty station code would be one wasted TLS session every
// ten minutes for the entire life of the bin.
static bool metarStationSet() {
    return strlen(cfg.airport) == 4 || strlen(cfg.metarIcao) == 4;
}

// ---- RUNWAY of the station, read from the SD database (OurAirports, public
// domain — regenerated by tools/generators/make-runways.py, see docs/guests).
// `runways.csv` holds FIXED-WIDTH records of 17 bytes, sorted by ICAO then by
// DECREASING length, so the first record of an aerodrome is its MAIN runway:
//
//     FMEE   12 30 102\n     ICAO on 7, ends on 3 + 3, TRUE heading on 3
//
// The fixed width is the whole point: file size / 17 = the record count, so
// the lookup is a BINARY SEARCH on offsets — 14 probes plus one confirming
// read for the ~14 200 runways of the base, against a 4 MB linear scan the bin
// could neither hold nor afford. No cache is needed at that price.
// Every failure path is the same one: no runway, and metarRose then draws
// ALONE — which is still true. A missing card, a missing file, an unknown
// ICAO, or a size that is not a whole number of records (i.e. not the file we
// generated: seeking into the middle of a line would answer nonsense) all
// return false rather than block the view.
// ⚠ SD from loop() ONLY, like saveConfigSd: SD and the LCD share SPI2, and
// this bin has no renderer to pause (the companion's A2.16 rule). netTask
// never touches the card.
static constexpr const char* RWY_PATH  = "/stackchan-companion/runways.csv";
static constexpr size_t      RWY_IDENT = 7;    // ICAO field width
static constexpr size_t      RWY_REC   = 17;   // 7 + 3 + 3 + 3 + '\n'

static bool runwayFromSd(const char* icao, char* out, size_t n) {
    if (n) out[0] = '\0';
    const size_t idl = icao ? strlen(icao) : 0;
    if (!sdOk || !idl || idl > RWY_IDENT) return false;
    File f = SD.open(RWY_PATH, FILE_READ);
    if (!f) return false;
    const uint32_t sz = f.size();
    if (sz < RWY_REC || (sz % RWY_REC) != 0) { f.close(); return false; }
    char key[RWY_IDENT];
    memset(key, ' ', RWY_IDENT);
    memcpy(key, icao, idl);                      // left-aligned, space padded
    char rec[RWY_REC];
    // lower_bound: the SMALLEST index whose ident is >= the key. Landing on
    // "an" occurrence would not do — an aerodrome has several records and we
    // want the FIRST, which the generator made the longest runway.
    uint32_t lo = 0, hi = sz / RWY_REC;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        if (!f.seek(mid * RWY_REC) ||
            f.read((uint8_t*)rec, RWY_REC) != (int)RWY_REC) {
            f.close(); return false;
        }
        if (memcmp(rec, key, RWY_IDENT) < 0) lo = mid + 1;
        else                                 hi = mid;
    }
    bool ok = false;
    if (lo < sz / RWY_REC && f.seek(lo * RWY_REC) &&
        f.read((uint8_t*)rec, RWY_REC) == (int)RWY_REC &&
        memcmp(rec, key, RWY_IDENT) == 0) {
        char le[4] = "", he[4] = "", hd[4] = "";
        memcpy(le, rec + 7,  3);
        memcpy(he, rec + 10, 3);
        memcpy(hd, rec + 13, 3);
        char* ends[2] = { le, he };              // fields are space padded
        for (int i = 0; i < 2; i++) {
            char* s = strchr(ends[i], ' ');
            if (s) *s = '\0';
        }
        // Rebuilt in the `metar_rwy` syntax on purpose: ONE parser
        // (metarRunway) reads both the typed setting and the base, so the two
        // sources can never drift apart.
        if (le[0] && he[0])
            ok = snprintf(out, n, "%s/%s@%d", le, he, atoi(hd)) < (int)n;
    }
    f.close();
    if (!ok && n) out[0] = '\0';
    return ok;
}

// Runway currently drawn, and the station it was resolved for. Written by
// loop() (the sole SD user), read by drawMetar.
static char rwyDbIcao[8]  = "";
static char rwyDbSpec[16] = "";

// PRIORITY: the typed setting FIRST. It is what lets a user pick a secondary
// runway (FMEE has two), or cover an aerodrome the base does not list, or
// correct it — the base is only the default. Neither one = no runway at all.
static inline const char* runwaySpec() {
    return cfg.metarRwy[0] ? cfg.metarRwy : rwyDbSpec;
}

// Percent-encoding, needed exactly once but non-negotiably: the credentials go
// into an x-www-form-urlencoded body and a real password holds @ # ^ &, every
// one of which changes the meaning of that body. Sending it raw does not fail
// loudly - it authenticates as a DIFFERENT, truncated password and returns a
// puzzling 401.
static String urlEncode(const char* s) {
    // NOT named HEX: Arduino's Print.h already defines that as the numeric
    // base constant, and the shadow turns every use below into a subscript on
    // an int (compile error at a line that looks innocent).
    static const char* HEXD = "0123456789ABCDEF";
    String out;
    for (const char* c = s; *c; c++) {
        const unsigned char u = (unsigned char)*c;
        if (isalnum(u) || u == '-' || u == '_' || u == '.' || u == '~') out += (char)u;
        else { out += '%'; out += HEXD[u >> 4]; out += HEXD[u & 15]; }
    }
    return out;
}
// =========================== NOTAM (autorouter / EUROCONTROL EAD) ==========
// The source that finally answered for this station (2026-07-31). It is FREE
// but not anonymous: OAuth 2.0 "client_credentials" reusing the account
// e-mail and password, and API access is a permission autorouter grants on
// request — an activated account alone returns 403 "privileges".
//
// WHAT IS KEPT, AND WHY IT IS A CHOICE. FMEE answered 27 NOTAMs; only 13 were
// in force at that moment, and their E field ran from 74 to 1673 characters.
// Showing all 27 on a 320x240 screen is not a display, it is a haystack. So:
//   - IN FORCE ONLY, when the clock is trustworthy (NTP). A NOTAM that expired
//     or has not started yet is noise on a card meant to be read at a glance.
//     Without NTP nothing is filtered and the screen says so, rather than
//     hiding items on the strength of a clock reading 1970.
//   - SORTED BY PIB PURPOSE, the aeronautical order and not ours: NBO (needs
//     immediate attention) before BO, before B, before M (miscellaneous, not
//     normally briefed). Same order the French SIA's own briefing tool uses.
//   - ONE PER SCREEN, paged with horizontal swipes. A list of 13 one-line
//     summaries would be a menu of things you cannot read.
// Text is bounded and a truncated one SAYS SO: a NOTAM cut without warning is
// exactly the failure this whole view refused to ship for.
// 40 and no longer 14 (user 08-01, "si je n'ai pas tous les NOTAM, ca peut
// etre problematique ?" - yes, it can). 14 was a screen-era number: it bit
// at FMEE with the briefing filter OFF (16 in force, 2 dropped), and a deck
// that silently stops is the one thing an aeronautical display must not do.
// The cost is small enough that the bound should not be the binding one:
// the slot array is 40 x ~60 B in PSRAM, the same again as a temporary on
// netTask's stack (16 KB, 10.8 KB free measured), and the E texts are PSRAM
// allocations of at most 1800 B each - 72 KB of the 8.19 MB free.
// It still CANNOT be unbounded: the API itself is asked for limit=100, and
// a bound you state beats a bound you discover. What changed is that the
// screen now SAYS when it bites ("1/40 of 57"), which is the difference
// between a bounded tool and a misleading one.
static constexpr int  NOTAM_MAX  = 40;    // kept per station
// Sized on the SOURCE now, not on the screen (user 08-01). The screen holds
// ~700 characters and the longest E field measured on FMEE was 1673, so a
// screen-sized buffer meant cutting the text and saying so — and the notice
// pointed at /config, where there was nothing to read. A NOTAM the reader
// cannot finish is a NOTAM they have to distrust. The text is kept WHOLE and
// PAGED instead; 1800 covers every field seen and the cap remains, because a
// bound you can state beats a bound you discover.
static constexpr int  NOTAM_TXT  = 1800;  // bytes of E kept per NOTAM
struct NotamItem {
    char     id[12];      // "A0944/26"
    char     q[6];        // Q-code subject+condition, "WLLW"
    char     sched[26];   // item D, the schedule ("MON-FRI 0200-1300")
    char     lo[14], hi[14];   // items F and G: lower / upper limit
    char     qline[52];  // Q) rebuilt as printed: FIR/Qcode/traffic/purpose
                         //     /scope/lower/upper/centre+radius
    char     itema[10];  // item A, the aerodrome the notice is ABOUT
    uint32_t from, to;    // validity, Unix seconds
    uint8_t  prio;        // 0 NBO, 1 BO, 2 B, 3 M/other
    bool     cut;         // E was longer than we kept
    char*    text;        // PSRAM
};
static NotamItem* notams = nullptr;       // PSRAM, NOTAM_MAX slots
static volatile int  notamCount = 0;
static volatile int  notamIdx   = 0;      // card being read
static volatile int  notamPage  = 0;      // page WITHIN that card
static volatile int  notamHttp  = 0;
static volatile int  notamInForce = 0;    // in force, BEFORE the cap
static volatile int  notamPages   = 1;    // pages of the card last drawn

// Move ONE page through the deck. Pages first, cards second: running off the
// end of a card lands on the first page of the next, and off the start lands
// on the LAST page of the previous — which is what "back" means when you were
// reading downwards. `notamPage = -1` is resolved by the draw, the only place
// that knows how many pages a card has.
// Shared by the horizontal SWIPE and the edge TAP: one behaviour, one place.
static void notamStep(int dir) {
    const int nn = notamCount;
    if (nn <= 0) return;
    const int np = notamPages > 0 ? notamPages : 1;
    if (dir > 0) {
        if (notamPage + 1 < np) notamPage++;
        else { notamPage = 0; notamIdx = (notamIdx + 1) % nn; }
    } else {
        if (notamPage > 0) notamPage--;
        else { notamIdx = (notamIdx - 1 + nn) % nn; notamPage = -1; }
    }
    uiDirty = true;
}
// Set by the long press; read by netTask to cancel the failure spacing ONCE.
static volatile uint32_t metarForceMs = 0, notamForceMs = 0;
// FEEDBACK FOR THE LONG PRESS. Holding a finger down produced nothing until it
// fired, so there was no way to tell "it is counting" from "it did not take"
// — and the natural reaction to no answer is to lift and try again, which is
// exactly what cancels it (user 08-01).
// Two moments, two states, and NOT a progress bar: an animated bar would mean
// redrawing the rose at 30 Hz for 700 ms to move a few pixels.
//   holdArmMs  set at 250 ms of a still finger — "I can see your finger"
//   holdFireMs set when the refresh actually goes out — "it left"
static volatile uint32_t holdArmMs = 0, holdFireMs = 0;
static constexpr uint32_t HOLD_MS      = 700;   // press -> refresh
static constexpr uint32_t HOLD_ARM_MS  = 250;   // press -> "counting"
static constexpr uint32_t HOLD_SHOW_MS = 1600;  // how long the answer stays
// UiEvent::NetInfo: the console address, on demand. Long enough to READ and
// TYPE somewhere else, which a gesture acknowledgement never has to be.
static volatile uint32_t netInfoMs = 0;
static constexpr uint32_t NETINFO_SHOW_MS = 8000;
// "FMEE" or "FMEE/FMMM": the cache belongs to the QUERY, and since 08-01 the
// query can carry a FIR as well. Keyed on the station alone, adding or
// clearing the FIR would have left the old deck in place under a header
// that no longer describes it.
static char     notamFor[16] = "";        // query the cache belongs to
static volatile uint32_t notamOkMs = 0;

// A NOTAM is not weather: it changes when an authority publishes one, not on a
// clock. 30 minutes is already generous, and like the METAR nothing is fetched
// at all until the view is opened once.
static constexpr uint32_t NOTAM_PERIOD_MS = 30UL * 60UL * 1000UL;

// PIB purpose -> our sort rank. The field is 4 chars, space padded ("NBO ").
static uint8_t notamPrio(const char* purpose) {
    if (!purpose) return 3;
    if (strncmp(purpose, "NBO", 3) == 0) return 0;
    if (strncmp(purpose, "BO",  2) == 0) return 1;
    if (purpose[0] == 'B')               return 2;
    return 3;                              // M, K, anything else
}

// POST the credentials, get a bearer token. Kept for its announced lifetime
// minus a minute: autorouter returned 604800 s (a week), so this is one login
// per week and not one per query.
// TRY THE CACHED TOKEN FIRST, whatever our own clock thinks of its age
// (user 08-02). The previous rule only reused it while the LOCAL deadline said
// it was alive — so a clock that had not synced, or had drifted, minted a
// fresh token for a perfectly good one. The scarce resource here is the TOKEN:
// twenty active per account, seven days each. An unnecessary round trip costs
// one HTTP request; an unnecessary mint costs one of the twenty for a week.
// The server is the authority on validity, so we let it say no.
// `force` = the caller was refused and wants a new one (see fetchNotam).
// Returns whether a token is available; `minted` (may be null) reports whether
// this call created one, which is what stops the retry from looping.
static bool notamToken(bool force = false, bool* minted = nullptr) {
    if (minted) *minted = false;
    if (!force && notamTok[0]) return true;      // optimistic: let the server judge
    if (netGuard.stopping()) return false;
    // SNAPSHOT under the mutex, the same discipline fetchPoint applies to the
    // SafeSky key: settingSet rewrites these buffers from the web-server task,
    // and a torn read here authenticates as a DIFFERENT, truncated password —
    // exactly the failure urlEncode exists to prevent (review 08-01).
    char user[64], pass[64];
    xSemaphoreTake(gMtx, portMAX_DELAY);
    strlcpy(user, cfg.notamUser, sizeof(user));
    strlcpy(pass, cfg.notamPass, sizeof(pass));
    xSemaphoreGive(gMtx);
    if (!user[0] || !pass[0]) {
        // Distinguishes "never configured" from every network failure below.
        // Credentials themselves never reach the trace.
        sce::trace::log("http", "oauth2 notam : identifiants absents");
        return false;
    }
    WiFiClientSecure tls;
    tls.setInsecure();
    HTTPClient http;
    if (!http.begin(tls, "https://api.autorouter.aero/v1.0/oauth2/token")) {
        sce::trace::log("http", "oauth2 notam : begin KO");
        notamHttp = HTTP_BEGIN_KO;
        return false;
    }
    http.setTimeout(12000);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    String body = String("grant_type=client_credentials&client_id=") +
                  urlEncode(user) + "&client_secret=" + urlEncode(pass);
    // This POST is the scarce operation the whole token cache protects (20
    // live tokens per week): every attempt deserves a line, forced or not.
    sce::trace::log("http", "oauth2 notam : demande de jeton%s",
                    force ? " (renouvellement force)" : "");
    const uint32_t t0 = millis();
    int code = http.POST(body);
    sce::trace::log("http", "oauth2 notam -> %d en %lu ms", code,
                    (unsigned long)(millis() - t0));
    if (code != 200) {
        // A 403 has TWO causes calling for OPPOSITE actions, so the code
        // alone must not decide what the screen says: "privileges" is a
        // support ticket, "toomanytokens" is nothing to ask anyone - the
        // account is fine and merely holds 20 live tokens. Sending someone
        // to support for the second costs a wasted week (user 08-01).
        if (code == 403) {
            String b = http.getString();
            if (b.indexOf("toomanytokens") >= 0) code = HTTP_TOOMANYTOK;
        }
        http.end();
        notamHttp = code;
        return false;
    }
    JsonDocument doc(&sce::psAlloc);
    sce::psAlloc.reset();
    // Same chunked trap as fetchJson: this host does not send Content-Length.
    DeserializationError err;
    if (http.getSize() >= 0) {
        err = deserializeJson(doc, http.getStream());
        http.end();
    } else {
        sce::PsSink sink;
        http.writeToStream(&sink);
        http.end();
        err = deserializeJson(doc, sink.data());
    }
    if (err) { notamHttp = HTTP_PARSE_KO; return false; }
    strlcpy(notamTok, doc["access_token"] | "", sizeof(notamTok));
    const uint32_t ttl = (uint32_t)(doc["expires_in"] | 3600);
    // An HOUR of margin, not a minute: the point is to survive reboots, and
    // a token that dies between the check and the request costs one more
    // out of the twenty.
    const time_t nowUtc = time(nullptr);
    const uint32_t life = (ttl > 7200 ? ttl - 3600 : ttl / 2);
    if (sce::clockSynced(nowUtc)) {
        notamTokExp = (uint32_t)nowUtc + life;
    } else {
        // MINTED BEFORE NTP. The absolute expiry cannot be computed yet, and
        // the old code answered that by leaving it at 0 — which meant the token
        // was never written to the card. That is the WORST case to drop, not
        // the safest: the account allows twenty tokens a week, and a bin that
        // mints before its clock arrives is a bin that just rebooted, i.e.
        // exactly the loop this cache exists to survive. The token is kept and
        // its remaining life is measured on the MONOTONIC clock instead; the
        // absolute stamp is written the moment NTP lands (`notamTokStamp`).
        notamTokExp     = 0;
        notamTokPendMs  = millis();
        notamTokPendTtl = life;
    }
    if (minted) *minted = true;
    // One weekly token just got spent: say so, with the lifetime the server
    // announced - a short ttl here explains a re-mint sooner than expected.
    sce::trace::log("http", "oauth2 notam : jeton obtenu (ttl=%lus, exp %s)",
                    (unsigned long)ttl, notamTokExp ? "datee" : "en attente NTP");
    if (notamTok[0] && notamTokExp) notamTokDirty = true;   // loop() writes
    return notamTok[0] != '\0';
}

static void notamFree() {
    if (!notams) return;
    for (int i = 0; i < NOTAM_MAX; i++) {
        if (notams[i].text) { free(notams[i].text); notams[i].text = nullptr; }
    }
}

static bool fetchNotam() {
    char id[8], fir[8];
    xSemaphoreTake(gMtx, portMAX_DELAY);
    metarStation(id, sizeof(id));           // SAME station as the METAR/TAF
    strlcpy(fir, cfg.notamFir, sizeof(fir));   // snapshot: settingSet rewrites it
    xSemaphoreGive(gMtx);
    if (strlen(id) != 4) { notamHttp = HTTP_NO_STATION; return false; }
    if (strlen(fir) != 4) fir[0] = '\0';    // anything else is not an ICAO code
    if (!cfg.notamUser[0] || !cfg.notamPass[0]) {
        notamHttp = HTTP_NO_KEY;
        return false;
    }
    // The cache BELONGS to a station. Without this, changing metar_icao left
    // the previous aerodrome's NOTAMs under the new one's header for up to 30
    // minutes — the "looks informed and is guessing" failure this view exists
    // to avoid, committed by the view itself (review 08-01).
    char want[16];
    snprintf(want, sizeof(want), "%s%s%s", id, fir[0] ? "/" : "", fir);
    if (strcmp(notamFor, want) != 0) {
        xSemaphoreTake(gMtx, portMAX_DELAY);
        notamFree();
        notamCount = 0; notamInForce = 0; notamIdx = 0;
        notamFor[0] = '\0';
        xSemaphoreGive(gMtx);
        notamOkMs = 0;
    }
    bool minted = false;
    if (!notamToken(false, &minted)) return false;
    if (!notams) {
        // ps_malloc and NOT sce::psAlloc: this block is permanent and is not
        // JSON. The ArduinoJson allocator counts its internal-heap fallbacks
        // against a budget that `reset()` wipes before every parse, so a
        // permanent block taken through it would be charged once and then
        // forgotten — rule 18's accounting would stop seeing it (review 08-01).
        notams = (NotamItem*)ps_malloc(sizeof(NotamItem) * NOTAM_MAX);
        if (!notams) { notamHttp = HTTP_PARSE_KO; return false; }
        memset(notams, 0, sizeof(NotamItem) * NOTAM_MAX);
    }
    // Filter: the full row carries 34 fields including four Garmin-format
    // bounding boxes we have no use for. 27 rows of that did not belong in
    // memory, filtered or not (rule 18: the document lives in PSRAM).
    JsonDocument filter;
    JsonObject f = filter["rows"][0].to<JsonObject>();
    f["series"] = f["number"] = f["year"] = true;
    f["code23"] = f["code45"] = f["purpose"] = true;
    f["startvalidity"] = f["endvalidity"] = f["itemd"] = f["iteme"] = true;
    // F and G: the VERTICAL BAND the notice applies to - lower and upper
    // limit. Never requested until 08-03, so they were nowhere on screen
    // (user: "where will the F and G fields be?"). They are the shortest
    // fields a NOTAM carries and among the most decisive: "SFC/999" is a
    // different object from "FL195/FL245", and item E rarely repeats them.
    f["itemf"] = f["itemg"] = true;
    // ...and the REST OF THE Q LINE. The captured response carries every
    // sub-field separately - `fir`, `traffic`, `scope`, `lower`, `upper`,
    // `radius`, `itema` - so the line can be REBUILT exactly as it is
    // printed, instead of showing its 4-letter code and dropping seven
    // eighths of it (user 08-03: "Q looks truncated", and it was).
    f["fir"] = f["traffic"] = f["scope"] = true;
    f["lower"] = f["upper"] = f["radius"] = f["itema"] = true;
    JsonDocument doc(&sce::psAlloc);
    // itemas is a JSON ARRAY: %5B %22 = [ " and %22%5D = " ]. Adding the FIR
    // costs NOTHING here — same request, same TLS session, same token.
    String url = String("https://api.autorouter.aero/v1.0/notam?itemas=%5B%22") + id;
    if (fir[0]) url += "%22,%22" + String(fir);
    url += "%22%5D&offset=0&limit=100";
    int code = fetchJson(url, doc, 15000, &filter,
                         "Authorization", (String("Bearer ") + notamTok).c_str());
    // REFUSED WITH A CACHED TOKEN -> re-authenticate and retry ONCE, here and
    // now (user 08-02). Clearing the token for the NEXT pass, as this did,
    // meant the 60 s failure spacing stood between the user and a working
    // screen — a full minute of "NOTAM service unavailable" for a token that
    // simply needed renewing.
    // ONE retry, and only when the token came from the CACHE: if `notamToken`
    // has just minted one and the server still refuses, minting again would
    // burn the twenty-token allowance on a request that is not going to work.
    if ((code == 401 || code == 403) && !minted) {
        Serial.printf("[radar] notam %d avec le jeton en cache -> renouvellement\n",
                      code);
        notamTok[0] = '\0';
        notamTokExp = 0; notamTokPendTtl = 0;
        bool second = false;
        if (notamToken(true, &second)) {
            // NO direct notamTokSave() here: notamToken() has already raised
            // notamTokDirty, and loop() is the file's ONLY writer — the very
            // rule the notamTokDirty comment states. The direct call this
            // replaced reopened the two-task truncate race, and on the
            // pre-NTP path it persisted notamTokExp=0, a file notamTokLoad()
            // discards — defeating the monotonic-clock stamping loop() does
            // before ITS save.
            code = fetchJson(url, doc, 15000, &filter,
                             "Authorization", (String("Bearer ") + notamTok).c_str());
            if (code == 401 || code == 403) {
                // Second refusal with a BRAND NEW token: this is not staleness.
                // Stop, and let the screen name it (privileges vs toomanytokens).
                Serial.printf("[radar] notam %d avec un jeton NEUF : ce n'est pas "
                              "l'age du jeton, on arrete\n", code);
                notamTok[0] = '\0';
                notamTokExp = 0; notamTokPendTtl = 0;
            }
        }
    }
    notamHttp = code;
    if (code != 200) return false;

    const time_t nowT = time(nullptr);
    const bool   clockOk = sce::clockSynced(nowT);
    // Collected then SORTED: the API returns them in its own order, which is
    // not an order of importance.
    // KEPT SORTED AS WE GO, so the cap falls on the LOWEST-ranked item and
    // not on whatever arrived last. Truncating in wire order (the first
    // version) discarded an NBO returned at position 20 in favour of an M at
    // position 3 — the exact inversion the ranking exists to prevent, and
    // invisible because the screen would still have read "14/14" (review
    // 08-01). `inForce` counts them ALL, so the screen can say it is showing
    // a subset.
    NotamItem tmp[NOTAM_MAX];
    memset(tmp, 0, sizeof(tmp));
    int n = 0, inForce = 0;
    for (JsonObject o : doc["rows"].as<JsonArray>()) {
        const uint32_t from = (uint32_t)(o["startvalidity"] | 0);
        const uint32_t to   = (uint32_t)(o["endvalidity"] | 0);
        if (clockOk && (from > (uint32_t)nowT || to < (uint32_t)nowT)) continue;
        // CHECKLIST NOTAMs are never read, they are RECONCILED. Q-code KKKK,
        // and their E field is a bare list of numbers:
        //   "CHECKLIST YEAR=2026 0103 0104 0106 0107 0112 ..."
        // They exist so an operator can verify their own holdings are complete.
        // Dropped OUTRIGHT, not left to the briefing filter: they only stayed
        // hidden because purpose "K" fell into the same bucket as "M", so
        // turning the filter off put two cards of raw numbers into the deck
        // (measured on FMMM, 08-01). Not counted as in force either — they say
        // nothing about the aerodrome.
        {
            const char* c23 = o["code23"] | "";
            const char* c45 = o["code45"] | "";
            if (!strcmp(c23, "KK") && !strcmp(c45, "KK")) continue;
        }
        // COUNTED HERE, before the briefing filter (fix 08-01). It used to be
        // incremented after it, which made `inForce` a count of SURVIVORS
        // while its own comment claimed it counted them all — so the "x/y of
        // TOTAL" suffix only ever appeared when the 14-cap bit, and never when
        // the briefing filter hid something. The screen could say "1/6" while
        // silently dropping seven, which is precisely how a reader ends up
        // asking whether six is really all there is (user 08-01).
        inForce++;
        const uint8_t prio = notamPrio(o["purpose"] | "");
        // Filtered HERE and not at draw time: the cap keeps the best 14, and
        // keeping fourteen M-rank items only to hide them would push the
        // briefing-worthy ones out of the deck entirely.
        if (cfg.notamBrief && prio >= 3) continue;
        int slot = n;
        if (n >= NOTAM_MAX) {
            // Full: the incoming one only earns a place by beating the worst
            // one held, which — the array being sorted — is the last.
            NotamItem& worst = tmp[NOTAM_MAX - 1];
            const bool better = (prio < worst.prio) ||
                                (prio == worst.prio && from > worst.from);
            if (!better) continue;
            if (worst.text) { free(worst.text); worst.text = nullptr; }
            slot = NOTAM_MAX - 1;
        } else {
            n++;
        }
        NotamItem& it = tmp[slot];
        snprintf(it.id, sizeof(it.id), "%s%04d/%02d",
                 (const char*)(o["series"] | "?"), (int)(o["number"] | 0),
                 (int)(o["year"] | 0));
        snprintf(it.q, sizeof(it.q), "%s%s", (const char*)(o["code23"] | "??"),
                 (const char*)(o["code45"] | "??"));
        strlcpy(it.sched, o["itemd"] | "", sizeof(it.sched));
        strlcpy(it.lo,    o["itemf"] | "", sizeof(it.lo));
        strlcpy(it.hi,    o["itemg"] | "", sizeof(it.hi));
        // ITEM A is an ARRAY in this feed (["FMEE"]) - a notice can name
        // several aerodromes. The first is taken and the rest is not invented:
        // the deck is queried per station anyway.
        {
            JsonArrayConst aa = o["itema"];
            const char* a0 = (!aa.isNull() && aa.size()) ? (aa[0] | "") : "";
            strlcpy(it.itema, a0, sizeof(it.itema));
        }
        // THE Q LINE, REBUILT IN THE ORDER IT IS PRINTED. Every piece comes
        // from its own field, nothing is derived, and the separators are the
        // form's own. `purpose` and `scope` arrive space-padded ("NBO ",
        // "AW ") - trimmed here, since a trailing space inside a slash-joined
        // line reads as a missing value.
        {
            char pu[8], sc[8];
            strlcpy(pu, o["purpose"] | "", sizeof(pu));
            strlcpy(sc, o["scope"]   | "", sizeof(sc));
            for (char* c = pu + strlen(pu); c > pu && c[-1] == ' '; c--) c[-1] = 0;
            for (char* c = sc + strlen(sc); c > sc && c[-1] == ' '; c--) c[-1] = 0;
            snprintf(it.qline, sizeof(it.qline), "%s/Q%s%s/%s/%s/%s/%03d/%03d/%dNM",
                     (const char*)(o["fir"] | ""),
                     (const char*)(o["code23"] | "??"),
                     (const char*)(o["code45"] | "??"),
                     (const char*)(o["traffic"] | ""), pu, sc,
                     (int)(o["lower"] | 0), (int)(o["upper"] | 999),
                     (int)(o["radius"] | 0));
        }
        it.from = from; it.to = to;
        it.prio = prio;
        const char* e = o["iteme"] | "";
        size_t len = strlen(e);
        it.cut = (len > (size_t)NOTAM_TXT - 1);
        it.text = (char*)ps_malloc(len < (size_t)NOTAM_TXT ? len + 1 : NOTAM_TXT);
        if (it.text) {
            strlcpy(it.text, e, len < (size_t)NOTAM_TXT ? len + 1 : NOTAM_TXT);
            // THE NEWLINES STAY (user 08-03: "for E, keep the line breaks and
            // the formatting too"). They used to be flattened to spaces on the
            // grounds that the wire wraps at ITS width and not ours - true of a
            // paragraph that merely ran long, and false of everything else item
            // E carries: coordinate lists, runway tables, enumerations of
            // frequencies. Flattening those turns a table into a sentence, and
            // a sentence is exactly what they are not.
            // `\r` alone still becomes `\n` - CRLF must not count twice - and
            // the wrapper below now breaks HARD on a newline and soft on spaces,
            // so a long line still wraps and a short one still ends where its
            // author ended it.
            // CRLF collapses PAIRWISE — "\r\n" counts once — while a lone \r
            // still becomes \n. NOT "no two newlines in a row" (review 08-04):
            // that erased deliberate BLANK rows, the very block separator the
            // wrapper below preserves on purpose — "\r\n\r\n" is one blank
            // line, not zero, and eating it glued the two blocks together.
            char* w = it.text;
            for (const char* r = it.text; *r; r++) {
                if (*r == '\r') { *w++ = '\n'; if (r[1] == '\n') r++; }
                else            *w++ = *r;
            }
            *w = '\0';
        }
        // Bubble the new entry up to its rank: rank ascending, then most
        // recently started first. n <= 14, so this is cheaper than it looks
        // and it keeps the array sorted for the cap test above.
        for (int j = slot; j > 0; j--) {
            if (tmp[j - 1].prio < tmp[j].prio ||
                (tmp[j - 1].prio == tmp[j].prio && tmp[j - 1].from >= tmp[j].from))
                break;
            NotamItem sw = tmp[j - 1]; tmp[j - 1] = tmp[j]; tmp[j] = sw;
        }
    }
    xSemaphoreTake(gMtx, portMAX_DELAY);
    notamFree();
    memcpy(notams, tmp, sizeof(tmp));
    notamCount = n;
    notamInForce = inForce;
    strlcpy(notamFor, want, sizeof(notamFor)); // the cache belongs to the QUERY
    if (notamIdx >= n) notamIdx = 0;
    notamPage = 0;
    xSemaphoreGive(gMtx);
    notamOkMs = millis() ? millis() : 1;
    uiDirty = true;
    // THE ONLY FETCH THAT SAID NOTHING (08-01). The METAR logs its reading and
    // the poll logs its aircraft; the NOTAM deck published a count on screen
    // and nothing on the wire, so "why does it say 6 when the API returns 27?"
    // could not be answered without reflashing. Every number the filters use,
    // in order, so the drop is attributable at a glance.
    Serial.printf("[radar] notam %s: %d recus, %d en vigueur, %d gardes "
                  "(brief:%d clock:%d)\n",
                  id, (int)doc["rows"].as<JsonArray>().size(), inForce, n,
                  (int)cfg.notamBrief, clockOk ? 1 : 0);
    return true;
}

// ================= STATION DATA (frequencies, field elevation) =============
// aviationweather.gov publishes a SECOND endpoint next to the METAR one:
//   GET /api/data/airport?ids=FMEE&format=json
//     -> "freqs": "ATIS,126.8;TWR,118.4", "elev": 20, ...
// Free, no key, same host as the METAR. Verified live on 2026-07-31.
//
// Fetched ONCE PER STATION and then cached: an aerodrome's tower frequency
// does not change between two weather refreshes, and spending a TLS session
// every ten minutes to re-learn 118.4 would be the kind of traffic this bin
// tiles its radar requests to avoid.
//
// WHAT IS NOT TAKEN FROM IT, deliberately: the `runways[].alignment` field of
// the same response is the MAGNETIC bearing (FMEE 12/30 reads 121 there, where
// the true bearing is 102). Using it would put the runway bar 19 degrees askew
// — the exact error runways.csv exists to prevent. Frequencies and elevation
// only.
struct StationInfo {
    char atis[10] = "";
    char twr[10]  = "";
    int  elevFt   = -9999;      // -9999 = unknown, 0 is a LEGAL elevation
};
static StationInfo stnInfo;
static char stnInfoFor[8] = "";  // the station it belongs to

// "ATIS,126.8;TWR,118.4" — semicolon-separated pairs, the name before the
// comma. Only the two a pilot tunes on the ground are kept; the rest of the
// list (approach, ground, radar) does not fit a 60 px column and is not what
// this card is for.
static void stnParseFreqs(const char* src, StationInfo& out) {
    out.atis[0] = out.twr[0] = '\0';
    if (!src) return;
    const char* p = src;
    while (*p) {
        const char* comma = strchr(p, ',');
        const char* semi  = strchr(p, ';');
        if (!comma || (semi && comma > semi)) break;      // malformed: stop
        const size_t nlen = (size_t)(comma - p);
        const char* val = comma + 1;
        const size_t vlen = semi ? (size_t)(semi - val) : strlen(val);
        char* dst = nullptr;
        if      (nlen == 4 && !strncmp(p, "ATIS", 4)) dst = out.atis;
        else if (nlen == 3 && !strncmp(p, "TWR",  3)) dst = out.twr;
        if (dst && vlen < 10) { memcpy(dst, val, vlen); dst[vlen] = '\0'; }
        if (!semi) break;
        p = semi + 1;
    }
}

// ---- FLIGHT-CATEGORY WATCH (08-01). State for "the weather got worse since
// the last observation", raised by fetchMetar on netTask and shown by
// viewHoldBanner on loop() — which every screen already calls, radar included,
// so the warning reaches you on the screen you are actually looking at.
// `fltCatRank` is defined further down with the colour code it belongs to;
// declared here rather than duplicated (it is the same 0..3 ranking).
static int fltCatRank(const char* cat);
static const char* const FLT_CATS[4] = { "VFR", "MVFR", "IFR", "LIFR" };
static char wxCatFor[8] = "";        // station wxCatSev belongs to
static int  wxCatSev    = -1;        // last rank seen, -1 = none published
// Read by loop(), written by netTask. ONE byte: ((old+1) << 4) | (new+1),
// 0 = nothing to say. A byte cannot be read half-updated; a char[] can.
static volatile uint8_t  wxAlertPair = 0;
static volatile uint32_t wxAlertMs   = 0;
// FIVE MINUTES. The chirp is what catches you; this line is the explanation
// for whoever looks within a reasonable delay, and the METAR refreshes every
// ten, so it fades well before the observation it describes is superseded.
static constexpr uint32_t WX_SHOW_MS = 300000;

static void fetchStationInfo(const char* id) {
    if (!strcmp(stnInfoFor, id)) return;          // already known
    // DROPPED BEFORE THE REQUEST, not after it succeeds. Only updating on 200
    // meant that changing metar_icao from FMEE to LFPG, with the airport
    // endpoint then timing out for LFPG, left FMEE's tower frequency printed
    // under LFPG's header — a card that looks informed and is guessing, which
    // is exactly what fetchNotam invalidates its own station cache to avoid.
    // Better an empty frequency row than a confident wrong one (review 08-01).
    xSemaphoreTake(gMtx, portMAX_DELAY);
    stnInfo = StationInfo();
    stnInfoFor[0] = '\0';
    xSemaphoreGive(gMtx);
    uiDirty = true;
    // THE AERODROME CACHE FIRST. A tower frequency and a field elevation are as
    // permanent as a runway, and this was one TLS session per boot and one per
    // return to a station already visited — spent to re-learn 118.4. The cache
    // is shared with the position lookups: same key, same file, one truth about
    // an aerodrome.
    if (const ApEntry* c = apFind(id)) {
        if (c->hasStn) {
            // A cache hit explains the ABSENCE of an HTTP line for this
            // station in the same capture.
            sce::trace::log("http", "info station %s : cache", id);
            StationInfo hit;
            strlcpy(hit.atis, c->atis, sizeof(hit.atis));
            strlcpy(hit.twr,  c->twr,  sizeof(hit.twr));
            hit.elevFt = c->elevFt;
            xSemaphoreTake(gMtx, portMAX_DELAY);
            stnInfo = hit;
            strlcpy(stnInfoFor, id, sizeof(stnInfoFor));
            xSemaphoreGive(gMtx);
            uiDirty = true;
            return;
        }
    }
    JsonDocument filter;
    JsonObject f = filter[0].to<JsonObject>();
    f["freqs"] = f["elev"] = true;
    JsonDocument doc(&sce::psAlloc);
    const int code = fetchJson(String("https://aviationweather.gov/api/data/"
                                      "airport?ids=") + id + "&format=json",
                               doc, 8000, &filter);
    if (code != 200) {
        // Silent on screen because it is a bonus; not silent in the trace,
        // because "no tower frequency" has to be attributable to something.
        sce::trace::log("http", "info station %s : http=%d, sans suite",
                        id, code);
        return;
    }
    JsonObject o = doc[0];
    if (o.isNull()) return;
    StationInfo tmp;
    stnParseFreqs(o["freqs"] | "", tmp);
    if (o["elev"].is<int>() || o["elev"].is<float>())
        tmp.elevFt = (int)lroundf((float)(o["elev"] | 0.0f) * 3.28084f);
    xSemaphoreTake(gMtx, portMAX_DELAY);
    stnInfo = tmp;
    strlcpy(stnInfoFor, id, sizeof(stnInfoFor));
    xSemaphoreGive(gMtx);
    // Remembered ONLY when the answer carried something: an aerodrome the
    // endpoint knows but publishes no frequency for would otherwise be cached
    // as "asked and empty" for ever, and never asked again on a day the record
    // is filled in.
    if (tmp.atis[0] || tmp.twr[0] || tmp.elevFt != -9999)
        apStoreStn(id, tmp.atis, tmp.twr, tmp.elevFt);
    uiDirty = true;
}

// One observation from aviationweather.gov (free, NO key, no quota published).
// The answer is an ARRAY of objects — 1 element for one station.
static bool fetchMetar() {
    char id[8];
    xSemaphoreTake(gMtx, portMAX_DELAY);
    metarStation(id, sizeof(id));
    xSemaphoreGive(gMtx);
    if (strlen(id) != 4) { metarHttp = HTTP_NO_STATION; return false; }
    // Filter: `filter[0]` describes EVERY element of the array (ArduinoJson
    // convention). The full record carries ~40 fields, several of them arrays.
    JsonDocument filter;
    JsonObject f = filter[0].to<JsonObject>();
    f["icaoId"] = f["name"] = f["obsTime"] = f["rawOb"] = true;
    f["rawTaf"] = true;                     // forecast, same object (taf=1)
    f["temp"] = f["dewp"] = f["wdir"] = f["wspd"] = true;
    f["visib"] = f["altim"] = f["cover"] = f["fltCat"] = true;
    f["clouds"] = true;                     // layers: cover + base (feet)
    JsonDocument doc(&sce::psAlloc);         // rule 18: PSRAM, not the heap
    // `taf=1` adds the FORECAST to the very same object — one connection for
    // both views (observation and forecast). The station is the same, the TLS
    // session is the same; asking separately would have doubled the cost of a
    // screen the user reaches by one more swipe.
    int code = fetchJson(String("https://aviationweather.gov/api/data/metar?ids=")
                         + id + "&format=json&taf=1", doc, 8000, &filter);
    metarHttp = code;
    // FAILURES ARE LOGGED TOO. Only the success path printed, so a METAR that
    // never arrived produced no serial output whatsoever — indistinguishable
    // from a fetch that was never attempted, which sent me looking at the
    // wrong gate for a quarter of an hour (08-02). The screen named the
    // reason, but the screen is not where you debug a board on a bench.
    if (code != 200) {
        Serial.printf("[radar] metar %s ECHEC http=%d\n", id, code);
        return false;
    }
    JsonObject o = doc[0];
    // An UNKNOWN station answers 200 with an empty array: without this test
    // the view would keep the previous station's data under the new name.
    if (o.isNull()) {
        Serial.printf("[radar] metar %s : station inconnue (reponse vide)\n", id);
        metarHttp = HTTP_PARSE_KO;
        return false;
    }

    Metar m;
    strlcpy(m.icao, o["icaoId"] | id, sizeof(m.icao));
    strlcpy(m.name, o["name"]   | "", sizeof(m.name));
    strlcpy(m.raw,  o["rawOb"]  | "", sizeof(m.raw));
    // strlcpy returns the length of the SOURCE, which is the only chance to
    // notice the cut: after the copy the buffer looks like a complete forecast
    // that simply ends. The NOTAM deck got this flag first, for the same reason.
    m.tafCut = strlcpy(m.taf, o["rawTaf"] | "", sizeof(m.taf)) >= sizeof(m.taf);
    strlcpy(m.cover, o["cover"] | "", sizeof(m.cover));
    strlcpy(m.cat,  o["fltCat"] | "", sizeof(m.cat));
    // The 6x8 font renders UTF-8 as "??" (see cityClean): station names carry
    // accents ("Saint-Denis", "Zurich/Kloten").
    fr::cityClean(m.name, sizeof(m.name) - 1);
    fr::cityClean(m.raw,  sizeof(m.raw) - 1);
    fr::cityClean(m.taf,  sizeof(m.taf) - 1);
    // Numeric fields: read the PRESENCE, not just the value — `| 0.0f` turns
    // an absent temperature into a plausible 0 C (see Metar::hasTemp).
    m.hasTemp  = o["temp"].is<int>()  || o["temp"].is<float>();
    m.hasDewp  = o["dewp"].is<int>()  || o["dewp"].is<float>();
    m.hasAltim = o["altim"].is<int>() || o["altim"].is<float>();
    m.temp  = o["temp"]  | 0.0f;
    m.dewp  = o["dewp"]  | 0.0f;
    m.altim = o["altim"] | 0.0f;
    m.obsTime = (uint32_t)(o["obsTime"] | 0);
    // `wdir` is an INTEGER for a steady wind but the STRING "VRB" when it is
    // variable, and absent when there is no wind information at all. Reading
    // it as a number would have printed a confident "0 deg" — i.e. north —
    // for a variable wind.
    m.wdir = (o["wdir"].is<int>() || o["wdir"].is<float>())
                 ? (int)(o["wdir"] | 0) : -1;
    m.wspd = (o["wspd"].is<int>() || o["wspd"].is<float>())
                 ? (float)(o["wspd"] | 0.0f) : -1.0f;
    // `visib` is a STRING in this API ("6+", "10", "1/2"): copied VERBATIM,
    // the conversion to metres happens at display time and only when the
    // value starts with a number (see metarVisib).
    if (o["visib"].is<const char*>())
        strlcpy(m.visib, o["visib"] | "", sizeof(m.visib));
    else if (!o["visib"].isNull())
        snprintf(m.visib, sizeof(m.visib), "%g", (double)(o["visib"] | 0.0f));
    for (JsonObject c : o["clouds"].as<JsonArray>()) {
        if (m.nLayers >= 2) break;           // 2 layers = the panel's budget
        const char* cv = c["cover"] | "";
        if (!cv[0]) continue;
        strlcpy(m.layerCov[m.nLayers], cv, sizeof(m.layerCov[0]));
        m.layerBaseFt[m.nLayers] =
            (c["base"].is<int>() || c["base"].is<float>())
                ? (int)(c["base"] | 0) : -1;      // FEET (API unit)
        m.nLayers++;
    }
    m.valid = true;
    // ---- FLIGHT CATEGORY WORSENED: the one thing on this card worth
    // interrupting for (08-01). The chip has always shown the CURRENT
    // category, but a VFR -> IFR transition — the most operationally
    // significant event this screen can report — arrived in silence, on a
    // screen nobody is necessarily looking at.
    // Only a WORSENING speaks. An improvement is good news and good news can
    // wait for the next glance; a sound for every change would teach the ear
    // to ignore the sound.
    // Compared BEFORE publishing, against the category of the SAME station:
    // `wxCatFor` invalidates on a metar_icao change exactly like the NOTAM
    // deck and the frequencies do, or the first report from a new aerodrome
    // would be announced as a deterioration of the previous one.
    {
        const int  nsev = fltCatRank(m.cat);          // 0 best .. 3 worst, -1 none
        const bool sameStation = !strcmp(wxCatFor, m.icao);
        const int  osev = sameStation ? wxCatSev : -1;
        // Both must be KNOWN: FMEE regularly omits fltCat, and treating an
        // absent category as "improved to nothing" then announcing its return
        // would fire on the station's own reporting gaps, not on the weather.
        if (osev >= 0 && nsev > osev) {
            // The two ranks travel PACKED IN ONE BYTE, not as a formatted
            // string. netTask writes, loop() reads, and a char[] shared across
            // those two is a torn read waiting to be blamed on the font. One
            // volatile byte cannot tear, and the banner formats it from the
            // same table the chip uses.
            wxAlertPair = (uint8_t)(((osev + 1) << 4) | (nsev + 1));
            wxAlertMs   = millis() ? millis() : 1;    // published LAST
            playSeq(SEQ_WX_DOWN);        // silent unless the "son" option is on
            Serial.printf("[radar] meteo degradee %s > %s\n",
                          FLT_CATS[osev], FLT_CATS[nsev]);
        }
        if (nsev >= 0 || !sameStation) wxCatSev = nsev;
        strlcpy(wxCatFor, m.icao, sizeof(wxCatFor));
    }
    xSemaphoreTake(gMtx, portMAX_DELAY);
    metar = m;
    xSemaphoreGive(gMtx);
    metarOkMs = millis();
    metarSchedule(m.obsTime);       // the report says when to come back
    uiDirty   = true;
    // Frequencies + field elevation, ONCE per station. Piggy-backed on the
    // METAR success so it cannot fire for a station that does not answer, and
    // it is a BONUS: a failure here leaves the card exactly as it was.
    fetchStationInfo(m.icao);
    Serial.printf("[radar] metar %s %dC/%dC vent %d/%.0fkt %s %s\n",
                  m.icao, (int)m.temp, (int)m.dewp, m.wdir, m.wspd,
                  m.visib, m.cat);
    return true;
}

// Full poll: ≤ 250 nm = 1 request; beyond that (max 500) = TILING with the
// centre + 6 hexagonal satellites at (R-250) nm, merged/deduplicated by hex —
// an approximate coverage of the outer lobes, plenty for an ambient radar.
// Requests spaced by 150 ms (politeness towards the API rate limit).
// Centre/radius/source LATCHED under mutex at the start of the cycle: an
// airport recentre or a settings OK during the tiling cannot tear the floats
// apart (new lat + old lon = satellites over the ocean, review 07-26).
static bool pollPlanes() {
    float cla, clo; int R; char api[24];
    xSemaphoreTake(gMtx, portMAX_DELAY);
    cla = cfg.lat; clo = cfg.lon; R = (int)cfg.radiusNm;
    strlcpy(api, cfg.api, sizeof(api));
    xSemaphoreGive(gMtx);
    httpStatus = 0;                                        // status of the cycle
    planesFullCycle = false;               // re-detected by the merges;
                                           // published at the END of a
                                           // successful cycle (a failed cycle
                                           // kept 48 without the "+", max
                                           // review 07-27)
    uint32_t g0 = routeReqGen;               // a route request during the
                                             // tiling → tiles abandoned
    // SafeSky needs NO tiling: past 20 km its `viewport` mode covers the whole
    // box in ONE request (see fetchPoint). Tiling it would mean 7 requests for
    // 7 overlapping boxes — and a quota burnt seven times faster.
    const bool sky = !strcmp(api, "safesky");
    // THE PREFERRED SOURCE FIRST, then the others until one speaks.
    // A source that answers 200 with an EMPTY sky has NOT failed - that is a
    // legitimate answer, and failing over on it would chase every quiet hour
    // through all three servers. Only a transport failure (no HTTP 200) counts,
    // which is what `httpStatus` reports.
    // SafeSky as the PREFERRED source falls back too (review 08-03): the guard
    // used to be `!sky`, which protected SafeSky users from the exact outage
    // this mechanism exists to survive. What stays excluded is anything that
    // means "your SETTING is wrong, not the service": a MISSING key has its
    // own on-screen diagnosis, and a 401/403 means the key itself is bad -
    // papering either over with another feed would show a working radar and
    // hide the misconfiguration indefinitely (review 08-04).
    // WHICH SOURCE SERVES THIS CYCLE is decided here, ONCE, and honoured by
    // EVERY request in the cycle (review 08-04): the satellite tiles and the
    // worldwide-track request kept hammering the dead preferred server after
    // the centre fetch had already fallen back - up to 7 extra timeouts per
    // cycle stalling the net task, an outer ring that silently emptied, and a
    // banner claiming the fallback was serving.
    const char* serving = api;
    // One line PER SOURCE TRIED, on top of fetchJson's per-request line: the
    // fallback decision below reads httpStatus, so the capture must show the
    // value the decision actually saw, next to the source it condemns.
    uint32_t tSrc = millis();
    bool any = fetchPoint(api, cla, clo, (sky || R <= 250) ? R : 250);
    sce::trace::log("http", "source %s : %s http=%d en %lu ms", api,
                    any ? "repond" : "muette", httpStatus,
                    (unsigned long)(millis() - tSrc));
    if (!any && httpStatus != 200 && httpStatus != HTTP_NO_KEY &&
        !(sky && (httpStatus == 401 || httpStatus == 403))) {
        for (int i = 0; i < API_ALT_N && !any; i++) {
            if (!strcmp(API_ALT[i], api)) continue;      // that is the one that failed
            httpStatus = 0;
            tSrc = millis();
            any = fetchPoint(API_ALT[i], cla, clo, R <= 250 ? R : 250);
            sce::trace::log("http", "source %s (repli) : %s http=%d en %lu ms",
                            API_ALT[i], any ? "repond" : "muette", httpStatus,
                            (unsigned long)(millis() - tSrc));
            if (any) {
                serving = API_ALT[i];
                strlcpy(apiServing, API_ALT[i], sizeof(apiServing));
                if (!apiFellBackMs) apiFellBackMs = millis() ? millis() : 1;
                Serial.printf("[radar] source %s muette -> repli sur %s\n",
                              api, API_ALT[i]);
            }
        }
        // Every source silent is not a fallback, it is an outage: leave
        // apiServing alone so the screen keeps naming the last one that spoke.
    } else if (any) {
        // The preferred source answered: the substitution is over, and it must
        // END LOUDLY TOO - a warning that never clears teaches people to ignore
        // warnings.
        if (apiFellBackMs) Serial.printf("[radar] source %s de retour\n", api);
        apiFellBackMs = 0;
        strlcpy(apiServing, api, sizeof(apiServing));
    }
    // Tiling keys on the source that ANSWERED, not the configured one: after
    // a SafeSky→v2 fallback the v2 source needs its tiles to cover past
    // 250 nm (SafeSky's viewport did that in one request; a v2 feed cannot),
    // and after a v2→v2 fallback the tiles must aim at the living server.
    const bool skyServing = !strcmp(serving, "safesky");
    if (R > 250 && !skyServing) {
        float d = (float)(R - 250);                        // satellite distance
        for (int k = 0; k < 6; k++) {
            if (netGuard.stopping()) { pollNow = true; break; }   // reflash: give up AND
                                             // reschedule the cycle
            if (airportState == 1 || routeReqGen != g0) {
                pollNow = true;              // recentre/route requested:
                break;                       // cut short AND RESCHEDULE
            }                                // (without this the outer tiles
                                             // waited up to 60 s, max review
                                             // 07-27)
            vTaskDelay(pdMS_TO_TICKS(150));
            float b = k * 60.0f * DEG_TO_RAD;              // bearing 0,60,...,300
            float slat = cla + (d * cosf(b)) / NM_PER_DEG_LAT;
            float slon = clo + (d * sinf(b)) /
                         (NM_PER_DEG_LAT * cosf(cla * DEG_TO_RAD));
            any |= fetchPoint(serving, slat, slon, 250);
        }
    }
    if (httpStatus == 0) httpStatus = 200;
    if (!any) return false;

    uint32_t now = millis();
    xSemaphoreTake(gMtx, portMAX_DELAY);
    // Purge of aircraft unseen for 90 s (compacts the array). The SELECTED
    // flight survives (user 07-27: swiping onto a stale blip started the
    // route lookup then FELL BACK to the RADAR summary at purge time — the
    // panel shows "seen Xs", so freshness stays honest).
    int w = 0;
    for (int i = 0; i < planeCount; i++)
        if (now - planes[i].seenMs < 90000 ||
            !strcmp(planes[i].hex, selectedHex)) {
            if (w != i) planes[w] = planes[i];
            w++;
        }
    planeCount = w;
    lastPollOkMs = now;
    // END OF LIFE of a track: the aircraft has stopped transmitting for
    // 10 min (transponder switched off after landing, typically). The tracked
    // flight is EXEMPT from every purge — by design — but without this exit
    // the panel stayed frozen FOREVER on dead data and the summary became
    // unreachable (user 07-27). Generous threshold: an ordinary coverage gap
    // (a few minutes) triggers nothing.
    if (selectedHex[0]) {
        for (int i = 0; i < planeCount; i++) {
            if (strcmp(planes[i].hex, selectedHex)) continue;
            if (now - planes[i].seenMs > 600000) {
                Serial.printf("[radar] traque %s : 10 min sans signal -> liberee\n",
                              planes[i].flight[0] ? planes[i].flight
                                                  : planes[i].hex);
                selectedHex[0] = '\0';
                trackQuery[0]  = '\0';   // else a futile worldwide re-lock
                routeFlight[0] = '\0';
                clearRouteLocked();
                routeStatus = 0;
                routeReqGen = routeReqGen + 1;
            }
            break;
        }
    }
    // Track: as soon as the wanted callsign (or a fragment of it) shows up it
    // is targeted automatically — same path as the tap (selectPlaneLocked).
    acquireTrackedLocked();
    // AUTOMATIC follow of the closest aircraft (option): only steps in when
    // nothing is tracked AND no query is armed — a manual choice is never
    // overwritten.
    if (cfg.follow && !selectedHex[0] && !trackQuery[0]) {
        int best = -1; float bd = 1e9f;
        for (int i = 0; i < planeCount; i++) {
            if (!cfg.showGround && planes[i].onGround) continue;
            float d = distNm(planes[i]);
            if (d < bd) { bd = d; best = i; }
        }
        if (best >= 0) { selectPlaneLocked(best, false); playSeq(SEQ_SEL); }
    }
    uiDirty = true;
    // Published only on a completed cycle AND if the table is STILL full
    // after the purge — otherwise the counter showed "39+" (max review
    // 07-27).
    planesFull = planesFullCycle && (planeCount >= MAX_PLANES);
    // WORLDWIDE request: FULL callsign only (≥ 4 characters) — /v2/callsign
    // matches EXACTLY, a fragment ("470") will NEVER match and used to open a
    // futile TLS session on EVERY cycle, forever (max review 07-27). Backoff:
    // 3 cycles of respite after an unsuccessful attempt.
    // SafeSky has no by-callsign endpoint: out-of-range tracking is an ADS-B
    // mirror feature. Asking anyway would cost one 404 per cycle.
    bool needGlobal = !sky && trackQuery[0] && !selectedHex[0] &&
                      strlen(trackQuery) >= 4;
    char tq[10]; strlcpy(tq, trackQuery, sizeof(tq));
    xSemaphoreGive(gMtx);
    static uint8_t globalSkip = 0;
    static char    lastGlobalQ[10] = "";
    // Backoff debt attached to THE query: a NEW track must not inherit the
    // 3 cycles of respite of the previous one (the pollNow forced from the
    // keyboard was being swallowed, adversarial check 07-27).
    if (strcmp(tq, lastGlobalQ)) {
        strlcpy(lastGlobalQ, tq, sizeof(lastGlobalQ));
        globalSkip = 0;
    }
    if (netGuard.stopping()) needGlobal = false;   // reflash imminent
    if (needGlobal && globalSkip) { globalSkip--; needGlobal = false; }

    // OUT-OF-RANGE track (user 07-27): callsign armed but not found locally
    // → WORLDWIDE /v2/callsign request. The flight merges into planes[]
    // (real position/alt/speed, complete panel) and the standard re-lock
    // targets it; draw() clamps its marker to the edge of the circle.
    if (needGlobal) {
        int hs = httpStatus;               // OPTIONAL request: a 404 (flight
        bool got = fetchPoint(serving, 0, 0, 0, tq);   // not airborne) must
        httpStatus = hs;                   // not condemn a successful cycle
        xSemaphoreTake(gMtx, portMAX_DELAY);
        if (got) { acquireTrackedLocked(); uiDirty = true; }
        globalSkip = selectedHex[0] ? 0 : 3;   // locked on → no more respite
        xSemaphoreGive(gMtx);
    }
    return true;
}

// Origin→destination route of the targeted flight (hexdb.io — adsb.lol's
// routeset endpoint now answers an empty 201). One route request plus two
// airport resolutions, run by the NETWORK TASK on a SNAPSHOT (cs, gen): the
// result is only applied if the selection did not change during the
// resolution (a tap while a fetch is in flight is neither lost nor
// misattributed — generation protocol, review 07-26).
// Returns: true = DEFINITIVE result (route found, or hexdb says unknown);
// false = NETWORK failure (timeout/TLS/5xx) → netTask will retry (diag
// 07-27: before that, a single failure froze "?" with no further attempt).
static bool fetchRoute(const char* cs, uint32_t gen, float plat, float plon) {
    if (netGuard.stopping()) return false;   // reflash imminent
    char res[32] = "", orig[8] = "", dest[8] = "";
    char  wp[4][8] = {};                 // route waypoints (multi-leg)
    int   nWp = 0;
    float wLa[4] = {}, wLo[4] = {};      // resolved by lookupAirport
    char  wCity[4][20] = {}, wIata[4][8] = {}, wCc[4][4] = {};
    char oCity[20] = "", dCity[20] = "";
    char oIata[8] = "", dIata[8] = "", oCc[4] = "", dCc[4] = "";
    char airl[20] = "";                  // airline (adsbdb, free)
    float ola = 0, olo = 0, dla = 0, dlo = 0;
    bool ov = false, dv = false;
    // 4-entry cache (ring, ~400 B): the automatic re-lock resolves the SAME
    // flight again after every 90 s purge, AND the panel's ↑/↓ cycling
    // alternates between 2-3 flights — with a single entry, each return cost
    // 3-4 TLS sessions (~6-18 s of netTask blocked, max review 07-27).
    struct RouteCache {
        char  cs[10] = "", res[32] = "";
        char  oCity[20] = "", dCity[20] = "";
        char  oIata[8] = "", dIata[8] = "", oCc[4] = "", dCc[4] = "";
        float ola = 0, olo = 0, dla = 0, dlo = 0;
        bool  ov = false, dv = false;
        char  airl[20] = "";              // airline
        float pLat = 0, pLon = 0;         // position USED to pick the leg
        bool  multi = false;              // route with several legs
        uint32_t ms = 0;                  // timestamp (2 h expiry)
    };
    // The cached destination is the CURRENT leg only: the same callsign
    // departs on another leg a few hours later — without expiry, the morning
    // route stayed on screen (with a wrong ETA and a wrong progress bar)
    // until the next reboot (adversarial check).
    static constexpr uint32_t CACHE_TTL_MS = 2UL * 3600UL * 1000UL;
    static RouteCache cache[4];
    static uint8_t    cacheNext = 0;
    for (const RouteCache& e : cache) {
        if (!e.cs[0] || strcmp(e.cs, cs)) continue;
        // A MULTI-LEG route is resolved FOR ONE POSITION: replaying the entry
        // as-is cancelled the leg correction as soon as the aircraft moved on
        // to the next leg (review 07-27c). Beyond 50 nm from the original
        // position, we resolve again.
        if (e.multi && fr::gcNm(e.pLat, e.pLon, plat, plon) > 50.0f) {
            const_cast<RouteCache&>(e).cs[0] = '\0';
            break;
        }
        if (millis() - e.ms > CACHE_TTL_MS) {   // expired: FREE the slot
            const_cast<RouteCache&>(e).cs[0] = '\0';   // (otherwise the ring
            break;                                     // of 4 decayed to 2)
        }
        xSemaphoreTake(gMtx, portMAX_DELAY);
        if (gen == routeReqGen) {
            strlcpy(selectedRoute, e.res, sizeof(selectedRoute));
            origLat = e.ola; origLon = e.olo; origValid = e.ov;
            destLat = e.dla; destLon = e.dlo; destValid = e.dv;
            strlcpy(origCity, e.oCity, sizeof(origCity));
            strlcpy(destCity, e.dCity, sizeof(destCity));
            strlcpy(origIata, e.oIata, sizeof(origIata));
            strlcpy(destIata, e.dIata, sizeof(destIata));
            strlcpy(origCtry, e.oCc, sizeof(origCtry));
            // destCtry BEFORE the swap (review 08-04): copied after it, a
            // reversed cache hit swapped the PREVIOUS selection's country
            // into origCtry and then overwrote destCtry with the canonical
            // value — both countries wrong on exactly the flights the swap
            // exists to fix.
            strlcpy(destCtry, e.dCc, sizeof(destCtry));
            maybeSwapLegLocked(selectedRoute, origLat, origLon, origValid,
                               destLat, destLon, destValid, origCity, destCity,
                               origIata, destIata, origCtry, destCtry, cs);
            strlcpy(airline,  e.airl, sizeof(airline));
            routeStatus = 2;
            uiDirty = true;
        }
        xSemaphoreGive(gMtx);
        return true;
    }
    int rc;                                // HTTP code (<0 = local failure)
    {
        JsonDocument doc(&sce::psAlloc);
        rc = fetchJson(String("https://hexdb.io/api/v1/route/icao/") + cs,
                       doc, 6000);
        // Named per source: the definitive/transient verdict at the end of
        // this function depends on WHICH database answered what.
        sce::trace::log("http", "route %s : hexdb http=%d", cs, rc);
        if (rc == 200) {
            const char* r = doc["route"] | "";
            if (r[0] && strcmp(r, "unknown")) {
                strlcpy(res, r, sizeof(res));
                // MULTI-LEG route ("LFPG-FIMP-FMEE"): we split ALL the
                // waypoints. The leg actually being flown is picked further
                // down from the aircraft position — always taking the first
                // one gave a wrong origin, a wrong progress and a wrong ETA
                // to an aircraft already on the 2nd leg, with nothing on
                // screen hinting it was wrong (review 07-27).
                const char* p0 = r;
                while (nWp < 4 && *p0) {
                    const char* dash = strchr(p0, '-');
                    size_t len = dash ? (size_t)(dash - p0) : strlen(p0);
                    if (len && len < sizeof(wp[0])) {
                        memcpy(wp[nWp], p0, len); wp[nWp][len] = 0;
                        nWp++;
                    }
                    if (!dash) break;
                    p0 = dash + 1;
                }
            }
        }
    }
    // adsbdb.com fallback when hexdb does not know the flight (DIFFERENT
    // community databases — diag 07-27: hexdb 404 on regional Indian Ocean
    // flights). Bonus: route + cities (municipality) + airport lat/lon in a
    // SINGLE call (no lookupAirport needed afterwards).
    int rc2 = 0;                           // 0 = not attempted
    if (!res[0]) {
        JsonDocument doc(&sce::psAlloc);
        rc2 = fetchJson(String("https://api.adsbdb.com/v0/callsign/") + cs,
                        doc, 6000);
        sce::trace::log("http", "route %s : adsbdb http=%d", cs, rc2);
        if (rc2 == 200) {
            JsonObject fr = doc["response"]["flightroute"];
            const char* oi = fr["origin"]["icao_code"] | "";
            const char* di = fr["destination"]["icao_code"] | "";
            if (oi[0] && di[0]) {
                snprintf(res, sizeof(res), "%s-%s", oi, di);
                strlcpy(orig, oi, sizeof(orig));
                strlcpy(dest, di, sizeof(dest));
                ov = fr["origin"]["latitude"].is<float>();
                dv = fr["destination"]["latitude"].is<float>();
                ola = fr["origin"]["latitude"]  | 0.0f;
                olo = fr["origin"]["longitude"] | 0.0f;
                dla = fr["destination"]["latitude"]  | 0.0f;
                dlo = fr["destination"]["longitude"] | 0.0f;
                strlcpy(airl, fr["airline"]["name"] | "", sizeof(airl));
                strlcpy(oCity, fr["origin"]["municipality"] | "", sizeof(oCity));
                strlcpy(dCity, fr["destination"]["municipality"] | "",
                        sizeof(dCity));
                strlcpy(oIata, fr["origin"]["iata_code"] | "", sizeof(oIata));
                strlcpy(dIata, fr["destination"]["iata_code"] | "", sizeof(dIata));
                strlcpy(oCc, fr["origin"]["country_iso_name"] | "", sizeof(oCc));
                strlcpy(dCc, fr["destination"]["country_iso_name"] | "",
                        sizeof(dCc));
            }
        }
    }
    if (nWp >= 2) {
        // Resolve each waypoint, then pick the leg that BRACKETS the aircraft
        // (fr::pickLeg, covered by test_flightgeo).
        // Waypoint-by-waypoint resolution; the leg is chosen among the SUBSET
        // that actually resolved — a single failure (hexdb 404 on the last
        // waypoint) forced leg 0 and reintroduced exactly the bug that had
        // been fixed (review 07-27c).
        bool okWp[4] = {};
        int  nOk = 0;
        float okLa[4], okLo[4]; int okIdx[4];
        for (int i = 0; i < nWp; i++) {
            okWp[i] = lookupAirport(wp[i], &wLa[i], &wLo[i],
                                    wCity[i], sizeof(wCity[i]),
                                    wIata[i], wCc[i]);
            if (okWp[i]) { okLa[nOk] = wLa[i]; okLo[nOk] = wLo[i];
                           okIdx[nOk] = i; nOk++; }
        }
        // The chosen leg joins TWO RESOLVED waypoints, not necessarily
        // adjacent in wp[]: if an intermediate waypoint did not answer, the
        // old code displayed the next one anyway (unresolved, coords at 0)
        // and contradicted the chosen leg (review 07-27d).
        int a = 0, b = 1;
        if (nOk >= 2) {
            int k = fr::pickLeg(okLa, okLo, nOk, plat, plon);
            a = okIdx[k]; b = okIdx[k + 1];
        }
        strlcpy(orig, wp[a], sizeof(orig));
        strlcpy(dest, wp[b], sizeof(dest));
        ola = wLa[a]; olo = wLo[a]; ov = okWp[a];
        dla = wLa[b]; dlo = wLo[b]; dv = okWp[b];
        strlcpy(oCity, wCity[a], sizeof(oCity));
        strlcpy(dCity, wCity[b], sizeof(dCity));
        strlcpy(oIata, wIata[a], sizeof(oIata));
        strlcpy(dIata, wIata[b], sizeof(dIata));
        strlcpy(oCc,   wCc[a],   sizeof(oCc));
        strlcpy(dCc,   wCc[b],   sizeof(dCc));
        if (nWp > 2)     // label the chosen leg, not the whole route
            snprintf(res, sizeof(res), "%s-%s", orig, dest);
    }
    if (orig[0] && !ov) ov = lookupAirport(orig, &ola, &olo,
                                           oCity, sizeof(oCity), oIata, oCc);
    if (dest[0] && !dv) dv = lookupAirport(dest, &dla, &dlo,
                                           dCity, sizeof(dCity), dIata, dCc);
    // Cities: printable ASCII ONLY (the 6×8 font renders UTF-8 as "??" —
    // "Arnavutkoy" seen on HW 07-27), 14 characters max (panel width 85 px).
    // The COUNTRY lives to the right of the airport code (user 07-27).
    fr::cityClean(oCity, 14);            // pure and tested (test_flightgeo)
    fr::cityClean(dCity, 14);
    fr::cityClean(airl,  14);
    Serial.printf("[radar] route %s -> %s (hexdb=%d adsbdb=%d orig=%d dest=%d "
                  "villes='%s'/'%s')\n",
                  cs, res[0] ? res : "?", rc, rc2, (int)ov, (int)dv,
                  oCity, dCity);
    // DEFINITIVE as soon as a COMPLETE route is in hand — no matter that one
    // source failed (hexdb down + adsbdb answering = result acquired:
    // retrying 5× blocked the poll on the same core for ~1 min for a route
    // ALREADY on screen, max review 07-27). Otherwise: definitive only if
    // every source ATTEMPTED did ANSWER (200, or 4xx = no data); an
    // unresolved airport stays transient (no partial caching, the retry will
    // attempt the lookups again).
    auto answered = [](int c) { return c == 200 || (c >= 400 && c < 500); };
    bool complete   = res[0] && (!orig[0] || ov) && (!dest[0] || dv);
    bool definitive = complete ||
                      (!res[0] && answered(rc) && (rc2 == 0 || answered(rc2)));
    if (res[0] && definitive) {            // complete route → cache it
        RouteCache& e = cache[cacheNext];
        cacheNext = (uint8_t)((cacheNext + 1) % 4);
        strlcpy(e.cs, cs, sizeof(e.cs));
        strlcpy(e.res, res, sizeof(e.res));
        e.ms = millis();
        e.ola = ola; e.olo = olo; e.ov = ov;
        e.dla = dla; e.dlo = dlo; e.dv = dv;
        strlcpy(e.oCity, oCity, sizeof(e.oCity));
        strlcpy(e.dCity, dCity, sizeof(e.dCity));
        strlcpy(e.oIata, oIata, sizeof(e.oIata));
        strlcpy(e.dIata, dIata, sizeof(e.dIata));
        strlcpy(e.oCc, oCc, sizeof(e.oCc));
        strlcpy(e.dCc, dCc, sizeof(e.dCc));
        strlcpy(e.airl, airl, sizeof(e.airl));
        e.pLat = plat; e.pLon = plon; e.multi = (nWp > 2);
    }
    xSemaphoreTake(gMtx, portMAX_DELAY);
    if (gen == routeReqGen) {              // selection unchanged: apply
        strlcpy(selectedRoute, res, sizeof(selectedRoute));
        origLat = ola; origLon = olo; origValid = ov;
        destLat = dla; destLon = dlo; destValid = dv;
        // ⚠ cities/IATA applied HERE too (the 07-27 retry edit had dropped
        // them → cities visible ONLY through the cache, HW bug)
        strlcpy(origCity, oCity, sizeof(origCity));
        strlcpy(destCity, dCity, sizeof(destCity));
        strlcpy(origIata, oIata, sizeof(origIata));
        strlcpy(destIata, dIata, sizeof(destIata));
        strlcpy(origCtry, oCc, sizeof(origCtry));
        strlcpy(destCtry, dCc, sizeof(destCtry));
        strlcpy(airline,  airl, sizeof(airline));
        maybeSwapLegLocked(selectedRoute, origLat, origLon, origValid,
                           destLat, destLon, destValid, origCity, destCity,
                           origIata, destIata, origCtry, destCtry, cs);
        routeStatus = res[0] ? 2 : (definitive ? 3 : 4);
        uiDirty = true;
    }                                      // else: stale result, dropped —
    xSemaphoreGive(gMtx);                  // the current generation follows
    return definitive;
}

// Network task (core 0, prio 1): the ONLY owner of the TLS sockets.
// Handles, in order: airport recentre, route resolutions (by generation),
// periodic/forced poll.
static void netTask(void*) {
    uint32_t routeDoneGen = 0;
    uint32_t routeTryGen  = 0;   // generation of the attempts in progress
    uint8_t  routeTries   = 0;   // consecutive network failures (max 5)
    uint32_t routeNextMs  = 0;   // backoff before the next attempt
    for (;;) {
        if (netGuard.shouldPark()) continue;   // reflash: park (ACK inside)
        if (WiFi.status() == WL_CONNECTED) {
            // ---- airport recentre (keyboard) ----
            // SNAPSHOT of the requested code + compare-and-swap under mutex
            // before applying: a UI timeout (state back to 0) or a new
            // request (airportReq rewritten) during the lookup INVALIDATES
            // the result — no more ghost recentres nor "lat/lon of AAA with
            // label BBB" (max review 07-26). NO SD write from netTask:
            // cfgDirty is consumed by loop() (SD/LCD share the same SPI bus).
            if (airportState == 1) {
                char code[8];
                xSemaphoreTake(gMtx, portMAX_DELAY);
                strlcpy(code, airportReq, sizeof(code));
                xSemaphoreGive(gMtx);
                float la, lo;
                bool reached = true;
                bool ok = lookupAirport(code, &la, &lo, nullptr, 0,
                                        nullptr, nullptr, &reached);
                xSemaphoreTake(gMtx, portMAX_DELAY);
                if (airportState == 1 && !strcmp(code, airportReq)) {
                    if (ok) {
                        cfg.lat = la; cfg.lon = lo;
                        strlcpy(cfg.airport, code, sizeof(cfg.airport));
                        cfgDirty = true;
                        pollNow  = true;
                        metarOkMs = 0; metarDueMs = 0; metarPrevObs = 0; metarCycleS = 1800;
                                         // new centre = new station: the held
                                         // observation belongs to the old one
                        // STALE data: the blips of the old centre would be
                        // projected anywhere — table cleared, "? aircraft"
                        // + alert until the 1st successful poll (07-27).
                        // The TRACKED flight survives (absolute lat/lon,
                        // reprojected on the new centre or clamped to the
                        // edge — a track holds through EVERY purge, user
                        // 07-27).
                        int w = 0;
                        for (int i = 0; i < planeCount; i++)
                            if (selectedHex[0] &&
                                !strcmp(planes[i].hex, selectedHex)) {
                                planes[w++] = planes[i];
                                break;
                            }
                        planeCount   = w;
                        lastPollOkMs = 0;
                        Serial.printf("[radar] centre -> %s (%.4f,%.4f)\n",
                                      code, la, lo);
                    }
                    // 3 = the database answered and does not know this code
                    // 4 = the database did not answer at all (08-03)
                    airportState = ok ? 2 : (reached ? 3 : 4);
                    uiDirty = true;
                }   // else: request cancelled/replaced → result dropped
                xSemaphoreGive(gMtx);
            }
            // ---- routes (generation protocol, bounded retry 07-27) ----
            // NETWORK failure (fetchRoute false) → up to 5 attempts spaced by
            // 8 s; a definitive answer (route/unknown) or a change of
            // selection → we stop.
            if (routeDoneGen != routeReqGen) {
                uint32_t gen; char cs[10];
                xSemaphoreTake(gMtx, portMAX_DELAY);
                gen = routeReqGen;
                strlcpy(cs, routeFlight, sizeof(cs));
                float sLat = cfg.lat, sLon = cfg.lon;   // fallback: the centre
                for (int i = 0; i < planeCount; i++)    // otherwise the
                    if (!strcmp(planes[i].hex, selectedHex)) {  // aircraft pos
                        sLat = planes[i].lat; sLon = planes[i].lon; break;
                    }
                xSemaphoreGive(gMtx);
                // NEW selection: counter AND backoff reset to zero BEFORE the
                // wait test — otherwise a tap inherited the 8 s of the
                // PREVIOUS route's retry, panel frozen on "route: searching..."
                // (max review 07-27).
                if (gen != routeTryGen) {
                    routeTryGen = gen; routeTries = 0; routeNextMs = millis();
                }
                if ((int32_t)(millis() - routeNextMs) >= 0) {
                    if (!cs[0]) {
                        routeDoneGen = gen;      // hex only: nothing to resolve
                    } else if (fetchRoute(cs, gen, sLat, sLon)) {
                        routeDoneGen = gen;      // definitive (found/unknown)
                    } else if (++routeTries >= 5) {
                        routeDoneGen = gen;      // network down for good: quit
                        Serial.printf("[radar] route %s : abandon (5 echecs)\n", cs);
                    } else {
                        routeNextMs = millis() + 8000;
                    }
                }
            }
            // ---- METAR (own period, only while the view is open) ----
            // A report is issued every 30 to 60 min: fetching it at the
            // radar's pace (5-60 s) would be hundreds of pointless TLS
            // sessions per hour against a free public service. And nothing is
            // fetched at all until the view is opened once — a bin that stays
            // on the radar never asks. Entering the view with data younger
            // than the period reloads NOTHING.
            // TAF counts as "the view is open": it comes from the SAME fetch,
            // so a user standing on the forecast must keep it fresh — gating on
            // METAR alone would have frozen the TAF at whatever it was when the
            // METAR step was last visited.
            // NO LONGER GATED ON THE VIEW (08-01), and the reason is a feature
            // shipped the same day that could not work. The weather-worsened
            // alert exists precisely because a VFR -> IFR transition "arrives
            // in silence, on a screen nobody is necessarily looking at" — and
            // this gate meant the comparison only ran WHILE you were looking at
            // the METAR or the TAF. The one situation the alert is for was the
            // one where it could not happen.
            // The old rationale ("a bin that stays on the radar never asks")
            // was sound when the observation was only ever read on demand. It
            // is not sound now that something WATCHES it: the traffic it saves
            // is six requests an hour against a free service publishing every
            // 30 to 60 min, and the price of saving it is a silent alarm.
            // Still nothing at all without a station to ask about.
            // DUE BY THE OBSERVATION, not by a stopwatch (see metarSchedule):
            // the signed comparison is what makes `metarDueMs == 0` mean "now"
            // whatever millis() has reached, so every place that invalidates
            // the station can simply zero it.
            if (metarStationSet() && (!metarOkMs ||
                              (int32_t)(millis() - metarDueMs) >= 0)) {
                // FAILURE spacing: a wrong station code or a service outage
                // would otherwise retry every 200 ms for as long as the view
                // stays open.
                static uint32_t metarTryMs = 0;
                if (metarForceMs) { metarForceMs = 0; metarTryMs = 0; }
                if (!metarTryMs || millis() - metarTryMs >= 60000) {
                    metarTryMs = millis();
                    if (!fetchMetar()) uiDirty = true;   // the view says why
                }
            }
            // ---- NOTAM (own period, only while its view is open) ----
            // Same discipline as the METAR: nothing is fetched until the view
            // is opened once, and a failure is spaced out so a 403 does not
            // hammer autorouter every 200 ms for as long as you stand there.
            if (viewLevel == VIEW_NOTAM && (!notamOkMs ||
                              millis() - notamOkMs >= NOTAM_PERIOD_MS)) {
                static uint32_t notamTryMs = 0;
                if (notamForceMs) { notamForceMs = 0; notamTryMs = 0; }
                if (!notamTryMs || millis() - notamTryMs >= 60000) {
                    notamTryMs = millis();
                    if (!fetchNotam()) uiDirty = true;   // the view says why
                }
            }
            // ---- periodic or forced poll ----
            uint32_t now = millis();
            // FORCED polls (recentre, settings, track, cut-short tiling) are
            // spaced by at least 3 s: otherwise cycling through flights
            // during a tiling pass relaunched a full-scale poll every 200 ms
            // against a rate-limited public API (adversarial check 07-27).
            // Adaptive back-off: an API answering 429 (rate-limit) or 5xx is
            // asking us to SLOW DOWN — insisting at the same pace makes it
            // worse. ×2 then ×4, immediate return to nominal on a successful
            // cycle.
            uint32_t periodMs = cfg.pollS * 1000UL * (1UL << pollPenalty);
            bool due = !lastPollMs || now - lastPollMs >= periodMs;
            if (due || (pollNow && now - lastPollMs >= 3000)) {
                pollNow = false;
                lastPollMs = now;
                pollBusy = true;  uiDirty = true;   // "searching..." notice
                bool ok = pollPlanes();
                pollFailed = !ok;
                if (httpStatus == 429 || httpStatus >= 500) {
                    if (pollPenalty < 2) pollPenalty++;
                    Serial.printf("[radar] API %d -> periode x%d\n",
                                  httpStatus, 1 << pollPenalty);
                } else if (ok) {
                    pollPenalty = 0;
                }
                pollBusy = false; uiDirty = true;
                Serial.printf("[radar] poll %s http=%d avions=%d\n",
                              ok ? "ok" : "echec", httpStatus, planeCount);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// ------------------------------------------------------------------- render
// ---- THEMES (user 07-27, selector in the settings, persisted in yaml) ----
// Four PAIRED day/night themes: even = day, odd = its night counterpart.
//   0 Gundam · 1 Gundam night (orange) · 2 Scope · 3 Scope night (astro)
// 0 "Gundam"    : the RX-78-2 Federation tricolour (user 07-28). Navy blue
//                 carries the STRUCTURE (rings, separators, panel), white
//                 carries the DATA, and the V-fin yellow marks whatever is
//                 active — title, accents, tracked target. Red stays the
//                 alert colour, the twin-camera green singles out military
//                 aircraft. Altitude ramp red → yellow → blue: low = warm,
//                 cruise = cold, in the kit's own colours.
// 1 "Scope"     : a real ATC scope — green phosphor, tracked target in WHITE
//                 (data block), amber/yellow = low altitudes (caution).
// 2 "Night"     : DARK red/amber that does not tire the eyes (night vision
//                 preserved — no white/blue at all, minimal brightness).
// Contrasts tuned to RGAA/WCAG AA (07-27): text ≥ ~4.5:1 on a dark
// background, graphical components ≥ 3:1 — except Night, darkened BY DESIGN
// (night-vision mode wins), kept at ≥ ~3:1 to stay readable.
struct Theme {
    uint16_t ring1, ring2, ringTxt;           // rings + N/nm labels
    uint16_t cs, selCol, trail;               // callsigns, tracked target, trail
    uint16_t panelBg, sep;                    // panel background + separators
    uint16_t txtMain, txt1, txt2, hint;       // main / labels / cities / hints
    uint16_t title, accent, alert;            // RADAR title, ~times, alerts
    uint16_t altG, altLow, altMid, altCruise; // ground / <10k / <25k / cruise
    uint16_t milCol;                          // military (magenta "special
                                              // track", ≠ altitudes 07-27)
};
static const Theme THEMES[4] = {
    // [0] Gundam DAY: navy-blue structure, white data, V-fin yellow for what
    // is active, alert red, "twin-eye" green for military aircraft.
    { 0x3B72, 0x220D, 0x6C36,   0xF79E, 0xFE25, 0x2A90,
      0x0043, 0x2A90,   0xF79E, 0xAE1C, 0x8D19, 0x6C16,
      0xFE25, 0xFE25, 0xE1C7,
      0x5AEC, 0xE1C7, 0xFE25, 0x4CBF, 0x3EA7 },
    // [1] Gundam NIGHT: saturated ORANGE, blue held at ZERO. Blue is what
    // pulled the earlier attempts towards yellow / sand; without it the hue
    // stays plainly orange (~30 deg) while preserving night vision. The
    // gradation comes from sliding red-orange towards orange — the only
    // degree of freedom left when blue is nil (user 07-28).
    { 0xB220, 0x6920, 0xE300,   0xFBC0, 0xFD20, 0x8980,
      0x1000, 0x7960,   0xFC60, 0xF340, 0xE2A0, 0xDA40,
      0xFC60, 0xFC60, 0xF904,
      0xAA20, 0xE280, 0xFC00, 0xFE08, 0xFAB0 },
    // [2] Scope DAY: EVERYTHING in graded phosphor — only the "data block"
    // (codes, tracked callsign, main values) stays white, like a real ATC
    // scope (user 07-27).
    { 0x0340, 0x0220, 0x0460,   0x0540, TFT_WHITE, 0x03E0,
      0x00E0, 0x02C0,   TFT_WHITE, 0x05C0, 0x0540, 0x04E0,
      TFT_GREEN, TFT_GREEN, TFT_ORANGE,
      TFT_DARKGREY, TFT_ORANGE, TFT_YELLOW, TFT_GREEN, 0xF81F },
    // [3] Scope NIGHT: ASTRO mode, in the spirit of Stellarium / SkySafari.
    // BLACK background and PURE red (G = B = 0) everywhere — text, rings,
    // callsigns, tracked target, military: that monochrome discipline is what
    // preserves dark adaptation. Hard constraint: pure red tops out at
    // 5.25:1 and needs R >= 235 to hold 4.5:1, so ONLY THREE text levels
    // exist (r5 = 29, 30, 31) — the hierarchy reads through size and
    // position, not brightness.
    // The ONE departure from monochrome: the altitude ramp slides towards
    // amber. It is the only place where colour carries information nothing
    // else carries, and 3:1..5.25:1 is not enough to separate four bands.
    // Amber (~590 nm) is still a long wavelength, so it does no harm to night
    // vision (user 07-28).
    { 0xC800, 0x7800, 0xF000,   0xF800, 0xF800, 0x9000,
      0x0000, 0x6800,   0xF800, 0xF000, 0xE800, 0xE800,
      0xF800, 0xF800, 0xF800,
      0xC000, 0xF800, 0xFB00, 0xFD00, 0xF800 },
};
static constexpr int THEME_N = 4;

// Themes come in day/night PAIRS: the even index is the day theme, the odd
// one its night counterpart. `auto_night` therefore switches to `theme | 1`
// and returns to the persisted theme in the morning — a night theme picked BY
// HAND stays in place (the user asked for it), since `1 | 1 == 1`.
static inline uint8_t nightThemeFor(uint8_t theme) { return theme | 1; }

static const Theme* TH = &THEMES[2];   // applied at boot from cfg.theme

static uint16_t altColor(const Plane& p) {
    if (p.onGround)        return TH->altG;
    if (p.altFt < 10000)   return TH->altLow;
    if (p.altFt < 25000)   return TH->altMid;
    return TH->altCruise;
}

// ---------------------------------------------------------- METAR view (↑)
// Flight category COLOUR CODE — the international one (VFR green, MVFR blue,
// IFR red, LIFR magenta), and the one datum a pilot reads at a glance. Taken
// RAW, not through the theme: a colour code that changes with the skin no
// longer is a code. It is the only breach of the theme in this view, and the
// text label is always next to it (WCAG 1.4.1: never colour alone).
// Rank of the category, 0 (best) to 3 (worst), -1 when unpublished. The NIGHT
// themes need an order they can draw without a hue — see metarCatChip.
static int fltCatRank(const char* cat) {
    if (!strcmp(cat, "VFR"))  return 0;
    if (!strcmp(cat, "MVFR")) return 1;
    if (!strcmp(cat, "IFR"))  return 2;
    if (!strcmp(cat, "LIFR")) return 3;
    return -1;
}

// Scale an RGB565 colour, channel by channel. Used to derive a shade FROM a
// theme instead of adding a constant to every palette: a fifth colour in the
// table is a fifth thing to keep consistent across four themes, and the
// derivation cannot drift.
static uint16_t dim565(uint16_t c, int num, int den) {
    const uint16_t r = ((c >> 11) & 31) * num / den;
    const uint16_t g = ((c >>  5) & 63) * num / den;
    const uint16_t b = ( c        & 31) * num / den;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// The runway SURFACE. `panelBg` used to serve, and it only worked in ONE
// theme: measured against each palette's own `ring1`, panelBg sits at 27 % in
// Scope — where the strip reads exactly as it should (user 08-01) — but at
// 7-17 % in Gundam day, 9 % in Gundam night and ZERO in Scope night, where the
// strip had no surface at all and survived only through its two edges.
// So the ratio Scope already proves right is applied to every theme, derived
// from that theme's own ring colour. Scope itself is unchanged to the bit:
// 26 * 27 / 100 = 7, which is exactly its old panelBg.
static uint16_t runwayFill() { return dim565(TH->ring1, 27, 100); }

static uint16_t fltCatColor(const char* cat) {
    if (!strcmp(cat, "VFR"))  return 0x2FE6;    // green
    if (!strcmp(cat, "MVFR")) return 0x3D7F;    // blue
    if (!strcmp(cat, "IFR"))  return 0xF800;    // red
    if (!strcmp(cat, "LIFR")) return 0xF81F;    // magenta
    return 0x8410;                              // unknown: grey
}

// Visibility. `visib` is a STRING in this API ("6+", "1/2", "10") expressed in
// STATUTE MILES. In aero mode it is shown VERBATIM — a pilot reads "6+", not a
// rounded conversion.
// In metric it is shown in KILOMETRES (user 07-30), with two traps:
//   - a trailing "+" is NOT a number. It is how this API re-encodes the METAR
//     group 9999, which conventionally means "10 km OR MORE" — a LOWER BOUND,
//     not a measurement. Converting the "6" of "6+" printed "9600+ m": a
//     metre-level precision the datum does not have, and a figure BELOW the
//     very threshold the group states. So "6+" reads "10 km+", period.
//   - a fraction ("1/2") or a keyword would be mangled by strtof, which stops
//     at the "/" and would have printed 1 mile instead of a half: verbatim.
// THE UNIT RIDES ON THE LABEL, not on the value (user 08-01), and `unit` is an
// out-parameter rather than a suffix because only the branch that produced the
// figure knows what it is measured in — the non-convertible fallback returns
// STATUTE MILES even in metric mode, so a label built by the caller from
// `cfg.metric` would have captioned "1/2" with "km".
// Two reasons, and the first is a defect the second only improves on:
//   - "6+ SM" is 5 glyphs, which is exactly the column's size-2 budget, so it
//     inked x 253..312 while the wind arrow's ring reaches x 254 — the value
//     grazed the rose, and at the one row (y 112..127) that straddles the rose's
//     horizontal axis, which is where a due-east wind puts the arrow. It was the
//     ONLY 5-glyph size-2 value in the right column, which is why this row and
//     no other showed it.
//   - "6+" alone is 2 glyphs, so it stays at size 2 where "10 km+" used to fall
//     to size 1. The figure gets BIGGER by losing its unit, not smaller.
// It is also the pattern this view already uses: the WIND row puts the
// direction on the label ("VENT 080" + "16kt") for the same reason.
static String metarVisib(const char* v, String* unit) {
    *unit = "";
    if (!v[0]) return "?";
    if (!cfg.metric) { *unit = "SM"; return v; }
    size_t n = strlen(v);
    if (v[n - 1] == '+') { *unit = "km"; return "10+"; }   // METAR 9999
    char* end = nullptr;
    float sm = strtof(v, &end);
    if (end == v || *end) { *unit = "SM"; return v; }      // not convertible
    float km = sm * 1.60934f;
    *unit = "km";
    // One decimal below 10 km ("3" SM -> 4.8 km is a useful figure), none
    // above: at that range a tenth of a kilometre is noise.
    return km < 10.0f ? String(km, 1) : String((int)(km + 0.5f));
}

// RELATIVE HUMIDITY — DERIVED, because the report does not carry it: Magnus
// formula over temperature and dew point, the same one the reference mockup
// uses (27 C / 20 C dew point -> 66 %, to the unit).
// -1 when either input is missing: `| 0.0f` on an absent dew point would have
// shown a confident 30-something percent that nothing measured.
static int metarRh(const Metar& m) {
    if (!m.hasTemp || !m.hasDewp) return -1;
    const float et = expf(17.625f * m.temp / (243.04f + m.temp));
    const float ed = expf(17.625f * m.dewp / (243.04f + m.dewp));
    if (!(et > 0.0f)) return -1;                  // also catches NaN
    int rh = (int)lroundf(100.0f * ed / et);
    return rh < 0 ? 0 : (rh > 100 ? 100 : rh);    // dew point > temp: sensor
}                                                 // noise, not 130 % humidity

// STATION NAME for the title line. The API answers
// "Reunion/St Denis/Garros Arpt, , RE" — name, state (often EMPTY), country.
// That ", , RE" tail is noise on a line that already shows the ICAO, so
// everything from the FIRST comma is dropped; the "/" separators are aired out
// into " / " (a bare "Reunion/St Denis" reads as one word at 6 px), and the
// result is clamped to `maxChars` with a ".." when it overflows.
// The bytes are already printable ASCII: fetchMetar runs cityClean over them,
// because this bin's 6x8 font renders UTF-8 as "??".
static void metarName(const char* in, char* out, size_t n, int maxChars) {
    if (!n) return;
    out[0] = '\0';
    if (!in) return;
    const int lim = (maxChars < (int)n - 1) ? maxChars : (int)n - 1;
    int  w = 0;
    bool over = false;
    for (const char* r = in; *r && *r != ','; r++) {
        if (*r == '/') {
            if (w + 3 > lim) { over = true; break; }   // never split " / "
            if (w && out[w - 1] == ' ') w--;           // already spaced source
            out[w++] = ' '; out[w++] = '/'; out[w++] = ' ';
            while (r[1] == ' ') r++;
            continue;
        }
        if (*r == ' ' && (!w || out[w - 1] == ' ')) continue;   // lead/double
        if (w + 1 > lim) { over = true; break; }
        out[w++] = *r;
    }
    while (w && out[w - 1] == ' ') w--;
    if (over && w >= 2) { out[w - 2] = '.'; out[w - 1] = '.'; }
    out[w] = '\0';
}

// A runway spec -> the two runway numbers plus the TRUE heading of the FIRST
// of them. ONE parser for the TWO sources: the typed `metar_rwy` and the
// record rebuilt from the SD base by runwayFromSd (they share this syntax so
// they cannot drift). Accepted forms: "12/30@102" (the right one), "12/30",
// "12@102", "12".
// A runway NUMBER IS NOT ITS HEADING: it is the MAGNETIC bearing rounded to
// the ten, and the true heading differs by the local magnetic declination.
// FMEE's runway "12/30" really lies 102/282 deg (OurAirports runways.csv),
// 18 deg away from the 120 the number suggests — enough to put the bar visibly
// askew on the rose. Hence the "@" form, which is the documented one; without
// it we fall back to number x 10, an approximation good only where the
// declination is nil.
// Returns false on an empty or unparsable spec: the rose is then drawn ALONE,
// which is still true. That is what happens for an aerodrome absent from
// runways.csv with nothing typed in — a METAR never carries the runway, so
// there is nothing to guess from.
static bool metarRunway(const char* s, int* n1, int* n2, float* hdg) {
    if (!s || !s[0]) return false;
    char* end = nullptr;
    long a = strtol(s, &end, 10);
    if (end == s || a < 1 || a > 36) return false;
    long b = -1;
    const char* sl = strchr(s, '/');
    if (sl) {
        char* e2 = nullptr;
        long v = strtol(sl + 1, &e2, 10);
        if (e2 != sl + 1 && v >= 1 && v <= 36) b = v;
    }
    if (b < 0) b = (a > 18) ? a - 18 : a + 18;      // reciprocal end
    float h = -1.0f;
    const char* at = strchr(s, '@');
    if (at) {
        char* e3 = nullptr;
        float v = strtof(at + 1, &e3);
        if (e3 != at + 1) h = fmodf(fmodf(v, 360.0f) + 360.0f, 360.0f);
    }
    if (h < 0.0f) h = (float)((a * 10) % 360);      // declination-blind fallback
    *n1 = (int)a; *n2 = (int)b; *hdg = h;
    return true;
}

// Bearing -> screen, compass convention: 0 deg is UP and the angle grows
// CLOCKWISE (a rose is not the trigonometric circle).
static inline int roseX(int cx, float r, float bearing) {
    return cx + (int)lroundf(r * sinf(bearing * DEG_TO_RAD));
}
static inline int roseY(int cy, float r, float bearing) {
    return cy - (int)lroundf(r * cosf(bearing * DEG_TO_RAD));
}

// CLOUD CODES ARE JARGON. "CAVOK", "///TCU" say everything to a pilot and
// nothing to anyone else, so the code STAYS — it is what the raw report
// carries — and a plain-French gloss goes UNDER it (user 07-30).
// EVERY gloss fits the 60 px column, i.e. 10 glyphs of this 6x8 font, and that
// budget is what dictated the wording: "degage" and not "ciel degage" (11),
// "non signif" and not "rien de significatif" (20), "quelques" and not
// "quelques nuages" (15), "invisible" and not "ciel invisible" (14). No accent
// either — the font renders UTF-8 as "??" (see cityClean).
// The ENGLISH side obeys the SAME 10-glyph budget, and it is what forced
// "no signif" over "no significant cloud" and "obscured" over "sky obscured":
// the column is shared, so whichever language is loaded must fit the same box.
// Matched on the PREFIX because the token may carry a type suffix ("///TCU").
static const char* cloudGloss(const char* c) {
    if (!c || !c[0])            return nullptr;
    if (!strncmp(c, "CAVOK", 5)) return sce::T("clear", "degage");
    if (!strncmp(c, "SKC", 3) || !strncmp(c, "CLR", 3) ||
        !strncmp(c, "NCD", 3))   return sce::T("sky clear", "ciel clair");
    if (!strncmp(c, "NSC", 3))   return sce::T("no signif", "non signif");
    if (!strncmp(c, "FEW", 3))   return sce::T("few", "quelques");     // 1-2 octas
    if (!strncmp(c, "SCT", 3))   return sce::T("scattered", "epars");  // 3-4 octas
    if (!strncmp(c, "BKN", 3))   return sce::T("broken", "fragmente"); // 5-7 octas
    if (!strncmp(c, "OVC", 3))   return sce::T("overcast", "couvert"); // 8 octas
    if (!strncmp(c, "VV",  2))   return sce::T("obscured", "invisible");
    if (!strncmp(c, "///", 3))   return sce::T("unmeasured", "non mesure");
    return nullptr;                       // unknown code: it stands alone
}

// The TYPE suffix, when the layer carries one. Only two exist that change a
// flight, and both are what FMEE publishes: TCU (towering cumulus, growing)
// and CB (cumulonimbus, i.e. a thunderstorm). Same 10-glyph budget, which is
// why it is "bourgeonne" and not "bourgeonnant" (12), "orage" and not
// "cumulonimbus" (12) — and "orage" is the operational meaning anyway.
static const char* cloudTypeGloss(const char* c) {
    if (!c)              return nullptr;
    if (strstr(c, "TCU")) return sce::T("towering", "bourgeonne");
    if (strstr(c, "CB"))  return sce::T("storm", "orage");
    return nullptr;
}

// COMPASS ROSE: outer ring, 36 ticks every 10 deg, and the 12 labels of the
// thirties read as TENS of degrees (N, 3, 6, E, 12 ... 33) like a compass card.
// The inner radii are DERIVED from `r` so the whole rose scales with one
// number: ticks r-5 / r-10 (the labelled thirties are the long ones), labels
// centred at r-22.
// A2.22: the ticks come in two lengths, and writing them as a "short" call
// beside a "long" call is EXACTLY the pattern GCC 8.4 Xtensa mis-optimises —
// it drops the second of two similar drawing calls in one body (the missing
// arm of the Dead cross, 07-25). So the length is a PARAMETER of one single
// call site inside the loop, never a second call.
// NOINLINE, on purpose: `draw()` inlines the whole view, and the radar body
// already holds its own fillTriangle / drawLine loops. Keeping each of the
// three rose parts in its OWN body is the cheapest way to guarantee that no
// two similar drawing loops ever share a body — the A2.22 hazard. This runs
// on demand, never per frame, so the call costs nothing that matters.
__attribute__((noinline))
static void metarRose(int cx, int cy, int r) {
    canvas.drawCircle(cx, cy, r, TH->ring1);
    for (int i = 0; i < 36; i++) {
        const float b   = (float)(i * 10);
        const bool  lbl = (i % 3) == 0;
        const float ri  = (float)(r - (lbl ? 10 : 5));
        canvas.drawLine(roseX(cx, (float)r, b), roseY(cy, (float)r, b),
                        roseX(cx, ri, b), roseY(cy, ri, b),
                        lbl ? TH->ring1 : TH->ring2);
    }
    static const char* const LBL[12] = { "N", "3", "6", "E", "12", "15",
                                         "S", "21", "24", "W", "30", "33" };
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    canvas.setTextColor(TH->ringTxt);
    canvas.setTextDatum(textdatum_t::middle_center);
    for (int i = 0; i < 12; i++)
        canvas.drawString(LBL[i], roseX(cx, (float)(r - 22), (float)(i * 30)),
                          roseY(cy, (float)(r - 22), (float)(i * 30)));
    canvas.setTextDatum(textdatum_t::top_left);
}

// RUNWAY CENTRELINE: DASHED, rim to rim through the centre — road markings
// (user 07-31). It is not decoration and it is not a load on the disc: it is
// the construction line that makes the rose READ. It lets the runway heading
// be taken straight off the graduations, and it puts the runway and the wind
// side by side in ONE glance — the crosswind component, the very thing a pilot
// looks for on this card (user 07-30).
// DASHED AND DARK on purpose. Solid, it was a third parallel line competing
// with the two edges; dashed, it reads as a road marking and the eye follows
// the runway instead of counting lines. `ring2` (the dim graduation colour)
// rather than `txt2`: this line has to be FOUND when looked for, not seen all
// the time. Contrast is deliberately traded away here — it guides, it carries
// no datum of its own (user 07-31).
// Drawn AFTER the bar, unlike before: the dashes now run OVER the fill for the
// whole length of the runway, which is what makes the road read. Under it,
// they only showed outside the bar and the marking stopped where the runway
// began — the exact opposite of a road.
// ONE drawLine call site in a loop (A2.22): it lives in its OWN body because
// metarRunwayBar already owns a drawLine loop for the edges, and two loops of
// the same primitive in one body is the shape GCC 8.4 Xtensa mis-optimises.
// Like the bar, nothing at all without a runway.
__attribute__((noinline))
static void metarRunwayAxis(int cx, int cy, int r, const char* spec) {
    int n1 = 0, n2 = 0;
    float hdg = 0.0f;
    if (!metarRunway(spec, &n1, &n2, &hdg)) return;
    const float a  = hdg * DEG_TO_RAD;
    const float ax = sinf(a), ay = -cosf(a);
    // 7 px of ink every 14: shorter dashes dissolve into the graduations at
    // this radius, longer ones close back into the solid line we just left.
    for (int d = -r; d <= r - 7; d += 14)
        canvas.drawLine(cx + (int)lroundf(ax * (float)d),
                        cy + (int)lroundf(ay * (float)d),
                        cx + (int)lroundf(ax * (float)(d + 7)),
                        cy + (int)lroundf(ay * (float)(d + 7)), TH->ring2);
}

// RUNWAY STRIP: the runway itself, drawn across the rose at its TRUE heading —
// a filled quad of half-length r+4 and half-width 11, i.e. surface AND sides
// running the whole way and a few pixels past the rim, the way the wind arrow
// leaves the disc at r+2. Runway and wind then quit the circle the same way and
// the pair reads as ONE comparison.
// NO END CAPS across the ends (user 07-31): they closed the shape into a
// rectangle, and a rectangle is a box, not a runway. Nothing needs to mark an
// end that runs off the card.
// A2.22: the quad is TWO triangles and the edges TWO segments — each repeated
// shape from ONE call site in a loop with a table for the varying part.
// Extracting them into a lambda is NOT the remedy: on 07-28 it made the
// elimination more likely, not less.
// NOINLINE for the same reason as metarRose: one body per drawing loop.
__attribute__((noinline))
static void metarRunwayBar(int cx, int cy, int r, const char* spec) {
    int n1 = 0, n2 = 0;
    float hdg = 0.0f;
    if (!metarRunway(spec, &n1, &n2, &hdg)) return;
    const float a  = hdg * DEG_TO_RAD;
    const float ax = sinf(a),  ay = -cosf(a);      // along the runway
    const float px = -ay,      py = ax;            // across it
    // ONE length for the fill AND the edges (user 07-31): the strip runs the
    // whole way, a few pixels past the rim (r+4) like the wind arrow at r+2, so
    // the runway leaves the disc the same way the wind does and the pair reads
    // as one comparison instead of one object inside a circle and another one
    // crossing it. The fill used to stop at r-30 while the edges ran on, which
    // left two bare rails past the ends — a runway drawn as a road has to have
    // its surface everywhere its sides are.
    // WHAT IT COSTS, and it is a real cost: the fill now REACHES the twelve
    // degree labels and covers whichever ones the strip passes over — up to two
    // of them, and by construction that happens whenever the runway points at a
    // thirty, which runways numbered in tens of degrees do. The heading is
    // still readable off the graduations either side of the strip; the labels
    // it hides are the rounded version of a figure the card gives elsewhere.
    // HALF-WIDTH 14, was 11 (user 08-03). The runway numbers are drawn ON the
    // axis, centred, in Font0 at size 2 — so 16 px tall and 12 px wide. Against
    // a 22 px strip that left 3 px of air on the tight axis, and a figure that
    // close to an edge reads as touching it. 28 px gives 6, which is the same
    // clearance the numbers already have from the degree labels above them.
    // It costs a little more of the twelve degree labels, which the strip
    // already covers where it crosses them — a cost the comment above accepts
    // for the same reason: the heading stays readable off the graduations
    // either side, and the hidden labels are the rounded form of a figure the
    // card prints elsewhere.
    const float EXT = (float)(r + 4), W = 14.0f;   // half length / half width
    static const float SL[4] = { 1, 1, -1, -1 }, SW[4] = { 1, -1, -1, 1 };
    int qx[4], qy[4];
    for (int i = 0; i < 4; i++) {
        qx[i] = cx + (int)lroundf(ax * EXT * SL[i] + px * W * SW[i]);
        qy[i] = cy + (int)lroundf(ay * EXT * SL[i] + py * W * SW[i]);
    }
    static const uint8_t TRI[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
    for (int t = 0; t < 2; t++)
        canvas.fillTriangle(qx[TRI[t][0]], qy[TRI[t][0]],
                            qx[TRI[t][1]], qy[TRI[t][1]],
                            qx[TRI[t][2]], qy[TRI[t][2]], runwayFill());
    // The two long edges on top of it. The outline is what shows the strip at
    // all in the two night themes, where panelBg is (almost) the background.
    // TWO segments, ONE call site, table for the varying part (A2.22).
    int lx0[2], ly0[2], lx1[2], ly1[2];
    for (int k = 0; k < 2; k++) {
        const float s = k ? 1.0f : -1.0f;
        lx0[k] = cx + (int)lroundf(-ax * EXT + px * W * s);
        ly0[k] = cy + (int)lroundf(-ay * EXT + py * W * s);
        lx1[k] = cx + (int)lroundf( ax * EXT + px * W * s);
        ly1[k] = cy + (int)lroundf( ay * EXT + py * W * s);
    }
    for (int i = 0; i < 2; i++)
        canvas.drawLine(lx0[i], ly0[i], lx1[i], ly1[i], TH->ring1);
}

// RUNWAY NUMBERS. Their own body, and drawn AFTER the centreline (user 07-31):
// the dashes used to run straight through the digits, because the marking was
// painted over a bar whose numbers had already been laid down. A number cut by
// a road marking is the one thing on this card that must never be ambiguous —
// it is the runway's identity.
// WITHOUT a box: what was the plate's FILL is now the glyph colour. The box was
// solving a contrast problem the bar no longer has — it used to sit on a filled
// quad, and a bright plate with black digits was the only pairing holding
// 4.5:1 in all four themes. Two plates parked inside the rose also read as
// objects of their own, which is precisely what a runway number is not. The
// font still cannot be rotated: the digits stay upright instead of following
// the runway.
// ONE drawString call site in a loop (A2.22).
__attribute__((noinline))
static void metarRunwayNums(int cx, int cy, int r, const char* spec) {
    int n1 = 0, n2 = 0;
    float hdg = 0.0f;
    if (!metarRunway(spec, &n1, &n2, &hdg)) return;
    const float a  = hdg * DEG_TO_RAD;
    const float ax = sinf(a), ay = -cosf(a);
    char num[2][4];
    snprintf(num[0], sizeof(num[0]), "%02d", n1);
    snprintf(num[1], sizeof(num[1]), "%02d", n2);
    int bx[2], by[2];
    // WHICH END CARRIES WHICH NUMBER — inverted here until 2026-07-30 (user
    // spotted it against metar-taf: we drew "30/12" where the reference shows
    // "12/30"). A runway number is painted at its THRESHOLD, i.e. at the end
    // OPPOSITE to the direction it is flown: you line up on runway 12 at the
    // north-west end and fly away on heading ~120. So `n1`, whose true heading
    // is `hdg`, belongs at bearing `hdg + 180` — hence s = -1 for k = 0.
    // They sit OUT near the rim, a few pixels inside the degree labels (user
    // 07-31): r-38 against labels centred at r-22, so 4 px of air between the
    // top of a number and the bottom of a label. Parked at the bar's own ends
    // minus 18 they were almost on the centre, where a runway number never is.
    const float NR = (float)(r - 38);
    for (int k = 0; k < 2; k++) {
        const float s = k ? 1.0f : -1.0f;
        bx[k] = cx + (int)lroundf(ax * NR * s);
        by[k] = cy + (int)lroundf(ay * NR * s);
    }
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(2);
    canvas.setTextColor(TH->txtMain);      // no background: the ex-plate colour
    canvas.setTextDatum(textdatum_t::middle_center);
    for (int k = 0; k < 2; k++)
        canvas.drawString(num[k], bx[k], by[k]);
    canvas.setTextDatum(textdatum_t::top_left);
}

// WIND TRAIL: the air's own path across the field, DOTTED, rim to rim through
// the centre — the wind's counterpart to the runway axis (user 07-31).
// The arrow alone marks a POINT on the rim; the eye then has to carry that
// azimuth across the disc by itself to compare it with the runway, which is
// the one comparison this card exists for (crosswind). The trail carries it
// for free.
// DOTTED, and that is the whole point of the shape: the runway is a REAL
// object, drawn solid; the wind is a measurement, so it gets a construction
// line. Two solid lines crossing the rose would read as two runways.
// ACCENT, unlike the discreet runway axis: the trail and the arrow are the
// same object and must read as one — being dotted already halves its weight.
// Drawn BEFORE the arrow and its figures: those carry an OPAQUE black
// background, so they punch their own room out of the dots exactly as they do
// out of the graduations.
// Its own body with ONE call site (A2.22): a lone drawing call sitting beside
// another loop of the same primitive is the shape GCC 8.4 Xtensa drops.
__attribute__((noinline))
static void metarWindTrail(int cx, int cy, int r, int wdir) {
    const float a  = (float)wdir * DEG_TO_RAD;
    const float ux = sinf(a), uy = -cosf(a);      // outward radial, wind FROM
    // OUT PAST THE RIM, exactly as far as the runway strip (r+4, user 07-31):
    // the two lines being compared must leave the disc the same way, or the eye
    // reads one as an object of the rose and the other as something laid over
    // it. It used to stop 2 px SHORT of the rim, which made the wind look
    // contained by a circle the runway crossed.
    // The pitch is DERIVED, not fixed: 2*lim is not a multiple of 6, so a
    // hard `d += 6` would land the last dot up to 5 px short at one end and
    // exactly on the other — an asymmetry that reads as a mistake on a line
    // whose whole job is to be straight through the centre. Solving for a whole
    // number of gaps puts a dot on BOTH extremities and keeps the pitch within
    // a fraction of a pixel of 6.
    // 2x2 dots: a 1 px dot vanishes against the graduations, and a shorter
    // pitch closes back into a solid line.
    const int lim = r + 4;
    const int n   = (2 * lim + 3) / 6;            // gaps of ~6 px
    for (int i = 0; i <= n; i++) {
        const float d = -(float)lim + (float)(2 * lim) * (float)i / (float)n;
        canvas.fillRect(cx + (int)lroundf(ux * d) - 1,
                        cy + (int)lroundf(uy * d) - 1,
                        2, 2, TH->accent);
    }
}

// WIND ARROW: hollow, on the rim, at the azimuth the wind comes FROM (aero
// convention) and pointing INWARDS — the tip says where the air goes.
// ITS FIGURES ARE NOT HERE ANY MORE (user 07-31). They used to ride on this
// same radial, just inside the rim, on an opaque black background: that put
// them where the eye already was, but it punched two black holes through the
// graduations and moved them somewhere else at every observation, so the rose
// could never be read as one object. They now live at the top of the RIGHT
// margin, still in the ACCENT — the colour is what ties them to this arrow
// across the gap.
// The arrow lives OUTSIDE the rim (r+2 .. r+12), which is what caps the
// radius: it is the ARROW ring, not the disc, that has to fit the band left
// between the header rule and the raw report.
// NOINLINE for the same reason as metarRose: one body per drawing loop.
__attribute__((noinline))
static void metarWindArrow(int cx, int cy, int r, int wdir) {
    const float a  = (float)wdir * DEG_TO_RAD;
    const float ux = sinf(a), uy = -cosf(a);      // outward radial
    const float px = -uy,     py = ux;            // tangential
    const float tip = (float)(r + 2), base = (float)(r + 12), half = 7.0f;
    canvas.drawTriangle(cx + (int)lroundf(ux * tip),
                        cy + (int)lroundf(uy * tip),
                        cx + (int)lroundf(ux * base - px * half),
                        cy + (int)lroundf(uy * base - py * half),
                        cx + (int)lroundf(ux * base + px * half),
                        cy + (int)lroundf(uy * base + py * half),
                        TH->accent);
}

// The category chip. Its own body, and every repeated shape from ONE call
// site in a loop: two rounded frames drawn from two statements in the same
// function is precisely the pair GCC 8.4 Xtensa drops (A2.22).
__attribute__((noinline))
static void metarCatChip(int x, int y, int w, const char* cat, bool night) {
    const int rank = fltCatRank(cat);
    uint16_t fg, bg;
    if (!night || rank < 0) {
        // DAY: the international code, taken RAW and not through the theme.
        const uint16_t c = fltCatColor(cat);
        canvas.fillRoundRect(x, y, w, 24, 4, c);
        fg = TFT_BLACK; bg = c;
    } else {
        // NIGHT: one hue (the theme's), four weights.
        //   0 VFR   outline only          the quietest
        //   1 MVFR  outline + dim fill
        //   2 IFR   solid
        //   3 LIFR  solid + inner frame   the heaviest
        const uint16_t ink  = TH->accent;
        const bool     fill = (rank >= 2);
        // ONE fillRoundRect call site: the plate is painted once, with the
        // colour the rank calls for — panelBg for the dim fill, the ink for a
        // solid one, and the background itself when the chip is an outline.
        canvas.fillRoundRect(x, y, w, 24, 4,
                             fill ? ink : (rank == 1 ? TH->panelBg
                                                     : (uint16_t)TFT_BLACK));
        // 1 frame for ranks 0..2, 2 for rank 3 — ONE call site in a loop.
        const int nFrame = (rank == 3) ? 2 : 1;
        for (int i = 0; i < nFrame; i++)
            canvas.drawRoundRect(x + i * 3, y + i * 3, w - i * 6, 24 - i * 6,
                                 4, fill ? (uint16_t)TFT_BLACK : ink);
        fg = fill ? (uint16_t)TFT_BLACK : ink;
        bg = fill ? ink : (uint16_t)TFT_BLACK;
    }
    // ONE drawString for BOTH paths. Written as two identical calls in the two
    // branches, GCC 8.4 Xtensa tail-merged them — which happens to be harmless
    // here, but it is the exact shape A2.22 says can instead DROP the second.
    // The label is not something to leave to that distinction.
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(2);
    canvas.setTextColor(fg, bg);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.drawString(cat, x + w / 2, y + 12);
    canvas.setTextDatum(textdatum_t::top_left);
}

// The view, re-laid out on 2026-07-30 after the reference mockup
// (docs/assets/metar.png). The mockup is PORTRAIT and this screen is 320x240
// LANDSCAPE, so it is RE-ARRANGED, not copied — but its PROPORTION is kept:
// the rose IS the display, not one panel among two (user 07-30).
//   y 0..21    shared header (viewHeader): ICAO left, station name RIGHT — no
//              screen label here, the rose IS the METAR's name (user 07-31)
//   y 24..212  the rose, CENTRED on (160,118) — 188 px of height, all of it
//   y 216..239 rawOb VERBATIM, unchanged — that string is what a pilot reads
//              (THREE lines at a pitch of 8 since 08-01: two only showed 100
//              of the report's 143 characters)
// RADIUS 82, and it is the ARROW that sets it: the wind arrow lives OUTSIDE
// the rim (r+2 .. r+12), so what must fit the 188 px band is the arrow ring
// (radius 94), not the disc. 82 + 12 = 94 = 188/2, exactly. The radius grew
// from 78 the day the navigation footer was dropped (07-31): those 12 px went
// to the rose rather than to a legend about the swipes.
// The disc then spans x 78..242 and the arrow ring x 66..254, which leaves the
// two side bands the data folds into. The data column is gone: its rows become
// short stacked label/value pairs, temperature side on the left with the
// category chip, pressure side on the right — both in ONE shared column width
// of 60 px (COLW), so the two margins look identical and it is the TEXT that
// gets abbreviated, never the column that grows (user 07-30).
// The word "METAR" is NOT written anywhere we compose: the view is reached by
// its own gesture and those pixels are better spent on the (long) station name
// (user 07-30). `rawOb` below does start with "METAR FMEE 301130Z ...", and it
// stays exactly as received.
// ============ SHARED CHROME OF THE READING SCREENS (METAR/TAF/NOTAM) ======
// The three sit one step apart on the SAME ring and describe the SAME station.
// They therefore have to read as ONE family, and the way to guarantee that is
// not discipline but a single implementation: the header and the footer are
// drawn HERE, once, and no screen re-invents them. What differs between the
// three is the BODY and nothing else. Before this (07-31) each screen had
// grown its own margins, its own first line and its own pitch, and the drift
// showed as soon as the up swipe made them a cycle you flick through.
//
//   y   0..21   header : ICAO (accent, size 2) | station name (txt2) | LABEL
//   y  22       rule, x 8..311 — the width EVERY other band aligns on
//   y  26..212  body, 187 px, the screen's own
//   y 216..239  raw band, up to 3 lines of the 6x8 font — for screens that carry
//               verbatim aeronautical text (the METAR's rawOb)
//
// THERE IS NO FOOTER (user 07-31). It carried "what the up swipe gives next"
// and the ring position, and it cost a 12 px band on EVERY screen to say what
// one swipe teaches once. The bottom of the screen is worth more to the data
// than to a legend about the navigation.
//
// FONT RULE, and it is a rule, not a habit: verbatim aeronautical text is
// drawn in Font0 (fixed 6 px pitch — a code must never re-flow or re-space),
// prose written by us is drawn in efontJA_12 (proportional, readable). That
// is what tells the eye, without a legend, which words came off the wire.
static constexpr int VIEW_RULE_L = 8, VIEW_RULE_R = 312;
static constexpr int VIEW_BODY_Y = 26, VIEW_RAW_Y = 216;
// The raw band's FULL height, down to the last screen row: 3 lines of 8 px.
// Named because the banner covers the band ENTIRELY (08-03) and "216 + 24"
// written twice in two files is how a band and its cover drift apart.
static constexpr int VIEW_RAW_H = 24;
static constexpr int VIEW_NAME_X = 62;     // past the 4 size-2 letters + gap

// Header. `label` names the screen and is OPTIONAL — the METAR passes none
// (user 07-31): the rose IS its name, no other screen looks remotely like it,
// and the word cost the station name five characters for nothing. The TAF and
// the NOTAM keep theirs because a wall of text and a statement of absence do
// need saying which is which.
// With no label the name goes back to being RIGHT-ALIGNED on the rule's end,
// which is where it sat before and where the eye expects the header to close.
// With one, the name starts after the code and stops before the label. Either
// way the room is MEASURED, never assumed: the label's width changes from one
// screen to the next.
// `ownRule`: the caller draws its own separator instead of the plain one
// (08-03, user: "could the deck strip replace the rule under the header?").
// It could, and it should: the rule does ONE job - it says the header stopped -
// and the NOTAM deck strip does that job while also saying how many notices are
// in force, which one is open and how many urgent ones are left. Two lines of
// screen for one function was the waste; the strip tiles the same span, so it
// still reads as a rule to anyone not looking for the colours.
// The METAR and the TAF have nothing to put there and keep the plain rule.
static void viewHeader(const char* icao, const char* name, const char* label,
                       bool ownRule = false) {
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(2);
    canvas.setTextColor(TH->accent);
    canvas.drawString(icao && icao[0] ? icao : "----", VIEW_RULE_L, 4);
    canvas.setTextSize(1);
    const bool lab = label && label[0];
    int room = VIEW_RULE_R - VIEW_NAME_X;
    if (lab) {
        canvas.setTextColor(TH->accent);
        const int lw = canvas.textWidth(label);
        canvas.drawString(label, VIEW_RULE_R - lw, 8);
        room -= lw + 8;                    // 8 px of air before the label
    }
    if (name && name[0] && room > 12) {
        char nm[64];
        strlcpy(nm, name, sizeof(nm));
        while (nm[0] && canvas.textWidth(nm) > room) nm[strlen(nm) - 1] = '\0';
        canvas.setTextColor(TH->txt2);
        canvas.drawString(nm, lab ? VIEW_NAME_X
                                  : VIEW_RULE_R - canvas.textWidth(nm), 8);
    }
    if (!ownRule)
        canvas.drawFastHLine(VIEW_RULE_L, 22, VIEW_RULE_R - VIEW_RULE_L, TH->sep);
}

// The long press, said out loud. Drawn LAST by each reading screen so it sits
// over whatever is there, in the raw band — the one strip every reading screen
// keeps free for a line of status.
// Returns what the caller needs, and it is TWO different questions:
//   BANNER_NONE   nothing drawn -- the raw band is free
//   BANNER_HOLD   drawn, transient: keep the screen dirty so it expires
//   BANNER_STICKY drawn, but for MINUTES: do NOT keep the screen dirty
// It used to return a bool meaning both at once, so the weather alert had to
// BREAK the contract to avoid a 5-minute redraw at loop rate: it drew and
// returned false, and every caller's `else` then believed the band was free
// and painted over it. On the NOTAM screen that stacked "HTTP 403" on top of
// "meteo degradee : VFR > IFR" for five minutes (review 08-02).
enum BannerState : uint8_t { BANNER_NONE = 0, BANNER_HOLD, BANNER_STICKY };

// CHOOSING the message is separate from DRAWING it (08-02), because the two
// screens that need it do not share a geometry: the reading screens have a
// dedicated band at (VIEW_RULE_L, VIEW_RAW_Y), the radar has one centred line
// at the bottom. Fused, the banner was simply ABSENT from the radar — the
// comment on the weather watch claimed "every screen already calls it, radar
// included", and the radar branch of draw() never did. So the "weather got
// worse" warning, whose whole point is to reach you unprompted, could not
// appear on the screen you spend all your time on (found 08-02, pre-existing).
// It also made the Fire's B-long refresh and A-long address silent, since both
// are used mostly from the radar.
// `out` owns the storage when the message is built rather than literal.
//
// WHEN THE BANNER GOES, THE BAND COMES BACK AT ONCE — and that needs a date,
// not a poll (08-03, user asked for the zone to be cleaned on expiry). The two
// STICKY messages deliberately do NOT keep the frame dirty (a 5-minute message
// redrawn at loop rate is the ~25 % of loop() that the 8 fps full redraw cost
// and that was removed for it), so nothing was watching for their end: the
// coloured bar sat on the raw text until some unrelated event, or the 1 s
// counter tick, happened to redraw. Now every windowed message publishes the
// millisecond it dies; loop() arms ONE redraw on that date. Zero cost while it
// shows, exact when it goes. `0` = no banner is running.
// Written only from the loop()/draw() context (netTask never calls this).
static uint32_t bannerUntilMs = 0;

static BannerState bannerPick(String& out, const char*& msg, uint16_t& col) {
    const uint32_t now = millis();
    String&     built = out;
    msg = nullptr;
    col = TH->hint;
    BannerState st = BANNER_NONE;
    if (holdFireMs) {
        if (now - holdFireMs < HOLD_SHOW_MS) {
            msg = sce::T("refreshing...", "mise a jour...");
            col = TH->accent;
        } else {
            holdFireMs = 0;          // one last frame, to wipe it
        }
        st = BANNER_HOLD;
    } else if (holdArmMs) {
        msg = sce::T("hold to refresh...", "maintenir pour majo...");
        st = BANNER_HOLD;
    } else if (netInfoMs && now - netInfoMs < NETINFO_SHOW_MS) {
        // WHERE THE SETTINGS LIVE. On a board with no touchscreen there is no
        // settings panel to swipe to, so the one thing the user needs is the
        // address to type. STICKY for the same reason as the weather line
        // below: it stands for seconds, not for a gesture, and forcing a
        // full-screen redraw at loop rate for that long is what the comment
        // there is about. It appears because applyEvent sets uiDirty once.
        built = String("http://") + ipStr + "/config";
        msg = built.c_str();
        col = TH->accent;
        st  = BANNER_STICKY;
        bannerUntilMs = netInfoMs + NETINFO_SHOW_MS;   // wipe on that date
    } else if (apiFellBackMs && apiServing[0]) {
        // THE SUBSTITUTION IS SAID, for as long as it lasts (user 08-03).
        // No window: this is not an event that fades, it is a STATE - the radar
        // is showing you traffic from a source you did not choose, and that
        // stays true until the preferred one comes back. It outranks the
        // weather line below because a reader who does not know WHERE the data
        // comes from cannot judge anything else on the screen.
        built = String(sce::T("source ", "source ")) + apiServing +
                sce::T(" (fallback)", " (repli)");
        msg = built.c_str();
        col = TH->alert;
        st  = BANNER_STICKY;
    } else if (wxAlertMs && now - wxAlertMs < WX_SHOW_MS) {
        // WEATHER WORSENED. Ranked BELOW the two hold messages on purpose:
        // those answer a finger that is on the glass right now, and an answer
        // to a live gesture outranks a report about the last ten minutes. The
        // weather line comes back by itself on the next frame — it lasts five
        // minutes, the gesture lasts one and a half seconds.
        //
        // `keep` STAYS FALSE, unlike the hold messages, and that is the whole
        // difference between a 1.6 s message and a 5 min one (review 08-01).
        // `keep` makes the caller set uiDirty, i.e. redraw on the NEXT loop
        // pass — a full-screen PSRAM pushSprite plus this String, at the ~100 Hz
        // of loop(). For 1.6 s that is the price of an animation-free message;
        // for five minutes it would be ~30 000 full redraws of a line that
        // never changes, on the very screen whose 8 fps permanent redraw was
        // removed for costing ~25 % of the loop.
        // fetchMetar sets uiDirty when it raises the alert, so the line APPEARS
        // at once; `bannerUntilMs` below arms the ONE redraw that takes it away
        // on the exact millisecond it dies (08-03). Before that it was the 1 s
        // counter tick that happened to wipe it — "within a second", which is
        // visible when the bar covers the whole band.
        //
        // wxAlertMs is NEVER cleared here either — the window test above is
        // enough, and a clear from loop() would race netTask raising a new
        // alert, losing it. Unsigned arithmetic makes the test survive the
        // millis() rollover.
        const uint8_t pr = wxAlertPair;          // ONE read of the byte
        const int o = (pr >> 4) - 1, n = (pr & 15) - 1;
        if (o >= 0 && o < 4 && n >= 0 && n < 4) {
            built = String(sce::T("weather worse: ", "meteo degradee : ")) +
                    FLT_CATS[o] + " > " + FLT_CATS[n];
            msg = built.c_str();
            col = TH->alert;
            st  = BANNER_STICKY;   // drawn, but the caller must NOT redraw for it
            bannerUntilMs = wxAlertMs + WX_SHOW_MS;    // wipe on that date
        }
    }
    // Nothing on screen: no wipe to schedule. Clearing it here also keeps a
    // date from surviving the message it belonged to — a banner replaced by a
    // higher-ranked one would otherwise leave the loser's deadline armed.
    if (st == BANNER_NONE) bannerUntilMs = 0;
    return st;
}

// A BANNER IS AN INTERRUPTION, NOT A CAPTION — so it is painted as a FILLED
// BAR in the message's own colour with BLACK text, centred, instead of coloured
// text on the black ground everything else uses (user 08-03).
//
// That is the whole point: the screens are dense, every value on them is
// already colour-coded, and one more coloured line among forty was reading as
// data rather than as an event. Inverting the ground is the one move that
// cannot be mistaken for a caption. The colour still carries the KIND — accent
// for "your gesture was heard", alert for "the weather turned" — it just
// carries it as a ground now. Black-on-accent and black-on-alert are checked
// for all four themes by `scripts/gates/check-contrast.py`.
//
// Kept SEPARATE from the drawString on purpose: A2.22 pins ONE drawString per
// drawing symbol and `check-a222.py` counts them per symbol, so folding the
// text call into a shared helper would EMPTY the two symbols the gate watches —
// silencing the check instead of satisfying it. What is genuinely shared is the
// ground and the ink, and that is all this takes.
static void bannerGround(uint16_t col, int x, int y, int w, int h) {
    canvas.fillRect(x, y, w, h, col);
    canvas.setTextColor(TFT_BLACK);
}

// The READING screens' banner: the dedicated raw band.
// ONE drawString call site — written as two calls in two branches, GCC emitted
// only one, which may be a tail merge and may be A2.22 dropping the second,
// and this banner exists precisely so the user is not left guessing whether
// something happened.
//
// IT COVERS THE BAND WHOLE (08-03, user), all 24 px of it, and centres the text
// on BOTH axes. A first attempt made the bar exactly one 8 px text line, which
// looked like a bug and was one: the METAR writes up to THREE raw lines in this
// band, so a one-line bar left the other two standing beside the banner. They
// read as leftover pixels the banner had failed to clear — and the fix is not a
// clear, it is covering the band the banner is claiming.
static BannerState viewHoldBanner() {
    String      built;
    const char* msg;
    uint16_t    col;
    const BannerState st = bannerPick(built, msg, col);
    if (msg) {
        canvas.setFont(&fonts::Font0);
        canvas.setTextSize(1);
        bannerGround(col, VIEW_RULE_L, VIEW_RAW_Y,
                     VIEW_RULE_R - VIEW_RULE_L, VIEW_RAW_H);
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.drawString(msg, (VIEW_RULE_L + VIEW_RULE_R) / 2,
                          VIEW_RAW_Y + VIEW_RAW_H / 2);
        canvas.setTextDatum(textdatum_t::top_left);   // the screens assume it
    }
    return st;
}

// Wraps `src` into `lines` on SPACES, at most `w` glyphs and `maxl` lines.
// Shared by the TAF and by whatever the NOTAM will one day receive: cutting
// mid-token is what turns "13010KT" into two unreadable halves, and every
// screen that shows wire text needs the same guarantee.
// `brk` (may be null) lists prefixes that must START a new line.
// `complete` (may be null) reports whether the WHOLE source fitted: a caller
// that picks a text size from the answer must be able to tell "it all fits" from
// "it was cut off at maxl".
static int viewWrap(const char* src, char (*lines)[64], int w, int maxl,
                    const char* const* brk, int nbrk, bool* complete = nullptr) {
    int nl = 0;
    const char* p = src;
    while (*p && nl < maxl) {
        int n = (int)strlen(p);
        for (int i = 1; i < n && i <= w && brk; i++) {
            if (p[i - 1] != ' ') continue;
            // ONE aeronautical rule lives in this generic wrapper, and it earns
            // its place: "PROB40 TEMPO 3106/3109" is a SINGLE group — the
            // probability qualifies the change that follows it. Breaking
            // between the two leaves "PROB40" alone on a line and turns one
            // conditional into what looks like two.
            if (i >= 7 && !strncmp(p + i - 7, "PROB", 4) &&
                p[i - 3] >= '0' && p[i - 3] <= '9' &&
                p[i - 2] >= '0' && p[i - 2] <= '9') continue;
            for (int b = 0; b < nbrk; b++)
                if (!strncmp(p + i, brk[b], strlen(brk[b]))) { n = i - 1; i = w + 1; break; }
        }
        if (n > w) {
            n = w;
            while (n > 0 && p[n] != ' ') n--;
            if (!n) n = w;                 // no space in reach: hard cut
        }
        // A break prefix sitting at the very start (leading space + BECMG)
        // would compute n = 0: an empty line that consumes NOTHING, i.e. maxl
        // blank rows and the text never shown. Always eat at least one line's
        // worth.
        if (n <= 0) n = (int)strlen(p) > w ? w : (int)strlen(p);
        if (n > 63) n = 63;
        memcpy(lines[nl], p, (size_t)n);
        lines[nl][n] = '\0';
        nl++;
        p += n;
        while (*p == ' ') p++;
    }
    if (complete) *complete = (*p == '\0');
    return nl;
}

// Same wrap, ONE PAGE at a time. Counts every line the text produces but only
// STORES the window asked for, so a 1700-character NOTAM costs `maxl` lines of
// buffer instead of thirty-six. That is what lets the NOTAM view show the
// whole text on a 320x240 screen without a scrollbar and without a second
// gesture: the deck already pages horizontally, so a long NOTAM is simply
// several cards.
// Returns the number of lines written into `lines`; `nPages` gets the total.
// `lines` MAY BE NULL: that is the COUNT-ONLY mode, and it is the caller that
// only wants `nPages`. It exists because the alternative was worse — the page
// counter is printed ABOVE the text block, so it has to be measured before the
// real line buffer is even in scope, and the first version passed a `char[1][64]`
// with the real `maxl`. `maxl` is 14, so the window wrote lines 0..13 into a
// one-line array: 832 bytes down loop()'s stack, on the first NOTAM longer than
// one line (review 08-01). A count that stores nothing cannot be given a buffer
// too small for it.
static int viewWrapPage(const char* src, char (*lines)[64], int w, int maxl,
                        int page, int* nPages) {
    if (maxl < 1) { if (nPages) *nPages = 1; return 0; }
    const int first = page * maxl, last = first + maxl;
    int total = 0, out = 0;
    const char* p = src;
    char scratch[64];
    while (*p) {
        // A NEWLINE IS A HARD BREAK (08-03). Measured FIRST, before the width:
        // a line the author ended at 12 characters ends there, and only a line
        // that runs past `w` is re-wrapped on its spaces. Without this the two
        // rules fight and the author's break is the one that loses.
        const char* nl = strchr(p, '\n');
        int avail = nl ? (int)(nl - p) : (int)strlen(p);
        int n = avail;
        if (n > w) {
            n = w;
            while (n > 0 && p[n] != ' ') n--;
            if (!n) n = w;
        }
        if (n <= 0) n = avail > w ? w : avail;
        if (n > 63) n = 63;
        const bool keep = lines && total >= first && total < last;
        char* dst = keep ? lines[out] : scratch;
        memcpy(dst, p, (size_t)n);
        dst[n] = '\0';
        if (keep) out++;
        total++;
        p += n;
        // Spaces are swallowed (they were the break point); a NEWLINE is
        // consumed exactly once - and an EMPTY line is kept, because a blank
        // row is how item E separates two blocks and dropping it would glue
        // them back together.
        while (*p == ' ') p++;
        if (*p == '\n') p++;
    }
    if (nPages) *nPages = total ? (total + maxl - 1) / maxl : 1;
    return out;
}

// NOINLINE like its two siblings: `draw()` is already a 15 KB body, and
// A2.22 (GCC 8.4 Xtensa dropping the second of two similar drawing calls)
// gets more likely the more of them share one body. One screen, one body.
__attribute__((noinline))
static void drawMetar() {
    xSemaphoreTake(gMtx, portMAX_DELAY);   // metar written by netTask
    Metar m = metar;                       // snapshot: the drawing below is
    xSemaphoreGive(gMtx);                  // long (fonts, rose, margins)
    // The whole geometry hangs off these five numbers.
    // The rose GREW when the footer went (user 07-31): the arrow ring — not the
    // disc — is what has to fit, so the band 24..212 gives a ring of 94 and a
    // radius of 82 (94 = 82 + 12, the arrow living at r+2..r+12). Centre moves
    // down with it. 78 -> 82 is 5 % more rose for pixels that were a legend.
    const int RCX = 160, RCY = 118, RR = 82;   // rose centre / radius
    // MARGIN ANCHORS = THE SHARED GRID (layout audit 08-01). They were 4 and
    // 316, i.e. 4 px OUTSIDE the rule at 8..311 that the header and the raw
    // band align on: the data columns overhung the very line that frames them.
    // What it costs, and it is stated rather than hidden: the 60 px column now
    // inks x 8..68 and x 252..312, while the wind-arrow ring reaches x 66 /
    // x 254 — so a full-width 10-glyph row can graze the arrow by 2 px, and
    // only at the one azimuth where the arrow sits due east or due west, at
    // y 111..125. Alignment with the grid is worth more than 2 px of air at a
    // single bearing.
    const int LX = VIEW_RULE_L, RX = VIEW_RULE_R;
    canvas.fillSprite(TFT_BLACK);
    char nm[64] = "";
    if (m.valid && m.name[0]) metarName(m.name, nm, sizeof(nm), 40);
    // No label: the rose IS the METAR's name (user 07-31), and the name of the
    // station gets the five characters back.
    viewHeader(m.valid ? m.icao : "----", nm, nullptr);

    if (!m.valid) {
        // Explicit reason, never an empty screen: the three failures have
        // three different remedies (set the station / wait / check the net).
        canvas.setFont(&fonts::efontJA_12);
        // No setTextColor here: the row table below carries a colour per row
        // and sets it at the single call site. Leaving one behind would be a
        // second place that decides what the first line looks like.
        const char* why;
        // ASK THE SAME QUESTION netTask ASKS, and ask it FIRST.
        // `metarHttp == HTTP_NO_STATION` was unreachable in the very case it
        // was written for: netTask gates the fetch behind `metarStationSet()`,
        // so with no station configured `fetchMetar` never runs, `metarHttp`
        // stays 0, and this fell through to "loading METAR..." \u2014 for ever.
        // The screen claimed a request was in flight while nothing would ever
        // be sent, and the one message naming the remedy could not appear.
        // Invisible on a CoreS3, whose config.yaml always names a station;
        // it is the DEFAULT state of a board with no SD card (Fire, 08-02).
        // A derived reason must come from the same source of truth as the
        // decision it describes, not from the residue of an attempt.
        if      (!metarStationSet() || metarHttp == HTTP_NO_STATION)
            why = sce::T("no station: set metar_icao in /config",
                         "aucune station : r\u00e9glez metar_icao dans /config");
        else if (WiFi.status() != WL_CONNECTED)
            why = sce::T("WiFi disconnected...",  // ≠ a service failure
                         "WiFi d\u00e9connect\u00e9...");
        else if (metarHttp)  why = sce::T("METAR unavailable", "METAR indisponible");
        else                 why = sce::T("loading METAR...", "chargement du METAR...");
        // ONE origin for the first body line of the three reading screens
        // (layout audit 08-01): this one started at y 100, the TAF's at 104 and
        // the NOTAM's at VIEW_BODY_Y + 8. Same kind of content — a one-line
        // explanation in efontJA_12 — so it now has one y, the NOTAM's.
        //
        // TWO ROWS, ONE CALL SITE (A2.22, fix 08-25). The reason and its HTTP
        // code used to be two `drawString` calls in this one body, differing
        // only in their string, their colour and eighteen pixels of y — which
        // is the exact shape GCC is entitled to thin out. It did: lowering
        // CORE_DEBUG_LEVEL from 3 to 1 changed the inlining budget enough that
        // the ESP32 backend emitted only ONE of the two, and the HTTP line
        // vanished from the Fire's binary. Nothing said so — the gate found it,
        // the screen would simply have been missing the number that names the
        // failure, on the board whose usual failure IS a bad fetch.
        //
        // The table makes the pair impossible to reconstruct: two similar
        // calls cannot be thinned into one when there is only one to begin
        // with. Adding a third row is adding a row. Note it survives the flag
        // that exposed the bug rather than depending on it — a shape that only
        // holds at one optimisation setting is not a fix.
        struct FailRow { const char* s; uint16_t c; int16_t dy; };
        FailRow rows[2];
        int nRows = 0;
        rows[nRows++] = { why, TH->alert, 8 };
        String httpLine;                       // outlives the loop below
        if (metarHttp && metarHttp != HTTP_NO_STATION) {
            httpLine = "HTTP " + String(metarHttp);
            rows[nRows++] = { httpLine.c_str(), TH->hint, 26 };
        }
        for (int i = 0; i < nRows; i++) {
            canvas.setTextColor(rows[i].c);
            canvas.drawString(rows[i].s, VIEW_RULE_L, VIEW_BODY_Y + rows[i].dy);
        }
        // The old "swipe down to go back" line is gone: down now belongs to
        // SceGuest at every step, so that hint pointed at the wrong gesture.
        // The shared footer says what the UP swipe gives, like everywhere else.
        // The long press speaks here, LAST, over whatever the screen drew. Keeping
    // the screen dirty while it shows is what makes it expire on its own.
    if (viewHoldBanner() == BANNER_HOLD) uiDirty = true;
    canvas.pushSprite(0, 0);
        return;
    }

    // ---- THE ROSE. ORDER IS THE WHOLE DESIGN, each layer answering the one
    // under it (user 07-31):
    //   runway fill+edges the GROUND. It went under the rose the day the fill
    //                     grew to full length: a filled strip drawn on top
    //                     swallowed whichever degree labels it crossed, and by
    //                     construction it crosses them whenever the runway
    //                     points at a thirty. Underneath, it costs nothing —
    //                     the scale is a transparent overlay, so putting the
    //                     terrain below it is what lets BOTH be read.
    //   dashed centreline over the fill, so the marking runs the LENGTH of the
    //                     runway; drawn under it, it stopped exactly where the
    //                     runway began, the opposite of a road
    //   rose              graduations and labels ON TOP of the strip: nothing
    //                     of the scale is ever hidden. (Until 07-31 it was the
    //                     first layer, on the argument that the runway is read
    //                     AGAINST it — true, and that is exactly why it must
    //                     survive the runway.)
    //   runway NUMBERS    over everything so far: a number cut by a marking or
    //                     a graduation is the one ambiguity this card cannot
    //                     afford — it is the runway's identity
    //   wind trail+arrow  LAST: the wind is the measurement being compared TO
    //                     the runway, so it must never be what gets hidden
    // The runway comes from the typed setting if there is one, from the SD
    // base otherwise (runwaySpec) — resolved by loop(), never read from the
    // card here: drawing must not touch SPI2 behind the LCD's back.
    metarRunwayBar(RCX, RCY, RR, runwaySpec());    // fill + the two long edges
    metarRunwayAxis(RCX, RCY, RR, runwaySpec());   // dashes OVER the fill
    metarRose(RCX, RCY, RR);                       // scale OVER the strip
    metarRunwayNums(RCX, RCY, RR, runwaySpec());   // numbers OVER everything
    // No arrow — and no figures on the rim — for a variable direction (wdir
    // < 0, the API's "VRB" string) nor for a calm wind: an arrow would claim
    // an azimuth nobody measured. Those two cases fall back to a plain VENT
    // pair in the left margin, below.
    const bool windOnRose = (m.wdir >= 0 && m.wspd > 0.5f);
    if (windOnRose) {
        metarWindTrail(RCX, RCY, RR, m.wdir);     // under the arrow's figures
        metarWindArrow(RCX, RCY, RR, m.wdir);
    }

    // ---- MARGINS. Everything outside the disc is built as a FLAT list of
    // strings and drawn from ONE drawString call site: A2.22 — GCC 8.4 Xtensa
    // drops the SECOND of two similar drawing calls in the same body, and a
    // "label call then value call" pair inside a loop is exactly that shape.
    // Nothing is emitted for a datum that does not exist, and the running y
    // closes the gap: a station without a dew point simply has one row fewer,
    // never a hole and never a fabricated zero.
    // ONE COLUMN WIDTH for BOTH sides (user 07-30): the two margins must LOOK
    // the same, so it is the TEXT that adapts, never the column. 60 px is 10
    // glyphs of the 6x8 font — and exactly 5 glyphs at size 2, which is why
    // the size-2 rule below is that same budget expressed twice. Anything
    // longer is ABBREVIATED at the source, and CLAMPED here as well so no API
    // string can ever run into the rose.
    //
    // THE CLEARANCE, RE-DERIVED (user 08-01) — and the numbers that used to sit
    // here were the reason it had to be. They read "left inks x 4..62, right
    // x 256..314, arrow ring x 70..250: 6 px of clear air each side", which was
    // true of the 4/316 anchors. Two later changes each kept their own books:
    // the rose grew 78 -> 82 (07-31) and the margins moved onto the shared grid
    // at 8/312 (layout audit 08-01). Neither re-derived this line, so it went on
    // asserting a margin that no longer existed.
    //   full-width size-2 value, right : x 253..312   (RX - 5*12 + 1)
    //   full-width size-2 value, left  : x   8..67
    //   arrow ring, outer               : RCX +- (RR + 12) = x 66 and x 254
    // So a 5-glyph size-2 value TOUCHES the ring by 2 px on either side, at the
    // one azimuth that puts the arrow due east or due west (y 111..125).
    // FIXED AT THE SOURCE rather than by shrinking the rose the user asked to
    // grow: the only such value was the visibility, and its unit now rides on
    // its label (metarVisib), which takes it to 2 glyphs. Nothing in either
    // column reaches 5 glyphs at size 2 today.
    // What is STILL known to graze, and is left standing because it is a
    // deliberate spill (`wide = 7`) rather than an oversight: the wind speed row
    // may reach x 229 at y 28..66, where an arrow on a NE/NW bearing can pass.
    // Say it here rather than let the next reader re-measure it.
    const int COLW = 60;
    // BOTTOM of both margins = the bottom of the rose band, 212. It was 204 on
    // the left and nothing at all on the right (layout audit 08-01): the left
    // column threw away a cloud layer it had room for, and the right one could
    // push its last row into the raw report at y 216.
    const int COLBOT = 212;
    // THE CELLS ARE DEFERRED, and into the SHARED table (`firmware/common/
    // CellText.h`) rather than a local one. This card carried its own copy of
    // that class until it was retired: a `struct Cell` holding a `String`, 28 of
    // them on loopTask's stack, and a drop that said nothing once the array was
    // full — the silent truncation the shared table exists to name. The two
    // things the copy had that the shared one lacked, a text DATUM and a
    // background colour, now live there, so the extras came across instead of
    // justifying the copy.
    gCells.reset();
    // `wide` (glyphs) overrides the column budget for the ONE row that is
    // allowed to spill past it — see the wind, below.
    auto put = [&](int x, int y, textdatum_t d, uint8_t sz, uint16_t fg,
                   uint16_t bg, const String& v, int wide = 0) {
        if (!v.length()) return;
        String s = v;
        const int maxGlyph = wide ? wide : COLW / (6 * sz);   // 10 at 1, 5 at 2
        // CLAMPED TO THE COLUMN, here and not in the table: this is a layout
        // budget, not a buffer limit. A value too long for its column must lose
        // its tail rather than run over the rose next to it.
        if ((int)s.length() > maxGlyph) s = s.substring(0, maxGlyph);
        gCells.put(x, y, fg, s.c_str(), sz, d, bg);
    };
    // A label + its value, stacked. The value takes size 2 when it fits the
    // column at that size (5 glyphs), size 1 otherwise — one rule, so "1017"
    // and "66%" read large while a longer one stays legible instead of running
    // over. Returns the height consumed so the caller can keep a running y.
    //
    // WHERE A UNIT LIVES (user 08-01, asking whether "SM" was a unit or a
    // reading — it is a unit, and the answer is worth writing down because the
    // card had THREE habits and no rule).
    // A unit is not a datum: it does not vary with the weather, only with
    // `cfg.metric`. So it belongs with the LABEL, which is the other thing on
    // the row that names rather than reports. It is chrome, and it wears the
    // chrome colour.
    //   - WORD units (hPa, ft, SM, km, min) ride on the LABEL, stated once:
    //     "QNH hPa" + "1017". They are 2-4 glyphs, and on the value they would
    //     eat the size-2 budget and shrink the very figure the row exists for.
    //   - SYMBOL units (C, %) stay GLUED to the figure: "27C", "66%". One
    //     glyph, read as part of the number, and detaching them would be odd
    //     typography for no gain.
    //   - THE WIND is the stated exception, both on the rose ("VENT 080" +
    //     "16kt") and in the margin ("VENT VRB" + "185km/h"): its label already
    //     carries a DATUM — the direction — so it is the one label on the card
    //     drawn in the accent rather than the hint grey, and "VENT 080 km/h"
    //     would be 13 glyphs against the column's 10.
    // The unit is NOT differentiated from the label by brightness, and that is
    // deliberate: the Scope Night theme has only THREE text levels by
    // construction (pure red tops out at 5.25:1 — see THEMES), and a hierarchy
    // that vanishes in one of four themes is not a hierarchy. Both are chrome;
    // what must never blur is chrome against DATUM, and that line is carried by
    // size 2 + txtMain against size 1 + hint.
    //
    // A SIDE EFFECT WORTH KEEPING: with the units off the value line, no size-2
    // value in either margin now reaches 5 glyphs while an arrow exists — and 5
    // glyphs was exactly the width that grazed the arrow ring by 2 px. The
    // French "CALME" is 5, but it can only appear when the wind is VRB or calm,
    // i.e. precisely when `windOnRose` is false and no arrow is drawn at all.
    // The graze is closed by construction rather than by margin.
    auto pair = [&](int x, int y, textdatum_t d, const String& label,
                    const String& v, uint16_t col) -> int {
        if (!v.length()) return 0;
        const uint8_t sz = (v.length() <= 5) ? 2 : 1;
        // The SYMMETRIC clamp of the cloud loop below, and the reason it lives
        // here: every row of BOTH margins is built by this lambda, so one test
        // bounds both. A row that does not fit is dropped whole — a label with
        // its value half over the raw report is worse than one row fewer.
        if (y + 10 + (sz == 2 ? 16 : 8) > COLBOT) return 0;
        put(x, y, d, 1, TH->hint, TFT_BLACK, label);
        put(x, y + 10, d, sz, col, TFT_BLACK, v);
        return (sz == 2) ? 34 : 26;
    };

    // LEFT margin — the "what it feels like" side.
    int ly = 28;
    // fltCat chip FIRST: it is the datum that reads at a glance, hence the top
    // of the column and the only rounded plate left in the view. Drawn ONLY
    // when the station publishes a category — FMEE regularly omits `fltCat`
    // (checked live 07-30) and a grey chip would invent one.
    //
    // BY DAY the international code carries it (VFR green, MVFR blue, IFR red,
    // LIFR magenta), taken RAW and not through the theme.
    //
    // BY NIGHT THE HUE IS ABANDONED, and that is a decision, not an oversight
    // (user 08-01). Two measurements forced it:
    //   - Scope night's accent IS 0xF800, the exact colour of the IFR chip: the
    //     chip stopped being a category and became the interface;
    //   - Gundam night holds BLUE AT ZERO — that is what preserves dark
    //     adaptation, and it is written as a hard constraint on the palette.
    //     MVFR (blue) and LIFR (magenta, blue at 31) do not just glare, they
    //     break the rule the theme is built on.
    // Neither night palette owns four distinct hues, so insisting means either
    // leaving the theme or ruining night vision. The rank moves to WEIGHT
    // instead — outline, dim fill, solid, solid + inner frame — which both
    // palettes can express, stays monotonic (heavier = more constrained), and
    // leaves the three letters doing what they already did.
    // THE COST, stated: under a night theme the international colour code is
    // GONE. Someone who reads by colour must know it no longer applies here,
    // rather than read a red plate as IFR when everything is red.
    // ALWAYS DRAWN, even with nothing to say (user 08-03: "sometimes there is
    // no indication where VFR should be - is that normal?"). Half normal: the
    // category is NOT computed here, it arrives as `fltCat` in the NOAA
    // response, and deriving it ourselves would mean contradicting the official
    // source on a safety-relevant call from a rounded visibility. NOAA omits it
    // often - a comment in fetchMetar already noted "FMEE regularly omits
    // fltCat", i.e. this very station.
    // The other half was a defect: the row was SKIPPED, so the 34 px vanished
    // and everything below moved up. The reader saw a different screen, with no
    // way to tell "the source said nothing" from "the display forgot". Same
    // rule this project applies to a truncation: silence reads as completeness.
    // `fltCatColor` already returns grey for the unknown - the chip could say
    // "I do not know" and was never asked to.
    {
        const int ti = (int)(TH - THEMES);      // odd index = a night theme
        metarCatChip(LX, ly, COLW, m.cat[0] ? m.cat : "--", (ti & 1) != 0);
        ly += 34;
    }
    if (m.hasTemp)
        ly += pair(LX, ly, textdatum_t::top_left, "TEMP",
                   String((int)lroundf(m.temp)) + "C", TH->txtMain);
    if (m.hasDewp)
        ly += pair(LX, ly, textdatum_t::top_left, sce::T("DEW PT", "ROSEE"),
                   String((int)lroundf(m.dewp)) + "C", TH->txtMain);
    if (!windOnRose && m.wspd >= 0.0f) {
        // The rose could not carry it: variable direction or calm. The
        // reported direction is where the wind comes FROM, and "VRB" is a
        // STRING in this API — read as a number it would have printed a
        // confident 000, i.e. north.
        // "VRB" rides on the LABEL, not on the value: "VRB 185 km/h" is 12
        // glyphs and would have been clipped, "VENT VRB" + "185km/h" is 8 and
        // 7 and says the same thing inside the column.
        const bool calm = (m.wspd < 0.5f);
        ly += pair(LX, ly, textdatum_t::top_left,
                   calm ? sce::T("WIND", "VENT") : sce::T("WIND VRB", "VENT VRB"),
                   calm ? String(sce::T("CALM", "CALME"))
                        : String((int)lroundf(dS(m.wspd))) + uS(),
                   TH->txtMain);
    }
    // CLOUDS: the code line (cover + base, feet -> metres in metric mode) and,
    // UNDER it, its plain-French gloss — plus the gloss of the type suffix
    // when the layer carries one. A pilot reads "BKN 2500", a curious reader
    // reads "fragmente" (user 07-30). The summary field ("CAVOK", "CLR") gets
    // the same treatment when the report has no layer at all.
    {
        String   ln[8];
        uint16_t lc[8];
        int      n = 0;
        auto add = [&](const char* s, uint16_t col) {
            if (n < 8 && s && s[0]) { ln[n] = s; lc[n] = col; n++; }
        };
        bool anyBase = false;
        for (int i = 0; i < m.nLayers; i++) {
            // The UNIT rides on the label ("NUAGES ft"), not on every line:
            // "SCT 10000ft" is 11 glyphs and would have been clipped, while
            // "SCT 10000" is 9 and the unit is stated once, above.
            String code = m.layerCov[i];
            if (m.layerBaseFt[i] >= 0) {
                code += " " + String((int)dA((float)m.layerBaseFt[i]));
                anyBase = true;
            }
            if (n < 8) { ln[n] = code; lc[n] = TH->txt1; n++; }
            add(cloudGloss(m.layerCov[i]),     TH->hint);
            add(cloudTypeGloss(m.layerCov[i]), TH->hint);
        }
        if (!n && m.cover[0]) {
            add(m.cover, TH->txt1);
            add(cloudGloss(m.cover), TH->hint);
        }
        if (n) {
            put(LX, ly, textdatum_t::top_left, 1, TH->hint, TFT_BLACK,
                anyBase ? String(sce::T("CLOUDS ", "NUAGES ")) + uA()
                        : String(sce::T("CLOUDS", "NUAGES")));
            // HARD BOUND: the margin stops where the rose band does (COLBOT).
            // Lines that do not fit are dropped rather than pushed onto the
            // raw report — and METAR orders its layers by INCREASING base, so
            // what falls off is always the highest, least significant one.
            for (int i = 0; i < n; i++) {
                const int y = ly + 10 + i * 10;
                if (y + 8 > COLBOT) break;
                put(LX, y, textdatum_t::top_left, 1, lc[i], TFT_BLACK, ln[i]);
            }
            ly += 10 + n * 10 + 4;
        }
    }

    // FREQUENCIES, at the FOOT of the column and last in it. They are the one
    // thing on this card you act on rather than read: a field's ATIS and tower
    // do not change between two weather refreshes, so they are fetched once
    // per station and cached (fetchStationInfo).
    // LAST on purpose, and bounded by the same COLBOT the clouds obey: on a
    // station reporting several layers there is no room, and a frequency is
    // the row that can wait — the weather is why this screen exists. Nothing
    // is half-drawn: a row that does not fit is not drawn at all.
    // ONE `put` call site fed by a flat table (A2.22).
    {
        StationInfo si;
        xSemaphoreTake(gMtx, portMAX_DELAY);
        si = stnInfo;
        xSemaphoreGive(gMtx);
        const char* FL[2] = { "ATIS", "TWR" };
        const char* FV[2] = { si.atis, si.twr };
        for (int i = 0; i < 2; i++) {
            if (!FV[i][0] || ly + 18 > COLBOT) continue;
            put(LX, ly,     textdatum_t::top_left, 1, TH->hint,    TFT_BLACK,
                String(FL[i]));
            put(LX, ly + 9, textdatum_t::top_left, 1, TH->txtMain, TFT_BLACK,
                String(FV[i]));
            ly += 21;
        }
    }

    // RIGHT margin — the "numbers a pilot reads out" side, flush right.
    int ry = 28;
    // WIND FIRST, and in the ACCENT (user 07-31). Its two figures used to ride
    // on the rose itself, on the arrow's own radial: that put them where the
    // eye already was, but it also punched two opaque black holes through the
    // graduations and moved them to a different place at every observation —
    // the rose could not be read as a single object. Out here they hold still,
    // and the accent is the ECHO: the arrow and its figures are the same
    // reading, said twice, so the colour is what ties them across the gap.
    // The DIRECTION rides on the LABEL, exactly as "VENT VRB" already does
    // below: "080 16kt" as one value is 8 glyphs and drops to size 1, while
    // "VENT 080" + "16kt" keeps the speed large and says the same thing.
    if (windOnRose) {
        char wl[12];
        snprintf(wl, sizeof(wl), sce::T("WIND %03d", "VENT %03d"), m.wdir);
        // Not built with pair(): the wind is the ONE row that departs from the
        // column rule, twice over.
        //   - The LABEL is in the ACCENT, not the hint grey. Everywhere else a
        //     label names a number; here it CARRIES one (the direction), and a
        //     figure whispered in grey next to the same figure shouted in
        //     colour reads as two different data. Same colour, one reading.
        //   - The VALUE keeps size 2 up to 7 glyphs instead of 5, so
        //     "185km/h" stays as large as "1017" and "66%" — the wind must not
        //     be the small one on a card whose whole subject is the wind.
        //     Spilling ~24 px past the 60 px column is safe HERE and nowhere
        //     else: the arrow ring only reaches x 254 for a wind from due east,
        //     i.e. at y 118, while this row lives at y 28..66.
        put(RX, ry, textdatum_t::top_right, 1, TH->accent, TFT_BLACK, wl);
        put(RX, ry + 12, textdatum_t::top_right, 2, TH->accent, TFT_BLACK,
            String((int)lroundf(dS(m.wspd))) + uS(), 7);
        ry += 40;                       // taller than a pair(): it leads here
    }
    if (m.hasAltim)
        ry += pair(RX, ry, textdatum_t::top_right, "QNH hPa",
                   String((int)lroundf(m.altim)), TH->txtMain);
    if (m.visib[0]) {
        // "VISIB." is the same word both ways; the UNIT is appended to it, so
        // the figure keeps the whole size-2 budget to itself. 9 glyphs at size 1
        // ("VISIB. SM") against the 10 the column allows.
        String vu;
        const String vv = metarVisib(m.visib, &vu);
        ry += pair(RX, ry, textdatum_t::top_right,
                   vu.length() ? "VISIB. " + vu : "VISIB.", vv, TH->txtMain);
    }
    const int rh = metarRh(m);                    // DERIVED (Magnus), not read
    if (rh >= 0)
        ry += pair(RX, ry, textdatum_t::top_right, "HUM.", String(rh) + "%",
                   TH->txtMain);
    // AGE of the observation: a METAR is valid for an hour, and a stale one is
    // the trap of every weather display. Needs NTP — without it we say so
    // rather than printing an age counted from 1970.
    {
        time_t nowT = time(nullptr);
        if (m.obsTime && sce::clockSynced(nowT)) {
            long mn = (long)((nowT - (time_t)m.obsTime) / 60);
            if (mn < 0) mn = 0;                   // clock drift, not a future
            // "min" JOINS THE LABEL, like every other word unit on this card
            // (see the unit rule above pair()). It was the last row still
            // spelling its unit on the value line, and the last one written
            // with a SPACE where the others glue — three forms for one thing.
            // The figure gains by it: "12" is 2 glyphs and reads at size 2,
            // where "12 min" was 6 and fell to size 1. The age of an
            // observation is the datum this row exists for; it should not be
            // the small one.
            // "AGE" and not "OBSERVED": the row already reports an age, and
            // "OBSERVED min" is 12 glyphs against the column's 10. Same word
            // both ways, like VISIB.
            // BOUNDED AT BOTH ENDS, and the top end is not cosmetic (review
            // 08-01): the invariant written above pair() says no size-2 value
            // reaches 5 glyphs, and 5 glyphs is the width that grazes the arrow
            // ring. `mn` was clamped only at zero, so a station that stops
            // reporting for a week — or an obsTime the API serves stale — gives
            // 10080 minutes, five glyphs, and falsifies the invariant in the
            // commit that states it. "999+" is 4 glyphs and says more than a
            // seven-digit count: a METAR is valid for ONE hour, so past a day
            // the exact figure carries nothing the alert colour has not already
            // said.
            ry += pair(RX, ry, textdatum_t::top_right, "AGE min",
                       mn > 999 ? String("999+") : String(mn),
                       mn > 90 ? TH->alert : TH->txt1);
        } else {
            // No unit on the label here: there is no figure to qualify.
            ry += pair(RX, ry, textdatum_t::top_right, "AGE",
                       sce::T("NTP sync", "sync NTP"), TH->hint);
        }
    }

    canvas.setFont(&fonts::Font0);
    gCells.flush(canvas);   // THE one call site (A2.22) — it restores the datum

    // RAW report in the shared raw band: the only line a pilot really needs,
    // shown as received. Same wrapper as the TAF (viewWrap) — one guarantee
    // written once, that a line never breaks inside a group.
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    canvas.setTextColor(TH->txt1);
    {
        // THREE lines, not two (layout audit 08-01). `Metar::raw` is
        // char[144], i.e. up to 143 characters, and 2 x 50 showed 100 of them:
        // up to 43 characters of the report were dropped with nothing on
        // screen saying so — on the one string here that is meant to be read
        // verbatim. 3 x 50 = 150 covers the buffer whole.
        // The PITCH drops from 11 to 8, which is the font's own cell height:
        // the band cannot start any higher (the rose ends at 212, the band at
        // 216) and 216 + 3 x 8 puts the last row on 232..239 — the last pixel
        // of the screen, and the only arithmetic that fits three lines without
        // touching the rose.
        char lines[3][64];
        const int nl = viewWrap(m.raw, lines, 50, 3, nullptr, 0);
        for (int i = 0; i < nl; i++)
            canvas.drawString(lines[i], VIEW_RULE_L, VIEW_RAW_Y + i * 8);
    }
    // The long press speaks here, LAST, over whatever the screen drew. Keeping
    // the screen dirty while it shows is what makes it expire on its own.
    if (viewHoldBanner() == BANNER_HOLD) uiDirty = true;
    canvas.pushSprite(0, 0);
}

// --------------------------------------------------------- TAF (level 2)
// The FORECAST, one step above the observation. Free: it arrives in the same
// object as the METAR (`taf=1`), so this screen opens no connection of its own.
//
// SHOWN RAW, and that is a decision, not laziness. A TAF is a sequence of
// CONDITIONAL groups (BECMG, TEMPO, PROB40) whose validity periods overlap;
// glossing them into prose means choosing which condition to state, which is
// forecasting on the pilot's behalf. The METAR view can gloss because an
// observation has exactly one meaning. So: the groups are LAID OUT — one per
// line, wrapped on spaces — and left in the language they were issued in.
// The header line is the one thing decoded: the validity period, because
// "3106/0112" is the field that says whether this bulletin still applies.
__attribute__((noinline))
static void drawTaf() {
    xSemaphoreTake(gMtx, portMAX_DELAY);
    Metar m = metar;
    xSemaphoreGive(gMtx);
    canvas.fillSprite(TFT_BLACK);
    char nm[64] = "";
    if (m.valid && m.name[0]) metarName(m.name, nm, sizeof(nm), 40);
    viewHeader(m.valid ? m.icao : (cfg.metarIcao[0] ? cfg.metarIcao : "----"),
               nm, "TAF");

    if (!m.taf[0]) {
        // A station CAN have a METAR and no TAF: only ~1 aerodrome in 5 issues
        // one. Saying WHICH of the two is missing is the difference between
        // "wait" and "this station will never have one" — a distinction the
        // NOTAM screen makes the same way, in the same place, for the same
        // reason.
        canvas.setFont(&fonts::efontJA_12);
        canvas.setTextColor(m.valid ? TH->txt2 : TH->alert);
        // Same first-body-line origin as the METAR and the NOTAM
        // (VIEW_BODY_Y + 8) — it started at 104 (layout audit 08-01).
        // THREE states, not two: "no station at all" is neither "this station
        // has no TAF" nor "the METAR failed". Saying "METAR unavailable" when
        // nothing was ever requested sends you looking at the network, which
        // is the one place the problem is not — the remedy is a setting, and
        // it is the same one the METAR screen names (08-02, no-SD board).
        canvas.drawString(
            !metarStationSet()
                ? sce::T("no station: set metar_icao in /config",
                         "aucune station : réglez metar_icao dans /config")
            : m.valid
                ? sce::T("This station issues no TAF",
                         "Cette station ne publie pas de TAF")
                : sce::T("METAR unavailable: no TAF either",
                         "METAR indisponible : pas de TAF non plus"),
            VIEW_RULE_L, VIEW_BODY_Y + 8);
        // The long press speaks here, LAST, over whatever the screen drew. Keeping
    // the screen dirty while it shows is what makes it expire on its own.
    if (viewHoldBanner() == BANNER_HOLD) uiDirty = true;
    canvas.pushSprite(0, 0);
        return;
    }

    // The change groups start a NEW LINE: BECMG / TEMPO / PROB / FM are where
    // the forecast changes, so breaking there turns a wall of codes into a
    // TIMELINE — which is the only structure a TAF has and the only thing an
    // 8 px font can give it back.
    // The body runs the WHOLE height (header rule to footer), not the top
    // third it used to occupy: a TAF is the one screen here with enough text
    // to fill the card, and cutting it short to leave a tidy margin threw away
    // the tail of the forecast — the part furthest in the future.
    // THE VALIDITY, DECODED — the one field that says whether this bulletin
    // still applies, and the one nobody should have to read out of
    // "3106/0112" (day-hour / day-hour, UTC). Everything else stays raw: a TAF
    // is conditional and glossing it would mean forecasting for the reader.
    // The prefix it decodes ("TAF FMEE 310500Z 3106/0112") is then SKIPPED in
    // the body — the header already names the station and this line already
    // gives the period, so printing it a second time spent three of the
    // nineteen lines saying nothing new.
    const char* body = m.taf;
    // The bulletin's OWN validity, kept beyond the decoding block: the
    // prevailing-conditions line carries no DDHH/DDHH of its own (it lives in
    // the header, and the body deliberately skips it), so without this it
    // could never be marked as in force — and it is what IS in force whenever
    // no change group covers the moment, i.e. most of the time. The screen
    // then showed no bar at all and read as "nothing is in force" (user 08-02:
    // "il n'y a aucune barre active").
    int hdD1 = 0, hdH1 = 0, hdD2 = 0, hdH2 = 0;
    {
        char head[56] = "";
        int d1, h1, d2, h2, iss = -1, n1 = 0;
        const char* v = strstr(m.taf, "/");
        // "ddhh/ddhh" sits right after the issue time; find it by shape, not
        // by counting tokens (AMD / COR / CNL bulletins shift the fields).
        if (v && v > m.taf + 4 &&
            sscanf(v - 4, "%2d%2d/%2d%2d%n", &d1, &h1, &d2, &h2, &n1) == 4) {
            // The ISSUE time is kept with it: skipping the prefix would
            // otherwise throw away the one field that says how OLD this
            // forecast is, and a stale TAF read as current is the trap of
            // every weather display. It sits just before the period.
            char iss6[8] = "";
            const char* e = v - 5;                  // just before "ddhh/"
            while (e > m.taf && *e == ' ') e--;     // the separating space
            const char* q = e;
            while (q > m.taf && q[-1] != ' ') q--;  // start of that token
            if (e - q == 6 && *e == 'Z')            // ddhhmmZ, exactly
                snprintf(iss6, sizeof(iss6), "%.7s", q);
            snprintf(head, sizeof(head),
                     sce::T("%s%svalid %02d/%02dh -> %02d/%02dh Z",
                            "%s%svalide %02d/%02dh -> %02d/%02dh Z"),
                     iss6, iss6[0] ? "  " : "", d1, h1, d2, h2);
            body = v - 4 + n1;                  // skip what we just decoded
            while (*body == ' ') body++;
            hdD1 = d1; hdH1 = h1; hdD2 = d2; hdH2 = h2;
        }
        (void)iss;
        if (head[0]) {
            canvas.setFont(&fonts::Font0);
            canvas.setTextSize(1);
            canvas.setTextColor(TH->accent);
            // THE VALIDITY LINE SITS BETWEEN TWO RULES, and the three gaps
            // around it were 3 / 4 / 7 px — the forecast looked pushed away
            // from its own header (user 08-05). Font0 inks seven rows of its
            // eight-row cell, so with the header rule at 22 and the forecast
            // fixed at VIEW_BODY_Y + 20, dropping the text two rows and the
            // rule two with it lands on 5 / 5 / 5: even under the first rule,
            // above the second, and below it. Moving the TEXT alone cannot do
            // it — every pixel it gains above it loses below.
            canvas.drawString(head, VIEW_RULE_L, VIEW_BODY_Y + 2);
            canvas.drawFastHLine(VIEW_RULE_L, VIEW_BODY_Y + 14,
                                 VIEW_RULE_R - VIEW_RULE_L, TH->sep);
        }
    }
    static const char* const BRK[4] = { "BECMG", "TEMPO", "PROB", "FM" };
    // SIZE 2 WHEN THE FORECAST FITS AT SIZE 2, size 1 when it does not (user
    // 07-31). A TAF is read at arm's length off a desk, and 6x8 was small for
    // the one screen here made entirely of text; but a TAF has no maximum
    // length, and choosing the big size unconditionally would have silently
    // dropped the tail — the part furthest into the future, i.e. the part you
    // came for. So the size is DERIVED from whether the whole thing fits:
    // 25 glyphs x 11 lines at size 2, 50 x 19 at size 1 (25 x 12 px = 300,
    // from x 8 that ends at 308 — 26 would have touched the screen edge).
    // Nothing is ever cut to keep the letters large: the size drops first, and
    // if even size 1 overflows, the cut is SAID (below) instead of being made
    // silently — the size-1 fallback used to drop the `complete` out-parameter,
    // which is the one call in the pair that can actually still overflow
    // (review 08-01).
    char lines[20][64];
    int  nl = 0, pitch = 18;
    uint8_t tsz = 2;
    bool full = false;
    const int tafY = VIEW_BODY_Y + (body != m.taf ? 20 : 0);
    nl = viewWrap(body, lines, 25, (240 - 5 - tafY) / 18, BRK, 4, &full);
    if (!full) {
        tsz = 1; pitch = 11;
        nl = viewWrap(body, lines, 50, (240 - 5 - tafY) / 11, BRK, 4, &full);
    }
    // TRUNCATION, SAID OUT LOUD — from either cause: the wire buffer (tafCut)
    // or a forecast that overflows even 19 rows of size 1 (!full). A TAF that
    // simply stops looks exactly like a TAF that ended, and the missing part is
    // always the far end of the forecast.
    // Written INTO the last row rather than drawn by a call of its own: the
    // row is the cut point anyway, it cannot overlap the text below it (this
    // view uses the full height, there is no free raw band), and it keeps the
    // ONE drawString call site A2.22 asks for. Both wordings fit the 25 glyphs
    // of size 2.
    const bool tafTrunc = (!full || m.tafCut) && nl > 0;
    if (tafTrunc)
        strlcpy(lines[nl - 1], sce::T("[...] TAF truncated", "[...] TAF tronque"),
                sizeof(lines[0]));
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(tsz);
    // WHICH GROUP IS IN FORCE RIGHT NOW (user 08-01). A TAF is a list of
    // periods in DAY+HOUR UTC and the reader had to work out which one applies;
    // that is arithmetic the screen can do, and the ONE thing it can add to a
    // verbatim forecast without interpreting it.
    // Marked by a BAR IN THE LEFT MARGIN (x 2..4, free — the text starts at 8),
    // not by a colour: the Scope Night theme has three text levels by
    // construction and `accent` already equals `txtMain` there, so a fourth
    // shade would simply not exist. Position works in all four themes.
    // The header validity never competes: it was decoded above and `body`
    // starts after it, so only real change groups carry a DDHH/DDHH here.
    // `fr::tafGroupCovers` is PURE and natively tested (hour 24, month wrap,
    // exclusive end) — date arithmetic on this screen is not something to
    // eyeball.
    int nowY = 0, nowMo = 0, nowD = 0, nowH = 0;
    {
        const time_t t = time(nullptr);
        struct tm g;
        if (sce::clockSynced(t) && gmtime_r(&t, &g)) {
            nowY = g.tm_year + 1900; nowMo = g.tm_mon + 1;
            nowD = g.tm_mday;        nowH  = g.tm_hour;
        }
    }
    // The change groups are the ARTICULATIONS of the forecast, so they carry
    // the accent while the conditions stay in the reading colour: the eye then
    // finds "when it changes" without reading "what it changes to".
    // Is the BULLETIN itself in force? Its lines up to the first change group
    // are the prevailing conditions, and they hold unless a group supersedes
    // them — TEMPO and PROB are overlays, they do not replace the base.
    const bool baseNow = nowY && hdD1 &&
        fr::ddhhRangeCovers(hdD1, hdH1, hdD2, hdH2, nowY, nowMo, nowD, nowH);
    bool inPrevailing = true;   // until the first BECMG/TEMPO/PROB/FM line
    for (int i = 0; i < nl; i++) {
        const bool grp = nowY && fr::tafGroupCovers(lines[i], nowY, nowMo,
                                                    nowD, nowH);
        const bool chg = !strncmp(lines[i], "BECMG", 5) ||
                         !strncmp(lines[i], "TEMPO", 5) ||
                         !strncmp(lines[i], "PROB",  4) ||
                         (lines[i][0] == 'F' && lines[i][1] == 'M' &&
                          lines[i][2] >= '0' && lines[i][2] <= '9');
        // The change group ENDS the prevailing block, and it must do so BEFORE
        // the bar is decided — computed after, the first BECMG/TEMPO line was
        // itself marked in force whenever the base was, which is precisely the
        // ambiguity the marker exists to remove: a group starting eleven hours
        // from now wore the same accent as the conditions in force (review
        // 08-03, one line off).
        if (chg) inPrevailing = false;
        if (grp || (baseNow && inPrevailing))
            canvas.fillRect(2, tafY + i * pitch, 3, tsz == 2 ? 16 : 8,
                            TH->accent);
        canvas.setTextColor(tafTrunc && i == nl - 1 ? TH->alert
                                                    : (chg ? TH->accent : TH->txt1));
        canvas.drawString(lines[i], VIEW_RULE_L, tafY + i * pitch);
    }
    // NO CLOCK, SAY SO. Without a synced clock `nowY` stays 0 and not one bar
    // is drawn — which on this screen READS as "no group is in force right
    // now", a statement about the weather. It is a statement about us.
    // The distinction is invisible on a CoreS3, whose RTC makes time()
    // plausible from the first frame; an M5Stack Fire has no RTC at all, so
    // this is its NORMAL state until SNTP answers (user report 08-02: "the
    // bar showing the one in force is missing").
    // SHORT, and only when the last body row leaves room. The first wording ran
    // ~348 px from x=8 on a 320 px screen — "indéterminable" was clipped off the
    // right edge — and at 19 wrapped rows it landed on top of the text (review
    // 08-03). A notice that overlaps what it comments on is worse than none.
    if (!nowY) {
        const int noticeY = 228;
        if (tafY + nl * pitch <= noticeY) {
            canvas.setFont(&fonts::efontJA_12);
            canvas.setTextColor(TH->hint);
            canvas.drawString(sce::T("clock not synced",
                                     "horloge non synchro"),
                              VIEW_RULE_L, noticeY);
            canvas.setFont(&fonts::Font0);
        }
    }
    // The long press speaks here, LAST, over whatever the screen drew. Keeping
    // the screen dirty while it shows is what makes it expire on its own.
    if (viewHoldBanner() == BANNER_HOLD) uiDirty = true;
    canvas.pushSprite(0, 0);
}

// The THREE ways this screen can have nothing to show, in their OWN body.
// They must never look alike: they call for three different remedies — an
// account to configure, an account matter to sort out with autorouter, or
// nothing at all to do because there simply is no NOTAM in force, which is
// good news and is therefore said in the neutral colour.
// Split out of drawNotam on A2.22 grounds: the two halves together put ~16
// similar drawing calls in one body, and GCC 8.4 Xtensa drops the second of
// two similar calls. One state, one body.
__attribute__((noinline))
static void drawNotamEmpty(bool creds) {
        // NOTHING TO SHOW IS THREE DIFFERENT STATES and they have three
    // different remedies. Saying which one it is was the whole point of
    // this screen when it had no source at all, and it stays the point now
    // that it has one: "no NOTAM in force" is good news, "403" is an
    // account to sort out, and they must never look alike.
    canvas.setFont(&fonts::efontJA_12);
    const char* why;
    uint16_t col = TH->alert;
    if      (!creds)                       why = sce::T("No autorouter account",
                                                        "Compte autorouter absent");
    else if (notamHttp == HTTP_NO_STATION) why = sce::T("No station: set metar_icao",
                                                        "Aucune station : r\u00e9glez metar_icao");
    else if (notamHttp == HTTP_TOOMANYTOK) why = sce::T("Too many active tokens (max 20)",
                                                        "Trop de jetons actifs (max 20)");
    else if (notamHttp == 403)             why = sce::T("API access not granted (403)",
                                                        "Acc\u00e8s API non accord\u00e9 (403)");
    else if (notamHttp == 401)             why = sce::T("Credentials rejected (401)",
                                                        "Identifiants refus\u00e9s (401)");
    else if (notamHttp && notamHttp != 200) why = sce::T("NOTAM service unavailable",
                                                         "Service NOTAM indisponible");
    else if (notamOkMs) { why = sce::T("No NOTAM in force",
                                       "Aucun NOTAM en vigueur"); col = TH->txt2; }
    else                                   why = sce::T("Loading NOTAM...",
                                                        "Chargement des NOTAM...");
    canvas.setTextColor(col);
    canvas.drawString(why, VIEW_RULE_L, VIEW_BODY_Y + 8);
    canvas.drawFastHLine(VIEW_RULE_L, VIEW_BODY_Y + 26,
                         VIEW_RULE_R - VIEW_RULE_L, TH->sep);
    // Pairs in a TABLE rather than two arrays picked by language: the two
    // forms of a line sit on the same row and cannot be added, renamed or
    // deleted one side only. A2.22 is untouched — still ONE drawString call
    // site, in the same loop, with the pair resolved inside the argument.
    // Each row carries its OWN colour now, so the warning at the bottom costs
    // no second call site (A2.22): one loop, one drawString, the row decides
    // its weight. A null row is a spacer.
    struct Line { const char* en; const char* fr; uint16_t col; };
    const Line HOWTO[7] = {
        { "Source: autorouter.aero (EUROCONTROL EAD),",
          "Source : autorouter.aero (EUROCONTROL EAD),", TH->txt1 },
        { "free. API access is a permission that has",
          "gratuit. L'acc\u00e8s API est une permission \u00e0", TH->txt1 },
        { "to be requested by support ticket - an",
          "demander par ticket support - un compte actif", TH->txt1 },
        { "active account is not enough. Settings:",
          "ne suffit pas. R\u00e9glages : notam_user /", TH->txt1 },
        { "notam_user / notam_pass, NETWORK tab of /config.",
          "notam_pass, onglet RESEAU de /config.", TH->txt2 },
        { nullptr, nullptr, 0 },
        // SAID ONCE, PLAINLY, ON THE SCREEN \u2014 not only in the docs (user
        // 08-01, asking whether an incomplete deck is a problem: it is).
        // This bin ranks, caps and filters, and it queries ONE aerodrome.
        // Each of those bounds is stated where it applies, and none of them
        // adds up to a briefing. Only the screen can say so to someone who
        // never opens the documentation.
        { "NOT a briefing source - fly on the official PIB.",
          "PAS une source de briefing - volez avec le PIB officiel.",
          TH->alert },
    };
    for (int i = 0; i < 7; i++) {
        if (!HOWTO[i].en) continue;
        canvas.setTextColor(HOWTO[i].col);
        canvas.drawString(sce::T(HOWTO[i].en, HOWTO[i].fr), VIEW_RULE_L,
                          VIEW_BODY_Y + 40 + i * 17);
    }
    canvas.setFont(&fonts::Font0);
    // The HTTP code and the hold banner share the raw band, so they cannot both
    // be there — and the BANNER WINS. The code is a permanent fact you can read
    // on the next frame; the banner is the answer to a gesture made half a
    // second ago, and a gesture with no answer is the one that gets repeated.
    // This screen had no banner at all until now, which made it the one place
    // where forcing a refresh mattered most (just after entering notam_user, or
    // once a 403 is sorted out) and said nothing (review 08-01).
    // The question here is whether the raw band is OCCUPIED, which a STICKY
    // banner also makes true -- not whether to redraw.
    const BannerState bst = viewHoldBanner();
    if (bst == BANNER_HOLD) uiDirty = true;
    if (bst == BANNER_NONE && notamHttp && notamHttp != 200 && creds) {
        canvas.setTextColor(TH->hint);
        canvas.drawString("HTTP " + String(notamHttp), VIEW_RULE_L,
                          VIEW_RAW_Y);
    }
    canvas.pushSprite(0, 0);
    return;
}

// ------------------------------------------------------------- NOTAM (top)
// Top step of the ring, and no longer empty: autorouter answers for this
// station (see fetchNotam). It stayed deliberately blank until 07-31 because
// no free source had ever returned anything for FMEE, and writing a parser
// against a response nobody had seen would have shipped a screen that LOOKS
// informed and is guessing.
//
// ONE NOTAM PER SCREEN, paged with HORIZONTAL swipes. 13 of them were in force
// the day this was written and their text ran to 1673 characters: a list of
// one-line summaries would have been a menu of things you cannot read. The
// vertical axis is spoken for (up = next view, down = the companion) and a tap
// returns to the radar, which leaves left/right free — and left/right is what
// a stack of cards asks for anyway.
// NOINLINE like its siblings: `draw()` is already a large body, and A2.22 gets
// more likely the more drawing calls share one.
__attribute__((noinline))
static void drawNotam() {
    xSemaphoreTake(gMtx, portMAX_DELAY);
    // THREE FIELDS, not the whole Metar (review 08-01). This screen shows the
    // station's identity in its header and nothing else of the weather, so a
    // `Metar m = metar` copied 800 bytes to read 48 — on the deepest frame the
    // loop task has (drawNotam measures 2272 bytes with -fstack-usage, against
    // 3772 of headroom), and on the very stack a page-count probe had just
    // been caught overrunning. Take what is used.
    char mIcao[sizeof(metar.icao)], mName[sizeof(metar.name)];
    memcpy(mIcao, metar.icao, sizeof(mIcao));
    memcpy(mName, metar.name, sizeof(mName));
    const bool mValid = metar.valid;
    const int  n   = notamCount;
    const int  idx = (n > 0) ? (notamIdx % n) : 0;
    NotamItem  it{};
    // The TEXT is copied, not borrowed. `it` is a copy of the struct but
    // `it.text` points into PSRAM owned by notams[], and netTask calls
    // notamFree() on the next refresh: reading it after the mutex is released
    // is a use-after-free with a 30-minute fuse — the kind that only fires
    // once you have stopped looking. 560 bytes of stack against a crash that
    // would be blamed on anything but this.
    // PSRAM, allocated once, NOT a local: at NOTAM_TXT = 1800 this buffer plus
    // the line window would put ~3 KB on loop()'s stack, which is shared with
    // the whole radar draw. It is only ever touched from loop().
    static char* textBuf = nullptr;
    if (!textBuf) textBuf = (char*)ps_malloc(NOTAM_TXT);
    if (textBuf) textBuf[0] = 0;
    if (n > 0 && notams) {
        it = notams[idx];
        if (it.text && textBuf) strlcpy(textBuf, it.text, NOTAM_TXT);
        it.text = textBuf;
    }
    xSemaphoreGive(gMtx);
    const bool creds = cfg.notamUser[0] && cfg.notamPass[0];
    canvas.fillSprite(TFT_BLACK);
    char nm[64] = "";
    if (mValid && mName[0]) metarName(mName, nm, sizeof(nm), 40);
    viewHeader(mValid ? mIcao : (cfg.metarIcao[0] ? cfg.metarIcao : "----"),
               nm, "NOTAM", /*ownRule=*/n > 0);

    if (n <= 0) { drawNotamEmpty(creds); return; }

    // ---- THE DECK, AT A GLANCE. Paging through thirteen cards told you
    // "3/13" and nothing else: how many URGENT ones are still behind, and
    // whether you are near the end, were facts you could only get by paging to
    // them. One segment per card, coloured by its rank, the current one drawn
    // full height - so the shape of the deck is readable before reading it.
    // Four pixels of screen for three facts, and it costs the text nothing: it
    // sits in the gap between the header rule (y 22) and the identity line.
    // A2.22: ONE fillRect call site, in a loop over the deck.
    {
        const int SX = VIEW_RULE_L, SW = VIEW_RULE_R - VIEW_RULE_L;
        const int seg = SW / (n > 0 ? n : 1);
        // Below ~3 px a segment stops being a segment; past that the strip
        // would lie about how many cards there are, so it says "many" by
        // filling and lets the counter carry the number.
        // Under 3 px a segment stops being a segment. The strip then gives
        // the row BACK to a plain rule rather than drawing a lie: without this
        // the header would simply lose its separator on a deck of forty.
        if (seg < 3) {
            canvas.drawFastHLine(SX, 23, SW, dim565(TH->sep, 1, 2));
        } else {
            for (int i = 0; i < n; i++) {
                const uint8_t r = notams[i].prio < 4 ? notams[i].prio : 3;
                const bool    me = (i == idx);
                const uint16_t c = (r == 0) ? TH->alert
                                 : (r == 1) ? TH->accent
                                 : (r == 2) ? TH->txt2 : TH->sep;
                // IT SITS ON THE RULE'S OWN ROW (08-03): y 22 is where the
                // header separator used to be, and the strip replaces it
                // rather than queueing under it. That buys the slot y 22..24
                // instead of 23..25, so the current card gets three full
                // pixels and the identity line at VIEW_BODY_Y = 26 keeps a
                // clear row above it.
                // The first version ran to y 26 — one row INTO the text (user:
                // "the deck strip and the writing overlap"). Measured now, not
                // eyeballed: rule 22, strip 22..24, gap 25, identity 26..33.
                // y 23 and no longer 22 (user 08-03: "add a pixel above the
                // strip too"). It still REPLACES the header rule - nothing else
                // claims row 22 - it simply stops touching the header's own
                // glyphs, which end at y 19. A strip pinned to the text above
                // and the text below belongs to neither.
                canvas.fillRect(SX + i * seg, 23,
                                seg - 1, me ? 3 : 2, c);
            }
        }
    }

    // PAGE COUNT FIRST: the identity line prints it, and it is measured by the
    // text block further down. Computed here, the counter cannot show the
    // PREVIOUS card's page count for one frame — a wrong "p1/3" is not a
    // cosmetic lag, it is a false statement about how much is left to read.
    // TEXT BLOCK GEOMETRY, and it is a GUTTER, not a taste: the two page-turn
    // chevrons live at y VIEW_BODY_Y + 78 = 104, and the text rows run
    // 58, 69, 80, 91, 102, 113... — rows 4 and 5 (y 102 and 113) fall inside the
    // size-2 chevron cell (16 px tall, 104..119). The text used to start at
    // VIEW_RULE_L = 8 and run 50 glyphs of the 6 px Font0 cell, i.e. x 8..307,
    // straight UNDER "<" (x 2..13) and ">" (x 306..317). Two rows of a NOTAM
    // were unreadable (user 08-01).
    // Derived, not guessed:
    //   left  chevron cell ends at  2 + 12 - 1 = 13  -> first free column 14
    //   TXT_X = 18                                   -> 4 px of clear gutter
    //   47 glyphs from 18 end at 18 + 47*6 - 1 = 299 -> next cell would be 300
    //   right chevron cell starts at 306             -> 6 px of clear gutter
    // 48 glyphs would put a cell at exactly 306 and collide again, so 47 is the
    // maximum. Only the TEXT BLOCK is inset: the header, the identity line
    // (y 26) and the validity line (y 38) all sit above 104 and are already
    // clear of the chevrons, so they keep the rule's own x = VIEW_RULE_L.
    // TXT_W feeds BOTH the page-count probe below and the draw further down —
    // one constant, or the counter would state a page count nobody is shown.
    // THE CHEVRONS LEFT (08-03), and the gutter left with them. They used to
    // sit at y = VIEW_BODY_Y + 78, i.e. INSIDE the text block, which is why the
    // text was inset to x 18 and clipped at 47 glyphs. They are pure
    // affordances - paging is a horizontal SWIPE or a BUTTON, no hit test ever
    // depended on where they were drawn - so moving them to the foot of the
    // block costs nothing and gives the text the rule's full width:
    // 50 glyphs from VIEW_RULE_L, i.e. 3 more per line and ~42 more characters
    // per page. On a field whose whole job is to be read in full, that is the
    // cheapest information there is.
    // NOTAM-LOCAL ORIGIN, two pixels below the shared one (user 08-03: "give
    // the deck strip 2 px more room underneath"). VIEW_BODY_Y is shared with
    // the METAR and the TAF, which sit under a plain 1 px rule and need no
    // clearance; this screen sits under a 3 px coloured strip that the eye
    // reads as an object, and an object wants air.
    // THREE pixels since 08-03 (the user asked for one more after seeing it):
    // a 3 px coloured strip needs about its own height of clearance before the
    // eye stops reading the two as one block.
    // They are NOT taken from the text: the page strip moving into the raw band
    // (below) gives ten back, so the block ends up ONE LINE LONGER than before
    // - 14 rows against 13.
    const int NB_Y = VIEW_BODY_Y + 4;   // +1 again, 08-03: air under the strip
    const int TXT_X = VIEW_RULE_L, TXT_W = 50, TXT_PITCH = 11;
    // -10: the page strip now lives at the foot of the block (y 206..212).
    // -2 and not -14: the page strip no longer lives at the foot of the block,
    // so the text may run down to the raw band itself.
    //
    // THE LAST ROW COSTS A GLYPH, NOT A PITCH. Dividing the free height by the
    // PITCH charges the bottom row for the 3 px of leading that follows it -
    // leading nothing sits in, since the band comes next. That over-charge is
    // invisible until it crosses a boundary: the third pixel of clearance added
    // above would have cost a whole row of NOTAM text for want of 3 px that
    // were never used. First row + N-1 pitches + one 8 px glyph.
    const int TXT_TOP = NB_Y + 42;   // rule at +35, then 7 px of standoff
    int txtMaxl = (VIEW_RAW_Y - 2 - TXT_TOP - 8) / TXT_PITCH + 1;
    if (txtMaxl > 18) txtMaxl = 18;
    {
        // COUNT-ONLY (lines = nullptr): this measures, it does not draw.
        int np = 1;
        viewWrapPage(it.text ? it.text : "", nullptr, TXT_W, txtMaxl, 0, &np);
        notamPages = np;
        if (notamPage < 0)        notamPage = np - 1;   // came in backwards
        else if (notamPage >= np) notamPage = np - 1;
    }

    // ---- IDENTITY LINE: which NOTAM, of how many, and its Q-code. The count
    // is not decoration — on a deck you page through blind, it is what tells
    // you there are twelve more behind this one.
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    // THE RANK, SAID OUT LOUD. It already decides the order of the deck; until
    // now the reader could not see why this card came first. The words are the
    // PIB purpose codes' own meaning, not our paraphrase:
    //   NBO -> immediate attention        BO -> operationally significant
    //   B   -> for the briefing           M  -> miscellaneous, not briefed
    // The Q-code beside it stays RAW. Glossing it would mean a table of
    // subject codes written from memory on an aeronautical display, which is
    // precisely the invention this whole view has refused from the start —
    // "OB" wrongly rendered as "obstacle" reads as authoritative and is not.
    static const char* const RANKW[4] = { "URGENT", "OPS", "BRIEF", "INFO" };
    const uint8_t rk = it.prio < 4 ? it.prio : 3;
    canvas.setTextColor(rk == 0 ? TH->alert : TH->accent);
    canvas.drawString(it.id, VIEW_RULE_L, NB_Y);
    canvas.setTextColor(rk == 0 ? TH->alert : (rk == 3 ? TH->hint : TH->txt2));
    canvas.drawString(RANKW[rk], VIEW_RULE_L + 62, NB_Y);
    canvas.setTextColor(TH->hint);
    // NO "Q)" HERE ANY MORE (user 08-03: "we print Q)... on the identity line
    // and Q) again just below - isn't that misleading?"). It was: the row below
    // is the REAL item Q, in full, and it CONTAINS these four letters. Two
    // things labelled Q), one of them a fragment, invites the reader to believe
    // they are different fields.
    // The code stays - it is what tells you WHAT the notice is about at a
    // glance - but unlabelled, as the headline it actually is.
    canvas.drawString(it.q, VIEW_RULE_L + 110, NB_Y);
    // ---- ITEMS F AND G, the vertical band, immediately after the Q-code -
    // which is where they belong: a real Q line carries the same two limits as
    // its fields 6 and 7, so the eye reads "what, and between which levels" in
    // one movement instead of hunting.
    // RAW and joined by a dash, never glossed: "SFC", "GND", "FL195", "3000FT
    // AMSL" are the source's own words, and rewriting them into a house format
    // is the invention this whole view refuses. Only the SEPARATOR is ours.
    // The slot is measured, not assumed: the Q-code ends at x 110 + 7*6 = 152,
    // the counter is right-aligned and the longest one ("12/40 of 213") is 12
    // glyphs = 72 px, so it starts at 312 - 72 = 240. What is left is
    // 158..234, i.e. 12 glyphs - and a band that does not fit is DROPPED
    // rather than overprinted onto the counter. It is still in item E for the
    // ones that restate it, and a mangled altitude is worse than none.
    // ---- THE Q LINE IN FULL, its own row (08-03). The screen used to print
    // `Q)PMCH` and stop, which is one sub-field out of eight: the FIR, the
    // traffic it applies to, its purpose, its scope, the vertical band and the
    // centre radius were all dropped, and the user read the result as a
    // truncation - correctly.
    // Rebuilt in the printed order and joined by the form's own slashes, so it
    // can be compared character for character with the NOTAM on paper. It costs
    // ONE row of item E, and that is the right trade: E is paged and loses
    // nothing, while a Q line you cannot see is information that has no other
    // home on this screen.
    // ITEM A rides at the end of the row and ONLY when it differs from the
    // station in the header - normally they are the same and repeating it would
    // spend a row's tail on a word already on screen; when they differ (a FIR
    // notice reaching an aerodrome deck) it is the one thing you must see.
    {
        char q[64];
        const bool other = it.itema[0] && strcasecmp(it.itema, mIcao) != 0;
        snprintf(q, sizeof(q), "Q)%s%s%s", it.qline,
                 other ? "  A)" : "", other ? it.itema : "");
        canvas.setTextColor(TH->hint);
        canvas.drawString(q, VIEW_RULE_L, NB_Y + 12);
    }
    {
        // "3/13" — and "3/13 sur 21" when the ranking had to drop some, because
        // 13/13 reads as "that is all of them" and it would not be.
        char pos[28];
        const int tot = notamInForce;
        const int np  = notamPages > 0 ? notamPages : 1;
        // The page only appears when there IS more than one: a "p1/1" on every
        // short NOTAM would be noise claiming to be information.
        // The page number left this line on 08-03: the pips at the foot of the
        // text block say it, in the place where the swipe happens. Kept as an
        // empty string rather than removed from the formats, so the two
        // branches below stay one shape.
        char pg[10] = "";
        (void)np;
        if (tot > n) snprintf(pos, sizeof(pos),
                              sce::T("%d/%d of %d%s", "%d/%d sur %d%s"),
                              idx + 1, n, tot, pg);
        else         snprintf(pos, sizeof(pos), "%d/%d%s", idx + 1, n, pg);
        canvas.setTextColor(TH->hint);
        const int posW = canvas.textWidth(pos);
        canvas.drawString(pos, VIEW_RULE_R - posW, NB_Y);

        // ---- ITEMS F AND G, in the gap the counter leaves.
        // MEASURED against that counter, not assumed: it is "3/13" most of the
        // time but "12/40 of 213" when the ranking dropped cards, and the
        // difference is 48 px. Drawn only if it genuinely fits, because the one
        // thing worse than a missing altitude band is one printed over a count.
        //
        // WHY THEY ARE RARE, and why the Q row carries the band anyway: in the
        // 46 captured NOTAM, itemf/itemg are absent from FORTY. The vertical
        // band normally lives inside Q) as `000/999`, and items F and G are the
        // human-readable form that only navigation warnings carry - which is
        // also why the guard had to grow: `G)400FT AMSL` is 18 characters, and
        // a 14-character limit threw away precisely the notices that have one.
        if (it.lo[0] || it.hi[0]) {
            char band[32];
            snprintf(band, sizeof(band), "F)%s G)%s",
                     it.lo[0] ? it.lo : "?", it.hi[0] ? it.hi : "?");
            const int x = VIEW_RULE_L + 158;
            if (x + canvas.textWidth(band) < VIEW_RULE_R - posW - 6) {
                canvas.setTextColor(TH->txt2);
                canvas.drawString(band, x, NB_Y);
            }
        }
    }

    // ---- VALIDITY, and the schedule when the NOTAM carries one. Dates in
    // UTC: a NOTAM is issued in UTC and reading it in local time is how you
    // arrive on the wrong day. The whole line is skipped rather than printed
    // from a 1970 clock.
    {
        char when[46] = "";
        time_t f = (time_t)it.from, t = (time_t)it.to;
        struct tm tf, tt;
        if (sce::clockSynced((time_t)it.from) && gmtime_r(&f, &tf) &&
            gmtime_r(&t, &tt))
            snprintf(when, sizeof(when), "%02d/%02d %02d:%02d  ->  %02d/%02d %02d:%02d Z",
                     tf.tm_mday, tf.tm_mon + 1, tf.tm_hour, tf.tm_min,
                     tt.tm_mday, tt.tm_mon + 1, tt.tm_hour, tt.tm_min);
        // A NOTAM that expires within the day is not the same object as one
        // running for months, and the dates alone do not say so at a glance.
        const time_t nw = time(nullptr);
        const bool soon = (sce::clockSynced(nw) && it.to > (uint32_t)nw &&
                           it.to - (uint32_t)nw < 86400);
        canvas.setTextColor(soon ? TH->alert : TH->txt1);
        if (when[0]) canvas.drawString(when, VIEW_RULE_L, NB_Y + 24);
        // HOW LONG IS LEFT, which is the question two dates are read to answer
        // (08-03). `soon` already turned the line red under a day, but red says
        // "hurry" and never says how much: a NOTAM ending in forty minutes and
        // one ending in twenty hours were the same colour. Coarse ON PURPOSE -
        // hours under two days, days beyond - because a minute count on a
        // notice issued to the hour would be false precision.
        if (when[0] && sce::clockSynced(nw)) {
            char left[16] = "";
            if (it.to <= (uint32_t)nw)              strlcpy(left, "EXPIRE", sizeof(left));
            else {
                const uint32_t d = it.to - (uint32_t)nw;
                if (d < 172800u) snprintf(left, sizeof(left), "%luh", (unsigned long)(d / 3600u));
                else             snprintf(left, sizeof(left), "%luj", (unsigned long)(d / 86400u));
            }
            // MEASURED against both neighbours, not pinned (review 08-04): a
            // fixed x=+200 overprinted a long item D schedule — right-aligned
            // on this same row — on exactly the notices where both matter
            // (scheduled AND expiring). Same guard the F)/G) band got first:
            // drawn only if it genuinely fits, dropped otherwise.
            const int lx = VIEW_RULE_L + canvas.textWidth(when) + 10;
            const int schedX = it.sched[0]
                ? VIEW_RULE_R - canvas.textWidth(it.sched) : VIEW_RULE_R;
            if (lx + canvas.textWidth(left) <= schedX - 6) {
                canvas.setTextColor(soon ? TH->alert : TH->hint);
                canvas.drawString(left, lx, NB_Y + 24);
            }
        }
        if (it.sched[0]) {
            canvas.setTextColor(TH->accent);
            canvas.drawString(it.sched, VIEW_RULE_R - canvas.textWidth(it.sched),
                              NB_Y + 24);
        }
    }
    // HALF-WEIGHT (user 08-03: "make the rule more discreet"). `sep` is already
    // the palette's separator level, but this screen now has a coloured strip
    // doing the loud separating at the top; a second full-strength line four
    // rows under it competes for the same job. `dim565` derives the shade FROM
    // the theme rather than adding a fifth constant to four palettes - the same
    // reasoning as the runway surface.
    // +23 and no longer +24, with the text at +30 instead of +32: one pixel
    // tightened ABOVE the rule and one BELOW (user 08-03). A half-weight rule
    // needs less standoff than a full one - it is a hint of a boundary, not a
    // wall - and the two pixels go where they are read, into the air under the
    // deck strip and into the text block.
    canvas.drawFastHLine(VIEW_RULE_L, NB_Y + 35,
                         VIEW_RULE_R - VIEW_RULE_L, dim565(TH->sep, 1, 2));

    // ---- THE E FIELD, verbatim, wrapped on spaces by the shared wrapper.
    // Font0 like every other piece of wire text on this bin: it is a code, not
    // our prose. Nothing is glossed — the same rule the TAF settled.
    {
        char lines[18][64];
        const int W = TXT_W, PITCH = TXT_PITCH, maxl = txtMaxl;
        // PAGED, not truncated (user 08-01). The text used to be cut at what
        // one screen holds and the reader was told to "see /config" — where
        // there is nothing to read. A NOTAM you cannot finish is a NOTAM you
        // have to distrust, and pointing somewhere useless is worse than
        // saying nothing. The horizontal swipe already pages the deck, so a
        // long NOTAM is simply several cards: no new gesture, no scrollbar.
        int np = 1;
        const int nl = viewWrapPage(it.text ? it.text : "", lines, W, maxl,
                                    notamPage, &np);
        canvas.setTextColor(TH->txtMain);
        // TXT_X, not VIEW_RULE_L: the chevron gutter, derived above.
        for (int i = 0; i < nl; i++)
            canvas.drawString(lines[i], TXT_X,
                              TXT_TOP + i * PITCH);
        // The ONLY truncation left is the source cap (NOTAM_TXT): a field
        // longer than anything ever measured. It still SAYS SO — but it no
        // longer sends the reader to a page that cannot help.
        if (it.cut) {
            canvas.setTextColor(TH->alert);
            canvas.drawString(sce::T("[...] text longer than the buffer",
                                     "[...] texte plus long que le tampon"),
                              VIEW_RULE_L, VIEW_RAW_Y);
        }
    }
    // PAGE-TURN MARKS. An invisible tap zone is not an interaction, it is a
    // secret: these two chevrons are the only thing that says the edges do
    // something. Drawn at the vertical middle of the body, where a thumb
    // naturally lands. ONE drawString call site fed by a table (A2.22).
    // ACCENT, not the hint grey they used to wear (user 08-01). Two reasons,
    // and the second is what settles it:
    //   - `hint` is the palette's DIMMEST level; a control the reader is meant
    //     to find should not be quieter than every label on the screen.
    //   - the obvious step up, `txt2`, is a NON-EVENT in the Scope themes: in
    //     Scope night txt2 and hint are the SAME word (0xE800 both — that
    //     theme only affords three text levels), and in Scope day it is
    //     0x0540 against 0x04E0, three units of green nobody will ever see.
    // `accent` is a different HUE from the body text in three themes (yellow
    // on white, orange, green on white) and a genuine step in the fourth. It
    // does not compete with the NOTAM text: that is `txtMain`, and the gutter
    // derived above now keeps the two apart in space as well as in colour.
    // In Scope night accent does land on the body's own red — but that theme
    // states outright that its hierarchy reads through size and position, and
    // these are size-2 glyphs alone in a 6 px gutter.
    // ---- THE PAGE STRIP SHARES THE RAW BAND with the banner (user 08-03:
    // "they are not shown at the same time anyway, and if they are it is
    // temporary"). That is the right reading: the banner answers a gesture made
    // a second ago or reports the weather turning, and it lasts 1.6 s or five
    // minutes; the page strip is permanent furniture nobody consults while a
    // message is up. Sharing those 24 px gives the TEXT ten of them back - the
    // block goes from 13 rows to 14, which is the whole point of the trade.
    // The banner is drawn LAST and simply covers the strip; that is why the
    // chevrons moved inside the rules, so the covering is complete. The
    // chevrons keep their meaning and lose their nuisance; between them, one
    // pip per page of THIS card, the current one filled. "p2/5" said the same
    // thing in eight characters of the identity line; the pips say it without
    // spending a line, and they say it in the place where you are about to
    // swipe. A2.22: one fillRect call site in a loop, like the deck strip.
    {
        const int np = notamPages > 0 ? notamPages : 1;
        if (np > 1) {
            const int PIP = 6, GAP = 3, tot = np * PIP + (np - 1) * GAP;
            int px = (320 - tot) / 2;      // screen width, this bin is 320 px
            for (int i = 0; i < np; i++)
                canvas.fillRect(px + i * (PIP + GAP), VIEW_RAW_Y + 9, PIP,
                                i == notamPage ? 5 : 2,
                                i == notamPage ? TH->accent : TH->sep);
        }
    }
    if (n > 1 || notamPages > 1) {
        static const char* const CHEV[2] = { "<", ">" };
        // 306 and not 312: at size 2 a Font0 cell is 12 px wide, so 312 put the
        // glyph on 312..323 — four pixels past a 320 px screen, of which the
        // ">" only showed the loss on its last column (user 08-01).
        // INSIDE THE RULES (8..312) now that the strip shares the raw band:
        // the banner paints exactly that span, so a chevron at x 2 or 306 would
        // have shown its outer half sticking out BESIDE a banner instead of
        // being cleanly replaced by it.
        static const int         CHEVX[2] = { 8, 300 };
        canvas.setFont(&fonts::Font0);
        canvas.setTextSize(2);
        canvas.setTextColor(TH->accent);
        for (int i = 0; i < 2; i++)
            // 199 and not 203: a size-2 Font0 cell is 16 px tall, so 203 put
            // the chevrons on 203..218 — three rows INSIDE the raw band, which
            // starts at VIEW_RAW_Y = 216 and belongs to the banner. The text
            // block's last row ends at 197, so 199..214 is the only span that
            // touches neither (same class of arithmetic slip as the deck strip
            // above, found the same way — by measuring instead of eyeballing).
            canvas.drawString(CHEV[i], CHEVX[i], VIEW_RAW_Y + 4);
        canvas.setTextSize(1);
    }
    // The long press speaks here, LAST, over whatever the screen drew. Keeping
    // the screen dirty while it shows is what makes it expire on its own.
    if (viewHoldBanner() == BANNER_HOLD) uiDirty = true;
    canvas.pushSprite(0, 0);
}

// THE RADAR'S BOTTOM LINE, and the ONLY place it is drawn (A2.22).
//
// The radar has two footers — one while tracking, one idle — and the banner
// overrides both when it has something to say. Written as a drawString inside
// each branch, which is how this started, GCC 8.4 Xtensa is entitled to drop
// the second: the failure mode is a footer that silently vanishes in one of
// the two states, exactly the class of bug that cost a session in July.
//
// It is a FUNCTION rather than a block inside draw() so the rule is CHECKABLE:
// `scripts/gates/check-a222.py` counts calls per symbol, and draw() makes 45
// drawString calls, a total that would be brittle to pin and would express
// nothing. Here the expected count is 1 and that number IS the design.
// noinline for the same reason — inlined back into draw(), the symbol the
// checker watches would simply cease to exist and the gate would go quiet.
//
// `bar` separates the two things that share this line: a BANNER (an event —
// filled ground, black text) from a HINT ("touch an aircraft to track" — plain
// coloured text). A hint is permanent screen furniture; painting it as a full
// colour bar would turn a discreet caption into a standing alarm. It is a
// PARAMETER and not a second branch around the draw for the reason above the
// function: the single call site is the invariant.
static void __attribute__((noinline))
drawRadarFooter(const char* msg, uint16_t col, bool bar) {
    if (!msg) return;
    canvas.setTextDatum(textdatum_t::top_center);
    canvas.setFont(&fonts::efontJA_12);            // accents
    if (bar) {
        // The bar covers the RADAR COLUMN, not the width of the screen: the
        // flight panel owns everything past PANEL_X and has its own bottom
        // rows. Centred on CX = 114 = PANEL_X / 2, so the default case is
        // exactly the column, edge to edge.
        // It grows for a message wider than the column rather than letting the
        // ends of the text spill onto black outside their own ground — an
        // address like `http://192.168.1.123/config` is the long case.
        const int tw = canvas.textWidth(msg) + 8;
        int bw = tw > PANEL_X ? tw : PANEL_X;
        int bx = CX - bw / 2;
        if (bx < 0) { bx = 0; }
        if (bx + bw > canvas.width()) bw = canvas.width() - bx;
        bannerGround(col, bx, 226, bw, 14);        // 12 px glyphs + 2 above
    } else {
        canvas.setTextColor(col);
    }
    canvas.drawString(msg, CX, 228);
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(textdatum_t::top_left);
}

#if SCE_INPUT_BUTTONS
// ---------------------------------------------------------- DEBUG overlay
// Held with A+C. Everything a touch board reads in the settings panel's
// RESEAU tab, on a board that has no settings panel — network, resources,
// and what each source last answered.
//
// A HELD overlay rather than a step of the view ring, deliberately: it is a
// diagnostic, not a destination. You cannot end up stranded in it, it cannot
// come up by accident on the ring, and letting go is the whole exit.
//
// A2.22: 15 rows, ONE drawString call site in ONE loop over a table. Written
// as fifteen calls, GCC 8.4 Xtensa is entitled to drop some of them, and a
// diagnostic screen missing a line it never mentions is worse than no
// diagnostic at all. `noinline` so `scripts/gates/check-a222.py` can watch it.
static bool dbgHeld = false;

static void __attribute__((noinline)) drawDebug() {
    // LET THE BANNERS EXPIRE UNDERNEATH. bannerPick is the only place that
    // clears holdFireMs, and draw() returns before it while the chord is held:
    // hold A+C for more than 1.6 s after a B-long refresh and "mise a jour..."
    // was still on the frame you came back to (review 08-03).
    { String b; const char* m; uint16_t c; (void)bannerPick(b, m, c); }
    canvas.fillSprite(TFT_BLACK);
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    canvas.setTextDatum(textdatum_t::top_left);

    char v[16][44];
    int n = 0;

    // BILINGUAL LIKE EVERY OTHER SCREEN. This one was written in French
    // outright while its own footer already went through sce::T() — so an
    // English board (no `lang:` key at all, i.e. the DEFAULT) showed fifteen
    // French rows and one English line at the bottom (review 08-03).
    // Labels are kept to SEVEN characters plus a space in both languages: the
    // columns are what make this readable at 6x8, and a translation that
    // shifted them by one would ruin the screen it exists to serve. Section
    // headings stay ACCENT-FREE and uppercase — the row-vs-heading test below
    // reads the first two characters, and Font0 has no accented glyphs.
    snprintf(v[n++], 40, sce::T("NETWORK %s  rssi %d dBm",
                                "RESEAU  %s  rssi %d dBm"),
             WiFi.status() == WL_CONNECTED ? "STA" : "AP", (int)WiFi.RSSI());
    // `ip`, `config`, `ssid`, `adsb`, `metar`, `notam`, `heap`, `psram` and
    // the SOURCES heading are spelled identically in both languages: one
    // literal, not a sce::T() with two equal halves, which would only invite
    // the two to drift.
    snprintf(v[n++], 44, "ip      %s", ipStr.c_str());
    // THE ADDRESS TO TYPE, spelled out. This board has no settings panel and
    // no keyboard: everything configurable lives behind that URL, so the one
    // screen a user reaches when something looks wrong is where it belongs.
    // Written in full rather than "see /config" — you retype what you read.
    snprintf(v[n++], 44, "config  http://%s/config", ipStr.c_str());
    snprintf(v[n++], 44, "ssid    %s", WiFi.SSID().c_str());
    {
        char h[10];
        snprintf(v[n++], 44, sce::T("clock   %s", "horloge %s"),
                 zuluHhmm(h, sizeof(h)) ? h : sce::T("NO SYNC", "NON SYNC"));
    }
    snprintf(v[n++], 44, "SOURCES");
    snprintf(v[n++], 44, sce::T("adsb    %s  http %d  %d aircraft",
                                "adsb    %s  http %d  %d avion(s)"),
             cfg.api, httpStatus, (int)planeCount);
    snprintf(v[n++], 44, "metar   %s  http %d",
             cfg.metarIcao[0] ? cfg.metarIcao : "-", (int)metarHttp);
    snprintf(v[n++], 44, "notam   %s  http %d",
             cfg.notamUser[0] ? sce::T("account ok", "compte ok")
                              : sce::T("no account", "sans compte"),
             (int)notamHttp);
    snprintf(v[n++], 44, sce::T("center  %s %.4f %.4f",
                                "centre  %s %.4f %.4f"),
             cfg.airport[0] ? cfg.airport : "-", cfg.lat, cfg.lon);
    snprintf(v[n++], 44, sce::T("radius  %d nm   poll %lu s",
                                "rayon   %d nm   poll %lu s"),
             (int)cfg.radiusNm, (unsigned long)cfg.pollS);
    snprintf(v[n++], 44, sce::T("RESOURCES    up %lus",
                                "RESSOURCES   up %lus"),
             (unsigned long)(millis() / 1000));
    snprintf(v[n++], 44, "heap    %u  min %u",
             (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
    snprintf(v[n++], 44, "psram   %u", (unsigned)ESP.getFreePsram());
    snprintf(v[n++], 44, sce::T("stack   loop %u  net %u  sd %s",
                                "pile    loop %u  net %u  sd %s"),
             (unsigned)uxTaskGetStackHighWaterMark(nullptr),
             netTaskHandle
                 ? (unsigned)uxTaskGetStackHighWaterMark(netTaskHandle) : 0u,
             sdOk ? "ok" : sce::T("missing", "absente"));

    // Section headings are the rows that carry no leading spaces in their
    // label; they get the accent so the eye finds the three groups at once.
    // Pitch 15 and not 16: fifteen rows plus the footer must fit 240 px, and
    // at 16 the last line fell off the bottom — the one place a diagnostic
    // must not silently truncate.
    constexpr int Y0 = 4, PITCH = 15;
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

static void draw() {
#if SCE_INPUT_BUTTONS
    // Before everything: the overlay owns the screen while the chord is held.
    if (dbgHeld) { drawDebug(); return; }
#endif
    // Each step of the stack OWNS the screen: a radar, a weather report and a
    // NOTAM list have nothing to share, and the 320 px are needed whole.
    if (viewLevel == VIEW_NOTAM) { drawNotam(); return; }
    if (viewLevel == VIEW_TAF)   { drawTaf();   return; }
    if (viewLevel == VIEW_METAR) { drawMetar(); return; }
    xSemaphoreTake(gMtx, portMAX_DELAY);   // planes[]/selectedRoute shared
    canvas.fillSprite(TFT_BLACK);
    // THE BANNER TAKES THE BOTTOM LINE when there is something to say. Picked
    // ONCE here and consumed by whichever branch below owns that line — the
    // radar has two footers (one tracking, one idle) and both are hints you
    // read once and then know, whereas the banner answers a gesture you just
    // made or warns that the weather turned.
    // Until 08-02 the radar called the banner code not at all: the weather
    // warning could not appear on the one screen you actually watch, despite a
    // comment claiming otherwise, and on the Fire the B-long refresh had no
    // acknowledgement anywhere.
    String      bbuilt;
    const char* bmsg;
    uint16_t    bcol;
    const BannerState bst = bannerPick(bbuilt, bmsg, bcol);
    const char* footer = nullptr;   // chosen by the branches, drawn once below
    // Range rings + centre cross + N (green phosphor)
    canvas.drawCircle(CX, CY, RPX,           TH->ring1);
    canvas.drawCircle(CX, CY, RPX * 2 / 3,   TH->ring2);
    canvas.drawCircle(CX, CY, RPX / 3,       TH->ring2);
    // A2.22: two similar line calls in the same body — GCC 8.4 Xtensa drops
    // the second one. A single call site, in a loop (rule of 07-25).
    const int cxA[2] = { CX - 4, CX }, cyA[2] = { CY, CY - 4 };
    const int cxB[2] = { CX + 4, CX }, cyB[2] = { CY, CY + 4 };
    for (int i = 0; i < 2; i++)
        canvas.drawLine(cxA[i], cyA[i], cxB[i], cyB[i], TH->ring2);
    canvas.setTextSize(1);
    canvas.setTextColor(TH->ringTxt);
    canvas.drawString("N", CX - 2, CY - RPX - 12);
    canvas.drawString(String((int)dD(cfg.radiusNm)) + uD(), CX + RPX - 32, CY + 4);
    canvas.drawString(String((int)dD(cfg.radiusNm / 3)) + uD(),
                      CX + RPX / 3 + 2, CY + 4);

    // The tracked flight is resolved BEFORE the screen clip: off-scope, the
    // TRACK panel stays on display (before: it silently fell back to RADAR
    // mode as soon as the aircraft left the radius, max review 07-26).
    const Plane* sel = nullptr;
    if (selectedHex[0])
        for (int i = 0; i < planeCount; i++)
            if (!strcmp(planes[i].hex, selectedHex)) { sel = &planes[i]; break; }
    const int vecMin = vectorMin();      // same horizon for ALL blips
    for (int i = 0; i < planeCount; i++) {
        Plane& p = planes[i];
        // "Ground traffic" filter: hidden unless it is the tracked flight
        if (!cfg.showGround && p.onGround && &p != sel) continue;
        int x, y; planeToXY(p.lat, p.lon, x, y);
        bool isSel = (sel == &p);
        if (x < 6 || x > PANEL_X - 8 || y < 10 || y > 232) {
            if (!isSel) continue;
            // Tracked flight OFF-SCOPE (worldwide track by callsign, user
            // 07-27): double-circle marker CLAMPED to the radar edge, true
            // direction — the data lives in the panel.
            float dx = (float)(x - CX), dy = (float)(y - CY);
            float d  = sqrtf(dx * dx + dy * dy);
            if (d < 1.0f) continue;
            int ex = CX + (int)(dx * (float)RPX / d);
            int ey = CY + (int)(dy * (float)RPX / d);
            const int mkR[2] = { 5, 2 };   // A2.22: two constant
            for (int k = 0; k < 2; k++)    // concentric circles
                canvas.drawCircle(ex, ey, mkR[k], TH->selCol);
            continue;              // no trail, no triangle: off scale
        }
        uint16_t col = isSel ? TH->selCol : altColor(p);
        // Trail of the targeted flight (polyline of the local history)
        if (isSel && p.tCount > 1) {
            int px = -1, py = -1;
            for (int k = 0; k < p.tCount; k++) {
                int idx = (p.tHead + Plane::TRAIL - p.tCount + k) % Plane::TRAIL;
                int tx, ty; planeToXY(p.tLat[idx], p.tLon[idx], tx, ty);
                if (px >= 0) canvas.drawLine(px, py, tx, ty, TH->trail);
                px = tx; py = ty;
            }
        }
        // Oriented blip — SYMBOL per ICAO category (user 07-27):
        // A7 helicopter = circle + rotor cross; B1 glider = 2 px wing +
        // fuselage; B4 microlight = HOLLOW delta; A1 light = small SOLID
        // triangle; default = triangle. Military (dbFlags bit0) = magenta
        // square around the blip + type under the callsign.
        float a  = p.track * DEG_TO_RAD;
        float ca = cosf(a), sa = sinf(a);
        auto rx = [&](float dx, float dy) { return x + (int)( dx * ca + dy * sa); };
        auto ry = [&](float dx, float dy) { return y + (int)( dx * sa - dy * ca); };
        char c0 = p.cat[0], c1 = p.cat[1];
        if (c0 == 'A' && c1 == '7') {                       // helicopter
            canvas.drawCircle(x, y, 4, col);
            // A2.22: the rotor cross is the EXACT pattern proven broken on
            // 07-25 (the arms of the Dead X). Loop with a single call site.
            const int ya[2] = { y - 6, y + 6 }, yb[2] = { y + 6, y - 6 };
            for (int k = 0; k < 2; k++)
                canvas.drawLine(x - 6, ya[k], x + 6, yb[k], col);
        } else if (c0 == 'B' && c1 == '1') {                // glider
            // A2.22: four line calls, two of them nearly identical (the two
            // wing lines). Table + single call site.
            const int glx0[4] = { -9, -9,  0, -3 }, gly0[4] = { 0, 1,  5, -4 };
            const int glx1[4] = {  9,  9,  0,  3 }, gly1[4] = { 0, 1, -4, -4 };
            for (int k = 0; k < 4; k++)
                canvas.drawLine(rx(glx0[k], gly0[k]), ry(glx0[k], gly0[k]),
                                rx(glx1[k], gly1[k]), ry(glx1[k], gly1[k]), col);
        } else if (c0 == 'B' && c1 == '4') {           // microlight: HOLLOW delta
            canvas.drawTriangle(rx(0, 5), ry(0, 5), rx(-4, -4), ry(-4, -4),
                                rx(4, -4), ry(4, -4), col);
        } else {
            // Light (A1) and airliner differ only by SIZE: a single call
            // site, which satisfies A2.22 without telling them apart through
            // two neighbouring fillTriangle calls.
            // ORIGINAL sizes: light = 3 with a nose of 4, airliner = 4 with a
            // nose of 6. A uniform `n + 1` had shortened the nose of the most
            // frequent blip by one pixel (review 07-29).
            bool light = (c0 == 'A' && c1 == '1');
            int n = light ? 3 : 4;
            int t = light ? 4 : 6;
            canvas.fillTriangle(rx(0, t), ry(0, t), rx(-n, -n), ry(-n, -n),
                                rx(n, -n), ry(n, -n), col);
        }
        // SPEED VECTOR: segment ahead of the blip = position in one MINUTE at
        // the current heading and speed. The convention of real scopes —
        // trajectories read at a glance, and two aircraft at the same spot
        // are told apart by where they are going.
        if (!p.onGround && p.gs > 40.0f) {
            float aheadNm = p.gs * (float)vecMin / 60.0f;
            int   len = (int)(aheadNm / cfg.radiusNm * RPX);
            // FLOOR: the horizon is calibrated on 450 kt, so a helicopter at
            // 90 kt fell back under the threshold and lost its vector
            // entirely — the very symptom we were fixing (max review 07-28).
            // A slightly short direction beats no direction at all.
            if (len < 5) len = 5;
            {
                if (len > 60) len = 60;
                canvas.drawLine(x, y, x + (int)(len * sa),
                                y - (int)(len * ca), col);
            }
        }
        if (p.mil) canvas.drawRect(x - 8, y - 8, 17, 17, TH->milCol);
        if (isSel) canvas.drawCircle(x, y, 9, TH->selCol);
        // Labels used to start at x + 8 with a blip clipped at PANEL_X - 8 =
        // 220, i.e. from 228 on — which is exactly where the right panel is
        // FILLED a few lines below, so every callsign near the right edge was
        // painted over (layout audit 08-01). The label now flips to the LEFT
        // of the blip when its last glyph would reach past PANEL_X - 2; it
        // then ends at x - 8 <= 212, clear of the panel AND of the blip.
        auto labelX = [&](const char* s) {
            const int w = canvas.textWidth(s);
            int lx = x + 8;
            if (lx + w > PANEL_X - 2) lx = x - 8 - w;
            return lx < 2 ? 2 : lx;
        };
        if (p.flight[0]) {
            canvas.setTextColor(isSel ? TH->selCol : TH->cs);
            canvas.drawString(p.flight, labelX(p.flight), y - 3); // phosphor grn
        }
        if (p.mil && p.typ[0]) {      // military: TYPE under the callsign
            canvas.setTextColor(TH->milCol);                  // (F16, A400M...)
            canvas.drawString(p.typ, labelX(p.typ), y + 7);
        }
    }

    // ---- RADAR corners (user 07-27): airport code TOP-LEFT, data freshness
    // TOP-RIGHT (visible in both modes).
    canvas.setTextSize(2);
    canvas.setTextColor(TH->txt1);
    if (cfg.airport[0]) canvas.drawString(cfg.airport, 4, 4);
    canvas.setTextSize(1);
    canvas.setTextDatum(textdatum_t::top_right);
    canvas.drawString(lastPollOkMs
        ? String(sce::T("upd ", "maj ")) +
          String((millis() - lastPollOkMs) / 1000) + "s"
        : String(sce::T("upd --", "maj --")), PANEL_X - 6, 6);
    // THE CLOCK, under the freshness (user 08-04). The two belong together:
    // this corner now answers "what time is it" and "how old is what you are
    // looking at", which is one question asked twice. Same placement and same
    // right-aligned column as the space bin's header, so the two guest bins
    // read alike.
    //
    // GATED ON A SYNCHRONISED CLOCK, and blank rather than wrong until then:
    // the radar already refuses to print its `~` times without NTP, and a
    // header showing 01:00 from an unset RTC would be the one element on
    // screen that lies with confidence.
    {
        const time_t nowT = time(nullptr);
        if (sce::clockSynced(nowT)) {
            struct tm lt;
            localtime_r(&nowT, &lt);      // configTime() already applied tz
            char hhmm[8];
            snprintf(hhmm, sizeof(hhmm), "%02d:%02d", lt.tm_hour, lt.tm_min);
            canvas.setTextColor(TH->txt2);
            canvas.drawString(hhmm, PANEL_X - 6, 18);
        }
    }
    canvas.setTextDatum(textdatum_t::top_left);

    // ---- RIGHT panel: tracked flight info, or radar status ----
    canvas.fillRect(PANEL_X, 0, 320 - PANEL_X, 240, TH->panelBg);
    canvas.drawFastVLine(PANEL_X, 0, 240, TH->sep);
    const int px = PANEL_X + 7;
    canvas.setTextSize(1);
    if (sel) {
        // ================= 3-BLOCK PANEL (user 07-26) ===================
        // Block 1: STATE (identification, alt/speed, distance, freshness)
        // ── separator ──
        // Block 2: DEPARTURE (code, city, time~)  ↓ arrow + time remaining
        // Block 3: ARRIVAL (code, city, time~)
        // ESTIMATED times (great circle / ground speed + NTP, floor 80 kt).
        // Age of the last message received for THIS flight (seconds)
        const uint32_t ageS = (millis() - sel->seenMs) / 1000;
        canvas.setTextSize(2);
        canvas.setTextColor(TH->selCol);
        canvas.drawString(sel->flight[0] ? sel->flight : sel->hex, px, 4);
        canvas.setTextSize(1);
        canvas.setTextColor(TH->txtMain);
        String l1;                     // level on the LEFT, speed on the RIGHT
        if (sel->onGround)            l1 = sce::T("GND", "SOL");
        else if (cfg.metric)          l1 = String((int)dA(sel->altFt)) + uA();
        else if (sel->altFt >= 18000) l1 = "FL" + String((int)(sel->altFt / 100));
        else                          l1 = String((int)sel->altFt) + uA();
        // 78 px column = 13 characters: in metric, "10668m" + "1000km/h"
        // overlapped each other. When it is tight, the altitude drops its
        // suffix — the unit is already given by the legend and the settings
        // (review 07-27b).
        String sp = String((int)dS(sel->gs)) + uS();
        if (cfg.metric && (int)(l1.length() + sp.length()) > 13)
            l1 = String((int)dA(sel->altFt));
        canvas.drawString(l1, px, 24);
        canvas.setTextDatum(textdatum_t::top_right);
        canvas.drawString(sp, 313, 24);
        canvas.setTextDatum(textdatum_t::top_left);
        // Freshness: seconds, then MINUTES beyond 90 s — and in the alert
        // colour as soon as the signal is lost (a counter climbing forever
        // did not say the data was dead).
        canvas.setTextColor(ageS > 90 ? TH->alert : TH->txt1);
        // The 85 px panel is 14 glyphs of this font and "999.9nm vu 45s" is
        // already 14 of them, so the English cannot afford " seen " (two more
        // than " vu "). It says the same thing postfixed — "999.9nm 45m ago" —
        // which is why the two forms are a FORMAT pair and not two words
        // spliced into one expression.
        {
            const String age = (ageS > 90)
                ? String(ageS / 60) + sce::T("m", "mn")
                : String(ageS) + "s";
            char fl[28];
            snprintf(fl, sizeof(fl), sce::T("%s%s %s ago", "%s%s vu %s"),
                     String(dD(distNm(*sel)), 1).c_str(), uD(), age.c_str());
            canvas.drawString(fl, px, 34);
        }
        // Category + type + VARIO: a dedicated line. Emergency SQUAWK
        // (7500 hijack / 7600 radio / 7700 distress) takes PRIORITY.
        {
            bool emerg = !strcmp(sel->squawk, "7500") ||
                         !strcmp(sel->squawk, "7600") ||
                         !strcmp(sel->squawk, "7700");
            // AUDIBLE alert once per appearance of the squawk (option
            // "son"): it is the only radar event that deserves to
            // interrupt the user.
            if (emerg && strcmp(lastEmerg, sel->squawk)) {
                strlcpy(lastEmerg, sel->squawk, sizeof(lastEmerg));
                // The pattern LOOPS, but is BOUNDED: an emergency squawk
                // can hold for hours, and an alarm that never stops
                // ceases to be a signal. With the 2 s of breathing room
                // a cycle lasts 3.7 s: 4 passes ~ 15 s. A CHANGE of
                // squawk rearms it.
                playSeq(SEQ_ALARM, false, 3);
            } else if (!emerg) {
                if (lastEmerg[0]) stopSeq();
                lastEmerg[0] = '\0';
            }
            String tl;
            if (emerg) {
                tl = "SQUAWK " + String(sel->squawk);
            } else {
                if      (sel->cat[0] == 'A' && sel->cat[1] == '7')
                    tl = sce::T("HELI ", "HELICO ");
                else if (sel->cat[0] == 'B' &&
                         (sel->cat[1] == '1' || sel->cat[1] == '4'))
                    tl = sce::T("GLIDER ", "PLANEUR ");
                if (sel->mil) tl = "MIL " + tl;
                tl += sel->typ;
                // vario: ft/min (aero) or m/s (metric)
                if (sel->vrate > 300 || sel->vrate < -300) {
                    int v = sel->vrate > 0 ? sel->vrate : -sel->vrate;
                    tl += (tl.length() ? " " : "");
                    tl += (sel->vrate > 0 ? "^" : "v");
                    tl += cfg.metric ? String(v * 0.00508f, 1) + "m/s"
                                     : String(v);
                }
            }
            // The AIRLINE wins over the aircraft type (more telling, and it
            // was already arriving in the adsbdb answer without being read);
            // the type stays the only marker for military and unknown ones.
            if (!emerg && airline[0] && !sel->mil) tl = airline;
            if (tl.length()) {
                canvas.setTextColor(emerg ? TH->alert
                                          : (sel->mil ? TH->milCol : TH->txt2));
                canvas.drawString(tl, px, 44);
            }
        }
        canvas.drawFastHLine(PANEL_X + 5, 57, 320 - PANEL_X - 10, TH->sep);

        // Origin/destination codes taken from selectedRoute ("LFPG-FIMP")
        char oc[8] = "", dc[8] = "";
        if (selectedRoute[0]) {
            const char* dash = strchr(selectedRoute, '-');
            if (dash) {
                size_t n = (size_t)(dash - selectedRoute);
                if (n >= sizeof(oc)) n = sizeof(oc) - 1;
                memcpy(oc, selectedRoute, n); oc[n] = '\0';
                strlcpy(dc, dash + 1, sizeof(dc));
                char* d2 = strchr(dc, '-'); if (d2) *d2 = '\0';
            }
        }
        time_t nowT = time(nullptr);
        bool  tOk   = sce::clockSynced(nowT) && sel->gs > 80.0f;
        char  hhmm[8];
        // City names are char[20] (up to 19 glyphs) drawn from px = 235 to the
        // panel's text edge 313: 78 px = 13 cells of the 6 px Font0. Glyphs 14
        // to 19 used to fall off the right of the SCREEN with nothing to say
        // so (layout audit 08-01). Right-aligning would only push the head of
        // the name over the radar, and a smaller font buys 2 or 3 glyphs at
        // best, so the name is CUT and the cut is MARKED: 12 glyphs then '.'.
        // A truncation you can see beats five characters that vanish.
        static constexpr int CITY_MAX = 13;          // (313 - 235) / 6
        char oCityF[CITY_MAX + 1], dCityF[CITY_MAX + 1];
        auto fitCity = [](const char* src, char* dst) {
            size_t n = strlen(src);
            if (n <= (size_t)CITY_MAX) { memcpy(dst, src, n + 1); return; }
            memcpy(dst, src, CITY_MAX - 1);
            dst[CITY_MAX - 1] = '.';                 // visible truncation mark
            dst[CITY_MAX]     = '\0';
        };
        // Great-circle distances computed ONCE (4 haversines → 2): the
        // remaining time is no longer produced INSIDE the progress block
        // (fragile ordering dependency, max review 07-27).
        float dDone = origValid ? gcNm(origLat, origLon, sel->lat, sel->lon) : 0;
        float dRem  = destValid ? gcNm(sel->lat, sel->lon, destLat, destLon) : 0;
        float hLeft = (tOk && destValid) ? dRem / sel->gs : 0;

        // ---- THE 6-TO-12 PANEL (user design 08-04, from the Aerospace
        // Tracker reflection). The panel's own separator line BECOMES the
        // route below the flight rule: DEPARTURE at the BOTTOM, ARRIVAL at
        // the TOP - you fly from 6 o'clock to 12 - with the travelled
        // segment SOLID from the bottom up to the aircraft marker and the
        // remainder DOTTED. Swapping the blocks is what makes the rail
        // readable; it also freed the middle row the horizontal bar used
        // to occupy, which now carries the progress % AND the phase chip
        // (CLB/CRZ/DES/GND - aviation abbreviations, language-free, the
        // same thresholds the reversed-leg corrector reasons with).
        static constexpr int RAIL_X = PANEL_X;       // the separator itself
        static constexpr int RAIL_T = 64;            // arrival end (12 o'clock)
        static constexpr int RAIL_B = 234;           // departure end (6 o'clock)

        // ---- Block 2: ARRIVAL (top - where the flight is heading) ----
        canvas.setTextColor(TH->txt1);
        canvas.setFont(&fonts::efontJA_12);
        canvas.drawString(sce::T("ARRIVAL", "ARRIVÉE"), px, 62);
        canvas.setFont(&fonts::Font0);
        canvas.setTextSize(2);
        canvas.setTextColor(TH->txtMain);
        // SHORT code (IATA "CDG") if resolved, else the route's ICAO
        canvas.drawString(destIata[0] ? destIata : (dc[0] ? dc : "?"), px, 77);
        canvas.setTextSize(1);
        canvas.setTextColor(TH->txt2);
        if (destCtry[0]) {             // country pinned RIGHT ("OSL   NO")
            canvas.setTextDatum(textdatum_t::top_right);
            canvas.drawString(destCtry, 313, 81);
            canvas.setTextDatum(textdatum_t::top_left);
        }
        if (destCity[0]) { fitCity(destCity, dCityF);
                           canvas.drawString(dCityF, px, 96); }
        if (tOk && destValid) {                      // the ETA rides with
            time_t arr = nowT + (time_t)(hLeft * 3600.0f);   // its airport
            struct tm tmv; localtime_r(&arr, &tmv);
            snprintf(hhmm, sizeof(hhmm), "~%02d:%02d", tmv.tm_hour, tmv.tm_min);
            canvas.setTextSize(2);
            canvas.setTextColor(TH->accent);
            canvas.drawString(hhmm, px, 107);
        }
        // ---- TRIP zone (middle, framed by its two dotted separators) ----
        {
            canvas.setTextSize(1);
            bool prog = origValid && destValid;
            float fr = (prog && dDone + dRem > 1.0f)
                           ? dDone / (dDone + dRem) : 0;
            for (int x = px; x < 314; x += 6) {
                const int by[2] = { 128, 168 };      // A2.22: pair
                for (int k = 0; k < 2; k++)
                    canvas.drawFastHLine(x, by[k], 3, TH->sep);
            }
            // TRIP ROWS, third cut (user 08-04). The first cut collided
            // the remaining time and distance on one row; the second gave
            // every datum its own row and left the phase as a bare code
            // the user had to ask about ("que veut dire CRZ ?"). Final:
            // the % and the remaining distance SHARE the top row (both
            // short, no collision possible), the phase code carries its
            // READABLE label on the middle row, the ETA countdown keeps
            // the bottom row. The flown distance stays GONE — the % and
            // the rail's solid segment already say it.
            if (prog) {
                canvas.setTextColor(TH->txt1);
                canvas.drawString(String((int)(fr * 100)) + "%", px, 133);
                canvas.setTextColor(TH->txt2);
                canvas.setTextDatum(textdatum_t::top_right);
                canvas.drawString(String((int)dD(dRem)) + uD(), 313, 133);
                canvas.setTextDatum(textdatum_t::top_left);
            }
            // Phase row. Independent of the route: an aircraft with no
            // known route still climbs or descends. Code in Font0 with its
            // state colour, label in efontJA_12 (accents: croisière,
            // montée) in the quiet text colour.
            const char* ph = nullptr, *phLbl = nullptr;
            if (sel->onGround) {
                ph = "GND"; phLbl = sce::T("ground",  "au sol");
            } else if (sel->vrate >  300) {
                ph = "CLB"; phLbl = sce::T("climb",   "montée");
            } else if (sel->vrate < -300) {
                ph = "DES"; phLbl = sce::T("descent", "descente");
            } else if (sel->gs > 150.0f) {
                ph = "CRZ"; phLbl = sce::T("cruise",  "croisière");
            }
            if (ph) {
                // Two fonts of different heights on one row: align them on
                // a SHARED BASELINE instead of guessing top offsets (user
                // 08-04: the label sat visibly lower than the code).
                canvas.setTextDatum(textdatum_t::baseline_left);
                canvas.setTextColor(ph[1] == 'R' ? TH->txt2            // CRZ
                                  : ph[0] == 'G' ? TH->hint            // GND
                                                 : TH->txt1);          // CLB/DES
                canvas.drawString(ph, px, 152);
                canvas.setTextColor(TH->txt2);
                canvas.setFont(&fonts::efontJA_12);
                canvas.drawString(phLbl, px + 26, 152);
                canvas.setFont(&fonts::Font0);
                canvas.setTextDatum(textdatum_t::top_left);
            }
            if (tOk && destValid) {                  // remaining TIME, alone
                // "ETA 0h28" (user 08-04): language-free like the phase
                // chip, shorter than left/reste. Pedantically the duration
                // is the ETE and the clock time above is the ETA, but the
                // colloquial ETA-as-countdown is what everyone reads.
                canvas.setTextColor(TH->txt1);       // on the bottom row
                canvas.drawString(String("ETA ") +
                    String((int)hLeft) + "h" +
                    (((int)(hLeft * 60)) % 60 < 10 ? "0" : "") +
                    String(((int)(hLeft * 60)) % 60),
                    px, 155);
            }
            // ---- THE RAIL: the separator line, made into the route ----
            // Erase the plain separator below the flight rule, then rebuild
            // it as the instrument: SOLID (travelled) from the departure dot
            // up to the aircraft, DOTTED (remaining) up to the arrival dot,
            // aircraft = the same triangle the horizontal bar used, now
            // pointing UP (towards the arrival). Without a complete route
            // the rail is all dots and carries no aircraft - a path is not
            // invented.
            canvas.drawFastVLine(RAIL_X, 58, 240 - 58, TH->panelBg);
            const int yPlane = prog
                ? RAIL_B - (int)((RAIL_B - RAIL_T) * fr) : RAIL_B;
            for (int y = RAIL_T; y < (prog ? yPlane : RAIL_B); y += 6)
                canvas.drawFastVLine(RAIL_X, y, 3, TH->sep);   // remaining
            if (prog && yPlane < RAIL_B) {
                // A2.22: two adjacent columns of thickness, one call site.
                for (int k = 0; k < 2; k++)
                    canvas.drawFastVLine(RAIL_X - k, yPlane, RAIL_B - yPlane,
                                         TH->accent);
                canvas.fillTriangle(RAIL_X - 4, yPlane + 5,
                                    RAIL_X + 4, yPlane + 5,
                                    RAIL_X,     yPlane - 3, TH->selCol);
            }
            // End dots: the pair is what marks the rail as a ROUTE rather
            // than a border.
            const int dotY[2] = { RAIL_T, RAIL_B };  // A2.22: one call site
            for (int k = 0; k < 2; k++)
                canvas.fillRect(RAIL_X - 2, dotY[k] - 2, 5, 5, TH->txt2);
        }
        // ---- Block 3: DEPARTURE (bottom - where the flight came from) ----
        canvas.setTextSize(1);
        canvas.setTextColor(TH->txt1);
        canvas.setFont(&fonts::efontJA_12);
        canvas.drawString(sce::T("DEPARTURE", "DÉPART"), px, 173);
        canvas.setFont(&fonts::Font0);
        canvas.setTextSize(2);
        canvas.setTextColor(TH->txtMain);
        canvas.drawString(origIata[0] ? origIata : (oc[0] ? oc : "?"), px, 188);
        canvas.setTextSize(1);
        canvas.setTextColor(TH->txt2);
        if (origCtry[0]) {
            canvas.setTextDatum(textdatum_t::top_right);
            canvas.drawString(origCtry, 313, 192);
            canvas.setTextDatum(textdatum_t::top_left);
        }
        if (origCity[0]) { fitCity(origCity, oCityF);
                           canvas.drawString(oCityF, px, 207); }
        if (tOk && origValid) {
            time_t dep = nowT - (time_t)((dDone / sel->gs) * 3600.0f);
            struct tm tmv; localtime_r(&dep, &tmv);
            snprintf(hhmm, sizeof(hhmm), "~%02d:%02d", tmv.tm_hour, tmv.tm_min);
            canvas.setTextSize(2);
            canvas.setTextColor(TH->accent);
            canvas.drawString(hhmm, px, 218);
        }
        canvas.setTextSize(1);
        // Status/diagnostic UNDER the radar, left zone (user 07-27): the
        // right panel only shows the flight DATA; the states ("route :
        // recherche", NTP sync...) are displayed here in orange.
        {
            const char* st = nullptr;
            if (!selectedRoute[0]) {
                if      (routeStatus == 1) st = sce::T("route: searching...",
                                                       "route : recherche...");
                else if (routeStatus == 4) st = sce::T("route: network down",
                                                       "route : r\u00e9seau KO");
                else if (routeStatus == 3) st = sce::T("route unknown",
                                                       "route inconnue");
                else                       st = sce::T("no callsign",
                                                       "pas de callsign");
            } else if (ageS > 90)         st = sce::T("signal lost", "signal perdu");
            else if (sel->onGround)        st = sce::T("landed", "atterri");
            else if (!sce::clockSynced(nowT)) st = sce::T("clock: NTP sync...",
                                                       "heure : sync NTP...");
            else if (sel->gs <= 80.0f)     st = sce::T("ETA: low speed",
                                                       "ETA : vitesse basse");
            else if (!destValid)           st = sce::T("airport unknown",
                                                       "a\u00e9roport inconnu");
            canvas.setTextDatum(textdatum_t::top_center);  // centred under the
            canvas.setFont(&fonts::efontJA_12);               // radar, accents
            if (st) { canvas.setTextColor(TH->alert);
                      canvas.drawString(st, CX, 214); }
            footer = sce::T("touch elsewhere to stop",   // ex "tap vide=stop"
                            "toucher ailleurs pour arrêter");
            canvas.setFont(&fonts::Font0);
            canvas.setTextDatum(textdatum_t::top_left);
        }
    } else {
        // Summary laid out with a RUNNING CURSOR + visual hierarchy (user
        // rebalancing 07-27): aircraft counter in LARGE type (the
        // at-a-glance datum), thin separators between groups (the same as
        // the flight panel), no holes when a block is missing
        // (track/IP/HTTP), hint pinned at the bottom.
        auto hsep = [&](int yy) {
            canvas.drawFastHLine(PANEL_X + 5, yy, 320 - PANEL_X - 10, TH->sep);
        };
        int y = 6;
        canvas.setTextColor(TH->title);
        canvas.setTextSize(2);
        canvas.drawString("RADAR", px, y); y = 27;
        hsep(y); y = 33;
        // Counter: figure at size 2, unit at size 1 aligned on the baseline.
        // "?" as long as no poll has succeeded since the (re)centring —
        // like the route, never a misleading "0" (user 07-27).
        String n = lastPollOkMs
            ? String(planeCount) + (planesFull ? "+" : "")
            : String("--");
        canvas.setTextColor(TH->txtMain);
        canvas.drawString(n, px, y);
        canvas.setTextSize(1);
        canvas.setTextColor(TH->txt1);
        canvas.drawString((planeCount > 1 || !lastPollOkMs)
                              ? sce::T("planes", "avions")
                              : sce::T("plane",  "avion"),
                          px + (int)n.length() * 12 + 5, y + 7);
        y = 53;    // "maj" and airport live in the radar CORNERS (07-27)
        canvas.drawString(String((int)dD(cfg.radiusNm)) + uD(), px, y);
        y = 65;
        if (trackQuery[0]) {           // REPLACES the source: same vertical
            canvas.setTextColor(TH->alert);   // budget, 85 px column
            canvas.setFont(&fonts::efontJA_12);
            // "track " and not "tracked ": the 85 px column takes 13 glyphs of
            // efontJA_12 and "tracked AFR470" is 14.
            canvas.drawString(String(sce::T("track ", "traqu\u00e9 ")) + trackQuery,
                              px, y);
            canvas.setFont(&fonts::Font0);
        } else {
            canvas.drawString(cfg.api, px, y);
        }
        y = 81;
        // 84 and not 87, and the colours use a pitch of 11 instead of 12
        // (see below): the column has 4 colour rows + 8 symbol rows to fit
        // under 240, and at the old spacing the LAST one - "vecteur N min" -
        // was drawn at y 236..247, i.e. eight pixels past the canvas, with
        // only its top third visible (layout audit 08-01).
        hsep(y); y = 84;
        {
            // ---- LEGEND: EVERY symbol that can show up on the radar (user
            // 07-27). This zone used to be SHARED with a network/debug view
            // toggled by the up swipe; the diagnostics now live in the
            // RESEAU tab of the settings, which freed that gesture for the
            // METAR view — a hidden second state of the same 85 px column
            // was also the least discoverable place to put an IP address.
            // efontJA font (Unicode): accents rendered — Font0 RESTORED on
            // the way out (6 px/char metrics of the rest of the UI).
            // Line spacing 12: the column has to fit 4 colours + 8
            // symbols under the 240 px (the track line REPLACES the source
            // one, it costs no extra height).
            canvas.setFont(&fonts::efontJA_12);
            struct { uint16_t c; const char* l; } LEG[] = {
                { TH->altG,      sce::T("on ground", "au sol") },
                { TH->altLow,    cfg.metric ? "< 3 000 m" : "< 10 000 ft" },
                { TH->altMid,    cfg.metric ? "< 7 600 m" : "< 25 000 ft" },
                { TH->altCruise, cfg.metric ? "7 600+ m"  : "25 000+ ft"  },
            };
            for (int i = 0; i < 4; i++) {
                canvas.fillRect(px, y + 1, 6, 6, LEG[i].c);
                canvas.setTextColor(LEG[i].c);
                canvas.drawString(LEG[i].l, px + 11, y); y += 11;
            }
            y += 5;                   // breathing room colours / symbols
            canvas.setTextColor(TH->txt1);
            canvas.fillTriangle(px + 3, y, px, y + 8,       // SOLID triangle
                                px + 7, y + 8, TH->txt1);
            canvas.drawString(sce::T("airliner", "ligne"), px + 11, y); y += 12;
            canvas.fillTriangle(px + 3, y + 2, px + 1, y + 7,   // small solid
                                px + 6, y + 7, TH->txt1);
            canvas.drawString(sce::T("light", "l\u00e9ger"), px + 11, y); y += 12;
            canvas.drawTriangle(px + 3, y + 1, px, y + 7,   // HOLLOW delta
                                px + 7, y + 7, TH->txt1);
            canvas.drawString(sce::T("microlight", "ultra-l\u00e9ger"),
                              px + 11, y); y += 12;
            for (int k = 0; k < 2; k++)                     // glider wing
                canvas.drawFastHLine(px, y + 2 + k, 9, TH->txt1);  // A2.22
            canvas.drawFastVLine(px + 4, y + 3, 5, TH->txt1);
            canvas.drawString(sce::T("glider", "planeur"), px + 11, y); y += 12;
            canvas.drawCircle(px + 3, y + 4, 3, TH->txt1);  // helico circle
            const int la[2] = { y + 1, y + 7 }, lb[2] = { y + 7, y + 1 };
            for (int k = 0; k < 2; k++)                     // A2.22
                canvas.drawLine(px, la[k], px + 6, lb[k], TH->txt1);
            canvas.drawString(sce::T("helicopter", "h\u00e9licopt\u00e8re"),
                              px + 11, y); y += 12;
            canvas.drawRect(px, y + 1, 7, 7, TH->milCol);   // military square
            canvas.setTextColor(TH->milCol);
            canvas.drawString(sce::T("military", "militaire"), px + 11, y); y += 12;
            canvas.setTextColor(TH->selCol);
            canvas.drawCircle(px + 3, y + 4, 4, TH->selCol); // tracked ring
            canvas.drawString(sce::T("tracked", "traqu\u00e9"), px + 11, y); y += 12;
            const int lgR[2] = { 4, 1 };   // A2.22: double circle
            for (int k = 0; k < 2; k++)
                canvas.drawCircle(px + 3, y + 4, lgR[k], TH->selCol);
            canvas.drawString(sce::T("off scope", "hors cadran"),
                              px + 11, y); y += 11;
            // Vector horizon: it VARIES with the radius, keeping it quiet
            // would make the segment lengths uninterpretable.
            canvas.setTextColor(TH->txt1);
            canvas.drawFastHLine(px, y + 4, 9, TH->txt1);
            char vl[20];
            snprintf(vl, sizeof(vl), sce::T("vector %d min", "vecteur %d min"),
                     vectorMin());
            canvas.drawString(vl, px + 11, y); y += 12;
            canvas.setFont(&fonts::Font0);
        }
        canvas.setTextDatum(textdatum_t::top_center);
        canvas.setFont(&fonts::efontJA_12);     // accents (user 07-27)
        // PROCESSING alert under the radar: on the MAIN screen, never in a
        // secondary view (the "HTTP xxx" line used to live only in the
        // network/debug view — an API failing after a first successful poll
        // left NO clue at all, max review 07-27; the raw HTTP code is still
        // in the RESEAU settings tab, this line is the one that must be
        // seen without looking for it).
        char abuf[34];
        const char* alert = nullptr;
        if (WiFi.status() != WL_CONNECTED) {
            alert = sce::T("WiFi disconnected...",       // ≠ an API failure
                           "WiFi d\u00e9connect\u00e9...");
        } else if (httpStatus == HTTP_NO_KEY) {
            // Source set from the yaml (or key revoked since) with no key: no
            // request is ever sent, so the counter would sit on "--" for ever.
            // Name the MISSING SETTING — "API en echec" would accuse SafeSky.
            alert = sce::T("safesky key missing (/config)",
                           "clef safesky absente (/config)");
        } else if (httpStatus && httpStatus != 200 &&
                   (pollFailed || !lastPollOkMs)) {
            // BEFORE the "connexion..." branch: at boot against a failing
            // API (or after a recentre, which resets lastPollOkMs to 0) the
            // diagnostic became unreachable (max review 07-27).
            snprintf(abuf, sizeof(abuf),
                     sce::T("API failed (HTTP %d)", "API en \u00e9chec (HTTP %d)"),
                     httpStatus);
            alert = abuf;
        } else if (!lastPollOkMs) {
            alert = pollBusy ? sce::T("searching for aircraft...",
                                      "recherche des avions...")
                             : sce::T("connecting to the API...",
                                      "connexion \u00e0 l'API...");
        }
        if (alert) {
            canvas.setTextColor(TH->alert);
            canvas.drawString(alert, CX, 214);
        }
        // THE MISSING CARD OUTRANKS THE NAVIGATION HINT, and permanently
        // (user 08-03: the warning should last as long as the condition does).
        // The boot notice can be missed — an unattended power cycle shows it to
        // an empty room — while this line is on the screen you actually watch,
        // for as long as there is no card. The hint below it is learnt once;
        // "nothing you type is being saved" never stops being true.
        // Still a HINT and not a banner: plain text, no coloured bar. A
        // permanent full-width alert bar is an alarm, and this board is MEANT
        // to run without a card.
        footer = sdOk
               ? sce::T("touch an aircraft to track",   // hint under the radar
                        "toucher un avion pour traquer")    // (user 07-27)
               : sce::T("no microSD: settings are not saved",
                        "pas de microSD : reglages non enregistres");
        canvas.setFont(&fonts::Font0);
        canvas.setTextDatum(textdatum_t::top_left);
    }
    drawRadarFooter(bmsg ? bmsg : footer, bmsg ? bcol : TH->hint,
                    bmsg != nullptr);   // banner = bar, hint = plain text
    xSemaphoreGive(gMtx);                  // before the SPI push (long)
    canvas.pushSprite(0, 0);
    // AFTER the push, and outside the mutex: BANNER_HOLD means "transient,
    // keep the frame dirty so it expires by itself". Setting uiDirty before
    // the draw would have been wiped by the `uiDirty = false` in loop().
    if (bst == BANNER_HOLD) uiDirty = true;
}

static void applyTrackQueryLocked(const char* q);  // gMtx HELD by the caller
#if SCE_INPUT_TOUCH
static void applyTrackQuery(const char* q);        // takes gMtx itself
#endif

// ---------------------------------------------------------- track keyboard
// LEFT swipe: entering an ICAO callsign (e.g. AFR470) or a fragment (e.g.
// 470) to track. Blocking modal (~60 s max); SceGuest's HTTP keeps being
// served, its exit gesture is suspended while typing.
//
// TOUCH ONLY, and NOT ported to the button boards: an 8x5 grid driven by
// three keys would be worse than what already exists, because a standalone
// build still serves http://<ip>/config, where the same field is a text box
// on a real keyboard. Same for the settings panel below.
#if SCE_INPUT_TOUCH
static void flightEntry() {
    guest.setSwipeExit(false);
    static const char KEYS[37] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    char buf[9]; strlcpy(buf, trackQuery, sizeof(buf));
    bool done = false, ok = false;
    // Feedback of the AEROPORT button, written INSIDE the input box (6..314,
    // 8 px of inner inset like the typed text at x = 14) hence a right text
    // edge of 306. It used to be drawn LEFT-aligned at 254 on a 12-glyph
    // budget, i.e. up to 326 — six pixels off the screen, in BOTH languages
    // (layout audit 08-01). It is now RIGHT-aligned on 306, x derived from
    // the string itself so the two forms cannot drift apart.
    // The padding is LEADING, not trailing, for the same reason it existed at
    // all: each of these strings is drawn with an opaque background and has to
    // erase the previous one in place ("Recherche..." then "Inconnu"). Padded
    // to 12 glyphs they all cover the same 234..306 band; trailing spaces
    // would have erased the band but left the short forms floating mid-panel.
    static constexpr int KB_FB_R = 306;

    auto drawKb = [&]() {
        canvas.fillSprite(TFT_BLACK);
        canvas.setTextSize(1);
        canvas.setTextColor(TH->txt1);
        // 59 characters at 6 px from x=6 ran to 360: SEVEN characters were
        // clipped off the right edge, and they were the example that tells you
        // what an airport code looks like (layout audit 08-01). 44 fits in 270.
        canvas.drawString(sce::T("Callsign (AFR470/470) or airport (RUN/FMEE)",
                                 "Callsign (AFR470/470) ou aeroport (RUN/FMEE)"),
                          6, 6);
        canvas.fillRoundRect(6, 18, 308, 24, 3, TH->panelBg);
        canvas.drawRoundRect(6, 18, 308, 24, 3, TH->sep);
        canvas.setTextSize(2);
        canvas.setTextColor(TH->selCol, TH->panelBg);
        canvas.drawString(buf, 14, 23);
        canvas.fillRect(14 + (int)strlen(buf) * 12 + 2, 23, 2, 14, TH->selCol);
        for (int i = 0; i < 36; i++) {                 // grid: 36 characters
            int r = i / 8, c = i % 8;
            int x = c * 40 + 2, y = 48 + r * 30;
            canvas.fillRoundRect(x, y, 36, 26, 3, TH->panelBg);
            canvas.setTextSize(2);
            canvas.setTextColor(TH->txtMain, TH->panelBg);
            canvas.setCursor(x + 13, y + 6); canvas.print(KEYS[i]);
        }
        // Row 5, cells 4-5: a REAL backspace ⌫ (glyph drawn by hand, the
        // font has none) on a DOUBLE cell; cells 6-7: "Effacer" (user 07-27)
        {
            int y = 48 + 4 * 30;
            canvas.fillRoundRect(162, y, 76, 26, 3, TH->sep);  // ⌫ (2 cells)
            int gx = 162 + 38, gy = y + 13;                    // glyph centre
            canvas.fillRect(gx - 3, gy - 7, 14, 14, TH->txtMain);
            canvas.fillTriangle(gx - 11, gy, gx - 3, gy - 7,
                                gx - 3,  gy + 7, TH->txtMain);
            for (int o = 0; o < 2; o++) {                      // thick cross
                const int ga[2] = { gy - 3, gy + 3 }, gb[2] = { gy + 3, gy - 3 };
                for (int k = 0; k < 2; k++)                 // A2.22
                    canvas.drawLine(gx + o, ga[k], gx + 6 + o, gb[k], TH->sep);
            }
            canvas.fillRoundRect(242, y, 76, 26, 3, TH->sep);  // Clear (2 cells)
            canvas.setTextSize(1);
            canvas.setTextColor(TH->txtMain, TH->sep);
            // CENTRED FROM THE LENGTH, not from a hard-coded 7: the two forms
            // of the label do not have the same width.
            const char* clr = sce::T("Clear", "Effacer");
            canvas.setCursor(242 + (76 - (int)strlen(clr) * 6) / 2, y + 9);
            canvas.print(clr);
        }
        // Action row: Annuler | AEROPORT (recentre) | VOL (track)
        canvas.fillRoundRect(2,   202, 76,  32, 3, TH->panelBg);
        canvas.setTextSize(2); canvas.setTextColor(TH->txt1, TH->panelBg);
        canvas.setCursor(34, 210); canvas.print("X");
        canvas.fillRoundRect(82,  202, 116, 32, 3, TH->selCol);
        canvas.setTextSize(2);                  // "ici" dropped → size 2
        canvas.setTextColor(TFT_BLACK, TH->selCol);
        // Both action labels are now CENTRED ON THE CELL from their own
        // length: "AEROPORT" happened to land on 92 by hand, "AIRPORT" and
        // "FLIGHT" do not. (It also re-centres the French "VOL", which sat
        // 16 px left of centre.)
        const char* apLbl = sce::T("AIRPORT", "AEROPORT");
        canvas.setCursor(82 + (116 - (int)strlen(apLbl) * 12) / 2, 210);
        canvas.print(apLbl);
        canvas.fillRoundRect(202, 202, 116, 32, 3, TH->accent);
        canvas.setTextSize(2);
        canvas.setTextColor(TFT_BLACK, TH->accent);
        const char* flLbl = sce::T("FLIGHT", "VOL");
        canvas.setCursor(202 + (116 - (int)strlen(flLbl) * 12) / 2, 210);
        canvas.print(flLbl);
        canvas.pushSprite(0, 0);
    };

    drawKb();
    uint32_t t0 = millis();
    while (!done && millis() - t0 < 60000) {
        M5.update();
        guest.update();                                 // HTTP kept alive
        auto t = M5.Touch.getDetail();
        if (t.wasClicked() && t.y >= 202 && t.y <= 234) {  // action row
            t0 = millis();                    // zones = the DRAWN rects (2-78 /
                                              // 82-198 / 202-318), gaps inert
            if      (t.x >= 2   && t.x <= 78)  { done = true; ok = false; }
            else if (t.x >= 82 && t.x <= 198) {                      // AEROPORT
                // Resolution DELEGATED to netTask (the SOLE TLS owner — a
                // direct lookup here could coexist with a tiling pass in
                // flight: 2 TLS sessions = heap KO, review 07-26). We wait
                // for the verdict while serving M5/HTTP; netTask cuts its
                // tiling short.
                size_t L = strlen(buf);
                if (L >= 3 && L <= 4) {
                    canvas.setTextSize(1);
                    canvas.setTextColor(TH->alert, TFT_BLACK);
                    const char* fb = sce::T("Searching...", "Recherche...");
                    canvas.drawString(fb, KB_FB_R - (int)strlen(fb) * 6, 30);
                    canvas.pushSprite(0, 0);
                    xSemaphoreTake(gMtx, portMAX_DELAY);
                    strlcpy(airportReq, buf, sizeof(airportReq));
                    airportState = 1;
                    xSemaphoreGive(gMtx);
                    uint32_t w0 = millis();
                    while (airportState == 1 && millis() - w0 < 20000) {
                        M5.update(); guest.update(); delay(50);
                    }
                    if (airportState == 2) {
                        airportState = 0;                            // consumed
                        done = true; ok = false;                    // recentred
                    } else {
                        // READ BEFORE THE RESET: the state is what says which
                        // failure it was, and the line below clears it.
                        const bool st4 = (airportState == 4);
                        xSemaphoreTake(gMtx, portMAX_DELAY);
                        airportState = 0;   // cancelled UNDER MUTEX: netTask
                        xSemaphoreGive(gMtx);   // (CAS) will drop the result
                        // "Unknown" was said for EVERY failure, including the
                        // one where nothing was asked and nothing answered. It
                        // is a statement about the user's airport, made from
                        // the silence of a server (user 08-03, searching CDG
                        // while hexdb.io was down). The two are now separate
                        // words: one blames the code, the other the database.
                        canvas.setTextColor(TH->alert, TFT_BLACK);
                        const char* fbk =
                            (airportState == 4 || st4)
                                ? sce::T("  Base HS", "  Base HS")
                                : sce::T("     Unknown", "     Inconnu");
                        canvas.drawString(fbk,
                                          KB_FB_R - (int)strlen(fbk) * 6, 30);
                        canvas.pushSprite(0, 0);
                        uint32_t w1 = millis();   // HTTP STILL SERVED while
                        while (millis() - w1 < 1200) {   // displaying:
                            M5.update();          // /api/bins/stop must
                            guest.update();       // stay reachable
                            delay(15);
                        }
                        drawKb();
                    }
                } else {                    // invalid length: give feedback
                    canvas.setTextSize(1);  // (before: a silent no-op, and
                    canvas.setTextColor(TH->alert, TFT_BLACK);   // a "dead"
                    const char* fbl = sce::T(" 3-4 letters", " 3-4 lettres");
                    canvas.drawString(fbl,                         // button)
                                      KB_FB_R - (int)strlen(fbl) * 6, 30);
                    canvas.pushSprite(0, 0);
                }
            }
            else if (t.x >= 202 && t.x <= 318) { done = true; ok = true; } // VOL
        } else if (t.wasClicked() && t.y >= 48) {      // grid
            t0 = millis();
            int c = t.x / 40, r = (t.y - 48) / 30;
            if (c >= 0 && c < 8 && r >= 0 && r < 5) {
                size_t L = strlen(buf);
                if (r < 4 || c < 4) {                 // characters (A-Z 0-9)
                    int i = r * 8 + c;
                    if (i < 36 && L < 8) { buf[L] = KEYS[i]; buf[L + 1] = '\0'; }
                } else if (c < 6) {                   // ⌫ (double cell 4-5)
                    if (L) buf[L - 1] = '\0';
                } else {                              // Effacer (double cell 6-7)
                    buf[0] = '\0';
                }
                drawKb();
            }
        }
        delay(15);
    }
    if (ok) applyTrackQuery(buf);
    uiDirty = true;                  // leaving the modal (OK, X, recentre,
                                     // timeout): immediate radar redraw
    armSwipeExit();                  // the modal held it; give it back
}
#endif  // SCE_INPUT_TOUCH — the on-screen keyboard needs a touchscreen

// Commits a tracked callsign (or a fragment, or "" to stop tracking).
//
// FACTORED OUT of the touch keyboard on 08-02 because a board with no
// touchscreen has no keyboard, and the `track` field of http://<ip>/config
// has to reach the SAME code. Every branch below was learned the hard way on
// target; a second, hand-copied implementation behind a web handler is
// precisely the divergence rule 17 is about.
// TWO ENTRY POINTS, and the split is not cosmetic: the web settings submit
// runs with gMtx ALREADY HELD (guest.onSettingsBegin takes it and the whole
// form is applied under one lock), while the keyboard modal runs outside it.
// gMtx is a plain xSemaphoreCreateMutex — NOT recursive — so a single
// self-locking version would deadlock the web handler on the spot. Same
// `...Locked` convention as selectPlaneLocked/clearRouteLocked in this file.
static void applyTrackQueryLocked(const char* q) {
    char buf[10];
    strlcpy(buf, q ? q : "", sizeof(buf));
    // ALWAYS UPPERCASE — the `track` field of /config takes whatever a
    // browser sends. The signed-char trap lives in upAscii's comment.
    upAscii(buf);
    {
        strlcpy(trackQuery, buf, sizeof(trackQuery));
        if (!trackQuery[0]) { selectedHex[0] = '\0'; routeFlight[0] = '\0';
                              clearRouteLocked();
                              routeStatus = 0;
                              routeReqGen = routeReqGen + 1; }
        else {                                          // already there? aim
            bool found = false;
            for (int i = 0; i < planeCount; i++) {
                char up[10]; strlcpy(up, planes[i].flight, sizeof(up));
                upAscii(up);
                if (up[0] && strstr(up, trackQuery)) {
                    selectPlaneLocked(i, false);        // query already set
                    found = true;
                    break;
                }
            }
            if (!found) {
                // OUT OF RANGE (user 07-27): route/cities pre-resolved with
                // the typed callsign (warm cache by the time of the lock) +
                // IMMEDIATE poll → the worldwide /v2/callsign request brings
                // the flight back wherever it is (if it transmits).
                // Fragment: route unknown, the real lock will rearm with the
                // full callsign. The PREVIOUS selection must fall: otherwise
                // needGlobal (trackQuery && !selectedHex) stays FALSE — the
                // worldwide request never leaves — and the panel shows the
                // route of the NEW flight under the header/telemetry of the
                // OLD one (max review 07-27, 3 converging angles).
                selectedHex[0] = '\0';
                strlcpy(routeFlight, trackQuery, sizeof(routeFlight));
                clearRouteLocked();
                routeStatus = 1;
                routeReqGen = routeReqGen + 1;
                pollNow = true;
            }
        }
    }
    uiDirty = true;
}

// Only the touch keyboard calls this; the web setter is already inside the
// lock and uses the Locked form directly.
#if SCE_INPUT_TOUCH
static void applyTrackQuery(const char* q) {
    xSemaphoreTake(gMtx, portMAX_DELAY);
    applyTrackQueryLocked(q);
    xSemaphoreGive(gMtx);
}
#endif

// ------------------------------------------------- yaml persistence (SD)
// Rewrites /stackchan-companion/flightradar.yaml (like the companion does for
// config.yaml: hand-written comments do not survive a save).
static bool saveConfigSd() {
    // SNAPSHOT of cfg under mutex (the other task can mutate it) then write
    // OUTSIDE the lock. Called ONLY from loop() (cfgDirty) — a single SD
    // writer, no concurrent double open(FILE_WRITE) (review 07-26).
    Config c;
    xSemaphoreTake(gMtx, portMAX_DELAY);
    c = cfg;
    xSemaphoreGive(gMtx);
    File f = SD.open(CFG_PATH, FILE_WRITE);   // truncates
    if (!f) { Serial.printf("[radar] ECHEC ecriture %s\n", CFG_PATH);
              return false; }
    f.printf("# flightradar.yaml - config du bin invite flight-radar (docs/guests/README.md)\n"
             "# (reecrit par le bin - reglages ecran ; commentaires non conserves)\n"
             "lat: %.4f\nlon: %.4f\nradius_nm: %d\npoll_s: %lu\napi: %s\n",
             c.lat, c.lon, (int)c.radiusNm,
             (unsigned long)c.pollS, c.api);
    if (c.airport[0]) f.printf("airport: %s\n", c.airport);
    // ONLY when the key is the bin's own. Writing it unconditionally SEALED a
    // companion-inherited offset into this yaml on the first unrelated save,
    // permanently defeating the "bin key wins, companion only fills the hole"
    // precedence — the robot then kept the old zone forever, with nothing to
    // say why. And %.2f, not %.1f: the quarter-hour zones the slider offers
    // (12.75) must survive the round trip.
    if (tzFromOwnYaml) f.printf("tz_offset_h: %.2f\n", c.tzOffsetH);
    f.printf("brightness: %u\n", (unsigned)c.bright);
    f.printf("theme: %u\ntheme_v: 1\n", (unsigned)c.theme);
    f.printf("units: %s\n", c.metric ? "metrique" : "aero");
    f.printf("volume: %u\nnotam_brief: %u\ndock_s: %u\n",
             (unsigned)c.volume, (unsigned)c.notamBrief, (unsigned)c.dockS);
    f.printf("servo: %u\nauto_bright: %u\nauto_night: %u\n"
             "show_ground: %u\nfollow: %u\n",
             (unsigned)c.servo, (unsigned)c.autoLum,
             (unsigned)c.autoNight, (unsigned)c.showGround, (unsigned)c.follow);
    if (c.metarIcao[0]) f.printf("metar_icao: %s\n", c.metarIcao);
    // QUOTED: "12/30@102" holds a "@" and a "/", and the day someone writes a
    // trailing comment after it the unquoted scalar would swallow half of it.
    if (c.metarRwy[0]) f.printf("metar_rwy: \"%s\"\n", c.metarRwy);
    // QUOTED: an API key is free-form (base64, dashes, a leading "#" would be
    // read as a comment). Same treatment as the ha-remote token.
    if (c.skyKey[0])   f.printf("safesky_key: \"%s\"\n", c.skyKey);
    if (c.notamFir[0])  f.printf("notam_fir: \"%s\"\n", c.notamFir);
    if (c.notamUser[0]) f.printf("notam_user: \"%s\"\n", c.notamUser);
    if (c.notamPass[0]) f.printf("notam_pass: \"%s\"\n", c.notamPass);
    // A FULL OR WRITE-PROTECTED CARD lets SD.open(FILE_WRITE) truncate the file
    // and succeed: without this test the page reports "Saved" over settings
    // that no longer exist. ha-remote paid for that on 07-28 and both it and
    // `space` have carried the check since; this bin never got it, so the one
    // failure mode it shares with them was the one it still reported as a
    // success.
    const bool bad = f.getWriteError();
    const size_t sz = f.size();              // read before close()
    f.close();
    // The write error is the failure mode a full or write-protected card
    // produces AFTER open() succeeded - the one this bin reported as a
    // success for a month. The byte count makes a truncated file visible.
    if (bad)
        sce::trace::log("sd", "yaml %s : erreur d ecriture (carte pleine ou protegee ?)",
                        CFG_PATH);
    else
        sce::trace::log("sd", "yaml %s ecrit (%u octets)", CFG_PATH,
                        (unsigned)sz);
    return !bad;
}

// Old numbering (0 Classic, 1 Scope, 2 Night) -> the new one (0 Gundam,
// 1 Gundam night, 2 Scope, 3 Scope night). Without this, an older card
// silently switches to an unrelated theme — in this instance an orange
// night in broad daylight (max review 07-28).
static void migrateTheme() {
    // NOTHING READ, NOTHING TO MIGRATE (08-03). `themeVer` starts at 0 meaning
    // "old numbering", which is the right assumption for a yaml that carries no
    // `theme_v` — and the WRONG one for a board that never opened a yaml at all.
    // A card-less Fire went through here on every boot and had its COMPILED
    // default translated as if it were a legacy value: the default was 2, MAP
    // sent it to 3, and the board came up in Scope night, an astro red screen
    // nobody had chosen (user 08-03). The default is Gundam now, which MAP
    // happens to leave alone, but a migration that runs against a file that
    // does not exist would find the next victim on its own.
    // The latch it sets is deliberately SHORT-LIVED: loadConfig() clears
    // `themeVer` before every read, so a card inserted later still gets its
    // migration. Without that reset this line locked it out for the whole
    // session (review 08-03).
    if (!cfgFileRead) { themeVer = 1; return; }
    if (themeVer >= 1) return;
    static const uint8_t MAP[3] = { 0, 2, 3 };  // Classic->Gundam, Scope, Night
    uint8_t old = (cfg.theme <= 2) ? cfg.theme : 0;
    Serial.printf("[cfg] theme %u (ancienne numerotation) -> %u\n",
                  (unsigned)old, (unsigned)MAP[old]);
    cfg.theme = MAP[old];
    themeVer  = 1;
    cfgDirty  = true;                           // rewritten with theme_v: 1
}

// SETTINGS panel (swipe →): poll period
// (slider 5-60 s), ADS-B source (chips — the touch equivalent of a
// drop-down list). OK = applied + persisted to SD + immediate re-poll.
#if SCE_INPUT_TOUCH
static void settingsPanel() {
    guest.setSwipeExit(false);
    static const char* APIS[N_API] = { "airplanes.live", "adsb.lol",
                                       "adsb.fi", "safesky" };
    int poll = (int)cfg.pollS;
    int lum  = (int)cfg.bright;
    const int lum0 = lum;          // restored on Annuler (live preview)
    // NOT static: sce::T is a runtime pick, and a function-local static would
    // freeze the language on the first opening of the panel.
    const char* THNAMES[THEME_N] = { "Gundam", sce::T("Gundam night", "Gundam nuit"),
                                     "Scope",  sce::T("Scope night",  "Scope nuit") };
    int api  = 0;
    for (int i = 0; i < N_API; i++) if (!strcmp(cfg.api, APIS[i])) api = i;
    int th   = cfg.theme;
    const int th0 = th;            // restored on Annuler (live preview)
    int met  = cfg.metric;         // aero ⇄ metric (display only)
    const int met0 = met;          // restored on Annuler
    bool done = false, ok = false;
    // The SafeSky chip is INERT without a key: the source would poll nothing
    // and the radar would look broken. Sampled once on entry — a key submitted
    // through the web form WHILE this modal is up (guest.update() keeps serving
    // it) only takes effect on the next opening, which beats re-reading a
    // shared field on every frame for a case nobody will hit.
    const bool skyNoKey = (cfg.skyKey[0] == '\0');
    const int  SKY_IDX  = N_API - 1;      // index of "safesky" in APIS

    // ---- GEOMETRY DEFINED ONCE: the drawing AND the touch test both read
    // it (the bands hard-coded on both sides drifted apart at every edit —
    // offset taps, dead zones).
    //   2 sliders · 3 rows of chips · buttons
    // The RADIUS no longer lives here: it is set by ZOOM (two-finger pinch
    // on the radar, double-tap for the steps) — the space freed up hosts
    // the options (user 07-27).
    static constexpr int TRKX0 = 28, TRKX1 = 292;
    struct SRow { int page; int y; const char* lbl; int* v;
                  int lo, hi, step; const char* u; };
    // Declared HERE and not with the other option variables: the slider table
    // below takes its address, and that table is what defines the row.
    int oVol = cfg.volume;
    const int vol0 = cfg.volume;   // restored on Annuler (live preview)
    // THREE rows now: the sound is a SLIDER too (user 07-31). A cycling chip
    // fitted the option grid, but it made you tap through four levels to reach
    // the fifth, and a volume is exactly the kind of quantity a track is for.
    // The row was COMPACTED to make them fit: the value drops to size 1 on the
    // label's own line, the track climbs from r.y+26 to r.y+16 and the grip
    // from 11 px to 9. 45 px per row became 30 — which is what buys the second
    // slider on the RADAR page AND leaves the AFFICHAGE page room to breathe.
    SRow rows[3] = {
        { 1,  38, sce::T("Refresh rate", "Rafraichissement"), &poll,  5,  60, 1, "s" },
        { 1,  76, sce::T("Sound volume", "Volume du son"),    &oVol,  0, 100, 5, "%" },
        { 0, 160, sce::T("Brightness",   "Luminosite"),       &lum,  10, 255, 5, ""  },
    };
    auto rowBand = [](const SRow& r, int ty) {
        return ty >= r.y + 8 && ty <= r.y + 34;    // track at r.y + 16
    };

    // ---- Cell grids: ONE description, read by the drawing AND by the
    // touch test. Every target is at least 98 x 26 px — the old 48 x 20
    // toggles were missed by the finger.
    struct Grid { int x0, y0, w, h, gapX, gapY, cols; };
    // ONE RIGHT EDGE FOR THE WHOLE PANEL: 312, the same x the header rule
    // uses elsewhere in this file (VIEW_RULE_R). The four grids used to stop
    // at 309, 310, 312 and 314 — four margins on one screen, visible as a
    // ragged right edge (layout audit 08-01). 312 is also the only candidate
    // that divides cleanly for EVERY grid, which is why it wins over 310:
    //   2 cols from x0=10, gap 4: (312 - 10 - 4)     / 2 = 149  (310 -> 148)
    //   3 cols from x0=10, gap 4: (312 - 10 - 2*4)   / 3 =  98  (310 -> 97.33)
    //   2 cols from x0= 6, gap 6: (312 -  6 - 6)     / 2 = 150  (310 -> 149)
    //   3 cols from x0= 6, gap 3: (312 -  6 - 2*3)   / 3 = 100  (unchanged)
    // TAB STRIP: 3 tabs of 100 px on the WHOLE width (6 + 3x100 + 2x3 = 312).
    // Two tabs used to sit at the top RIGHT, next to a "Reglages" title;
    // a third one there overflowed the panel (150 + 3x78 + 2x4 = 392 > 320)
    // and shrinking the cells would have taken the targets back under the
    // ~70 px the finger actually hits. The title went instead: three named
    // tabs already say what the screen is, and they are the one control the
    // finger looks for first.
    static constexpr Grid G_TAB   = {   6,   2, 100, 26,  3,  0, 3 };
    static constexpr Grid G_THEME = {  10,  46, 149, 26,  4,  4, 2 };
    static constexpr Grid G_UNITS = {  10, 124, 149, 26,  4,  4, 2 };
    // SOURCE: 4 chips since SafeSky joined, so 2 rows of 2 cells 149 px wide
    // and NOT 4 of 72 — "airplanes.live" is 14 characters, i.e. 84 px in the
    // 6 px font, and a chip that truncates its own label defeats the point of
    // naming the source. The two rows only fit on the RESEAU page (see the
    // page split below).
    static constexpr Grid G_API   = {  10,  46, 149, 26,  4,  4, 2 };
    // OPTIONS: 28 px tall and not 26. Two rows of 26 with a 4 px gutter ended
    // at 180 and the buttons start at 204, leaving a 23 px hole at the bottom
    // of the RADAR page — the one page whose controls could have used it
    // (layout audit 08-01). The height went into the TARGETS rather than into
    // a gap: 124 + 2*28 + 4 = 184, which leaves exactly the 20 px of air the
    // buttons are documented to need below, and nothing dead in between.
    static constexpr Grid G_OPT   = {  10, 124,  98, 28,  4,  4, 3 };
    // Annuler / OK take the WHOLE bottom edge (user 07-31): they were two
    // 130 px cells centred in 320 with 20 px of margin either side and 20
    // between, leaving a 4 px strip of nothing under them. The two most-tapped
    // targets of the panel had no reason to be the smallest.
    // The MARGIN above them does the separating on its own (user 07-31): a
    // rule was tried there and removed — with 20 px of air already between the
    // last control and the buttons, the line was one more horizontal stroke on
    // a panel that has enough of them.
    static constexpr Grid G_BTN   = {   6, 204, 150, 32,  6,  0, 2 };
    auto gX = [](const Grid& g, int i) { return g.x0 + (i % g.cols) * (g.w + g.gapX); };
    auto gY = [](const Grid& g, int i) { return g.y0 + (i / g.cols) * (g.h + g.gapY); };
    auto gHit = [&](const Grid& g, int n, int x, int y) {
        for (int i = 0; i < n; i++)
            if (x >= gX(g, i) && x <= gX(g, i) + g.w &&
                y >= gY(g, i) && y <= gY(g, i) + g.h) return i;
        return -1;                       // gutter: trigger nothing
    };
    // 0 AFFICHAGE · 1 RADAR · 2 RESEAU. The ADS-B SOURCE lives on the RESEAU
    // page, not the RADAR one: it is the SERVICE we talk to, it now needs two
    // rows of chips, and the RADAR page has NO vertical slack left — the two
    // sliders run 38..102, the "Options" label sits at 112 and the two rows of
    // toggles fill 124..184, i.e. down to the 20 px margin the buttons need
    // (the "6 px" this comment used to claim predated both the second slider
    // and the taller cells — layout audit 08-01). A fourth chip does not fit
    // there. Its neighbours on the RESEAU page are exactly what tells you
    // whether that service answers (IP, signal, HTTP code) and the SafeSky
    // "no key" warning.
    int page = 0;
    // Options: 6 toggles on a 3-column grid (98 x 28 px each) = two FULL rows.
    // servo/sound OFF by default (intrusive); the display automations are
    // ON by default (non-intrusive).
    struct Opt { const char* lbl; int* v; };
    int oServo = cfg.servo, oLum = cfg.autoLum,
        oNight = cfg.autoNight, oGnd = cfg.showGround, oFollow = cfg.follow,
        oBrief = cfg.notamBrief;
    // WHOLE labels grouped by nature: "lum." / "sol" did not say what they
    // did, and the six toggles mixed robot behaviour with display options
    // (user 07-28).
    // SIXTH TOGGLE, at no layout cost (user 08-01): the grid is 3 columns over
    // two rows, so the fifth option left the sixth cell EMPTY. `notam_brief`
    // was reachable only from /config in a browser, while the deck it filters
    // is one of the four screens the up swipe cycles through — the one option
    // you want to flip while looking at the thing it changes.
    Opt opts[6] = {
        { "servos",                          &oServo },
        { sce::T("follow",     "suivi"),     &oFollow },
        { sce::T("auto light", "lum. auto"), &oLum   },
        { sce::T("auto night", "nuit auto"), &oNight  },
        { sce::T("on ground",  "au sol"),    &oGnd   },
        { sce::T("NOTAM brief","NOTAM brief"), &oBrief },
    };

    // Generic cell: frame + centred label. Active = solid chip.
    auto cell = [&](const Grid& g, int i, const char* label, bool on,
                    uint16_t col) {
        int x = gX(g, i), y = gY(g, i);
        canvas.fillRoundRect(x, y, g.w, g.h, 3, on ? col : TH->panelBg);
        if (!on) canvas.drawRoundRect(x, y, g.w, g.h, 3, TH->sep);
        canvas.setTextSize(1);
        canvas.setTextColor(on ? TFT_BLACK : TH->txt1,
                            on ? col : TH->panelBg);
        canvas.setCursor(x + (g.w - (int)strlen(label) * 6) / 2,
                         y + (g.h - 8) / 2);
        canvas.print(label);
    };
    auto slider = [&](const SRow& r) {
        canvas.setFont(&fonts::Font0);
        canvas.setTextSize(1);
        canvas.setTextColor(TH->txt1);
        canvas.drawString(r.lbl, 10, r.y);
        // THE VALUE ALONE is bigger (user 07-31): efontJA_12 against the
        // label's 8 px Font0. setTextSize only MULTIPLIES, so the next step up
        // there would have been 16 px and the whole compacted row would be
        // back where it started; a 12 px font is the one way to gain a few
        // pixels on the figure without paying for them twice.
        // It is PROPORTIONAL, so the value is placed with textWidth and not
        // with a glyph count — 6 px per character stops being true here — and
        // lifted 2 px so its centre lines up with the label's.
        String v = r.u[0] ? String(*r.v) + " " + r.u : String(*r.v);
        canvas.setFont(&fonts::efontJA_12);
        canvas.setTextColor(TH->accent);
        canvas.drawString(v, 312 - canvas.textWidth(v), r.y - 2);  // panel edge
        canvas.setFont(&fonts::Font0);   // the shapes below assume nothing
        int ty = r.y + 18;
        int hx = TRKX0 + (*r.v - r.lo) * (TRKX1 - TRKX0) / (r.hi - r.lo);
        if (hx > TRKX1) hx = TRKX1;
        // A2.22: GCC 8.4 Xtensa drops the SECOND of two similar drawing
        // calls in the same body. Extracting this cursor into a lambda made
        // the elimination MORE likely, not less — three pairs were exposed
        // there (arrows, track, chip). A single call site per shape, in a
        // loop (max review 07-28; same remedy as ha-remote).
        const int      arrX[2] = { 20, 300 }, arrT[2] = { 8, 312 };
        for (int i = 0; i < 2; i++)
            canvas.fillTriangle(arrX[i], ty - 3, arrX[i], ty + 11,
                                arrT[i], ty + 4, TH->txt1);
        const int      trkW[2] = { TRKX1 - TRKX0, hx - TRKX0 };
        const uint16_t trkC[2] = { TH->sep, TH->accent };
        for (int i = 0; i < 2; i++)
            canvas.fillRoundRect(TRKX0, ty, trkW[i], 8, 3, trkC[i]);
        const int      dotR[2] = { 9, 4 };
        const uint16_t dotC[2] = { TH->accent, (uint16_t)TFT_BLACK };
        for (int i = 0; i < 2; i++)
            canvas.fillCircle(hx, ty + 4, dotR[i], dotC[i]);
    };

    auto drawS = [&]() {
        canvas.fillSprite(TFT_BLACK);
        // THREE TABS: eight groups of controls in 240 px forced tiny targets
        // and truncated labels. Splitting by INTENT — what we look at / what
        // the radar does / what the network does — leaves room to make every
        // control legible and tappable (user 07-28).
        // NOT static, for the same reason as THNAMES above.
        const char* TABS[3] = { sce::T("DISPLAY", "AFFICHAGE"), "RADAR",
                                sce::T("NETWORK", "RESEAU") };
        for (int i = 0; i < 3; i++)
            cell(G_TAB, i, TABS[i], page == i, TH->accent);

        if (page == 0) {
            canvas.setTextSize(1);
            canvas.setTextColor(TH->hint);
            // "Theme" AND NOT "Theme accentue": this panel draws in Font0,
            // which has no accented glyph — the e-grave would come out blank.
            // Through T() anyway, so the next reader sees the choice was made
            // rather than forgotten, and knows where the constraint lives.
            canvas.drawString(sce::T("Theme", "Theme"), 10, 34);
            for (int i = 0; i < THEME_N; i++)
                cell(G_THEME, i, THNAMES[i], i == th, THEMES[i].selCol);
            canvas.setTextColor(TH->hint);
            canvas.drawString(sce::T("Units", "Unites"), 10, 112);
            const char* UN[2] = { "aero (nm, kt, ft)",
                                  sce::T("metric (km, m)", "metrique (km, m)") };
            for (int i = 0; i < 2; i++)
                cell(G_UNITS, i, UN[i], met == i, TH->accent);
            slider(rows[2]);
        } else if (page == 1) {
            slider(rows[0]);
            slider(rows[1]);
            canvas.setTextSize(1);
            canvas.setTextColor(TH->hint);
            canvas.drawString(sce::T("Options", "Options"), 10, 112);
            for (int i = 0; i < 6; i++)
                cell(G_OPT, i, opts[i].lbl, *opts[i].v != 0, TH->accent);
        } else {
            // ---- RESEAU: the SERVICE (source chips) and the state of the
            // link to it. These diagnostics used to hide behind an up swipe
            // on the summary — the least discoverable place for the one datum
            // you need before opening a browser, the IP.
            canvas.setTextSize(1);
            canvas.setTextColor(TH->hint);
            canvas.drawString(sce::T("Source ADS-B", "Source ADS-B"), 10, 34);
            for (int i = 0; i < N_API; i++)
                cell(G_API, i, APIS[i], i == api, TH->accent);
            // SafeSky needs a key (30-day free trial). The chip REFUSES to
            // select without one and the reason is written right under it: a
            // silent selection would have shown a permanently empty radar with
            // nothing on screen to explain it. The key cannot be typed with a
            // finger (a 30+ character token), hence the pointer to /config.
            if (skyNoKey) {
                canvas.setFont(&fonts::efontJA_12);
                canvas.setTextColor(TH->alert);
                // y 100 and not 104: the warning is 12 px tall and the first
                // diagnostic row starts at 114, so it overlapped by 2 px - and
                // it is shown BY DEFAULT, the key being empty out of the box
                // (layout audit 08-01).
                canvas.drawString(sce::T("safesky: key missing, see /config",
                                         "safesky : clef absente, voir /config"),
                                  10, 100);
                canvas.setFont(&fonts::Font0);
            }
            // Label / value rows, 13 px apart: 6 lines under the chips fit
            // between them and the rule above the buttons (y=194). Started at
            // 118 while the buttons sat lower; the last row then ended ON the
            // rule.
            canvas.setFont(&fonts::efontJA_12);
            char st[24];
            int yy = 114;
            auto row = [&](const char* label, const String& val, uint16_t col) {
                canvas.setTextColor(TH->hint);
                canvas.drawString(label, 10, yy);
                canvas.setTextColor(col);
                canvas.drawString(val, 150, yy);
                yy += 13;
            };
            bool sta = (WiFi.status() == WL_CONNECTED);
            row("IP", sta ? ipStr
                          : (ipStr.length() ? "AP " + ipStr
                                            : String(sce::T("no WiFi", "WiFi KO"))),
                sta ? TH->txt2 : TH->alert);
            // On the access point, the NETWORK TO JOIN is the one fact worth
            // more than the address: nobody can open the page without it, and
            // this screen is the only place it is ever shown. The page it
            // leads to is where a real network gets entered.
            if (!sta && guest.isAp())
                row(sce::T("JOIN", "REJOINDRE"), guest.apSsid(), TH->accent);
            row("SIGNAL", String((int)WiFi.RSSI()) + " dB", TH->txt2);
            snprintf(st, sizeof(st), "%u / %u k",
                     (unsigned)(ESP.getFreeHeap() / 1024),
                     (unsigned)(ESP.getMinFreeHeap() / 1024));
            row(sce::T("MEMORY free / min", "MÉMOIRE libre / min"), st, TH->txt2);
            snprintf(st, sizeof(st), "%.1f M", ESP.getFreePsram() / 1048576.0f);
            row("PSRAM", st, TH->txt2);
            snprintf(st, sizeof(st), "%u / %u o",
                     (unsigned)uxTaskGetStackHighWaterMark(nullptr),
                     netTaskHandle ? (unsigned)
                         uxTaskGetStackHighWaterMark(netTaskHandle) : 0);
            row(sce::T("STACKS loop / net", "PILES loop / net"), st, TH->txt2);
            snprintf(st, sizeof(st), "%s / %s", sdOk ? "OK" : sce::T("FAIL", "KO !"),
                     httpStatus ? String(httpStatus).c_str() : "--");
            row("SD / HTTP", st, (sdOk && (!httpStatus || httpStatus == 200))
                                    ? TH->txt2 : TH->alert);
            canvas.setFont(&fonts::Font0);
        }

        cell(G_BTN, 0, sce::T("Cancel", "Annuler"), false, TH->accent);
        canvas.fillRoundRect(gX(G_BTN, 1), G_BTN.y0, G_BTN.w, G_BTN.h, 3,
                             TH->accent);
        canvas.setTextSize(2);
        canvas.setTextColor(TFT_BLACK, TH->accent);
        canvas.setCursor(gX(G_BTN, 1) + (G_BTN.w - 24) / 2, G_BTN.y0 + 9);
        canvas.print("OK");
        canvas.pushSprite(0, 0);
    };

    drawS();
    uint32_t t0 = millis();
    while (!done && millis() - t0 < 90000) {
        M5.update();
        soundTick();                                     // sound preview
        guest.update();                                  // HTTP kept alive
        auto t = M5.Touch.getDetail();
        bool handled = false;
        // ---- drag along the slider track of the current page ----
        if (t.isPressed() && t.x >= TRKX0 && t.x <= TRKX1) {
            for (int i = 0; i < 3 && !handled; i++) {
                SRow& r = rows[i];
                if (r.page != page || !rowBand(r, t.y)) continue;
                t0 = millis();
                int v = r.lo + (t.x - TRKX0) * (r.hi - r.lo) / (TRKX1 - TRKX0);
                v = (v / r.step) * r.step;
                if (v < r.lo) v = r.lo;
                if (v > r.hi) v = r.hi;
                if (v != *r.v) {
                    *r.v = v;
                    if (r.v == &oVol) {
                        // LIVE, like the brightness: soundTick reads cfg.volume
                        // note by note, so a preview has to be applied for real
                        // and undone by Annuler. Arming the pattern with a
                        // temporarily raised level would have changed nothing —
                        // playSeq only PUBLISHES the sequence, the notes are
                        // emitted milliseconds later.
                        cfg.volume = (uint8_t)oVol;
                        // Re-armed only when nothing is playing: re-triggering
                        // on every step of a drag restarted the pattern dozens
                        // of times and you never heard past its first note.
                        if (oVol) { if (!seqPtr) playSeq(SEQ_ALARM, true); }
                        else      stopSeq();
                    }
                    if (r.v == &lum) {  // A2.2: PMIC I2C — preview capped 10 Hz
                        oLum = 0;       // setting by hand DISABLES the auto:
                                        // the two fought over the screen
                                        // intermittently (review 07-27c)
                        static uint32_t lastBr = 0;
                        if (millis() - lastBr > 100) {
                            lastBr = millis();
                            M5.Display.setBrightness((uint8_t)lum);
                        }
                    }
                    drawS();
                }
                handled = true;
            }
        }
        if (!handled && t.wasClicked()) {
            t0 = millis();
            int hit = gHit(G_TAB, 3, t.x, t.y);          // tabs
            if (hit >= 0) {
                if (hit != page) { page = hit; drawS(); }
                handled = true;
            }
            for (int i = 0; i < 3 && !handled; i++) {    // arrows ◄ ►
                SRow& r = rows[i];
                if (r.page != page || !rowBand(r, t.y)) continue;
                int v = *r.v;
                if      (t.x < TRKX0) v -= r.step;
                else if (t.x > TRKX1) v += r.step;
                else break;
                if (v < r.lo) v = r.lo;
                if (v > r.hi) v = r.hi;
                if (v != *r.v) {
                    *r.v = v;
                    if (r.v == &lum) M5.Display.setBrightness((uint8_t)lum);
                    if (r.v == &oVol) {                  // same LIVE rule
                        cfg.volume = (uint8_t)oVol;
                        if (oVol) { if (!seqPtr) playSeq(SEQ_ALARM, true); }
                        else      stopSeq();
                    }
                    drawS();
                }
                handled = true;
            }
            if (handled) {
                // already handled
            } else if ((hit = gHit(G_BTN, 2, t.x, t.y)) >= 0) {
                done = true;
                ok   = (hit == 1);
            } else if (page == 0) {
                if ((hit = gHit(G_THEME, THEME_N, t.x, t.y)) >= 0 && hit != th) {
                    th = hit;
                    TH = &THEMES[th];                    // LIVE preview
                    drawS();
                } else if ((hit = gHit(G_UNITS, 2, t.x, t.y)) >= 0 && hit != met) {
                    met = hit;
                    cfg.metric = (uint8_t)met;           // LIVE preview
                    drawS();
                }
            } else if (page == 2) {
                if ((hit = gHit(G_API, N_API, t.x, t.y)) >= 0 && hit != api) {
                    // Keyless SafeSky: the tap is REFUSED, not applied then
                    // silently broken. The warning under the chips is already
                    // on screen and says where to enter the key.
                    if (!(hit == SKY_IDX && skyNoKey)) { api = hit; drawS(); }
                }
            } else {
                if ((hit = gHit(G_OPT, 6, t.x, t.y)) >= 0) {
                    *opts[hit].v = !*opts[hit].v;
                    drawS();
                }
            }
        }
        // The RESEAU page shows LIVE figures (heap, signal, HTTP code): with
        // a redraw on touch only, a diagnostic screen would freeze on the
        // values it had when it opened — the opposite of its purpose. 1 s
        // tick, and only on that page (the others are static).
        static uint32_t tickMs = 0;
        if (page == 2 && millis() - tickMs >= 1000) { tickMs = millis(); drawS(); }
        delay(15);
    }
    // FINAL value applied in every case: the last step of the drag may have
    // been capped by the 10 Hz throttle above.
    M5.Display.setBrightness((uint8_t)(ok ? lum : lum0));
    if (!ok) { TH = &THEMES[th0]; cfg.metric = (uint8_t)met0;
               cfg.volume = (uint8_t)vol0; stopSeq(); }             // cancelled
    if (ok) {
        bool changed = (int)cfg.pollS != poll || lum != lum0 ||
                       th != th0 || met != met0 ||
                       oServo != (int)cfg.servo || oVol != vol0 ||
                       oLum != (int)cfg.autoLum || oNight != (int)cfg.autoNight ||
                       oGnd != (int)cfg.showGround || oFollow != (int)cfg.follow ||
                       oBrief != (int)cfg.notamBrief ||
                       strcmp(cfg.api, APIS[api]);
        // Write under mutex: pollPlanes LATCHES cfg.* at the start of a
        // cycle under the same lock — no torn source/centre (review 07-26).
        xSemaphoreTake(gMtx, portMAX_DELAY);
        cfg.pollS    = (uint32_t)poll;
        cfg.bright   = (uint8_t)lum;
        cfg.theme    = (uint8_t)th;
        cfg.metric   = (uint8_t)met;
        cfg.servo      = (uint8_t)oServo;
        cfg.volume     = (uint8_t)oVol;
        cfg.autoLum    = (uint8_t)oLum;
        cfg.autoNight  = (uint8_t)oNight;
        cfg.showGround = (uint8_t)oGnd;
        cfg.follow     = (uint8_t)oFollow;
        // Toggling the briefing filter changes WHICH NOTAM are collected,
        // not just which are drawn, so the deck has to be rebuilt. Dropping
        // the success stamp is what makes the next netTask pass refetch;
        // without it the filter would appear to do nothing for 30 minutes.
        if (oBrief != (int)cfg.notamBrief) {
            // BOTH stamps, exactly like the long press — and the reason is
            // written over there too: clearing the "last success" opens the
            // outer gate, but `notamTryMs` (the 60 s spacing set on EVERY
            // attempt, successful ones included) then swallows the very
            // refetch you just asked for. Only notamForceMs resets it.
            // Shipped with only the first one (user 08-01: "que j'active ou
            // non, j'ai toujours 1/14") — the toggle looked inert for a minute.
            notamOkMs    = 0;
            notamForceMs = millis() ? millis() : 1;
        }
        cfg.notamBrief = (uint8_t)oBrief;
        TH = &THEMES[th];            // (already active via the live preview)
        strlcpy(cfg.api, APIS[api], sizeof(cfg.api));
        xSemaphoreGive(gMtx);
        if (changed) { cfgDirty = true; pollNow = true; }
        gAutoLumReset = true;              // same reset as the web path
    }
    uiDirty = true;                  // leaving the modal: immediate redraw
    armSwipeExit();                  // the modal held it; give it back
}
#endif  // SCE_INPUT_TOUCH — replaced by http://<ip>/config on button boards

// Cycles the track to the next/previous flight of planes[] (swipe ↑/↓ on
// the right PANEL — the info column acts as a list, user 07-26).
static void cyclePlane(int dir) {
    xSemaphoreTake(gMtx, portMAX_DELAY);
    if (planeCount > 0) {
        int idx = -1;
        if (selectedHex[0])
            for (int i = 0; i < planeCount; i++)
                if (!strcmp(planes[i].hex, selectedHex)) { idx = i; break; }
        int next = (idx < 0) ? (dir > 0 ? 0 : planeCount - 1)
                             : (idx + dir + planeCount) % planeCount;
        selectPlaneLocked(next, true);
        // Cycling onto a blip WITHOUT a callsign: cut the residual track,
        // otherwise the re-lock brings the OLD flight back on the next poll
        // (user 07-27: "it comes back all by itself").
        if (!planes[next].flight[0]) trackQuery[0] = '\0';
    }
    xSemaphoreGive(gMtx);
}

// ------------------------------------------------------------------- touch
// Inside the guard, like everything else that only a touchscreen can produce:
// left outside it, a Fire build carried gesture state that no code path could
// ever write, which is worse than useless — it reads as if touch were handled.
#if SCE_INPUT_TOUCH
static int  _tsx = 0, _tsy = 0;
static uint32_t _tsMs = 0;      // when the finger went down (long press)
static bool _tsHeld = false;    // the hold already fired for this press
static bool _tsActive = false;
#endif

// Applies a new radius (nm): clamp, purge of what falls outside, re-poll
// and persistence — the same contract as the settings OK.
// `commit` false = PREVIEW during a gesture (scale only, no SD write and
// no purge); true = end of gesture: persistence + purge + re-poll.
static void applyRadius(int nm, bool commit) {
    if (nm < 10)  nm = 10;
    if (nm > 500) nm = 500;
    xSemaphoreTake(gMtx, portMAX_DELAY);
    if ((int)cfg.radiusNm != nm) { cfg.radiusNm = (float)nm; uiDirty = true; }
    if (commit) {
        int w = 0;
        for (int i = 0; i < planeCount; i++)
            if (distNm(planes[i]) <= (float)nm ||
                !strcmp(planes[i].hex, selectedHex)) {
                if (w != i) planes[w] = planes[i];
                w++;
            }
        planeCount = w;
        cfgDirty = true; pollNow = true;
    }
    xSemaphoreGive(gMtx);
}

// ---------------------------------------------------------------- UiEvent
// THE SINGLE CONSUMER of the interaction vocabulary declared in input.h.
// Touch gestures and button presses both end up here, so an action behaves
// identically whichever board asked for it — and a new action is added in one
// place instead of two.
// DOCK MODE's activity stamp. Every producer of a UiEvent lands here, so
// this is the one place that sees "the user (or the dock itself) just acted"
// — the auto-cycle clock restarts on it. Touch that does not become an event
// (a blip tap) and raw button presses stamp it at their handlers.
static uint32_t dockLastMs = 0;

static void applyEvent(fr::UiEvent e) {
    using fr::UiEvent;
    // Named after input.h's declaration order: the trace has to say WHICH
    // action ran, or the reader is sent back to the enum to count. This is
    // the single consumer of every producer (touch and buttons), so one line
    // here dates every user action in the capture.
    static const char* const EV_NAMES[] = {
        "None", "NextView", "Back", "PagePrev", "PageNext",
        "Refresh", "NetInfo", "ZoomCycle", "TrackPrev", "TrackNext" };
    if (e != UiEvent::None && (size_t)e < sizeof(EV_NAMES) / sizeof(EV_NAMES[0]))
        sce::trace::log("ui", "evenement %s", EV_NAMES[(size_t)e]);
    dockLastMs = millis();
    switch (e) {
        case UiEvent::NextView:  stepView();                 break;
        case UiEvent::Back:      setViewLevel(VIEW_RADAR);    break;
        // Paging belongs to the NOTAM deck, and ONLY to it: moving the deck's
        // index from a screen that is not the deck would be discovered two
        // screens later, on a card nobody chose. Both producers decide this
        // BEFORE emitting — the touch swipes inside a `viewLevel == VIEW_NOTAM`
        // test, the buttons through `fr::Screen::Deck` — so this guard is the
        // belt to their braces, and it stays: it is the only place that sees
        // every producer, present and future.
        case UiEvent::PagePrev:
            if (viewLevel == VIEW_NOTAM && notamCount > 0) notamStep(-1);
            break;
        case UiEvent::PageNext:
            if (viewLevel == VIEW_NOTAM && notamCount > 0) notamStep(+1);
            break;
        case UiEvent::TrackPrev: cyclePlane(-1);              break;
        case UiEvent::TrackNext: cyclePlane(+1);              break;
        case UiEvent::Refresh:
            // BOTH clocks: the METAR and the TAF ride in one request, the
            // NOTAM is its own. Clearing the "last success" stamps is what
            // makes the next netTask pass fetch immediately, and clearing the
            // RETRY stamps is what stops the 60 s failure spacing from
            // swallowing the very refresh you just asked for.
            metarOkMs = 0; metarDueMs = 0; metarPrevObs = 0; metarCycleS = 1800; notamOkMs = 0;
            metarForceMs = notamForceMs = millis() ? millis() : 1;
            holdArmMs  = 0;
            holdFireMs = millis() ? millis() : 1;
            uiDirty = true;
            break;
        case UiEvent::ZoomCycle: {
            static const int PRESETS[4] = { 50, 100, 250, 500 };
            int cur = (int)cfg.radiusNm, nxt = PRESETS[0];
            for (int i = 0; i < 4; i++)
                if (PRESETS[i] > cur) { nxt = PRESETS[i]; break; }
            applyRadius(nxt, true);
            break;
        }
        case UiEvent::NetInfo:
            netInfoMs = millis() ? millis() : 1;
            uiDirty = true;
            break;
        case UiEvent::None:
        default: break;
    }
}

#if SCE_INPUT_BUTTONS
// ------------------------------------------------------------ buttons (Fire)
// The whole navigation on a board with no touchscreen. Everything hard lives
// in fr::ButtonFsm (input.h), which is pure and natively tested; this is the
// three lines of glue that read M5's button objects and hand the result to the
// shared consumer.
static fr::ButtonFsm btnFsm;

static void handleButtons() {
    const uint32_t now = millis();
    const bool lvl[fr::BTN_COUNT] = {
        M5.BtnA.isPressed(), M5.BtnB.isPressed(), M5.BtnC.isPressed()
    };
    // Any finger on any button restarts the dock clock — including holds and
    // chords that never become a UiEvent.
    if (lvl[0] || lvl[1] || lvl[2]) dockLastMs = now;
    // A+C = the debug overlay, for as long as it is held. Polled BEFORE
    // update() so the chord suppresses both buttons' own actions in the very
    // tick it is detected — otherwise letting go would also page the aircraft
    // list, and holding would fire A's long press underneath the overlay.
    const bool chord = btnFsm.chord(fr::BTN_A, fr::BTN_C);
    if (chord != dbgHeld) { dbgHeld = chord; uiDirty = true; }
    if (dbgHeld) {
        // Live figures, THROTTLED (review 08-04): uiDirty on every 10 ms pass
        // meant a full 150 KB pushSprite plus a WiFi.RSSI() IPC call at
        // ~100 Hz for numbers that change at most once a second — the one
        // screen opened to diagnose slowness was itself pinning the SPI bus
        // and distorting what it reported. 250 ms is the same cadence the
        // companion's gauge band uses; enter/exit stays instant above.
        static uint32_t dbgDrawMs = 0;
        if (now - dbgDrawMs >= 250) { dbgDrawMs = now; uiDirty = true; }
        btnFsm.update(now, lvl, nullptr);   // keep debouncing, emit nothing
        return;
    }
    // The SAME arming prompt as the touch hold: at 250 ms the banner says the
    // press is counting. Without it a 700 ms hold is indistinguishable from a
    // button that does not work, and the natural reaction — let go and press
    // again — is exactly what cancels it.
    const bool arming = btnFsm.arming(now, fr::BTN_B, HOLD_ARM_MS);
    if (arming && !holdArmMs)      { holdArmMs = now ? now : 1; uiDirty = true; }
    else if (!arming && holdArmMs) { holdArmMs = 0;             uiDirty = true; }

    fr::BtnEvent ev{};
    if (btnFsm.update(now, lvl, &ev)) {
        // THE SCREEN, not "radar or not". A METAR and a TAF have no pages, so
        // A and C cannot page there — they leave for the radar instead, which
        // is what every non-paging gesture already means on the touch path.
        // `notamCount` is volatile and netTask zeroes it on a refresh that
        // finds nothing in force: read ONCE, then work on the snapshot (the
        // same discipline as the swipe handler below, and for the same
        // reason).
        const int nn = notamCount;
        const fr::Screen scr =
            (viewLevel == VIEW_RADAR)                ? fr::Screen::Radar
          : (viewLevel == VIEW_NOTAM && nn > 0)      ? fr::Screen::Deck
                                                     : fr::Screen::Reading;
        applyEvent(fr::buttonEvent(ev, scr));
    }
}
#endif  // SCE_INPUT_BUTTONS

#if SCE_INPUT_TOUCH
static void handleTouch() {
    // Above the radar we are on a READING screen: nothing to aim at, so the
    // gestures are only about moving through the stack. Handled BEFORE the
    // radar gestures, otherwise a tap would fall through to the blip
    // selection of a radar that is not on screen.
    //   swipe UP       → one step on the ring (past the top comes the radar)
    //   swipe DOWN     → NOT OURS. Left untouched for SceGuest, which owns it
    //                    at every level: down always means "back to the
    //                    companion". We only decline to consume it — both
    //                    poll M5.Touch, so ignoring it here is what lets the
    //                    guest see it.
    //   anything else  → straight back to the radar, the short way out of a
    //                    reading screen.
    // No zone test here: the panel only exists on the radar, so its
    // flight-cycling swipes cannot collide with these.
    if (viewLevel != VIEW_RADAR) {
        auto t = M5.Touch.getDetail();
        if (t.wasPressed()) {
            _tsx = t.x; _tsy = t.y; _tsActive = true;
            _tsMs = millis(); _tsHeld = false;
        }
        // LONG PRESS = FORCE A REFRESH (user 08-01). Every other gesture on a
        // reading screen is taken: a tap returns to the radar, up is the next
        // view, down belongs to SceGuest, left/right page the NOTAM deck. A
        // DOUBLE tap could not work either — the first tap already leaves for
        // the radar, so the second never arrives.
        // A hold is also the right shape for this: forcing a fetch is a
        // deliberate act, not something a sleeve should trigger. 700 ms, and
        // the finger must not have moved (a slow swipe is still a swipe).
        // It fires ON THE HOLD, not on release, so the screen answers while
        // the finger is still down — and _tsActive is cleared so the release
        // that follows does not also count as a tap back to the radar.
        // ARMING: one redraw at 250 ms, so the screen answers long before the
        // gesture completes. Cheap — one frame, not an animation.
        if (_tsActive && !_tsHeld && t.isPressed() && !holdArmMs &&
            millis() - _tsMs >= HOLD_ARM_MS) {
            const int ax = t.x - _tsx, ay = t.y - _tsy;
            if (ax * ax + ay * ay <= 12 * 12) {
                holdArmMs = millis() ? millis() : 1;
                uiDirty = true;
            }
        }
        if (_tsActive && !_tsHeld && t.isPressed() &&
            millis() - _tsMs >= HOLD_MS) {
            const int hx = t.x - _tsx, hy = t.y - _tsy;
            if (hx * hx + hy * hy <= 12 * 12) {
                _tsHeld = true; _tsActive = false;
                applyEvent(fr::UiEvent::Refresh);
            }
        }
        // EVERYTHING BELOW IS DECIDED ON RELEASE, and only for a press SHORTER
        // than the long press (user 08-01). Both conditions are stated rather
        // than implied: _tsActive is already cleared when the hold fires, so
        // the duration test is redundant TODAY - and a redundant guard on the
        // boundary between two gestures earns its line, because the day the
        // hold stops clearing that flag a tap would silently fire on top of a
        // refresh that just went out.
        const bool shortPress = (millis() - _tsMs) < HOLD_MS;
        if (_tsActive && t.wasReleased() && shortPress) {
            _tsActive = false;
            dockLastMs = millis();   // any finger restarts the dock clock
            if (holdArmMs) { holdArmMs = 0; uiDirty = true; }   // gave up
            const int dx = t.x - _tsx, dy = t.y - _tsy;
            // THE shared classifier (firmware/common/Gesture.h). This bin's 60
            // is where the constant came from; what changes is that an exact
            // diagonal now resolves to the vertical axis instead of to
            // nothing, which is the one rule the three bins disagreed on.
            const auto dir = sce::gesture::classify(dx, dy);
            const bool vertical = (dir == sce::gesture::Dir::Up ||
                                   dir == sce::gesture::Dir::Down);
            const bool horizontal = (dir == sce::gesture::Dir::Left ||
                                     dir == sce::gesture::Dir::Right);
            if (vertical) {                            // down: SceGuest's
                if (dy < 0) applyEvent(fr::UiEvent::NextView);
            }
            else if (horizontal) {
                // HORIZONTAL = page through a deck, and only the NOTAM has
                // one. The axis was free precisely because the vertical one is
                // spoken for twice over (up = next view, down = the companion),
                // and paging is what a stack of cards asks for anyway.
                // Elsewhere a horizontal swipe still means "back to the radar",
                // so a gesture that does nothing never leaves you stuck.
                // ONE read of notamCount, then work on the snapshot.
                // It is volatile and netTask sets it to 0 whenever a refresh
                // finds nothing in force: re-reading it after the guard put a
                // `% 0` one instruction away from an IntegerDivideByZero
                // panic (review 08-01).
                if (viewLevel == VIEW_NOTAM && notamCount > 0) {
                    applyEvent(dx < 0 ? fr::UiEvent::PageNext
                                      : fr::UiEvent::PagePrev);
                } else {
                    applyEvent(fr::UiEvent::Back);
                }
            }
            else if (viewLevel == VIEW_NOTAM && notamCount > 0) {
                // EDGE TAPS PAGE THE DECK (user 08-01), which is what a reader
                // reaches for before a swipe: a tap is the cheaper gesture and
                // the deck is the only thing on this screen worth moving.
                // The MIDDLE THIRD still leaves for the radar, so the escape
                // every other reading screen offers is not taken away — it is
                // narrowed. Thirds, not halves, precisely so that leaving stays
                // possible without aiming.
                if      (t.x < 110) applyEvent(fr::UiEvent::PagePrev);
                else if (t.x > 209) applyEvent(fr::UiEvent::PageNext);
                else                applyEvent(fr::UiEvent::Back);
            }
            else applyEvent(fr::UiEvent::Back);
        } else if (t.wasReleased()) {
            // Released after the hold (or after a long, slow press): the press
            // state has to die here or the NEXT press starts already armed.
            _tsActive = false;
            _tsHeld   = false;
            // AND THE PROMPT WITH IT. A press held past 250 ms arms the banner,
            // and a press held past 700 ms that MOVED more than 12 px never
            // fires the hold — so it landed here with holdArmMs still set, and
            // viewHoldBanner then kept "hold to refresh..." on screen and the
            // frame dirty FOREVER, until some later short tap happened to clear
            // it. A slow swipe up is the natural way to hit it (review 08-01).
            if (holdArmMs) { holdArmMs = 0; uiDirty = true; }
        }
        return;
    }
    // ---- TWO-FINGER zoom (replaces the radius slider, user 07-27).
    // The distance between the fingers drives the radius: spreading =
    // zooming in = a smaller radius. Guaranteed fallback if the touch
    // reports only one point: DOUBLE-TAP on the radar → 50/100/250/500 nm.
    static float pinch0 = 0; static int rad0 = 0; static bool pinching = false;
    // POSITIONS FROM THE PACKED RAW ARRAY, never getDetail() (fix 08-25, same
    // defect found on the companion's band, and the SAME fix TouchGestures.h
    // uses for every gesture — see its "Trap 2"). getDetail()'s slots are
    // indexed by the panel's HARDWARE touch id, which the FT6336 rotates
    // between contacts and does NOT pack: a live second finger can land in
    // slot 2 while slot 1 is stale or empty, so `getDetail(1).isPressed()`
    // can read false with two real fingers down and getCount() == 2 — the
    // pinch silently never registers. `getTouchPointRaw()` IS packed by the
    // panel read: index 0/1 are simply the first two points reported this
    // pass, whatever hardware id they carry.
    if (M5.Touch.getCount() >= 2) {
        auto& a = M5.Touch.getTouchPointRaw(0);
        auto& b = M5.Touch.getTouchPointRaw(1);
        float d = sqrtf((float)((a.x - b.x) * (a.x - b.x) +
                                (a.y - b.y) * (a.y - b.y)));
        if (!pinching) { pinch0 = d; rad0 = (int)cfg.radiusNm; pinching = true; }
        else if (d > 10.0f) {
            int nr = (int)(rad0 * pinch0 / d);      // spread → radius ↓
            applyRadius((nr / 5) * 5, false);       // PREVIEW: no SD, no purge
        }
        _tsActive = false;                          // not a swipe
        return;
    }
    if (pinching) {          // end of gesture: NOW is when we persist
        pinching = false;    // and purge (the gesture used to write the
        applyRadius((int)cfg.radiusNm, true);   // yaml on every frame,
    }                                           // over the LCD's SPI)

    auto t = M5.Touch.getDetail();
    // Swipes: UP = METAR view; LEFT = track keyboard; RIGHT = settings.
    // (The long DOWN swipe is handled by SceGuest: back to the companion.)
    if (t.wasPressed()) { _tsx = t.x; _tsy = t.y; _tsActive = true; }
    if (_tsActive && t.wasReleased()) {
        _tsActive = false;
        const int dx = t.x - _tsx, dy = t.y - _tsy;
        // Swipes STARTING on the PANEL: ↑ = next flight, ↓ = previous one
        // (the panel is a list; the SceGuest exit is disabled there through
        // swipeExitMaxX = PANEL_X). Horizontal ones: inert here.
        //
        // FORTY, and this is the one place a threshold below the shared one is
        // EARNED rather than left over: the shared 60 exists to keep a bin's
        // gestures clear of the 100 px exit, and over the panel there is no
        // exit to be clear of. A shorter flick on a list is the right feel; it
        // is stated as an argument so the exception is visible at the point of
        // use instead of living as a second classifier.
        if (_tsx >= PANEL_X) {
            const auto pd = sce::gesture::classify(dx, dy, 40);
            if (pd == sce::gesture::Dir::Up)   applyEvent(fr::UiEvent::TrackNext);
            if (pd == sce::gesture::Dir::Down) applyEvent(fr::UiEvent::TrackPrev);
            return;
        }
        const auto dir = sce::gesture::classify(dx, dy);
        // UP = one step on the ring: the METAR from here. The gesture used to
        // toggle the summary between the legend and a network/debug column;
        // those diagnostics moved into the RESEAU settings tab.
        // DOWN is NOT handled here, and not anywhere else either: it belongs
        // to SceGuest at every level (long swipe = back to the companion).
        if (dir == sce::gesture::Dir::Up) { applyEvent(fr::UiEvent::NextView);
                                                                        return; }
        if (dir == sce::gesture::Dir::Left)  { flightEntry();          return; }
        if (dir == sce::gesture::Dir::Right) { settingsPanel();        return; }
    }
    if (!t.wasClicked()) return;
    int bx = t.x, by = t.y;
    // Tap: targets the nearest blip (< 24 px), otherwise deselects.
    // Right panel = a READING zone: a tap on it neither selects nor
    // deselects anything (max review 07-26: consulting the panel killed
    // the track, and ghost blips projected under the panel were
    // selectable).
    if (bx >= PANEL_X) return;
    // Double-tap on the radar = cycle through the radii (zoom fallback when
    // the touch controller does not report two points).
    // Armed ONLY when nothing is tracked: otherwise the first tap cut the
    // track before the second changed the radius, and two taps on two
    // different aircraft were swallowed as a zoom (07-27c).
    static uint32_t lastTapMs = 0;
    if (!selectedHex[0] && millis() - lastTapMs < 400) {
        lastTapMs = 0;
        applyEvent(fr::UiEvent::ZoomCycle);
        return;
    }
    lastTapMs = millis();
    xSemaphoreTake(gMtx, portMAX_DELAY);
    int best = -1; long bestD2 = 24 * 24;
    for (int i = 0; i < planeCount; i++) {
        if (!cfg.showGround && planes[i].onGround) continue;   // invisible
        int x, y; planeToXY(planes[i].lat, planes[i].lon, x, y);
        if (x < 6 || x > PANEL_X - 8 || y < 10 || y > 232) continue;  // same
                                              // clip as draw(): no invisible
                                              // blip is selectable
        long d2 = (long)(x - bx) * (x - bx) + (long)(y - by) * (y - by);
        // The displayed CALLSIGN is clickable too (a far larger target than
        // the blip on a small screen, 07-26): text rect (x+8, y-3, 6 px/char)
        // + 4 px of touch margin → equivalent to a direct tap (d2 = 0).
        if (planes[i].flight[0]) {
            int lw = (int)strlen(planes[i].flight) * 6;
            if (bx >= x + 4 && bx <= x + 12 + lw &&
                by >= y - 7 && by <= y + 9) d2 = 0;
        }
        if (d2 < bestD2) { bestD2 = d2; best = i; }
    }
    if (best >= 0) {
        // Tap on an aircraft (blip OR callsign) = a full TRACK: selection
        // + callsign remembered → automatic re-lock on every poll. Single
        // path selectPlaneLocked (route via netTask, never any TLS here).
        selectPlaneLocked(best, true);
    } else {
        selectedHex[0] = '\0';
        trackQuery[0]  = '\0';           // tap on nothing = end of track
        routeFlight[0] = '\0';           // else netTask re-resolves the OLD
        clearRouteLocked();
        routeStatus = 0;
        routeReqGen = routeReqGen + 1;    // in-flight route → stale, dropped
        uiDirty = true;
    }
    xSemaphoreGive(gMtx);
}
#endif  // SCE_INPUT_TOUCH

// -------------------------------------------------------------------- setup
void setup() {
    auto mcfg = M5.config();
    M5.begin(mcfg);
    Serial.begin(115200);
    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE);

    // SD on SPI2 shared with the LCD: the PINS must be configured BEFORE
    // SD.begin (SCK=36, MISO=35, MOSI=37, CS=4 — the same values as the
    // companion's hal/Board.h; without this SPI.begin the mount fails
    // silently → no WiFi credentials → no STA, bug 07-25).
    SPI.begin(SCE_SD_SCK, SCE_SD_MISO, SCE_SD_MOSI, SCE_SD_CS);
    sdOk = SD.begin(SCE_SD_CS, SPI, SCE_SD_HZ);
    // THE CONFIG COMES BEFORE THE FIRST PIXEL. loadConfig() is what reads the
    // shared `lang:` key, so it has to run before anything is drawn — the boot
    // line below used to be the first statement of setup() and would have been
    // painted in the fallback language whatever the card said. It costs an SD
    // mount of delay on a splash that exists to say "the card is being read".
    loadConfig();
    notamTokLoad();   // the bearer token lives in its own cache, not the yaml
    apLoadSd();       // airports resolved on earlier runs: they have not moved
    M5.Display.drawString(sce::T("flight-radar: starting...",
                                 "flight-radar : demarrage..."), 10, 10);
    // PHYSICAL safety net promised by the launcher's confirmation screen
    // ("BtnA au boot"): without this lobby, BtnA did NOTHING on this bin
    // (max review 07-26). 1.5 s of button wait at boot.
#if SCE_COMPANION
    if (sdOk) {
        // Lobby in the THEME of the companion launcher (Liquid Glass) — one
        // single visual language from boot to app (user 07-27).
        sce::SceGuest::applyLobbyTheme("flight-radar");
        checkSDUpdater(SD, String("/companion.bin"), 2500, 4);  // 2.5 s:
        // the buttons are REALLY tappable (1.5 s did not leave the time
        // to aim) — going back to the companion is possible from boot
    }
#endif  // SCE_COMPANION — standalone: there is no companion to go back to
    Serial.printf("[radar] SD %s\n", sdOk ? "montee" : "ECHEC montage");
    // Says what a missing card actually costs, and offers to remount. A card
    // inserted THERE has to be read from scratch: loadConfig() and
    // notamTokLoad() already ran against no card and loaded nothing, so the
    // yaml would otherwise be ignored until the next boot — the one outcome
    // that would make the retry a lie.
    // The screen is SceGuest's; what a missing card costs THIS bin is ours —
    // and the autorouter line is the whole reason the screen is worth showing
    // here rather than a generic "no card" toast.
    if (!sdOk) {
        const char* why[] = {
            sce::T("WiFi falls back to the compiled-in network, or to",
                   "Le WiFi retombe sur le reseau compile, sinon sur le"),
            sce::T("the SCE-Guest access point (192.168.4.1).",
                   "point d'acces SCE-Guest (192.168.4.1)."),
            sce::T("The autorouter account must be re-entered every",
                   "Le compte autorouter est a ressaisir a chaque"),
            sce::T("boot - and that mints a token, 20 per week.",
                   "demarrage - et cela consomme un jeton, 20 par semaine."),
        };
        if (guest.noSdNotice([] { SD.end();
                                  sdOk = SD.begin(SCE_SD_CS, SPI, SCE_SD_HZ);
                                  return sdOk; },
                             why, (int)(sizeof(why) / sizeof(why[0])))) {
            loadConfig();
            notamTokLoad();
            apLoadSd();
            migrateTheme();
            Serial.println("[radar] carte inseree : configuration relue");
        }
    }

    // Seeded HERE and not at the mount above: the notice offers a RETRY
    // that mounts the card, and a watcher seeded before it would keep
    // the pre-retry answer -- then unmount a working card to "discover"
    // it (firmware/common/SdWatch.h).
    gSdWatch.begin(sdOk, SCE_SD_CS, SCE_SD_HZ);
    migrateTheme();   // yaml older than 07-28: themes renumbered
    // The SafeSky key is reported as PRESENT/ABSENT, never printed: the serial
    // console is the one place a secret leaks without anyone noticing.
    Serial.printf("[radar] cfg lat=%.2f lon=%.2f r=%dnm poll=%lus api=%s "
                  "metar=%s clef_safesky=%s\n",
                  cfg.lat, cfg.lon, (int)cfg.radiusNm,
                  (unsigned long)cfg.pollS, cfg.api,
                  cfg.metarIcao[0] ? cfg.metarIcao : "(aeroport)",
                  cfg.skyKey[0] ? "oui" : "NON");
    // Names the browser tab and the home card. Without it both pages said
    // "StackChan", which is the HOST and not the thing being configured: with
    // two guest bins installed, two tabs and two bookmarks looked identical.
    guest.appName = "flight-radar";
    // The SD card's credentials take priority over these (readSdCreds
    // overwrites the fallbacks); they only matter when there is no card.
    ipStr = guest.begin(SCE_WIFI_SSID, SCE_WIFI_PASS);   // WiFi + stop endpoint
    Serial.printf("[radar] WiFi %s ip=%s\n",
                  WiFi.status() == WL_CONNECTED ? "STA" : "AP/echec",
                  ipStr.c_str());
    // Local time (the panel's DEPART~/ARRIVEE~): NTP + timezone from yaml
    configTime((long)(cfg.tzOffsetH * 3600.0f), 0, "pool.ntp.org");
    // `airport:` key (IATA/ICAO) — resolved to lat/lon now that WiFi is
    // up; takes PRIORITY over the yaml's lat/lon. Offline/unknown → silent
    // fallback on lat/lon (resolved and persisted at the last success).
    // ⚠ DELEGATED to netTask (started further down) and not run here: one
    // TLS session + ArduinoJson on loopTask's 8 KB stack can overflow it
    // ("Guru Meditation ... loopTask") — and only when the optional
    // `airport:` key is present, so it passed for a config bug (max review
    // 07-27). netTask has 16 KB.
    if (cfg.airport[0]) {
        strlcpy(airportReq, cfg.airport, sizeof(airportReq));
        airportState = 1;                  // netTask resolves then re-polls
    }

    // OPTIONAL peripherals (each one behind a settings toggle)
#if SCE_HAS_LTR553
    ltrOk = ltrBegin();
#else
    constexpr unsigned ltrOk = 0;   // no sensor on this board: auto brightness
                                    // is not merely off, it does not exist
#endif
#if SCE_HAS_SERVO
    if (cfg.servo) head.begin();  // lazy init: only when the option is on
#endif
    Serial.printf("[radar] options servo=%u volume=%u autoLum=%u(ltr=%u) "
                  "nuit=%u sol=%u suivi=%u\n",
                  cfg.servo, cfg.volume, cfg.autoLum, (unsigned)ltrOk,
                  cfg.autoNight, cfg.showGround, cfg.follow);
    M5.Display.setBrightness(cfg.bright);   // persisted brightness (yaml)
    TH = &THEMES[cfg.theme];                // persisted theme (user 07-27)
    canvas.setColorDepth(16);
    canvas.setPsram(true);                 // 320x240x16: PSRAM (no 30 Hz here)
    canvas.createSprite(320, 240);

    gMtx = xSemaphoreCreateMutex();
    // TLS = a generous stack (16 KB); core 0 so loop()/UI can breathe
    xTaskCreatePinnedToCore(netTask, "radar-net", 16384, nullptr, 1,
                            &netTaskHandle, 0);
    // Park netTask before a companion reflash — concurrent TLS/SD during
    // the flash write = heap pressure at the worst moment, on the one
    // recovery operation there is (max review 07-26). Window 3 s: netTask
    // can be inside a 15 s GET, and if it does not park in time we flash
    // anyway. SceGuest parks/releases around the reflash.
    netGuard.windowMs = 3000;
    guest.netGuard = &netGuard;
    // ---- Settings exposed at http://<ip>/config (SceGuest renders the
    // form; the bin keeps ownership of its yaml config).
    // The /config form is served as UTF-8 (SceGuest's own <meta charset>), so
    // the French here is properly accented — unlike everything drawn in Font0.
    guest.addSetting("airport",   sce::T("Airport (IATA/ICAO)",
                                         "A\u00e9roport (IATA/ICAO)"),
                     sce::SceGuest::Text);
    guest.addSetting("metar_icao",sce::T("METAR station (ICAO, empty = airport)",
                                         "Station METAR (ICAO, vide = a\u00e9roport)"),
                     sce::SceGuest::Text);
    // OVERRIDE, not the only source: empty, the runway comes from the SD base
    // (runwayFromSd). Typed in, it wins — to pick a secondary runway or cover
    // an aerodrome the base ignores. Its NUMBER is not its heading (magnetic,
    // rounded to the ten) — hence the "@cap" form. See metarRunway.
    guest.addSetting("metar_rwy", sce::T("METAR runway (12/30@102, empty = SD base)",
                                         "Piste METAR (12/30@102, vide = base SD)"),
                     sce::SceGuest::Text);
    // Callsign to track, the web twin of the on-screen keyboard. Present on
    // BOTH boards on purpose: on the Fire it is the only way to name a flight
    // (there is no keyboard to swipe to), and on the CoreS3 typing AFR470 on
    // a real keyboard beats an 8x5 grid under a fingertip.
    guest.addSetting("track",
                     sce::T("Track a callsign (empty = unchanged, - = stop)",
                            "Traquer un indicatif (vide = inchange, - = arret)"),
                     sce::SceGuest::Text);
    guest.addSetting("radius_nm", sce::T("Radius (nm)", "Rayon (nm)"),
                     sce::SceGuest::Num, 10, 500);
    guest.addSetting("poll_s",    sce::T("Refresh rate (s)",
                                         "Rafra\u00eechissement (s)"),
                     sce::SceGuest::Num, 5, 60);
    guest.addSetting("api",       "Source ADS-B", sce::SceGuest::Choice, 0, 0,
                     "airplanes.live|adsb.lol|adsb.fi|safesky");
    // Secret: NEVER echoed back by the form; an empty submission means
    // "unchanged" and the sentinel "-" REVOKES it (SceGuest contract).
    guest.addSetting("safesky_key",sce::T("SafeSky API key (- to clear)",
                                          "Clef API SafeSky (- pour effacer)"),
                     sce::SceGuest::Secret);
    // The NOTAM deck's three settings. All live: notam_user/notam_pass are the
    // autorouter account fetchNotam signs in with, notam_brief is the filter
    // that drops rank M at collection time.
    guest.addSetting("notam_brief",
                     sce::T("NOTAM: briefing only (hide rank M)",
                            "NOTAM : briefing seul (masquer le rang M)"),
                     sce::SceGuest::Bool);
    guest.addSetting("dock_s",
                     sce::T("Auto-cycle the views (s, 0 = off)",
                            "Défilement auto des vues (s, 0 = off)"),
                     sce::SceGuest::Num, 0, 120);
    guest.addSetting("notam_fir",
                     sce::T("NOTAM: FIR to add (e.g. FMMM, empty = none)",
                            "NOTAM : FIR a ajouter (ex. FMMM, vide = aucune)"),
                     sce::SceGuest::Text);
    guest.addSetting("notam_user", sce::T("autorouter NOTAM account (e-mail)",
                                          "Compte NOTAM autorouter (e-mail)"),
                     sce::SceGuest::Text);
    guest.addSetting("notam_pass", sce::T("autorouter password (- to clear)",
                                          "Mot de passe autorouter (- pour effacer)"),
                     sce::SceGuest::Secret);
    guest.addSetting("theme",
                     sce::T("Theme (0 Gundam, 1 Gundam night, 2 Scope, 3 Scope night)",
                            "Th\u00e8me (0 Gundam, 1 Gundam nuit, 2 Scope, 3 Scope nuit)"),
                     sce::SceGuest::Num, 0, 3);
    guest.addSetting("units",     sce::T("Units", "Unit\u00e9s"),
                     sce::SceGuest::Choice, 0, 0, "aero|metrique");
    guest.addSetting("brightness",sce::T("Brightness", "Luminosit\u00e9"),
                     sce::SceGuest::Num, 10, 255);
    // Only OFFERED where the hardware answers. A settings page that lists a
    // toggle the board cannot honour is worse than one that omits it: the user
    // ticks it, the yaml records it, and nothing ever moves.
#if SCE_HAS_SERVO
    guest.addSetting("servo",     sce::T("Head pointed at the flight",
                                         "T\u00eate point\u00e9e vers le vol"),
                     sce::SceGuest::Bool);
#endif
    guest.addSetting("volume",    sce::T("Chirp volume (0 = mute, %)",
                                         "Volume des chirps (0 = muet, %)"),
                     sce::SceGuest::Num, 0, 100);
#if SCE_HAS_LTR553
    guest.addSetting("auto_bright",sce::T("Auto brightness", "Luminosit\u00e9 auto"),
                     sce::SceGuest::Bool);
#endif
    guest.addSetting("auto_night",sce::T("Auto Night theme", "Th\u00e8me Nuit auto"),
                     sce::SceGuest::Bool);
    guest.addSetting("show_ground",sce::T("Ground traffic", "Trafic au sol"),
                     sce::SceGuest::Bool);
    guest.addSetting("follow",    sce::T("Track the nearest", "Suivre le plus proche"),
                     sce::SceGuest::Bool);
    guest.settingGet = [](const char* k) -> String {
        if (!strcmp(k, "airport"))     return cfg.airport;
        if (!strcmp(k, "metar_icao"))  return cfg.metarIcao;
        if (!strcmp(k, "metar_rwy"))   return cfg.metarRwy;
        // NEVER echoed back — see the setter. The page must not be able to
        // replay a callsign the robot has since changed on its own.
        if (!strcmp(k, "track"))       return String();
        if (!strcmp(k, "radius_nm"))   return String((int)cfg.radiusNm);
        if (!strcmp(k, "poll_s"))      return String((int)cfg.pollS);
        if (!strcmp(k, "api"))         return cfg.api;
        // The VALUE of a Secret is only read to tell "defined" from "not
        // defined" (the form shows a placeholder, never the token itself).
        if (!strcmp(k, "safesky_key")) return cfg.skyKey;
        if (!strcmp(k, "notam_brief")) return String((int)cfg.notamBrief);
        if (!strcmp(k, "dock_s"))      return String((int)cfg.dockS);
        if (!strcmp(k, "notam_fir"))   return cfg.notamFir;
        if (!strcmp(k, "notam_user"))  return cfg.notamUser;
        if (!strcmp(k, "notam_pass"))  return cfg.notamPass;
        if (!strcmp(k, "theme"))       return String((int)cfg.theme);
        if (!strcmp(k, "units"))       return cfg.metric ? "metrique" : "aero";
        if (!strcmp(k, "brightness"))  return String((int)cfg.bright);
        if (!strcmp(k, "servo"))       return String((int)cfg.servo);
        if (!strcmp(k, "volume"))      return String((int)cfg.volume);
        if (!strcmp(k, "auto_bright")) return String((int)cfg.autoLum);
        if (!strcmp(k, "auto_night"))  return String((int)cfg.autoNight);
        if (!strcmp(k, "show_ground")) return String((int)cfg.showGround);
        if (!strcmp(k, "follow"))      return String((int)cfg.follow);
        return "";
    };
    // ONE single lock for the whole submission: per field, netTask could
    // latch a half-applied config (new radius, old source) — review
    // 07-27d.
    // A ticked checkbox is sent as "on" by the browser: toInt() returned 0,
    // so EVERY toggle enabled from the web page was applied as zero (seen
    // on HW 07-27d). We accept the three usual forms.
    guest.onSettingsBegin = []() { xSemaphoreTake(gMtx, portMAX_DELAY); };
    guest.settingSet = [](const char* k, const String& v) {
        if      (!strcmp(k, "airport")) {
            // Changing airport must RESOLVE the new centre: without that
            // the label showed CDG while the radar stayed on the old
            // lat/lon, and the yaml persisted the inconsistent pair
            // (review 07-27d).
            if (strcmp(cfg.airport, v.c_str())) {
                strlcpy(cfg.airport, v.c_str(), sizeof(cfg.airport));
                strlcpy(airportReq, v.c_str(), sizeof(airportReq));
                if (v.length() >= 3) airportState = 1;   // netTask resolves
                metarOkMs = 0; metarDueMs = 0; metarPrevObs = 0; metarCycleS = 1800;
                                    // another airport = another station: the
            }                       // observation we hold is no longer ours
        }
        else if (!strcmp(k, "track")) {
            // EMPTY = UNCHANGED, "-" = stop tracking. The same convention as
            // the secrets above, and here it is not politeness — it is the
            // only thing that makes this field safe.
            //
            // A browser posts the value the page held when it was RENDERED,
            // and `trackQuery` is the most volatile field on this form: the
            // firmware rewrites it by itself from four places (a blip tap,
            // A/C cycling, a tap on empty space, and netTask releasing a track
            // that has gone silent for ten minutes). Echoing it back and
            // acting on "it differs from the current value" therefore had the
            // page fighting the robot: open /config with nothing tracked, tap
            // an aircraft on the radar, then save the BRIGHTNESS from that
            // still-open page — it posts the stale empty string, which differs
            // from the live callsign, and the track is destroyed. Guarding on
            // "a real change" only moved the bug from "same value" to "stale
            // value" (found by two independent review angles, 08-02).
            // Never echoing it (settingGet returns "") means a form that was
            // not touched carries no opinion at all, which is the truth.
            if (v.length() == 0) return;               // no opinion
            String u = (v == "-") ? String("") : v;
            u.toUpperCase();
            // Longer than the field holds: REFUSE rather than truncate. A
            // silently shortened query matches the wrong flights and, being
            // permanently different from what the page shows, would re-fire on
            // every later save. Callsigns are 8 characters at most.
            if (u.length() >= sizeof(trackQuery)) return;
            if (strcmp(trackQuery, u.c_str()))
                applyTrackQueryLocked(u.c_str());      // gMtx already held
        }
        else if (!strcmp(k, "metar_icao")) {
            String u = v; u.toUpperCase();
            if (strcmp(cfg.metarIcao, u.c_str())) {
                strlcpy(cfg.metarIcao, u.c_str(), sizeof(cfg.metarIcao));
                metarOkMs = 0; metarDueMs = 0; metarPrevObs = 0; metarCycleS = 1800;
                                    // refetched on the next view opening
            }
        }
        else if (!strcmp(k, "metar_rwy")) {
            // Purely COSMETIC (the bar on the rose): no cache to invalidate,
            // the next redraw picks it up. Upper case for the "L/C/R" suffixes.
            String u = v; u.toUpperCase();
            strlcpy(cfg.metarRwy, u.c_str(), sizeof(cfg.metarRwy));
            uiDirty = true;
        }
        else if (!strcmp(k, "radius_nm")) cfg.radiusNm = constrain(v.toFloat(),
                                                                  10.0f, 500.0f);
        else if (!strcmp(k, "poll_s"))    cfg.pollS = constrain(v.toInt(), 5, 60);
        else if (!strcmp(k, "api"))       strlcpy(cfg.api, v.c_str(),
                                                  sizeof(cfg.api));
        else if (!strcmp(k, "safesky_key")) strlcpy(cfg.skyKey, v.c_str(),
                                                    sizeof(cfg.skyKey));
        else if (!strcmp(k, "dock_s"))
            cfg.dockS = (uint8_t)constrain(v.toInt(), 0, 120);
        else if (!strcmp(k, "notam_brief")) {
            cfg.notamBrief = webBool(v);
            notamOkMs = 0;              // re-filter on the next pass
            notamForceMs = millis() ? millis() : 1;   // and BEAT the 60 s spacing
        }
        else if (!strcmp(k, "notam_fir")) {
            // Uppercased like every other ICAO code this bin accepts: the API
            // matches item A exactly, and "fmmm" would silently return nothing.
            for (size_t i = 0; i < sizeof(cfg.notamFir) - 1 && i < v.length(); i++)
                cfg.notamFir[i] = (char)toupper((unsigned char)v[i]);
            cfg.notamFir[v.length() < sizeof(cfg.notamFir) - 1
                             ? v.length() : sizeof(cfg.notamFir) - 1] = '\0';
            notamOkMs = 0;
            notamForceMs = millis() ? millis() : 1;
            notamFor[0] = '\0';        // the cache no longer covers the query
        }
        // Both DROP the token: it lives a week, so correcting the account
        // would otherwise keep querying with the old bearer until then.
        //
        // ON A REAL CHANGE ONLY (08-03). `settingSet` is called for EVERY field
        // the form posts, not for the ones that moved, and `notam_user` is a
        // Text field — so the browser echoes its current value on every save and
        // this branch fired every time. Changing the BRIGHTNESS dropped the
        // bearer, the next NOTAM query re-authenticated, and that mints a token
        // out of an allowance of TWENTY A WEEK. The whole point of the store is
        // that a token survives a reboot; it survived reboots and died of a
        // settings save. (`notam_pass` is a Secret: an empty field means
        // "unchanged" and never reaches here — which is exactly why the two
        // twins behaved differently and only one of them was visible.)
        else if (!strcmp(k, "notam_user")) {
            if (strcmp(cfg.notamUser, v.c_str())) {
                strlcpy(cfg.notamUser, v.c_str(), sizeof(cfg.notamUser));
                notamTok[0] = '\0'; notamTokExp = 0; notamTokPendTtl = 0;
                notamOkMs = 0;
            }
        }
        else if (!strcmp(k, "notam_pass")) {
            if (strcmp(cfg.notamPass, v.c_str())) {
                strlcpy(cfg.notamPass, v.c_str(), sizeof(cfg.notamPass));
                notamTok[0] = '\0'; notamTokExp = 0; notamTokPendTtl = 0;
                notamOkMs = 0;
            }
        }
        else if (!strcmp(k, "theme"))     cfg.theme = constrain(v.toInt(), 0, 3);
        else if (!strcmp(k, "units"))     cfg.metric = (v == "metrique");
        else if (!strcmp(k, "brightness")) {
            // Setting it by hand DISABLES the auto — otherwise the value
            // was overwritten in less than a second (the touch panel
            // already does this, the web path did not).
            if (cfg.bright != (uint8_t)constrain(v.toInt(), 10, 255))
                cfg.autoLum = 0;
            cfg.bright = constrain(v.toInt(), 10, 255);
        }
        else if (!strcmp(k, "servo"))     cfg.servo      = webBool(v);
        else if (!strcmp(k, "volume"))    cfg.volume = (uint8_t)constrain(v.toInt(), 0, 100);
        else if (!strcmp(k, "auto_bright"))cfg.autoLum   = webBool(v);
        else if (!strcmp(k, "auto_night"))cfg.autoNight  = webBool(v);
        else if (!strcmp(k, "show_ground"))cfg.showGround= webBool(v);
        else if (!strcmp(k, "follow"))    cfg.follow     = webBool(v);
    };
    guest.onSettingsSaved = []() -> bool {
        // Purge of aircraft outside the new radius: the web path skipped
        // this step, which the zoom gesture does (the counter and the "48+"
        // were counting off-scope blips) — review 07-27d.
        int w = 0;
        for (int i = 0; i < planeCount; i++)
            if (distNm(planes[i]) <= cfg.radiusNm ||
                !strcmp(planes[i].hex, selectedHex)) {
                if (w != i) planes[w] = planes[i];
                w++;
            }
        planeCount = w;
        xSemaphoreGive(gMtx);                // end of submit (onSettingsBegin)
        TH = &THEMES[cfg.theme];
        // Manual brightness applies only when AUTO is off. Applying it under
        // auto dimmed (or blinded) the screen until ambient moved >6 steps:
        // the sensor loop's hysteresis compared to a stale value. The reset
        // below makes the ruling source reassert itself within a second.
        if (!cfg.autoLum) M5.Display.setBrightness(cfg.bright);
        gAutoLumReset = true;
        // head.begin() BLOCKS ~1.8 s (the lib's delays) and reopens Wire1:
        // forbidden in an HTTP handler — loop() takes care of it (07-27d).
#if SCE_HAS_SERVO
        if (cfg.servo) servoInitReq = true;
#endif
        cfgDirty = true;                     // persisted by loop() (SD)
        pollNow  = true;
        uiDirty  = true;
        // Dates the web submit in the capture: the immediate poll, the theme
        // change and the SD write that follow all trace back to this line.
        sce::trace::log("cfg", "reglages web appliques (radius=%d api=%s)",
                        (int)cfg.radiusNm, cfg.api);
        return true;                         // real verdict: see cfgDirty
    };
    guest.swipeExitMaxX = PANEL_X;   // right panel = flight cycling
                                     // (companion exit: down swipe on the
                                     // RADAR zone only)
}

// ------------------------------------------------- a card came back mid-run
// A CARD THAT IS SEEN BUT NOT READ WOULD BE A LIE. This is the exact reasoning
// setup() already writes for the boot notice's RETRY, and the same three
// sources: everything persisted was read against no card and loaded nothing, so
// without this the yaml would stay ignored until the next reboot while the
// footer had just stopped saying that settings are not saved.
//
// THE CARD WINS over whatever was typed while it was out — the boot retry's rule
// too. The opposite (rewriting the card from RAM) would have a card-less session
// silently overwrite a documented yaml, which is precisely what saveConfigSd()
// is kept away from unless someone asked for it.
//
// Called from loop() and nowhere else: it reads the card, and loop() is this
// bin's sole SD user (netTask never touches it).
static void sdReloadAfterInsert() {
    // Under gMtx because `cfg` is read by netTask. The lock is held across a
    // yaml read, i.e. across I/O — acceptable HERE and nowhere else, because it
    // happens once per physical card insertion and not on a schedule.
    char wasAirport[sizeof(cfg.airport)];
    xSemaphoreTake(gMtx, portMAX_DELAY);
    strlcpy(wasAirport, cfg.airport, sizeof(wasAirport));
    loadConfig();
    migrateTheme();
    // APPLIED, not merely loaded: setup() applies these two right after reading
    // the yaml, and a reload that stopped at the struct would leave the screen
    // on the old theme and the old brightness while /config showed the new ones.
    TH = &THEMES[cfg.theme];
    // An `airport:` that CHANGED has to be RESOLVED, the way setup() resolves
    // it: otherwise the label names one aerodrome while the sweep stays centred
    // on the previous lat/lon — the inconsistency `settingSet("airport")` exists
    // to prevent. Unchanged, this costs nothing, and the yaml's lat/lon are that
    // resolution's own persisted result.
    if (cfg.airport[0] && strcmp(cfg.airport, wasAirport)) {
        strlcpy(airportReq, cfg.airport, sizeof(airportReq));
        airportState = 1;                    // netTask resolves, then re-polls
    }
    xSemaphoreGive(gMtx);
    M5.Display.setBrightness(cfg.bright);
    // The token cache, BOTH ways round. A token held in RAM is the fresher of
    // the two and the scarce one (20 per account, one week each), so a card that
    // arrives RECEIVES it instead of overwriting it; only when we hold none do
    // we take the card's. That ordering also keeps us off netTask's toes — it is
    // netTask that mints and rewrites `notamTok`.
    // `notamTokPendTtl` counts too. A token minted BEFORE NTP has no absolute
    // expiry yet, and it is exactly the one this cache exists for. Testing only
    // `notamTokExp` sent it down the `else` branch, so inserting a card
    // OVERWROTE a freshly minted bearer with whatever stale one the card held —
    // burning one of the twenty in the very scenario the fix addressed.
    if (notamTok[0] && (notamTokExp || notamTokPendTtl)) notamTokSave();
    else                                                 notamTokLoad();
    // The airport cache, the same way round and for the same reason: what is in
    // RAM was paid for with TLS sessions this run, so a card that arrives
    // RECEIVES it. Reading the card first would drop entries in favour of an
    // older file. A merge would be nicer still; a ring of forty-eight makes it
    // not worth the code.
    { int held = 0;
      for (const ApEntry& e : gApCache) if (e.code[0] && (e.hasPos || e.hasStn)) held++;
      if (held) gApDirty = true; else apLoadSd(); }
    // The runway base is looked up once per station change and cached; the
    // lookup that ran with no card cached "not in the base" for a station that
    // is in it. Forgetting the station is enough — loop()'s own block re-runs on
    // the next pass, which is where that read belongs (one SD reader).
    rwyDbIcao[0] = '\0';
    uiDirty      = true;
    Serial.println("[radar] SD REMONTEE : configuration relue");
}

// --------------------------------------------------------------------- loop
void loop() {
    M5.update();
    soundTick();                           // advances the current pattern
    guest.update();                        // POST /api/bins/stop → companion
    // The exit dialog (down swipe) is BLOCKING and pumps M5.update() itself:
    // the frame of the "Non" tap came back here as a release without a
    // swipe → tap on nothing → end of track. We skip the touch cycle it
    // consumed (max review 07-27).
    // TWO INDEPENDENT PRODUCERS, not an either/or. Written as `#if TOUCH …
    // #else buttons`, the two flags the board profile calls orthogonal were
    // in fact exclusive: a board with both (a Core2, or a CoreS3 built with
    // buttons added) would compile handleButtons() and never call it — dead
    // input, clean build, no diagnostic.
#if SCE_INPUT_TOUCH
    if (guest.consumedTouch()) { _tsActive = false; }
    else                       { handleTouch(); }
#endif
#if SCE_INPUT_BUTTONS
    handleButtons();   // no touchscreen: the three buttons ARE the navigation
#endif
    // ------------------------------------------------ IS THE CARD STILL THERE?
    // `sdOk` used to be the BOOT MOUNT RESULT and nothing else, believed until
    // the next reboot. Pull the card while the radar runs and every consumer
    // kept saying the opposite of the truth: the footer went on promising that
    // settings are saved (the "no microSD" hint is drawn only when `!sdOk`),
    // `cfgDirty` dropped on `saveConfigSd()` returns nobody could trust, the
    // autorouter token cache wrote into a stale mount, and the A+C debug screen
    // reported SD OK. The reverse was just as wrong: INSERTING a card mid-run
    // did nothing at all — the only way in was a power cycle.
    //
    // A flag that only ever goes FALSE would make the first removal permanent,
    // so the same tick owns both directions: a cheap presence test while the
    // card is there, a full remount attempt while it is not.
    //
    // NO renderer.pause() HERE, and that is a decision, not an omission. A2.16
    // exists because on the companion a 30 Hz Renderer TASK pushes the LCD while
    // loop() reads the SD, and SPI2 carries both — the pause is how two tasks
    // stop overlapping. This bin has no renderer task: loop() draws (canvas +
    // pushSprite) AND loop() is this bin's sole SD user, the same statement the
    // two blocks below already rely on. The two accesses are sequential by
    // construction; there is nothing to pause but ourselves. What we owe instead
    // is CHEAPNESS, hence the 3 s spacing and the split below — three seconds is
    // chosen against what the answer is FOR (a human swapping a card, then
    // looking at the screen), not against how fast a card can be pulled.
    // ---- DOCK MODE (08-04, Aerospace Tracker idea): parked on a desk, the
    // views cycle by themselves. cfg.dockS seconds per view, 0 = off; every
    // manual action — event, blip tap, button hold — restarts the current
    // view's clock at its producer. The touch MODALS (keyboard, settings)
    // block loop() while they own the screen, so they suspend the cycle by
    // construction; the debug overlay re-stamps while held. The advance goes
    // through applyEvent like any other producer: one consumer, and the
    // auto-advance restarts its own clock by the same stamp.
    if (cfg.dockS > 0) {
        const uint32_t nowD = millis();
#if SCE_INPUT_BUTTONS
        if (dbgHeld) dockLastMs = nowD;   // debug overlay held: no cycling
#endif
        if (!dockLastMs) dockLastMs = nowD;
        if (nowD - dockLastMs >= (uint32_t)cfg.dockS * 1000u)
            applyEvent(fr::UiEvent::NextView);
    }

    // THE CARD, WATCHED. The two cadences and the never-seen backoff moved to
    // firmware/common/SdWatch.h, where the other two guest bins could reach
    // them: this bin had the mechanism and they had the boot answer standing
    // for the whole run. Leaving the original here would have made the
    // extraction a fourth copy.
    if (gSdWatch.update(sdReloadAfterInsert)) {
        sdOk    = gSdWatch.mounted();
        uiDirty = true;              // footer + A+C tell the truth now
        Serial.printf("[radar] SD %s\n",
                      sdOk ? "INSEREE : configuration relue"
                           : "RETIREE : reglages non persistes");
    }

    // The flag only drops on SUCCESS: a full or write-protected card silently
    // lost the setting (review 07-27d) — so a FAILED write is retried.
    //
    // But NO CARD AT ALL is not a transient failure, and retrying it is not
    // patience, it is a busy loop: `cfgDirty` never clears, so every single
    // pass of loop() reopened a file on an unmounted filesystem, forever
    // (found 08-02 while bringing up a Fire with no SD card — the board this
    // bin now also targets, and the same thing happens on a CoreS3 whose card
    // is missing or dead). The two cases are told apart by `sdOk`, which is
    // the boot-time mount result: nothing to save to, so drop the flag and say
    // so ONCE rather than every 10 ms.
    static uint32_t cfgRetry = 0;
    if (cfgDirty) {
        if (!sdOk) {
            static bool said = false;
            if (!said) { said = true;
                Serial.println("[radar] pas de SD : reglages non persistes "
                               "(valables jusqu'au redemarrage)"); }
            cfgDirty = false;
        } else if ((int32_t)(millis() - cfgRetry) >= 0) {
            // A FAILED WRITE IS RETRIED, BUT NOT EVERY PASS. ha-remote grew
            // this backoff and wrote down why — "without it, loop() hammered
            // the SPI2 bus shared with the display" — and the fix was never
            // carried back here, where the same shape sat unthrottled: on a
            // full or write-protected card this reopened the file every ten
            // milliseconds, for ever, on the bus the renderer needs.
            if (saveConfigSd()) cfgDirty = false;       // SD: loop() ALONE
            else                cfgRetry = millis() + 5000;
        }
    }
    // A token minted before NTP gets its deadline the moment the clock lands.
    notamTokStamp();
    // ...and loop() is the ONLY task that opens the token file.
    if (notamTokDirty) { notamTokDirty = false; notamTokSave(); }
    // The airport cache, written back on the same rule and by the same writer.
    // THROTTLED to once a minute: resolving a multi-leg route raises the flag
    // several times within a second, and rewriting forty-eight lines per
    // waypoint would put the card in the way of the very lookups it is meant to
    // spare. No card = nothing to write to, so the flag is dropped rather than
    // spun on (the busy-loop lesson two paragraphs up).
    if (gApDirty) {
        static uint32_t lastApSave = 0;
        if (!sdOk) gApDirty = false;
        else if (millis() - lastApSave > 60000UL) {
            lastApSave = millis();
            // CLEARED BEFORE THE WRITE, not after. netTask can store an airport
            // WHILE the card is being written; clearing afterwards marked that
            // one clean without it ever reaching the file — and with no expiry
            // on this cache, it would never be written again.
            gApDirty = false;
            if (!apSaveSd()) gApDirty = true;      // failed: try again later
        }
    }
    // Runway of the METAR station, looked up in the SD base ONCE per station
    // change — the guard is a strcmp, the seeks only happen when the station
    // actually moves (settings, a resolved airport). Here and not in netTask
    // for the same reason as the line above: loop() is this bin's sole SD
    // user, SD and LCD share SPI2.
    {
        char st[8];
        xSemaphoreTake(gMtx, portMAX_DELAY);
        metarStation(st, sizeof(st));          // reads cfg
        xSemaphoreGive(gMtx);
        if (strcmp(st, rwyDbIcao)) {
            strlcpy(rwyDbIcao, st, sizeof(rwyDbIcao));
            runwayFromSd(st, rwyDbSpec, sizeof(rwyDbSpec));   // "" if unknown
            Serial.printf("[radar] piste %s : %s\n", st[0] ? st : "(aucune)",
                          rwyDbSpec[0] ? rwyDbSpec : "hors base");
            uiDirty = true;
        }
    }
    // Redraw: on EVENT (poll/selection/gesture → uiDirty) + a 1 s tick for
    // the "maj Xs / vu Xs" counters — the permanent 8 fps full redraw wasted
    // ~25 % of the loop in PSRAM pushSprite (review 07-26).
    static uint32_t lastDraw = 0;
    uint32_t now = millis();
    // THE BANNER'S OWN WIPE DATE (see bannerPick). A sticky banner keeps the
    // frame CLEAN on purpose, so without this nothing would take it off the
    // screen until an unrelated event: the coloured bar would sit on the raw
    // text for up to a second past its window. One redraw, on the millisecond.
    // Signed difference so it survives the millis() rollover.
    if (bannerUntilMs && (int32_t)(now - bannerUntilMs) >= 0) {
        bannerUntilMs = 0;
        uiDirty = true;
    }
    if (uiDirty || now - lastDraw > 1000) {
        uiDirty = false;
        lastDraw = now;
        draw();
    }
    // ---- 1 Hz automations (options). Deliberately OUTSIDE the render
    // path: setBrightness and the servo bus are I/O, not drawing.
#if SCE_HAS_SERVO
    if (servoInitReq) { servoInitReq = false; head.begin(); }
#endif
    static uint32_t autoMs = 0;
    if (now - autoMs >= 1000) {
        autoMs = now;
        // Auto brightness (LTR-553). Floor of 60: the sensor is nearly
        // occluded by the K151 shell, and without a floor the screen went
        // black in a normally lit room.
#if SCE_HAS_LTR553
        if (cfg.autoLum && ltrOk) {
            const int32_t v = ltrVisible();
            if (v >= 0) {
                const int b = sce::ltr553::brightnessFrom(v);
                static int lastB = -1;
                if (gAutoLumReset) { gAutoLumReset = false; lastB = -1000; }
                if (abs(b - lastB) > 6) {      // hysteresis: no pumping
                    lastB = b;                 // on the PMIC (A2.2)
                    M5.Display.setBrightness((uint8_t)b);
                }
            }
        }
#endif  // SCE_HAS_LTR553
        // Automatic Night theme, driven by the REAL SUN (08-01). TEMPORARY
        // skin — the persisted theme is not touched, the choice comes back in
        // the morning.
        // It used to be a fixed 21 h → 7 h window, and a fixed window is wrong
        // almost everywhere almost always: at this station sunset moves 1 h 13
        // across the year, so in June the screen stayed bright more than three
        // hours after dark. `sce::isNight` (firmware/common/SunClock.h,
        // NOAA solar position, unit-tested natively) answers from the radar's
        // OWN latitude and longitude — the ones already configured for the
        // sweep — so it is right for wherever this bin is actually running,
        // not just for here.
        // It works in UTC on purpose: the sun does not care about time zones,
        // and going through local time was what broke the old window (the
        // RTC holds UTC and nothing in this project ever sets it, so 21 h-7 h
        // fired at 01 h-11 h local on Reunion — 07-27c).
        time_t nowT = time(nullptr);
        if (cfg.autoNight && sce::clockSynced(nowT)) {
            bool night = sce::isNight(cfg.lat, cfg.lon, nowT);
            nightNow = night;            // read by soundTick (half volume)
            const Theme* want = night ? &THEMES[nightThemeFor(cfg.theme)]
                                      : &THEMES[cfg.theme];
            if (TH != want) { TH = want; uiDirty = true; }
        } else {
            // auto_night off, or no NTP yet: there is no night to speak of, so
            // the sound is not attenuated on a guess.
            nightNow = false;
        }
        // Torque released once the positioning is DONE (user 07-27): at
        // rest the SCS0009 draw current and heat up for nothing, and the
        // head becomes movable by hand. Torque re-engages on its own on the
        // next move. Margin: the setpoint lasts 900 ms.
#if SCE_HAS_SERVO
        head.service(now, cfg.servo);
        // Head pointed at the tracked flight (option). The yaw is clamped at
        // ±130° (headtrack.h): the head "looks towards" without claiming to
        // point exactly. The robot is assumed to FACE NORTH — this bin has no
        // facing setting, unlike space's `servo_az`.
        if (cfg.servo && head.ready()) {
            float bearing = -1.0f;
            xSemaphoreTake(gMtx, portMAX_DELAY);
            for (int i = 0; i < planeCount; i++)
                if (selectedHex[0] && !strcmp(planes[i].hex, selectedHex)) {
                    bearing = fr::bearingDeg(cfg.lat, cfg.lon,
                                             planes[i].lat, planes[i].lon);
                    break;
                }
            xSemaphoreGive(gMtx);
            if (bearing >= 0.0f) {
                // The pose comes from the SAME function as space's, whose sign
                // was checked on the robot. This used to be `166 + bearing`:
                // mirrored, so the head turned away from the flight (09-25).
                const float target = spc::headtrack::aim(bearing, 0.0f, 0.0f).yaw;
                head.moveTo(target, NAN, 900);      // NON blocking, yaw only
            }
        }
#endif  // SCE_HAS_SERVO
    }

    // 10 s DEBUG heartbeat (user 07-27): the StackChan's resource usage —
    // same spirit as the "companion alive" line (heap/min/PSRAM, loop and
    // netTask stack margins, RSSI, aircraft tracked).
    static uint32_t statsMs = 0;
    if (now - statsMs >= 10000) {
        statsMs = now;
        // `clock` REPORTED, because a board with no RTC cannot be asked any
        // other way. The CoreS3 has one and M5.begin() loads it into the
        // system clock, so time() is plausible from the first frame; an
        // M5Stack Fire has NO RTC, so until SNTP answers time() is 1970 and
        // every clock-dependent feature silently does nothing — the TAF's
        // "in force" marker, the automatic Night theme, the local ETAs.
        // Absent from the heartbeat, "not synced yet" and "the feature is
        // broken" looked exactly alike (08-02).
        char clk[24];
        if (!zuluHhmm(clk, sizeof(clk))) {
            snprintf(clk, sizeof(clk), "NON-SYNC");
        }
        Serial.printf("[radar] stats up:%lus heap:%u min:%u psram:%u "
                      "stkLoop:%u stkNet:%u rssi:%d avions:%d clock:%s\n",
                      (unsigned long)(now / 1000),
                      (unsigned)ESP.getFreeHeap(),
                      (unsigned)ESP.getMinFreeHeap(),
                      (unsigned)ESP.getFreePsram(),
                      (unsigned)uxTaskGetStackHighWaterMark(nullptr),
                      netTaskHandle
                          ? (unsigned)uxTaskGetStackHighWaterMark(netTaskHandle)
                          : 0,
                      WiFi.RSSI(), planeCount, clk);
    }
    delay(10);
}
