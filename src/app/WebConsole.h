#pragma once
// =============================================================================
// WebConsole.h — StackChan-Companion (app)
// =============================================================================
// Embedded HTML pages (PROGMEM) served by WebApi:
//   CONSOLE_HTML : SELF-CONTAINED control console (vanilla JS, zero CDN — works
//                  in AP mode with no internet). Sticky header with a heap+frame
//                  CHART (canvas) + status panel; emotions, dances, centred
//                  servo pad + option toggles side by side, tuning as a TABLE
//                  (id | default | slider | description), VOR calibration, wifi.
//                  (User-driven redesign 2026-07-12.)
//   SWAGGER_HTML : Swagger UI loaded from unpkg (needs internet on the BROWSER
//                  side — STA mode) and pointing at /api/openapi.json.
//   OPENAPI_JSON : OpenAPI 3 spec of the endpoints (source of truth for the API
//                  documentation).
//
// NOT COMPILED — THIS FILE IS AN ASSET. The three literals below are the
//   editable SOURCE OF TRUTH, but WebApi.h does NOT include this header: it
//   includes the GENERATED WebConsoleGz.h, which holds the same bytes gzipped
//   (scripts/build/gen_console_gz.py, run as a `pre:` PlatformIO action of
//   env:companion — see platformio.ini). Edit the markup here, build, done;
//   nothing to regenerate by hand. Consequence to keep in mind: a syntax error
//   in the C++ around the literals is caught by the GENERATOR (it parses the
//   raw-string delimiters), not by the compiler.
//
// LANGUAGE — English is CANONICAL, French is applied on top (2026-08-01):
//   the markup below carries ENGLISH, so the page stays readable even if the
//   i18n JavaScript never runs. `window.applyLang()` resolves the language and
//   translates the document. Only the firmware knows the `lang:` key of
//   config.yaml, so WebApi publishes it in the `sce_lang` COOKIE of the very
//   response that carries this page — a `<script>` trailer after </html>
//   cannot exist any more, you cannot append text to a gzip stream (rationale
//   in WebApi::sendConsole). The cookie is stored before the parser reaches
//   the page scripts, so the first pass is already in the right language.
//   `window.SCE_LANG` still wins when set, so applyLang() must stay callable
//   after the page scripts have run.
//   Same mechanism as tools/choregraphies/i18n.js (table, t(key,vars) with
//   {placeholders}, data-i18n / -html / -title attributes, missingKeys()) with
//   ONE deliberate difference: the English of the static markup is NOT stored a
//   second time in the table — applyLang() snapshots it from the DOM on its
//   first run, before translating anything. A full English table would have
//   duplicated ~4 KB of flash for strings the page already carries.
//   SWAGGER_HTML and OPENAPI_JSON stay ENGLISH-ONLY: neither carries i18n
//   JavaScript, and a spec is read by machines.
// =============================================================================

