// =============================================================================
// test_fluid — the solver's invariants, not its pixels
// =============================================================================
// A fluid looks right or it does not, and no assertion settles that. What CAN
// be settled is that it never leaves the box, never produces a NaN, and reacts
// to viscosity in the one direction viscosity has. Those three are what turn a
// tuning session on hardware into a bounded exercise instead of a hunt for a
// solver that was broken all along.
// =============================================================================

#include <unity.h>
#include <math.h>
#include "../../firmware/led-fluid/fluid.h"

using namespace sce::fluid;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

static Params defaults() {
    Params p;
    p.viscosity = 30;
    p.gravity   = 100;
    p.bounce    = 25;
    p.gyroGain  = 100;
    p.particles = 240;
    p.pitch     = 12;
    return p;
}

// ---- nothing ever leaves the box -------------------------------------------
static void test_aucune_particule_ne_sort_de_la_boite(void) {
    Sim s;
    s.setParams(defaults());
    s.reset(12345);
    for (int i = 0; i < 600; i++) s.step(0.0f, 1.0f, 0.0f, 1.0f / 60.0f);
    for (int i = 0; i < s.count(); i++) {
        TEST_ASSERT_TRUE(s.px(i) >= 0.0f && s.px(i) <= (float)WORLD_W);
        TEST_ASSERT_TRUE(s.py(i) >= 0.0f && s.py(i) <= (float)WORLD_H);
    }
}

// ---- a violent shake must not tunnel through a wall ------------------------
static void test_une_secousse_violente_ne_traverse_pas_la_paroi(void) {
    Sim s;
    s.setParams(defaults());
    s.reset(7);
    // Ten frames of absurd input: full gravity flipping sides plus a swirl far
    // past anything the IMU can report. The clamp is what has to hold here.
    for (int i = 0; i < 10; i++)
        s.step(i % 2 ? -40.0f : 40.0f, 40.0f, 90.0f, 1.0f / 60.0f);
    for (int i = 0; i < s.count(); i++) {
        TEST_ASSERT_TRUE(s.px(i) >= 0.0f && s.px(i) <= (float)WORLD_W);
        TEST_ASSERT_TRUE(s.py(i) >= 0.0f && s.py(i) <= (float)WORLD_H);
    }
}

// ---- no NaN, ever ----------------------------------------------------------
static void test_dix_mille_pas_sans_nan(void) {
    Sim s;
    s.setParams(defaults());
    s.reset(99);
    for (int i = 0; i < 10000; i++) s.step(0.3f, 0.95f, 0.2f, 1.0f / 60.0f);
    for (int i = 0; i < s.count(); i++) {
        TEST_ASSERT_FALSE(isnan(s.px(i)));
        TEST_ASSERT_FALSE(isnan(s.py(i)));
    }
}

// ---- same seed, same film --------------------------------------------------
static void test_meme_graine_meme_film(void) {
    Sim a, b;
    a.setParams(defaults());  b.setParams(defaults());
    a.reset(4242);            b.reset(4242);
    for (int i = 0; i < 120; i++) {
        a.step(0.1f, 1.0f, 0.0f, 1.0f / 60.0f);
        b.step(0.1f, 1.0f, 0.0f, 1.0f / 60.0f);
    }
    for (int i = 0; i < a.count(); i++) {
        TEST_ASSERT_FLOAT_WITHIN(1e-4, a.px(i), b.px(i));
        TEST_ASSERT_FLOAT_WITHIN(1e-4, a.py(i), b.py(i));
    }
}

// ---- viscosity dissipates, and more of it dissipates more -------------------
static void test_la_viscosite_dissipe_davantage(void) {
    // Same shake, two viscosities: the thick one must end up calmer. This is
    // the one assertion that would fail if the viscosity slider were wired to
    // nothing — the failure mode a screenshot cannot show.
    auto energyAfterShake = [](float visc) {
        Params p = defaults();
        p.viscosity = visc;
        p.gravity   = 0;         // weightless, so only the shake feeds energy
        Sim s;
        s.setParams(p);
        s.reset(2024);
        for (int i = 0; i < 30; i++) s.step(0.0f, 0.0f, 8.0f, 1.0f / 60.0f);
        for (int i = 0; i < 90; i++) s.step(0.0f, 0.0f, 0.0f, 1.0f / 60.0f);
        return s.kineticEnergy();
    };
    TEST_ASSERT_TRUE(energyAfterShake(90.0f) < energyAfterShake(5.0f));
}

