/*
	evhub.h -- the event hub: the one place the interpreter's engine
	thread waits for events from outside it (Phase 7 of
	MIDI_SEQUENCER_NOTES.md).

	Phase 7a: window events. Phase 7b: the sequencer too, and one wait
	for ⎕WCLICK and ⎕EVWAIT (evwait.c calls evhub_wait() for both).
	Phase 7c: the sequencer's note events (evhub_seq_notes()).
	The Qt front end reports every click, window close and Escape here
	without knowing whether anyone is waiting; seqfuncs.c's library
	callback reports beats and stops the same way; the hub decides.

	The hub knows nothing about the sequencer library (seqfuncs.c stays
	the only interpreter file that includes seq.h). A wait that includes
	the sequencer is handed three hooks (struct evhub_seq_ops), which the
	hub calls at fixed points, with its lock released.

	Plain C, no interpreter headers, so the Qt files can include it.

	Threads and locks:
	  - evhub_wait() runs on the engine thread only, one wait at a time.
	  - The reporters (evhub_click(), evhub_closed(), evhub_escape(),
	    evhub_seq_event(), evhub_seq_notes(), evhub_shutdown()) may be
	    called from any
	    thread, any time. They take only the hub's own lock, briefly,
	    and call nothing else.
	  - The hub never calls out while holding its lock: the window-
	    state query (a blocking round trip to the GUI thread), the test
	    close callback and the sequencer hooks are called with it
	    released. The GUI thread takes the hub's lock to report a click,
	    so calling the GUI under it could deadlock; the sequencer's
	    callback takes it inside the library's lock, so calling the
	    library under it could too. Order: the library's lock, then the
	    hub's, never the reverse. (INVARIANTS.md, "Event Hub".)
*/

#ifndef EVHUB_H
#define EVHUB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The front end's window-state query, with ⎕WVIS's codes: one state
   per handle, ¯1 not open, 0 hidden, 1 shown, 2 minimized. Returns
   the number written. Called on the engine thread at the start of a
   wait, without the hub's lock. The Qt front end registers gw_vis();
   apl_cli its fake window table. With none registered, no window is
   open. */
typedef long (*evhub_vis_fn)(const int32_t *handles, long count,
			     int32_t *states);

/* Test mode (apl_cli): called when a close posted by ⎕EVPOST is
   delivered, so the fake window table can drop the window, as a real
   close would. */
typedef void (*evhub_close_fn)(long handle);

void evhub_set_vis(evhub_vis_fn fn);

/* apl_cli only, at startup: window events come from ⎕EVPOST, and a
   wait that runs out of posted events with nothing else to wait for
   returns EVHUB_NOTHING instead of blocking (there is no one to
   click). */
void evhub_set_test(evhub_close_fn on_close);

/* ── Reporters (any thread) ──────────────────────────────────────── */

/* A left click at x,y in window `handle`. Dropped unless a wait is
   waiting on that window: clicks never queue. */
void evhub_click(long handle, long x, long y);

/* Window `handle` is closing. Ends a wait on that window (empty). */
void evhub_closed(long handle);

/* Escape. Ends a wait in progress (empty). Returns 1 if a wait was in
   progress, 0 if none was. Since 7b nothing else happens either way
   (Escape with nothing waiting is ignored); the result is kept for
   callers that want to know. */
int evhub_escape(void);

/* A sequencer event, from seqfuncs.c's library callback (which runs
   with the library's lock held: this takes the hub's lock inside it).
   Counts only while a wait that armed the sequencer is in progress;
   otherwise dropped. `id` is the library's request id, compared with
   the one the arm hook returned (a check, not the mechanism). */
#define EVHUB_SEQ_BEAT		1	/* value: the main-clock beat */
#define EVHUB_SEQ_STOPPED	2	/* value: 1 port lost, 0 stopped
					   or closed */
#define EVHUB_SEQ_NOTES		3	/* Phase 7c: evhub_seq_notes() */
void evhub_seq_event(int kind, uint32_t id, int64_t value);

/* Phase 7c: note events, from the same callback and under the same
   rules as evhub_seq_event(). data holds n groups of four, tracks
   ascending: track, pitch, index (the note's column, from 0; the wait
   adds ⎕IO), count. n is 1..EVHUB_MAXNOTES; the data is copied. */
