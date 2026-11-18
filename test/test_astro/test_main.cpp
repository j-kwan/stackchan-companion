// =============================================================================
// test_astro — Sun, Moon, planets, geodesy, passes (space bin, docs/guests/SPACE.md)
// =============================================================================
// Pinned against PUBLISHED reference values (Meeus worked examples and known
// astronomical events), never against what this code happens to return: a test
// written from the implementation's own output proves only that it is
// deterministic. Tolerances are stated per case and match the precision budget
// documented at the top of astro.h.
// =============================================================================

#include <unity.h>
#include <initializer_list>
#include "../../firmware/space/astro.h"

using namespace spc;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

// ---- Julian date, both directions -------------------------------------------
// Meeus ch. 7 worked examples; the round trip catches the classic March/January
// month-shift bug in one line.
static void test_date_julienne(void) {
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 2451545.0,   jdFromUtc(2000,  1,  1, 12, 0, 0));
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 2451179.5,   jdFromUtc(1999,  1,  1,  0, 0, 0));
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 2446822.5,   jdFromUtc(1987,  1, 27,  0, 0, 0));
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 2436116.31,  jdFromUtc(1957, 10,  4, 19, 26, 24));

    CalDate c = calFromJd(2451545.0);
    TEST_ASSERT_EQUAL_INT(2000, c.year);
    TEST_ASSERT_EQUAL_INT(1, c.month);
    TEST_ASSERT_EQUAL_INT(1, c.day);
    TEST_ASSERT_EQUAL_INT(12, c.hour);
    // Round trip over a leap day and a year boundary — where the month
    // arithmetic breaks if the January/February shift is wrong.
    const double jd = jdFromUtc(2024, 2, 29, 23, 45, 0);
    c = calFromJd(jd);
    TEST_ASSERT_EQUAL_INT(2024, c.year);
    TEST_ASSERT_EQUAL_INT(2, c.month);
    TEST_ASSERT_EQUAL_INT(29, c.day);
    TEST_ASSERT_EQUAL_INT(23, c.hour);
    TEST_ASSERT_EQUAL_INT(45, c.minute);
}

// ---- GMST at a documented epoch ---------------------------------------------
// At J2000.0 exactly, Greenwich mean sidereal time is 18h 41m 50.55s
// = 280.46062 deg. This one constant anchors every Earth-fixed conversion.
static void test_gmst(void) {
    const double g = gmstRad(J2000) * RAD2DEG_D;
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 280.46062, g);
    // A sidereal day is 23h56m04s: GMST must come back to the same value.
    const double g2 = gmstRad(J2000 + 0.99726957) * RAD2DEG_D;
    TEST_ASSERT_DOUBLE_WITHIN(0.01, g, g2);
}

// ---- the Sun: solstices and equinoxes are the checkable events ---------------
static void test_soleil(void) {
    // June solstice 2024: declination at its maximum, ~+23.44 deg.
    Equatorial s = sunPosition(jdFromUtc(2024, 6, 20, 20, 51, 0));
    TEST_ASSERT_DOUBLE_WITHIN(0.05, 23.44, s.decDeg);
    // December solstice: the mirror.
    s = sunPosition(jdFromUtc(2024, 12, 21, 9, 20, 0));
    TEST_ASSERT_DOUBLE_WITHIN(0.05, -23.44, s.decDeg);
    // March equinox: declination through zero, RA through zero.
    s = sunPosition(jdFromUtc(2024, 3, 20, 3, 6, 0));
    TEST_ASSERT_DOUBLE_WITHIN(0.05, 0.0, s.decDeg);
    // Earth is at perihelion in early January (~0.983 AU) and aphelion in
    // early July (~1.017): the distance term is not decorative, the Moon's
    // phase angle uses it.
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 0.9833, sunPosition(jdFromUtc(2024, 1, 3, 0, 0, 0)).distAu);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 1.0167, sunPosition(jdFromUtc(2024, 7, 5, 0, 0, 0)).distAu);
}

