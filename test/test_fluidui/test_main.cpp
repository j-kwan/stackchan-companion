// =============================================================================
// test_fluidui — the table the panel and the finger must agree on
// =============================================================================
// A row drawn at one height and hit-tested at another is the classic panel
// bug: it looks right in a screenshot and the finger lands on the wrong
// setting. The defence is that both read ONE table, and these tests are what
// stops a second one from appearing.
//
// The same table also feeds the yaml and /config, so a third and a fourth list
// cannot drift either — which is the failure check-guest-config.py was written
// after: a key that loadConfig read and saveConfig did not write was DESTROYED
// on the first save from the web page.
// =============================================================================

#include <unity.h>
#include <string.h>
#include "../../firmware/led-fluid/panels.h"

using namespace sce::ui;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

// ---- every parameter is bilingual, and its default is inside its bounds ----
static void test_chaque_parametre_est_bilingue_et_borne(void) {
    TEST_ASSERT_TRUE(paramCount() > 0);
    for (int i = 0; i < paramCount(); i++) {
        const Param& p = params()[i];
        TEST_ASSERT_NOT_NULL(p.key);
        TEST_ASSERT_NOT_NULL(p.en);
        TEST_ASSERT_NOT_NULL(p.fr);
        TEST_ASSERT_TRUE(strlen(p.en) > 0);
        TEST_ASSERT_TRUE(strlen(p.fr) > 0);
        TEST_ASSERT_TRUE(p.lo <= p.def && p.def <= p.hi);
        // A key starting with '_' is reserved by SceGuest for its own form
        // fields, and addSetting() refuses it — silently, at runtime, where
        // nobody is watching. Here it is loud and free.
        TEST_ASSERT_TRUE(p.key[0] != '_');
        // A boolean spans exactly 0..1: anything else would draw a checkbox
        // that cannot represent its own range.
        if (p.kind == Kind::Bool) {
            TEST_ASSERT_EQUAL_INT(0, p.lo);
            TEST_ASSERT_EQUAL_INT(1, p.hi);
        }
    }
}

// ---- no duplicate key ------------------------------------------------------
static void test_aucune_cle_en_double(void) {
    for (int i = 0; i < paramCount(); i++)
        for (int j = i + 1; j < paramCount(); j++)
            TEST_ASSERT_TRUE(strcmp(params()[i].key, params()[j].key) != 0);
}

// ---- paramIndex finds what is there and refuses what is not ----------------
static void test_l_index_trouve_et_refuse(void) {
    TEST_ASSERT_EQUAL_INT(-1, paramIndex("pas_une_cle"));
    for (int i = 0; i < paramCount(); i++)
        TEST_ASSERT_EQUAL_INT(i, paramIndex(params()[i].key));
}

// ---- the drawing and the hit-test read the SAME rows -----------------------
static void test_le_dessin_et_le_doigt_lisent_les_memes_rangees(void) {
    for (int slot = 0; slot < ROWS_PER_TAB; slot++) {
        const Rect r = rowRect(slot);
        // The centre of a row belongs to that row, both edges included.
        TEST_ASSERT_EQUAL_INT(slot, rowAt(r.y + r.h / 2));
        TEST_ASSERT_EQUAL_INT(slot, rowAt(r.y));
        TEST_ASSERT_EQUAL_INT(slot, rowAt(r.y + r.h - 1));
        // Rows do not overlap.
        if (slot > 0) {
            const Rect prev = rowRect(slot - 1);
            TEST_ASSERT_TRUE(prev.y + prev.h <= r.y);
        }
        // And no row runs off the panel.
        TEST_ASSERT_TRUE(r.y + r.h <= SCR_H);
    }
    TEST_ASSERT_EQUAL_INT(-1, rowAt(0));              // above the first row
    TEST_ASSERT_EQUAL_INT(-1, rowAt(SCR_H - 1));      // below the last
}

