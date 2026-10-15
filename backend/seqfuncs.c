/*
	seqfuncs.c -- the ⎕SEQ quad functions: the interpreter's glue to the
	MIDI sequencer library (../sequencer, seq.h). Module k=3 in links.c.

	The only interpreter file that includes seq.h. Everything else
	reaches the sequencer through the hooks at the end of this file:
	seqf_close() (dll_cleanup() in main.c, stopEngine() in main.cpp)
	and seqf_panic() (fatal_error() in main.c), plus seqf_virtual,
	which apl_cli's -S sets for the regression tests.

	Design: MIDI_SEQUENCER_NOTES.md ("APL interface", "Implementation
	rules", "Events and waiting"); user reference: SEQ_FUNCTION_HELP.md
	until each function is tested, then quad_function_help.md.

	⎕EVWAIT's name lives here (slot 12), as ⎕WCLICK's lives in
	grfuncs.c; since Phase 7b both call the one wait in evwait.c,
	which waits in the event hub (evhub.c). This file supplies the
	sequencer's side of it (see "Events for the event hub" below).
	⎕EVPOST (Phase 7a), the test function that feeds window events to
	the hub in apl_cli, lives here too.

	Phase 7c (Oct 12, 2026): ⎕SEQNOTES (slot 14) chooses which tracks
	report note events, ⎕SEQBEAT (slot 15) turns beat events on (off by
	default from then on), and the wait arms the library's event wait
	(seq_request_event()) instead of its beat wait.

	House rules followed here:
	  - Every function produces a new value: arguments are copied out
	    of the workspace first (into malloc'd C arrays -- the library
	    copies them again, so nothing points into the workspace once a
	    call returns), then popped (stack_pop), then a fresh result is
	    pushed. No swapdrop().
	  - SEQTEMPO, SEQPLAY and SEQBEAT are MONAD_OPT: stack_pop(1) only inside
	    "if (re)" (INVARIANTS.md, MONAD_OPT Part 4).
	  - Fractional results (SEQTEMPO, SEQTRACK's beat, SEQSTATE,
	    SEQRUNTO) are FCONST_CODE temps, as ⎕ATN builds them; whole
	    numbers go through push_val().
	  - New pattern, agreed (Phase 1): a whole-number argument may be a
	    float as long as it is whole (T←8÷2); a fraction is ERR_DOMAIN.
	    ⎕CTRL and ⎕RL reject floats outright.
	  - Elements are read by this file's elem(), not get_data()/
	    get_fdata(): those take a short index, so they can't reach past
	    element 32767, and a track has no length limit.

	The table below is permanent once shipped: compiled code holds
	module+index, never the name (INVARIANTS.md, "Quad-Function
	Module+Index Encoding"). Add new functions at the end only, never
	in the middle, never more than 31. No placeholder at index 0: that
	is needed only in k=0, where index 0 would compile to a zero byte.
	Here index 0 is byte 0x60, the internal comment character, which is
	safe: input_translate() (platform_translate.c) stops at a comment
	in its *input* (font byte 0xE3) before it writes any CHAR_SYS pair.
*/

#define SEQFUNCS_C 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parse.h"
#include "funcs.h"
#include "seq.h"
#include "evhub.h"	/* ⎕EVPOST; the sequencer's events and hooks */

short ev_wait_apl();	/* evwait.c: ⎕EVWAIT and ⎕WCLICK (Phase 7b) */

UCHAR seq_fnames[] = {
	'S','E','Q','O','P','E','N',0,		/* 0 */
	'S','E','Q','C','L','O','S','E',0,	/* 1 */
	'S','E','Q','T','R','A','C','K',0,	/* 2 */
	'S','E','Q','C','L','E','A','R',0,	/* 3 */
	'S','E','Q','M','U','T','E',0,		/* 4 */
	'S','E','Q','L','O','O','P',0,		/* 5 */
	'S','E','Q','V','E','L',0,		/* 6 */
	'S','E','Q','T','E','M','P','O',0,	/* 7 */
	'S','E','Q','P','L','A','Y',0,		/* 8 */
	'S','E','Q','S','T','A','T','E',0,	/* 9 */
	'S','E','Q','R','U','N','T','O',0,	/* 10 test: virtual clock only */
	'S','E','Q','L','O','G',0,		/* 11 test: log backends only */
	'E','V','W','A','I','T',0,		/* 12 event wait (Phase 5; evwait.c since 7b) */
	'E','V','P','O','S','T',0,		/* 13 test: apl_cli only (Phase 7a) */
	'S','E','Q','N','O','T','E','S',0,	/* 14 note events (Phase 7c) */
	'S','E','Q','B','E','A','T',0,		/* 15 beat events (Phase 7c) */
	0,0,0
};

