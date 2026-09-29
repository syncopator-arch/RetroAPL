/*
	kapl_unicode.h

	KAPL.ttf font-index <-> Unicode translation, plain C, no dependencies
	on the backend headers or on Qt. Used by the command-line front end
	(apl_cli.c) for UTF-8 input and output.

	The core table is a C copy of kapl_unicode_mapping.hpp's
	KaplToUnicodeTable[], which the Qt front end uses for clipboard
	copy/paste. The two must stay identical -- check_kapl_tables.sh
	compares them. See kapl_unicode.c for the input-only alias table,
	which has no counterpart in the .hpp.
*/

#ifndef KAPL_UNICODE_H
#define KAPL_UNICODE_H

#include <stdint.h>

/* Status bits returned through kapl_utf8_to_font()'s *status argument */
#define KAPL_BADCHARS	1	/* unmappable or malformed input became '?' */
#define KAPL_TRUNCATED	2	/* output buffer filled before input ended */

/* One font-index byte -> Unicode codepoint (same as the GUI's copy) */
uint32_t kapl_to_unicode(unsigned char b);

/* One Unicode codepoint -> font-index byte, or -1 if unmappable */
int kapl_from_unicode(uint32_t u);

/* NUL-terminated UTF-8 -> NUL-terminated font-index bytes.
   Writes at most outsize-1 bytes plus the NUL. Returns the number of
   bytes written (excluding the NUL). status may be NULL. */
int kapl_utf8_to_font(const char *in, unsigned char *out, int outsize,
	int *status);

/* NUL-terminated font-index bytes -> NUL-terminated UTF-8.
   Never splits a multi-byte character; stops early instead if out
   fills. Returns the number of bytes written (excluding the NUL). */
int kapl_font_to_utf8(const unsigned char *in, char *out, int outsize);

#endif /* KAPL_UNICODE_H */
