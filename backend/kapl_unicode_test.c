/*
	kapl_unicode_test.c

	Self-test for kapl_unicode.c. Includes the .c directly so it can
	inspect the static tables. Build and run:

	    cc -o kapl_unicode_test kapl_unicode_test.c
	    ./kapl_unicode_test [path to kapl_unicode_mapping.hpp, or its directory]

	With a path, it also checks that kapl_unicode.c's core table matches
	the Qt front end's KaplToUnicodeTable[] entry for entry -- the two
	copies must never drift apart. (This replaced check_kapl_tables.sh,
	whose shell tools behaved differently under Windows shells.)

	Prints each failure and a summary; exit status is the failure count
	(0 = all passed).
*/

#include <stdio.h>
#include <string.h>
#include "kapl_unicode.c"

static int failures = 0;

#define CHECK(cond, ...) do { \
	if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); failures++; } \
} while (0)

static void
test_tables(void)
{
	int i, j;

	for (i = 0; i < KAPL_TABLE_SIZE; i++) {
		CHECK(kapl_table[i].kapl >= 0x80,
			"table entry %d: kapl 0x%02X below 0x80", i, kapl_table[i].kapl);
		for (j = i + 1; j < KAPL_TABLE_SIZE; j++) {
			CHECK(kapl_table[i].kapl != kapl_table[j].kapl,
				"table: kapl 0x%02X appears twice", kapl_table[i].kapl);
			CHECK(kapl_table[i].unicode != kapl_table[j].unicode,
				"table: U+%04X appears twice", (unsigned)kapl_table[i].unicode);
		}
	}
	/* an alias may repeat a table codepoint only if it agrees with it */
	for (i = 0; i < KAPL_ALIAS_SIZE; i++)
		for (j = 0; j < KAPL_TABLE_SIZE; j++)
			if (kapl_alias[i].unicode == kapl_table[j].unicode)
				CHECK(kapl_alias[i].kapl == kapl_table[j].kapl,
					"alias U+%04X -> 0x%02X contradicts table -> 0x%02X",
					(unsigned)kapl_alias[i].unicode, kapl_alias[i].kapl,
					kapl_table[j].kapl);
}

static void
test_round_trip(void)
{
	int b, back;

	/* every font byte survives output-then-input, except 0x7C, which
	   comes back as stile 0xCD (unreachable from the keyboard since V22) */
	for (b = 1; b < 256; b++) {
		back = kapl_from_unicode(kapl_to_unicode((unsigned char)b));
		if (b == 0x7C)
			CHECK(back == 0xCD, "0x7C should come back as 0xCD, got 0x%02X", back);
		else
			CHECK(back == b, "byte 0x%02X -> U+%04X -> 0x%02X", b,
				(unsigned)kapl_to_unicode((unsigned char)b), back);
	}
}

static void
test_aliases(void)
{
	int i;

	for (i = 0; i < KAPL_ALIAS_SIZE; i++)
		CHECK(kapl_from_unicode(kapl_alias[i].unicode) == kapl_alias[i].kapl,
			"alias U+%04X", (unsigned)kapl_alias[i].unicode);
	CHECK(kapl_from_unicode(0x1F600) == -1, "emoji should be unmappable");
	CHECK(kapl_from_unicode(0x00E9) == 0xE9, "Latin-1 e-acute passes through as raw byte");
}

static void
expect_font(const char *utf8, const char *expect, int expect_status, const char *label)
{
	unsigned char out[64];
	int st, n;

	n = kapl_utf8_to_font(utf8, out, sizeof out, &st);
	CHECK(n == (int)strlen(expect) && memcmp(out, expect, n) == 0,
		"%s: wrong bytes (got %d bytes)", label, n);
	CHECK(st == expect_status, "%s: status %d, expected %d", label, st, expect_status);
}

static void
test_utf8_in(void)
{
	unsigned char small[4];
	int st, n;

	expect_font("X\xE2\x86\x90\xE2\x8D\xB4\xE2\x8D\xB3" "3",
		"X\xFB\xD2\xC9" "3", 0, "X<-rho iota 3");
	expect_font("\xCF\x81\xCE\xB9", "\xD2\xC9", 0, "Greek rho iota aliases");
	expect_font("5|7", "5\xCD" "7", 0, "ASCII bar becomes stile");
	expect_font("\xC3\xA9", "\xE9", 0, "e-acute passes through");
	expect_font("\xF0\x9F\x98\x80" "A", "?A", KAPL_BADCHARS, "emoji");
	expect_font("\xC3", "?", KAPL_BADCHARS, "truncated 2-byte sequence");
	expect_font("\xE2\x8D" "A", "??A", KAPL_BADCHARS, "truncated 3-byte sequence");
	expect_font("\xC0\xAF", "??", KAPL_BADCHARS, "overlong encoding");
	expect_font("\xED\xA0\x80", "???", KAPL_BADCHARS, "surrogate");
	expect_font("", "", 0, "empty");

	n = kapl_utf8_to_font("ABCDEF", small, sizeof small, &st);
	CHECK(n == 3 && memcmp(small, "ABC", 4) == 0 && st == KAPL_TRUNCATED,
		"truncation: n=%d st=%d", n, st);
}

