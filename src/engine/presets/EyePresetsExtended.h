#pragma once
// =============================================================================
// EyePresetsExtended.h — StackChan-Companion (engine/presets)
// =============================================================================
// The 11 expressions missing from esp32-eyes (the original 9, then Dead and
// the new Squint), defined in the rounded rectangular style (consistent with
// EyePresetsM5.h)
//
// WHY A SEPARATE FILE FROM EyePresetsM5.h: the entries over there descend from
// the upstream tables (scaled ×2.5, then retouched by hand), so comparing one
// against its source still means something. The presets below have no upstream
// at all — they were authored here. Keeping the two apart is what keeps that
// distinction legible; nothing else distinguishes them, the struct and the
// units are the same.
//
// A preset is DATA, never behaviour: both preset headers are included by
// engine/EyeRig.h and by NOTHING else, and the emotion → EyeConfig resolution
// happens in the single switch of EyeRig::setEmotion. Adding an expression
// means adding a table below plus one case there — never a branch anywhere
// upstream in behavior/.
//
// Inspiration sources:
//   - Grobot_Animations : Excited, Questioning (IDLE states)
//   - RoboEyes          : Frozen, Scary, Curious (special modes)
//   - m5stack-avatar    : Doubt (kept as-is)
//   - Originals         : Contempt, Disgust, Smug
//
// ASYMMETRIC EXPRESSIONS come as a PAIR of presets, one per eye, and the
// suffix here names the ROLE rather than a side (EyePresetsM5.h uses an `_Alt`
// suffix instead): Questioning_Big/_Normal, Curious_Big/_Normal,
// Contempt_Squint/_Normal, Smug_Closed/_Normal. EyeRig chooses between the two
// from its leftness flag; WHICH eye ends up carrying the asymmetry is the
// BRAIN's decision (FaceState.asymMirror, A2.17) — a preset never encodes it.
//
// CONVENTIONS SHARED with EyePresetsM5.h (read that header first):
//   - presets are written for the LEFT eye; EyeRig::mirrored flips slopes and
//     OffsetX for the right one
//   - Slope_Top/Bottom are dimensionless ratios: NOT scaled
//   - EQUIDISTANCE (A2.17): OffsetX = 0 in every preset below, no exception —
//     the gap between eye centres must not move from one emotion to the next
//
// Parameters (all in pixels, ×2.5 vs the 128×64 source):
//   OffsetX/Y   : offset from the center
//   Height/Width: eye dimensions
//   Slope_Top/Bottom : tilt (dimensionless ratio)
//   Radius_Top/Bottom: corner radius
//   Inverse_Radius_* : concave (inward) corners (0 = disabled)
// =============================================================================

#include "../EyeConfig.h"

// ------------------------------------------------------------------
// Excited — wide-open eyes, high brows (Grobot EXCITED)
// Source: setBase(baseL, 0, 0, 0, 23, 45) → very shrunken pupil
// ------------------------------------------------------------------
static const sce::EyeConfig Preset_Excited = {
    /* OffsetX             */ 0,
    /* OffsetY             */ 5,    // slightly upward
    /* Height              */ 110,
    /* Width               */ 112,
    /* Slope_Top           */ 0.00f,
    /* Slope_Bottom        */ 0.00f,
    /* Radius_Top          */ 35,
    /* Radius_Bottom       */ 35,
    /* Inverse_Radius_Top  */ 0,
    /* Inverse_Radius_Bot  */ 0,
    /* Inverse_Offset_Top  */ 0,
    /* Inverse_Offset_Bot  */ 0
};

