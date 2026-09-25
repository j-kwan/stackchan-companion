#pragma once
// =============================================================================
// SoundViz.h — StackChan-Companion (app)
// =============================================================================
// Turns a block of microphone samples into everything the status band draws:
// an OSCILLOSCOPE TRACE per microphone, the ENVELOPE of that trace with its
// peak hold — the two bar skins' own picture — and a BAND SPECTRUM per
// microphone, which nothing paints today and which is the assistant mouth's
// food (ROADMAP §8).
//
// ── WHERE IT LIVES, AND WHY ────────────────────────────────────────────────
// On loop(), in the same pass that already consumes a microphone block
// (`SoundTracker`). That placement is not incidental:
//   · the samples are ALREADY THERE. Handing the ring to another task would
//     introduce the first cross-task access to memory whose only protection is
//     the convention that loop() is its sole reader;
//   · A2.15 puts every bit of smoothing on the app side. Attack/release, the
//     TWO peak holds and the decibel scale therefore belong HERE, and the
//     renderer draws exactly what it is handed. That is the whole reason
//     matrix's peak marker is computed in this file and not on the display
//     column it decorates;
//   · A2.1 says one task owns the panel. This one computes and publishes; it
//     never draws.
//
// ── THE TRANSPORT ──────────────────────────────────────────────────────────
// A `TripleBuffer<SoundFrame>`, the project's own single-producer /
// single-consumer bus. The blackboard cannot carry this: it holds one float or
// one short string per key, under a spinlock sized for 24-byte copies, and a
// spectrum would be sixty-four keys.
//
// ── THE COST ───────────────────────────────────────────────────────────────
// One 512-point transform per 32 ms block — about thirty a second — and it
// yields BOTH channels, because a real signal has a conjugate-symmetric
// spectrum and the two therefore pack into one complex transform (Fft.h).
// =============================================================================

#include <math.h>
#include <string.h>
#include "engine/Fft.h"
#include "engine/FaceState.h"    // TripleBuffer lives there
#include "engine/SoundFrame.h"   // the type both ends share

namespace sce {

class SoundViz {
public:
    // 16 kHz, 512 frames per block — the microphone's own cadence. Fixing them
    // here rather than passing them in keeps the band edges a compile-time
    // table instead of something recomputed per block.
    static constexpr int   N    = 512;
    static constexpr float RATE = 16000.0f;

    explicit SoundViz(TripleBuffer<SoundFrame>& bus) : _bus(bus) {
        dsp::bandEdges(_edge, SoundFrame::BAND_N, N, RATE, 60.0f, 7000.0f);
    }

    // The microphone is not listening. Publishes ONE frame that says so, then
    // stays quiet — the renderer holds the last frame it read, and a widget
    // repainting an unchanged nothing thirty times a second is pure cost.
    //
    // THE FIRST CALL PUBLISHES (`_wasLive` starts true). At boot the
    // microphone is off for twenty seconds (A2.20) and it stays off for ever
    // when `sound_track` is 0, which is the default: a producer that only
    // published on a live→idle EDGE would never publish at all in either case,
    // the renderer would find nothing on the bus, and the band would sit blank
    // instead of saying the microphone is not listening.
    void idle() {
        if (!_wasLive) return;
        _wasLive = false;
        SoundFrame& f = _bus.beginWrite();
        f = SoundFrame();                   // all zeroes, live = false
        _bus.publish();
        // THE PEAK-HOLD FORGETS TOO. It is a memory of the last few seconds;
        // across a silence of a minute — the speaker taking the bus, the option
        // switched off — it would come back as the first thing the band says
        // when listening resumes, a transient from another era.
        for (int i = 0; i < SoundFrame::ENV_N; i++) _envPk[i] = 0.0f;
    }

