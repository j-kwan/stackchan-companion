#pragma once
// =============================================================================
// SoundFrame.h — StackChan-Companion (engine)
// =============================================================================
// What the sound analyser hands the status band: one triggered oscilloscope
// trace per microphone, the ENVELOPE of that trace with its peak hold, and one
// band spectrum per microphone.
//
// Everything here is FINISHED — triggered, scaled, smoothed, held. The renderer
// paints it and derives nothing (A2.15), which is why the envelope and the
// peak hold are fields of this struct and not arithmetic in `drawSound`.
//
// It lives in `engine/` and not beside the analyser because BOTH ENDS need it
// and they sit on opposite sides of the layering — the analyser is app code on
// loop(), the band is the renderer. A shared type that only one of them owns is
// how a layer ends up including the other one's headers.
//
// Fixed size, no pointers, trivially copyable: it crosses a task boundary
// inside a `TripleBuffer`, which swaps whole slots.
// =============================================================================

#include <stdint.h>

namespace sce {

struct SoundFrame {
    static constexpr int WAVE_N = 160;   // scope columns per channel —
                                         // 160 x 2 px spans the panel's
                                         // full 320, edge to edge
    static constexpr int BAND_N = 16;    // spectrum bands per channel

    // The trace, signed, 0 = the centre line. One byte per column is the whole
    // vertical resolution the band has anyway.
    int8_t  waveL[WAVE_N] = { 0 };
    int8_t  waveR[WAVE_N] = { 0 };
    // THE ENVELOPE of the trace, one value per display column: the louder of
    // the two microphones over each group of samples. It is what the `columns`
    // and `matrix` skins draw — the same waveform as `wave`, quantised.
    //
    // It is computed HERE, on the producer side, and not in the renderer that
    // consumes it, because `envPeak` is a peak-HOLD: a memory, i.e. smoothing,
    // and A2.15 says smoothing has exactly one home. A renderer that derived
    // its own peak would be a second opinion about how loud the room just was.
    static constexpr int ENV_N = 32;     // display columns of the bar skins
    uint8_t env[ENV_N]     = { 0 };
    uint8_t envPeak[ENV_N] = { 0 };      // instant up, slow fall
    // The spectrum, 0..255 after the decibel scale, the attack/release and the
    // peak hold — all of which happen on the producer side (A2.15). `peak`
    // trails `band` and falls on its own clock.
    uint8_t bandL[BAND_N] = { 0 };
    uint8_t bandR[BAND_N] = { 0 };
    // Nothing DRAWS the bands today — the three styles are skins of the
    // waveform. They stay computed and published because they are the
    // assistant mouth's food (ROADMAP §8), and because dropping them would
    // mean rebuilding the FFT path to get them back.
    uint8_t peakL[BAND_N] = { 0 };
    uint8_t peakR[BAND_N] = { 0 };
    // false = the microphone is not listening: the speaker has the shared I2S
    // bus, or the boot warm-up is not over. The band must SAY that rather than
    // draw a silence it never measured.
    bool    live = false;
};

} // namespace sce
