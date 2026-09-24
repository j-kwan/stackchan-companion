#pragma once
// =============================================================================
// Dances.h — StackChan-Companion (behavior)
// =============================================================================
// The dances, ported from the original servo keyframes (Apache-2.0 data,
// inspired by the official StackChan firmware) and RE-CHOREOGRAPHED per
// ROADMAP §3.7:
//
//   - PANIC fixed: the old version commanded 1000 ms servo moves inside
//     100 ms keyframes → limp fidgeting. Now: short moves (120 ms) that are
//     actually reached, amplitude ±25°.
//   - ANTICIPATION: a micro counter-movement (4°, 80 ms) before the large
//     displacements (HAPPY, LOOK_AROUND, SHAKE_NO).
//   - SLOW-IN/SLOW-OUT: gentler entry/exit keyframes, returns to neutral in
//     400-500 ms; the middle movements stay crisp.
//   - The last keyframe is ALWAYS Normal + a neutral pose (otherwise the
//     roulette stays stuck).
//   - NOD/SHY: COMBINED dive (2026-07-16, home pitch 93) — the servo REALLY
//     lowers the head (pitchOff +6 = PITCH_MAX 99, spec 5~85°) AND the
//     gazeYBias dives the eyes: the head genuinely goes down, the gaze
//     emphasises it (§3.7-7).
//   - Angles are OFFSETS (yawOff/pitchOff) taken from Units.h — recalibration
//     stays centralised.
//
// lidEvent: 0=nothing 1=blink 2=left wink 3=right wink.
// emotion EMOTIONS_COUNT = "unchanged" (keeps the current expression).
// =============================================================================

#include "Sequencer.h"

