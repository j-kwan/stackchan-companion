/* Choreography editor — StackChan
 * ---------------------------------------------------------------------------
 * Composes a dance and produces the CSV expected by `app/DanceStore.h`, format
 * described in docs/reference/CHOREGRAPHIES.md:
 *
 *     yaw,pitch,servoMs,holdMs,emotion,lid,gazeY
 *
 * The simulator guesses nothing. It replays the firmware chain:
 *   EyeRig::setEmotion   picks a preset PER EYE (12 emotions are
 *                        asymmetric) and the eyelid closing anchor
 *   EyeRig::mirrored     always flips OffsetY, plus OffsetX and the slopes for
 *                        the right eye, and resolves the outer radii
 *   eyegeom::normalize   applies the radius invariants
 *   EyeDrawer::Draw      traces the shape, one radius per corner
 * The data comes from presets.js, regenerated from the sources by
 * extract-presets.py — nothing is copied by hand.
 *
 * No dependency, no network: opens with a double-click on index.html.
 */
'use strict';

// ── Firmware limits ────────────────────────────────────────────────────────
// Taken from DanceStore (clamps at load time) and from Units.h. Showing them
// here avoids writing a CSV that the robot would silently clip.
const LIM = {
  // ±130 depuis le 2026-08-01 (etait ±40, un nombre sans source).
  // Borne DERIVEE : writeDeg n'adresse que 0-300 deg et le centre mesure
  // 166, donc l'enveloppe est +134/-166 et le symetrique vaut ±134 ;
  // 130 garde 4 deg de marge contre le clamp de conversion.
  yaw:     { min: -130, max: 130 },  // + = VIEWER's right
  pitch:   { min: -74, max:   6 },  // PITCH_MIN..MAX minus PITCH_NEUTRAL:
                                    // − raises the head, + LOWERS it (max +6)
  gazeY:   { min:  -1, max:   1 },
  servoMs: { min:   0, max: 1200 },
  holdMs:  { min:   0, max: 3000 },
};
const MAX_KEYS = 23;   // DanceStore::MAX_KEYS(24) − the exit keyframe
const MAX_NAME = 23;   // DanceStore::NAME_LEN(24) − the NUL
const LIDS = ['0', 'blink', 'winkG', 'winkD'];

// 320×160 eye zone and eye centers, taken from src/engine/Units.h.
const EYEZONE_W = 320, EYEZONE_H = 160;
const EYE_L_CX = 90, EYE_R_CX = 230, EYE_CY = 80;
const GAZE_MAX_Y = 0.20, GAZE_PX_Y = 50;   // vertical gaze SATURATES at ±0.20

// Eye color — it DEPENDS on the expression: yellow for joy, red-orange for
// anger, lavender for fear, white for surprise... Twelve hues for thirty
// emotions (`emotionToRgb`). The tool used to draw them all in cyan, which
// gave a false idea of what the robot actually shows.
//
// `dimRgb888` — global dimming of the palette, integer TRUNCATION as in C++:
// rounding would give one channel too many here and there, and a hue that
// does not exist on the screen.
function dimRgb(rgb, f) {
  if (f >= 1) return rgb;
  if (f <= 0) return 0;
  const c = (sh) => Math.trunc(((rgb >> sh) & 0xFF) * f);
  return (c(16) << 16) | (c(8) << 8) | c(0);
}
const toHex = (v) => '#' + (v >>> 0).toString(16).padStart(6, '0');
function eyeColor(emoName) {
  const rgb = window.EMOTION_RGB[emoName];
  return toHex(dimRgb(rgb === undefined ? 0x0096C8 : rgb, window.EYE_COLOR_DIM));
}

// ── SPECIAL renderings and overlays ────────────────────────────────────────
// Two emotions do not draw an eye but a SHAPE, alone on the background
// (invariant A2.17): Excited a star, Dead a cross. Drawing them like ordinary
// presets showed a rectangle where the robot shows something else — the most
// deceptive kind of mismatch, since nothing flagged the error.
// Constants and geometry taken from `engine/EyeRig.h` and
// `engine/EyeEffects.h`.
const EXCITED_STAR_HALF = 55, DEAD_EYE_HALF = 22;
const BLUSH_RGB = 0xF07898, SWEAT_RGB = 0x58B8F0, SPARKLE_RGB = 0xFFF0A0;
const HAS_BLUSH    = ['Blush', 'Glee', 'Smug'];
const HAS_SPARKLES = ['Excited', 'Awe'];
const HAS_SWEAT    = ['Scared', 'Worried', 'Frustrated'];

// Star ✦: a diamond, then four concave circular cut-outs.
//
// The two triangles used to share exactly the `y = cy` edge. Canvas
// antialiasing then makes EACH of them cover half of that pixel row, and two
// translucent halves do not recombine into a solid pixel: a dark seam ran
// right across the star (user report 07-30). So the TOP triangle pushes its
// base one pixel below the center — the two overlap and the seam disappears.
// The firmware does not need this: there `fillTriangle` draws SOLID pixels,
// and both triangles already include row `cy`.
const SEAM_PX = 1;
function drawStarEye(ctx, cx, cy, R, color) {
  ctx.save();
  ctx.fillStyle = color;
  for (const s of [-1, 1]) {
    const baseY = (s < 0) ? cy + SEAM_PX : cy;
    ctx.beginPath();
    ctx.moveTo(cx, cy + s * R);
    ctx.lineTo(cx - R, baseY); ctx.lineTo(cx + R, baseY);
    ctx.closePath(); ctx.fill();
  }
  // The firmware paints these cut-outs in the BACKGROUND color. Here we ERASE
  // instead: the background is the screen panel's, defined in CSS, and
  // repainting it here would create a second source to keep in sync.
  ctx.globalCompositeOperation = 'destination-out';
  for (const [sx, sy] of [[1, -1], [1, 1], [-1, -1], [-1, 1]]) {
    ctx.beginPath(); ctx.arc(cx + sx * R, cy + sy * R, R, 0, 2 * Math.PI); ctx.fill();
  }
  ctx.restore();
}

// "Dead" cross: two arms stamped with fillCircle, round caps for free.
// The firmware interleaves both arms in ONE loop to work around a GCC Xtensa
// bug (A2.22); here two loops give the same union of disks, that constraint
// being specific to the target's compiler.
function drawDeadXEye(ctx, cx, cy, half, color) {
  const rad = Math.max(4, Math.round(half * 0.34));
  const n = Math.trunc((8 * half) / rad) + 1;
  ctx.fillStyle = color;
  for (let j = 0; j <= n; j++) {
    const o = Math.trunc(2 * half * j / n);
    for (const y of [cy - half + o, cy + half - o]) {
      ctx.beginPath(); ctx.arc(cx - half + o, y, rad, 0, 2 * Math.PI); ctx.fill();
    }
  }
}

