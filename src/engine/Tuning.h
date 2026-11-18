#pragma once
// =============================================================================
// Tuning.h — StackChan-Companion (engine)
// =============================================================================
// Registry of the LIVE-tunable parameters (ROADMAP P-1 item 2: without this,
// every tuning attempt = recompile + flash). Exposed by
// GET/POST /api/tuning (app/WebApi.h) and persisted in config.yaml (P6).
//
// Memory model: float fields read by Brain/Renderer on every tick, written
// by the AsyncTCP callback. Writing an aligned float is atomic on Xtensa
// (32-bit) and every field is independent → no lock needed: worst case a
// tick reads the stale value of ONE field. (Any pair of fields that must be
// consistent TOGETHER would have to go through a Command — no such case yet.)
//
// Adding a parameter = 1 field + 1 TABLE line. Keys follow
// docs/reference/CONFIG.md §tuning.
//
// PURITY: no Arduino dependency.
// =============================================================================

#include <cstdint>
#include <cstring>

namespace sce {

struct Tuning {
    // ---- Rendering ----
    float eye_color_dim      = 0.80f;  // palette darkening
    float crt_glow_dim       = 0.28f;  // halo intensity vs eye color
    float crt_glow_px        = 3.0f;   // halo dilation (px)
    float eye_spacing        = 14.0f;  // EDGE-TO-EDGE gap between the eyes in
                                       // px (widest preset): 0 = the edges
                                       // touch, 14 = historical position
                                       // (default), max 44
    float eye_depth_scale    = 0.0f;   // near/far "depth" effect: the eye on
                                       // the gaze side widens (0-1). OFF by
                                       // default: no proportion change, shift
                                       // only (the shrunken eye got in the way
                                       // of the VOR)

    // ---- Personality (behavior/Personalities.h) ----
    // WHICH CHARACTER is loaded: 0 = the historical robot, 1 = Haro. An INDEX
    // and not a `haro_mode` boolean, deliberately — the existing behaviour is
    // not "the absence of a costume", it is a personality in its own right, and
    // a boolean would have to be renamed the day a third one appears. On this
    // project renaming a key IS a migration (default inverted + saveConfig
    // rewriting everything = the old value silently reapplied), so the name is
    // paid for now, while it costs nothing.
    // SANITISED in loop() like `band_mode`: an unknown index falls back to 0
    // and is re-persisted.
    float personality        = 0.0f;
    // THE EMOTION ROULETTE, which can now be switched off. Left ON so the
    // default robot is untouched. Off, the face stops changing by itself —
    // which is what lets an expression MEAN something, since a draw every
    // 6-12 s otherwise overwrites whatever a rule just said. It does not
    // freeze the robot: timed emotions still expire back to rest, and blinks,
    // saccades, breathing and the VOR are driven elsewhere.
    float roulette           = 1.0f;

    // ---- Idle / saccades (IdleBehavior — wired in P2b) ----
    float saccade_ms         = 80.0f;  // saccade duration (60-120)
    float fixation_min_ms    = 800.0f;
    float fixation_max_ms    = 4000.0f;

    // ---- Blink (BlinkController — wired in P2b) ----
    float blink_median_ms    = 3500.0f;
    float blink_lag_ms       = 30.0f;  // right-eye lag (0-150) — 80 judged
                                       // too visible in practice

    // ---- Head-follow (§3.0-1) — ON by default since 2026-07-13: the
    //      "eyes lead, head follows" loop is THE Cozmo core of this project;
    //      the initial OFF was only a calibration precaution, since lifted
    //      (efference copy §1.7 + head-follow 4.2 validated on HW) ----
    float head_follow        = 1.0f;    // 1 = the head follows fixations
    float headfollow_hold_ms = 1000.0f; // off-center fixation held before follow
    // 4 s, not 15. Fifteen was right for a COMMANDED move — you asked the head
    // to look somewhere, it stays there — and wrong for the autonomous posture
    // nudges that arrive in a loop: each emotion carries a pitch bias, the home
    // return re-engages the torque to follow it, and the timer restarts. With
    // the roulette running, the neck was held permanently: hot, buzzing and
    // unmovable by hand (user 08-02). A head that adopts a pose and then
    // softens is also the more lifelike of the two — rigidity held for fifteen
    // seconds is what betrays the machine.
    float servo_idle_release_ms = 4000.0f;  // >0: torque released after X ms
                                        // without motion (0 = never). FOUR
                                        // seconds since 08-03 (was 15, and the
                                        // trailing comment still said so):
                                        // measured from the END of a
                                        // trajectory, 15 s let the emotion
                                        // roulette's pitch nudges restart the
                                        // timer forever. Schema v5 re-defaults
                                        // it on cards that carry the old value
    float servos             = 1.0f;    // 0 = servos DISABLED: torque RELEASED
                                        // (limp head, movable by hand, zero
                                        // servo draw) + no writes at all →
                                        // no head motion (VOR/dances/
                                        // head-follow inert). On re-enable:
                                        // SOFT resume (trajectory rebased; the
                                        // head returns to its commanded pose).
                                        // The eyes animate normally.

