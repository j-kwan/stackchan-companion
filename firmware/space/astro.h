#pragma once
// =============================================================================
// astro.h — space: astronomy for the five views (PURE)
// =============================================================================
// Sun, Moon, naked-eye planets, geodesy, topocentric look angles and the ISS
// pass search. No Arduino, no M5, no allocation — natively tested
// (`pio test -e native`, suite test_astro) like sgp4.h next to it.
//
// FRAME NOTE, read before touching anything here. SGP4 returns TEME (True
// Equator, Mean Equinox of date); the Sun/Moon/planet routines below return
// equatorial coordinates of DATE. The two differ by the equation of the
// equinoxes — at most ~1.1 arcsecond of right ascension. Every consumer in
// this bin displays degrees (an azimuth chip, a 3 px dot on a 300 px map, a
// rise time to the minute), so they are treated as the same frame and the
// error stays four orders of magnitude below the smallest thing drawn. Do NOT
// carry that shortcut into anything that points a telescope.
//
// PRECISION BUDGET, stated so nobody "improves" the wrong term:
//   Sun        ~0.01 deg   (Meeus low precision)
//   Moon       ~0.05 deg   (Meeus abridged series, 7 longitude terms)
//   Planets    ~0.05 deg   (Schlyter's simplified Keplerian elements)
// A pass rise time is therefore good to a few seconds, and a Moon phase
// percentage to well under one point. That is the resolution of the SCREEN.
//
// SPACE.md trap 1: every routine here is `double` and therefore SOFTWARE
// floating point on the ESP32-S3. Call them on demand (1 Hz, or sliced), never
// once per frame.
// =============================================================================

#include "sgp4.h"