    // SENSITIVITY — a gain on the samples, applied BEFORE anything else so
    // the trace, the envelope and the bands scale together. It exists because the
    // decibel floor is a fixed -48 dB while a room is not: an office never
    // leaves the floor, a workshop pins every band. Bounded rather than
    // trusted: this comes from `/api/tuning`, which accepts any float, and a
    // zero or a negative would silence or invert the display.
    void setGain(float g) {
        _gain = (g < 0.1f) ? 0.1f : (g > 16.0f ? 16.0f : g);
    }

    // One block, interleaved stereo, `N` L/R pairs.
    //
    // CHANNEL ORDER IS NOT THE OBVIOUS ONE: on this board sample 2i is the
    // RIGHT microphone and 2i+1 the LEFT (validated on hardware, and the same
    // order `SoundDirection` reads). Getting it backwards mirrors the whole
    // display and nothing about the picture says so.
    void feed(const int16_t* lr) {
        if (!lr) return;
        for (int i = 0; i < N; i++) {
            _r[i] = (float)lr[2 * i]     * _gain;
            _l[i] = (float)lr[2 * i + 1] * _gain;
        }

        SoundFrame& f = _bus.beginWrite();
        f = SoundFrame();
        f.live = true;
        // The trace comes from the raw samples, before the transform destroys
        // them — MINUS THE MEAN. The band path removes it (Fft.h says the
        // mic's DC dwarfs the signal); the trace used to keep it, so a mic
        // riding a positive offset never crossed zero, the trigger never
        // fired, and the tone slid sideways for ever — the artifact the
        // trigger exists to prevent. Same fact, both paths.
        float meanL = 0.0f, meanR = 0.0f;
        for (int i = 0; i < N; i++) { meanL += _l[i]; meanR += _r[i]; }
        meanL /= (float)N; meanR /= (float)N;
        trace(_l, meanL, f.waveL);
        trace(_r, meanR, f.waveR);

        // The envelope the bar skins draw, from the SAME trace the curve
        // skin plots — three views of one picture, so switching skin can
        // never change what is being said.
        envelope(f);

        // ONE transform for the two channels (Fft.h). It consumes `_l`/`_r`.
        if (dsp::magnitudes2(_l, _r, _magL, _magR, N)) {
            bands(_magL, _smL, _pkL, f.bandL, f.peakL);
            bands(_magR, _smR, _pkR, f.bandR, f.peakR);
        }
        _bus.publish();
        _wasLive = true;
    }

private:
    // ---- the trace -----------------------------------------------------
    // TRIGGERED on a rising zero crossing, and that is what separates a scope
    // from a shimmer: without it the same tone starts at a different phase in
    // every block and the trace slides sideways for ever. The search is
    // bounded to the first half of the block; finding nothing (silence, or a
    // signal that never crosses) simply starts at zero.
    //
    // THE WINDOW IS FIXED, and that is the other half of the job. Plotting
    // "whatever remains after the trigger" makes the time base depend on where
    // the trigger landed: the same tone comes out stretched by a few percent
    // from one block to the next, and the picture breathes horizontally
    // instead of standing still. A fixed span means one pixel is always the
    // same number of microseconds. Half the block, so the search above can
    // never run past the end.
    static constexpr int SPAN = N / 2;
    static void trace(const float* x, float mean, int8_t* out) {
        int start = 0;
        for (int i = 1; i < N / 2; i++) {
            if (x[i - 1] - mean <= 0.0f && x[i] - mean > 0.0f) { start = i; break; }
        }
        // Nearest-neighbour decimation over that window: one sample per
        // column, no averaging. Averaging would turn a square wave into a
        // sine, which is the one thing a scope must not do.
        for (int c = 0; c < SoundFrame::WAVE_N; c++) {
            const int i = start + (int)((int64_t)c * (SPAN - 1) / (SoundFrame::WAVE_N - 1));
            int v = (int)((x[i] - mean) * 127.0f / 20000.0f);   // full scale a little
            if (v < -127) v = -127;                    // before the ADC's, so
            if (v >  127) v =  127;                    // normal speech is visible
            out[c] = (int8_t)v;
        }
    }

