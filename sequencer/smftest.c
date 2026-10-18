/*
	smftest.c -- test driver for the MIDI file reader (smf.c; Phase 9,
	step 1, Oct 16, 2026).

	Each test is a MIDI file, tests/smf/NAME.mid, and the dump of what
	the reader makes of it, tests/smf/NAME.expected: the file's header
	and tempo, then each track's end, grid and length, and each voice's
	columns exactly as ⎕MIDIREAD returns them (pitch, step, velocity,
	length, channel; steps and lengths in beats). A file the reader
	refuses dumps as the error.

	One more test is built in: "write" (Phase 9, step 2) writes tracks
	with smf_write() and reads each file back, so its .expected records
	the round trip; it has no .mid.

	Usage (the same shape as seqtest):
	  smftest             run every tests/smf/NAME.mid, in name order
	  smftest NAME ...    run these (NEW if no NAME.expected yet; read
	                      tests/smf/NAME.out)
	  smftest -r NAME ... record: copy the output to NAME.expected
	  smftest -p FILE     print FILE's dump (any MIDI file) and exit

	The dump goes to tests/smf/NAME.out; a pass removes it. Exit status
	0 if everything passed (or was recorded), 1 otherwise.

	Most fixtures are written by tests/smf/make_fixtures.py (kept with
	them, so they can be made again); t5000.mid (the person's old
	utility) and t5000m3.mid (MuseScore 3) are real files.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include "smf.h"

#define DIR_SMF	"tests/smf"
#define PATHSZ	1024

static unsigned char *
read_all(const char *path, size_t *len)
{
	FILE *fp;
	unsigned char *buf = NULL, *nb;
	size_t cap = 0, n = 0, got;

	fp = fopen(path, "rb");
	if (!fp) return NULL;
	for (;;)
	{
		if (n == cap)
		{
			cap = cap ? cap * 2 : 65536;
			nb = realloc(buf, cap);
			if (!nb) { free(buf); fclose(fp); return NULL; }
			buf = nb;
		}
		got = fread(buf + n, 1, cap - n, fp);
		if (got == 0) break;
		n += got;
	}
	fclose(fp);
	*len = n;
	return buf ? buf : malloc(1);
}

static void
num_or_none(FILE *out, const char *what, long v)
{
	if (v < 0) fprintf(out, "%s none", what);
	else fprintf(out, "%s %ld", what, v);
}

static void
dump_buf(FILE *out, const unsigned char *buf, size_t len)
{
	struct smf_file *f;
	struct smf_info info;
	struct smf_voice_info vi;
	struct smf_col *cols;
	int32_t ncols, i;
	int r, t, v, nv;

	r = smf_parse(buf, len, &f);
	if (r != SMF_OK)
	{
		fprintf(out, "error: %s\n", smf_strerror(r));
		return;
	}
	smf_get_info(f, &info);
	fprintf(out, "file: format %d, %d track%s, %d ticks per beat, tempo ",
	        info.format, info.ntracks, info.ntracks == 1 ? "" : "s",
	        info.division);
	if (info.tempo < 0) fprintf(out, "none");
	else fprintf(out, "%.10g", info.tempo);
	if (info.ts_num < 0) fprintf(out, ", time signature none\n");
	else fprintf(out, ", time signature %d/%d\n", info.ts_num, info.ts_den);

	for (t = 1; t <= info.ntracks; t++)
	{
		nv = smf_voices(f, t);
		smf_voice_info(f, t, nv ? 1 : 0, &vi);
		fprintf(out, "track %d: end %lld, grid %lld, length %lld "
		        "(%.10g beats%s), %d voice%s\n", t, (long long)vi.end,
		        (long long)vi.grid, (long long)vi.length,
		        (double)vi.length / info.division,
		        smf_exact(f, t) == 1 ? ", exact" : "", nv,
		        nv == 1 ? "" : "s");
		for (v = 1; v <= nv; v++)
		{
			smf_voice_info(f, t, v, &vi);
			fprintf(out, "  voice %d: %d note%s, channel %d, "
			        "pitches %d..%d, ", v, vi.notes,
			        vi.notes == 1 ? "" : "s", vi.channel, vi.low,
			        vi.high);
			num_or_none(out, "program", vi.program);
			/* V100: only when there is one, so the transcripts
			   from before stay as they were */
			if (vi.bank >= 0)
				fprintf(out, ", bank %d", vi.bank);
			fprintf(out, "\n");
			r = smf_voice_cols(f, t, v, &cols, &ncols);
			if (r != SMF_OK)
			{
				fprintf(out, "    error: %s\n", smf_strerror(r));
				continue;
			}
			for (i = 0; i < ncols; i++)
				fprintf(out, "    %4d %-12.10g %4d %-12.10g %3d\n",
				        cols[i].pitch, cols[i].step, cols[i].vel,
				        cols[i].len, cols[i].chan);
			free(cols);
		}
	}
	/* the calls' edges, the same for every file */
	fprintf(out, "edges: voices(0) %d, voices(%d) %d, cols(1,%d) %d, "
	        "info(1,%d) %d\n", smf_voices(f, 0), info.ntracks + 1,
	        smf_voices(f, info.ntracks + 1),
	        info.ntracks ? smf_voices(f, 1) + 1 : 1,
	        smf_voice_cols(f, 1, info.ntracks ? smf_voices(f, 1) + 1 : 1,
	                       &cols, &ncols),
	        info.ntracks ? smf_voices(f, 1) + 1 : 1,
	        smf_voice_info(f, 1, info.ntracks ? smf_voices(f, 1) + 1 : 1,
	                       &vi));
	smf_free(f);
}

