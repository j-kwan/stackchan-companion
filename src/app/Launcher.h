#pragma once
// =============================================================================
// Launcher.h — StackChan-Companion (app)
// =============================================================================
// SD binary picker / launcher (ROADMAP §3.6, T3).
//
// Trigger: TOP→BOTTOM swipe on the screen (wired up in main.cpp).
// How it works:
//   1. renderer.pause() — the Launcher becomes the sole owner of the screen
//      (rule A2.1 upheld: the renderer is stopped, not competed with)
//   2. scan of /bins/*.bin on the SD card (name + size)
//   3. full-page UI: 5 files per page, tap = select → confirmation screen
//      [Launch]/[Back]; special "Save firmware" row (saveSketchToFS →
//      /companion.bin, the standard SD-Updater recovery binary); paging via
//      the [Next page] row; exit via the [Quit] zone, or a 30 s timeout
//      without a touch
//   4. Launch → updateFromFS(SD, path) → ESP.restart() (never returns)
//   5. exit → renderer.resume() + blink (wake-up) through the CommandQueue
//
// Safety: refuses .bin files larger than the OTA partition. Getting back to
// the companion from a third-party .bin: hold BtnA at boot (standard
// SD-Updater, reloads /companion.bin) — documented in PLAYBOOK-HW.
//
// run() is BLOCKING inside loop(): accepted (§3.6) — the web server is
// unreachable while the UI is up, the Brain keeps running but is no longer
// displayed. Touch: direct M5.Touch reads (M5.update() is called here).
// =============================================================================

#include <Arduino.h>
#include <M5Unified.h>
#include <SD.h>
#include <esp_ota_ops.h>
#include <M5StackUpdater.h>
#include "../engine/Units.h"
#include "../engine/Renderer.h"   // drawBusyDots ("..." screen while flashing)
#include "../hal/I2cBus.h"        // 11/12 bus lock (FT6336 touch vs IMU)
#include "../../firmware/common/I18n.h"   // sce::T — bilingual UI (EN default)
#include "BinLimits.h"                    // shared /bins/ capacity + name length

// EVERY string on these screens is drawn with the 6x8 bitmap face, which
// renders UTF-8 as "??": the French forms below carry NO accent
// ("Sauvegarde", "Echec"). See firmware/common/I18n.h.

namespace sce {

class Launcher {
public:
    // Shared with the API name cache (BinLimits.h, review 08-04): the two
    // catalogues of the same directory used to cap at 24/40 here against
    // 48/50 there, so a well-stocked card behaved differently per entry
    // point. Costs ~1.5 KB of static RAM over the old pair.
    static constexpr int      MAX_BINS     = sce::BINS_MAX;
    static constexpr int      ROWS         = 5;      // files per page
    static constexpr uint32_t TIMEOUT_MS   = 30000;
    static constexpr const char* BIN_DIR   = "/bins";
    static constexpr const char* SELF_PATH = "/companion.bin";

