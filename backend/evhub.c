/*
	evhub.c -- the event hub (see evhub.h for the interface and the
	lock rules; MIDI_SEQUENCER_NOTES.md, Phase 7, for the design).

	Phase 7a moved ⎕WCLICK's wait here from the Qt front end
	(GraphManager::click(), which ran a nested event loop on the GUI
	thread while the engine thread sat in a blocking call). Phase 7b
	adds the sequencer, so ⎕WCLICK and ⎕EVWAIT are one wait (evwait.c
	calls evhub_wait() for both). Phase 7c adds the sequencer's note
	events, carried like its beats and stops. The front end and the
	sequencer's callback only report; this file decides:
	  - a click counts only while a wait is waiting on that window;
	    otherwise it is dropped (clicks never queue; agreed V89);
	  - a sequencer event counts only while a wait has armed the
	    sequencer (the library arms one event per wait, so there is
	    nothing to queue either; notes the program missed are the
	    library's to hand over at the next arm, 7c);
	  - the first event wins;
	  - a window waited on closing ends the wait, empty, even if other
	    sources are still waited on (as ⎕WCLICK always has; kept in 7b,
	    agreed Oct 8, 2026), while a sequencer stop is an event;
	  - Escape during a wait ends it, empty;
	  - Ctrl-C (ABORTSIG) ends it, empty, ABORTSIG left set, so the
	    line runs on and the interrupt comes after it (⎕WCLICK's rule,
	    for both waits since 7b, agreed Oct 8, 2026). A Ctrl-C already
	    pending when the wait starts returns at once.

	One wait:
	  1. Arm with the whole list, so a close from now on is seen, and
	     accept sequencer events if the sequencer is in the list.
	  2. Arm the sequencer (seq->arm(), no lock): it fails when the
	     sequencer can't produce anything, and is then skipped. Armed
	     here, after step 1, so an event that comes at once isn't
	     dropped (since 7c one can come during the arm itself: notes
	     outstanding are handed over then).
	  3. Ask the front end which windows are open and not hidden,
	     without the lock (a blocking round trip to the GUI thread).
	  4. Keep only those; no windows and no sequencer: empty at once.
	     Asking before arming would leave a gap where a close is
	     dropped and the wait never ends.
	  5. Block on hub_cond, looking at ABORTSIG every EVHUB_POLL_MS,
	     until a reporter ends the wait. In test mode (apl_cli) feed
	     posted events instead, synchronously; when they run out, run
	     the virtual clock to the wait's event if the sequencer is
	     armed there (seq->pump(); EVHUB_NOTHING if none comes within
	     its limit, 7c), block as above if it's armed on the real
	     clock (apl_cli with a real port), or else return EVHUB_NOTHING
	     (the caller's Domain Error), never a hang.
	  6. If the sequencer was armed and didn't end the wait, cancel it
	     (no lock), and only then disarm: once the cancel returns, no
	     event for it can arrive, so nothing late reaches the next wait.
	     7c: from the moment it decides to cancel, the hub refuses
	     sequencer events; notes refused since the wait ended are put
	     back by the library on the cancel, so a note the program
	     didn't get still counts at its next wait (catch-up).
*/

#define EVHUB_C 1

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include "parse.h"	/* ABORTSIG, through globals.h */
#include "evhub.h"

#define EVHUB_POLL_MS	50	/* how often a wait looks at ABORTSIG */
#define EVHUB_QMAX	64	/* posted test events waiting */

/* done: how the wait ended */
#define D_WAITING	0
#define D_CLICK		1
#define D_EMPTY		2
#define D_SEQ		3

static pthread_once_t	hub_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t	hub_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t	hub_cond;
static clockid_t	hub_clk = CLOCK_REALTIME;

/* Everything below is guarded by hub_lock, except where noted. */
static int	armed;		/* a wait is in progress */
static int	done;		/* D_WAITING, or how it ended */
static int32_t	*want;		/* the windows the wait accepts */
static long	nwant;
static int	seq_on;		/* the wait accepts sequencer events */
static int32_t	got[3];		/* the click, when done == D_CLICK */
static int	seq_kind;	/* the sequencer event, when D_SEQ */
static int64_t	seq_value;
static uint32_t	seq_id;
static int	seq_nnotes;	/* EVHUB_SEQ_NOTES (7c) */
static int32_t	seq_notes[4 * EVHUB_MAXNOTES];
static long	ender;		/* the window whose click or close ended
				   the wait; -1 for anything else */
