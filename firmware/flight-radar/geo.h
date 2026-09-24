#pragma once
// =============================================================================
// geo.h — flight-radar: PURE functions (geodesy, routes, formatting)
// =============================================================================
// Extracted from main.cpp so they are NATIVELY TESTABLE (`pio test -e native`)
// like the companion's engine/ and behavior/: no Arduino/M5 dependency, only
// <math.h> and <string.h>. The rest of the bin (network, rendering, touch) is
// not testable off-target — these four functions are, and they are the ones
// carrying the correctness of distances, ETA and leg selection.
// =============================================================================

#include <math.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include "../common/SunClock.h"   // the ONE days_from_civil

namespace fr {

inline constexpr float NM_PER_DEG_LAT = 60.0f;
inline constexpr float DEG2RAD        = 0.01745329252f;

// Great-circle distance (haversine) in nautical miles. Routes span whole
// continents: the equirectangular approximation is no longer good enough.
inline float gcNm(float la1, float lo1, float la2, float lo2) {
    float p1 = la1 * DEG2RAD, p2 = la2 * DEG2RAD;
    float dp = (la2 - la1) * DEG2RAD, dl = (lo2 - lo1) * DEG2RAD;
    float a = sinf(dp / 2) * sinf(dp / 2) +
              cosf(p1) * cosf(p2) * sinf(dl / 2) * sinf(dl / 2);
    return 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a)) * 3440.065f;
}

// Local FLAT distance (equirectangular) — accurate enough at radar scale
// (≤ 500 nm) and far cheaper than haversine, which matters per frame.
inline float planarNm(float la1, float lo1, float la2, float lo2) {
    float dLat = (la2 - la1) * NM_PER_DEG_LAT;
    float dLon = (lo2 - lo1) * NM_PER_DEG_LAT * cosf(la1 * DEG2RAD);
    return sqrtf(dLat * dLat + dLon * dLon);
}

// Initial bearing (0-360°, 0 = North) from (la1,lo1) towards (la2,lo2).
inline float bearingDeg(float la1, float lo1, float la2, float lo2) {
    float p1 = la1 * DEG2RAD, p2 = la2 * DEG2RAD;
    float dl = (lo2 - lo1) * DEG2RAD;
    float y = sinf(dl) * cosf(p2);
    float x = cosf(p1) * sinf(p2) - sinf(p1) * cosf(p2) * cosf(dl);
    float b = atan2f(y, x) / DEG2RAD;
    return b < 0 ? b + 360.0f : b;
}

// Leg of a multi-stop route the aircraft currently sits on.
// `n` waypoints (n-1 legs); returns the index i of leg [i, i+1].
//
// An hexdb route can read "LFPG-FIMP-FMEE": always taking the FIRST leg
// displayed Paris→Mauritius for an aircraft already flying
// Mauritius→Reunion — wrong origin, wrong progress bar, wrong ETA, with no
// sign at all that it was wrong (review 07-27). We pick the leg whose
// endpoints BRACKET the position best: the extra length of the path going
// through the aircraft over the direct leg is minimal (zero when the
// aircraft is exactly on it).
inline int pickLeg(const float* wpLat, const float* wpLon, int n,
                   float plat, float plon) {
    if (n < 3) return 0;                       // 0 or 1 leg: nothing to choose
    int   best = 0;
    float bestCost = -1.0f;
    for (int i = 0; i + 1 < n; i++) {
        float direct = gcNm(wpLat[i], wpLon[i], wpLat[i + 1], wpLon[i + 1]);
        float via    = gcNm(wpLat[i], wpLon[i], plat, plon) +
                       gcNm(plat, plon, wpLat[i + 1], wpLon[i + 1]);
        float cost   = via - direct;           // ≥ 0, zero if right on the leg
        if (bestCost < 0 || cost < bestCost) { bestCost = cost; best = i; }
    }
    return best;
}