// ------------------------------------------------------------------
// Questioning — asymmetric: left eye big, right eye normal
// Source: Grobot QUESTIONING setBase(baseR, 50, 0, 0, 30, 45)
// Preset for the right eye (the big one, raised brow)
// Slope_Top/Radius_Top taken from Preset_Worried_Alt (user 2026-07-17:
// "it comes apart on the slope", Worried's junction judged perfect) — the old
// Radius_Top 30 did not close cleanly against the -0.15 slope.
// Height/Width aligned on Curious (user 2026-07-17: "same size as
// Curious") — Big 104×105, Normal 68×92/radius18.
// ------------------------------------------------------------------
static const sce::EyeConfig Preset_Questioning_Big = {
    /* OffsetX             */ 0,
    /* OffsetY             */ -8,
    /* Height              */ 104,
    /* Width               */ 105,
    /* Slope_Top           */ -0.20f,
    /* Slope_Bottom        */ 0.00f,
    /* Radius_Top          */ 15,
    /* Radius_Bottom       */ 25,
    /* Inverse_Radius_Top  */ 0,
    /* Inverse_Radius_Bot  */ 0,
    /* Inverse_Offset_Top  */ 0,
    /* Inverse_Offset_Bot  */ 0
};

// Preset for the left eye (normal)
static const sce::EyeConfig Preset_Questioning_Normal = {
    /* OffsetX             */ 0,
    /* OffsetY             */ 0,
    /* Height              */ 68,
    /* Width               */ 92,
    /* Slope_Top           */ 0.00f,
    /* Slope_Bottom        */ 0.00f,
    /* Radius_Top          */ 18,
    /* Radius_Bottom       */ 18,
    /* Inverse_Radius_Top  */ 0,
    /* Inverse_Radius_Bot  */ 0,
    /* Inverse_Offset_Top  */ 0,
    /* Inverse_Offset_Bot  */ 0
};

// ------------------------------------------------------------------
// Frozen — fixed stare, half-closed eyes (RoboEyes FROZEN)
// Partially closed eyes, no slope, frozen look
// ------------------------------------------------------------------
static const sce::EyeConfig Preset_Frozen = {
    /* OffsetX             */ 0,
    /* OffsetY             */ 0,
    /* Height              */ 45,
    /* Width               */ 100,
    /* Slope_Top           */ 0.00f,
    /* Slope_Bottom        */ 0.00f,
    /* Radius_Top          */ 8,
    /* Radius_Bottom       */ 8,
    /* Inverse_Radius_Top  */ 0,
    /* Inverse_Radius_Bot  */ 0,
    /* Inverse_Offset_Top  */ 0,
    /* Inverse_Offset_Bot  */ 0
};

// ------------------------------------------------------------------
// Scary — menacing, inverted triangular eyes (RoboEyes SCARY)
// Strong negative slope: the top corners drop toward the inside
// ------------------------------------------------------------------
static const sce::EyeConfig Preset_Scary = {
    /* OffsetX             */ 0,
    /* OffsetY             */ 0,
    /* Height              */ 55,
    /* Width               */ 100,
    /* Slope_Top           */ -0.45f,
    /* Slope_Bottom        */ 0.00f,
    /* Radius_Top          */ 3,
    /* Radius_Bottom       */ 20,
    /* Inverse_Radius_Top  */ 0,
    /* Inverse_Radius_Bot  */ 0,
    /* Inverse_Offset_Top  */ 0,
    /* Inverse_Offset_Bot  */ 0
};

// ------------------------------------------------------------------
// Curious — one eye bigger (RoboEyes curious heightOffset)
// Big preset: the eye closest to the EDGE on the gaze side (dynamic
// switching in EyeRig::updateDynamicPreset, user 2026-07-16) — AND STATIC
// asymmetry right from entry (EyeRig::setEmotion, fix 2026-07-17).
// Dimensions reverted 2026-07-17 (user: "rectangular eyes"): the square
// 112×112 / radius 22 (T8) rounded proportionally LESS than the
// original (22/56=39% vs 30/52.5=57%). Then a 2nd tweak (user: "reduce
// the height of the bigger eye"): Height 120→104.
// ------------------------------------------------------------------
static const sce::EyeConfig Preset_Curious_Big = {
    /* OffsetX             */ 0,
    /* OffsetY             */ 0,
    /* Height              */ 104,
    /* Width               */ 105,
    /* Slope_Top           */ 0.00f,
    /* Slope_Bottom        */ 0.00f,
    /* Radius_Top          */ 30,
    /* Radius_Bottom       */ 30,
    /* Inverse_Radius_Top  */ 0,
    /* Inverse_Radius_Bot  */ 0,
    /* Inverse_Offset_Top  */ 0,
    /* Inverse_Offset_Bot  */ 0
};

