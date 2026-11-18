#pragma once
// =============================================================================
// sgp4.h — space: TLE parsing + SGP4 near-Earth propagator (PURE)
// =============================================================================
// No Arduino, no M5, no allocation: <math.h> and <string.h> only, so the whole
// thing is NATIVELY TESTABLE (`pio test -e native`, suite test_sgp4) exactly
// like the companion's engine/ and the radar's geo.h. This is where the bin's
// correctness lives — a wrong propagator draws a confident dot in the wrong
// ocean — so it is the part that gets pinned by the canonical Spacetrack
// Report No. 3 verification vector rather than by eyeballing a map.
//
// SCOPE: the NEAR-EARTH model only (orbital period < 225 min). That covers the
// ISS (92 min) and every LEO satellite this bin will ever track. Deep-space
// (SDP4: lunar/solar resonances, 12 h and geostationary orbits) is DELIBERATELY
// absent — it is three times this file for satellites we do not display, and
// `Tle::isDeepSpace()` reports the case so the caller refuses instead of
// drawing nonsense.
//
// SPACE.md trap 1: `double` is SOFTWARE-emulated on the ESP32-S3 (its FPU
// is single-precision only) and SGP4 genuinely needs the mantissa. So callers
// propagate ON DEMAND — 1 Hz for the live view, sliced batches for the pass
// search — and NEVER once per frame (A2.22 budget: 33 ms).
//
// Reference: Hoots & Roehrich, "Models for Propagation of NORAD Element Sets"
// (Spacetrack Report No. 3, 1980), WGS-72 constants as the TLEs assume.
// =============================================================================

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>

namespace spc {

// ---- WGS-72, the geodetic model the element sets are FITTED to --------------
// Using WGS-84 numbers here is a classic silent error: the TLEs are generated
// against WGS-72, so "improving" the constants degrades the result.
// NAMES, and why they are ugly: `PI` and `TWO_PI` are MACROS in the Arduino
// core (cores/esp32/Arduino.h), so `constexpr double TWO_PI` expands to
// `constexpr double 6.28...` and the compiler points at Arduino.h, not at us.
// The prefix is what keeps these headers includable from a bin AND from a
// native test.
inline constexpr double SGP4_PI   = 3.14159265358979323846;
inline constexpr double SGP4_TAU  = 2.0 * SGP4_PI;
inline constexpr double DEG2RAD_D = SGP4_PI / 180.0;
inline constexpr double XKMPER    = 6378.135;          // Earth equatorial radius, km
inline constexpr double XKE       = 0.0743669161;      // sqrt(GM) in er^1.5/min
inline constexpr double CK2       = 5.413080e-4;       // 0.5 * J2 * aE^2
inline constexpr double CK4       = 0.62098875e-6;     // -0.375 * J4 * aE^4
inline constexpr double QOMS2T    = 1.88027916e-9;     // (qo - s)^4, er^4
inline constexpr double S_CONST   = 1.01222928;        // s = aE + 78/XKMPER
inline constexpr double XJ3       = -0.253881e-5;      // J3 zonal harmonic
inline constexpr double A3OVK2    = -XJ3 / CK2;        // -J3/k2 * aE^3 (> 0)
inline constexpr double MIN_PER_DAY = 1440.0;

// =============================================================================
// TLE — the two-line element set
// =============================================================================
struct Tle {
    char     name[25] = "";     // optional line 0
    uint32_t norad    = 0;
    double   epochJd  = 0.0;    // Julian date of the epoch (UTC)
    double   incl     = 0.0;    // rad
    double   raan     = 0.0;    // rad
    double   ecc      = 0.0;
    double   argp     = 0.0;    // rad
    double   ma       = 0.0;    // mean anomaly, rad
    double   no       = 0.0;    // mean motion, rad/min
    double   bstar    = 0.0;    // drag term, 1/er
    bool     valid    = false;

