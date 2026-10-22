// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Keith Doyle

/*
	seqtest.c -- standalone test driver for the sequencer library.
	Links only libseq.a; no interpreter, no device.

	    seqtest                run every tests/NAME.seq
	    seqtest NAME ...       run the named tests
	    seqtest -r NAME ...    record: copy NAME.out to NAME.expected
	    seqtest -d DIR ...     tests in DIR instead of tests/
	    seqtest -i             report which clock timed waits use

	Works like the backend's run_tests: each test writes a transcript
	to NAME.out (each script line, then its output indented by two
	spaces) and compares it with NAME.expected. A test with no
	expected file reports NEW, which counts as a failure on purpose.
	Recording always names the tests; there is no record-everything.

	Script commands (one per line; '#' lines and blank lines are
	copied to the transcript):

	  open virtual | realtime | device PORT
	                            PORT may be a device name of several
	                            words, or left out: an empty name
	                            (Phase 8)
	  close
	  track T[:C] P:D P:D ...   P a pitch 0..127, or r (or -1) for a
	                            rest; D beats: 1, 0.5, 4/3, 1/7 ...
	                            Phase 8: P:D:V:L:C, each extra field
	                            optional (V velocity, L length in
	                            beats, C channel); - for the track's
	                            setting. Any extra field on any note
	                            uses seq_submit_track_ex().
	  clear T ...
	  mute T ... = V ...        one V for all, or one per track
	  loop T ... = V ...
	  vel  T ... = V ...
	  gate T ... = F ...        Phase 8: F a fraction (1/2, 0.75)
	  prog T ... = P ...        V100: program 0-127, -1 none
	  bank T ... = B ...        V100: bank 0-16383, -1 none
	  play stop | continue | restart
	  tempo [BPM]               bare: report
	  meter [N D]               V101: the meter for MIDI files, -1 -1
	                            none; bare: report
	  name T [WORDS ...]        V101: track T's name, the words
	                            rejoined with one space (none: no
	                            name); \xHH is the byte HH
	  names                     V101: each track that has a name
	  runto X                   X in beats (8, 4/3) or ticks (t20160)
	  beatwait                  arm a beat wait, run to just past that
	                            beat (virtual), show the event
	  arm                       arm a beat wait only
	  cancel                    cancel the last armed wait
	  notes T ... = V ...       note events on/off (Phase 7c)
	  beats [0|1]               beat events for evwait (Phase 7c); bare:
	                            report
	  evarm                     arm an event wait only (Phase 7c)
	  late                      how late the last note event was handed
	                            over, ms (Phase 7c)
	  evwait [B]                arm an event wait, run the virtual clock
	                            to its event (limit B beats, default
	                            10000), show the event and where the
	                            clock holds
	  state                     the state and one line per track
	  trackout T                a track's notes as a MIDI file gets them
	                            (seq_get_track_notes(), Phase 9): the
	                            pass's length, then each note's start,
	                            note-off, pitch, velocity and channel
	                            (V100: and, when it has one, the
	                            version's channel, program and bank)
	  log                       the log since the last `log`
	  logticks                  the same without the time column
	                            (repeatable on the real clock)
	  logdrop                   empty the log, showing only how many
	                            lines it held
	  failwrites on | off       simulated unplug
	  panic
	  sleep MS                  real time
	  stress MS                 hammer the API from several threads
	                            (for check_tsan)
	  devdiag                   the device backend's diagnostics
	                            (Phase 3; times left out)
	  ports                     the device output ports (Phase 3)
	  fakedev ...               the fake device (check_device builds
	                            only; commands in seq_fake_dev.c)
*/

#include "seq.h"
#include "seq_internal.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>

#ifdef SEQ_DEVICE_TEST
#include "seq_fake_dev.h"
#endif

#define TPB SEQ_TICKS_PER_BEAT
#define MAXNOTES 4096

static FILE *out;

/* ── Events from the callback ─────────────────────────────────────── */

static pthread_mutex_t ev_lock = PTHREAD_MUTEX_INITIALIZER;
static char ev_text[4096];
static size_t ev_len;
static int ev_count;

static void on_event(void *ctx, const struct seq_event *ev)
{
	char line[512];
	int n;

	(void)ctx;
	if (ev->kind == SEQ_EV_BEAT)
		n = snprintf(line, sizeof line, "  event: BEAT %lld (id %u)\n",
		             (long long)ev->beat, (unsigned)ev->request_id);
	else if (ev->kind == SEQ_EV_NOTES) {
		/* track, pitch, column (from 0), count, for each */
		int i;

		n = snprintf(line, sizeof line, "  event: NOTES %d (id %u):",
		             ev->nnotes, (unsigned)ev->request_id);
		for (i = 0; i < ev->nnotes && n < (int)sizeof line - 48; i++)
			n += snprintf(line + n, sizeof line - (size_t)n,
			              " t%d p%d i%d k%d", ev->notes[i].track,
			              ev->notes[i].pitch, (int)ev->notes[i].index,
			              (int)ev->notes[i].count);
		n += snprintf(line + n, sizeof line - (size_t)n, "\n");
	} else
		n = snprintf(line, sizeof line,
		             "  event: STOPPED %s (id %u)\n",
		             ev->reason == SEQ_OK ? "OK" : "PORT LOST",
		             (unsigned)ev->request_id);
	pthread_mutex_lock(&ev_lock);
	if (ev_len + (size_t)n < sizeof ev_text) {
		memcpy(ev_text + ev_len, line, (size_t)n);
		ev_len += (size_t)n;
	}
	ev_count++;
	pthread_mutex_unlock(&ev_lock);
}

