// =============================================================================
// test_sgp4 — TLE parsing + SGP4 propagator (space bin, docs/guests/SPACE.md)
// =============================================================================
// The propagator is the one piece of the "space" bin whose correctness cannot
// be judged by looking at the screen: a wrong orbit draws a confident dot in
// the wrong ocean. So it is pinned against the CANONICAL verification vector
// of Spacetrack Report No. 3 (satellite 88888, the test case published with
// the algorithm itself) rather than against our own expectations.
//
// Tolerance: ±1 km on position. The published vector carries 8 decimals; our
// spread against it comes from the constants and the un-Kozai form, both of
// which are documented choices in sgp4.h. 1 km on a 6800 km radius is 1.5e-4
// relative — far tighter than a 3-pixel dot on a 300 px world map, and far
// looser than the double-precision noise it would be pointless to chase.
// =============================================================================

#include <unity.h>
#include "../../firmware/space/sgp4.h"

using namespace spc;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

// The Spacetrack Report No. 3 test satellite. Kept verbatim, columns included:
// the parser is tested on exactly the byte layout it will meet on the wire.
static const char* T1 =
    "1 88888U          80275.98708465  .00073094  13844-3  66816-4 0    8";
static const char* T2 =
    "2 88888  72.8435 115.9689 0086731  52.6988 110.5714 16.05824518  105";

// A real ISS element set (Celestrak format, 3 lines with the name).
static const char* ISS0 = "ISS (ZARYA)";
static const char* ISS1 =
    "1 25544U 98067A   24001.50000000  .00016717  00000-0  30777-3 0  9003";
static const char* ISS2 =
    "2 25544  51.6416 247.4627 0006703 130.5360 325.0288 15.49815308  1239";

// ---- the element set is decoded field by field ------------------------------
static void test_tle_champs_decodes(void) {
    Tle t;
    TEST_ASSERT_TRUE(parseTle(T1, T2, t, nullptr, false));
    TEST_ASSERT_EQUAL_UINT32(88888, t.norad);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 72.8435 * DEG2RAD_D, t.incl);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 115.9689 * DEG2RAD_D, t.raan);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0086731, t.ecc);       // assumed "0."
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 52.6988 * DEG2RAD_D, t.argp);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 110.5714 * DEG2RAD_D, t.ma);
    // 16.05824518 rev/day in rad/min
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 16.05824518 * SGP4_TAU / 1440.0, t.no);
    // BSTAR " 66816-4" = 0.66816e-4 — the assumed-decimal-point encoding
    TEST_ASSERT_DOUBLE_WITHIN(1e-10, 0.66816e-4, t.bstar);
}

// ---- epoch year pivot: 57 is 1957, 56 is 2056 (SPACE.md, Reading a TLE) ----------------
static void test_pivot_annee_epoque(void) {
    Tle t;
    TEST_ASSERT_TRUE(parseTle(T1, T2, t, nullptr, false));
    // 80275.98708465 = 1980, day 275.987...  → JD 2444514.48708465
    TEST_ASSERT_DOUBLE_WITHIN(1e-5, 2444514.48708465, t.epochJd);

    // Same set relabelled to day 001 of year 24 must land in 2024, not 1924.
    char l1[80];
    strcpy(l1, T1);
    l1[18] = '2'; l1[19] = '4';                       // year 24
    memcpy(l1 + 20, "001.00000000", 12);              // 1 January, midnight
    Tle t24;
    TEST_ASSERT_TRUE(parseTle(l1, T2, t24, nullptr, false));
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 2460310.5, t24.epochJd);   // 2024-01-01 00:00
}

// ---- a corrupted line is REFUSED, not silently propagated -------------------
static void test_lignes_invalides_refusees(void) {
    Tle t;
    TEST_ASSERT_FALSE(parseTle(nullptr, T2, t));
    TEST_ASSERT_FALSE(parseTle("1 88888U", T2, t, nullptr, false));   // truncated
    TEST_ASSERT_FALSE(parseTle(T2, T1, t, nullptr, false));           // swapped
    // Two DIFFERENT satellites must never be fused into one chimera.
    char l2[80];
    strcpy(l2, T2);
    l2[4] = '7';                                       // 88888 vs 88878
    TEST_ASSERT_FALSE(parseTle(T1, l2, t, nullptr, false));
    // Checksum: flipping one digit of the payload must be caught.
    char bad[80];
    strcpy(bad, ISS1);
    bad[30] = (bad[30] == '5') ? '6' : '5';
    Tle ti;
    TEST_ASSERT_FALSE(parseTle(bad, ISS2, ti, ISS0, true));
}

// ---- THE canonical vector: STR#3 satellite 88888 ----------------------------
// Published values (km, km/s) at t = 0 and t = 360 min from epoch.
static void test_vecteur_canonique_str3(void) {
    Tle t;
    TEST_ASSERT_TRUE(parseTle(T1, T2, t, nullptr, false));
    Sgp4 prop;
    TEST_ASSERT_TRUE(prop.init(t));

    StateVector s0 = prop.propagate(0.0);
    TEST_ASSERT_TRUE(s0.ok);
    TEST_ASSERT_DOUBLE_WITHIN(1.0,  2328.97048951, s0.x);
    TEST_ASSERT_DOUBLE_WITHIN(1.0, -5995.22076416, s0.y);
    TEST_ASSERT_DOUBLE_WITHIN(1.0,  1719.97067261, s0.z);
    TEST_ASSERT_DOUBLE_WITHIN(0.01,  2.91207230, s0.vx);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, -0.98341546, s0.vy);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, -7.09081703, s0.vz);

    // 6 hours later — this is the one that catches a wrong secular rate: an
    // error in mdot/nodedot is invisible at t=0 and glaring here.
    StateVector s6 = prop.propagate(360.0);
    TEST_ASSERT_TRUE(s6.ok);
    TEST_ASSERT_DOUBLE_WITHIN(1.0,  2456.10705566, s6.x);
    TEST_ASSERT_DOUBLE_WITHIN(1.0, -6071.93853760, s6.y);
    TEST_ASSERT_DOUBLE_WITHIN(1.0,  1222.89727783, s6.z);
    TEST_ASSERT_DOUBLE_WITHIN(0.01,  2.67938992, s6.vx);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, -0.44829041, s6.vy);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, -7.22879231, s6.vz);
}