    // Deep-space regime: SGP4 alone is NOT valid here, and saying so is the
    // whole point (SPACE.md, SGP4 section — assert on period > 225 min).
    bool isDeepSpace() const { return no > 0.0 && (SGP4_TAU / no) >= 225.0; }
    double periodMin() const { return no > 0.0 ? SGP4_TAU / no : 0.0; }
};

// ---- Column slicing --------------------------------------------------------
// TLE columns are 1-indexed and FIXED WIDTH; fields may be blank-padded, and
// the exponent fields carry an ASSUMED decimal point. Everything below works
// on a fixed 69-char line, and refuses anything shorter rather than reading
// past the end (a truncated line off a flaky SD card is a realistic input).
namespace detail {

inline double slice(const char* s, int col, int len) {   // col: 1-indexed
    char buf[24];
    if (len > (int)sizeof(buf) - 1) len = (int)sizeof(buf) - 1;
    memcpy(buf, s + col - 1, (size_t)len);
    buf[len] = '\0';
    return atof(buf);
}

// The "assumed decimal point" fields: `-11606-4` means -0.11606e-4, and
// ` 66816-4` means 0.66816e-4. Mantissa sign is column 1, the exponent's sign
// is embedded in the last two characters. This encoding is where hand-rolled
// parsers usually break.
inline double sliceExp(const char* s, int col, int len) {
    char buf[16];
    if (len > (int)sizeof(buf) - 1) len = (int)sizeof(buf) - 1;
    memcpy(buf, s + col - 1, (size_t)len);
    buf[len] = '\0';
    // Split mantissa / exponent at the LAST sign that is not the leading one.
    int expPos = -1;
    for (int i = 1; i < len; i++)
        if (buf[i] == '+' || buf[i] == '-') expPos = i;
    if (expPos < 0) return 0.0;
    char mant[16];
    memcpy(mant, buf, (size_t)expPos);
    mant[expPos] = '\0';
    const int e = atoi(buf + expPos);
    // Blank mantissa (a genuinely zero drag term) must read 0, not 1e-4.
    bool anyDigit = false;
    for (int i = 0; i < expPos; i++) if (mant[i] >= '0' && mant[i] <= '9') anyDigit = true;
    if (!anyDigit) return 0.0;
    const double m = atof(mant) * 1e-5;    // 5 implied decimals
    return m * pow(10.0, (double)e);
}

// Modulo-10 checksum: digits add their value, '-' adds 1, everything else 0.
inline bool checksumOk(const char* line) {
    int sum = 0;
    for (int i = 0; i < 68; i++) {
        const char c = line[i];
        if (c >= '0' && c <= '9') sum += c - '0';
        else if (c == '-')        sum += 1;
    }
    return (sum % 10) == (line[68] - '0');
}

} // namespace detail

// Julian date from a calendar UTC instant (Gregorian, valid 1901-2099 which
// covers every TLE epoch this bin can meet).
inline double julianDate(int y, int mo, double dayFrac) {
    if (mo <= 2) { y -= 1; mo += 12; }
    const int A = y / 100;
    const int B = 2 - A + A / 4;
    return floor(365.25 * (y + 4716)) + floor(30.6001 * (mo + 1))
           + dayFrac + B - 1524.5;
}

// Day-of-year (1-based, fractional) → Julian date, the form TLE epochs use.
inline double jdFromEpoch(int year, double doy) {
    // 1 January is doy = 1.0, so the JD of "year-01-01 00:00" plus (doy - 1).
    return julianDate(year, 1, 1.0) + (doy - 1.0);
}

// ---- Parse ------------------------------------------------------------------
// `l1`/`l2` must each be at least 69 characters. `name` is optional (line 0 of
// a 3-line "TLE" as Celestrak serves it).
inline bool parseTle(const char* l1, const char* l2, Tle& out,
                     const char* name = nullptr, bool verifyChecksum = true) {
    out.valid = false;
    if (!l1 || !l2) return false;
    // 68 characters carry EVERY field; the 69th is the checksum. Celestrak
    // always sends 69, but the canonical Spacetrack Report No. 3 test case
    // prints its two lines WITHOUT the checksum digit — refusing 68 would
    // mean refusing the one vector that proves the propagator right. So the
    // data requirement is 68, and the checksum is verified when it is there.
    const size_t n1len = strlen(l1), n2len = strlen(l2);
    if (n1len < 68 || n2len < 68) return false;
    if (l1[0] != '1' || l2[0] != '2') return false;
    // PER LINE, not per pair. Written as one condition over both lengths, a
    // line 2 truncated by a power cut mid-write disabled verification on line
    // 1 TOO — and line 1 is where the epoch and the drag term live, so a
    // flipped bit there is a silently wrong orbit rather than a rejected file.
    if (verifyChecksum) {
        if (n1len >= 69 && !detail::checksumOk(l1)) return false;
        if (n2len >= 69 && !detail::checksumOk(l2)) return false;
    }

    // The two lines must describe the SAME satellite: a cache file that
    // interleaved two objects would otherwise propagate a chimera.
    const uint32_t n1 = (uint32_t)detail::slice(l1, 3, 5);
    const uint32_t n2 = (uint32_t)detail::slice(l2, 3, 5);
    if (n1 != n2) return false;
    out.norad = n1;

    if (name) {
        size_t n = strlen(name);
        while (n > 0 && (name[n - 1] == ' ' || name[n - 1] == '\r')) n--;
        if (n > sizeof(out.name) - 1) n = sizeof(out.name) - 1;
        memcpy(out.name, name, n);
        out.name[n] = '\0';
    }

    // Epoch year pivot (SPACE.md, Reading a TLE): 57-99 are 1957-1999, 00-56 are
    // 2000-2056 — Sputnik is the hinge, and 57 will mean 1957 until 2057.
    const int yy = (int)detail::slice(l1, 19, 2);
    const int year = yy < 57 ? 2000 + yy : 1900 + yy;
    out.epochJd = jdFromEpoch(year, detail::slice(l1, 21, 12));
    out.bstar   = detail::sliceExp(l1, 54, 8);

    out.incl = detail::slice(l2,  9, 8) * DEG2RAD_D;
    out.raan = detail::slice(l2, 18, 8) * DEG2RAD_D;
    out.ecc  = detail::slice(l2, 27, 7) * 1e-7;      // assumed leading "0."
    out.argp = detail::slice(l2, 35, 8) * DEG2RAD_D;
    out.ma   = detail::slice(l2, 44, 8) * DEG2RAD_D;
    out.no   = detail::slice(l2, 53, 11) * SGP4_TAU / MIN_PER_DAY;  // rev/day→rad/min

    // Physically impossible sets are refused here rather than producing NaN
    // three layers up, where the cause is invisible.
    if (out.no <= 0.0 || out.ecc < 0.0 || out.ecc >= 1.0) return false;
    out.valid = true;
    return true;
}

// =============================================================================
// The propagator
// =============================================================================
// Output is TEME-of-epoch (True Equator, Mean Equinox) — the frame SGP4
// natively works in. `astro.h` converts to geodetic lat/lon and to topocentric
// az/el; do NOT feed these coordinates to a J2000 routine without conversion.
struct StateVector {
    double x = 0, y = 0, z = 0;      // km
    double vx = 0, vy = 0, vz = 0;   // km/s
    bool   ok = false;
};

class Sgp4 {
public:
    // Initialise from a parsed element set. Returns false for a deep-space
    // object: the caller must SAY so, not silently draw a wrong orbit.
    bool init(const Tle& tle) {
        _ok = false;
        if (!tle.valid || tle.isDeepSpace()) return false;
        _t = tle;

        _cosio = cos(tle.incl);
        _sinio = sin(tle.incl);
        const double theta2 = _cosio * _cosio;
        _x3thm1 = 3.0 * theta2 - 1.0;
        _x1mth2 = 1.0 - theta2;
        _x7thm1 = 7.0 * theta2 - 1.0;
        const double eosq  = tle.ecc * tle.ecc;
        const double betao2 = 1.0 - eosq;
        const double betao  = sqrt(betao2);

        // --- Un-Kozai the mean motion: the TLE's n is a Brouwer mean value,
        // and using it directly puts the satellite kilometres off track.
        const double a1 = pow(XKE / tle.no, 2.0 / 3.0);
        const double del1 = 1.5 * CK2 * _x3thm1 / (a1 * a1 * betao * betao2);
        const double ao = a1 * (1.0 - del1 * (0.5 * (2.0 / 3.0) + del1 *
                                (1.0 + 134.0 / 81.0 * del1)));
        const double delo = 1.5 * CK2 * _x3thm1 / (ao * ao * betao * betao2);
        _xnodp = tle.no / (1.0 + delo);
        _aodp  = ao / (1.0 - delo);

        // --- Atmospheric drag fit. Below 156 km perigee the standard model
        // is re-fitted (and below 98 km it is clamped): satellites this low
        // are days from re-entry, but the branch must exist or the powers
        // below go negative and produce NaN.
        const double rp = _aodp * (1.0 - tle.ecc);        // perigee, earth radii
        _isimp = rp < (220.0 / XKMPER + 1.0);
        double s4 = S_CONST, qoms24 = QOMS2T;
        const double perigee = (rp - 1.0) * XKMPER;
        if (perigee < 156.0) {
            s4 = perigee - 78.0;
            if (perigee < 98.0) s4 = 20.0;
            const double d = (120.0 - s4) / XKMPER;
            qoms24 = d * d * d * d;
            s4 = s4 / XKMPER + 1.0;
        }
        const double pinvsq = 1.0 / (_aodp * _aodp * betao2 * betao2);
        const double tsi = 1.0 / (_aodp - s4);
        _eta = _aodp * tle.ecc * tsi;
        const double etasq = _eta * _eta;
        const double eeta  = tle.ecc * _eta;
        const double psisq = fabs(1.0 - etasq);
        const double coef  = qoms24 * pow(tsi, 4.0);
        const double coef1 = coef / pow(psisq, 3.5);
        const double c2 = coef1 * _xnodp *
            (_aodp * (1.0 + 1.5 * etasq + eeta * (4.0 + etasq)) +
             0.75 * CK2 * tsi / psisq * _x3thm1 *
             (8.0 + 3.0 * etasq * (8.0 + etasq)));
        _c1 = tle.bstar * c2;
        _c4 = 2.0 * _xnodp * coef1 * _aodp * betao2 *
              (_eta * (2.0 + 0.5 * etasq) + tle.ecc * (0.5 + 2.0 * etasq) -
               2.0 * CK2 * tsi / (_aodp * psisq) *
               (-3.0 * _x3thm1 * (1.0 - 2.0 * eeta + etasq * (1.5 - 0.5 * eeta)) +
                0.75 * _x1mth2 * (2.0 * etasq - eeta * (1.0 + etasq)) *
                cos(2.0 * tle.argp)));
        _c5 = 2.0 * coef1 * _aodp * betao2 *
              (1.0 + 2.75 * (etasq + eeta) + eeta * etasq);
        _c3 = (tle.ecc > 1.0e-4)
            ? coef * tsi * A3OVK2 * _xnodp * _sinio / tle.ecc : 0.0;

        // --- Secular rates of the mean elements (J2, J2², J4 terms).
        const double temp1 = 3.0 * CK2 * pinvsq * _xnodp;
        const double temp2 = temp1 * CK2 * pinvsq;
        const double temp3 = 1.25 * CK4 * pinvsq * pinvsq * _xnodp;
        const double theta4 = theta2 * theta2;
        _xmdot = _xnodp + 0.5 * temp1 * betao * _x3thm1 +
                 0.0625 * temp2 * betao * (13.0 - 78.0 * theta2 + 137.0 * theta4);
        _omgdot = -0.5 * temp1 * (1.0 - 5.0 * theta2) +
                  0.0625 * temp2 * (7.0 - 114.0 * theta2 + 395.0 * theta4) +
                  temp3 * (3.0 - 36.0 * theta2 + 49.0 * theta4);
        const double xhdot1 = -temp1 * _cosio;
        _xnodot = xhdot1 + (0.5 * temp2 * (4.0 - 19.0 * theta2) +
                            2.0 * temp3 * (3.0 - 7.0 * theta2)) * _cosio;
        _xnodcf = 3.5 * betao2 * xhdot1 * _c1;
        _t2cof  = 1.5 * _c1;
        _xlcof  = 0.125 * A3OVK2 * _sinio * (3.0 + 5.0 * _cosio) / (1.0 + _cosio);
        _aycof  = 0.25 * A3OVK2 * _sinio;
        _delmo  = pow(1.0 + _eta * cos(tle.ma), 3.0);
        _sinmo  = sin(tle.ma);
        _omgcof = tle.bstar * _c3 * cos(tle.argp);
        _xmcof  = (tle.ecc > 1.0e-4)
            ? -(2.0 / 3.0) * coef * tle.bstar / eeta : 0.0;

        if (!_isimp) {
            const double c1sq = _c1 * _c1;
            _d2 = 4.0 * _aodp * tsi * c1sq;
            const double temp = _d2 * tsi * _c1 / 3.0;
            _d3 = (17.0 * _aodp + s4) * temp;
            _d4 = 0.5 * temp * _aodp * tsi * (221.0 * _aodp + 31.0 * s4) * _c1;
            _t3cof = _d2 + 2.0 * c1sq;
            _t4cof = 0.25 * (3.0 * _d3 + _c1 * (12.0 * _d2 + 10.0 * c1sq));
            _t5cof = 0.2 * (3.0 * _d4 + 12.0 * _c1 * _d3 + 6.0 * _d2 * _d2 +
                            15.0 * c1sq * (2.0 * _d2 + c1sq));
        }
        _ok = true;
        return true;
    }

