/*
	seq_internal.h -- inside the sequencer library only (and the test
	driver, for seq_clock_is_monotonic()). Not part of the public
	interface; seq.h is.

	Output backends sit behind three functions. Every call is made
	with the sequencer's lock held (seq_panic() excepted: it may not
	get the lock), so backends need no locking of their own.
*/

#ifndef SEQ_INTERNAL_H
#define SEQ_INTERNAL_H

#include <stdint.h>
#include <stddef.h>

struct seq_output {
	/* SEQ_OK, or SEQ_ERR_PORT if the port can't be opened. */
	int  (*open)(const char *port);
	/* One complete MIDI message (never running status). tick is the
	   main-clock position, ns the clock time since open; only the log
	   uses them. 0 if sent, -1 if the device stopped taking data. */
	int  (*send)(const unsigned char *msg, int len, int64_t tick,
	             int64_t ns);
	void (*close)(void);
};

extern const struct seq_output seq_out_log;	/* seq_out_log.c */
extern const struct seq_output seq_out_device;	/* seq_out_device.c */

/* Log backend extras (seq_out_log.c). */
void seq_log_reset(void);			/* empty it, writes on */
void seq_log_fail(int on);			/* simulated unplug */
int  seq_log_copy(char *buf, size_t bufsize, size_t *len, int clear);

/* Device backend diagnostics (seq_out_device.c, Phase 3), for seqdemo
   and seqtest: why the last open or write failed, and how long writes
   and closes took. Not for the interpreter (whether APL ever sees any
   of this is a Phase 4 question). Safe from any thread. */
struct seq_dev_diag {
	const char *step;	/* where the last failure happened ("midi
				   open", "write", "write timeout", ...),
				   NULL if none since open */
	int     err;		/* its errno on Linux, MMRESULT on Windows
				   (0 for a timeout) */
	char    errtext[96];	/* that error as text, "" if none */
	long    writes;		/* messages written since open */
	long    waits;		/* of those, how many had to wait for room
				   (buffer full) or were written in pieces */
	int64_t wait_max_ns;	/* longest such wait */
	int64_t close_ns;	/* how long the last close() took (the
				   kernel waits for the output to drain) */
	int     subdevice;	/* Linux: subdevice actually opened, -1 if
				   unknown; Windows: -1 */
};
void seq_dev_get_diag(struct seq_dev_diag *d);

/* Output ports: seq_dev_list() and struct seq_port_info moved to
   seq.h in Phase 8 (Oct 15, 2026), for the interpreter's ⎕SEQPORTS. */

/* Playback timing on the real clock (Phase 3/6, seq.c): how late each
   tick went out compared with when it was due. The virtual clock
   records nothing. reset != 0 starts counting again after copying.
   Reset by seq_open() from CLOSED. */
struct seq_timing {
	long    ticks;		/* ticks played (one tick may carry
				   several messages) */
	long    late_2ms;	/* of those, how many over 2 ms late */
	int64_t late_max_ns;	/* the latest */
	int64_t late_sum_ns;	/* for an average */
};
void seq_get_timing(struct seq_timing *t, int reset);

/* Longest a device write waits for room before the port counts as
   lost (Phase 3, agreed: option (b)). */
#define SEQ_DEV_WRITE_LIMIT_MS 1000

/* 1 if timed waits count on the monotonic clock, 0 if they fell back
   to the time-of-day clock with capped waits (seq.c, timed_wait()).
   For the test driver's -i report. */
int  seq_clock_is_monotonic(void);

#endif /* SEQ_INTERNAL_H */
