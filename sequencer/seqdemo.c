// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Keith Doyle

/*
	seqdemo.c -- Phase 3 demo: plays through a real MIDI port.

	    seqdemo [-t BPM] [-b CH] [-m CH] [-d CH] [-P] PORT
	    seqdemo -l
	    seqdemo -T

	PORT     Linux: hw:C,D,S, hw:C,D or hw:C; Windows: a device number
	         (seqdemo -l lists them); or, either (Phase 8), part of a
	         port's name as listed, any case: the first match opens
	-l       list the output ports and exit
	-T       timer test and exit: how long 1 ms waits really take
	         (Windows: before and after timeBeginPeriod(1))
	-t BPM   tempo (default 100)
	-b CH    bass channel (default 1)
	-m CH    melody channel (default 2)
	-d CH    drum channel, 0 for none (default 0; 10 on a GM synth)
	-P       Ctrl-C takes the fatal_error() path instead of a normal
	         close: seq_panic(), then exit without closing anything

	Plays an 8-beat bass loop (track 1), a 16-beat melody (track 2)
	that gets a new variation every pass, sent while the previous one
	plays, and with -d a 4-beat drum loop (track 3). Links only
	libseq.a.

	Ctrl-C (or SIGTERM) closes the sequencer normally: note-offs for
	everything sounding, then the port is closed, which waits for them
	to reach the device. A second Ctrl-C exits at once.

	After each melody pass it prints how late the notes went out
	compared with when they were due (worst, average, how many over
	2 ms), measured by the library on the real clock.

	If the port is lost (cable unplugged), it reports what the write
	saw and how long the close took, then tries to reopen the same
	port once a second; plug it back in and it carries on from where
	it stopped. (The library never reopens by itself; this program
	does.)
*/

#include "seq.h"
#include "seq_internal.h"

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

#define TPB SEQ_TICKS_PER_BEAT

static volatile sig_atomic_t got_signal;

static void on_signal(int sig)
{
	got_signal = 1;
	signal(sig, SIG_DFL);		/* a second one ends it at once */
}

#ifdef _WIN32
/* Windows: the console's own Ctrl-C handling, rather than the C
   runtime's emulation of signals (Phase 6: under the person's shell a
   Ctrl-C ended the program at once, "Trace/BPT trap", before it could
   close). Also catches Ctrl-Break and closing the console window. A
   second Ctrl-C ends the program at once. */
static BOOL WINAPI on_console(DWORD ev)
{
	if (got_signal)
		return FALSE;		/* the default: end the program */
	got_signal = 1;
	if (ev == CTRL_CLOSE_EVENT)
		Sleep(3000);		/* the process ends when this returns:
					   give main() time to close */
	return TRUE;
}
#endif