    bool ready() const { return _ok; }
    const Tle& tle() const { return _t; }

    // Propagate to `tsince` MINUTES from the element-set epoch (negative is
    // legal: it propagates backwards).
    StateVector propagate(double tsince) const {
        StateVector sv;
        if (!_ok) return sv;

        // --- Secular effects of gravity and drag.
        const double xmdf   = _t.ma   + _xmdot  * tsince;
        const double omgadf = _t.argp + _omgdot * tsince;
        const double xnoddf = _t.raan + _xnodot * tsince;
        double omega = omgadf, xmp = xmdf;
        const double tsq = tsince * tsince;
        const double xnode = xnoddf + _xnodcf * tsq;
        double tempa = 1.0 - _c1 * tsince;
        double tempe = _t.bstar * _c4 * tsince;
        double templ = _t2cof * tsq;
        if (!_isimp) {
            const double delomg = _omgcof * tsince;
            const double delm = _xmcof * (pow(1.0 + _eta * cos(xmdf), 3.0) - _delmo);
            const double temp = delomg + delm;
            xmp   = xmdf + temp;
            omega = omgadf - temp;
            const double t3 = tsq * tsince, t4 = t3 * tsince;
            tempa = tempa - _d2 * tsq - _d3 * t3 - _d4 * t4;
            tempe = tempe + _t.bstar * _c5 * (sin(xmp) - _sinmo);
            templ = templ + _t3cof * t3 + t4 * (_t4cof + tsince * _t5cof);
        }
        const double a = _aodp * tempa * tempa;
        double e = _t.ecc - tempe;
        // A decayed element set drives e out of range; report failure rather
        // than return a NaN position that renders as a dot at the pole.
        if (e < 1.0e-6 || e >= 1.0 || a < 1.0) return sv;
        const double xl = xmp + omega + xnode + _xnodp * templ;
        const double beta = sqrt(1.0 - e * e);
        const double xn = XKE / pow(a, 1.5);

        // --- Long-period periodics.
        const double axn = e * cos(omega);
        const double temp0 = 1.0 / (a * beta * beta);
        const double xll = temp0 * _xlcof * axn;
        const double aynl = temp0 * _aycof;
        const double xlt = xl + xll;
        const double ayn = e * sin(omega) + aynl;

        // --- Kepler's equation, Newton-Raphson with a bounded step. The
        // unbounded form oscillates for near-parabolic corrections; the
        // classic ±0.95 clamp is what makes it converge in ~5 passes.
        double capu = fmod(xlt - xnode, SGP4_TAU);
        if (capu < 0.0) capu += SGP4_TAU;
        double epw = capu, sinepw = 0.0, cosepw = 0.0, ecose = 0.0, esine = 0.0;
        for (int i = 0; i < 10; i++) {
            sinepw = sin(epw); cosepw = cos(epw);
            ecose = axn * cosepw + ayn * sinepw;
            esine = axn * sinepw - ayn * cosepw;
            const double f = capu - epw + esine;
            if (fabs(f) < 1.0e-12) break;
            const double df = 1.0 - ecose;
            double delta = f / df;
            if (delta >  0.95) delta =  0.95;
            if (delta < -0.95) delta = -0.95;
            epw += delta;
        }

        // --- Short-period preliminary quantities.
        const double elsq = axn * axn + ayn * ayn;
        const double tmp = 1.0 - elsq;
        const double pl = a * tmp;
        if (pl < 0.0) return sv;
        const double r = a * (1.0 - ecose);
        const double rdot = XKE * sqrt(a) * esine / r;
        const double rfdot = XKE * sqrt(pl) / r;
        const double betal = sqrt(tmp);
        const double temp3 = 1.0 / (1.0 + betal);
        const double cosu = a / r * (cosepw - axn + ayn * esine * temp3);
        const double sinu = a / r * (sinepw - ayn - axn * esine * temp3);
        const double u = atan2(sinu, cosu);
        const double sin2u = 2.0 * sinu * cosu;
        const double cos2u = 2.0 * cosu * cosu - 1.0;
        const double tempA = 1.0 / pl;
        const double tempB = CK2 * tempA;
        const double tempC = tempB * tempA;

        // --- Update for short-period periodics.
        const double rk = r * (1.0 - 1.5 * tempC * betal * _x3thm1) +
                          0.5 * tempB * _x1mth2 * cos2u;
        const double uk = u - 0.25 * tempC * _x7thm1 * sin2u;
        const double xnodek = xnode + 1.5 * tempC * _cosio * sin2u;
        const double xinck = _t.incl + 1.5 * tempC * _cosio * _sinio * cos2u;
        const double rdotk = rdot - xn * tempB * _x1mth2 * sin2u;
        const double rfdotk = rfdot + xn * tempB * (_x1mth2 * cos2u + 1.5 * _x3thm1);

        // --- Orientation vectors → TEME position and velocity.
        const double sinuk = sin(uk),    cosuk = cos(uk);
        const double sinik = sin(xinck), cosik = cos(xinck);
        const double sinnok = sin(xnodek), cosnok = cos(xnodek);
        const double xmx = -sinnok * cosik;
        const double xmy =  cosnok * cosik;
        const double ux = xmx * sinuk + cosnok * cosuk;
        const double uy = xmy * sinuk + sinnok * cosuk;
        const double uz = sinik * sinuk;
        const double vx = xmx * cosuk - cosnok * sinuk;
        const double vy = xmy * cosuk - sinnok * sinuk;
        const double vz = sinik * cosuk;

        sv.x = rk * ux * XKMPER;
        sv.y = rk * uy * XKMPER;
        sv.z = rk * uz * XKMPER;
        // er/min → km/s
        const double kms = XKMPER / 60.0;
        sv.vx = (rdotk * ux + rfdotk * vx) * kms;
        sv.vy = (rdotk * uy + rfdotk * vy) * kms;
        sv.vz = (rdotk * uz + rfdotk * vz) * kms;
        sv.ok = true;
        return sv;
    }

