// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Keith Doyle

/*
	seq.h -- MIDI sequencer library, public interface.
	Agreed Oct 4, 2026 (Phase 1); Phase 2 additions marked "Phase 2";
	Phase 7c (note events, the beat switch, the event wait; Oct 12,
	2026) marked "Phase 7c"; Phase 8 (opening a port by device name,
	the port list; per-note velocity, length and channel, the gate;
	Oct 15, 2026) marked "Phase 8"; Phase 9 (reading a track's notes
	back out, for writing MIDI files; Oct 16, 2026) marked "Phase 9";
	program changes and bank select (Oct 18, 2026, V100) marked
	"V100"; the meter and track names for MIDI files (Oct 19, 2026,
	V101) marked "V101". All added without
	changing what was there, except that a name not in the "hw:" form
	(Linux) or not a number (Windows), once SEQ_ERR_PORT_NAME, is now
	looked up as a device name (Phase 8), and that V100 gave
	seq_get_track_notes() three more outputs and struct
	seq_track_info two more fields (the sequencer is young enough to
	refactor; every caller is in this project).

	Lives in sequencer/ at the project root, beside backend/ and
	frontend/. The only contact surface between the APL interpreter's
	⎕SEQ glue (backend/seqfuncs.c), the Qt front end's shutdown path,
	and the standalone test driver on one side, and the sequencer
	library on the other. Includes no interpreter, Qt or pthreads
	headers, knows nothing about APL arrays. See MIDI_SEQUENCER_NOTES.md
	for the design.

	One sequencer per process (one MIDI port), so there is no handle:
	the state is a single static object inside the library. That also
	means seq_close() from one thread can never leave another thread
	holding a freed pointer.

	States (seq_get_state()):
	  CLOSED     nothing open, no tracks. Only seq_open(), seq_close()
	             and seq_get_state() do anything; every other call
	             returns SEQ_ERR_NOT_OPEN.
	  STOPPED    port open, not playing. Tracks, settings and the
	             position are kept (a stop is a pause).
	  PLAYING    port open, playing.
	  PORT_LOST  a device write failed (cable unplugged, say). Playback
	             stopped, the port is closed, but tracks, settings and
	             the position are kept. Track and setting calls still
	             work (nothing sounds); seq_play() to start returns
	             SEQ_ERR_IO. seq_open() reopens a port and returns to
	             STOPPED, so seq_play(SEQ_CONTINUE) carries on where it
	             stopped. seq_close() discards everything, as always.

	Threads. Every call is safe from any thread and returns quickly,
	except seq_virtual_run_to(), which waits for the playback thread to
	reach the horizon (test mode only). The playback thread is created
	by seq_open() from CLOSED and ended by seq_close(). Nothing in this
	library blocks waiting for music: a beat wait is armed here and the
	caller does its own blocking (see seq_request_beat()).

	Numbering. Tracks 1..SEQ_MAX_TRACKS and channels 1..16, the same
	numbers APL uses, so logs, tests and errors read the same at both
	levels. The library converts channels to 0..15 at the one place it
	writes MIDI bytes.

	Time. Positions are whole ticks, SEQ_TICKS_PER_BEAT per beat, so
	triplets, quintuplets and septuplets are exact. Beat numbers count
	whole beats of the main clock from 0.

	Same tick (Phase 2). When several things fall on one tick, the
	order is: every note-off (all tracks), then loop boundaries (swaps,
	clears, play-once endings), then program changes (V100: bank
	select and program change for tracks starting a pass), then
	note-ons, then the beat event. So a
	repeated pitch, or a loop whose last and first notes share a pitch,
	always gets a fresh note-off then note-on. Within each step, tracks
	go in ascending order. A note-on is always sent, even if the same
	pitch is already sounding on that channel from another track (a
	re-strike); the note-off goes out only when the last one ends.

	Start points. Each track has a start point on the main clock, the
	tick at which its current run began. Each version plays from its
	first note at the end of the version before, so the track never
	drifts against itself. A track that starts
	from nothing (see seq_submit_track()) gets the next whole beat as its
	start point; a new version of a playing track keeps the old start
	point, so the track stays in step with itself. Tracks started at the
	same beat stay in step with each other. Restarting playback from
	beat 0 sets every track's start point to 0.
*/

#ifndef SEQ_H
#define SEQ_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {	/* main.cpp's stopEngine() calls seq_close() */
#endif

#define SEQ_API_VERSION     1      /* for a later dlopen() check */

#define SEQ_MAX_TRACKS      16
#define SEQ_TICKS_PER_BEAT  2520   /* divisible by 2..10 */
#define SEQ_REST            (-1)   /* pitch value for a rest */
#define SEQ_UNCHANGED       (-1)   /* "leave this setting as it is" */