static void discard_events(void)
{
	pthread_mutex_lock(&ev_lock);
	ev_len = 0;
	pthread_mutex_unlock(&ev_lock);
}

static void flush_events(void)
{
	pthread_mutex_lock(&ev_lock);
	fwrite(ev_text, 1, ev_len, out);
	ev_len = 0;
	pthread_mutex_unlock(&ev_lock);
}

/* V101: words w[0..nw-1] rejoined with one space into buf (size n),
   \xHH as the byte HH. 0 if malformed or too long for buf. */
static int join_words(char **w, int nw, char *buf, size_t n)
{
	size_t k = 0;
	int i;
	const char *c;

	for (i = 0; i < nw; i++) {
		if (i > 0) {
			if (k + 1 >= n)
				return 0;
			buf[k++] = ' ';
		}
		for (c = w[i]; *c; c++) {
			int b = (unsigned char)*c;

			if (c[0] == '\\' && c[1] == 'x') {
				char hex[3] = { c[2], c[2] ? c[3] : 0, 0 };
				char *end;

				b = (int)strtol(hex, &end, 16);
				if (!hex[0] || !hex[1] || *end || b == 0)
					return 0;
				c += 3;
			}
			if (k + 1 >= n)
				return 0;
			buf[k++] = (char)b;
		}
	}
	buf[k] = '\0';
	return 1;
}

/* ── Formatting ───────────────────────────────────────────────────── */

static const char *err_name(int e)
{
	static const char *names[] = {
		"SEQ_OK", "SEQ_ERR_NOT_OPEN", "SEQ_ERR_ALREADY_OPEN",
		"SEQ_ERR_PORT", "SEQ_ERR_IO", "SEQ_ERR_TRACK",
		"SEQ_ERR_CHANNEL", "SEQ_ERR_PITCH", "SEQ_ERR_DURATION",
		"SEQ_ERR_LENGTH", "SEQ_ERR_VALUE", "SEQ_ERR_TEMPO",
		"SEQ_ERR_NOT_PLAYING", "SEQ_ERR_NOMEM", "SEQ_ERR_BACKEND",
		"SEQ_ERR_PORT_BUSY", "SEQ_ERR_PORT_ACCESS", "SEQ_ERR_PORT_NAME",
		"SEQ_ERR_NO_EVENT"
	};

	return (e >= 0 && e < (int)(sizeof names / sizeof names[0]))
	       ? names[e] : "SEQ_ERR_?";
}

/* 1 if it was an error (and says so). */
static int report(int r)
{
	if (r == SEQ_OK)
		return 0;
	fprintf(out, "  error: %s (%s)\n", err_name(r), seq_strerror(r));
	return 1;
}

/* A tick as beats with 3 decimals, plus the exact tick. */
static void beats(char *buf, size_t n, int64_t tick)
{
	long long milli = ((long long)tick * 1000 + TPB / 2) / TPB;

	snprintf(buf, n, "%lld.%03lld (t%lld)", milli / 1000, milli % 1000,
	         (long long)tick);
}

static const char *state_name(int s)
{
	switch (s) {
	case SEQ_CLOSED:    return "CLOSED";
	case SEQ_STOPPED:   return "STOPPED";
	case SEQ_PLAYING:   return "PLAYING";
	case SEQ_PORT_LOST: return "PORT_LOST";
	default:            return "?";
	}
}

/* ── Parsing ──────────────────────────────────────────────────────── */

/* A number: 3, -1, 0.5, 4/3. Returns 0 if not a number. */
static int parse_num(const char *s, double *v)
{
	char *end;
	double a, b;

	a = strtod(s, &end);
	if (end == s)
		return 0;
	if (*end == '/') {
		const char *d = end + 1;

		b = strtod(d, &end);
		if (end == d || b == 0)
			return 0;
		a /= b;
	}
	if (*end != '\0')
		return 0;
	*v = a;
	return 1;
}

static int parse_int(const char *s, int *v)
{
	char *end;
	long x = strtol(s, &end, 10);

	if (end == s || *end != '\0')
		return 0;
	*v = (int)x;
	return 1;
}

/* Splits a line into words (in place). */
static int split(char *line, char **w, int max)
{
	int n = 0;
	char *p = line;

	while (*p && n < max) {
		while (*p && isspace((unsigned char)*p))
			p++;
		if (!*p)
			break;
		w[n++] = p;
		while (*p && !isspace((unsigned char)*p))
			p++;
		if (*p)
			*p++ = '\0';
	}
	return n;
}

