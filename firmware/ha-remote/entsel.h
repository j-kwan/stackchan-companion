// =============================================================================
// entsel.h — ha-remote: WHICH entities survive a table that is too small
// =============================================================================
// The bin holds `MAX_ENT` entities and a real Home Assistant serves far more.
// The old rule was "take them until the table is full, then stop", which is not
// a rule at all: what you lose is whatever `/api/states` happened to serve
// last. An installation whose lights come before its covers in that order
// showed FOUR HUNDRED lights and zero covers — and the home screen, counting
// only what it had, announced "0" covers with the same confidence it announces
// a real zero. A cap that truncates in silence reads as "everything is here".
//
// So the choice is made explicit, and it is made on what the bin ALREADY knows
// about each entity at discovery time — no extra request, no extra field.
//
//   1. PINNED first. If the user listed entities in `ha-remote.yaml`
//      (`entities:`), those are theirs and no heuristic outranks them. The
//      entity currently on screen is pinned too: it must not vanish from under
//      the finger because a poll landed.
//   2. LIVE before STALE. An `unavailable` / `unknown` entity can neither be
//      shown (no state) nor commanded (the service call fails). It is a real
//      device and it stays listed while there is room, but it is the first to
//      yield its slot to one that answers.
//   3. FAIR SHARE between categories. The home screen is four cards with four
//      counters; a category emptied by the truncation turns those counters into
//      false statements. Each category therefore gets an equal share of what is
//      left, and whatever a small category does not use goes back to the big
//      ones (water-filling). Four covers among nine hundred lights survive.
//   4. Within one (category, tier), Home Assistant's own order decides. It is
//      stable from one poll to the next, so the same entities come back at the
//      same ranks and `curId` re-anchoring keeps working.
//
// The whole thing is PURE — no Arduino, no JSON, no display — which is the
// point: it is the part that can be wrong in a way nobody notices on target,
// and it is tested natively (`test/test_entsel`, `pio test -e native`).
//
// Usage is two passes over the parsed document (it is already fully in PSRAM,
// walking it twice costs nothing next to the download):
//
//     sel.reset(MAX_ENT, CAT_N);
//     for (e : doc)  sel.offer(catOf(e), tierOf(e));     // pass 1: demand
//     sel.plan();                                        // quotas
//     for (e : doc)  if (sel.admit(catOf(e), tierOf(e))) keep(e);
//
// `dropped()` and `droppedIn()` are known BEFORE pass 2 and are what the UI
// displays: the truncation is a fact to report, not a detail to hide.
// =============================================================================
#pragma once

#include <stdint.h>
#include <string.h>

namespace ha {

// Worth of one entity, independent of its category. Higher wins.
enum Tier : uint8_t {
    TIER_STALE  = 0,   // not answering: nothing to show, nothing to command
    TIER_LIVE   = 1,   // ordinary entity
    TIER_PINNED = 2,   // named by the user, or currently on screen
    TIER_N      = 3
};

// A state that means "this entity is not answering". An empty state counts as
// stale: Home Assistant always publishes one, so its absence is a broken
// entity, not a discreet one.
inline bool isStale(const char* state) {
    if (!state || !state[0]) return true;
    return !strcmp(state, "unavailable") || !strcmp(state, "unknown") ||
           !strcmp(state, "none");
}

inline char lowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

// A separator in the `entities:` list. Whitespace is one of them, and that is
// the whole point: it used to be accepted only BETWEEN two tokens (skipped on
// entry) and never to END one, so `light. cover.` was read as a SINGLE token of
// thirteen characters, matched nothing, and pinned nothing — in silence, which
// is the failure mode this file exists to avoid. A list is separated by commas,
// semicolons or plain spaces, indifferently; an empty token is skipped.
inline bool isListSep(char c) {
    return c == ',' || c == ';' || c == ' ' || c == '\t';
}

// Does the user's list name this entity? `list` is the `entities:` line of
// ha-remote.yaml: identifiers separated by commas, semicolons or whitespace.
//
// A token matches as a PREFIX, deliberately: `light.` pins a whole domain and
// `cover.salon` pins `cover.salon` along with `cover.salon_2`. Requiring exact
// identifiers would have meant typing forty of them to say "my covers matter".
// Comparison is case-insensitive — Home Assistant identifiers are lowercase,
// but a list typed by hand is not always.
inline bool isPinned(const char* list, const char* id) {
    if (!list || !id || !id[0]) return false;
    for (const char* p = list; *p; ) {
        while (*p && isListSep(*p)) p++;
        const char* tok = p;
        while (*p && !isListSep(*p)) p++;
        size_t n = (size_t)(p - tok);
        if (n) {
            size_t i = 0;
            while (i < n && id[i] && lowerAscii(id[i]) == lowerAscii(tok[i])) i++;
            if (i == n) return true;
        }
    }
    return false;
}

// Hands out `capacity` slots between `groups` categories and the three tiers.
class Selector {
public:
    static constexpr int MAX_GROUPS = 4;