/* Defaults for a track that has never been set. */
#define SEQ_DEFAULT_VELOCITY 100   /* 1..127 */
#define SEQ_DEFAULT_LOOP     1
#define SEQ_DEFAULT_GATE     1.0   /* Phase 8: notes sound their whole
                                      step */
#define SEQ_NONE            (-1)   /* V100: no program, no bank (the
                                      default): nothing is sent */
#define SEQ_PROGRAM_MAX     127
#define SEQ_BANK_MAX        16383  /* controller 0 (MSB) x 128 +
                                      controller 32 (LSB) */
#define SEQ_METER_NUM_MAX   255    /* V101: a meter's top number */
#define SEQ_METER_DEN_MAX   32     /* V101: its bottom, a power of 2 */
#define SEQ_NAME_MAX        64     /* V101: a track name's bytes */
/* default channel = track number */

#define SEQ_MAX_BEATS       1e9    /* longest track, beats (Phase 2;
                                      keeps ticks exact in a double) */

#define SEQ_TEMPO_MIN       1.0    /* beats per minute */
#define SEQ_TEMPO_MAX       1000.0
#define SEQ_DEFAULT_TEMPO   120.0

/* ── Error codes ─────────────────────────────────────────────────────
   Every call returns one of these. The glue maps them to APL errors
   (most to ERR_DOMAIN or ERR_LENGTH). */
enum seq_err {
	SEQ_OK = 0,
	SEQ_ERR_NOT_OPEN,	/* CLOSED */
	SEQ_ERR_ALREADY_OPEN,	/* seq_open() while STOPPED or PLAYING */
	SEQ_ERR_PORT,		/* device couldn't be opened: no such
				   port (Phase 3: the specific causes
				   below have their own codes) */
	SEQ_ERR_IO,		/* PORT_LOST: the device stopped taking
				   data; reopen with seq_open() */
	SEQ_ERR_TRACK,		/* track number out of range */
	SEQ_ERR_CHANNEL,	/* channel out of range */
	SEQ_ERR_PITCH,		/* pitch not 0..127 or SEQ_REST */
	SEQ_ERR_DURATION,	/* duration under one tick
				   (1/SEQ_TICKS_PER_BEAT beat), not a
				   number, or a track longer than
				   SEQ_MAX_BEATS in all */
	SEQ_ERR_LENGTH,		/* note count < 1 */
	SEQ_ERR_VALUE,		/* velocity, loop flag or mode out of range */
	SEQ_ERR_TEMPO,		/* outside SEQ_TEMPO_MIN..MAX */
	SEQ_ERR_NOT_PLAYING,	/* beat wait or virtual run_to asked for
				   while not PLAYING */
	SEQ_ERR_NOMEM,
	SEQ_ERR_BACKEND,	/* call not valid for this backend (e.g.
				   run_to on a real device), or seq_open()
				   from PORT_LOST with a different backend */
	/* Phase 3: why seq_open() couldn't open a device, added at the
	   end so no existing code changes number. */
	SEQ_ERR_PORT_BUSY,	/* in use (by another program, or every
				   subdevice taken) */
	SEQ_ERR_PORT_ACCESS,	/* no permission (Linux: not in the audio
				   group; never on Windows) */
	SEQ_ERR_PORT_NAME,	/* port name not understood. Since
				   Phase 8 only an empty name, or (Linux)
				   one starting "hw:" that isn't "hw:C",
				   "hw:C,D" or "hw:C,D,S": any other name
				   is looked up as a device name */
	/* Phase 7c, added at the end like the Phase 3 codes. */
	SEQ_ERR_NO_EVENT	/* seq_request_event(): playing, but
				   nothing can produce an event (beats
				   off, no reporting track has notes);
				   seq_virtual_run_to_event(): none
				   within the limit */
};

/* Short English text for an error code, for messages and the test
   driver. Never NULL. */
const char *seq_strerror(int err);

/* ── Events (callback) ───────────────────────────────────────────────
   The library reports to its caller through one callback, given at
   open. It runs on whichever thread caused the event: usually the
   playback thread (SEQ_EV_BEAT, and SEQ_EV_STOPPED for a lost port),
   but SEQ_EV_STOPPED caused by seq_play(SEQ_STOP) or seq_close() may
   run on the thread that made that call (the engine thread, or the GUI
   thread from stopEngine()). Either way it may run while the library
   holds its own lock, so it must be quick, must only touch the
   caller's own lock-protected state, and must not call any seq_*
   function (that would deadlock).
   Lock order, for the caller: never call a seq_* function while
   holding the lock the callback takes. The library's lock is always
   taken first; the caller's lock, inside the callback, second. */
