// =============================================================================
// led-fluid — liquid in a box, gravity from the tilt of the board
// =============================================================================
// A guest .bin: a particle fluid whose gravity IS the inclination of the
// CoreS3, painted as a grid of round dots — the look of an LED matrix. The
// physics panel comes in on a swipe right, the colour rectangle on a swipe
// left. Specification: docs/guests/LED-FLUID.md (Design notes).
//
// BOARD PROFILE. The defaults describe the CoreS3 inside a K151, which is the
// only board this bin is built for today. The flags exist so a Fire port stays
// a session and not a rewrite — the shape flight-radar and space already use.
#ifndef SCE_INPUT_BUTTONS
#define SCE_INPUT_BUTTONS 0     // CoreS3: a touch panel, not three buttons
#endif
#ifndef SCE_COMPANION
#define SCE_COMPANION 1         // there is a companion to go back to
#endif
// The WS2812 ring lives on the K151 BODY, behind a PY32 expander on Wire1.
// A FLAG AND NOT A PROBE, unlike the light sensor — and the difference is that
// probing costs nothing on a board without an LTR-553 while probing for the
// PY32 means calling `Wire1.begin(12, 11)` first. On a classic ESP32 such as
// the Fire, GPIO 6-11 are the SPI FLASH: pin 11 is not a free pin there, it is
// the one the program is being read from. A capability that cannot be asked
// about safely has to be declared.
#ifndef SCE_HAS_PY32
#define SCE_HAS_PY32 1          // K151 body: the twelve-LED ring
#endif
//
// THE ONE RULE THIS FILE MUST NOT BREAK (A2 rule 15). The 11/12 I2C pins are
// shared by the IMU, the touch panel, the PMIC, the LTR-553 and the PY32, and
// m5gfx::i2c is not thread-safe. This bin needs NO bus lock for one reason
// only: NO I2C TRANSACTION EVER RUNS CONCURRENTLY WITH THE SIMULATION TASK.
// That holds in two halves — setup() does talk to the bus (the LTR probe, the
// PY32 init, the first blackout) but does it BEFORE the task is created, and
// everything after that lives in loop(). The simulation task is pure
// arithmetic and must stay that way — the day somebody reads the IMU from it,
// the lock becomes mandatory and its absence becomes a crash nobody can place.
//
// TWO SURFACES, ON PURPOSE. The fluid is painted STRAIGHT to the display, cell
// by changed cell: a full-screen sprite push is ~31 ms at 40 MHz and would eat
// the frame on its own. The panels are drawn into a PSRAM sprite and pushed
// once, because they change rarely and because a sprite is what CellText — the
// project's single-drawString-call-site (A2.22) — knows how to draw into.
// =============================================================================

#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <M5StackUpdater.h>
#include <WiFi.h>
#include <atomic>

#include "../common/SdPins.h"
#include "../common/SdRoot.h"    // one-time /stackchan-eyes -> /stackchan-companion move
#include "../common/Py32Leds.h"  // the WS2812 payload, shared with the companion
#include "../common/Ltr553.h"    // ambient light: ONE calibration, four readers
#include "../common/I18n.h"      // sce::T(en, fr) — English default
#include "../common/Gesture.h"   // ONE swipe classifier, shared with the others
#include "../common/CellText.h"  // deferred text, ONE draw call site (A2.22)
#include "../common/SdWatch.h"   // the card, watched and not only mounted
#include "guest/SceGuest.h"
#include "fluid.h"
#include "look.h"
#include "panels.h"
#include "input.h"       // the interaction vocabulary, pure (test_fluidinput)

using namespace sce;

static SceGuest      guest;
static sce::CoopStop simGuard;      // guest contract: any background task
static sce::SdWatch  gSdWatch;
static bool          sdOk = false;
static String        ipStr;

// ---- what the two cores exchange -------------------------------------------
// One producer (the simulation, core 0), one consumer (the painting, core 1):
// the Brain/Renderer discipline of A2.5, in the smallest form that carries it.
// Two buffers and an index; the writer fills the spare one and publishes by
// swapping. No lock: a torn read would cost one frame of a wrong dot, and a
// mutex held across a frame would cost the frame itself.
static fluid::DotGrid   gGrid[2];
static volatile uint8_t gReady = 0;      // index of the published buffer
static volatile bool    gDirty = false;

// Input the simulation consumes, written by loop() only.
static volatile float gGx = 0.0f, gGy = 1.0f, gSwirl = 0.0f;

// THE TAP, PACKED INTO ONE WORD. Written as two floats it was a torn pair: the
// simulation gates on X alone, so waking between the two stores applied the new
// tap's X with the PREVIOUS tap's Y and splashed somewhere nobody touched. One
// atomic carries both coordinates (x+1 and y+1, so zero can mean "nothing
// pending"), and the consumer takes it with an exchange — which also makes
// "consumed" and "cleared" the same indivisible step.
static std::atomic<uint32_t> gImpulse{0};

static inline void postImpulse(int x, int y) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    gImpulse.store((uint32_t)(((x + 1) << 16) | ((y + 1) & 0xFFFF)),
                   std::memory_order_release);
}

// ---- the live settings -----------------------------------------------------
// Every one of them is a row of ui::params(), and paramValue/paramApply below
// are the ONLY way in and out. That is what keeps the panel, the card and
// /config from ever holding three opinions.
static fluid::Params gParams;
// PUBLISHED, not merely written. `gParams` is a plain struct that loop() edits
// and the simulation task on the other core reads: nothing inside that task
// writes it, so a compiler is entitled to hoist the reads out of the task's
// endless loop and the fluid would simply never notice a slider moving —
// silently, and only in an optimised build. The counter is what turns a write
// into a publication: the task loads it with acquire on every tick (a load it
// is not allowed to elide), and only then copies the struct.
static std::atomic<uint32_t> gParamsGen{0};

static uint16_t      gHue    = 200;   // 0-359
static uint8_t       gSatMax = 85;    // 0-100, the ceiling
static uint8_t       gBright = 120;   // 10-255, the panel
static uint8_t       gTrail  = 60;    // 0-95 %
static bool          gLeds = false, gSound = false, gAutoBright = false;
// ALL THREE START FALSE, and that is a statement about the UI as much as the
// physics: a correct robot must not open its settings claiming an axis has been
// inverted. The measured mapping is baked into the baseline where the vector is
// read (see loop()), so "no departure" and "correct" are the same state.
//
// Getting there cost two wrong guesses, both wrong for the same reason: they
// were reasoned from the datasheet's axis drawing and confirmed against a report
// taken from a build that never called `M5.Imu.update()`. Its gravity vector was
// frozen garbage, so the edge the fluid had chosen said nothing about any sign.
// A wrong-way report is only evidence once the thing it describes is actually
// being read; until then it is two unknowns wearing one symptom.
static bool          gTiltSwap = false, gTiltInvX = false, gTiltInvY = false;

// Painting state: the key each cell was last painted with. This array is the
// whole reason a frame fits — a cell whose key is unchanged is not drawn.
static uint16_t gPainted[fluid::MAX_CELLS];
static int      gPaintedCols = 0, gPaintedRows = 0, gPaintedPitch = 0;
static int      gPaintCursor = 0;      // where the budgeted sweep resumes

