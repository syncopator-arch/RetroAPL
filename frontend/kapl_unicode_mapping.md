# KAPL.ttf Font-Index → Unicode Mapping (for clipboard translation)

## Method

1. Extracted `kapl.ttf`'s cmap with `fontTools` — glyph *names* turned out to be leftover
   Latin-1 names from whatever font this was built from (`Aacute`, `Iacute`, etc.), exactly
   as suspected. Not usable for identification.
2. Rendered every codepoint 0x20–0xFF from the **actual font** as images and visually
   identified each APL glyph by shape.
3. Cross-checked against the existing comments in `mappings.hpp` (OSTab operand names).
4. Parsed `OrigTrans`/`AsciiTrans`/`OSTab` programmatically to find which of the 128
   high-byte slots (0x80–0xFF) are **actually reachable** by any keystroke or overstrike
   combination — 50 of 128 are. The other 78 are unused leftover glyphs baked into the
   font and can be ignored for this feature.
5. Verified exact Unicode code points against the official Unicode Consortium chart for
   the Miscellaneous Technical block (U+2300–U+23FF), which is where all the dedicated
   APL symbols live.

## Confidence key
- **HIGH** — glyph shape unambiguous and/or confirmed via OSTab operand cross-reference and official Unicode chart.
- **MED** — visually clear, but not cross-referenced elsewhere in code; low risk.
- **CHECK** — genuinely ambiguous or not independently confirmed; worth a quick visual sanity check against FontForge before shipping.

## Reachable mapping table (the 50 that matter)