    // ---- VOR (VestibularSystem P3) ----
    float vor_gain           = 0.90f;  // counter-rotation (0-1.2)
    float vor_drift_alpha    = 0.02f;  // complementary accel correction
    float vor_mag_alpha      = 0.0f;   // magnetometer yaw correction — OFF
                                       // until the heading sign is validated
                                       // on hardware (wrong sign = drift
                                       // doubled, VestibularSystem 08-04)
    float saccade_recentre   = 0.75f;  // |offset|/max triggering the catch-up
    float shake_gyro_thr     = 35.0f;  // sustained °/s → Scared (15 was too
                                       // sensitive in practice)

    // ---- Gyro → screen axes mapping (VOR v3) — LIVE-TUNABLE ----
    // Calibration WITHOUT reflashing: telemetry=1 prints raw gX/gY/gZ, then
    // POST /api/tuning?gyro_yaw_axis=..&gyro_yaw_sign=.. (PLAYBOOK-HW §1).
    // Defaults derived from imu_test (validated on HW): at rest with the
    // screen vertical, gravity lands on sensor Y → world yaw = gyro.y;
    // pitch = gyro.x.
    float gyro_yaw_axis      = 1.0f;   // 0=gyro.x 1=gyro.y 2=gyro.z
    float gyro_yaw_sign      = 1.0f;   // +1/-1
    float gyro_pitch_axis    = 0.0f;
    float gyro_pitch_sign    = 1.0f;

    // ---- Pickup (§2.2) — thresholds raised (field feedback: "not sensitive enough") ----
    float pickup_dev_g       = 0.08f;  // sustained |a|-1g deviation → lifted
    float pickup_hold_ms     = 120.0f; // how long the deviation must hold

    // ---- LEDs (§3.9 — protocol validated in P0, off by default) ----
    float leds               = 0.0f;    // 1 = LED emphasis active
    float leds_brightness    = 38.0f;   // 0-255 (~15 %)

    // ---- Sound (§3.9 — chirps, off by default) ----
    float sound              = 0.0f;    // 1 = chirps active
    float sound_volume       = 96.0f;   // 0-255
    float sound_volume_night = 32.0f;   // 0-255: volume used at night — from
                                        // real SUNSET to real SUNRISE at
                                        // lat/lon below (see SoundFx.h)

    // ---- Where the robot is (08-01). TWO uses, and both were broken without
    // it: the night volume fired on a fixed 22 h-6 h window read off an RTC
    // that NOTHING in this project ever set, and the same "is it night"
    // question is already answered properly by the radar bin from a real solar
    // position. One notion of night for both binaries, `firmware/common/
    // SunClock.h`, unit-tested natively.
    // Live-tunable like everything else here: moving the robot is a setting,
    // not a rebuild. Defaults = the station the radar defaults to.
    float lat = -20.89f;                // degrees, + = north
    float lon =  55.53f;                // degrees, + = east