// The consumer's OWN copy of the published frame. Two buffers are not enough on
// their own: the producer flips every 16 ms, so after two flips it is writing
// the very buffer this side is still reading. That is not a corner case — a
// return from a panel invalidates every cell, and the full repaint that follows
// takes longer than two producer periods. Copying costs ~2.4 KB of memcpy,
// microseconds, and the publication index is re-checked afterwards: a flip
// during the copy costs one retry, never a torn picture.
static fluid::DotGrid gPaintCopy;

static void snapshotGrid() {
    for (int attempt = 0; attempt < 2; attempt++) {
        const uint8_t idx = gReady;
        gPaintCopy = gGrid[idx];
        if (gReady == idx) return;      // no flip during the copy: it is clean
    }
}

// HOW MANY DOTS ONE FRAME MAY DRAW. Measured on hardware, a full 1200-cell
// repaint took ~112 ms — a third of a second of frozen touch and dark HTTP
// every time somebody closed a panel, which is the same uncapped-burst shape
// A2.22 already cost this project once. At roughly 67 us per circle, 256 dots
// is about 17 ms: the steady-state fluid never reaches the cap, and a full
// repaint spreads over five frames instead of blocking one.
inline constexpr int MAX_DOTS_PER_FRAME = 256;

// ---- the panels ------------------------------------------------------------
// `View` lives in input.h now: the pure half has to be able to name a screen to
// say what a button means on it.
using flu::View;
static View      gView = View::Fluid;
static uint8_t   gTab  = 0;             // 0 = PHYSICS, 1 = RENDER
#if SCE_INPUT_BUTTONS
// The cursor a button build points with. Declared up here beside the tab it
// walks, because the panel has to DRAW it: a highlight is the only thing that
// tells somebody holding a Fire which row A and C are about to change, and
// without it the three buttons act on an invisible selection.
static flu::Cursor gCursor;
#endif
static bool      gPanelDirty = true;    // drawn on change, never per frame
static M5Canvas  gPanel(&M5.Display);
static bool      gPanelReady = false;
static ui::CellText gCell;

// The panel surface is 150 KB of PSRAM, and it is RELEASED when the bin hands
// the robot back — `updateFromFS` wants every byte it can get while it writes
// 1.4 MB of flash. Which is why creating it is a FUNCTION and not a line in
// setup(): a reflash that fails releases the guard and the guest keeps running,
// so the next panel opening has to be able to build its surface again. A one-
// shot creation would have left that path drawing into a freed sprite.
static bool ensurePanel() {
    if (gPanelReady) return true;
    gPanel.setColorDepth(16);
    gPanel.setPsram(true);
    gPanelReady = (gPanel.createSprite(ui::SCR_W, ui::SCR_H) != nullptr);
    return gPanelReady;
}

static void releasePanel() {
    if (!gPanelReady) return;
    gPanel.deleteSprite();
    gPanelReady = false;
}

// Declared here because loop() routes to them before they are defined — a
// single translation unit still needs the declaration first.
static void handleSettingsTap(int x, int y);
static void handleSettingsDrag(int x, int y);
static void handleColourDrag(int x, int y);
static int  paramValue(const ui::Param& p);
static void paramApply(const ui::Param& p, int v);

// Deferred SD write. The card shares SPI2 with the display, so the write
// happens in loop() and never inside a touch handler or an HTTP callback.
static bool     gSaveDue = false;
static uint32_t gSaveNextMs = 0;

// The PY32 expander, named rather than spelled out at the call site. Same
// registers as hal/Py32Expander.h — the ADDRESSES are hardware, the payload
// arithmetic is what is shared (firmware/common/Py32Leds.h).
static constexpr uint8_t PY32_ADDR        = 0x6F;
static constexpr uint8_t PY32_REG_LED_CFG = 0x24;   // bits 0-5 count, bit 6 refresh
static constexpr uint8_t PY32_REG_LED_RAM = 0x30;   // 2 bytes/LED, RGB565 LE
static constexpr uint8_t PY32_LED_REFRESH = 0x40;
static constexpr uint8_t PY32_LED_PIN_BIT = 0x20;   // GPIO 13 = bit 5 of the _H regs

// Panel colours. Kept together so check-contrast.py has one place to read.
static constexpr uint16_t COL_BG    = 0x0000;
static constexpr uint16_t COL_INK   = 0xFFFF;
static constexpr uint16_t COL_DIM   = 0x8410;
static constexpr uint16_t COL_ACCENT= 0x04FF;
static constexpr uint16_t COL_TRACK = 0x4208;
static constexpr uint16_t COL_CHIP  = 0x2124;

// =============================================================================
// The simulation task — core 0, and NOT ONE bus transaction
// =============================================================================
static void simTask(void*) {
    // STATIC, and not a local. A Sim carries eight float arrays of MAX_PARTS
    // plus the neighbour grid — about 21 KB. On the task stack that is a smash
    // waiting for the first step; in .bss it is a number you can read in the
    // build output. There is exactly one simulation, so a single instance is
    // also the honest description.
    static fluid::Sim sim;
    sim.setParams(gParams);
    sim.reset(20260818u);
    fluid::Params applied = gParams;
    uint32_t seenGen = gParamsGen.load(std::memory_order_acquire);
    const TickType_t period = pdMS_TO_TICKS(16);   // 60 Hz, fixed dt
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        if (simGuard.shouldPark()) continue;       // reflash: park (ACK inside)

        // A parameter change is adopted here and not written from loop(): the
        // solver owns its own state, and a particle count changed mid-step
        // would index an array that has not been refilled. The generation is
        // read FIRST and with acquire — it is the only load the compiler is
        // obliged to perform every tick, and therefore the only thing that
        // makes the copy below happen at all.
        const uint32_t gen = gParamsGen.load(std::memory_order_acquire);
        if (gen != seenGen) {
            seenGen = gen;
            const bool reseed = (applied.particles != gParams.particles ||
                                 applied.pitch     != gParams.pitch);
            applied = gParams;
            sim.setParams(applied);
            if (reseed) sim.reset(20260818u);
            // The ONLY evidence, from outside, that a slider reached the
            // solver. The screen shows a fluid that looks different, which is
            // not the same as knowing the value crossed the core boundary —
            // and crossing it is exactly what the generation counter above is
            // for. Human-paced, so it cannot flood the console.
            Serial.printf("[led-fluid] parametres adoptes : visc %d grav %d "
                          "rebond %d gyro %d part %d pas %d%s\n",
                          (int)applied.viscosity, (int)applied.gravity,
                          (int)applied.bounce, (int)applied.gyroGain,
                          applied.particles, applied.pitch,
                          reseed ? " (fluide reinitialise)" : "");
        }

        const uint32_t imp = gImpulse.exchange(0, std::memory_order_acquire);
        if (imp) sim.impulse((float)((imp >> 16) - 1), (float)((imp & 0xFFFF) - 1), 420.0f);
        sim.step(gGx, gGy, gSwirl, 1.0f / 60.0f);

        const uint8_t spare = (uint8_t)(gReady ^ 1);
        sim.splat(gGrid[spare]);
        gReady = spare;
        gDirty = true;

        // YIELD UNCONDITIONALLY, and this is not belt-and-braces. On its own,
        // vTaskDelayUntil blocks only while the deadline is still ahead: the
        // first step that outruns its period returns from it AT ONCE, and
        // since `last` keeps being advanced towards a deadline already in the
        // past, it never blocks again. The task then spins forever, IDLE0 is
        // never scheduled, and the task watchdog aborts the chip — measured on
        // hardware as a boot loop at 20.5 s uptime, `CPU 0: fluid-sim`.
        // Re-syncing `last` on an overrun is what stops one late step from
        // becoming permanent starvation; the one-tick floor is what guarantees
        // the idle task runs even then. A dropped frame is a visual cost; a
        // starved core is a dead robot.
        const TickType_t now = xTaskGetTickCount();
        if ((int32_t)(last + period - now) <= 0) {
            last = now;
            vTaskDelay(1);
        } else {
            vTaskDelayUntil(&last, period);
        }
    }
}

