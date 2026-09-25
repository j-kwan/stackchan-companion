#pragma once
// =============================================================================
// FieldStore.h — StackChan-Companion (engine)
// =============================================================================
// THE "blackboard": a store of named fields (float + short string), the SINGLE
// source of the status bar's display data AND of the rules
// (behavior/RuleEngine.h).
//
// It is the keystone of the "sources → fields → {widgets, rules}" contract:
//   - SOURCES write fields (API POST /api/field, loop() pushing battery/rssi/
//     microphones/camera, future BLE/Claude);
//   - the RENDERER reads fields to draw the status bar widgets;
//   - the RULE ENGINE reads fields → posts Commands (RuleEngine.h — built in
//     setup(), stepped from loop()).
//
// Every source and reader is SYMMETRIC: nobody wires up a dedicated path,
// everything goes through named fields. Adding a piece of data means posting a
// field, never touching the plumbing.
//
// THREAD SAFETY: written from AsyncTCP (API) AND loop(), read from the
// Renderer task and the loop()-stepped rule engine — loop() and the Renderer
// BOTH live on core 1. Each key/value pair is mutated under a short SPINLOCK
// (portMUX) — the copies are tiny (key ≤13, string ≤23). PURE except for the
// portMUX (Arduino): also compiled natively through the shim below (tests).
// =============================================================================

#include <cstdint>
#include <cstring>

#if defined(ARDUINO) || defined(ESP_PLATFORM)
  #include <Arduino.h>
  #define SCE_FS_MUX_T        portMUX_TYPE
  #define SCE_FS_MUX_INIT     portMUX_INITIALIZER_UNLOCKED
  #define SCE_FS_ENTER(m)     portENTER_CRITICAL(&(m))
  #define SCE_FS_EXIT(m)      portEXIT_CRITICAL(&(m))
#else
  // Native (tests): single-threaded, no real lock.
  #define SCE_FS_MUX_T        int
  #define SCE_FS_MUX_INIT     0
  #define SCE_FS_ENTER(m)     ((void)(m))
  #define SCE_FS_EXIT(m)      ((void)(m))
#endif

namespace sce {

class FieldStore {
public:
    static constexpr int  MAX_FIELDS = 28;
    static constexpr int  KEY_LEN    = 14;   // key (including '\0')
    static constexpr int  STR_LEN    = 24;   // string value (including '\0')

    // Writes/updates a field. `s` = nullptr → only the float value changes
    // (the existing string is kept). Returns false when the table is full and
    // the key is unknown (never in practice — MAX_FIELDS is generous).
    bool set(const char* key, float f, const char* s = nullptr) {
        if (!key || !key[0]) return false;
        bool ok = true;
        SCE_FS_ENTER(_mux);
        int i = indexOf(key);
        if (i < 0) {
            if (_n >= MAX_FIELDS) { ok = false; }
            else { i = _n++; copyStr(_f[i].key, key, KEY_LEN); _f[i].s[0] = '\0'; }
        }
        if (ok) {
            _f[i].f = f;
            _f[i].hasF = true;
            if (s) { copyStr(_f[i].s, s, STR_LEN); _f[i].hasS = true; }
        }
        SCE_FS_EXIT(_mux);
        return ok;
    }

    // Float read. Returns `dflt` when the field does not exist.
    float getF(const char* key, float dflt = 0.0f) const {
        float out = dflt;
        SCE_FS_ENTER(_mux);
        int i = indexOf(key);
        if (i >= 0 && _f[i].hasF) out = _f[i].f;
        SCE_FS_EXIT(_mux);
        return out;
    }

    // String read into `out` (always NUL-terminated). true when the field has
    // a non-empty string. `out` is emptied otherwise.
    bool getS(const char* key, char* out, int outLen) const {
        if (!out || outLen <= 0) return false;
        bool has = false;
        SCE_FS_ENTER(_mux);
        int i = indexOf(key);
        if (i >= 0 && _f[i].hasS && _f[i].s[0]) { copyStr(out, _f[i].s, outLen); has = true; }
        else out[0] = '\0';
        SCE_FS_EXIT(_mux);
        return has;
    }

    bool has(const char* key) const {
        SCE_FS_ENTER(_mux);
        bool r = indexOf(key) >= 0;
        SCE_FS_EXIT(_mux);
        return r;
    }

    int count() const { return _n; }

private:
    struct Field {
        char  key[KEY_LEN] = "";
        char  s[STR_LEN]   = "";
        float f    = 0.0f;
        bool  hasF = false;
        bool  hasS = false;
    };
    Field _f[MAX_FIELDS];
    int   _n = 0;
    mutable SCE_FS_MUX_T _mux = SCE_FS_MUX_INIT;

    // Linear search (n ≤ 28, negligible cost). Called UNDER the lock.
    int indexOf(const char* key) const {
        for (int i = 0; i < _n; i++)
            if (strncmp(_f[i].key, key, KEY_LEN) == 0) return i;
        return -1;
    }
    static void copyStr(char* dst, const char* src, int n) {
        int i = 0;
        for (; i < n - 1 && src[i]; i++) dst[i] = src[i];
        dst[i] = '\0';
    }
};

} // namespace sce
