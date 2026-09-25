#pragma once
// =============================================================================
// Clock.h — StackChan-Companion (engine)
// =============================================================================
// Time abstraction (docs/architecture/CONVENTIONS.md §5).
// engine/ and behavior/ NEVER call millis()/micros() directly: they are
// handed a Clock. Two implementations:
//   - ArduinoClock (hal/ArduinoClock.h): millis/esp_timer — production
//   - FakeClock (below)                : time driven by the native tests
//
// Why: state machines built on falling edges/timeouts/suspend-restore wired
// straight to millis() cannot be tested off hardware → a whole class of
// synchronisation bugs only shows up after a flash. With a FakeClock, a
// native test advances time millisecond by millisecond and checks every
// state transition deterministically.
//
// Usage rules (wrap-safe):
//   - relative durations only: `clock.ms() - startMs >= durMs`
//   - never compare absolute timestamps
//
// PURITY: no Arduino dependency → compiles natively.
// =============================================================================

#include <cstdint>

namespace sce {

// -----------------------------------------------------------------------
// Interface — injected into every component that needs time
// -----------------------------------------------------------------------
class Clock {
public:
    virtual ~Clock() = default;
    virtual uint32_t ms() const = 0;  // milliseconds since boot (wraps at ~49 days)
    virtual uint64_t us() const = 0;  // microseconds (perf measurements)
};

// -----------------------------------------------------------------------
// FakeClock — simulated time for the native unit tests
//
//   FakeClock clk;
//   BlinkController bc(clk);
//   clk.advanceMs(3500);   // "3.5 s later"
//   bc.update();           // → must have fired a blink
// -----------------------------------------------------------------------
class FakeClock : public Clock {
public:
    uint32_t ms() const override { return _nowUs / 1000ULL; }
    uint64_t us() const override { return _nowUs; }

    void advanceMs(uint32_t deltaMs) { _nowUs += (uint64_t)deltaMs * 1000ULL; }
    void advanceUs(uint64_t deltaUs) { _nowUs += deltaUs; }
    void setMs(uint32_t absoluteMs)  { _nowUs = (uint64_t)absoluteMs * 1000ULL; }

private:
    uint64_t _nowUs = 0;
};

} // namespace sce