// =============================================================================
// Painting the fluid — ONE draw call site for the dots (A2.22)
// =============================================================================
// Written as a single fillCircle inside one loop, deliberately. Two similar
// draw calls in one body is the shape GCC 8.4 Xtensa is entitled to thin out,
// and half the fluid would vanish in the binary while the source looked right.
__attribute__((noinline)) static void paintGrid(const fluid::DotGrid& g) {
    if (g.cols != gPaintedCols || g.rows != gPaintedRows ||
        gPaintedPitch != gParams.pitch) {
        M5.Display.fillScreen(COL_BG);
        for (int i = 0; i < fluid::MAX_CELLS; i++) gPainted[i] = 0xFFFF;
        gPaintedCols = g.cols;  gPaintedRows = g.rows;
        gPaintedPitch = gParams.pitch;
    }
    const int pitch = gParams.pitch;
    const int rad   = pitch / 2 - 1;
    const int fade  = 255 - (int)gTrail * 255 / 100;   // trail: how fast a cell dies
    const int cells = g.cols * g.rows;
    if (cells <= 0) return;
    if (gPaintCursor >= cells) gPaintCursor = 0;

    // The sweep RESUMES where it stopped rather than restarting: capped at the
    // top of the grid every frame, the bottom rows of a full repaint would
    // never get their turn while the top keeps changing.
    int budget = MAX_DOTS_PER_FRAME;
    int examined = 0;
    while (examined < cells && budget > 0) {
        int i = gPaintCursor + examined;
        if (i >= cells) i -= cells;
        examined++;
        const int r = i / g.cols;
        const int c = i - r * g.cols;
        // The trail lives here and not in the solver: it is a property of
        // the PICTURE, and putting it in the physics would make a thick
        // fluid and a long trail the same slider.
        uint8_t d = g.dens[i];
        const uint16_t old = gPainted[i];
        const uint8_t oldD = (uint8_t)((old == 0xFFFF ? 0 : old >> 5) << 3);
        if (d < oldD) {
            const int decayed = oldD - fade;
            if (decayed > d) d = (uint8_t)decayed;
        }
        const uint16_t key = look::dotKey(d, g.spd[i]);
        if (key == old) continue;
        gPainted[i] = key;
        budget--;
        M5.Display.fillCircle(c * pitch + pitch / 2, r * pitch + pitch / 2,
                              rad, look::keyToRgb565(key, gHue, gSatMax));
    }
    gPaintCursor += examined;
    if (gPaintCursor >= cells) gPaintCursor -= cells;
}

// =============================================================================
// The panels — each primitive gets its OWN noinline body (A2.22)
// =============================================================================
// Two fillRoundRect in one function is the exact shape GCC is entitled to thin
// out, and the tab strip, the toggles and the preset chips all want one. So
// they are three functions, each with a single call site inside a loop, each
// `noinline` so the compiler cannot fold them back together and recreate the
// problem the split exists to avoid.

__attribute__((noinline)) static void drawTabStrip() {
    for (int t = 0; t < 2; t++)
        gPanel.fillRoundRect(8 + t * 152, 4, 146, ui::TAB_H - 8, 4,
                             t == gTab ? COL_ACCENT : COL_CHIP);
    gCell.put(8 + 40,   9, gTab == 0 ? COL_BG : COL_INK,
              sce::T("PHYSICS", "PHYSIQUE"));
    gCell.put(160 + 46, 9, gTab == 1 ? COL_BG : COL_INK,
              sce::T("RENDER", "RENDU"));
}

// Tab 0 is all sliders, tab 1 is all toggles — which is why each tab gets its
// own body rather than one loop with a branch: the branch would put a
// fillRoundRect and a fillCircle in the same function again.
__attribute__((noinline)) static void drawSliderRows() {
    int slot = 0;
    for (int i = 0; i < ui::paramCount(); i++) {
        const ui::Param& p = ui::params()[i];
        if (p.tab != 0) continue;
        const ui::Rect r = ui::rowRect(slot++);
        const int mid = r.y + r.h / 2;
        gPanel.drawFastHLine(ui::SLIDER_X0, mid, ui::SLIDER_X1 - ui::SLIDER_X0, COL_TRACK);
        gPanel.fillCircle(ui::sliderX(p, paramValue(p)), mid, 6, COL_ACCENT);
        char buf[12];
        snprintf(buf, sizeof(buf), "%d", paramValue(p));
        gCell.put(r.x + 4, r.y + 8, COL_INK, sce::T(p.en, p.fr));
        gCell.putR(ui::SLIDER_X0 - 8, r.y + 8, COL_DIM, buf);
    }
}

__attribute__((noinline)) static void drawToggleRows() {
    int slot = 0;
    for (int i = 0; i < ui::paramCount(); i++) {
        const ui::Param& p = ui::params()[i];
        if (p.tab != 1) continue;
        const ui::Rect r = ui::rowRect(slot++);
        gPanel.fillRoundRect(ui::SLIDER_X1 - 46, r.y + 3, 46, r.h - 6, 4,
                             paramValue(p) ? COL_ACCENT : COL_CHIP);
        gCell.put(r.x + 4, r.y + 8, COL_INK, sce::T(p.en, p.fr));
        gCell.put(ui::SLIDER_X1 - 36, r.y + 8,
                  paramValue(p) ? COL_BG : COL_DIM,
                  paramValue(p) ? sce::T("ON", "ON") : sce::T("OFF", "OFF"));
    }
}

__attribute__((noinline)) static void drawPresetChips() {
    for (int i = 0; i < look::presetCount(); i++)
        gPanel.fillRoundRect(ui::PRESET_X0 + i * ui::PRESET_STEP, ui::PRESET_Y,
                             ui::PRESET_W, ui::PRESET_H, 4, COL_CHIP);
    for (int i = 0; i < look::presetCount(); i++)
        gCell.put(ui::PRESET_X0 + 4 + i * ui::PRESET_STEP, ui::PRESET_Y + 7,
                  COL_INK, sce::T(look::presets()[i].en, look::presets()[i].fr));
}

static void drawSettings() {
    if (!ensurePanel()) return;
    gPanel.fillScreen(COL_BG);
    gCell.reset();
    drawTabStrip();
    if (gTab == 0) { drawSliderRows(); drawPresetChips(); }
    else           { drawToggleRows(); }
#if SCE_INPUT_BUTTONS
    // The selection, on a board that has one. Drawn AFTER the rows so it frames
    // them rather than being painted over, and as an outline so the row's own
    // value stays readable underneath.
    const ui::Rect cr = ui::rowRect(gCursor.row);
    gPanel.drawRoundRect(cr.x - 3, cr.y - 2, cr.w + 6, cr.h + 4, 4, COL_ACCENT);
#endif
    gCell.flush(gPanel);
    gPanel.pushSprite(0, 0);
}

