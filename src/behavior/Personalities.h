#pragma once
// =============================================================================
// Personalities.h — StackChan-Companion (behavior) — PURE, tested natively
// =============================================================================
// WHAT A PERSONALITY IS. Not a theme: a set of dispositions. It owns its RULES
// FILE, its roulette (cadence + weights), its identity colour and the console
// skin that goes with it. It owns NOTHING else — and that boundary is the whole
// design, so it is stated as a rule rather than left to be inferred:
//
//     A personality may change WHAT THE ROBOT FEELS and HOW LONG IT SHOWS IT.
//     It may NEVER change WHAT THE HARDWARE TOLERATES.
//
// Servo end stops, the frame budget, bus timings and the touch vocabulary are
// therefore absent from this file by construction, not by omission. A
// "personality" able to park a servo against its stop would be a damage
// mechanism wearing a costume; `test_personalities` asserts it cannot.
//
// WHY A FILE PER PERSONALITY (rules). The rule engine holds 24 rules, 3 of them
// built-in, and refuses further ones SILENTLY. Gating rules on the active
// personality would make every personality share those 21 slots. Giving each one
// its own file means only one is ever loaded, so each gets the full 21 — and the
// rules need no personality gate at all, because THE FILE IS THE GATE.
//
// THE COMPILED TABLE IS THE DEFAULT, THE CARD IS AN OVERLAY. `defaults()` below
// is what the firmware ships with; `PersonalityStore` may then overwrite any
// field from `/stackchan-companion/personalities/<name>.yaml`. A key absent from the
// file keeps the compiled value — the file states DIFFERENCES, never a complete
// declaration, so a half-written file degrades to "mostly the default" instead
// of to a personality with holes in it.
//
// PERSONALITY 0 MUST WORK WITH NO FILE AT ALL. That is the acceptance criterion
// of the whole feature, not an aspiration: a card with no `personalities/`
// directory behaves exactly as the firmware did before any of this existed.
// `hasWeights == false` is how index 0 says "leave EmotionRoulette's own table
// alone" rather than restating it here — a copy would be a second source for one
// set of numbers, and the two would drift.
// =============================================================================

#include <stdint.h>
#include <string.h>
#include "../engine/Emotions.h"
#include "../engine/Units.h"   // clampVal — the bounds below share it with everything else

namespace sce {

// Console skins are COMPILED and named, never colours from the card. A theme
// authored in a file cannot be contrast-checked at build time, and an
// unreadable console is the one thing you cannot fix from the console. The
// personality picks one by name; the palettes live in the firmware where
// `check-contrast.py` can measure them.
enum class ConsoleTheme : uint8_t { Default = 0, Gundam = 1, COUNT };

// How many characters can exist at once. Eight is not a guess about taste: the
// table is held in RAM so the card can edit it, and this bounds that cost
// (~200 B each) the way MAX_RULES bounds the rule table.
static constexpr int MAX_PERSONALITIES = 8;

// Lengths sized to what they hold, not to a round number: a name is an id shown
// in a `<select>`, a rules path is `/stackchan-companion/rules.<name>.txt`.
static constexpr int PERSO_NAME_MAX  = 16;
static constexpr int PERSO_PATH_MAX  = 48;

struct Personality {
    char         name[PERSO_NAME_MAX];
    char         rulesFile[PERSO_PATH_MAX];
    uint32_t     eyeRgb;        // 0 = keep the emotion palette
    ConsoleTheme theme;
    bool         roulette;      // the roulette's default state for this character
    uint32_t     minMs, maxMs;  // draw interval
    // Sparse by convention: an emotion left at 0 is never drawn. That is what
    // makes a narrow character short to declare — and narrowness is the point,
    // since a face that can wear thirty expressions at random tells you nothing.
    float        weights[EMOTIONS_COUNT];
    bool         hasWeights;    // false = do not touch EmotionRoulette's table