    // ---- Sound tracking (ES7210 stereo mics, off by default) ----
    // The head turns toward the noise until both mic levels balance out.
    // The left/right mapping sign is live-tunable (same philosophy as
    // gyro_*: HW validation without reflashing).
    // soundtrack_* defaults = FIELD-VALIDATED settings (user 2026-07-16:
    // sign -1, thr 100, step 30)
    // THE MICROPHONE AND WHAT LISTENS TO IT ARE TWO SETTINGS. `mic_enable`
    // says the ES7210 pair runs at all; `sound_track` says the head follows
    // what it hears. They used to be one, so the band's sound visualiser could
    // not be watched without the head swinging at every noise, and there was
    // no way to say "listen, but stay still".
    //
    // `sound_track` still IMPLIES the microphone — a card written before this
    // key existed carries `sound_track: 1` alone, and must keep working
    // exactly as it did.
    float mic_enable         = 0.0f;    // 1 = the microphone captures
    float sound_track        = 0.0f;    // 1 = head turns toward the noise
                                        //     (implies mic_enable)
    float soundtrack_thr     = 100.0f;  // SENSITIVITY: triggering RMS
                                        // threshold (low = sensitive; the
                                        // ×2 ambient gate is the real filter)
    // +1/-1: left/right direction of the sound turn.
    // -1 is the value VERIFIED ON HARDWARE (user, 2026-07-30): claps on either
    // side, head tracking them correctly. It was 1.0 here, and that is what
    // REGRESSED the feature — the v3 migration re-defaulted the sign to +1 on
    // the assumption that the 07-17 channel-mapping fix cancelled the need for
    // it. On this hardware the two do NOT cancel: the current mapping needs -1.
    //
    // Note the comment four lines above: it already recorded "sign -1" as
    // field-validated on 2026-07-16. The 07-17 change overrode a HARDWARE
    // verdict with a code-reading argument, one day later, and the evidence
    // against it was sitting in this very file.
    //
    // HONEST OPEN QUESTION: reading the chain end to end (feed() labels
    // channel 1 as the RIGHT mic, imb > 0 = right louder, Units.h:135 says
    // "yaw < YAW_CENTER = viewer's left") predicts +1. The robot says
    // otherwise, so ONE of those three statements is inverted and we have not
    // yet found which. The sign stays a tunable precisely for that reason —
    // but do NOT "correct" it back to +1 from the reasoning alone: that is the
    // mistake that has now been made twice.
    float soundtrack_sign    = -1.0f;
    float soundtrack_step_deg = 30.0f;  // DISTANCE: max yaw per step (°) —
                                        // actual step ∝ √(imbalance)
    float soundtrack_move_ms  = 400.0f; // base SPEED: step duration (ms) —
                                        // modulated ×1.4 (faint/barely
                                        // lateralized sound) to ×0.4
                                        // (loud/clearly sided)
    float soundtrack_shock_thr = 4000.0f; // STARTLE: RMS level triggering the
                                        // shocked dance before the turn
                                        // (0 = disabled)
    // ---- Return to home (generalized 2026-07-17, ex-soundtrack_return_ms):
    // after head_home_ms with NO head activity at all (dance, pickup, sound
    // tracking, head-follow, remote control) and no sound event, the head
    // ALWAYS returns to home (the emotion posture is kept).
    float head_home_ms       = 8000.0f;  // 0 = never

    // ---- GC0308 camera (Home Assistant / Frigate, off by default) ----
    // Init DEFERRED until requested (first /api/camera/* access), never at
    // boot (anti-brick A2.20). Sensor settings applied live (SCCB through
    // M5.In_I2C). The GC0308 is not very sensitive → defaults pushed toward
    // brightness.
    float camera             = 0.0f;    // 1 = /api/camera/* active
    float cam_fps            = 10.0f;   // capture/stream cap (1-15 fps)
    float cam_quality        = 12.0f;   // STILL JPEG quality (1-63, lower =
                                        // better) — /api/camera/still.jpg?full=1
    float cam_stream_quality = 45.0f;   // JPEG quality of the STREAM/live view
                                        // (1-63, lower = better): more
                                        // COMPRESSED by default → frames ~4x
                                        // lighter → fast sends, responsive
                                        // commands (the stream shares a single
                                        // WiFi/AsyncTCP with the API —
                                        // 2026-07-20)
    float cam_stream_qvga    = 1.0f;    // 1 = stream/view DOWNSAMPLED to
                                        // 320x240 (the console displays about
                                        // 300 px: VGA is wasted) → ~4x fewer
                                        // bytes on the air AND ~4x less
                                        // encoding CPU. 0 = VGA (fine for
                                        // Frigate). The ?full=1 still stays VGA.
    float cam_brightness     = 1.0f;    // exposure (AEC target, -2..+2;
                                        // +1 = backlight balance validated HW)
    float cam_contrast       = 0.0f;    // contrast (-2..+2; 0 = calibration)
    float cam_saturation     = 1.0f;    // saturation (-2..+2; +1 validated HW)
    float cam_vflip          = 0.0f;    // vertical mirror (0/1 — 0/0 =
                                        // orientation validated HW 2026-07-18)
    float cam_hmirror        = 0.0f;    // horizontal mirror (0/1)
    float cam_lowlight       = 0.0f;    // 1 = low-light mode: raises the AEC
                                        // digital-gain ceiling (0xee=x4) and
                                        // exposure-level ceiling (0xec) —
                                        // GC0308 datasheet
    float cam_colorbar       = 0.0f;    // 1 = sensor test pattern
                                        // (diagnostic: bars OK = capture chain
                                        // is good, black image = optics/
                                        // exposure, not the firmware)