// Blushing cheeks: four thin strokes of unequal lengths under each eye,
// mirrored left/right, all parallel (the offset is proportional to the
// half-height, so the slope stays constant).
function drawBlush(ctx, fade, color, dx, anchorY) {
  const LENF = [0.60, 1.00, 0.85, 0.65];
  const baseLen = 18 * fade;
  if (baseLen < 4) return;
  const LEAN = 0.32, gap = 9;
  const cxs = [EYE_L_CX - 18 + dx, EYE_R_CX + 18 + dx];
  ctx.save();
  ctx.strokeStyle = color; ctx.lineWidth = 2.5; ctx.lineCap = 'round';
  for (let side = 0; side < 2; side++) {
    const dir = side === 0 ? 1 : -1;
    for (let i = 0; i < 4; i++) {
      const half = 0.5 * baseLen * LENF[i], lean = half * LEAN;
      const x = cxs[side] + (i - 1.5) * gap;
      ctx.beginPath();
      ctx.moveTo(x - dir * lean, anchorY + half);
      ctx.lineTo(x + dir * lean, anchorY - half);
      ctx.stroke();
    }
  }
  ctx.restore();
}

// Sparkles: three fixed positions, cycles offset by one third of the period.
function drawSparkles(ctx, nowMs, fade, color) {
  const POS = [[46, 34], [160, 24], [272, 38]], PERIOD = 1100;
  ctx.save(); ctx.strokeStyle = color; ctx.fillStyle = color; ctx.lineWidth = 1;
  for (let i = 0; i < 3; i++) {
    const t = ((nowMs + i * (PERIOD / 3)) % PERIOD) / PERIOD;
    let pulse = t < 0.5 ? t * 2 : (1 - t) * 2;
    pulse = pulse * pulse * (3 - 2 * pulse);
    const s = Math.trunc(7 * pulse * fade);
    if (s < 2) continue;
    const [x, y] = POS[i];
    ctx.fillRect(x, y - s, 1, 2 * s + 1);
    ctx.fillRect(x - s, y, 2 * s + 1, 1);
    const d = Math.trunc(s / 2);
    if (d > 0) {
      ctx.beginPath();
      ctx.moveTo(x - d, y - d); ctx.lineTo(x + d, y + d);
      ctx.moveTo(x - d, y + d); ctx.lineTo(x + d, y - d);
      ctx.stroke();
    }
  }
  ctx.restore();
}

// Sweat drop: beads up at the top right, grows then slides down while
// accelerating, vanishes, starts over — 2400 ms cycle.
function drawSweat(ctx, nowMs, fade, color) {
  const PERIOD = 2400;
  const t = (nowMs % PERIOD) / PERIOD;
  if (t > 0.85) return;                          // pause between two drops
  const x = EYE_R_CX + 62;
  let y, r;
  if (t < 0.40) { const g = t / 0.40;          y = 34; r = 2 + 3 * g; }
  else          { const g = (t - 0.40) / 0.45; y = 34 + Math.trunc(58 * g * g);
                  r = 5 - 1.5 * g; }
  r *= fade;
  if (r < 1.5) return;
  ctx.save();
  ctx.fillStyle = color;
  ctx.beginPath(); ctx.arc(x, y, Math.trunc(r), 0, 2 * Math.PI); ctx.fill();
  ctx.beginPath();                               // tip: teardrop shape
  ctx.moveTo(x - Math.trunc(r) + 1, y);
  ctx.lineTo(x + Math.trunc(r) - 1, y);
  ctx.lineTo(x, y - Math.trunc(r * 2.2));
  ctx.closePath(); ctx.fill();
  ctx.restore();
}

function drawOverlays(ctx, emoName, nowMs, dy) {
  const dim = window.EYE_COLOR_DIM;
  if (HAS_BLUSH.includes(emoName))
    drawBlush(ctx, 1, toHex(dimRgb(BLUSH_RGB, dim)), 0, EYE_CY + 48 + dy);
  if (HAS_SPARKLES.includes(emoName))
    drawSparkles(ctx, nowMs, 1, toHex(dimRgb(SPARKLE_RGB, dim)));
  if (HAS_SWEAT.includes(emoName))
    drawSweat(ctx, nowMs, 1, toHex(dimRgb(SWEAT_RGB, dim)));
}

// Play-button labels. Behind FUNCTIONS, not constants: they were written out at
// three places and had ALREADY diverged (the button opened on "▶ Play" and came
// back from playback as "▶ Play the sequence"), and they now also have to
// follow a language change — a constant captured at load time would keep the
// old language forever.
const playLabel = () => t('play');
const stopLabel = () => t('stop');

// ── State ──────────────────────────────────────────────────────────────────
let frames = [];
let sel = 0;
let playing = false, playT0 = 0;

const $ = (id) => document.getElementById(id);
const clamp = (v, lo, hi) => v < lo ? lo : (v > hi ? hi : v);

const newFrame = (from) => from ? { ...from }
  : { yaw: 0, pitch: 0, servoMs: 250, holdMs: 400,
      emotion: 'Normal', lid: '0', gazeY: 0 };

// REAL duration of a keyframe: the Sequencer clamps holdMs ≥ servoMs.
const dur = (f) => Math.max(f.holdMs, f.servoMs);

// An empty `emotion` column means "unchanged": the current expression is the
// one set by the last keyframe that specified any. This is the most common
// form of the format, not an edge case.
function effEmotion(i) {
  for (let k = i; k >= 0; k--) if (frames[k].emotion) return frames[k].emotion;
  return 'Normal';
}

// ── Transformation chain of a single eye ───────────────────────────────────
// EyeRig::mirrored — `isLeft` carries IsMirrored.
function mirrored(p, isLeft) {
  const c = { ...p };
  c.OffsetX      = isLeft ?  p.OffsetX : -p.OffsetX;
  c.OffsetY      = -p.OffsetY;                       // ALWAYS flipped
  c.Slope_Top    = isLeft ?  p.Slope_Top    : -p.Slope_Top;
  c.Slope_Bottom = isLeft ?  p.Slope_Bottom : -p.Slope_Bottom;
  if (!c.Radius_Top_Outer)    c.Radius_Top_Outer    = c.Radius_Top;
  if (!c.Radius_Bottom_Outer) c.Radius_Bottom_Outer = c.Radius_Bottom;
  c.OuterIsLeft  = isLeft ? 1 : 0;
  return c;
}