    // ---- FEEL, not a per-emotion table -------------------------------------
    // `transitionFor()`/`pitchBiasFor()` (EmotionRoulette.h, Brain.h) hold
    // roughly a hundred hardcoded numbers between them — per-emotion transition
    // timing and head posture. Making EVERY one of those personality-editable
    // was the plan once; it is not this. A character that rests in six
    // expressions and one that rests in seventeen already feel different
    // WITHOUT touching any of those numbers (§Personalities: colour, weights,
    // rules already carry a Haro). What a hundred more file-editable values
    // would buy past that is a character that moves a little faster or holds
    // its head a little higher — real, but not worth the surface: 30 emotions
    // × 4 transition fields is a diff nobody reviews, and a YAML key for each
    // is a YAML key nobody reads either.
    //
    // A SCALE instead: one number bends EVERY transition toward snappier or
    // dreamier, one number bends every posture toward more or less expressive,
    // without restating the table pitchBiasFor()/transitionFor() already are.
    // Multiplying a table by two numbers is not a personality system pretending
    // to be simpler than it is — it is the two dials that table can actually
    // TAKE without becoming the table it started as.
    float        transitionScale;   // 1.0 = unscaled; <1 snappier, >1 dreamier
    float        pitchBiasScale;    // 1.0 = unscaled; 0 = stoic (no head bias)

