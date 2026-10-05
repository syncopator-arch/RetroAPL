/*
	seq.h -- MIDI sequencer library, public interface.
	Agreed Oct 4, 2026 (Phase 1); Phase 2 additions marked "Phase 2".

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
	clears, play-once endings), then note-ons, then the beat event. So a
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
	SEQ_ERR_PORT_NAME	/* port name not understood (Linux:
				   "hw:C", "hw:C,D" or "hw:C,D,S";
				   Windows: a device number) */
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
	SEQ_EV_STOPPED		/* playback stopped while a wait was armed;
				   the wait is over */
};

struct seq_event {
	int      kind;		/* enum seq_event_kind */
	uint32_t request_id;	/* from seq_request_beat() */
	int64_t  beat;		/* SEQ_EV_BEAT: main-clock beat number */
	int      reason;	/* SEQ_EV_STOPPED: SEQ_OK (stopped or
				   closed) or SEQ_ERR_IO (port lost) */
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
				   text ("0") on Windows.
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
   (Phase 3). Opening never waits for a busy port. */
int seq_open(const struct seq_config *cfg);

/* From any state: stops playback, sends note-off for every sounding
   note, ends the playback thread, closes the port if open, discards
   tracks and settings, and goes to CLOSED. Any armed beat wait gets
   SEQ_EV_STOPPED. Safe to call more than once and from any thread (the
   GUI's stopEngine() calls it too); from CLOSED it returns SEQ_OK with
   *nclosed = 0.
   closed (room for SEQ_MAX_TRACKS, may be NULL) receives the numbers
   of the tracks that held something, *nclosed how many. */
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

/* One entry per track in use (a playing version or one waiting to take
   over), for ⎕SEQSTATE's track rows. */
struct seq_track_info {
	int     track;		/* 1..SEQ_MAX_TRACKS */
	int32_t notes;		/* notes in the playing version, 0 if none
				   yet (only a pending one, or a version
				   whose start beat hasn't been reached) */
	int     channel;	/* 1..16: the playing version's channel; with
				   only a waiting version, the channel it
				   will start on (Phase 2, agreed Oct 4) */
	int     mute;		/* 0/1 */
	int     loop;		/* 0/1 */
	int     velocity;	/* 1..127 */
	int64_t length;		/* loop length, ticks (playing version;
				   0 if none) */
	int     pending;	/* 1 if a version is waiting to take over
				   (including one whose start beat hasn't
				   been reached yet) */
	int64_t position;	/* position within the playing version's
				   current pass, ticks, 0..length-1 (0 if
				   no playing version) */
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
     is in place at once. Resending a finished play-once track is how
     to play it again.
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

/* The list calls below take n tracks and apply them all under one
   lock, so a list takes effect at a single instant. values has n
   entries (the glue extends a single APL value to all n). prev (may be
   NULL) receives each track's previous setting. Any invalid entry
   fails the whole call before anything changes. */

/* Removes tracks at their next loop boundaries, or at once if not
   playing: a deliberate exception to "a stop changes nothing", since
   a stopped track has nothing sounding to cut off.
   prev[i] is 1 if the track held something, else 0. */
int seq_clear(const int *tracks, int n, int *prev);

/* Mute on (1) or off (0). Immediate; muting sends note-off for the
   track's sounding notes. */
int seq_set_mute(const int *tracks, const int *values, int n, int *prev);

/* Loop (1, default) or play once and fall silent (0). Applies from the
   track's next boundary. */
int seq_set_loop(const int *tracks, const int *values, int n, int *prev);

/* Note-on velocity, 1..127. Applies to notes started from now on. */
int seq_set_velocity(const int *tracks, const int *values, int n, int *prev);

/* ── Transport and tempo ─────────────────────────────────────────── */

enum seq_play_mode {
	SEQ_STOP     = 0,	/* pause: note-off for sounding notes; tracks,
				   settings, position and pending versions
				   all stay */
	SEQ_CONTINUE = 1,	/* from where it stopped */
	SEQ_RESTART  = 2	/* from beat 0, a boundary for every track:
				   every start point is set to 0 and
				   pending versions take over at once */
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
   id. */
int seq_cancel_wait(uint32_t request_id);

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

/* Copies the log written since open (or since the last clear) into
   buf as text, one line per MIDI message, decoded:

       8.000 t20160 4000.000ms ON  ch1 60 v100
       8.500 t21420 4250.000ms OFF ch1 60
       9.000 t22680 4500.000ms CC  ch1 123 0

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