    // ---- the envelope, and its memory ------------------------------------
    // One value per display column: the loudest sample of the group, taking
    // the louder microphone — a silhouette has ONE height, and averaging the
    // two would let a channel hide inside the other.
    //
    // The peak-hold lives here rather than in the renderer for the reason
    // A2.15 exists: it is a memory of what the room did, not a property of the
    // pixels. Instant up, then a constant fall of 2.5 per block — about three
    // seconds from full scale at ~31 blocks a second, long enough for a clap
    // to leave a visible mark and short enough not to fossilise it.
    void envelope(SoundFrame& f) {
        constexpr int GRP = SoundFrame::WAVE_N / SoundFrame::ENV_N;
        for (int i = 0; i < SoundFrame::ENV_N; i++) {
            int amax = 0;
            for (int g = 0; g < GRP; g++) {
                const int c = i * GRP + g;
                int a = f.waveL[c] < 0 ? -f.waveL[c] : f.waveL[c];
                const int r = f.waveR[c] < 0 ? -f.waveR[c] : f.waveR[c];
                if (r > a) a = r;
                if (a > amax) amax = a;
            }
            int v = amax * 255 / 127;
            if (v > 255) v = 255;
            f.env[i] = (uint8_t)v;
            if ((float)v >= _envPk[i]) _envPk[i] = (float)v;
            else                       _envPk[i] -= 2.5f;
            if (_envPk[i] < 0.0f) _envPk[i] = 0.0f;
            f.envPeak[i] = (uint8_t)_envPk[i];
        }
    }

    // ---- the spectrum --------------------------------------------------
    // Per band: the PEAK of its bins, a decibel scale, then an ASYMMETRIC
    // smoothing — fast up, slow down. Symmetric smoothing is what makes a
    // spectrum look like jelly: it rounds off the attack, which is the part
    // of a sound the eye actually reads.
    void bands(const float* mag, float* sm, float* pk,
               uint8_t* outBand, uint8_t* outPeak) {
        for (int b = 0; b < SoundFrame::BAND_N; b++) {
            const float v = dsp::dbNorm(dsp::bandValue(mag, _edge, b));
            sm[b] = (v > sm[b]) ? (sm[b] + 0.55f * (v - sm[b]))   // attack
                                : (sm[b] - 0.14f * (sm[b] - v));  // release
            // The peak marker: instant up, then a slow constant fall. It is
            // the only part of this display with a memory, and it is what lets
            // you see a transient that the bar itself has already forgotten.
            if (sm[b] >= pk[b]) pk[b] = sm[b];
            else                pk[b] -= 0.010f;
            if (pk[b] < 0.0f) pk[b] = 0.0f;
            outBand[b] = (uint8_t)(sm[b] * 255.0f + 0.5f);
            outPeak[b] = (uint8_t)(pk[b] * 255.0f + 0.5f);
        }
    }

    TripleBuffer<SoundFrame>& _bus;
    int   _edge[SoundFrame::BAND_N + 1] = { 0 };
    // Working buffers as MEMBERS, not locals: loop() has one stack for every
    // module it steps, and four kilobytes of float arrays do not belong on it.
    float _l[N] = { 0 }, _r[N] = { 0 };
    float _magL[N / 2] = { 0 }, _magR[N / 2] = { 0 };
    float _envPk[SoundFrame::ENV_N] = { 0 };   // see envelope()
    float _smL[SoundFrame::BAND_N] = { 0 }, _smR[SoundFrame::BAND_N] = { 0 };
    float _pkL[SoundFrame::BAND_N] = { 0 }, _pkR[SoundFrame::BAND_N] = { 0 };
    float _gain    = 1.0f;      // see setGain()
    bool  _wasLive = true;      // see idle(): the first call must publish
};

} // namespace sce