static void
dump(FILE *out, const char *path)
{
	unsigned char *buf;
	size_t len = 0;

	buf = read_all(path, &len);
	if (!buf)
	{
		fprintf(out, "can't read %s\n", path);
		return;
	}
	dump_buf(out, buf, len);
	free(buf);
}

/* The writer (Phase 9, step 2): the built-in test "write". Each case
   writes tracks with smf_write(), shows the file's bytes for the short
   ones, then reads the file back with the reader and dumps it, so the
   round trip is what's recorded. Ticks are the sequencer's (2520 per
   beat). */
static void
hexdump(FILE *out, const unsigned char *b, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		fprintf(out, "%s%02x", i % 16 ? " " : (i ? "\n    " : "    "),
		        b[i]);
	fprintf(out, "\n");
}

static void
write_case(FILE *out, const char *what, const struct smf_wtrack *t, int nt,
           double tempo, int hex)
{
	unsigned char *buf;
	size_t len;
	int r;

	fprintf(out, "## %s\n", what);
	r = smf_write(t, nt, 2520, tempo, &buf, &len);
	if (r != SMF_OK)
	{
		fprintf(out, "write error: %s\n", smf_strerror(r));
		return;
	}
	fprintf(out, "%lu bytes\n", (unsigned long)len);
	if (hex) hexdump(out, buf, len);
	dump_buf(out, buf, len);
	free(buf);
}

#define B 2520			/* a beat */

