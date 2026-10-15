/*
	kapl_unicode.c

	KAPL.ttf font-index <-> Unicode translation for the command-line
	front end, and for the session transcript in crash.log (crashlog.c,
	so it's in the backend library now as well). Plain C, self-contained.

	Behavior deliberately matches the Qt front end's clipboard
	translation (APLTerminalWidget.cpp: translateClipboardTextToKapl()
	for input, kaplByteToClipboardUnicode() for output), including the
	input-only alias table, below, so that the common alternate
	codepoints for APL glyphs (Greek letters for rho/iota/etc., U+2227
	for and, and so on) are accepted instead of becoming '?'. The GUI
	got its copy of the aliases (KaplAliasTable[]) in October 2026;
	before that it had none.

	Output direction: bytes below 0x80 pass through; bytes 0x80-0xFF
	use the table, or pass through as the Latin-1 codepoint of the same
	value if unmapped. Identical to the GUI's copy.

	Input direction: aliases first, then ASCII passes through, then the
	table (reverse lookup), then 0x80-0xFF passes through as a raw byte,
	and anything else is unmappable (-1; the string converter
	substitutes '?'). Same result as the GUI's paste.
*/

#include <stddef.h>
#include "kapl_unicode.h"

struct kapl_entry {
	unsigned char kapl;
	uint32_t unicode;
};

/*
 * Core table -- MUST match KaplToUnicodeTable[] in
 * kapl_unicode_mapping.hpp entry for entry. kapl_unicode_test compares
 * it against the pairs between the .hpp's BEGIN/END KAPL TABLE markers;
 * keep one {0xKK, 0xUUUU}, entry per line in exactly that form in both.
 */