    void reset(int capacity, int groups) {
        _cap    = capacity < 0 ? 0 : capacity;
        _groups = groups < 1 ? 1 : (groups > MAX_GROUPS ? MAX_GROUPS : groups);
        memset(_demand, 0, sizeof(_demand));
        memset(_quota,  0, sizeof(_quota));
        memset(_drop,   0, sizeof(_drop));
        _kept = 0;
    }

    // Pass 1: one candidate seen. Out-of-range groups are IGNORED rather than
    // clamped onto group 0 — a miscount there would silently steal slots from
    // a real category.
    void offer(int group, Tier t) {
        if (group < 0 || group >= _groups || (int)t >= TIER_N) return;
        _demand[group][(int)t]++;
    }

    // Computes the quotas. Tiers are served whole, highest first; inside a
    // tier the remaining budget is shared equally between the categories that
    // still want slots, and the leftovers of the modest ones are redistributed
    // until either the budget or the demand runs out.
    void plan() {
        memset(_quota, 0, sizeof(_quota));
        int left = _cap;
        for (int t = TIER_N - 1; t >= 0; t--) {
            for (;;) {
                int active = 0;
                for (int g = 0; g < _groups; g++)
                    if (_demand[g][t] > _quota[g][t]) active++;
                if (!active || left <= 0) break;
                int share = left / active;
                if (share == 0) {
                    // Fewer slots than claimants: one each, in category order,
                    // until the budget is gone. Deterministic on purpose — a
                    // list that reshuffles between two polls is worse than a
                    // list that favours the first category.
                    for (int g = 0; g < _groups && left > 0; g++)
                        if (_demand[g][t] > _quota[g][t]) { _quota[g][t]++; left--; }
                    break;                       // left is necessarily 0 here
                }
                for (int g = 0; g < _groups; g++) {
                    int want = _demand[g][t] - _quota[g][t];
                    if (want <= 0) continue;
                    int take = want < share ? want : share;
                    _quota[g][t] += take;
                    left -= take;
                }
            }
        }
        // The verdict, frozen now: `admit()` consumes the quotas, so counting
        // the drops afterwards would report zero every time.
        _kept = 0;
        for (int g = 0; g < _groups; g++) {
            _drop[g] = 0;
            for (int t = 0; t < TIER_N; t++) {
                _kept    += _quota[g][t];
                _drop[g] += _demand[g][t] - _quota[g][t];
            }
        }
    }

    // Pass 2: consumes a slot. false = this entity is one of the dropped ones.
    bool admit(int group, Tier t) {
        if (group < 0 || group >= _groups || (int)t >= TIER_N) return false;
        if (_quota[group][(int)t] <= 0) return false;
        _quota[group][(int)t]--;
        return true;
    }

    int seen() const {
        int n = 0;
        for (int g = 0; g < _groups; g++)
            for (int t = 0; t < TIER_N; t++) n += _demand[g][t];
        return n;
    }
    int seenIn(int group) const {
        if (group < 0 || group >= _groups) return 0;
        int n = 0;
        for (int t = 0; t < TIER_N; t++) n += _demand[group][t];
        return n;
    }
    int kept() const       { return _kept; }
    int dropped() const    { return seen() - _kept; }
    int droppedIn(int group) const {
        return (group < 0 || group >= _groups) ? 0 : _drop[group];
    }

private:
    int _cap = 0, _groups = 1, _kept = 0;
    int _demand[MAX_GROUPS][TIER_N] = {};
    int _quota [MAX_GROUPS][TIER_N] = {};
    int _drop  [MAX_GROUPS]         = {};
};

}  // namespace ha