// ---- slider: value and position are each other's inverse -------------------
static void test_le_curseur_fait_l_aller_retour(void) {
    for (int i = 0; i < paramCount(); i++) {
        const Param& p = params()[i];
        if (p.kind != Kind::Num) continue;
        // The slack is ONE PIXEL'S WORTH of this parameter, not one unit: hue
        // spans 360 values across a 155 px slider, so an exact round trip is
        // not a bug that can be fixed, it is a screen that does not exist.
        // Asserting the real resolution still catches the failure this test was
        // written for — sliderX truncating while valueAt rounds, which stacked
        // both errors the same way and put the knob left of its own label.
        const int slack = unitsPerPixel(p);
        for (int v = p.lo; v <= p.hi; v += (p.hi - p.lo) / 4 + 1) {
            const int back = valueAt(p, sliderX(p, v));
            TEST_ASSERT_INT_WITHIN(slack, v, back);
        }
        // The ends are exact, whatever the resolution: a slider pushed fully
        // to one side must read exactly its bound, or a parameter can never be
        // set to its own maximum.
        TEST_ASSERT_EQUAL_INT(p.lo, valueAt(p, sliderX(p, p.lo)));
        TEST_ASSERT_EQUAL_INT(p.hi, valueAt(p, sliderX(p, p.hi)));
    }
}

// ---- a finger past the ends clamps, it does not wrap -----------------------
static void test_un_doigt_hors_du_curseur_borne(void) {
    const Param& p = params()[0];
    TEST_ASSERT_EQUAL_INT(p.lo, valueAt(p, -50));
    TEST_ASSERT_EQUAL_INT(p.hi, valueAt(p, 400));
}

// ---- the hue rectangle covers the whole circle, saturation the whole range -
static void test_le_rectangle_couvre_teinte_et_saturation(void) {
    const Rect r = hueRect();
    TEST_ASSERT_TRUE(r.w > 0 && r.h > 0);
    TEST_ASSERT_TRUE(r.x >= 0 && r.x + r.w <= SCR_W);
    TEST_ASSERT_TRUE(r.y >= 0 && r.y + r.h <= SCR_H);
    TEST_ASSERT_EQUAL_INT(0, hueAt(r.x));
    TEST_ASSERT_TRUE(hueAt(r.x + r.w - 1) >= 355);
    TEST_ASSERT_TRUE(hueAt(r.x + r.w - 1) <= 359);
    // Y is inverted: the TOP of the rectangle is the saturated end, which is
    // what the picture shows and therefore what the finger must find.
    TEST_ASSERT_EQUAL_INT(100, satAt(r.y));
    TEST_ASSERT_EQUAL_INT(0,   satAt(r.y + r.h - 1));
}

// ---- the drawn tabs fit, and the undrawn one is deliberate -----------------
static void test_les_onglets_dessines_tiennent(void) {
    int perTab[3] = {0, 0, 0};
    for (int i = 0; i < paramCount(); i++) {
        const Param& p = params()[i];
        TEST_ASSERT_TRUE(p.tab <= TAB_HIDDEN);
        perTab[p.tab]++;
    }
    TEST_ASSERT_TRUE(perTab[0] > 0);
    TEST_ASSERT_TRUE(perTab[1] > 0);
    // A tab with more rows than the geometry has slots would draw off-screen
    // and hit-test onto nothing.
    TEST_ASSERT_TRUE(perTab[0] <= ROWS_PER_TAB);
    TEST_ASSERT_TRUE(perTab[1] <= ROWS_PER_TAB);
    // The hidden tab is not an accident, and its membership is NARROW: only
    // what already has another widget. Hue, saturation and brightness are the
    // colour rectangle's, and a second slider for them here would be two
    // controls writing one value.
    TEST_ASSERT_TRUE(perTab[TAB_HIDDEN] > 0);
    TEST_ASSERT_TRUE(paramIndex("hue")    >= 0 &&
                     params()[paramIndex("hue")].tab == TAB_HIDDEN);
    TEST_ASSERT_TRUE(paramIndex("sat")    >= 0 &&
                     params()[paramIndex("sat")].tab == TAB_HIDDEN);
    TEST_ASSERT_TRUE(paramIndex("bright") >= 0 &&
                     params()[paramIndex("bright")].tab == TAB_HIDDEN);

    // The three tilt switches are REACHABLE. They were hidden on the argument
    // that they are "set once", which was doing no work: they duplicate no
    // other widget, and the person who can see which way the fluid runs is
    // holding the robot, not reading /config on a laptop.
    //
    // Named `tilt_*` since 08-23, and the rename IS the migration: the measured
    // mapping moved into the baseline, so the correct value of each went from
    // "inv_x on" to "all off" — and a card already saved on a robot carried the
    // old answer, which saveConfig rewrites in full. Under the old names the
    // upgrade would have re-inverted X on exactly the robots that were right.
    const char* tilt[] = { "tilt_swap", "tilt_inv_x", "tilt_inv_y" };
    for (const char* k : tilt) {
        const int i = paramIndex(k);
        TEST_ASSERT_TRUE(i >= 0);
        TEST_ASSERT_EQUAL_INT(1, params()[i].tab);
        // and a switch, not a slider: the RENDER tab draws toggles only
        TEST_ASSERT_TRUE(params()[i].kind == Kind::Bool);
    }
}