// ---- solar altitude: the twilight threshold the whole bin gates on ----------
static void test_altitude_solaire(void) {
    // Local noon at the equator on an equinox: the Sun is overhead.
    // 2024-03-20 03:06 UTC is the equinox; noon at longitude 0 is 12:00 UTC.
    const double alt = sunAltDeg(jdFromUtc(2024, 3, 20, 12, 0, 0), 0.0, 0.0);
    TEST_ASSERT_TRUE(alt > 88.0);
    // Local midnight, same place: the Sun is as far below.
    const double mid = sunAltDeg(jdFromUtc(2024, 3, 20, 0, 0, 0), 0.0, 0.0);
    TEST_ASSERT_TRUE(mid < -85.0);
    // Polar day: in June the Sun never sets at 80 deg north.
    for (int h = 0; h < 24; h += 3)
        TEST_ASSERT_TRUE(sunAltDeg(jdFromUtc(2024, 6, 21, h, 0, 0), 80.0, 0.0) > 0.0);
}

// ---- the Moon: phases are datable events, so they are the reference ---------
static void test_lune_phases(void) {
    // Full Moon of 2024-01-25 17:54 UTC: elongation ~180, illumination ~1.
    MoonInfo m = moonInfo(jdFromUtc(2024, 1, 25, 17, 54, 0));
    TEST_ASSERT_DOUBLE_WITHIN(1.5, 180.0, m.elongDeg);
    TEST_ASSERT_TRUE(m.illum > 0.995);
    TEST_ASSERT_TRUE(moonPhaseOf(m.elongDeg) == MoonPhase::Full);

    // New Moon of 2024-02-09 22:59 UTC: elongation ~0 (or ~360), illum ~0.
    m = moonInfo(jdFromUtc(2024, 2, 9, 22, 59, 0));
    const double e = m.elongDeg > 180.0 ? 360.0 - m.elongDeg : m.elongDeg;
    TEST_ASSERT_DOUBLE_WITHIN(1.5, 0.0, e);
    TEST_ASSERT_TRUE(m.illum < 0.005);
    TEST_ASSERT_TRUE(moonPhaseOf(m.elongDeg) == MoonPhase::New);

    // First quarter of 2024-02-16 15:01 UTC: half lit, and WAXING — the flag
    // that decides which limb the terminator is drawn on.
    m = moonInfo(jdFromUtc(2024, 2, 16, 15, 1, 0));
    TEST_ASSERT_DOUBLE_WITHIN(0.03, 0.5, m.illum);
    TEST_ASSERT_TRUE(m.waxing);
    TEST_ASSERT_TRUE(moonPhaseOf(m.elongDeg) == MoonPhase::FirstQuarter);

    // Last quarter of 2024-02-02 23:18 UTC: half lit, and WANING.
    m = moonInfo(jdFromUtc(2024, 2, 2, 23, 18, 0));
    TEST_ASSERT_DOUBLE_WITHIN(0.03, 0.5, m.illum);
    TEST_ASSERT_FALSE(m.waxing);
    TEST_ASSERT_TRUE(moonPhaseOf(m.elongDeg) == MoonPhase::LastQuarter);
}

// ---- de quel cote la Lune est ECLAIREE, vue du sol -------------------------
// La phase est un fait sur le Soleil et la Lune ; le cote qu'on VOIT eclaire
// est un fait sur l'endroit ou l'on se tient. Au nord, une Lune croissante est
// eclairee a DROITE ; au sud, la meme Lune est vue retournee, donc a GAUCHE.
// Dessiner partout la convention du nord est faux pour la moitie de la planete,
// et faux de la seule facon que personne ne signale : l'image ressemble
// toujours a une Lune.
static void test_cote_eclaire_selon_l_hemisphere(void) {
    // Paris : croissante a droite, decroissante a gauche.
    TEST_ASSERT_TRUE (moonLitOnRight(true,   48.85));
    TEST_ASSERT_FALSE(moonLitOnRight(false,  48.85));
    // La Reunion : l'inverse, exactement.
    TEST_ASSERT_FALSE(moonLitOnRight(true,  -20.89));
    TEST_ASSERT_TRUE (moonLitOnRight(false, -20.89));
    // Sydney, Ushuaia : le signe suffit, la valeur ne compte pas.
    TEST_ASSERT_FALSE(moonLitOnRight(true,  -33.87));
    TEST_ASSERT_FALSE(moonLitOnRight(true,  -54.80));
    // L'equateur est une vraie egalite ; elle est tranchee au nord, et ce cas
    // fixe ce choix pour que personne ne le "corrige" au hasard plus tard.
    TEST_ASSERT_TRUE (moonLitOnRight(true,    0.0));
    // Et pour une latitude donnee, les deux phases sont toujours opposees :
    // c'est la propriete qui fait qu'un croissant ne peut pas etre dessine du
    // meme cote que son decroissant.
    for (double lat = -80.0; lat <= 80.0; lat += 10.0)
        TEST_ASSERT_TRUE(moonLitOnRight(true, lat) != moonLitOnRight(false, lat));
}