namespace sce {
namespace dances {

// ---- HAPPY: L/R oscillations + left wink, opening with an anticipation ----
inline const DanceKey HAPPY[] = {
    {   0,  0, 200, 250, Happy },
    {  -4,  0,  80,  90, EMOTIONS_COUNT },            // anticipation (counter)
    {  30,  0, 350, 600, Happy },
    { -30,  0, 400, 650, Glee, 2 },                    // left wink
    {  30,  0, 400, 650, Happy },
    { -30,  0, 400, 650, Glee },
    {   0,  0, 450, 600, Normal },                     // soft exit
};

// ---- ROBOT: unashamedly mechanical (the metronomic feel is INTENDED — §3.7).
// This dance turns ±40, and that is a CHOREOGRAPHIC choice, not the limit.
// The clamp in ServoMotion is ±130 since 08-01 (it was ±40, a number with no
// recorded source; the real bound is writeDeg's 0-300 deg span around a centre
// of 166, i.e. +134/-166, and 130 keeps four degrees of margin). Widening the
// clamp deliberately did NOT re-choreograph anything: a bigger envelope is
// available to whoever wants it, the existing dances keep the amplitude they
// were composed and validated with.
inline const DanceKey ROBOT[] = {
    {   0,   0, 400, 450, Normal },
    { -40,   0, 500, 700, Focused },
    { -40, -10, 300, 500, Focused },
    {  40,   0, 700, 900, Focused },
    {  40, -10, 300, 500, Focused },
    {   0,   0, 500, 650, Normal },
};

// ---- PANIC: LIVELY fidgeting (fixed — the short moves are reached) ----
inline const DanceKey PANIC[] = {
    {   0,   0, 100, 120, Scared },
    {  25, -10, 120, 140, Scared, 1 },
    { -25,   0, 120, 140, Scared },
    {  25,  -8, 120, 140, Scared },
    { -25, -10, 120, 140, Scared, 1 },
    {  25,   0, 120, 140, Scared },
    { -20,  -6, 120, 140, Scared },
    {   0,   0, 450, 600, Normal },
};

// ---- NOD ("yes"): a real nod — the head goes UP (-18) then DOWN to the
//      safe maximum (+6 = pitch 99, spec 5~85°), the gaze emphasises the dive ----
inline const DanceKey NOD[] = {
    { 0,   0, 250, 300, Normal },
    { 0, -18, 350, 420, Happy },
    { 0,   6, 300, 380, Happy,  0, -0.8f },   // head down + gaze to the floor
    { 0, -18, 350, 420, Happy },
    { 0,   6, 300, 380, Happy,  0, -0.8f },
    { 0,   0, 400, 500, Normal },
};

// ---- LOOK_AROUND: combined yaw+pitch arcs (no "L" shape — §3.7) ----
inline const DanceKey LOOK_AROUND[] = {
    {   0,  0, 250, 300, Normal },
    {   4,  0,  80,  90, EMOTIONS_COUNT },            // anticipation
    { -40, -6, 500, 800, Curious },
    { -40,  0, 250, 500, Curious, 1 },
    {  40, -6, 650, 900, Curious },
    {  40,  0, 250, 500, Curious },
    {   0,  0, 450, 600, Normal },
};

// ---- SHAKE_NO ("no"): anticipation + brisk cadence ----
inline const DanceKey SHAKE_NO[] = {
    {  -4, 0,  80,  90, Annoyed },                     // anticipation
    {  30, 0, 250, 300, Annoyed },
    { -30, 0, 250, 300, Annoyed },
    {  30, 0, 250, 300, Annoyed },
    { -30, 0, 250, 300, Annoyed },
    {   0, 0, 400, 550, Normal },
};

// ---- GREET: greeting + wink, back to Normal (GREET-1 lesson) ----
// Timing refined (user 2026-07-21): the wink only fires once the head IS up
// — rise with no wink, then a FROZEN keyframe (same target → servo stays
// still) that carries the wink for as long as it plays, then a normal
// descent.
inline const DanceKey GREET[] = {
    { 0,   0, 250, 300, Happy },
    { 0, -20, 400, 450, Happy },                       // rise (no wink)
    { 0, -20,  60, 1700, Happy, 2 },                   // UP THERE: hold + left wink
                                                       // (~1.7 s, user 07-21)
    { 0,   0, 400, 550, Normal },                      // back down
};

// ---- LAUGH: 6 crisp "ha!" beats (cadence validated on HW) ----
inline const DanceKey LAUGH[] = {
    { 0, -8, 110, 130, Glee, 1 },
    { 0,  0, 100, 120, Glee },
    { 0, -8, 110, 130, Glee },
    { 0,  0, 100, 120, Glee },
    { 0, -8, 110, 130, Glee },
    { 0,  0, 100, 120, Glee },
    { 0, -8, 110, 130, Glee },
    { 0,  0, 100, 120, Glee },
    { 0, -8, 110, 130, Glee, 1 },
    { 0,  0, 100, 120, Glee },
    { 0, -8, 110, 130, Glee },
    { 0,  0, 400, 550, Normal },
};

// ---- THINKING: held upper-left gaze ----
inline const DanceKey THINKING[] = {
    {   0,   0, 250, 300, Normal },
    { -15, -15, 450, 1800, Focused },
    {   0,   0, 450, 600, Normal },
};

// ---- CRY: deep sorrow (user 2026-07-16, 2nd pass) — the head DROPS to the
//      end stop, SNIFFLES TWICE (a sharp hiccup upwards, then falls back),
//      then STAYS DOWN for a while, dejected, before pulling itself together ----
inline const DanceKey CRY[] = {
    {  0,  0, 250, 300, Normal },
    {  0,  6, 700, 900, Sad, 0, -0.9f },   // the head drops, eyes to the floor
    // sniffle 1 (sharp hiccup, teary blink on the way back down)
    {  0,  1, 110, 130, Sad },
    {  0,  6, 160, 650, Sad, 1, -0.9f },
    // sniffle 2
    {  0,  1, 110, 130, Sad },
    {  0,  6, 160, 220, Sad, 0, -0.9f },
    // stays down for a long while, dejected
    {  0,  6, 100, 3000, Sad, 1, -0.9f },
    // slowly pulls itself together
    {  0,  0, 600, 750, Normal },
};

// ---- SHY: looks away + HEAD DOWN (end stop) + eyes to the floor + blink ----
inline const DanceKey SHY[] = {
    {  0,  0, 300, 400, Normal },
    { 20,  6, 500, 650, Worried, 0, -1.0f },  // turns away AND lowers the head
    { 20,  6, 150, 750, Worried, 1, -1.0f },
    {  0,  0, 450, 550, Normal },
};

// ---- SHOCKED: head snapped up ALL AT ONCE + Surprised + blinks, held ~3 s,
// soft return. Named "shocked" so it is not confused with the Surprised
// EMOTION (which can still be triggered on its own via /api/emotion).
inline const DanceKey SHOCKED[] = {
    { 0,   0,  60,  70, Surprised },                   // the expression snaps in
    { 0, -14, 140, 900, EMOTIONS_COUNT, 1 },           // sharp recoil + blink
    { 0, -14, 100, 800, EMOTIONS_COUNT, 1 },           // double blink, held up
    { 0, -12, 100, 1300, EMOTIONS_COUNT },             // frozen hold
    { 0,   0, 450, 650, EMOTIONS_COUNT, 1 },           // back down + blink
    { 0,   0, 250, 350, Normal },
};

// ---- WIGGLE: joyful wiggling — the Cozmo signature (small fast yaw
// oscillations, short amplitude, metronomic cadence) ----
inline const DanceKey WIGGLE[] = {
    {  0, 0,  80,  90, Glee },
    {  8, 0, 100, 110, EMOTIONS_COUNT },
    { -8, 0, 100, 110, EMOTIONS_COUNT },
    {  8, 0, 100, 110, EMOTIONS_COUNT },
    { -8, 0, 100, 110, EMOTIONS_COUNT, 2 },            // wink along the way
    {  8, 0, 100, 110, EMOTIONS_COUNT },
    { -8, 0, 100, 110, EMOTIONS_COUNT },
    {  0, 0, 300, 400, Normal },
};

// ---- PEEK: the furtive Cozmo glance — slides slowly to the side, freezes
// there (suspicious), then re-centres SHARPLY + blink (the slow/fast
// contrast is what makes the gag) ----
inline const DanceKey PEEK[] = {
    {  0, 0, 200, 250, Suspicious },
    { 30, 0, 700, 1600, EMOTIONS_COUNT },              // slide + freeze
    { 30, 0, 100, 700, EMOTIONS_COUNT },               // micro-hold
    {  0, 0, 150, 250, Normal, 1 },                    // sharp re-centre + blink
};

// ---- FURIOUS: cartoon pressure cooker (user 2026-07-12) — nervous spasms
// intensifying while the head climbs notch by notch, an automatic LED
// crescendo through the intensity chain (Annoyed 0.35 → Frustrated 0.7 →
// Angry 0.9 → Furious 1.0, a pulse at every step), a pause at the top, the
// EXPLOSION (full-amplitude spasms) then a deflating fall ----
inline const DanceKey FURIOUS_D[] = {
    {  0,   0, 150, 400, Annoyed },              // the pressure builds up
    {  3,  -2, 100, 120, EMOTIONS_COUNT },       // first tight twitches
    { -3,  -2, 100, 120, EMOTIONS_COUNT },
    {  3,  -4, 100, 120, Frustrated },           // LED step 0.7
    { -4,  -6, 100, 120, EMOTIONS_COUNT },
    {  4,  -8, 100, 120, EMOTIONS_COUNT },
    { -5, -10,  90, 110, Angry },                // LED step 0.9, rising
    {  5, -12,  90, 110, EMOTIONS_COUNT },
    { -6, -14,  90, 110, EMOTIONS_COUNT },
    {  6, -16,  90, 110, EMOTIONS_COUNT },
    { -7, -18,  80, 100, Furious, 1 },           // LED step 1.0
    {  7, -20,  80, 100, EMOTIONS_COUNT },
    { -8, -22,  80, 100, EMOTIONS_COUNT },
    {  8, -22,  80, 420, EMOTIONS_COUNT },       // pause: the kettle whistles...
    { -20, -24,  90, 110, EMOTIONS_COUNT },      // EXPLOSION
    {  20, -24,  90, 110, EMOTIONS_COUNT, 1 },
    { -20, -24,  90, 110, EMOTIONS_COUNT },
    {  20, -24,  90, 110, EMOTIONS_COUNT },
    {   0,  -6, 500, 900, EMOTIONS_COUNT, 1 },   // falls back, out of breath
    {   0,   0, 500, 700, Normal },              // soft exit
};

// ------------------------------------------------------------------
// name → sequence table (API POST /api/dance?name=...).
// mirrorable: the Brain may flip the sign of the yawOff values (a 50/50 draw
// at launch) — an unpredictable starting direction for symmetric dances.
// ------------------------------------------------------------------
struct Entry {
    const char*     name;
    const DanceKey* keys;
    int             count;
    bool            mirrorable;
};

inline const Entry* table() {
    static const Entry T[] = {
        { "happy",      HAPPY,       (int)(sizeof(HAPPY)       / sizeof(DanceKey)), true  },
        { "robot",      ROBOT,       (int)(sizeof(ROBOT)       / sizeof(DanceKey)), true  },
        { "panic",      PANIC,       (int)(sizeof(PANIC)       / sizeof(DanceKey)), true  },
        { "nod",        NOD,         (int)(sizeof(NOD)         / sizeof(DanceKey)), false },
        { "lookAround", LOOK_AROUND, (int)(sizeof(LOOK_AROUND) / sizeof(DanceKey)), true  },
        { "shakeNo",    SHAKE_NO,    (int)(sizeof(SHAKE_NO)    / sizeof(DanceKey)), true  },
        { "greet",      GREET,       (int)(sizeof(GREET)       / sizeof(DanceKey)), false },
        { "laugh",      LAUGH,       (int)(sizeof(LAUGH)       / sizeof(DanceKey)), false },
        { "thinking",   THINKING,    (int)(sizeof(THINKING)    / sizeof(DanceKey)), true  },
        { "shy",        SHY,         (int)(sizeof(SHY)         / sizeof(DanceKey)), true  },
        { "cry",        CRY,         (int)(sizeof(CRY)         / sizeof(DanceKey)), true  },
        { "shocked",    SHOCKED,     (int)(sizeof(SHOCKED)     / sizeof(DanceKey)), false },
        { "wiggle",     WIGGLE,      (int)(sizeof(WIGGLE)      / sizeof(DanceKey)), true  },
        { "peek",       PEEK,        (int)(sizeof(PEEK)        / sizeof(DanceKey)), true  },
        { "furious",    FURIOUS_D,   (int)(sizeof(FURIOUS_D)   / sizeof(DanceKey)), true  },
        { nullptr, nullptr, 0, false },
    };
    return T;
}

// Number of dances (random swipe-up pick, API bounds checking)
inline int count() {
    static int n = [] { int c = 0; while (table()[c].name) c++; return c; }();
    return n;
}

// Index by name (case-insensitive) — -1 if unknown
inline int indexOf(const char* name) {
    if (!name) return -1;
    const Entry* t = table();
    for (int i = 0; t[i].name; i++) {
        const char* n = t[i].name; int j = 0;
        while (n[j] && name[j] && (n[j] | 0x20) == (name[j] | 0x20)) j++;
        if (n[j] == '\0' && name[j] == '\0') return i;
    }
    return -1;
}

} // namespace dances
} // namespace sce