static int	down;		/* evhub_shutdown() was called: sticky */

static evhub_vis_fn	vis_fn;		/* set once at startup */
static int		test_mode;	/* set once at startup */
static evhub_close_fn	test_close;

static struct { int kind; int32_t a[3]; } queue[EVHUB_QMAX];
static int		qhead, qcount;

/* The monotonic clock where the condition variable accepts it, as the
   sequencer library does: otherwise (winpthreads) the time of day,
   where a clock change can only delay noticing Ctrl-C. */
static void
hub_init(void)
{
	pthread_condattr_t a;

	pthread_condattr_init(&a);
#if defined(CLOCK_MONOTONIC)
	if (pthread_condattr_setclock(&a, CLOCK_MONOTONIC) == 0)
		hub_clk = CLOCK_MONOTONIC;
#endif
	pthread_cond_init(&hub_cond, &a);
	pthread_condattr_destroy(&a);
}

/* Called with hub_lock held. */
static int
wanted(long handle)
{
	long i;

	for (i = 0; i < nwant; i++)
		if (want[i] == handle) return 1;
	return 0;
}

/* Called with hub_lock held. */
static void
finish(int how)
{
	done = how;
	pthread_cond_broadcast(&hub_cond);
}

void
evhub_set_vis(evhub_vis_fn fn)
{
	pthread_once(&hub_once, hub_init);
	pthread_mutex_lock(&hub_lock);
	vis_fn = fn;
	pthread_mutex_unlock(&hub_lock);
}

void
evhub_set_test(evhub_close_fn on_close)
{
	pthread_once(&hub_once, hub_init);
	pthread_mutex_lock(&hub_lock);
	test_mode = 1;
	test_close = on_close;
	pthread_mutex_unlock(&hub_lock);
}

/* ── Reporters ─────────────────────────────────────────────────────── */

void
evhub_click(long handle, long x, long y)
{
	pthread_once(&hub_once, hub_init);
	pthread_mutex_lock(&hub_lock);
	if (armed && !done && wanted(handle))
	{
		got[0] = (int32_t)handle;
		got[1] = (int32_t)x;
		got[2] = (int32_t)y;
		ender = handle;
		finish(D_CLICK);
	}
	pthread_mutex_unlock(&hub_lock);
}

void
evhub_closed(long handle)
{
	pthread_once(&hub_once, hub_init);
	pthread_mutex_lock(&hub_lock);
	if (armed && !done && wanted(handle))
	{
		ender = handle;
		finish(D_EMPTY);
	}
	pthread_mutex_unlock(&hub_lock);
}

int
evhub_escape(void)
{
	int r;

	pthread_once(&hub_once, hub_init);
	pthread_mutex_lock(&hub_lock);
	/* armed, not "armed && !done": a wait that has just ended but not
	   yet returned still counts as in progress (7a kept this so the
	   Escape wasn't turned into an interrupt a moment later; since 7b
	   Escape does nothing else anyway). */
	r = armed;
	if (armed && !done)
	{
		ender = -1;
		finish(D_EMPTY);
	}
	pthread_mutex_unlock(&hub_lock);
	return r;
}

void
evhub_seq_event(int kind, uint32_t id, int64_t value)
{
	pthread_once(&hub_once, hub_init);
	pthread_mutex_lock(&hub_lock);
	if (armed && !done && seq_on)
	{
		seq_kind = kind;
		seq_value = value;
		seq_id = id;
		ender = -1;
		finish(D_SEQ);
	}
	pthread_mutex_unlock(&hub_lock);
}

void
evhub_seq_notes(uint32_t id, int n, const int32_t *data)
{
	pthread_once(&hub_once, hub_init);
	if (n < 0) n = 0;
	if (n > EVHUB_MAXNOTES) n = EVHUB_MAXNOTES;
	pthread_mutex_lock(&hub_lock);
	if (armed && !done && seq_on)
	{
		seq_kind = EVHUB_SEQ_NOTES;
		seq_value = n;
		seq_nnotes = n;
		memcpy(seq_notes, data, (size_t)n * 4 * sizeof(int32_t));
		seq_id = id;
		ender = -1;
		finish(D_SEQ);
	}
	pthread_mutex_unlock(&hub_lock);
}