UCHAR seq_ftypes[] = {
	MONAD,		/* 0 SEQOPEN 'port' */
	NILAD,		/* 1 SEQCLOSE */
	DYAD,		/* 2 t SEQTRACK M, t c SEQTRACK M */
	MONAD,		/* 3 SEQCLEAR tracks */
	DYAD,		/* 4 tracks SEQMUTE flags */
	DYAD,		/* 5 tracks SEQLOOP flags */
	DYAD,		/* 6 tracks SEQVEL velocities */
	MONAD_OPT,	/* 7 SEQTEMPO, SEQTEMPO←bpm */
	MONAD_OPT,	/* 8 SEQPLAY, SEQPLAY←0/1/2 */
	NILAD,		/* 9 SEQSTATE */
	MONAD,		/* 10 SEQRUNTO beat */
	NILAD,		/* 11 SEQLOG */
	MONAD,		/* 12 EVWAIT sources */
	MONAD,		/* 13 EVPOST kind,values */
	DYAD,		/* 14 tracks SEQNOTES flags */
	MONAD_OPT,	/* 15 SEQBEAT, SEQBEAT←0/1 */
	0
};

/*
	Set by apl_cli's -S option, before dll_startup(): every ⎕SEQOPEN
	then opens the virtual-clock backend (simulated time, output to
	the in-memory log), whatever port it names, so a program written
	for a real port runs unchanged under the regression tests. The
	argument is still checked for type. Never set by the GUI.
*/
int seqf_virtual = 0;

#define TPB	((double)SEQ_TICKS_PER_BEAT)
#define NCOLS	10			/* ⎕SEQSTATE columns (10th: 7c) */
#define EV_LIMIT 10000			/* beats the virtual clock runs
					   looking for a wait's event (7c) */
#define PORT_MAX 255			/* longest port name accepted */
#define PUSH_MAX 32767			/* push_val()'s count is a short */

/* ── Argument helpers ──────────────────────────────────────────────── */

/* Numeric data (any width), as opposed to character or ⎕STRUCT data.
   VARIABLE_BIT is set on temps and clear on variables, so it's forced
   on before comparing. */
static int
is_numeric(struct ValEnt *ve)
{
	return ((ve->ve_vtype | VARIABLE_BIT) & 0xf0) == CONST_CODE;
}

static int
is_char(struct ValEnt *ve)
{
	return (ve->ve_vtype | VARIABLE_BIT) == QUOT_CODE;
}

/* Element i of a numeric value, as a double. Byte data is signed
   (INVARIANTS.md, "Signed/Unsigned Char"). */
static double
elem(struct ValEnt *ve, int32_t i)
{
	union MultiP dp;

	dp.cp = DATA_AD(ve);
	switch (ve->ve_vtype & BYTES_MASK)
	{
		case 1:
			return (double)(signed char)dp.cp[i];
		case 2:
			return (double)dp.sp[i];
		case 4:
			return (double)dp.lp[i];
		case FLOATSIZE:
			return dp.dp[i];
	}
	return 0.0;
}

/* Element i as a whole number: a fraction (or anything outside int32
   range, or NaN) is ERR_DOMAIN. Range checks proper (track 1..16,
   pitch 0..127, ...) are the library's. */
static short
whole(struct ValEnt *ve, int32_t i, int32_t *out)
{
	double d = elem(ve, i);

	*out = 0;	/* always set: SET_ERR() may be err_track(), which
			   gcc can't see returns non-zero */
	if (!(d >= -2147483648.0 && d <= 2147483647.0) || d != floor(d))
		return SET_ERR(ERR_DOMAIN);
	*out = (int32_t)d;
	return 0;
}

/* A scalar or vector of whole numbers into a malloc'd int array (at
   least one element allocated, so an empty list is still a valid
   pointer). *list is NULL on error. */
static short
get_list(struct ValEnt *ve, int **list, int32_t *n)
{
	int32_t i, v;
	short e;

	*list = NULL;
	if (!is_numeric(ve)) return SET_ERR(ERR_DOMAIN);
	if (ve->ve_rank > 1) return SET_ERR(ERR_RANK);
	*n = ve->ve_length;
	if (*n > PUSH_MAX) return SET_ERR(ERR_LENGTH);
	*list = malloc(((size_t)*n + 1) * sizeof(int));
	if (!*list) return SET_ERR(ERR_NOMEM);
	for (i = 0; i < *n; i++)
	{
		if ((e = whole(ve, i, &v)) != 0)
		{
			free(*list);
			*list = NULL;
			return e;
		}
		(*list)[i] = (int)v;
	}
	return 0;
}