// eyegeom::normalize — invariants I1..I3 (I4/I5 have no effect here: no preset
// has a re-entrant radius nor a negative value).
function normalize(c) {
  c.Width = Math.max(0, c.Width);
  c.Height = Math.max(0, c.Height);
  const th = c.Height + Math.abs(Math.trunc(c.Height * c.Slope_Top / 2))
                      + Math.abs(Math.trunc(c.Height * c.Slope_Bottom / 2));
  const scale = (r) => { for (const k of ['Radius_Top', 'Radius_Bottom',
                                          'Radius_Top_Outer', 'Radius_Bottom_Outer'])
                           c[k] = Math.trunc(c[k] * r); };
  const rT = Math.max(c.Radius_Top, c.Radius_Top_Outer);
  const rB = Math.max(c.Radius_Bottom, c.Radius_Bottom_Outer);
  if (rT + rB > 0 && th - 1 < rT + rB) scale(th > 1 ? (th - 1) / (rT + rB) : 0);
  const maxR = Math.max(c.Radius_Top, c.Radius_Bottom,
                        c.Radius_Top_Outer, c.Radius_Bottom_Outer);
  const halfW = Math.trunc(c.Width / 2);
  if (maxR > halfW) scale(maxR > 0 ? halfW / maxR : 0);
  return c;
}

// EyeDrawer::Draw — outline, one radius PER CORNER (the outer corner may
// differ: that is the design point of Surprised and Awe).
function eyePath(ctx, cx, cy, c) {
  const dTop = Math.trunc(c.Height * c.Slope_Top    / 2);
  const dBot = Math.trunc(c.Height * c.Slope_Bottom / 2);
  const rTL = c.OuterIsLeft ? c.Radius_Top_Outer    : c.Radius_Top;
  const rTR = c.OuterIsLeft ? c.Radius_Top          : c.Radius_Top_Outer;
  const rBL = c.OuterIsLeft ? c.Radius_Bottom_Outer : c.Radius_Bottom;
  const rBR = c.OuterIsLeft ? c.Radius_Bottom       : c.Radius_Bottom_Outer;
  const x = cx + c.OffsetX, y = cy + c.OffsetY;
  const hw = c.Width / 2, hh = c.Height / 2;

  const TLx = x - hw + rTL, TLy = y - hh + rTL - dTop;
  const TRx = x + hw - rTR, TRy = y - hh + rTR + dTop;
  const BLx = x - hw + rBL, BLy = y + hh - rBL - dBot;
  const BRx = x + hw - rBR, BRy = y + hh - rBR + dBot;

  ctx.beginPath();
  ctx.moveTo(TLx, TLy - rTL);
  ctx.lineTo(TRx, TRy - rTR);
  if (rTR > 0) ctx.arcTo(TRx + rTR, TRy - rTR, TRx + rTR, TRy, rTR);
  ctx.lineTo(BRx + rBR, BRy);
  if (rBR > 0) ctx.arcTo(BRx + rBR, BRy + rBR, BRx, BRy + rBR, rBR);
  ctx.lineTo(BLx, BLy + rBL);
  if (rBL > 0) ctx.arcTo(BLx - rBL, BLy + rBL, BLx - rBL, BLy, rBL);
  ctx.lineTo(TLx - rTL, TLy);
  if (rTL > 0) ctx.arcTo(TLx - rTL, TLy - rTL, TLx, TLy - rTL, rTL);
  ctx.closePath();
}

// Closed eye. Two modes, like the firmware (invariant A2.17): the closing
// converges toward the CENTER of the smaller eye when the emotion asked for it
// (lidCenterOn), otherwise it lands on the BOTTOM edge. A 1 px line.
function drawClosed(ctx, cx, cy, c, lidAnchorY) {
  const y = (lidAnchorY !== null) ? cy + lidAnchorY
                                  : cy + c.OffsetY + c.Height / 2;
  ctx.fillRect(cx + c.OffsetX - c.Width / 2, Math.round(y), c.Width, 1);
}

// ── Rendering ──────────────────────────────────────────────────────────────
// The drawing REASONS in 320×160, the robot's frame of reference
// (`units::EYEZONE_*`), but the buffer is sized on the panel's ACTUAL displayed
// size. Otherwise the browser shrinks a 320 px wide image down to ~160 px, and
// `image-rendering:pixelated` turns that reduction into nearest-neighbour
// decimation: since the factor is not an integer (0.4988), the sampling grid
// DRIFTS along the image. The two arms of the Dead cross advance in x in
// OPPOSITE directions, so they accumulate opposite phases — one stays thick,
// the other is shaved down to a faint ~1 px stroke (user report 2026-07-29;
// the canvas content itself WAS symmetric: 26 stamps per arm, same radius,
// same color).
// Sizing the buffer on the display removes the resampling INSTEAD of
// compensating for its effects: it holds for every shape, at any panel size,
// and it makes the preview sharp on a HiDPI screen (where the old canvas went
// through TWO resamplings).
function eyeCtx() {
  const cv = $('eyes');
  const dpr = window.devicePixelRatio || 1;
  const w = Math.max(1, Math.round((cv.clientWidth  || EYEZONE_W) * dpr));
  const h = Math.max(1, Math.round((cv.clientHeight || EYEZONE_H) * dpr));
  // Reassigning width/height CLEARS the canvas: only do it when the size
  // actually changed, otherwise every playback frame would start from an empty
  // buffer.
  if (cv.width !== w || cv.height !== h) { cv.width = w; cv.height = h; }
  const ctx = cv.getContext('2d');
  ctx.setTransform(w / EYEZONE_W, 0, 0, h / EYEZONE_H, 0, 0);
  return ctx;
}

function drawEyes(f, emoName, showLid, nowMs) {
  const ctx = eyeCtx();
  // ERASE, do not repaint a background: the panel color is defined once, in
  // CSS (`--screen-bg`), and shows through under the canvas. Painting it here
  // would have made it a second source to keep in sync.
  ctx.clearRect(0, 0, EYEZONE_W, EYEZONE_H);

  const map = window.EMOTION_PRESETS[emoName];
  if (!map) return;
  const anchor = map.lidCenter && window.PRESETS[map.lidCenter]
               ? -window.PRESETS[map.lidCenter].OffsetY : null;

  // Vertical gaze SATURATES at ±0.20 before conversion to pixels: beyond that,
  // moving the slider changes nothing on the robot.
  const dy = -clamp(f.gazeY, -GAZE_MAX_Y, GAZE_MAX_Y) * GAZE_PX_Y;
  ctx.fillStyle = eyeColor(emoName);

  const closedL = showLid && (f.lid === 'blink' || f.lid === 'winkG');
  const closedR = showLid && (f.lid === 'blink' || f.lid === 'winkD');

  for (const [cx, isLeft, closed] of [[EYE_L_CX, true, closedL],
                                      [EYE_R_CX, false, closedR]]) {
    // SPECIAL RENDERINGS — drawn ALONE, never on top of a preset (A2.17).
    if (emoName === 'Excited') {
      // The firmware modulates the size by ScaleY × Lid and floors it at 8 px:
      // a "blinking" star shrinks without disappearing.
      drawStarEye(ctx, cx, EYE_CY + dy,
                  closed ? 8 : EXCITED_STAR_HALF, ctx.fillStyle);
      continue;
    }
    if (emoName === 'Dead') {
      // The cross ignores the eyelid event: the firmware leaves through this
      // path as soon as the transition is over, without reading the `lid`
      // channel.
      drawDeadXEye(ctx, cx, EYE_CY + dy, DEAD_EYE_HALF, ctx.fillStyle);
      continue;
    }
    const base = window.PRESETS[isLeft ? map.left : map.right];
    if (!base) continue;
    const c = normalize(mirrored(base, isLeft));
    if (c.Width <= 0 || c.Height <= 0) continue;   // isDrawable()
    if (closed) { drawClosed(ctx, cx, EYE_CY + dy, c, anchor); continue; }
    eyePath(ctx, cx, EYE_CY + dy, c);
    ctx.fill();
  }
  drawOverlays(ctx, emoName, nowMs, dy);
}

