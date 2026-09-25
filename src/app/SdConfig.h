#pragma once
// =============================================================================
// SdConfig.h — StackChan-Companion (app)
// =============================================================================
// SD persistence: /stackchan-companion/config.yaml (schema docs/reference/CONFIG.md).
// Deliberately MINIMAL YAML SCHEMA: `section:` + `  key: value` (2 spaces),
// `#` comments, no lists — anything richer goes through the API (CONFIG
// decision).
//
// This file does NOT parse YAML. What a line MEANS (indentation, quotes,
// end-of-line comment) is decided in `firmware/common/Yaml.h`, the ONE decoder
// shared with guest/SceGuest.h and tested natively — rule A2.23, written after
// two twin parsers had already drifted apart. What load() still owns is the
// bounded READ (MAX_LINE, see its comment) and the section dispatch below.
//
// Sections handled here:
//   (top level) lang — UI language, read BEFORE the section dispatch: it
//            belongs to no section, and burying it in one would have made it
//            look like a property of that section (absent = English)
//   wifi:    client_ssid / client_password / ap_ssid / ap_password / hostname
//   api:     username / password (Basic Auth protection of the console+API,
//            empty = open — see WebApi::begin, AsyncAuthenticationMiddleware)
//   tuning:  every key of Tuning::table() (persistence of /api/tuning), passed
//            through a schema MIGRATION on load: a yaml written before a key
//            was recalibrated would otherwise freeze the defaults of its era
// The other sections of the schema (display/servo/behavior/launcher/debug)
// get wired in as they land — unknown keys are IGNORED when reading and not
// rewritten (save() regenerates the whole file).
//
// load() at boot AFTER Board::begin (SD mounted); save() after each POST
// /api/tuning or /api/wifi (called from loop via a flag — AsyncTCP callbacks
// never write the SD directly: FAT access is a multi-step operation).
// =============================================================================

#include <Arduino.h>
#include <SD.h>
#include "../engine/Tuning.h"
#include "../../firmware/common/I18n.h"   // sce::setLang — one language, set here
#include "../../firmware/common/Yaml.h"   // the ONE line decoder (rule 17)

namespace sce {

struct WifiCreds {
    String clientSsid;                    // empty = direct AP mode
    String clientPass;
    String apSsid   = "StackChan-AP";
    String apPass   = "goodlife";
    String hostname = "stackchan";
};

// Basic Auth protection of the console + REST API (POST /api/*, GET /,
// /swagger...). An empty `password` (the default) means the API is OPEN, with
// no behavioural change. Changeable at runtime via POST /api/security
// (WebApi) — that endpoint is ITSELF behind the global middleware as soon as
// a password is set (no bypass: changing it proves you already know the
// current one, or that the API was still open).
struct ApiCreds {
    // FIXED buffers (not Strings): read by the auth middleware and by
    // uploadAuthed() on the AsyncTCP task while loop() updates them — a
    // stable char[] avoids the use-after-free of a String realloc.
    char username[64] = "admin";
    char password[64] = "";               // empty = no protection
};

class SdConfig {
public:
    static constexpr const char* PATH = "/stackchan-companion/config.yaml";
    // Must match `SceGuest::MAX_LINE` — deliberate twins, see load().
    static constexpr size_t MAX_LINE = 512;

    WifiCreds wifi;
    ApiCreds  api;
    // UI language, EN/FR — see firmware/common/I18n.h. ABSENT = English: a card
    // written before this key existed must behave like an English build, not
    // like a broken one.
    String    lang = "en";

