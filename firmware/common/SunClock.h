#pragma once
// =============================================================================
// SunClock — sunrise / sunset / "is it night" from latitude, longitude, date
// =============================================================================
// Pure computation. No network, no clock hardware, no Arduino: this header is
// shared by the companion and by both guest bins, and it compiles as-is in the
// `native` test environment. Only <math.h>, <stdint.h> and <time.h>.
//
// WHY IT EXISTS. Two features key off a FIXED wall-clock window: the
// flight-radar guest switches to its Night theme between 21:00 and 07:00, and
// the companion lowers its volume between 22:00 and 06:00. At Reunion
// (-20.89, 55.53) true sunset swings by about 1 h 20 across the year, so a
// fixed window is wrong for most of it — dark at 17:45 in June with the day
// theme still on, broad daylight at 18:55 in December with the night one. Real
// sunrise/sunset costs a few dozen floating-point operations, is called at
// most a few times a minute, and removes the guesswork.
//
// ALGORITHM — NOAA's general solar position approximations (the "sunrise
// equation", gml.noaa.gov/grad/solcalc/solareqns.PDF): fractional year, then
// the equation of time and the solar declination as short harmonic series in
// it, then the hour angle at the -0.833 deg zenith. That zenith is not the
// geometric horizon: it carries ~34' of atmospheric refraction plus the ~16'
// solar semi-diameter, because sunrise is called on the UPPER LIMB of the disc
// rather than on its centre.
//
// ONE DELIBERATE DEPARTURE FROM THE PUBLISHED FORM, and it is worth stating
// because it is the difference between "a few minutes off" and "half a
// minute off". NOAA writes the fractional year as
//
//     gamma = 2.pi/365 . (dayOfYear - 1 + (hour-12)/24)
//
// i.e. it re-zeroes the Sun's phase on 1 January of each calendar year. The
// calendar year is not the tropical year, so that phase drifts by a quarter of
// a day per year and resets every leap year. Compared against Meeus'
// solar position (Astronomical Algorithms ch. 25, accurate to ~0.01 deg) over
// 2020-2040, the day-number form runs 0.7 to 1.4 days BEHIND the real Sun,
// which is a declination error up to 0.44 deg — about 4 minutes of sunrise at
// Paris, and worse further north, since the error scales with tan(latitude).
//
// Measuring the same angle in TROPICAL years since a fixed epoch removes the
// drift without touching a single one of NOAA's coefficients:
//
//     gamma = 2.pi . frac( daysSince(2000-01-01 00:00 UT) / 365.2422 )
//
// The epoch needs no fitted offset — 2000-01-01 falls where the series expects
// its origin. Residuals over 2020-2040 drop to 0.043 deg of declination and
// 0.65 min on the equation of time, which is the intrinsic accuracy of the
// series itself. Everything else below is NOAA's, unmodified.
//
// It remains an APPROXIMATION: no nutation, no parallax, no observer altitude,
// and it degrades towards the poles, where the Sun crosses the horizon at a
// shallow angle and a small elevation error becomes a large time error. For
// "is it dark enough to switch the theme" that is far more accuracy than the
// question deserves. Do not use it for navigation.
//
// POLAR CASES ARE NOT ERRORS. Above the polar circles the hour-angle equation
// has no solution — |cos H| > 1 — because the Sun either stays up all day or
// never comes up. A caller must be able to tell that from a bad input, which
// is what SunTimes::polar is for. The two sub-cases are told apart by the
// interval itself, and callers may rely on it:
//
//     polar && setUtcH > riseUtcH   -> midnight sun  (rise 0, set 24)
//     polar && setUtcH == riseUtcH  -> polar night   (rise 0, set 0)
//
// so "daylight = [riseUtcH, setUtcH)" stays true in every case.
// =============================================================================

#include <math.h>
#include <stdint.h>
#include <time.h>

