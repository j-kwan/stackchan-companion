#pragma once
// =============================================================================
// EyeRig.h — StackChan-Companion (engine)
// =============================================================================
// ONE complete eye: geometry + animation chain + emotion application +
// drawing. Merges two responsibilities:
//   - the Config → Transition → Transformation → Variations → Draw chain,
//     ported from esp32-eyes, AGPL-3.0 (Luis Llamas). The original Blink
//     stage (trapezium) was removed: eyelids now go through the `lid`
//     channel (BlinkController → Renderer → lookAtPx), bottom-anchored by
//     default (centred mode for the 12 "round" emotions — A2.17).
//   - the emotion→preset resolution + the special Dead (X) and Excited
//     (star ✦) renders
//
// The Renderer owns two EyeRig instances (mirrored left / right) and drives
// them from the FaceState snapshot. EyeRig knows NEITHER the tasks NOR the
// expression registry: setEmotion / lookAtPx / tick / draw, that is all.
//
// UNITS: EyeRig lives in engine/ → pixels allowed (CONVENTIONS §2).
// lookAtPx expects PIXELS already converted (the Renderer applies
// units::pxFromGaze) and the near/far "witchcraft" scaleY stays with the
// caller: EyeRig is purely mechanical.
//
// SAFE STATE: Config is initialized to Preset_Normal at construction and
// EyeTransition starts as a no-op → the first frame is ALWAYS well formed.
// =============================================================================

#include <M5GFX.h>
#include "Clock.h"
#include "EyeConfig.h"
#include "EyeDrawer.h"
#include "Transitions.h"
#include "Animations.h"
#include "Emotions.h"
#include "Units.h"
#include "presets/EyePresetsM5.h"
#include "presets/EyePresetsExtended.h"

namespace sce {

// ------------------------------------------------------------------
// EyeTransformation — gaze (MoveX/Y) + scale (ScaleX/Y), applied DIRECTLY.
// A 200 ms ramp restarted on every SetDestin acted as a low-pass filter on
// values ALREADY smoothed by the Brain (saccades, blenders, VOR,
// openRatio): blink reduced to a few pixels, mushy wink, invisible VOR
// compensation. The Brain is the ONLY source of smoothing for continuous
// channels — here we apply as-is (rule A2.15, ROADMAP.md).
// Radii are scaled proportionally to the scale (known bug: "the corner
// roundings overflow when an eye is small"); normalize() (EyeGeometry)
// remains the safety net downstream.
// ------------------------------------------------------------------
struct EyeTransformData {
    float MoveX = 0.0f, MoveY = 0.0f, ScaleX = 1.0f, ScaleY = 1.0f;
    float Lid   = 1.0f;   // eyelid opening [0..1] — channel SEPARATE from
                          // scale: closing is ANCHORED AT THE BOTTOM (a lid
                          // that falls, user 2026-07-12), while squash/depth
                          // stay centered
};

class EyeTransformation {
public:
    explicit EyeTransformation(EyeConfig* input) : Input(input) {}

    EyeConfig*       Input;
    EyeConfig        Output;
    EyeTransformData Current;

    // Closing anchor (set by EyeRig::setEmotion, 2026-07-16):
    //   - default (LidCenter=false): FALLING lid, BOTTOM edge fixed
    //     (Happy/Glee/Sleepy/... — the historical A2.17 invariant);
    //   - LidCenter=true: the closing CONVERGES towards LidAnchorY = the
    //     vertical center of the SMALLEST eye of the pair (user request:
    //     Normal, Surprised, Awe, Nervous, Excited, Questioning, Curious,
    //     Doubt, Contempt, Smug, Dead, Squint) — both eyes close on the
    //     SAME line, wink included.
    bool    LidCenter  = false;
    int16_t LidAnchorY = 0;    // screen offset (relative to CenterY)

    void Set(const EyeTransformData& t) { Current = t; }