static void
test_utf8_out(void)
{
	char out[64];
	char small[4];
	int n;
	const unsigned char rho_iota[] = { 0xD2, 0xC9, '3', 0 };
	const unsigned char iota[] = { 0xC9, 0 };
	const unsigned char unmapped[] = { 0xE9, 0 };

	n = kapl_font_to_utf8(rho_iota, out, sizeof out);
	CHECK(n == 7 && strcmp(out, "\xE2\x8D\xB4\xE2\x8D\xB3" "3") == 0,
		"rho iota 3 out (n=%d)", n);

	n = kapl_font_to_utf8(unmapped, out, sizeof out);
	CHECK(n == 2 && strcmp(out, "\xC3\xA9") == 0, "unmapped 0xE9 -> Latin-1 (n=%d)", n);

	/* 3-byte character must not be split: 4-byte buffer holds it exactly */
	n = kapl_font_to_utf8(iota, small, sizeof small);
	CHECK(n == 3, "iota fits in 4-byte buffer (n=%d)", n);
	n = kapl_font_to_utf8(iota, small, 3);
	CHECK(n == 0 && small[0] == '\0', "iota must not be split in 3-byte buffer (n=%d)", n);
}

/*
 * Compare the core table against kapl_unicode_mapping.hpp. Every
 * "{0xKK, 0xUUUU}" pair in the .hpp file must appear in kapl_table[] and
 * vice versa. path may name the file or the directory containing it.
 */
static void
test_hpp_match(const char *path)
{
	static char buf[65536];
	char fname[1024];
	unsigned hk[256], hu[256];
	int nh = 0, i, j, found;
	unsigned k, u;
	size_t n;
	char *p;
	FILE *fp;

	snprintf(fname, sizeof fname, "%s/kapl_unicode_mapping.hpp", path);
	fp = fopen(fname, "rb");
	if (!fp) {
		snprintf(fname, sizeof fname, "%s", path);
		fp = fopen(fname, "rb");
	}
	if (!fp) {
		CHECK(0, "can't open %s (or %s/kapl_unicode_mapping.hpp)", path, path);
		return;
	}
	n = fread(buf, 1, sizeof buf - 1, fp);
	fclose(fp);
	buf[n] = '\0';
	CHECK(n < sizeof buf - 1, "%s: larger than expected, may be truncated", fname);

	for (p = strstr(buf, "{0x"); p; p = strstr(p + 1, "{0x")) {
		if (sscanf(p, "{0x%x , 0x%x}", &k, &u) == 2 && nh < 256) {
			hk[nh] = k;
			hu[nh] = u;
			nh++;
		}
	}
	CHECK(nh > 0, "%s: no table entries found (wrong file?)", fname);
	if (!nh) return;

	for (i = 0; i < nh; i++) {
		for (found = 0, j = 0; j < KAPL_TABLE_SIZE && !found; j++)
			found = (kapl_table[j].kapl == hk[i] && kapl_table[j].unicode == hu[i]);
		CHECK(found, "only in .hpp: {0x%02X, 0x%04X}", hk[i], hu[i]);
	}
	for (j = 0; j < KAPL_TABLE_SIZE; j++) {
		for (found = 0, i = 0; i < nh && !found; i++)
			found = (kapl_table[j].kapl == hk[i] && kapl_table[j].unicode == hu[i]);
		CHECK(found, "only in kapl_unicode.c: {0x%02X, 0x%04X}",
			kapl_table[j].kapl, (unsigned)kapl_table[j].unicode);
	}
	printf("kapl_unicode_test: compared %d entries against %s\n", nh, fname);
}

int
main(int argc, char **argv)
{
	test_tables();
	test_round_trip();
	test_aliases();
	test_utf8_in();
	test_utf8_out();
	if (argc > 1) test_hpp_match(argv[1]);
	printf("kapl_unicode_test: %s (%d table entries, %d aliases, %d failures)\n",
		failures ? "FAILED" : "all passed", KAPL_TABLE_SIZE, KAPL_ALIAS_SIZE, failures);
	return failures;
}
