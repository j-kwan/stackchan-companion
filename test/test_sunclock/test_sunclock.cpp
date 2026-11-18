// =============================================================================
// test_sunclock — sunrise / sunset / night (firmware/common/SunClock.h)
// =============================================================================
// SunClock is a pure header on purpose: no network, no RTC, no Arduino. That
// makes the one thing that can actually be wrong about it — the arithmetic —
// checkable on the PC, which is the whole reason it lives in firmware/common/
// rather than inside a guest bin.
//
// REFERENCE VALUES ARE PUBLISHED ONES, not this implementation's own output.
// They come from the public sunrise-sunset.org API (a NOAA-derived service),
// queried per site and per date and quoted below to the second in UTC. Where a
// second opinion was needed the same dates were checked against Meeus'
// solar position (Astronomical Algorithms ch. 25, ~0.01 deg) and against
// web-calendar.org.
//
// TOLERANCES, STATED RATHER THAN HIDDEN:
//
//   kTolMin   = 3 min, low and mid latitudes. The measured errors are 1.1 to
//               1.8 min (see each test). The margin is not padding: published
//               tables DISAGREE WITH EACH OTHER by about 2 min at Paris —
//               sunrise-sunset.org says 05:51:34 UTC on 2026-03-20,
//               web-calendar.org says 05:54, Meeus says 05:53:42 — because
//               they do not all take the same horizon and refraction model.
//               Asserting tighter than the spread between the references would
//               be asserting on the reference, not on the code.
//
//   kTolHiLat = 8 min, above 70 deg. Near the poles the Sun crosses the horizon
//               at a shallow angle, so the same small elevation error buys a
//               much larger time error — and the references scatter with it:
//               at Svalbard on 2026-09-22 sunrise-sunset.org gives a day
//               10.5 min LONGER than Meeus does. This implementation sits
//               1 to 2 min from Meeus and 4 to 5 min from the API.
//
// Execution : .\scripts\gates\test-native.ps1 test_sunclock
// =============================================================================

#include <unity.h>
#include <math.h>
#include "../../firmware/common/SunClock.h"

using namespace sce;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

// Tolerances, expressed in hours because that is what the API returns.
static constexpr float kTolMin   = 3.0f / 60.0f;
static constexpr float kTolHiLat = 8.0f / 60.0f;

// Sites. Longitude positive EAST throughout.
static constexpr float REU_LA = -20.89f, REU_LO =  55.53f;  // Saint-Denis, Reunion (UTC+4)
static constexpr float PAR_LA =  48.85f, PAR_LO =   2.35f;  // Paris (UTC+1 in March)
static constexpr float SVA_LA =  78.22f, SVA_LO =  15.65f;  // Longyearbyen, Svalbard

static constexpr float H(int h, int m, int s) {
    return (float)h + (float)m / 60.0f + (float)s / 3600.0f;
}

// Unix timestamps used below (all UTC):
//   1782028800 = 2026-06-21 08:00Z  (12:00 local at Reunion, UTC+4)
//   1781985600 = 2026-06-20 20:00Z  (00:00 local at Reunion on the 21st)
//   1782000000 = 2026-06-21 00:00Z
//   1797811200 = 2026-12-21 00:00Z

// ------------------------------------------------------- Reunion, solstices
// The site that motivated the header: the flight-radar guest switches theme on
// a fixed 21:00-07:00 window, and Reunion's sunset moves by 1 h 13 min between
// the two solstices.

void test_reunion_june_solstice(void) {
    // Published (sunrise-sunset.org), 2026-06-21: rise 02:52:35Z, set 13:46:45Z.
    // Measured error of this implementation: +1.06 min on rise, -1.49 min on set.
    SunTimes s = sunTimes(REU_LA, REU_LO, 2026, 6, 21);
    TEST_ASSERT_FALSE(s.polar);
    TEST_ASSERT_FLOAT_WITHIN(kTolMin, H(2, 52, 35),  s.riseUtcH);
    TEST_ASSERT_FLOAT_WITHIN(kTolMin, H(13, 46, 45), s.setUtcH);
}

void test_reunion_december_solstice(void) {
    // Published, 2026-12-21: rise 01:32:32Z, set 14:59:11Z.
    // Measured error: +1.61 min on rise, -1.00 min on set.
    SunTimes s = sunTimes(REU_LA, REU_LO, 2026, 12, 21);
    TEST_ASSERT_FALSE(s.polar);
    TEST_ASSERT_FLOAT_WITHIN(kTolMin, H(1, 32, 32),  s.riseUtcH);
    TEST_ASSERT_FLOAT_WITHIN(kTolMin, H(14, 59, 11), s.setUtcH);
}