    // Convenience: propagate to a Julian date instead of minutes-from-epoch.
    StateVector propagateJd(double jd) const {
        return propagate((jd - _t.epochJd) * MIN_PER_DAY);
    }

    // Age of the element set in days at `jd`. SPACE.md trap 3: SGP4 error
    // grows by kilometres per day, so the VIEW greys out past 14 days rather
    // than presenting a stale position with the same confidence as a fresh one.
    double ageDays(double jd) const { return jd - _t.epochJd; }

private:
    Tle    _t;
    bool   _ok = false, _isimp = false;
    double _cosio = 0, _sinio = 0, _x3thm1 = 0, _x1mth2 = 0, _x7thm1 = 0;
    double _xnodp = 0, _aodp = 0, _eta = 0;
    double _c1 = 0, _c3 = 0, _c4 = 0, _c5 = 0;
    double _xmdot = 0, _omgdot = 0, _xnodot = 0, _xnodcf = 0;
    double _t2cof = 0, _t3cof = 0, _t4cof = 0, _t5cof = 0;
    double _xlcof = 0, _aycof = 0, _delmo = 0, _sinmo = 0;
    double _omgcof = 0, _xmcof = 0;
    double _d2 = 0, _d3 = 0, _d4 = 0;
};

} // namespace spc