// ---- combien de jours jusqu'a la prochaine phase ---------------------------
// "C'est quand la pleine lune" est LA question qu'on pose a un ecran de lune,
// et l'ancienne colonne n'y repondait pas. Le calcul est au taux synodique
// MOYEN : pour un chiffre affiche en jours entiers, l'ecart avec le taux vrai
// (~10 h aux quartiers) est invisible, et la reponse exacte demanderait une
// recherche de racine sur moonInfo.
static void test_jours_jusqu_a_la_phase(void) {
    // Premier quartier (90 deg) : la pleine lune est dans un quart de mois.
    TEST_ASSERT_DOUBLE_WITHIN(0.01, SYNODIC / 4.0, moonDaysToElong(90.0, 180.0));
    // Juste avant la pleine lune : presque zero, jamais negatif.
    TEST_ASSERT_TRUE(moonDaysToElong(179.0, 180.0) > 0.0);
    TEST_ASSERT_TRUE(moonDaysToElong(179.0, 180.0) < 0.1);
    // Decroissante a 350 deg : la NOUVELLE lune est proche, la pleine est loin
    // - l'enroulement a 360 doit rendre un delai court, pas un tour complet.
    TEST_ASSERT_TRUE(moonDaysToElong(350.0, 0.0) < 1.0);
    TEST_ASSERT_TRUE(moonDaysToElong(350.0, 180.0) > 14.0);
    // Et sur tout le cycle, le delai reste dans [0, un mois synodique).
    for (double e = 0.0; e < 360.0; e += 15.0) {
        const double d = moonDaysToElong(e, 180.0);
        TEST_ASSERT_TRUE(d >= 0.0 && d < SYNODIC);
    }
}

// ---- the Moon: distance stays inside the real perigee/apogee band -----------
static void test_lune_distance_et_age(void) {
    // Sampled across two months, the distance must never leave 356-407 Mm,
    // and the age must stay inside one synodic month.
    for (int d = 0; d < 60; d++) {
        MoonInfo m = moonInfo(jdFromUtc(2024, 1, 1, 0, 0, 0) + d);
        TEST_ASSERT_TRUE(m.distKm > 355000.0 && m.distKm < 408000.0);
        TEST_ASSERT_TRUE(m.ageDays >= 0.0 && m.ageDays <= SYNODIC + 0.01);
        TEST_ASSERT_TRUE(m.illum >= 0.0 && m.illum <= 1.0);
        TEST_ASSERT_TRUE(m.decDeg > -29.0 && m.decDeg < 29.0);   // |dec| <= 28.6
    }
}