// Normal preset (inner eye) — slightly reduced (user 2026-07-17,
// echoing the shrunken big eye): Height 75→68, Width 100→92, Radius 20→18.
static const sce::EyeConfig Preset_Curious_Normal = {
    /* OffsetX             */ 0,
    /* OffsetY             */ 0,
    /* Height              */ 68,
    /* Width               */ 92,
    /* Slope_Top           */ 0.00f,
    /* Slope_Bottom        */ 0.00f,
    /* Radius_Top          */ 18,
    /* Radius_Bottom       */ 18,
    /* Inverse_Radius_Top  */ 0,
    /* Inverse_Radius_Bot  */ 0,
    /* Inverse_Offset_Top  */ 0,
    /* Inverse_Offset_Bot  */ 0
};

// ------------------------------------------------------------------
// Doubt — inspired by m5stack-avatar (≈ Skeptic but slightly
// different: left eye normal, right eye slightly narrowed)
// ------------------------------------------------------------------
static const sce::EyeConfig Preset_Doubt = {
    /* OffsetX             */ 0,
    /* OffsetY             */ 0,
    /* Height              */ 65,
    /* Width               */ 95,
    /* Slope_Top           */ 0.08f,
    /* Slope_Bottom        */ 0.00f,
    /* Radius_Top          */ 15,
    /* Radius_Bottom       */ 22,
    /* Inverse_Radius_Top  */ 0,
    /* Inverse_Radius_Bot  */ 0,
    /* Inverse_Offset_Top  */ 0,
    /* Inverse_Offset_Bot  */ 0
};

// ------------------------------------------------------------------
// Contempt — one narrowed eye (right), one normal (left)
// Strong asymmetry: right eye heavily narrowed, with slope
// ------------------------------------------------------------------
static const sce::EyeConfig Preset_Contempt_Squint = {
    /* OffsetX             */ 0,
    /* OffsetY             */ -5,
    /* Height              */ 28,
    /* Width               */ 95,
    /* Slope_Top           */ 0.25f,
    /* Slope_Bottom        */ 0.00f,
    /* Radius_Top          */ 3,
    /* Radius_Bottom       */ 18,
    /* Inverse_Radius_Top  */ 0,
    /* Inverse_Radius_Bot  */ 0,
    /* Inverse_Offset_Top  */ 0,
    /* Inverse_Offset_Bot  */ 0
};

static const sce::EyeConfig Preset_Contempt_Normal = {
    /* OffsetX             */ 0,
    /* OffsetY             */ 0,
    /* Height              */ 75,
    /* Width               */ 100,
    /* Slope_Top           */ 0.00f,
    /* Slope_Bottom        */ 0.00f,
    /* Radius_Top          */ 20,
    /* Radius_Bottom       */ 20,
    /* Inverse_Radius_Top  */ 0,
    /* Inverse_Radius_Bot  */ 0,
    /* Inverse_Offset_Top  */ 0,
    /* Inverse_Offset_Bot  */ 0
};

// ------------------------------------------------------------------
// Disgust — half-closed eyes.
// Slope_Bottom -0.15 -> 0 (2026-07-12): a "line at the bottom" showed up at
// the slope/corner junction (user report — same family as Sleepy/Scared/Awe)
// ------------------------------------------------------------------
static const sce::EyeConfig Preset_Disgust = {
    /* OffsetX             */ 0,
    /* OffsetY             */ -3,
    /* Height              */ 28,
    /* Width               */ 100,
    /* Slope_Top           */ 0.00f,
    /* Slope_Bottom        */ 0.00f,
    /* Radius_Top          */ 0,
    /* Radius_Bottom       */ 18,
    /* Inverse_Radius_Top  */ 0,
    /* Inverse_Radius_Bot  */ 0,
    /* Inverse_Offset_Top  */ 0,
    /* Inverse_Offset_Bot  */ 0
};

