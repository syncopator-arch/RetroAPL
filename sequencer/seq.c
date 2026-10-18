/*
	seq.c -- MIDI sequencer library: state, playback thread, tracks.

	See seq.h for the interface and MIDI_SEQUENCER_NOTES.md for the
	design. One sequencer per process: everything lives in the static
	object S, guarded by S.lock. Every public call takes the lock,
	does its work quickly and returns; only seq_virtual_run_to() and
	seq_close() wait (for the playback thread).

	Time. The clock is counted in ticks (SEQ_TICKS_PER_BEAT per beat).
	While playing, the tick for a clock time comes from an anchor: the
	time and position of the last start, continue or tempo change.
	    deadline(T) = anchor_ns + (T - anchor_tick) * ns_per_tick
	Re-anchoring at each tempo change is what keeps the position from
	jumping when the tempo changes mid-play.

	The playback thread keeps S.done: every event before tick S.done
	has been played. It finds the next event tick T, waits until its
	deadline (or, in test mode, jumps there if T is before the
	horizon), plays everything at T, and sets S.done = T + 1. A new
	version or a beat wait is always scheduled at or after S.done, so
	nothing can land on a tick the thread has already passed.

	A track's current version is "live" once the thread has reached
	its start point (start_point < S.done). A new version for a live
	track waits in pend for the end of the current pass; for a track
	that isn't live it replaces cur outright, since nothing of it has
	played yet.

	A play-once track that reaches the end of its pass with nothing
	pending keeps its version, finished (agreed after V95): silent, with
	no events and not live, until a restart rewinds it with the rest, a
	new version replaces it (starting at the next whole beat, as for any
	track with nothing playing), or a clear or close drops it. Until V95
	the version was freed there, so a restart couldn't bring it back.

	Clocks. Real time is always read from CLOCK_MONOTONIC (S.clk),
	kept as ns since open. Timed waits use it too if
	pthread_condattr_setclock() accepts it (glibc does); if not
	(winpthreads, confirmed on Windows), see timed_wait(). Test mode:
	S.vnow, simulated ns, moved only by the playback thread.

	Notes (Phase 8, Oct 15, 2026). A version may carry, per note, a
	velocity, a sounding length and a channel (the matrix's rows 3-5);
	each note's note-on and the tick of its note-off are worked out
	when it starts (note_vel(), note_chan(), note_end()). A track still
	sounds one note at a time: a length never reaches past the next
	note's start.

	Programs (V100, Oct 18, 2026). A track's program and bank are
	settings; each track also remembers what it last sent (program,
	bank, channel) and what was in effect when its current pass began.
	At the start of a pass, after the boundary step and before any
	note-on, pass_program() sends bank select and program change if
	the track has a program and it differs from what was last sent, or
	the pass is forced (a fresh start, a restart). Reopening after a
	lost port sends each live track's program at once.

	Floating point: positions and deadlines are computed in double.
	Built with -ffp-contract=off so x86-64 and aarch64 round the same
	way, which keeps the test logs identical on both.
*/

#include "seq.h"
#include "seq_internal.h"

#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>	/* timed waits, see timed_wait() */
#endif

#define TPB SEQ_TICKS_PER_BEAT

struct version {
	int32_t  n;
	int32_t *pitch;		/* n entries */
	int64_t *start;		/* n + 1 entries: note starts in ticks,
				   start[n] = loop length */
	int      channel;	/* 1..16, fixed when submitted */
	/* Phase 8: per note, each NULL if not given; 0 in an entry (and
	   for every rest) means the track's setting */
	unsigned char *vel;	/* 1..127, scaled by the track's velocity */
	int64_t *len;		/* sounding length, ticks, >= 1 */
	unsigned char *chan;	/* 1..16 */
};

struct track {
	struct version *cur;	/* playing (or about to start) */
	struct version *pend;	/* waiting for cur's loop boundary */
	int64_t start_point;	/* tick the current run began */
	int64_t pass_base;	/* tick the current pass of cur began */
	int32_t next;		/* index of cur's next note */
	int     clear_req;	/* remove at the next boundary */
	int     finished;	/* play-once, played through: cur kept,
				   silent until restart, a new version,
				   clear or close (after V95) */

	/* settings, kept across versions and seq_clear() */
	int     channel;	/* 1..16 */
	int     mute;
	int     loop;
	int     velocity;
	int     notes;		/* Phase 7c: reports note events */
	double  gate;		/* Phase 8: sounding fraction of each
				   note's step, (0, 1], for notes with
				   no length of their own */
	int     program;	/* V100: 0..127, SEQ_NONE */
	int     bank;		/* V100: 0..16383, SEQ_NONE */

	/* V100: the program and bank in effect since cur's current pass
	   began (what was heard with it; ⎕MIDIWRITE writes them), what
	   was last sent (SEQ_NONE: nothing yet), and whether the next
	   pass start sends whatever it has (a fresh start, a restart) */
	int     prog_on, bank_on;
	int     sent_prog, sent_bank, sent_chan;
	int     prog_force;

	/* Phase 7c: the latest note started and not yet reported (only
	   while notes is on): its pitch and column, and how many notes
	   were started since the last one reported (0: nothing
	   outstanding) */
	int     ev_pitch;
	int32_t ev_index;
	int64_t ev_count;
	int64_t ev_tick;	/* the tick it started on (lateness) */

	/* the note this track has sounding (one at a time) */
	int     snd_pitch;	/* -1 if none */
	int     snd_chan;	/* 1..16 */
	int64_t snd_off;	/* tick of its note-off */
};

static struct {
	pthread_mutex_t lock;
	pthread_cond_t  wake;	/* the playback thread waits here */
	pthread_cond_t  held;	/* run_to, open and close wait here */
	clockid_t       clk;
	int             monotonic;

	int             state;		/* enum seq_state */
	int             closing;	/* seq_close() is ending the thread */
	int             backend;	/* enum seq_backend */
	const struct seq_output *out;
	seq_notify_fn   notify;
	void           *ctx;

	pthread_t       thread;
	int             quit;

	struct track    tr[SEQ_MAX_TRACKS + 1];	/* [0] unused */
	int             count[16][128];	/* sounding notes per channel/pitch */
	unsigned char   lost[16][128];	/* sounding when the port was lost,
					   or whose note-off was the write
					   that failed: released on reopen
					   (Phase 3) */

	double          tempo;
	int64_t         done;		/* events before this tick played */
	double          stop_pos;	/* position while not PLAYING */
	int64_t         anchor_ns;	/* while PLAYING */
	double          anchor_tick;

	int64_t         base_ns;	/* real clock at open */
	int64_t         vnow;		/* test mode: simulated ns */
	int64_t         horizon;	/* test mode: hold at this tick */
	int             holding;	/* test mode: thread is holding */

	struct seq_timing timing;	/* real clock only (Phase 3/6) */

	int             wait_armed;
	uint32_t        wait_id;
	uint32_t        next_id;
	int64_t         wait_tick;	/* the beat asked for; INT64_MAX if
					   the wait doesn't take beats */
	int             wait_notes;	/* Phase 7c: the wait takes notes */
	int             beats_on;	/* Phase 7c: seq_set_beats() */
	int             run_event;	/* Phase 7c, test mode: hold just
					   past the tick that ends the wait */
	/* Phase 7c: the last notes handed to the callback, kept until
	   the next wait is armed, so that seq_cancel_wait() for that id
	   can hand them back (the caller didn't take them) */
	uint32_t        given_id;	/* 0: none kept */
	int64_t         given_count[SEQ_MAX_TRACKS + 1];
	int             given_pitch[SEQ_MAX_TRACKS + 1];
	int32_t         given_index[SEQ_MAX_TRACKS + 1];
	int64_t         given_tick[SEQ_MAX_TRACKS + 1];
	/* Phase 7c: how late the last note event was handed over, ms
	   (seq_get_note_late()); 0 until the first */
	double          late_ms;
} S = { .lock = PTHREAD_MUTEX_INITIALIZER };

static pthread_once_t init_once = PTHREAD_ONCE_INIT;

#ifdef _WIN32
/* Windows: set with every wake-up of the playback thread, for its timed
   waits (see timed_wait()). */
static HANDLE wake_ev;
#endif