    // ---- Misc ----
    float dark_sleepy        = 1.0f;   // 1 = NIGHT MODE (LTR-553): full dark
                                       // sustained ~6 s → the ROULETTE swaps
                                       // Normal for Sleepy (dominant weight
                                       // ~66 %) — the robot dozes off, while
                                       // reflexes/dances/API keep priority.
                                       // Light returns → day mode (wake-up
                                       // blink if the face is Sleepy).
                                       // Hysteresis: night ≤1 %, day >10 %.
    // SCREEN brightness, the whole panel — distinct from `eye_color_dim`,
    // which only darkens the eye palette and leaves the status band, the
    // launcher and every guest bin at full blast. One knob per thing: this is
    // the backlight, that is the ink.
    float screen_bright      = 76.0f;  // panel backlight, 10..255. Ignored
                                       // while auto_brightness is on, and
                                       // setting it from the console turns
                                       // that off — a manual value that the
                                       // sensor overwrites within two seconds
                                       // is a control that does nothing.
    float auto_brightness    = 0.0f;   // 1 = automatic screen brightness
                                       // (LTR-553); 0 = fixed brightness.
                                       // Adjusted about every 2 s, only on
                                       // change (A2.2: never setBrightness
                                       // per frame).
    float led_swap           = 0.0f;   // 1 = swap the left/right LED bars
                                       // (if the L/R wiring is reversed).
    // ---- The bars as a DEPTH channel (ROADMAP §10) ----
    // The two bars of six run PERPENDICULAR to the screen, so their spare axis
    // is depth. `led_depth` is the gain in %, 0 = off and the payload is then
    // byte-identical to what the bars received before the feature existed.
    // While the head turns, the far end of each bar DARKENS — the light is
    // never added, because a bar can legitimately sit at brightness 255 and
    // anything above it would clip.
    float led_depth          = 0.0f;   // 0-200 %, 0 = off (default)
    float led_depth_front    = 1.0f;   // 1 = LED index 0 is at the FRONT.
                                       // NOT measured yet (§10 open question):
                                       // the depth twin of led_swap, so that a
                                       // reversed wiring stays telling apart
                                       // from wrong arithmetic.