// ---- the planets: elongation is what makes each one checkable ---------------
static void test_planetes(void) {
    const double jd = jdFromUtc(2024, 1, 1, 0, 0, 0);
    const Equatorial sun = sunPosition(jd);
    // Mercury and Venus are INNER planets: their elongation from the Sun can
    // never exceed 28 and 47 degrees. Any frame or epoch mistake blows this.
    for (Planet p : { Planet::Mercury, Planet::Venus }) {
        const Equatorial q = planetPosition(p, jd);
        const double sep = fabs(wrap180(q.raDeg - sun.raDeg));
        TEST_ASSERT_TRUE(sep < 50.0);
    }
    // Distances must sit in each planet's real geocentric band.
    TEST_ASSERT_TRUE(planetPosition(Planet::Venus,   jd).distAu > 0.25 &&
                     planetPosition(Planet::Venus,   jd).distAu < 1.75);
    TEST_ASSERT_TRUE(planetPosition(Planet::Mars,    jd).distAu > 0.35 &&
                     planetPosition(Planet::Mars,    jd).distAu < 2.70);
    TEST_ASSERT_TRUE(planetPosition(Planet::Jupiter, jd).distAu > 3.90 &&
                     planetPosition(Planet::Jupiter, jd).distAu < 6.50);
    TEST_ASSERT_TRUE(planetPosition(Planet::Saturn,  jd).distAu > 8.00 &&
                     planetPosition(Planet::Saturn,  jd).distAu < 11.10);
    // The two outer planets (added 08-04 for telescope use). Their bands are
    // narrow — Uranus never leaves 17.3-21.1 AU from Earth, Neptune
    // 28.8-31.3 — so a wrong element set or a dropped semi-major-axis drift
    // term shows up here immediately.
    TEST_ASSERT_TRUE(planetPosition(Planet::Uranus,  jd).distAu > 17.20 &&
                     planetPosition(Planet::Uranus,  jd).distAu < 21.20);
    TEST_ASSERT_TRUE(planetPosition(Planet::Neptune, jd).distAu > 28.70 &&
                     planetPosition(Planet::Neptune, jd).distAu < 31.40);
    // Every planet stays near the ecliptic: |declination| <= 30 deg.
    for (int i = 0; i < (int)Planet::COUNT; i++)
        TEST_ASSERT_TRUE(fabs(planetPosition((Planet)i, jd).decDeg) < 30.0);
    TEST_ASSERT_EQUAL_STRING("Jupiter", planetName(Planet::Jupiter));
    TEST_ASSERT_EQUAL_STRING("Neptune", planetName(Planet::Neptune));
    // The observing aid is what keeps the SKY view honest: the five classical
    // planets need nothing, Uranus binoculars, Neptune a telescope.
    for (Planet p : { Planet::Mercury, Planet::Venus, Planet::Mars,
                      Planet::Jupiter, Planet::Saturn })
        TEST_ASSERT_EQUAL_UINT8(0, planetAid(p));
    TEST_ASSERT_EQUAL_UINT8(1, planetAid(Planet::Uranus));
    TEST_ASSERT_EQUAL_UINT8(2, planetAid(Planet::Neptune));
    // An outer planet moves SLOWLY: Neptune covers under 2.2 deg of ecliptic
    // longitude a year. A sign error in its mean motion would show as a jump.
    const double n0 = planetPosition(Planet::Neptune, jd).raDeg;
    const double n1 = planetPosition(Planet::Neptune, jd + 365.25).raDeg;
    TEST_ASSERT_TRUE(fabs(wrap180(n1 - n0)) < 4.0);
}

// ---- geodesy: the round trip is the test ------------------------------------
static void test_geodesie_aller_retour(void) {
    const double jd = jdFromUtc(2024, 6, 15, 10, 30, 0);
    struct { double lat, lon, alt; } sites[] = {
        {  48.8566,   2.3522, 0.035 },     // Paris
        { -20.8823,  55.4504, 0.020 },     // Réunion (the user's sky)
        {   0.0,   -179.5,    0.0   },     // just west of the date line
        {  71.0,     25.0,    0.5   },     // high latitude
    };
    for (auto& s : sites) {
        double x, y, z;
        observerEci(s.lat, s.lon, s.alt, jd, x, y, z);
        Geodetic g = eciToGeodetic(x, y, z, jd);
        TEST_ASSERT_DOUBLE_WITHIN(1e-6, s.lat, g.latDeg);
        TEST_ASSERT_DOUBLE_WITHIN(1e-6, s.lon, g.lonDeg);
        TEST_ASSERT_DOUBLE_WITHIN(1e-4, s.alt, g.altKm);
    }
    // Longitude must WRAP, not run off: a point at +181 is -179.
    double x, y, z;
    observerEci(0.0, 181.0, 0.0, jd, x, y, z);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, -179.0, eciToGeodetic(x, y, z, jd).lonDeg);
}

// ---- look angles: an object at the observer's zenith reads 90 deg -----------
static void test_angles_de_vue(void) {
    const double jd = jdFromUtc(2024, 6, 15, 10, 30, 0);
    const double lat = -20.8823, lon = 55.4504;
    // Put a point 400 km straight up from the observer.
    double ox, oy, oz;
    observerEci(lat, lon, 0.0, jd, ox, oy, oz);
    const double n = sqrt(ox * ox + oy * oy + oz * oz);
    const double k = (n + 400.0) / n;
    LookAngle la = lookAngle(ox * k, oy * k, oz * k, jd, lat, lon, 0.0);
    TEST_ASSERT_DOUBLE_WITHIN(0.2, 90.0, la.elDeg);
    TEST_ASSERT_DOUBLE_WITHIN(1.0, 400.0, la.rangeKm);
    // A point on the opposite side of the Earth is well below the horizon.
    la = lookAngle(-ox * k, -oy * k, -oz * k, jd, lat, lon, 0.0);
    TEST_ASSERT_TRUE(la.elDeg < -80.0);
    // Compass letters, including the wrap at north.
    TEST_ASSERT_EQUAL_STRING("N",   compass16(0.0));
    TEST_ASSERT_EQUAL_STRING("N",   compass16(359.0));
    TEST_ASSERT_EQUAL_STRING("E",   compass16(90.0));
    TEST_ASSERT_EQUAL_STRING("SW",  compass16(225.0));
    TEST_ASSERT_EQUAL_STRING("NNE", compass16(22.5));
}