// ---- the colour rectangle --------------------------------------------------
// Drawn ONCE per opening: a gradient recomputed every frame would spend the
// budget on a picture identical to the one already on the glass.
__attribute__((noinline)) static void drawHueRect() {
    const ui::Rect r = ui::hueRect();
    for (int x = 0; x < r.w; x++)
        for (int y = 0; y < r.h; y += 3)
            gPanel.fillRect(r.x + x, r.y + y, 1, 3,
                            look::keyToRgb565(look::dotKey(255, 190),
                                              (uint16_t)ui::hueAt(r.x + x),
                                              (uint8_t)ui::satAt(r.y + y)));
}

// TWO radii through ONE call site, for the reason A2.22 exists: written as two
// drawCircle calls in this body, the compiler may keep only one and the ring
// loses the outline that makes it readable over a bright gradient.
__attribute__((noinline)) static void drawCrosshair() {
    const ui::Rect r = ui::hueRect();
    const int cx = r.x + (int)((long)gHue * (r.w - 1) / 359);
    const int cy = r.y + (int)((long)(100 - gSatMax) * (r.h - 1) / 100);
    const uint16_t cols[2] = { COL_INK, COL_BG };
    for (int i = 0; i < 2; i++) gPanel.drawCircle(cx, cy, 7 + i, cols[i]);
}

__attribute__((noinline)) static void drawBrightBar() {
    const ui::Param& bp = ui::params()[ui::paramIndex("bright")];
    gPanel.drawFastHLine(ui::BRI_X0, ui::BRI_Y, ui::BRI_X1 - ui::BRI_X0, COL_TRACK);
    gPanel.fillCircle(ui::brightnessX(bp, gBright), ui::BRI_Y, 8, COL_ACCENT);
}

// The swatches show what a dot will actually look like, from empty to packed,
// at the hue and ceiling just picked. It is the only honest preview: the
// rectangle above shows the CHOICE, this row shows the CONSEQUENCE.
__attribute__((noinline)) static void drawSwatches() {
    for (int i = 0; i < 6; i++)
        gPanel.fillCircle(30 + i * 52, 224, 9,
                          look::keyToRgb565(look::dotKey((uint8_t)(40 + i * 43), 170),
                                            gHue, gSatMax));
}

static void drawColour() {
    if (!ensurePanel()) return;
    gPanel.fillScreen(COL_BG);
    gCell.reset();
    char buf[28];
    snprintf(buf, sizeof(buf), "%s %d  /  %s %d%%",
             sce::T("HUE", "TEINTE"), (int)gHue,
             sce::T("SAT", "SAT"), (int)gSatMax);
    gCell.put(16, 14, COL_INK, buf);
    snprintf(buf, sizeof(buf), "%s %d", sce::T("BRIGHTNESS", "LUMINOSITE"), (int)gBright);
    gCell.put(16, 172, COL_DIM, buf);
    drawHueRect();
    drawCrosshair();
    drawBrightBar();
    drawSwatches();
    gCell.flush(gPanel);
    gPanel.pushSprite(0, 0);
}

// ---- switching views -------------------------------------------------------
// SceGuest owns the downward swipe past gesture::EXIT_PX: that is how any guest
// bin is left. While a panel is OPEN the exit is switched off entirely —
// otherwise a slider dragged downwards reflashes the robot, which is the most
// expensive misread a panel can produce.
static void setView(View v) {
    gView = v;
    gPanelDirty = true;
    guest.setSwipeExit(SCE_COMPANION != 0 && v == View::Fluid);
    if (v == View::Fluid) {
        // Coming back to the fluid, every cell must be repainted: the panel
        // covered them and gPainted still claims they show their old colour.
        M5.Display.fillScreen(COL_BG);
        for (int i = 0; i < fluid::MAX_CELLS; i++) gPainted[i] = 0xFFFF;
        gSaveDue = true;
        gSaveNextMs = millis() + 400;
    }
}

// ---- the settings, in and out ----------------------------------------------
// The four lists live in ONE table (ui::params()), and these two functions are
// the only door between that table and the running state. check-guest-config.py
// verifies the round trip; writing it this way is what makes the check boring.
static int paramValue(const ui::Param& p) {
    const char* k = p.key;
    if (!strcmp(k, "particles"))   return gParams.particles;
    if (!strcmp(k, "dot_pitch"))   return gParams.pitch;
    if (!strcmp(k, "viscosity"))   return (int)gParams.viscosity;
    if (!strcmp(k, "gravity"))     return (int)gParams.gravity;
    if (!strcmp(k, "bounce"))      return (int)gParams.bounce;
    if (!strcmp(k, "trail"))       return gTrail;
    if (!strcmp(k, "gyro_gain"))   return (int)gParams.gyroGain;
    if (!strcmp(k, "hue"))         return gHue;
    if (!strcmp(k, "sat"))         return gSatMax;
    if (!strcmp(k, "bright"))      return gBright;
    if (!strcmp(k, "leds"))        return gLeds ? 1 : 0;
    if (!strcmp(k, "sound"))       return gSound ? 1 : 0;
    if (!strcmp(k, "auto_bright")) return gAutoBright ? 1 : 0;
    if (!strcmp(k, "tilt_swap"))    return gTiltSwap ? 1 : 0;
    if (!strcmp(k, "tilt_inv_x"))   return gTiltInvX ? 1 : 0;
    if (!strcmp(k, "tilt_inv_y"))   return gTiltInvY ? 1 : 0;
    return p.def;
}

static void paramApply(const ui::Param& p, int v) {
    const char* k = p.key;
    if      (!strcmp(k, "particles"))   gParams.particles = v;
    else if (!strcmp(k, "dot_pitch"))   gParams.pitch     = v;
    else if (!strcmp(k, "viscosity"))   gParams.viscosity = (float)v;
    else if (!strcmp(k, "gravity"))     gParams.gravity   = (float)v;
    else if (!strcmp(k, "bounce"))      gParams.bounce    = (float)v;
    else if (!strcmp(k, "trail"))       gTrail            = (uint8_t)v;
    else if (!strcmp(k, "gyro_gain"))   gParams.gyroGain  = (float)v;
    else if (!strcmp(k, "hue"))         gHue              = (uint16_t)v;
    else if (!strcmp(k, "sat"))         gSatMax           = (uint8_t)v;
    else if (!strcmp(k, "bright"))    { gBright = (uint8_t)v; M5.Display.setBrightness(gBright); }
    else if (!strcmp(k, "leds"))        gLeds       = v != 0;
    else if (!strcmp(k, "sound"))       gSound      = v != 0;
    else if (!strcmp(k, "auto_bright")) gAutoBright = v != 0;
    else if (!strcmp(k, "tilt_swap"))    gTiltSwap    = v != 0;
    else if (!strcmp(k, "tilt_inv_x"))   gTiltInvX    = v != 0;
    else if (!strcmp(k, "tilt_inv_y"))   gTiltInvY    = v != 0;
    // PUBLISH, every time. Bumping the counter after the field is written is
    // what tells the simulation task on the other core that there is something
    // new to read — see gParamsGen. Bumping it for keys the solver does not
    // consume costs one struct copy and keeps the rule "one write, one
    // publication" true without a table of exceptions to get wrong.
    gParamsGen.fetch_add(1, std::memory_order_release);
}