/* The library's error codes as APL errors (SEQ_FUNCTION_HELP.md).
   Out-of-range tracks, channels, pitches, durations, values and
   tempos, a backend that doesn't support the call, and "not playing"
   are all Domain Errors. */
static short
seq_apl_err(int r)
{
	switch (r)
	{
		case SEQ_ERR_NOT_OPEN:		return SET_ERR(ERR_SEQNOTOPEN);
		case SEQ_ERR_ALREADY_OPEN:	return SET_ERR(ERR_SEQOPEN);
		case SEQ_ERR_IO:		return SET_ERR(ERR_SEQLOST);
		case SEQ_ERR_NOMEM:		return SET_ERR(ERR_NOMEM);
		case SEQ_ERR_LENGTH:		return SET_ERR(ERR_LENGTH);
	}
	return SET_ERR(ERR_DOMAIN);
}

/* ── Result helpers ────────────────────────────────────────────────── */

static short
push_float(double d)
{
	struct ValEnt *ve;

	ve = make_temp(FCONST_CODE, 0, 1L);
	if (!ve) return SET_ERR(ERR_WSFULL);
	*(double *)DATA_AD(ve) = d;
	return 0;
}

/* n ints as a vector, or as a scalar when scalar is set (n is then 1) */
static short
push_ints(const int *v, int32_t n, int scalar)
{
	int32_t *lp;
	int32_t i;
	short r;

	lp = malloc(((size_t)n + 1) * sizeof(int32_t));
	if (!lp) return SET_ERR(ERR_NOMEM);
	for (i = 0; i < n; i++) lp[i] = (int32_t)v[i];
	r = push_val((short)n, (short)(scalar ? 0 : 1), lp);
	free(lp);
	return r;
}

/* The library's state as APL sees it (agreed Oct 5, 2026): >0 open (1
   stopped, 2 playing), 0 closed, <0 trouble (¯1 port lost; any later
   failure state also negative). The library's own numbering (enum
   seq_state) has port lost as 3. Used by ⎕SEQSTATE and by ⎕EVWAIT's
   "stopped" event. */
static int32_t
apl_state(int state)
{
	return (state == SEQ_PORT_LOST) ? -1 : (int32_t)state;
}

/* ── Events for the event hub (Phase 7b) ───────────────────────────── */

/*
	⎕EVWAIT and ⎕WCLICK are one wait since Phase 7b: evwait.c holds it,
	and it waits in the event hub (evhub.c). This file's part is the
	sequencer's side, reached through the hooks below, so evwait.c and
	evhub.c never include seq.h:
	  - ev_notify(), the library's callback, reports each event to the
	    hub (evhub_seq_event()). It runs while the library holds its own
	    lock, on the playback thread or whichever thread stopped or
	    closed the sequencer; the hub's lock is taken inside it, which
	    is the order evhub.h requires. It calls nothing in the library.
	  - seqf_wait_ops(): the arm, cancel and (virtual clock only) pump
	    hooks the hub calls for a wait whose list has ¯1, each without
	    the hub's lock.
	  - seqf_stop_code(): a stop's new state in ⎕SEQSTATE's codes, for
	    the result ¯1 2 S.

	Phase 7c: the wait arms seq_request_event(), which takes notes
	from the tracks set to report them and, with ⎕SEQBEAT←1, beats;
	notes outstanding are handed over during the arm itself (the hub is
	armed first, so they count). The virtual clock's pump runs the
	clock to the wait's event, whatever it is, capped at EV_LIMIT
	beats: a wait with nothing to come is then a Domain Error, as a
	wait with nothing to wait for always was in apl_cli.

	Until 7b this section held the wait itself, on its own one-event
	slot and condition variable (ev_lock, ev_cond: Phase 5, V88); the
	hub's condition variable replaces them. What carries over (worked
	out in V88):
	  - The id is a check, not the mechanism. The library keeps one
	    wait armed and calls back under its own lock, so once
	    seq_cancel_wait() returns nothing for the old wait can arrive;
	    the hub only accepts sequencer events between arming and
	    disarming a wait, and cancels before it disarms.
	  - The hub is armed before the library is, so a beat or stop that
	    comes the moment seq_request_beat() returns isn't lost (V88's
	    first sketch could lose it and hang).
	  - On the virtual clock (apl_cli -S) the pump runs the clock one
	    tick past the next whole beat, as seqtest's "beatwait" does, so
	    the event arrives during that call; the clock then holds just
	    past the beat: a track sent next starts on the following beat,
	    and a loop boundary on this beat has already passed, as on the
	    real clock, where the beat event comes after the swaps on its
	    tick.
*/