// ---- Earth's shadow: the test that decides VISIBLE from radio ---------------
static void test_ombre_terrestre(void) {
    const double jd = jdFromUtc(2024, 6, 15, 0, 0, 0);
    double ux, uy, uz;
    sunUnitVector(jd, ux, uy, uz);
    const double R = EARTH_A + 400.0;
    // Straight at the Sun: lit.
    TEST_ASSERT_TRUE(isSunlit(ux * R, uy * R, uz * R, jd));
    // Straight behind the Earth, inside the cylinder: dark.
    TEST_ASSERT_FALSE(isSunlit(-ux * R, -uy * R, -uz * R, jd));
    // Behind, but far enough off-axis to clear the shadow cylinder: lit.
    // Build a vector perpendicular to the Sun direction to offset along.
    double px = -uy, py = ux, pz = 0.0;
    const double pn = sqrt(px * px + py * py + pz * pz);
    px /= pn; py /= pn;
    const double off = EARTH_A + 500.0;
    TEST_ASSERT_TRUE(isSunlit(-ux * R + px * off, -uy * R + py * off, -uz * R, jd));
}

// ---- rise/set: the polar cases are the ones that break naive scanners -------
static void test_lever_coucher(void) {
    const double lat = -20.8823, lon = 55.4504;
    const double j0 = jdFromUtc(2024, 6, 15, 0, 0, 0);
    auto sunAlt = [&](double t) { return sunAltDeg(t, lat, lon); };
    RiseSet rs = findRiseSet(sunAlt, j0, 24.0, -0.833);
    TEST_ASSERT_TRUE(rs.hasRise && rs.hasSet);
    // Réunion in June: sunrise ~02:30 UTC, sunset ~13:50 UTC (06:30/17:50
    // local, UTC+4) — a winter day of roughly 11 hours.
    CalDate r = calFromJd(rs.riseJd), s = calFromJd(rs.setJd);
    TEST_ASSERT_TRUE(r.hour >= 1 && r.hour <= 4);
    TEST_ASSERT_TRUE(s.hour >= 12 && s.hour <= 15);
    const double dayH = (rs.setJd - rs.riseJd) * 24.0;
    TEST_ASSERT_TRUE(dayH > 10.5 && dayH < 12.0);

    // Polar day: no crossing at all, and the search must SAY so rather than
    // return a garbage instant.
    auto polar = [&](double t) { return sunAltDeg(t, 80.0, 0.0); };
    RiseSet pr = findRiseSet(polar, jdFromUtc(2024, 6, 21, 0, 0, 0), 24.0, -0.833);
    TEST_ASSERT_FALSE(pr.hasRise);
    TEST_ASSERT_FALSE(pr.hasSet);
}