// ------------------------------------------------------------------
// Smug — asymmetric Glee (one eye narrowed, the other normal)
// 2nd user verdict 2026-07-16: eyes vertically CENTERED (the bottom
// alignment of the 1st pass set the narrowed eye too low) — OffsetY 0
// everywhere.
// ------------------------------------------------------------------
static const sce::EyeConfig Preset_Smug_Closed = {
    /* OffsetX             */ 0,
    /* OffsetY             */ 0,
    /* Height              */ 18,
    /* Width               */ 100,
    /* Slope_Top           */ 0.05f,
    /* Slope_Bottom        */ 0.00f,
    /* Radius_Top          */ 18,
    /* Radius_Bottom       */ 0,
    /* Inverse_Radius_Top  */ 0,
    /* Inverse_Radius_Bot  */ 8,
    /* Inverse_Offset_Top  */ 0,
    /* Inverse_Offset_Bot  */ 0
};

// ------------------------------------------------------------------
// Dead — flat bar (transition target before the X rendering)
// The eye closes → then EyeRig::draw() renders the X in its place
// (drawDeadXEye, once the transition completes). Height 5 = the visible
// thickness of the closing bar while the transition is still running.
// ------------------------------------------------------------------
static const sce::EyeConfig Preset_Dead = {
    /* OffsetX             */ 0,
    /* OffsetY             */ 0,
    /* Height              */ 5,
    /* Width               */ 70,
    /* Slope_Top           */ 0.00f,
    /* Slope_Bottom        */ 0.00f,
    /* Radius_Top          */ 0,
    /* Radius_Bottom       */ 0,
    /* Inverse_Radius_Top  */ 0,
    /* Inverse_Radius_Bot  */ 0,
    /* Inverse_Offset_Top  */ 0,
    /* Inverse_Offset_Bot  */ 0
};

// ------------------------------------------------------------------
// Squint — NEW (user 2026-07-16, replaces the ex-Squint that became Nervous):
// concentrated narrowing. Focused base (35×100, corners 8/2) WITHOUT the top
// slope, WITH a bottom slope: +0.28 = MIDDLE OF THE FACE (inner corners) at
// the LOWEST point, outer corners high.
// Sign history: the 1st "invert it" verdict was given while the mirror bug
// was still present (slopes flipped outward) — once the anatomical mirror was
// fixed, the POSITIVE sign is the right one (user re-verdict).
// ------------------------------------------------------------------
static const sce::EyeConfig Preset_Squint = {
    /* OffsetX             */ 0,
    /* OffsetY             */ 0,
    /* Height              */ 28,
    /* Width               */ 100,
    /* Slope_Top           */ 0.00f,
    /* Slope_Bottom        */ 0.28f,
    /* Radius_Top          */ 8,
    /* Radius_Bottom       */ 2,
    /* Inverse_Radius_Top  */ 0,
    /* Inverse_Radius_Bot  */ 0,
    /* Inverse_Offset_Top  */ 0,
    /* Inverse_Offset_Bot  */ 0
};

static const sce::EyeConfig Preset_Smug_Normal = {
    /* OffsetX             */ 0,
    /* OffsetY             */ 0,
    /* Height              */ 55,
    /* Width               */ 100,
    /* Slope_Top           */ 0.10f,
    /* Slope_Bottom        */ 0.00f,
    /* Radius_Top          */ 20,
    /* Radius_Bottom       */ 15,
    /* Inverse_Radius_Top  */ 0,
    /* Inverse_Radius_Bot  */ 0,
    /* Inverse_Offset_Top  */ 0,
    /* Inverse_Offset_Bot  */ 0
};
