#pragma once
// =============================================================================
// headtrack.h — pointing the K151 head at a compass bearing (guest bins)
// =============================================================================
// PURE (no Arduino include), tested natively by test_astro. Shared by space
// (a satellite: azimuth AND elevation) and flight-radar (a flight: bearing
// only). Each bin owns its servo bus through HeadServo.h; this file only
// answers "which yaw and pitch look at azimuth A, elevation E, for a robot
// whose face points at compass bearing F".
//
// ONE implementation on purpose: the radar used to carry its own `166 + rel`,
// and that copy had the sign backwards — the head turned AWAY from the flight.
// Nobody had watched it on the robot. The sign below was (09-25): a target set
// 90 deg to the robot's left turned the head to the left of the robot.
//
// The constants are HAND COPIES of src/engine/Units.h: a guest bin cannot
// include the companion's headers. scripts/gates/check-mirrors.py holds each
// one against its original, which is the only reason a copy is tolerable.
// =============================================================================

namespace spc {
namespace headtrack {

constexpr float YAW_CENTER    = 166.0f;  // Units.h YAW_CENTER: head facing forward
constexpr float YAW_REL_MAX   = 130.0f;  // Units.h YAW_RANGE: reachable either side
constexpr float PITCH_NEUTRAL = 93.0f;   // Units.h PITCH_NEUTRAL: level gaze
constexpr float PITCH_MIN     = 19.0f;   // Units.h PITCH_MIN: highest safe raise

struct Pose { float yaw; float pitch; };

inline float wrap180(float a) {
    while (a > 180.0f)   a -= 360.0f;
    while (a <= -180.0f) a += 360.0f;
    return a;
}

// az, facingAz: compass bearings (0 = north, 90 = east). el: degrees above the
// horizon. The servo convention is viewer-centric (A2.9): a yaw BELOW the
// centre turns the head towards the VIEWER's left, which is the ROBOT's right.
// An object clockwise of where the robot faces (rel > 0) is on the robot's
// right, hence `YAW_CENTER - rel`.
//
// Beyond ±YAW_REL_MAX the head stops at the end of its range: it looks
// towards the object without claiming to point at it, like the radar.
//
// Pitch: one servo degree per degree of elevation, raising from the level
// pose. A lower raw pitch is a RAISED head (Units.h), and the head cannot rise
// past PITCH_MIN (85 deg on M5Stack's scale): above about 74 deg of elevation
// it holds its highest safe pose rather than stalling against the stop.
inline Pose aim(float az, float el, float facingAz) {
    float rel = wrap180(az - facingAz);
    if (rel >  YAW_REL_MAX) rel =  YAW_REL_MAX;
    if (rel < -YAW_REL_MAX) rel = -YAW_REL_MAX;
    if (el < 0.0f)  el = 0.0f;
    if (el > 90.0f) el = 90.0f;
    float pitch = PITCH_NEUTRAL - el;
    if (pitch < PITCH_MIN) pitch = PITCH_MIN;
    return { YAW_CENTER - rel, pitch };
}

// The resting pose: facing forward, level.
inline Pose home() { return { YAW_CENTER, PITCH_NEUTRAL }; }

} // namespace headtrack
} // namespace spc