enum seq_event_kind {
	SEQ_EV_BEAT = 1,	/* the beat a seq_request_beat() asked for */
	SEQ_EV_STOPPED,		/* playback stopped while a wait was armed;
				   the wait is over */
	SEQ_EV_NOTES		/* Phase 7c: notes from the tracks set to
				   report them (seq_set_notes()) */
};

/* Phase 7c: one reporting track's latest note not yet reported. */
struct seq_note_event {
	int      track;		/* 1..SEQ_MAX_TRACKS */
	int      pitch;		/* 0..127 (a rest never reports) */
	int32_t  index;		/* the note's column in its version, from
				   0, rests counted as columns */
	int32_t  count;		/* notes (not rests) the track started
				   since the last one reported, this one
				   included: normally 1 (capped at
				   INT32_MAX) */
};

struct seq_event {
	int      kind;		/* enum seq_event_kind */
	uint32_t request_id;	/* from seq_request_beat() or
				   seq_request_event() */
	int64_t  beat;		/* SEQ_EV_BEAT: main-clock beat number */
	int      reason;	/* SEQ_EV_STOPPED: SEQ_OK (stopped or
				   closed) or SEQ_ERR_IO (port lost) */
	/* Phase 7c, SEQ_EV_NOTES: every reporting track with a note not
	   yet reported, in ascending track order. */
	int      nnotes;
	struct seq_note_event notes[SEQ_MAX_TRACKS];
};

typedef void (*seq_notify_fn)(void *ctx, const struct seq_event *ev);

/* ── Open, close, state ──────────────────────────────────────────── */

enum seq_backend {
	SEQ_BACKEND_DEVICE = 0,	/* ALSA raw MIDI on Linux (Phase 3),
				   WinMM on Windows (Phase 6, brought
				   forward to Oct 4, 2026) */
	SEQ_BACKEND_VIRTUAL,	/* test mode: simulated clock with a
				   horizon, output to an in-memory log */
	SEQ_BACKEND_LOG_REALTIME /* test mode (Phase 2): the real clock,
				   output to the in-memory log. Checks the
				   timed-wait path without a device; the
				   ticks in the log repeat exactly, the
				   times don't. */
};

struct seq_config {
	int           backend;	/* enum seq_backend */
	const char   *port;	/* DEVICE: "hw:C,D,S" on Linux (Phase 3:
				   also "hw:C,D", any free subdevice, and
				   "hw:C", device 0), a device number as
				   text ("0") on Windows; or (Phase 8) a
				   device name, see seq_open().
				   Log backends: ignored. */
	seq_notify_fn notify;	/* may be NULL: no events */
	void         *ctx;	/* passed back to notify */
};

/* From CLOSED: opens the port and starts the playback thread, STOPPED,
   no tracks, at SEQ_DEFAULT_TEMPO, position 0.
   From PORT_LOST: opens the port given (it may differ from the old
   one), keeps everything else, and returns to STOPPED. Phase 3 (agreed
   Oct 4, 2026): it first sends note-off for every note that was
   sounding when the port was lost (including one whose note-off was
   the write that failed), then All Notes Off (CC 123) on all 16
   channels, so nothing is left hanging on the synth. If one of those
   writes fails, the port is lost again: SEQ_ERR_IO, still PORT_LOST,
   and the next reopen sends whatever didn't go out.
   From STOPPED or PLAYING: SEQ_ERR_ALREADY_OPEN.
   A device that can't be opened gives SEQ_ERR_PORT (no such port),
   SEQ_ERR_PORT_BUSY, SEQ_ERR_PORT_ACCESS or SEQ_ERR_PORT_NAME
   (Phase 3). Opening never waits for a busy port.

   Opening by device name (Phase 8, agreed Oct 15, 2026). A port that
   isn't in the "hw:" form (Linux; the prefix in either case) or all
   digits (Windows) is a device name: the first port in
   seq_dev_list()'s order whose label contains it, ignoring the case of
   A-Z, is opened ("u2midi" opens "U2MIDI Pro / U2MIDI Pro MIDI 1").
   No match is SEQ_ERR_PORT, as for a port that isn't there, or
   SEQ_ERR_PORT_ACCESS if a card was left out of the list for lack of
   permission (Linux: the audio group covers every card, so that is
   the likelier reason); an empty name is SEQ_ERR_PORT_NAME (it would
   match everything). The name is
   looked up at every open, so reopening after a lost port finds the
   device again even if it came back under another card or device
   number. Only the first match is tried: if that port is busy the
   result is SEQ_ERR_PORT_BUSY, even if a later one would match. */
