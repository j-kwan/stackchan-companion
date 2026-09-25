#pragma once
// =============================================================================
// Command.h — StackChan-Companion (behavior)
// =============================================================================
// The ONE AND ONLY channel for mutating behaviour: every source (API, touch,
// sensors, rule engine, BLE...) posts a `Command` into the Brain's CommandQueue
// (rule A2.7). Split out of Brain.h (2026-07-21) so that PURE modules
// (RuleEngine, native tests) can include it without pulling in FreeRTOS/Arduino.
//
// CONSUMER: Brain::tick() drains the queue at the top of every 100 Hz tick
// (xQueueReceive until empty) and dispatches in Brain::handle(). While the
// Brain is SUSPENDED (exclusive launcher mode) the drain still runs but every
// command is DROPPED — a suspended Brain must never let the queue clog up.
// A posted command is therefore a REQUEST, not an effect: it is applied one
// tick later at the earliest, and the Brain arbitrates it against the reflexes
// and the dance in progress (ROADMAP §A2.8).
//
// PAYLOAD: one generic 5-field envelope (i / u / f / f2 / ptr) reused by every
// command — which fields carry meaning is documented against each enumerant
// below, not against the fields, because the fields have no meaning of their
// own. `ptr` must point at STATIC storage: a command can still be sitting in
// the queue long after the producer that posted it has returned (ROADMAP
// §A2.17, last bullet — DanceStore's double bank exists for exactly this,
// never a dynamic buffer).
//
// PURITY: depends on <cstdint> only.
// =============================================================================

#include <cstdint>

namespace sce {

enum class CmdType : uint8_t {
    SetEmotion,    // i = eEmotions, u = duration ms (0 = default 10 s) — override
    Blink,         // deliberate blink of both eyes
    WinkLeft,
    WinkRight,
    SetCrt,        // i = 0/1 — CRT effect master switch
    SetColorDim,   // f = factor [0..1]
    PlayDance,     // i = index into dances::table() — preempts the running dance
    PlayCustom,    // ptr = DanceKey[] (STATIC DanceStore memory), i = count,
                   // u = 1 if mirrorable — SD choreographies (§CHOREGRAPHIES.md)
    AbortDance,    // cuts the dance short (API stop)
    MoveHead,      // servo remote control (API §4d): f = yaw, f2 = pitch (deg),
                   // i = 1 → relative to the current pose, u = duration ms
    SoundDir,      // noise tracking (SoundTracker): f = left/right imbalance
                   // [-1..+1], f2 = RMS level — head turns toward the noise
    AmbientDark,   // i = 0/1 — NIGHT mode (light sensor, dark_sleepy option):
                   // the ROULETTE replaces Normal with Sleepy at a dominant
                   // weight. Posted on TRANSITION only — not an override, so
                   // reflexes/dances/API stay in charge.
};

struct Command {
    CmdType     type;
    int32_t     i = 0;
    uint32_t    u = 0;
    float       f = 0.0f;
    float       f2 = 0.0f;
    const void* ptr = nullptr;   // PlayCustom: keyframes (static, stable)
};

} // namespace sce
