// =============================================================================
// test_emocolor - the emotion palette, and the one thing it must never do
// =============================================================================
// The palette is derived from Disney/Pixar's INSIDE OUT (2026-08-03, reference
// images in docs/assets/): nine base feelings with one unmistakable hue each,
// and blends computed from them the way the film's own chart overlaps two
// feelings into a named third one.
//
// THE REASON THIS FILE EXISTS: the eyes are drawn on BLACK. A palette derived
// by mixing is a palette where any single edit propagates, and a mix of two
// saturated hues is ALWAYS darker than both parents — so the failure mode of
// this design is not an ugly colour, it is an emotion that quietly stops being
// visible. That cannot be caught by reading the table, and on target it looks
// like a dead LED or a screen problem.
//
// So every one of the 30 emotions is checked for contrast against black, at
// the DEFAULT eye_color_dim, at the WCAG AA threshold for a graphical object
// (3:1) — the same doctrine scripts/gates/check-contrast.py applies to the radar
// themes. The eyes are large filled shapes, not text, which is why 3:1 and not
// 4.5:1.
//
// Run: pio test -e native
// =============================================================================
#include <unity.h>
#include <cmath>
#include <cstdio>
#include "../../src/engine/Emotions.h"
#include "../../src/engine/Tuning.h"

using namespace sce;

void setUp(void) {}
void tearDown(void) {}

// ------------------------------------------------------------------ helpers
// WCAG relative luminance of an sRGB triple.
// 0.04045 is the sRGB-spec linearisation threshold, the SAME constant
// scripts/gates/check-contrast.py uses (review 08-04: this copy carried the old
// WCAG-errata 0.03928 — the repo's two contrast authorities disagreed at
// exactly the near-floor colours the gates are tuned against).
static double luminance(uint32_t rgb) {
    auto chan = [](int v) {
        const double s = v / 255.0;
        return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * chan((rgb >> 16) & 0xFF)
         + 0.7152 * chan((rgb >>  8) & 0xFF)
         + 0.0722 * chan( rgb        & 0xFF);
}

// Contrast against BLACK, which is the only background the eyes ever have.
static double contrastOnBlack(uint32_t rgb) {
    return (luminance(rgb) + 0.05) / 0.05;
}

// What the screen ACTUALLY receives: the table value, dimmed.
static uint32_t asDrawn(eEmotions e) {
    Tuning t;                                  // defaults, eye_color_dim = 0.80
    return dimRgb888(emotionToRgb(e), t.eye_color_dim);
}

// --------------------------------------------------------------- the gate
// EVERY emotion stays visible on black once dimmed. This is the test the whole
// file is for.
static void test_toutes_les_emotions_se_detachent_du_noir(void) {
    for (int i = 0; i < EMOTIONS_COUNT; i++) {
        const eEmotions e = (eEmotions)i;
        const double c = contrastOnBlack(asDrawn(e));
        // ONE DECLARED EXCEPTION, and it is the only face allowed to be hard to
        // see: Dead. A "dead" that shines as brightly as Happy contradicts
        // itself, so the user asked for a dark violet knowing what it costs.
        // It is exempted HERE, by name and with a floor of its own, rather than
        // by lowering the bar for the other twenty-nine — an exception that
        // widens the rule protects nothing.
        const double floorC = (e == Dead) ? 2.2 : 3.0;
        if (c < floorC) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "%s : contraste %.2f:1 sur noir, seuil %.1f (couleur %06lX)",
                     emotionName(e), c, floorC, (unsigned long)asDrawn(e));
            TEST_FAIL_MESSAGE(msg);
        }
    }
}

// Not dimmed either — a user who sets eye_color_dim to 1.0 must not be the only
// one for whom it works, and a base that only passes because of the dim would
// mean the margin sits in the wrong place.
static void test_les_neuf_bases_se_detachent_aussi_sans_attenuation(void) {
    const uint32_t BASES[] = {
        insideout::JOY,     insideout::SADNESS, insideout::ANGER,
        insideout::FEAR,    insideout::DISGUST, insideout::ANXIETY,
        insideout::ENVY,    insideout::EMBARRASS, insideout::ENNUI,
    };
    for (uint32_t b : BASES)
        TEST_ASSERT_TRUE_MESSAGE(contrastOnBlack(b) >= 3.5,
                                 "une base de reference passe sous 3.5:1");
}