    // ------------------------------------------------------------------
    // Read — applies wifi + tuning. false if the file is missing (defaults).
    // ------------------------------------------------------------------
    bool load(Tuning& tuning) {
        File f = SD.open(PATH, FILE_READ);
        if (!f) return false;

        String section;
        // DELIBERATE twin of `SceGuest::yamlForEach` (rule A2.23): the
        // companion does not depend on `guest/`, so the loop is duplicated —
        // but it must stay LINE FOR LINE identical, otherwise the same
        // config.yaml reads differently depending on the binary. It had
        // already diverged once: the line cap was missing here (found during
        // the 2026-07-29 review).
        char buf[MAX_LINE + 1];
        while (f.available()) {
            size_t n = f.readBytesUntil('\n', buf, MAX_LINE);
            buf[n] = '\0';
            // Line longer than the cap: drop the rest up to the newline,
            // otherwise the leftover would come back as a bogus key/value.
            if (n == MAX_LINE && f.available() && f.peek() != '\n') {
                while (f.available() && f.read() != '\n') { }
                continue;
            }
            // ONE decoder, shared with SceGuest (08-01). What a line MEANS —
            // indentation, quotes, end-of-line comment — is no longer written
            // twice: `firmware/common/Yaml.h` is a place both sides already
            // include, and it is natively tested (test_yaml, 18 cases, both
            // historical outages among them). Only the bounded READ above
            // stays duplicated: that is I/O, not interpretation, and it is not
            // where the twins ever disagreed.
            const sce::yaml::Line ln = sce::yaml::decodeLine(buf);
            if (!ln.ok) continue;
            const bool indented = ln.indented;
            const String key(ln.key);
            const String val(ln.val);

            // TOP-LEVEL keys, read before the section dispatch: `lang` is
            // not a property of the wifi, of the api or of the tuning, and
            // burying it in a section would have made it look like one.
            if (!indented) {
                if (key == "lang") {
                    lang = val;
                    sce::setLang(lang.c_str());
                    continue;
                }
                section = key; continue;                 // `section:`
            }

            if (section == "wifi") {
                if      (key == "client_ssid")     wifi.clientSsid = val;
                else if (key == "client_password") wifi.clientPass = val;
                else if (key == "ap_ssid")         wifi.apSsid     = val;
                else if (key == "ap_password")     wifi.apPass     = val;
                else if (key == "hostname")        wifi.hostname   = val;
            } else if (section == "api") {
                if      (key == "username") strlcpy(api.username, val.c_str(), sizeof(api.username));
                else if (key == "password") strlcpy(api.password, val.c_str(), sizeof(api.password));
            } else if (section == "tuning") {
                tuning.set(key.c_str(), val.toFloat());   // unknown keys ignored
            }
        }
        f.close();

        // API credentials are emitted in the /api/status JSON: strip " \ and
        // the control characters a hand-edited config.yaml could contain (the
        // API already filters them on POST) — otherwise the JSON is invalid.
        sanitizeApi(api.username);
        sanitizeApi(api.password);

        // MIGRATION, not a live implication: a card written before
        // `mic_enable` existed carries `sound_track: 1` alone. SoundTracker
        // honours that (tracking implies the microphone), but the REGISTER
        // then lied — the console's Microphone toggle and GET /api/tuning
        // reported off while the capture ran. The one switch meant to answer
        // "is the robot listening" gave the wrong answer. Made truthful here,
        // and persisted by the next save like every other migration.
        if (tuning.sound_track >= 0.5f && tuning.mic_enable < 0.5f)
            tuning.mic_enable = 1.0f;

        // ---- Schema MIGRATION (2026-07-17 review): an older yaml freezes
        // the defaults of its own era. If its cfg_version is older (or
        // absent = 0), the keys RECALIBRATED since are reset to the current
        // defaults; the current version will be persisted on the next save.
        // v2 (2026-07-17): the soundtrack_* family was recalibrated several
        // times while being tuned (thr 900→100, sign ±1, sqrt/per-channel
        // normalisation) + head_home_ms introduced. ----
        if (tuning.cfg_version < Tuning::CFG_VERSION) {
            Tuning d;   // current compiled-in defaults
            if (tuning.cfg_version < 2.0f) {
                // v2: soundtrack_* family recalibrated + head_home_ms
                tuning.soundtrack_thr       = d.soundtrack_thr;
                tuning.soundtrack_step_deg  = d.soundtrack_step_deg;
                tuning.soundtrack_move_ms   = d.soundtrack_move_ms;
                tuning.soundtrack_shock_thr = d.soundtrack_shock_thr;
                tuning.head_home_ms         = d.head_home_ms;
            }
            if (tuning.cfg_version < 3.0f) {
                // v3: microphone channel mapping fixed (channel 1 = RIGHT) —
                // the persisted sign was compensating the old labelling
                tuning.soundtrack_sign      = d.soundtrack_sign;
            }
            if (tuning.cfg_version < 4.0f) {
                // v4: the sign re-defaulted by v3 (+1) turned the head AWAY
                // from the sound on real hardware. Re-default it again, this
                // time to a value verified on target (-1, user 2026-07-30).
                tuning.soundtrack_sign      = d.soundtrack_sign;
            }
            if (tuning.cfg_version < 5.0f) {
                // v5: the idle torque release went 15 s -> 4 s, and it is
                // now measured from the END of a trajectory rather than its
                // start. Fifteen seconds was long enough that the emotion
                // roulette's pitch nudges kept restarting the timer, so the
                // neck was held permanently, warm and unmovable by hand.
                // Re-defaulting it HERE is the only way the fix reaches a
                // robot that already has a card: save() persists the whole
                // table, so any device that ever took a POST /api/tuning
                // carries the old value and would keep it forever.
                tuning.servo_idle_release_ms = d.servo_idle_release_ms;
            }
            Serial.printf("[config] migration schema tuning v%.0f -> v%.0f\n",
                          (double)tuning.cfg_version,
                          (double)Tuning::CFG_VERSION);
            tuning.cfg_version = Tuning::CFG_VERSION;
        }
        return true;
    }