    // Blocking. Returns when the user quits (or on timeout).
    // Does NOT return if a binary is launched (restart).
    // `rd` (optional): screen SELF-DEFENCE — if the renderer pause is lost
    // while the menu is up (unbalanced external resume, ack timeout on the
    // entry pause(): "the eyes show up on top of the menu", user 07-26), the
    // Launcher TAKES IT BACK and redraws the list. Self-heal that does not
    // care about the cause.
    void run(Renderer* rd = nullptr) {
        _rd = rd;
        _selfPauses = 0;
        runLoop();
        // GIVE BACK the pauses taken by the self-heal: Renderer::pause() is
        // REFCOUNTED and isPaused() returns the ACK, NOT the counter — a late
        // ACK (>500 ms, e.g. the camera SCCB burst holding the I2C lock)
        // caused a re-pause here, main.cpp's single resume left the counter
        // at 1 and the renderer parked FOREVER: screen frozen on the last
        // launcher frame, only a reboot got out of it (max review 07-27).
        while (_selfPauses > 0) { if (_rd) _rd->resume(); _selfPauses--; }
    }

private:
    void runLoop() {
        if (!scan()) {
            splash(T("No SD or /bins empty", "Pas de SD ou /bins vide"),
                   TFT_ORANGE);
            delay(1500);
            return;
        }
        _page = 0;
        _lastTouchMs = millis();
        drawList();

        for (;;) {
            { sce::i2cbus::Guard g; M5.update(); }   // FT6336 touch = bus shared with IMU
            uint32_t now = millis();
            if (now - _lastTouchMs > TIMEOUT_MS) return;   // timeout → exit
            if (_rd && !_rd->isPaused()) { _rd->pause(); _selfPauses++; drawList(); }

            auto t = M5.Touch.getDetail(0);
            if (!t.wasClicked()) { delay(20); continue; }
            _lastTouchMs = now;
            int y = t.y, x = t.x;

            // ---- Bottom bar: hit zones = the DRAWN buttons only
            // (y 206-233, x 4-102 / 110-208 / 216-315). The gaps and the
            // strips above and below are INERT: an imprecise tap used to fire
            // [SauvFW] — a ~1.5 MB flash dump over /companion.bin — or to
            // page through a button that was NOT DRAWN
            // (max review 07-27; mirrors the same hardening in confirm()).
            if (y >= 200) {
                if (y >= 206 && y <= 233) {
                    if      (x >= 4   && x <= 102) return;            // [Quitter]
                    else if (x >= 110 && x <= 208) { saveSelf(); drawList();
                                                     _lastTouchMs = millis(); }
                    else if (x >= 216 && x <= 315 &&
                             (_count + ROWS - 1) / ROWS > 1) {        // [Page >]
                        nextPage(); drawList();                       // drawn
                    }                                                 // only if
                }                                                     // >1 page
                continue;
            }

            // ---- File rows (5 × 32 px starting at y=40) ----
            int row = (y - 40) / 32;
            if (y < 40 || row < 0 || row >= ROWS) continue;
            int idx = _page * ROWS + row;
            if (idx >= _count) continue;

            if (confirm(idx)) {
                launch(idx);   // does not return on success
                drawList();    // failure → back to the list
            } else {
                drawList();
            }
        }
    }

    struct BinEntry { char name[sce::BIN_NAME_MAX]; uint32_t size; };
    BinEntry  _bins[MAX_BINS];
    int       _count = 0;
    int       _page  = 0;
    uint32_t  _lastTouchMs = 0;
    Renderer* _rd = nullptr;   // self-heal of the screen pause (07-26)
    int       _selfPauses = 0; // pauses TAKEN by the self-heal, to give back

    // ------------------------------------------------------------------
    bool scan() {
        _count = 0;
        File dir = SD.open(BIN_DIR);
        if (!dir || !dir.isDirectory()) return false;
        File f;
        while (_count < MAX_BINS && (f = dir.openNextFile())) {
            String n = f.name();               // name without path (core 2.x)
            if (!f.isDirectory() && n.endsWith(".bin")) {
                strncpy(_bins[_count].name, n.c_str(), sizeof(_bins[0].name) - 1);
                _bins[_count].name[sizeof(_bins[0].name) - 1] = '\0';
                _bins[_count].size = f.size();
                _count++;
            }
            f.close();
        }
        dir.close();
        return _count > 0;
    }

    void nextPage() {
        int pages = (_count + ROWS - 1) / ROWS;
        _page = (_page + 1) % (pages > 0 ? pages : 1);
    }

    // ------------------------------------------------------------------
    // Screens (renderer paused: drawing straight to M5.Display is allowed).
    // Design aligned with the "Liquid Glass" console: dark background,
    // rounded cards, cyan/indigo accent (redesign 07-25).
    // ------------------------------------------------------------------
    static constexpr uint16_t C_ACC  = 0x269D;   // cyan  #22d3ee
    static constexpr uint16_t C_ACC2 = 0x847F;   // indigo #818cf8
    static constexpr uint16_t C_CARD = 0x10C5;   // card #131a2b
    static constexpr uint16_t C_HI   = 0x1928;   // highlighted card #1c2740
    static constexpr uint16_t C_BORD = 0x29AA;   // border #2a3550
    // THE SOURCE OF TRUTH for the two copies (SceGuest's lobby, ha-remote),
    // which check-mirrors.py holds to these values. The RGAA claim below is
    // MEASURED by check-contrast.py against the card, not asserted: it was
    // written in three files and verified in none.
    static constexpr uint16_t C_MUT  = 0xA534;   // secondary text (RGAA
                                                 // ≥4.5:1, matches radar 07-27)
    static constexpr uint16_t C_DIM  = 0x738E;   // dimmed text (same)
    static constexpr uint16_t C_KO   = 0xFB8E;   // error #f87171 (= --ko from
                                                 // the WebConsole.h console)