namespace spc {

inline constexpr double RAD2DEG_D = 180.0 / SGP4_PI;
inline constexpr double J2000     = 2451545.0;
inline constexpr double AU_KM     = 149597870.7;
inline constexpr double SYNODIC   = 29.530588853;    // mean lunar month, days
inline constexpr double EARTH_A   = 6378.137;        // WGS-84 semi-major, km
inline constexpr double EARTH_F   = 1.0 / 298.257223563;

// ---- small helpers ---------------------------------------------------------
inline double wrap360(double d) { d = fmod(d, 360.0); return d < 0 ? d + 360.0 : d; }
inline double wrap180(double d) { d = wrap360(d); return d > 180.0 ? d - 360.0 : d; }
inline double wrap2pi(double r) { r = fmod(r, SGP4_TAU); return r < 0 ? r + SGP4_TAU : r; }
inline double sinD(double d) { return sin(d * DEG2RAD_D); }
inline double cosD(double d) { return cos(d * DEG2RAD_D); }

// Calendar UTC → Julian date. Delegates the date arithmetic to sgp4.h's
// julianDate so there is ONE implementation in the bin (the A2.23 discipline
// the YAML parsers were merged under).
inline double jdFromUtc(int y, int mo, int d, int h, int mi, double s) {
    return julianDate(y, mo, d + (h + mi / 60.0 + s / 3600.0) / 24.0);
}

// Julian date → calendar UTC (Meeus ch. 7). Needed to LABEL a computed
// instant: a pass rise time is produced as a JD and has to be shown as hh:mm.
struct CalDate { int year, month, day, hour, minute; double second; };
inline CalDate calFromJd(double jd) {
    CalDate c{};
    const double z0 = jd + 0.5;
    long   Z = (long)floor(z0);
    double F = z0 - (double)Z;
    long A = Z;
    if (Z >= 2299161) {
        const long alpha = (long)floor(((double)Z - 1867216.25) / 36524.25);
        A = Z + 1 + alpha - alpha / 4;
    }
    const long B = A + 1524;
    const long C = (long)floor(((double)B - 122.1) / 365.25);
    const long D = (long)floor(365.25 * (double)C);
    const long E = (long)floor(((double)(B - D)) / 30.6001);
    const double dayF = (double)(B - D) - floor(30.6001 * (double)E) + F;
    c.day   = (int)floor(dayF);
    c.month = (int)((E < 14) ? E - 1 : E - 13);
    c.year  = (int)((c.month > 2) ? C - 4716 : C - 4715);
    double frac = (dayF - c.day) * 24.0;
    c.hour = (int)floor(frac);
    frac = (frac - c.hour) * 60.0;
    c.minute = (int)floor(frac);
    c.second = (frac - c.minute) * 60.0;
    // Rounding can push 59.9999 s to a 60th second; normalise rather than
    // ever print "10:59:60".
    if (c.second >= 59.9995) { c.second = 0.0; if (++c.minute >= 60) { c.minute = 0; c.hour++; } }
    return c;
}

// Greenwich Mean Sidereal Time, radians. The hinge of every Earth-fixed
// conversion below: an error here rotates the whole world under the satellite.
inline double gmstRad(double jd) {
    const double d = jd - J2000;
    const double T = d / 36525.0;
    double g = 280.46061837 + 360.98564736629 * d
             + 0.000387933 * T * T - T * T * T / 38710000.0;
    return wrap2pi(wrap360(g) * DEG2RAD_D);
}

// =============================================================================
// Geodesy
// =============================================================================
struct Geodetic { double latDeg = 0, lonDeg = 0, altKm = 0; };

// ECI (equatorial of date, km) → geodetic latitude/longitude/altitude.
// Iterative because the Earth is an ellipsoid: the first-pass spherical
// latitude is off by up to 0.19 deg, which is 20 km of ground track.
inline Geodetic eciToGeodetic(double x, double y, double z, double jd) {
    Geodetic g;
    const double gmst = gmstRad(jd);
    g.lonDeg = wrap180((atan2(y, x) - gmst) * RAD2DEG_D);
    const double r = sqrt(x * x + y * y);
    const double e2 = EARTH_F * (2.0 - EARTH_F);
    double lat = atan2(z, r);                    // spherical first guess
    double C = 1.0;
    for (int i = 0; i < 8; i++) {                // converges in ~4
        const double sl = sin(lat);
        C = 1.0 / sqrt(1.0 - e2 * sl * sl);
        lat = atan2(z + EARTH_A * C * e2 * sl, r);
    }
    g.latDeg = lat * RAD2DEG_D;
    // cos(lat) → 0 at the poles: use the polar form there instead of dividing
    // by a vanishing cosine (the satellite DOES cross high latitudes).
    if (fabs(g.latDeg) < 89.5) g.altKm = r / cos(lat) - EARTH_A * C;
    else                       g.altKm = fabs(z) - EARTH_A * (1.0 - EARTH_F);
    return g;
}

// Observer geodetic → ECI (equatorial of date, km), same frame as SGP4's out.
inline void observerEci(double latDeg, double lonDeg, double altKm, double jd,
                        double& x, double& y, double& z) {
    const double lat = latDeg * DEG2RAD_D;
    const double theta = gmstRad(jd) + lonDeg * DEG2RAD_D;   // local sidereal
    const double e2 = EARTH_F * (2.0 - EARTH_F);
    const double sl = sin(lat);
    const double C = 1.0 / sqrt(1.0 - e2 * sl * sl);
    const double S = C * (1.0 - e2);
    const double rc = (EARTH_A * C + altKm) * cos(lat);
    x = rc * cos(theta);
    y = rc * sin(theta);
    z = (EARTH_A * S + altKm) * sl;
}

// ---- look angles -----------------------------------------------------------
struct LookAngle { double azDeg = 0, elDeg = 0, rangeKm = 0; };

// Topocentric az/el/range of an ECI point, via the classic SEZ rotation.
inline LookAngle lookAngle(double sx, double sy, double sz, double jd,
                           double obsLat, double obsLon, double obsAltKm = 0.0) {
    double ox, oy, oz;
    observerEci(obsLat, obsLon, obsAltKm, jd, ox, oy, oz);
    const double rx = sx - ox, ry = sy - oy, rz = sz - oz;
    const double lat = obsLat * DEG2RAD_D;
    const double theta = gmstRad(jd) + obsLon * DEG2RAD_D;
    const double sinLat = sin(lat), cosLat = cos(lat);
    const double sinT = sin(theta), cosT = cos(theta);
    // South-East-Zenith
    const double s =  sinLat * cosT * rx + sinLat * sinT * ry - cosLat * rz;
    const double e = -sinT * rx + cosT * ry;
    const double zz = cosLat * cosT * rx + cosLat * sinT * ry + sinLat * rz;
    LookAngle la;
    la.rangeKm = sqrt(rx * rx + ry * ry + rz * rz);
    la.elDeg = asin(zz / la.rangeKm) * RAD2DEG_D;
    la.azDeg = wrap360(atan2(-e, s) * RAD2DEG_D + 180.0);   // 0 = North, E = 90
    return la;
}

// Az/el of a distant object given its equatorial coordinates (RA/Dec in
// degrees). Distant = parallax ignored, true for everything but the Moon,
// whose ~1 deg parallax is below this bin's display resolution anyway.
inline LookAngle altAzFromRaDec(double raDeg, double decDeg, double jd,
                                double obsLat, double obsLon) {
    const double lstDeg = gmstRad(jd) * RAD2DEG_D + obsLon;
    const double ha = (lstDeg - raDeg) * DEG2RAD_D;
    const double lat = obsLat * DEG2RAD_D, dec = decDeg * DEG2RAD_D;
    LookAngle la;
    la.elDeg = asin(sin(lat) * sin(dec) +
                    cos(lat) * cos(dec) * cos(ha)) * RAD2DEG_D;
    la.azDeg = wrap360(atan2(-sin(ha) * cos(dec),
                             cos(lat) * sin(dec) -
                             sin(lat) * cos(dec) * cos(ha)) * RAD2DEG_D);
    return la;
}

// Compass letters for an azimuth — a heading is read, not computed, on a
// 320 px screen (the radar's precedent).
inline const char* compass16(double azDeg) {
    static const char* P[16] = { "N","NNE","NE","ENE","E","ESE","SE","SSE",
                                 "S","SSW","SW","WSW","W","WNW","NW","NNW" };
    return P[(int)(wrap360(azDeg) / 22.5 + 0.5) % 16];
}

// =============================================================================
// The Sun — needed twice over: for twilight, and for every illumination test
// =============================================================================
struct Equatorial { double raDeg = 0, decDeg = 0, distAu = 0, eclLonDeg = 0; };

inline Equatorial sunPosition(double jd) {
    const double n = jd - J2000;
    const double L = wrap360(280.460 + 0.9856474 * n);
    const double g = wrap360(357.528 + 0.9856003 * n);
    const double lambda = wrap360(L + 1.915 * sinD(g) + 0.020 * sinD(2 * g));
    const double eps = 23.439 - 0.0000004 * n;
    Equatorial e;
    e.eclLonDeg = lambda;
    e.raDeg  = wrap360(atan2(cosD(eps) * sinD(lambda), cosD(lambda)) * RAD2DEG_D);
    e.decDeg = asin(sinD(eps) * sinD(lambda)) * RAD2DEG_D;
    e.distAu = 1.00014 - 0.01671 * cosD(g) - 0.00014 * cosD(2 * g);
    return e;
}

// Solar altitude for an observer. The threshold that matters for this bin is
// -6 deg (civil twilight): above it the sky is too bright for a satellite
// pass to be visible, and the Sky view calls its planets "day".
inline double sunAltDeg(double jd, double obsLat, double obsLon) {
    const Equatorial s = sunPosition(jd);
    return altAzFromRaDec(s.raDeg, s.decDeg, jd, obsLat, obsLon).elDeg;
}

// Unit vector to the Sun in the same equatorial frame as the SGP4 output.
inline void sunUnitVector(double jd, double& ux, double& uy, double& uz) {
    const Equatorial s = sunPosition(jd);
    const double ra = s.raDeg * DEG2RAD_D, dec = s.decDeg * DEG2RAD_D;
    ux = cos(dec) * cos(ra);
    uy = cos(dec) * sin(ra);
    uz = sin(dec);
}

// Is a satellite in sunlight? Cylindrical Earth-shadow model: exact enough
// for a VISIBLE/radio verdict (the penumbra it ignores is a few seconds of a
// pass), and it is one dot product plus one norm instead of a cone geometry.
inline bool isSunlit(double sx, double sy, double sz, double jd) {
    double ux, uy, uz;
    sunUnitVector(jd, ux, uy, uz);
    const double proj = sx * ux + sy * uy + sz * uz;
    if (proj > 0.0) return true;                 // sunward hemisphere
    const double px = sx - proj * ux, py = sy - proj * uy, pz = sz - proj * uz;
    return sqrt(px * px + py * py + pz * pz) > EARTH_A;
}

// =============================================================================
// The Moon
// =============================================================================
struct MoonInfo {
    double raDeg = 0, decDeg = 0;
    double distKm = 0;
    double illum = 0;        // lit fraction, 0..1
    double ageDays = 0;      // days since the last new Moon
    double elongDeg = 0;     // 0 new, 180 full — drives the terminator drawing
    double phaseAngle = 0;   // Sun-Moon-Earth angle, degrees
    double betaDeg = 0;      // ecliptic latitude — what makes eclipses RARE
    bool   waxing = false;
    // Umbral lunar eclipse, geocentric: 0 none, 1 partial, 2 total. Solar
    // eclipses are deliberately NOT flagged: they are a narrow ground track,
    // and a geocentric figure claiming one for THIS observer would usually
    // be wrong.
    uint8_t eclipse = 0;
};

// WHICH SIDE OF THE DISC IS LIT, as seen from the ground.
//
// The phase is a fact about the Sun and the Moon; which side of it you SEE lit
// is a fact about where you stand. A northern observer sees a waxing Moon lit
// on the RIGHT. A southern observer sees the same Moon rotated roughly half a
// turn — lit on the LEFT. Drawing the northern convention everywhere is wrong
// for half the planet, and wrong in the one way nobody reports as a bug: the
// picture still looks like a Moon.
//
// This is the CONVENTIONAL simplification, not the full truth. The terminator
// really tilts continuously through the night (the parallactic angle), so at
// mid-latitudes a rising crescent is lit from below rather than from the side.
// Rendering that means rotating the disc; this returns the side, which is what
// a scanline drawing can honour, and it is right at the two moments a phase is
// usually checked — near the meridian, and on the strip where only the SHAPE
// carries meaning.
//
// The equator is a genuine tie. It resolves north, arbitrarily and only there.
inline bool moonLitOnRight(bool waxing, double observerLatDeg) {
    return waxing != (observerLatDeg < 0.0);
}

// DAYS until the Moon next reaches `targetElongDeg` (0 = new, 180 = full),
// from its current elongation, at the MEAN synodic rate. "When is the full
// moon" is the one question a moon display gets asked and the previous layout
// never answered — the phase strip shows the order of what comes next, not how
// long it takes to get there.
//
// Mean rate, deliberately: the true rate varies with the anomaly, putting the
// answer off by up to ~10 hours at the quarters. For a figure printed in whole
// days that error is invisible, and the exact answer would need a root search
// over moonInfo — 130 series evaluations to refine a number nobody reads to
// the hour. Same trade as the strip's thumbnails, which use the mean relation
// for their shading too.
inline double moonDaysToElong(double elongDeg, double targetElongDeg) {
    return wrap360(targetElongDeg - elongDeg) / 360.0 * SYNODIC;
}

// Meeus ch. 47, abridged: the seven largest longitude terms, EIGHT in
// latitude, four in distance.
//
// The abridgement is not uniform, on purpose. Longitude and distance only ever
// place the disc and name the phase, where 0.05 deg is three orders below the
// 140 px the MOON view draws — but latitude also decides whether the Moon
// enters Earth's shadow, and that verdict turns on a quarter of a degree. Four
// terms were enough for the drawing and wrong for the eclipse; the count each
// series carries follows what depends on it, which is why they differ.
inline MoonInfo moonInfo(double jd) {
    const double T = (jd - J2000) / 36525.0;
    const double Lp = wrap360(218.316 + 481267.8813 * T);     // mean longitude
    const double M  = wrap360(357.529 + 35999.0503 * T);      // Sun anomaly
    const double Mp = wrap360(134.963 + 477198.8676 * T);     // Moon anomaly
    const double D  = wrap360(297.850 + 445267.1115 * T);     // elongation
    const double F  = wrap360( 93.272 + 483202.0175 * T);     // arg. latitude

    const double lambda = wrap360(Lp
        + 6.289 * sinD(Mp)      + 1.274 * sinD(2 * D - Mp)
        + 0.658 * sinD(2 * D)   + 0.214 * sinD(2 * Mp)
        - 0.186 * sinD(M)       - 0.114 * sinD(2 * F));
    // LATITUDE: EIGHT terms, and the count is not cosmetic. With four it
    // carried an error of 0.12 deg (measured against Meeus example 47.a,
    // 1992 April 12.0 TD: -3.3525 against the true -3.2291) — and the
    // eclipse grader below discriminates total from partial on a quantity
    // whose whole range is about 0.25 deg. Graded against the NASA canon,
    // the four-term series called FIVE of the eight umbral eclipses of
    // 2023-2028 wrong: three missed entirely, one partial announced total,
    // one total announced partial. With these eight, all eight are right.
    //
    // The 2D-F term also had the WRONG SIGN. Meeus table 47.B gives
    // +173237e-6 for the argument (D=2, F=-1); it was subtracted, which on
    // its own is a swing of twice 0.173 deg.
    const double beta =
          5.128122 * sinD(F)             + 0.280602 * sinD(Mp + F)
        + 0.277693 * sinD(Mp - F)        + 0.173237 * sinD(2 * D - F)
        + 0.055413 * sinD(2 * D - Mp + F) + 0.046271 * sinD(2 * D - Mp - F)
        + 0.032573 * sinD(2 * D + F)     + 0.017198 * sinD(2 * Mp + F);
    const double dist = 385001.0
        - 20905.0 * cosD(Mp)    - 3699.0 * cosD(2 * D - Mp)
        -  2956.0 * cosD(2 * D) -  570.0 * cosD(2 * Mp);

    const double eps = 23.439 - 0.0000004 * (jd - J2000);
    const double sl = sinD(lambda), cl = cosD(lambda);
    const double sb = sinD(beta),   cb = cosD(beta);
    MoonInfo mi;
    mi.distKm = dist;
    mi.raDeg  = wrap360(atan2(sl * cosD(eps) - (sb / cb) * sinD(eps), cl) * RAD2DEG_D);
    mi.decDeg = asin(sb * cosD(eps) + cb * sinD(eps) * sl) * RAD2DEG_D;

    // Elongation from the Sun drives BOTH the phase name and the terminator.
    const Equatorial sun = sunPosition(jd);
    mi.elongDeg = wrap360(lambda - sun.eclLonDeg);
    mi.waxing   = mi.elongDeg < 180.0;
    mi.ageDays  = mi.elongDeg / 360.0 * SYNODIC;

    // Illuminated fraction from the true phase angle (Meeus 48.2-48.3), not
    // from the elongation directly: at quarter the two differ by ~0.2 deg,
    // which is a visible tenth of a percent on the readout.
    const double psi = acos(cb * cosD(lambda - sun.eclLonDeg));
    const double sunKm = sun.distAu * AU_KM;
    mi.phaseAngle = atan2(sunKm * sin(psi), dist - sunKm * cos(psi)) * RAD2DEG_D;
    mi.illum = (1.0 + cosD(mi.phaseAngle)) * 0.5;
    mi.betaDeg = beta;

    // ---- umbral lunar eclipse -------------------------------------------
    // The Moon is eclipsed when it stands inside Earth's umbra: angular
    // separation from the ANTI-SOLAR point below the umbra radius. At these
    // small angles the separation is the hypotenuse of "how far past full"
    // (elongation - 180) and "how far off the ecliptic" (beta) — beta is the
    // whole reason full moons usually miss: without it this would flag an
    // eclipse at every opposition.
    // Radii per Meeus ch. 54 (the 1.02 factor covers Earth's atmosphere):
    // umbra = 1.02 (pi_moon - sd_sun + pi_sun), all functions of the two
    // distances already in hand. Thresholds: total when the whole disc fits
    // inside, partial when it merely overlaps.
    {
        const double dLam   = fabs(mi.elongDeg - 180.0);
        const double sep    = sqrt(dLam * dLam + beta * beta);
        const double sdMoon = asin(1737.4 / dist) * RAD2DEG_D;
        const double piMoon = asin(6378.14 / dist) * RAD2DEG_D;
        const double sdSun  = 0.267 / sun.distAu;      // scales with distance
        const double piSun  = 0.00244;
        const double umbra  = 1.02 * (piMoon - sdSun + piSun);
        if      (sep < umbra - sdMoon) mi.eclipse = 2;
        else if (sep < umbra + sdMoon) mi.eclipse = 1;
    }
    return mi;
}

// Phase name from the elongation. Eight bins of 45 deg centred on the named
// phases, so "first quarter" covers 90 +/- 22.5 as an observer would say it.
enum class MoonPhase : uint8_t {
    New, WaxingCrescent, FirstQuarter, WaxingGibbous,
    Full, WaningGibbous, LastQuarter, WaningCrescent
};
inline MoonPhase moonPhaseOf(double elongDeg) {
    const int k = (int)(wrap360(elongDeg) / 45.0 + 0.5) % 8;
    return (MoonPhase)k;
}

// =============================================================================
// The planets (Schlyter's simplified elements)
// =============================================================================
// Mercury through Neptune. Uranus and Neptune were left out of the first
// version on the grounds that a row saying "up" for something nobody can see
// is a lie — but the honest fix is not to hide them, it is to say WHAT they
// need (user 08-04: "so we can refer to them later with an amateur
// telescope"). Uranus is magnitude ~5.7, a binocular object; Neptune ~7.8,
// a telescope one. `planetAid` carries that, so the screen stays truthful
// while still pointing you at them.
enum class Planet : uint8_t {
    Mercury, Venus, Mars, Jupiter, Saturn, Uranus, Neptune, COUNT
};

inline const char* planetName(Planet p) {
    switch (p) {
        case Planet::Mercury: return "Mercury";
        case Planet::Venus:   return "Venus";
        case Planet::Mars:    return "Mars";
        case Planet::Jupiter: return "Jupiter";
        case Planet::Saturn:  return "Saturn";
        case Planet::Uranus:  return "Uranus";
        default:              return "Neptune";
    }
}

// What it takes to actually SEE it: 0 = naked eye, 1 = binoculars,
// 2 = telescope. Fixed per planet rather than derived from a live magnitude:
// the brightness of Uranus varies by a tenth of a magnitude over its orbit and
// never crosses into naked-eye territory from a real sky, so a computed value
// would add arithmetic without changing a single answer.
inline uint8_t planetAid(Planet p) {
    if (p == Planet::Uranus)  return 1;
    if (p == Planet::Neptune) return 2;
    return 0;
}

namespace detail {
// Kepler's equation in DEGREES (Schlyter's convention), Newton iteration.
inline double keplerDeg(double Mdeg, double e) {
    double E = Mdeg + RAD2DEG_D * e * sinD(Mdeg) * (1.0 + e * cosD(Mdeg));
    for (int i = 0; i < 12; i++) {
        const double dE = (E - RAD2DEG_D * e * sinD(E) - Mdeg) /
                          (1.0 - e * cosD(E));
        E -= dE;
        if (fabs(dE) < 1e-9) break;
    }
    return E;
}
// Heliocentric ecliptic rectangular coordinates from a set of elements.
inline void helioXyz(double N, double i, double w, double a, double e, double M,
                     double& x, double& y, double& z, double& r) {
    const double E = keplerDeg(wrap360(M), e);
    const double xv = a * (cosD(E) - e);
    const double yv = a * sqrt(1.0 - e * e) * sinD(E);
    const double v = atan2(yv, xv) * RAD2DEG_D;
    r = sqrt(xv * xv + yv * yv);
    const double vw = v + w;
    x = r * (cosD(N) * cosD(vw) - sinD(N) * sinD(vw) * cosD(i));
    y = r * (sinD(N) * cosD(vw) + cosD(N) * sinD(vw) * cosD(i));
    z = r * sinD(vw) * sinD(i);
}
} // namespace detail

// Heliocentric ecliptic longitude and radius — what an orbital, top-down view
// of the solar system needs, and what `planetPosition` computes internally and
// then throws away on its way to a geocentric answer. Exposed rather than
// recomputed in the drawing code: two copies of Schlyter's tables is exactly
// the divergence the YAML parsers were merged to end (A2.23).
struct Helio { double lonDeg = 0, rAu = 0; };

// Earth's own heliocentric position, from the Sun's geocentric one: they are
// the same vector with opposite signs, so no second element set is needed.
inline Helio earthHelio(double jd) {
    const double d = jd - 2451543.5;
    const double ws = 282.9404 + 4.70935e-5 * d;
    const double es = 0.016709 - 1.151e-9  * d;
    const double Ms = wrap360(356.0470 + 0.9856002585 * d);
    const double Es = detail::keplerDeg(Ms, es);
    const double xv = cosD(Es) - es;
    const double yv = sqrt(1.0 - es * es) * sinD(Es);
    Helio h;
    h.rAu    = sqrt(xv * xv + yv * yv);
    h.lonDeg = wrap360(atan2(yv, xv) * RAD2DEG_D + ws + 180.0);   // opposite
    return h;
}

// Forward declaration: the element table lives in planetPosition below, and
// planetHelio reads it through the same switch — see the definition after it.
inline Helio planetHelio(Planet p, double jd);

// Geocentric equatorial position of a planet. `d` is Schlyter's day number,
// counted from 1999-12-31 00:00 UT — NOT from J2000 noon. Mixing the two
// epochs is a half-day error, i.e. a planet half a degree off; the constant
// lives here once so no caller can get it wrong.
namespace detail {
// THE element table, in ONE place. `planetPosition` and `planetHelio` both
// read it: two copies of Schlyter's constants is precisely the "assumed twins"
// pattern that had already diverged once in this project (A2.23).
inline void planetElements(Planet p, double d, double& N, double& i, double& w,
                           double& a, double& e, double& M) {
    switch (p) {
        case Planet::Mercury:
            N =  48.3313 + 3.24587e-5 * d; i = 7.0047 + 5.00e-8  * d;
            w =  29.1241 + 1.01444e-5 * d; a = 0.387098;
            e = 0.205635 + 5.59e-10   * d; M = 168.6562 + 4.0923344368 * d; break;
        case Planet::Venus:
            N =  76.6799 + 2.46590e-5 * d; i = 3.3946 + 2.75e-8  * d;
            w =  54.8910 + 1.38374e-5 * d; a = 0.723330;
            e = 0.006773 - 1.302e-9   * d; M =  48.0052 + 1.6021302244 * d; break;
        case Planet::Mars:
            N =  49.5574 + 2.11081e-5 * d; i = 1.8497 - 1.78e-8  * d;
            w = 286.5016 + 2.92961e-5 * d; a = 1.523688;
            e = 0.093405 + 2.516e-9   * d; M =  18.6021 + 0.5240207766 * d; break;
        case Planet::Jupiter:
            N = 100.4542 + 2.76854e-5 * d; i = 1.3030 - 1.557e-7 * d;
            w = 273.8777 + 1.64505e-5 * d; a = 5.20256;
            e = 0.048498 + 4.469e-9   * d; M =  19.8950 + 0.0830853001 * d; break;
        case Planet::Saturn:
            N = 113.6634 + 2.38980e-5 * d; i = 2.4886 - 1.081e-7 * d;
            w = 339.3939 + 2.97661e-5 * d; a = 9.55475;
            e = 0.055546 - 9.499e-9   * d; M = 316.9670 + 0.0334442282 * d; break;
        // The two outer planets have a SEMI-MAJOR AXIS that drifts with the
        // epoch in Schlyter's set, unlike the five above where it is a
        // constant. Dropping the `a` term because the others do not have one
        // is a tempting tidy-up that costs thousands of kilometres a decade.
        case Planet::Uranus:
            N =  74.0005 + 1.3978e-5  * d; i = 0.7733 + 1.9e-8   * d;
            w =  96.6612 + 3.0565e-5  * d; a = 19.18171 - 1.55e-8 * d;
            e = 0.047318 + 7.45e-9    * d; M = 142.5905 + 0.011725806 * d; break;
        default: // Neptune
            N = 131.7806 + 3.0173e-5  * d; i = 1.7700 - 2.55e-7  * d;
            w = 272.8461 - 6.027e-6   * d; a = 30.05826 + 3.313e-8 * d;
            e = 0.008606 + 2.15e-9    * d; M = 260.2471 + 0.005995147 * d; break;
    }
}
} // namespace detail

inline Equatorial planetPosition(Planet p, double jd) {
    const double d = jd - 2451543.5;
    double N, i, w, a, e, M;
    detail::planetElements(p, d, N, i, w, a, e, M);
    double xh, yh, zh, rh;
    detail::helioXyz(N, i, w, a, e, M, xh, yh, zh, rh);

    // The Sun's geocentric rectangular position IS the negative of Earth's
    // heliocentric one, so adding it converts helio → geocentric in one step.
    // TAKEN FROM `earthHelio`, not recomputed: this block used to be a
    // byte-for-byte copy of it, down to a double space in one constant — the
    // "assumed twins" pattern A2.23 exists to end, and the more dangerous for
    // being invisible (no test touches either, and the planet bounds are wide
    // enough to swallow a large solar-vector error). `earthHelio` returns the
    // EARTH's longitude, which is the Sun's plus 180: subtract it back.
    const Helio eh = earthHelio(jd);
    const double lonsun = eh.lonDeg - 180.0;
    const double rs = eh.rAu;
    const double xs = rs * cosD(lonsun), ys = rs * sinD(lonsun);

    const double xg = xh + xs, yg = yh + ys, zg = zh;
    const double ecl = 23.4393 - 3.563e-7 * d;
    const double xe = xg;
    const double ye = yg * cosD(ecl) - zg * sinD(ecl);
    const double ze = yg * sinD(ecl) + zg * cosD(ecl);
    Equatorial q;
    q.raDeg  = wrap360(atan2(ye, xe) * RAD2DEG_D);
    q.decDeg = atan2(ze, sqrt(xe * xe + ye * ye)) * RAD2DEG_D;
    q.distAu = sqrt(xe * xe + ye * ye + ze * ze);
    return q;
}

inline Helio planetHelio(Planet p, double jd) {
    const double d = jd - 2451543.5;
    double N, i, w, a, e, M;
    detail::planetElements(p, d, N, i, w, a, e, M);
    double xh, yh, zh, rh;
    detail::helioXyz(N, i, w, a, e, M, xh, yh, zh, rh);
    Helio h;
    // Projected onto the ecliptic PLANE: the orbital view is top-down, and the
    // largest inclination in this set is Mercury's 7 degrees — under a pixel
    // at the scale the screen draws.
    h.rAu    = sqrt(xh * xh + yh * yh);
    h.lonDeg = wrap360(atan2(yh, xh) * RAD2DEG_D);
    return h;
}

// Semi-major axis in AU, for laying out the orbit circles. Taken at J2000 so
// the rings do not breathe from frame to frame — they are a SCALE, not a
// measurement, and the planet dot is what carries the live position.
inline double planetSemiMajorAu(Planet p) {
    double N, i, w, a, e, M;
    detail::planetElements(p, 0.0, N, i, w, a, e, M);
    return a;
}

// =============================================================================
// Rise / set of a slow object (Moon, planet)
// =============================================================================
// Scan-then-bisect over a bounded window. Deliberately generic and dumb: these
// objects move less than a degree an hour, a 20-minute step cannot miss a
// crossing, and the bisection converges to the second in 12 halvings. `h0` is
// the horizon altitude — 0 for a point source, -0.833 for a body whose limb
// and refraction matter.
struct RiseSet { double riseJd = 0, setJd = 0; bool hasRise = false, hasSet = false; };

template <typename AltFn>
inline RiseSet findRiseSet(AltFn alt, double jdStart, double hours, double h0) {
    RiseSet rs;
    const double step = 20.0 / 1440.0;               // 20 minutes
    const double end = jdStart + hours / 24.0;
    double t0 = jdStart, a0 = alt(t0) - h0;
    for (double t1 = t0 + step; t1 <= end; t1 += step) {
        const double a1 = alt(t1) - h0;
        if ((a0 < 0.0) != (a1 < 0.0)) {              // a crossing is bracketed
            double lo = t0, hi = t1;
            for (int k = 0; k < 14; k++) {           // ~0.1 s
                const double mid = 0.5 * (lo + hi);
                if (((alt(lo) - h0) < 0.0) != ((alt(mid) - h0) < 0.0)) hi = mid;
                else lo = mid;
            }
            const double cross = 0.5 * (lo + hi);
            if (a1 > 0.0) { if (!rs.hasRise) { rs.riseJd = cross; rs.hasRise = true; } }
            else          { if (!rs.hasSet)  { rs.setJd  = cross; rs.hasSet  = true; } }
            if (rs.hasRise && rs.hasSet) break;
        }
        t0 = t1; a0 = a1;
    }
    return rs;
}

// =============================================================================
// ISS pass prediction
// =============================================================================
// SPACE.md trap 2: a 48 h search at a 30 s step is thousands of SGP4 calls
// in software double — hundreds of milliseconds, which would blow the 33 ms
// frame budget (A2.22) and starve touch polling if it ran in one go. So the
// search is a SLICED state machine: loop() calls `step(N)` with a small budget
// each pass and asks `done()`. Nothing here allocates or blocks.
struct Pass {
    double riseJd = 0, maxJd = 0, setJd = 0;
    double riseAz = 0, maxEl = 0, setAz = 0;
    bool   visible = false;      // sunlit satellite AND observer in the dark
};

class PassFinder {
public:
    static constexpr int MAX_PASSES = 8;