// ---- the splat lands on the grid the pitch describes ------------------------
static void test_le_splat_remplit_la_grille_du_pas(void) {
    Sim s;
    Params p = defaults();
    p.pitch = 8;
    s.setParams(p);
    s.reset(1);
    for (int i = 0; i < 120; i++) s.step(0.0f, 1.0f, 0.0f, 1.0f / 60.0f);

    DotGrid g;
    s.splat(g);
    TEST_ASSERT_EQUAL_INT(WORLD_W / 8, g.cols);
    TEST_ASSERT_EQUAL_INT(WORLD_H / 8, g.rows);
    TEST_ASSERT_TRUE(g.cols * g.rows <= MAX_CELLS);

    // Gravity pulled everything down, so the bottom half must carry more mass
    // than the top half. A splat that indexed rows the wrong way round passes
    // every bound check above and fails this one.
    long top = 0, bottom = 0;
    for (int r = 0; r < g.rows; r++)
        for (int c = 0; c < g.cols; c++)
            (r < g.rows / 2 ? top : bottom) += g.dens[r * g.cols + c];
    TEST_ASSERT_TRUE(bottom > top);
}

// ---- the cost stays bounded when the fluid COMPACTS ------------------------
// A neighbour grid bounds the pair count only while the particles are spread
// out. Gravity does the opposite for a living: it piles them up, and once a
// pile fits inside one grid cell every pair is a neighbour again and the step
// is O(n^2) after all. On the robot that took about ten seconds of sloshing to
// develop, and then the step outran its 16 ms period, the simulation task
// stopped yielding, IDLE0 starved and the task watchdog aborted the chip in a
// boot loop. So the bound is not an optimisation, it is the invariant that
// keeps the task schedulable — and it is asserted here, where a regression
// costs a second instead of a flash cycle.
static void test_le_cout_reste_borne_quand_le_fluide_se_tasse(void) {
    Params p = defaults();
    p.pitch     = 8;          // the finest grid: the tightest cells
    p.particles = MAX_PARTS;  // and the worst case of the slider
    Sim s;
    s.setParams(p);
    s.reset(3);
    uint32_t worst = 0;
    // Four seconds of gravity pulling into ONE corner — the compaction the
    // hardware run produced, reproduced deterministically.
    for (int i = 0; i < 240; i++) {
        s.step(1.0f, 1.0f, 0.0f, 1.0f / 60.0f);
        if (s.pairsLastStep() > worst) worst = s.pairsLastStep();
    }
    TEST_ASSERT_TRUE(worst > 0);                       // it did do work
    TEST_ASSERT_TRUE(worst <= 3u * (uint32_t)MAX_PARTS * (uint32_t)MAX_NEIGH);
}

// ---- and the truncation is FAIR, not biased by particle number -------------
// A ceiling charged only to the outer particle of a pair is a ceiling on the
// LOW indices: (i, j) is visited once, at i, so i pays and j never does. In a
// compacted pile that measured 24 pairs for particle 0 against 59 for particle
// 59 — and since the index is handed out at reset() with no relation to
// position, it made two identical piles damp differently depending on who
// happened to be numbered first. Cost and fairness are two properties; the
// test above pins the first, this one pins the second.
static void test_le_plafond_ne_favorise_pas_les_petits_indices(void) {
    Params p = defaults();
    p.pitch     = 8;
    p.particles = MAX_PARTS;
    Sim s;
    s.setParams(p);
    s.reset(3);
    int worstOne = 0;
    for (int i = 0; i < 240; i++) {
        s.step(1.0f, 1.0f, 0.0f, 1.0f / 60.0f);   // pile everything into a corner
        if (s.maxPairsForOneParticle() > worstOne) worstOne = s.maxPairsForOneParticle();
    }
    TEST_ASSERT_TRUE(worstOne > 0);                    // the cap was reached
    TEST_ASSERT_TRUE(worstOne <= MAX_NEIGH);
}

// ---- A LIQUID HAS A VOLUME, and this is the test that was missing ----------
// Every assertion above passed while the fluid was falling into a single row of
// dots along the bottom edge: nothing left the box, nothing was NaN, the bottom
// half did carry more mass than the top. "More mass at the bottom" is true of a
// puddle and equally true of a collapse, which is exactly why it never rang.
//
// The cause was units. The pressure stiffnesses were the paper's, written for a
// world a few units across and dropped into one 320 pixels across, worth about
// 13 px/s² against a gravity of 900: the fluid had no way to hold itself up.
// What has to be asserted is therefore the thing the eye actually judges — that
// the settled body is a BODY, several dots deep and spread over the width — and
// it is asserted in dot rows, the unit the picture is made of.
static void test_le_fluide_pose_garde_un_volume(void) {
    Sim s;
    s.setParams(defaults());
    s.reset(12345);
    for (int i = 0; i < 600; i++) s.step(0.0f, 1.0f, 0.0f, 1.0f / 60.0f);

    DotGrid g;
    s.splat(g);
    int lit = 0, rowsUsed = 0;
    for (int r = 0; r < g.rows; r++) {
        bool any = false;
        for (int c = 0; c < g.cols; c++)
            if (g.dens[r * g.cols + c] >= 8) { lit++; any = true; }
        if (any) rowsUsed++;
    }
    // 240 grains cannot honestly light fewer than a third of that many dots.
    // The collapsed version lit 26 of them, all on one row.
    TEST_ASSERT_TRUE(lit > s.count() / 2);
    TEST_ASSERT_TRUE(rowsUsed >= 4);
}