int seq_open(const struct seq_config *cfg);

/* Output ports (Phase 8; internal since Phase 3, for seqdemo -l).
   Fills up to max entries in the order a device name is matched in,
   and returns how many ports there are (may be more than max). Doesn't
   need the sequencer open; safe from any thread.
   Linux: every raw MIDI output subdevice of cards 0..31, "hw:C,D,S",
   labelled with its card, device and subdevice names; a card that
   can't be opened (no permission) is left out.
   Windows: every WinMM output device, by number, labelled with its
   name.
   Labels are plain ASCII: any byte outside 32..126 becomes '?', so
   what a list shows is exactly what a name is matched against. */
#define SEQ_MAX_PORTS 128	/* enough for any real setup; a name is
				   matched against the first this many */
struct seq_port_info {
	char port[40];		/* what seq_open() takes */
	char name[120];		/* label: card / device / subdevice names */
};
int seq_dev_list(struct seq_port_info *ports, int max);

/* From any state: stops playback, sends note-off for every sounding
   note, ends the playback thread, closes the port if open, discards
   tracks and settings, and goes to CLOSED. Any armed beat wait gets
   SEQ_EV_STOPPED. Safe to call more than once and from any thread (the
   GUI's stopEngine() calls it too); from CLOSED it returns SEQ_OK with
   *nclosed = 0.
   closed (room for SEQ_MAX_TRACKS, may be NULL) receives the numbers
   of the tracks that held something (finished play-once tracks
   included, since V96), *nclosed how many. */
int seq_close(int *closed, int *nclosed);

enum seq_state {
	SEQ_CLOSED = 0,
	SEQ_STOPPED,
	SEQ_PLAYING,
	SEQ_PORT_LOST
};

/* Never fails, in any state. state receives enum seq_state; tick (may
   be NULL) the current main-clock position (0 when CLOSED). */
int seq_get_state(int *state, int64_t *tick);

/* One entry per track in use (a playing version, one waiting to take
   over, or a finished play-once one), for ⎕SEQSTATE's track rows. */
struct seq_track_info {
	int     track;		/* 1..SEQ_MAX_TRACKS */
	int32_t notes;		/* notes in the playing version (or the
				   finished one), 0 if none yet (only a
				   pending one, or a version whose start
				   beat hasn't been reached) */
	int     channel;	/* 1..16: the playing version's channel; with
				   only a waiting version, the channel it
				   will start on (Phase 2, agreed Oct 4) */
	int     mute;		/* 0/1 */
	int     loop;		/* 0/1 */
	int     velocity;	/* 1..127 */
	int64_t length;		/* loop length, ticks (playing or finished
				   version; 0 if none) */
	int     pending;	/* 1 if a version is waiting to take over
				   (including one whose start beat hasn't
				   been reached yet) */
	int64_t position;	/* position within the playing version's
				   current pass, ticks, 0..length-1 (0 if
				   no playing version, and when finished) */
	int     report;		/* Phase 7c: 1 if it reports note events
				   (seq_set_notes()) */
	double  gate;		/* Phase 8: seq_set_gate() */
	int     finished;	/* 1: a play-once track that has played
				   through. It keeps its version, silent,
				   until a restart (which plays it again),
				   a new version, seq_clear() or
				   seq_close() (agreed after V95). pending
				   is 0. */
	int     program;	/* V100: seq_set_program(), 0..127 or
				   SEQ_NONE: the setting, which applies
				   from the track's next pass */
	int     bank;		/* V100: seq_set_bank(), 0..16383 or
				   SEQ_NONE, likewise */
};

/* Fills info (room for SEQ_MAX_TRACKS) in ascending track order, all
   read under one lock so the rows agree with each other; *n receives
   how many. Never fails; *n = 0 when CLOSED. */
int seq_get_tracks(struct seq_track_info *info, int *n);

/* Last-resort note release for fatal_error(), just before _Exit().
   Tries the library's lock for a short time; whether or not it gets
   it, sends note-off for everything in the sounding-note table, then
   All Notes Off (CC 123) on all 16 channels. Does not end the thread
   or free anything. Never blocks for long, never fails; does nothing
   unless STOPPED or PLAYING. */
void seq_panic(void);

/* ── Tracks ──────────────────────────────────────────────────────────
   These work in STOPPED, PLAYING and PORT_LOST. */

