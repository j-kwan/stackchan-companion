#pragma once
// =============================================================================
// DanceStore.h — StackChan-Companion (app)
// =============================================================================
// USER-EDITABLE choreographies on the SD card (docs/reference/CHOREGRAPHIES.md
// §6, user request 2026-07-12): one CSV file per dance in /dances/.
//
//   # /dances/salut.csv — one line = one keyframe
//   # yaw,pitch,servoMs,holdMs,emotion,lid,gazeY
//   0,0,200,250,Happy,0,0
//   20,-10,400,800,,2,0        (empty emotion = unchanged; lid 2 = left wink)
//
// Fields: yaw/pitch = ° OFFSETS (clamped to ±YAW_RANGE / -(NEUTRAL-MIN)..0),
// lid = 0|1|2|3 or blink|winkG|winkD (G = left, D = right), gazeY in [-1..1].
// The dance name is the file name without its extension. The authoring RULES
// (§4) are ENFORCED at load time: if the last keyframe is not Normal + neutral
// pose, an exit keyframe is ADDED (so the roulette never gets stuck).
//
// CONCURRENCY: lookups (AsyncTCP callbacks → POST /api/dance) read the store
// while loop() may be reloading it (POST /api/dances/reload).
// → DOUBLE BANK: reload() builds into the inactive bank, then flips the index
// atomically; pointers into the old bank stay valid (static memory, never
// freed) — a dance being read, or ALREADY sitting in the CommandQueue,
// survives a reload (it plays the old version).
//
// Static limits (no allocation): 8 dances × 24 keyframes, 24-char names.
// =============================================================================

#include <Arduino.h>
#include <SD.h>
#include <atomic>
#include "../engine/Units.h"
#include "../behavior/Sequencer.h"
#include "../engine/Emotions.h"

namespace sce {

class DanceStore {
public:
    static constexpr const char* DIR       = "/dances";
    static constexpr int         MAX_DANCES = 8;
    static constexpr int         MAX_KEYS   = 24;
    static constexpr int         NAME_LEN   = 24;
    // Line ceiling (stack buffer). A keyframe is 7 short fields; past that,
    // the file is not a dance CSV. Same bound as its twins
    // `SceGuest::MAX_LINE` / `SdConfig::MAX_LINE`.
    static constexpr size_t      MAX_LINE   = 512;

    struct Entry {
        char     name[NAME_LEN];
        DanceKey keys[MAX_KEYS];
        int      count;
    };

    // ------------------------------------------------------------------
    // (Re)loads /dances/*.csv into the inactive bank, then flips.
    // Call from setup()/loop() ONLY (FAT read, rule A2.6).
    // Returns the number of dances loaded.
    // ------------------------------------------------------------------
    int reload() {
        int bank = 1 - _active.load();
        int n    = 0;
        File dir = SD.open(DIR);
        if (dir && dir.isDirectory()) {
            File f;
            while (n < MAX_DANCES && (f = dir.openNextFile())) {
                String fname = f.name();
                if (!f.isDirectory() && fname.endsWith(".csv")) {
                    if (parseCsv(f, _banks[bank][n])) n++;
                }
                f.close();
            }
            dir.close();
        }
        _counts[bank] = n;
        _active.store(bank);
        Serial.printf("[dances] %d chorégraphie(s) SD chargée(s)\n", n);
        return n;
    }

    // ---- Lookups (safe from AsyncTCP — they read the active bank) ----
    int count() const { return _counts[_active.load()]; }

    const Entry* get(int i) const {
        int b = _active.load();
        return (i >= 0 && i < _counts[b]) ? &_banks[b][i] : nullptr;
    }

    const Entry* find(const char* name) const {
        int b = _active.load();
        for (int i = 0; i < _counts[b]; i++) {
            if (strcasecmp(_banks[b][i].name, name) == 0) return &_banks[b][i];
        }
        return nullptr;
    }

private:
    Entry            _banks[2][MAX_DANCES];
    int              _counts[2] = {0, 0};
    std::atomic<int> _active{0};

