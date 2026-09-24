#pragma once
// =============================================================================
// RuleEngine.h — StackChan-Companion (behavior)
// =============================================================================
// The REACTIVE HALF of the "sources -> fields -> {widgets, rules}" contract.
//
// A DECLARATIVE rule engine: every rule watches a blackboard FIELD and, on
// transition (edge), fires an ACTION — either posting a WHITELISTED `Command`
// (the CommandQueue arbitrates: reflexes/dances/API stay on top, rule A2.5),
// or writing another field (feedback loop, e.g. an approval decision read back
// by a script).
//
// "Held edge" semantics (covers dark_sleepy AND the Claude-buddy reactions):
//   - the condition must be TRUE continuously for `sustainMs` before firing;
//   - fires ONCE, then stays "armed" as long as the condition holds — it
//     re-fires only after a return to FALSE (no spam);
//   - `cooldownMs`: minimum delay between two firings;
//   - `enableKey`: rule inactive while that field < 0.5 (option gate).
//
// Adding a behavior = adding a RULE (data), never plumbing:
//   dark_sleepy, Claude reactions, alerts... all expressed here.
//
// PURITY: depends on FieldStore.h + Command.h (pure) + an injected action
// callback. No Arduino/FreeRTOS -> tested natively (Clock via the passed `now`).
// =============================================================================

#include <cstdint>
#include <cstring>
#include "../engine/FieldStore.h"
#include "Command.h"

namespace sce {

class RuleEngine {
public:
    enum Op : uint8_t { GT, GE, LT, LE, EQ, NE };

    struct Rule {
        const char* enableKey = nullptr;  // nullptr = always; else active if field>=0.5
        const char* field     = nullptr;  // watched field
        Op          op        = GT;
        float       value     = 0.0f;
        uint32_t    sustainMs = 0;         // condition held before firing
        uint32_t    cooldownMs = 0;

        // The comment line written just ABOVE the rule in rules.txt. Not part
        // of the format - the format has no description field and adding one
        // would break every file already written - but a human puts the
        // explanation there anyway, so that is where it is read from. Never
        // evaluated; it exists so the console can show WHY a rule is there.
        const char* desc      = nullptr;

        bool        setAction = false;     // true = write a field; false = post Command
        Command     cmd;                   // if !setAction
        const char* setKey    = nullptr;   // if setAction
        float       setVal    = 0.0f;
    };

    // Action sink: injected by the app (-> brain->post). Return value ignored.
    using PostFn = void (*)(void* ctx, const Command& c);

    RuleEngine(FieldStore& fields, PostFn post, void* ctx)
        : _fields(fields), _post(post), _ctx(ctx) {}

    // Adds a rule (STABLE string pointers — literals or persistent storage).
    // Returns false if the table is full.
    bool add(const Rule& r) {
        if (_n >= MAX_RULES) return false;
        _r[_n]  = r;
        _st[_n] = RtState{};
        _n++;
        return true;
    }
    int count() const { return _n; }
    // READ-ONLY access to a loaded rule, for the API that lists them. The
    // engine stays the only thing that EVALUATES: this hands out the
    // declaration, never the runtime state, so nothing outside can decide a
    // rule has fired.
    const Rule& at(int i) const { return _r[i]; }
    // Is this rule's gate satisfied RIGHT NOW — the same test evalRule makes,
    // asked once instead of restated. A console that computed it from a
    // separate copy of the field would eventually disagree with the engine
    // about which rules are live, which is the one thing the list is for.
    bool gateOpen(int i) const {
        const Rule& r = _r[i];
        return !r.enableKey || _fields.getF(r.enableKey, 0.0f) >= 0.5f;
    }

    // Truncates to `n` rules (keeps the first `n` — the BUILT-INs). Used for
    // hot-reload: reset to the built-in rule count, then re-parse the SD.
    void truncate(int n) { if (n >= 0 && n < _n) _n = n; }

    // Call periodically (loop). Evaluates every rule; fires the held edges.
    // `now` = millis().
    void update(uint32_t now) {
        for (int i = 0; i < _n; i++) evalRule(i, now);
    }

private:
    static constexpr int MAX_RULES = 24;

    struct RtState {
        uint32_t metSinceMs = 0;   // start of the true condition (0 = false)
        bool     armed      = false; // already fired on this edge (anti-spam)
        uint32_t lastFireMs = 0;
    };

    FieldStore& _fields;
    PostFn      _post;
    void*       _ctx;
    Rule        _r[MAX_RULES];
    RtState     _st[MAX_RULES];
    int         _n = 0;

    static bool cmp(float a, Op op, float b) {
        switch (op) {
            case GT: return a >  b;
            case GE: return a >= b;
            case LT: return a <  b;
            case LE: return a <= b;
            case EQ: return a == b;
            case NE: return a != b;
        }
        return false;
    }

    void evalRule(int i, uint32_t now) {
        Rule& r = _r[i]; RtState& s = _st[i];

        // Enable gate (option). Inactive -> reset the state (no phantom edge
        // when it is re-enabled).
        if (r.enableKey && _fields.getF(r.enableKey, 0.0f) < 0.5f) {
            s.metSinceMs = 0; s.armed = false;
            return;
        }
        if (!r.field) return;

        bool met = cmp(_fields.getF(r.field, 0.0f), r.op, r.value);
        if (!met) { s.metSinceMs = 0; s.armed = false; return; }

        if (s.metSinceMs == 0) s.metSinceMs = now ? now : 1;   // edge start
        if (s.armed) return;                                   // already fired
        if ((uint32_t)(now - s.metSinceMs) < r.sustainMs) return;  // not held long enough
        if (s.lastFireMs && (uint32_t)(now - s.lastFireMs) < r.cooldownMs) return;

        // ---- FIRING ----
        s.armed = true; s.lastFireMs = now ? now : 1;
        if (r.setAction) { if (r.setKey) _fields.set(r.setKey, r.setVal); }
        else if (_post)  { _post(_ctx, r.cmd); }
    }
};

} // namespace sce