// ---- the sliced pass search --------------------------------------------
static void test_recherche_de_passages(void) {
    const char* l1 =
        "1 25544U 98067A   24001.50000000  .00016717  00000-0  30777-3 0  9003";
    const char* l2 =
        "2 25544  51.6416 247.4627 0006703 130.5360 325.0288 15.49815308  1239";
    Tle t; TEST_ASSERT_TRUE(parseTle(l1, l2, t, "ISS", false));
    Sgp4 prop; TEST_ASSERT_TRUE(prop.init(t));

    // Paris, 48 h window. The ISS is inclined 51.6 deg so Paris (48.9) gets
    // several passes a day: a window this wide CANNOT legitimately be empty,
    // which is what makes the assertion meaningful.
    PassFinder pf;
    pf.begin(&prop, t.epochJd, 48.0, 48.8566, 2.3522, 0.035, 10.0, 30.0);
    int slices = 0;
    while (!pf.step(200) && slices < 2000) slices++;     // sliced, as loop() does
    TEST_ASSERT_TRUE(pf.done());
    TEST_ASSERT_TRUE(pf.count() > 0);
    TEST_ASSERT_TRUE(slices > 1);                        // genuinely sliced
    for (int i = 0; i < pf.count(); i++) {
        const Pass& p = pf.at(i);
        TEST_ASSERT_TRUE(p.setJd > p.riseJd);                    // ordered
        TEST_ASSERT_TRUE(p.maxJd >= p.riseJd && p.maxJd <= p.setJd);
        TEST_ASSERT_TRUE(p.maxEl >= 10.0 && p.maxEl <= 90.0);    // above the floor
        const double durMin = (p.setJd - p.riseJd) * 1440.0;
        TEST_ASSERT_TRUE(durMin > 0.5 && durMin < 12.0);         // LEO geometry
        TEST_ASSERT_TRUE(p.riseAz >= 0.0 && p.riseAz < 360.0);
        if (i > 0) TEST_ASSERT_TRUE(p.riseJd > pf.at(i - 1).setJd);   // disjoint
    }
    // A finder with no propagator is DONE immediately rather than looping.
    PassFinder empty;
    empty.begin(nullptr, t.epochJd, 24.0, 0.0, 0.0);
    TEST_ASSERT_TRUE(empty.step(10));
    TEST_ASSERT_EQUAL_INT(0, empty.count());
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.0, empty.progress());
}

// ---- ISS ground track: a real orbit stays on the real Earth -----------------
static void test_trace_au_sol_iss(void) {
    const char* l1 =
        "1 25544U 98067A   24001.50000000  .00016717  00000-0  30777-3 0  9003";
    const char* l2 =
        "2 25544  51.6416 247.4627 0006703 130.5360 325.0288 15.49815308  1239";
    Tle t; TEST_ASSERT_TRUE(parseTle(l1, l2, t, "ISS", false));
    Sgp4 prop; TEST_ASSERT_TRUE(prop.init(t));
    bool sawNorth = false, sawSouth = false;
    for (double m = 0; m <= 93.0; m += 1.0) {
        StateVector sv = prop.propagate(m);
        TEST_ASSERT_TRUE(sv.ok);
        Geodetic g = eciToGeodetic(sv.x, sv.y, sv.z, t.epochJd + m / 1440.0);
        // The ISS can never be seen from beyond its inclination in latitude.
        TEST_ASSERT_TRUE(fabs(g.latDeg) <= 52.5);
        TEST_ASSERT_TRUE(g.lonDeg >= -180.0 && g.lonDeg <= 180.0);
        TEST_ASSERT_TRUE(g.altKm > 380.0 && g.altKm < 440.0);
        if (g.latDeg >  40.0) sawNorth = true;
        if (g.latDeg < -40.0) sawSouth = true;
    }
    // One revolution must visit both hemispheres — a frozen or wrongly scaled
    // propagation would sit in a band and still pass every bound above.
    TEST_ASSERT_TRUE(sawNorth && sawSouth);
}