    // ---- Status band ----
    float band_mode          = 0.0f;   // dynamic-zone mode (0=none 2=vu
                                       // 3=gauges 4=timer 5=pomodoro) —
                                       // PERSISTED (survives reboot).
    float icon_mask          = 31.0f;  // visible icons (bits: batt=1 wifi=2
                                       // cam=4 mic=8 night=16; 31 = all).
    float band_text_size     = 1.0f;   // say/alert text size (1..3).
    float band_scroll_speed  = 70.0f;  // marquee scroll speed (px/s).
    float band_debug         = 1.0f;   // 1 = emotion·ip info centered in the
                                       // icon row (independent option).
    // Pomodoro (band mode 5, engine/BandTimer.h). Clamped in the machine
    // (1-120 / 1-60 / 1-8): a zero-length work block would ring forever.
    float pomo_work_min      = 25.0f;  // work block (min)
    float pomo_break_min     = 5.0f;   // break block (min)
    float pomo_cycles        = 4.0f;   // work blocks before AllDone
    // Hydration prompt, inserted between a work block and its break. 0 = OFF
    // and the machine then behaves exactly as it did before the phase
    // existed — the only one of the four that accepts zero, because it is an
    // addition rather than part of what a pomodoro is (BandTimer::hydraS).
    float pomo_hydra_min     = 1.0f;   // drink break (min), 0 = off, max 15
    // A DANCE at the end of a countdown: 0 = none, else the 1-based index in
    // the dance list. The alarm already has a face (Excited, star eyes); this
    // is for the robot that has to be noticed from across a room. Fires on
    // Ring (timer) and AllDone (pomodoro) — never on the phase changes, which
    // happen too often to be worth standing up for.
    float timer_dance        = 0.0f;   // 0 = none, 1..N = dance index
    // Wall clock in the band when mode = 0 (user 08-04). The robot only
    // knows UTC (NTP; no timezone by design — the sun drives the night),
    // so a DISPLAY clock needs the local offset, display-only.
    // Which of the three SOUND styles the band draws (mode 2). A DISPLAY
    // choice like clock_24h, not a capability: all three read the same two
    // microphones and differ only in how a column is painted.
    float band_sound         = 0.0f;   // 0 = wave, 1 = columns, 2 = matrix
    // SENSITIVITY of the sound visualiser — a plain gain on the samples,
    // BEFORE the transform, so the trace and the spectrum move together. One
    // knob for the three styles: two would let them disagree about how loud
    // the room is. 1 = as measured, 2 = +6 dB, 0.5 = -6 dB. Needed because
    // the decibel floor is fixed at -48 dB and a room is not: a quiet office
    // never leaves the floor, a workshop pins every band.
    float band_sound_gain    = 1.0f;   // 0.1 .. 16
    float band_clock         = 0.0f;   // 1 = show HH:MM instead of black
    float clock_24h          = 1.0f;   // band clock: 24 h (1) or 12 h with
                                       // AM/PM (0). A display choice, not a
                                       // locale one: the same robot may be read
                                       // by people who disagree about it.
    float tz_offset_h        = 0.0f;   // local display offset, DECIMAL HOURS
                                       // (±14). Same key and unit as the guest
                                       // bins: three programs with two units for
                                       // one idea was a divergence with no upside
                                       // (user 08-04). Decimals cover every real
                                       // zone -- India 5.5, Nepal 5.75.

    // ---- Debug ----
    float telemetry          = 0.0f;   // 1 = serial stream in serial-plotter format
    // CROSS-ORIGIN READS, off by default and deliberately so.
    //
    // The choreography editor (`tools/choregraphies/`) is a local file:// page,
    // so every fetch it makes at the robot is cross-origin and the browser
    // drops the answer unless the robot says otherwise. That is the ONLY thing
    // standing between the editor and `GET /api/servo/pos` — the endpoint that
    // reports the pose a released head has been PUT INTO by hand.
    //
    // Why a switch and not a default: `Access-Control-Allow-Origin: *` lets
    // any page the browser happens to be showing talk to the robot on the local
    // network, and with Basic Auth off — which is the default — that includes
    // making it dance or reboot. Off, the robot is reachable only by something
    // already served from it. On, you have decided.
    //
    // Read ONCE at boot (see WebApi::begin): the header list is global to the
    // server and add-only, so flipping this takes a restart, like the WiFi
    // credentials next to it.
    float cors               = 0.0f;   // 1 = Access-Control-Allow-Origin: *

    // ---- Debug trace (2026-08-15) ----
    // 1 = sce::trace narrates every step on serial: network joins, config
    // reads, HTTP attempts, SD writes. LIVE (loop() syncs the flag each
    // tick) and persisted, because the moments that need it are the moments
    // a reflash is unavailable or would destroy the evidence.
    float debug              = 0.0f;