    void Update() {
        Output = *Input;
        float lid       = clampVal(Current.Lid, 0.0f, 1.0f);
        float effScaleY = Current.ScaleY * lid;
        // Falling lid (default): the BOTTOM edge stays FIXED — the center
        // moves down by half the height eaten by the lid (the top comes
        // down, the bottom never rises). CENTERED mode: the center SLIDES
        // towards the anchor line as the eye closes.
        // The squash (ScaleY) stays centered in both cases.
        float lidDrop = LidCenter
            ? (LidAnchorY - Input->OffsetY) * (1.0f - lid)
            : Input->Height * Current.ScaleY * (1.0f - lid) * 0.5f;
        Output.OffsetX = (int16_t)(Input->OffsetX + Current.MoveX);
        Output.OffsetY = (int16_t)(Input->OffsetY - Current.MoveY + lidDrop);
        Output.Width   = (int16_t)(Input->Width   * Current.ScaleX);
        Output.Height  = (int16_t)(Input->Height  * effScaleY);
        // Radii ∝ scale (the smallest axis leads — keeps the character of
        // the shape, avoids full-size corners on a shrunk body)
        float rs = Current.ScaleX < effScaleY ? Current.ScaleX : effScaleY;
        if (rs < 0.0f) rs = 0.0f;
        Output.Radius_Top            = (int16_t)(Input->Radius_Top            * rs);
        Output.Radius_Bottom         = (int16_t)(Input->Radius_Bottom         * rs);
        Output.Radius_Top_Outer      = (int16_t)(Input->Radius_Top_Outer      * rs);
        Output.Radius_Bottom_Outer   = (int16_t)(Input->Radius_Bottom_Outer   * rs);
        Output.Inverse_Radius_Top    = (int16_t)(Input->Inverse_Radius_Top    * rs);
        Output.Inverse_Radius_Bottom = (int16_t)(Input->Inverse_Radius_Bottom * rs);
    }
};

// ------------------------------------------------------------------
// EyeVariation — periodic oscillation (breathing, bounce, tremble)
// Values holds the amplitudes; the pulse animation modulates in [-1, +1].
// ------------------------------------------------------------------
class EyeVariation {
public:
    EyeVariation(const Clock& clock, EyeConfig* input,
                 uint32_t t0, uint32_t t1, uint32_t t2, uint32_t t3, uint32_t t4)
        : Input(input), Animation(clock, t0, t1, t2, t3, t4) {}

    EyeConfig*              Input;
    EyeConfig               Output;
    TrapeziumPulseAnimation Animation;
    EyeConfig               Values;   // amplitudes (default: all zero = no-op)

    void Clear() { Values = EyeConfig{}; }

    void Update(float scale = 1.0f) {
        float t = (2.0f * Animation.GetValue() - 1.0f) * scale;
        Output = *Input;
        Output.OffsetX += (int16_t)(Values.OffsetX * t);
        Output.OffsetY += (int16_t)(Values.OffsetY * t);
        Output.Height  += (int16_t)(Values.Height  * t);
        Output.Width   += (int16_t)(Values.Width   * t);
        Output.Slope_Top    += Values.Slope_Top    * t;
        Output.Slope_Bottom += Values.Slope_Bottom * t;
        Output.Radius_Top   += (int16_t)(Values.Radius_Top    * t);
        Output.Radius_Bottom+= (int16_t)(Values.Radius_Bottom * t);
    }
};

// (An old EyeBlink stage — 40/100/40 ms trapezium — was removed: never
//  triggered, blink goes through the BlinkController `lid` channel —
//  bottom-anchored + Cozmo line. History: git.)

// ==================================================================
// EyeRig — the complete eye
// ==================================================================
class EyeRig {
public:
    EyeRig(const Clock& clock, bool isLeft, int16_t centerX, int16_t centerY)
        : CenterX(centerX), CenterY(centerY), IsMirrored(isLeft),
          _transition(clock, &Config),
          _transformation(&Config),
          _variation1(clock, &_transformation.Output, 200, 200, 200, 200, 0),
          _variation2(clock, &_variation1.Output,       0, 200, 200, 200, 200)
    {
        // COMPLETE initial state before the first frame (banding fix §2.1):
        // Config = Preset_Normal, no-op transition, variations at zero.
        Config = mirrored(Preset_Normal);
        _transition.Destin = Config;
        _variation1.Animation.Interval = 800;
        _variation2.Animation.Interval = 800;
        setEmotion(Normal, DefaultTransitions::NORMAL);
    }

