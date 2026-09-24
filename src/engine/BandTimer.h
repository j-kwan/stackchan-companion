#pragma once
// =============================================================================
// BandTimer.h — StackChan-Companion (engine)
// =============================================================================
// The state machines behind the two interactive status-band modes (user
// 08-04): a countdown TIMER set by swiping on its HH / MM halves, and a
// POMODORO. The machines live HERE, pure and Clock-injected, tested natively
// (test_bandtimer); loop() owns the single instance, feeds it taps and
// swipes, publishes its display state through the FieldStore blackboard, and
// turns its EVENTS into emotions through the CommandQueue (A2.4/A2.5 — this
// file never touches the Brain, the renderer, or the display).
//
// INTERACTION CONTRACT (the one the touch wiring in main.cpp implements):
//   · hold-and-slide (or flick, or the alarm-clock taps) on the band,
//     left half  = MINUTES ±1 (wraps 0..99),
//     right half = SECONDS ±1 (wraps 0..59)
//     — accepted while Idle or Paused; a running countdown is not edited,
//     it is paused first (deliberate: an accidental brush must not silently
//     change an armed timer).
//   · tap on the band = the PRIMARY action of the current phase:
//     Idle→Run (if a duration is set), Run→Paused, Paused→Run, Ring→Idle
//     (acknowledge; the set duration is restored for reuse), Done→Idle.
//
// EVENTS, consumed by loop() and turned into emotions (documented in
// STATUSBAR.md): Ring → Excited (the alarm face — star eyes), WorkStart →
// Focused, BreakStart → Happy, AllDone → Glee. The machine only REPORTS;
// what an alarm looks like is the caller's decision.
// =============================================================================

#include <cstdint>
#include "Tuning.h"

namespace sce {

class BandTimer {
public:
    enum class Kind  : uint8_t { Timer, Pomodoro };
    // Hydrate sits BETWEEN work and break, and that placement is the whole
    // point: a drink prompt at the START of the pause is a prompt you act on
    // while you are getting up. Folded into the break it is a label nobody
    // reads; put after it, it interrupts the return to work.
    enum class Phase : uint8_t { Idle, Run, Paused, Ring, Work, Break, Done, Hydrate };
    enum class Event : uint8_t { None, Ring, WorkStart, BreakStart, AllDone,
                                 HydrateStart };

    // NO CLOCK IS INJECTED, and that is not an oversight against A2.7. This
    // machine never asks what time it is: `tap()`, `tick()` and `remainingS()`
    // are all handed the instant by their caller, precisely so the one pass of
    // loop() that drives them sees a SINGLE timestamp (the wrap-safe window in
    // remainingS() exists because that discipline was once broken). A `const
    // Clock&` member sat here unused for the whole life of the file; do not
    // put it back to satisfy the pattern — the pattern is "time comes from
    // outside", and a parameter is a stricter way of saying it than a member.
    explicit BandTimer(const Tuning& tuning) : _tuning(tuning) {}

    // Switching band mode re-arms the machine for that mode: a half-run
    // pomodoro must not leak into the timer's display, nor the other way.
    void setKind(Kind k) {
        if (k == _kind) return;
        _kind  = k;
        _phase = Phase::Idle;
    }
    Kind  kind()  const { return _kind; }
    Phase phase() const { return _phase; }

    // ---- Timer setting (kind Timer, Idle/Paused only) --------------------
    // MM:SS, minutes up to 99 (user 08-04, final form after HH:MM and
    // HH:MM:SS both lived less than an hour): a band timer is a kitchen
    // timer, and 99 minutes outlast any pasta.
    void addMinutes(int d) { if (editable()) _setM = wrap(_setM + d, 100); }
    void addSeconds(int d) { if (editable()) _setS = wrap(_setS + d, 60); }
    // PUBLIC, so the touch wiring routes on the SAME predicate this class
    // enforces (fix 08-25). It did not: the drag and the tap keypad both
    // accepted `Paused`, which `editable()` has refused since it was narrowed
    // to Idle. The setters then did nothing while the gesture still counted
    // itself as consumed — so a brush on a PAUSED countdown swallowed the tap
    // that would have resumed it, and a keypad press outside the centre third
    // did nothing at all. The narrowing fixed "silently rewrites the setting";
    // leaving the callers on their own copy of the rule turned it into
    // "silently does nothing", which is the same bug wearing a quieter coat.
    bool settable() const { return editable(); }
    int  setM() const { return _setM; }
    int  setS() const { return _setS; }