void test_reunion_seasonal_swing_breaks_a_fixed_window(void) {
    // The reason the header exists, asserted rather than argued. Local time is
    // UTC+4 all year at Reunion (no DST), so the local sunset is setUtcH + 4.
    SunTimes jun = sunTimes(REU_LA, REU_LO, 2026, 6, 21);
    SunTimes dec = sunTimes(REU_LA, REU_LO, 2026, 12, 21);

    float sunsetJun = jun.setUtcH + 4.0f;      // ~17:45 local
    float sunsetDec = dec.setUtcH + 4.0f;      // ~18:58 local

    // More than an hour of swing: a single fixed hour cannot serve both.
    TEST_ASSERT_TRUE(sunsetDec - sunsetJun > 1.0f);

    // And the fixed 21:00 night switch is more than three hours late in June:
    // the sky is dark and the day theme is still on.
    TEST_ASSERT_TRUE(21.0f - sunsetJun > 3.0f);

    // Day length: ~10 h 52 in June, ~13 h 24 in December.
    TEST_ASSERT_TRUE((dec.setUtcH - dec.riseUtcH) - (jun.setUtcH - jun.riseUtcH) > 2.0f);
}

// ----------------------------------------------------- Paris, March equinox

void test_paris_march_equinox(void) {
    // Published, 2026-03-20: rise 05:51:34Z, set 18:04:32Z (06:51 / 19:04 CET).
    // Measured error: +1.82 min on rise, -1.08 min on set.
    SunTimes s = sunTimes(PAR_LA, PAR_LO, 2026, 3, 20);
    TEST_ASSERT_FALSE(s.polar);
    TEST_ASSERT_FLOAT_WITHIN(kTolMin, H(5, 51, 34),  s.riseUtcH);
    TEST_ASSERT_FLOAT_WITHIN(kTolMin, H(18, 4, 32),  s.setUtcH);
}

void test_equinox_day_is_slightly_over_twelve_hours(void) {
    // "Equinox = twelve hours" is only true for the centre of the disc at the
    // geometric horizon. With the -0.833 deg zenith the day is a few minutes
    // LONGER, and more so the further from the equator: this checks that the
    // refraction term is actually in the formula and applied the right way.
    float atEquator = 0.0f, atParis = 0.0f;
    {
        SunTimes e = sunTimes(0.0f, 0.0f, 2026, 3, 20);
        atEquator = e.setUtcH - e.riseUtcH;                    // ~12 h 07
        SunTimes p = sunTimes(PAR_LA, PAR_LO, 2026, 3, 20);
        atParis = p.setUtcH - p.riseUtcH;                      // ~12 h 10
    }
    TEST_ASSERT_TRUE(atEquator > 12.0f && atEquator < 12.25f);
    TEST_ASSERT_TRUE(atParis > atEquator);
}

// ------------------------------------------------------------ polar cases
// Longyearbyen (78.22 N): published midnight sun from 20 April to 22 August,
// published polar night from 26 October to 15 February. The flag must let a
// caller tell "no sunrise today" from "the computation failed" — and the
// interval must still say WHICH of the two it is.

void test_svalbard_june_is_midnight_sun(void) {
    SunTimes s = sunTimes(SVA_LA, SVA_LO, 2026, 6, 21);
    TEST_ASSERT_TRUE(s.polar);
    TEST_ASSERT_TRUE(s.setUtcH > s.riseUtcH);          // daylight covers the day
    TEST_ASSERT_FLOAT_WITHIN(0.001f,  0.0f, s.riseUtcH);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 24.0f, s.setUtcH);
}

void test_svalbard_december_is_polar_night(void) {
    SunTimes s = sunTimes(SVA_LA, SVA_LO, 2026, 12, 21);
    TEST_ASSERT_TRUE(s.polar);
    TEST_ASSERT_EQUAL_FLOAT(s.riseUtcH, s.setUtcH);    // empty daylight interval
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.riseUtcH);
}

void test_svalbard_september_is_an_ordinary_day(void) {
    // Between the two polar seasons the same site must come back to a normal
    // rise/set — the flag is a property of the DATE, not of the latitude.
    // Published, 2026-09-22: rise 04:24:27Z, set 17:15:47Z. Measured error
    // +4.4 / -5.1 min against that source, +1.1 / +2.1 min against Meeus:
    // hence kTolHiLat (see the header of this file).
    SunTimes s = sunTimes(SVA_LA, SVA_LO, 2026, 9, 22);
    TEST_ASSERT_FALSE(s.polar);
    TEST_ASSERT_FLOAT_WITHIN(kTolHiLat, H(4, 24, 27),  s.riseUtcH);
    TEST_ASSERT_FLOAT_WITHIN(kTolHiLat, H(17, 15, 47), s.setUtcH);
}