// ---- a search started MID-PASS must not invent a rise time ------------------
// `startPassSearch(nowJd())` fires at arbitrary instants (a forced refresh, the
// 6-hourly re-run), so the window can open with the satellite already up. The
// first version recorded riseJd = jdStart and riseAz = "wherever it is now",
// and the table then printed a rise time equal to NOW with a fabricated
// bearing — wrong data presented as fact (review 08-04).
static void test_passage_deja_commence_est_ignore(void) {
    const char* l1 =
        "1 25544U 98067A   24001.50000000  .00016717  00000-0  30777-3 0  9003";
    const char* l2 =
        "2 25544  51.6416 247.4627 0006703 130.5360 325.0288 15.49815308  1239";
    Tle t; TEST_ASSERT_TRUE(parseTle(l1, l2, t, "ISS", false));
    Sgp4 prop; TEST_ASSERT_TRUE(prop.init(t));
    const double lat = 48.8566, lon = 2.3522;

    // Find a real pass first, to get an instant that is provably mid-pass.
    PassFinder ref;
    ref.begin(&prop, t.epochJd, 48.0, lat, lon, 0.035, 10.0, 30.0);
    while (!ref.step(400)) { }
    TEST_ASSERT_TRUE(ref.count() > 0);
    const Pass& p0 = ref.at(0);
    const double mid = 0.5 * (p0.riseJd + p0.setJd);
    // Sanity: the satellite really is above the horizon at `mid`.
    {
        const StateVector sv = prop.propagateJd(mid);
        TEST_ASSERT_TRUE(sv.ok);
        TEST_ASSERT_TRUE(lookAngle(sv.x, sv.y, sv.z, mid, lat, lon, 0.035).elDeg > 0);
    }

    // Start the search THERE. The partial pass must be discarded, not filed.
    PassFinder pf;
    pf.begin(&prop, mid, 48.0, lat, lon, 0.035, 10.0, 30.0);
    while (!pf.step(400)) { }
    TEST_ASSERT_TRUE(pf.count() > 0);
    for (int i = 0; i < pf.count(); i++) {
        const Pass& p = pf.at(i);
        // No pass may claim to rise at the very instant the search began...
        TEST_ASSERT_TRUE(p.riseJd > mid + 30.0 / 86400.0);
        // ...and every listed rise must be a REAL horizon crossing, i.e. the
        // satellite is below the horizon one step earlier.
        const double before = p.riseJd - 60.0 / 86400.0;
        const StateVector sv = prop.propagateJd(before);
        TEST_ASSERT_TRUE(sv.ok);
        TEST_ASSERT_TRUE(lookAngle(sv.x, sv.y, sv.z, before, lat, lon, 0.035).elDeg <= 0);
    }
}

// ---- the Sun and Earth are ONE vector, seen from two ends ------------------
// `earthHelio` and `sunPosition` are separate theories (Schlyter and Meeus),
// and `planetPosition` needs Earth's vector to turn a heliocentric position
// into a geocentric one. That block used to be a byte-for-byte COPY of
// earthHelio inside planetPosition — twins nothing tested, which is exactly
// how they drift (review 08-04). Now they are one call, and this pins it: the
// Earth's heliocentric longitude is the Sun's geocentric longitude plus 180,
// by definition, and the two independent theories have to agree on it.
static void test_terre_et_soleil_sont_un_seul_vecteur(void) {
    for (int d = 0; d < 400; d += 37) {
        const double jd = jdFromUtc(2024, 1, 1, 0, 0, 0) + d;
        const double sunLon   = sunPosition(jd).eclLonDeg;
        const double earthLon = earthHelio(jd).lonDeg;
        // 0.03 deg: the spread between Meeus's low-precision Sun and
        // Schlyter's Earth. A copy that drifted would show as degrees.
        TEST_ASSERT_DOUBLE_WITHIN(0.03, 0.0,
                                  fabs(wrap180(earthLon - 180.0 - sunLon)));
        // And the distances are the same one number.
        TEST_ASSERT_DOUBLE_WITHIN(1e-4, sunPosition(jd).distAu, earthHelio(jd).rAu);
    }
}

// ---- a planet's geocentric distance obeys the triangle ---------------------
// The strongest cheap check on the helio -> geo conversion: with r the
// planet's heliocentric distance and R the Earth's, the geocentric distance
// must lie in [|r-R|, r+R]. A wrong solar vector breaks this immediately,
// where the per-planet distance bands are loose enough to hide it.
static void test_triangle_helio_geo(void) {
    for (int d = 0; d < 400; d += 53) {
        const double jd = jdFromUtc(2024, 1, 1, 0, 0, 0) + d;
        const double R = earthHelio(jd).rAu;
        for (int i = 0; i < (int)Planet::COUNT; i++) {
            const double r = planetHelio((Planet)i, jd).rAu;
            const double g = planetPosition((Planet)i, jd).distAu;
            // 0.02 AU of slack for the inclination this planar test ignores.
            TEST_ASSERT_TRUE(g >= fabs(r - R) - 0.02);
            TEST_ASSERT_TRUE(g <= r + R + 0.02);
        }
    }
}