// Is the announced leg being flown in the OPPOSITE direction? The community
// route databases give the CANONICAL direction of a flight number, and some
// airlines reuse it on the return leg (SS636 announced MRS→RUN while flying
// RUN→MRS, user 07-28). Two independent tests, either one convicts:
//
//   TRACK: in cruise (> 150 kt, track published) the ground track pointing
//   more than 120° away from the bearing to the announced destination means
//   the aircraft is flying AWAY from where the database says it is going.
//   The 150 kt floor is deliberate and must NOT be lowered: below it the
//   aircraft may be turning (departure pattern, circling approach) and a
//   transient track would swap a CORRECT route.
//
//   PHASE OF FLIGHT (08-04, user watched an aircraft LAND at RUN while the
//   panel said RUN→Marseille): on approach the speed sits under the 150 kt
//   floor, so the track test is blind exactly when the contradiction is on
//   display. But an aircraft DESCENDING low, close to its announced ORIGIN
//   and far from its announced destination, is arriving there — no track
//   needed. Mirrored for a CLIMB out of the announced destination. The
//   ground case is deliberately excluded: parked at the announced origin is
//   ambiguous (arrived on the reversed leg, or about to depart the correct
//   one), and descent/climb is not.
//
// Pure, natively tested (test_flightgeo).
inline bool legReversed(bool hasTrk, float gsKt, float trkDeg,
                        float brgToDestDeg, float altFt, float vrFtMin,
                        float dOrigNm, float dDestNm) {
    if (hasTrk && gsKt > 150.0f) {
        float e = fabsf(trkDeg - brgToDestDeg);
        if (e > 180.0f) e = 360.0f - e;
        if (e > 120.0f) return true;
    }
    if (altFt < 10000.0f && dOrigNm < 40.0f && dDestNm > 150.0f &&
        vrFtMin < -256.0f) return true;    // descending onto the "origin"
    if (altFt < 10000.0f && dDestNm < 40.0f && dOrigNm > 150.0f &&
        vrFtMin > 256.0f) return true;     // climbing out of the "destination"
    return false;
}

// Sanitises a city name for the screen: printable ASCII ONLY (the 6×8 font
// renders UTF-8 as "??") and length clamped to the panel width. Edits the
// string in place.
inline void cityClean(char* s, size_t maxChars) {
    if (!s) return;
    char* w = s;
    for (char* r = s; *r && (size_t)(w - s) < maxChars; r++)
        if ((uint8_t)*r >= 0x20 && (uint8_t)*r < 0x7F) *w++ = *r;
    *w = '\0';
}


// ---------------------------------------------------------------------------
// TAF change group: does "DDHH/DDHH" cover a given instant?
// ---------------------------------------------------------------------------
// A TAF states its periods as DAY-OF-MONTH + HOUR UTC, with no month and no
// year: "PROB40 TEMPO 0215/0224" is day 02, 15:00Z to day 02, 24:00Z. The
// reader has to work out which group is in force RIGHT NOW, and that is the
// one thing the screen can answer for them.
//
// Three traps, and all three are why this is a tested function and not three
// lines inside the draw:
//   - HOUR 24 IS LEGAL and means midnight ENDING that day, not 00:00 starting
//     it. Treated as day+1 hour 0, which is what it is.
//   - THE MONTH WRAPS. A TAF issued on the 31st runs into day 01, and a naive
//     day comparison puts that thirty days in the past. Resolved by trying the
//     neighbouring months and keeping the reading that lands within a fortnight
//     of `now` — a TAF is at most ~30 h long, so the choice is never ambiguous.
//   - THE END IS EXCLUSIVE. Two consecutive groups share a boundary hour, and
//     an inclusive end would light both.
//
// `dayNum` is days since 1970-01-01 for a civil date (Howard Hinnant's
// algorithm, the same one SunClock.h uses).
// Returns false on anything that is not exactly DDHH/DDHH — no guessing.
inline long long dayNum(int y, int m, int d) {
    // FORWARDS to the ONE implementation (firmware/common/SunClock.h). This was
    // a second, character-for-character copy of Howard Hinnant's
    // days_from_civil — and the comment above it already SAID it was "the same
    // one SunClock.h uses", which is the confession A2.23 is about. Kept as a
    // named wrapper because the pure-geodesy suite calls it by this name and
    // the forwarding costs nothing.
    return sce::sunclock::daysFromCivil(y, m, d);
}