// ------------------------------------------------------- the blend contract
// The chart's overlaps are symmetric: Joy over Sadness and Sadness over Joy are
// the same cell. A blend that depended on argument order would make the table
// order-sensitive, which is exactly the kind of thing that gets "fixed" by
// swapping two arguments and silently changes six emotions.
static void test_le_melange_est_symetrique(void) {
    TEST_ASSERT_EQUAL_HEX32(blendRgb888(insideout::JOY, insideout::FEAR, 0.5f),
                            blendRgb888(insideout::FEAR, insideout::JOY, 0.5f));
    TEST_ASSERT_EQUAL_HEX32(blendRgb888(insideout::ANGER, insideout::SADNESS, 0.5f),
                            blendRgb888(insideout::SADNESS, insideout::ANGER, 0.5f));
}

// A feeling blended with itself is that feeling. Sounds trivial; it is the
// property that breaks first when the renormalisation is wrong.
static void test_melanger_une_couleur_avec_elle_meme_ne_change_rien(void) {
    TEST_ASSERT_EQUAL_HEX32(insideout::JOY,
                            blendRgb888(insideout::JOY, insideout::JOY, 0.5f));
    TEST_ASSERT_EQUAL_HEX32(insideout::DISGUST,
                            blendRgb888(insideout::DISGUST, insideout::DISGUST, 0.3f));
}

// THE POINT OF THE RENORMALISATION. A plain mix of two saturated hues is darker
// than both parents; on black that reads as "the same emotion, dimmer", which
// is what eye_color_dim already means. The blend must keep the ENERGY of the
// brighter parent and take only the HUE from the mix.
static void test_un_melange_ne_perd_pas_la_lumiere_de_ses_parents(void) {
    auto peak = [](uint32_t c) {
        int m = (c >> 16) & 0xFF;
        if ((int)((c >> 8) & 0xFF) > m) m = (c >> 8) & 0xFF;
        if ((int)(c & 0xFF) > m) m = c & 0xFF;
        return m;
    };
    const uint32_t mix = blendRgb888(insideout::JOY, insideout::FEAR, 0.5f);
    const int want = peak(insideout::JOY) > peak(insideout::FEAR)
                   ? peak(insideout::JOY) : peak(insideout::FEAR);
    TEST_ASSERT_EQUAL_INT(want, peak(mix));
    // And a naive mix really would have been dimmer — otherwise this test
    // proves nothing about the mechanism.
    TEST_ASSERT_TRUE(peak(lerpRgb888(insideout::JOY, insideout::FEAR, 0.5f)) < want);
}

// `intensify` is the chart's "x2" cells (Ecstasy, Rage, Terror): the SAME
// feeling with more light behind it. It must brighten and never invert.
static void test_intensifier_eclaircit_sans_changer_de_camp(void) {
    TEST_ASSERT_TRUE(luminance(intensify(insideout::ANGER))
                     > luminance(insideout::ANGER));
    TEST_ASSERT_TRUE(luminance(intensify(insideout::JOY))
                     >= luminance(insideout::JOY));
    // Rage is still red: the red channel stays the dominant one.
    const uint32_t rage = intensify(insideout::ANGER, 0.25f);
    TEST_ASSERT_TRUE(((rage >> 16) & 0xFF) > ((rage >> 8) & 0xFF));
    TEST_ASSERT_TRUE(((rage >> 16) & 0xFF) > (rage & 0xFF));
}

