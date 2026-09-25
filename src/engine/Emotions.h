#pragma once
// =============================================================================
// Emotions.h — StackChan-Companion (engine)
// =============================================================================
// The 30 emotions + their associated color palette. Freed from every
// dependency on the m5avatar mappings (ROADMAP §3.7/3.10).
//
// eEmotions stays a PLAIN enum (not enum class): it is used as an array index
// (roulette weights, blink policies) and serialised in the REST API.
//
// Colors: RGB888 uint32_t everywhere (CONVENTIONS §2 convention), the
// conversion to the canvas depth is done by M5GFX at draw time.
// The global darkening ("one shade darker") is applied through
// dimRgb888(emotionToRgb(e), tuning.eye_color_dim) — the table itself keeps
// the original values.
//
// PURITY: no Arduino dependency → testable natively.
// =============================================================================

#include <cstdint>

namespace sce {

// ------------------------------------------------------------------
// Main enum — 18 esp32-eyes values (order preserved) + 9 extensions
// ------------------------------------------------------------------
enum eEmotions {
    // ---- 18 esp32-eyes originals ----
    Normal       = 0,
    Angry        = 1,
    Glee         = 2,
    Happy        = 3,
    Sad          = 4,
    Worried      = 5,
    Focused      = 6,
    Annoyed      = 7,
    Surprised    = 8,
    Skeptic      = 9,
    Frustrated   = 10,
    Unimpressed  = 11,
    Sleepy       = 12,
    Suspicious   = 13,
    Nervous      = 14,   // ex-Squint (renamed 2026-07-16 — nervous squinting)
    Furious      = 15,
    Scared       = 16,
    Awe          = 17,
    // ---- 9 extensions ----
    Excited      = 18,
    Questioning  = 19,
    Frozen       = 20,
    Scary        = 21,
    Curious      = 22,
    Doubt        = 23,
    Contempt     = 24,
    Disgust      = 25,
    Smug         = 26,
    Dead         = 27,
    // ---- extensions T7 ----
    Blush        = 28,   // fond embarrassment: Happy eyes + pink cheeks (overlay)
    Squint       = 29,   // NEW 2026-07-16: focused squinting (Focused base,
                         // low slope, reduced height)
    // ----
    EMOTIONS_COUNT = 30
};

// ------------------------------------------------------------------
// Canonical names (REST API, status bar, logs) — enum order.
// Single source (avoids any table duplicated between main.cpp and WebServer).
// ------------------------------------------------------------------
inline const char* emotionName(eEmotions e) {
    static const char* NAMES[EMOTIONS_COUNT] = {
        "Normal", "Angry", "Glee", "Happy", "Sad", "Worried", "Focused",
        "Annoyed", "Surprised", "Skeptic", "Frustrated", "Unimpressed",
        "Sleepy", "Suspicious", "Nervous", "Furious", "Scared", "Awe",
        "Excited", "Questioning", "Frozen", "Scary", "Curious", "Doubt",
        "Contempt", "Disgust", "Smug", "Dead", "Blush", "Squint",
    };
    return (e >= 0 && e < EMOTIONS_COUNT) ? NAMES[e] : "?";
}

// Name → enum (case-insensitive). Returns EMOTIONS_COUNT if unknown.
inline eEmotions emotionFromName(const char* name) {
    if (!name) return EMOTIONS_COUNT;
    for (int i = 0; i < EMOTIONS_COUNT; i++) {
        const char* n = emotionName((eEmotions)i);
        int j = 0;
        while (n[j] && name[j] &&
               (n[j] | 0x20) == (name[j] | 0x20)) j++;   // tolower ASCII
        if (n[j] == '\0' && name[j] == '\0') return (eEmotions)i;
    }
    return EMOTIONS_COUNT;
}

// ------------------------------------------------------------------
// THE PALETTE IS A SYSTEM, NOT THIRTY CHOICES (2026-08-03, user reference:
// `docs/assets/insideout - colors.png` and `insideout - overlap.png`).
//
// Reference: Disney/Pixar's INSIDE OUT. Not for the look of the characters —
// for the fact that the films already solved the problem this table has. They
// give a small set of BASE feelings, each with one unmistakable hue, and a
// published chart of what every PAIR of them adds up to, with a name for each
// blend. That is exactly the shape of our 30 emotions: a handful of families
// and a long tail of things that are two feelings at once.
//
// THE CHART USED IS `insideout - combination.png`, not `insideout - overlap.png`
// (the Vox one), and the difference decides the design: the Vox grid is
// SYMMETRIC, so it says ten things in twenty cells. The other is DIRECTIONAL —
// the row is the feeling that leads, the column the one underneath — so
// Joy+Disgust is "disdain" when joy leads and "ironic" when disgust does. That
// is precisely what the `w` weight below expresses, and the symmetric chart
// would have wasted it. It also names the cells this robot actually needs:
// "contempt" (Sadness over Disgust), "excitement", "terror", "hostility",
// "betrayal". Both images are kept: the symmetric one is still the clearer
// picture of WHY two feelings mix into a third.
//
// Before this, the table was twelve hand-picked hex values grouped by
// intuition, and the grouping showed: Happy, Glee, Excited and Smug were the
// SAME yellow, so four very different faces lit the LEDs identically, while
// Surprised and Awe were plain white — the absence of a colour rather than one.
//
// The nine bases are the films' own. The blends are computed, so a pair that
// looks wrong is fixed by changing ONE base and every blend follows.
//
// THE WEIGHTS WERE SEARCHED, NOT PICKED. The chart says which two feelings and
// which one leads; anything inside that is free, so the free part was chosen by
// maximising the SMALLEST distance between any two of the thirty colours, under
// the constraint that none falls below 3:1 against black. Tuning them by hand
// does not converge — every fix pushes a colour into its neighbour, and the
// crowded neighbourhoods (six reds, six blue-violets) are exactly where the eye
// needs the separation. The search settles at a minimum separation of ~34 in
// RGB, and that is a CEILING, not laziness: thirty colours in a finite gamut,
// half of them lifted towards white to survive a black background, cannot be
// spread further. Two emotions never appear at the same instant anyway — the
// comparison the user makes is over time, not side by side.
//
// Both properties are locked by `test/test_emocolor`. If a base moves, run it:
// it names the pair that collided and the one that went dark.
// (Colours are not protected expression; what is borrowed here is a colour
// system, and the reference is credited so the choice is traceable.)
// ------------------------------------------------------------------
namespace insideout {
// Inside Out (2015)
constexpr uint32_t JOY       = 0xFFD21E;   // gold
constexpr uint32_t SADNESS   = 0x2E7BD6;   // blue
constexpr uint32_t ANGER     = 0xE0271C;   // red
constexpr uint32_t FEAR      = 0x9557CC;   // violet
constexpr uint32_t DISGUST   = 0x4FBE2A;   // green
// Inside Out 2 (2024)
constexpr uint32_t ANXIETY   = 0xF07B1E;   // orange
constexpr uint32_t ENVY      = 0x14A79D;   // teal
constexpr uint32_t EMBARRASS = 0xEE3D8F;   // pink
// Ennui's indigo is LIGHTENED from the film's own (0x6C63C4), and the gate is
// what said so: dimmed by the default eye_color_dim it measured 2.99:1 against
// black, just under the 3:1 a filled shape needs. Which is the SECOND time this
// exact emotion has had to be lightened for this exact reason — the old palette
// carried the note "a darker violet becomes unreadable once dimmed: you cannot
// see the eyelids close". Sleepy closes its eyes; a colour that dies in the
// dark takes the animation with it. Now it is a test rather than a memory.
constexpr uint32_t ENNUI     = 0x8076DE;   // indigo (film 0x6C63C4, lightened)
}  // namespace insideout

// The ROBOT'S OWN colour, and it stays. Inside Out has no neutral — there is no
// character for "nothing in particular" — while this robot spends most of its
// life there, and the esp32-eyes cyan is the identity every photo of it shows.
// Borrowing a system is not the same as surrendering the face.
constexpr uint32_t CALM_CYAN = 0x0096C8;

// Blend two feelings the way the Vox chart overlaps two translucent discs —
// but RENORMALISED, and that is the whole difficulty.
//
// A plain 50/50 sRGB mix of two saturated hues is always DARKER than both
// parents (yellow 255 + violet 149 gives 202 on red, and the sum of two
// mid-channels lands mid). On a printed chart, over grey, that reads as a third
// colour. On our black canvas it reads as "the same emotion, dimmer" — and
// dimmer is already what `eye_color_dim` means, so the two would be confused.
// So the mix is rescaled until its brightest channel matches the brightest of
// the two parents: the HUE comes from the blend, the ENERGY is not lost on the
// way. Clamped, because a rescale can overflow a channel.
//
// `w` is how far towards `b`: 0.5 = the chart's own overlap, and the weights
// used below say WHICH of the two feelings leads when the pair is not equal.
inline uint32_t blendRgb888(uint32_t a, uint32_t b, float w = 0.5f) {
    auto ch   = [](uint32_t c, int s) { return (float)((c >> s) & 0xFF); };
    auto hi   = [&](uint32_t c) { float m = ch(c,16); if (ch(c,8) > m) m = ch(c,8);
                                  if (ch(c,0) > m) m = ch(c,0); return m; };
    auto lo   = [&](uint32_t c) { float m = ch(c,16); if (ch(c,8) < m) m = ch(c,8);
                                  if (ch(c,0) < m) m = ch(c,0); return m; };
    // Saturation the cheap way: how far the darkest channel sits below the
    // brightest, as a fraction of the brightest. No HSV, no trigonometry.
    auto sat  = [&](uint32_t c) { float m = hi(c); return m > 0 ? (m - lo(c)) / m : 0.0f; };

    float r = ch(a,16) + (ch(b,16) - ch(a,16)) * w;
    float g = ch(a, 8) + (ch(b, 8) - ch(a, 8)) * w;
    float bl = ch(a, 0) + (ch(b, 0) - ch(a, 0)) * w;

    // 1. RESTORE THE SATURATION. Mixing two saturated hues in sRGB does not
    //    only darken, it WASHES OUT: red over blue lands on a dusty mauve
    //    whose darkest channel has climbed most of the way to its brightest.
    //    Left alone, Frustrated (anger over sadness), Scary (anger over fear)
    //    and Blush all came out as three neighbouring pinks — three faces you
    //    could not tell apart by their light. The hue that came out of the mix
    //    is kept; the darkest channel is pushed back DOWN until the colour is
    //    as vivid as its parents were, weighted the same way the mix was.
    float mx = r > g ? r : g;  if (bl > mx) mx = bl;
    float mn = r < g ? r : g;  if (bl < mn) mn = bl;
    if (mx > 0.0f && mx > mn) {
        const float sMix  = (mx - mn) / mx;
        const float sWant = sat(a) + (sat(b) - sat(a)) * w;
        if (sWant > sMix) {
            const float k = sWant / sMix;      // widen the gap from the peak
            r  = mx - (mx - r)  * k;
            g  = mx - (mx - g)  * k;
            bl = mx - (mx - bl) * k;
            if (r  < 0) r  = 0;
            if (g  < 0) g  = 0;
            if (bl < 0) bl = 0;
        }
    }
    // 2. RESTORE THE LIGHT. Rescale until the brightest channel matches the
    //    brighter parent's: the HUE comes from the blend, the ENERGY is not
    //    lost on the way. Multiplicative, so step 1's saturation survives it.
    const float want = hi(a) > hi(b) ? hi(a) : hi(b);
    float got = r > g ? r : g;  if (bl > got) got = bl;
    if (got > 0.0f && got < want) {
        const float k = want / got;
        r *= k;  g *= k;  bl *= k;
    }
    auto clamp8 = [](float v) -> uint32_t {
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        return (uint32_t)(v + 0.5f);
    };
    return (clamp8(r) << 16) | (clamp8(g) << 8) | clamp8(bl);
}

// Push a base towards white — the chart's "×2" cells (Ecstasy, Rage, Terror,
// Despair): the same feeling at a higher intensity, which on a black screen
// reads as more light rather than more hue.
// A PLAIN LERP TOWARDS WHITE, and it has to be plain (review 08-03). Routed
// through blendRgb888 it inherited step 2, which rescales the result until its
// brightest channel matches the BRIGHTER PARENT — and the brighter parent here
// is 0xFFFFFF, so every output came back with a 255 channel whatever `t` said.
// `intensify(FEAR, 0.0)` returned 0xBA6DFF instead of FEAR, and 0.18 / 0.20 /
// 0.80 were indistinguishable in luminance: the three call sites whose comments
// present `t` as the measured lift needed to clear 3:1 were all pinned at
// maximum, and turning a weight DOWN to recover separation — the stated purpose
// of the documented search — changed the hue wash and never the light. The knob
// the comments described did not exist.
// `t = 0` is now the identity, and that is the property to test.
// NOT folded into lerpRgb888(rgb, 0xFFFFFF, t), on purpose (review 08-04):
// that one TRUNCATES where this ROUNDS (+0.5), and the palette gates in
// test_emocolor are tuned against this arithmetic to the LSB — Dead sits two
// hundredths above its contrast floor. Harmonising the two would shift every
// near-floor colour by one step and flip a tuned gate for zero visible gain.
inline uint32_t intensify(uint32_t rgb, float t = 0.35f) {
    if (t <= 0.0f) return rgb;
    if (t >= 1.0f) return 0xFFFFFFu;
    auto ch = [&](int sh) {
        const int v = (int)((rgb >> sh) & 0xFF);
        return (uint32_t)(v + (int)((255 - v) * t + 0.5f));
    };
    return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

// The OTHER way to say "more of the same feeling", and the one Furious needed
// (user 08-03: "Furious closer to Angry - Angry but more vivid"). `intensify`
// adds white, which raises the light but WASHES the hue: a furious red drifting
// towards pink stops reading as the same emotion turned up. This drives the
// colour to the top of the gamut instead - same hue, same saturation, all the
// energy the panel can give it. Nothing is added; the volume is turned up.
inline uint32_t vivify(uint32_t rgb) {
    int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    int mx = r > g ? r : g; if (b > mx) mx = b;
    if (mx == 0 || mx == 255) return rgb;
    r = r * 255 / mx;  g = g * 255 / mx;  b = b * 255 / mx;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

// ------------------------------------------------------------------
// RGB888 color per emotion (eyes, LEDs, effects)
//
// Each blend cites the cell it comes from, written the chart's way: LEADING
// over UNDERNEATH. The weight is what "leading" means numerically — 0.35 keeps
// the first feeling clearly on top, 0.5 is the chart's own even overlap.
// Where our emotion has no cell, the derivation is stated instead of implied.
// ------------------------------------------------------------------
inline uint32_t emotionToRgb(eEmotions e) {
    using namespace insideout;
    switch (e) {
        // ---- NEUTRAL: the robot's own cyan, deliberately outside the system
        case Normal: case Focused: case Squint:
            return CALM_CYAN;

        // ---- JOY
        // ONE COLOUR FOR THE THREE JOYS (user 08-03: "the shape already tells
        // them apart"). They are the same feeling at three pitches, and the
        // faces already say which - a plain smile, a squeezed grin, wide-open
        // eyes with a star. Spending three slots of a crowded gamut on a
        // distinction the geometry makes better is what a palette should not do.
        // Note this REVERSES the finding that opened this rewrite: four yellows
        // were a bug when nothing else separated them, and are a choice now that
        // the shapes do.
        case Happy:
        case Glee:
        case Excited:     return blendRgb888(JOY, FEAR, 0.40f);     // excitement
        case Smug:        return blendRgb888(JOY, DISGUST, 0.40f);  // disdain
        // WHITE, and it STAYS white (user 08-03). Neither has a cell - the
        // films have no character for being startled - and the derivation that
        // filled the gap (joy and fear at once, lifted) produced two warm creams
        // that read as "a kind of joy". Being startled is not a kind of
        // anything: it is the absence of a settled feeling, and white is the one
        // entry here carrying no hue that could be mistaken for one.
        case Surprised:
        case Awe:         return 0xFFFFFF;

        // ---- SADNESS
        case Sad:         return SADNESS;
        // "dread" and "anxiety" are the same two feelings the two ways round,
        // and both land in the deep blue-violet that carries the LEAST light of
        // any hue — the gate measured them at 2.66:1 and 2.34:1 on black, well
        // under the 3:1 a filled shape needs. So they are the two cells that
        // have to be lifted towards white to exist at all. That is not a
        // liberty taken with the reference; it is what a black background costs.
        case Worried:     return intensify(blendRgb888(SADNESS, FEAR, 0.25f), 0.18f);
        // Doubt leaves the fear family, and the gate is why: "dread" and
        // "anxiety" are the SAME two feelings the two ways round, so they were
        // always going to sit on top of each other, and the blue-violet band is
        // both the most crowded here and the one with the least light to spare.
        // Our Doubt is not fearful anyway — it is the raised eyebrow, nearer to
        // Skeptic than to Worried. Envy's teal (looking hard at something) over
        // Ennui's indigo (unconvinced) is that, and it is nobody else's colour.
        case Doubt:       return blendRgb888(ENVY, ENNUI, 0.30f);

        // ---- ANGER
        case Angry:       return ANGER;
        // "rage" - VIVIFIED, not whitened, so it stays plainly Angry's colour
        // with the volume up rather than drifting towards pink (user 08-03).
        case Furious:     return vivify(ANGER);                     // rage
        // Annoyance is anger that has not committed: ANGER carried most of the
        // way towards ANXIETY's orange, which is the shortest road from red to
        // "restless" without leaving red behind.
        // The comment that stood here said the OPPOSITE of the line below — it
        // ruled the ANGER-ANXIETY line out by name and described an
        // anger/disgust blend, while the code was, and is, ANGER-ANXIETY
        // (review 08-03). The two are not interchangeable: the documented
        // blendRgb888(ANGER, DISGUST, 0.33f) computes to 0xE06D23, 24 away from
        // this one, i.e. under this file's own separation floor of 32. In a
        // table whose whole claim is that every colour can be re-derived from
        // its stated reason, an entry that cannot is worse than an ugly hue.
        case Annoyed:     return blendRgb888(ANGER, ANXIETY, 0.60f);
        case Frustrated:  return blendRgb888(ANGER, SADNESS, 0.50f); // betrayal
        case Scary:       return blendRgb888(FEAR, ANGER, 0.30f);    // hostility

        // ---- FEAR
        case Scared:      return FEAR;
        case Frozen:      return intensify(FEAR, 0.20f);            // terror
        // Nervous is anxiety WITH the fear under it — and it has to be, because
        // pure Anxiety orange sits between red and yellow and squeezed Annoyed
        // out of every weight it could have taken. Which is the honest reading
        // anyway: this face is a nervous squint, not free-floating worry.
        case Nervous:     return blendRgb888(ANXIETY, FEAR, 0.50f);

        // ---- DISGUST
        case Disgust:     return DISGUST;
        case Contempt:    return blendRgb888(SADNESS, DISGUST, 0.55f); // contempt
                                            // — the chart names this one exactly
        // "repulsed" is fear over disgust, and at an even weight it collapsed
        // onto Contempt's teal: violet and green are opposite enough that the
        // mix falls to whichever channel happens to lead. Fear is pushed well
        // forward so the violet survives — two faces that mean different things
        // must not light the LEDs the same colour, which is the whole failure
        // this palette replaced.
        // ...and lifted for the same reason as dread and anxiety: violet
        // carries little light, and the gate measured this one at 2.63:1.
        case Suspicious:  return intensify(blendRgb888(FEAR, DISGUST, 0.28f), 0.45f);
        //                ^ 0.20 before 08-03, and the number meant nothing:
        //                intensify pinned every result at maximum luminance
        //                whatever was written. With the weight made real, this
        //                one had to be re-found against the gate - 0.30 and
        //                0.38 collide with Frozen, 0.45 clears it.
        case Skeptic:     return blendRgb888(DISGUST, JOY, 0.22f);     // ironic
        case Unimpressed: return blendRgb888(ENNUI, DISGUST, 0.58f);

        // ---- CURIOSITY. No cell either, and it is not a blend of the five:
        //      wanting to know is not two feelings at once. It is built from
        //      Envy's teal — the IO2 colour for looking hard at something you
        //      have not got — warmed towards Joy, which lands on the green-cyan
        //      these two faces have always worn.
        case Curious:     return blendRgb888(ENVY, JOY, 0.22f);
        case Questioning: return blendRgb888(ENVY, JOY, 0.40f);

        // ---- LOW ENERGY
        case Sleepy:      return ENNUI;

        // ---- EMBARRASSMENT (T7 overlay: pink cheeks)
        case Blush:       return EMBARRASS;

        // ---- KO. A DARK GREY, and deliberately the dimmest entry in the table
        //      (user 08-03, after a violet was tried first). Grey is the point:
        //      the other twenty-nine all carry a hue, so the ONE face that is
        //      not a feeling is the one with no hue at all — it cannot be
        //      mistaken for a quiet version of anything. That also buys an
        //      enormous margin on the spacing gate (111 from its nearest
        //      neighbour, against ~34 for the crowded pairs): a neutral is far
        //      from everything by construction, so the only constraint that
        //      binds here is the contrast one.
        //      It is the one face allowed to be hard to see. Every other colour
        //      is lifted until it holds 3:1 against black; this one is let
        //      through at 2.3:1 by a DECLARED exemption in test_emocolor rather
        //      than by lowering the bar for everyone — a light "dead" would
        //      contradict itself. Walked down in steps of 2 against the gate:
        //      0x585858 lands on 2.22, too close to the floor to survive a
        //      rounding change, so 0x5A5A5A at 2.30 is taken. The margin belongs
        //      in the colour, not in a floor bent to fit it.
        case Dead:        return 0x5A5A5A;

        default:          return CALM_CYAN;
    }
}

// ------------------------------------------------------------------
// Global darkening of an RGB888 color (tuning.eye_color_dim).
// factor ∈ [0, 1]: 1.0 = original color, 0.80 = default.
// ------------------------------------------------------------------
inline uint32_t dimRgb888(uint32_t rgb, float factor) {
    if (factor >= 1.0f) return rgb;
    if (factor <= 0.0f) return 0;
    uint32_t r = (uint32_t)(((rgb >> 16) & 0xFF) * factor);
    uint32_t g = (uint32_t)(((rgb >>  8) & 0xFF) * factor);
    uint32_t b = (uint32_t)(( rgb        & 0xFF) * factor);
    return (r << 16) | (g << 8) | b;
}

// ------------------------------------------------------------------
// Linear interpolation between two RGB888 colors (emotion transitions)
// ------------------------------------------------------------------
inline uint32_t lerpRgb888(uint32_t a, uint32_t b, float t) {
    auto l = [](uint8_t x, uint8_t y, float t) -> uint8_t {
        return (uint8_t)(x + (y - x) * t);
    };
    return ((uint32_t)l((a >> 16) & 0xFF, (b >> 16) & 0xFF, t) << 16)
         | ((uint32_t)l((a >>  8) & 0xFF, (b >>  8) & 0xFF, t) <<  8)
         |  (uint32_t)l( a        & 0xFF,  b        & 0xFF, t);
}

} // namespace sce