    void clearWeights() {
        for (int i = 0; i < EMOTIONS_COUNT; i++) weights[i] = 0.0f;
        hasWeights = false;
    }
    void setWeight(eEmotions e, float w) {
        if (e >= EMOTIONS_COUNT) return;
        weights[e] = w < 0.0f ? 0.0f : w;
        hasWeights = true;
    }
};

// ---- THE FEEL DIALS' MATH, PURE — Brain.h calls these, never inlines the
// clamp itself. `Brain` is not natively testable at all (it opens a FreeRTOS
// queue and pins a task in its constructor path, so no native test can even
// construct one) — without this extraction, the one place these bounds are
// enforced would also be the one place nothing could ever prove they were.
// Bounds match `Brain::setTransitionScale`/`setPitchBiasScale` exactly because
// they ARE that code now, not a second copy of it kept in step by hand.
inline float clampTransitionScale(float s) {
    // Below 0.3 a transition is close enough to a cut that EASE_IN_OUT and
    // SPRING stop reading as different METHODS — the whole reason this is a
    // scale and not a switch. Above 3 a blink-length change starts taking a
    // full second, which reads as the robot lagging, not as a mood.
    return clampVal(s, 0.3f, 3.0f);
}
inline float clampPitchBiasScale(float s) {
    // 0 is a real, INTENDED value — a stoic character whose head never tilts
    // with its mood — so the floor is zero, not "close to one". The servo's
    // own clamp (ServoMotion::moveTo, A2.17) protects the hardware regardless;
    // this ceiling is about taste, not about the envelope.
    return clampVal(s, 0.0f, 2.0f);
}
// The exact computation `Brain::scaledTransition` applies to a compiled
// duration. 40 ms floor: below that a transition and a cut are the same frame
// on a 33 ms budget. 3000 ms ceiling: independent of the scale bound above,
// because a personality near the top of ITS range stacked on an already-long
// compiled duration (Sleepy's SOFT 400 ms, say) must still land somewhere a
// user calls "dreamy" rather than "frozen".
inline uint32_t scaledDurationMs(uint32_t baseMs, float scale) {
    return (uint32_t)clampVal((float)baseMs * clampTransitionScale(scale),
                              40.0f, 3000.0f);
}

namespace personalities {

// ---- THE COMPILED DEFAULTS -------------------------------------------------
// Filled rather than aggregate-initialised because `weights` is thirty floats
// and a positional initialiser would be unreadable and unmaintainable — the one
// place where writing it out long-hand is worse than a builder.
inline void fill(Personality& p, const char* name, const char* rules,
                 uint32_t rgb, ConsoleTheme th, uint32_t lo, uint32_t hi) {
    memset(&p, 0, sizeof(p));
    strncpy(p.name, name, PERSO_NAME_MAX - 1);
    strncpy(p.rulesFile, rules, PERSO_PATH_MAX - 1);
    p.eyeRgb = rgb; p.theme = th; p.roulette = true;
    p.minMs = lo; p.maxMs = hi; p.hasWeights = false;
    // memset left these at 0.0, which for a SCALE means "erase every
    // transition and every posture" — the one value a fresh character must
    // never silently carry. 1.0 here is what makes "no scale data" the same
    // thing as "unscaled", the same discipline `hasWeights` already keeps for
    // the roulette table.
    p.transitionScale = 1.0f; p.pitchBiasScale = 1.0f;
}

// Number of COMPILED characters. They may be EDITED by a card file but never
// removed: the firmware has to behave the same on a card that has been wiped.
inline constexpr int BUILTIN_COUNT = 2;

// Writes the compiled characters into a table and clears the rest. Callable
// more than once ON PURPOSE: `PersonalityStore::loadAll` resets to this and
// then re-overlays every file, which makes loading IDEMPOTENT — a reload after
// a deletion cannot leave the deleted character behind, and a reload after an
// edit cannot apply that edit twice.
inline void seedBuiltins(Personality* T) {
    // 0 — THE ROBOT THIS DOCUMENTATION DESCRIBES. No weights: it declines to
    // touch the roulette rather than restating its table (see the header).
    fill(T[0], "default", "/stackchan-companion/rules.txt",
         0u, ConsoleTheme::Default, 6000, 12000);
    // 1 — HARO. Deliberately narrow and cheerful to the point of being
    // oblivious: nothing dark is drawn at rest, so when a RULE puts Worried on
    // the face it can only have come from something that happened. That is the
    // property the default personality cannot have.
    fill(T[1], "haro", "/stackchan-companion/rules.haro.txt",
         0x22FF66u, ConsoleTheme::Gundam, 8000, 20000);
    T[1].setWeight(Normal,    1.00f);
    T[1].setWeight(Happy,     0.60f);
    T[1].setWeight(Glee,      0.35f);
    T[1].setWeight(Curious,   0.25f);
    T[1].setWeight(Surprised, 0.15f);
    T[1].setWeight(Excited,   0.10f);
    for (int i = BUILTIN_COUNT; i < MAX_PERSONALITIES; i++) {
        memset(&T[i], 0, sizeof(T[i]));
        T[i].name[0] = 0;
    }
}

// The live table. Starts as the compiled defaults; PersonalityStore overlays the
// card on top. Returned by reference so the store can write into it — there is
// ONE table, not a compiled one plus a runtime copy that could disagree.
inline Personality* table() {
    static Personality T[MAX_PERSONALITIES];
    static bool seeded = false;
    if (!seeded) { seeded = true; seedBuiltins(T); }
    return T;
}

// Back to the firmware's own characters, card forgotten. The loader calls this
// before re-reading the directory.
inline void resetToBuiltins() { seedBuiltins(table()); }

// How many slots are OCCUPIED. A name is what makes a slot real: the store
// writes `name[0] = '\0'` to delete one, so this is also what shrinks after a
// deletion without any second counter to keep in step.
inline int count() {
    Personality* t = table();
    int n = 0;
    while (n < MAX_PERSONALITIES && t[n].name[0]) n++;
    return n;
}

// Bounded accessor. An unknown index yields personality 0 rather than nothing:
// the robot always has a character, and a caller that asked for a bad one gets
// the default robot instead of an empty one. main.cpp SANITISES the tuning key
// on top of this (unknown -> 0, re-persisted) the way `band_mode` already is —
// this is the second line of defence, not the first.
inline Personality& at(int i) {
    Personality* t = table();
    return (i >= 0 && i < count()) ? t[i] : t[0];
}

// RAW slot access, for the loader and the editor ONLY. `at()` above is a
// READING accessor: it clamps an unknown index to slot 0 so a caller always
// gets a working robot. That clamp is exactly wrong for WRITING — it silently
// redirects a write meant for an empty slot onto personality 0, which is how
// the first version of the card loader overwrote the default character and then
// erased it (found on target, 09-14: `personality=2` read back 0 and the robot
// drew personality 0's emotions). Bounded by the TABLE, never by `count()`,
// because filling slot N is precisely how count() grows.
inline Personality* slot(int i) {
    return (i >= 0 && i < MAX_PERSONALITIES) ? &table()[i] : nullptr;
}

// Index by name (case-sensitive — these are ids, not prose). -1 if unknown.
inline int indexOf(const char* name) {
    if (!name || !name[0]) return -1;
    Personality* t = table();
    const int n = count();
    for (int i = 0; i < n; i++)
        if (strcmp(t[i].name, name) == 0) return i;
    return -1;
}

// PERSONALITY 0 IS NOT DELETABLE, and that is load-bearing rather than polite:
// `at()` falls back to it for any unknown index, and main.cpp falls back to its
// RULE FILE when a character's own is missing. Removing it would remove the
// floor the rest of the design stands on.
inline bool deletable(int i) { return i > 0 && i < count(); }

} // namespace personalities
} // namespace sce