/* "T ... = V ..." for the list calls. */
static int parse_list(char **w, int nw, int *tracks, int *values, int *n)
{
	int i, k = 0, nv = 0, eq = -1;

	for (i = 0; i < nw; i++)
		if (strcmp(w[i], "=") == 0)
			eq = i;
	if (eq < 0 && values)
		return 0;
	for (i = 0; i < (values ? eq : nw); i++)
		if (k >= SEQ_MAX_TRACKS * 2 || !parse_int(w[i], &tracks[k++]))
			return 0;
	if (values) {
		for (i = eq + 1; i < nw; i++)
			if (nv >= SEQ_MAX_TRACKS * 2
			    || !parse_int(w[i], &values[nv++]))
				return 0;
		if (nv == 1)
			for (i = 1; i < k; i++)
				values[i] = values[0];
		else if (nv != k)
			return 0;
	}
	*n = k;
	return 1;
}

static void print_ints(const char *label, const int *v, int n)
{
	int i;

	fprintf(out, "  %s:", label);
	if (n == 0)
		fprintf(out, " (none)");
	for (i = 0; i < n; i++)
		fprintf(out, " %d", v[i]);
	fprintf(out, "\n");
}

static void sleep_ms(int ms)
{
	struct timespec ts;

	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (long)(ms % 1000) * 1000000;
	nanosleep(&ts, NULL);
}

/* ── Log output ───────────────────────────────────────────────────── */

static void show_log(int ticks_only)
{
	size_t len = 0;
	char *buf, *p, *line;

	seq_virtual_log(NULL, 0, &len, 0);
	buf = malloc(len + 1);
	if (!buf)
		return;
	report(seq_virtual_log(buf, len + 1, &len, 1));
	buf[len] = '\0';
	if (ticks_only == 2) {		/* logdrop: count only */
		long k = 0;

		for (p = buf; *p; p++)
			k += (*p == '\n');
		fprintf(out, "  (%ld lines dropped)\n", k);
		free(buf);
		return;
	}
	for (line = buf; *line; line = p) {
		p = strchr(line, '\n');
		p = p ? p + 1 : line + strlen(line);
		if (ticks_only) {
			/* drop the third field (the time) */
			char *a = strchr(line, ' ');
			char *b = a ? strchr(a + 1, ' ') : NULL;
			char *c = b ? strchr(b + 1, ' ') : NULL;

			if (b && c) {
				fprintf(out, "  %.*s", (int)(b - line), line);
				fwrite(c, 1, (size_t)(p - c), out);
				continue;
			}
		}
		fprintf(out, "  ");
		fwrite(line, 1, (size_t)(p - line), out);
	}
	free(buf);
}

static void show_state(void)
{
	struct seq_track_info info[SEQ_MAX_TRACKS];
	int st, n, i;
	int64_t tick;
	double tempo = 0;
	int bon = 0;
	char b[64], c[64], d[64];

	seq_get_state(&st, &tick);
	seq_get_tracks(info, &n);
	if (st != SEQ_CLOSED) {
		seq_get_tempo(&tempo);
		seq_get_beats(&bon);
	}
	beats(b, sizeof b, tick);
	/* Phase 7c settings shown only when on, so the transcripts of
	   the tests from before 7c stay as they were */
	fprintf(out, "  seq: %s beat %s tempo %g tracks %d%s\n",
	        state_name(st), b, tempo, n, bon ? " beats 1" : "");
	for (i = 0; i < n; i++) {
		beats(c, sizeof c, info[i].length);
		beats(d, sizeof d, info[i].position);
		/* Phase 8: the gate likewise, only when not 1 */
		char g[40] = "";

		if (info[i].gate != 1.0)
			snprintf(g, sizeof g, " gate %g", info[i].gate);
		/* V100: program and bank likewise, only when set */
		char pb[40] = "";

		if (info[i].program >= 0 || info[i].bank >= 0)
			snprintf(pb, sizeof pb, " prog %d bank %d",
			         info[i].program, info[i].bank);
		/* after V95: finished likewise, only when set */
		fprintf(out, "  track %d: notes %d ch %d mute %d loop %d vel %d"
		        " length %s pending %d position %s%s%s%s%s\n",
		        info[i].track, (int)info[i].notes, info[i].channel,
		        info[i].mute, info[i].loop, info[i].velocity, c,
		        info[i].pending, d, info[i].report ? " notes 1" : "", g,
		        info[i].finished ? " finished 1" : "", pb);
	}
}

/* ── Stress (for ThreadSanitizer) ─────────────────────────────────── */

static volatile int stress_stop;

static unsigned next_rand(unsigned *s)
{
	*s = *s * 1103515245u + 12345u;
	return (*s >> 16) & 0x7fff;
}