    int16_t CenterX, CenterY;
    bool    IsMirrored;      // left eye: mirrored geometry (offsets/slopes)
    EyeConfig Config;        // live geometry (written by the transition)

    // ------------------------------------------------------------------
    // Emotion → preset + variations (_Alt asymmetries for the left eye,
    // per-emotion variations, default transitions). The special
    // Dead/Excited renders are resolved in draw().
    // `mirrorAsym` (2026-07-16): global RANDOM mirror — drawn by the Brain
    // at every emotion episode (and aligned with the mirrored direction of
    // the current dance): the eye "carrying" the asymmetry switches side,
    // slopes and OffsetX follow (EFFECTIVE leftness = IsMirrored XOR flip).
    // ------------------------------------------------------------------
    void setEmotion(eEmotions e, const TransitionConfig& tcfg,
                    bool mirrorAsym = false) {
        _emotion  = e;
        _asymFlip = mirrorAsym;
        _dynAlt   = false;          // dynamic variant (Sad sky / Curious)
        _transformation.LidCenter  = false;   // default: falling lid
        _transformation.LidAnchorY = 0;
        _variation1.Clear();
        _variation2.Clear();
        _variation1.Animation.Restart();
        _variation2.Animation.Restart();

        const bool L = (IsMirrored != _asymFlip);   // effective leftness
        switch (e) {
        case Normal:
            lidCenterOn(Preset_Normal_Alt);
            // Eyes vertically CENTERED; one of the two (random side via
            // the global mirror) is SLIGHTLY smaller in height
            // (Preset_Normal_Alt 92 vs 100 — 3rd user verdict 2026-07-16).
            // SYNCHRONIZED breathing (same period, same phase — a phase
            // shift made the motion look "strange"), slightly different
            // amplitudes. Width identical, untouched.
            _variation1.Values.Height = L ? SCALE_3 : SCALE_5;
            _variation1.Animation.SetTriangle(1000, 0);
            transitionTo(L ? Preset_Normal_Alt : Preset_Normal,
                         DefaultTransitions::NORMAL);                      return;
        case Happy:       transitionTo(Preset_Happy, tcfg);                return;
        case Blush:
            // Happy eyes (soft arcs) — the blushing cheeks are an overlay
            // (EyeEffects, drawn by the Renderer). Slight shy vertical
            // oscillation.
            _variation1.Values.OffsetY = SCALE_2;
            _variation1.Animation.SetTriangle(900, 0);
            transitionTo(Preset_Happy, tcfg);                              return;
        case Glee:
            _variation1.Values.OffsetY = SCALE_5;
            _variation1.Animation.SetTriangle(300, 0);
            transitionTo(Preset_Glee, tcfg);                               return;
        case Sad:
            // Starting preset — the "looking at the sky" variant (Scary
            // shape when the eyes go up) is handled dynamically by
            // updateDynamicPreset() (user 2026-07-16).
            transitionTo(Preset_Sad, tcfg);                                return;
        case Sleepy:
            // SYNCHRONIZED breathing (same period/phase — 2nd user
            // verdict: a phase shift looks "strange"), slightly asymmetric
            // amplitudes. The Sleepy ASYNCHRONY is carried by the eyelid
            // (BlinkController right-eye modulation), not by the breathing.
            // BOTTOM ANCHOR: the height oscillates and the center rises by
            // HALF of it in opposition — the BOTTOM edge does not move.
            _variation1.Values.Height  = L ? 6 : 8;
            _variation1.Values.OffsetY = L ? -3 : -4;   // = -Height/2: bottom fixed
            _variation1.Animation.SetTriangle(2000, 0);
            transitionTo(L ? Preset_Sleepy_Alt : Preset_Sleepy, tcfg);     return;
        case Worried:     transitionTo(L ? Preset_Worried_Alt     : Preset_Worried, tcfg);     return;
        case Focused:     transitionTo(Preset_Focused, tcfg);              return;
        case Annoyed:     transitionTo(L ? Preset_Annoyed_Alt     : Preset_Annoyed, tcfg);     return;
        case Suspicious:  transitionTo(L ? Preset_Suspicious_Alt  : Preset_Suspicious, tcfg);  return;
        case Skeptic:     transitionTo(L ? Preset_Skeptic_Alt     : Preset_Skeptic, tcfg);     return;
        case Frustrated:  transitionTo(Preset_Frustrated, tcfg);           return;
        case Unimpressed: transitionTo(L ? Preset_Unimpressed_Alt : Preset_Unimpressed, tcfg); return;
        case Nervous:
            // ex-Squint, renamed (user 2026-07-16). Fast horizontal tremble
            // of both eyes (150 ms, asymmetric) — the narrowing scrutinizes.
            // The small eye is PULLED IN (preset OffsetX) to keep a near
            // standard spacing despite its width of 50.
            lidCenterOn(Preset_Nervous_Alt);
            _variation1.Values.OffsetX = L ? 7 : 4;
            _variation1.Animation.SetTriangle(150, 0);
            transitionTo(L ? Preset_Nervous_Alt : Preset_Nervous, tcfg);   return;
        case Squint:
            // NEW Squint (user 2026-07-16): concentrated narrowing —
            // Focused base without the top slope, bottom slope (center low /
            // outer high), reduced height.
            lidCenterOn(Preset_Squint);
            transitionTo(Preset_Squint, tcfg);                             return;
        case Angry:
            _variation1.Values.OffsetY = SCALE_2;
            _variation1.Animation.SetTriangle(300, 0);
            transitionTo(Preset_Angry, tcfg);                              return;
        case Furious:     transitionTo(Preset_Furious, tcfg);              return;
        case Surprised:
            lidCenterOn(Preset_Surprised);
            transitionTo(Preset_Surprised, tcfg);                          return;
        case Scared:      transitionTo(Preset_Scared, tcfg);               return;
        case Awe:
            lidCenterOn(Preset_Awe);
            transitionTo(Preset_Awe, tcfg);                                return;
        case Excited:
            // Preset waypoint: the eye widens, then draw() renders the star
            lidCenterOn(Preset_Excited);
            transitionTo(Preset_Excited, tcfg);                            return;
        case Questioning:
            lidCenterOn(Preset_Questioning_Normal);
            transitionTo(L ? Preset_Questioning_Normal
                           : Preset_Questioning_Big, tcfg);                return;
        case Frozen:      transitionTo(Preset_Frozen, tcfg);               return;
        case Scary:       transitionTo(Preset_Scary, tcfg);                return;
        case Curious:
            // STATIC asymmetry right from entry (like Questioning/Contempt/
            // Worried...) — LOST on 2026-07-16 when the dynamic switch (gaze
            // towards the edge, updateDynamicPreset) REPLACED the base
            // instead of adding to it: at rest (centered gaze, the common
            // pickup case) both eyes stayed identical. Regression reported by
            // the user 2026-07-17 ("something is missing from Curious").
            lidCenterOn(Preset_Curious_Normal);
            transitionTo(L ? Preset_Curious_Normal : Preset_Curious_Big, tcfg); return;
        case Doubt:
            lidCenterOn(Preset_Doubt);
            transitionTo(Preset_Doubt, tcfg);                              return;
        case Contempt:
            lidCenterOn(Preset_Contempt_Squint);
            transitionTo(L ? Preset_Contempt_Normal
                           : Preset_Contempt_Squint, tcfg);                return;
        case Disgust:     transitionTo(Preset_Disgust, tcfg);              return;
        case Smug:
            lidCenterOn(Preset_Smug_Closed);
            transitionTo(L ? Preset_Smug_Normal
                           : Preset_Smug_Closed, tcfg);                    return;
        case Dead:
            lidCenterOn(Preset_Dead);
            transitionTo(Preset_Dead, tcfg);                               return;
        default:          transitionTo(Preset_Normal, DefaultTransitions::NORMAL); return;
        }
    }