    // Rounded bottom-bar button (fill = solid accent, otherwise outlined)
    static void button(int x, int w, const char* label, uint16_t col, bool fill) {
        auto& d = M5.Display;
        if (fill) { d.fillRoundRect(x, 206, w, 28, 6, col);
                    d.setTextColor(TFT_BLACK, col); }
        else      { d.fillRoundRect(x, 206, w, 28, 6, C_CARD);
                    d.drawRoundRect(x, 206, w, 28, 6, col);
                    d.setTextColor(col, C_CARD); }
        d.setTextSize(2);
        d.setCursor(x + (w - (int)strlen(label) * 12) / 2, 213);
        d.print(label);
    }

    static void header(const char* title, const char* right) {
        auto& d = M5.Display;
        d.fillScreen(TFT_BLACK);
        d.setTextSize(2);
        d.setTextColor(C_ACC, TFT_BLACK);
        d.setCursor(8, 8); d.print(title);
        if (right) {
            d.setTextColor(C_MUT, TFT_BLACK);
            d.setCursor(320 - 8 - (int)strlen(right) * 12, 8); d.print(right);
        }
        // accent rule with a gradient (cyan → indigo, half and half)
        d.fillRect(0, 30, 160, 2, C_ACC);
        d.fillRect(160, 30, 160, 2, C_ACC2);
    }

    // The card grows with the text and is capped at 312 px: BOTH forms of a
    // bilingual message must stay under ~24 characters, otherwise the longer
    // one runs off the right edge of a card that can no longer widen.
    void splash(const char* msg, uint16_t color) {
        auto& d = M5.Display;
        d.fillScreen(TFT_BLACK);
        int w = (int)strlen(msg) * 12 + 40;
        if (w > 312) w = 312;
        int x = (320 - w) / 2;
        d.fillRoundRect(x, 96, w, 48, 8, C_CARD);
        d.drawRoundRect(x, 96, w, 48, 8, color);
        d.setTextSize(2);
        d.setTextColor(color, C_CARD);
        d.setCursor(x + 20, 113);
        d.print(msg);
    }

    void drawList() {
        char pg[12];
        snprintf(pg, sizeof(pg), "%d/%d", _page + 1,
                 (_count + ROWS - 1) / ROWS > 0 ? (_count + ROWS - 1) / ROWS : 1);
        header("LAUNCHER", pg);
        auto& d = M5.Display;
        d.setTextSize(1);
        d.setTextColor(C_DIM, TFT_BLACK);
        // ASCII: the M5 font has no U+2014
        d.setCursor(8, 34); d.print(T("/bins - tap = launch",
                                      "/bins - tap = lancer"));

        for (int r = 0; r < ROWS; r++) {
            int idx = _page * ROWS + r;
            if (idx >= _count) break;
            int y = 44 + r * 32;                       // 28 px card + 4 of air
            d.fillRoundRect(4, y, 312, 28, 6, C_CARD);
            d.drawRoundRect(4, y, 312, 28, 6, C_BORD);
            d.fillRoundRect(10, y + 8, 4, 12, 2, C_ACC2);   // "bin" bullet
            d.setTextSize(2);
            d.setTextColor(TFT_WHITE, C_CARD);
            d.setCursor(22, y + 7);
            char nm[18]; snprintf(nm, sizeof(nm), "%.17s", _bins[idx].name);
            d.print(nm);
            d.setTextSize(1);
            d.setTextColor(C_MUT, C_CARD);
            char sz[10]; snprintf(sz, sizeof(sz), "%luK",
                                  (unsigned long)(_bins[idx].size / 1024));
            d.setCursor(316 - 8 - (int)strlen(sz) * 6, y + 11); d.print(sz);
        }

        // Bottom bar — touch zones = these EXACT rectangles
        // (4-102 / 110-208 / 216-315, see run())
        button(4,   99, T("Quit",   "Quitter"), C_MUT,  false);
        button(110, 99, T("SaveFW", "SauvFW"),  C_ACC2, true);
        int pages = (_count + ROWS - 1) / ROWS;
        // "Page >" reads the same in both languages: no pair to write.
        if (pages > 1) button(216, 100, "Page >", C_ACC, true);
    }

