#pragma once

#include <string>
#include "MemScanner.h"

// Ported from MenuCrackTNX08.html (an H5GG WebView script) - same search/edit value
// pairs and ranges, run through MemScanner instead of a WebView + H5GG runtime.
namespace ModHacks {

// Each on/off hack gets its own MemScanner instance (they used to share one static
// `scanner()`), so toggling one hack can never clobber another's remembered results.
inline MemScanner &antenaScanner() { static MemScanner instance; return instance; }
inline MemScanner &speedX2Scanner() { static MemScanner instance; return instance; }
inline MemScanner &speedX8Scanner() { static MemScanner instance; return instance; }
inline MemScanner &noRecoilScanner() { static MemScanner instance; return instance; }
inline MemScanner &magicBulletScanner() { static MemScanner instance; return instance; }

// On: fresh search + edit, which leaves the matched addresses sitting in the
// scanner's results. Off: write the original value back to those same remembered
// addresses instead of re-searching for the hacked value.
//
// The old code re-searched on Off, which is what broke Speed x2/x8 Off and crashed
// the game on No Recoil Off: a fresh, un-narrowed searchNumber for a short/common
// value (e.g. "180" across a 1.5GB range) matches thousands of unrelated addresses,
// and editAll-ing all of them back to a huge sentinel value corrupts memory that has
// nothing to do with the hack. Reusing the exact addresses found on On touches only
// what this hack itself changed.
inline void antena(bool on) {
    MemScanner &s = antenaScanner();
    if (on) {
        s.clearResults();
        s.searchNumber("4575657222469336965", "I64");
        s.editAll("4848124999925251973", "I64");
    } else {
        s.editAll("4575657222469336965", "I64");
    }
}

inline void speedX2(bool on) {
    MemScanner &s = speedX2Scanner();
    if (on) {
        s.clearResults();
        s.searchNumber("4397530849764387586", "I64");
        s.editAll("4397530849758414897", "I64");
    } else {
        s.editAll("4397530849764387586", "I64");
    }
}

inline void speedX8(bool on) {
    MemScanner &s = speedX8Scanner();
    if (on) {
        s.clearResults();
        s.searchNumber("4397530849764387586", "I64");
        s.editAll("4397530849757180000", "I64");
    } else {
        s.editAll("4397530849764387586", "I64");
    }
}

inline void noRecoil(bool on) {
    MemScanner &s = noRecoilScanner();
    if (on) {
        s.clearResults();
        s.searchNumber("1016018816", "I32", 0x100000000ULL, 0x160000000ULL);
        s.editAll("180", "I32");
    } else {
        s.editAll("1016018816", "I32");
    }
}

// Ported call-for-call from the source script, including its own oddity: the final
// searchNumber below re-scans the full range and supersedes the four searchNearby
// calls before it, since MemScanner::searchNumber (like h5gg's) always resets the
// result set. That's how the source script is written; kept as-is since there's no
// way to verify the "intended" narrowing behavior without the live game binary.
inline void magicBullet(bool on) {
    if (!on) return; // source script's OFF handler only shows a toast, there's no revert
    MemScanner &s = magicBulletScanner();
    s.clearResults();
    s.searchNumber("4333543704410193920", "I64", 0x100000000ULL, 0x160000000ULL);
    s.searchNearby("0.01", "F32", 0x8);
    s.searchNearby("0.0219~0.02975", "F32", 0x32);
    s.searchNearby("0.1035~0.1070", "F32", 0x4);
    s.searchNearby("2.802597e-45", "F32", 0x4);
    s.searchNumber("0.1035~0.1070", "F32", 0x100000000ULL, 0x160000000ULL);
    s.editAll("1.875", "F32");
}

// Two-step "Hanh Dong" (action) patcher: pick a base emote/gesture ("goc") to locate,
// then a target weapon-skin/emote ("mod") whose hex value overwrites it - same flow
// as the HTML's selectedGocFunc + action-mod handlers.
inline MemScanner &actionScanner() {
    static MemScanner instance;
    return instance;
}

inline void selectGocAction(const std::string &hex) {
    MemScanner &s = actionScanner();
    s.clearResults();
    s.searchNumber(hex, "I32", 0x100000000ULL, 0x160000000ULL);
}

inline bool applyModAction(const std::string &hex) {
    return actionScanner().editAll(hex, "I32");
}

} // namespace ModHacks