    // ------------------------------------------------------------------
    // Gaze in PIXELS + scales (near/far witchcraft × squash, centered)
    // + `lid` (eyelid opening 0..1 — falling lid by default, centred closing
    // when LidCenter is set by setEmotion, A2.17).
    // DIRECT application — the values arrive already smoothed by the Brain
    // (100 Hz), no filtering here.
    // ------------------------------------------------------------------
    void lookAtPx(float moveX, float moveY,
                  float scaleY = 1.0f, float scaleX = 1.0f, float lid = 1.0f) {
        EyeTransformData t;
        t.MoveX = moveX; t.MoveY = moveY; t.ScaleX = scaleX; t.ScaleY = scaleY;
        t.Lid   = lid;
        _transformation.Set(t);
    }

    // Advances transitions/variations without drawing (the Renderer's "closed
    // line" mode: emotion morphs keep running while the eyes are shut)
    void tick() { update(); }

    // Screen Y of the eye's current BOTTOM edge: where the slit ends its
    // travel whatever the anchor mode — falling lid (fixed bottom, the value
    // IS the anchor) or centered closing (the eye collapses onto the
    // LidAnchorY line: at lid→0 the bottom edge ≈ that line). The Renderer
    // aligns the "both eyes closed" line on it (slit → line continuity; on
    // FLAT-bottomed presets — Happy/Glee/Blush... — it coincides with the
    // first pixel of the eye's bottom). By construction: bottom = transformed
    // center + Height/2 whatever the stage of the closing.
    int16_t bottomEdgeY(int16_t breathPx = 0) const {
        const EyeConfig& o = _transformation.Output;
        return (int16_t)(CenterY + breathPx + o.OffsetY + o.Height / 2);
    }