/* Submits a new version of one track. The data is copied; the caller's
   arrays may be reused as soon as this returns. No limit on n beyond
   memory.

   track    1..SEQ_MAX_TRACKS
   channel  1..16, or SEQ_UNCHANGED (keep the track's current channel;
            a track's first channel is its own number). A change takes
            effect with this version of the track.
   pitch    n values, 0..127 or SEQ_REST
   dur      n values, beats, each at least one tick
            (1/SEQ_TICKS_PER_BEAT beat); anything shorter is
            SEQ_ERR_DURATION, whatever its position. Start positions
            (running sums) are rounded to whole ticks, so rounding never
            accumulates, and every accepted note gets at least one tick.
   n        >= 1

   When it takes over. The same rule applies whether or not playback is
   running (a stop is a pause and changes nothing here):
   - The track has a playing version: the new version waits in the
     track's pending buffer and takes over at the end of the playing
     version's current loop, keeping the track's start point.
   - The track has no playing version (never used, cleared, or a
     play-once track that has finished): it starts from its first note
     at the next whole beat at or after the current position, which
     becomes its start point. Stopped at beat 0, that is beat 0, so it
     is in place at once. A finished track's kept version is replaced.
     Resending a finished play-once track plays it again from there; a
     restart plays it again from beat 0 with the rest.
   A later submission before the takeover replaces the pending one.
   seq_play(SEQ_RESTART) makes every pending version take over at
   beat 0.

   swap_tick (may be NULL) receives the main-clock tick at which it
   will take over, as things stand when this returns: a restart, a
   seq_clear() of the track, or a newer submission can change that
   before it happens. Tempo changes don't affect it. */
int seq_submit_track(int track, int channel,
                     const int32_t *pitch, const double *dur, int32_t n,
                     int64_t *swap_tick);

/* Phase 8 (agreed Oct 15, 2026): seq_submit_track() with, per note, a
   velocity, a sounding length and a channel; seq_submit_track() is
   this with all three NULL. Each array may be NULL (not given), and an
   entry of SEQ_UNCHANGED (-1; -1.0 for len) means the track's setting.
   For a rest the three are ignored, whatever they hold.

   vel   1..127, scaled by the track's velocity (seq_set_velocity()):
         sent as vel * track velocity / 100, rounded, kept to 1..127,
         so at the default 100 it plays as written. SEQ_ERR_VALUE
         outside 1..127.
   len   how long the note sounds, beats, at least one tick
         (SEQ_ERR_DURATION otherwise). The note-off falls that many
         ticks (rounded) after the note's start, but never past the
         next note's start: a longer length is shortened to the step
         (dur), so a track still sounds one note at a time. -1: the
         track's gate (seq_set_gate()) times the step.
   chan  1..16 for this note alone (SEQ_ERR_CHANNEL otherwise); the
         track's channel, and seq_get_tracks()'s report of it, don't
         change. -1: the version's channel. */
int seq_submit_track_ex(int track, int channel,
                        const int32_t *pitch, const double *dur,
                        const int32_t *vel, const double *len,
                        const int32_t *chan, int32_t n,
                        int64_t *swap_tick);

/* The list calls below take n tracks and apply them all under one
   lock, so a list takes effect at a single instant. values has n
   entries (the glue extends a single APL value to all n). prev (may be
   NULL) receives each track's previous setting. Any invalid entry
   fails the whole call before anything changes. */

/* Removes tracks at their next loop boundaries, or at once if not
   playing: a deliberate exception to "a stop changes nothing", since
   a stopped track has nothing sounding to cut off. A finished
   play-once track has no boundary left, so it goes at once either way.
   prev[i] is 1 if the track held something (a finished track
   included), else 0. */
int seq_clear(const int *tracks, int n, int *prev);

/* Mute on (1) or off (0). Immediate; muting sends note-off for the
   track's sounding notes. */
int seq_set_mute(const int *tracks, const int *values, int n, int *prev);

/* Loop (1, default) or play once and fall silent (0). Applies from the
   track's next boundary. A track that has played once keeps its
   version, finished (see struct seq_track_info); setting loop on
   doesn't start it again, but applies when a restart does. */
int seq_set_loop(const int *tracks, const int *values, int n, int *prev);

/* Note-on velocity, 1..127. Applies to notes started from now on. */
int seq_set_velocity(const int *tracks, const int *values, int n, int *prev);

/* Phase 7c: note events on (1) or off (0, the default). A reporting
   track remembers its latest note not yet reported and how many notes
   it started since the last one reported; seq_request_event() hands
   them over. Independent of mute: a muted track still reports (a sync
   track), and a rest never does. Turning a track on starts it with
   nothing outstanding; turning it off discards what it had; setting
   the value it already has changes nothing. Kept through new versions
   and seq_clear(), like the other settings; a restart (SEQ_RESTART)
   discards what every track has outstanding. */