// ---------------------------------------------------------- range and wrap

void test_hours_always_inside_the_day(void) {
    // Including the far east and the far west, where the true event falls on
    // the adjacent UTC day and the documented wrap kicks in.
    const float lons[6] = { -175.0f, -120.0f, -0.0f, 55.53f, 120.0f, 178.44f };
    for (int i = 0; i < 6; i++) {
        for (int mo = 1; mo <= 12; mo++) {
            SunTimes s = sunTimes(30.0f, lons[i], 2026, mo, 15);
            TEST_ASSERT_TRUE(s.riseUtcH >= 0.0f && s.riseUtcH < 24.0f);
            TEST_ASSERT_TRUE(s.setUtcH  >= 0.0f && s.setUtcH  <= 24.0f);
        }
    }
}

void test_far_east_site_still_yields_the_right_local_time(void) {
    // Suva, Fiji (-18.14, +178.44, UTC+12) on 2026-06-21. Sunrise falls at
    // 18:37 UTC — on the PREVIOUS UTC day — and the wrap hides that. Adding
    // the local offset modulo 24 must still give a plausible local morning,
    // which is the contract the header promises to callers that only display.
    SunTimes s = sunTimes(-18.14f, 178.44f, 2026, 6, 21);
    float riseLocal = fmodf(s.riseUtcH + 12.0f, 24.0f);
    float setLocal  = fmodf(s.setUtcH  + 12.0f, 24.0f);
    TEST_ASSERT_TRUE(riseLocal > 6.0f && riseLocal < 7.0f);    // ~06:37
    TEST_ASSERT_TRUE(setLocal  > 17.0f && setLocal < 18.0f);   // ~17:39
}

// --------------------------------------------------------- isNight / elevation

void test_isnight_agrees_with_suntimes_at_reunion(void) {
    // 2026-06-21, local noon (08:00Z) and local midnight (2026-06-20 20:00Z).
    TEST_ASSERT_FALSE(isNight(REU_LA, REU_LO, (time_t)1782028800));
    TEST_ASSERT_TRUE (isNight(REU_LA, REU_LO, (time_t)1781985600));

    // And it must flip within a couple of minutes of the computed sunset:
    // both come from the same model, so the only thing that could separate
    // them is a sign or a wrap error.
    SunTimes s = sunTimes(REU_LA, REU_LO, 2026, 6, 21);
    time_t midnightUtc = (time_t)1782000000;                    // 2026-06-21 00:00Z
    time_t justBefore  = midnightUtc + (time_t)(s.setUtcH * 3600.0f) - 120;
    time_t justAfter   = midnightUtc + (time_t)(s.setUtcH * 3600.0f) + 120;
    TEST_ASSERT_FALSE(isNight(REU_LA, REU_LO, justBefore));
    TEST_ASSERT_TRUE (isNight(REU_LA, REU_LO, justAfter));
}

void test_isnight_under_midnight_sun_and_polar_night(void) {
    // 24 hourly samples: never night in June at Svalbard, always night in
    // December. This is the case a "between 21:00 and 07:00" rule gets wrong
    // by twelve hours.
    for (int h = 0; h < 24; h++) {
        TEST_ASSERT_FALSE(isNight(SVA_LA, SVA_LO, (time_t)(1782000000 + h * 3600)));
        TEST_ASSERT_TRUE (isNight(SVA_LA, SVA_LO, (time_t)(1797811200 + h * 3600)));
    }
}

void test_elevation_peaks_at_solar_noon(void) {
    // Sampling the elevation minute by minute over a day must place the maximum
    // on the midpoint of the rise/set pair. The two are computed by different
    // code paths (hour angle solved for, versus hour angle evaluated), so this
    // is a real cross-check and not a tautology.
    time_t base = (time_t)1782000000;                           // 2026-06-21 00:00Z
    float  best = -999.0f;
    int    bestMin = 0;
    for (int i = 0; i < 1440; i++) {
        float e = solarElevationDeg(REU_LA, REU_LO, base + i * 60);
        if (e > best) { best = e; bestMin = i; }
    }
    SunTimes s = sunTimes(REU_LA, REU_LO, 2026, 6, 21);
    float noonH = (s.riseUtcH + s.setUtcH) * 0.5f;               // ~08:19Z
    TEST_ASSERT_FLOAT_WITHIN(2.0f / 60.0f, noonH, (float)bestMin / 60.0f);

    // Reunion at the June solstice: the Sun tops out around 45.7 deg
    // (90 - 20.89 - 23.44). A sign error on the declination would show here.
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 45.7f, best);
}