// ------------------------------------------------------- the family contract
// COLOUR IS NOT THE ONLY CHANNEL, and three pairs say so out loud (user 08-03).
// The shapes carry differences the palette does not have to repeat, and a
// crowded gamut is exactly where NOT to spend a slot twice. Each exemption is
// declared with its reason, because an exemption without one is just a hole:
//
//   Happy / Glee / Excited  one amber for the three. A plain smile, a squeezed
//                           grin and wide eyes with a star are already three
//                           unmistakable faces. This deliberately REVERSES the
//                           finding that opened the rewrite - four yellows were
//                           a bug when nothing else separated them, and are a
//                           choice now that the geometry does.
//   Surprised / Awe         both white. Being startled is not a kind of joy, and
//                           white is the one entry carrying no hue to misread.
//   Angry / Furious         the same red, vivified. Fury is anger with the
//                           volume up, not a different feeling.
//
// Everything NOT in this table still has to stand apart.
struct Exempt { eEmotions a, b; };
static const Exempt EXEMPT[] = {
    { Happy, Glee }, { Happy, Excited }, { Glee, Excited },
    { Surprised, Awe },
    { Angry, Furious },
};
static bool exempted(eEmotions x, eEmotions y) {
    for (const Exempt& e : EXEMPT)
        if ((e.a == x && e.b == y) || (e.a == y && e.b == x)) return true;
    return false;
}

// The exemptions are DECLARED, so they must still be true: an entry left behind
// after a colour moves would silently license a collision nobody intended.
static void test_les_exemptions_declarees_sont_reelles(void) {
    TEST_ASSERT_EQUAL_HEX32(emotionToRgb(Happy),     emotionToRgb(Glee));
    TEST_ASSERT_EQUAL_HEX32(emotionToRgb(Happy),     emotionToRgb(Excited));
    TEST_ASSERT_EQUAL_HEX32(emotionToRgb(Surprised), emotionToRgb(Awe));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu,               emotionToRgb(Awe));
    // Furious is Angry turned up: same hue order, strictly brighter, and NOT
    // washed towards pink the way blending with white would have left it.
    const uint32_t an = emotionToRgb(Angry), fu = emotionToRgb(Furious);
    TEST_ASSERT_TRUE(((fu >> 16) & 0xFF) > ((an >> 16) & 0xFF));
    TEST_ASSERT_TRUE(((fu >> 16) & 0xFF) > ((fu >> 8) & 0xFF));
    TEST_ASSERT_EQUAL_HEX32(0xFFu, (fu >> 16) & 0xFF);   // at the top of the gamut
    TEST_ASSERT_TRUE_MESSAGE(luminance(fu) > luminance(an),
                             "Furious doit etre plus vif qu'Angry");
}

// Smug is the one joy that is NOT the shared amber: it is joy over disgust, and
// it has to stay tellable from the three that share.
static void test_smug_reste_distinct_des_trois_joies(void) {
    TEST_ASSERT_TRUE(emotionToRgb(Smug) != emotionToRgb(Happy));
}

// NO TWO FACES WEAR THE SAME LIGHT - except where the table above says so.
// This is the test that actually bites: lifting a colour towards white to make
// it visible on black also pulls it towards every OTHER colour that had to be
// lifted, so the deep blues and violets - dread, repulsion, terror, sleep - all
// drift onto the same pale periwinkle. Measured while this palette was written:
// Doubt landed on 0x8889FF and Dead on 0x8899FF, sixteen units apart on one
// channel. On the LEDs that is the same colour.
//
// The three neutrals are excluded because they are the same colour ON PURPOSE,
// and so are the declared exemptions.
static void test_aucune_emotion_ne_porte_la_meme_couleur_qu_une_autre(void) {
    for (int i = 0; i < EMOTIONS_COUNT; i++) {
        for (int j = i + 1; j < EMOTIONS_COUNT; j++) {
            // The three neutrals share ONE colour on purpose, so cyan-vs-cyan
            // is skipped - but cyan vs ANYTHING ELSE is exactly what has to be
            // measured. Both loops used to `continue` when EITHER side was
            // cyan, so no computed blend was ever compared against the colour
            // the robot displays most of its life (review 08-03). Sad sits 55
            // away from it and Doubt 70: one base tweak crosses that, and the
            // gate would have stayed green while an emotion became
            // indistinguishable from Normal.
            if (emotionToRgb((eEmotions)i) == CALM_CYAN &&
                emotionToRgb((eEmotions)j) == CALM_CYAN) continue;
            if (exempted((eEmotions)i, (eEmotions)j)) continue;
            const uint32_t a = emotionToRgb((eEmotions)i);
            const uint32_t b = emotionToRgb((eEmotions)j);
            const int dr = (int)((a >> 16) & 0xFF) - (int)((b >> 16) & 0xFF);
            const int dg = (int)((a >>  8) & 0xFF) - (int)((b >>  8) & 0xFF);
            const int db = (int)( a        & 0xFF) - (int)( b        & 0xFF);
            const int d2 = dr * dr + dg * dg + db * db;
            if (d2 < 32 * 32) {
                char msg[160];
                snprintf(msg, sizeof(msg),
                         "%s (%06lX) et %s (%06lX) : distance %d, minimum 32",
                         emotionName((eEmotions)i), (unsigned long)a,
                         emotionName((eEmotions)j), (unsigned long)b,
                         (int)(std::sqrt((double)d2) + 0.5));
                TEST_FAIL_MESSAGE(msg);
            }
        }
    }
}