    // ---- Schema version (migration, reviewed 2026-07-17) ----
    // config.yaml persists the WHOLE table: a yaml saved by an old build
    // FREEZES the defaults of its era. cfg_version timestamps the schema —
    // at load time, if the yaml is older than the current version, the keys
    // RECALIBRATED since are discarded and fall back to the new defaults
    // (SdConfig::load). Bump CFG_VERSION on every default recalibration and
    // complete the migration in SdConfig.
    // v3 (2026-07-17): mic channel mapping fixed → soundtrack_sign
    // re-defaulted (+1).
    // v4 (2026-07-30): that +1 was WRONG on hardware and had silently broken
    // sound tracking — the head turned away from the sound. Verified value: -1
    // (claps on both sides, tracked correctly). Every config still carrying the
    // v3 default must therefore be re-defaulted, hence this bump.
    // v5 (08-03): `servo_idle_release_ms` recalibrated 15000 -> 4000. The bump
    // is the WHOLE fix on a robot that already has a card. `SdConfig::save()`
    // persists the entire table, so every device that has ever taken a
    // `POST /api/tuning` carries the old 15 s AND `cfg_version: 4`; without a
    // v5 the migration chain stops at `< 4.0f`, the persisted value wins over
    // the compiled default, and the neck goes on being held — on exactly the
    // robots that have the bug. Editing the template in `sdcard/` only helps a
    // freshly copied card (review 08-03).
    static constexpr float CFG_VERSION = 5.0f;
    float cfg_version        = CFG_VERSION;

    // ------------------------------------------------------------------
    // Name → field table (API + YAML persistence). nullptr = end of table.
    // ------------------------------------------------------------------
    struct Entry { const char* key; float Tuning::*field; };
    static const Entry* table() {
        static const Entry T[] = {
            { "eye_color_dim",    &Tuning::eye_color_dim    },
            { "eye_spacing",      &Tuning::eye_spacing      },
            { "eye_depth_scale",  &Tuning::eye_depth_scale  },
            { "blink_lag_ms",     &Tuning::blink_lag_ms     },
            { "crt_glow_dim",     &Tuning::crt_glow_dim     },
            { "crt_glow_px",      &Tuning::crt_glow_px      },
            { "saccade_ms",       &Tuning::saccade_ms       },
            { "fixation_min_ms",  &Tuning::fixation_min_ms  },
            { "fixation_max_ms",  &Tuning::fixation_max_ms  },
            { "blink_median_ms",  &Tuning::blink_median_ms  },
            { "personality",      &Tuning::personality      },
            { "roulette",         &Tuning::roulette         },
            { "head_follow",      &Tuning::head_follow      },
            { "headfollow_hold_ms", &Tuning::headfollow_hold_ms },
            { "servo_idle_release_ms", &Tuning::servo_idle_release_ms },
            { "servos",           &Tuning::servos           },
            { "leds",             &Tuning::leds             },
            { "leds_brightness",  &Tuning::leds_brightness  },
            { "sound",            &Tuning::sound            },
            { "sound_volume",     &Tuning::sound_volume     },
            { "sound_volume_night", &Tuning::sound_volume_night },
            { "lat",              &Tuning::lat              },
            { "lon",              &Tuning::lon              },
            { "mic_enable",       &Tuning::mic_enable       },
            { "sound_track",      &Tuning::sound_track      },
            { "soundtrack_thr",   &Tuning::soundtrack_thr   },
            { "soundtrack_sign",  &Tuning::soundtrack_sign  },
            { "soundtrack_step_deg", &Tuning::soundtrack_step_deg },
            { "soundtrack_move_ms",  &Tuning::soundtrack_move_ms  },
            { "soundtrack_shock_thr", &Tuning::soundtrack_shock_thr },
            { "head_home_ms",         &Tuning::head_home_ms         },
            { "camera",           &Tuning::camera           },
            { "cam_fps",          &Tuning::cam_fps          },
            { "cam_quality",      &Tuning::cam_quality      },
            { "cam_stream_quality", &Tuning::cam_stream_quality },
            { "cam_stream_qvga",  &Tuning::cam_stream_qvga  },
            { "cam_brightness",   &Tuning::cam_brightness   },
            { "cam_contrast",     &Tuning::cam_contrast     },
            { "cam_saturation",   &Tuning::cam_saturation   },
            { "cam_vflip",        &Tuning::cam_vflip        },
            { "cam_hmirror",      &Tuning::cam_hmirror      },
            { "cam_lowlight",     &Tuning::cam_lowlight     },
            { "cam_colorbar",     &Tuning::cam_colorbar     },
            { "vor_gain",         &Tuning::vor_gain         },
            { "vor_drift_alpha",  &Tuning::vor_drift_alpha  },
            { "vor_mag_alpha",    &Tuning::vor_mag_alpha    },
            { "saccade_recentre", &Tuning::saccade_recentre },
            { "shake_gyro_thr",   &Tuning::shake_gyro_thr   },
            { "gyro_yaw_axis",    &Tuning::gyro_yaw_axis    },
            { "gyro_yaw_sign",    &Tuning::gyro_yaw_sign    },
            { "gyro_pitch_axis",  &Tuning::gyro_pitch_axis  },
            { "gyro_pitch_sign",  &Tuning::gyro_pitch_sign  },
            { "pickup_dev_g",     &Tuning::pickup_dev_g     },
            { "pickup_hold_ms",   &Tuning::pickup_hold_ms   },
            { "dark_sleepy",      &Tuning::dark_sleepy      },
            { "screen_bright",    &Tuning::screen_bright    },
            { "auto_brightness",  &Tuning::auto_brightness  },
            { "led_swap",         &Tuning::led_swap         },
            { "led_depth",        &Tuning::led_depth        },
            { "led_depth_front",  &Tuning::led_depth_front  },
            { "band_mode",        &Tuning::band_mode        },
            { "icon_mask",        &Tuning::icon_mask        },
            { "band_text_size",   &Tuning::band_text_size   },
            { "band_scroll_speed",&Tuning::band_scroll_speed},
            { "band_debug",       &Tuning::band_debug       },
            { "pomo_work_min",    &Tuning::pomo_work_min    },
            { "pomo_break_min",   &Tuning::pomo_break_min   },
            { "pomo_cycles",      &Tuning::pomo_cycles      },
            { "pomo_hydra_min",   &Tuning::pomo_hydra_min   },
            { "timer_dance",      &Tuning::timer_dance      },
            { "band_sound",       &Tuning::band_sound       },
            { "band_sound_gain",  &Tuning::band_sound_gain  },
            { "band_clock",       &Tuning::band_clock       },
            { "clock_24h",        &Tuning::clock_24h        },
            { "tz_offset_h",      &Tuning::tz_offset_h      },
            { "telemetry",        &Tuning::telemetry        },
            { "cors",             &Tuning::cors             },
            { "debug",            &Tuning::debug            },
            { "cfg_version",      &Tuning::cfg_version      },
            { nullptr, nullptr },
        };
        return T;
    }