static void
ev_notify(void *ctx, const struct seq_event *ev)
{
	int32_t d[4 * EVHUB_MAXNOTES];
	int i, n;

	(void)ctx;
	if (ev->kind == SEQ_EV_NOTES)	/* 7c */
	{
		n = ev->nnotes < EVHUB_MAXNOTES ? ev->nnotes : EVHUB_MAXNOTES;
		for (i = 0; i < n; i++)
		{
			d[4 * i] = ev->notes[i].track;
			d[4 * i + 1] = ev->notes[i].pitch;
			d[4 * i + 2] = ev->notes[i].index;
			d[4 * i + 3] = ev->notes[i].count;
		}
		evhub_seq_notes(ev->request_id, n, d);
	}
	else if (ev->kind == SEQ_EV_BEAT)
		evhub_seq_event(EVHUB_SEQ_BEAT, ev->request_id, ev->beat);
	else	/* SEQ_EV_STOPPED: stopped, closed, or port lost (SEQ_ERR_IO) */
		evhub_seq_event(EVHUB_SEQ_STOPPED, ev->request_id,
				ev->reason == SEQ_ERR_IO ? 1 : 0);
}

/* Arm the library's event wait (7c; the beat wait until then). Fails
   (nonzero) when nothing can produce an event -- not playing with no
   notes outstanding, or playing with beats off and no reporting track
   that has notes -- so the hub skips the sequencer for this wait. */
static int
ev_arm(uint32_t *id)
{
	return (seq_request_event(id) == SEQ_OK) ? 0 : 1;
}

static void
ev_cancel(uint32_t id)
{
	seq_cancel_wait(id);
}

/* Virtual clock: play until the armed wait's event (notes, a beat, or
   STOPPED if the port is lost on the way), holding one tick past it;
   the event arrives during the call. 7c: nonzero if none comes within
   EV_LIMIT beats (until then the pump ran to the next beat, which
   always came). */
static int
ev_pump(void)
{
	return (seq_virtual_run_to_event((int64_t)EV_LIMIT
					 * SEQ_TICKS_PER_BEAT) == SEQ_OK) ? 0 : 1;
}

static const struct evhub_seq_ops ev_ops_real = { ev_arm, ev_cancel, NULL };
static const struct evhub_seq_ops ev_ops_virtual = { ev_arm, ev_cancel, ev_pump };

/* For evwait.c: the hooks for a wait on ¯1. */
const struct evhub_seq_ops *
seqf_wait_ops(void)
{
	return seqf_virtual ? &ev_ops_virtual : &ev_ops_real;
}

/* For evwait.c: the S of ¯1 2 S, in ⎕SEQSTATE's codes (1 stopped, 0
   closed, ¯1 port lost). `lost` is the hub's value for the stop: the
   library's reason settles port lost; otherwise the state now. Called
   on the engine thread after the wait, with no lock held. */
int32_t
seqf_stop_code(int lost)
{
	int state;

	if (lost) return apl_state(SEQ_PORT_LOST);
	seq_get_state(&state, NULL);
	return apl_state(state);
}

/* ── The list functions: SEQMUTE, SEQLOOP, SEQVEL, SEQNOTES ────────── */

typedef int (*SETFN)(const int *, const int *, int, int *);

/* tracks (left) and values (right, one for all or one per track).
   Result: the previous settings, a scalar if the track list was. */
static short
list_set(SETFN fn, struct ValEnt *le, struct ValEnt *re)
{
	int *tracks, *vals = NULL, *all = NULL, *prev = NULL;
	int32_t n, nv, i;
	int scalar;
	short e;
	int r;

	if ((e = get_list(le, &tracks, &n)) != 0) return e;
	scalar = (le->ve_rank == 0);
	if ((e = get_list(re, &vals, &nv)) != 0) goto done;
	if (nv != 1 && nv != n)
	{
		e = SET_ERR(ERR_LENGTH);
		goto done;
	}
	all = malloc(((size_t)n + 1) * sizeof(int));
	prev = malloc(((size_t)n + 1) * sizeof(int));
	if (!all || !prev)
	{
		e = SET_ERR(ERR_NOMEM);
		goto done;
	}
	for (i = 0; i < n; i++) all[i] = vals[nv == 1 ? 0 : i];
	r = (*fn)(tracks, all, (int)n, prev);
	if (r != SEQ_OK)
	{
		e = seq_apl_err(r);
		goto done;
	}
	stack_pop(2);
	e = push_ints(prev, n, scalar);
done:
	free(tracks);
	free(vals);
	free(all);
	free(prev);
	return e;
}