// ------------------------------------------------------------ year phase
// The published NOAA form re-zeroes the Sun's phase on 1 January, which makes
// the same calendar date drift by up to 1.4 days inside the leap cycle. This
// header measures the fractional year in tropical years instead; the test that
// would have caught the original bug is this one.

void test_same_date_across_the_leap_cycle_is_stable(void) {
    float rise0 = 0.0f, set0 = 0.0f;
    for (int y = 2024; y <= 2031; y++) {
        SunTimes s = sunTimes(REU_LA, REU_LO, y, 6, 21);
        TEST_ASSERT_FALSE(s.polar);
        if (y == 2024) { rise0 = s.riseUtcH; set0 = s.setUtcH; continue; }
        // The true solstice wanders by under a day, so sunrise on 21 June
        // must not move by more than a minute from one year to the next.
        TEST_ASSERT_FLOAT_WITHIN(1.0f / 60.0f, rise0, s.riseUtcH);
        TEST_ASSERT_FLOAT_WITHIN(1.0f / 60.0f, set0,  s.setUtcH);
    }
}

void test_leap_day_is_a_valid_date(void) {
    // 29 February must be accepted and must land between the 28th and 1 March,
    // which is only true if the calendar conversion knows about leap years.
    SunTimes feb28 = sunTimes(PAR_LA, PAR_LO, 2028, 2, 28);
    SunTimes feb29 = sunTimes(PAR_LA, PAR_LO, 2028, 2, 29);
    SunTimes mar01 = sunTimes(PAR_LA, PAR_LO, 2028, 3, 1);
    TEST_ASSERT_TRUE(feb29.riseUtcH < feb28.riseUtcH);   // days lengthening
    TEST_ASSERT_TRUE(mar01.riseUtcH < feb29.riseUtcH);
    TEST_ASSERT_TRUE(feb29.setUtcH  > feb28.setUtcH);
    TEST_ASSERT_TRUE(mar01.setUtcH  > feb29.setUtcH);
}

// `clockSynced` is the guard every consumer of time() needs, and it used to be
// a bare `> 1600000000` copied into ten places (review 08-01). The boundary is
// deliberately loose — what matters is that an UNSET clock never passes and a
// real one always does.
static void test_clock_synced_separates_unset_from_real(void) {
    TEST_ASSERT_FALSE(sce::clockSynced(0));            // ESP32 with no NTP
    TEST_ASSERT_FALSE(sce::clockSynced(1));            // 1970-01-01
    TEST_ASSERT_FALSE(sce::clockSynced(946684800));    // 2000-01-01, still stale
    TEST_ASSERT_FALSE(sce::clockSynced(sce::kClockSyncedEpoch));   // exclusive
    TEST_ASSERT_TRUE(sce::clockSynced(sce::kClockSyncedEpoch + 1));
    TEST_ASSERT_TRUE(sce::clockSynced(1785586610));    // 2026-08-01, measured
                                                       // on target that day
    // A negative time_t (clock skew, or a bad cast) must not read as synced.
    TEST_ASSERT_FALSE(sce::clockSynced((time_t)-1));
}

// ---------------------------------------------------------------- harness
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_clock_synced_separates_unset_from_real);
    RUN_TEST(test_reunion_june_solstice);
    RUN_TEST(test_reunion_december_solstice);
    RUN_TEST(test_reunion_seasonal_swing_breaks_a_fixed_window);
    RUN_TEST(test_paris_march_equinox);
    RUN_TEST(test_equinox_day_is_slightly_over_twelve_hours);
    RUN_TEST(test_svalbard_june_is_midnight_sun);
    RUN_TEST(test_svalbard_december_is_polar_night);
    RUN_TEST(test_svalbard_september_is_an_ordinary_day);
    RUN_TEST(test_hours_always_inside_the_day);
    RUN_TEST(test_far_east_site_still_yields_the_right_local_time);
    RUN_TEST(test_isnight_agrees_with_suntimes_at_reunion);
    RUN_TEST(test_isnight_under_midnight_sun_and_polar_night);
    RUN_TEST(test_elevation_peaks_at_solar_noon);
    RUN_TEST(test_same_date_across_the_leap_cycle_is_stable);
    RUN_TEST(test_leap_day_is_a_valid_date);
    return UNITY_END();
}