void
evhub_shutdown(void)
{
	pthread_once(&hub_once, hub_init);
	pthread_mutex_lock(&hub_lock);
	down = 1;
	pthread_cond_broadcast(&hub_cond);
	pthread_mutex_unlock(&hub_lock);
}

/* ── Test injection ────────────────────────────────────────────────── */

int
evhub_post(int kind, const int32_t *a)
{
	int i, r;

	pthread_once(&hub_once, hub_init);
	pthread_mutex_lock(&hub_lock);
	if (!test_mode) r = -1;
	else if (qcount == EVHUB_QMAX) r = -2;
	else
	{
		i = (qhead + qcount) % EVHUB_QMAX;
		queue[i].kind = kind;
		memset(queue[i].a, 0, sizeof queue[i].a);
		if (kind == EVHUB_POST_CLICK) memcpy(queue[i].a, a, 3 * sizeof(int32_t));
		else if (kind == EVHUB_POST_CLOSE) queue[i].a[0] = a[0];
		r = ++qcount;
	}
	pthread_mutex_unlock(&hub_lock);
	return r;
}

#define FEED_BLOCK	1	/* feed(): nothing posted left, but the
				   sequencer is armed on the real clock */

/* Test mode: deliver posted events, in order, until the wait ends.
   Each goes through the same reporter a real one would, so the rules
   above are what's tested. When they run out: the virtual clock's
   pump, once (the event arrives during it; EVHUB_NOTHING if it found
   none within its limit, 7c); FEED_BLOCK for the real clock; else
   EVHUB_NOTHING. Returns 0 when the wait has ended. */
static int
feed(const struct evhub_seq_ops *seq)
{
	int kind = 0, finished, seqwait, pumped = 0;
	int32_t a[3];

	for (;;)
	{
		pthread_mutex_lock(&hub_lock);
		finished = done || down;
		seqwait = seq_on;
		if (!finished && qcount == 0)
		{
			pthread_mutex_unlock(&hub_lock);
			if (!seqwait) return EVHUB_NOTHING;
			if (!seq->pump) return FEED_BLOCK;
			if (pumped) return EVHUB_INTERNAL;
			pumped = 1;
			if (seq->pump() != 0) return EVHUB_NOTHING;
			continue;
		}
		if (!finished)
		{
			kind = queue[qhead].kind;
			memcpy(a, queue[qhead].a, sizeof a);
			qhead = (qhead + 1) % EVHUB_QMAX;
			qcount--;
		}
		pthread_mutex_unlock(&hub_lock);
		if (finished) return 0;

		switch (kind)
		{
			case EVHUB_POST_CLICK:
				evhub_click(a[0], a[1], a[2]);
				break;
			case EVHUB_POST_CLOSE:
				if (test_close) test_close(a[0]);
				evhub_closed(a[0]);
				break;
			case EVHUB_POST_ESCAPE:
				evhub_escape();
				break;
			case EVHUB_POST_CTRLC:
				ABORTSIG = 1;
				return 0;
		}
	}
}

/* ── The wait ──────────────────────────────────────────────────────── */

