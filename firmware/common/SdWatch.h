#pragma once
// =============================================================================
// SdWatch.h — the card, watched WHILE the bin runs and not only at boot
// =============================================================================
// WHY IT EXISTS. Every guest bin mounts the SD card in setup() and then treats
// that one answer as permanent. On flight-radar it stopped being permanent on
// 08-04, because a card is a thing a human pulls out: settings stopped
// persisting for the rest of the session with the footer still promising they
// would, and a card inserted AFTER boot was never seen at all. The radar grew
// a probe; space and ha-remote did not, so on those two the boot answer still
// stands for the whole run — including the answer "no card", which is the one
// a user is most likely to fix by hand ten seconds later.
//
// The shape is the radar's, extracted rather than reinvented, and the reason
// it has the shape it has is worth carrying with it:
//
// TWO CADENCES, AND A BACKOFF FOR THE NEVER-SEEN CASE. Re-mounting blocks the
// caller inside a failing `SD.begin()` (CMD0 retry timeout, hundreds of ms),
// and in these bins the caller is loop() — the only task drawing and reading
// buttons. Paying that every three seconds forever on a board deliberately run
// card-less is a permanent, rhythmic hole in the input. So: probe at 3 s while
// the card is THERE (one directory open, nearly free), retry the mount at 10 s
// after a REMOVAL, and while no card has EVER been seen this run back off
// 10 -> 60 s. The first successful mount pins the cadence back.
//
// THREE SECONDS is chosen against what the answer is FOR — a human swapping a
// card and then looking at the screen — not against how fast a card can be
// pulled. Nothing here is a race to notice.
//
// WHAT IT DOES NOT DO. It never touches the display, never writes, and never
// decides what to reload: it reports the two transitions and the bin says what
// they mean. A guest that must re-read its config on insertion passes that as
// `onInsert`; one that only cares about the footer passes nothing.
// =============================================================================

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

namespace sce {

class SdWatch {
public:
    // `mounted` is the boot result — the answer this class exists to stop
    // trusting forever. `cs`/`hz` are what the bin already passed to
    // SD.begin(); asking for them keeps the wiring in ONE place per bin
    // (firmware/common/SdPins.h) instead of adding a second opinion here.
    void begin(bool mounted, uint8_t cs, uint32_t hz) {
        _ok = mounted;
        _cs = cs;
        _hz = hz;
        _everSeen = mounted;
    }

    // Called from loop(). Returns true when the state CHANGED, so the caller
    // can mark its UI dirty without tracking the previous value itself.
    //
    // `onInsert` runs on a fresh mount only, and runs with the card known
    // good — the moment a bin re-reads whatever it could not read at boot.
    bool update(void (*onInsert)() = nullptr) {
        const uint32_t now = millis();
        const uint32_t every = _ok ? 3000 : (_everSeen ? 10000 : _absentEvery);
        if (now - _probeMs <= every) return false;
        _probeMs = now;

        if (_ok) {
            // Costs one directory open and only that: on a card that has been
            // pulled the mount is stale and the open fails, which is exactly
            // the question. This is the branch that runs forever on a healthy
            // board, so it is the branch that has to be free.
            File root = SD.open("/");
            const bool alive = root && root.isDirectory();
            if (root) root.close();
            if (alive) return false;
            _ok = false;
            return true;
        }

        // The EXPENSIVE half, and it only ever runs while the card is known
        // absent, at its own slower cadence. `end()` first: SD.begin on an
        // already-failed bus does not re-probe reliably — the driver holds a
        // mount that no longer describes anything.
        SD.end();
        if (SD.begin(_cs, SPI, _hz)) {
            _ok = true;
            _everSeen = true;
            if (onInsert) onInsert();
            return true;
        }
        if (!_everSeen && _absentEvery < 60000) _absentEvery *= 2;
        return false;
    }

    bool mounted() const { return _ok; }

private:
    bool     _ok = false;
    bool     _everSeen = false;
    uint8_t  _cs = 0;
    uint32_t _hz = 0;
    uint32_t _probeMs = 0;
    uint32_t _absentEvery = 10000;
};

}  // namespace sce
