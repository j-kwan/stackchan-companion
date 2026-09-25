#pragma once
// =============================================================================
// SoundFx.h — StackChan-Companion (app)
// =============================================================================
// Cozmo-style procedural chirps (ROADMAP §3.9, T4). M5.Speaker (AW88298).
//
// Principles (§3.9):
//   - EVENT-DRIVEN only — total silence when idle (silence = rest).
//   - A chirp is a sequence of 2-4 short notes (60-120 ms), non-blocking
//     (state machine in update(), M5.Speaker.tone is asynchronous).
//   - Valence: positive emotion → RISING pattern; negative → FALLING;
//     Surprised → "?!" two high notes; Sleepy → slow low sigh.
//   - 400 ms throttle between chirps.
//   - OFF by default: POST /api/tuning?sound=1 (+ sound_volume 0-255).
//
// Event detection: polls Brain::currentEmotion() (atomic) from loop() — same
// pattern as EmotionLeds (NO faceBus.read(), rule A2.5).
// Future extensions (dance, saccade, boot jingle): wire explicit Brain
// events — noted in ROADMAP §3.9.
// =============================================================================

#include <Arduino.h>
#include <M5Unified.h>
#include "../engine/Emotions.h"
#include "../engine/Tuning.h"
#include <time.h>                          // NTP-synced wall clock
#include "../../firmware/common/SunClock.h"  // real sunset/sunrise, not a window

namespace sce {

class SoundFx {
public:
    explicit SoundFx(const Tuning& tuning) : _tuning(tuning) {}

    // Call from loop() (~50 Hz)
    void update(eEmotions emotion) {
        uint32_t now = millis();
        bool enabled = _tuning.sound >= 0.5f;

        // ---- Advance the chirp in progress (non-blocking) ----
        if (_stepCount && now >= _nextStepMs) {
            if (_stepIdx < _stepCount) {
                M5.Speaker.setVolume((uint8_t)nightVolume());
                M5.Speaker.tone(_steps[_stepIdx].freq, _steps[_stepIdx].ms);
                _nextStepMs = now + _steps[_stepIdx].ms + 15;   // small gap
                _stepIdx++;
            } else {
                _stepCount = 0;   // sequence finished
            }
        }

        // ---- Emotion change detection ----
        if (emotion == _lastEmotion) return;
        _lastEmotion = emotion;

        if (!enabled) return;
        if (now - _lastChirpMs < 400) return;   // throttle §3.9
        // I2S1 bus shared with the microphone (sound_track, 2026-07-16): the
        // chirp has PRIORITY — grab the bus on the fly (the SoundTracker
        // takes it back 300 ms after the chirp ends). REAL state comes from
        // isRunning() — isEnabled() only reflects the pin configuration.
        if (M5.Mic.isRunning()) M5.Mic.end();
        if (!M5.Speaker.isRunning()) M5.Speaker.begin();
        _lastChirpMs = now;
        startChirp(emotion);
    }

private:
    struct Step { uint16_t freq; uint16_t ms; };

    const Tuning& _tuning;
    eEmotions _lastEmotion = EMOTIONS_COUNT;   // 1st update: no boot chirp
    uint32_t  _lastChirpMs = 0;
    Step      _steps[4];
    int       _stepCount = 0, _stepIdx = 0;
    uint32_t  _nextStepMs = 0;

    // ---- Night volume, driven by the REAL SUN (08-01, §3.9).
    // It used to be a fixed 22 h-6 h window read off `M5.Rtc.getTime()`, and
    // it was wrong twice over: NOTHING in this project ever set the BM8563, so
    // the hour was whatever the chip happened to hold, and the BM8563 keeps
    // UTC anyway — 22 h-6 h would have fired at 02 h-10 h local here even with
    // a correct clock. The feature has been in the backlog as "never observed
    // at night" (T9) since it shipped, and this is why.
    // Now: NTP time (main.cpp syncs it once WiFi is up, and sets the RTC from
    // it so the chip finally means something) + `sce::isNight` over the
    // robot's own lat/lon — the SAME solar code the radar bin uses, so the two
    // binaries cannot disagree about what "night" is.
    // NO VALID TIME => NOT NIGHT. The loud setting is the safe default: a
    // robot that is too quiet at noon is a nuisance, one that is loud at 3 am
    // is the reason the option exists.
    // Evaluated at a low rate (60 s): sunrise does not need polling per chirp.
    uint32_t _lastNightCheckMs = 0;
    bool     _isNight          = false;
    float nightVolume() {
        uint32_t now = millis();
        if (now - _lastNightCheckMs >= 60000 || _lastNightCheckMs == 0) {
            _lastNightCheckMs = now;
            const time_t t = time(nullptr);
            // Same epoch sanity guard the radar uses: an unsynced clock starts
            // in 1970 and would answer for a date the almanac cannot mean.
            _isNight = sce::clockSynced(t) &&
                       sce::isNight(_tuning.lat, _tuning.lon, t);
        }
        return _isNight ? _tuning.sound_volume_night : _tuning.sound_volume;
    }

    // Emotion valence → pattern (frequencies in the "robot beep" register)
    void startChirp(eEmotions e) {
        _stepIdx = 0; _nextStepMs = 0;
        switch (e) {
        case Surprised: case Awe:               // "?!" — two high notes
            set({{1400, 70}, {1900, 110}});      break;
        case Happy: case Glee: case Excited: case Smug:
        case Curious: case Blush:                // cheerful rise
            set({{900, 60}, {1200, 60}, {1600, 90}}); break;
        case Sad: case Worried: case Frustrated: // sad fall
            set({{900, 90}, {700, 90}, {520, 130}});  break;
        case Angry: case Furious: case Annoyed: case Scary:
        case Disgust:                            // dry low buzz
            set({{300, 90}, {260, 120}});        break;
        case Scared: case Frozen:                // falling trill
            set({{1600, 50}, {1200, 50}, {800, 50}, {500, 80}}); break;
        case Sleepy:                             // slow low sigh
            set({{500, 160}, {380, 220}});       break;
        case Dead:                               // single flat note
            set({{220, 300}});                   break;
        default:                                 // back to neutral: quiet tick
            set({{1000, 40}});                   break;
        }
    }

    void set(std::initializer_list<Step> s) {
        _stepCount = 0;
        for (auto& st : s) { if (_stepCount < 4) _steps[_stepCount++] = st; }
    }
};

} // namespace sce