// ---- an ISS set produces a plausible orbit ----------------------------------
// Not a published vector: a SANITY envelope. It catches the class of bug the
// canonical vector cannot — a unit slip that only shows on a different orbit.
static void test_iss_enveloppe_plausible(void) {
    Tle t;
    TEST_ASSERT_TRUE(parseTle(ISS1, ISS2, t, ISS0, false));
    TEST_ASSERT_EQUAL_UINT32(25544, t.norad);
    TEST_ASSERT_EQUAL_STRING("ISS (ZARYA)", t.name);
    TEST_ASSERT_FALSE(t.isDeepSpace());
    TEST_ASSERT_DOUBLE_WITHIN(1.0, 92.9, t.periodMin());     // ~93 min

    Sgp4 prop;
    TEST_ASSERT_TRUE(prop.init(t));
    // Sample a full orbit: the radius must stay in the ISS band, and the
    // speed near 7.66 km/s, at EVERY point (a sign error on one term shows
    // up as an excursion somewhere in the revolution, not necessarily at 0).
    for (double m = 0.0; m <= 93.0; m += 3.0) {
        StateVector s = prop.propagate(m);
        TEST_ASSERT_TRUE(s.ok);
        const double r = sqrt(s.x * s.x + s.y * s.y + s.z * s.z);
        const double v = sqrt(s.vx * s.vx + s.vy * s.vy + s.vz * s.vz);
        TEST_ASSERT_TRUE(r > 6700.0 && r < 6820.0);          // ~400 km altitude
        TEST_ASSERT_TRUE(v > 7.5 && v < 7.8);
    }
    // Backwards propagation is legal and must stay in the same envelope.
    StateVector back = prop.propagate(-45.0);
    TEST_ASSERT_TRUE(back.ok);
    const double rb = sqrt(back.x * back.x + back.y * back.y + back.z * back.z);
    TEST_ASSERT_TRUE(rb > 6700.0 && rb < 6820.0);
}

// ---- a deep-space object is REFUSED, not approximated -----------------------
// SGP4 alone is invalid past a 225 min period. Saying so is the contract
// (SPACE.md, SGP4 section): the view must state "unsupported", never draw a wrong orbit.
static void test_espace_profond_refuse(void) {
    // A geostationary set: 1.0027 rev/day.
    const char* g1 =
        "1 28884U 05041A   24001.50000000 -.00000267  00000-0  00000-0 0  9998";
    const char* g2 =
        "2 28884   0.0175  84.4258 0002357 195.7683 292.0996  1.00270159 66668";
    Tle t;
    TEST_ASSERT_TRUE(parseTle(g1, g2, t, nullptr, false));
    TEST_ASSERT_TRUE(t.isDeepSpace());
    TEST_ASSERT_TRUE(t.periodMin() > 1400.0);          // ~1436 min
    Sgp4 prop;
    TEST_ASSERT_FALSE(prop.init(t));                   // refused
    TEST_ASSERT_FALSE(prop.ready());
    TEST_ASSERT_FALSE(prop.propagate(0.0).ok);         // and yields nothing
}

// ---- the age of the element set is available for the staleness rule ---------
static void test_age_du_jeu_delements(void) {
    Tle t;
    TEST_ASSERT_TRUE(parseTle(ISS1, ISS2, t, ISS0, false));
    Sgp4 prop;
    TEST_ASSERT_TRUE(prop.init(t));
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 0.0,  prop.ageDays(t.epochJd));
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 14.0, prop.ageDays(t.epochJd + 14.0));
    // propagateJd and propagate must agree — the same instant two ways.
    // Tolerance 1 m, and that is a REAL numerical property rather than
    // slack: a Julian date is ~2.46e6, so a double holds it to ~1e-10 day
    // (about 10 us), and the epoch round-trip (add 120/1440, subtract the
    // epoch back) costs that. At 7.7 km/s, 10 us is ~8 cm. Anyone tempted
    // to tighten this is chasing the representation of the DATE, not the
    // accuracy of the orbit.
    StateVector a = prop.propagate(120.0);
    StateVector b = prop.propagateJd(t.epochJd + 120.0 / 1440.0);
    TEST_ASSERT_DOUBLE_WITHIN(1e-3, a.x, b.x);
    TEST_ASSERT_DOUBLE_WITHIN(1e-3, a.z, b.z);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_tle_champs_decodes);
    RUN_TEST(test_pivot_annee_epoque);
    RUN_TEST(test_lignes_invalides_refusees);
    RUN_TEST(test_vecteur_canonique_str3);
    RUN_TEST(test_iss_enveloppe_plausible);
    RUN_TEST(test_espace_profond_refuse);
    RUN_TEST(test_age_du_jeu_delements);
    UNITY_END();
    return 0;
}
