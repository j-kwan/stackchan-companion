#pragma once
// =============================================================================
// CpuLoad.h — StackChan-Companion (hal)
// =============================================================================
// Per-core CPU load (htop equivalent, user request 2026-07-16) measured by
// STARVING the idle task: a FreeRTOS hook counts idle passes per core; the
// observed rate, compared to the self-calibrated maximum (an idle core),
// gives the load. Zero dependency on CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
// (disabled in the Arduino core).
//
//   load = 100 × (1 - idle_passes/s ÷ observed_max)
//
// Self-calibration: the max only ratchets up window after window — the
// first readings slightly underestimate, then converge.
// A proxy, not an exact measurement — plenty good enough for the graph.
//
// Threading: hooks called by the idle tasks (one per core, 32-bit counters —
// atomic read/write on Xtensa); update() from loop() (~1 s window); getters
// read from AsyncTCP.
// =============================================================================

#include <Arduino.h>
#include <atomic>
#include <esp_freertos_hooks.h>

namespace sce {

class CpuLoad {
public:
    void begin() {
        esp_register_freertos_idle_hook_for_cpu(idleHook0, 0);
        esp_register_freertos_idle_hook_for_cpu(idleHook1, 1);
        _lastMs = millis();
    }

    // Call from loop() — samples every ~1 s
    void update() {
        uint32_t now = millis();
        uint32_t dt  = now - _lastMs;
        if (dt < 1000) return;
        // Atomic exchange (review 2026-07-17): the two-step read-then-zero
        // lost the increments of the OPPOSITE core's idle hook landing
        // between the read and the reset (lost update, bias < 1 %)
        uint32_t c0 = _cnt0.exchange(0);
        uint32_t c1 = _cnt1.exchange(0);
        _lastMs = now;

        float r0 = (float)c0 * 1000.0f / (float)dt;   // idle passes/s
        float r1 = (float)c1 * 1000.0f / (float)dt;
        // SHARED self-calibration: one idle pass costs the same on both
        // cores — the max comes from whichever core is the most idle
        // (core 0). Without this, a core that is NEVER idle (core 1:
        // Brain+Renderer) would calibrate its max under load and read ~0 %.
        float mx = r0 > r1 ? r0 : r1;
        if (mx > _max) _max = mx;
        // _max ≥ r0 and ≥ r1 by construction (ratchet above) → the loads
        // stay within [0, 100] without any extra clamp
        _load0 = (_max > 1.0f) ? 100.0f * (1.0f - r0 / _max) : 0.0f;
        _load1 = (_max > 1.0f) ? 100.0f * (1.0f - r1 / _max) : 0.0f;
    }

    // Load 0-100 per core (core 0: WiFi/loop/servo · core 1: Brain/Renderer)
    int load0() const { return (int)(_load0 + 0.5f); }
    int load1() const { return (int)(_load1 + 0.5f); }

private:
    static bool idleHook0() { _cnt0.fetch_add(1, std::memory_order_relaxed); return true; }
    static bool idleHook1() { _cnt1.fetch_add(1, std::memory_order_relaxed); return true; }

    inline static std::atomic<uint32_t> _cnt0{0};
    inline static std::atomic<uint32_t> _cnt1{0};

    uint32_t _lastMs = 0;
    float    _max   = 0.0f;               // shared reference (idle core)
    float    _load0 = 0.0f, _load1 = 0.0f;
};

} // namespace sce