function render(f, emoName, showLid, nowMs) {
  // The cyclic overlays (sparkles, sweat drop) need a clock. When stopped we
  // freeze a REPRESENTATIVE phase rather than zero: at t=0 the drop is
  // invisible and the sparkles are at zero size, so the static preview would
  // have lied by omission.
  drawEyes(f, emoName, showLid, nowMs === undefined ? 900 : nowMs);
  // Viewer-centric convention: yaw + = observer's right, pitch − = head raised
  // (docs/architecture/CONVENTIONS.md).
  //
  // rotateX BEFORE rotateY: the head tilts in its own frame, then pivots —
  // that is the order of the real assembly (the pitch servo is CARRIED by the
  // yaw one). The reverse order rolls the head onto its side as soon as both
  // axes are engaged.
  $('head').style.transform = `rotateY(${f.yaw}deg) rotateX(${-f.pitch}deg)`;
  $('rYaw').textContent = f.yaw;
  $('rPitch').textContent = f.pitch;
  $('rGaze').textContent = f.gazeY.toFixed(2);
  // "End stop": the downward travel is only 6°, and without this indicator one
  // thinks the drag has let go when in fact the servo has hit its limit.
  $('sYaw').hidden   = f.yaw   > LIM.yaw.min   && f.yaw   < LIM.yaw.max;
  $('sPitch').hidden = f.pitch > LIM.pitch.min && f.pitch < LIM.pitch.max;
  $('band').textContent = emoName;
}

function renderSel() {
  const f = frames[sel];
  if (f) render(f, effEmotion(sel), f.lid !== '0');
}

// ── Interface ──────────────────────────────────────────────────────────────
function fillEmotions() {
  const s = $('emotion');
  s.innerHTML = '';
  s.append(new Option(t('unchanged_opt'), ''));
  // The 30 CANONICAL names. Offering the preset names would have allowed
  // writing values the firmware silently ignores.
  window.EMOTIONS.forEach(n => s.append(new Option(n, n)));
}

// The eyelid <select> is filled IN JS, not frozen in the HTML: its VALUES are
// the CSV vocabulary (`0|blink|winkG|winkD`) and never move, but its LABELS get
// translated. Leaving them in the HTML would have required a per-<option>
// translation mechanism, for three lines.
const LID_KEYS = { '0': 'lid_none', blink: 'lid_blink',
                   winkG: 'lid_winkL', winkD: 'lid_winkR' };
function fillLids() {
  const s = $('lid');
  s.innerHTML = '';
  for (const v of LIDS) s.append(new Option(t(LID_KEYS[v]), v));
}

// The button shows the language it SWITCHES TO, never the current one: a
// button labelled with the state you are already in reads as broken.
function refreshLangBtn() {
  $('lang').textContent = (window.lang === 'en') ? 'FR' : 'EN';
}

// ANY input stops playback. During playback, `tick()` reassigns `sel` to the
// playhead on every frame: a slider moved at that moment writes into the
// keyframe that is SCROLLING BY, not into the one you think you are holding —
// and the result is overwritten on the next frame. The head drag already
// applied this rule; routing it through a single function keeps any one input
// from forgetting it.
function stopIfPlaying() { if (playing) stop(); }

// ── Operations on a DESIGNATED keyframe ────────────────────────────────────
// Each action takes its INDEX as a parameter rather than working on the current
// selection: the buttons live on the row, they must act on THAT row, without
// depending on a prior selection click.
function addAt(i) { stopIfPlaying(); frames.splice(i + 1, 0, newFrame()); sel = i + 1; syncAll(); }
function dupAt(i) { stopIfPlaying(); frames.splice(i + 1, 0, newFrame(frames[i])); sel = i + 1; syncAll(); }

function delAt(i) {
  if (frames.length <= 1) return;            // an empty dance makes no sense
  stopIfPlaying();
  // Confirmation (user request 07-30). A keyframe is deleted with a single
  // click on a 16 px button sitting next to "duplicate": the question recalls
  // WHICH one is going, with its expression and its duration — a plain "Are you
  // sure?" does not tell you whether you aimed at the right row.
  if (!confirm(t('confirm_del', { n: i + 1,
                                  emo: frames[i].emotion || effEmotion(i),
                                  ms: dur(frames[i]) }))) return;
  frames.splice(i, 1);
  if (sel >= frames.length) sel = frames.length - 1;
  else if (sel > i) sel--;                   // the selection FOLLOWS its keyframe
  syncAll();
}

function moveAt(i, d) {
  const j = i + d;
  if (j < 0 || j >= frames.length) return;
  stopIfPlaying();
  [frames[i], frames[j]] = [frames[j], frames[i]];
  if (sel === i) sel = j; else if (sel === j) sel = i;
  syncAll();
}