    float transitionProgress() const { return _transition.Progress(); }
    eEmotions emotion()        const { return _emotion; }

    // Height the eye is actually DRAWN at (px) — after emotion, transform
    // (squash + lid) and variations (last stage of the chain).
    // Used to lock the LED bar brightness onto the SIZE of the eye:
    // Surprised (112) = max, Curious/Questioning asymmetric, blink → 0.
    int16_t currentHeight() const { return _variation2.Output.Height; }

    // ------------------------------------------------------------------
    // Draw — advances the chain, applies the inter-eye constraint, draws.
    // fg       : RGB888 color already interpolated/dimmed by the caller.
    // breathPx : vertical breathing offset (FaceState channel).
    // glowPx   : if > 0, CRT halo — the eye is first drawn dilated by glowPx
    //            in glowColor (pass UNDER the sharp drawing, §3.8).
    // ------------------------------------------------------------------
    void draw(M5Canvas* canvas, uint32_t fg, int16_t breathPx = 0,
              int16_t glowPx = 0, uint32_t glowColor = 0) {
        update();
        EyeConfig cfg = _variation2.Output;

        // ---- Inter-eye spacing constraint ----
        // Clamps the width so the inner edges never cross over.
        // FIX 2026-07-11 ("one eye shrinks in width", PLAYBOOK-HW
        // §3.6/§2.7): the midline FOLLOWS the common gaze offset (MoveX is
        // identical on both eyes) — a clamp against the FIXED middle of the
        // screen shrank the eye on the gaze side as soon as the pair shifted
        // (VOR, gaze), even though the real gap between the eyes had not
        // changed.
        const int16_t MID = (int16_t)(units::SCREEN_W / 2
                                      + _transformation.Current.MoveX);
        const int16_t HALF_GAP = units::EYE_GAP / 4;
        if (IsMirrored) {   // left eye: inner edge = CX + Off + W/2
            int16_t maxW = (int16_t)(2 * (MID - HALF_GAP - CenterX - cfg.OffsetX));
            if (maxW > 4 && cfg.Width > maxW) cfg.Width = maxW;
        } else {            // right eye: inner edge = CX + Off - W/2
            int16_t maxW = (int16_t)(2 * (CenterX + cfg.OffsetX - MID - HALF_GAP));
            if (maxW > 4 && cfg.Width > maxW) cfg.Width = maxW;
        }

        const int16_t cy = (int16_t)(CenterY + breathPx);

        // ---- CRT halo (glow): same shape dilated, dimmed color, UNDER the
        //      sharp drawing. No halo on the special renders (star/X: their
        //      solid shapes stand on their own).
        if (glowPx > 0 && _emotion != Excited && _emotion != Dead) {
            EyeConfig g = cfg;
            g.Width         = (int16_t)(g.Width  + 2 * glowPx);
            g.Height        = (int16_t)(g.Height + 2 * glowPx);
            g.Radius_Top    = (int16_t)(g.Radius_Top    + glowPx);
            g.Radius_Bottom = (int16_t)(g.Radius_Bottom + glowPx);
            EyeDrawer::colorFg = glowColor;
            EyeDrawer::Draw(canvas, CenterX, cy, g);
        }

        EyeDrawer::colorFg = fg;

        // ---- Special renders ----
        if (_emotion == Excited) {
            // Star ALONE (fix 2026-07-12): the Preset_Excited rect drawn
            // underneath let yellow show past the concave cutouts —
            // artifacts above/below the points.
            // The star grows from nothing (scale = progress) on a black
            // background, and follows squash + lid (ScaleY × Lid).
            float p     = transitionProgress();
            float scale = (p >= 1.0f) ? (1.0f + breathPx * 0.04f) : p;
            float lidS  = _transformation.Current.ScaleY
                        * _transformation.Current.Lid;
            if (lidS < 0.0f) lidS = 0.0f;
            if (lidS > 1.3f) lidS = 1.3f;
            int16_t size = (int16_t)(EXCITED_STAR_HALF * scale * lidS);
            if (size < 8) size = 8;   // star invisible below 8 px
            // SIGN: `+MoveY = up` (convention of EyeTransformation::Update:
            // `OffsetY = Input->OffsetY - MoveY`). The star used to ADD
            // MoveY: it went down when the gaze went up, the opposite of the
            // whole rest of the face (review finding 2026-07-29).
            drawStarEye(canvas,
                        CenterX + (int32_t)_transformation.Current.MoveX,
                        cy      - (int32_t)_transformation.Current.MoveY,
                        size, fg);
            return;
        }
        if (_emotion == Dead) {
            if (transitionProgress() < 1.0f) {
                EyeDrawer::Draw(canvas, CenterX, cy, cfg);  // eye closing
            } else {
                // The cross follows the gaze like the rest of the face — it
                // used to ignore it completely, the only render in the
                // firmware left nailed to the center while the neighbouring
                // eyes moved.
                drawDeadXEye(canvas,
                             CenterX + (int32_t)_transformation.Current.MoveX,
                             cy      - (int32_t)_transformation.Current.MoveY,
                             DEAD_EYE_HALF, fg);
            }
            return;
        }

        EyeDrawer::Draw(canvas, CenterX, cy, cfg);
    }

private:
    eEmotions         _emotion  = Normal;
    bool              _asymFlip = false;  // global random mirror (per episode)
    bool              _dynAlt   = false;  // dynamic variant active
    EyeTransition     _transition;
    EyeTransformation _transformation;
    EyeVariation      _variation1;
    EyeVariation      _variation2;