// =============================================================================
// The card — and why the round trip is generated from ONE table
// =============================================================================
// saveConfig TRUNCATES the file and rewrites it from its own list. A key that
// loadConfig reads and saveConfig does not write is not merely unpersisted, it
// is DESTROYED on the first save made from the web page — including when it had
// been typed on the card by hand. Generating both directions from ui::params()
// is how this bin cannot have that bug in the first place.
static const char* CFG_PATH = "/stackchan-companion/ledfluid.yaml";

static void loadConfig() {
    // Idempotent and cheap (two SD.exists()) — called here rather than at
    // each mount call site so a card inserted mid-session (retry, SdWatch
    // reinsertion) is covered the same as the initial boot mount.
    sce::migrateSdRoot();
    SceGuest::yamlForEach(CFG_PATH, /*sectioned=*/false,
        [](void*, const char*, const char* key, const char* val) {
            const int i = ui::paramIndex(key);
            if (i < 0) return;                       // unknown key: ignored
            const ui::Param& p = ui::params()[i];
            int v;
            // A CHECKBOX IS NOT ALWAYS "1". SceGuest writes an explicit
            // value='1', but a hand-edited card may well say "on" or "true",
            // and a bare atoi() reads both as 0 — the toggle somebody meant to
            // enable comes back disabled, silently.
            if (p.kind == ui::Kind::Bool)
                v = (!strcmp(val, "1") || !strcasecmp(val, "on") ||
                     !strcasecmp(val, "true") || !strcasecmp(val, "yes")) ? 1 : 0;
            else
                v = atoi(val);
            if (v < p.lo) v = p.lo;
            if (v > p.hi) v = p.hi;
            paramApply(p, v);
        }, nullptr);
}

static bool saveConfig() {
    if (!sdOk) return false;
    SD.mkdir("/stackchan-companion");
    File f = SD.open(CFG_PATH, FILE_WRITE);
    if (!f) return false;
    f.println("# led-fluid - ecrit par le bin ; chaque cle vient de ui::params()");
    for (int i = 0; i < ui::paramCount(); i++) {
        const ui::Param& p = ui::params()[i];
        f.printf("%s: %d\n", p.key, paramValue(p));
    }
    f.close();
    return true;
}

// ---- /config: the same table again, so the page cannot drift from the panel
static void setupSettingsPage() {
    for (int i = 0; i < ui::paramCount(); i++) {
        const ui::Param& p = ui::params()[i];
        guest.addSetting(p.key, sce::T(p.en, p.fr),
                         p.kind == ui::Kind::Bool ? SceGuest::Bool : SceGuest::Num,
                         p.lo, p.hi);
    }
    guest.settingGet = [](const char* k) -> String {
        const int i = ui::paramIndex(k);
        return i < 0 ? String("") : String(paramValue(ui::params()[i]));
    };
    guest.settingSet = [](const char* k, const String& v) {
        const int i = ui::paramIndex(k);
        if (i < 0) return;
        const ui::Param& p = ui::params()[i];
        int n = (p.kind == ui::Kind::Bool)
                    ? ((v == "1" || v.equalsIgnoreCase("on")) ? 1 : 0)
                    : v.toInt();
        if (n < p.lo) n = p.lo;
        if (n > p.hi) n = p.hi;
        paramApply(p, n);
    };
    // Synchronous, like `space` and unlike the radar: the write is short, it
    // holds no lock, and the simulation runs on the other core — so the page
    // can be told the truth instead of "accepted".
    guest.onSettingsSaved = []() -> bool {
        const bool ok = saveConfig();
        gPanelDirty = true;                 // the panel may be showing old values
        return ok;
    };
}

// ---- touch handlers --------------------------------------------------------
static void handleSettingsTap(int x, int y) {
    if (y < ui::TAB_H) { gTab = (uint8_t)(x < 160 ? 0 : 1); gPanelDirty = true; return; }
    if (gTab == 0) {
        const int pi = ui::presetAt(x, y);
        if (pi >= 0 && pi < look::presetCount()) {
            const look::Preset& pr = look::presets()[pi];
            gParams.viscosity = pr.viscosity;
            gParams.gravity   = pr.gravity;
            gParams.bounce    = pr.bounce;
            gTrail            = pr.trail;
            gPanelDirty = true;
            return;
        }
    }
    const int slot = ui::rowAt(y);
    if (slot < 0) return;
    int seen = 0;
    for (int i = 0; i < ui::paramCount(); i++) {
        const ui::Param& p = ui::params()[i];
        if (p.tab != gTab) continue;
        if (seen++ != slot) continue;
        if (p.kind == ui::Kind::Bool) { paramApply(p, paramValue(p) ? 0 : 1); gPanelDirty = true; }
        return;
    }
}

// A finger dragged on a slider: the value follows it, and only the panel is
// redrawn — the fluid behind keeps running on the other core.
static void handleSettingsDrag(int x, int y) {
    if (gTab != 0 || x < ui::SLIDER_X0 - 12) return;
    const int slot = ui::rowAt(y);
    if (slot < 0) return;
    int seen = 0;
    for (int i = 0; i < ui::paramCount(); i++) {
        const ui::Param& p = ui::params()[i];
        if (p.tab != 0) continue;
        if (seen++ != slot) continue;
        const int v = ui::valueAt(p, x);
        if (v != paramValue(p)) { paramApply(p, v); gPanelDirty = true; }
        return;
    }
}

// ---- THE SINGLE CONSUMER ---------------------------------------------------
// Both producers — the touch panel and, on a board that has them, the three
// buttons — end here. Everything a control can do to this bin is on this page,
// which is the point: the alternative is the same decisions spread through two
// backends that then stop agreeing about what a panel is.
//
// The cursor exists only for the button build. A finger POINTS at the row it
// wants, so the touch path never needed one, and carrying it on a CoreS3 would
// mean drawing a highlight nobody can move.
#if SCE_INPUT_BUTTONS
// The parameter the cursor is on, or nullptr if the tab is empty.
static const ui::Param* cursorParam() {
    const int i = flu::paramOnTab(gCursor.tab, gCursor.row);
    return i < 0 ? nullptr : &ui::params()[i];
}

// One button step, in the units the widget itself has. A slider is 155 px wide
// and `unitsPerPixel` is what one of those pixels is worth, so a press moves the
// value by exactly as much as the smallest finger movement would: the two
// backends land on the same numbers instead of one of them being able to reach
// values the other cannot.
static void nudgeCursor(int dir) {
    const ui::Param* p = cursorParam();
    if (!p) return;
    if (p->kind == ui::Kind::Bool) {
        paramApply(*p, paramValue(*p) ? 0 : 1);
    } else {
        int v = paramValue(*p) + dir * ui::unitsPerPixel(*p);
        if (v < p->lo) v = p->lo;
        if (v > p->hi) v = p->hi;
        paramApply(*p, v);
    }
    gPanelDirty = true;
}
#endif