// Absolute hour of a DDHH reading, resolved against the year/month of `now`.
inline long long ddhhAbsHour(int dd, int hh, int nowY, int nowM, long long nowAbs) {
    for (int shift = 0; shift <= 2; shift++) {
        // 0 = same month, 1 = next, 2 = previous.
        int y = nowY, m = nowM;
        if (shift == 1) { if (++m > 12) { m = 1; y++; } }
        else if (shift == 2) { if (--m < 1) { m = 12; y--; } }
        const long long h = dayNum(y, m, dd) * 24LL + hh;
        if (h >= nowAbs - 15LL * 24 && h <= nowAbs + 15LL * 24) return h;
    }
    return dayNum(nowY, nowM, dd) * 24LL + hh;   // out of range: unshifted
}

// Does the period ddhh/ddhh cover the given UTC moment? End EXCLUSIVE.
//
// Split out of tafGroupCovers on 08-02 because the BULLETIN's own validity is
// needed too, and it does not arrive as text: it is parsed out of the header
// and stripped from the body, so the prevailing-conditions line carries no
// range of its own and could never be marked in force. Two copies of this
// arithmetic — month wrap, hour 24, exclusive end — is exactly the kind of
// duplication that drifts.
inline bool ddhhRangeCovers(int d1, int h1, int d2, int h2,
                            int nowY, int nowM, int nowD, int nowH) {
    if (d1 < 1 || d1 > 31 || d2 < 1 || d2 > 31) return false;
    if (h1 > 24 || h2 > 24) return false;
    const long long nowAbs = dayNum(nowY, nowM, nowD) * 24LL + nowH;
    long long a = ddhhAbsHour(d1, h1 % 24, nowY, nowM, nowAbs) + (h1 == 24 ? 24 : 0);
    long long b = ddhhAbsHour(d2, h2 % 24, nowY, nowM, nowAbs) + (h2 == 24 ? 24 : 0);
    if (b <= a) return false;                          // not a period
    return nowAbs >= a && nowAbs < b;                  // end EXCLUSIVE
}

// `line` may hold the group anywhere in it ("PROB40 TEMPO 0215/0224 3000 SHRA").
// nowY/nowM/nowD/nowH = the current UTC date and hour.
inline bool tafGroupCovers(const char* line, int nowY, int nowM, int nowD,
                           int nowH) {
    if (!line) return false;
    for (const char* p = line; p[0]; p++) {
        // Exactly 4 digits, '/', 4 digits — and not part of a longer run.
        int i = 0;
        while (i < 4 && p[i] >= '0' && p[i] <= '9') i++;
        if (i != 4 || p[4] != '/') continue;
        int j = 5;
        while (j < 9 && p[j] >= '0' && p[j] <= '9') j++;
        if (j != 9) continue;
        if (p[9] >= '0' && p[9] <= '9') continue;          // 5-digit run
        if (p != line && p[-1] >= '0' && p[-1] <= '9') continue;
        int d1 = (p[0] - '0') * 10 + (p[1] - '0');
        int h1 = (p[2] - '0') * 10 + (p[3] - '0');
        int d2 = (p[5] - '0') * 10 + (p[6] - '0');
        int h2 = (p[7] - '0') * 10 + (p[8] - '0');
        return ddhhRangeCovers(d1, h1, d2, h2, nowY, nowM, nowD, nowH);
    }
    return false;
}

}  // namespace fr