    // Variation amplitudes (scale factor ×2.5 vs the source)
    static constexpr int16_t SCALE_2 = 5, SCALE_3 = 7, SCALE_5 = 12;
    // Special renders (hardware-validated values)
    static constexpr int16_t DEAD_EYE_HALF     = 22;   // half-size of the Dead cross
    static constexpr int16_t EXCITED_STAR_HALF = 55;

    // Left/right mirror — EVERYTHING here is ANATOMICAL (PHYSICAL eye,
    // IsMirrored): the global random mirror (_asymFlip) governs ONLY the
    // preset/variation SELECTION in setEmotion (which eye gets the Alt),
    // NEVER the mirror geometry. Lesson 2026-07-16: applying the flip to the
    // slopes tilted Angry & co towards the OUTSIDE of the face (symmetric
    // presets — same preset on both eyes — must stay physically mirrored
    // whatever the flip).
    //   - Slopes: presets are written for the LEFT eye (+Slope_Top = INNER
    //     corner low) — inverted for the right eye.
    //   - OffsetX: preset +X = "towards the center of the face" — inverted on
    //     the right (the small Nervous eye moves in from BOTH sides).
    //   - OuterIsLeft: screen-edge side.
    // EQUIDISTANCE (amended 2026-07-16): OffsetX = 0 by preset DISCIPLINE
    // (no neutralization here any more) — documented exception Nervous_Alt.
    // Outer radii resolved HERE (0 = inherit) so the transitions interpolate
    // real values.
    EyeConfig mirrored(const EyeConfig& p) const {
        EyeConfig c = p;
        c.OffsetX      = IsMirrored ? p.OffsetX : (int16_t)-p.OffsetX;
        c.OffsetY      = (int16_t)-p.OffsetY;
        c.Slope_Top    = IsMirrored ?  p.Slope_Top    : -p.Slope_Top;
        c.Slope_Bottom = IsMirrored ?  p.Slope_Bottom : -p.Slope_Bottom;
        if (c.Radius_Top_Outer    == 0) c.Radius_Top_Outer    = c.Radius_Top;
        if (c.Radius_Bottom_Outer == 0) c.Radius_Bottom_Outer = c.Radius_Bottom;
        c.OuterIsLeft  = IsMirrored ? 1 : 0;
        return c;
    }