static void *stress_thread(void *arg)
{
	unsigned seed = (unsigned)(size_t)arg * 7919u + 1;
	int kind = (int)(size_t)arg;

	while (!__atomic_load_n(&stress_stop, __ATOMIC_RELAXED)) {
		int32_t p[8];
		double d[8];
		int t = 1 + (int)(next_rand(&seed) % 4), v, i, st;
		int64_t tick;
		uint32_t id;
		struct seq_track_info info[SEQ_MAX_TRACKS];

		switch (kind % 4) {
		case 0:
			for (i = 0; i < 8; i++) {
				p[i] = 40 + (int)(next_rand(&seed) % 40);
				d[i] = 1.0 / (1 + next_rand(&seed) % 4);
			}
			seq_submit_track(t, SEQ_UNCHANGED, p, d, 8, NULL);
			break;
		case 1:
			v = (int)(next_rand(&seed) % 2);
			seq_set_mute(&t, &v, 1, NULL);
			seq_set_tempo(200 + next_rand(&seed) % 800, NULL);
			break;
		case 2:
			seq_get_state(&st, &tick);
			seq_get_tracks(info, &i);
			break;
		default:
			if (seq_request_beat(&id) == SEQ_OK
			    && next_rand(&seed) % 2)
				seq_cancel_wait(id);
			/* Phase 7c */
			v = (int)(next_rand(&seed) % 2);
			seq_set_notes(&t, &v, 1, NULL);
			seq_set_beats((int)(next_rand(&seed) % 2), NULL);
			if (seq_request_event(&id) == SEQ_OK
			    && next_rand(&seed) % 2)
				seq_cancel_wait(id);
			if (next_rand(&seed) % 50 == 0)
				seq_clear(&t, 1, NULL);
			break;
		}
		sleep_ms(1);
	}
	return NULL;
}

static void stress(int ms)
{
	pthread_t th[8];
	int i;
	uint32_t id;

	__atomic_store_n(&stress_stop, 0, __ATOMIC_RELAXED);
	for (i = 0; i < 8; i++)
		pthread_create(&th[i], NULL, stress_thread, (void *)(size_t)i);
	sleep_ms(ms);
	__atomic_store_n(&stress_stop, 1, __ATOMIC_RELAXED);
	for (i = 0; i < 8; i++)
		pthread_join(th[i], NULL);
	/* a wait left armed would fire later: replace it, then cancel */
	if (seq_request_beat(&id) == SEQ_OK)
		seq_cancel_wait(id);
	{	/* Phase 7c: and nothing left reporting */
		int t, z = 0;

		for (t = 1; t <= SEQ_MAX_TRACKS; t++)
			seq_set_notes(&t, &z, 1, NULL);
		seq_set_beats(0, NULL);
	}
	discard_events();		/* how many depends on timing */
	fprintf(out, "  done\n");
}

/* ── One script line ──────────────────────────────────────────────── */

static uint32_t last_id;

/* Phase 8: an optional extra field of a note ("-" or missing: the
   track's setting). 0 if malformed. */
static int extra_int(char **f, int32_t *v)
{
	char *c;
	int x;

	*v = SEQ_UNCHANGED;
	if (!*f)
		return 1;
	c = strchr(*f, ':');
	if (c)
		*c = '\0';
	if (strcmp(*f, "-") != 0) {
		if (!parse_int(*f, &x))
			return 0;
		*v = x;
	}
	*f = c ? c + 1 : NULL;
	return 1;
}

static int extra_num(char **f, double *v)
{
	char *c;

	*v = -1.0;
	if (!*f)
		return 1;
	c = strchr(*f, ':');
	if (c)
		*c = '\0';
	if (strcmp(*f, "-") != 0 && !parse_num(*f, v))
		return 0;
	*f = c ? c + 1 : NULL;
	return 1;
}

static void do_track(char **w, int nw)
{
	static int32_t pitch[MAXNOTES], vel[MAXNOTES], ch[MAXNOTES];
	static double dur[MAXNOTES], len[MAXNOTES];
	int track, chan = SEQ_UNCHANGED, i, n = 0, extra = 0, r;
	char *colon, buf[64];
	int64_t swap;

	if (nw < 2)
		goto bad;
	colon = strchr(w[1], ':');
	if (colon) {
		*colon = '\0';
		if (!parse_int(colon + 1, &chan))
			goto bad;
	}
	if (!parse_int(w[1], &track))
		goto bad;
	for (i = 2; i < nw && n < MAXNOTES; i++, n++) {
		char *c = strchr(w[i], ':');
		int p;

		if (!c)
			goto bad;
		*c = '\0';
		if (strcmp(w[i], "r") == 0)
			p = SEQ_REST;
		else if (!parse_int(w[i], &p))
			goto bad;
		pitch[n] = p;
		{
			char *f = strchr(c + 1, ':');	/* Phase 8 fields */

			if (f) {
				*f++ = '\0';
				extra = 1;
			}
			if (!parse_num(c + 1, &dur[n]))
				goto bad;
			if (!extra_int(&f, &vel[n]) || !extra_num(&f, &len[n])
			    || !extra_int(&f, &ch[n]) || f)
				goto bad;
		}
	}
	r = extra ? seq_submit_track_ex(track, chan, pitch, dur, vel, len, ch,
	                                n, &swap)
	          : seq_submit_track(track, chan, pitch, dur, n, &swap);
	if (report(r))
		return;
	beats(buf, sizeof buf, swap);
	fprintf(out, "  takes over at beat %s\n", buf);
	return;
bad:
	fprintf(out, "  ?? bad track command\n");
}