int seq_set_notes(const int *tracks, const int *values, int n, int *prev);

/* V100 (agreed V99, Oct 18, 2026): a track's program and bank, sticky
   settings like its channel, kept through new versions and seq_clear().
   Programs 0..127, as sent; banks 0..16383, sent as controller 0 (MSB,
   bank / 128) and controller 32 (LSB, bank % 128) just before the
   program change; SEQ_NONE (-1, the default) for either sends nothing
   (a bank with no program sends nothing either: a bank select takes
   effect only at the next program change). SEQ_ERR_VALUE otherwise.
   Sent on the track's channel (the version's; a note's own channel,
   row 5, gets nothing).

   When. Never at once: at the start of a pass of the track (where its
   first note, or rest, falls; after the boundary step of the same-tick
   order and before any note-on), and only if the track has a program
   and either it differs, with the bank and the version's channel, from
   what the track last sent, or the pass is
   - a fresh start: a version starting on a track with nothing playing
     (never used, cleared, finished), or
   - a restart (seq_play(SEQ_RESTART)): every track at beat 0.
   So setting a program and sending a version take effect together, in
   either order; a change while a track plays with no new version takes
   effect at the end of its current pass; a new version with the same
   program and channel sends nothing (some synths cut sounding notes at
   a program change). Reopening after a lost port sends each playing
   track's program at once, after the note releases (the synth may have
   been reset). A muted track's program is sent like any other's. Two
   tracks on one channel with different programs: the last sent wins. */
int seq_set_program(const int *tracks, const int *values, int n, int *prev);
int seq_set_bank(const int *tracks, const int *values, int n, int *prev);

/* Phase 8: the gate, the fraction of each note's step it sounds when it
   has no length of its own: greater than 0, at most 1 (SEQ_ERR_VALUE
   otherwise); default 1, the whole step. The note-off falls at the
   rounded fraction of the step, at least one tick after the note-on.
   Applies to notes started from now on (a sounding note keeps its
   note-off). Kept through new versions and seq_clear(), like the
   other settings. prev (may be NULL) receives the previous values. */
int seq_set_gate(const int *tracks, const double *values, int n,
                 double *prev);

/* Phase 9 (Oct 16, 2026): a track's notes as they would sound, for
   writing a MIDI file. One pass of the track's version: the playing one,
   or, if nothing is playing on it yet, the one waiting to start (a
   version waiting behind a playing one isn't it). Each note as play
   would send it: its note-on velocity (its own scaled by the track's
   velocity, or the track's), its channel (its own, or the version's),
   and the tick of its note-off within the pass (its own length, or the
   gate times its step, at least one tick, never past the next note's
   start). Rests are left out. Mute is ignored: a muted track's notes
   are returned like any other's.

   *notes receives a malloc'd array (free it; NULL when there are none),
   *n how many, *length the pass's length in ticks (0 if the track has
   no version). Works in STOPPED, PLAYING and PORT_LOST.
   V100: chan, program and bank (each may be NULL) receive the version's
   channel (1..16; SEQ_NONE with no version) and the program and bank
   for it (SEQ_NONE for none): for a version that has played (the
   playing one, a finished one), those in effect when its current pass
   began, which is what was heard with it; for one waiting to start,
   the settings, which will apply when it does. All read under one
   lock with the notes.
   SEQ_ERR_TRACK, SEQ_ERR_NOT_OPEN, SEQ_ERR_NOMEM. */
struct seq_note_out {
	int64_t start;		/* ticks from the start of the pass */
	int64_t off;		/* its note-off, the same way */
	int     pitch;		/* 0..127 */
	int     vel;		/* 1..127 */
	int     chan;		/* 1..16 */
};
int seq_get_track_notes(int track, struct seq_note_out **notes, int32_t *n,
                        int64_t *length, int *chan, int *program,
                        int *bank);

/* ── Transport and tempo ─────────────────────────────────────────── */

enum seq_play_mode {
	SEQ_STOP     = 0,	/* pause: note-off for sounding notes; tracks,
				   settings, position and pending versions
				   all stay */
	SEQ_CONTINUE = 1,	/* from where it stopped */
	SEQ_RESTART  = 2	/* from beat 0, a boundary for every track:
				   every start point is set to 0, pending
				   versions take over at once and finished
				   play-once tracks play again */
};

/* prev (may be NULL) receives 1 if it was playing, else 0. The clock
   keeps running until stopped, even if every track is silent or has
   finished playing once. SEQ_STOP works in PORT_LOST (nothing to do);
   starting there returns SEQ_ERR_IO. */
int seq_play(int mode, int *prev);

/* Applies at once; positions are in ticks, so nothing jumps. Works in
   PORT_LOST. */