    // Confirmation screen. true = launch.
    bool confirm(int idx) {
        header("LAUNCHER", nullptr);
        auto& d = M5.Display;
        d.fillRoundRect(12, 52, 296, 116, 8, C_CARD);
        d.drawRoundRect(12, 52, 296, 116, 8, C_BORD);
        d.setTextSize(1);
        d.setTextColor(C_MUT, C_CARD);
        d.setCursor(28, 66); d.print(T("Flash this binary?",
                                       "Flasher ce binaire ?"));
        d.setTextSize(2);
        d.setTextColor(C_ACC, C_CARD);
        d.setCursor(28, 84);
        char nm[22]; snprintf(nm, sizeof(nm), "%.21s", _bins[idx].name);
        d.print(nm);
        d.setTextSize(1);
        d.setTextColor(C_MUT, C_CARD);
        d.setCursor(28, 112);
        d.printf(T("%lu KB", "%lu Ko"), (unsigned long)(_bins[idx].size / 1024));
        d.setTextColor(TFT_ORANGE, C_CARD);
        d.setCursor(28, 140);
        d.print(T("Back: remote stop or BtnA at boot",
                  "Retour : stop distant ou BtnA au boot"));

        button(4,   150, T("Cancel", "Annuler"), C_MUT, false);
        button(166, 150, T("LAUNCH", "LANCER"),  C_ACC, true);

        uint32_t start = millis();
        for (;;) {
            { sce::i2cbus::Guard g; M5.update(); }   // FT6336 touch = bus shared with IMU
            if (millis() - start > TIMEOUT_MS) return false;
            if (_rd && !_rd->isPaused()) { _rd->pause(); _selfPauses++; return false; }
            auto t = M5.Touch.getDetail(0);
            if (!t.wasClicked()) { delay(20); continue; }
            _lastTouchMs = millis();
            // Hit zones = the DRAWN buttons only (y 206-234, x 4-154 /
            // 166-316): gaps AND the strip below the buttons are INERT — an
            // imprecise tap must never start a flash (review 07-26/max).
            if (t.y >= 206 && t.y <= 234) {
                if (t.x >= 166 && t.x <= 316) return true;    // [LANCER]
                if (t.x >= 4   && t.x <= 154) return false;   // [Annuler]
            }
        }
    }

    // ------------------------------------------------------------------
    void launch(int idx) {
        // Safety net: the confirmation screen PROMISES "remote stop or BtnA
        // at boot" — both need /companion.bin. Without it the robot stays
        // trapped in the guest binary (USB recovery only): we refuse rather
        // than keep a promise we cannot honour (max review 07-27).
        if (!SD.exists(SELF_PATH)) {
            splash(T("Save the firmware", "Sauvez le firmware"), C_KO);
            delay(2200);
            return;
        }
        // Guard: size ≤ OTA partition (otherwise a black screen is certain).
        // null `part` → we do NOT flash (the old test let it through).
        const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
        if (!part) {
            splash(T("No OTA partition", "Pas de partition OTA"), C_KO);
            delay(2000);
            return;
        }
        if (_bins[idx].size > part->size) {
            splash(T("Too big for partition", "Trop gros pour la partition"),
                   C_KO);
            delay(2000);
            return;
        }
        char path[56];
        snprintf(path, sizeof(path), "%s/%s", BIN_DIR, _bins[idx].name);
        drawFlashScreen(T("Flashing", "Flash en cours"), _bins[idx].name);
        // ALL of SD-Updater's UI is REPLACED (otherwise the library attaches
        // its default DisplayUpdateUI/DisplayErrorUI and writes OVER the
        // dots, user report 07-25): dots ON TOP, text BELOW, a real progress
        // bar. Same task as the SD access: frames interleave between chunks
        // (A2.16), renderer already paused.
        SDUCfg.setProgressCb(flashProgressCb);
        SDUCfg.setMessageCb(flashMessageCb);
        SDUCfg.setErrorCb(flashErrorCb);
        updateFromFS(SD, path);   // SD-Updater: flash + restart on success
        // updateFromFS RETURNED = the flash failed: we do NOT reboot (the old
        // defensive restart turned every failure into a full reboot — run()'s
        // "failure → back to the list" was dead code, max review 07-26).
        // The error is already on screen (flashErrorCb).
        splash(T("Flash failed - back", "Echec flash - retour liste"), C_KO);
        delay(1500);
    }