static void applyEvent(flu::UiEvent e) {
    using flu::UiEvent;
    switch (e) {
        case UiEvent::OpenSettings: setView(View::Settings); break;
        case UiEvent::OpenColour:   setView(View::Colour);   break;
        case UiEvent::Back:
            if (gView != View::Fluid) setView(View::Fluid);
            break;
#if SCE_INPUT_BUTTONS
        case UiEvent::Prev:
        case UiEvent::Next: {
            const int dir = (e == UiEvent::Next) ? +1 : -1;
            if (gView == View::Settings) {
                nudgeCursor(dir);
            } else if (gView == View::Colour) {
                // Hue wraps: it is a circle, and stopping at red would be the
                // one place the widget disagreed with the thing it draws.
                gHue = (uint16_t)((gHue + dir * 8 + 360) % 360);
                gPanelDirty = true;
            }
            break;
        }
        case UiEvent::Act:
            if (gView == View::Fluid) {
                postImpulse(fluid::WORLD_W / 2, fluid::WORLD_H / 2);
            } else if (gView == View::Settings) {
                gCursor = flu::nextRow(gCursor);
                gTab = gCursor.tab;
                gPanelDirty = true;
            } else {
                gSatMax = (uint8_t)(gSatMax >= 100 ? 0 : gSatMax + 20);
                gPanelDirty = true;
            }
            break;
#else
        // A touch build reaches its values by pointing at them; these three
        // exist so the vocabulary is one enum on both boards, and doing nothing
        // here is the honest implementation of "this board has no buttons".
        case UiEvent::Prev:
        case UiEvent::Next:
        case UiEvent::Act:
            break;
#endif
        default: break;
    }
}

static void handleColourDrag(int x, int y) {
    const ui::Rect r = ui::hueRect();
    if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
        gHue    = (uint16_t)ui::hueAt(x);
        gSatMax = (uint8_t)ui::satAt(y);
        gPanelDirty = true;
    } else if (ui::inBrightness(y)) {
        const ui::Param& bp = ui::params()[ui::paramIndex("bright")];
        const int v = ui::brightnessAt(bp, x);
        if (v != gBright) {
            gBright = (uint8_t)v;
            M5.Display.setBrightness(gBright);   // I2C PMIC — loop() only (rule 15)
            gPanelDirty = true;
        }
    }
}

// =============================================================================
// The three optional automations — all OFF by default, like the other bins
// =============================================================================
// The ring echoes the fluid: twelve angular sectors across the grid, each
// taking its brightest cell. Rate-limited to 20 Hz — the write is cheap but it
// sits on the SAME pins as the touch panel (rule 15), and a per-frame write
// buys nothing an eye can see on twelve LEDs.
static sce::ltr553::Lite gLtr;
static bool              gLtrOk = false;
static bool              gPy32Ok = false;

// Push twelve colours and refresh. The ONE place that talks to the LED RAM,
// so "light them" and "put them out" cannot drift apart.
static bool writeLeds(const uint16_t colors[sce::py32::LED_COUNT], uint8_t* ramErr,
                      uint8_t* cfgErr) {
    uint8_t payload[sce::py32::PAYLOAD_LEN];
    sce::py32::buildPayload(colors, payload);
    // No bus lock, and that is not an oversight: every I2C transaction in this
    // bin happens in loop(), so there is no second task to race with. The
    // companion's copy of this write DOES take the lock, because its Brain
    // reads the IMU from another task — same payload, different context.
    Wire1.beginTransmission(PY32_ADDR);
    Wire1.write(PY32_REG_LED_RAM);
    Wire1.write(payload, sizeof(payload));
    const uint8_t re = Wire1.endTransmission();
    uint8_t ce = 0xFF;
    if (re == 0) {
        Wire1.beginTransmission(PY32_ADDR);
        Wire1.write(PY32_REG_LED_CFG);
        Wire1.write((uint8_t)(sce::py32::LED_COUNT | PY32_LED_REFRESH));
        ce = Wire1.endTransmission();
    }
    if (ramErr) *ramErr = re;
    if (cfgErr) *cfgErr = ce;
    return re == 0 && ce == 0;
}

// PUT THEM OUT, and say why this exists. A WS2812 latches: it keeps the last
// colour it was given until something writes another one, across a reboot and
// across a reflash. So a bin that simply stops writing leaves twelve lit LEDs
// on the robot forever — which is exactly what happened when this bin handed
// control back to the companion, and again when the option was switched off
// while the fluid was still running. Neither is an LED bug; both are this
// function not being called.
static void ledsOff() {
    if (!gPy32Ok) return;
    uint16_t black[sce::py32::LED_COUNT] = {0};
    writeLeds(black, nullptr, nullptr);
}

__attribute__((noinline)) static void echoLeds(const fluid::DotGrid& g) {
    static uint32_t last = 0;
    // The 1 -> 0 edge has to be ACTED ON, not merely obeyed: returning early
    // on !gLeds leaves whatever was last written lit on the robot.
    static bool wasOn = false;
    if (!gLeds || !gPy32Ok) {
        if (wasOn) { wasOn = false; ledsOff(); }
        return;
    }
    wasOn = true;
    if (g.cols == 0) return;
    if (millis() - last < 50) return;
    last = millis();

    uint16_t colors[sce::py32::LED_COUNT];
    for (int s = 0; s < sce::py32::LED_COUNT; s++) {
        const int c0 = s * g.cols / sce::py32::LED_COUNT;
        const int c1 = (s + 1) * g.cols / sce::py32::LED_COUNT;
        uint8_t bestD = 0, bestS = 0;
        for (int c = c0; c < c1; c++)
            for (int r = 0; r < g.rows; r++) {
                const int i = r * g.cols + c;
                if (g.dens[i] > bestD) { bestD = g.dens[i]; bestS = g.spd[i]; }
            }
        // Through the SAME colour mapping as the dots, so the ring and the
        // screen cannot disagree about what the fluid looks like.
        const uint16_t rgb = look::keyToRgb565(look::dotKey(bestD, bestS), gHue, gSatMax);
        colors[s] = sce::py32::scale565((uint8_t)((rgb >> 11) << 3),
                                        (uint8_t)(((rgb >> 5) & 0x3F) << 2),
                                        (uint8_t)((rgb & 0x1F) << 3), 255);
    }
    uint8_t ramErr = 0xFF, cfgErr = 0xFF;
    writeLeds(colors, &ramErr, &cfgErr);
    // ONCE, and only once. Twelve LEDs on the far side of an expander are the
    // one output nobody can check from a log or a screenshot: either somebody
    // is looking at the robot, or this line is the whole evidence that the
    // burst left the bus. Repeating it every 50 ms would make it unreadable.
    static bool said = false;
    if (!said) {
        said = true;
        Serial.printf("[led-fluid] echo WS2812 : ram=%u refresh=%u (0 = ecrit)\n",
                      (unsigned)ramErr, (unsigned)cfgErr);
    }
}