// ---- every RENDER row is a toggle ------------------------------------------
// `drawToggleRows` paints an ON/OFF chip and the tap handler flips a boolean,
// neither of them asking what kind the row is. A Num row put on that tab would
// therefore be drawn as a switch and turned into 0 or 1 by a finger — losing
// the value silently, and only for whoever tapped it.
static void test_l_onglet_rendu_ne_contient_que_des_interrupteurs(void) {
    for (int i = 0; i < paramCount(); i++)
        if (params()[i].tab == 1)
            TEST_ASSERT_TRUE(params()[i].kind == Kind::Bool);
}

// ---- the preset chips must not sit on top of a slider row ------------------
static void test_les_presets_ne_recouvrent_aucune_rangee(void) {
    const Rect last = rowRect(ROWS_PER_TAB - 1);
    // At 212 against a last row ending at 213, the chips covered two pixels of
    // `gyro_gain`. That is not cosmetic: a TAP tests the chips first and would
    // apply a preset, while a DRAG at the same height still moved the slider —
    // two gestures, two answers, in the same two pixels.
    TEST_ASSERT_TRUE(PRESET_Y >= last.y + last.h);
    TEST_ASSERT_TRUE(PRESET_Y + PRESET_H <= SCR_H);
    // And a finger on the last row must not be read as a preset.
    TEST_ASSERT_EQUAL_INT(-1, presetAt(PRESET_X0 + 2, last.y + last.h - 1));
    // ...while the chips themselves still answer, and only inside their width.
    TEST_ASSERT_EQUAL_INT(0, presetAt(PRESET_X0 + 2, PRESET_Y + 2));
    TEST_ASSERT_EQUAL_INT(1, presetAt(PRESET_X0 + PRESET_STEP + 2, PRESET_Y + 2));
    TEST_ASSERT_EQUAL_INT(-1, presetAt(PRESET_X0 + PRESET_W + 1, PRESET_Y + 2));
    TEST_ASSERT_EQUAL_INT(-1, presetAt(PRESET_X0 + 2, PRESET_Y - 1));
}

// ---- the physics presets have a parameter to write into --------------------
static void test_les_presets_ont_leurs_parametres(void) {
    // look.h sets viscosity, gravity, bounce and trail; each has to exist here
    // or a preset would write a value nothing displays and nothing saves.
    TEST_ASSERT_TRUE(paramIndex("viscosity") >= 0);
    TEST_ASSERT_TRUE(paramIndex("gravity")   >= 0);
    TEST_ASSERT_TRUE(paramIndex("bounce")    >= 0);
    TEST_ASSERT_TRUE(paramIndex("trail")     >= 0);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_chaque_parametre_est_bilingue_et_borne);
    RUN_TEST(test_aucune_cle_en_double);
    RUN_TEST(test_l_index_trouve_et_refuse);
    RUN_TEST(test_le_dessin_et_le_doigt_lisent_les_memes_rangees);
    RUN_TEST(test_le_curseur_fait_l_aller_retour);
    RUN_TEST(test_un_doigt_hors_du_curseur_borne);
    RUN_TEST(test_le_rectangle_couvre_teinte_et_saturation);
    RUN_TEST(test_les_onglets_dessines_tiennent);
    RUN_TEST(test_l_onglet_rendu_ne_contient_que_des_interrupteurs);
    RUN_TEST(test_les_presets_ne_recouvrent_aucune_rangee);
    RUN_TEST(test_les_presets_ont_leurs_parametres);
    return UNITY_END();
}
