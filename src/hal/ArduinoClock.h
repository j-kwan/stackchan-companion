#pragma once
// =============================================================================
// ArduinoClock.h — StackChan-Companion (hal)
// =============================================================================
// Production implementation of the Clock interface (engine/Clock.h):
//   ms() → millis()             (Arduino)
//   us() → esp_timer_get_time() (µs precision, 64-bit ESP-IDF clock)
//
// This header is the ONLY time ↔ Arduino bridge: it lives in hal/ (allowed to
// depend on the framework), not in engine/ (pure, testable natively).
// =============================================================================

#include <Arduino.h>
#include <esp_timer.h>
#include "../engine/Clock.h"

namespace sce {

class ArduinoClock : public Clock {
public:
    uint32_t ms() const override { return millis(); }
    uint64_t us() const override { return (uint64_t)esp_timer_get_time(); }
};

} // namespace sce
