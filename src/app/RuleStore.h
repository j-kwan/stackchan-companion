#pragma once
// =============================================================================
// RuleStore.h — StackChan-Companion (app)
// =============================================================================
// COMMUNITY REACTIVE PLUGINS: loads rules from the SD card
// (/stackchan-companion/rules.txt), hot-reloadable (POST /api/rules/reload) — same
// precedent as the CSV dances. A text file, one rule per line, no
// recompilation: the community adds behaviours (Claude-buddy, automations)
// by editing a file.
//
// Format (fields separated by "|", spaces ignored; "#" = comment):
//   enable | field | op | value | sustainMs | cooldownMs | action | a1 | a2
//     enable  : gate field (empty = always active)
//     op      : gt ge lt le eq ne
//     action  : AmbientDark|SetEmotion|PlayDance|Blink|WinkLeft|WinkRight|set
//               SetEmotion a1=name [a2=ms] · PlayDance a1=name · set a1=key a2=val
// Examples:
//   dark_sleepy | light | le | 1 | 6000 | 2000 | AmbientDark | 1
//               | approval | ge | 1 | 0 | 3000 | SetEmotion | Questioning
//               | ctx | gt | 90 | 0 | 0 | SetEmotion | Worried
//
// BUILT-IN rules (dark_sleepy) are added FIRST by main; a reload truncates
// back to that count then re-parses the SD file. The strings (enable/field/
// setKey) live in a persistent ARENA owned by this store (they must outlive
// the engine).
//
// The PARSER (parseLine) is PURE → tested natively; only load() touches SD.
// =============================================================================

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include "../behavior/RuleEngine.h"
#include "../behavior/Command.h"
#include "../engine/Emotions.h"
#include "../behavior/Dances.h"

namespace sce {

class RuleStore {
public:
    static constexpr const char* PATH = "/stackchan-companion/rules.txt";

    // Parses ONE line (modified in place: tokenised) and adds the rule to
    // `eng`. `arena`/`used` = persistent string storage. Returns true if a
    // rule was added (false = empty/comment/invalid line). PURE.
    // `desc` = the comment line seen just above this one (nullptr if none).
    static bool parseLine(char* line, char* arena, int arenaCap, int& used,
                          RuleEngine& eng, const char* desc = nullptr) {
        // Tokenise on '|' (max 9 fields), trim every token.
        char* tok[9] = {nullptr};
        int nt = 0;
        char* p = line;
        // skip comments / empty lines
        char* t0 = trim(p);
        if (*t0 == '\0' || *t0 == '#') return false;
        while (nt < 9) {
            tok[nt++] = p;
            char* bar = strchr(p, '|');
            if (!bar) break;
            *bar = '\0';
            p = bar + 1;
        }
        for (int i = 0; i < nt; i++) tok[i] = trim(tok[i]);

        // Minimum required fields: field(1) op(2) value(3) action(6)
        if (nt < 7 || !tok[1][0] || !tok[2][0] || !tok[6][0]) return false;

        RuleEngine::Rule r;
        r.enableKey  = tok[0][0] ? intern(arena, arenaCap, used, tok[0]) : nullptr;
        r.field      = intern(arena, arenaCap, used, tok[1]);
        RuleEngine::Op op;
        if (!parseOp(tok[2], op)) return false;
        r.op         = op;
        r.value      = strtof(tok[3], nullptr);
        r.sustainMs  = (nt > 4 && tok[4][0]) ? (uint32_t)strtoul(tok[4], nullptr, 10) : 0;
        r.cooldownMs = (nt > 5 && tok[5][0]) ? (uint32_t)strtoul(tok[5], nullptr, 10) : 0;

        const char* a1 = (nt > 7) ? tok[7] : "";
        const char* a2 = (nt > 8) ? tok[8] : "";
        if (!parseAction(tok[6], a1, a2, arena, arenaCap, used, r)) return false;

        // INTERNED LAST, and deliberately so. The arena is finite; a full one
        // makes `intern` return nullptr, and a rule that lost its DESCRIPTION
        // still works while a rule that lost its FIELD NAME does not. Taking
        // the description first would let a wordy file break the rules it
        // describes.
        if (desc && desc[0]) {
            char cut[DESC_MAX];
            size_t n = strlen(desc);
            if (n >= sizeof(cut)) {
                // ON A WORD, and with an ellipsis. Cutting at the byte gave
                // "...does not r", which reads like a parser that lost track
                // rather than a sentence someone wrote too long. Back up to
                // the last space, unless there is none near the end - a single
                // very long token is better shown clipped than erased.
                n = sizeof(cut) - 4;
                size_t w = n;
                while (w > sizeof(cut) / 2 && desc[w] != ' ') w--;
                if (desc[w] == ' ') n = w;
                memcpy(cut, desc, n);
                cut[n] = '.'; cut[n + 1] = '.'; cut[n + 2] = '.';
                cut[n + 3] = '\0';
            } else {
                memcpy(cut, desc, n); cut[n] = '\0';
            }
            r.desc = intern(arena, arenaCap, used, cut);
        }
        return eng.add(r);
    }

private:
    // Long enough for two lines of comment joined, short enough that twenty
    // of them cannot starve the field names they sit beside.
    static constexpr int DESC_MAX = 96;