// The five bases of the first film must stay far apart from each other: they are
// the anchors the whole table hangs on, and two anchors that drift together take
// their blends with them.
static void test_les_bases_restent_distinctes(void) {
    const uint32_t B[] = { insideout::JOY, insideout::SADNESS, insideout::ANGER,
                           insideout::FEAR, insideout::DISGUST };
    for (int i = 0; i < 5; i++)
        for (int j = i + 1; j < 5; j++) {
            const int dr = (int)((B[i] >> 16) & 0xFF) - (int)((B[j] >> 16) & 0xFF);
            const int dg = (int)((B[i] >>  8) & 0xFF) - (int)((B[j] >>  8) & 0xFF);
            const int db = (int)( B[i]        & 0xFF) - (int)( B[j]        & 0xFF);
            TEST_ASSERT_TRUE_MESSAGE(dr * dr + dg * dg + db * db > 100 * 100,
                                     "deux bases de reference sont trop proches");
        }
}

// The robot keeps its own colour where it has no feeling to show. Inside Out has
// no character for "nothing in particular"; this robot spends most of its life
// there, and the cyan is what every photo of it shows.
static void test_le_neutre_reste_le_cyan_du_robot(void) {
    TEST_ASSERT_EQUAL_HEX32(CALM_CYAN, emotionToRgb(Normal));
    TEST_ASSERT_EQUAL_HEX32(CALM_CYAN, emotionToRgb(Focused));
    TEST_ASSERT_EQUAL_HEX32(CALM_CYAN, emotionToRgb(Squint));
}

// Every emotion answers something: a `default` that swallowed a missing case
// would show cyan on a face that is anything but calm.
static void test_aucune_emotion_ne_tombe_dans_le_defaut_par_accident(void) {
    int cyans = 0;
    for (int i = 0; i < EMOTIONS_COUNT; i++)
        if (emotionToRgb((eEmotions)i) == CALM_CYAN) cyans++;
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, cyans,
        "exactement trois emotions sont neutres (Normal, Focused, Squint)");
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_toutes_les_emotions_se_detachent_du_noir);
    RUN_TEST(test_les_neuf_bases_se_detachent_aussi_sans_attenuation);
    RUN_TEST(test_le_melange_est_symetrique);
    RUN_TEST(test_melanger_une_couleur_avec_elle_meme_ne_change_rien);
    RUN_TEST(test_un_melange_ne_perd_pas_la_lumiere_de_ses_parents);
    RUN_TEST(test_intensifier_eclaircit_sans_changer_de_camp);
    RUN_TEST(test_les_exemptions_declarees_sont_reelles);
    RUN_TEST(test_smug_reste_distinct_des_trois_joies);
    RUN_TEST(test_aucune_emotion_ne_porte_la_meme_couleur_qu_une_autre);
    RUN_TEST(test_les_bases_restent_distinctes);
    RUN_TEST(test_le_neutre_reste_le_cyan_du_robot);
    RUN_TEST(test_aucune_emotion_ne_tombe_dans_le_defaut_par_accident);
    return UNITY_END();
}
