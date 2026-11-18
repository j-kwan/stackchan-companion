// =============================================================================
// test_fluidlook — the quantisation that decides what gets repainted
// =============================================================================
// The colour of a dot is a taste. WHEN a dot is repainted is a budget: the
// frame only fits because a cell whose appearance did not change is skipped.
// So the assertions here are about the key, not about the prettiness — a key
// that changed on every frame would silently repaint the whole grid and the
// bin would drop to 12 fps with nothing on screen to explain why.
// =============================================================================

#include <unity.h>
#include "../../firmware/led-fluid/look.h"

using namespace sce::look;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

// ---- a small drift must NOT be a repaint -----------------------------------
static void test_une_derive_minuscule_ne_repeint_pas(void) {
    // One unit of density out of 256 is below the eye and below the 5-bit
    // quantisation: same key, so the renderer skips the cell.
    TEST_ASSERT_EQUAL_UINT16(dotKey(120, 40), dotKey(121, 41));
    // A real change crosses a step and must repaint.
    TEST_ASSERT_TRUE(dotKey(120, 40) != dotKey(160, 40));
    TEST_ASSERT_TRUE(dotKey(120, 40) != dotKey(120, 200));
}

// ---- an empty cell is ALWAYS the same key, whatever the speed --------------
static void test_une_cellule_vide_est_une_seule_cle(void) {
    // No matter, no dot: the speed byte of an empty cell is noise from the
    // splat and must not make a black cell repaint itself forever.
    TEST_ASSERT_EQUAL_UINT16(dotKey(0, 0), dotKey(0, 255));
}

// ---- the empty key is black, and black is what the wipe leaves -------------
static void test_la_cle_vide_est_noire(void) {
    TEST_ASSERT_EQUAL_UINT16(0x0000, keyToRgb565(dotKey(0, 0), 200, 100));
}

// ---- the hue is respected: red stays red, cyan stays cyan ------------------
static void test_la_teinte_choisie_est_celle_qui_sort(void) {
    const uint16_t red  = keyToRgb565(dotKey(255, 0), 0,   100);
    const uint16_t cyan = keyToRgb565(dotKey(255, 0), 180, 100);
    // RGB565: red occupies the top 5 bits, blue the bottom 5.
    TEST_ASSERT_TRUE((red  >> 11)   > (red  & 0x1F));
    TEST_ASSERT_TRUE((cyan & 0x1F)  > (cyan >> 11));
}

// ---- saturation is a CEILING the density can only move under ---------------
static void test_la_saturation_choisie_est_un_plafond(void) {
    // At satMax = 0 the dot is grey whatever the density does: the picker's
    // ceiling wins. A mapping that let density raise saturation would make the
    // "pastel" end of the rectangle do nothing.
    const uint16_t g = keyToRgb565(dotKey(255, 128), 0, 0);
    const int r = (g >> 11) & 0x1F, b = g & 0x1F;
    TEST_ASSERT_INT_WITHIN(1, r, b);
}

// ---- every preset lands inside the bounds the sliders enforce --------------
static void test_les_presets_tiennent_dans_les_bornes(void) {
    TEST_ASSERT_EQUAL_INT(5, presetCount());
    for (int i = 0; i < presetCount(); i++) {
        const Preset& p = presets()[i];
        TEST_ASSERT_TRUE(p.viscosity >= 0   && p.viscosity <= 100);
        TEST_ASSERT_TRUE(p.gravity   >= 0   && p.gravity   <= 200);
        TEST_ASSERT_TRUE(p.bounce    >= 0   && p.bounce    <= 90);
        TEST_ASSERT_TRUE(p.trail     <= 95);
        TEST_ASSERT_NOT_NULL(p.en);
        TEST_ASSERT_NOT_NULL(p.fr);
    }
}

// ---- the weightless preset is the only one at zero gravity -----------------
static void test_apesanteur_est_la_seule_a_gravite_nulle(void) {
    int zero = 0;
    for (int i = 0; i < presetCount(); i++)
        if (presets()[i].gravity == 0) zero++;
    TEST_ASSERT_EQUAL_INT(1, zero);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_une_derive_minuscule_ne_repeint_pas);
    RUN_TEST(test_une_cellule_vide_est_une_seule_cle);
    RUN_TEST(test_la_cle_vide_est_noire);
    RUN_TEST(test_la_teinte_choisie_est_celle_qui_sort);
    RUN_TEST(test_la_saturation_choisie_est_un_plafond);
    RUN_TEST(test_les_presets_tiennent_dans_les_bornes);
    RUN_TEST(test_apesanteur_est_la_seule_a_gravite_nulle);
    return UNITY_END();
}