static void do_line(char *line)
{
	char *w[MAXNOTES + 4];
	int nw = split(line, w, MAXNOTES + 4);
	int tracks[SEQ_MAX_TRACKS * 2], values[SEQ_MAX_TRACKS * 2];
	int prev[SEQ_MAX_TRACKS * 2], n, r, v;
	double x;
	const char *cmd;

	if (nw == 0 || w[0][0] == '#')
		return;
	cmd = w[0];

	if (strcmp(cmd, "open") == 0) {
		struct seq_config cfg;
		char port[256];
		int k;

		memset(&cfg, 0, sizeof cfg);
		cfg.notify = on_event;
		if (nw >= 2 && strcmp(w[1], "virtual") == 0)
			cfg.backend = SEQ_BACKEND_VIRTUAL;
		else if (nw >= 2 && strcmp(w[1], "realtime") == 0)
			cfg.backend = SEQ_BACKEND_LOG_REALTIME;
		else if (nw >= 2 && strcmp(w[1], "device") == 0) {
			cfg.backend = SEQ_BACKEND_DEVICE;
			port[0] = '\0';	/* the words rejoined (Phase 8) */
			for (k = 2; k < nw; k++)
				snprintf(port + strlen(port),
				         sizeof port - strlen(port), "%s%s",
				         k > 2 ? " " : "", w[k]);
			cfg.port = port;
		} else
			goto bad;
		report(seq_open(&cfg));
	} else if (strcmp(cmd, "close") == 0) {
		int closed[SEQ_MAX_TRACKS];

		if (!report(seq_close(closed, &n)))
			print_ints("closed", closed, n);
	} else if (strcmp(cmd, "track") == 0) {
		do_track(w, nw);
	} else if (strcmp(cmd, "clear") == 0) {
		if (!parse_list(w + 1, nw - 1, tracks, NULL, &n))
			goto bad;
		if (!report(seq_clear(tracks, n, prev)))
			print_ints("held", prev, n);
	} else if (strcmp(cmd, "mute") == 0 || strcmp(cmd, "loop") == 0
	           || strcmp(cmd, "vel") == 0 || strcmp(cmd, "notes") == 0
	           || strcmp(cmd, "prog") == 0 || strcmp(cmd, "bank") == 0) {
		if (!parse_list(w + 1, nw - 1, tracks, values, &n))
			goto bad;
		r = cmd[0] == 'p' ? seq_set_program(tracks, values, n, prev)
		  : cmd[0] == 'b' ? seq_set_bank(tracks, values, n, prev)
		  : cmd[0] == 'm' ? seq_set_mute(tracks, values, n, prev)
		  : cmd[0] == 'l' ? seq_set_loop(tracks, values, n, prev)
		  : cmd[0] == 'n' ? seq_set_notes(tracks, values, n, prev)
		  : seq_set_velocity(tracks, values, n, prev);
		if (!report(r))
			print_ints("prev", prev, n);
	} else if (strcmp(cmd, "gate") == 0) {
		double gv[SEQ_MAX_TRACKS * 2], gp[SEQ_MAX_TRACKS * 2];
		int k, eq = -1, nv = 0;

		for (k = 1; k < nw; k++)
			if (strcmp(w[k], "=") == 0)
				eq = k;
		if (eq < 0 || !parse_list(w + 1, eq - 1, tracks, NULL, &n))
			goto bad;
		for (k = eq + 1; k < nw; k++)
			if (nv >= SEQ_MAX_TRACKS * 2 || !parse_num(w[k], &gv[nv++]))
				goto bad;
		if (nv == 1)
			for (k = 1; k < n; k++)
				gv[k] = gv[0];
		else if (nv != n)
			goto bad;
		if (!report(seq_set_gate(tracks, gv, n, gp))) {
			fprintf(out, "  prev:");
			for (k = 0; k < n; k++)
				fprintf(out, " %g", gp[k]);
			fprintf(out, "\n");
		}
	} else if (strcmp(cmd, "play") == 0) {
		int mode;

		if (nw < 2)
			goto bad;
		if (strcmp(w[1], "stop") == 0)
			mode = SEQ_STOP;
		else if (strcmp(w[1], "continue") == 0)
			mode = SEQ_CONTINUE;
		else if (strcmp(w[1], "restart") == 0)
			mode = SEQ_RESTART;
		else if (!parse_int(w[1], &mode))
			goto bad;
		if (!report(seq_play(mode, &v)))
			fprintf(out, "  was playing: %d\n", v);
	} else if (strcmp(cmd, "tempo") == 0) {
		if (nw == 1) {
			if (!report(seq_get_tempo(&x)))
				fprintf(out, "  tempo: %g\n", x);
		} else {
			double p;

			if (!parse_num(w[1], &x))
				goto bad;
			if (!report(seq_set_tempo(x, &p)))
				fprintf(out, "  prev: %g\n", p);
		}
	} else if (strcmp(cmd, "meter") == 0) {	/* V101 */
		int num, den, pn, pd;

		if (nw == 1) {
			if (!report(seq_get_meter(&num, &den)))
				fprintf(out, "  meter: %d %d\n", num, den);
		} else {
			if (nw != 3 || !parse_int(w[1], &num)
			    || !parse_int(w[2], &den))
				goto bad;
			if (!report(seq_set_meter(num, den, &pn, &pd)))
				fprintf(out, "  prev: %d %d\n", pn, pd);
		}
	} else if (strcmp(cmd, "name") == 0) {	/* V101 */
		char nm[256], pv[SEQ_NAME_MAX + 1];
		int t;

		if (nw < 2 || !parse_int(w[1], &t)
		    || !join_words(w + 2, nw - 2, nm, sizeof nm))
			goto bad;
		if (!report(seq_set_name(t, nm, pv)))
			fprintf(out, "  prev: '%s'\n", pv);
	} else if (strcmp(cmd, "names") == 0) {	/* V101 */
		char nm[SEQ_NAME_MAX + 1];
		int t;

		for (t = 1; t <= SEQ_MAX_TRACKS; t++) {
			if (report(seq_get_name(t, nm)))
				break;
			if (nm[0])
				fprintf(out, "  %d: '%s'\n", t, nm);
		}
	} else if (strcmp(cmd, "runto") == 0) {
		int64_t t;

		if (nw < 2)
			goto bad;
		if (w[1][0] == 't')
			t = strtoll(w[1] + 1, NULL, 10);
		else if (parse_num(w[1], &x))
			t = (int64_t)(x * TPB + 0.5);
		else
			goto bad;
		report(seq_virtual_run_to(t));
	} else if (strcmp(cmd, "arm") == 0 || strcmp(cmd, "beatwait") == 0) {
		int st;
		int64_t tick;

		if (report(seq_request_beat(&last_id)))
			return;
		fprintf(out, "  armed (id %u)\n", (unsigned)last_id);
		if (cmd[0] == 'b') {
			/* The beat asked for is the next whole beat at or
			   after the held position; run just past it. */
			seq_get_state(&st, &tick);
			tick = ((tick + TPB - 1) / TPB) * TPB;
			report(seq_virtual_run_to(tick + 1));
		}
	} else if (strcmp(cmd, "beats") == 0) {
		if (nw == 1) {
			if (!report(seq_get_beats(&v)))
				fprintf(out, "  beats: %d\n", v);
		} else {
			int p;

			if (!parse_int(w[1], &v))
				goto bad;
			if (!report(seq_set_beats(v, &p)))
				fprintf(out, "  prev: %d\n", p);
		}
	} else if (strcmp(cmd, "evarm") == 0 || strcmp(cmd, "evwait") == 0) {
		int st;
		int64_t tick;
		char b[64];

		x = 10000;
		if (nw >= 2 && !parse_num(w[1], &x))
			goto bad;
		if (report(seq_request_event(&last_id)))
			return;
		/* an event handed over during the arm prints before this */
		flush_events();
		fprintf(out, "  armed (id %u)\n", (unsigned)last_id);
		if (cmd[2] == 'w') {
			report(seq_virtual_run_to_event((int64_t)(x * TPB + 0.5)));
			flush_events();
			seq_get_state(&st, &tick);
			beats(b, sizeof b, tick);
			fprintf(out, "  holds at %s\n", b);
		}
	} else if (strcmp(cmd, "late") == 0) {
		if (!report(seq_get_note_late(&x)))
			fprintf(out, "  late: %.3f ms\n", x);
	} else if (strcmp(cmd, "cancel") == 0) {
		report(seq_cancel_wait(last_id));
	} else if (strcmp(cmd, "state") == 0) {
		show_state();
	} else if (strcmp(cmd, "trackout") == 0) {	/* Phase 9 */
		struct seq_note_out *no;
		int32_t nn, k;
		int64_t len;
		int tch, tpg, tbk;
		char b[64], c[64];

		if (nw < 2 || !parse_int(w[1], &v))
			goto bad;
		if (report(seq_get_track_notes(v, &no, &nn, &len, &tch, &tpg,
		                               &tbk)))
			return;
		beats(b, sizeof b, len);
		fprintf(out, "  length %s, %d note%s\n", b, (int)nn,
		        nn == 1 ? "" : "s");
		/* V100: shown only when there is a program or bank, so
		   the transcripts from before stay as they were */
		if (tpg >= 0 || tbk >= 0)
			fprintf(out, "  ch %d prog %d bank %d\n", tch, tpg, tbk);
		for (k = 0; k < nn; k++) {
			beats(b, sizeof b, no[k].start);
			beats(c, sizeof c, no[k].off);
			fprintf(out, "  %s to %s: %d v%d ch%d\n", b, c,
			        no[k].pitch, no[k].vel, no[k].chan);
		}
		free(no);
	} else if (strcmp(cmd, "log") == 0) {
		show_log(0);
	} else if (strcmp(cmd, "logticks") == 0) {
		show_log(1);
	} else if (strcmp(cmd, "logdrop") == 0) {
		show_log(2);
	} else if (strcmp(cmd, "failwrites") == 0) {
		if (nw < 2)
			goto bad;
		report(seq_virtual_fail_writes(strcmp(w[1], "on") == 0));
	} else if (strcmp(cmd, "panic") == 0) {
		seq_panic();
	} else if (strcmp(cmd, "sleep") == 0) {
		if (nw < 2 || !parse_int(w[1], &v))
			goto bad;
		sleep_ms(v);
	} else if (strcmp(cmd, "ports") == 0) {
		struct seq_port_info pi[SEQ_MAX_PORTS];
		int k, np = seq_dev_list(pi, SEQ_MAX_PORTS);

		fprintf(out, "  %d port%s\n", np, np == 1 ? "" : "s");
		for (k = 0; k < np && k < SEQ_MAX_PORTS; k++)
			fprintf(out, "  %s  %s\n", pi[k].port, pi[k].name);
	} else if (strcmp(cmd, "devdiag") == 0) {
		struct seq_dev_diag dg;

		seq_dev_get_diag(&dg);
		fprintf(out, "  failed: %s", dg.step ? dg.step : "(none)");
		if (dg.step)
			fprintf(out, " (%s)", dg.errtext[0] ? dg.errtext
			                                    : "no error code");
		fprintf(out, "\n  writes %ld, waited %ld, subdevice %d\n",
		        dg.writes, dg.waits, dg.subdevice);
#ifdef SEQ_DEVICE_TEST
	} else if (strcmp(cmd, "fakedev") == 0) {
		if (fake_command(w, nw, out))
			goto bad;
#endif
	} else if (strcmp(cmd, "stress") == 0) {
		if (nw < 2 || !parse_int(w[1], &v))
			goto bad;
		stress(v);
	} else {
		goto bad;
	}
	return;
bad:
	fprintf(out, "  ?? bad command\n");
}