int
evhub_wait(const int32_t *handles, long count,
	   const struct evhub_seq_ops *seq, struct evhub_event *ev)
{
	int32_t *list, *states;
	struct timespec ts;
	long i, n, k;
	uint32_t id = 0;
	int r, seq_armed = 0;

	pthread_once(&hub_once, hub_init);
	memset(ev, 0, sizeof *ev);
	if (count < 0) count = 0;
	if ((count == 0 && !seq) || ABORTSIG) return EVHUB_EMPTY;

	list = malloc(((size_t)count + 1) * sizeof(int32_t));
	states = malloc(((size_t)count + 1) * sizeof(int32_t));
	if (!list || !states)
	{
		free(list);
		free(states);
		return EVHUB_NOMEM;
	}
	if (count) memcpy(list, handles, (size_t)count * sizeof(int32_t));

	/* 1. arm with the whole list, and the sequencer if asked */
	pthread_mutex_lock(&hub_lock);
	if (down || armed)	/* armed: can't happen, one engine thread */
	{
		pthread_mutex_unlock(&hub_lock);
		free(list);
		free(states);
		return EVHUB_EMPTY;
	}
	want = list;
	nwant = count;
	seq_on = (seq != NULL);
	done = D_WAITING;
	armed = 1;
	pthread_mutex_unlock(&hub_lock);

	/* 2. the sequencer; can't: skip it. An event may arrive during the
	   arm (7c: notes outstanding), and counts: the hub is armed. */
	if (seq)
	{
		if (seq->arm(&id) == 0) seq_armed = 1;
		else
		{
			pthread_mutex_lock(&hub_lock);
			seq_on = 0;
			pthread_mutex_unlock(&hub_lock);
		}
	}

	/* 3. which windows are open and not hidden; no lock (GUI round
	   trip) */
	for (i = 0; i < count; i++) states[i] = -1;
	if (vis_fn && count)
	{
		k = vis_fn(handles, count, states);
		for (i = (k < 0 ? 0 : k); i < count; i++) states[i] = -1;
	}

	/* 4. keep those; nothing at all: empty at once. A click or close
	   that came in between steps 1 and 4 counts only if its window is
	   kept: one for a window that turned out not to be waited on is
	   undone, as if it had come before the wait (found by the V89
	   stress test). */
	pthread_mutex_lock(&hub_lock);
	for (i = n = 0; i < count; i++)
		if (states[i] == 1 || states[i] == 2)
			list[n++] = list[i];
	nwant = n;
	if (done && ender >= 0 && !wanted(ender)) done = D_WAITING;
	if (!done && n == 0 && !seq_on) finish(D_EMPTY);
	pthread_mutex_unlock(&hub_lock);
	free(states);

	/* 5. wait */
	r = 0;
	if (test_mode) r = feed(seq);
	if (!test_mode || r == FEED_BLOCK)
	{
		r = 0;
		pthread_mutex_lock(&hub_lock);
		while (!done && !down && !ABORTSIG)
		{
			clock_gettime(hub_clk, &ts);
			ts.tv_nsec += EVHUB_POLL_MS * 1000000L;
			if (ts.tv_nsec >= 1000000000L)
			{
				ts.tv_sec++;
				ts.tv_nsec -= 1000000000L;
			}
			pthread_cond_timedwait(&hub_cond, &hub_lock, &ts);
		}
		pthread_mutex_unlock(&hub_lock);
	}

	/* 6. the sequencer is still armed unless its event ended the
	   wait: cancel it before disarming, with no lock (the library
	   takes its own, and its callback ours) */
	if (seq_armed)
	{
		pthread_mutex_lock(&hub_lock);
		k = (done == D_SEQ);
		/* 7c: refuse anything later. The cancel then tells the library
		   the wait's event wasn't taken, and it puts notes refused
		   since the wait ended (an Escape, a click or a Ctrl-C came
		   first) back for the next wait; without this, an event taken
		   after the decision would come back twice. */
		if (!k) seq_on = 0;
		pthread_mutex_unlock(&hub_lock);
		if (!k) seq->cancel(id);
	}

	/* disarm. An event that won before Ctrl-C or shutdown still
	   counts, as a click that landed first always did. */
	pthread_mutex_lock(&hub_lock);
	if (r == 0)
	{
		if (done == D_CLICK)
		{
			memcpy(ev->win, got, sizeof got);
			r = EVHUB_CLICK;
		}
		else if (done == D_SEQ)
		{
			ev->seq_kind = seq_kind;
			ev->seq_value = seq_value;
			if (seq_kind == EVHUB_SEQ_NOTES)
			{
				ev->nnotes = seq_nnotes;
				memcpy(ev->notes, seq_notes,
				       (size_t)seq_nnotes * 4 * sizeof(int32_t));
			}
			r = (seq_id == id) ? EVHUB_SEQ : EVHUB_INTERNAL;
		}
		else r = EVHUB_EMPTY;
	}
	armed = 0;
	done = D_WAITING;
	want = NULL;
	nwant = 0;
	seq_on = 0;
	pthread_mutex_unlock(&hub_lock);
	free(list);
	return r;
}