#define EVHUB_MAXNOTES		16	/* the sequencer's track count */
void evhub_seq_notes(uint32_t id, int n, const int32_t *data);

/* The application is shutting down: ends any wait (empty), and every
   later wait returns empty at once. stopEngine() calls it, so a
   running loop that waits can't hold up the close. */
void evhub_shutdown(void);

/* ── The wait (engine thread) ────────────────────────────────────── */

/* The sequencer's side of a wait, supplied by seqfuncs.c. Each is
   called on the engine thread without the hub's lock.
     arm     arm the library's event wait; 0 and *id on success, else
             nonzero: the sequencer can't produce an event now, so it
             is skipped for this wait. Since 7c an event may arrive
             during the call (notes outstanding are handed over at
             once): the hub is armed first, so it counts.
     cancel  disarm it, when the wait ended any other way; once it
             returns, no event for it can arrive
     pump    test mode only (the virtual clock; NULL on the real
             clock): run the clock until the wait's event, so it
             arrives during the call. Called when the posted events run
             out. Since 7c it returns 0 if it ran to an event, nonzero
             if nothing came within its limit (the wait then returns
             EVHUB_NOTHING, a Domain Error, rather than hanging). */
struct evhub_seq_ops {
	int  (*arm)(uint32_t *id);
	void (*cancel)(uint32_t id);
	int  (*pump)(void);
};

/* What ended a wait */
#define EVHUB_CLICK	1	/* ev->win: handle, x, y */
#define EVHUB_SEQ	2	/* ev->seq_kind, ev->seq_value */
#define EVHUB_EMPTY	0	/* nothing waitable, a window waited on
				   closed, Escape, Ctrl-C, or shutdown */
#define EVHUB_NOTHING	(-1)	/* test mode: posted events ran out,
				   and the virtual clock found no event
				   within its limit */
#define EVHUB_NOMEM	(-2)
#define EVHUB_INTERNAL	(-3)	/* the sequencer's hooks broke their
				   promise (an event for another request,
				   or the pump delivered nothing): a bug,
				   never expected */

struct evhub_event {
	int32_t	win[3];		/* EVHUB_CLICK */
	int	seq_kind;	/* EVHUB_SEQ: EVHUB_SEQ_BEAT, _STOPPED or
				   _NOTES */
	int64_t	seq_value;
	int	nnotes;		/* EVHUB_SEQ_NOTES (Phase 7c): groups of
				   four in notes[], as evhub_seq_notes() */
	int32_t	notes[4 * EVHUB_MAXNOTES];
};

/* Waits for a left click in any of the `count` windows given, or, with
   `seq` non-NULL, an event from the sequencer as well. Only open
   windows that aren't hidden are waited on (minimized counts), and the
   sequencer only if its arm hook succeeds; if nothing qualifies,
   returns EVHUB_EMPTY at once. A window waited on closing ends the
   wait with EVHUB_EMPTY, even if other sources remain (as ⎕WCLICK
   always has); a sequencer stop is an event (EVHUB_SEQ). Ctrl-C
   (ABORTSIG) ends the wait with EVHUB_EMPTY and leaves ABORTSIG set,
   so the line runs on and is interrupted after it (7b: both waits). */
int evhub_wait(const int32_t *handles, long count,
	       const struct evhub_seq_ops *seq, struct evhub_event *ev);

/* ── Test injection (⎕EVPOST) ────────────────────────────────────── */

#define EVHUB_POST_CLICK	1	/* a: handle, x, y */
#define EVHUB_POST_CLOSE	2	/* a: handle */
#define EVHUB_POST_ESCAPE	3
#define EVHUB_POST_CTRLC	4	/* sets ABORTSIG when delivered */

/* Queues an event for the waits to come: each wait takes queued
   events in order until it ends, and leaves the rest. Returns the
   number queued, or -1 if not in test mode, -2 if the queue is
   full. The kind and the number of values are the caller's to
   check. */
int evhub_post(int kind, const int32_t *a);

#ifdef __cplusplus
}
#endif

#endif