/* ── The functions ─────────────────────────────────────────────────── */

/* SEQOPEN 'port'. 1 opened, 0 no such port, ¯1 in use, ¯2 no
   permission (agreed Oct 5, 2026: codes in the result rather than a
   ⎕SEQSTATE column, since "in use" is common enough to test for
   directly). A name the library can't parse is a Domain Error: that's
   a mistake in the program, not a missing device. The name is passed
   as its bytes, as ⎕FOPEN passes a file name. Windows also takes the
   device number as a number. */
static short
f_open(struct ValEnt *re)
{
	char port[PORT_MAX + 1];
	struct seq_config cfg;
	int32_t n, acc, i;
	int r;

	if (is_char(re))
	{
		if (re->ve_rank > 1) return SET_ERR(ERR_RANK);
		n = re->ve_length;
		if (n > PORT_MAX) return SET_ERR(ERR_DOMAIN);
		memcpy(port, DATA_AD(re), (size_t)n);
		port[n] = '\0';
		/* Case doesn't matter (agreed Oct 5, 2026): the APL keyboard
		   types upper case, and 'HW:3,0,0' should work without
		   ⎕LCASE. Same change ⎕LCASE makes. Every name the library
		   takes now is lower case or digits ("hw:C,D,S" on Linux, a
		   number on Windows); opening by device name (Phase 8) will
		   have to match names case-insensitively. */
		for (i = 0; i < n; i++)
			if (port[i] >= 'A' && port[i] <= 'Z') port[i] += 32;
	}
#ifdef _WIN32
	else if (is_numeric(re))
	{
		if (re->ve_length != 1) return SET_ERR(ERR_LENGTH);
		if (whole(re, 0, &n) != 0 || n < 0) return SET_ERR(ERR_DOMAIN);
		sprintf(port, "%ld", (long)n);
	}
#endif
	else
		return SET_ERR(ERR_DOMAIN);

	memset(&cfg, 0, sizeof cfg);
	cfg.backend = seqf_virtual ? SEQ_BACKEND_VIRTUAL : SEQ_BACKEND_DEVICE;
	cfg.port = port;
	cfg.notify = ev_notify;			/* events for the hub (7b) */
	cfg.ctx = NULL;

	r = seq_open(&cfg);
	switch (r)
	{
		case SEQ_OK:		acc = 1; break;
		case SEQ_ERR_PORT:	acc = 0; break;
		case SEQ_ERR_PORT_BUSY:	acc = -1; break;
		case SEQ_ERR_PORT_ACCESS: acc = -2; break;
		default:		return seq_apl_err(r);
	}
	stack_pop(1);
	return push_val(1, 0, &acc);
}

/* SEQCLOSE: the tracks that held something (empty if none). Never an
   error. */
static short
f_close(void)
{
	int closed[SEQ_MAX_TRACKS];
	int n = 0;

	seq_close(closed, &n);
	return push_ints(closed, n, 0);
}

/* t SEQTRACK M, or t c SEQTRACK M. M: 2 rows (pitches, durations in
   beats), at least one column. Result: the beat at which this version
   takes over. */
static short
f_track(struct ValEnt *le, struct ValEnt *re)
{
	int32_t track, channel, cols, i, v;
	int32_t *pitch;
	double *dur;
	int64_t swap;
	short e;
	int r;

	/* left: track, or track and channel */
	if (!is_numeric(le)) return SET_ERR(ERR_DOMAIN);
	if (le->ve_rank > 1) return SET_ERR(ERR_RANK);
	if (le->ve_length != 1 && le->ve_length != 2) return SET_ERR(ERR_LENGTH);
	if ((e = whole(le, 0, &track)) != 0) return e;
	channel = SEQ_UNCHANGED;
	if (le->ve_length == 2 && (e = whole(le, 1, &channel)) != 0) return e;
	/* SEQ_UNCHANGED is -1: a channel given as ¯1 must not read as
	   "unchanged" */
	if (le->ve_length == 2 && channel == SEQ_UNCHANGED)
		return SET_ERR(ERR_DOMAIN);

	/* right: the 2×N matrix. ve_rho is columns first: rho[0] is N,
	   rho[1] the row count (parse.h, struct ValEnt). */
	if (!is_numeric(re)) return SET_ERR(ERR_DOMAIN);
	if (re->ve_rank != 2) return SET_ERR(ERR_RANK);
	if (re->ve_rho[1] != 2 || re->ve_rho[0] < 1) return SET_ERR(ERR_LENGTH);
	cols = re->ve_rho[0];

	pitch = malloc((size_t)cols * sizeof(int32_t));
	dur = malloc((size_t)cols * sizeof(double));
	if (!pitch || !dur)
	{
		free(pitch);
		free(dur);
		return SET_ERR(ERR_NOMEM);
	}
	for (i = 0; i < cols; i++)
	{
		if ((e = whole(re, i, &v)) != 0)
		{
			free(pitch);
			free(dur);
			return e;
		}
		pitch[i] = v;
		dur[i] = elem(re, cols + i);
	}

	r = seq_submit_track((int)track, (int)channel, pitch, dur, cols, &swap);
	free(pitch);
	free(dur);
	if (r != SEQ_OK) return seq_apl_err(r);
	stack_pop(2);
	return push_float((double)swap / TPB);
}