int seq_set_tempo(double bpm, double *prev);
int seq_get_tempo(double *bpm);

/* ── For MIDI files (V101, Oct 19, 2026) ─────────────────────────────
   Settings playback never reads; kept here with the sequence so that
   the caller writing a file (the glue's ⎕MIDIWRITE) finds them where
   it finds the tempo and the tracks. Like the other settings, reset by
   seq_open() from CLOSED and kept through a reopen after PORT_LOST;
   work in STOPPED, PLAYING and PORT_LOST.

   The meter (time signature): num over den, num 1..SEQ_METER_NUM_MAX
   and den a power of 2 from 1 to SEQ_METER_DEN_MAX, or both SEQ_NONE
   for none (the default). SEQ_ERR_VALUE for anything else (one of the
   two SEQ_NONE alone included). prev_num, prev_den (may be NULL)
   receive the previous setting. */
int seq_set_meter(int num, int den, int *prev_num, int *prev_den);
int seq_get_meter(int *num, int *den);

/* A track's name: up to SEQ_NAME_MAX bytes, each 32..126 (printable
   ASCII), "" for none (the default). A sticky setting like the
   track's channel, kept through new versions and seq_clear().
   SEQ_ERR_TRACK for a track outside 1..SEQ_MAX_TRACKS, SEQ_ERR_VALUE
   for a byte outside 32..126 or a name too long (nothing changed).
   prev (may be NULL) and name receive a copy, nul-terminated, and
   need SEQ_NAME_MAX + 1 bytes. */
int seq_set_name(int track, const char *name, char *prev);
int seq_get_name(int track, char *name);

/* ── Beat wait ───────────────────────────────────────────────────────
   Arms a one-shot wait for the next main-clock beat and returns at
   once. At that beat the callback gets SEQ_EV_BEAT with this request's
   id; if playback stops first (stop, close, port lost) it gets
   SEQ_EV_STOPPED. The caller does its own blocking (the glue waits on
   its own condition variable, checking ABORTSIG every ~50 ms). Fails
   with SEQ_ERR_NOT_PLAYING unless PLAYING, so the caller returns empty
   instead of hanging.
   Arming a new wait replaces any earlier one; only one is ever armed. */
int seq_request_beat(uint32_t *request_id);

/* Disarms the wait if request_id is still the armed one (interrupt or
   Escape). A reply already on its way is the caller's to discard by
   id. Works for either kind of wait.
   Phase 7c: if the wait's event was SEQ_EV_NOTES and has already been
   handed to the callback, cancelling it says the caller didn't take
   it (its wait ended some other way first), and the notes are put
   back as outstanding, so the next event wait still gets them: added
   to the count of a track that has started newer notes since (which
   keep their pitch and index), restored as they were otherwise. A
   track whose note events were switched since, a restart, or a newer
   wait armed forgets them. So a caller must cancel only a wait whose
   event it did not take, and must refuse any event that arrives for
   it once it has decided to cancel. A beat is never put back. */
int seq_cancel_wait(uint32_t request_id);

/* ── Event wait (Phase 7c) ───────────────────────────────────────────
   The wait the interpreter uses since 7c. seq_request_beat() stays as
   it was (a beat, whatever seq_set_beats() says) for the test driver.

   Beat events on (1) or off (0, the default after seq_open() from
   CLOSED; kept through a reopen after PORT_LOST, like the tempo). Only
   seq_request_event() looks at it. Works in STOPPED, PLAYING and
   PORT_LOST; SEQ_ERR_VALUE for anything but 0 or 1. */
int seq_set_beats(int on, int *prev);
int seq_get_beats(int *on);

/* Arms a one-shot wait for the next event: notes from the reporting
   tracks, or, with beats on, the next main-clock beat. Returns at once.
   - Some reporting track has a note not yet reported (in any open
     state, stopped or port lost included): the callback gets
     SEQ_EV_NOTES with all of them during this call (the library's lock
     is held, as for every callback), and nothing stays armed.
   - Otherwise, PLAYING, and beats are on or some reporting track has a
     version (playing or waiting): armed. At the first tick where a
     reporting track starts a note, the callback gets SEQ_EV_NOTES with
     every reporting track that started one on that tick; at the beat
     asked for, SEQ_EV_BEAT. Notes come before the beat at a tick (the
     same-tick order), so a beat on a tick with notes is never
     reported. A stop, close or lost port gives SEQ_EV_STOPPED.
   - Otherwise SEQ_ERR_NOT_PLAYING (not playing) or SEQ_ERR_NO_EVENT
     (playing, but nothing can produce an event): the caller skips the
     sequencer instead of hanging. A reporting track whose notes are all
     rests can still leave a wait armed with nothing to come; on the
     real clock the caller's own interrupt ends it.
   *request_id is set whenever the result is SEQ_OK, including when the
   event was delivered during the call. Arming replaces any earlier
   wait (beat or event); only one is ever armed. */