    static char* trim(char* s) {
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
        char* e = s + strlen(s);
        while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
            *--e = '\0';
        return s;
    }
    // Copies `s` into the arena, returns a STABLE pointer (nullptr if full/empty).
    static const char* intern(char* arena, int cap, int& used, const char* s) {
        if (!s || !s[0]) return nullptr;
        int len = (int)strlen(s) + 1;
        if (used + len > cap) return nullptr;
        char* dst = arena + used;
        memcpy(dst, s, len);
        used += len;
        return dst;
    }
    static bool eqi(const char* a, const char* b) {
        for (; *a && *b; a++, b++) if ((*a | 0x20) != (*b | 0x20)) return false;
        return *a == *b;
    }
    static bool parseOp(const char* s, RuleEngine::Op& out) {
        if (eqi(s, "gt")) { out = RuleEngine::GT; return true; }
        if (eqi(s, "ge")) { out = RuleEngine::GE; return true; }
        if (eqi(s, "lt")) { out = RuleEngine::LT; return true; }
        if (eqi(s, "le")) { out = RuleEngine::LE; return true; }
        if (eqi(s, "eq")) { out = RuleEngine::EQ; return true; }
        if (eqi(s, "ne")) { out = RuleEngine::NE; return true; }
        return false;
    }
    static bool parseAction(const char* act, const char* a1, const char* a2,
                            char* arena, int cap, int& used, RuleEngine::Rule& r) {
        if (eqi(act, "set")) {                    // write to a field
            if (!a1[0]) return false;
            r.setAction = true;
            r.setKey = intern(arena, cap, used, a1);
            r.setVal = strtof(a2, nullptr);
            return r.setKey != nullptr;
        }
        r.setAction = false;
        if (eqi(act, "AmbientDark")) {
            r.cmd.type = CmdType::AmbientDark; r.cmd.i = (int)strtol(a1, nullptr, 10);
        } else if (eqi(act, "SetEmotion")) {
            eEmotions e = emotionFromName(a1);
            if (e >= EMOTIONS_COUNT) return false;
            r.cmd.type = CmdType::SetEmotion; r.cmd.i = (int)e;
            r.cmd.u = a2[0] ? (uint32_t)strtoul(a2, nullptr, 10) : 0;
        } else if (eqi(act, "PlayDance")) {
            // THE NAME TRAVELS, NOT THE INDEX — and the reason is an ordering
            // fact, not a preference. At boot `loadRulesForPersonality()` runs
            // BEFORE `danceStore.reload()`, so when this line executes the SD
            // bank is EMPTY: resolving here could never see a choreography from
            // the card, and `indexOf` (compiled table only) rejected the rule
            // outright. A rule naming an SD dance was therefore dropped in
            // silence — the file had a line, the engine had no rule.
            //
            // Even with the order reversed an index would be wrong: DanceStore
            // is DOUBLE-BANKED and reloads hot, so a stored index can designate
            // a different dance after an upload. The app-side sink resolves the
            // name at FIRE time (`rulePost`, main.cpp) against the merged list.
            //
            // `intern` returns a pointer into RuleStore's own `_arena`, a member
            // of a file-scope object: it stays valid exactly as long as the rule
            // that holds it, which is what A2.17 demands of anything crossing
            // the CommandQueue.
            if (!a1[0]) return false;
            r.cmd.type = CmdType::PlayDance;
            r.cmd.ptr  = intern(arena, cap, used, a1);
            if (!r.cmd.ptr) return false;      // arena full — refuse, do not guess
            // `cmd.i` is INFORMATIONAL ONLY: the compiled index when the name
            // is a built-in dance, -1 when it is SD-only (pinned by
            // test_rulestore). It is NOT a fallback and nothing in the
            // firing path reads it — `rulePost` (main.cpp) resolves `cmd.ptr`
            // by NAME at fire time, against the merged built-in+SD list, for
            // exactly the reason given above: a stored index would go stale
            // across a hot DanceStore reload, so it must never be trusted to
            // resolve anything, only to describe what it resolved to today.
            r.cmd.i = dances::indexOf(a1);
        } else if (eqi(act, "Blink")) {
            r.cmd.type = CmdType::Blink;
        } else if (eqi(act, "WinkLeft")) {
            r.cmd.type = CmdType::WinkLeft;
        } else if (eqi(act, "WinkRight")) {
            r.cmd.type = CmdType::WinkRight;
        } else {
            return false;
        }
        return true;
    }

// ---- SD I/O (Arduino only): reset to the built-in count, then parse -------
#if defined(ARDUINO)
public:
    // Writes a COMMENTED rules.txt (template) when missing → visible,
    // downloadable and editable from the console (file manager). Returns true
    // when created. Called at boot from loop()/setup (SD off AsyncTCP, with
    // the renderer paused).
    static bool writeDefaultIfAbsent() {
        File chk = SD.open(PATH, FILE_READ);       // missing OR empty (0 B) → (re)write
        bool nonEmpty = chk && chk.size() > 0;
        if (chk) chk.close();
        if (nonEmpty) return false;
        SD.mkdir("/stackchan-companion");
        File f = SD.open(PATH, FILE_WRITE);
        if (!f) return false;
        // THE DEFAULT rules.txt, and the only copy that is authoritative.
        // `sdcard/stackchan-companion/rules.txt.example` is a mirror of it, held
        // by scripts/gates/check-mirrors.py - a template that teaches the
        // format is worth exactly as much as its agreement with the parser
        // twelve lines above.
        f.print(
            "# rules.txt - StackChan reactive plugins. One rule per line, '#' = comment.\n"
            "# Hot reload: the console's Rules section, or POST /api/rules/reload.\n"
            "# Edit it from the console: Files tab > Configurations > rules.txt.\n"
            "#\n"
            "# FORMAT\n"
            "#   enable | field | op | value | sustainMs | cooldownMs | action | a1 | a2\n"
            "#\n"
            "#   enable      GATE: the rule sleeps while THIS field is < 0.5.\n"
            "#               Empty = always active.\n"
            "#   field       what is watched. Published by the robot itself:\n"
            "#                 batt chg rssi cam night mic micL micR light dark_sleepy ip\n"
            "#               Anything else is yours: POST /api/field?name=value\n"
            "#   op          gt  ge  lt  le  eq  ne\n"
            "#   value       the threshold, a number\n"
            "#   sustainMs   the condition must HOLD this long before firing (0 = at once)\n"
            "#   cooldownMs  minimum delay between two firings of the same rule\n"
            "#   action      SetEmotion <name> [ms] | PlayDance <name> | Blink\n"
            "#               | WinkLeft | WinkRight | AmbientDark <0|1> | set <field> <val>\n"
            "#\n"
            "# HELD EDGE. A rule fires ONCE, when its condition becomes true and stays true\n"
            "# for sustainMs. It re-arms only when the condition goes false again - so a\n"
            "# field parked above its threshold does not spam the robot.\n"
            "#\n"
            "# SAFE BY CONSTRUCTION. A rule can only post whitelisted commands: it cannot\n"
            "# draw on the screen and it cannot brick the robot. The reflexes (shake ->\n"
            "# Scared, lift -> Curious) outrank every rule. And an unknown field reads 0,\n"
            "# so a rule written for a source you do not run simply never fires.\n"
            "#\n"
            "# LINE LIMIT 127 BYTES. A longer line is DISCARDED whole, not truncated.\n"
            "#\n"
            "# =========================================================================\n"
            "# CLAUDE CODE - THE CONTEXT GAUGE.  These four are LIVE, not examples.\n"
            "# =========================================================================\n"
            "# They are gated on `claude`, and only the statusline bridge pushes that\n"
            "# field (scripts/dev/claude-statusline.ps1, or the .sh twin on Linux). Until\n"
            "# you install it, `claude` reads 0 and none of them can fire. Install it and\n"
            "# they need nothing else - the same script pushes `ctx`, the percentage of\n"
            "# the context window in use. It sets `claude` to 1 ONLY while it can\n"
            "# actually compute `ctx`, so these never fire on a missing number.\n"
            "#\n"
            "# Watch it on the band too: mode Gauges (console, or POST\n"
            "# /api/statusbar?mode=3) shows g0 with the CTX label the bridge sets.\n"
            "#\n"
            "# Getting full: a first warning, well before it hurts.\n"
            "claude | ctx | ge | 75 | 3000  | 120000 | SetEmotion | Worried | 4000\n"
            "# Nearly full: this is the one worth reacting to.\n"
            "claude | ctx | ge | 90 | 3000  | 60000  | SetEmotion | Scared  | 4000\n"
            "# About to compact - the robot shakes its head, once every five minutes.\n"
            "claude | ctx | ge | 97 | 2000  | 300000 | PlayDance  | shakeNo\n"
            "# Fresh window, room to work. Held ten seconds so a compaction\n"
            "# does not read as celebration.\n"
            "claude | ctx | lt | 20 | 10000 | 600000 | SetEmotion | Happy   | 3000\n"
            "#\n"
            "# --- More to copy, all inactive (leading '#') ------------------------------\n"
            "# Battery under 15 %, at most once a minute:\n"
            "#  | batt | lt | 15 | 0 | 60000 | SetEmotion | Worried\n"
            "# A loud noise startles it (needs the microphone on):\n"
            "#  | micL | gt | 0.95 | 0 | 4000 | SetEmotion | Surprised | 1500\n"
            "# Your own field, pushed by anything: POST /api/field?build=1\n"
            "#  | build | ge | 1 | 0 | 5000 | PlayDance | nod\n");
        f.flush();
        f.close();
        return true;
    }
    // Reloads the SD rules: truncates `eng` to `keepBuiltins` (keeping the
    // built-in ones) then parses `path`. Returns the number of SD rules added,
    // or -1 if the file is missing. Call from loop() (SD off AsyncTCP).
    //
    // THE PATH IS A PARAMETER because a personality owns its own rule file
    // (behavior/Personalities.h). That is not a convenience: the engine holds
    // 24 rules and refuses further ones SILENTLY, so gating rules on the active
    // personality would make every personality share the same 21 slots. One
    // file per personality means only one is ever loaded and each gets all 21
    // — and the rules need no personality gate at all, because THE FILE IS THE
    // GATE. Defaulted to PATH so no existing caller changes.
    int load(RuleEngine& eng, int keepBuiltins, const char* path = PATH) {
        eng.truncate(keepBuiltins);
        _used = 0;
        if (!path || !SD.exists(path)) return -1;
        File f = SD.open(path, FILE_READ);
        if (!f) return -1;
        int n = 0;
        char line[128];
        // The pending comment: the last `#` line seen, handed to the next rule
        // that parses. Cleared by a BLANK line, so the paragraph of format
        // documentation at the top of the file does not end up attached to the
        // first rule fifty lines below it.
        char pending[DESC_MAX] = { 0 };
        while (f.available()) {
            int len = f.readBytesUntil('\n', line, sizeof(line) - 1);
            line[len] = '\0';
            // OVER-LONG LINE: throw the remainder away up to the next newline.
            // This was the FOURTH bounded-read loop of A2.23 and the one that
            // had diverged (review 08-02): SdConfig::load, SceGuest::yamlForEach
            // and DanceStore::parseCsv all discard the tail, this one did not.
            // Without it a rules.txt line over 127 bytes fed its head to
            // parseLine and then its TAIL came back as a line of its own and
            // parsed as a second, INVENTED rule — the "bogus key/value pair"
            // the discipline exists for, and the exact shape of the 96-byte
            // truncation that produced the rule.
            if (len == (int)sizeof(line) - 1 && f.available() && f.peek() != '\n') {
                while (f.available() && f.read() != '\n') { }
                continue;
            }
            // Read the line's shape BEFORE parseLine tokenises it in place.
            {
                const char* t = line;
                while (*t == ' ' || *t == '\t') t++;
                if (*t == '\0' || *t == '\r') { pending[0] = '\0'; }
                else if (*t == '#') {
                    t++;                       // the '#'
                    while (*t == ' ' || *t == '\t') t++;
                    if (*t == '\0' || *t == '\r') {
                        // A BARE `#` is a paragraph break, and that is what
                        // separates the file's documentation header from the
                        // comment that actually belongs to the next rule.
                        pending[0] = '\0';
                    } else {
                        // ACCUMULATE: a sentence wrapped over two comment lines
                        // is ONE description. Keeping only the last line gave
                        // rules whose description was the tail of a phrase —
                        // "mid-answer does not read as celebration" — which
                        // reads like a bug in the parser rather than a wrap.
                        size_t k = strlen(pending);
                        if (k && k < sizeof(pending) - 1) pending[k++] = ' ';
                        while (*t && *t != '\r' && k < sizeof(pending) - 1)
                            pending[k++] = *t++;
                        pending[k] = '\0';
                    }
                }
            }
            // CONSUMED WHATEVER HAPPENS. Clearing it only on success left a
            // failed rule's comment attached to the NEXT rule that parsed —
            // so a typo produced a working rule described by someone else's
            // sentence, which is worse than no description at all.
            const bool wasComment = (pending[0] != '\0');
            if (parseLine(line, _arena, sizeof(_arena), _used, eng, pending)) n++;
            if (wasComment) {
                const char* t = line;
                while (*t == ' ' || *t == '\t') t++;
                if (*t != '#') pending[0] = '\0';
            }
        }
        f.close();
        return n;
    }
private:
    // 768 -> 1536 the day rules carried their description. A full arena is not
    // an error: `intern` returns nullptr and the rule simply shows none.
    char _arena[2560];
    int  _used = 0;
#endif
};

} // namespace sce