    // ---- The primary tap -------------------------------------------------
    void tap(uint32_t now) {
        switch (_phase) {
        case Phase::Idle:
            if (_kind == Kind::Timer) {
                if (_setM == 0 && _setS == 0) return;   // nothing armed
                _remainS = (uint32_t)_setM * 60u + (uint32_t)_setS;
                startRun(now, Phase::Run);
            } else {
                _cycle = 1;
                _remainS = workS();
                startRun(now, Phase::Work);
                _pending = Event::WorkStart;
            }
            break;
        case Phase::Run:
        case Phase::Work:
        case Phase::Break:
        case Phase::Hydrate:
            // Pause FREEZES the remainder: the resumed countdown continues
            // from the frozen value, it does not re-anchor on wall time.
            _remainS = remainingS(now);
            _resume  = _phase;
            _phase   = Phase::Paused;
            break;
        case Phase::Paused:
            startRun(now, _resume);
            break;
        case Phase::Ring:                      // acknowledge the alarm
        case Phase::Done:
            _phase = Phase::Idle;
            break;
        }
    }

    // HOLD on the band = reset (user 08-04): back to Idle from ANY phase.
    // The timer keeps its set duration (a reset empties the countdown, not
    // the setting); the pomodoro restarts from cycle 1 on its next start.
    void reset() { _phase = Phase::Idle; _pending = Event::None; }

    // ---- The 10 ms heartbeat --------------------------------------------
    // Returns at most ONE event per call; expiry decides the next phase.
    Event tick(uint32_t now) {
        if (_pending != Event::None) {          // start events (tap-produced)
            Event e = _pending;
            _pending = Event::None;
            return e;
        }
        if (_phase != Phase::Run && _phase != Phase::Work &&
            _phase != Phase::Break && _phase != Phase::Hydrate)
            return Event::None;
        if (remainingS(now) > 0) return Event::None;
        return advance(now);
    }

    // SKIP THE CURRENT PHASE (user 08-25). The verb that was missing: "this
    // block is done early", "I do not want this break". Until now the only
    // way out of a phase was reset(), which throws the whole session away —
    // so the cheap act had only the expensive expression.
    //
    // It takes the SAME transition expiry takes (advance()), so a skipped
    // block advances the cycle counter, honours the last-block rule and
    // raises the same event as one that ran its course. Two transition tables
    // would be two answers to "what comes after work".
    //
    // The event is STAGED rather than returned: loop() already maps events to
    // emotions in one switch, and a second mapping at the touch call site is
    // how those two drift apart. Same channel tap() uses for WorkStart.
    void skip(uint32_t now) {
        if (_phase != Phase::Run && _phase != Phase::Work &&
            _phase != Phase::Break && _phase != Phase::Hydrate) return;
        _pending = advance(now);
    }

    // How long the CURRENT phase lasts in total — the denominator of the
    // progress bar. DERIVED from the tuning rather than stored at the start
    // of the phase, because `_remainS` becomes the frozen REMAINDER on a
    // pause: a bar built on it would leap forward the moment you paused.
    uint32_t phaseTotalS() const {
        // PAUSED ASKS FOR THE PHASE IT WILL RETURN TO. Answering with the work
        // length regardless would make a bar paused mid-BREAK jump to a
        // different scale the instant you touched it, and back again on
        // resume — the display contradicting itself about a value that did
        // not move.
        return totalOf(_phase == Phase::Paused ? _resume : _phase);
    }

private:
    uint32_t totalOf(Phase p) const {
        switch (p) {
        case Phase::Work:    return workS();
        case Phase::Hydrate: return hydraS();
        case Phase::Break:   return breakS();
        case Phase::Run:     return _kind == Kind::Timer
                                  ? (uint32_t)_setM * 60u + (uint32_t)_setS
                                  : workS();
        default:             return 0u;        // Idle / Ring / Done / Paused
        }
    }