/* Wakes the playback thread (stop, tempo, new track, close ...). */
static void wake_thread(void)
{
	pthread_cond_broadcast(&S.wake);
#ifdef _WIN32
	SetEvent(wake_ev);
#endif
}

static void init_conds(void)
{
	pthread_condattr_t a;

	struct timespec ts;

	pthread_condattr_init(&a);
	S.clk = CLOCK_REALTIME;
	S.monotonic = 0;
#if defined(CLOCK_MONOTONIC)
	/* the clock "now" is read from */
	if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
		S.clk = CLOCK_MONOTONIC;
#ifndef SEQ_FORCE_REALTIME_WAIT	/* test switch: try the fallback */
	/* the clock timed waits count on: the same one if possible */
	if (S.clk == CLOCK_MONOTONIC
	    && pthread_condattr_setclock(&a, CLOCK_MONOTONIC) == 0)
		S.monotonic = 1;
#endif
#endif
	(void)ts;
	pthread_cond_init(&S.wake, &a);
#ifdef _WIN32
	wake_ev = CreateEvent(NULL, FALSE, FALSE, NULL);	/* auto-reset */
#endif
	pthread_condattr_destroy(&a);
	pthread_cond_init(&S.held, NULL);
}

int seq_clock_is_monotonic(void)
{
	pthread_once(&init_once, init_conds);
	return S.monotonic;
}

/* ── Time ─────────────────────────────────────────────────────────── */