// The PY32 boots in ~200 ms and the LED data line has to be configured before
// the colour RAM means anything: direction, pull-up, push-pull, then the count.
// Straight from the vendor sequence — the same one hal/Py32Expander.h runs.
static bool initPy32Leds() {
#if !SCE_HAS_PY32
    // No ring on this board, and no bus to go looking on either — see the flag.
    return false;
#else
    // M5Unified has usually started this bus already, and Arduino answers with
    // "Bus already started in Master Mode" on the console. The call stays: the
    // K151's init order (Wire1 → PY32 → servo → Si12T) is a hardware contract,
    // and a bin that relied on somebody else having started the bus would fail
    // silently on the day that changed. The warning is the cheaper half.
    Wire1.begin(12, 11);
    uint8_t ver = 0;
    const uint32_t t0 = millis();
    while (millis() - t0 < 1200) {
        Wire1.beginTransmission(PY32_ADDR);
        Wire1.write(0x02);                            // REG_VERSION
        if (Wire1.endTransmission(false) == 0 &&
            Wire1.requestFrom(PY32_ADDR, (uint8_t)1) == 1) {
            ver = Wire1.read();
            if (ver != 0x00 && ver != 0xFF) break;
        }
        delay(50);
    }
    if (ver == 0x00 || ver == 0xFF) return false;

    auto rd = [](uint8_t reg) -> uint8_t {
        Wire1.beginTransmission(PY32_ADDR);
        Wire1.write(reg);
        Wire1.endTransmission(false);
        return (Wire1.requestFrom(PY32_ADDR, (uint8_t)1) == 1) ? Wire1.read() : 0xFF;
    };
    auto wr = [](uint8_t reg, uint8_t val) {
        Wire1.beginTransmission(PY32_ADDR);
        Wire1.write(reg);
        Wire1.write(val);
        Wire1.endTransmission();
    };
    wr(0x04, (uint8_t)(rd(0x04) |  PY32_LED_PIN_BIT));   // GPIO 13 = output
    wr(0x0C, (uint8_t)(rd(0x0C) & ~PY32_LED_PIN_BIT));   // pull-down off
    wr(0x0A, (uint8_t)(rd(0x0A) |  PY32_LED_PIN_BIT));   // pull-up on
    wr(0x14, (uint8_t)(rd(0x14) & ~PY32_LED_PIN_BIT));   // push-pull
    wr(PY32_REG_LED_CFG, sce::py32::LED_COUNT);
    delay(200);                                          // settling (vendor value)
    return true;
#endif
}

// A splash is a peak of |accel| — a decision about the OUTPUT, which is why it
// is read here and never inside the solver. The solver has no opinion on sound.
static void splashSound(const m5::imu_data_t& d) {
    if (!gSound) return;
    const float mag = sqrtf(d.accel.x * d.accel.x + d.accel.y * d.accel.y +
                            d.accel.z * d.accel.z);
    static uint32_t lastChirp = 0;
    if (mag > 2.2f && millis() - lastChirp > 350) {
        lastChirp = millis();
        M5.Speaker.tone(880, 40);
    }
}

// setBrightness is an I2C PMIC transaction: A2.2 forbids one per frame, so it
// runs at 2 Hz. `gLtrOk` is the PROBE result and not the option — a board
// without the sensor must not make `auto_bright` look like a setting that does
// nothing.
static void autoBrightness() {
    if (!gAutoBright || !gLtrOk) return;
    static uint32_t lastLux = 0;
    if (millis() - lastLux < 500) return;
    lastLux = millis();
    const int32_t v = gLtr.visible();          // -1 on a failed read, never 0
    if (v >= 0) M5.Display.setBrightness((uint8_t)sce::ltr553::brightnessFrom(v));
}

// =============================================================================
void setup() {
    auto mcfg = M5.config();
    M5.begin(mcfg);
    Serial.begin(115200);
    M5.Display.setRotation(1);
    M5.Display.fillScreen(COL_BG);
    M5.Display.setTextColor(COL_INK);

    SPI.begin(SCE_SD_SCK, SCE_SD_MISO, SCE_SD_MOSI, SCE_SD_CS);
    sdOk = SD.begin(SCE_SD_CS, SPI, SCE_SD_HZ);
    loadConfig();                       // before the first pixel: it carries `lang`
    M5.Display.drawString(sce::T("led-fluid: starting...", "led-fluid : demarrage..."), 10, 10);

#if SCE_COMPANION
    if (sdOk) {
        SceGuest::applyLobbyTheme("led-fluid");
        checkSDUpdater(SD, String("/companion.bin"), 2500, 4);
    }
#endif
    Serial.printf("[led-fluid] SD %s\n", sdOk ? "montee" : "ECHEC montage");

    if (!sdOk) {
        const char* why[] = {
            sce::T("Settings apply but are NOT kept:",
                   "Les reglages s appliquent mais ne sont PAS gardes :"),
            sce::T("hue, physics and the three switches go back",
                   "teinte, physique et les trois interrupteurs reviennent"),
            sce::T("to their compiled defaults at the next boot.",
                   "aux valeurs compilees au prochain demarrage."),
        };
        // A card found on the retry means everything read before it read
        // nothing — so the configuration is read again from scratch.
        if (guest.noSdNotice([] { SD.end();
                                  sdOk = SD.begin(SCE_SD_CS, SPI, SCE_SD_HZ);
                                  return sdOk; },
                             why, (int)(sizeof(why) / sizeof(why[0]))))
            loadConfig();
    }

    // Seeded HERE and not at the mount above: the notice offers a RETRY that
    // mounts the card, and a watcher seeded before it would keep the pre-retry
    // answer — then unmount a working card to "discover" it.
    gSdWatch.begin(sdOk, SCE_SD_CS, SCE_SD_HZ);

    guest.appName = "led-fluid";
    guest.setSwipeExit(SCE_COMPANION != 0);
    setupSettingsPage();
    ipStr = guest.begin();
    Serial.printf("[led-fluid] WiFi %s ip=%s\n",
                  WiFi.status() == WL_CONNECTED ? "STA" : "AP/echec", ipStr.c_str());

    M5.Display.setBrightness(gBright);
    M5.Display.fillScreen(COL_BG);
    for (int i = 0; i < fluid::MAX_CELLS; i++) gPainted[i] = 0xFFFF;

    // Built now rather than on the first swipe: 150 KB of PSRAM found at boot
    // is a failure you can read on the console, where the same failure found
    // mid-gesture is a panel that silently does not open.
    if (!ensurePanel()) Serial.println("[led-fluid] sprite panneau : PSRAM refusee");

    // Both PROBED, never assumed. A board without the sensor, or with a dead
    // one, must not leave an option that silently does nothing.
    gLtrOk  = gLtr.begin();
    gPy32Ok = initPy32Leds();
    // BLACK IMMEDIATELY, even when the echo option is off. The init above
    // configures the data line with a pull-up and announces twelve LEDs, and a
    // WS2812 chain held high with no colour ever clocked into it latches
    // whatever it makes of the noise — reported from the robot as twelve LEDs
    // stuck WHITE. Probing the expander must not be the thing that lights it,
    // so the known state is written once, here, before anything else can.
    ledsOff();
    Serial.printf("[led-fluid] LTR-553 %s, PY32 %s\n",
                  gLtrOk ? "present" : "absent", gPy32Ok ? "present" : "absent");

    // 8 KB is generous for a task whose Sim lives in .bss and whose deepest
    // frame is one lambda inside forEachPair.
    xTaskCreatePinnedToCore(simTask, "fluid-sim", 8192, nullptr, 1, nullptr, 0);
    // The window is short on purpose: a simulation tick is under a
    // millisecond, so there is no long request to drain before a reflash.
    simGuard.windowMs = 2000;
    guest.netGuard = &simGuard;
    // HANDING THE ROBOT BACK — everything this bin lit, sounded or held.
    // It runs whichever exit was used: the HTTP stop, the swipe down, or the
    // lobby's button at the next boot. The simulation task is not touched here
    // on purpose: CoopStop has already PARKED it, and the contract is explicit
    // that a task is parked cooperatively and never suspended or killed under
    // a reflash that may still fail.
    guest.onBeforeStop = [] {
        ledsOff();            // twelve WS2812 latch: not writing is not off
        M5.Speaker.stop();    // a splash chirp must not outlive the app
        releasePanel();       // 150 KB of PSRAM back to updateFromFS
    };
}