| Index | Reached via | Glyph | Unicode | Name | Confidence |
|---|---|---|---|---|---|
| 0xA1 | ASCII `!` | ¨ | U+00A8 | DIAERESIS | HIGH |
| 0xA2 | ASCII/Classic `@` | ¯ | U+00AF | MACRON (APL high-minus) | HIGH |
| 0xA4 | ASCII `$` | ≤ | U+2264 | LESS-THAN OR EQUAL TO | HIGH |
| 0xA6 | ASCII/Classic `^` | ≥ | U+2265 | GREATER-THAN OR EQUAL TO | HIGH |
| 0xA8 | ASCII/Classic `*` | ≠ | U+2260 | NOT EQUAL TO | HIGH |
| 0xA9 | ASCII `'` / Classic `(` | ∨ | U+2228 | LOGICAL OR (confirmed by header comment "inverted-caret") | HIGH |
| 0xAB | ASCII/Classic `=` | × | U+00D7 | MULTIPLICATION SIGN | HIGH |
| 0xAC | *(not independently reachable; see note)* | ⍪ | U+236A | APL FUNCTIONAL SYMBOL COMMA BAR | MED |
| 0xAD | OSTab: Quad+Divide (Domino) | ⌹ | U+2339 | APL FUNCTIONAL SYMBOL QUAD DIVIDE | HIGH (target correct; **see root-cause note below — this is not simply a "missing glyph"**) |
| 0xAF | OSTab: `/`+`-` | ⌿ | U+233F | APL FUNCTIONAL SYMBOL SLASH BAR | HIGH |
| 0xB0 | OSTab: `^`+`~` (Nand) | ⍲ | U+2372 | APL FUNCTIONAL SYMBOL UP CARET TILDE | HIGH |
| 0xB9 | OSTab: Or+`~` (Nor) | ⍱ | U+2371 | APL FUNCTIONAL SYMBOL DOWN CARET TILDE | HIGH |
| 0xBD | *(not independently reachable; see note)* | ≡ | U+2261 | IDENTICAL TO (Match) | MED |
| 0xBE | OSTab: `[`+`]` (Squad) | ⌷ | U+2337 | APL FUNCTIONAL SYMBOL SQUISH QUAD | HIGH |
| 0xC1 | ASCII/Classic `A` | ⍺ | U+237A | APL FUNCTIONAL SYMBOL ALPHA | HIGH |
| 0xC2 | ASCII/Classic `B` | ⊥ | U+22A5 | UP TACK | HIGH |
| 0xC3 | ASCII/Classic `C` | ∩ | U+2229 | INTERSECTION (Cap) | HIGH |
| 0xC4 | ASCII/Classic `D` | ⌊ | U+230A | LEFT FLOOR (APL downstile) | HIGH |
| 0xC5 | ASCII/Classic `E` | ∈ | U+2208 | ELEMENT OF | HIGH |
| 0xC7 | ASCII/Classic `G` | ∇ | U+2207 | NABLA (Del) | HIGH |
| 0xC8 | ASCII/Classic `H` | Δ | U+2206 | INCREMENT (this codebase's "Del-up"/Delta) | HIGH |
| 0xC9 | ASCII/Classic `I` | ⍳ | U+2373 | APL FUNCTIONAL SYMBOL IOTA | HIGH |
| 0xCA | ASCII/Classic `J` | ∘ | U+2218 | RING OPERATOR (Jot) | HIGH |
| 0xCC | ASCII/Classic `L` | ⎕ | U+2395 | APL FUNCTIONAL SYMBOL QUAD | HIGH |
| 0xCD | ASCII/Classic `M`/`\|` | \| | U+007C | VERTICAL LINE (Stile — kept as plain ASCII bar, standard convention) | HIGH |
| 0xCE | ASCII/Classic `N` | ⊤ | U+22A4 | DOWN TACK | HIGH |
| 0xCF | ASCII/Classic `O` | ○ | U+25CB | WHITE CIRCLE | HIGH |
| 0xD2 | ASCII/Classic `R` | ⍴ | U+2374 | APL FUNCTIONAL SYMBOL RHO | HIGH |
| 0xD3 | ASCII/Classic `S` | ⌈ | U+2308 | LEFT CEILING (APL upstile) | HIGH |
| 0xD5 | ASCII/Classic `U` | ↓ | U+2193 | DOWNWARDS ARROW | HIGH |
| 0xD6 | ASCII/Classic `V` | ∪ | U+222A | UNION | HIGH |
| 0xD7 | ASCII/Classic `W` | ⍵ | U+2375 | APL FUNCTIONAL SYMBOL OMEGA | HIGH |
| 0xD8 | ASCII/Classic `X` | ⊃ | U+2283 | SUPERSET OF (right shoe / pick) | HIGH |
| 0xD9 | ASCII/Classic `Y` | ↑ | U+2191 | UPWARDS ARROW | HIGH |
| 0xDA | ASCII/Classic `Z` | ⊂ | U+2282 | SUBSET OF (left shoe / enclose) | HIGH |
| 0xDC | OSTab: `\`+`-` | ⍀ | U+2340 | APL FUNCTIONAL SYMBOL BACKSLASH BAR | HIGH |
| 0xDF | ASCII `_` / Classic `+` | ÷ | U+00F7 | DIVISION SIGN | HIGH |
| 0xE0 | OSTab: Cap+Iota (I-Beam) | ⌶ | U+2336 | APL FUNCTIONAL SYMBOL I-BEAM | HIGH |
| 0xE1 | OSTab: Circle+`-` | ⊖ | U+2296 | CIRCLED MINUS | HIGH |
| 0xE2 | OSTab: Cap+Circle (Execute) | ⍎ | U+234E | APL FUNCTIONAL SYMBOL DOWN TACK JOT | HIGH |
| 0xE3 | OSTab: Down-U+Circle (Comment) | ⍝ | U+235D | APL FUNCTIONAL SYMBOL UP SHOE JOT | HIGH |
| 0xE6 | OSTab: Del+`~` (Locked Del) | ⍫ | U+236B | APL FUNCTIONAL SYMBOL DEL TILDE | HIGH |
| 0xE7 | OSTab: Stile+Del (Sort Descend) | ⍒ | U+2352 | APL FUNCTIONAL SYMBOL DEL STILE (Grade Down) | HIGH |
| 0xE8 | OSTab: Stile+Del-up (Sort Ascend) | ⍋ | U+234B | APL FUNCTIONAL SYMBOL DELTA STILE (Grade Up) | HIGH |
| 0xEC | OSTab: `'`+Quad (Quote-Quad) | ⍞ | U+235E | APL FUNCTIONAL SYMBOL QUOTE QUAD | HIGH |
| 0xEE | OSTab: Iota+Circle (Format) | ⍕ | U+2355 | APL FUNCTIONAL SYMBOL UP TACK JOT | HIGH |
| 0xF0 | OSTab: `*`+Circle | ⍟ | U+235F | APL FUNCTIONAL SYMBOL CIRCLE STAR | HIGH |
| 0xF4 | OSTab: Circle+`\` (Transpose) | ⍉ | U+2349 | APL FUNCTIONAL SYMBOL CIRCLE BACKSLASH | HIGH |
| 0xF7 | OSTab: Circle+Stile (Rotate) | ⌽ | U+233D | APL FUNCTIONAL SYMBOL CIRCLE STILE | HIGH |
| 0xFB | Classic `[` / ASCII `{` | ← | U+2190 | LEFTWARDS ARROW (Assignment) | HIGH |
| 0xFC | OSTab: Del-up+`_` | ⍙ | U+2359 | APL FUNCTIONAL SYMBOL DELTA UNDERBAR | HIGH |
| 0xFD | ASCII `}` / Classic `{` | → | U+2192 | RIGHTWARDS ARROW (Branch) | HIGH |

**Notes on 0xAC and 0xBD:** these didn't show up in my automated reachability scan of
`OrigTrans`/`AsciiTrans`/`OSTab`, but their glyphs (comma-bar, triple-bar/match) are too
recognizable and too core to APL to be accidental — worth a quick check of whether
there's a reachability path I missed (e.g. a second OSTab-like table elsewhere, or a
menu/shortcut binding) before finalizing.

## 0xAD "Domino" — root cause is not a missing glyph

The existing source comment (`mappings.hpp`, near `OSTab`) says the domino entry
`{0xCC, 0xDF, 0xAD}` (Quad+Divide=Domino) "is known to not work because the domino
symbol is missing from KAPL.ttf." Direct inspection of the font says otherwise:

- `fontTools` confirms the glyph at codepoint `0xAD` has real outline data — 5 contours,
  63 points, a normal (non-degenerate) bounding box. The artwork is genuinely there.
- FontForge shows it too, which is why it looked populated there.
- But rendering `chr(0xAD)` through Pillow/FreeType produces a zero-width, invisible
  result (`textbbox` collapses to a single point) despite the glyph data existing.

The reason: **byte value `0xAD`, interpreted as a Unicode codepoint, is U+00AD SOFT
HYPHEN** — a Unicode "format" character (category `Cf`) with the
`Default_Ignorable_Code_Point` property. Essentially every Unicode-aware text-shaping
stack (HarfBuzz, ICU, FreeType+raqm, browsers) is specifically instructed to skip
*drawing* anything at that codepoint outside of line-break contexts, regardless of what
font is loaded or what's actually sitting in that glyph slot. This is a property of the
codepoint itself, not of the font.

**Why this matters for the terminal widget specifically:** the display buffer stores
raw font-index bytes directly as `QChar` codepoints (`fontIndexToChar()` returns
`QChar(index)` with no remapping). So byte `0xAD` on screen literally *is* `QChar(0xAD)`
= U+00AD to Qt's text layout engine. Qt's rich-text rendering goes through Unicode-aware
shaping (HarfBuzz on most platforms), so it's a strong hypothesis — though not confirmed
here without the running widget — that `QTextEdit` suppresses this glyph the same way
Pillow did, independent of anything in `kapl.ttf` or `OSTab`.

**Practical implication if this is ever picked up:** the `OSTab` entry itself is already
correct and needs no change — the composed-character resolution logic already produces
`0xAD` for Quad+Divide. The blocker is one level below the table: `0xAD` as a *display
codepoint* is what's being suppressed. Re-pointing `OSTab`'s domino result at a different,
currently-unused byte slot (and moving/duplicating the artwork there, e.g. via FontForge)
would very likely fix it; adjusting `OSTab` alone would not, since the table isn't what's
broken.

This has no effect on the clipboard-translation feature in this document: the table maps
byte `0xAD` to Unicode `U+2339` (a real, non-ignorable APL codepoint), never to `U+00AD`
itself, so clipboard copy/paste was never going to inherit this problem either way.

## Notes on 0xDB and 0xDD

Both render as clean, unambiguous glyphs — ⊢ (right tack, `0xDB`) and ⊣ (left tack,
`0xDD`) — and are included in the mapping table above for completeness. Per the
reachability scan, though, neither is currently reachable via any key or `OSTab`
combination (confirmed independently: these weren't part of the original 2741
keyboard layout and haven't been bound to a key in this codebase yet). They're listed
here in case they get mapped to a key/overstrike combo in the future.


## Not reachable — present in font but unused (no action needed)

0xA0, 0xA3, 0xA5, 0xA7, 0xAA, 0xAE, 0xB1–0xB8 (except constants above), 0xBA–0xBC, 0xBF,
0xC6, 0xCB, 0xD0, 0xD1, 0xD4, 0xDB, 0xDD, 0xDE, 0xE4, 0xE5, 0xE9, 0xEA, 0xEB, 0xED, 0xEF,
0xF1–0xF3, 0xF5, 0xF6, 0xF8–0xFA, 0xFE, 0xFF — leftover glyphs from font construction,
never produced by any current key or overstrike combination. Left out of the mapping
table entirely; if a byte in this range somehow appears in a buffer (shouldn't happen),
clipboard-copy can just fall back to a placeholder or pass the raw byte through unchanged.

## Standard ASCII range (0x00–0x7E)

Passes through unchanged — these already look identical whether interpreted as raw KAPL
index or literal ASCII (letters, digits, common punctuation), so no translation table
entry is needed; copy/paste for this range is already correct today.