function drawList() {
  const ul = $('frames');
  ul.innerHTML = '';
  frames.forEach((f, i) => {
    const li = document.createElement('li');
    if (i === sel) li.className = 'sel';
    // Three FLAT elements, positioned by the card's grid (style.css): the
    // number and the expression on the first line, the duration on the second
    // but in the SAME column as the expression — so it lines up with the name,
    // not under the number.
    li.innerHTML = `<span class="n">${i + 1}</span>` +
                   `<span class="em">${f.emotion || '↓ ' + effEmotion(i)}</span>` +
                   `<span class="ms">${dur(f)} ms</span>`;
    li.onclick = () => { stopIfPlaying(); sel = i; syncAll(); };

    const acts = document.createElement('span');
    acts.className = 'acts';
    // 2×2 grid, in THIS order: the actions that change the NUMBER of keyframes
    // on top, those that only change their ORDER below. The tooltip states the
    // EFFECT, not the button's name: reduced to a single glyph, these commands
    // have nowhere else to make themselves understood.
    // [label, tooltip, action, disabled?]
    const n = i + 1;
    const defs = [
      ['⧉', t('dup_t',  { n }), () => dupAt(i), false],
      ['✕', t('del_t',  { n }), () => delAt(i),
       frames.length <= 1],
      ['↑', t('up_t',   { n }), () => moveAt(i, -1), i === 0],
      ['↓', t('down_t', { n }), () => moveAt(i, +1), i === frames.length - 1],
    ];
    for (const [txt, title, fn, off] of defs) {
      const b = document.createElement('button');
      b.textContent = txt;
      b.title = title;
      b.disabled = off;
      if (txt === '✕') b.className = 'danger';
      // Without this, the click would bubble up to the <li> and re-select a
      // row that has just moved or disappeared.
      b.onclick = (e) => { e.stopPropagation(); fn(); };
      acts.append(b);
    }
    li.append(acts);
    ul.append(li);
  });

  const total = frames.reduce((a, f) => a + dur(f), 0);
  $('total').textContent =
    t('total', { n: frames.length, s: frames.length > 1 ? 's' : '',
                 sec: (total / 1000).toFixed(2) });
}

function syncEditor() {
  const f = frames[sel];
  if (!f) return;
  $('kfNum').textContent = `${sel + 1}/${frames.length}`;
  $('emotion').value = f.emotion;
  $('yaw').value = f.yaw;         $('vYaw').textContent = `${f.yaw}°`;
  $('pitch').value = f.pitch;     $('vPitch').textContent = `${f.pitch}°`;
  $('gazeY').value = f.gazeY;     $('vGaze').textContent = f.gazeY.toFixed(2);
  $('lid').value = f.lid;
  $('servoMs').value = f.servoMs; $('vServo').textContent = `${f.servoMs} ms`;
  $('holdMs').value = f.holdMs;   $('vHold').textContent = `${f.holdMs} ms`;
  $('vEm').textContent = f.emotion || t('unchanged_v', { emo: effEmotion(sel) });

  // Warnings taken from the firmware's rules and bounds: better to see them
  // here than after putting the SD card back into the robot.
  const w = [];
  if (f.holdMs < f.servoMs)
    w.push(t('w_hold', { hold: f.holdMs, servo: f.servoMs }));
  if (Math.abs(f.gazeY) > GAZE_MAX_Y)
    w.push(t('w_gaze', { max: GAZE_MAX_Y }));
  if (frames.length > MAX_KEYS)
    w.push(t('w_max', { n: frames.length, max: MAX_KEYS }));
  if (sel === frames.length - 1) {
    // `DanceStore::parseCsv` tests the LITERAL column, not the inherited
    // emotion: an empty column means "unchanged" (EMOTIONS_COUNT), which is NOT
    // Normal even when the inherited expression is. Testing the inherited one
    // therefore let the most common case through — a last row with no emotion —
    // and the robot appended its exit keyframe without the tool announcing it.
    if (f.emotion !== 'Normal')
      w.push(t(f.emotion ? 'w_last_normal' : 'w_last_empty'));
    if (f.yaw !== 0 || f.pitch !== 0)
      w.push(t('w_last_pose'));
  }
  $('warn').hidden = !w.length;
  $('warn').textContent = w.join(' ');
}

function syncAll() { drawList(); syncEditor(); renderSel(); writeCsv(); }

// TARGETED updates of the list. `drawList()` recreates every row and its four
// buttons: calling it on every `oninput` event of a slider made dozens of them
// per drag, and during playback it reset the list's scroll position on every
// keyframe crossed.
function markSelected() {
  const ul = $('frames');
  for (let k = 0; k < ul.children.length; k++)
    ul.children[k].className = (k === sel) ? 'sel' : '';
  const li = ul.children[sel];
  if (li && li.scrollIntoView) li.scrollIntoView({ block: 'nearest' });
}

// Only the duration and the expression are shown on a row. Changing the
// expression ALSO changes what the following rows inherit ("↓ X") and therefore
// goes through `syncAll()`; the sliders only affect their own row.
function refreshRow(i) {
  const li = $('frames').children[i];
  if (!li) return;
  const ms = li.querySelector('.ms');
  if (ms) ms.textContent = `${dur(frames[i])} ms`;
}

// ── CSV ────────────────────────────────────────────────────────────────────
function writeCsv() {
  $('csv').value = '# yaw,pitch,servoMs,holdMs,emotion,lid,gazeY\n' +
    frames.map(f => [f.yaw, f.pitch, f.servoMs, f.holdMs, f.emotion, f.lid,
                     Number(f.gazeY.toFixed(2))].join(',')).join('\n') + '\n';
}

function readCsv(text) {
  const out = [];
  for (const raw of text.split(/\r?\n/)) {
    // DanceStore cuts at the FIRST '#', wherever it is: end-of-line comments
    // are a documented form of the format.
    const h = raw.indexOf('#');
    const line = (h >= 0 ? raw.slice(0, h) : raw).trim();
    if (!line) continue;
    const c = line.split(',').map(s => s.trim());
    const num = (v, d, lim) => {
      const n = parseFloat(v);
      const x = Number.isFinite(n) ? n : d;
      return lim ? clamp(x, lim.min, lim.max) : x;
    };
    const lidRaw = (c[5] || '').toLowerCase();      // case-INSENSITIVE compare
    const lid = LIDS.find(l => l.toLowerCase() === lidRaw)
             || ({ '1': 'blink', '2': 'winkG', '3': 'winkD' })[lidRaw] || '0';
    // Unknown emotion name = "unchanged", exactly like DanceStore.
    const emo = window.EMOTIONS.find(e => e.toLowerCase() === (c[4] || '').toLowerCase());
    out.push({
      yaw:     Math.round(num(c[0], 0, LIM.yaw)),
      pitch:   Math.round(num(c[1], 0, LIM.pitch)),
      servoMs: Math.round(num(c[2], 0, LIM.servoMs)),
      holdMs:  Math.round(num(c[3], 0, LIM.holdMs)),
      emotion: emo || '',
      lid,
      gazeY:   num(c[6], 0, LIM.gazeY),
    });
  }
  return out;
}

// ── Playback ───────────────────────────────────────────────────────────────
// `BlinkController` envelopes: a blink closes in 60 ms, holds 40 ms and reopens
// in 100 ms; a wink closes in 80 ms, holds 220 ms, reopens in 150 ms. The
// eyelid event therefore has its OWN duration, independent of the servo travel.
// Tying it to a fraction of `servoMs` made it disappear as soon as the travel
// was 0 — yet a keyframe that moves ONLY the eyelids is perfectly legitimate,
// and the static preview did show it.
const LID_MS = { blink: 200, winkG: 450, winkD: 450 };