static double now_s(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void sleep_ms(int ms)
{
	struct timespec ts;

	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (long)(ms % 1000) * 1000000;
	nanosleep(&ts, NULL);
}

static void show_diag(const char *what)
{
	struct seq_dev_diag d;

	seq_dev_get_diag(&d);
	printf("  %s: ", what);
	if (d.step)
		printf("failed at %s (%s)\n", d.step,
		       d.errtext[0] ? d.errtext : "no error code");
	else
		printf("no failure recorded\n");
	printf("  %ld messages written, %ld had to wait for room"
	       " (longest %.1f ms); last close took %.1f ms\n",
	       d.writes, d.waits, d.wait_max_ns / 1e6, d.close_ns / 1e6);
}

/* Timing since the last call, then start counting again. */
static void show_timing(const char *what)
{
	struct seq_timing t;

	seq_get_timing(&t, 1);
	if (t.ticks == 0)
		return;
	printf("  %s: %ld events, worst %.2f ms late, average %.3f ms,"
	       " %ld over 2 ms\n", what, t.ticks, t.late_max_ns / 1e6,
	       t.late_sum_ns / 1e6 / t.ticks, t.late_2ms);
}

/* ── Timer test (-T) ──────────────────────────────────────────────── */

static pthread_mutex_t tt_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  tt_cond = PTHREAD_COND_INITIALIZER;

/* One 1 ms wait of the kind the playback thread uses. */
static void wait_cond_1ms(void)
{
	struct timespec ts;

	pthread_mutex_lock(&tt_lock);
#ifdef _WIN32
	ts.tv_sec = 0;
	ts.tv_nsec = 1000000;
	pthread_cond_timedwait_relative_np(&tt_cond, &tt_lock, &ts);
#else
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_nsec += 1000000;
	if (ts.tv_nsec >= 1000000000) {
		ts.tv_sec++;
		ts.tv_nsec -= 1000000000;
	}
	pthread_cond_timedwait(&tt_cond, &tt_lock, &ts);
#endif
	pthread_mutex_unlock(&tt_lock);
}

#ifdef _WIN32
/* The wait the playback thread uses on Windows (seq.c, timed_wait()). */
static HANDLE tt_ev;

static void wait_event_1ms(void)
{
	WaitForSingleObject(tt_ev, 1);
}
#endif

static void wait_sleep_1ms(void)
{
#ifdef _WIN32
	Sleep(1);
#else
	sleep_ms(1);
#endif
}

static void time_it(const char *what, void (*f)(void))
{
	double worst = 0, sum = 0, t;
	int i;

	for (i = 0; i < 20; i++) {
		t = now_s();
		f();
		t = (now_s() - t) * 1000;
		sum += t;
		if (t > worst)
			worst = t;
	}
	printf("  %-24s average %6.2f ms, worst %6.2f ms\n", what, sum / 20,
	       worst);
}

static int timer_test(void)
{
	printf("twenty 1 ms waits of each kind:\n");
	time_it("sleep", wait_sleep_1ms);
	time_it("condition-variable wait", wait_cond_1ms);
#ifdef _WIN32
	tt_ev = CreateEvent(NULL, FALSE, FALSE, NULL);
	time_it("event wait (playback)", wait_event_1ms);
	printf("after timeBeginPeriod(1) (returned %u, 0 = accepted):\n",
	       (unsigned)timeBeginPeriod(1));
	time_it("sleep", wait_sleep_1ms);
	time_it("condition-variable wait", wait_cond_1ms);
	time_it("event wait (playback)", wait_event_1ms);
	timeEndPeriod(1);
#endif
	return 0;
}

static int list_ports(void)
{
	struct seq_port_info p[64];
	int i, n = seq_dev_list(p, 64);

	if (n == 0)
		printf("no MIDI output ports found\n");
	for (i = 0; i < n && i < 64; i++)
		printf("%-12s %s\n", p[i].port, p[i].name);
	return 0;
}

/* ── The music ────────────────────────────────────────────────────── */

static unsigned rnd(unsigned *s)
{
	*s = *s * 1103515245u + 12345u;
	return (*s >> 16) & 0x7fff;
}

/* A 16-beat melody in A minor pentatonic: a random walk with mixed
   lengths, the odd rest and triplet, always exactly 16 beats. */
static int melody(int k, int32_t *p, double *d)
{
	static const int scale[] = { 57, 60, 62, 64, 67, 69, 72, 74, 76 };
	unsigned seed = 12345u + 7919u * (unsigned)k;
	double left = 16, len;
	int n = 0, at = 4, j;

	while (left > 1e-9 && n < 60) {
		switch (rnd(&seed) % 6) {
		case 0:  len = 0.5; break;
		case 1:
		case 2:  len = 1;   break;
		case 3:  len = 1.5; break;
		case 4:  len = 2;   break;
		default: len = -1;  break;	/* a triplet */
		}
		if (len < 0 && left >= 1 && n < 57) {
			for (j = 0; j < 3; j++) {
				at += (int)(rnd(&seed) % 3) - 1;
				at = at < 0 ? 0 : at > 8 ? 8 : at;
				p[n] = scale[at];
				d[n++] = 1.0 / 3;
			}
			left -= 1;
			continue;
		}
		if (len < 0)
			len = 0.5;
		if (len > left)
			len = left;
		at += (int)(rnd(&seed) % 5) - 2;
		at = at < 0 ? 0 : at > 8 ? 8 : at;
		p[n] = (rnd(&seed) % 9 == 0 && n > 0) ? SEQ_REST : scale[at];
		d[n++] = len;
		left -= len;
	}
	return n;
}

static int submit(int track, int chan, const int32_t *p, const double *d,
                  int n, const char *what)
{
	int64_t at;
	int r = seq_submit_track(track, chan, p, d, n, &at);

	if (r != SEQ_OK)
		printf("%s: %s\n", what, seq_strerror(r));
	else
		printf("%s: takes over at beat %lld\n", what,
		       (long long)(at / TPB));
	return r;
}

/* ── Main ─────────────────────────────────────────────────────────── */

static void usage(void)
{
	fprintf(stderr, "usage: seqdemo [-t BPM] [-b CH] [-m CH] [-d CH] [-P]"
	        " PORT\n       seqdemo -l    (list ports)\n"
	        "       seqdemo -T    (timer test)\n"
#ifdef _WIN32
	        "  PORT: a device number,\n"
#else
	        "  PORT: hw:C,D,S, hw:C,D or hw:C,\n"
#endif
	        "        or part of a port's name (seqdemo -l)\n"
	        );
	exit(2);
}

int main(int argc, char **argv)
{
	static const int32_t bass_p[] = { 45, SEQ_REST, 45, 52, 55,
	                                  53, SEQ_REST, 53, 52, 48 };
	static const double bass_d[] = { 1, 0.5, 0.5, 1, 1,
	                                 1, 0.5, 0.5, 1, 1 };
	static const int32_t drum_p[] = { 36, 42, 38, 42 };
	static const double drum_d[] = { 1, 1, 1, 1 };
	struct seq_config cfg;
	double tempo = 100, lost_at = 0, last_try = 0, t0;
	int bass_ch = 1, mel_ch = 2, drum_ch = 0, panic_exit = 0;
	int i, r, st, var = 1, last_err = SEQ_OK, n;
	int32_t mp[64];
	double md[64];
	int64_t tick;
	char what[64];

	/* unbuffered: under a Unix-style shell on Windows stdout may be a
	   pipe, fully buffered, so messages would show up late or never */
	setvbuf(stdout, NULL, _IONBF, 0);
	for (i = 1; i < argc && argv[i][0] == '-'; i++) {
		if (!strcmp(argv[i], "-P"))
			panic_exit = 1;
		else if (!strcmp(argv[i], "-l"))
			return list_ports();
		else if (!strcmp(argv[i], "-T"))
			return timer_test();
		else if (i + 1 >= argc)
			usage();
		else if (!strcmp(argv[i], "-t"))
			tempo = atof(argv[++i]);
		else if (!strcmp(argv[i], "-b"))
			bass_ch = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-m"))
			mel_ch = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-d"))
			drum_ch = atoi(argv[++i]);
		else
			usage();
	}
	if (i != argc - 1)
		usage();

	memset(&cfg, 0, sizeof cfg);
	cfg.backend = SEQ_BACKEND_DEVICE;
	cfg.port = argv[i];
	r = seq_open(&cfg);
	if (r != SEQ_OK) {
		printf("can't open %s: %s\n", cfg.port, seq_strerror(r));
		show_diag("device");
		if (r == SEQ_ERR_PORT_ACCESS)
			printf("  (is this user in the audio group?)\n");
		else if (r == SEQ_ERR_PORT || r == SEQ_ERR_PORT_NAME)
			printf("  (seqdemo -l lists the ports)\n");
		return 1;
	}
	{
		struct seq_dev_diag d;

		seq_dev_get_diag(&d);
		if (d.subdevice >= 0)
			printf("opened %s (subdevice %d)\n", cfg.port, d.subdevice);
		else
			printf("opened %s\n", cfg.port);
	}

	r = seq_set_tempo(tempo, NULL);
	if (r != SEQ_OK)
		printf("tempo: %s\n", seq_strerror(r));
	if (submit(1, bass_ch, bass_p, bass_d, 10, "bass, track 1")
	    || (n = melody(var, mp, md), submit(2, mel_ch, mp, md, n,
	                                        "melody 1, track 2"))
	    || (drum_ch && submit(3, drum_ch, drum_p, drum_d, 4,
	                          "drums, track 3"))) {
		seq_close(NULL, NULL);
		return 1;
	}
	{
		int t = 1, v = 90;

		seq_set_velocity(&t, &v, 1, NULL);
	}

#ifdef _WIN32
	SetConsoleCtrlHandler(on_console, TRUE);
#else
	signal(SIGINT, on_signal);
#endif
	signal(SIGTERM, on_signal);
	seq_play(SEQ_RESTART, NULL);
	printf("playing at %g bpm; Ctrl-C to stop%s\n", tempo,
	       panic_exit ? " (panic exit)" : "");

	while (!got_signal) {
		sleep_ms(50);
		seq_get_state(&st, &tick);
		if (st == SEQ_PLAYING) {
			struct seq_track_info info[SEQ_MAX_TRACKS];

			seq_get_tracks(info, &n);
			for (i = 0; i < n; i++)
				if (info[i].track == 2 && !info[i].pending) {
					if (var > 1)	/* the first is at the start */
						show_timing("timing, last pass");
					var++;
					snprintf(what, sizeof what, "melody %d", var);
					n = melody(var, mp, md);
					submit(2, SEQ_UNCHANGED, mp, md, n, what);
					break;
				}
		} else if (st == SEQ_PORT_LOST) {
			double t = now_s();

			if (lost_at == 0) {
				lost_at = t;
				last_err = SEQ_OK;
				printf("port lost at beat %.2f (seen within 50 ms)\n",
				       (double)tick / TPB);
				show_diag("device");
				printf("  trying to reopen %s once a second\n",
				       cfg.port);
			}
			if (t - last_try >= 1) {
				last_try = t;
				r = seq_open(&cfg);
				if (r == SEQ_OK) {
					printf("reopened after %.1f s; continuing\n",
					       t - lost_at);
					lost_at = 0;
					seq_play(SEQ_CONTINUE, NULL);
				} else if (r != last_err) {
					printf("  reopen: %s\n", seq_strerror(r));
					last_err = r;
				}
			}
		} else {
			printf("sequencer state %d; stopping\n", st);
			break;
		}
	}

	if (panic_exit) {
		printf("\npanic exit: seq_panic(), then exit without closing\n");
		fflush(stdout);
		seq_panic();
		_Exit(70);
	}
	show_timing("timing, since the last report");
	printf("\nclosing\n");
	t0 = now_s();
	seq_close(NULL, NULL);
	printf("closed in %.1f ms\n", (now_s() - t0) * 1000);
	show_diag("device");
	return 0;
}