    void transitionTo(const EyeConfig& preset, const TransitionConfig& tcfg) {
        _transition.SetDestin(mirrored(preset), tcfg);
    }

    // CENTERED closing (user 2026-07-16): blinks/winks converge towards the
    // vertical center of the SMALLEST eye of the pair — pass the smallest
    // preset of the emotion here. Screen offset = -preset OffsetY (same sign
    // inversion as mirrored(), independent of the side).
    void lidCenterOn(const EyeConfig& smallest) {
        _transformation.LidCenter  = true;
        _transformation.LidAnchorY = (int16_t)-smallest.OffsetY;
    }

    // ------------------------------------------------------------------
    // DYNAMIC preset variants (2026-07-16) — the target preset depends on the
    // current gaze, with hysteresis (no flapping at the threshold):
    //   - Sad: eyes going up (MoveY > 0 = up) → Scary shape (height +
    //     inverted bevel) = "looking at the sky"; back to the Sad preset when
    //     the gaze comes down.
    //   - Curious: the eye CLOSEST TO THE EDGE on the gaze side (viewer-
    //     centric: +X = right → right eye) becomes square and bigger.
    // Real gaze amplitudes: ±10 px in Y, ±25 px in X (Units.h).
    // ------------------------------------------------------------------
    void updateDynamicPreset() {
        if (_emotion == Sad) {
            // LOW entry threshold (3 px ≈ 30 % of the vertical travel): idle
            // fixations are biased towards the center (r²) — at 5 px the
            // variant almost never showed up ("the variant is missing",
            // user 2026-07-16). Hysteresis 1.5 px > jitter (±0.75 px).
            float my = _transformation.Current.MoveY;      // +MoveY = up
            if (!_dynAlt && my > 3.0f) {
                _dynAlt = true;
                transitionTo(Preset_Scary, DefaultTransitions::SOFT);
            } else if (_dynAlt && my < 1.5f) {
                _dynAlt = false;
                transitionTo(Preset_Sad, DefaultTransitions::SOFT);
            }
        } else if (_emotion == Curious) {
            float mx = _transformation.Current.MoveX;      // +X = viewer right
            float toEdge = IsMirrored ? -mx : mx;          // >0 = towards MY edge
            if (!_dynAlt && toEdge > 12.0f) {
                _dynAlt = true;
                transitionTo(Preset_Curious_Big, DefaultTransitions::NORMAL);
            } else if (_dynAlt && toEdge < 6.0f) {
                _dynAlt = false;
                transitionTo(Preset_Curious_Normal, DefaultTransitions::NORMAL);
            }
        }
    }