// The pose is INTERPOLATED during servoMs then held — what ServoMotion does.
function tick(now) {
  if (!playing) return;
  const t = now - playT0;
  const total = frames.reduce((a, f) => a + dur(f), 0);
  let acc = 0, i = 0, phase = 1;
  for (; i < frames.length; i++) {
    const d = dur(frames[i]);
    if (t < acc + d) {
      phase = frames[i].servoMs ? Math.min(1, (t - acc) / frames[i].servoMs) : 1;
      break;
    }
    acc += d;
  }
  $('progress').style.width = `${Math.min(100, total ? t / total * 100 : 0)}%`;
  if (i >= frames.length) { stop(); return; }

  const prev = frames[i - 1] || frames[i];
  const cur  = frames[i];
  const mix  = (a, b) => a + (b - a) * phase;
  render({ ...cur,
           yaw:   Math.round(mix(prev.yaw,   cur.yaw)),
           pitch: Math.round(mix(prev.pitch, cur.pitch)),
           gazeY: mix(prev.gazeY, cur.gazeY) },
         effEmotion(i),
         // The event occupies the START of the keyframe, over its own duration,
         // and never beyond the keyframe itself.
         cur.lid !== '0' && (t - acc) < Math.min(LID_MS[cur.lid] || 0, dur(cur)),
         t);
  // Follow the playhead without REBUILDING the list: only the highlight
  // changes. Rebuilding it reset the scroll position on every keyframe
  // crossed, and a long dance became unreadable during playback.
  if (i !== sel) { sel = i; markSelected(); syncEditor(); }
  requestAnimationFrame(tick);
}

function stop() {
  playing = false;
  $('play').textContent = playLabel();
  $('progress').style.width = '0';
  renderSel();
}

// ── Driving the servos by DRAGGING the head ────────────────────────────────
// Setting two angles with sliders forces you to mentally translate "+22" into a
// pose; here you grab the head and put it where you want it. The sliders remain
// the EXACT input, the drag is the fast input — both write the same keyframe.
//
// Sensitivity: the FULL travel of each axis is covered in ~260 px, roughly the
// width of the model on screen. So the gesture spans the whole range without
// having to cross the page.
const DRAG_PX_FULL = 260;

function bindHeadDrag() {
  const scene = $('scene');
  let id = null, x0 = 0, y0 = 0, yaw0 = 0, pitch0 = 0;

  scene.addEventListener('pointerdown', (e) => {
    if (!frames[sel]) return;
    stopIfPlaying();
    id = e.pointerId;
    scene.setPointerCapture(id);          // the drag survives leaving
    scene.classList.add('dragging');      // the frame
    x0 = e.clientX; y0 = e.clientY;
    yaw0 = frames[sel].yaw; pitch0 = frames[sel].pitch;
    e.preventDefault();
  });

  scene.addEventListener('pointermove', (e) => {
    if (id === null || e.pointerId !== id) return;
    const f = frames[sel];
    if (!f) return;
    const kYaw   = (LIM.yaw.max   - LIM.yaw.min)   / DRAG_PX_FULL;
    const kPitch = (LIM.pitch.max - LIM.pitch.min) / DRAG_PX_FULL;
    // To the RIGHT = positive yaw (the observer's right, viewer-centric
    // convention). DOWNWARD = positive pitch = head LOWERED: the head is
    // pushed in the direction of the gesture, not the opposite.
    f.yaw   = Math.round(clamp(yaw0   + (e.clientX - x0) * kYaw,
                               LIM.yaw.min,   LIM.yaw.max));
    f.pitch = Math.round(clamp(pitch0 + (e.clientY - y0) * kPitch,
                               LIM.pitch.min, LIM.pitch.max));
    syncEditor(); renderSel(); writeCsv();
  });

  const release = (e) => {
    if (id === null || (e && e.pointerId !== id)) return;
    // `releasePointerCapture` THROWS `NotFoundError` when the pointer is no
    // longer active — which is exactly the case on `pointercancel` (a touch
    // gesture interrupted by a scroll, for instance). The exception skipped
    // all the cleanup that follows: the head stayed "grabbed", it kept
    // following the mouse with no button held down.
    try {
      if (scene.hasPointerCapture(id)) scene.releasePointerCapture(id);
    } catch (_) { /* capture already released: nothing left to free */ }
    id = null;
    scene.classList.remove('dragging');
    drawList();       // the list does not depend on the angles: once, at the
  };                  // end of the gesture, rather than on every pixel
  scene.addEventListener('pointerup', release);
  scene.addEventListener('pointercancel', release);
  // Safety net: this is the event the browser GUARANTEES when the capture is
  // pulled from under us (node removed, gesture stolen by the UA).
  // Re-entrancy is harmless — `id` is already null when we are the one
  // releasing.
  scene.addEventListener('lostpointercapture', release);
}

// ── Wiring ─────────────────────────────────────────────────────────────────
function bindRange(id, key, round) {
  $(id).oninput = () => {
    stopIfPlaying();
    const f = frames[sel];
    if (!f) return;
    f[key] = round ? Math.round(+$(id).value) : +$(id).value;
    // `refreshRow` and not `drawList`: a slider affects ONLY its own row, and
    // rebuilding the whole list on every pixel of the drag recreated four
    // buttons per keyframe, dozens of times per second.
    syncEditor(); renderSel(); writeCsv(); refreshRow(sel);
  };
}