/* SEQCLEAR tracks: those of the tracks given that held something. */
static short
f_clear(struct ValEnt *re)
{
	int *tracks, *prev;
	int32_t n, i, k;
	short e;
	int r;

	if ((e = get_list(re, &tracks, &n)) != 0) return e;
	prev = malloc(((size_t)n + 1) * sizeof(int));
	if (!prev)
	{
		free(tracks);
		return SET_ERR(ERR_NOMEM);
	}
	r = seq_clear(tracks, (int)n, prev);
	if (r != SEQ_OK)
	{
		free(tracks);
		free(prev);
		return seq_apl_err(r);
	}
	for (i = k = 0; i < n; i++)
		if (prev[i]) tracks[k++] = tracks[i];
	stack_pop(1);
	e = push_ints(tracks, k, 0);
	free(tracks);
	free(prev);
	return e;
}

/* SEQTEMPO (MONAD_OPT): reports the tempo; with an argument, sets it
   and returns the previous one. */
static short
f_tempo(struct ValEnt *re)
{
	double prev, bpm;
	int r;

	if ((r = seq_get_tempo(&prev)) != SEQ_OK) return seq_apl_err(r);
	if (re)
	{
		if (!is_numeric(re)) return SET_ERR(ERR_DOMAIN);
		if (re->ve_length != 1) return SET_ERR(ERR_LENGTH);
		bpm = elem(re, 0);
		if ((r = seq_set_tempo(bpm, &prev)) != SEQ_OK)
			return seq_apl_err(r);
		stack_pop(1);
	}
	return push_float(prev);
}

/* SEQPLAY (MONAD_OPT): reports 1 if playing, 0 if not; with an
   argument (0 stop, 1 continue, 2 restart) returns the previous
   state. */
static short
f_play(struct ValEnt *re)
{
	int state, prev, r;
	int32_t mode, acc;
	short e;

	seq_get_state(&state, NULL);
	if (state == SEQ_CLOSED) return SET_ERR(ERR_SEQNOTOPEN);
	acc = (state == SEQ_PLAYING);
	if (re)
	{
		if (!is_numeric(re)) return SET_ERR(ERR_DOMAIN);
		if (re->ve_length != 1) return SET_ERR(ERR_LENGTH);
		if ((e = whole(re, 0, &mode)) != 0) return e;
		if ((r = seq_play((int)mode, &prev)) != SEQ_OK)
			return seq_apl_err(r);
		acc = prev;
		stack_pop(1);
	}
	return push_val(1, 0, &acc);
}

/* SEQBEAT (MONAD_OPT, Phase 7c): reports 1 if a wait on ¯1 reports
   beats, 0 if not (the default from ⎕SEQOPEN on); with an argument (0
   or 1) sets it and returns the previous setting. Same shape as
   SEQPLAY: Sequencer Not Open when closed. */
static short
f_beat(struct ValEnt *re)
{
	int on, prev, r;
	int32_t v, acc;
	short e;

	if ((r = seq_get_beats(&on)) != SEQ_OK) return seq_apl_err(r);
	acc = on;
	if (re)
	{
		if (!is_numeric(re)) return SET_ERR(ERR_DOMAIN);
		if (re->ve_length != 1) return SET_ERR(ERR_LENGTH);
		if ((e = whole(re, 0, &v)) != 0) return e;
		if ((r = seq_set_beats((int)v, &prev)) != SEQ_OK)
			return seq_apl_err(r);
		acc = prev;
		stack_pop(1);
	}
	return push_val(1, 0, &acc);
}