    // ------------------------------------------------------------------
    // Parses one CSV → Entry. Applies the clamps + the "last keyframe =
    // Normal + neutral" rule. Returns false if no keyframe is valid.
    // ------------------------------------------------------------------
    static bool parseCsv(File& f, Entry& e) {
        // Name = file name without extension, truncated
        String base = f.name();
        int dot = base.lastIndexOf('.');
        if (dot > 0) base = base.substring(0, dot);
        strncpy(e.name, base.c_str(), NAME_LEN - 1);
        e.name[NAME_LEN - 1] = '\0';
        e.count = 0;

        // BOUNDED read (same discipline as `SceGuest::yamlForEach`, rule
        // A2.23): these files arrive through an API upload, so a truncated
        // CSV — or a binary renamed .csv, without a single '\n' for megabytes
        // — would grow the String until the heap is exhausted, and the network
        // stack would be the thing that died, far from the cause.
        char buf[MAX_LINE + 1];
        while (f.available() && e.count < MAX_KEYS - 1) {   // -1: room for the
            size_t n = f.readBytesUntil('\n', buf, MAX_LINE);   // forced exit key
            buf[n] = '\0';
            // Buffer full with no newline: drop the rest, otherwise the
            // leftover would come back as a bogus keyframe.
            if (n == MAX_LINE && f.available() && f.peek() != '\n') {
                while (f.available() && f.read() != '\n') { }
                continue;
            }
            String line(buf);
            int h = line.indexOf('#');
            if (h >= 0) line = line.substring(0, h);
            line.trim();
            if (line.length() == 0) continue;

            // 7 comma-separated fields (missing ones → defaults)
            String tok[7];
            int idx = 0, from = 0;
            while (idx < 7) {
                int c = line.indexOf(',', from);
                tok[idx++] = (c < 0) ? line.substring(from)
                                     : line.substring(from, c);
                if (c < 0) break;
                from = c + 1;
            }
            for (auto& t : tok) t.trim();

            DanceKey& k = e.keys[e.count];
            k = DanceKey{};
            k.yawOff   = clampVal(tok[0].toFloat(),
                                  -(float)units::YAW_RANGE, (float)units::YAW_RANGE);
            // pitchOff: relative to HOME (93° since 2026-07-16) — negative =
            // head raised (down to PITCH_MIN), positive = head LOWERED (up to
            // PITCH_MAX 99 — official spec 5~85°). The old clamp at 0 dated
            // back to home 103 (no downward margin at all).
            k.pitchOff = clampVal(tok[1].toFloat(),
                                  (float)(units::PITCH_MIN - units::PITCH_NEUTRAL),
                                  (float)(units::PITCH_MAX - units::PITCH_NEUTRAL));
            k.servoMs  = (uint16_t)tok[2].toInt();
            k.holdMs   = (uint16_t)tok[3].toInt();   // Sequencer clamps it ≥ servoMs
            k.emotion  = tok[4].length() ? emotionFromName(tok[4].c_str())
                                         : EMOTIONS_COUNT;   // empty = unchanged
            k.lidEvent = lidFrom(tok[5]);
            k.gazeYBias = clampVal(tok[6].toFloat(), -1.0f, 1.0f);
            e.count++;
        }
        if (e.count == 0) {
            Serial.printf("[dances] %s : vide/malformé — ignoré\n", e.name);
            return false;
        }
        // Rule §4.1: the dance MUST hand control back (Normal + neutral pose)
        DanceKey& last = e.keys[e.count - 1];
        if (last.emotion != Normal || last.yawOff != 0.0f || last.pitchOff != 0.0f) {
            e.keys[e.count++] = DanceKey{ 0, 0, 400, 500, Normal };
            Serial.printf("[dances] %s : keyframe de sortie ajoutée (règle §4)\n",
                          e.name);
        }
        return true;
    }

    static uint8_t lidFrom(const String& s) {
        if (s.equalsIgnoreCase("blink"))  return 1;
        if (s.equalsIgnoreCase("winkG"))  return 2;
        if (s.equalsIgnoreCase("winkD"))  return 3;
        int v = s.toInt();
        return (v >= 0 && v <= 3) ? (uint8_t)v : 0;
    }
};

} // namespace sce