// An umbral eclipse needs MORE than a full moon: the ecliptic latitude must
// be small too. The naive test — "darken the Moon when it sits behind
// Earth" — fires at every opposition, wrong 12 times out of 13; beta is the
// whole reason they are rare. Dates from the NASA eclipse canon.
// THE ROOT CAUSE, PINNED WHERE IT LIVES. The eclipse assertions below test the
// grader through its verdict; this one tests the quantity the verdict is made
// of, against the reference every implementation of it is checked against
// (Meeus, example 47.a: 1992 April 12.0 TD gives beta = -3.229126 deg).
// The four-term series answered -3.3525, an error of 0.12 deg — the failure
// was in the LATITUDE and had no witness of its own, so the verdicts were the
// only place it could show, one grade at a time.
static void test_lune_latitude_reference(void) {
    // 1992-04-12.0 TD. TD - UTC was ~58 s then; the Moon's latitude moves at
    // most 0.2 deg/h, so the difference is 0.003 deg and does not reach the
    // tolerance below.
    const MoonInfo m = moonInfo(jdFromUtc(1992, 4, 12, 0, 0, 0));
    TEST_ASSERT_TRUE(fabs(m.betaDeg - (-3.229126)) < 0.02);
}

static void test_eclipse_lunaire(void) {
    // 2025-09-07, greatest 18:11 UTC, umbral magnitude 1.36: comfortably total.
    MoonInfo m = moonInfo(jdFromUtc(2025, 9, 7, 18, 11, 0));
    TEST_ASSERT_EQUAL_UINT8(2, m.eclipse);
    // 2026-03-03, greatest 11:33 UTC, magnitude 1.15: TOTAL, and asserted as
    // total. This line used to accept "umbral or better" and excuse itself
    // with "against a truncated series" — so the gate stayed green while the
    // grader answered "partial" here, and the excuse was the bug's alibi. A
    // test written around a known-wrong answer protects it.
    m = moonInfo(jdFromUtc(2026, 3, 3, 11, 33, 0));
    TEST_ASSERT_EQUAL_UINT8(2, m.eclipse);
    // 2026-08-28, greatest 04:13 UTC, magnitude 0.93: PARTIAL, and the case
    // that fails in the OTHER direction — a deep partial the four-term series
    // called total, which is how it would have painted the whole disc copper
    // for an eclipse that never covers it. Both grades need a witness or the
    // discriminant is only tested from one side.
    m = moonInfo(jdFromUtc(2026, 8, 28, 4, 13, 0));
    TEST_ASSERT_EQUAL_UINT8(1, m.eclipse);
    // 2023-10-28, magnitude 0.12: a SHALLOW partial, the kind the short
    // series missed entirely (it read beta = 1.10 against the true 0.90 and
    // reported a clear full moon). Three of the eight umbral eclipses of
    // 2023-2028 failed this way, all of them shallow ones.
    m = moonInfo(jdFromUtc(2023, 10, 28, 20, 14, 0));
    TEST_ASSERT_EQUAL_UINT8(1, m.eclipse);
    // One week later: no eclipse, whatever the phase.
    m = moonInfo(jdFromUtc(2025, 9, 14, 18, 0, 0));
    TEST_ASSERT_EQUAL_UINT8(0, m.eclipse);
    // THE discriminant: the NEXT full moon (2025-10-07 03:48 UTC) has no
    // eclipse - the Moon passes ~2.5 deg above the shadow. Full, yet clear.
    m = moonInfo(jdFromUtc(2025, 10, 7, 3, 48, 0));
    TEST_ASSERT_TRUE(m.illum > 0.98);
    TEST_ASSERT_EQUAL_UINT8(0, m.eclipse);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_date_julienne);
    RUN_TEST(test_gmst);
    RUN_TEST(test_soleil);
    RUN_TEST(test_altitude_solaire);
    RUN_TEST(test_lune_phases);
    RUN_TEST(test_lune_distance_et_age);
    RUN_TEST(test_planetes);
    RUN_TEST(test_geodesie_aller_retour);
    RUN_TEST(test_angles_de_vue);
    RUN_TEST(test_ombre_terrestre);
    RUN_TEST(test_lune_latitude_reference);
    RUN_TEST(test_eclipse_lunaire);
    RUN_TEST(test_jours_jusqu_a_la_phase);
    RUN_TEST(test_cote_eclaire_selon_l_hemisphere);
    RUN_TEST(test_lever_coucher);
    RUN_TEST(test_recherche_de_passages);
    RUN_TEST(test_trace_au_sol_iss);
    RUN_TEST(test_passage_deja_commence_est_ignore);
    RUN_TEST(test_terre_et_soleil_sont_un_seul_vecteur);
    RUN_TEST(test_triangle_helio_geo);
    UNITY_END();
    return 0;
}