    // The one transition table: what follows a phase that is over, whether it
    // ran out or was skipped.
    Event advance(uint32_t now) {
        switch (_phase) {
        case Phase::Run:
            _phase = Phase::Ring;              // rings until acknowledged
            return Event::Ring;
        case Phase::Work:
            // The LAST work block ends the pomodoro: a break nobody asked
            // for after the final block is homework past the bell. The drink
            // prompt goes with it — the session is over, go and live.
            if (_cycle >= cycles()) {
                _phase = Phase::Done;
                return Event::AllDone;
            }
            // HYDRATE FIRST, when it is worth anything. `pomo_hydra_min` at 0
            // means the phase is skipped entirely and the machine behaves
            // exactly as it did before it existed — the same "off is the old
            // behaviour, exactly" contract the LED depth channel keeps.
            if (hydraS() > 0) {
                _remainS = hydraS();
                startRun(now, Phase::Hydrate);
                return Event::HydrateStart;
            }
            _remainS = breakS();
            startRun(now, Phase::Break);
            return Event::BreakStart;
        case Phase::Hydrate:
            // The drink prompt is a PREFIX of the break, not a replacement:
            // the break that follows is the full configured break. Taking the
            // hydration minute out of it would make enabling the reminder
            // shorten the rest, which is a tax on doing the healthy thing.
            _remainS = breakS();
            startRun(now, Phase::Break);
            return Event::BreakStart;
        case Phase::Break:
            _cycle++;
            _remainS = workS();
            startRun(now, Phase::Work);
            return Event::WorkStart;
        default: return Event::None;
        }
    }

public:
    // ---- Display state ---------------------------------------------------
    uint32_t remainingS(uint32_t now) const {
        switch (_phase) {
        case Phase::Run: case Phase::Work: case Phase::Break:
        case Phase::Hydrate: {
            // WRAP-SAFE against a caller whose `now` PREDATES the tap's
            // (user 08-04: "tap shows 00:00 instead of starting"). loop()
            // captures its timestamp at the top of the pass; the tap lands
            // mid-pass with a fresher millis(), so the same pass's tick ran
            // with now < _startMs — and the unsigned subtraction turned
            // those few milliseconds into ~49 days elapsed: remaining 0,
            // instant ring. A signed window floored at zero.
            const int32_t winMs = (int32_t)(now - _startMs);
            const uint32_t elapsed = winMs <= 0 ? 0u
                                                : (uint32_t)winMs / 1000u;
            return elapsed >= _remainS ? 0u : _remainS - elapsed;
        }
        case Phase::Paused: return _remainS;
        case Phase::Idle:
            return _kind == Kind::Timer
                 ? (uint32_t)_setM * 60u + (uint32_t)_setS
                 : workS();
        default: return 0u;                     // Ring / Done
        }
    }
    int cycle()  const { return _cycle; }       // 1-based current pomodoro
    int cycles() const {
        int n = (int)_tuning.pomo_cycles;
        return n < 1 ? 1 : (n > 8 ? 8 : n);
    }