function init() {
  if (!window.PRESETS || !window.EMOTIONS || !window.EMOTION_PRESETS ||
      !window.EMOTION_RGB || window.EYE_COLOR_DIM === undefined) {
    document.body.innerHTML =
      '<p style="padding:30px;color:#f87171">' + t('err_presets_html') + '</p>';
    return;
  }
  // LANGUAGE first: everything that follows builds labels, and building them
  // before the language is chosen would freeze them in English.
  applyLang();
  refreshLangBtn();
  // A key present in one table and missing from the other would show an English
  // sentence in the middle of a French interface, with nothing to explain it.
  // So we SAY it, in the browser console.
  const gaps = window.missingKeys();
  if (gaps.length) console.warn('[i18n] ' + gaps.join(' · '));

  // The HTML carries the same label, but JS is its SOURCE: setting it here
  // guarantees that opening and returning from playback look identical.
  $('play').textContent = playLabel();
  fillEmotions();
  fillLids();
  $('pitch').min = LIM.pitch.min;
  $('pitch').max = LIM.pitch.max;

  // Starting sequence: a variant of `furious` (behavior/Dances.h), the richest
  // of the fifteen built-in dances. It serves as a TELLING example — you see at
  // once the complete dramatic arc (pressure building up through emotion
  // steps, suspension, explosion, fall), the use of the eyelids, of the gaze,
  // and the contrasted durations of the art direction (§4 of
  // CHOREGRAPHIES.md): dry ticks at 100 ms, held suspension, soft exit at
  // 500 ms.
  //
  // What it adds to the original, and why: the original falls back to −6° and
  // stops there. Here the fall goes on the CONTRARY below the horizon, to +6°
  // — the lowest the servo tolerates — with Sad and the gaze on the floor.
  // That gives a dejected collapse rather than a plain return, and it puts the
  // tool in a real situation right at opening: the "end stop" indicator lights
  // up, and one understands without reading the note that the downward travel
  // is very short.
  frames = [
    { yaw:   0, pitch:   0, servoMs: 150, holdMs: 400, emotion: 'Annoyed',    lid: '0',     gazeY:  0    },
    { yaw:   3, pitch:  -2, servoMs: 100, holdMs: 120, emotion: '',           lid: '0',     gazeY:  0    },
    { yaw:  -3, pitch:  -2, servoMs: 100, holdMs: 120, emotion: '',           lid: '0',     gazeY:  0    },
    { yaw:   4, pitch:  -6, servoMs: 100, holdMs: 120, emotion: 'Frustrated', lid: '0',     gazeY:  0    },
    { yaw:  -5, pitch: -10, servoMs:  90, holdMs: 110, emotion: '',           lid: '0',     gazeY:  0    },
    { yaw:   6, pitch: -14, servoMs:  90, holdMs: 110, emotion: 'Angry',      lid: '0',     gazeY:  0    },
    { yaw:  -7, pitch: -18, servoMs:  80, holdMs: 100, emotion: '',           lid: '0',     gazeY:  0    },
    { yaw:   8, pitch: -22, servoMs:  80, holdMs: 100, emotion: 'Furious',    lid: 'blink', gazeY:  0.10 },
    { yaw:  -8, pitch: -26, servoMs:  80, holdMs: 100, emotion: '',           lid: '0',     gazeY:  0.15 },
    { yaw:   8, pitch: -28, servoMs:  80, holdMs: 420, emotion: '',           lid: '0',     gazeY:  0.20 },
    { yaw: -20, pitch: -30, servoMs:  90, holdMs: 110, emotion: '',           lid: '0',     gazeY:  0.20 },
    { yaw:  20, pitch: -30, servoMs:  90, holdMs: 110, emotion: '',           lid: 'blink', gazeY:  0.20 },
    { yaw: -20, pitch: -30, servoMs:  90, holdMs: 110, emotion: '',           lid: '0',     gazeY:  0.20 },
    { yaw:  20, pitch: -30, servoMs:  90, holdMs: 110, emotion: '',           lid: '0',     gazeY:  0.20 },
    { yaw:   0, pitch:   6, servoMs: 450, holdMs: 900, emotion: 'Sad',        lid: 'blink', gazeY: -0.20 },
    { yaw:   0, pitch:   6, servoMs: 200, holdMs: 600, emotion: '',           lid: 'winkG', gazeY: -0.20 },
    { yaw:   0, pitch:   0, servoMs: 500, holdMs: 700, emotion: 'Normal',     lid: '0',     gazeY:  0    },
  ];

  // ── LIVE CAPTURE ────────────────────────────────────────────────────────
  // Reads `GET /api/servo/pos`, which the firmware fills ONLY while the servos
  // are released — the SCS0009 bus is write-only in operation, so a read taken
  // between two WritePos leaves the servos mute. Releasing them is therefore
  // not a convenience here, it is the precondition of the measurement.
  //
  // RAW SERVO DEGREES COME BACK, and this editor speaks offsets: yaw 166 is
  // straight ahead and pitch 93 is the home pose (Units.h). Both conversions
  // are a subtraction, and both directions already agree with the sliders —
  // `yaw < YAW_CENTER` is the viewer's left, `pitch < PITCH_NEUTRAL` is a
  // raised head, which is exactly what LIM says at the top of this file.
  const YAW_CENTER = 166, PITCH_NEUTRAL = 93;
  let lvTimer = null, lvPrevServos = null;

  const lvSay = (key, cls, vars) => {
    const m = $('lvMsg');
    m.textContent = t(key, vars || {});
    m.className = 'hint' + (cls ? ' ' + cls : '');
  };

  // Every call the robot answers goes through here, so a CORS refusal is named
  // ONCE and named for what it is. A blocked cross-origin fetch surfaces as a
  // bare TypeError with no status and no body: reported raw it reads as "the
  // robot is off", and the user power-cycles a robot that was answering fine.
  const lvFetch = async (path, opts) => {
    const ip = $('lvIp').value.trim();
    if (!ip) throw new Error('noip');
    const r = await fetch(`http://${ip}${path}`, opts).catch(() => {
      throw new Error('blocked');
    });
    if (!r.ok) throw new Error('http' + r.status);
    return r.json().catch(() => ({}));
  };

  const lvStop = async (restore) => {
    if (lvTimer) { clearInterval(lvTimer); lvTimer = null; }
    $('lvGrab').disabled = true;
    $('lvFollow').disabled = true;
    $('lvFollow').checked = false;
    document.querySelector('.live').classList.remove('on');
    $('lvPose').textContent = '—';
    // PUT THE SERVOS BACK THE WAY THEY WERE, not the way we assume they were.
    // A robot whose owner runs with servos off would have been handed back a
    // servo task it never had.
    if (restore && lvPrevServos !== null) {
      try { await lvFetch(`/api/tuning?servos=${lvPrevServos}`, { method: 'POST' }); }
      catch (e) { /* saying so below is enough; the toggle is already off */ }
    }
    lvPrevServos = null;
  };

  const lvTick = async () => {
    let p;
    try { p = await lvFetch('/api/servo/pos'); }
    catch (e) {
      lvSay(e.message === 'blocked' ? 'lv_blocked' : 'lv_neterr', 'ko');
      await lvStop(false);
      $('lvOn').checked = false;
      return;
    }
    if (!p.valid) {
      // Named separately from a network failure: the robot IS answering, it
      // just has no fresh sample — the servo task takes one only while the
      // servos are off, and it needs a tick to produce the first.
      $('lvPose').textContent = '…';
      lvSay(p.servosOff ? 'lv_wait' : 'lv_noff', 'ko');
      return;
    }
    const yaw   = Math.round(p.yaw   - YAW_CENTER);
    const pitch = Math.round(p.pitch - PITCH_NEUTRAL);
    $('lvPose').textContent = `${yaw}° / ${pitch}°`;
    lvSay('lv_live', 'ok');
    if ($('lvFollow').checked) lvApply(yaw, pitch);
  };

  // The measured pose lands in the SELECTED keyframe, clamped like anything
  // else the editor writes: the head can be posed a little past what a
  // choreography may ask for, and a CSV the robot silently trims is the exact
  // failure this tool exists to prevent.
  const lvApply = (yaw, pitch) => {
    const f = frames[sel];
    if (!f) return;
    f.yaw   = Math.max(LIM.yaw.min,   Math.min(LIM.yaw.max,   yaw));
    f.pitch = Math.max(LIM.pitch.min, Math.min(LIM.pitch.max, pitch));
    syncAll();
  };

  $('lvIp').value = localStorage.getItem('sce_ip') || '';
  $('lvIp').oninput = () => localStorage.setItem('sce_ip', $('lvIp').value.trim());

  $('lvOn').onchange = async () => {
    if (!$('lvOn').checked) { lvSay('lv_idle'); await lvStop(true); return; }
    if (!$('lvIp').value.trim()) { $('lvOn').checked = false; lvSay('lv_noip', 'ko'); return; }
    lvSay('lv_conn');
    try {
      const tun = await lvFetch('/api/tuning');
      lvPrevServos = (tun.servos >= 0.5) ? 1 : 0;
      await lvFetch('/api/tuning?servos=0', { method: 'POST' });
    } catch (e) {
      $('lvOn').checked = false;
      lvSay(e.message === 'blocked' ? 'lv_blocked' : 'lv_neterr', 'ko');
      return;
    }
    document.querySelector('.live').classList.add('on');
    $('lvGrab').disabled = false;
    $('lvFollow').disabled = false;
    // 5 Hz: fast enough that posing the head feels answered, slow enough to
    // leave the robot's own loop alone. The endpoint is a variable read, but
    // the HTTP round trip is not free on an ESP32 serving its own console.
    lvTick();
    lvTimer = setInterval(lvTick, 200);
  };

  $('lvGrab').onclick = () => {
    const m = ($('lvPose').textContent || '').match(/(-?\d+)°\s*\/\s*(-?\d+)°/);
    if (!m) return;
    stopIfPlaying();
    lvApply(parseInt(m[1], 10), parseInt(m[2], 10));
  };
  // Leaving the page with the servos released would be a robot left limp by a
  // tab someone closed.
  addEventListener('beforeunload', () => {
    if (lvTimer && lvPrevServos) {
      navigator.sendBeacon(
        `http://${$('lvIp').value.trim()}/api/tuning?servos=${lvPrevServos}`);
    }
  });

  bindRange('yaw', 'yaw', true);
  bindRange('pitch', 'pitch', true);
  bindRange('gazeY', 'gazeY', false);
  bindRange('servoMs', 'servoMs', true);
  bindRange('holdMs', 'holdMs', true);
  $('emotion').onchange = () => { stopIfPlaying(); frames[sel].emotion = $('emotion').value; syncAll(); };
  $('lid').onchange     = () => { stopIfPlaying(); frames[sel].lid = $('lid').value; syncAll(); };

  // The row buttons (↑ ↓ ⧉ ✕) are recreated by drawList() on every redraw:
  // they carry their own index, so there is nothing to wire here.
  // "+" acts on the SEQUENCE (append at the end), so it lives in the title.
  $('add').onclick    = () => addAt(frames.length - 1);
  // "New sequence" is the MOST destructive action of the tool: it throws the
  // whole dance away, and nothing is saved anywhere. The tooltip already said
  // so, but a tooltip is not read at the moment of the click.
  $('newSeq').onclick = () => {
    stopIfPlaying();
    if (frames.length > 1 &&
        !confirm(t('confirm_newseq', { n: frames.length }))) return;
    frames = [newFrame()]; sel = 0; syncAll();
  };

  bindHeadDrag();

  $('play').onclick = () => {
    if (playing) { stop(); return; }
    playing = true; playT0 = performance.now();
    $('play').textContent = stopLabel();
    requestAnimationFrame(tick);
  };

  // The CSV field is an EDITABLE VIEW: pasting an existing dance loads it.
  $('csv').oninput = () => {
    stopIfPlaying();          // the text REPLACES `frames`: playing back an
    const got = readCsv($('csv').value);   // array that is being replaced
    if (!got.length) return;               // makes no sense
    frames = got; sel = Math.min(sel, frames.length - 1);
    drawList(); syncEditor(); renderSel();
  };

  // EN/FR toggle. `applyLang()` only covers the HTML that carries keys;
  // everything JS built (<select> labels, row tooltips, warnings, counter) must
  // be REBUILT — hence the `syncAll()`.
  // The robot has its own setting (the `lang` key of `config.yaml`): this tool
  // runs on a PC, over file://, with no access to the SD card.
  $('lang').onclick = () => {
    setLang(window.lang === 'en' ? 'fr' : 'en');
    refreshLangBtn();
    fillEmotions();
    fillLids();
    $('play').textContent = playing ? stopLabel() : playLabel();
    syncAll();          // also restores the values of both <select>s
  };

  $('import').onclick = () => $('file').click();
  $('file').onchange = (e) => {
    const file = e.target.files[0];
    if (!file) return;
    const r = new FileReader();
    r.onload = () => {
      stopIfPlaying();        // playback would run on the REPLACED dance
      const got = readCsv(r.result);
      if (!got.length) { alert(t('alert_nokf')); return; }
      // The import OVERWRITES the work in progress, and nothing is saved
      // anywhere (user request 07-30). We ask AFTER reading the file: that way
      // we can name what is coming in and what is going out, and an unreadable
      // file does not ask a pointless question.
      if (frames.length > 1 &&
          !confirm(t('confirm_import', { file: file.name, n: frames.length })))
        return;
      frames = got; sel = 0; syncAll();
    };
    r.readAsText(file);
    e.target.value = '';                     // re-import the SAME file
  };

  $('download').onclick = () => {
    const raw = (prompt(t('prompt_name'), t('default_name')) || '').trim();
    const name = raw.replace(/[^\w-]/g, '').slice(0, MAX_NAME);
    // Sanitizing AFTER testing would have let "!!!" through as ".csv", a file
    // DanceStore could never find.
    if (!name) { alert(t('alert_name')); return; }
    if (frames.length > MAX_KEYS &&
        !confirm(t('confirm_trunc', { n: frames.length, max: MAX_KEYS })))
      return;
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([$('csv').value], { type: 'text/csv' }));
    a.download = name + '.csv';
    document.body.append(a);                 // Firefox ignores an anchor
    a.click();                               // detached from the document
    setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 0);
  };

  syncAll();
}

document.addEventListener('DOMContentLoaded', init);