static void
dump_write(FILE *out)
{
	/* a legato line, repeated pitch included (off then on) */
	static const struct smf_wnote a[] = {
		{ 0, B, 60, 100, 1 }, { B, 2 * B, 60, 90, 1 },
		{ 2 * B, 4 * B, 64, 80, 1 } };
	/* triplets with gaps on channel 10 and a leading rest, in a 4.5
	   beat loop: 4.5 isn't on the triplets' grid, so grid rounding
	   alone would bring it back as 4 2/3; the writer's marker keeps
	   it 4.5 */
	static const struct smf_wnote c[] = {
		{ B, B + 420, 42, 100, 10 }, { B + 840, B + 1260, 42, 70, 10 },
		{ B + 1680, B + 2100, 42, 70, 10 }, { 3 * B, 4 * B, 38, 127, 10 } };
	/* quarter notes, then a rest to 3.5: kept by the marker (grid
	   rounding alone would make it 4) */
	static const struct smf_wnote d[] = {
		{ 0, B / 2, 50, 100, 2 }, { B, B + B / 2, 52, 100, 2 },
		{ 2 * B, 2 * B + B / 2, 53, 100, 2 } };
	/* eighth notes in a 4.5 beat loop: on their grid, so kept */
	static const struct smf_wnote e[] = {
		{ 0, B / 2, 60, 100, 3 }, { B / 2, B, 62, 100, 3 },
		{ 3 * B, 3 * B + B / 2, 64, 100, 3 },
		{ 7 * B / 2, 4 * B, 65, 100, 3 } };
	static const struct smf_wnote bad[] = { { 0, 0, 60, 100, 1 } };
	struct smf_wtrack t[5];
	int i;

	/* V100: no programs unless a case gives them */
	for (i = 0; i < 5; i++) {
		t[i].channel = 1;
		t[i].program = t[i].bank = -1;
	}

	t[0].notes = a; t[0].n = 3; t[0].length = 4 * B;
	write_case(out, "one track, tempo 120", t, 1, 120.0, 1);

	t[1].notes = NULL; t[1].n = 0; t[1].length = 0;	/* a gap */
	t[2].notes = c; t[2].n = 4; t[2].length = 9 * B / 2;
	t[3].notes = d; t[3].n = 3; t[3].length = 7 * B / 2;
	t[4].notes = e; t[4].n = 4; t[4].length = 9 * B / 2;
	write_case(out, "tracks 1, (2 empty), 3, 4, 5 at tempo 90", t, 5, 90.0, 0);

	write_case(out, "no tempo", t, 1, 0.0, 0);
	write_case(out, "tempo 2 (slower than a file holds)", t, 1, 2.0, 0);

	t[0].notes = bad; t[0].n = 1; t[0].length = B;
	write_case(out, "a note-off not after its start", t, 1, 120.0, 0);
	t[0].notes = a; t[0].n = 3; t[0].length = 3 * B;
	write_case(out, "a length before the last note-off", t, 1, 120.0, 0);
	t[0].length = 0x10000000LL + 4 * B;
	write_case(out, "a gap too long for a delta time", t, 1, 120.0, 0);
	write_case(out, "no tracks", t, 0, 120.0, 0);

	/* V100: programs and banks at each track's start, on its
	   channel; read back as the voices' program and bank */
	t[0].notes = a; t[0].n = 3; t[0].length = 4 * B;
	t[0].program = 40; t[0].bank = 129;
	write_case(out, "a program and bank 129 on channel 1", t, 1, 120.0, 1);
	t[0].bank = -1;
	t[1].channel = 2; t[1].program = 7;		/* a track with no notes */
	t[2].channel = 10; t[2].program = 0; t[2].bank = 16383;
	t[3].channel = 2; t[3].program = 127; t[3].bank = 0;
	write_case(out, "programs 40, 7 (no notes), 0 bank 16383, 127 bank 0, "
	           "none", t, 5, 120.0, 0);
	t[1].program = -1;
	t[0].program = 128;
	write_case(out, "program 128", t, 1, 120.0, 0);
	t[0].program = 0; t[0].channel = 0;
	write_case(out, "a program on channel 0", t, 1, 120.0, 0);
	t[0].channel = 1; t[0].bank = 16384;
	write_case(out, "bank 16384", t, 1, 120.0, 0);
	t[0].program = -1;
	write_case(out, "bank 16384 with no program (not looked at)", t, 1,
	           120.0, 0);
}