    // ---- Flash screen: fixed layout, disjoint zones ----
    //   y  48 : STATIC "..." dots (drawBusyDots), laid down ONCE here
    //   y 128 : "Flash en cours" + binary name (accent colour)
    //   y 176 : progress bar + percentage
    //   y 214 : SD-Updater messages/errors (dedicated strip, AT THE BOTTOM)
    // PUBLIC: the REMOTE launch (/api/bins/launch, main.cpp) uses the SAME
    // screen — otherwise the library attaches its default
    // DisplayUpdateUI/DisplayErrorUI, which write over the dots (review 07-26).
public:
    inline static int _lastPct = -1;
    static void drawFlashScreen(const char* title, const char* name) {
        auto& d = M5.Display;
        d.fillScreen(TFT_BLACK);
        _lastPct = -1;
        // The dots are MOTIONLESS: they belong to the backdrop, laid down
        // once. Redrawing them from `flashProgressCb` made them interleave
        // with the flash's SD reads for a result identical to the previous
        // image — pure wasted SPI2 traffic.
        Renderer::drawBusyDots(d, 160, 64);
        d.setTextSize(2);
        d.setTextColor(TFT_WHITE, TFT_BLACK);
        d.setCursor((320 - (int)strlen(title) * 12) / 2, 122); d.print(title);
        d.setTextColor(C_ACC, TFT_BLACK);
        char nm[22]; snprintf(nm, sizeof(nm), "%.21s", name);
        d.setCursor((320 - (int)strlen(nm) * 12) / 2, 146); d.print(nm);
        d.fillRoundRect(40, 176, 240, 10, 4, C_CARD);       // progress bar track
        d.drawRoundRect(40, 176, 240, 10, 4, C_BORD);
    }
    static void flashProgressCb(int done, int total) {
        if (total <= 0) return;
        int pct = (int)((int64_t)done * 100 / total);
        if (pct == _lastPct) return;
        _lastPct = pct;
        auto& d = M5.Display;
        int w = 236 * pct / 100;
        if (w > 0) d.fillRoundRect(42, 178, w, 6, 3, C_ACC);
        d.setTextSize(1);
        d.setTextColor(C_MUT, TFT_BLACK);
        char p[8]; snprintf(p, sizeof(p), "%3d%%", pct);
        d.setCursor(148, 192); d.print(p);
    }
    static void flashMessageCb(const String& label) {
        auto& d = M5.Display;
        d.fillRect(0, 210, 320, 20, TFT_BLACK);             // dedicated strip
        d.setTextSize(1);
        d.setTextColor(C_DIM, TFT_BLACK);
        int w = (int)label.length() * 6;
        d.setCursor((320 - (w > 316 ? 316 : w)) / 2, 216);
        d.print(label);
    }
    static void flashErrorCb(const String& msg, unsigned long waitMs) {
        auto& d = M5.Display;
        d.fillRect(0, 210, 320, 20, TFT_BLACK);
        d.setTextSize(1);
        d.setTextColor(C_KO, TFT_BLACK);
        int w = (int)msg.length() * 6;
        d.setCursor((320 - (w > 316 ? 316 : w)) / 2, 216);
        d.print(msg);
        delay(waitMs);
    }
private:

    void saveSelf() {
        // Same callbacks as launch(): without them saveSketchToFS attaches
        // the library's default UI, which writes over our screen.
        drawFlashScreen(T("Saving", "Sauvegarde"), "/companion.bin");
        SDUCfg.setProgressCb(flashProgressCb);
        SDUCfg.setMessageCb(flashMessageCb);
        SDUCfg.setErrorCb(flashErrorCb);
        if (saveSketchToFS(SD, SELF_PATH, 4 /*TFCARD_CS*/)) {
            splash("OK -> /companion.bin", C_ACC);   // same in both languages
        } else {
            splash(T("SAVE FAILED", "ECHEC sauvegarde"), C_KO);
        }
        delay(1200);
    }
};

} // namespace sce