/* BEGIN KAPL TABLE */
static const struct kapl_entry kapl_table[] = {
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
/* END KAPL TABLE */

#define KAPL_TABLE_SIZE ((int)(sizeof(kapl_table) / sizeof(kapl_table[0])))

/*
 * Input-only aliases: alternate codepoints for the same glyph, accepted
 * on input but never produced on output. MUST match KaplAliasTable[] in
 * kapl_unicode_mapping.hpp (kapl_unicode_test compares the pairs between
 * its BEGIN/END KAPL ALIASES markers).
 *
 * The first entry is a correctness fix, not a convenience. The core
 * table maps stile (0xCD) to plain '|' on output, but the input path
 * passes ASCII straight through, so '|' would come back as font byte
 * 0x7C -- which alt_input_proc() turns into the internal code for OR,
 * not residue (5|7 gives 1, not 2). The GUI's paste had the same gap
 * until it got the alias table (October 2026).
 */
static const struct kapl_entry kapl_alias[] = {
	{0xCD, 0x007C},	/* | vertical line       -> stile           */
	{0xCD, 0x2223},	/* ∣ divides             -> stile           */
	{0x5E, 0x2227},	/* ∧ logical and         -> and (font 0x5E) */
	{0x7E, 0x223C},	/* ∼ tilde operator      -> tilde           */
	{0x2A, 0x22C6},	/* ⋆ star operator       -> power           */
	{0x2D, 0x2212},	/* − minus sign          -> minus           */
	{0xC1, 0x03B1},	/* α Greek alpha         -> alpha           */
	{0xC5, 0x03B5},	/* ε Greek epsilon       -> epsilon         */
	{0xC5, 0x220A},	/* ∊ small element of    -> epsilon         */
	{0xC8, 0x0394},	/* Δ Greek capital delta -> delta           */
	{0xC9, 0x03B9},	/* ι Greek iota          -> iota            */
	{0xD2, 0x03C1},	/* ρ Greek rho           -> rho             */
	{0xD7, 0x03C9},	/* ω Greek omega         -> omega           */
};

#define KAPL_ALIAS_SIZE ((int)(sizeof(kapl_alias) / sizeof(kapl_alias[0])))

uint32_t
kapl_to_unicode(unsigned char b)
{
	int i;

	if (b < 0x80) return b;
	for (i = 0; i < KAPL_TABLE_SIZE; i++)
		if (kapl_table[i].kapl == b) return kapl_table[i].unicode;
	return b;
}

int
kapl_from_unicode(uint32_t u)
{
	int i;

	for (i = 0; i < KAPL_ALIAS_SIZE; i++)
		if (kapl_alias[i].unicode == u) return kapl_alias[i].kapl;
	if (u < 0x80) return (int)u;
	for (i = 0; i < KAPL_TABLE_SIZE; i++)
		if (kapl_table[i].unicode == u) return kapl_table[i].kapl;
	if (u <= 0xFF) return (int)u;
	return -1;
}

/*
 * Decode one UTF-8 sequence at *pp, advancing *pp past it. Returns the
 * codepoint, or -1 for a malformed sequence (overlong, surrogate, out
 * of range, bad continuation byte), in which case exactly one byte is
 * consumed so decoding can resynchronize.
 */
static long
utf8_decode(const unsigned char **pp)
{
	const unsigned char *p = *pp;
	uint32_t u;
	int n, i;

	if (p[0] < 0x80) { *pp = p + 1; return p[0]; }
	if ((p[0] & 0xE0) == 0xC0)      { u = p[0] & 0x1F; n = 1; }
	else if ((p[0] & 0xF0) == 0xE0) { u = p[0] & 0x0F; n = 2; }
	else if ((p[0] & 0xF8) == 0xF0) { u = p[0] & 0x07; n = 3; }
	else { *pp = p + 1; return -1; }

	for (i = 1; i <= n; i++) {
		if ((p[i] & 0xC0) != 0x80) { *pp = p + 1; return -1; }
		u = (u << 6) | (p[i] & 0x3F);
	}
	if ((n == 1 && u < 0x80) || (n == 2 && u < 0x800) ||
	    (n == 3 && u < 0x10000) || u > 0x10FFFF ||
	    (u >= 0xD800 && u <= 0xDFFF)) {
		*pp = p + 1;
		return -1;
	}
	*pp = p + n + 1;
	return (long)u;
}

int
kapl_utf8_to_font(const char *in, unsigned char *out, int outsize,
	int *status)
{
	const unsigned char *p = (const unsigned char *)in;
	int len = 0, st = 0, b;
	long u;

	if (outsize < 1) { if (status) *status = KAPL_TRUNCATED; return 0; }
	while (*p) {
		if (len >= outsize - 1) { st |= KAPL_TRUNCATED; break; }
		u = utf8_decode(&p);
		b = (u < 0) ? -1 : kapl_from_unicode((uint32_t)u);
		if (b < 0) { b = '?'; st |= KAPL_BADCHARS; }
		out[len++] = (unsigned char)b;
	}
	out[len] = '\0';
	if (status) *status = st;
	return len;
}

int
kapl_font_to_utf8(const unsigned char *in, char *out, int outsize)
{
	int len = 0, n, i;
	uint32_t u;
	unsigned char buf[4];

	if (outsize < 1) return 0;
	for (; *in; in++) {
		u = kapl_to_unicode(*in);
		if (u < 0x80) {
			buf[0] = (unsigned char)u; n = 1;
		} else if (u < 0x800) {
			buf[0] = (unsigned char)(0xC0 | (u >> 6));
			buf[1] = (unsigned char)(0x80 | (u & 0x3F)); n = 2;
		} else if (u < 0x10000) {
			buf[0] = (unsigned char)(0xE0 | (u >> 12));
			buf[1] = (unsigned char)(0x80 | ((u >> 6) & 0x3F));
			buf[2] = (unsigned char)(0x80 | (u & 0x3F)); n = 3;
		} else {
			buf[0] = (unsigned char)(0xF0 | (u >> 18));
			buf[1] = (unsigned char)(0x80 | ((u >> 12) & 0x3F));
			buf[2] = (unsigned char)(0x80 | ((u >> 6) & 0x3F));
			buf[3] = (unsigned char)(0x80 | (u & 0x3F)); n = 4;
		}
		if (len + n > outsize - 1) break;
		for (i = 0; i < n; i++) out[len++] = (char)buf[i];
	}
	out[len] = '\0';
	return len;
}
