#ifndef KAPL_UNICODE_MAPPING_HPP
#define KAPL_UNICODE_MAPPING_HPP

#include <cstdint>
#include <QChar>
#include <QHash>

// =============================================================================
// KAPL.ttf font-index <-> real Unicode APL glyph translation
// =============================================================================
//
// PURPOSE: clipboard copy/paste ONLY. Does not touch screen codes or .fn file
// codes -- those keep using the raw 0-255 font-index bytes exactly as today
// (see mappings.hpp / OrigTrans / AsciiTrans / displayLineToStorage()).
//
// This table only covers the 50 font-index values (out of 256) that are
// actually reachable via a keystroke (OrigTrans/AsciiTrans) or an overstrike
// combination (OSTab). The other ~78 values above 0x7F are leftover glyphs
// baked into kapl.ttf from whatever font it was derived from, and can never
// appear in the display buffer today -- they are intentionally omitted here.
//
// DERIVATION: identified by rendering every codepoint of the actual kapl.ttf
// and visually matching against the Unicode Consortium's Miscellaneous
// Technical block chart (U+2300-U+23FF), cross-checked against the existing
// operand comments in OSTab. See kapl_unicode_mapping.md for the full
// per-entry sourcing/confidence notes. Two entries (0xAC, 0xBD) are flagged
// there as not independently confirmed reachable -- please sanity-check
// those two against FontForge before relying on them.
//
// PASTE-ONLY ALIASES (KaplAliasTable[], below the main table): alternate
// codepoints for the same glyphs -- U+2227 for and, Greek letters for
// rho/iota/etc., plain '|' for stile -- accepted on paste, never produced
// on copy. Without them those characters pasted as '?' (or, for '|', as
// font byte 0x7C, which the engine reads as OR, not residue).
//
// Both tables MUST match kapl_unicode.c's kapl_table[] and kapl_alias[]
// entry for entry. kapl_unicode_test (backend, `make check`) compares
// the {0xKK, 0xUUUU} pairs between each table's BEGIN/END marker lines
// below, so keep one entry per line in exactly that form, and don't
// write that form anywhere else between the markers.
// =============================================================================

struct KaplUnicodeEntry {
    uint8_t kaplIndex;
    char32_t unicodeChar;
};

// Sorted by kaplIndex for potential binary search; also fine to linear-scan
// given the small size (50 entries).
// BEGIN KAPL TABLE
const KaplUnicodeEntry KaplToUnicodeTable[] = {
    {0xA1, 0x00A8},  // diaeresis
    {0xA2, 0x00AF},  // macron / APL high-minus
    {0xA4, 0x2264},  // <=
    {0xA6, 0x2265},  // >=
    {0xA8, 0x2260},  // <>
    {0xA9, 0x2228},  // or (inverted-caret)
    {0xAB, 0x00D7},  // multiply
    {0xAC, 0x236A},  // comma bar          -- CHECK reachability (see .md)
    {0xAD, 0x2339},  // quad divide (domino) -- font glyph known missing
    {0xAF, 0x233F},  // slash bar (compress w/ dash)
    {0xB0, 0x2372},  // nand
    {0xB9, 0x2371},  // nor
    {0xBD, 0x2261},  // identical to (match) -- CHECK reachability (see .md)
    {0xBE, 0x2337},  // squish quad (squad)
    {0xC1, 0x237A},  // alpha
    {0xC2, 0x22A5},  // up tack
    {0xC3, 0x2229},  // cap / intersection
    {0xC4, 0x230A},  // left floor
    {0xC5, 0x2208},  // element of
    {0xC7, 0x2207},  // nabla / del
    {0xC8, 0x2206},  // delta ("Del-up")
    {0xC9, 0x2373},  // iota
    {0xCA, 0x2218},  // jot
    {0xCC, 0x2395},  // quad
    {0xCD, 0x007C},  // stile -> plain '|'
    {0xCE, 0x22A4},  // down tack
    {0xCF, 0x25CB},  // circle
    {0xD2, 0x2374},  // rho
    {0xD3, 0x2308},  // left ceiling
    {0xD5, 0x2193},  // down arrow
    {0xD6, 0x222A},  // union
    {0xD7, 0x2375},  // omega
    {0xD8, 0x2283},  // right shoe (pick)
    {0xD9, 0x2191},  // up arrow
    {0xDA, 0x2282},  // left shoe (enclose)
    {0xDC, 0x2340},  // backslash bar (expand w/ dash)
    {0xDF, 0x00F7},  // divide
    {0xE0, 0x2336},  // I-beam
    {0xE1, 0x2296},  // circle minus
    {0xE2, 0x234E},  // execute
    {0xE3, 0x235D},  // comment
    {0xE6, 0x236B},  // locked del
    {0xE7, 0x2352},  // grade down
    {0xE8, 0x234B},  // grade up
    {0xEC, 0x235E},  // quote-quad
    {0xEE, 0x2355},  // format
    {0xF0, 0x235F},  // circle star
    {0xF4, 0x2349},  // transpose
    {0xF7, 0x233D},  // rotate (circle stile)
    {0xFB, 0x2190},  // left arrow (assignment)
    {0xFC, 0x2359},  // delta underbar
    {0xFD, 0x2192},  // right arrow (branch)
};
// END KAPL TABLE
const int KaplToUnicodeTableSize = sizeof(KaplToUnicodeTable) / sizeof(KaplToUnicodeTable[0]);