static int64_t real_ns(void)
{
	struct timespec ts;

	clock_gettime(S.clk, &ts);
	return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static int is_virtual(void)
{
	return S.backend == SEQ_BACKEND_VIRTUAL;
}

/* Clock time since open, ns. */
static int64_t now_ns(void)
{
	return is_virtual() ? S.vnow : real_ns() - S.base_ns;
}

static double ns_per_tick(void)
{
	return 60e9 / (S.tempo * TPB);
}

static int64_t ceil_i64(double x)
{
	int64_t i = (int64_t)x;		/* truncates toward zero */

	return (i < x) ? i + 1 : i;
}

static int64_t floor_i64(double x)
{
	int64_t i = (int64_t)x;

	return (i > x) ? i - 1 : i;
}

/* Clock time (ns since open) at which tick T is due. */
static int64_t deadline(int64_t T)
{
	return S.anchor_ns + ceil_i64(((double)T - S.anchor_tick)
	                              * ns_per_tick());
}

static int64_t next_event(void);

/* Current main-clock position in ticks (fractional in real time). */
static double cur_pos(void)
{
	double p;

	if (S.state != SEQ_PLAYING)
		return S.stop_pos;
	if (is_virtual())
		return (double)S.done;
	p = S.anchor_tick + (double)(real_ns() - S.base_ns - S.anchor_ns)
	                    / ns_per_tick();
	return (p < 0) ? 0 : p;
}

/* Position for stamping a message sent outside the playback thread. */
static int64_t stamp_tick(void)
{
	return floor_i64(cur_pos());
}

/* Earliest tick at which something new can be scheduled. */
static int64_t avail_tick(void)
{
	int64_t t = ceil_i64(cur_pos());

	return (t < S.done) ? S.done : t;
}

static int64_t next_beat(int64_t t)
{
	return ((t + TPB - 1) / TPB) * TPB;
}

/* ── Events to the caller ─────────────────────────────────────────── */

static void notify(int kind, uint32_t id, int64_t beat, int reason)
{
	struct seq_event ev;

	if (!S.notify)
		return;
	ev.kind = kind;
	ev.request_id = id;
	ev.beat = beat;
	ev.reason = reason;
	S.notify(S.ctx, &ev);
}

/* Phase 7c: 1 if some track has a note not yet reported. */
static int notes_outstanding(void)
{
	int t;

	for (t = 1; t <= SEQ_MAX_TRACKS; t++)
		if (S.tr[t].ev_count > 0)
			return 1;
	return 0;
}

/* Phase 7c: hands every outstanding note to the callback as one
   SEQ_EV_NOTES for request id, and clears them. The wait is over.
   pos is the position now, in ticks: how late the newest note in the
   event is handed over is pos less its tick, at the current tempo. */
static void deliver_notes(uint32_t id, double pos)
{
	struct seq_event ev;
	int t;
	int64_t newest = -1;

	S.wait_armed = 0;
	memset(S.given_count, 0, sizeof S.given_count);
	memset(&ev, 0, sizeof ev);
	ev.kind = SEQ_EV_NOTES;
	ev.request_id = id;
	ev.reason = SEQ_OK;
	for (t = 1; t <= SEQ_MAX_TRACKS; t++) {
		struct track *tr = &S.tr[t];
		struct seq_note_event *ne;

		if (tr->ev_count <= 0)
			continue;
		ne = &ev.notes[ev.nnotes++];
		ne->track = t;
		ne->pitch = tr->ev_pitch;
		ne->index = tr->ev_index;
		ne->count = (tr->ev_count > INT32_MAX) ? INT32_MAX
		                                       : (int32_t)tr->ev_count;
		S.given_count[t] = tr->ev_count;
		S.given_pitch[t] = tr->ev_pitch;
		S.given_index[t] = tr->ev_index;
		S.given_tick[t] = tr->ev_tick;
		if (tr->ev_tick > newest)
			newest = tr->ev_tick;
		tr->ev_count = 0;
	}
	S.given_id = id;
	/* ticks to ms at the current tempo, written so that whole
	   numbers of ticks give exact results (half a beat at 120 is
	   250, not 249.99999...) */
	S.late_ms = (pos > (double)newest)
	            ? (pos - (double)newest) * 60000.0 / (S.tempo * TPB) : 0.0;
	if (S.notify)
		S.notify(S.ctx, &ev);
}

static void end_wait(int reason)
{
	if (S.wait_armed) {
		S.wait_armed = 0;
		notify(SEQ_EV_STOPPED, S.wait_id, 0, reason);
	}
}

/* ── MIDI output and the note table ───────────────────────────────── */

static void clear_sounding(void)
{
	int t;

	memset(S.count, 0, sizeof S.count);
	for (t = 1; t <= SEQ_MAX_TRACKS; t++)
		S.tr[t].snd_pitch = -1;
}

/* The device stopped taking data: stop, keep tracks and position.
   The notes still sounding are remembered in S.lost, so reopening can
   release them on the synth (it never got their note-offs). */
static void port_lost(int64_t tick)
{
	int ch, p;

	for (ch = 0; ch < 16; ch++)
		for (p = 0; p < 128; p++)
			if (S.count[ch][p] > 0)
				S.lost[ch][p] = 1;
	S.state = SEQ_PORT_LOST;
	S.stop_pos = (double)tick;
	S.holding = 0;
	clear_sounding();
	S.out->close();
	end_wait(SEQ_ERR_IO);
	pthread_cond_broadcast(&S.held);
}

/* 0 if sent; -1 if not (port already lost, or lost by this write).
   len 3, or 2 for a program change (V100; d2 unused). */
static int sendn(int len, int status, int d1, int d2, int64_t tick)
{
	unsigned char m[3];

	if (S.state != SEQ_STOPPED && S.state != SEQ_PLAYING)
		return -1;
	m[0] = (unsigned char)status;
	m[1] = (unsigned char)d1;
	m[2] = (unsigned char)d2;
	if (S.out->send(m, len, tick, now_ns()) != 0) {
		port_lost(tick);
		return -1;
	}
	return 0;
}

static int send3(int status, int d1, int d2, int64_t tick)
{
	return sendn(3, status, d1, d2, tick);
}

/* V100: bank select (if a bank is given) and program change on channel
   chan (1..16); prog must be 0..127. 0 if all sent. Records what was
   sent only once it has all gone out, so a lost port partway sends it
   again (a reopen, or the rest of the tick on continuing). */
static int send_program(struct track *tr, int chan, int prog, int bank,
                        int64_t tick)
{
	int c = chan - 1;

	if (bank >= 0) {
		if (send3(0xB0 | c, 0, bank / 128, tick) != 0
		    || send3(0xB0 | c, 32, bank % 128, tick) != 0)
			return -1;
	}
	if (sendn(2, 0xC0 | c, prog, 0, tick) != 0)
		return -1;
	tr->sent_prog = prog;
	tr->sent_bank = bank;
	tr->sent_chan = chan;
	tr->prog_force = 0;	/* sent: a forced pass needn't again */
	return 0;
}

static int note_on(struct track *tr, int pitch, int chan, int vel,
                   int64_t off, int64_t tick)
{
	S.count[chan - 1][pitch]++;
	tr->snd_pitch = pitch;
	tr->snd_chan = chan;
	tr->snd_off = off;
	return send3(0x90 | (chan - 1), pitch, vel, tick);
}

/* Phase 8: note i of version v, played by track tr. */

/* Its note-on velocity: its own scaled by the track's ÷ 100, rounded
   and kept to 1..127 (so with the track at the default 100 it plays as
   written); else the track's. */
static int note_vel(const struct track *tr, const struct version *v,
                    int32_t i)
{
	int x;

	if (!v->vel || !v->vel[i])
		return tr->velocity;
	x = (v->vel[i] * tr->velocity + 50) / 100;
	return x < 1 ? 1 : x > 127 ? 127 : x;
}

static int note_chan(const struct version *v, int32_t i)
{
	return (v->chan && v->chan[i]) ? v->chan[i] : v->channel;
}

/* The tick of its note-off within the pass: its start plus its own
   length, or else the track's gate times its step, at least one tick
   and never past the next note's start (a longer length is shortened
   to the step, agreed Oct 15, 2026). With neither, the next note's
   start, as always. */
static int64_t note_end(const struct track *tr, const struct version *v,
                        int32_t i)
{
	int64_t step = v->start[i + 1] - v->start[i], len;

	if (v->len && v->len[i])
		len = v->len[i];
	else if (tr->gate >= 1.0)
		len = step;
	else
		len = (int64_t)(tr->gate * (double)step + 0.5);
	if (len < 1)
		len = 1;
	if (len > step)
		len = step;
	return v->start[i] + len;
}

/* Releases the track's sounding note; the note-off goes out only when
   no other track still has that pitch sounding on that channel. */
static int note_off(struct track *tr, int64_t tick)
{
	int *c, chan = tr->snd_chan, pitch = tr->snd_pitch;

	if (pitch < 0)
		return 0;
	c = &S.count[chan - 1][pitch];
	tr->snd_pitch = -1;
	if (*c > 0 && --*c == 0) {
		if (send3(0x80 | (chan - 1), pitch, 0, tick) != 0) {
			S.lost[chan - 1][pitch] = 1;	/* never arrived */
			return -1;
		}
	}
	return 0;
}

/* Reopened after a lost port (Phase 3): note-off for every note the
   synth may still be sounding, then All Notes Off (CC 123) on all 16
   channels as a backstop for anything missed (some receivers ignore
   CC 123, every one obeys a note-off). Each flag is cleared only once
   its note-off has gone out, so if the port is lost again partway,
   the next reopen sends the rest. 0 if all sent. */
static int release_lost(int64_t tick)
{
	int ch, p;

	for (ch = 0; ch < 16; ch++)
		for (p = 0; p < 128; p++)
			if (S.lost[ch][p]) {
				if (send3(0x80 | ch, p, 0, tick) != 0)
					return -1;
				S.lost[ch][p] = 0;
			}
	for (ch = 0; ch < 16; ch++)
		if (send3(0xB0 | ch, 123, 0, tick) != 0)
			return -1;
	return 0;
}

/* V100: reopened after a lost port, after release_lost(): the synth
   may have been reset, so every live track's program in effect goes
   out again at once. A track not yet started sends its own at its
   start. 0 if all sent. */
static int resend_programs(int64_t tick)
{
	int t;

	for (t = 1; t <= SEQ_MAX_TRACKS; t++) {
		struct track *tr = &S.tr[t];

		if (tr->cur && !tr->finished && tr->start_point < S.done
		    && tr->prog_on >= 0)
			if (send_program(tr, tr->cur->channel, tr->prog_on,
			                 tr->bank_on, tick) != 0)
				return -1;
	}
	return 0;
}

static void release_all(int64_t tick)
{
	int t;

	for (t = 1; t <= SEQ_MAX_TRACKS; t++)
		if (note_off(&S.tr[t], tick) != 0)
			break;
	if (S.state == SEQ_STOPPED || S.state == SEQ_PLAYING)
		memset(S.count, 0, sizeof S.count);
}

/* ── Versions ─────────────────────────────────────────────────────── */

static void free_version(struct version *v)
{
	if (v) {
		free(v->pitch);
		free(v->start);
		free(v->vel);
		free(v->len);
		free(v->chan);
		free(v);
	}
}

static void drop_versions(struct track *tr)
{
	free_version(tr->cur);
	free_version(tr->pend);
	tr->cur = tr->pend = NULL;
	tr->clear_req = 0;
	tr->finished = 0;
}

static int64_t length_of(const struct version *v)
{
	return v->start[v->n];
}

static int is_live(const struct track *tr)
{
	return tr->cur && !tr->finished && tr->start_point < S.done;
}

static int is_open(void)
{
	return S.state != SEQ_CLOSED && !S.closing;
}

/* ── Playback ─────────────────────────────────────────────────────── */

/* V100: a pass of tr's version starts at tick T: what is set now is in
   effect for this pass; it is sent if there is a program and it
   differs from what the track last sent (with the bank and the
   version's channel), or the pass is forced (a fresh start, a
   restart). -1 if the port was lost. */
static int pass_program(struct track *tr, int64_t T)
{
	int chan = tr->cur->channel;

	tr->prog_on = tr->program;
	tr->bank_on = tr->bank;
	if (tr->program < 0) {
		tr->prog_force = 0;
		return 0;
	}
	if (!tr->prog_force && tr->sent_prog == tr->program
	    && tr->sent_bank == tr->bank && tr->sent_chan == chan)
		return 0;
	return send_program(tr, chan, tr->program, tr->bank, T);
}

static int64_t next_event(void)
{
	int64_t m = INT64_MAX, e;
	int t;

	for (t = 1; t <= SEQ_MAX_TRACKS; t++) {
		struct track *tr = &S.tr[t];

		if (tr->snd_pitch >= 0 && tr->snd_off < m)
			m = tr->snd_off;
		if (tr->cur && !tr->finished) {
			e = tr->pass_base + tr->cur->start[tr->next];
			if (e < m)
				m = e;
		}
	}
	if (S.wait_armed && S.wait_tick < m)
		m = S.wait_tick;
	return m;
}

/* The end of a track's pass, at tick T. A play-once track with nothing
   pending keeps its version, finished: nothing more is scheduled for it
   (next_event() skips it) until a restart or a new version. */
static void boundary(struct track *tr, int64_t T)
{
	if (tr->clear_req) {
		drop_versions(tr);
		return;
	}
	if (tr->pend) {
		free_version(tr->cur);
		tr->cur = tr->pend;
		tr->pend = NULL;
	} else if (!tr->loop) {
		tr->finished = 1;
		return;
	}
	tr->pass_base = T;
	tr->next = 0;
}

/* Plays everything due at tick T, in the order seq.h gives. Stops
   early if the port is lost. */
static void play_tick(int64_t T)
{
	int t;

	for (t = 1; t <= SEQ_MAX_TRACKS; t++) {
		struct track *tr = &S.tr[t];

		if (tr->snd_pitch >= 0 && tr->snd_off == T)
			if (note_off(tr, T) != 0)
				return;
	}
	for (t = 1; t <= SEQ_MAX_TRACKS; t++) {
		struct track *tr = &S.tr[t];

		if (tr->cur && !tr->finished && tr->next == tr->cur->n
		    && tr->pass_base + length_of(tr->cur) == T)
			boundary(tr, T);
	}
	/* V100: program changes for every track starting a pass here
	   (pass_base is T and its first column hasn't played; start[0]
	   is always 0), before any note-on */
	for (t = 1; t <= SEQ_MAX_TRACKS; t++) {
		struct track *tr = &S.tr[t];

		if (tr->cur && !tr->finished && tr->next == 0
		    && tr->pass_base == T)
			if (pass_program(tr, T) != 0)
				return;
	}
	for (t = 1; t <= SEQ_MAX_TRACKS; t++) {
		struct track *tr = &S.tr[t];
		struct version *v = tr->cur;
		int32_t i;
		int p;

		if (!v || tr->finished || tr->next >= v->n
		    || tr->pass_base + v->start[tr->next] != T)
			continue;
		i = tr->next;
		p = v->pitch[i];
		/* Phase 7c: recorded before the mute test, so a muted
		   track still reports; a rest never does */
		if (p != SEQ_REST && tr->notes) {
			tr->ev_pitch = p;
			tr->ev_index = tr->next;
			tr->ev_tick = T;
			tr->ev_count++;
		}
		tr->next++;
		if (p != SEQ_REST && !tr->mute)
			if (note_on(tr, p, note_chan(v, i), note_vel(tr, v, i),
			            tr->pass_base + note_end(tr, v, i), T) != 0)
				return;
	}
	/* Phase 7c: notes, then the beat. While a wait for notes is
	   armed, anything outstanding started on this tick (arming hands
	   over anything older), so "any outstanding" is the test; it also
	   covers this tick being finished after a lost port. A beat on a
	   tick with notes is never reported: the wait is already over. */
	if (S.wait_armed && S.wait_notes && notes_outstanding())
		/* now: on the real clock, the time actually reached (any
		   lateness of the playback thread itself shows); on the
		   virtual clock, T, since the clock is at T */
		deliver_notes(S.wait_id, is_virtual() ? (double)T : cur_pos());
	if (S.wait_armed && S.wait_tick == T) {
		S.wait_armed = 0;
		notify(SEQ_EV_BEAT, S.wait_id, T / TPB, SEQ_OK);
	}
}

/* Waits on S.wake until clock time when_ns (ns since open), or until
   woken. If the condition variable couldn't be set to the monotonic
   clock (winpthreads), the wait is capped at 50 ms and:
   - Windows (Phase 6, Oct 4, 2026): a native wait on an event that
     every wake-up sets (wake_thread()), for the time left on the
     monotonic clock, in whole milliseconds rounded up. winpthreads'
     timed waits can't be used: read in its source, it rechecks each
     wait against GetTickCount64(), which moves only every ~15.6 ms
     and ignores timeBeginPeriod(), so every wait ran on to the next
     tick (measured on the person's machine with seqdemo -T: a 1 ms
     wait took 15.4 ms even after timeBeginPeriod(1), while Sleep(1)
     took 1.5 ms; playback was 9-10 ms late on average). A native
     wait follows the 1 ms timer, as Sleep() does. Converting an
     absolute time was a second, smaller problem
     (GetSystemTimeAsFileTime() steps too). No time of day is
     involved, so a change of the system time can't affect it. The
     event may be left set by a wake-up that came while the thread
     wasn't waiting; that only makes one wait end early, and the loop
     simply waits again.
   - elsewhere (only check_fallback, forcing this path on Linux): the
     deadline is turned into a time of day for this one wait, so a
     change of the system time can't stall playback or rush it for
     longer than the cap. */
static void timed_wait(int64_t when_ns)
{
	struct timespec ts;
	int64_t abs;

	if (S.monotonic) {
		abs = S.base_ns + when_ns;
	} else {
		int64_t rel = when_ns - now_ns();

		if (rel > 50000000)
			rel = 50000000;
		if (rel < 0)
			rel = 0;
#ifdef _WIN32
		pthread_mutex_unlock(&S.lock);
		WaitForSingleObject(wake_ev, (DWORD)((rel + 999999) / 1000000));
		pthread_mutex_lock(&S.lock);
		(void)ts;
		(void)abs;
		return;
#else
		clock_gettime(CLOCK_REALTIME, &ts);
		abs = (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec + rel;
#endif
	}

	ts.tv_sec = (time_t)(abs / 1000000000);
	ts.tv_nsec = (long)(abs % 1000000000);
	pthread_cond_timedwait(&S.wake, &S.lock, &ts);
}

static void *playback_thread(void *arg)
{
	(void)arg;
	pthread_mutex_lock(&S.lock);
	while (!S.quit) {
		int64_t T;

		if (S.state != SEQ_PLAYING) {
			pthread_cond_wait(&S.wake, &S.lock);
			continue;
		}
		T = next_event();
		if (is_virtual()) {
			if (T >= S.horizon) {
				if (!S.holding) {
					int64_t d = deadline(S.horizon);

					S.done = S.horizon;
					if (d > S.vnow)
						S.vnow = d;
					S.holding = 1;
					pthread_cond_broadcast(&S.held);
				}
				pthread_cond_wait(&S.wake, &S.lock);
				continue;
			}
			{
				int64_t d = deadline(T);

				if (d > S.vnow)
					S.vnow = d;
			}
		} else {
			int64_t late;

			if (T == INT64_MAX) {
				pthread_cond_wait(&S.wake, &S.lock);
				continue;
			}
			late = now_ns() - deadline(T);
			if (late < 0) {
				timed_wait(deadline(T));
				continue;
			}
			S.timing.ticks++;
			S.timing.late_sum_ns += late;
			if (late > S.timing.late_max_ns)
				S.timing.late_max_ns = late;
			if (late > 2000000)
				S.timing.late_2ms++;
		}
		play_tick(T);
		/* If the port was lost partway through T, T stays unplayed:
		   continuing finishes what's left of it (each step skips
		   what it has already done). */
		if (S.state == SEQ_PLAYING && S.done <= T)
			S.done = T + 1;
		/* Phase 7c, seq_virtual_run_to_event(): the wait has had
		   its event, so hold just past this tick */
		if (S.run_event && !S.wait_armed) {
			S.run_event = 0;
			if (S.horizon > S.done)
				S.horizon = S.done;
		}
	}
	pthread_mutex_unlock(&S.lock);
	return NULL;
}

/* ── Open, close, state ───────────────────────────────────────────── */

static void reset_tracks(void)
{
	int t;

	for (t = 1; t <= SEQ_MAX_TRACKS; t++) {
		struct track *tr = &S.tr[t];

		memset(tr, 0, sizeof *tr);
		tr->channel = t;
		tr->loop = SEQ_DEFAULT_LOOP;
		tr->velocity = SEQ_DEFAULT_VELOCITY;
		tr->gate = SEQ_DEFAULT_GATE;
		tr->snd_pitch = -1;
		tr->program = tr->bank = SEQ_NONE;		/* V100 */
		tr->prog_on = tr->bank_on = SEQ_NONE;
		tr->sent_prog = tr->sent_bank = tr->sent_chan = SEQ_NONE;
	}
	memset(S.count, 0, sizeof S.count);
}

int seq_open(const struct seq_config *cfg)
{
	int r;

	if (!cfg || cfg->backend < SEQ_BACKEND_DEVICE
	    || cfg->backend > SEQ_BACKEND_LOG_REALTIME)
		return SEQ_ERR_BACKEND;
	pthread_once(&init_once, init_conds);
	pthread_mutex_lock(&S.lock);
	while (S.closing)
		pthread_cond_wait(&S.held, &S.lock);

	if (S.state == SEQ_STOPPED || S.state == SEQ_PLAYING) {
		r = SEQ_ERR_ALREADY_OPEN;
	} else if (S.state == SEQ_PORT_LOST) {
		if (cfg->backend != S.backend) {
			r = SEQ_ERR_BACKEND;
		} else if ((r = S.out->open(cfg->port)) == SEQ_OK) {
			S.notify = cfg->notify;
			S.ctx = cfg->ctx;
			S.state = SEQ_STOPPED;
			if (release_lost((int64_t)S.stop_pos) != 0
			    || resend_programs((int64_t)S.stop_pos) != 0)
				r = SEQ_ERR_IO;		/* lost again */
		}
	} else {
		S.backend = cfg->backend;
		S.out = (cfg->backend == SEQ_BACKEND_DEVICE)
		        ? &seq_out_device : &seq_out_log;
		if (S.out != &seq_out_device)
			seq_log_reset();
		r = S.out->open(cfg->port);
		if (r == SEQ_OK) {
			reset_tracks();
			memset(S.lost, 0, sizeof S.lost);
			memset(&S.timing, 0, sizeof S.timing);
			S.notify = cfg->notify;
			S.ctx = cfg->ctx;
			S.tempo = SEQ_DEFAULT_TEMPO;
			S.done = 0;
			S.stop_pos = 0;
			S.anchor_ns = 0;
			S.anchor_tick = 0;
			S.base_ns = real_ns();
			S.vnow = 0;
			S.horizon = 0;
			S.holding = 0;
			S.wait_armed = 0;
			S.wait_notes = 0;
			S.beats_on = 0;		/* Phase 7c: off by default */
			S.run_event = 0;
			S.given_id = 0;
			S.late_ms = 0;
			S.next_id = 0;
			S.quit = 0;
			if (pthread_create(&S.thread, NULL,
			                   playback_thread, NULL) != 0) {
				S.out->close();
				r = SEQ_ERR_NOMEM;
			} else {
				S.state = SEQ_STOPPED;
			}
		}
	}
	pthread_mutex_unlock(&S.lock);
	return r;
}

int seq_close(int *closed, int *nclosed)
{
	int t, k = 0;

	pthread_mutex_lock(&S.lock);
	while (S.closing)
		pthread_cond_wait(&S.held, &S.lock);
	if (S.state == SEQ_CLOSED) {
		pthread_mutex_unlock(&S.lock);
		if (nclosed)
			*nclosed = 0;
		return SEQ_OK;
	}
	S.closing = 1;
	if (S.state == SEQ_PLAYING)
		release_all(stamp_tick());
	end_wait(SEQ_OK);
	for (t = 1; t <= SEQ_MAX_TRACKS; t++)
		if (S.tr[t].cur || S.tr[t].pend) {
			if (closed)
				closed[k] = t;
			k++;
		}
	S.quit = 1;
	wake_thread();
	pthread_cond_broadcast(&S.held);	/* releases any run_to */
	pthread_mutex_unlock(&S.lock);

	pthread_join(S.thread, NULL);

	pthread_mutex_lock(&S.lock);
	for (t = 1; t <= SEQ_MAX_TRACKS; t++)
		drop_versions(&S.tr[t]);
	if (S.state != SEQ_PORT_LOST)
		S.out->close();
	S.state = SEQ_CLOSED;
	S.closing = 0;
	pthread_cond_broadcast(&S.held);
	pthread_mutex_unlock(&S.lock);
	if (nclosed)
		*nclosed = k;
	return SEQ_OK;
}

int seq_get_state(int *state, int64_t *tick)
{
	pthread_mutex_lock(&S.lock);
	if (!is_open()) {
		*state = SEQ_CLOSED;
		if (tick)
			*tick = 0;
	} else {
		*state = S.state;
		if (tick)
			*tick = stamp_tick();
	}
	pthread_mutex_unlock(&S.lock);
	return SEQ_OK;
}

int seq_get_tracks(struct seq_track_info *info, int *n)
{
	int t, k = 0;

	pthread_mutex_lock(&S.lock);
	if (is_open()) {
		int64_t pos = stamp_tick();

		for (t = 1; t <= SEQ_MAX_TRACKS; t++) {
			struct track *tr = &S.tr[t];
			struct seq_track_info *in = &info[k];
			int live = is_live(tr);
			/* a finished track reports its kept version, the
			   one a restart plays and a MIDI file gets */
			int kept = live || tr->finished;

			if (!tr->cur && !tr->pend)
				continue;
			in->track = t;
			/* the channel notes are sounding on now: the playing
			   version's, or, with only a waiting version, the one
			   it will start on (pend exists only when cur is
			   live, so a waiting-only track's version is cur) */
			in->channel = tr->cur ? tr->cur->channel
			                      : tr->pend->channel;
			in->mute = tr->mute;
			in->loop = tr->loop;
			in->velocity = tr->velocity;
			in->notes = kept ? tr->cur->n : 0;
			in->length = kept ? length_of(tr->cur) : 0;
			in->pending = (tr->pend != NULL) || (tr->cur && !kept);
			in->report = tr->notes;
			in->gate = tr->gate;
			in->finished = tr->finished;
			in->program = tr->program;	/* V100 */
			in->bank = tr->bank;
			in->position = 0;
			if (live) {
				int64_t q = pos - tr->pass_base;

				if (q < 0)
					q = 0;
				in->position = q % in->length;
			}
			k++;
		}
	}
	pthread_mutex_unlock(&S.lock);
	*n = k;
	return SEQ_OK;
}

static void sleep_ms(int ms)
{
	struct timespec ts;

	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (long)(ms % 1000) * 1000000;
	nanosleep(&ts, NULL);
}

void seq_panic(void)
{
	int got = 0, i, ch, p;
	int64_t tick;
	unsigned char m[3];

	for (i = 0; i < 20 && !got; i++) {
		if (pthread_mutex_trylock(&S.lock) == 0)
			got = 1;
		else
			sleep_ms(5);
	}
	if ((S.state == SEQ_STOPPED || S.state == SEQ_PLAYING) && S.out) {
		tick = stamp_tick();
		for (ch = 0; ch < 16; ch++)
			for (p = 0; p < 128; p++)
				if (S.count[ch][p] > 0) {
					m[0] = (unsigned char)(0x80 | ch);
					m[1] = (unsigned char)p;
					m[2] = 0;
					S.out->send(m, 3, tick, now_ns());
				}
		for (ch = 0; ch < 16; ch++) {
			m[0] = (unsigned char)(0xB0 | ch);
			m[1] = 123;
			m[2] = 0;
			S.out->send(m, 3, tick, now_ns());
		}
		clear_sounding();
	}
	if (got)
		pthread_mutex_unlock(&S.lock);
}

/* ── Tracks ───────────────────────────────────────────────────────── */

static int bad_track(int t)
{
	return t < 1 || t > SEQ_MAX_TRACKS;
}

/* Converts a submission into a version: start ticks are the rounded
   running sums of the durations, each at least one tick after the
   last. Phase 8: vel, len and chan (each may be NULL) are the per-note
   values, SEQ_UNCHANGED (-1) for the track's setting; for a rest they
   aren't looked at. */
static int make_version(const int32_t *pitch, const double *dur,
                        const int32_t *vel, const double *len,
                        const int32_t *chan, int32_t n,
                        struct version **out)
{
	struct version *v;
	double sum = 0;
	int32_t i;

	if (n < 1)
		return SEQ_ERR_LENGTH;
	for (i = 0; i < n; i++) {
		if (pitch[i] != SEQ_REST && (pitch[i] < 0 || pitch[i] > 127))
			return SEQ_ERR_PITCH;
		if (pitch[i] != SEQ_REST) {
			if (vel && vel[i] != SEQ_UNCHANGED
			    && (vel[i] < 1 || vel[i] > 127))
				return SEQ_ERR_VALUE;
			/* a length: -1, or at least one tick, with the
			   same slack as a duration; rejects NaN */
			if (len && len[i] != -1.0
			    && !(len[i] * TPB >= 1.0 - 1e-9))
				return SEQ_ERR_DURATION;
			if (chan && chan[i] != SEQ_UNCHANGED
			    && (chan[i] < 1 || chan[i] > 16))
				return SEQ_ERR_CHANNEL;
		}
		/* a little slack, so 1/2520 computed in floating point
		   still counts as one tick; also rejects NaN */
		if (!(dur[i] * TPB >= 1.0 - 1e-9))
			return SEQ_ERR_DURATION;
		sum += dur[i];
		if (!(sum <= SEQ_MAX_BEATS))
			return SEQ_ERR_DURATION;
	}
	v = calloc(1, sizeof *v);
	if (!v)
		return SEQ_ERR_NOMEM;
	v->pitch = malloc((size_t)n * sizeof *v->pitch);
	v->start = malloc(((size_t)n + 1) * sizeof *v->start);
	if (!v->pitch || !v->start) {
		free_version(v);
		return SEQ_ERR_NOMEM;
	}
	if ((vel && !(v->vel = calloc((size_t)n, 1)))
	    || (len && !(v->len = calloc((size_t)n, sizeof *v->len)))
	    || (chan && !(v->chan = calloc((size_t)n, 1)))) {
		free_version(v);
		return SEQ_ERR_NOMEM;
	}
	for (i = 0; i < n; i++) {
		if (pitch[i] == SEQ_REST)
			continue;	/* 0: nothing of its own */
		if (vel && vel[i] != SEQ_UNCHANGED)
			v->vel[i] = (unsigned char)vel[i];
		if (len && len[i] != -1.0) {
			/* no longer than the longest track, so the ticks
			   fit; note_end() shortens it to the step anyway */
			double b = len[i] < SEQ_MAX_BEATS ? len[i]
			                                  : SEQ_MAX_BEATS;
			int64_t t = (int64_t)(b * TPB + 0.5);

			v->len[i] = t < 1 ? 1 : t;
		}
		if (chan && chan[i] != SEQ_UNCHANGED)
			v->chan[i] = (unsigned char)chan[i];
	}
	v->n = n;
	memcpy(v->pitch, pitch, (size_t)n * sizeof *v->pitch);
	v->start[0] = 0;
	sum = 0;
	for (i = 0; i < n; i++) {
		int64_t s;

		sum += dur[i];
		s = (int64_t)(sum * TPB + 0.5);
		if (s <= v->start[i])
			s = v->start[i] + 1;
		v->start[i + 1] = s;
	}
	*out = v;
	return SEQ_OK;
}

int seq_submit_track(int track, int channel,
                     const int32_t *pitch, const double *dur, int32_t n,
                     int64_t *swap_tick)
{
	return seq_submit_track_ex(track, channel, pitch, dur, NULL, NULL,
	                           NULL, n, swap_tick);
}

int seq_submit_track_ex(int track, int channel,
                        const int32_t *pitch, const double *dur,
                        const int32_t *vel, const double *len,
                        const int32_t *chan, int32_t n,
                        int64_t *swap_tick)
{
	struct version *v = NULL;
	struct track *tr;
	int64_t swap;
	int r;

	if (bad_track(track))
		return SEQ_ERR_TRACK;
	if (channel != SEQ_UNCHANGED && (channel < 1 || channel > 16))
		return SEQ_ERR_CHANNEL;
	if ((r = make_version(pitch, dur, vel, len, chan, n, &v)) != SEQ_OK)
		return r;

	pthread_mutex_lock(&S.lock);
	if (!is_open()) {
		pthread_mutex_unlock(&S.lock);
		free_version(v);
		return SEQ_ERR_NOT_OPEN;
	}
	tr = &S.tr[track];
	if (channel != SEQ_UNCHANGED)
		tr->channel = channel;
	v->channel = tr->channel;
	tr->clear_req = 0;
	if (is_live(tr)) {
		free_version(tr->pend);
		tr->pend = v;
		swap = tr->pass_base + length_of(tr->cur);
	} else {
		/* cur not live: one waiting to start (replaced, keeping its
		   start beat), or a finished one (nothing playing: the next
		   beat, as for an empty track) */
		swap = (tr->cur && !tr->finished) ? tr->start_point
		                                  : next_beat(avail_tick());
		free_version(tr->cur);
		free_version(tr->pend);
		tr->pend = NULL;
		tr->finished = 0;
		tr->cur = v;
		tr->start_point = tr->pass_base = swap;
		tr->next = 0;
		tr->prog_force = 1;	/* V100: a fresh start sends it */
	}
	wake_thread();
	pthread_mutex_unlock(&S.lock);
	if (swap_tick)
		*swap_tick = swap;
	return SEQ_OK;
}

/* Common front end for the list calls: checks every track (and value,
   when lo <= hi) before anything changes. */
static int check_list(const int *tracks, const int *values, int n,
                      int lo, int hi)
{
	int i;

	if (n < 0)
		return SEQ_ERR_LENGTH;
	for (i = 0; i < n; i++) {
		if (bad_track(tracks[i]))
			return SEQ_ERR_TRACK;
		if (values && (values[i] < lo || values[i] > hi))
			return SEQ_ERR_VALUE;
	}
	return SEQ_OK;
}

int seq_clear(const int *tracks, int n, int *prev)
{
	int i, r;

	if ((r = check_list(tracks, NULL, n, 0, 0)) != SEQ_OK)
		return r;
	pthread_mutex_lock(&S.lock);
	if (!is_open()) {
		pthread_mutex_unlock(&S.lock);
		return SEQ_ERR_NOT_OPEN;
	}
	if (prev)
		for (i = 0; i < n; i++)
			prev[i] = S.tr[tracks[i]].cur || S.tr[tracks[i]].pend;
	for (i = 0; i < n; i++) {
		struct track *tr = &S.tr[tracks[i]];

		if (S.state == SEQ_PLAYING && is_live(tr)) {
			free_version(tr->pend);
			tr->pend = NULL;
			tr->clear_req = 1;
		} else {
			note_off(tr, stamp_tick());
			drop_versions(tr);
		}
	}
	wake_thread();
	pthread_mutex_unlock(&S.lock);
	return SEQ_OK;
}

enum { SET_MUTE, SET_LOOP, SET_VEL, SET_NOTES, SET_PROG, SET_BANK };

static int set_list(int which, const int *tracks, const int *values,
                    int n, int *prev)
{
	int i, r;
	int lo = (which == SET_VEL) ? 1
	       : (which == SET_PROG || which == SET_BANK) ? SEQ_NONE : 0;
	int hi = (which == SET_VEL) ? 127
	       : (which == SET_PROG) ? SEQ_PROGRAM_MAX
	       : (which == SET_BANK) ? SEQ_BANK_MAX : 1; /* 0/1 for the rest */

	if ((r = check_list(tracks, values, n, lo, hi)) != SEQ_OK)
		return r;
	pthread_mutex_lock(&S.lock);
	if (!is_open()) {
		pthread_mutex_unlock(&S.lock);
		return SEQ_ERR_NOT_OPEN;
	}
	if (prev)
		for (i = 0; i < n; i++) {
			struct track *tr = &S.tr[tracks[i]];

			prev[i] = which == SET_MUTE ? tr->mute
			        : which == SET_LOOP ? tr->loop
			        : which == SET_NOTES ? tr->notes
			        : which == SET_PROG ? tr->program
			        : which == SET_BANK ? tr->bank : tr->velocity;
		}
	for (i = 0; i < n; i++) {
		struct track *tr = &S.tr[tracks[i]];

		switch (which) {
		case SET_MUTE:
			tr->mute = values[i];
			if (tr->mute)
				note_off(tr, stamp_tick());
			break;
		case SET_LOOP:
			tr->loop = values[i];
			break;
		case SET_NOTES:
			/* on: start with nothing outstanding; off: discard
			   it; the same value: no change */
			if (tr->notes != values[i]) {
				tr->ev_count = 0;
				S.given_count[tracks[i]] = 0;
			}
			tr->notes = values[i];
			break;
		case SET_PROG:		/* V100: from the next pass start */
			tr->program = values[i];
			break;
		case SET_BANK:
			tr->bank = values[i];
			break;
		default:
			tr->velocity = values[i];
			break;
		}
	}
	wake_thread();
	pthread_mutex_unlock(&S.lock);
	return SEQ_OK;
}

int seq_set_mute(const int *tracks, const int *values, int n, int *prev)
{
	return set_list(SET_MUTE, tracks, values, n, prev);
}

int seq_set_loop(const int *tracks, const int *values, int n, int *prev)
{
	return set_list(SET_LOOP, tracks, values, n, prev);
}

int seq_set_velocity(const int *tracks, const int *values, int n, int *prev)
{
	return set_list(SET_VEL, tracks, values, n, prev);
}

int seq_set_notes(const int *tracks, const int *values, int n, int *prev)
{
	return set_list(SET_NOTES, tracks, values, n, prev);
}

int seq_set_program(const int *tracks, const int *values, int n, int *prev)
{
	return set_list(SET_PROG, tracks, values, n, prev);
}

int seq_set_bank(const int *tracks, const int *values, int n, int *prev)
{
	return set_list(SET_BANK, tracks, values, n, prev);
}

int seq_set_gate(const int *tracks, const double *values, int n,
                 double *prev)
{
	int i, r;

	if ((r = check_list(tracks, NULL, n, 0, 0)) != SEQ_OK)
		return r;
	for (i = 0; i < n; i++)
		if (!(values[i] > 0.0 && values[i] <= 1.0))	/* and NaN */
			return SEQ_ERR_VALUE;
	pthread_mutex_lock(&S.lock);
	if (!is_open()) {
		pthread_mutex_unlock(&S.lock);
		return SEQ_ERR_NOT_OPEN;
	}
	if (prev)
		for (i = 0; i < n; i++)
			prev[i] = S.tr[tracks[i]].gate;
	for (i = 0; i < n; i++)
		S.tr[tracks[i]].gate = values[i];
	pthread_mutex_unlock(&S.lock);
	return SEQ_OK;
}

/* Phase 9: see seq.h. The version is cur: the playing one, a finished
   play-once one, or the one about to start (pend exists only behind a
   live cur). Worked out with
   the same note_vel(), note_chan() and note_end() play_tick() uses, so
   a file gets what the synth hears. */
int seq_get_track_notes(int track, struct seq_note_out **notes, int32_t *n,
                        int64_t *length, int *chan, int *program,
                        int *bank)
{
	struct seq_note_out *a = NULL;
	struct version *v;
	struct track *tr;
	int32_t i, k = 0, cnt = 0;

	*notes = NULL;
	*n = 0;
	*length = 0;
	if (chan) *chan = SEQ_NONE;
	if (program) *program = SEQ_NONE;
	if (bank) *bank = SEQ_NONE;
	if (bad_track(track))
		return SEQ_ERR_TRACK;
	pthread_mutex_lock(&S.lock);
	if (!is_open()) {
		pthread_mutex_unlock(&S.lock);
		return SEQ_ERR_NOT_OPEN;
	}
	tr = &S.tr[track];
	v = tr->cur;
	if (v) {
		for (i = 0; i < v->n; i++)
			if (v->pitch[i] != SEQ_REST)
				cnt++;
		if (cnt) {
			a = malloc((size_t)cnt * sizeof *a);
			if (!a) {
				pthread_mutex_unlock(&S.lock);
				return SEQ_ERR_NOMEM;
			}
		}
		for (i = 0; i < v->n; i++) {
			if (v->pitch[i] == SEQ_REST)
				continue;
			a[k].start = v->start[i];
			a[k].off = note_end(tr, v, i);
			a[k].pitch = v->pitch[i];
			a[k].vel = note_vel(tr, v, i);
			a[k].chan = note_chan(v, i);
			k++;
		}
		*length = length_of(v);
		/* V100: played (live or finished): what its pass began
		   with; waiting to start: the settings it will start with */
		if (chan) *chan = v->channel;
		if (is_live(tr) || tr->finished) {
			if (program) *program = tr->prog_on;
			if (bank) *bank = tr->bank_on;
		} else {
			if (program) *program = tr->program;
			if (bank) *bank = tr->bank;
		}
	}
	pthread_mutex_unlock(&S.lock);
	*notes = a;
	*n = k;
	return SEQ_OK;
}

/* ── Transport and tempo ──────────────────────────────────────────── */

static void stop_playing(void)
{
	double p;
	int64_t ne;

	if (is_virtual()) {
		p = (double)S.done;
	} else {
		/* never past an event the thread hasn't played yet */
		p = cur_pos();
		ne = next_event();
		if (ne != INT64_MAX && p > (double)ne)
			p = (double)ne;
	}
	release_all(floor_i64(p));
	if (S.state != SEQ_PLAYING)	/* port lost while releasing */
		return;
	S.stop_pos = p;
	S.state = SEQ_STOPPED;
	S.holding = 0;
	end_wait(SEQ_OK);
	pthread_cond_broadcast(&S.held);
}

static void start_playing(double from)
{
	S.anchor_ns = now_ns();
	S.anchor_tick = from;
	S.state = SEQ_PLAYING;
	S.holding = 0;
	if (is_virtual())
		S.horizon = S.done;	/* hold where it stopped */
}

int seq_play(int mode, int *prev)
{
	int t, r = SEQ_OK;

	if (mode < SEQ_STOP || mode > SEQ_RESTART)
		return SEQ_ERR_VALUE;
	pthread_mutex_lock(&S.lock);
	if (!is_open()) {
		pthread_mutex_unlock(&S.lock);
		return SEQ_ERR_NOT_OPEN;
	}
	if (prev)
		*prev = (S.state == SEQ_PLAYING);
	if (mode == SEQ_STOP) {
		if (S.state == SEQ_PLAYING)
			stop_playing();
	} else if (S.state == SEQ_PORT_LOST) {
		r = SEQ_ERR_IO;
	} else if (mode == SEQ_RESTART) {
		if (S.state == SEQ_PLAYING) {
			release_all(stamp_tick());
			end_wait(SEQ_OK);
		}
		if (S.state == SEQ_PORT_LOST) {
			r = SEQ_ERR_IO;
		} else {
			for (t = 1; t <= SEQ_MAX_TRACKS; t++) {
				struct track *tr = &S.tr[t];

				if (tr->clear_req) {
					drop_versions(tr);
					continue;
				}
				if (tr->pend) {
					free_version(tr->cur);
					tr->cur = tr->pend;
					tr->pend = NULL;
				}
				tr->finished = 0;	/* plays again */
				tr->prog_force = 1;	/* V100 */
				tr->start_point = tr->pass_base = 0;
				tr->next = 0;
				tr->ev_count = 0;	/* Phase 7c */
				S.given_count[t] = 0;
			}
			S.done = 0;
			S.horizon = 0;
			start_playing(0);
		}
	} else if (S.state == SEQ_STOPPED) {	/* SEQ_CONTINUE */
		start_playing(S.stop_pos);
	}
	wake_thread();
	pthread_mutex_unlock(&S.lock);
	return r;
}

int seq_set_tempo(double bpm, double *prev)
{
	if (!(bpm >= SEQ_TEMPO_MIN && bpm <= SEQ_TEMPO_MAX))
		return SEQ_ERR_TEMPO;
	pthread_mutex_lock(&S.lock);
	if (!is_open()) {
		pthread_mutex_unlock(&S.lock);
		return SEQ_ERR_NOT_OPEN;
	}
	if (prev)
		*prev = S.tempo;
	if (S.state == SEQ_PLAYING) {
		double p = cur_pos();

		S.anchor_ns = now_ns();
		S.anchor_tick = p;
	}
	S.tempo = bpm;
	wake_thread();
	pthread_mutex_unlock(&S.lock);
	return SEQ_OK;
}

int seq_get_tempo(double *bpm)
{
	int r = SEQ_OK;

	pthread_mutex_lock(&S.lock);
	if (!is_open())
		r = SEQ_ERR_NOT_OPEN;
	else
		*bpm = S.tempo;
	pthread_mutex_unlock(&S.lock);
	return r;
}

/* ── Beat wait ────────────────────────────────────────────────────── */

int seq_request_beat(uint32_t *request_id)
{
	int r = SEQ_OK;

	pthread_mutex_lock(&S.lock);
	if (!is_open()) {
		r = SEQ_ERR_NOT_OPEN;
	} else if (S.state != SEQ_PLAYING) {
		r = SEQ_ERR_NOT_PLAYING;
	} else {
		if (++S.next_id == 0)
			S.next_id = 1;
		S.wait_id = S.next_id;
		S.wait_tick = next_beat(avail_tick());
		S.wait_notes = 0;
		S.given_id = 0;
		S.wait_armed = 1;
		*request_id = S.wait_id;
		wake_thread();
	}
	pthread_mutex_unlock(&S.lock);
	return r;
}

/* ── Event wait (Phase 7c) ────────────────────────────────────────── */

int seq_set_beats(int on, int *prev)
{
	int r = SEQ_OK;

	if (on != 0 && on != 1)
		return SEQ_ERR_VALUE;
	pthread_mutex_lock(&S.lock);
	if (!is_open()) {
		r = SEQ_ERR_NOT_OPEN;
	} else {
		if (prev)
			*prev = S.beats_on;
		S.beats_on = on;
	}
	pthread_mutex_unlock(&S.lock);
	return r;
}

int seq_get_beats(int *on)
{
	int r = SEQ_OK;

	pthread_mutex_lock(&S.lock);
	if (!is_open())
		r = SEQ_ERR_NOT_OPEN;
	else
		*on = S.beats_on;
	pthread_mutex_unlock(&S.lock);
	return r;
}

int seq_get_note_late(double *ms)
{
	int r = SEQ_OK;

	pthread_mutex_lock(&S.lock);
	if (!is_open())
		r = SEQ_ERR_NOT_OPEN;
	else
		*ms = S.late_ms;
	pthread_mutex_unlock(&S.lock);
	return r;
}

/* 1 if some reporting track has a version, playing or waiting (a
   finished one plays nothing more until a restart or a new version). */
static int notes_possible(void)
{
	int t;

	for (t = 1; t <= SEQ_MAX_TRACKS; t++) {
		struct track *tr = &S.tr[t];

		if (tr->notes && ((tr->cur && !tr->finished) || tr->pend))
			return 1;
	}
	return 0;
}

int seq_request_event(uint32_t *request_id)
{
	int r = SEQ_OK;

	pthread_mutex_lock(&S.lock);
	if (!is_open()) {
		r = SEQ_ERR_NOT_OPEN;
	} else if (notes_outstanding()) {
		/* hand them over now: any earlier wait is replaced */
		S.given_id = 0;
		if (++S.next_id == 0)
			S.next_id = 1;
		S.wait_id = S.next_id;
		*request_id = S.wait_id;
		deliver_notes(S.wait_id, cur_pos());	/* disarms */
	} else if (S.state != SEQ_PLAYING) {
		r = SEQ_ERR_NOT_PLAYING;
	} else if (!S.beats_on && !notes_possible()) {
		r = SEQ_ERR_NO_EVENT;
	} else {
		if (++S.next_id == 0)
			S.next_id = 1;
		S.wait_id = S.next_id;
		S.wait_tick = S.beats_on ? next_beat(avail_tick()) : INT64_MAX;
		S.wait_notes = 1;
		S.given_id = 0;
		S.wait_armed = 1;
		*request_id = S.wait_id;
		wake_thread();
	}
	pthread_mutex_unlock(&S.lock);
	return r;
}

int seq_cancel_wait(uint32_t request_id)
{
	int r = SEQ_OK;

	pthread_mutex_lock(&S.lock);
	if (!is_open()) {
		r = SEQ_ERR_NOT_OPEN;
	} else if (S.wait_armed && S.wait_id == request_id) {
		S.wait_armed = 0;
	} else if (request_id != 0 && S.given_id == request_id) {
		/* Phase 7c: its notes were handed over but not taken: put
		   them back, under anything newer (a newer note keeps its
		   pitch and index; the count covers both) */
		int t;

		for (t = 1; t <= SEQ_MAX_TRACKS; t++) {
			struct track *tr = &S.tr[t];

			if (S.given_count[t] <= 0 || !tr->notes)
				continue;
			if (tr->ev_count <= 0) {
				tr->ev_pitch = S.given_pitch[t];
				tr->ev_index = S.given_index[t];
				tr->ev_tick = S.given_tick[t];
			}
			tr->ev_count += S.given_count[t];
		}
		S.given_id = 0;
		memset(S.given_count, 0, sizeof S.given_count);
	}
	pthread_mutex_unlock(&S.lock);
	return r;
}

/* ── Test mode ────────────────────────────────────────────────────── */

int seq_virtual_run_to(int64_t tick)
{
	int r = SEQ_OK;

	pthread_mutex_lock(&S.lock);
	if (!is_open())
		r = SEQ_ERR_NOT_OPEN;
	else if (S.backend != SEQ_BACKEND_VIRTUAL)
		r = SEQ_ERR_BACKEND;
	else if (S.state == SEQ_PORT_LOST)
		r = SEQ_ERR_IO;
	else if (S.state != SEQ_PLAYING)
		r = SEQ_ERR_NOT_PLAYING;
	else if (tick > S.done) {
		S.horizon = tick;
		S.holding = 0;
		wake_thread();
		while (S.state == SEQ_PLAYING && !S.closing
		       && !(S.holding && S.done == tick))
			pthread_cond_wait(&S.held, &S.lock);
		if (S.state == SEQ_PORT_LOST)
			r = SEQ_ERR_IO;
		else if (S.state != SEQ_PLAYING || S.closing)
			r = SEQ_ERR_NOT_PLAYING;
	}
	pthread_mutex_unlock(&S.lock);
	return r;
}

int seq_virtual_run_to_event(int64_t max_ticks)
{
	int r = SEQ_OK;

	pthread_mutex_lock(&S.lock);
	if (!is_open())
		r = SEQ_ERR_NOT_OPEN;
	else if (S.backend != SEQ_BACKEND_VIRTUAL)
		r = SEQ_ERR_BACKEND;
	else if (S.state == SEQ_PORT_LOST)
		r = SEQ_ERR_IO;
	else if (S.state != SEQ_PLAYING)
		r = SEQ_ERR_NOT_PLAYING;
	else if (S.wait_armed) {
		int64_t limit = S.done + (max_ticks > 0 ? max_ticks : 1);

		S.horizon = limit;
		S.run_event = 1;
		S.holding = 0;
		wake_thread();
		/* until the thread holds: just past the event (the thread
		   moved the horizon there), or at the limit; or playback
		   stopped (a lost port ends the wait with STOPPED) */
		while (S.state == SEQ_PLAYING && !S.closing
		       && !(S.holding && S.done == S.horizon))
			pthread_cond_wait(&S.held, &S.lock);
		S.run_event = 0;
		if (!S.wait_armed)
			r = SEQ_OK;
		else if (S.state == SEQ_PORT_LOST)
			r = SEQ_ERR_IO;
		else if (S.state != SEQ_PLAYING || S.closing)
			r = SEQ_ERR_NOT_PLAYING;
		else
			r = SEQ_ERR_NO_EVENT;
	}
	pthread_mutex_unlock(&S.lock);
	return r;
}

int seq_virtual_log(char *buf, size_t bufsize, size_t *len, int clear)
{
	int r;

	pthread_mutex_lock(&S.lock);
	if (is_open() && S.backend == SEQ_BACKEND_DEVICE)
		r = SEQ_ERR_BACKEND;
	else
		r = seq_log_copy(buf, bufsize, len, clear);
	pthread_mutex_unlock(&S.lock);
	return r;
}

int seq_virtual_fail_writes(int on)
{
	int r = SEQ_OK;

	pthread_mutex_lock(&S.lock);
	if (is_open() && S.backend == SEQ_BACKEND_DEVICE)
		r = SEQ_ERR_BACKEND;
	else
		seq_log_fail(on != 0);
	pthread_mutex_unlock(&S.lock);
	return r;
}

/* ── Timing (internal, seq_internal.h) ──────────────────────────── */

void seq_get_timing(struct seq_timing *t, int reset)
{
	pthread_mutex_lock(&S.lock);
	*t = S.timing;
	if (reset)
		memset(&S.timing, 0, sizeof S.timing);
	pthread_mutex_unlock(&S.lock);
}

/* ── Messages ─────────────────────────────────────────────────────── */

const char *seq_strerror(int err)
{
	switch (err) {
	case SEQ_OK:               return "OK";
	case SEQ_ERR_NOT_OPEN:     return "sequencer not open";
	case SEQ_ERR_ALREADY_OPEN: return "sequencer already open";
	case SEQ_ERR_PORT:         return "MIDI port could not be opened";
	case SEQ_ERR_IO:           return "MIDI port lost";
	case SEQ_ERR_TRACK:        return "track number out of range";
	case SEQ_ERR_CHANNEL:      return "channel out of range";
	case SEQ_ERR_PITCH:        return "pitch out of range";
	case SEQ_ERR_DURATION:     return "bad duration";
	case SEQ_ERR_LENGTH:       return "no notes";
	case SEQ_ERR_VALUE:        return "value out of range";
	case SEQ_ERR_TEMPO:        return "tempo out of range";
	case SEQ_ERR_NOT_PLAYING:  return "not playing";
	case SEQ_ERR_NOMEM:        return "out of memory";
	case SEQ_ERR_BACKEND:      return "not valid for this backend";
	case SEQ_ERR_PORT_BUSY:    return "MIDI port in use";
	case SEQ_ERR_PORT_ACCESS:  return "no permission for MIDI port";
	case SEQ_ERR_PORT_NAME:    return "MIDI port name not understood";
	case SEQ_ERR_NO_EVENT:     return "no event to wait for";
	default:                   return "unknown error";
	}
}