void loop() {
    M5.update();
    guest.update();                    // POST /api/bins/stop -> companion

    // THE CARD, WATCHED. Until this call the boot mount stood for the whole
    // run: a card inserted afterwards was never seen, so settings silently did
    // not persist for the session while the panel and /config went on offering
    // to save them. On insertion the configuration is re-read from scratch.
    if (gSdWatch.update([] { loadConfig(); gPanelDirty = true; })) {
        sdOk = gSdWatch.mounted();
        Serial.printf("[led-fluid] SD %s\n",
                      sdOk ? "INSEREE : configuration relue"
                           : "RETIREE : reglages non persistes");
    }

    // ---- the IMU, and every other bus transaction, live HERE (rule 15) -----
    // UPDATE FIRST, and it is not optional. `getImuData()` only CONVERTS the
    // driver's raw buffer — it never talks to the sensor. `update()` is what
    // reads it. Without this line the gravity vector stays frozen at whatever
    // the struct was born with: the fluid floats, tilting the robot does
    // nothing, and it reads exactly like looking down at liquid from above
    // (reported from the robot, 08-20). The companion never hit this because
    // its Brain calls `update()` on every tick; a guest bin has no Brain, so
    // refreshing the sensor is its own job.
    //
    // `getAccel()` would have refreshed it implicitly — and that is precisely
    // why the project forbids it: it fires an I2C read of its own, outside
    // anyone's control, on the bus rule 15 governs.
    M5.Imu.update();
    m5::imu_data_t d;
    M5.Imu.getImuData(&d);             // getImuData, never getAccel
    // THE MEASURED MAPPING LIVES HERE, IN THE BASELINE — not in the defaults of
    // the three switches below. On the K151 standing upright, the direction the
    // liquid should fall is gx = -accel.x, gy = +accel.y, and that minus sign
    // belongs to the board, not to a preference.
    //
    // Carrying it as `tilt_inv_x = 1` instead worked exactly as well and read
    // exactly as badly: the panel and /config then opened on a robot that was
    // behaving correctly while announcing that an axis had been inverted, so the
    // one honest state of the machine was the one that looked like somebody had
    // already been fiddling. A switch has to mean "depart from what is right",
    // or nobody can tell a correct robot from a corrected one. Both boxes are
    // now unticked on a fresh install, and ticking either is a real departure.
    //
    // Viewer-centric: +X is the observer's right (rule 6). They stay tunable
    // because this mapping is a property of how the sensor is MOUNTED, and this
    // bin also runs on a bare Fire where it is mounted differently — on another
    // body a wrong sign should cost a yaml edit, not a rebuild.
    float ax = -d.accel.x, ay = d.accel.y;
    if (gTiltSwap) { const float t = ax; ax = ay; ay = t; }
    gGx    = gTiltInvX ? -ax : ax;
    gGy    = gTiltInvY ? -ay : ay;
    gSwirl = d.gyro.z;

    // ---- touch: swipes route between views, taps act inside one -----------
    // A frame eaten by SceGuest's exit modal must RESET our gesture state, or
    // the next release is decoded as a swipe nobody made — the space bin's
    // 08-04 bug, and the reason SceGuest exposes consumedTouch().
    static bool down = false;
    static int  x0 = 0, y0 = 0;
    if (guest.consumedTouch()) {
        down = false;
    } else if (M5.Touch.getCount() > 0) {
        const auto t = M5.Touch.getDetail(0);
        if (t.wasPressed()) { down = true; x0 = t.x; y0 = t.y; }
        if (down && gView == View::Settings) handleSettingsDrag(t.x, t.y);
        if (down && gView == View::Colour)   handleColourDrag(t.x, t.y);
    } else if (down) {
        down = false;
        const auto t = M5.Touch.getDetail(0);
        const flu::Swipe sw{ t.x - x0, t.y - y0 };
        const flu::UiEvent e = flu::swipeEvent(gView, sw);
        if (e != flu::UiEvent::None) {
            applyEvent(e);
        } else if (!flu::isSwipe(sw)) {
            // A TAP, and taps are the one input that carries pixels: poking the
            // liquid and hitting a row both need the point. They stay on their
            // own path rather than being squeezed into an event that would have
            // to smuggle coordinates through it.
            if      (gView == View::Fluid)    postImpulse(t.x, t.y);
            else if (gView == View::Settings) handleSettingsTap(t.x, t.y);
        }
    }

#if SCE_INPUT_BUTTONS
    // ---- the second producer: three buttons, same vocabulary --------------
    // The bank is shared and natively tested (firmware/common/ButtonFsm.h): it
    // owns the debounce, the boot priming and the rule that a fired long press
    // does not also count as a short one. What is this bin's own is the mapping
    // in input.h, and it is one function.
    {
        static flu::ButtonFsm btns;
        const bool pressed[flu::BTN_COUNT] = {
            M5.BtnA.isPressed(), M5.BtnB.isPressed(), M5.BtnC.isPressed()
        };
        flu::BtnEvent be;
        if (btns.update(millis(), pressed, &be)) applyEvent(flu::buttonEvent(be));
    }
#endif

    splashSound(d);
    autoBrightness();
    // From the SNAPSHOT, not from the live buffer: the ring reads the same
    // frame the screen does, and a producer flip mid-scan cannot tear it.
    echoLeds(gPaintCopy);              // the ring follows the fluid, panel or not

    // The frame is COPIED whatever the view: the ring echoes the fluid while a
    // panel is open, so the snapshot cannot be the fluid view's private affair.
    if (gDirty) { gDirty = false; snapshotGrid(); }

    if (gView == View::Fluid) {
        // Painting runs every pass, not only on a new frame: a budgeted sweep
        // may still owe cells from the frame before.
        paintGrid(gPaintCopy);
    } else if (gPanelDirty) {
        gPanelDirty = false;
        if (gView == View::Settings) drawSettings();
        else                         drawColour();
    }

    // The SD shares SPI2 with the display: the write happens HERE, once, and
    // never inside a touch handler. The backoff is what stops a full card from
    // being hammered every few milliseconds by a retry loop.
    if (gSaveDue && (int32_t)(millis() - gSaveNextMs) >= 0) {
        if (saveConfig()) gSaveDue = false;
        else              gSaveNextMs = millis() + 5000;
    }

    static uint32_t lastBeat = 0;
    if (millis() - lastBeat > 5000) {
        lastBeat = millis();
        Serial.printf("[led-fluid] alive - ip:%s heap:%u\n",
                      ipStr.c_str(), (unsigned)ESP.getFreeHeap());
    }
}
