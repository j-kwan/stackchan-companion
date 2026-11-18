#pragma once
// =============================================================================
// FaceState.h — StackChan-Companion (engine)
// =============================================================================
// THE facial state snapshot (ROADMAP §3.1/§3.5): a POD struct written by a
// single producer (Brain — or the app until P2 lands) and consumed by the
// Renderer through a lock-free TRIPLE BUFFER. The renderer never blocks the
// producer, the producer never blocks the renderer, and the renderer always
// reads a COMPLETE, coherent snapshot (never a half-written state — a whole
// class of synchronisation bugs disappears by construction).
//
// Event-ish fields: one-shot triggers (blink) are COUNTERS — point events
// travel through the CommandQueue, not through here.
// Pattern is robust against the triple buffer: even if an intermediate
// snapshot is skipped, the counter still carries the event.
//
// PURITY: no Arduino/M5GFX dependency — the TripleBuffer is tested natively
// (test_facestate).
// =============================================================================

#include <cstdint>
#include <atomic>
#include "Units.h"
#include "Emotions.h"
#include "Transitions.h"

namespace sce {

// ------------------------------------------------------------------
// CRT options (§3.8) — carried inside the snapshot, so they can be toggled
// at runtime
// ------------------------------------------------------------------
struct CrtOptions {
    bool enabled  = false;   // master switch — off by default (CONFIG)
    bool scanlines = true;   // individual components (only if enabled)
    bool phosphor  = true;
    bool flicker   = true;
    bool glow      = true;
    // Glow parameters — copied from the Tuning registry by the Brain
    // (live-tunable via /api/tuning, P1b verdicts: 3 px / 28 %)
    float glowPx  = 3.0f;
    float glowDim = 0.28f;
};

// ------------------------------------------------------------------
// FaceState — everything the Renderer needs in order to draw one frame
// ------------------------------------------------------------------
struct FaceState {
    // Expression
    eEmotions        emotion    = Normal;
    TransitionConfig transition = DefaultTransitions::NORMAL;  // for THIS change
    // RANDOM mirroring of the asymmetries (2026-07-16): drawn by the Brain on
    // every emotion episode (and LOCKED onto the mirrored direction of the
    // running dance — the eye carrying the movement matches its direction).
    bool             asymMirror = false;

    // Gaze (gaze units, viewer-centric — CONVENTIONS §1/§2)
    Vec2f gaze{};

    // Eyelids [0..1] per eye (BlinkController P2; 1.0 = open)
    float openL = 1.0f;
    float openR = 1.0f;

    // Breathing [-1..+1] — projected to ±3 px by the Renderer
    float breath = 0.0f;

    // Squash & stretch (§3.0-2) — procedural scale multipliers computed by
    // the Brain from gaze dynamics (1.0 = neutral)
    float squashX = 1.0f;
    float squashY = 1.0f;

    // Rendering
    float      colorDim   = 0.80f;  // tuning.eye_color_dim
    float      depthScale = 0.0f;   // tuning.eye_depth_scale — strength of
                                    // the near/far effect (0 = pure offset,
                                    // the default — user request 2026-07-11)
    float      eyeSpacing = 0.0f;   // tuning.eye_spacing — separation (px per
                                    // eye away from the default position)
    CrtOptions crt{};
};

// ==================================================================
// TripleBuffer<T> — lock-free 1 producer → 1 consumer publication
//
// Classic algorithm: 3 slots; the producer writes into `back` then swaps it
// atomically with `middle` (tagged FRESH); the consumer, if it sees FRESH,
// swaps `middle` with `front` and reads `front`.
// Each side exclusively owns its slot between two swaps → no concurrent
// write on a slot being read. The swap is a single atomic XCHG.
// ==================================================================
template <typename T>
class TripleBuffer {
public:
    TripleBuffer() : _middle(1) {}   // back=0, middle=1, front=2

    // --- Producer side (Brain) ---
    T&   beginWrite() { return _slots[_back]; }
    void publish() {
        // The written slot becomes middle (FRESH); the old middle becomes back.
        _back = _middle.exchange(_back | FRESH, std::memory_order_acq_rel) & IDX_MASK;
        _published.store(true, std::memory_order_release);
    }

    // --- Consumer side (Renderer) ---
    // Returns the most recent snapshot (the previous one if nothing is new).
    const T& read() {
        if (_middle.load(std::memory_order_acquire) & FRESH) {
            _front = _middle.exchange(_front, std::memory_order_acq_rel) & IDX_MASK;
        }
        return _slots[_front];
    }

    // True once publish() has run at least once. Before that, read() can only
    // return the DEFAULT-constructed slot — a state nobody produced. The
    // consumer uses this to refuse to draw it (review 08-04): the boot-order
    // fix ("Brain first, 60 ms head start") only made the race unlikely, and
    // a Brain whose first tick slips past the head start put the default
    // Normal on screen again. This gate closes the race for every boot path,
    // whatever the scheduling.
    bool hasEverPublished() const {
        return _published.load(std::memory_order_acquire);
    }

private:
    static constexpr uint8_t FRESH    = 0x80;
    static constexpr uint8_t IDX_MASK = 0x03;

    T                    _slots[3]{};
    std::atomic<uint8_t> _middle;
    std::atomic<bool>    _published{false};
    uint8_t              _back  = 0;   // owned by the producer
    uint8_t              _front = 2;   // owned by the consumer
};

} // namespace sce