/* ── Running and comparing ────────────────────────────────────────── */

static char *read_file(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	char *buf;
	long n;

	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = malloc((size_t)n + 1);
	if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
		free(buf);
		buf = NULL;
	}
	fclose(f);
	if (buf) {
		buf[n] = '\0';
		if (len)
			*len = (size_t)n;
	}
	return buf;
}

static int split_lines(char *text, char ***lines)
{
	int n = 0, cap = 256;
	char **v = malloc(sizeof *v * cap), *p = text;

	while (*p) {
		char *e = strchr(p, '\n');

		if (n == cap)
			v = realloc(v, sizeof *v * (cap *= 2));
		v[n++] = p;
		if (!e)
			break;
		*e = '\0';
		p = e + 1;
	}
	*lines = v;
	return n;
}

/* Prints a line diff: '-' expected, '+' got, two lines of context. */
static void diff(char *exp, char *got)
{
	char **a, **b;
	int na = split_lines(exp, &a), nb = split_lines(got, &b);
	int i, j, k, shown = -1;
	unsigned short *L;

	if ((long)(na + 1) * (nb + 1) > 40000000L || na > 65000 || nb > 65000) {
		for (i = 0; i < na && i < nb && strcmp(a[i], b[i]) == 0; i++)
			;
		printf("  first difference at line %d:\n  - %s\n  + %s\n", i + 1,
		       i < na ? a[i] : "(end)", i < nb ? b[i] : "(end)");
		goto done;
	}
	/* L[i][j] = LCS length of a[i..] and b[j..] */
	L = calloc((size_t)(na + 1) * (nb + 1), sizeof *L);
	for (i = na - 1; i >= 0; i--)
		for (j = nb - 1; j >= 0; j--)
			L[i * (nb + 1) + j] = strcmp(a[i], b[j]) == 0
			    ? L[(i + 1) * (nb + 1) + j + 1] + 1
			    : (L[(i + 1) * (nb + 1) + j] > L[i * (nb + 1) + j + 1]
			       ? L[(i + 1) * (nb + 1) + j]
			       : L[i * (nb + 1) + j + 1]);
	/* walk it, marking each output line: ' ', '-' or '+' */
	{
		int cap = na + nb, m = 0;
		char *mark = malloc((size_t)cap + 1);
		char **txt = malloc(sizeof *txt * ((size_t)cap + 1));

		i = j = 0;
		while (i < na || j < nb) {
			if (i < na && j < nb && strcmp(a[i], b[j]) == 0) {
				mark[m] = ' '; txt[m++] = a[i]; i++; j++;
			} else if (j < nb && (i == na || L[i * (nb + 1) + j + 1]
			           >= L[(i + 1) * (nb + 1) + j])) {
				mark[m] = '+'; txt[m++] = b[j]; j++;
			} else {
				mark[m] = '-'; txt[m++] = a[i]; i++;
			}
		}
		for (k = 0; k < m; k++) {
			int near = 0, q;

			for (q = k - 2; q <= k + 2; q++)
				if (q >= 0 && q < m && mark[q] != ' ')
					near = 1;
			if (!near)
				continue;
			if (shown >= 0 && k > shown + 1)
				printf("  ...\n");
			printf("  %c %s\n", mark[k], txt[k]);
			shown = k;
		}
		free(mark);
		free(txt);
	}
	free(L);
done:
	free(a);
	free(b);
}