// ---- and no two grains stand in the same place ------------------------------
// The promise the fluid makes about its own picture: a dot is one grain. It is
// checked on the SETTLED fluid, which is the hard case — a liquid at rest sits
// at its packing limit, so this is where a solver that merely leans on pressure
// gives up and lets the grains interpenetrate. The tolerance is honest: the
// projection is iterated a bounded number of times per step, so it holds the
// separation to within a fraction of a diameter rather than exactly.
static void test_deux_grains_ne_se_superposent_pas(void) {
    Sim s;
    s.setParams(defaults());
    s.reset(12345);
    for (int i = 0; i < 600; i++) s.step(0.0f, 1.0f, 0.0f, 1.0f / 60.0f);

    const float d = GRAIN_DIAM * (float)defaults().pitch;
    float worst = 1e9f;
    for (int i = 0; i < s.count(); i++)
        for (int j = i + 1; j < s.count(); j++) {
            const float dx = s.px(i) - s.px(j), dy = s.py(i) - s.py(j);
            const float r = sqrtf(dx * dx + dy * dy);
            if (r < worst) worst = r;
        }
    // Three quarters of a diameter. The collapsed solver measured 0.00 px here,
    // and the version that let the wall clamp undo the projection measured 6.7
    // against a diameter of 10.8 — both fail this, which is the point.
    TEST_ASSERT_TRUE(worst > 0.75f * d);
}

// ---- the grain count is capped by the room the dots leave -------------------
// Non-penetration cannot invent space. Asking for 400 grains on a 20 px grid is
// asking for more grains than the screen can hold apart, and the solver answers
// with the number it can actually keep separated rather than jittering for ever
// trying to satisfy an impossible constraint.
static void test_le_nombre_de_grains_est_plafonne_par_le_pas(void) {
    Params p = defaults();
    p.pitch     = 20;
    p.particles = MAX_PARTS;
    Sim s;
    s.setParams(p);
    TEST_ASSERT_TRUE(s.count() < MAX_PARTS);
    TEST_ASSERT_TRUE(s.count() >= 60);          // still a fluid, not a handful

    // and a fine grid is NOT capped: the room is there
    p.pitch = 8;
    Sim f;
    f.setParams(p);
    TEST_ASSERT_EQUAL_INT(MAX_PARTS, f.count());
}

// ---- the fluid does not START on the floor ---------------------------------
// reset() lays a lattice, and a lattice sized by dividing the box by a step has
// to floor the column count — so the rows it then needs can overshoot the
// height, and the clamp used to stack the whole last row on y = WORLD_H. A line
// of grains welded to the bottom edge, in the first frame of the animation.
//
// Asserted STRICTLY inside the box rather than within it: "inside the box" was
// already true of the broken version, which is why nothing caught it. What has
// to be true is that no grain is ON an edge, because only the clamp puts one
// there. Swept over the pitch range, since the overshoot depends on how the
// count divides — at pitch 12 with 400 grains the old lattice needed 18 rows of
// 13.86 px in a 240 px box and clamped the eighteenth.
static void test_la_grille_de_depart_tient_dans_la_boite(void) {
    for (int pitch = MIN_PITCH; pitch <= 20; pitch++)
        for (int w = 0; w < 4; w++) {
            static const int wants[4] = {60, 153, 240, MAX_PARTS};
            Params p = defaults();
            p.pitch     = pitch;
            p.particles = wants[w];
            Sim s;
            s.setParams(p);
            s.reset(4242);
            for (int i = 0; i < s.count(); i++) {
                TEST_ASSERT_TRUE(s.px(i) > 0.0f && s.px(i) < (float)WORLD_W);
                TEST_ASSERT_TRUE(s.py(i) > 0.0f && s.py(i) < (float)WORLD_H);
            }
        }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_la_grille_de_depart_tient_dans_la_boite);
    RUN_TEST(test_le_fluide_pose_garde_un_volume);
    RUN_TEST(test_deux_grains_ne_se_superposent_pas);
    RUN_TEST(test_le_nombre_de_grains_est_plafonne_par_le_pas);
    RUN_TEST(test_aucune_particule_ne_sort_de_la_boite);
    RUN_TEST(test_une_secousse_violente_ne_traverse_pas_la_paroi);
    RUN_TEST(test_dix_mille_pas_sans_nan);
    RUN_TEST(test_meme_graine_meme_film);
    RUN_TEST(test_la_viscosite_dissipe_davantage);
    RUN_TEST(test_le_splat_remplit_la_grille_du_pas);
    RUN_TEST(test_le_cout_reste_borne_quand_le_fluide_se_tasse);
    RUN_TEST(test_le_plafond_ne_favorise_pas_les_petits_indices);
    return UNITY_END();
}