    // Advances the whole chain. Variations damped at the start of a
    // transition (avoids the visible jump on an expression change).
    void update() {
        updateDynamicPreset();
        _transition.Update();
        _transformation.Update();
        float p        = transitionProgress();
        float varScale = p < 0.3f ? p / 0.3f : 1.0f;
        _variation1.Update(varScale);
        _variation2.Update(varScale);
    }

    // 4-pointed star ✦: diamond + 4 concave circular cutouts
    static void drawStarEye(M5Canvas* canvas, int32_t cx, int32_t cy,
                            int16_t halfSize, uint32_t color) {
        const int32_t R  = halfSize;
        const uint32_t bg = EyeDrawer::colorBg;
        canvas->fillTriangle(cx, cy - R, cx - R, cy, cx + R, cy, color);
        canvas->fillTriangle(cx, cy + R, cx - R, cy, cx + R, cy, color);
        canvas->fillCircle(cx + R, cy - R, R, bg);
        canvas->fillCircle(cx + R, cy + R, R, bg);
        canvas->fillCircle(cx - R, cy - R, R, bg);
        canvas->fillCircle(cx - R, cy + R, R, bg);
    }

    // "Dead" X 😵: two thick arms with rounded edges, stamped with fillCircle
    // (round caps for free) in ONE SINGLE loop with strictly POSITIVE
    // offsets — see the constraints below.
    // Two NON-NEGOTIABLE constraints, both proven on target by reading the
    // canvas buffer back over serial (diagnosis 2026-07-25):
    //   1. NO drawWideLine (anti-aliased): 4 AA bars/frame = ~57 ms
    //      (> the 33 ms budget) → the renderer task stops yielding and
    //      STARVES the touch polling of loop() (lower priority, SAME core 1)
    //      → no swipe received at all while Dead is on screen.
    //   2. ONE SINGLE drawing call site: GCC 8.4 Xtensa DROPS from the binary
    //      the SECOND of two similar drawing calls in the same body — a
    //      "bar A→B" helper called 2× (internal counter: 2 increments/frame
    //      instead of 4) JUST LIKE two fillCircle per loop iteration (2nd arm
    //      absent from the buffer). Hence the ALTERNATING loop below (even i
    //      = arm \, odd = arm /), validated pixel by pixel on target (both
    //      arms at 0x6F in the buffer).
    static void drawDeadXEye(M5Canvas* canvas, int32_t cx, int32_t cy,
                             int16_t half, uint32_t color) {
        int32_t rad = (int32_t)(half * 0.34f + 0.5f);   // half-thickness of the stroke
        if (rad < 4) rad = 4;
        // Step ~rad/4: heavy overlap of the circles → SMOOTH edges (a step of
        // ~rad/2 left visible notches — scalloping of the union of circles,
        // user 07-25). ~×2 the stamps, still cheap (scanline).
        const int32_t n = (8 * half) / rad + 1;
        for (int32_t i = 0; i <= 2 * n + 1; ++i) {
            int32_t j = i >> 1;
            int32_t o = (2 * half) * j / n;             // 0..2·half, increasing
            int32_t y = (i & 1) ? (cy + half - o) : (cy - half + o);
            // Caps = SAME radius as the body (user 07-25: bigger caps stick
            // out of the stroke) — natural half-circles from the stamping.
            canvas->fillCircle(cx - half + o, y, rad, color);
        }
    }
};

} // namespace sce