    // ------------------------------------------------------------------
    // OLD names, still accepted on the WRITE path — a card written by an
    // earlier firmware, an HA script that was not updated. Renaming a
    // persisted key without an alias makes every card already in the field
    // fall silently back to the default (the launch_poll_min lesson).
    //
    // Deliberately NOT in `table()`: that table is what `SdConfig::save()` and
    // `GET /api/tuning` ENUMERATE. An alias listed there is written back to
    // the card as a second key holding the same value, and returned as a
    // second field to every API consumer — a migration aid that turns itself
    // into a permanent duplicate, which is the opposite of the point.
    // ------------------------------------------------------------------
    static const Entry* aliases() {
        static const Entry A[] = {
            { "band_mouth",       &Tuning::band_sound       },
            { nullptr, nullptr },
        };
        return A;
    }

    // The field a key names — canonical name or alias — or nullptr. The ONE
    // place a name is resolved, so the API, the card and the alias list can
    // never disagree about what a key means.
    static float Tuning::* fieldFor(const char* key) {
        for (const Entry* e = table();   e->key; e++)
            if (strcmp(e->key, key) == 0) return e->field;
        for (const Entry* e = aliases(); e->key; e++)
            if (strcmp(e->key, key) == 0) return e->field;
        return nullptr;
    }

    // Write by key (API). Returns false if the key is unknown.
    bool set(const char* key, float value) {
        float Tuning::* f = fieldFor(key);
        if (!f) return false;
        // The `band_mouth` alias carried the KEY across the rename but the
        // VALUES were renumbered underneath it: the old scale was 0=lips,
        // 1=wave, 2=matrix; the new one is 0=wave, 1=columns, 2=matrix. Loaded
        // raw, a card that chose Wave got Columns and one that chose the
        // deleted Lips got Wave by accident — the silent style change the
        // alias exists to prevent. Old 0 and 1 both map to Wave (the nearest
        // survivor of Lips, and Wave's own new number); 2 stays Matrix.
        if (strcmp(key, "band_mouth") == 0)
            value = (value >= 1.5f) ? 2.0f : 0.0f;
        this->*f = value;
        return true;
    }
};

} // namespace sce