namespace sce {

inline const char CONSOLE_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>StackChan</title><style>
/* ===== Liquid Glass (macOS-like) — translucent panels over aurora bg ====== */
:root{--txt:#e8eefb;--mut:#93a1bd;--acc:#22d3ee;--acc2:#818cf8;
--acc-lt:#a5f3fc;--acc-glow:rgba(34,211,238,.5);--acc-tint:rgba(34,211,238,.12);
--ok:#34d399;--ko:#f87171;
--bg:#0a0d16;--head-glass:rgba(13,17,28,.55);
--glass:rgba(255,255,255,.055);--glass2:rgba(255,255,255,.09);
--bord:rgba(255,255,255,.12);--bord2:rgba(255,255,255,.2);
--sunk:rgba(8,12,22,.55);           /* "sunken" background: inputs, seg, code */
--r:6px;--rc:3px;--rp:3px;          /* radii: panels · controls · pills */
--wrap:1020px;                      /* ONE content width, header included */
--acc-grad:linear-gradient(120deg,var(--acc),var(--acc-lt));
--aurora:
 radial-gradient(600px 420px at 12% -5%,rgba(34,211,238,.16),transparent 60%),
 radial-gradient(720px 500px at 95% 12%,rgba(129,140,248,.17),transparent 65%),
 radial-gradient(560px 560px at 55% 115%,rgba(52,211,153,.10),transparent 60%),
 radial-gradient(400px 300px at 80% 80%,rgba(244,114,182,.07),transparent 60%),
 linear-gradient(165deg,#0b0f1c 0%,#0d1322 55%,#0a0e1a 100%)}
/* ---- CONSOLE SKINS, one per personality --------------------------------
   The active character's `theme:` is stamped on <html> as data-theme, so the
   console wears the robot's colours. EVERY theme, including the default one
   nobody picks a name for, has a DARK face (declared plainly) and a LIGHT one
   (declared under `@media (prefers-color-scheme:light)`), because the choice
   between them belongs to the room you are in, not to the character. A theme
   may reshape its own surface -- background, ink, glass, not only the accents
   -- and each one does, since a page that only swapped a link colour would
   not actually look different at a glance. What does NOT move is the
   GUARANTEE: every surface a theme declares, in every mode it declares,
   clears the same WCAG floor as the page always has — `check-contrast.py`
   measures each one, an unmeasured mode is the one failure you cannot fix
   from the page.

   LIGHT vs DARK is `prefers-color-scheme`, never a console toggle: asking the
   system once is more honest than adding a switch nobody will find. Within
   Gundam specifically, gold and red stay the SAME hex in both modes -- they
   read against either surface -- while the THIRD accent swaps: white on the
   navy (dark), Gundam blue on the off-white (light), since whichever of the
   two would blend into its own background is exactly the one the other mode
   needs instead. */

@media (prefers-color-scheme:light){
 :root{
  /* Cooler undertone than Gundam's light face on purpose: the two themes
     should not read as the same off-white with a different accent swapped in
     -- default stays crisp/cyan-toned, Gundam stays warm/ivory-toned, so the
     `theme:` picker is telling two visually distinct stories, day or night. */
  --acc:#0e7490;--acc2:#4338ca;--acc-lt:#22d3ee;
  --acc-glow:rgba(14,116,144,.35);--acc-tint:rgba(14,116,144,.09);
  --bg:#f5f7fb;--txt:#101828;--mut:#58637a;--head-glass:rgba(245,247,251,.72);
  --ok:#047857;--ko:#b91c1c;
  --glass:rgba(16,24,56,.04);--glass2:rgba(16,24,56,.07);
  --bord:rgba(16,24,56,.13);--bord2:rgba(16,24,56,.20);
  --sunk:rgba(16,24,56,.045);
  --acc-grad:linear-gradient(120deg,var(--acc),var(--acc-lt));
  --aurora:
   radial-gradient(600px 420px at 12% -5%,rgba(14,116,144,.06),transparent 60%),
   radial-gradient(720px 500px at 95% 12%,rgba(67,56,202,.06),transparent 65%),
   radial-gradient(560px 560px at 55% 115%,rgba(14,116,144,.04),transparent 60%),
   linear-gradient(165deg,#f7f9fc 0%,#f3f6fb 55%,#eef2f8 100%)}}
:root[data-theme="gundam"]{
 /* DARK (default): navy plating, white the interactive accent, gold the trim.
    Gold is kept visibly DEEPER than white rather than a pale lemon -- the two
    sit next to each other in the icon row, and a pale yellow reads as an off-
    white at that size, which is the one pairing in this theme that must stay
    told apart. */
 --acc:#ffffff;--acc2:#be8c00;--acc-lt:#c9d8ff;
 --acc-glow:rgba(255,255,255,.4);--acc-tint:rgba(255,255,255,.10);
 --bg:#0a1730;--txt:#c7d1ea;--mut:#8b97b8;--head-glass:rgba(8,15,32,.55);
 --glass:rgba(255,255,255,.055);--glass2:rgba(255,255,255,.09);
 --bord:rgba(255,255,255,.14);--bord2:rgba(255,255,255,.22);
 --sunk:rgba(5,10,24,.55);
 --acc-grad:linear-gradient(120deg,var(--acc),var(--acc-lt));
 --aurora:
  radial-gradient(600px 420px at 12% -5%,rgba(255,255,255,.07),transparent 60%),
  radial-gradient(720px 500px at 95% 12%,rgba(232,174,0,.09),transparent 65%),
  radial-gradient(560px 560px at 55% 115%,rgba(95,143,255,.10),transparent 60%),
  linear-gradient(165deg,#0a1730 0%,#0c1d40 55%,#081026 100%)}
@media (prefers-color-scheme:light){
 :root[data-theme="gundam"]{
  /* LIGHT: off-white plating (blanc casse), Gundam blue the interactive
     accent -- white would vanish on its own background, so blue takes the
     role white held at night. Gold darkens: the same #be8c00 read fine as
     text on navy and reads WEAK on off-white, so light mode gets its own,
     richer amber rather than reusing the dark value out of false symmetry. */
  --acc:#2452c9;--acc2:#7c4f00;--acc-lt:#5b7ee0;
  --acc-glow:rgba(36,82,201,.35);--acc-tint:rgba(36,82,201,.09);
  --bg:#f4f1ea;--txt:#1c2436;--mut:#57607a;--head-glass:rgba(244,241,234,.74);
  --glass:rgba(15,25,55,.035);--glass2:rgba(15,25,55,.06);
  --bord:rgba(15,25,55,.13);--bord2:rgba(15,25,55,.20);
  --sunk:rgba(15,25,55,.045);
  --acc-grad:linear-gradient(120deg,var(--acc),var(--acc-lt));
  --aurora:
   radial-gradient(600px 420px at 12% -5%,rgba(36,82,201,.06),transparent 60%),
   radial-gradient(720px 500px at 95% 12%,rgba(160,100,0,.07),transparent 65%),
   radial-gradient(560px 560px at 55% 115%,rgba(36,82,201,.04),transparent 60%),
   linear-gradient(165deg,#f6f3ec 0%,#f2efe5 55%,#efece0 100%)}}
*{box-sizing:border-box}
html{scrollbar-color:rgba(255,255,255,.18) transparent}
body{font-family:-apple-system,system-ui,'Segoe UI',sans-serif;margin:0;
color:var(--txt);min-height:100vh;background:var(--bg)}
/* character tab: the weight grid. `auto-fill` rather than a fixed count —
   thirty emotions on a phone is one column, on a laptop four, with no
   breakpoint to keep in step with the panel width. */
.wgrid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));
gap:6px 14px;margin:8px 0}
.wgrid label{display:flex;align-items:center;gap:8px;font-size:.82em;
color:var(--mut)}
.wgrid input{flex:1;min-width:0}
.wgrid b{width:2.2em;text-align:right;color:var(--txt);font-variant-numeric:tabular-nums}
.fld{display:flex;flex-direction:column;gap:4px;flex:1;min-width:140px;
font-size:.82em;color:var(--mut)}
/* WIDTH ONLY: `input[type=text]` and `select` already carry the console's full
   look (background/border/radius/padding) from the base rules below — this
   wrapper must not restate any of that, or the day the base rule changes this
   one quietly stops matching it. A native colour swatch cannot take that same
   skin (the browser owns its inside), so its OUTER chrome is matched by hand:
   same height as a text input, same cursor a control invites. */
.fld input,.fld select{width:100%}
.fld input[type=color]{height:38px;padding:2px;cursor:pointer}
/* fixed aurora background: gives the panel blur something to diffuse.
   THE GRADIENT ITSELF IS A VARIABLE, not just its colours -- a theme's light
   and dark faces need different NUMBERS of glow layers (light drops the
   fourth, pink one has no role here), and a token that only carried colours
   would leave the layer count hard-wired to the original theme regardless. */
body::before{content:"";position:fixed;inset:0;z-index:-1;background:var(--aurora)}
/* ---- header: thick glass, sticky ---- */
/* THE HEADER IS NOW BRAND + TABS, and nothing else. The chart and the status
   pills used to live in it, which made ~180 px of sticky chrome permanent on a
   laptop; they are worth a look when you arrive and not worth a fifth of the
   viewport while you drag a slider, so they moved into the page (#tele).
   `--wrap` is the ONE place the content width is written: the header used to
   pad 20 px from the viewport while `main` centred at 960, so on a wide screen
   the logo sat far left of everything it introduced. */
/* THE HEADER AND THE TELEMETRY ARE PINNED TOGETHER, as one block (user
   08-10). Two stacked `sticky` elements would need the second one's `top` to
   equal the first one's HEIGHT — a number nothing measures, that changes with
   the font, the language and whether the tab bar wraps. A single sticky
   ancestor has no such number in it. */
.top{position:sticky;top:0;z-index:9}
header{padding:6px 0 0;
background:var(--head-glass);backdrop-filter:blur(26px) saturate(1.7);
-webkit-backdrop-filter:blur(26px) saturate(1.7);
border-bottom:1px solid var(--bord);
}
.wrap{max-width:var(--wrap);margin:0 auto;padding:0 20px}
/* ONE ROW: identity on the left, destinations on the right. It was two rows,
   and since the header became part of a PINNED block every one of its pixels is
   permanent — the brand is read once, the tabs are used all day, and stacking
   them charged the whole session for both.
   `align-items:flex-end` is what lets the active tab's underline land exactly
   on the header's own bottom border instead of floating above it. */
header>.wrap{display:flex;align-items:flex-end;gap:20px;flex-wrap:wrap}
header>.wrap>.brand{padding-bottom:8px}
header h1{margin:0;font-size:1.04em;font-weight:700;letter-spacing:.09em;
background:linear-gradient(90deg,var(--acc),var(--acc-lt));
-webkit-background-clip:text;background-clip:text;color:transparent}
#legend{margin-left:auto;display:flex;gap:12px;flex-wrap:wrap;
font:10px ui-monospace,monospace;color:var(--mut)}
/* 72 -> 132 px. At 72 the four traces spent most of their travel inside two
   or three pixels of each other and the shape of a memory dip was guesswork.
   Height is the only thing a line chart has. */
#chart{display:block;width:100%;height:132px;background:transparent;
border:0;margin-bottom:2px}
/* Pinned, the block must not eat the page on a short screen: past that, the
   chart shrinks rather than the panel scrolling under it. */
@media(max-height:820px){#chart{height:96px}}
@media(max-height:640px){#chart{height:72px}}
main{padding:14px 20px 28px;max-width:var(--wrap);margin:0 auto}
/* ---- telemetry: the live block, above the tabs, shared by all of them ----
   Outside the tab panels because it is true whatever you are looking at, and
   outside the sticky header because it is a thing you consult, not a thing you
   need pinned. */
/* FULL BLEED, deliberately (user 08-10): it sits directly under the sticky
   header and reads as its continuation - a jumbo band, not a card. Boxed at
   the content width it looked like the first panel of whatever tab you were
   on, which is exactly what it is not: it belongs to none of them and is true
   for all of them. Hence no side border and no radius either; a rounded card
   pinned to both screen edges is the one shape that always looks like a
   mistake. The INNER content still lines up with the tabs above and the panels
   below, through the same `.wrap`. */
#tele{background:rgba(255,255,255,.04);border:0;border-bottom:1px solid var(--bord);
border-radius:0;padding:9px 0 12px;margin:0 0 16px;
backdrop-filter:blur(22px) saturate(1.5);-webkit-backdrop-filter:blur(22px) saturate(1.5)}
/* EDGE TO EDGE, content included (user 08-10). Constraining the inner block to
   `--wrap` gave a full-width background around a 1020 px chart, which is the
   worst of both: it read as a card that had lost its border. A jumbo band means
   the GRAPH is the band - only the 20 px that keep text off the bezel. */
#tele>summary{cursor:pointer;list-style:none;display:flex;align-items:center;gap:12px;
font-size:.7em;text-transform:uppercase;letter-spacing:.14em;font-weight:600;color:var(--mut);
padding:0 20px}
#tele .tw{padding:0 20px}
/* The two switches that are ABOUT telemetry live with it. Laid on one row, so
   the band stays a band. */
#teleopt{display:flex;flex-wrap:wrap;align-items:center;gap:8px 28px;margin-top:9px}
/* PINNED RIGHT, and it survives the wrap: on a narrow window the pills drop to
   their own line and `margin-left:auto` would leave them hugging the right
   edge under two switches that start at the left, which reads as a third
   column that is not there. */
#teleicons{margin-left:auto;display:flex;align-items:center;gap:10px}
#teleicons .lbl2{margin:0;white-space:nowrap}
#teleicons .pill{padding:3px 9px;font-size:.75em}
@media(max-width:760px){#teleicons{margin-left:0;flex-wrap:wrap}}
#teleopt .opt{border:0;padding:2px 0;gap:12px;font-size:.85em}
#teleopt .sw{width:38px;height:21px}
#teleopt .sl:before{width:15px;height:15px}
#teleopt .sw input:checked+.sl:before{transform:translateX(17px)}
#tele>summary::-webkit-details-marker{display:none}
#tele>summary::after{content:"›";margin-left:auto;font-size:1.6em;line-height:0;
transition:transform .2s;color:var(--acc)}
#tele[open]>summary::after{transform:rotate(90deg)}
#tele[open]>summary{margin-bottom:8px}
/* The GENERIC `details` rules paint a glass card on every summary and every
   direct div - which is exactly what a full-bleed band must not be, and what
   put a 1020 px rounded box back inside it. Neutralised here rather than by
   narrowing the generic selector: every other details on the page wants that
   card, this one is the exception and says so. */
#tele>summary,#tele>.tw{background:transparent;border:0;border-radius:0;
backdrop-filter:none;-webkit-backdrop-filter:none}
#tele[open]>summary{color:var(--mut);border-bottom-color:transparent}
#tele>summary::after{content:"›";margin-left:0;color:var(--acc)}
/* ---- tabs ----
   Five destinations instead of one scroll. The page had grown to eight panels
   stacked head to tail, so reaching the tuning tables meant scrolling past the
   camera and the file manager every time. */
#tabs{display:flex;gap:2px;overflow-x:auto;scrollbar-width:none;
margin-left:auto}
/* Narrow enough that the two no longer fit side by side: the tabs drop to their
   own line and take the full width, rather than being squeezed into whatever is
   left of it. */
@media(max-width:720px){header>.wrap{gap:0}
 #tabs{margin-left:0;width:100%}
 header>.wrap>.brand{padding-bottom:4px}}
#tabs::-webkit-scrollbar{display:none}
#tabs button{margin:0;border:0;border-radius:0;background:transparent;
color:var(--mut);font-size:.74em;text-transform:uppercase;letter-spacing:.12em;
font-weight:600;padding:9px 14px 10px;white-space:nowrap;
border-bottom:2px solid transparent;transition:color .15s,border-color .15s}
#tabs button:hover{color:var(--txt);transform:none;background:rgba(255,255,255,.05)}
#tabs button:active{background:rgba(255,255,255,.05);color:var(--txt);transform:none}
#tabs button.on{color:var(--acc);border-bottom-color:var(--acc)}
#tabs .ti{width:15px;height:15px;vertical-align:-3px;margin-right:7px}
@media(max-width:640px){#tabs .tl{display:none}#tabs button{padding:9px 13px}
 #tabs .ti{margin-right:0}}
/* A tab is a CONTAINER, not a card: it is a `section` only because that is the
   honest element for it, and `section{}` would otherwise draw a second frame
   around panels that already have their own. */
.tab{display:none;background:none;border:0;border-radius:0;padding:0;
margin-bottom:0;backdrop-filter:none;-webkit-backdrop-filter:none}
.tab.on{display:block;animation:fade .18s ease}
@keyframes fade{from{opacity:0;transform:translateY(3px)}to{opacity:1;transform:none}}
/* A tab's own heading: says where you are once the tab bar has scrolled off. */
.th{display:flex;align-items:baseline;gap:12px;margin:0 0 12px}
.th h2{margin:0}
.th .thd{font-size:.8em;color:var(--mut)}
/* ---- glass cards ---- */
section{background:var(--glass);backdrop-filter:blur(22px) saturate(1.5);
-webkit-backdrop-filter:blur(22px) saturate(1.5);
border:1px solid var(--bord);border-radius:var(--r);padding:14px 16px;
margin-bottom:14px;display:flex;flex-direction:column;
}
h2{font-size:.76em;text-transform:uppercase;letter-spacing:.14em;
color:var(--mut);margin:0 0 10px;font-weight:600}
.h2row{display:flex;justify-content:space-between;align-items:center;gap:8px}
.h2v{text-transform:none;letter-spacing:0;color:var(--acc);
font-family:ui-monospace,monospace;font-size:1.05em}
/* ---- buttons: glass pills ---- */
button{background:var(--glass2);color:var(--txt);border:1px solid var(--bord);
border-radius:var(--rc);padding:0.25rem 0.5rem;margin:2px;cursor:pointer;font-size:.88em;
backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px);
transition:all .16s cubic-bezier(.2,.8,.3,1);
}
button:hover{background:rgba(255,255,255,.16);border-color:var(--bord2);
transform:translateY(-1px)}
button:disabled{opacity:.35;cursor:not-allowed;pointer-events:none;
transform:none}
button:active{transform:translateY(0) scale(.97);
background:var(--acc-grad);color:#06121f;
border-color:transparent}
button.ok{border-color:var(--ok);box-shadow:0 0 10px rgba(52,211,153,.45)}
button.ko{border-color:var(--ko);box-shadow:0 0 10px rgba(248,113,113,.45)}
button.stop{display:inline-flex;align-items:center;justify-content:center;
background:rgba(248,113,113,.12);border-color:rgba(248,113,113,.5);
color:var(--ko);width:32px;height:26px;padding:0;margin:0}
button.stop::before{content:"";width:10px;height:10px;
background:currentColor;border-radius:2.5px}
button.stop:hover{background:var(--ko);color:#08090c;border-color:var(--ko)}
.row{display:flex;flex-wrap:wrap;gap:4px}
/* ---- status panel ---- */
#grid{display:flex;flex-wrap:wrap;gap:7px;margin-top:9px;font-family:ui-monospace,monospace}
#grid div{display:inline-flex;align-items:baseline;gap:7px;padding:4px 11px;border-radius:var(--rp);
background:var(--glass);border:1px solid var(--bord);white-space:nowrap;
font-size:.62em;color:var(--mut);text-transform:uppercase;letter-spacing:.08em}
#grid b{color:var(--ok);font-size:1em;font-weight:600;
letter-spacing:0;text-transform:none;font-variant-numeric:tabular-nums}
#grid .off{opacity:.7}#grid .off b{color:#55607a}
#grid .ko{border-color:rgba(248,113,113,.42);background:rgba(248,113,113,.08)}
#grid .ko b{color:var(--ko)}
/* layout grids (control row 3 cols, panels 2 cols) */
.cgrid{display:grid;grid-template-columns:repeat(3,1fr);gap:14px;margin-bottom:14px;align-items:stretch}
.cgrid>section{margin-bottom:0}
@media(max-width:820px){.cgrid{grid-template-columns:1fr}}
/* brand */
.brand{display:flex;align-items:center;gap:13px;flex-wrap:wrap}
.logo{width:26px;height:26px;border-radius:7px;flex:none;position:relative;
background:linear-gradient(135deg,var(--acc),var(--acc-lt));box-shadow:0 0 16px var(--acc-glow)}
/* ASYMMETRIC ON PURPOSE: the robot's own eyes never match in size (A2.17 —
   equidistant CENTRES, never equal SIZE), and a badge with two identical
   pills reads as a generic robot-face glyph rather than as THIS one. */
.logo::before,.logo::after{content:"";position:absolute;border-radius:3px;
background:#06121f}
.logo::before{left:6px;top:9px;width:5px;height:7px}
.logo::after{right:5px;top:7px;width:7px;height:10px}
.brand .sub{font-size:.62em;letter-spacing:.13em;text-transform:uppercase;color:var(--mut)}
button.b-acc{background:var(--acc-grad);color:#06121f;
border-color:transparent;font-weight:600}
button.b-acc:hover{filter:brightness(1.08)}
.sysgrp{padding:2px 0 4px}
.sysgrp+.sysgrp{margin-top:16px;padding-top:14px;border-top:1px solid var(--bord)}
/* Build identity. A definition list, not a table: five fixed labels whose
   values are read, compared to a working copy, and occasionally copied out -
   so the values get the monospace and the labels stay quiet. */
.fwid{display:grid;grid-template-columns:auto 1fr;gap:3px 14px;
margin:0 0 10px;font-size:.82rem;align-items:baseline}
.fwid>span{color:var(--mut)}
.fwid>b{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-weight:600;
word-break:break-all}
/* A crash-shaped restart is a FINDING, not a value: poweron and sw are how a
   robot normally starts, panic/wdt/brownout are not. */
.fwid>b.ko{color:var(--ko)}
/* The footer glance. Deliberately quiet: it is reference, not status - it
   never changes while the page is open. */
.fwfoot{text-align:center;margin:2px 0 0;color:var(--mut);font-size:.7rem;
font-family:ui-monospace,SFMono-Regular,Menlo,monospace;word-break:break-all}
.fwfoot b{font-weight:600;color:var(--mut)}
.fwfoot b.ko{color:var(--ko)}
.sysh{font-size:.72em;text-transform:uppercase;letter-spacing:.1em;color:var(--acc2);
font-weight:600;margin:0 0 8px}
/* ---- collapsible panels: glass + animated chevron ---- */
details{margin-bottom:14px}
details>summary{cursor:pointer;list-style:none;display:flex;align-items:center;
gap:10px;background:var(--glass);backdrop-filter:blur(22px) saturate(1.5);
-webkit-backdrop-filter:blur(22px) saturate(1.5);
border:1px solid var(--bord);border-radius:var(--r);padding:11px 16px;
font-size:.76em;text-transform:uppercase;letter-spacing:.14em;font-weight:600;
color:var(--mut);transition:color .15s;
}
details>summary .ci{width:17px;height:17px;flex:none;color:var(--acc)}
details>summary::after{content:"›";margin-left:auto;font-size:1.5em;line-height:0;
transition:transform .2s;color:var(--acc)}
details[open]>summary::after{transform:rotate(90deg)}
/* when a status badge (.sst) is present it is the badge that absorbs the free
   space (pinned right) and the chevron follows it — otherwise the 2 auto
   margins would split the space and centre the badge. */
details>summary:has(.sst)::after{margin-left:8px}
/* segmented (status-bar mode) + pills (icons) + sliders */
.seg{display:inline-flex;background:var(--sunk);border:1px solid var(--bord);
border-radius:var(--rc);padding:3px;gap:2px}
.seg button{border:0;background:transparent;border-radius:2px;padding:5px 12px;
color:var(--mut);font-size:.82em;margin:0}
.seg button.on{background:var(--acc-grad);color:#06121f;font-weight:600}
.pills{display:flex;flex-wrap:wrap;gap:6px}
.pill{display:inline-flex;align-items:center;gap:6px;padding:5px 11px;border-radius:var(--rp);
border:1px solid var(--bord);background:var(--glass);font-size:.8em;color:var(--mut);cursor:pointer;user-select:none}
.pill.on{color:var(--txt);border-color:var(--acc-glow);background:var(--acc-tint)}
.pill.on::before{content:"✓";color:var(--acc);font-size:.85em}
.slider{display:flex;align-items:center;gap:10px;font-size:.85em;color:var(--mut);padding:6px 0}
.slider input[type=range]{flex:1;max-width:150px}
.slider .sl-l{flex:none;width:7.5em}   /* fixed-width label → sliders aligned */
.slider b{color:var(--acc);font-variant-numeric:tabular-nums;min-width:34px;text-align:right}
.bandm{display:none}.bandm.on{display:block}
/* ---- the rules table ----
   Greyed = loaded but GATED OFF: its enable field is under 0.5, so it cannot
   fire. Kept in the list rather than hidden, because "my rule does nothing" is
   answered by seeing it there, greyed, far faster than by its absence. */
/* `table-layout:AUTO`, explicitly. The global `table{}` rule sets `fixed` for
   the tuning table, and under `fixed` a `width:1px` is obeyed literally - every
   column collapsed to a pixel and the rows overlapped into a smear. Auto is
   what makes shrink-to-content mean "as wide as the content". */
.rtab{width:100%;border-collapse:collapse;font-size:.84em;table-layout:auto;
min-width:0}
.rtab th{text-align:left;font-size:.72em;text-transform:uppercase;letter-spacing:.09em;
color:var(--acc2);font-weight:600;padding:0 8px 3px 0;white-space:nowrap;
vertical-align:bottom}
/* Line 2 carries the NAMES: monospace, not shouted, and it is the row that
   closes the header — line 1 has no rule under it or the two would read as two
   separate headers instead of one two-storey one. */
.rtab .rh2 th{text-transform:none;letter-spacing:0;padding-top:0;padding-bottom:5px;
border-bottom:1px solid var(--bord)}
.rtab .rh2 code{color:var(--acc);font-size:1.05em;padding:0 4px 0 0}
.rtab .rh1 th[rowspan]{border-bottom:1px solid var(--bord);padding-bottom:5px}
.rtab .rh1 .rc{border-bottom:1px solid rgba(255,255,255,.14);padding-bottom:3px}
.rtab td{padding:6px 8px 6px 0;border-bottom:1px solid rgba(255,255,255,.06);
vertical-align:top}
.rtab tr:last-child td{border-bottom:0}
.rtab tr.off td{opacity:.42}
.rtab tr.off .rsrc{text-decoration:line-through}
.rtab .rsrc{font-size:.72em;text-transform:uppercase;letter-spacing:.08em;color:var(--mut);
white-space:nowrap}
.rtab b{color:var(--acc);font-variant-numeric:tabular-nums}
.rtab .dim{color:var(--mut);opacity:.5}
.rtab code{padding:0 4px}
.rtab .rdsc{color:var(--mut);font-size:.94em}
/* EVERY column shrinks to its content, and the DESCRIPTION takes what is left.
   Six columns of short, fixed vocabulary - "INTEGREE", "ctx ge 75", "10s",
   "600s", "SetEmotion Worried 4000" - sharing the width evenly pushed the one
   column made of sentences into three words per line. `width:1px` + `nowrap` is
   the shrink-to-content idiom: the browser widens each cell to its content and
   no further. Written as "all, then the last one back" rather than as a list of
   nth-child, so a seventh column added later does not silently opt out. */
.rtab th,.rtab td{width:1px;white-space:nowrap}
.rtab th:last-child,.rtab td:last-child{width:auto;white-space:normal}
/* ---- the help block: it is there to be WRITTEN FROM, so the format line is
   set as code and the columns are a definition list rather than a paragraph
   somebody has to parse into one. ---- */
.rlead{margin:0 0 10px;color:var(--mut);font-size:.88em;line-height:1.5}
/* The how-to is a details INSIDE a details, so it must not inherit the card
   look of the outer one — nested glass on glass reads as a bug. */
.rguide{margin:14px 0 0;border-top:1px solid var(--bord);padding-top:12px}
.rguide>summary,.rguide>.rhelp{background:none;backdrop-filter:none;
-webkit-backdrop-filter:none;border:0;border-radius:0;padding:0}
.rguide>summary{font-size:.72em;color:var(--acc2);
text-transform:uppercase;letter-spacing:.12em}
.rguide>summary::after{content:"›";margin-left:8px;font-size:1.5em;line-height:0;
transition:transform .2s}
.rguide[open]>summary::after{transform:rotate(90deg)}
.rguide>summary:hover{color:var(--acc)}
.rhelp{margin:12px 0 0;color:var(--mut);font-size:.86em;line-height:1.5}
.rhelp p{margin:0 0 8px}
/* Numbered, because they are done in this order and only in this order. */
.rsteps{margin:0 0 4px;padding-left:1.5em}
.rsteps li{margin:0 0 12px}
.rsteps li::marker{color:var(--acc);font-weight:600}
.rread{display:block;margin-top:6px}
.rnotes{margin:0;padding-left:1.2em}
.rnotes li{margin:0 0 7px}
.rnotes li::marker{color:var(--acc2)}
.rsub{font-size:.72em;text-transform:uppercase;letter-spacing:.12em;
color:var(--acc2);font-weight:600;margin:18px 0 7px;
border-bottom:1px solid var(--bord);padding-bottom:4px}
/* THE ANNOTATION TABLE: token, column it fills, what it means HERE. Three
   columns and not two, so the eye can run down the middle one and read the
   format itself. */
.rann td:first-child{width:1px;white-space:nowrap;text-align:right;
padding-right:10px}
.rann td:first-child code{color:var(--acc);font-weight:600}
.rann td:nth-child(2){width:1px;white-space:nowrap;padding-right:14px;
color:var(--mut)}
.rann td:nth-child(2) code{opacity:.75}
/* UN TITRE, et pas une puce de code egaree. Ils ouvrent chacun une liste, donc
   ils portent le meme vocabulaire que les autres sous-titres du guide - filet
   au-dessus, majuscules pour la partie traduite - tout en gardant le NOM de la
   colonne en code, puisque c est ce qu on tape. */
.rvsub{margin:18px 0 7px;color:var(--txt);font-size:.86em;
display:flex;align-items:baseline;gap:9px;
border-top:1px solid var(--bord);padding-top:12px}
.rvsub>code{font-size:1.15em;font-weight:600;color:var(--acc);
background:var(--sunk);border-color:var(--bord);padding:2px 9px}
.rvsub span{color:var(--mut);font-size:.9em;text-transform:uppercase;
letter-spacing:.09em}
/* Le premier ne suit rien : son filet ferait doublon avec celui du .rsub. */
.rsub+.rvsub{border-top:0;padding-top:0;margin-top:10px}
.rvals{margin:0;padding-left:1.1em;columns:2;column-gap:26px}
.rvals li{margin:0 0 4px;break-inside:avoid}
.rvals li::marker{color:var(--acc2)}
.rvals span{color:var(--mut)}
.rops{columns:3}
.rarg{opacity:.8}
.rvnote{margin:8px 0 0}
@media(max-width:700px){.rvals,.rops{columns:1}}
.rex{color:var(--txt);font-weight:400}
.rex code,pre.rex{font-weight:400}
.rfmt{margin:8px 0;padding:9px 12px;background:var(--sunk);
border:1px solid var(--bord);border-radius:var(--rc);color:var(--acc);
font:600 .95em ui-monospace,monospace;overflow-x:auto;white-space:pre}
/* `table-layout:AUTO` et `min-width:0`, tous deux contre le `table{}` global
   qui impose `fixed` et 640 px pour les tables de reglages. Sous `fixed`, le
   `width:1px` des deux premieres colonnes est obei au pied de la lettre : la
   colonne du milieu - celle qui porte le NOM de la colonne remplie - se
   reduisait a un pixel et ne laissait qu un chevron. */
.rdoc{width:100%;border-collapse:collapse;margin:0 0 10px;
table-layout:auto;min-width:0}
.rdoc td{padding:4px 10px 4px 0;vertical-align:top;
border-bottom:1px solid rgba(255,255,255,.05)}
.rdoc tr:last-child td{border-bottom:0}
.rdoc td:first-child{width:8.5em;white-space:nowrap;text-align:right;
padding-right:14px}
/* A block inside a panel: same idea as `.sysgrp` in System, and the same rule -
   the separator belongs BETWEEN blocks, never above the first. */
.blk+.blk{margin-top:16px;padding-top:14px;border-top:1px solid var(--bord)}
/* The notification's two sliders read as one pair, so they sit on one row and
   wrap together rather than stacking under a half-empty column. */
.nrow{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:0 26px}
.cgrid2b{display:grid;grid-template-columns:1fr 1fr;gap:20px}
/* A GRID ITEM DEFAULTS TO min-width:auto, so it refuses to shrink below
   its content and overflows the column instead - which is how the band
   section's switches ended up half off the panel (user 08-04). The two
   children have to be told they may be narrower than what is in them;
   the labels then wrap, which is the correct give. */
.cgrid2b>*{min-width:0}
.opt>span,.slider .sl-l{min-width:0;overflow-wrap:anywhere}
/* The slider's value sits at the end and must not be squeezed out. */
.slider input[type=range]{flex:1;min-width:0}.slider>b{flex:none}
@media(max-width:700px){.cgrid2b{grid-template-columns:1fr}}
.lbl2{font-size:.7em;text-transform:uppercase;letter-spacing:.1em;color:var(--mut);margin:0 0 8px;font-weight:600}
.field{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
.field.mb{margin-bottom:12px}.mt{margin-top:12px}
details>summary:hover{color:var(--txt)}
.sst{margin-left:auto;text-transform:none;letter-spacing:0;font-weight:400;
color:var(--mut);font-size:1.15em}
details[open]>summary{border-radius:var(--r) var(--r) 0 0;color:var(--acc);
border-bottom-color:transparent}
details>div{background:var(--glass);backdrop-filter:blur(22px) saturate(1.5);
-webkit-backdrop-filter:blur(22px) saturate(1.5);
border:1px solid var(--bord);border-top:0;
border-radius:0 0 var(--r) var(--r);padding:13px 16px;
}
/* ---- controls ---- */
input[type=range]{width:150px;vertical-align:middle;accent-color:var(--acc)}
input[type=text],input[type=password],input[type=number]{background:var(--sunk);
color:var(--txt);border:1px solid var(--bord);border-radius:var(--rc);
padding:8px 12px;backdrop-filter:blur(8px);outline:none;transition:border .15s}
input:focus{border-color:var(--acc)}
input[type=file]{color:var(--mut);font-size:.85em}
select{background:var(--sunk);color:var(--txt);border:1px solid var(--bord);
border-radius:var(--rc);padding:6px 9px;font:inherit;font-size:.85em;outline:none}
select:focus{border-color:var(--acc)}
progress{appearance:none;height:8px;border:0;border-radius:var(--rc);
background:rgba(255,255,255,.08);overflow:hidden}
progress::-webkit-progress-bar{background:rgba(255,255,255,.08)}
progress::-webkit-progress-value{background:linear-gradient(90deg,var(--acc),var(--acc-lt))}
.desc{color:var(--mut);font-size:.82em}
code{background:var(--sunk);border:1px solid var(--bord);
border-radius:var(--rc);padding:1px 6px;font-size:.92em;color:var(--acc)}
/* ---- servo pad: compact FLEX (3 centred rows) — the old grid
   height:calc(100%-30px) stretched the pad over the whole section and pushed
   the servo toggles out under the following sections (user 07-21) ---- */
.pad{display:flex;flex-direction:column;gap:5px;align-items:center;
justify-content:center;flex:1;margin:6px 0 2px}
.pad .prow{display:flex;gap:5px;justify-content:center}
.pad button{width:60px;padding:13px 0;font-size:1.1em;margin:0}
/* ---- options: label row + switch ---- */
.opt{display:flex;justify-content:space-between;align-items:center;
padding:8px 2px;border-bottom:1px solid rgba(255,255,255,.07);font-size:.9em}
.opt:last-child{border-bottom:0}
/* ---- option groups: nine switches in one list is a list you read to the end
   every time. Grouped by WHAT THEY ACT ON, in columns, so "is the microphone
   on" is answered by looking at one place instead of scanning ten rows. ---- */
/* THREE CARDS, like Pilot's row (user 08-10). The auto-fit grid packed the
   groups by available width, so a four-group set wrapped one under another and
   the headings stopped lining up - which is the one thing a column heading is
   for. `.cgrid` is the row the Emotions/Dances/Head cards already use; reusing
   it is also one layout fewer to keep in agreement with itself. */
.ogrp{min-width:0}
.sw{position:relative;width:46px;height:26px;flex:none}
.sw input{opacity:0;width:0;height:0}
.sl{position:absolute;inset:0;background:rgba(255,255,255,.12);
border:1px solid var(--bord);border-radius:13px;transition:.18s;cursor:pointer}
.sl:before{content:"";position:absolute;width:20px;height:20px;left:2px;top:2px;
background:#fff;border-radius:50%;transition:.18s;
}
.sw input:checked+.sl{background:var(--acc-grad);
border-color:transparent}
.sw input:checked+.sl:before{transform:translateX(20px)}
/* ---- tuning: tables by category, columns ALIGNED across categories ---- */
#tun{overflow-x:auto}
.tcat{margin:16px 0 4px;font-size:.8em;font-weight:600;letter-spacing:.06em;
text-transform:uppercase;color:var(--acc2);
border-bottom:1px solid var(--bord);padding-bottom:4px}
.tcat:first-child{margin-top:2px}
table{width:100%;min-width:640px;border-collapse:collapse;table-layout:fixed}
td{padding:6px 8px;font-size:.84em;border-bottom:1px solid rgba(255,255,255,.06);
vertical-align:middle;overflow:hidden}
tr:hover td{background:rgba(255,255,255,.03)}
tr:last-child td{border-bottom:0}
td.tid{width:185px;text-align:right;white-space:nowrap}
td.tdef{width:56px;text-align:center;color:var(--mut);
font-family:ui-monospace,monospace}
td.tsl{width:270px;white-space:nowrap}
td.tsl b{color:var(--acc);margin-left:6px}
.rmin,.rmax{display:inline-block;width:23px;color:var(--mut);font-size:.72em;opacity:.75;vertical-align:middle}
.rmin{text-align:right;margin-right:5px}
.rmax{text-align:left;margin-left:5px}
td.tdesc{text-align:left;color:var(--mut);font-size:.82em}
a{color:var(--acc);text-decoration:none}
a:hover{text-decoration:underline}
/* ---- camera panel: settings on the left, view/stream on the right ---- */
.camwrap{display:flex;gap:16px;flex-wrap:wrap;align-items:flex-start}
.camL{flex:1 1 340px;min-width:300px}
.camR{flex:1 1 320px;min-width:280px}
.camR img{width:100%;border-radius:var(--rc);border:1px solid var(--bord);
background:rgba(5,8,16,.7);min-height:120px;display:none}
.camph{display:flex;flex-direction:column;align-items:center;
justify-content:center;gap:8px;min-height:200px;border:1px dashed var(--bord2);
border-radius:var(--rc);background:rgba(5,8,16,.4);color:var(--mut);font-size:.9em}
.camph .ico{font-size:2.2em;opacity:.45}
.ctr{display:flex;justify-content:space-between;align-items:center;gap:8px;
padding:6px 2px;border-bottom:1px solid rgba(255,255,255,.07);font-size:.86em}
.ctr:last-child{border-bottom:0}
.ctr>span:first-child{color:var(--mut)}
.ctr input[type=range]{width:130px}
.ctr b{color:var(--acc);display:inline-block;min-width:26px;text-align:right}
/* ---- SD file manager (dances/bins/config/rules unified) ---- */
.fm{display:flex;flex-direction:column;gap:2px}
.fgrp{margin-top:12px}.fgrp:first-child{margin-top:0}
.fgrp .gh{font-size:.68em;text-transform:uppercase;letter-spacing:.1em;color:var(--acc2);
 border-bottom:1px solid var(--bord);padding-bottom:4px;margin-bottom:4px}
.fgrp .ghs{color:var(--mut);opacity:.7;text-transform:none;letter-spacing:0;margin-left:8px}
.frow{display:flex;align-items:center;gap:8px;padding:6px 2px;
 border-bottom:1px solid rgba(255,255,255,.05);font-size:.88em}
.frow:last-child{border-bottom:0}
a.fnm{color:var(--txt);text-decoration:none;border-bottom:1px dashed var(--mut)}
a.fnm:hover{color:var(--acc);border-color:var(--acc)}
a.fnm::after{content:" ⬇";font-size:.8em;color:var(--acc);opacity:.7}
.fsz{color:var(--mut);font-size:.76em;margin-left:auto;font-family:ui-monospace,monospace}
.frow button{padding:3px 9px;font-size:.85em;margin:0}
.frow .del{color:var(--ko);border-color:rgba(248,113,113,.4)}
.frow .del:hover{background:var(--ko);color:#08090c;border-color:var(--ko)}
button.b-acc{background:var(--acc-grad);color:#06121f;
 border-color:transparent;font-weight:600}
</style></head><body>
<div class="top"><header><div class="wrap">
<div class="brand"><span class="logo"></span>
<div><h1>STACKCHAN</h1><div class="sub" data-i18n="sub">companion console · K151</div></div>
</div>
<nav id="tabs">
<button data-tab="pilot" class="on" onclick="tab('pilot')"><svg class="ti" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="3"/></svg><span class="tl" data-i18n="tb_pilot">Pilot</span></button>
<button data-tab="opt" onclick="tab('opt')"><svg class="ti" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"/><path d="M12 3v3M12 18v3M3 12h3M18 12h3M5.6 5.6l2 2M16.4 16.4l2 2M18.4 5.6l-2 2M7.6 16.4l-2 2"/></svg><span class="tl" data-i18n="tb_opt">Options</span></button>
<button data-tab="perso" onclick="tab('perso')"><svg class="ti" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="8" r="4"/><path d="M4 21v-1a6 6 0 0 1 6-6h4a6 6 0 0 1 6 6v1"/></svg><span class="tl" data-i18n="tb_perso">Characters</span></button>
<button data-tab="files" onclick="tab('files')"><svg class="ti" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 5h5l2 2h9v11a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V6a1 1 0 0 1 1-1z"/></svg><span class="tl" data-i18n="tb_files">Files</span></button>
<button data-tab="sys" onclick="tab('sys')"><svg class="ti" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="4" y="4" width="16" height="16" rx="2"/><rect x="9" y="9" width="6" height="6"/><path d="M9 2v2M15 2v2M9 20v2M15 20v2M2 9h2M2 15h2M20 9h2M20 15h2"/></svg><span class="tl" data-i18n="tb_sys">System</span></button>
</nav></div></header>
<!-- TELEMETRY. Outside `main` on purpose: it belongs to no tab and is true
     for all of them, so it runs edge to edge under the header as its
     continuation. Collapsible and remembered - a chart is worth a glance on
     arrival and not worth a fifth of the viewport while you work. -->
<details id="tele" open ontoggle="try{localStorage.setItem('sce_tele',this.open?1:0)}catch(e){};drawChart()">
<summary><span data-i18n="h_tele">Live telemetry</span><div id="legend"></div></summary>
<div class="tw">
<canvas id="chart" height="56"></canvas>
<div id="grid"></div>
<!-- The two switches that are ABOUT telemetry, gathered with it: one publishes
     it on the robot's own band, the other on the serial line. Neither is a
     property of the status band or of the options they used to sit in. -->
<div id="teleopt">
<div class="opt"><span data-i18n="o_bdbg">Debug info (emotion·IP)</span><label class="sw">
<input type="checkbox" id="o_band_debug" onchange="T('band_debug',this.checked?1:0)"><span class="sl"></span></label></div>
<div class="opt"><span data-i18n="o_tel">Serial telemetry</span><label class="sw">
<input type="checkbox" id="o_telemetry" onchange="T('telemetry',this.checked?1:0)"><span class="sl"></span></label></div>
<!-- Same key as the System > Debug switch; a SECOND id, kept in step by the
     resync below - two controls, one register, no disagreement beyond one
     refresh. Here because it sits with its siblings: telemetry narrates the
     numbers, the trace narrates the steps. -->
<div class="opt"><span data-i18n="o_dbg2">Debug trace (serial)</span><label class="sw">
<input type="checkbox" id="o_debug_t" onchange="T('debug',this.checked?1:0)"><span class="sl"></span></label></div>
<!-- THE ICON PILLS, pinned right on the same row (user 08-10). They choose
     what the robot's own status row shows, which is the same question this
     band answers about the robot's state - and they were the tail of the
     status-band panel, three scrolls away from the chips they mirror. -->
<div id="teleicons">
<div class="lbl2" data-i18n="l_icons">Visible icons</div>
<div class="pills" id="iconPills">
<span class="pill" data-bit="1" data-i18n="ic_batt" onclick="this.classList.toggle('on');setIcons()">battery</span>
<span class="pill" data-bit="16" data-i18n="ic_night" onclick="this.classList.toggle('on');setIcons()">night</span>
<span class="pill" data-bit="8" data-i18n="ic_mic" onclick="this.classList.toggle('on');setIcons()">mic</span>
<span class="pill" data-bit="4" data-i18n="ic_cam" onclick="this.classList.toggle('on');setIcons()">camera</span>
<span class="pill" data-bit="2" onclick="this.classList.toggle('on');setIcons()">wifi</span></div>
</div>
</div>
</div></details>
</div>
<main>

<section class="tab on" id="t-pilot">
<div class="cgrid">
<section><h2 data-i18n="h_emo">Emotions · 10 s priority</h2><div class="row" id="emo"></div></section>
<section><h2 class="h2row"><span data-i18n="h_dance">Dances</span><button class="stop"
data-i18n-title="stop_t" title="Stop the dance"
aria-label="Stop the dance" onclick="P('/api/dance?name=stop',this)"></button></h2>
<div class="row" id="dan"></div>
<div class="row" style="margin-top:6px">
<button data-i18n="blink" onclick="P('/api/animation?name=blink',this)">Blink</button>
<button data-i18n="winkL" onclick="P('/api/animation?name=winkLeft',this)">Wink L</button>
<button data-i18n="winkR" onclick="P('/api/animation?name=winkRight',this)">Wink R</button></div></section>
<section><h2 class="h2row"><span data-i18n="h_head">Head</span><span class="h2v" id="hpose">yaw —° · pitch —°</span></h2>
<!-- yaw+ = head toward the observer's RIGHT -->
<div class="pad">
<div class="prow"><button onclick="P('/api/servo?dpitch=-10',this)">↑</button></div>
<div class="prow"><button onclick="P('/api/servo?dyaw=-10',this)">←</button>
<button onclick="P('/api/servo?yaw=166&pitch=93',this)">⌂</button>
<button onclick="P('/api/servo?dyaw=10',this)">→</button></div>
<div class="prow"><button onclick="P('/api/servo?dpitch=10',this)">↓</button></div>
</div>
<div style="margin-top:8px">
<!-- SERVOS FIRST, then the two things that ride on it (user 08-17). It is the
     MASTER switch of the group: with the torque released neither of the two
     below can move anything, so the block reads in the order it depends --
     the rail, then what uses it. That ordering is also what lets the two
     labels drop "head" from their own text: the group already says it.
     Sound tracking used to live in Options next to the microphone because it
     NEEDS the microphone, but `setTrack` turns the mic on when you enable it,
     so the adjacency bought nothing and the split cost a tab (user 08-10). -->
<div class="opt"><span data-i18n="o_servos">Servos</span><label class="sw">
<input type="checkbox" id="o_servos" onchange="onServosOpt(this)"><span class="sl"></span></label></div>
<div class="opt"><span data-i18n="o_hfollow">Sync with the eyes</span><label class="sw">
<input type="checkbox" id="o_head_follow" onchange="T('head_follow',this.checked?1:0)"><span class="sl"></span></label></div>
<div class="opt"><span data-i18n="o_track">Sound tracking</span><label class="sw">
<input type="checkbox" id="o_sound_track" onchange="setTrack(this.checked)"><span class="sl"></span></label></div>
</div></section>
</div>
<details id="dBand" open><summary><svg class="ci" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="4" width="18" height="16" rx="2"/><path d="M3 15h18"/></svg><span data-i18n="h_band">Status band</span></summary><div>
<!-- NOTIFICATION — ITS OWN BLOCK, and not a corner of Mode (user 08-10).
     A notification INTERRUPTS whatever the band was showing, whichever mode
     that is, and it scrolls at its own size and its own speed. Sitting under
     the mode selector with those two sliders beside it, all three read as
     settings OF the mode — which is how someone ends up switching mode to
     change the size of a message. -->
<div class="blk">
<div class="lbl2" data-i18n="l_notif">Notification</div>
<div class="field mb">
<input type="text" id="sayTxt" data-i18n-ph="say_ph" placeholder="notification to display…" style="flex:1;min-width:150px">
<button class="b-acc" data-i18n="say" onclick="P('/api/say?ms='+Math.max(4000,sayTxt.value.length*250)+'&text='+encodeURIComponent(sayTxt.value),this)">Say ↗</button></div>
<div class="nrow">
<div class="slider"><span class="sl-l" data-i18n="l_tsize">Text size</span><input type="range" id="bts" min="1" max="3" step="1"
 oninput="btsv.textContent=this.value" onchange="T('band_text_size',this.value)"> <b id="btsv">1</b></div>
<div class="slider"><span class="sl-l" data-i18n="l_speed">Speed (px/s)</span><input type="range" id="bss" min="10" max="200" step="5"
 oninput="bssv.textContent=this.value" onchange="T('band_scroll_speed',this.value)"> <b id="bssv">70</b></div>
</div>
<p class="desc" data-i18n="d_say" style="margin:6px 0 0">Applies to every notification, alert or `say`, whatever the band is showing underneath. An alert is drawn at size 2 minimum whatever this says: it is meant to be read.</p>
</div>

<div class="blk">
<div class="field mb"><span class="lbl2" style="margin:0">Mode</span>
<div class="seg" id="o_statusbar">
<button data-m="0" data-i18n="sb_none" onclick="setMode(0)">Default</button>
<button data-m="2" data-i18n="sb_vu" onclick="setMode(2)">Sound</button>
<button data-m="3" data-i18n="sb_gauge" onclick="setMode(3)">Gauges</button>
<button data-m="4" data-i18n="sb_timer" onclick="setMode(4)">Timer</button>
<button data-m="5" data-i18n="sb_pomo" onclick="setMode(5)">Pomodoro</button></div></div>
<div>
<!-- Only what the CURRENT mode can use. A clock offset shown while the band is
     the sound visualiser is a control that does nothing, and a control that
     does nothing teaches people to distrust the panel. `syncMode` toggles
     these blocks. -->
<div class="bandm" data-mode="0">
<div class="opt"><span data-i18n="o_bclk">Clock in Default mode</span><label class="sw">
<input type="checkbox" id="o_band_clock" onchange="T('band_clock',this.checked?1:0)"><span class="sl"></span></label></div>
<div class="opt"><span data-i18n="o_c24">24-hour clock</span><label class="sw">
<input type="checkbox" id="o_clock_24h" onchange="T('clock_24h',this.checked?1:0)"><span class="sl"></span></label></div>
<div class="slider"><span class="sl-l" data-i18n="l_tz">UTC offset (h)</span><input type="range" id="tzo" min="-14" max="14" step="0.25"
 oninput="tzov.textContent=this.value" onchange="T('tz_offset_h',this.value)"> <b id="tzov">0</b></div>
</div>
<div class="bandm" data-mode="2">
<div class="lbl2" data-i18n="l_mstyle">Style</div>
<div class="seg" id="o_snd">
<button data-s="0" data-i18n="ms_wave" onclick="setSnd(0)">Wave</button>
<button data-s="1" data-i18n="ms_col" onclick="setSnd(1)">Columns</button>
<button data-s="2" data-i18n="ms_mtx" onclick="setSnd(2)">Matrix</button></div>
<div class="slider"><span class="sl-l" data-i18n="l_sgain">Sensitivity</span><input type="range" id="sgn" min="0.25" max="16" step="0.25"
 oninput="sgnv.textContent=this.value+'x'" onchange="T('band_sound_gain',this.value)"> <b id="sgnv">1x</b></div>
<!-- THE SWITCH THIS MODE CANNOT WORK WITHOUT, here rather than named in a
     sentence pointing at another tab (user 08-10). It says what the microphone
     is doing right now and offers the one action that changes it; when the mic
     is already listening there is nothing to offer, so the button is gone
     rather than greyed. -->
<div class="field mt" id="sndMicRow">
<span class="desc" id="sndMicState">…</span>
<button id="sndMicBtn" class="b-acc" data-i18n="snd_micon" onclick="setMic(true)" hidden>Turn the microphone on</button></div>
<div class="desc" data-i18n="d_vu">Three skins of the same triggered waveform — curve, strokes, blocks — fed by both microphones. Nothing is drawn when it is quiet. Sensitivity is a gain applied before the analysis: raise it in a quiet room, lower it if the shape saturates.</div>
</div>
<div class="bandm" data-mode="3">
<div class="lbl2" data-i18n="l_gauges">Test gauges</div>
<!-- CAPPED. Full width the mode block is now, a three-digit percentage does
     not become a thousand-pixel field, and a "Push" button as wide as the
     panel looks like the panel's own action rather than this mode's. -->
<div style="display:flex;flex-direction:column;gap:6px;max-width:280px">
<input type="number" id="jg0" placeholder="CTX %" min="0" max="100">
<input type="number" id="jg1" placeholder="5H %" min="0" max="100">
<input type="number" id="jg2" placeholder="7J %" min="0" max="100">
<button class="b-acc" data-i18n="push" onclick="pushGauges(this)">Push</button></div>
</div>
<div class="bandm" data-mode="4">
<div class="lbl2" data-i18n="l_dur">Duration</div>
<!-- The two fields read as ONE value, "07:30", so the minutes are
     right-aligned against the colon and the units live in the placeholders
     instead of in labels beside them (user 08-04): a field that already says
     "min" when empty does not need the word repeated next to it. -->
<div class="field"><input type="number" id="tmM" min="0" max="99" placeholder="min" style="width:4.5em;text-align:right">
<b style="opacity:.7">:</b>
<input type="number" id="tmS" min="0" max="59" placeholder="sec" style="width:4.5em">
<button data-i18n="set" onclick="tmSet(this)">Set</button></div>
<!-- PRESETS: one press = set AND start, in a single request. They step over
     the edit lock on purpose — pressing "10 min" is an explicit intent, not
     the stray brush the lock protects a running countdown from. -->
<div class="lbl2 mt" data-i18n="l_quick">Quick start</div>
<div class="field" id="tmPre">
<button onclick="tmGo(5,this)">5&nbsp;min</button>
<button onclick="tmGo(7,this)">7&nbsp;min</button>
<button onclick="tmGo(10,this)">10&nbsp;min</button>
<button onclick="tmGo(15,this)">15&nbsp;min</button></div>
<div class="field mt"><button class="b-acc" data-i18n="t_tap" onclick="P('/api/timer?action=tap',this)">▶ / ⏸</button>
<button data-i18n="t_rst" onclick="P('/api/timer?action=reset',this)">↺ Reset</button></div>
<div class="desc mt" data-i18n="d_tmr">On the robot: slide on the band to set, tap to start or pause, hold to reset.</div>
</div>
<div class="bandm" data-mode="5">
<div class="slider"><span class="sl-l" data-i18n="l_work">Work (min)</span><input type="range" id="pw" min="1" max="120" step="1"
 oninput="pwv.textContent=this.value" onchange="T('pomo_work_min',this.value)"> <b id="pwv">25</b></div>
<div class="slider"><span class="sl-l" data-i18n="l_break">Break (min)</span><input type="range" id="pb" min="1" max="60" step="1"
 oninput="pbv.textContent=this.value" onchange="T('pomo_break_min',this.value)"> <b id="pbv">5</b></div>
<div class="slider"><span class="sl-l" data-i18n="l_hydra">Hydration (min)</span><input type="range" id="ph" min="0" max="15" step="1" value="1"
 oninput="phv.textContent=this.value" onchange="T('pomo_hydra_min',this.value)"> <b id="phv">1</b></div>
<div class="slider"><span class="sl-l" data-i18n="l_cycles">Cycles</span><input type="range" id="pc" min="1" max="8" step="1"
 oninput="pcv.textContent=this.value" onchange="T('pomo_cycles',this.value)"> <b id="pcv">4</b></div>
<div class="field mt"><button class="b-acc" data-i18n="t_tap" onclick="P('/api/timer?action=tap',this)">▶ / ⏸</button>
<button data-i18n="t_rst" onclick="P('/api/timer?action=reset',this)">↺ Reset</button></div>
<div class="desc mt" data-i18n="d_pomo">On the robot: TAP anywhere on the band to start or pause, tap the PHASE ICON (left of the digits) to skip the current phase, HOLD to reset, and swipe up/down while stopped to cycle the four session shapes. Changing a duration re-arms the next block; it does not cut the one running.</div>
</div>
<div class="field mt"><span class="sl-l" data-i18n="l_tdance">Dance at the end</span>
<select id="tdance" onchange="T('timer_dance',this.value)"><option value="0">—</option></select></div>
<div class="desc" data-i18n="d_tdance">Played when a countdown ENDS: the ring for the timer, the last block for the pomodoro. Not on the phase changes, which come round too often to stand up for.</div>
</div>
</div>

</div></details>

<!-- RULES — A SECTION OF ITS OWN (user 08-10). They were the third block of
     the status-band panel, which is where they least belong: a rule watches a
     field and posts a command, and the band is one of the things it may end up
     changing, not its home. -->
<details id="dRules" open ontoggle="if(this.open)loadRules(null)"><summary><svg class="ci" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M4 6h16M4 12h10M4 18h7"/><circle cx="18" cy="12" r="2"/><circle cx="15" cy="18" r="2"/></svg><span data-i18n="h_rules">Rules · reactive plugins</span> <span id="rulesState" class="sst"></span></summary><div>
<!-- WHAT YOU CAME FOR IS FIRST. The section used to open with a format
     line, a worked example, an eight-row column reference and three
     paragraphs of caveats — all of it true, none of it what you want on the
     nine visits out of ten where you only wanted to see whether your rule is
     loaded. One line of context, the two buttons, the table; the how-to is
     folded underneath for the tenth visit. -->
<p class="rlead" data-i18n-html="d_lead">Rules are written in <code>rules.txt</code> on the SD card, one per line. The robot follows them <b>on its own</b>. A rule watches one field, and when it crosses a threshold it triggers an action.</p>
<div class="row" style="margin-bottom:10px">
<button data-i18n="rules_rl" onclick="reloadRules(this)">↻ Re-read rules.txt</button>
<button data-i18n="rules_rf" onclick="loadRules(this)">↺ Refresh this table</button></div>
<div id="rulesBox"><span class="desc">…</span></div>

<details class="rguide"><summary data-i18n="h_howto">How to write one</summary>
<div class="rhelp">
<p data-i18n-html="d_g0">A rule is <b>one line of nine columns</b> separated by <code>|</code>. Here is a real one, taken from the file the robot ships with:</p>
<pre class="rfmt rex">claude | ctx | ge | 90 | 3000 | 60000 | SetEmotion | Scared | 4000</pre>
<p class="rread" data-i18n-html="d_gread"><i>While the Claude bridge is running, if the context window reaches 90 % and stays there for three seconds, look Scared for four seconds — and do not do it again for a minute.</i></p>
<!-- EACH TOKEN OF THAT LINE, in order, against its column. The reader can
     point at the thing being explained instead of holding nine abstract
     definitions in their head. -->
<table class="rdoc rann"><tbody>
<tr><td><code>claude</code></td><td><code>enable</code></td><td data-i18n-html="a_en">A <b>gate</b>. The rule sleeps while this field is under 0.5 — here, while nothing is publishing <code>claude</code>. Leave the column empty and the rule is always awake.</td></tr>
<tr><td><code>ctx</code></td><td><code>field</code></td><td data-i18n="a_f">The field being watched.</td></tr>
<tr><td><code>ge</code></td><td><code>op</code></td><td data-i18n="a_op">How it is compared: “greater than or equal”.</td></tr>
<tr><td><code>90</code></td><td><code>value</code></td><td data-i18n="a_v">The threshold it is compared to.</td></tr>
<tr><td><code>3000</code></td><td><code>sustainMs</code></td><td data-i18n-html="a_sus">The condition must hold this long, in milliseconds, before the rule fires. <code>0</code> fires immediately.</td></tr>
<tr><td><code>60000</code></td><td><code>cooldownMs</code></td><td data-i18n="a_cd">The shortest delay before this rule may fire again, in milliseconds.</td></tr>
<tr><td><code>SetEmotion</code></td><td><code>action</code></td><td data-i18n="a_act">What it does.</td></tr>
<tr><td><code>Scared</code></td><td><code>a1</code></td><td data-i18n="a_a1">The action's first argument — here, which emotion.</td></tr>
<tr><td><code>4000</code></td><td><code>a2</code></td><td data-i18n="a_a2">The second — here, for how many milliseconds.</td></tr>
</tbody></table>

<div class="rsub" data-i18n="h_vals">What you may put in each column</div>

<div class="rvsub"><code>field</code> <span data-i18n="v_f">— published by the robot itself:</span></div>
<ul class="rvals">
<li><code>batt</code> <span data-i18n="vf_batt">battery, 0-100</span></li>
<li><code>chg</code> <span data-i18n="vf_chg">1 while charging</span></li>
<li><code>rssi</code> <span data-i18n="vf_rssi">WiFi strength, dBm (negative)</span></li>
<li><code>light</code> <span data-i18n="vf_light">ambient light, 0-100</span></li>
<li><code>night</code> <span data-i18n="vf_night">1 between real sunset and sunrise</span></li>
<li><code>dark_sleepy</code> <span data-i18n="vf_dark">1 when the “sleep in the dark” option is on</span></li>
<li><code>cam</code> <span data-i18n="vf_cam">1 when the camera is streaming</span></li>
<li><code>mic</code> <span data-i18n="vf_mic">1 when the microphone is listening</span></li>
<li><code>micL</code> <code>micR</code> <span data-i18n="vf_miclr">per-microphone level, 0-1</span></li>
<li><code>ip</code> <span data-i18n="vf_ip">the address, as text</span></li>
</ul>
<p class="rvnote" data-i18n-html="v_fmine">Anything else is yours. <code>POST /api/field?name=value</code> from a script, a cron job, Home Assistant — the robot does not care who wrote it. An unknown field reads <b>0</b>.</p>

<div class="rvsub"><code>op</code></div>
<ul class="rvals rops">
<li><code>gt</code> <span data-i18n="vo_gt">strictly greater</span></li>
<li><code>ge</code> <span data-i18n="vo_ge">greater or equal</span></li>
<li><code>lt</code> <span data-i18n="vo_lt">strictly less</span></li>
<li><code>le</code> <span data-i18n="vo_le">less or equal</span></li>
<li><code>eq</code> <span data-i18n="vo_eq">equal</span></li>
<li><code>ne</code> <span data-i18n="vo_ne">different</span></li>
</ul>

<div class="rvsub"><code>action</code> <span data-i18n="v_a">— with its arguments:</span></div>
<ul class="rvals">
<li><code>SetEmotion</code> <code class="rarg">&lt;name&gt; [ms]</code> <span data-i18n="va_emo">one of the 30 expressions, optionally for a duration</span></li>
<li><code>PlayDance</code> <code class="rarg">&lt;name&gt;</code> <span data-i18n="va_dance">one of the choreographies listed in the Dances card</span></li>
<li><code>Blink</code> · <code>WinkLeft</code> · <code>WinkRight</code> <span data-i18n="va_wink">no argument</span></li>
<li><code>AmbientDark</code> <code class="rarg">0|1</code> <span data-i18n="va_dark">night mode: Sleepy becomes the dominant idle</span></li>
<li><code>set</code> <code class="rarg">&lt;field&gt; &lt;value&gt;</code> <span data-i18n="va_set">writes a field back, so one rule can arm another</span></li>
</ul>

<div class="rsub" data-i18n="h_where">Where to put it</div>
<p data-i18n-html="d_where">Files tab → <i>Import</i> → <code>rules.txt</code>, then <b>↻ Re-read rules.txt</b> above. Your rule appears in the table — or it does not, which is how you find out the line did not parse. The comment written just above it becomes its description there.</p>

<div class="rsub" data-i18n="h_gotcha">Three things to know</div>
<ul class="rnotes">
<li data-i18n-html="d_n1"><b>A rule fires once.</b> It waits for the condition to go false again before it can fire a second time, so a field parked above its threshold does not repeat.</li>
<li data-i18n-html="d_n2"><b>An unknown field reads 0.</b> So <code>ctx lt 20</code> is TRUE when nothing publishes <code>ctx</code> — that is what <code>enable</code> is for.</li>
<li data-i18n-html="d_n3"><b>Nothing you write can break the robot.</b> A rule only posts the actions listed above, and the reflexes outrank it. A line over 127 bytes is discarded whole, silently — that is the one failure that is quiet.</li>
</ul>
</div></details>
</div></details>
</section>

<section class="tab" id="t-opt">
<!-- THREE CARDS, grouped by what they act on and laid out like Pilot's row.
     Nine switches in one column is a list you read to the end every time;
     "is the microphone on" should be answered by looking at one place. No
     heading of its own: the tab is already called Options, and a panel inside
     it repeating the word said nothing twice. -->
<div class="cgrid">
<section><h2 data-i18n="og_screen">Screen</h2>
<!-- The PANEL backlight, whole screen. Distinct from `eye_color_dim` in Fine
     tuning, which only darkens the eye palette and leaves the band, the
     launcher and the guest bins at full blast. Moving this slider turns the
     adaptive option OFF: a manual value the sensor overwrites two seconds
     later is a control that does nothing. -->
<div class="slider"><span class="sl-l" data-i18n="l_sbr">Screen brightness</span><input type="range" id="sbr" min="10" max="255" step="5"
 oninput="sbrv.textContent=this.value" onchange="setBright(this.value)"> <b id="sbrv">76</b></div>
<div class="opt"><span data-i18n="o_ab">Adaptive brightness</span><label class="sw">
<input type="checkbox" id="o_auto_brightness" onchange="T('auto_brightness',this.checked?1:0)"><span class="sl"></span></label></div>
<div class="opt"><span data-i18n="o_dark">Sleep in the dark</span><label class="sw">
<input type="checkbox" id="o_dark_sleepy" onchange="T('dark_sleepy',this.checked?1:0)"><span class="sl"></span></label></div>
<div class="opt"><span data-i18n="o_crt">CRT effect</span><label class="sw">
<input type="checkbox" id="o_crt" onchange="P('/api/config?crt='+(this.checked?1:0))"><span class="sl"></span></label></div>
<!-- THE PERSONALITY SELECTOR AND THE ROULETTE SWITCH MOVED to the Characters
     tab, and this note is here so the next person looks there rather than adds
     them back. They lived here while a personality was ONE setting; now that a
     character can be created, edited and deleted, a list you edit needs room —
     and TWO controls for one tuning key would be two things to keep in step,
     which is the drift the dance-select race already cost once. -->
</section>

<section><h2 data-i18n="og_leds">LEDs</h2>
<div class="opt"><span data-i18n="o_leds">Emotion LEDs</span><label class="sw">
<input type="checkbox" id="o_leds" onchange="T('leds',this.checked?1:0)"><span class="sl"></span></label></div>
<div class="opt"><span data-i18n="o_ledsw">Swap LED order</span><label class="sw">
<input type="checkbox" id="o_led_swap" onchange="T('led_swap',this.checked?1:0)"><span class="sl"></span></label></div>
<div class="opt"><span data-i18n="o_leddf">LED index 0 at the front</span><label class="sw">
<input type="checkbox" id="o_led_depth_front" onchange="T('led_depth_front',this.checked?1:0)"><span class="sl"></span></label></div>
</section>

<section><h2 data-i18n="og_sound">Sound</h2>
<div class="opt"><span>Chirps</span><label class="sw">
<input type="checkbox" id="o_sound" onchange="T('sound',this.checked?1:0)"><span class="sl"></span></label></div>
<!-- TWO settings, one microphone. The mic can run without the head following
     what it hears — that is what lets the band's sound visualiser be watched
     on a robot that stays still. Turning tracking ON turns the mic on too,
     because tracking cannot work without it; turning the mic OFF turns
     tracking off for the same reason. Neither can be left in a state that
     silently does nothing. -->
<div class="opt"><span data-i18n="o_mic">Microphone</span><label class="sw">
<input type="checkbox" id="o_mic_enable" onchange="setMic(this.checked)"><span class="sl"></span></label></div>
</section>
</div>
<details><summary><svg class="ci" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M23 7l-7 5 7 5V7z"/><rect x="1" y="5" width="15" height="14" rx="2"/></svg><span data-i18n="h_cam">Camera</span> <span id="camState" class="sst"></span></summary><div>
<div class="camwrap">
<div class="camL">
<div class="opt"><span data-i18n="o_cam">Enable the camera</span><label class="sw">
<input type="checkbox" id="o_camera" onchange="onCamOpt(this)"><span class="sl"></span></label></div>
<div id="camtun"></div>
<p class="desc" data-i18n="cam_desc" style="margin:8px 2px 0">GC0308 — init on demand, off at
rest, VOR active during the stream. Settings applied on the fly (~1 s).</p>
</div>
<div class="camR">
<div id="camPh" class="camph"><span class="ico">📷</span>
<span data-i18n="cam_stopped">View stopped</span>
<span class="desc" data-i18n="cam_hint">Click “▶ View camera”</span></div>
<img id="camImg" alt="camera">
<div id="camMsg" class="desc" style="margin-top:4px"></div>
<div class="row" style="margin-top:6px">
<button id="camBtn" data-i18n="cam_view" onclick="toggleCam(this)">▶ View camera</button>
<button data-i18n="cam_snap" onclick="snapshot(this)">Snapshot ↗</button>
<a href="/api/camera/stream" target="_blank"><button data-i18n="cam_mjpeg">MJPEG stream ↗</button></a></div>
</div>
</div>
</div></details>
<!-- FINE TUNING is the long tail of this tab, not a chapter of its own: it is
     the same question - what the robot does on its own - asked of seventy
     numbers instead of ten switches. Closed by default; nobody opens it by
     accident and everybody knows where it is now. -->
<details><summary><svg class="ci" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M4 21v-7M4 10V3M12 21v-9M12 8V3M20 21v-5M20 12V3M1 14h6M9 8h6M17 16h6"/></svg><span data-i18n="h_tun">Fine tuning</span></summary><div id="tun"></div></details>
</section>

<section class="tab" id="t-perso">
<!-- THE CHARACTER TAB. It exists because creating and deleting cannot live in
     a row of Options: a list you edit needs room. What it edits is the CARD
     (/stackchan-companion/personalities/*.yaml), never the firmware, so the two
     compiled characters can be reshaped but not removed. -->
<section><h2 data-i18n="pg_who">Who the robot is</h2>
<div class="opt"><span data-i18n="o_perso">Personality</span>
<select id="o_personality" onchange="T('personality',this.value);setTimeout(perLoad,400)"></select></div>
<div class="opt"><span data-i18n="o_roul">Random moods</span><label class="sw">
<input type="checkbox" id="o_roulette" onchange="T('roulette',this.checked?1:0)"><span class="sl"></span></label></div>
<p class="hint" data-i18n="p_roulhint">With random moods off, only rules and reflexes
change the face &mdash; blinking, glancing and breathing carry on.</p>
</section>

<section><h2 data-i18n="pg_edit">Edit a character</h2>
<div class="opt"><span data-i18n="o_pedit">Editing</span>
<select id="pEdit" onchange="perPick()"></select></div>
<div class="row">
<label class="fld"><span data-i18n="l_pname">Name</span>
<input id="pName" type="text" maxlength="15" placeholder="zaku"></label>
<label class="fld"><span data-i18n="l_pcolor">Eye colour</span>
<input id="pColor" type="color" value="#22ff66"></label>
</div>
<div class="opt"><span data-i18n="o_pnocol">Use the emotion palette</span><label class="sw">
<input type="checkbox" id="pNoCol" onchange="perColState()"><span class="sl"></span></label></div>
<p class="hint" data-i18n="p_colhint">One colour makes a character recognisable at a
glance; the emotion palette tells you <em>which</em> feeling is on the face. You
cannot have both.</p>
<div class="row">
<label class="fld"><span data-i18n="l_ptheme">Console skin</span>
<select id="pTheme"><option value="default">Default</option><option value="gundam">Gundam</option></select></label>
<label class="fld"><span data-i18n="l_prules">Rules file</span>
<input id="pRules" type="text" placeholder="/stackchan-companion/rules.txt"></label>
</div>
<div class="slider"><span class="sl-l" data-i18n="l_pmin">Mood every, from</span>
<input type="range" id="pMin" min="1000" max="60000" step="1000"
 oninput="pMinV.textContent=(this.value/1000)+'s'"> <b id="pMinV">6s</b></div>
<div class="slider"><span class="sl-l" data-i18n="l_pmax">&hellip;to</span>
<input type="range" id="pMax" min="1000" max="60000" step="1000"
 oninput="pMaxV.textContent=(this.value/1000)+'s'"> <b id="pMaxV">12s</b></div>
<div class="slider"><span class="sl-l" data-i18n="l_ptscale">Animation pace</span>
<input type="range" id="pTScale" min="0.3" max="3.0" step="0.1"
 oninput="pTScaleV.textContent=(+this.value).toFixed(1)+'x'"> <b id="pTScaleV">1.0x</b></div>
<div class="slider"><span class="sl-l" data-i18n="l_ppscale">Head-bias strength</span>
<input type="range" id="pPScale" min="0" max="2.0" step="0.1"
 oninput="pPScaleV.textContent=(+this.value).toFixed(1)+'x'"> <b id="pPScaleV">1.0x</b></div>
<p class="hint" data-i18n="p_scalehint">1.0 is unscaled. Lower pace is snappier,
higher is dreamier; zero head-bias holds the head level no matter what it feels.</p>
</section>

<section><h2 data-i18n="pg_w">Resting expressions</h2>
<p class="hint" data-i18n="p_whint">What the robot drifts to when nothing is
happening. Leave an emotion at zero and it is never drawn on its own &mdash;
which is what gives the rules their weight: a face it never wears by chance
means something when a rule puts it there.</p>
<p class="hint" id="pWNote"></p>
<div id="pW" class="wgrid"></div>
</section>

<section><h2 data-i18n="pg_save">Apply</h2>
<div class="row">
<button onclick="perPreview()" data-i18n="b_ptry">Try it now</button>
<button class="b-acc" onclick="perSave()" data-i18n="b_psave">Save</button>
<button onclick="perDup()" data-i18n="b_pdup">Duplicate</button>
<button class="ko" id="pDel" onclick="perDel()" data-i18n="b_pdel">Delete</button>
</div>
<p class="hint" id="pMsg"></p>
</section>
</section>
<section class="tab" id="t-files">
<details open><summary><svg class="ci" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M4 5h5l2 2h9v11a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V6a1 1 0 0 1 1-1z"/></svg><span data-i18n="h_files">Files · SD card</span></summary><div>
<p class="desc" data-i18n-html="files_desc" style="margin:0 0 10px">Choreographies, apps, config and
rules — gathered here. Click a <b style="color:var(--acc)">name</b> to
download it; ▶ plays a dance, 🚀 launches an app, ↻ reloads, ✕ deletes.
⚠ <code>config.yaml</code> may hold the WiFi password.</p>
<div class="row" style="margin-bottom:8px"><button data-i18n="sd_refresh" onclick="loadSdList(this)">↻ Refresh the list</button></div>
<div id="sdlist" class="fm"><span class="desc">…</span></div>
<div class="row" style="align-items:center;margin-top:14px;padding-top:12px;border-top:1px solid var(--bord);gap:8px;flex-wrap:wrap">
<span class="desc" data-i18n="l_import">Import:</span>
<select id="impCat" onchange="impHint()">
<option value="config">config.yaml</option><option value="rules">rules.txt</option>
<option value="dance" data-i18n="opt_dance">choreography (.csv)</option><option value="bin" data-i18n="opt_bin">binary (.bin)</option>
<option value="guest" data-i18n="opt_guest">guest config (.yaml)</option>
<option value="companion" data-i18n="opt_comp">companion.bin (restore)</option></select>
<input type="text" id="impName" data-i18n-ph="fname_ph" placeholder="file name" style="width:140px;display:none">
<input type="file" id="impFile">
<button data-i18n="import" onclick="importSd(this)">⬆ Import</button></div>
<p class="desc" data-i18n="imp_desc" style="margin:6px 0 0">config/rules → reloaded automatically;
dances/bins → available at once.</p>
</div></details>
</section>

<section class="tab" id="t-sys">
<details open><summary><svg class="ci" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="4" y="4" width="16" height="16" rx="2"/><rect x="9" y="9" width="6" height="6"/><path d="M9 2v2M15 2v2M9 20v2M15 20v2M2 9h2M2 15h2M20 9h2M20 15h2"/></svg><span data-i18n="h_sys">System</span> <span id="authState" class="sst"></span></summary><div>
<div class="sysgrp"><div class="sysh"><span data-i18n="sys_clock">Clock · NTP</span> <span id="clkState" class="sst"></span></div>
<div class="row" style="align-items:center">
<button data-i18n="clk_sync" onclick="syncClock(this)">↻ Sync now</button>
<span class="desc" id="clkNow">—</span></div>
<p class="desc" data-i18n="clk_desc" style="margin:6px 0 0">Restarts the NTP client and rewrites the RTC chip from the first packet that lands. Useful when the network arrived after boot, or when the time drifted. Unavailable in AP mode: the robot is the network there, so there is no route to a time server.</p></div>

<div class="sysgrp"><div class="sysh" data-i18n="sys_cors">Cross-origin access · CORS</div>
<div class="opt"><span data-i18n="o_cors">Allow other pages to read the API</span><label class="sw">
<input type="checkbox" id="o_cors" onchange="T('cors',this.checked?1:0)"><span class="sl"></span></label></div>
<p class="desc" data-i18n-html="cors_desc" style="margin:6px 0 0">Needed by the choreography editor, which runs from a local file and is therefore a different origin. <b>Applied on restart.</b> While it is on, any page your browser happens to show can talk to this robot on the local network — with Basic Auth off, that includes making it move. Leave it off unless you are capturing a pose.</p></div>

<!-- DEBUG TRACE. Beside CORS because both are "plumbing you flip while
     diagnosing", not preferences. Live (loop syncs it each tick), persisted
     with the rest of the tuning. -->
<div class="sysgrp"><div class="sysh" data-i18n="sys_dbg">Debug trace · serial</div>
<div class="opt"><span data-i18n="o_debug">Narrate every step on serial</span><label class="sw">
<input type="checkbox" id="o_debug" onchange="T('debug',this.checked?1:0)"><span class="sl"></span></label></div>
<p class="desc" data-i18n="dbg_desc" style="margin:6px 0 0">Serial 115200: network joins, config reads, HTTP attempts with status and duration, SD writes. Applied at once, no reboot. Off, it costs one boolean test per step. Guest bins have the same switch on their /config page.</p></div>

<div class="sysgrp"><div class="sysh" data-i18n="sys_wifi">WiFi network</div>
<div class="row" style="align-items:center">
<input type="text" id="ssid" placeholder="SSID"> <input type="text" id="pw" data-i18n-ph="pw_ph" placeholder="password">
<button data-i18n="save" onclick="P('/api/wifi?ssid='+encodeURIComponent(ssid.value)+'&pass='+encodeURIComponent(pw.value),this)">Save</button></div>
<p class="desc" data-i18n="wifi_desc" style="margin:6px 0 0">Applied on restart.</p></div>

<div class="sysgrp"><div class="sysh" data-i18n="sys_vor">VOR calibration · gyroscope</div><div class="row">
<span style="font-size:.85em;align-self:center;color:var(--mut)" data-i18n="l_yawax">yaw axis:</span>
<button onclick="T('gyro_yaw_axis',0,this)">X</button><button onclick="T('gyro_yaw_axis',1,this)">Y</button>
<button onclick="T('gyro_yaw_axis',2,this)">Z</button>
<button onclick="T('gyro_yaw_sign',1,this)">+</button><button onclick="T('gyro_yaw_sign',-1,this)">-</button>
<span style="font-size:.85em;align-self:center;color:var(--mut)" data-i18n="l_pitchax">· pitch axis:</span>
<button onclick="T('gyro_pitch_axis',0,this)">X</button><button onclick="T('gyro_pitch_axis',1,this)">Y</button>
<button onclick="T('gyro_pitch_axis',2,this)">Z</button>
<button onclick="T('gyro_pitch_sign',1,this)">+</button><button onclick="T('gyro_pitch_sign',-1,this)">-</button>
</div></div>

<div class="sysgrp"><div class="sysh" data-i18n="sys_auth">API security · Basic Auth</div>
<div class="row" style="align-items:center">
<input type="text" id="authUser" data-i18n-ph="user_ph" placeholder="username"> <input type="password" id="authPass" data-i18n-ph="apw_ph" placeholder="password (empty = disabled)">
<button data-i18n="save2" onclick="P('/api/security?username='+encodeURIComponent(authUser.value)+'&password='+encodeURIComponent(authPass.value),this)">Save</button></div>
<p class="desc" data-i18n="auth_desc" style="margin:6px 0 0">Protects the console + the whole API, applied immediately.
Empty password = open API.</p></div>

<div class="sysgrp"><div class="sysh" data-i18n="sys_fw">Firmware · OTA update</div>
<!-- WHAT IS RUNNING. A USB flash writes one OTA slot while `otadata` picks the
     slot, so a flash can succeed, verify its own hash, and change nothing -
     which is exactly what happened on 08-10 and took a flash dump to see.
     `console` is the digest `gen_console_gz.py --check` prints, so a working
     copy is one command away from confirming this is its own build. -->
<div class="fwid">
<span data-i18n="fw_slot">OTA slot</span><b id="fw_slot">—</b>
<span data-i18n="fw_sha">Build</span><b id="fw_sha">—</b>
<span data-i18n="fw_console">Console</span><b id="fw_console">—</b>
<span data-i18n="fw_reset">Last start</span><b id="fw_reset">—</b></div>
<p class="desc" data-i18n-html="fw_id_desc" style="margin:-2px 0 10px">Both fingerprints are reproducible from a working copy: <code>Build</code> is <code>sha256sum .pio/build/companion/firmware.elf</code> (first 8 hex), <code>Console</code> is what <code>python scripts/build/gen_console_gz.py --check</code> prints. Same values from <code>GET /api/firmware</code>.</p>
<div class="row" style="align-items:center">
<input type="file" id="fwbin" accept=".bin">
<button data-i18n="flash" onclick="upFw(this)">⚡ Flash</button></div>
<progress id="fwprog" max="100" value="0" style="width:100%;display:none;margin-top:8px"></progress>
<p class="desc" data-i18n-html="fw_desc" style="margin:6px 0 0"><code>firmware.bin</code> (companion env).
Screen frozen during the flash, auto reboot — <b>do not cut the power</b>.</p></div>

<!-- LANGUAGE. It decides whether the user can read this page at all, and it
     was the one setting the page could not change: `lang:` on the card, then
     a reboot. Applied at once and written to the card, so it survives. -->
<div class="sysgrp"><div class="sysh" data-i18n="sys_lang">Language</div>
<div class="seg" id="o_lang">
<button data-l="en" onclick="setLang('en')">English</button>
<button data-l="fr" onclick="setLang('fr')">Français</button></div>
<p class="desc" data-i18n="lang_desc" style="margin:6px 0 0">Applies to the console, the API messages, the launcher and the guest bins. Written to <code>config.yaml</code>.</p></div>

<div class="sysgrp" style="display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px">
<button data-i18n="cfg_rl" onclick="P('/api/config/reload',this)">↻ Reload config.yaml</button>
<div class="row">
<button data-i18n="reboot" onclick="if(confirm(t('c_reboot')))P('/api/reboot',this)">⟳ Restart</button>
<button data-i18n="poweroff" onclick="if(confirm(t('c_poweroff')))P('/api/poweroff',this)">⏻ Power off</button></div></div>
</div></details>
</section>
<p style="text-align:center;margin-top:4px"><a href="/swagger">Swagger</a> · <a href="/api/openapi.json">OpenAPI</a></p>
<!-- WHICH BUILD IS RUNNING, at the bottom of every tab. The detailed block
     lives in System > Firmware, next to the button that replaces it; this
     line is the glance — it costs one row of page chrome and answers "am I
     looking at my own code?" without changing tab. Filled by fwRead(). -->
<p class="fwfoot" id="fwFoot"></p>
</main>
<script>
const EMOS=["Normal","Angry","Glee","Happy","Sad","Worried","Focused","Annoyed",
"Surprised","Skeptic","Frustrated","Unimpressed","Sleepy","Suspicious","Nervous",
"Furious","Scared","Awe","Excited","Questioning","Frozen","Scary","Curious",
"Doubt","Contempt","Disgust","Smug","Dead","Blush","Squint"];
// Tuning: [key, min, max, step, compiled-in default, description, category KEY]
// The category is a KEY, not a label: it is compared in code (the camera rows
// are rendered apart) and translated for display through `cat_<key>`.
const TUN=[
["eye_color_dim",0,1,.05,0.80,"eye brightness (1 = full, undimmed colours)","eyes"],
["eye_spacing",0,44,1,14,"edge-to-edge gap between the eyes (px): 0 = they touch, 14 = historic position","eyes"],
["eye_depth_scale",0,1,.05,0,"depth effect: the eye on the gaze side grows (0 = pure offset)","eyes"],
["blink_lag_ms",0,150,10,30,"lag of the right eye behind the left (0 = in sync)","eyes"],
["vor_gain",0,1.2,.05,0.90,"strength of the eye counter-rotation (stabilisation)","vor"],
["vor_drift_alpha",0,.1,.005,0.02,"speed of the realignment on the tilt (at rest)","vor"],
["vor_mag_alpha",0,.2,.01,0,"magnetometer yaw correction (0 = off; validate the heading sign first — CONFIG)","vor"],
["saccade_recentre",.3,1,.05,0.75,"saturation threshold triggering the catch-up saccade","vor"],
["shake_gyro_thr",5,60,1,35,"sustained °/s triggering Scared (shake)","vor"],
["saccade_ms",40,150,5,80,"duration of one gaze saccade","vor"],
["pickup_dev_g",.03,.3,.01,0.08,"sensitivity of the pick-up detection (g)","vor"],
["pickup_hold_ms",60,500,20,120,"sustained deviation before 'lifted'","vor"],
["fixation_min_ms",300,2000,100,800,"min duration of an idle fixation","idle"],
["fixation_max_ms",1000,8000,250,4000,"max duration of an idle fixation","idle"],
["blink_median_ms",1000,8000,250,3500,"median rate of the autoblink","idle"],
["headfollow_hold_ms",300,3000,100,1000,"off-centre fixation held before the head follows","head"],
["head_home_ms",0,30000,500,8000,"return home: pause (ms) without head activity before a systematic return (0 = never)","head"],
["servo_idle_release_ms",0,60000,1000,4000,"torque released after X ms without movement (0 = never)","head"],
["leds_brightness",0,255,5,38,"brightness of the emotion LEDs","leds"],
["led_depth",0,200,5,0,"depth channel: while the head turns, the FAR end of each bar darkens (the bars run perpendicular to the screen). 0 = off, and the bars then receive exactly what they received before the feature existed","leds"],
["sound_volume",0,255,5,96,"chirp volume","leds"],
["sound_volume_night",0,255,5,32,"chirp volume from real sunset to real sunrise, computed from lat/lon (needs NTP - loud until the clock syncs)","leds"],
["lat",-90,90,0.01,-20.89,"latitude of the robot (+ = north) - drives the night volume via the solar position","leds"],
["lon",-180,180,0.01,55.53,"longitude of the robot (+ = east) - drives the night volume via the solar position","leds"],
["soundtrack_thr",10,8000,10,100,"sensitivity: RMS trigger threshold (low = sensitive - see evts in the status panel)","snd"],
["soundtrack_sign",-1,1,2,-1,"left/right direction. -1 is the HARDWARE-VERIFIED value (schema v4, 2026-07-30): the earlier +1 turned the head AWAY from the sound","snd"],
["soundtrack_step_deg",4,40,1,30,"distance: max rotation per step (°) - real step ∝ √(L/R imbalance)","snd"],
["soundtrack_move_ms",100,2000,50,400,"base speed (ms/step) - modulated by the sound intensity (loud = brisk)","snd"],
["soundtrack_shock_thr",0,16000,250,4000,"startle: RMS level triggering the shocked dance then the turn (0 = disabled)","snd"],
["pomo_work_min",1,120,1,25,"pomodoro: work block (min)","pomo"],
["pomo_break_min",1,60,1,5,"pomodoro: break block (min)","pomo"],
["pomo_hydra_min",0,15,1,1,"pomodoro: drink prompt between a work block and its break (0 = off)","pomo"],
["pomo_cycles",1,8,1,4,"pomodoro: work blocks before the end","pomo"],
["timer_dance",0,15,1,0,"dance played when a countdown ends - Ring for the timer, the last block for the pomodoro (0 = none)","pomo"],
["crt_glow_px",0,8,1,3,"dilation of the CRT halo (px)","crt"],
["crt_glow_dim",0,1,.02,0.28,"intensity of the CRT halo","crt"],
["cam_fps",1,15,1,10,"ceiling of frames per second (capture + stream)","cam"],
["cam_quality",1,63,1,12,"JPEG quality of the snapshot ↗ (low = better, heavier)","cam"],
["cam_stream_quality",1,63,1,45,"JPEG quality of the stream/live view (high = more compressed = more responsive)","cam"],
["cam_stream_qvga",0,1,1,1,"stream/view in 320×240: 4× lighter and faster (0 = VGA, e.g. Frigate)","cam"],
["cam_brightness",-2,2,1,1,"exposure (AEC target — raises/lowers the overall light)","cam"],
["cam_contrast",-2,2,1,0,"sensor contrast (0 = official calibration)","cam"],
["cam_saturation",-2,2,1,1,"colour saturation","cam"],
["cam_vflip",0,1,1,0,"vertical mirror of the image (0/1)","cam"],
["cam_hmirror",0,1,1,0,"horizontal mirror of the image (0/1)","cam"],
["cam_lowlight",0,1,1,0,"low-light mode: AEC gain/exposure uncapped (brighter but noisier)","cam"],
["cam_colorbar",0,1,1,0,"sensor test pattern (diagnostic: visible bars = capture chain OK)","cam"]];
// ---- i18n ------------------------------------------------------------------
// ENGLISH is in the markup above; FRENCH is applied on top. The English side of
// the table therefore only carries what JavaScript BUILDS (plus the tuning
// descriptions, which live in TUN): `snapEn()` copies the rest out of the DOM
// before the first translation. Storing it twice would have cost ~4 KB of
// flash for strings the page already serves.
// Markup attributes: data-i18n (textContent) · data-i18n-html (innerHTML) ·
// data-i18n-title (title, and aria-label when present) · data-i18n-ph
// (placeholder). Anything built in JS goes through t(key,vars) with {name}
// placeholders — never concatenation, or half a sentence stays untranslatable.
const I18N={en:{
 tb_perso:"Characters",pg_who:"Who the robot is",pg_edit:"Edit a character",
 pg_w:"Resting expressions",pg_save:"Apply",o_pedit:"Editing",l_pname:"Name",
 l_pcolor:"Eye colour",o_pnocol:"Use the emotion palette",
 l_ptheme:"Console skin",l_prules:"Rules file",
 l_pmin:"Mood every, from",l_pmax:"\u2026to",
 l_ptscale:"Animation pace",l_ppscale:"Head-bias strength",
 b_ptry:"Try it now",b_psave:"Save",b_pdup:"Duplicate",b_pdel:"Delete",
 p_roulhint:"With random moods off, only rules and reflexes change the face \u2014 blinking, glancing and breathing carry on.",
 p_scalehint:"1.0 is unscaled. Lower pace is snappier, higher is dreamier; zero head-bias holds the head level no matter what it feels.",
 p_colhint:"One colour makes a character recognisable at a glance; the emotion palette tells you which feeling is on the face. You cannot have both.",
 p_whint:"What the robot drifts to when nothing is happening. Leave an emotion at zero and it is never drawn on its own \u2014 which is what gives the rules their weight: a face it never wears by chance means something when a rule puts it there.",
 m_pname:"A name is required.",
 m_psaved:"Saved to the card.",
 m_ptry:"Applied \u2014 and saved: there is no try without writing.",
 m_pdup:"Name it, then Save. Nothing is written until you do.",
 m_pdelq:"Delete the character \u201c%s\u201d? Its rules file stays on the card.",
 m_pdeleted:"Deleted.",
 m_pdelno:"This one cannot be deleted.",
 m_pfail:"The robot did not answer.",
 m_pwnone:"This character declares no weights: it leaves the firmware's own resting table alone. Move any slider and it takes over the whole set.",
 t_nodance:"— none —",
 o_mic:"Microphone",o_ledsw:"Swap LED order",o_leddf:"LED index 0 at the front",sys_lang:"Language",
 o_hfollow:"Sync with the eyes",o_servos:"Servos",
 fw_start:"start",
 lang_desc:"Applies to the console, the API messages, the launcher and the guest bins. Written to config.yaml.",
 cat_eyes:"Eyes",cat_vor:"VOR / gaze",cat_idle:"Idle / blink",cat_head:"Head / servo",
 cat_leds:"LEDs / sound",cat_snd:"Turning to noise",cat_crt:"CRT",cat_cam:"Camera",
 cat_pomo:"Pomodoro",
 c_reboot:"Reboot StackChan?",c_poweroff:"Power off StackChan?",
 cam_view:"▶ View camera",cam_stop:"⏹ Stop the view",
 cam_start:"camera starting up…",cam_noimg:"no image (see the state above)",
 snap_title:"Snapshot…",snap_wait:"Full-quality capture in progress…",
 snap_fail:"snapshot failed (try again)",
 ota_c:"Flash {f} ({k} kB) and reboot?",ota_ko:"OTA failed: ",
 t_dl:"Download",t_del:"Delete",t_rl:"Reload",t_play:"Play",t_launch:"Launch (reboot)",
 g_cfg:"Configurations",g_dan:"Choreographies",g_bin:"Applications",
 nofile:"no file",c_launch:"Launch {n}? (flash + reboot)",c_del:"Delete {n}?",
 del_ko:"Delete failed ({s})",del_ko0:"Delete failed",
 g_emo:"emotion",ti_emo:"Emotion currently displayed",
 g_net:"network",ti_net:"Network mode (STA/AP), IP address and WiFi signal",
 g_sd:"SD card",v_nosd:"missing",ti_sd:"Presence of the micro-SD card",
 g_bat:"battery",ti_bat:"Battery level (⚡ = charging) — PMIC AXP2101",
 g_volt:"voltage",ti_volt:"Measured battery voltage (INA226 gauge)",
 g_light:"light",ti_light:"Ambient brightness (LTR-553 sensor)",
 g_hdg:"heading",ti_hdg:"Magnetic heading 0-360° (BMM150 compass)",
 g_up:"online",ti_up:"Time elapsed since boot (uptime) - the two largest units only",
 g_clock:"clock",ti_clock:"NTP-synced wall clock, UTC. The robot keeps no timezone: the only consumer is the solar night, and the sun does not observe daylight saving. n/a = never synced",
 g_night:"night",ti_night:"What SoundFx decides from the clock and the lat/lon tuning keys: below the horizon the chirps use sound_volume_night instead of sound_volume",
 v_night:"night",v_day:"day",v_nontp:"n/a",
 rules_n:"{n} rules · {a} active",rules_none:"no rule loaded",
 rules_ko:"list unavailable",rc_src:"source",rc_gate:"gated by",
 rc_cond:"condition",rc_hold:"held",rc_cool:"cooldown",rc_act:"action",
 rc_builtin:"built-in",rc_desc:"description",
 clk_ap:"AP mode",clk_wait:"waiting for a packet",clk_never:"never synced",
 clk_ok:"✓ synced",
 clk_rtc:"from the RTC, unverified",
 g_fmax:"max render",ti_fmax:"Longest render time of a frame (30 Hz target)",
 g_mic:"mics",ti_mic:"ES7210 stereo mics: left/right levels, ambience, detected events",
 v_miclv:"L {l} · R {r} · amb {a} · evts {e}",v_warm:"warming up… {s}s",
 v_standby:"paused (speaker)",v_micoff:"off",
 g_state:"state",v_offline:"offline",
 lg_mem:"mem {v}kB",ti_mem:"Free memory (internal heap) — the higher the better",
 lg_fr:"render {v}ms",ti_fr:"Average render time of a frame (30 Hz target ≈ 33 ms)",
 ti_c0:"CPU load on core 0: network (AsyncTCP), servos, camera",
 ti_c1:"CPU load on core 1: Brain 100 Hz + Renderer 30 Hz",
 // No data-i18n element carries these (the mic status span starts empty,
 // "…", and is filled only through t() from JS) - the snapshot mechanism
 // that spares every OTHER English string here never sees them, so they
 // have to be spelled out like the tuning descriptions above. Missing
 // until 2026-09-22: the span showed the literal key ("snd_mact") in
 // English mode instead of a sentence, found reviewing EN/FR drift.
 snd_moff:"Mic off — the band has nothing to draw.",
 snd_mwarm:"Mic warming up… ({s} s)",
 snd_mstby:"Mic on standby: the speaker holds the audio bus.",
 snd_mact:"Mic listening."
},fr:{
 tb_perso:"Caract\u00e8res",pg_who:"Qui est le robot",pg_edit:"Modifier un caract\u00e8re",
 pg_w:"Expressions de repos",pg_save:"Appliquer",o_pedit:"Modification de",l_pname:"Nom",
 l_pcolor:"Couleur des yeux",o_pnocol:"Utiliser la palette des \u00e9motions",
 l_ptheme:"Peau de la console",l_prules:"Fichier de r\u00e8gles",
 l_pmin:"Humeur toutes les, de",l_pmax:"\u2026\u00e0",
 l_ptscale:"Rythme d'animation",l_ppscale:"Force du biais de t\u00eate",
 b_ptry:"Essayer",b_psave:"Enregistrer",b_pdup:"Dupliquer",b_pdel:"Supprimer",
 p_roulhint:"Humeurs al\u00e9atoires \u00e9teintes, seuls les r\u00e8gles et les r\u00e9flexes changent le visage \u2014 clignements, saccades et respiration continuent.",
 p_scalehint:"1.0 est neutre. Plus bas rend chaque changement d'humeur plus vif, plus haut plus r\u00eaveur ; un biais de t\u00eate nul garde la t\u00eate droite quoi qu'il ressente.",
 p_colhint:"Une seule couleur rend un caract\u00e8re reconnaissable d'un coup d'\u0153il ; la palette des \u00e9motions dit laquelle est sur le visage. On ne peut pas avoir les deux.",
 p_whint:"Ce vers quoi le robot d\u00e9rive quand rien ne se passe. Laisser une \u00e9motion \u00e0 z\u00e9ro et elle n'est jamais tir\u00e9e d'elle-m\u00eame \u2014 c'est ce qui donne leur poids aux r\u00e8gles : un visage qu'il ne porte jamais par hasard signifie quelque chose quand une r\u00e8gle l'y met.",
 m_pname:"Un nom est requis.",
 m_psaved:"Enregistr\u00e9 sur la carte.",
 m_ptry:"Appliqu\u00e9 \u2014 et enregistr\u00e9 : essayer suppose d'\u00e9crire.",
 m_pdup:"Nommez-le, puis Enregistrer. Rien n'est \u00e9crit avant.",
 m_pdelq:"Supprimer le caract\u00e8re \u00ab %s \u00bb ? Son fichier de r\u00e8gles reste sur la carte.",
 m_pdeleted:"Supprim\u00e9.",
 m_pdelno:"Celui-ci n'est pas supprimable.",
 m_pfail:"Le robot n'a pas r\u00e9pondu.",
 m_pwnone:"Ce caract\u00e8re ne d\u00e9clare aucun poids : il laisse la table de repos du firmware tranquille. Bouger un curseur reprend l'ensemble \u00e0 son compte.",
 sub:"console compagnon · K151",
 h_emo:"Émotions · priorité 10 s",h_dance:"Danses",stop_t:"Arrêter la danse",
 blink:"Clignement",winkL:"Clin G",winkR:"Clin D",h_head:"Tête",
 h_band:"Bande de statut",
 sb_none:"Défaut",sb_vu:"Son",sb_gauge:"Jauges",l_mstyle:"Style",ms_wave:"Ondes",ms_col:"Colonnes",ms_mtx:"Matrice",
 sb_timer:"Minuteur",sb_pomo:"Pomodoro",
 say_ph:"notification à afficher…",say:"Dire ↗",
 l_tsize:"Taille texte",l_speed:"Vitesse (px/s)",
 o_bdbg:"Infos debug (émotion·IP)",l_gauges:"Jauges test",push:"Pousser",
o_c24:"Horloge 24 h",l_dur:"Duree",l_quick:"Depart rapide",set:"Regler",t_tap:"Lancer / pause",t_rst:"Remise a zero",l_work:"Travail (min)",l_break:"Pause (min)",l_cycles:"Cycles",l_hydra:"Hydratation (min)",l_tdance:"Danse a la fin",t_nodance:"— aucune —",d_tdance:"Jouee quand un compte a rebours SE TERMINE : la sonnerie pour le minuteur, le dernier bloc pour le pomodoro. Pas aux changements de phase, qui reviennent trop souvent pour qu on se leve.",d_vu:"Trois habillages de la même forme d’onde déclenchée — courbe, traits, blocs — nourris par les deux micros. Rien n’est dessiné au repos. La sensibilité est un gain appliqué avant l’analyse : montez-la dans une pièce calme, baissez-la si la forme sature.",d_tmr:"Sur le robot : glissez sur le bandeau pour régler, tapez pour lancer ou mettre en pause, maintenez pour remettre à zéro.",d_pomo:"Sur le robot : TAPEZ n’importe où sur le bandeau pour lancer ou mettre en pause, tapez l’ICÔNE DE PHASE (à gauche des chiffres) pour sauter la phase en cours, MAINTENEZ pour remettre à zéro, et glissez haut/bas à l’arrêt pour faire défiler les quatre formes de session. Changer une durée réarme le bloc suivant ; cela ne coupe pas celui qui tourne.",
 o_bclk:"Horloge en mode Défaut",l_tz:"Décalage UTC (h)",
 rules_rl:"↻ Relire rules.txt",l_icons:"Icônes visibles",
 l_notif:"Notification",
 h_rules:"Règles · plugins réactifs",rules_rf:"↺ Actualiser ce tableau",
 rules_n:"{n} règles · {a} actives",rules_none:"aucune règle chargée",
 rules_ko:"liste indisponible",rc_src:"source",rc_gate:"activée par",
 rc_cond:"condition",rc_hold:"tenue",rc_cool:"répit",rc_act:"action",
 rc_builtin:"intégrée",rc_desc:"description",
 h_howto:"Comment en écrire une",h_vals:"Ce qu'on peut mettre dans chaque colonne",
 snd_micon:"Allumer le microphone",
 snd_moff:"Micro éteint — la bande n'a rien à dessiner.",
 snd_mwarm:"Micro en préchauffage… ({s} s)",
 snd_mstby:"Micro en attente : le haut-parleur tient le bus audio.",
 snd_mact:"Micro à l'écoute.",
 h_where:"Où la déposer",h_gotcha:"Trois choses à savoir",
 d_lead:"Les règles sont écrites dans <code>rules.txt</code> sur la carte SD, une par ligne. Le robot les suit <b>tout seul</b>. Une règle surveille un champ, et quand il franchit un seuil elle déclenche une action.",
 d_g0:"Une règle est <b>une ligne de neuf colonnes</b> séparées par <code>|</code>. En voici une vraie, tirée du fichier livré avec le robot :",
 d_gread:"<i>Tant que le pont Claude tourne, si la fenêtre de contexte atteint 90 % et y reste trois secondes, prendre l'air effrayé quatre secondes — et ne pas recommencer avant une minute.</i>",
 a_en:"Une <b>grille</b>. La règle dort tant que ce champ est sous 0,5 — ici, tant que rien ne publie <code>claude</code>. Laissez la colonne vide et la règle est toujours éveillée.",
 a_f:"Le champ surveillé.",
 a_op:"Comment il est comparé : « supérieur ou égal ».",
 a_v:"Le seuil auquel il est comparé.",
 a_sus:"La condition doit tenir ce temps, en millisecondes, avant que la règle parte. <code>0</code> déclenche aussitôt.",
 a_cd:"Le délai minimal avant que cette règle puisse repartir, en millisecondes.",
 a_act:"Ce qu'elle fait.",
 a_a1:"Le premier argument de l'action — ici, quelle émotion.",
 a_a2:"Le second — ici, pendant combien de millisecondes.",
 v_f:"— publiés par le robot lui-même :",
 vf_batt:"batterie, 0-100",vf_chg:"1 pendant la charge",
 vf_rssi:"force du WiFi, en dBm (négatif)",vf_light:"lumière ambiante, 0-100",
 vf_night:"1 entre le vrai coucher et le vrai lever du soleil",
 vf_dark:"1 quand l'option « sommeil dans le noir » est active",
 vf_cam:"1 quand la caméra diffuse",vf_mic:"1 quand le micro écoute",
 vf_miclr:"niveau par micro, 0-1",vf_ip:"l'adresse, en texte",
 v_fmine:"Tout le reste est à vous. <code>POST /api/field?nom=valeur</code> depuis un script, une tâche planifiée, Home Assistant — le robot ne demande pas qui l'a écrit. Un champ inconnu vaut <b>0</b>.",
 vo_gt:"strictement supérieur",vo_ge:"supérieur ou égal",
 vo_lt:"strictement inférieur",vo_le:"inférieur ou égal",
 vo_eq:"égal",vo_ne:"différent",
 v_a:"— avec ses arguments :",
 va_emo:"une des 30 expressions, éventuellement pour une durée",
 va_dance:"une des chorégraphies listées dans la carte Danses",
 va_wink:"sans argument",
 va_dark:"mode nuit : Sleepy devient l'inactivité dominante",
 va_set:"réécrit un champ, donc une règle peut en armer une autre",
 d_where:"Onglet Fichiers → <i>Importer</i> → <code>rules.txt</code>, puis <b>↻ Relire rules.txt</b> ci-dessus. Votre règle apparaît dans le tableau — ou pas, et c'est ainsi qu'on apprend que la ligne n'a pas été analysée. Le commentaire écrit juste au-dessus devient sa description là-bas.",
 d_n1:"<b>Une règle se déclenche une fois.</b> Elle attend que la condition redevienne fausse pour pouvoir repartir : un champ garé au-dessus de son seuil ne se répète pas.",
 d_n2:"<b>Un champ inconnu vaut 0.</b> Donc <code>ctx lt 20</code> est VRAIE quand rien ne publie <code>ctx</code> — c'est à cela que sert <code>enable</code>.",
 d_n3:"<b>Rien de ce que vous écrivez ne peut casser le robot.</b> Une règle ne poste que les actions listées plus haut, et les réflexes restent au-dessus d'elle. Une ligne de plus de 127 octets est jetée en entier, en silence — c'est la seule panne qui soit muette.",
 d_say:"S'applique à toute notification, alerte ou « say », quoi que la bande montre en dessous. Une alerte est dessinée en taille 2 minimum quoi qu'en dise ce réglage : elle est faite pour être lue.",
 ic_batt:"batterie",ic_night:"nuit",ic_mic:"micro",ic_cam:"caméra",
 o_crt:"Effet CRT",o_leds:"LEDs émotion",o_tel:"Télémétrie série",
 o_dbg2:"Trace debug (série)",
 o_mic:"Microphone",o_ledsw:"Inverser l'ordre des LEDs",o_leddf:"LED d'index 0 a l'avant",sys_lang:"Langue",
 lang_desc:"S'applique à la console, aux messages de l'API, au launcher et aux bins invités. Écrit dans config.yaml.",
 o_hfollow:"Synchronisation avec les yeux",o_servos:"Servos",
 o_track:"Suivi du son",o_ab:"Luminosité adaptative",o_dark:"Sommeil dans le noir",
 o_perso:"Personnalité",o_roul:"Humeurs aléatoires",
 h_cam:"Caméra",o_cam:"Activer la caméra",
 cam_desc:"GC0308 — init à la demande, éteinte au repos, VOR actif pendant le flux. Réglages appliqués à chaud (~1 s).",
 cam_stopped:"Vue arrêtée",cam_hint:"Cliquer « ▶ Voir la caméra »",
 cam_snap:"Instantané ↗",cam_mjpeg:"Flux MJPEG ↗",
 h_files:"Fichiers · carte SD",
 files_desc:'Chorégraphies, applications, config et règles — réunis ici. Cliquez un <b style="color:var(--acc)">nom</b> pour le télécharger ; ▶ joue une danse, 🚀 lance une app, ↻ recharge, ✕ supprime. ⚠ <code>config.yaml</code> peut contenir le mot de passe WiFi.',
 sd_refresh:"↻ Rafraîchir la liste",l_import:"Importer :",
 opt_dance:"chorégraphie (.csv)",opt_bin:"binaire (.bin)",
 opt_guest:"config invité (.yaml)",opt_comp:"companion.bin (restauration)",
 fname_ph:"nom de fichier",import:"⬆ Importer",
 imp_desc:"config/règles → rechargés automatiquement ; danses/bins → disponibles aussitôt.",
 h_tun:"Réglages fins",h_sys:"Système",
 sys_clock:"Horloge · NTP",clk_sync:"↻ Synchroniser",
 clk_desc:"Redémarre le client NTP et réécrit la puce RTC depuis le premier paquet reçu. Utile quand le réseau est arrivé après le démarrage, ou quand l'heure a dérivé. Indisponible en mode AP : le robot y EST le réseau, donc aucune route vers un serveur de temps.",
 sys_cors:"Accès cross-origin · CORS",
 sys_dbg:"Trace debug · série",
 o_debug:"Raconter chaque étape sur le port série",
 dbg_desc:"Série 115200 : connexions réseau, lectures de config, tentatives HTTP avec code et durée, écritures SD. Appliqué immédiatement, sans redémarrage. Éteint, le coût est un test booléen par étape. Les bins invités ont le même interrupteur sur leur page /config.",
 o_cors:"Autoriser d'autres pages à lire l'API",
 cors_desc:"Nécessaire à l'éditeur de chorégraphies, qui tourne depuis un fichier local et constitue donc une autre origine. <b>Appliqué au redémarrage.</b> Tant que c'est actif, n'importe quelle page affichée par votre navigateur peut parler à ce robot sur le réseau local — Basic Auth désactivé, cela inclut le faire bouger. Laissez-le éteint hors capture de pose.",
 sys_wifi:"Réseau WiFi",pw_ph:"mot de passe",save:"Enregistrer",
 wifi_desc:"Appliqué au redémarrage.",
 sys_vor:"Calibration VOR · gyroscope",l_yawax:"axe yaw :",l_pitchax:"· axe pitch :",
 sys_auth:"Sécurité API · Basic Auth",user_ph:"utilisateur",
 apw_ph:"mot de passe (vide = désactivée)",save2:"Enregistrer",
 auth_desc:"Protège la console + toute l'API, appliqué immédiatement. Mot de passe vide = API ouverte.",
 sys_fw:"Firmware · mise à jour OTA",flash:"⚡ Flasher",
 fw_desc:"<code>firmware.bin</code> (env companion). Écran figé pendant le flash, reboot auto — <b>ne pas couper l'alimentation</b>.",
 fw_slot:"Slot OTA",fw_sha:"Build",fw_start:"démarrage",
 fw_console:"Console",fw_reset:"Dernier démarrage",
 fw_id_desc:"Les deux empreintes sont reproductibles depuis une copie de travail : <code>Build</code> vaut <code>sha256sum .pio/build/companion/firmware.elf</code> (8 premiers hex), <code>Console</code> vaut ce qu'imprime <code>python scripts/build/gen_console_gz.py --check</code>. Mêmes valeurs par <code>GET /api/firmware</code>.",
 cfg_rl:"↻ Relire config.yaml",reboot:"⟳ Redémarrer",poweroff:"⏻ Éteindre",
 cat_eyes:"Yeux",cat_vor:"VOR / regard",cat_idle:"Idle / blink",cat_head:"Tête / servo",
 cat_leds:"LEDs / son",cat_snd:"Rotation vers le bruit",cat_crt:"CRT",cat_cam:"Caméra",
 cat_pomo:"Pomodoro",
 c_reboot:"Rebooter StackChan ?",c_poweroff:"Éteindre StackChan ?",
 cam_view:"▶ Voir la caméra",cam_stop:"⏹ Arrêter la vue",
 cam_start:"démarrage de la caméra…",cam_noimg:"pas d'image (voir état ci-dessus)",
 snap_title:"Instantané…",snap_wait:"Capture pleine qualité en cours…",
 snap_fail:"échec instantané (réessayer)",
 ota_c:"Flasher {f} ({k} Ko) et rebooter ?",ota_ko:"Échec OTA : ",
 t_dl:"Télécharger",t_del:"Supprimer",t_rl:"Recharger",t_play:"Jouer",
 t_launch:"Lancer (reboot)",
 g_cfg:"Configurations",g_dan:"Chorégraphies",g_bin:"Applications",
 nofile:"aucun fichier",c_launch:"Lancer {n} ? (flash + reboot)",c_del:"Supprimer {n} ?",
 del_ko:"Échec suppression ({s})",del_ko0:"Échec suppression",
 g_emo:"émotion",ti_emo:"Émotion actuellement affichée",
 g_net:"réseau",ti_net:"Mode réseau (STA/AP), adresse IP et signal WiFi",
 g_sd:"carte SD",v_nosd:"absente",ti_sd:"Présence de la carte micro-SD",
 g_bat:"batterie",ti_bat:"Niveau de batterie (⚡ = en charge) — PMIC AXP2101",
 g_volt:"tension",ti_volt:"Tension batterie mesurée (jauge INA226)",
 g_light:"lumière",ti_light:"Luminosité ambiante (capteur LTR-553)",
 g_hdg:"cap",ti_hdg:"Cap magnétique 0-360° (boussole BMM150)",
 g_up:"en ligne",ti_up:"Temps écoulé depuis le démarrage (uptime) — les deux plus grandes unités seulement",
 g_clock:"horloge",ti_clock:"Horloge synchronisée NTP, en UTC. Le robot ne garde pas de fuseau : le seul consommateur est la nuit solaire, et le soleil n'observe pas l'heure d'été. n/a = jamais synchronisée",
 g_night:"nuit",ti_night:"Ce que SoundFx décide de l'horloge et des réglages lat/lon : sous l'horizon, les chirps utilisent sound_volume_night au lieu de sound_volume",
 v_night:"nuit",v_day:"jour",v_nontp:"n/a",
 tb_pilot:"Pilotage",tb_opt:"Options",tb_files:"Fichiers",
 tb_sys:"Système",h_tele:"Télémétrie en direct",
 og_screen:"Écran",og_leds:"LEDs",og_sound:"Son",
 clk_ap:"mode AP",clk_wait:"en attente d'un paquet",clk_never:"jamais synchronisée",
 clk_ok:"✓ synchronisée",
 clk_rtc:"depuis la RTC, non vérifiée",
 g_fmax:"rendu max",ti_fmax:"Durée de rendu maximale d'une frame (30 Hz cible)",
 g_mic:"micros",ti_mic:"Micros stéréo ES7210 : niveaux gauche/droite, ambiance, événements détectés",
 v_miclv:"L {l} · R {r} · amb {a} · évts {e}",v_warm:"chauffe… {s}s",
 v_standby:"pause (haut-parleur)",v_micoff:"éteints",
 g_state:"état",v_offline:"hors ligne",
 lg_mem:"mém {v}Ko",ti_mem:"Mémoire libre (heap interne) — plus c'est haut, mieux c'est",
 lg_fr:"rendu {v}ms",ti_fr:"Durée moyenne de rendu d'une frame (cible 30 Hz ≈ 33 ms)",
 ti_c0:"Charge CPU cœur 0 : réseau (AsyncTCP), servos, caméra",
 ti_c1:"Charge CPU cœur 1 : Brain 100 Hz + Renderer 30 Hz",
 d_eye_color_dim:"luminosité des yeux (1 = couleurs pleines, non assombries) — n’affecte QUE les yeux, pas le bandeau ni les bins invités",
 l_sbr:"Luminosité écran",l_sgain:"Sensibilité",
 d_eye_spacing:"écart bord-à-bord des yeux (px) : 0 = ils se touchent, 14 = position historique",
 d_eye_depth_scale:"effet profondeur : l'œil côté regard grossit (0 = décalage pur)",
 d_blink_lag_ms:"retard de l'œil droit sur le gauche (0 = synchrone)",
 d_vor_gain:"force de la contre-rotation des yeux (stabilisation)",
 d_vor_drift_alpha:"vitesse de recalage sur l'inclinaison (au calme)",
 d_vor_mag_alpha:"correction de lacet par le magnétomètre (0 = coupée ; valider le signe du cap d'abord — CONFIG)",
 d_saccade_recentre:"seuil de saturation déclenchant la saccade de rattrapage",
 d_shake_gyro_thr:"°/s soutenus déclenchant Scared (secousse)",
 d_saccade_ms:"durée d'une saccade du regard",
 d_pickup_dev_g:"sensibilité de détection du soulèvement (g)",
 d_pickup_hold_ms:"durée d'écart soutenu avant « soulevé »",
 d_fixation_min_ms:"durée min d'une fixation idle",
 d_fixation_max_ms:"durée max d'une fixation idle",
 d_blink_median_ms:"fréquence médiane de l'autoblink",
 d_headfollow_hold_ms:"fixation excentrée tenue avant que la tête suive",
 d_head_home_ms:"retour au home : pause (ms) sans activité de tête avant retour systématique (0 = jamais)",
 d_servo_idle_release_ms:"couple relâché après X ms sans mouvement (0 = jamais)",
 d_leds_brightness:"luminosité des LEDs d'émotion",
 d_led_depth:"canal de profondeur : quand la tete tourne, l'extremite ARRIERE de chaque barre s'assombrit (les barres sont perpendiculaires a l'ecran). 0 = desactive, et les barres recoivent alors exactement ce qu'elles recevaient avant la fonctionnalite",
 d_sound_volume:"volume des chirps",
 d_sound_volume_night:"volume des chirps du coucher au lever réels du soleil, calculés depuis lat/lon (nécessite NTP — fort tant que l'horloge n'est pas synchronisée)",
 d_lat:"latitude du robot (+ = nord) — pilote le volume de nuit via la position solaire",
 d_lon:"longitude du robot (+ = est) — pilote le volume de nuit via la position solaire",
 d_soundtrack_thr:"sensibilité : seuil RMS de déclenchement (bas = sensible — voir évts au panneau d'états)",
 d_soundtrack_sign:"sens gauche/droite. -1 est la valeur VÉRIFIÉE SUR MATÉRIEL (schéma v4, 2026-07-30) : le +1 antérieur tournait la tête À L'OPPOSÉ du son",
 d_soundtrack_step_deg:"distance : rotation max par pas (°) — pas réel ∝ √(déséquilibre G/D)",
 d_soundtrack_move_ms:"vitesse de base (ms/pas) — modulée par l'intensité du son (fort = vif)",
 d_soundtrack_shock_thr:"sursaut : niveau RMS déclenchant la danse shocked puis le virage (0 = désactivé)",
 d_pomo_work_min:"pomodoro : bloc de travail (min)",
 d_pomo_break_min:"pomodoro : bloc de pause (min)",
 d_pomo_cycles:"pomodoro : blocs de travail avant la fin",
 d_crt_glow_px:"dilatation du halo CRT (px)",
 d_crt_glow_dim:"intensité du halo CRT",
 d_cam_fps:"plafond d'images par seconde (capture + flux)",
 d_cam_quality:"qualité JPEG de l'instantané ↗ (bas = meilleure, plus lourde)",
 d_cam_stream_quality:"qualité JPEG du flux/vue live (haut = + compressé = + réactif)",
 d_cam_stream_qvga:"flux/vue en 320×240 : 4× plus léger et rapide (0 = VGA, ex. Frigate)",
 d_cam_brightness:"exposition (cible AEC — monte/baisse la lumière globale)",
 d_cam_contrast:"contraste du capteur (0 = calibration officielle)",
 d_cam_saturation:"saturation des couleurs",
 d_cam_vflip:"miroir vertical de l'image (0/1)",
 d_cam_hmirror:"miroir horizontal de l'image (0/1)",
 d_cam_lowlight:"mode basse lumière : gain/exposition AEC déplafonnés (plus clair mais plus de bruit)",
 d_cam_colorbar:"mire de test du capteur (diagnostic : barres visibles = chaîne de capture OK)"
}};
// The English of the tuning descriptions lives in TUN (single copy).
TUN.forEach(r=>I18N.en["d_"+r[0]]=r[5]);
window.lang="en";
// Unknown key -> the key itself, never an empty string: a visible anomaly gets
// fixed, a blank label goes unnoticed.
function t(k,v){const T=I18N[window.lang]||I18N.en;
 let s=T[k];if(s===undefined)s=I18N.en[k];if(s===undefined)return k;
 if(v)for(const n in v)s=s.split("{"+n+"}").join(v[n]);
 return s}
// attribute · read (English snapshot) · write (translation)
const I18A=[["data-i18n",e=>e.textContent,(e,s)=>{e.textContent=s}],
["data-i18n-html",e=>e.innerHTML,(e,s)=>{e.innerHTML=s}],
["data-i18n-title",e=>e.title,(e,s)=>{e.title=s;
 if(e.hasAttribute("aria-label"))e.setAttribute("aria-label",s)}],
["data-i18n-ph",e=>e.placeholder,(e,s)=>{e.placeholder=s}]];
let snapped=false;
// Reports a key present on one side only. Without it a gap shows an English
// sentence in the middle of a French interface, with nothing to explain it.
window.missingKeys=function(){const o=[];
 for(const k in I18N.en)if(!(k in I18N.fr))o.push('fr misses "'+k+'"');
 for(const k in I18N.fr)if(!(k in I18N.en))o.push('en misses "'+k+'"');
 return o};
// Called once at the end of this script. The language comes from the firmware
// (only it can read `lang:` off the SD card): `window.SCE_LANG` if something
// set it, else the `sce_lang` cookie sent with THIS page. The cookie is stored
// before the parser reaches this script, so the very first pass is already in
// the right language - no second request, no flash of English. No cookie (or
// cookies refused) simply leaves the page English.
window.applyLang=function(){
 let l=window.SCE_LANG;
 if(!l){const m=/(?:^|;\s*)sce_lang=([A-Za-z-]+)/.exec(document.cookie);
  if(m)l=m[1]}
 window.lang=(l&&I18N[l])?l:"en";
 if(!snapped){snapped=true;
  for(const a of I18A)document.querySelectorAll("["+a[0]+"]").forEach(e=>{
   const k=e.getAttribute(a[0]);if(!(k in I18N.en))I18N.en[k]=a[1](e)});
  const m=window.missingKeys();if(m.length)console.warn("i18n:",m)}
 document.documentElement.lang=window.lang;
 for(const a of I18A)document.querySelectorAll("["+a[0]+"]").forEach(e=>
  a[2](e,t(e.getAttribute(a[0]))))};
let tunState={};
// Commands: BOUNDED PARALLELISM (3 in flight max) + feedback (600 ms border).
// The old single-slot lock QUEUED the clicks: under camera congestion (TCP
// spikes of 1-3 s measured 2026-07-20), each click waited for the previous
// one's spike — cumulative perceived lag. In parallel, each click only pays
// ITS OWN latency. busy=true when >=1 command is in flight: the camera view
// and the status poll GIVE WAY (radio priority goes to commands).
// No refreshTun() per command (a pointless 2nd request); 5 s timeout
// (AbortController): a hanging fetch no longer blocks the console forever.
let inflight=0,busy=false,resyncT=null;
// RESYNC after a POST fails or times out: the optimistic UI would otherwise
// lie (displayed toggle != device state) until a reload — same for two quick
// toggles arriving out of order under congestion (07-21 review). Debounced
// 400 ms.
function resync(){if(resyncT)return;resyncT=setTimeout(()=>{resyncT=null;refreshTun()},400)}
function P(u,btn){
 if(inflight>=3){setTimeout(()=>P(u,btn),150);return}
 inflight++;busy=true;
 const ac=new AbortController(),to=setTimeout(()=>ac.abort(),5000);
 fetch(u,{method:"POST",signal:ac.signal})
  .then(r=>{flash(btn,r.ok);if(!r.ok)resync()})
  .catch(()=>{flash(btn,false);resync()})
  .finally(()=>{clearTimeout(to);if(--inflight<=0){inflight=0;busy=false}})}
function T(k,v,btn){P("/api/tuning?"+k+"="+v,btn)}
// ---- RULES ------------------------------------------------------------------
// Lists what the ENGINE holds, which is not the same thing as what the file
// contains: a line that fails to parse is absent, silently. Greyed = the rule
// is loaded but its gate field is currently below 0.5, so it cannot fire.
function loadRules(btn){
 const box=document.getElementById("rulesBox");if(!box)return;
 fetch("/api/rules").then(r=>r.json()).then(j=>{
  const R=j.rules||[];
  const st=document.getElementById("rulesState");
  const live=R.filter(r=>r.on).length;
  if(st)st.textContent=R.length?t("rules_n",{n:R.length,a:live}):t("rules_none");
  if(!R.length){box.innerHTML='<span class="desc">'+t("rules_none")+'</span>';
   if(btn)flash(btn,true);return}
  // TWO HEADER ROWS. The first says what a column is FOR, in the reader's
  // language; the second is the FIELD NAME exactly as it is written in
  // rules.txt, in code, never translated. `Condition` spans its three
  // (`field` `op` `value`) because that is what a condition is made of, and
  // splitting them is what lets the guide below name each one.
  box.innerHTML='<table class="rtab">'
   +'<thead><tr class="rh1">'
   +`<th rowspan="2">${t("rc_src")}</th>`
   +`<th>${t("rc_gate")}</th>`
   +`<th colspan="3" class="rc">${t("rc_cond")}</th>`
   +`<th>${t("rc_hold")}</th><th>${t("rc_cool")}</th><th>${t("rc_act")}</th>`
   +`<th rowspan="2">${t("rc_desc")}</th></tr>`
   +'<tr class="rh2">'
   +['enable','field','op','value','sustainMs','cooldownMs','action']
     .map(k=>`<th><code>${k}</code></th>`).join('')
   +'</tr></thead><tbody>'
   +R.map(r=>`<tr class="${r.on?'':'off'}">`
     +`<td class="rsrc">${r.sd?"SD":t("rc_builtin")}</td>`
     +`<td>${r.en?`<code>${esc(r.en)}</code>`:'<span class="dim">—</span>'}</td>`
     +`<td><code>${esc(r.f)}</code></td>`
     +`<td>${esc(r.op)}</td>`
     +`<td><b>${rNum(r.v)}</b></td>`
     +`<td>${r.sus?rMs(r.sus):'<span class="dim">—</span>'}</td>`
     +`<td>${r.cd?rMs(r.cd):'<span class="dim">—</span>'}</td>`
     +`<td>${esc(r.act)}</td>`
     +`<td class="rdsc">${r.d?esc(r.d):'<span class="dim">—</span>'}</td></tr>`)
     .join('')+'</tbody></table>';
  if(btn)flash(btn,true)})
 .catch(()=>{box.innerHTML='<span class="desc">'+t("rules_ko")+'</span>';
  if(btn)flash(btn,false)})}
// Reload THEN re-read: the 202 says the request was accepted, not that loop()
// has re-parsed the card. A second of grace, then the list speaks for it.
function reloadRules(btn){P("/api/rules/reload",btn);setTimeout(()=>loadRules(null),1200)}
// `esc` already exists further down as a FUNCTION declaration; a `const esc`
// here is not a shadow, it is a redeclaration in the same scope - a SyntaxError
// that kills the WHOLE script at parse time, so the page renders as bare HTML
// with every list empty. Reuse it, and give the two rule-table helpers names
// that say whose they are.
const rNum=v=>Number.isInteger(v)?v:String(v).replace(/0+$/,"").replace(/\.$/,"");
const rMs=v=>v>=1000?(v/1000).toFixed(v%1000?1:0)+"s":v+"ms";
// ---- TABS ------------------------------------------------------------------
// The page had grown to eight panels head to tail; reaching the tuning tables
// meant scrolling past the camera and the file manager every single time.
//
// The tab is in the HASH, so a link to /#sys opens System and a reload stays
// where you were - and it falls back to localStorage, because the hash is lost
// the moment anything on the page navigates. Neither alone is enough: the hash
// is shareable but fragile, the storage is durable but private to the browser.
// `band` is gone from this list, and that is what makes a remembered "band"
// fall back to `pilot` rather than open an empty page: `tab()` validates
// against it before touching anything.
const TABS=["pilot","opt","perso","files","sys"];
// ---- CHARACTERS -----------------------------------------------------------
// The tab edits a COPY (`PER.cur`) and only writes on Save, so a half-dragged
// slider is never on the card. `Try it now` is the exception and says so: it
// applies without saving, which is the only way to judge a character — a colour
// and a cadence are not things you can read off a form.
let PER={list:[],cur:null};
const PER_EMO=["Normal","Happy","Glee","Excited","Curious","Surprised",
 "Questioning","Focused","Awe","Blush","Smug","Sleepy","Doubt","Skeptic",
 "Suspicious","Nervous","Worried","Annoyed","Sad","Frustrated","Unimpressed",
 "Contempt","Disgust","Angry","Furious","Scared","Scary","Frozen","Squint","Dead"];

function perLoad(){
 return fetch("/api/personalities").then(r=>r.json()).then(d=>{
  PER.list=d.items||[];
  const act=document.getElementById("o_personality");
  const ed=document.getElementById("pEdit");
  if(act)act.innerHTML=PER.list.map(p=>
   `<option value="${p.i}">${p.name}</option>`).join("");
  if(ed)ed.innerHTML=PER.list.map(p=>
   `<option value="${p.i}">${p.name}</option>`).join("");
  if(act)act.value=String(d.active);
  // Edit whatever is being WORN by default: that is the character you are
  // looking at, so it is the one you most likely came here to change.
  if(ed&&!PER.cur)ed.value=String(d.active);
  perPick();
  perSkin();
 }).catch(()=>{});
}

// The console wears the ACTIVE character's skin. Stamped on <html> so the CSS
// variables cascade to everything, including panels drawn before this ran.
function perSkin(){
 const a=PER.list.find(p=>p.i==(document.getElementById("o_personality")||{}).value);
 const t=(a&&a.theme)||"default";
 if(t==="default")document.documentElement.removeAttribute("data-theme");
 else document.documentElement.setAttribute("data-theme",t);
}

function perPick(){
 const ed=document.getElementById("pEdit");if(!ed)return;
 const p=PER.list.find(x=>x.i==ed.value);if(!p)return;
 PER.cur=JSON.parse(JSON.stringify(p));
 document.getElementById("pName").value=p.name;
 document.getElementById("pRules").value=p.rules;
 document.getElementById("pTheme").value=p.theme;
 document.getElementById("pNoCol").checked=(p.color===0);
 if(p.color)document.getElementById("pColor").value=
  "#"+("000000"+p.color.toString(16)).slice(-6);
 const mn=document.getElementById("pMin"),mx=document.getElementById("pMax");
 mn.value=p.min_ms;mx.value=p.max_ms;
 pMinV.textContent=(p.min_ms/1000)+"s";pMaxV.textContent=(p.max_ms/1000)+"s";
 const ts=document.getElementById("pTScale"),ps=document.getElementById("pPScale");
 ts.value=p.tscale!=null?p.tscale:1;ps.value=p.pbscale!=null?p.pbscale:1;
 pTScaleV.textContent=(+ts.value).toFixed(1)+"x";pPScaleV.textContent=(+ps.value).toFixed(1)+"x";
 // The DEFAULT character declares no weights: it leaves the firmware's own
 // table alone. Shown as every slider at zero with a note, rather than as a
 // grid of fake values that saving would make real.
 const w=p.w||{};
 const note=document.getElementById("pWNote");
 // A character with NO declared weights is not a character that draws nothing:
 // it leaves the firmware's own table alone. Thirty sliders at zero say the
 // opposite, so the difference is spelled out rather than left to be inferred
 // from an empty grid.
 if(note)note.textContent=p.w&&Object.keys(p.w).length?"":t("m_pwnone");
 document.getElementById("pW").innerHTML=PER_EMO.map(e=>{
  const v=w[e]||0;
  return `<label>${e}<input type="range" min="0" max="1" step="0.05" value="${v}"
   data-emo="${e}" oninput="this.nextElementSibling.textContent=(+this.value).toFixed(2)">
   <b>${(+v).toFixed(2)}</b></label>`}).join("");
 // Name is the identity: renaming an existing character would create a second
 // one rather than move it, so the field is locked unless you duplicated.
 document.getElementById("pName").readOnly=true;
 // DELETE IS DISABLED, not merely refused, when the character cannot go. The
 // robot says no either way; a button that looks live and then scolds you is a
 // worse way to learn a rule than one that was never offered.
 const db=document.getElementById("pDel");
 if(db){db.disabled=!p.del;db.title=p.del?"":t("m_pdelno")}
 perColState();
 perMsg("");
}

// The swatch is meaningless while the emotion palette is in use, so it is
// greyed rather than left showing a colour the robot is not wearing — which is
// exactly what the default character looked like before this: a confident green
// for something with no colour at all.
function perColState(){
 const no=document.getElementById("pNoCol"),c=document.getElementById("pColor");
 if(!no||!c)return;
 c.disabled=no.checked;
 c.style.opacity=no.checked?.35:1;
}

function perMsg(t,bad){const e=document.getElementById("pMsg");if(!e)return;
 e.textContent=t;e.style.color=bad?"var(--ko)":"var(--mut)"}

function perForm(){
 const wl=[...document.querySelectorAll("#pW input")]
  .filter(i=>+i.value>0).map(i=>i.dataset.emo+":"+(+i.value).toFixed(2));
 const noCol=document.getElementById("pNoCol").checked;
 return {name:document.getElementById("pName").value.trim(),
  rules:document.getElementById("pRules").value.trim(),
  theme:document.getElementById("pTheme").value,
  color:noCol?0:parseInt(document.getElementById("pColor").value.slice(1),16),
  min_ms:+document.getElementById("pMin").value,
  max_ms:+document.getElementById("pMax").value,
  transition_scale:+document.getElementById("pTScale").value,
  pitch_bias_scale:+document.getElementById("pPScale").value,
  w:wl.join(",")};
}

function perPost(f){
 const q=Object.keys(f).map(k=>k+"="+encodeURIComponent(f[k])).join("&");
 return fetch("/api/personalities?"+q,{method:"POST"}).then(r=>r.json());
}

function perSave(){
 const f=perForm();
 if(!f.name){perMsg(t("m_pname"),1);return}
 perPost(f).then(d=>{
  if(d.error){perMsg(d.error,1);return}
  perMsg(t("m_psaved"));
  // Reload from the ROBOT, never from the form: what the card holds after a
  // write is the only truth, and a field the firmware clamped (a cadence under
  // one second) has to come back changed rather than look accepted.
  setTimeout(()=>{PER.cur=null;perLoad()},600);
 }).catch(()=>perMsg(t("m_pfail"),1));
}

// APPLY WITHOUT SAVING. It writes the file, applies it, and leaves it there —
// so "try" is really "save and look". Honest about that in the message rather
// than pretending to a preview the firmware has no path for: a character that
// existed only in RAM would vanish on the next reload and look like a bug.
function perPreview(){
 const f=perForm();
 if(!f.name){perMsg(t("m_pname"),1);return}
 perPost(f).then(d=>{
  if(d.error){perMsg(d.error,1);return}
  const p=PER.list.find(x=>x.name===f.name);
  if(p)fetch("/api/tuning?personality="+p.i,{method:"POST"});
  perMsg(t("m_ptry"));
  setTimeout(()=>{PER.cur=null;perLoad()},900);
 }).catch(()=>perMsg(t("m_pfail"),1));
}

// Duplicate = the same character under a free name. It does NOT write: the name
// field unlocks and Save is yours to press, so a duplicate you thought better of
// leaves nothing behind.
function perDup(){
 const base=document.getElementById("pName").value.trim()||"perso";
 let n=base.slice(0,12)+"2",i=2;
 while(PER.list.some(p=>p.name===n)){i++;n=base.slice(0,12)+i}
 const f=document.getElementById("pName");
 f.readOnly=false;f.value=n;f.focus();f.select();
 perMsg(t("m_pdup"));
}

function perDel(){
 const ed=document.getElementById("pEdit");
 const p=PER.list.find(x=>x.i==ed.value);if(!p)return;
 if(!p.del){perMsg(t("m_pdelno"),1);return}
 if(!confirm(t("m_pdelq").replace("%s",p.name)))return;
 fetch("/api/personalities?name="+encodeURIComponent(p.name),{method:"DELETE"})
  .then(r=>r.json()).then(d=>{
   if(d.error){perMsg(d.error,1);return}
   perMsg(t("m_pdeleted"));
   setTimeout(()=>{PER.cur=null;perLoad()},600);
  }).catch(()=>perMsg(t("m_pfail"),1));
}

function tab(n){
 if(TABS.indexOf(n)<0)n=TABS[0];
 TABS.forEach(k=>{
  const p=document.getElementById("t-"+k);if(p)p.classList.toggle("on",k==n)});
 document.querySelectorAll("#tabs button").forEach(b=>
  b.classList.toggle("on",b.dataset.tab==n));
 try{localStorage.setItem("sce_tab",n)}catch(e){}
 if(location.hash.slice(1)!=n)history.replaceState(null,"","#"+n);
 // The canvas has no width while its panel is display:none, so a chart drawn
 // then is drawn into nothing. Redraw on arrival rather than waiting out the
 // two-second poll with an empty box on screen.
 drawChart();
}
addEventListener("hashchange",()=>tab(location.hash.slice(1)));
function restoreTab(){
 let n=location.hash.slice(1);
 if(TABS.indexOf(n)<0){try{n=localStorage.getItem("sce_tab")||""}catch(e){n=""}}
 tab(TABS.indexOf(n)>=0?n:TABS[0]);
 // Telemetry: same idea, one flag. Only ever CLOSED explicitly - the default
 // is open, because a console that opens on a blank rectangle says nothing.
 try{const te=document.getElementById("tele");
  if(te&&localStorage.getItem("sce_tele")==="0")te.open=false}catch(e){}
}
// THE WALL CLOCK. `plausible` and `ntp` are two different facts and the
// console shows both: the chip restores a believable time from the RTC at
// every boot, so a clock that LOOKS right proves nothing about NTP. Only the
// second badge means a packet really landed.
function clkShow(c){
 const st=document.getElementById("clkState"),nw=document.getElementById("clkNow");
 if(!st||!nw)return;
 st.textContent=c.ap?t("clk_ap"):c.ntp?t("clk_ok"):c.pending?t("clk_wait"):t("clk_never");
 nw.textContent=c.epoch?utcHm(c.epoch)+" UTC"+(c.ntp?"":" ("+t("clk_rtc")+")"):t("v_nontp")}
// Read ONCE per page load: every field is fixed for the whole boot, so
// polling it would be pure noise on a robot that already answers /api/status
// every two seconds.
function fwRead(){fetch("/api/firmware").then(r=>r.json()).then(d=>{
 const put=(k,v)=>{const e=document.getElementById("fw_"+k);if(e)e.textContent=v||"—"};
 put("slot",d.slot);put("sha",d.sha);
 put("console",d.console);put("reset",d.reset);
 // A restart the robot did NOT choose gets the error colour.
 const bad=/^(panic|task_wdt|int_wdt|wdt|brownout)$/.test(d.reset||"");
 const e=document.getElementById("fw_reset");
 if(e)e.className=bad?"ko":"";
 // Same four values in the footer, one line, same crash highlight.
 const f=document.getElementById("fwFoot");
 if(f)f.innerHTML="build <b>"+esc(d.sha||"?")+"</b> · console <b>"+esc(d.console||"?")
  +"</b> · "+esc(d.slot||"?")+" · "+t("fw_start")+" <b"+(bad?' class="ko"':"")+">"
  +esc(d.reset||"?")+"</b>";
}).catch(()=>{})}
function clkRead(){fetch("/api/clock").then(r=>r.json()).then(clkShow).catch(()=>{})}
// After the press, look again: the answer is 202 and the packet lands later.
// Three looks over six seconds — past that the server is unreachable or the
// port is blocked, and a spinner that never stops says less than a stale badge.
function syncClock(btn){P("/api/clock/sync",btn);
 [1500,3500,6000].forEach(d=>setTimeout(clkRead,d))}
// Pushes 3 test gauges (fields g0..g2 + labels) and switches to Gauges mode.
// The labels are the ones the ROBOT displays on its band: kept as they are,
// they are data pushed to the device, not console wording.
// Sets the band timer's duration from the console. Minutes and seconds are
// sent SEPARATELY and clamped server-side to the same 0-99 / 0-59 the band's
// own gesture uses — one definition of a valid timer, not two.
function tmSet(btn){
 const m=Math.max(0,Math.min(99,parseInt(tmM.value)||0));
 const s=Math.max(0,Math.min(59,parseInt(tmS.value)||0));
 tmM.value=m;tmS.value=s;
 P('/api/timer?action=set&m='+m+'&s='+s,btn);
}
// A preset sets AND starts in ONE request: the firmware holds a single
// command slot, so two calls in a row could see the second overwrite the
// first before loop() drained it.
function tmGo(m,btn){tmM.value=m;tmS.value=0;P('/api/timer?action=set&m='+m+'&s=0&start=1',btn)}
function pushGauges(btn){
 const L=["CTX","5H","7J"],q=[];
 [0,1,2].forEach(i=>{const el=document.getElementById("jg"+i);
  if(el&&el.value!==""){q.push("g"+i+"="+el.value,"g"+i+"l_s="+L[i])}});
 if(!q.length){flash(btn,false);return}
 P("/api/statusbar?mode=3");syncMode(3);
 P("/api/field?"+q.join("&"),btn)}
function flash(b,ok){if(!b)return;b.className=ok?"ok":"ko";setTimeout(()=>b.className="",600)}
// ---- Camera view: refreshing stills (~1/s). More robust inside an <img>
// than the MJPEG stream (reserved for Frigate/ffmpeg): it copes with the
// start-up 503 (~2 s of init) by retrying, and keeps the camera awake.
let camTimer=null,camFails=0,camPending=false,camPendingMs=0,camGen=0;
function camTick(){
 // Anti-PILE-UP: do not start a new request while the previous one has not
 // answered yet — otherwise slow still.jpg requests stack up (browser
 // connections saturated), which delays every other request (dances, tuning)
 // more and more for as long as the view stays open.
 // 6 s watchdog: an Image() that never fires onload/onerror (aborted
 // connection) would leave camPending stuck -> view frozen forever.
 if(camPending&&Date.now()-camPendingMs>6000)camPending=false;
 if(camPending)return;
 // COMMANDS FIRST: if a command is in flight -> skip this image tick.
 // Measured 2026-07-20: POST avg 35 ms with no camera, 1150 ms with the view
 // active (3 s spikes = TCP retransmissions, the radio is the bottleneck) —
 // one image less lets the command through without fighting for the antenna.
 if(busy)return;
 // Generation TOKEN: callbacks from an ABANDONED Image() (view stopped,
 // watchdog) are ignored — a late onload used to resurrect the ghost image
 // after "Stop" and overwrite a more recent frame (07-21 review).
 const img=document.getElementById("camImg");
 const pre=new Image(),gen=++camGen;camPending=true;camPendingMs=Date.now();
 pre.onload=()=>{if(gen!==camGen||!camTimer)return;
  camPending=false;img.src=pre.src;camFails=0;
  img.style.display="block";
  document.getElementById("camPh").style.display="none";
  document.getElementById("camMsg").textContent=""};
 pre.onerror=()=>{if(gen!==camGen)return;
  camPending=false;camFails++;
  // Clear the last frame after 2 consecutive failures (camera turned off via
  // the option → still.jpg 403, or init lost): otherwise the old "ghost"
  // image would stay on screen while the camera is off.
  if(camFails>=2){img.style.display="none";img.src="";
   document.getElementById("camPh").style.display="flex"}
  document.getElementById("camMsg").textContent=
   camFails<5?t("cam_start"):t("cam_noimg")};
 pre.src="/api/camera/still.jpg?t="+Date.now()}
function stopCamView(){
 if(camTimer){clearInterval(camTimer);camTimer=null}
 camGen++;camPending=false;
 const img=document.getElementById("camImg");
 img.style.display="none";img.src="";
 document.getElementById("camPh").style.display="flex";
 document.getElementById("camMsg").textContent="";
 const b=document.getElementById("camBtn");if(b)b.textContent=t("cam_view")}
function toggleCam(btn){
 if(camTimer){stopCamView();return}
 const cb=document.getElementById("o_camera");
 if(cb&&!cb.checked){cb.checked=true;T("camera",1)}
 btn.textContent=t("cam_stop");camFails=0;
 document.getElementById("camMsg").textContent=t("cam_start");
 camTick();camTimer=setInterval(camTick,1000)}
// "Enable the camera" checkbox: OFF ALSO kills the console view; the MJPEG
// streams (other tabs / Frigate) are closed on the ESP side as soon as
// camera=0 (the filler returns 0 → end of response).
function onCamOpt(cb){T("camera",cb.checked?1:0);if(!cb.checked)stopCamView()}
// FULL-quality snapshot (?full=1): the live view is compressed, but this
// button captures a full-quality frame on demand. The tab is opened WITHIN the
// user gesture (otherwise the popup is blocked), then navigates to the image
// once it is ready (retry on 503 = full capture in progress on the ESP side).
function snapshot(btn){
 const w=window.open("","_blank");
 if(w)try{w.document.write("<title>"+t("snap_title")+"</title><body style=font:14px system-ui;padding:1em>"+t("snap_wait"))}catch(e){}
 let n=0;
 const RETRY={};// sentinel: a 503-retry IS NOT a failure — the old null
                // flashed red + showed "failure" on EVERY normal retry (07-21)
 const done=b=>{// blob → show/download, then REVOKE (else a leak per click)
  if(b&&w){const u=URL.createObjectURL(b);w.location=u;
   setTimeout(()=>URL.revokeObjectURL(u),60000)}
  else if(b){const a=document.createElement("a"),u=URL.createObjectURL(b);
   a.href=u;a.download="stackchan-"+Date.now()+".jpg";a.click();
   setTimeout(()=>URL.revokeObjectURL(u),60000)}
  else{if(w)try{w.document.body.textContent=t("snap_fail")}catch(e){}
   flash(btn,false)}};
 const go=()=>{
  const ac=new AbortController(),to=setTimeout(()=>ac.abort(),8000);
  fetch("/api/camera/still.jpg?full=1",{signal:ac.signal}).then(r=>{
    if(r.status==503&&n++<12){setTimeout(go,300);return RETRY}
    return r.ok?r.blob():null})
   .then(b=>{if(b!==RETRY)done(b)})
   .catch(()=>{if(n++<12)setTimeout(go,400);else done(null)})
   .finally(()=>clearTimeout(to))};
 go()}
// Companion firmware OTA: XHR (upload progress) + auto reboot
function upFw(btn){const f=fwbin.files[0];if(!f){flash(btn,false);return}
 if(!confirm(t("ota_c",{f:f.name,k:(f.size/1024)|0})))return;
 const fd=new FormData();fd.append("firmware",f,f.name);
 const x=new XMLHttpRequest();x.open("POST","/api/update");
 fwprog.style.display="block";fwprog.value=0;btn.disabled=true;
 x.upload.onprogress=e=>{if(e.lengthComputable)fwprog.value=100*e.loaded/e.total};
 x.onload=()=>{flash(btn,x.status==200);btn.disabled=false;
  if(x.status==200){fwprog.value=100;setTimeout(()=>location.reload(),15000)}
  else alert(t("ota_ko")+x.responseText)};
 x.onerror=()=>{flash(btn,false);btn.disabled=false};
 x.send(fd)}
emo.innerHTML=EMOS.map(e=>`<button onclick="P('/api/emotion?name=${e}',this)">${e}</button>`).join("");
function loadDances(){fetch("/api/dances").then(r=>r.json()).then(a=>{
 dan.innerHTML=a.map(d=>`<button onclick="P('/api/dance?name=${encodeURIComponent(d)}',this)">${esc(d)}</button>`).join("");
 // THE END-OF-TIMER DANCE, filled from the SAME fetch as the buttons above.
 // `timer_dance` is a 1-based INDEX into this list, so a hand-written <option>
 // set would be a second copy of the dance table - and the day somebody adds a
 // dance, the console would name the wrong one while the robot danced the
 // right one. One source, and the index is the position in it.
 const s=document.getElementById("tdance");
 if(s){s.innerHTML=`<option value="0">${t("t_nodance")}</option>`+
   a.map((d,i)=>`<option value="${i+1}">${esc(d)}</option>`).join("");
  syncTimerDance();}})}
// THE SELECTED value, in ONE place, because two racing fetches write it:
// /api/dances builds the <option> set, /api/tuning brings the saved index, and
// whichever landed second used to win. Re-reading `tunState` from both ends
// makes the order stop mattering. An index the list no longer holds falls back
// to "none" rather than leaving the select blank - a control showing nothing
// at all cannot be told apart from a broken page.
function syncTimerDance(){const s=document.getElementById("tdance");
 if(!s||!s.options.length)return;
 s.value=String(Math.round((tunState&&tunState.timer_dance)||0));
 if(!s.value)s.value="0"}
// SD FILE management gathered in a single panel (dances + bins + config +
// rules): no more dances/bins duplication across sections.
// Fetched from refreshDynamicText() near the bottom of this script, AFTER
// applyLang() - not from here, where window.lang would still be its
// hardcoded default and every t()-rendered string in these two would boot
// in English regardless of the card's configured language.
// Options & status bar: LINKED unfolding (never one empty half of the pair)
(function(){const a=document.getElementById("dOpt"),b=document.getElementById("dBand");
 if(a&&b){const s=(x,y)=>x.addEventListener("toggle",()=>{if(y.open!==x.open)y.open=x.open});s(a,b);s(b,a)}})();
function refreshTun(){fetch("/api/tuning").then(r=>r.json()).then(t2=>{tunState=t2;
 // Grouped by category KEY (last column): one table per category.
 // "cam" is rendered APART, compact, in the Camera panel (#camtun).
 const cats=[];TUN.forEach(r=>{const c=r[6]||"misc";
  let g=cats.find(x=>x.c==c);if(!g){g={c,rows:[]};cats.push(g)}g.rows.push(r)});
 const row=([k,mi,ma,st,df])=>`<tr>
 <td class="tid"><code>${k}</code></td><td class="tdef">${df}</td>
 <td class="tsl"><i class="rmin">${mi}</i><input type="range" min="${mi}" max="${ma}" step="${st}" value="${t2[k]??0}"
 oninput="this.parentElement.querySelector('b').textContent=this.value" onchange="T('${k}',this.value,this)"><i class="rmax">${ma}</i>
 <b>${t2[k]??"?"}</b></td><td class="tdesc">${t("d_"+k)}</td></tr>`;
 tun.innerHTML=cats.filter(g=>g.c!="cam").map(g=>`<div class="tcat">${t("cat_"+g.c)}</div>`+
  `<table><tbody>${g.rows.map(row).join("")}</tbody></table>`).join("");
 const cam=cats.find(g=>g.c=="cam"),ct=document.getElementById("camtun");
 if(cam&&ct)ct.innerHTML=cam.rows.map(([k,mi,ma,st])=>
  `<div class="ctr"><span title="${t("d_"+k)}">${k.replace("cam_","")}</span>
  <span><i class="rmin">${mi}</i><input type="range" min="${mi}" max="${ma}" step="${st}" value="${t2[k]??0}"
  oninput="this.parentElement.querySelector('b').textContent=this.value" onchange="T('${k}',this.value,this)"><i class="rmax">${ma}</i>
  <b>${t2[k]??"?"}</b></span></div>`).join("");
 // Options toggles ← real state of the register
 for(const k of["leds","sound","telemetry","head_follow","servos","sound_track","mic_enable","led_swap","led_depth_front","camera","auto_brightness","dark_sleepy","band_debug","band_clock","clock_24h","debug","roulette"]){
  const el=document.getElementById("o_"+k);if(el)el.checked=(t2[k]??0)>=.5}
 // The trace toggle exists TWICE (telemetry row + System); same register.
 const dbt=document.getElementById("o_debug_t");if(dbt)dbt.checked=(t2.debug??0)>=.5;
 // Personality is a SELECT, not a checkbox, so it misses the loop above. Read
 // back from the register like everything else rather than trusted to the last
 // click: the key is also reachable from the API and from config.yaml, and a
 // control showing something the robot is not doing is worse than no control.
 const pe=document.getElementById("o_personality");
 if(pe&&pe.value!==String((t2.personality??0)|0)){
  pe.value=String((t2.personality??0)|0);
  // The character changed under us (API, another browser, config reload), so
  // the SKIN has to follow. Only on a real change: re-stamping every poll would
  // rewrite the root attribute twice a second for nothing.
  if(typeof perSkin==="function")perSkin();
 }
 // Status-bar icons ← mask (bits batt=1 wifi=2 cam=4 mic=8 night=16), pills
 const im=(t2.icon_mask??31)|0;
 document.querySelectorAll("#iconPills .pill").forEach(p=>p.classList.toggle("on",(im&+p.dataset.bit)!==0));
 // Status-bar sliders (text size / scroll speed) ← tuning
 const bts=document.getElementById("bts"),bss=document.getElementById("bss");
 if(bts){bts.value=(t2.band_text_size??1)|0;btsv.textContent=bts.value}
 if(bss){bss.value=(t2.band_scroll_speed??70)|0;bssv.textContent=bss.value}
 if(tzo){tzo.value=t2.tz_offset_h??0;tzov.textContent=tzo.value}
 const sbr=document.getElementById("sbr");
 if(sbr){sbr.value=t2.screen_bright??76;sbrv.textContent=sbr.value}
 const sgn=document.getElementById("sgn");
 if(sgn){sgn.value=t2.band_sound_gain??1;sgnv.textContent=sgn.value+"x"}
 syncSnd(t2.band_sound??0)
 // The pomodoro sliders live in the band panel AND in Fine tuning;
 // both must show the same value or one of them is lying.
 for(const[el,k,d]of[[window.pw,"pomo_work_min",25],[window.pb,"pomo_break_min",5],[window.pc,"pomo_cycles",4],[window.ph,"pomo_hydra_min",1]])
  if(el){el.value=t2[k]??d;document.getElementById(el.id+"v").textContent=el.value}
 // The end-of-timer dance is a SELECT, not a slider, and loadDances() fills it
 // BEFORE the first /api/tuning answer exists - so it had nothing to select.
 // Re-applying it here is what makes the console show the saved choice instead
 // of "none" while the robot dances anyway.
 syncTimerDance()
 setPadEnabled((t2.servos??1)>=.5)})}
// Rebuilds the icon mask from the active PILLS and persists it.
function setIcons(){let m=0;document.querySelectorAll("#iconPills .pill.on").forEach(p=>m|=+p.dataset.bit);
 T("icon_mask",m)}
// Status-bar mode: segmented control (#o_statusbar holds <button data-m>)
function syncMode(m){const c=document.getElementById("o_statusbar");if(!c||m==null)return;
 // The right column follows the mode: exactly one block is shown, so a
 // control is never on screen for a mode that cannot use it.
 document.querySelectorAll(".bandm").forEach(function(b){b.classList.toggle("on",b.dataset.mode==String(m))});
 c.querySelectorAll("button").forEach(b=>b.classList.toggle("on",+b.dataset.m===+m))}
function setMode(m){syncMode(m);P("/api/statusbar?mode="+m)}
// Setting the backlight by hand disables the adaptive option, and the checkbox
// follows so the page never shows two settings that contradict each other.
function setBright(v){const a=document.getElementById("o_auto_brightness");
 if(a&&a.checked){a.checked=false;T("auto_brightness",0)}
 T("screen_bright",v)}
// The sound style is a TUNING value, not a band mode: it rides the same
// /api/tuning pipe as every other display choice, so it persists to the card
// through the machinery that already does that.
function syncSnd(v){const c=document.getElementById("o_snd");if(!c||v==null)return;
 c.querySelectorAll("button").forEach(b=>b.classList.toggle("on",+b.dataset.s===Math.round(+v)))}
function setSnd(v){syncSnd(v);T("band_sound",v)}
// The mic and the head-following are two settings, but one implies the other:
// tracking without a microphone is a switch that does nothing, so the pair is
// kept coherent HERE rather than leaving the user to discover it.
function setMic(on){const t=document.getElementById("o_sound_track");
 T("mic_enable",on?1:0);
 if(!on&&t&&t.checked){t.checked=false;T("sound_track",0)}}
function setTrack(on){const m=document.getElementById("o_mic_enable");
 T("sound_track",on?1:0);
 if(on&&m&&!m.checked){m.checked=true;T("mic_enable",1)}}
// LANGUAGE. The firmware owns it (it is `lang:` on the card), so the button
// posts and then re-translates the page on the spot instead of reloading:
// a reload would be a second round trip to show what is already known.
// APPLIED ONLY ONCE THE ROBOT HAS ACCEPTED IT. Translating the page first and
// posting afterwards would leave the console speaking French while the robot,
// the launcher and the guest bins stayed English whenever the POST failed —
// and nothing would say so, because `resync()` re-reads the tuning register and
// the language is not in it. The page reverts on failure instead.
//
// applyLang() ONLY SWEEPS data-i18n ELEMENTS. Rules/Files/Dances/Tuning are
// each loaded ONCE at boot and rendered straight to text through t() in JS,
// with nothing marking the result as translatable — the same gap that made
// loadRules(null) run a line before applyLang() at boot harmless (both still
// happen before the user can see anything) turns into a visible bug the
// moment the switch is flipped AFTER the page is up: everything data-i18n
// flips language immediately, but a rule count already on screen keeps
// showing yesterday's language until its own "refresh this table" is
// pressed by hand (found 2026-09-22, screenshot: an English console
// reading "23 règles · 23 actives"). Re-running the same four loaders the
// boot sequence already runs is the fix, not a new mechanism.
function refreshDynamicText(){loadRules(null);loadSdList();loadDances();refreshTun()}
function setLang(l){const prev=window.lang;
 const ac=new AbortController(),to=setTimeout(()=>ac.abort(),5000);
 fetch("/api/config?lang="+l,{method:"POST",signal:ac.signal})
  .then(r=>{if(!r.ok)throw 0;
   window.SCE_LANG=l;window.lang=l;applyLang();syncLang();refreshDynamicText()})
  .catch(()=>{window.SCE_LANG=prev;window.lang=prev;applyLang();syncLang();refreshDynamicText()})
  .finally(()=>clearTimeout(to))}
function syncLang(){document.querySelectorAll("#o_lang button").forEach(
 b=>b.classList.toggle("on",b.dataset.l===window.lang))}
// ---- SD files (unified manager: dances/bins/config/rules) ----
// Escaping is MANDATORY: the names come from the SD (import or an external
// card) — without esc(), a name holding ' or < would inject JS/HTML (XSS).
function esc(s){return String(s).replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]))}
function loadSdList(btn){
 fetch("/api/sd/list").then(r=>r.json()).then(j=>{
  const files=j.files||[],grp={config:[],rules:[],dance:[],bin:[],guest:[]};
  files.forEach(f=>{if(grp[f.cat])grp[f.cat].push(f)});
  const link=f=>`<a class="fnm" href="/api/sd/get?path=${encodeURIComponent(f.path)}" download title="${t("t_dl")}">${esc(f.name)}</a><span class="fsz">${f.size} o</span>`;
  const del=f=>`<button class="del" data-del="${esc(f.path)}" data-nm="${esc(f.name)}" title="${t("t_del")}">✕</button>`;
  let h="";
  if(grp.config.length||grp.rules.length||grp.guest.length){h+=`<div class="fgrp"><div class="gh">${t("g_cfg")}</div>`;
   grp.config.forEach(f=>h+=`<div class="frow">${link(f)}</div>`);
   grp.rules.forEach(f=>h+=`<div class="frow">${link(f)}<button onclick="P('/api/rules/reload',this)" title="${t("t_rl")}">↻</button></div>`);
   grp.guest.forEach(f=>h+=`<div class="frow">${link(f)}${del(f)}</div>`);
   h+='</div>'}
  if(grp.dance.length){h+=`<div class="fgrp"><div class="gh">${t("g_dan")} <span class="ghs">/dances</span></div>`;
   grp.dance.forEach(f=>h+=`<div class="frow">${link(f)}<button class="b-acc" data-play="${esc(f.name.replace(/\.csv$/,""))}" title="${t("t_play")}">▶</button>${del(f)}</div>`);
   h+='</div>'}
  if(grp.bin.length){h+=`<div class="fgrp"><div class="gh">${t("g_bin")} <span class="ghs">/bins</span></div>`;
   grp.bin.forEach(f=>h+=`<div class="frow">${link(f)}<button class="b-acc" data-launch="${esc(f.name)}" title="${t("t_launch")}">🚀</button>${del(f)}</div>`);
   h+='</div>'}
  const el=document.getElementById("sdlist");el.innerHTML=h||`<span class="desc">${t("nofile")}</span>`;
  // Wired with listeners (no interpolated onclick → no injection)
  el.querySelectorAll("[data-del]").forEach(b=>b.onclick=()=>delSd(b.getAttribute("data-del"),b.getAttribute("data-nm")));
  el.querySelectorAll("[data-play]").forEach(b=>b.onclick=()=>P("/api/dance?name="+encodeURIComponent(b.getAttribute("data-play")),b));
  el.querySelectorAll("[data-launch]").forEach(b=>b.onclick=()=>{if(confirm(t("c_launch",{n:b.getAttribute("data-launch")})))P("/api/bins/launch?name="+encodeURIComponent(b.getAttribute("data-launch")),b)});
  if(btn)flash(btn,true)
 }).catch(()=>{if(btn)flash(btn,false)})}
function impHint(){const c=document.getElementById("impCat").value;
 document.getElementById("impName").style.display=(c==="dance"||c==="bin"||c==="guest")?"":"none"}
function importSd(btn){
 const c=document.getElementById("impCat").value,file=document.getElementById("impFile").files[0];
 if(!file){flash(btn,false);return}
 let path;
 if(c==="config")path="/stackchan-companion/config.yaml";
 else if(c==="rules")path="/stackchan-companion/rules.txt";
 else if(c==="companion")path="/companion.bin";
 else{let n=document.getElementById("impName").value||file.name;
  if(c==="dance"&&!n.endsWith(".csv"))n+=".csv";if(c==="bin"&&!n.endsWith(".bin"))n+=".bin";
  if(c==="guest"&&!n.endsWith(".yaml"))n+=".yaml";
  path=(c==="dance"?"/dances/":c==="guest"?"/stackchan-companion/":"/bins/")+n}
 const fd=new FormData();fd.append("f",file);const x=new XMLHttpRequest();
 x.open("POST","/api/sd/put?path="+encodeURIComponent(path));
 x.onload=()=>{flash(btn,x.status==200);loadSdList();loadDances()};x.onerror=()=>flash(btn,false);x.send(fd)}
function delSd(path,name){if(!confirm(t("c_del",{n:name})))return;
 fetch("/api/sd/delete?path="+encodeURIComponent(path),{method:"DELETE"})
  .then(r=>{loadSdList();if(!r.ok)alert(t("del_ko",{s:r.status}))}).catch(()=>alert(t("del_ko0")))}
// Servo pad greyed out when servos=0: the moves are inert (torque released)
// — active buttons would lie. Resynchronised by refreshTun.
function setPadEnabled(on){
 document.querySelectorAll(".pad button").forEach(b=>b.disabled=!on)}
function onServosOpt(cb){T("servos",cb.checked?1:0);setPadEnabled(cb.checked)}
// Fetched from refreshDynamicText() near the bottom of this script, AFTER
// applyLang() - see the note by loadDances()/loadSdList() above for why.
// The character list, ONCE at boot: it feeds the selector in the Characters tab
// AND stamps the console's skin, which has to happen whatever tab you land on.
perLoad();
// ---- Header: status panel + heap/frame chart (same canvas) ----
const H=[],FA=[],L0=[],L1=[],MAXPTS=90;   // ~3 min at 2 s/sample
function cell(l,v,cls,ti){const c=cls===true?'off':cls||'';
 return `<div${c?` class="${c}"`:''}${ti?` title="${ti}"`:''}>${l}<b>${v}</b></div>`}
// Uptime in units a human reads (user 08-01). "412563s" is a number you have
// to divide before it means anything; the two LARGEST units are what carries
// the information — "4j 18h" says it, "4j 18h 32min 43s" makes you hunt.
// Seconds only below a minute, because there the second IS the datum.
function dur(sec){
 // A missing field must read as missing, not as "NaNs" — which looks like a
 // firmware fault and sends you diagnosing the wrong end.
 if(!Number.isFinite(sec))return"—";
 sec=Math.max(0,Math.floor(sec));
 const d=Math.floor(sec/86400),h=Math.floor(sec%86400/3600),
       m=Math.floor(sec%3600/60),s=sec%60;
 if(d)return d+"j "+h+"h";
 if(h)return h+"h "+m+"min";
 if(m)return m+"min";
 return s+"s";}
// The robot keeps UTC and has no timezone (deliberate: the only consumer of
// the clock is the solar night, and the sun does not observe daylight saving).
// So the chip SAYS Z rather than pretending to be local time.
function utcHm(e){
 const d=new Date(e*1000);
 const p=n=>String(n).padStart(2,"0");
 return p(d.getUTCDate())+"/"+p(d.getUTCMonth()+1)+" "+
        p(d.getUTCHours())+":"+p(d.getUTCMinutes())+"Z";}
function drawGrid(s){
 if(s)syncMode(s.statusbar);   // mirrors the status-bar mode in the segmented
 // `s.mic` / `s.cam` are API ENUM values (SoundTracker::stateName), not
 // wording: they are compared as-is and only their rendering is translated.
 grid.innerHTML=s?[
  cell(t("g_emo"),s.emotion,0,t("ti_emo")),
  cell(t("g_net"),`${s.mode} ${s.ip}${s.mode=="STA"?" "+s.rssi+"dBm":""}`,0,t("ti_net")),
  cell(t("g_sd"),s.sd?"OK":t("v_nosd"),!s.sd,t("ti_sd")),
  cell(t("g_bat"),s.batt<0?"n/a":`${s.batt}%${s.chg?" ⚡":""}`,
   s.batt<0?true:(s.batt<=15&&!s.chg?"ko":false),t("ti_bat")),
  cell(t("g_volt"),s.inaV>0?`${s.inaV.toFixed(2)}V`:"n/a",!(s.inaV>0),t("ti_volt")),
  cell(t("g_light"),s.light>=0?`${s.light}%`:"n/a",!(s.light>=0),t("ti_light")),
  cell(t("g_hdg"),s.heading>=0?`${Math.round(s.heading)}°`:"n/a",!(s.heading>=0),t("ti_hdg")),
  cell(t("g_up"),dur(s.uptimeS),0,t("ti_up")),
  cell(t("g_clock"),s.clock?utcHm(s.clock):t("v_nontp"),!s.clock,t("ti_clock")),
  cell(t("g_night"),s.clock?(s.night?t("v_night"):t("v_day")):"n/a",
   !s.clock,t("ti_night")),
  cell(t("g_fmax"),(s.frameMaxUs/1000).toFixed(1)+"ms",0,t("ti_fmax")),
  cell(t("g_mic"),
   s.mic=="active"?t("v_miclv",{l:s.micL,r:s.micR,a:s.micAmb,e:s.micEvt})
   :s.mic=="warmup"?t("v_warm",{s:s.micWait})
   :s.mic=="standby"?t("v_standby")
   :t("v_micoff"),s.mic!="active",t("ti_mic")),
 ].join(""):cell(t("g_state"),t("v_offline"));
 const hp=document.getElementById("hpose");
 if(hp)hp.textContent=s?`yaw ${s.yaw}° · pitch ${s.pitch}°`:"—";
 const as=document.getElementById("authState"),au=document.getElementById("authUser");
 if(as&&s)as.textContent=s.authOn?"🔒":"🔓";
 if(au&&s&&document.activeElement!==au&&!au.value)au.value=s.authUser;
 // The Sound mode's microphone line. Same `s.mic` enum the chip above uses —
 // one source, so the two can never disagree about whether it is listening.
 const ms=document.getElementById("sndMicState"),mb=document.getElementById("sndMicBtn");
 if(ms&&mb){
  if(!s){ms.textContent="";mb.hidden=true}
  else{
   ms.textContent=s.mic=="active"?t("snd_mact")
    :s.mic=="warmup"?t("snd_mwarm",{s:s.micWait})
    :s.mic=="standby"?t("snd_mstby"):t("snd_moff");
   mb.hidden=(s.mic!="off");
  }}
 const cs=document.getElementById("camState");
 if(cs&&s)cs.textContent=s.cam=="off"?"":s.cam=="ready"?"● active":s.cam=="idle"?"○ ready":s.cam=="failed"?"⚠ init KO ("+s.camErr+")":"";}
function drawChart(){
 // BOTH dimensions from the CSS box. Only the width was taken, so the
 // canvas kept its 56-pixel bitmap height and a taller box scaled it up —
 // the same four traces, blurrier, which is not what "bigger" means.
 const c=document.getElementById("chart");
 c.width=c.clientWidth;c.height=c.clientHeight;
 const g=c.getContext("2d"),W=c.width,Hh=c.height;
 g.clearRect(0,0,W,Hh);
 if(H.length<2)return;
 // discreet guide: a single median line
 g.strokeStyle="#1c2536";g.lineWidth=1;
 g.beginPath();g.moveTo(0,Hh/2);g.lineTo(W,Hh/2);g.stroke();
 // plot: smoothed curve + translucent flat fill under it (fill=alpha hex)
 const trace=(arr,color,norm,fill)=>{
  g.strokeStyle=color;g.lineWidth=1.5;g.lineJoin="round";g.beginPath();
  arr.forEach((v,i)=>{const x=i/(MAXPTS-1)*W,
   y=Hh-3-norm(v)*(Hh-10);i?g.lineTo(x,y):g.moveTo(x,y)});
  g.stroke();
  if(fill){const x=(arr.length-1)/(MAXPTS-1)*W;
   g.lineTo(x,Hh);g.lineTo(0,Hh);g.closePath();
   g.fillStyle=color+fill;g.fill()}};
 const auto=arr=>{const mi=Math.min(...arr),sp=(Math.max(...arr)-mi)||1;
  return v=>(v-mi)/sp};
 trace(H,"#34d399",auto(H),"12");            // heap (light flat fill)
 trace(FA,"#22d3ee",auto(FA));               // frame
 trace(L0,"#f59e0b",v=>Math.min(v,100)/100); // cpu: FIXED scale 0-100 %
 trace(L1,"#a78bfa",v=>Math.min(v,100)/100,"0d");
 legend.innerHTML=[
  ["#34d399",t("lg_mem",{v:(H[H.length-1]/1024).toFixed(0)}),t("ti_mem")],
  ["#22d3ee",t("lg_fr",{v:(FA[FA.length-1]/1000).toFixed(1)}),t("ti_fr")],
  ["#f59e0b",`c0 ${L0[L0.length-1]??0}%`,t("ti_c0")],
  ["#a78bfa",`c1 ${L1[L1.length-1]??0}%`,t("ti_c1")],
 ].map(([c,x,ti])=>`<span style="color:${c}" title="${ti}">●&hairsp;${x}</span>`).join("");}
// Anti-PILE-UP guard (like camTick): do NOT fire a new /api/status while the
// previous one has not answered. Otherwise, when the server slows down (camera
// load), the fetches stack up -> AsyncTCP connection blow-up -> the server
// ends up answering nothing at all (spiral). Same for any periodic request.
// Periodic resync of the toggles/sliders (30 s): a net against the drift of
// the optimistic UI (lost POST, another client editing, HA...).
clkRead();
fwRead();      // once: constant for the whole boot (see fwRead)
setInterval(()=>{if(!busy)resync()},30000);
let stBusy=false;
setInterval(()=>{if(stBusy||busy)return;stBusy=true;   // gives way to commands
 // 5 s timeout: a status that HANGS would leave stBusy=true forever (frozen
 // header) — the abort self-heals the lock.
 const ac=new AbortController(),to=setTimeout(()=>ac.abort(),5000);
 fetch("/api/status",{signal:ac.signal}).then(r=>r.json())
 .then(s=>{drawGrid(s);
  const crt=document.getElementById("o_crt");if(crt)crt.checked=!!s.crt;
  H.push(s.heap);FA.push(s.frameAvgUs);L0.push(s.load0??0);L1.push(s.load1??0);
  if(H.length>MAXPTS){H.shift();FA.shift();L0.shift();L1.shift()}
  drawChart()})
 .catch(()=>drawGrid(null)).finally(()=>{clearTimeout(to);stBusy=false})},2000);
restoreTab();  // #hash, then the remembered tab, then Pilot
applyLang();   // English snapshot, then straight to the `sce_lang` language
syncLang();    // ...and the toggle shows which one that turned out to be
// AFTER applyLang(), not before: loadRules/loadSdList/loadDances/refreshTun
// each render text through t() straight to the DOM, so whichever value
// window.lang holds at the moment they run is what boots on screen - and
// before applyLang() runs, that is still the hardcoded "en" default, not
// whatever the card's config.yaml actually says. The Rules section used to
// call loadRules(null) one line above this comment, ahead of applyLang(),
// for exactly the reason the old comment there gave (its <details> starts
// open, so no ontoggle fires to load it) - true, but it picked the wrong
// place to fix it from.
refreshDynamicText();
</script></body></html>)HTML";

inline const char SWAGGER_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8"><title>StackChan API</title>
<link rel="stylesheet" href="https://unpkg.com/swagger-ui-dist@5/swagger-ui.css">
</head><body><div id="ui"></div>
<script src="https://unpkg.com/swagger-ui-dist@5/swagger-ui-bundle.js"></script>
<script>SwaggerUIBundle({url:"/api/openapi.json",dom_id:"#ui"})</script>
<p style="font-family:system-ui">No internet (AP mode)? Use the
<a href="/">embedded console</a>.</p></body></html>)HTML";

// OpenAPI 3 spec — compact, tags by domain. ENGLISH ONLY: a spec is read by
// machines, this page carries no i18n trailer, and the response messages below
// quote the English defaults WebApi returns (sce::T(en, fr)).
inline const char OPENAPI_JSON[] PROGMEM = R"JSON({
"openapi":"3.0.0","info":{"title":"StackChan-Companion","version":"1.0.0"},
"tags":[{"name":"Expressions"},{"name":"Animations"},{"name":"Servo"},{"name":"Options"},{"name":"Bins"},{"name":"System"}],
"paths":{
"/api/status":{"get":{"tags":["System"],"summary":"Current state","responses":{"200":{"description":"emotion, frameAvgUs, frameMaxUs, uptimeS, heap, rssi, mode, ip, crt, sd, yaw, pitch, mic (state name: off|warmup|standby|active), micWait (warm-up seconds left), micL, micR (RMS, valid in the active state), micAmb (ambient floor), micEvt (counter of sharp noises), load0/load1 (CPU load pct per core), batt (battery level pct, -1 if the PMIC is unknown), chg (1 while charging), authOn (1 if Basic Auth is active), authUser (configured user name), cam (camera state: off|idle|ready|failed), camErr (init error code when failed), inaV (INA226 bus voltage V), light (ambient light pct LTR-553), heading (BMM150 magnetic heading deg), clock (NTP-synced UTC epoch, 0 = never synced), night (1 between real sunset and sunrise at the lat/lon tuning keys)"}}}},
"/api/servo/pos":{"get":{"tags":["System"],"summary":"MEASURED servo pose, as opposed to the COMMANDED one in /api/status. Diagnostic only: the SCS0009 bus is write-only in operation (a read between two WritePos leaves the servos mute), so the sample is taken by the servo task and ONLY while servos=0. Fields: valid (false = no measurement: servos enabled, or no answer), yaw, pitch (degrees, emitted only when valid), ageMs, servosOff, cmdYaw, cmdPitch (what was commanded, for comparison)","responses":{"200":{"description":"measured pose JSON"}}}},"/api/sensors":{"get":{"tags":["System"],"summary":"Telemetry of the extra K151 sensors: batt_pct, charging, ina_v (INA226 bus voltage), ina_shunt_mv, light_pct (LTR-553), heading (BMM150 heading, -1 if absent), clock (NTP-synced UTC epoch, 0 = never synced), night (1 between real sunset and sunrise at the lat/lon tuning keys — what drives sound_volume_night). Cached values (refreshed by loop, no I2C inside the callback)","responses":{"200":{"description":"sensors JSON"}}}},"/api/firmware":{"get":{"tags":["System"],"summary":"WHICH BUILD IS RUNNING. slot = OTA partition actually booted (app0/app1) - a USB flash writes one slot while otadata picks the slot, so a flash can succeed and change nothing; sha = first 8 hex of the app ELF sha256, reproducible with `sha256sum .pio/build/companion/firmware.elf`; console = digest of WebConsole.h, the same one `python scripts/build/gen_console_gz.py --check` prints — compare either to confirm the robot runs your working copy; reset = why it last started (poweron, ext, sw, panic, int_wdt, task_wdt, wdt, deepsleep, brownout, sdio). All fixed for the whole boot - read once, not polled","responses":{"200":{"description":"firmware identity JSON"}}}},
"/api/camera/still.jpg":{"get":{"tags":["System"],"summary":"JPEG snapshot (GC0308, camera=1 required) - init on demand","parameters":[{"name":"full","in":"query","schema":{"type":"number"},"description":"1 = full-quality VGA capture (cam_quality) on demand - 503 until the fresh shot is ready (retry after ~300 ms). Without full: last LIVE frame (compressed cam_stream_quality, 320x240 when cam_stream_qvga=1)"}],"responses":{"200":{"description":"image/jpeg"},"403":{"description":"camera disabled (POST /api/tuning?camera=1)"},"503":{"description":"camera starting up - retry, or camera init failed"}}}},
"/api/camera/stream":{"get":{"tags":["System"],"summary":"MJPEG stream (multipart/x-mixed-replace) for Frigate / browser (camera=1 required)","responses":{"200":{"description":"multipart/x-mixed-replace;boundary=frame"},"403":{"description":"camera disabled"},"503":{"description":"camera starting up - retry"}}}},
"/api/rules":{"get":{"tags":["System"],"summary":"The rule table AS LOADED, which is not the same thing as the contents of rules.txt: a line that fails to parse is simply absent, with no error and no log. Returns builtins (how many rules are compiled in rather than read from the card) and, per rule: en (the enable-gate field, empty = always active), f (watched field), op (gt|ge|lt|le|eq|ne), v (threshold), sus (ms the condition must hold), cd (ms cooldown), act (the action, rendered), on (the gate is satisfied RIGHT NOW), sd (came from the card)","responses":{"200":{"description":"rules JSON"}}}},"/api/clock":{"get":{"tags":["System"],"summary":"Wall clock: epoch (UTC), plausible (the epoch looks like a real date - the RTC restores one at every boot, so this proves nothing about NTP), ntp (a packet REALLY landed since boot), pending (a sync is armed and waiting), ap (AP mode, no route to a time server)","responses":{"200":{"description":"clock JSON"}}}},"/api/clock/sync":{"post":{"tags":["System"],"summary":"Force an NTP resync: restarts the SNTP client and re-arms the RTC write, which happens on the first packet that lands. Deferred to loop() (A2.6). 409 in AP mode","responses":{"202":{"description":"client restarted"},"409":{"description":"no route out in AP mode"}}}},"/api/reboot":{"post":{"tags":["System"],"summary":"Clean restart (deferred ~1 s, the response leaves first)","responses":{"202":{"description":"rebooting in ~1 s"}}}},
"/api/poweroff":{"post":{"tags":["System"],"summary":"Full shutdown through the AXP2101 PMIC (deferred ~1 s, does not restart on its own)","responses":{"202":{"description":"shutting down in ~1 s"}}}},
"/api/security":{"post":{"tags":["System"],"summary":"Basic Auth on the fly (console+API) - an empty password disables the protection - persisted on SD","parameters":[{"name":"username","in":"query","schema":{"type":"string"}},{"name":"password","in":"query","schema":{"type":"string"}}],"responses":{"200":{"description":"applied immediately"}}}},
"/api/emotion":{"post":{"tags":["Expressions"],"summary":"Emotion override (ex-TimedEmotion)","parameters":[{"name":"name","in":"query","required":true,"schema":{"type":"string"},"example":"Happy"},{"name":"ms","in":"query","schema":{"type":"integer"},"example":10000}],"responses":{"200":{"description":"ok"},"400":{"description":"the name parameter is required"},"404":{"description":"unknown emotion"}}}},
"/api/animation":{"post":{"tags":["Animations"],"summary":"blink | winkLeft | winkRight","parameters":[{"name":"name","in":"query","required":true,"schema":{"type":"string"}}],"responses":{"200":{"description":"ok"},"404":{"description":"unknown animation"}}}},
"/api/dances":{"get":{"tags":["Animations"],"summary":"List of the dances","responses":{"200":{"description":"array"}}}},
"/api/dance":{"post":{"tags":["Animations"],"summary":"Play a dance (or stop)","parameters":[{"name":"name","in":"query","required":true,"schema":{"type":"string"},"example":"happy"}],"responses":{"200":{"description":"ok"},"404":{"description":"unknown dance (GET /api/dances)"}}}},
"/api/servo":{"post":{"tags":["Servo"],"summary":"Head remote control: absolute (yaw/pitch) or relative (dyaw/dpitch), degrees","parameters":[{"name":"yaw","in":"query","schema":{"type":"number"},"example":166},{"name":"pitch","in":"query","schema":{"type":"number"},"example":93},{"name":"dyaw","in":"query","schema":{"type":"number"}},{"name":"dpitch","in":"query","schema":{"type":"number"}},{"name":"ms","in":"query","schema":{"type":"integer"},"example":400}],"responses":{"200":{"description":"current pose"},"400":{"description":"no known parameter"}}}},
"/api/config":{"post":{"tags":["Options"],"summary":"Runtime options","parameters":[{"name":"crt","in":"query","schema":{"type":"integer"}}],"responses":{"200":{"description":"ok"}}}},
"/api/config/reload":{"post":{"tags":["System"],"summary":"Re-read config.yaml from the SD (tuning on the fly, wifi at restart)","responses":{"202":{"description":"reloaded in ~1 s"},"503":{"description":"no SD card"}}}},
"/api/dances/files":{"get":{"tags":["Animations"],"summary":"SD choreographies loaded (/dances/*.csv)","responses":{"200":{"description":"[{name,keys}]"}}}},
"/api/dances/file":{"post":{"tags":["Animations"],"summary":"Upload/edit of a CSV choreography (multipart; same name = edit)","responses":{"200":{"description":"reloaded in ~1 s"},"500":{"description":"upload failed"}}},"delete":{"tags":["Animations"],"summary":"Delete a choreography","parameters":[{"name":"name","in":"query","required":true,"schema":{"type":"string"}}],"responses":{"200":{"description":"ok"},"400":{"description":"invalid name"},"404":{"description":"not found"}}}},
"/api/dances/reload":{"post":{"tags":["Animations"],"summary":"Reload /dances/*.csv","responses":{"202":{"description":"reloaded in ~1 s"},"503":{"description":"no SD card"}}}},
"/api/statusbar":{"post":{"tags":["Status band"],"summary":"Dynamic zone mode: 0=default (black, or the clock with band_clock) 2=sound (stereo mic visualiser) 3=gauges 4=timer 5=pomodoro (debug info = the band_debug option, not a mode)","parameters":[{"name":"mode","in":"query","required":true,"schema":{"type":"integer","enum":[0,2,3,4,5]}}],"responses":{"200":{"description":"ok"},"400":{"description":"invalid mode (0=default, 2=sound, 3=gauges, 4=timer, 5=pomodoro)"}}}},
"/api/field":{"post":{"tags":["Status band"],"summary":"Sets the blackboard FIELDS (band + rules). A numeric value is a float, anything else a string; the _s suffix forces a string. E.g. g0=62&g0l_s=CTX","responses":{"200":{"description":"{set:N}"},"503":{"description":"no fieldstore"}}}},
"/api/say":{"post":{"tags":["Status band"],"summary":"Ephemeral notification on the bottom band","parameters":[{"name":"text","in":"query","required":true,"schema":{"type":"string"}},{"name":"ms","in":"query","schema":{"type":"integer"},"description":"duration (default 4000)"}],"responses":{"200":{"description":"ok"},"400":{"description":"the text parameter is required"}}}},
"/api/rules/reload":{"post":{"tags":["Status band"],"summary":"Reloads the SD reactive rules (/stackchan-companion/rules.txt)","responses":{"202":{"description":"reloaded in ~1 s"},"503":{"description":"no SD card"}}}},
"/api/tuning":{"get":{"tags":["Options"],"summary":"All the parameters"},"post":{"tags":["Options"],"summary":"Write on the fly - ANY key of GET /api/tuning is accepted (sample below). Persisted on SD only when the value CHANGES.","parameters":[{"name":"vor_gain","in":"query","schema":{"type":"number"}},{"name":"leds","in":"query","schema":{"type":"number"}},{"name":"sound","in":"query","schema":{"type":"number"}},{"name":"sound_track","in":"query","schema":{"type":"number"},"description":"1 = the head turns toward the noise (stereo mics)"},{"name":"servos","in":"query","schema":{"type":"number"},"description":"0 = servos off (torque released, head limp) - the eyes carry on"},{"name":"camera","in":"query","schema":{"type":"number"},"description":"1 = /api/camera/* active (0 cuts capture + stream)"},{"name":"cam_stream_quality","in":"query","schema":{"type":"number"},"description":"JPEG quality of the stream/live view (1-63, high = more compressed/responsive)"},{"name":"cam_stream_qvga","in":"query","schema":{"type":"number"},"description":"1 = stream/view 320x240 (4x lighter); 0 = VGA (Frigate)"},{"name":"telemetry","in":"query","schema":{"type":"number"}}],"responses":{"200":{"description":"applied"}}}},
"/api/wifi":{"post":{"tags":["System"],"summary":"STA credentials (restart required)","parameters":[{"name":"ssid","in":"query","required":true,"schema":{"type":"string"}},{"name":"pass","in":"query","schema":{"type":"string"}}],"responses":{"200":{"description":"restart to apply"},"400":{"description":"the ssid parameter is required"}}}},
"/api/update":{"post":{"tags":["System"],"summary":"OTA update of the companion firmware (multipart firmware.bin) then automatic reboot","responses":{"200":{"description":"flash ok - rebooting in ~1 s"},"500":{"description":"update failed (Update.h message)"}}}},
"/api/bins":{"get":{"tags":["Bins"],"summary":"List /bins/*.bin"},"post":{"tags":["Bins"],"summary":"Multipart upload of a .bin","responses":{"200":{"description":"ok"},"500":{"description":"upload failed"}}},"delete":{"tags":["Bins"],"summary":"Delete","parameters":[{"name":"name","in":"query","required":true,"schema":{"type":"string"}}],"responses":{"200":{"description":"ok"},"400":{"description":"invalid name"},"404":{"description":"not found"}}}},
"/api/bins/launch":{"post":{"tags":["Bins"],"summary":"Flash and reboot onto a .bin","parameters":[{"name":"name","in":"query","required":true,"schema":{"type":"string"}}],"responses":{"202":{"description":"flash + reboot in ~2 s"},"404":{"description":"not found"}}}},
"/api/bins/stop":{"post":{"tags":["Bins"],"summary":"Back to the companion (no-op here; SceGuest stub in the home-made bins)","responses":{"200":{"description":"already running the companion"}}}},
"/api/sd/list":{"get":{"tags":["Import/Export"],"summary":"Lists the exportable SD files (config.yaml, rules.txt, /dances/*.csv, /bins/*.bin) with their sizes","responses":{"200":{"description":"{files:[{name,path,size,cat}]}"}}}},
"/api/sd/get":{"get":{"tags":["Import/Export"],"summary":"Downloads an SD file (attachment). WHITELISTED path.","parameters":[{"name":"path","in":"query","required":true,"schema":{"type":"string"},"description":"e.g. /stackchan-companion/rules.txt"}],"responses":{"200":{"description":"file"},"403":{"description":"path not allowed"},"404":{"description":"file not found"}}}},
"/api/sd/put":{"post":{"tags":["Import/Export"],"summary":"Imports (replaces) an SD file (multipart upload). WHITELISTED path; config/rules reloaded automatically.","parameters":[{"name":"path","in":"query","required":true,"schema":{"type":"string"}}],"responses":{"200":{"description":"ok"},"403":{"description":"path not allowed, or authentication"},"500":{"description":"import failed"}}}},
"/api/sd/delete":{"delete":{"tags":["Import/Export"],"summary":"Deletes an SD file (WHITELISTED path)","parameters":[{"name":"path","in":"query","required":true,"schema":{"type":"string"}}],"responses":{"200":{"description":"ok"},"403":{"description":"path not allowed"},"404":{"description":"file not found"}}}}
}})JSON";

} // namespace sce