    // `minElDeg`: a pass is only worth listing above it (10 deg is the usual
    // amateur threshold — below that the satellite is in the roof line).
    void begin(const Sgp4* prop, double jdStart, double hours,
               double obsLat, double obsLon, double obsAltKm = 0.0,
               double minElDeg = 10.0, double stepSec = 30.0) {
        _prop = prop; _jd = jdStart; _start = jdStart; _lat = obsLat; _lon = obsLon;
        _alt = obsAltKm; _minEl = minElDeg;
        _end = jdStart + hours / 24.0;
        _step = stepSec / 86400.0;
        _count = 0; _inPass = false; _first = true; _skipping = false;
        _done = (prop == nullptr || !prop->ready());
        _cur = Pass{};
        _prevEl = -90.0;
    }

    // Advance the search by at most `budget` samples. Returns true when the
    // whole window has been covered.
    bool step(int budget) {
        if (_done) return true;
        for (int i = 0; i < budget && !_done; i++) {
            const StateVector sv = _prop->propagateJd(_jd);
            double el = -90.0;
            LookAngle la{};
            if (sv.ok) {
                la = lookAngle(sv.x, sv.y, sv.z, _jd, _lat, _lon, _alt);
                el = la.elDeg;
            }
            if (_skipping) {
                if (el <= 0.0) _skipping = false;
                _jd += _step;
                if (_jd > _end) _done = true;
                continue;
            }
            if (!_inPass && el > 0.0) {              // horizon crossing: rise
                // ...unless the satellite was ALREADY up on the very first
                // sample. Then this is not a rise, it is the middle of a pass
                // we joined late — and recording jdStart as the rise time
                // publishes "rises now, bearing wherever it happens to be" as
                // fact, which the table prints and the polar chart draws an
                // arc from. `_prevEl` existed for this test and was never
                // read; the guard is the whole reason it is sampled.
                if (_first) { _first = false; _skipping = true; _jd += _step; continue; }
                _inPass = true;
                _cur = Pass{};
                _cur.riseJd = _jd; _cur.riseAz = la.azDeg;
                _cur.maxEl = el;   _cur.maxJd  = _jd;
            } else if (_inPass) {
                if (el > _cur.maxEl) { _cur.maxEl = el; _cur.maxJd = _jd; }
                // A pass counts as VISIBLE if at any sampled instant the
                // satellite is sunlit while the observer's sky is dark
                // (SPACE.md trap 5) — both conditions, same instant.
                if (!_cur.visible && sv.ok && el > _minEl &&
                    isSunlit(sv.x, sv.y, sv.z, _jd) &&
                    sunAltDeg(_jd, _lat, _lon) < -6.0)
                    _cur.visible = true;
                if (el <= 0.0) {                     // set
                    _cur.setJd = _jd; _cur.setAz = la.azDeg;
                    _inPass = false;
                    if (_cur.maxEl >= _minEl && _count < MAX_PASSES)
                        _passes[_count++] = _cur;
                }
            }
            if (_skipping && el <= 0.0) _skipping = false;   // partial pass over
            _first = false;
            _prevEl = el;
            _jd += _step;
            if (_jd > _end || _count >= MAX_PASSES) _done = true;
        }
        return _done;
    }

    bool  done()  const { return _done; }
    int   count() const { return _count; }
    const Pass& at(int i) const { return _passes[i]; }
    // Progress 0..1 — the search spans several seconds of wall time and the
    // view shows a bar rather than freezing on "computing".
    double progress() const {
        if (_done) return 1.0;
        const double span = _end - _start;
        if (span <= 0.0) return 1.0;
        const double p = (_jd - _start) / span;
        return p < 0.0 ? 0.0 : (p > 1.0 ? 1.0 : p);
    }

private:
    const Sgp4* _prop = nullptr;
    double _jd = 0, _start = 0, _end = 0, _step = 0, _lat = 0, _lon = 0, _alt = 0;
    double _minEl = 10.0, _prevEl = -90.0;
    Pass   _passes[MAX_PASSES];
    Pass   _cur;
    int    _count = 0;
    bool   _inPass = false, _done = true;
    bool   _first = true;      // is this the very first sample?
    bool   _skipping = false;  // discarding a pass we joined late
};

} // namespace spc