    // Strips " \ and control characters IN PLACE (keeps UTF-8 intact) — the
    // credentials are emitted verbatim in the /api/status JSON, and yamlQuote()
    // relies on values already being free of " and \.
    static void sanitizeApi(char* s) {
        char* w = s;
        for (char* r = s; *r; r++) {
            unsigned char c = (unsigned char)*r;
            if (c >= 0x20 && c != 0x7F && c != '"' && c != '\\') *w++ = *r;
        }
        *w = '\0';
    }

    // Serialises a value inside double quotes (safe for empty strings,
    // spaces, '#', ':').
    //
    // `"` and `\` are ESCAPED, not dropped. They used to be silently deleted
    // here on the grounds that callers had already removed them — which was
    // true of the API credentials and NEVER of the WiFi ones. A WPA passphrase
    // is any 8..63 printable ASCII characters, those two included, so the
    // "safety net" quietly saved a different passphrase from the one that had
    // just been accepted: the robot joined the network once, then failed for
    // ever after the next reboot, with nothing on any screen to say why.
    // `sce::yaml::scalar` decodes both escapes on the way back in.
    static String yamlQuote(const String& s) {
        String o = "\"";
        for (unsigned i = 0; i < s.length(); i++) {
            char ch = s[i];
            if (ch == '"' || ch == '\\') o += '\\';
            o += ch;
        }
        o += "\"";
        return o;
    }

    // ------------------------------------------------------------------
    // Write — regenerates the whole file (wifi + the entire tuning table).
    // A SINGLE write() (buffer built in RAM) instead of ~25 f.printf
    // (2026-07-12): fewer SD command exchanges = a shorter SPI contention
    // window (see main.cpp — the renderer is paused during the write; this
    // also shortens the pause itself).
    // ------------------------------------------------------------------
    bool save(const Tuning& tuning) {
        SD.mkdir("/stackchan-companion");              // idempotent

        String buf;
        buf.reserve(1024);
        buf += "# StackChan-Companion — genere par /api (schema docs/reference/CONFIG.md)\n";
        buf += "lang: ";              buf += yamlQuote(lang);            buf += '\n';
        buf += "wifi:\n";
        buf += "  client_ssid: ";     buf += yamlQuote(wifi.clientSsid); buf += '\n';
        buf += "  client_password: "; buf += yamlQuote(wifi.clientPass); buf += '\n';
        buf += "  ap_ssid: ";         buf += yamlQuote(wifi.apSsid);     buf += '\n';
        buf += "  ap_password: ";     buf += yamlQuote(wifi.apPass);     buf += '\n';
        buf += "  hostname: ";        buf += yamlQuote(wifi.hostname);   buf += '\n';
        buf += "api:\n";
        buf += "  username: ";        buf += yamlQuote(api.username); buf += '\n';
        buf += "  password: ";        buf += yamlQuote(api.password); buf += '\n';
        buf += "tuning:\n";
        char line[48];
        for (const Tuning::Entry* e = Tuning::table(); e->key; e++) {
            snprintf(line, sizeof(line), "  %s: %.4f\n",
                     e->key, (double)(tuning.*(e->field)));
            buf += line;
        }

        File f = SD.open(PATH, FILE_WRITE);       // truncates
        if (!f) return false;
        size_t written = f.write((const uint8_t*)buf.c_str(), buf.length());
        f.close();
        return written == buf.length();
    }
};

} // namespace sce