/* 0 pass, 1 fail, 2 new, 3 missing script */
static int run_test(const char *dir, const char *name, int record)
{
	char path[1024], opath[1024], epath[1024], line[65536];
	FILE *in;
	char *exp, *got;
	int r;

	snprintf(path, sizeof path, "%s/%s.seq", dir, name);
	snprintf(opath, sizeof opath, "%s/%s.out", dir, name);
	snprintf(epath, sizeof epath, "%s/%s.expected", dir, name);

	if (record) {
		got = read_file(opath, NULL);
		if (!got) {
			printf("NOOUT %s (run it first)\n", name);
			return 1;
		}
		in = (FILE *)fopen(epath, "wb");
		if (!in) {
			free(got);
			return 1;
		}
		fputs(got, in);
		fclose(in);
		free(got);
		printf("RECORDED %s\n", name);
		return 0;
	}

	in = fopen(path, "rb");
	if (!in) {
		printf("MISSING %s\n", path);
		return 3;
	}
	out = fopen(opath, "wb");
	if (!out) {
		fclose(in);
		return 1;
	}
#ifdef SEQ_DEVICE_TEST
	fake_reset();
#endif
	while (fgets(line, sizeof line, in)) {
		size_t n = strlen(line);

		while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[--n] = '\0';
		fprintf(out, "%s\n", line);
		do_line(line);
		flush_events();
	}
	fclose(in);
	seq_virtual_fail_writes(0);
	seq_close(NULL, NULL);		/* every test starts closed */
	discard_events();		/* not part of the script */
	fclose(out);
	out = stdout;

	got = read_file(opath, NULL);
	exp = read_file(epath, NULL);
	if (!exp) {
		printf("NEW  %s\n", name);
		r = 2;
	} else if (got && strcmp(got, exp) == 0) {
		printf("PASS %s\n", name);
		r = 0;
	} else {
		printf("FAIL %s\n", name);
		if (got)
			diff(exp, got);
		r = 1;
	}
	free(got);
	free(exp);
	return r;
}