/* SEQSTATE: a float matrix, the sequencer row and then one row per
   track in use (MIDI_SEQUENCER_NOTES.md, "⎕SEQSTATE layout"). Never an
   error, except WS Full. The state, the tracks, the tempo and the beat
   setting are separate library calls, so with music playing on the
   real clock the beat and the track positions can be a moment apart;
   the track rows agree with each other (one lock). Phase 7c: column 6
   of the sequencer row is ⎕SEQBEAT, column 7 how late the last note
   event was handed over (ms; for watching a loop while tuning it),
   column 10 of a track row ⎕SEQNOTES (the sequencer row's column 10
   is 0). */
static short
f_state(void)
{
	struct seq_track_info info[SEQ_MAX_TRACKS];
	struct ValEnt *ve;
	double *dp;
	double tempo = 0.0;
	int64_t tick;
	int state, n = 0, i, beats = 0;
	double late = 0.0;

	seq_get_state(&state, &tick);
	seq_get_tracks(info, &n);
	if (seq_get_tempo(&tempo) != SEQ_OK) tempo = 0.0;	/* closed */
	if (seq_get_beats(&beats) != SEQ_OK) beats = 0;		/* closed */
	if (seq_get_note_late(&late) != SEQ_OK) late = 0.0;	/* closed */

	ve = make_temp(FCONST_CODE, 2, (long)(n + 1) * NCOLS);
	if (!ve) return SET_ERR(ERR_WSFULL);
	ve->ve_rho[0] = NCOLS;
	ve->ve_rho[1] = n + 1;
	dp = (double *)DATA_AD(ve);
	memset(dp, 0, (size_t)(n + 1) * NCOLS * sizeof(double));

	dp[0] = 0.0;
	dp[1] = (double)apl_state(state);	/* see apl_state() */
	dp[2] = (double)tick / TPB;
	dp[3] = tempo;
	dp[4] = (double)n;
	dp[5] = (double)beats;
	dp[6] = late;
	for (i = 0; i < n; i++)
	{
		double *row = dp + (size_t)(i + 1) * NCOLS;

		row[0] = (double)info[i].track;
		row[1] = (double)info[i].notes;
		row[2] = (double)info[i].channel;
		row[3] = (double)info[i].mute;
		row[4] = (double)info[i].loop;
		row[5] = (double)info[i].velocity;
		row[6] = (double)info[i].length / TPB;
		row[7] = (double)info[i].pending;
		row[8] = (double)info[i].position / TPB;
		row[9] = (double)info[i].report;
	}
	return 0;
}

/* SEQRUNTO beat (test): plays everything before beat on the virtual
   clock, then holds there. Returns the beat it holds at. Domain Error
   unless the virtual backend is open and playing. */
static short
f_runto(struct ValEnt *re)
{
	double beat;
	int64_t tick;
	int r;

	if (!is_numeric(re)) return SET_ERR(ERR_DOMAIN);
	if (re->ve_length != 1) return SET_ERR(ERR_LENGTH);
	beat = elem(re, 0);
	if (!(beat >= 0.0 && beat <= SEQ_MAX_BEATS)) return SET_ERR(ERR_DOMAIN);
	tick = (int64_t)(beat * TPB + 0.5);
	if ((r = seq_virtual_run_to(tick)) != SEQ_OK) return seq_apl_err(r);
	stack_pop(1);
	return push_float((double)tick / TPB);
}

/* SEQLOG (test): the log written since the last SEQLOG (or since the
   open), one row per MIDI message, then empties it. A 0×0 matrix if
   there is nothing. Domain Error while a real device is open; after a
   close it still returns what the last test session left. */
static short
f_log(void)
{
	struct ValEnt *ve;
	char *buf = NULL, *nb, *cp, *line, *end;
	size_t size = 0, len = 0;
	int32_t rows, cols, w, r;
	int rc;

	/* The copy empties the log only if it all fit, so grow and
	   retry until it does. */
	if ((rc = seq_virtual_log(NULL, 0, &len, 0)) != SEQ_OK)
		return seq_apl_err(rc);
	for (;;)
	{
		size = len + 4096;
		nb = realloc(buf, size);
		if (!nb)
		{
			free(buf);
			return SET_ERR(ERR_NOMEM);
		}
		buf = nb;
		if ((rc = seq_virtual_log(buf, size, &len, 1)) != SEQ_OK)
		{
			free(buf);
			return seq_apl_err(rc);
		}
		if (len < size) break;
	}

	/* rows and the widest line */
	rows = cols = 0;
	for (line = buf; line < buf + len; line = end + 1)
	{
		end = memchr(line, '\n', (size_t)(buf + len - line));
		if (!end) end = buf + len;
		w = (int32_t)(end - line);
		if (w > cols) cols = w;
		rows++;
	}

	ve = make_temp(QUOT_CODE, 2, (long)rows * cols);
	if (!ve)
	{
		free(buf);
		return SET_ERR(ERR_WSFULL);
	}
	ve->ve_rho[0] = cols;
	ve->ve_rho[1] = rows;
	cp = DATA_AD(ve);
	memset(cp, ' ', (size_t)rows * cols);
	for (r = 0, line = buf; r < rows; r++, line = end + 1)
	{
		end = memchr(line, '\n', (size_t)(buf + len - line));
		if (!end) end = buf + len;
		memcpy(cp + (size_t)r * cols, line, (size_t)(end - line));
	}
	free(buf);
	return 0;
}