namespace sce {

struct SunTimes {
    float riseUtcH;  // sunrise, hours UTC in [0, 24)
    float setUtcH;   // sunset,  hours UTC in [0, 24)
    bool  polar;     // no rise or no set today (see the header comment)
};

// -----------------------------------------------------------------------------
// "Has the wall clock been set?" — the guard every consumer of `time()` needs.
// -----------------------------------------------------------------------------
// An ESP32 with no NTP starts its clock in 1970, and EVERY function here (plus
// the METAR age, the NOTAM validity window, the ETA) would answer confidently
// for a date fifty years out. The test was written as a bare `> 1600000000` in
// TEN places across three files (review 08-01) — the same threshold, the same
// meaning, and nothing naming either.
// 1600000000 = 2020-09-13. Any correct clock on this hardware is far past it,
// and no unsynced one ever reaches it: the boundary does not need to be tight,
// it needs to be unmistakable and to have a name.
inline constexpr time_t kClockSyncedEpoch = 1600000000;
inline bool clockSynced(time_t utc) { return utc > kClockSyncedEpoch; }

namespace sunclock {

constexpr double kPi      = 3.14159265358979323846;
constexpr double kDeg2Rad = kPi / 180.0;
constexpr double kRad2Deg = 180.0 / kPi;

// Zenith angle of sunrise/sunset: 90 deg of geometric horizon plus 0.833 deg
// of refraction and solar semi-diameter.
constexpr double kZenithDeg = 90.833;

constexpr double kTropicalYear = 365.2422;   // days
constexpr double kJ2000Epoch   = 10957.0;    // 2000-01-01 in days since 1970-01-01

// Days since 1970-01-01, from a proleptic Gregorian date. Howard Hinnant's
// days_from_civil: pure integer maths, no <time.h> call. mktime() would need a
// TZ-free environment we do not control, and timegm() is not portable to every
// toolchain this project builds with.
inline long long daysFromCivil(int y, int m, int d) {
    y -= (m <= 2);
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const long long yoe = (long long)y - era * 400;                     // [0, 399]
    const long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;        // [0, 146096]
    return era * 146097 + doe - 719468;
}

// Equation of time (minutes) and solar declination (radians). Kept together:
// every caller needs both and they share the same fractional-year angle.
struct Solar { double eqTimeMin; double declRad; };

// `daysSince2000` is a UT instant expressed in days since 2000-01-01 00:00,
// fractional part included. One argument, one instant — the daily rise/set and
// the instantaneous elevation therefore come from exactly the same model.
inline Solar solarAt(double daysSince2000) {
    double frac = fmod(daysSince2000 / kTropicalYear, 1.0);
    if (frac < 0.0) frac += 1.0;
    const double g = 2.0 * kPi * frac;

    Solar s;
    s.eqTimeMin = 229.18 * (0.000075
                          + 0.001868 * cos(g)     - 0.032077 * sin(g)
                          - 0.014615 * cos(2 * g) - 0.040849 * sin(2 * g));
    s.declRad  =   0.006918
                 - 0.399912 * cos(g)     + 0.070257 * sin(g)
                 - 0.006758 * cos(2 * g) + 0.000907 * sin(2 * g)
                 - 0.002697 * cos(3 * g) + 0.001480 * sin(3 * g);
    return s;
}

inline float wrap24(double h) {
    h = fmod(h, 24.0);
    if (h < 0.0) h += 24.0;
    return (float)h;
}

}  // namespace sunclock

// -----------------------------------------------------------------------------
// Sunrise / sunset for a UTC calendar date.
// -----------------------------------------------------------------------------
// Longitude is positive EAST, the convention the rest of the project uses:
// Reunion is +55.53, not -55.53.
//
// The returned hours are wrapped into [0, 24). For longitudes far from
// Greenwich the event can genuinely fall on the adjacent UTC day and the wrap
// hides that — but adding the local UTC offset and taking the result modulo 24
// still gives the correct LOCAL clock time, which is what callers display. A
// caller needing the event's UTC *date* must work it out itself.
inline SunTimes sunTimes(float latDeg, float lonDeg, int year, int month, int day) {
    using namespace sunclock;

    // Solar noon-ish: the series is evaluated once for the middle of the day,
    // which is where the rise/set pair is most nearly symmetric about it.
    const double n  = (double)(daysFromCivil(year, month, day) - (long long)kJ2000Epoch) + 0.5;
    const Solar  s  = solarAt(n);
    const double la = (double)latDeg * kDeg2Rad;

    SunTimes out{ 0.0f, 0.0f, false };

    // cos H = cos(zenith)/(cos lat . cos decl) - tan lat . tan decl
    const double denom = cos(la) * cos(s.declRad);
    if (fabs(denom) < 1e-12) {
        // Exactly at a pole. tan(lat) has blown up; decide by hemisphere and
        // the sign of the declination — every day there is polar anyway.
        const bool sunUp = ((latDeg >= 0.0f) == (s.declRad >= 0.0));
        out.polar    = true;
        out.riseUtcH = 0.0f;
        out.setUtcH  = sunUp ? 24.0f : 0.0f;
        return out;
    }

    const double cosH = cos(kZenithDeg * kDeg2Rad) / denom - tan(la) * tan(s.declRad);

    if (cosH > 1.0) {           // the Sun never reaches the horizon: polar night
        out.polar = true;
        return out;             // rise = set = 0 -> empty daylight interval
    }
    if (cosH < -1.0) {          // the Sun never leaves it: midnight sun
        out.polar    = true;
        out.setUtcH  = 24.0f;
        return out;
    }

    const double haDeg = acos(cosH) * kRad2Deg;   // positive branch = sunrise side
    // Minutes UTC: 720 = solar noon at Greenwich, 4 min per degree of longitude.
    const double riseMin = 720.0 - 4.0 * ((double)lonDeg + haDeg) - s.eqTimeMin;
    const double setMin  = 720.0 - 4.0 * ((double)lonDeg - haDeg) - s.eqTimeMin;

    out.riseUtcH = wrap24(riseMin / 60.0);
    out.setUtcH  = wrap24(setMin  / 60.0);
    return out;
}

// -----------------------------------------------------------------------------
// Solar elevation (degrees above the horizon) at an instant.
// -----------------------------------------------------------------------------
// `utc` is a Unix timestamp (seconds since 1970-01-01 UTC), what time() gives
// once the RTC or SNTP has set the clock.
//
// Going through the elevation, rather than asking whether the clock sits
// between rise and set, removes every day-boundary case at a stroke: no wrap
// at midnight UTC, no adjacent-day correction for far-eastern longitudes, and
// the polar cases answer themselves.
inline float solarElevationDeg(float latDeg, float lonDeg, time_t utc) {
    using namespace sunclock;

    const double days = (double)utc / 86400.0 - kJ2000Epoch;   // since 2000-01-01
    const Solar  s    = solarAt(days);

    double hourUtc = fmod((double)utc / 3600.0, 24.0);
    if (hourUtc < 0.0) hourUtc += 24.0;

    // True solar time (minutes), longitude positive east; hour angle is 0 at
    // solar noon, +/-180 deg at solar midnight.
    const double tst   = hourUtc * 60.0 + s.eqTimeMin + 4.0 * (double)lonDeg;
    const double haDeg = tst / 4.0 - 180.0;

    const double la = (double)latDeg * kDeg2Rad;
    double cosZ = sin(la) * sin(s.declRad)
                + cos(la) * cos(s.declRad) * cos(haDeg * kDeg2Rad);
    if (cosZ >  1.0) cosZ =  1.0;      // guard acos against rounding
    if (cosZ < -1.0) cosZ = -1.0;
    return (float)(90.0 - acos(cosZ) * kRad2Deg);
}

// -----------------------------------------------------------------------------
// Night = the Sun is below the sunrise/sunset zenith (-0.833 deg elevation).
// -----------------------------------------------------------------------------
// NOTE that this is geometric night, not civil twilight: it flips at the very
// instant the upper limb touches the horizon, while the sky stays usably light
// for another 20-30 minutes at mid latitudes. A caller that wants "dark enough
// for the night theme" should test solarElevationDeg() against a few degrees
// below instead — the function is public for exactly that.
inline bool isNight(float latDeg, float lonDeg, time_t utc) {
    return solarElevationDeg(latDeg, lonDeg, utc)
           < (float)(90.0 - sunclock::kZenithDeg);
}

}  // namespace sce