    // ---- The pomodoro's SHAPES (user 08-25) ------------------------------
    // A vertical swipe on the pomodoro band cycles these. The band already
    // spells the choice out — Idle reads "1/N MM:00" — so the gesture needs
    // no label of its own: the two numbers that change ARE the feedback.
    //
    // DATA, not a switch: the table is the whole definition, adding a shape is
    // adding a row, and the wrap is arithmetic rather than a chain of cases
    // that can disagree with itself in one direction.
    //
    // This lives here, pure and natively tested, rather than in the touch
    // wiring: it is a fact about what a pomodoro can BE, and main.cpp is the
    // only thing allowed to write `tuning`. The machine itself never applies
    // one — it reads whatever `Tuning` ends up holding, exactly as before.
    // FOUR numbers, not three (user 08-25: "it cycles the cycles but not
    // concentration, hydration"). The drink prompt is part of what a session
    // IS -- a 90-minute block and a 15-minute sprint do not want the same
    // reminder -- so leaving it out made one slider sit still while its three
    // neighbours moved, which reads as the control being half-wired.
    struct Preset { uint8_t workMin, breakMin, cycles, hydraMin; };
    static constexpr int PRESET_N = 4;
    static const Preset& preset(int i) {
        // 25/5 is the pomodoro as Cirillo defined it and the default; 50/10
        // and 90/20 are the two longer rhythms people actually keep to (one
        // ultradian cycle for the last); 15/3 is for a bad day, when the
        // point is to start at all.
        // Hydration follows the BLOCK: the longer you sit, the more the
        // reminder is worth, and the sprint drops it entirely because a
        // fifteen-minute block interrupted to drink is not a sprint.
        static const Preset T[PRESET_N] = {
            { 25,  5, 4, 1 },
            { 50, 10, 3, 2 },
            { 90, 20, 2, 3 },
            { 15,  3, 4, 0 },
        };
        return T[i < 0 ? 0 : (i >= PRESET_N ? PRESET_N - 1 : i)];
    }
    // Which row the current settings ARE, or -1 for anything else. Compared on
    // the rounded integers the console's sliders produce, not on the floats:
    // `Tuning` carries every key as a float and 25.0f == 25 has to stay true
    // after a round trip through the YAML.
    static int presetIndex(float workMin, float breakMin, float cyclesF,
                           float hydraMin) {
        const int w = (int)(workMin + 0.5f), b = (int)(breakMin + 0.5f),
                  c = (int)(cyclesF + 0.5f), h = (int)(hydraMin + 0.5f);
        for (int i = 0; i < PRESET_N; i++) {
            const Preset& p = preset(i);
            if (p.workMin == w && p.breakMin == b && p.cycles == c &&
                p.hydraMin == h) return i;
        }
        return -1;
    }
    // The row `dir` steps away from what is configured now. SETTINGS THAT
    // MATCH NO ROW land on the first row of the swipe's direction rather than
    // being treated as row zero: a custom 30/7 typed into the console is not
    // "the classic one", and a gesture that pretended it was would answer the
    // first swipe by changing nothing visible.
    static int nextPreset(float workMin, float breakMin, float cyclesF,
                          float hydraMin, int dir) {
        const int cur = presetIndex(workMin, breakMin, cyclesF, hydraMin);
        if (cur < 0) return dir > 0 ? 0 : PRESET_N - 1;
        return (cur + (dir > 0 ? 1 : PRESET_N - 1)) % PRESET_N;
    }

private:
    // IDLE ONLY. Paused was allowed too, and it was incoherent: a paused
    // countdown lives in `_remainS`, while addMinutes/addSeconds only touch
    // `_setM`/`_setS`. Pause at 03:12 and slide up and the display did not
    // move — the change surfaced on the NEXT arm, minutes later, looking like
    // a fault. Worse through the tap keypad, where a press outside the centre
    // silently rewrote the setting instead of resuming. The header's own
    // contract says a running countdown is paused first and then edited; that
    // is now true, because a pause has to be followed by a reset (a hold) to
    // become editable, which is an explicit act.
    bool editable() const {
        return _kind == Kind::Timer && _phase == Phase::Idle;
    }
    static int wrap(int v, int m) { return ((v % m) + m) % m; }
    void startRun(uint32_t now, Phase p) { _startMs = now; _phase = p; }
    uint32_t workS() const {
        float m = _tuning.pomo_work_min;
        return (uint32_t)((m < 1.0f ? 1.0f : (m > 120.0f ? 120.0f : m)) * 60.0f);
    }
    uint32_t breakS() const {
        float m = _tuning.pomo_break_min;
        return (uint32_t)((m < 1.0f ? 1.0f : (m > 60.0f ? 60.0f : m)) * 60.0f);
    }
    // ZERO IS ALLOWED HERE, unlike work and break, and that asymmetry is the
    // feature: work and break are what a pomodoro IS, so a zero there is a
    // typo and gets clamped up to one minute. Hydration is an addition, so
    // zero is a real answer meaning "not for me" — clamping it to a minute
    // would make the reminder impossible to turn off from the one control
    // that names it.
    uint32_t hydraS() const {
        float m = _tuning.pomo_hydra_min;
        if (m <= 0.0f) return 0u;
        return (uint32_t)((m > 15.0f ? 15.0f : m) * 60.0f);
    }

    const Tuning& _tuning;
    Kind     _kind    = Kind::Timer;
    Phase    _phase   = Phase::Idle;
    Phase    _resume  = Phase::Run;    // what Paused goes back to
    Event    _pending = Event::None;   // start event raised by tap()
    int      _setM = 5, _setS = 0;     // timer default: 5 minutes
    uint32_t _remainS = 0;             // seconds left at _startMs (or frozen)
    uint32_t _startMs = 0;
    int      _cycle = 1;
};

} // namespace sce