// Paste-only aliases (see the header comment). An alias may repeat a main
// table codepoint only if it maps to the same byte (the '|' entry does,
// on purpose: it documents the case). Not sorted; duplicates of a font
// byte are fine.
// BEGIN KAPL ALIASES
const KaplUnicodeEntry KaplAliasTable[] = {
    {0xCD, 0x007C},  // | vertical line       -> stile
    {0xCD, 0x2223},  // divides (U+2223)      -> stile
    {0x5E, 0x2227},  // logical and           -> and (font 0x5E)
    {0x7E, 0x223C},  // tilde operator        -> tilde
    {0x2A, 0x22C6},  // star operator         -> power
    {0x2D, 0x2212},  // minus sign            -> minus
    {0xC1, 0x03B1},  // Greek alpha           -> alpha
    {0xC5, 0x03B5},  // Greek epsilon         -> epsilon
    {0xC5, 0x220A},  // small element of      -> epsilon
    {0xC8, 0x0394},  // Greek capital delta   -> delta
    {0xC9, 0x03B9},  // Greek iota            -> iota
    {0xD2, 0x03C1},  // Greek rho             -> rho
    {0xD7, 0x03C9},  // Greek omega           -> omega
};
// END KAPL ALIASES
const int KaplAliasTableSize = sizeof(KaplAliasTable) / sizeof(KaplAliasTable[0]);

// Lazily-built lookup structures. Not thread-safe to construct concurrently;
// build once at startup (e.g. via a static local or explicit init call) if
// this matters for your threading model.

inline const QHash<uint8_t, char32_t>& kaplToUnicodeMap() {
    static QHash<uint8_t, char32_t> map = [] {
        QHash<uint8_t, char32_t> m;
        for (int i = 0; i < KaplToUnicodeTableSize; ++i)
            m.insert(KaplToUnicodeTable[i].kaplIndex, KaplToUnicodeTable[i].unicodeChar);
        return m;
    }();
    return map;
}

inline const QHash<char32_t, uint8_t>& unicodeToKaplMap() {
    static QHash<char32_t, uint8_t> map = [] {
        QHash<char32_t, uint8_t> m;
        for (int i = 0; i < KaplToUnicodeTableSize; ++i)
            m.insert(KaplToUnicodeTable[i].unicodeChar, KaplToUnicodeTable[i].kaplIndex);
        // Aliases go in the same map. They never contradict the main
        // table (kapl_unicode_test checks this), so the order is moot.
        for (int i = 0; i < KaplAliasTableSize; ++i)
            m.insert(KaplAliasTable[i].unicodeChar, KaplAliasTable[i].kaplIndex);
        return m;
    }();
    return map;
}

// Convert one raw KAPL-index byte (as stored/displayed today) to its real
// Unicode APL codepoint for clipboard export. Bytes below 0x80, and any
// byte >=0x80 not in the table, pass through unchanged.
inline char32_t kaplByteToClipboardUnicode(uint8_t b) {
    if (b < 0x80) return b;
    const auto& map = kaplToUnicodeMap();
    auto it = map.find(b);
    return (it != map.end()) ? it.value() : static_cast<char32_t>(b);
}

// Convert one incoming Unicode codepoint (from an external paste) back to
// the KAPL font-index byte used on screen. Returns -1 if there's no mapping,
// meaning the caller should decide how to handle unmappable pasted text
// (skip, substitute a placeholder, etc.) -- this deliberately does NOT fall
// through to raw truncation, since silently mangling unmapped Unicode into
// a wrong byte would be worse than flagging it.
//
// The table and aliases are looked up first, ASCII passes through after:
// the same order as kapl_unicode.c's kapl_from_unicode(). Until the alias
// table was added, ASCII returned before the lookup, so a pasted '|'
// stayed byte 0x7C (OR to the engine) instead of becoming stile 0xCD.
// Bytes 0x80-0xFF with no mapping return -1 here; the GUI's paste passes
// those through as raw bytes itself (translateClipboardTextToKapl()).
inline int unicodeToKaplByte(char32_t u) {
    const auto& map = unicodeToKaplMap();
    auto it = map.find(u);
    if (it != map.end()) return static_cast<int>(it.value());
    if (u < 0x80) return static_cast<int>(u);
    return -1;
}

#endif // KAPL_UNICODE_MAPPING_HPP
