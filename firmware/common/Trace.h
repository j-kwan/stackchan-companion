#pragma once
// =============================================================================
// Trace.h - THE DEBUG TRACE, one implementation for every firmware
// =============================================================================
// A runtime-toggled verbose log of every step that matters: network joins,
// config reads, each HTTP attempt with its status and duration, SD writes,
// task parking. OFF it costs one boolean test per call site; ON it narrates
// on the serial console, which is the one channel that survives when HTTP is
// the thing being debugged.
//
// Runtime and not compile-time, deliberately: the moments that need tracing
// (a robot that will not join, a fetch chain that fails in the field) are
// exactly the moments a reflash is unavailable or destroys the evidence.
// Toggles: companion `POST /api/tuning?debug=1` (persisted, console switch);
// guests: the framework Debug checkbox on /config (NVS, survives the card).
//
// IN `firmware/common/` because every side needs it (rule 17 - one
// implementation, no assumed twins); vendored into SceGuest.h so that header
// stays copyable alone, `scripts/gates/check-vendored.py` holding the copy.
// =============================================================================

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

// >>> VENDORABLE BEGIN Trace <<<
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
// >>> VENDORABLE END Trace <<<