static int cmp_str(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

int main(int argc, char **argv)
{
	const char *dir = "tests";
	int record = 0, i, nfail = 0, nnames = 0, listed = 0;
	char **names = malloc(sizeof *names * (size_t)(argc + 1));

	out = stdout;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-r") == 0)
			record = 1;
		else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc)
			dir = argv[++i];
		else if (strcmp(argv[i], "-i") == 0) {
			printf("timed waits use %s\n",
			       seq_clock_is_monotonic()
			       ? "CLOCK_MONOTONIC"
			       : "CLOCK_REALTIME, waits capped at 50 ms"
			       " (monotonic not accepted)");
			return 0;
		} else
			names[nnames++] = argv[i];
	}
	if (record && nnames == 0) {
		fprintf(stderr, "seqtest: -r needs test names\n");
		return 2;
	}
	if (nnames == 0) {
		DIR *d = opendir(dir);
		struct dirent *e;
		int cap = argc + 1;

		if (!d) {
			fprintf(stderr, "seqtest: can't open %s\n", dir);
			return 2;
		}
		while ((e = readdir(d)) != NULL) {
			size_t n = strlen(e->d_name);

			if (n > 4 && strcmp(e->d_name + n - 4, ".seq") == 0) {
				if (nnames == cap)
					names = realloc(names,
					        sizeof *names * (size_t)(cap *= 2));
				names[nnames] = malloc(n - 3);
				memcpy(names[nnames], e->d_name, n - 4);
				names[nnames][n - 4] = '\0';
				nnames++;
			}
		}
		closedir(d);
		listed = 1;
		qsort(names, (size_t)nnames, sizeof *names, cmp_str);
	}
	for (i = 0; i < nnames; i++)
		if (run_test(dir, names[i], record) != 0)
			nfail++;
	if (!record)
		printf("%d of %d passed\n", nnames - nfail, nnames);
	if (listed)
		for (i = 0; i < nnames; i++)
			free(names[i]);
	free(names);
	return nfail ? 1 : 0;
}