/* EVPOST kind,values (test only, Phase 7a): queues an event for the
   event hub to deliver to the waits to come, in apl_cli, which has no
   real windows to click. Each wait takes queued events in order until
   it ends and leaves the rest (evhub.c). The first number is the kind:
	1 W X Y   a left click in window W at X,Y
	2 W       window W closed by the user (apl_cli's fake window table
	          drops it when the close is delivered)
	3         Escape
	4         Ctrl-C (sets ABORTSIG when delivered)
   Result: how many events are now queued (a scalar). Domain Error
   outside apl_cli, for an unknown kind, or when the queue (64) is
   full; Length Error for the wrong number of values; the usual list
   errors (Rank, Domain for a fraction), as for a sources list. */
static short
f_evpost(struct ValEnt *re)
{
	int *v;
	int32_t n, a[3], acc;
	short e;
	int i, kind;
	static const int len[5] = { 0, 4, 2, 1, 1 };

	if ((e = get_list(re, &v, &n)) != 0) return e;
	if (n == 0)
	{
		free(v);
		return SET_ERR(ERR_LENGTH);
	}
	kind = v[0];
	if (kind < 1 || kind > 4)
	{
		free(v);
		return SET_ERR(ERR_DOMAIN);
	}
	if (n != len[kind])
	{
		free(v);
		return SET_ERR(ERR_LENGTH);
	}
	for (i = 0; i < 3; i++) a[i] = (i + 1 < n) ? v[i + 1] : 0;
	free(v);
	acc = evhub_post(kind, a);
	if (acc < 0) return SET_ERR(ERR_DOMAIN);	/* not apl_cli, full */
	stack_pop(1);
	return push_val(1, 0, &acc);
}

/* ── Dispatch ──────────────────────────────────────────────────────── */

short
seq_funcs(f,le,re)
short f;
struct ValEnt *le,*re;
{
	switch (f)
	{
		case 0:		/* SEQOPEN */
			return(f_open(re));
		case 1:		/* SEQCLOSE */
			return(f_close());
		case 2:		/* SEQTRACK */
			return(f_track(le,re));
		case 3:		/* SEQCLEAR */
			return(f_clear(re));
		case 4:		/* SEQMUTE */
			return(list_set(seq_set_mute,le,re));
		case 5:		/* SEQLOOP */
			return(list_set(seq_set_loop,le,re));
		case 6:		/* SEQVEL */
			return(list_set(seq_set_velocity,le,re));
		case 7:		/* SEQTEMPO */
			return(f_tempo(re));
		case 8:		/* SEQPLAY */
			return(f_play(re));
		case 9:		/* SEQSTATE */
			return(f_state());
		case 10:	/* SEQRUNTO (test) */
			return(f_runto(re));
		case 11:	/* SEQLOG (test) */
			return(f_log());
		case 12:	/* EVWAIT: the same wait as WCLICK (evwait.c, 7b) */
			return(ev_wait_apl(re));
		case 13:	/* EVPOST (test) */
			return(f_evpost(re));
		case 14:	/* SEQNOTES (7c) */
			return(list_set(seq_set_notes,le,re));
		case 15:	/* SEQBEAT (7c) */
			return(f_beat(re));
		case CLEANUP_FUNC:
			return(0);
	}
	return(SET_ERR(ERR_SYSTEM));
}

/* ── Hooks for the rest of the program ─────────────────────────────── */

/* Stops playback, sends note-offs, closes the port. Safe from any
   thread and more than once: dll_cleanup() (engine thread, normal
   exits) and stopEngine() (GUI thread, window closed, possibly while
   APL is busy and dll_cleanup() will never run) both call it. A
   wait on ¯1 blocked on the engine thread then returns ¯1 2 0 (the
   library's SEQ_EV_STOPPED reaches the hub through ev_notify()). */
void
seqf_close(void)
{
	seq_close(NULL, NULL);
}

/* fatal_error(), just before _Exit(): note-offs and All Notes Off,
   without waiting long for the library's lock. Touches nothing in the
   workspace. */
void
seqf_panic(void)
{
	seq_panic();
}