static int
same_file(const char *a, const char *b)
{
	unsigned char *x, *y;
	size_t xl = 0, yl = 0;
	int same;

	x = read_all(a, &xl);
	y = read_all(b, &yl);
	same = x && y && xl == yl && memcmp(x, y, xl) == 0;
	free(x);
	free(y);
	return same;
}

static int
cmp_str(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

/* 0 pass/recorded, 1 fail/new */
static int
run_one(const char *name, int record)
{
	char mid[PATHSZ], out[PATHSZ], exp[PATHSZ];
	FILE *fp;

	snprintf(mid, sizeof mid, "%s/%s.mid", DIR_SMF, name);
	snprintf(out, sizeof out, "%s/%s.out", DIR_SMF, name);
	snprintf(exp, sizeof exp, "%s/%s.expected", DIR_SMF, name);
	fp = fopen(out, "wb");
	if (!fp)
	{
		printf("FAIL     %s  (can't write %s)\n", name, out);
		return 1;
	}
	if (!strcmp(name, "write")) dump_write(fp);	/* built in */
	else dump(fp, mid);
	fclose(fp);
	if (record)
	{
		remove(exp);			/* Windows rename() won't replace */
		if (rename(out, exp) != 0)
		{
			printf("FAIL     %s  (can't write %s)\n", name, exp);
			return 1;
		}
		printf("RECORDED %s\n", name);
		return 0;
	}
	fp = fopen(exp, "rb");
	if (!fp)
	{
		printf("NEW      %s  (review %s, then record with -r)\n", name, out);
		return 1;
	}
	fclose(fp);
	if (same_file(exp, out))
	{
		remove(out);
		printf("PASS     %s\n", name);
		return 0;
	}
	printf("FAIL     %s  (compare %s with %s)\n", name, out, exp);
	return 1;
}

int
main(int argc, char **argv)
{
	int record = 0, argi = 1, fails = 0, n = 0, cap = 0, i;
	char **names = NULL;
	DIR *d;
	struct dirent *e;

	if (argc == 3 && !strcmp(argv[1], "-p"))
	{
		dump(stdout, argv[2]);
		return 0;
	}
	if (argi < argc && !strcmp(argv[argi], "-r"))
	{
		record = 1;
		argi++;
		if (argi == argc)
		{
			fprintf(stderr, "smftest: -r needs test names\n");
			return 1;
		}
	}
	if (argi < argc)
	{
		for (; argi < argc; argi++)
			fails += run_one(argv[argi], record);
	}
	else
	{
		d = opendir(DIR_SMF);
		if (!d)
		{
			fprintf(stderr, "smftest: no %s\n", DIR_SMF);
			return 1;
		}
		while ((e = readdir(d)) != NULL)
		{
			size_t l = strlen(e->d_name);

			if (l <= 4 || strcmp(e->d_name + l - 4, ".mid")) continue;
			if (n == cap)
			{
				char **nn;

				cap = cap ? cap * 2 : 32;
				nn = realloc(names, (size_t)cap * sizeof *nn);
				if (!nn) return 1;
				names = nn;
			}
			names[n] = malloc(l - 3);
			if (!names[n]) return 1;
			memcpy(names[n], e->d_name, l - 4);
			names[n][l - 4] = '\0';
			n++;
		}
		closedir(d);
		/* the built-in writer test, tests/smf/write.expected */
		if (n == cap)
		{
			char **nn = realloc(names, (size_t)(cap + 1) * sizeof *nn);

			if (!nn) return 1;
			names = nn;
			cap++;
		}
		names[n] = malloc(6);
		if (!names[n]) return 1;
		strcpy(names[n++], "write");
		if (n) qsort(names, (size_t)n, sizeof *names, cmp_str);
		for (i = 0; i < n; i++)
		{
			fails += run_one(names[i], 0);
			free(names[i]);
		}
		free(names);
		printf("smftest: %d tests, %d failed\n", n, fails);
	}
	return fails ? 1 : 0;
}