int seq_request_event(uint32_t *request_id);

/* Phase 7c (agreed Oct 12, 2026, for watching a loop while tuning
   it): how late the last SEQ_EV_NOTES was handed to the callback, in
   milliseconds: from the start of the newest note in the event to the
   hand-over. Measured in ticks and converted at the current tempo, so
   it is defined while stopped (outstanding notes are handed over
   then too) and exact on the virtual clock; on the real clock while
   playing it is real elapsed time (across a tempo change, the part
   before the change is counted at the new tempo). A wait that was
   already armed gets its notes as they start, so about 0 (on the real
   clock, the playback thread's own lateness); notes handed over
   during seq_request_event() (catch-up) show how stale they were.
   Kept until the next note event, so it can be read any time after
   the wait; an event the caller didn't take (seq_cancel_wait()) still
   counts. 0 after seq_open() from CLOSED, until the first. Beat
   events aren't measured (always handed over as they happen). */
int seq_get_note_late(double *ms);

/* ── Test mode (SEQ_BACKEND_VIRTUAL only) ────────────────────────────
   Always compiled; the APL functions that reach these (⎕SEQRUNTO,
   ⎕SEQLOG) exist only in test builds. */

/* Lets the playback thread run, with no real sleeping, through every
   event *before* main-clock tick `tick`, then hold with the clock at
   `tick` (Phase 2: before, not "up to and including", so that a
   version sent while held at a loop boundary takes over at that
   boundary, as in the notes' example). Returns only once the thread is
   holding there, so whatever the caller does next happens at a known
   position. A tick at or before the current position does nothing.
   Unless PLAYING, the clock can't move, so it returns
   SEQ_ERR_NOT_PLAYING at once rather than waiting forever (a test that
   forgot to start playback fails here, not later). If the port is
   lost on the way, returns SEQ_ERR_IO. SEQ_BACKEND_VIRTUAL only. */
int seq_virtual_run_to(int64_t tick);

/* Phase 7c: runs the virtual clock until the armed wait (either kind)
   gets its event, then holds one tick past that event's tick, as a
   seq_virtual_run_to() just past a beat would; the event arrives
   during the call. SEQ_OK once the wait has had its event (a STOPPED
   included), and at once if no wait is armed. If nothing comes within
   max_ticks of the current position, the clock holds there with the
   wait still armed and the result is SEQ_ERR_NO_EVENT (a reporting
   track that will never report again ends in an error, not a hang).
   SEQ_ERR_NOT_PLAYING unless PLAYING, SEQ_ERR_IO if the port is lost
   with the wait still armed (can't happen: a lost port ends a wait).
   SEQ_BACKEND_VIRTUAL only. */
int seq_virtual_run_to_event(int64_t max_ticks);

/* Copies the log written since open (or since the last clear) into
   buf as text, one line per MIDI message, decoded:

       8.000 t20160 4000.000ms ON  ch1 60 v100
       8.500 t21420 4250.000ms OFF ch1 60
       9.000 t22680 4500.000ms CC  ch1 123 0
       9.000 t22680 4500.000ms PC  ch1 40       (V100)

   Main-clock beat with 3 decimals, the exact tick, then the clock time
   since open in milliseconds (Phase 2: tick and time added; simulated
   time in SEQ_BACKEND_VIRTUAL, so tempo changes show), then the
   message. Pitch as a number, since programs disagree on whether 60 is
   C3 or C4. *len receives the full length, which may exceed bufsize
   (the copy is then cut short and unterminated; call again with a
   larger buffer). clear != 0 empties the log after copying. Works with
   both log backends. */
int seq_virtual_log(char *buf, size_t bufsize, size_t *len, int clear);

/* Phase 2: simulates an unplug. While on, every write to the log
   fails, so the sequencer goes through the same path as a failed
   device write (PORT_LOST). Turn it off before seq_open() to reopen.
   Either log backend.
   Phase 3: matches the hardware. After a USB disconnect every write
   fails at once with ENODEV (Linux 5.15 ALSA source; seen on the
   Jetson, kernel 5.15.148-tegra, CME U2MIDI Pro, Oct 4, 2026: the
   first note after the unplug failed, and the close took 0.1 ms). */
int seq_virtual_fail_writes(int on);

#ifdef __cplusplus
}
#endif

#endif /* SEQ_H */
