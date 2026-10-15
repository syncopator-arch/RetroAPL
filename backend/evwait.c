/*
	evwait.c -- the one event wait behind ⎕EVWAIT (k3 slot 12,
	seqfuncs.c) and ⎕WCLICK (k2 slot 11, grfuncs.c). Phase 7b of
	MIDI_SEQUENCER_NOTES.md: the two names are the same function, so
	each module's case is one call to ev_wait_apl() (the person, Oct 7,
	2026). A file of its own (agreed Oct 8, 2026) so the graphics code
	needn't know the sequencer and the sequencer code needn't know
	windows: it waits in the event hub (evhub.c), reaches the sequencer
	only through seqfuncs.c's hooks, and never includes seq.h.

	The argument is a list of sources: window numbers (0 and up; one
	that isn't open, or is hidden, is skipped) and ¯1, the sequencer
	(skipped unless playing). A scalar or vector of whole numbers:
	text is a Domain Error, a matrix a Rank Error, a fraction or any
	other negative number a Domain Error (room for a timer, ¯2, later),
	an empty list a Length Error. These were ⎕EVWAIT's rules; ⎕WCLICK
	took them in 7b (until then a matrix was used whole, 0 2⍴0 returned
	empty and 2 0⍴0 was a Length Error; no program relied on it, the
	person, Oct 8, 2026). Errors pop nothing.

	Results (the first number says the source: a window, positive, or
	a source, negative):
	  W X Y    a left click in window W at X,Y (no type code: shipped)
	  ¯1 1 B   beat B of the main clock (clamped to the int32 range);
	           since 7c only with ⎕SEQBEAT←1 (off by default)
	  ¯1 2 S   the music stopped during the wait; S is the new state in
	           ⎕SEQSTATE's codes: 1 stopped, 0 closed, ¯1 port lost
	  ¯1 3 N T P I K ...
	           Phase 7c: notes from the tracks ⎕SEQNOTES set to report,
	           N groups of four, tracks ascending: track, pitch, index
	           (the note's column in the track's matrix, rests counted,
	           in ⎕IO: the library's 0-based column plus ⎕IO) and count
	           (notes the track started since the last one reported).
	           (E[⎕IO+2],4)⍴3↓E is the N×4 matrix.
	  empty    nothing in the list could produce an event when the wait
	           started; a window waited on closed (even if other
	           sources remain); Escape; Ctrl-C; shutdown.
	Ctrl-C ends the wait with an empty result and leaves ABORTSIG set:
	the rest of the line runs, then do_exec() reports the interrupt
	(⎕WCLICK's rule, for both since 7b, agreed Oct 8, 2026; ⎕EVWAIT
	used to stop the line with ERR_INTERRUPT at once). A Ctrl-C already
	pending makes the wait return empty at once, so a second wait on the
	same line doesn't block.

	In apl_cli the hub is in test mode: window events come from ⎕EVPOST,
	beats from the virtual clock (-S); a wait that runs out of both is a
	Domain Error rather than a hang.
*/

#define EVWAIT_C 1

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "parse.h"
#include "mplmem.h"	/* wspace (⎕IO), extern there */
#include "funcs.h"
#include "evhub.h"

#define EV_SEQ		(-1)	/* source: the sequencer */
#define EV_BEAT		1	/* the sequencer's event types */
#define EV_STOPPED	2
#define EV_NOTES	3	/* Phase 7c */

/* seqfuncs.c: the sequencer's side of a wait */
const struct evhub_seq_ops *seqf_wait_ops(void);
int32_t seqf_stop_code(int lost);

/* Element i of a numeric value, as a double; byte data is signed
   (INVARIANTS.md, "Signed/Unsigned Char"). The same as seqfuncs.c's
   elem(): read here directly, not with get_data(), whose short index
   can't pass element 32767. */
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

/* The sources list: windows into a malloc'd array (at least one
   element allocated), *seq set if ¯1 is in it. Whole numbers only, as
   seqfuncs.c's get_list() (a whole float such as ¯1.0 is fine). */
static short
get_sources(struct ValEnt *re, int32_t **wins, long *nwin, int *seq)
{
	int32_t n, i;
	long k;
	double d;

	*wins = NULL;
	*nwin = 0;
	*seq = 0;
	if (((re->ve_vtype | VARIABLE_BIT) & 0xf0) != CONST_CODE)
		return SET_ERR(ERR_DOMAIN);		/* text, ⎕STRUCT */
	if (re->ve_rank > 1) return SET_ERR(ERR_RANK);
	n = re->ve_length;
	*wins = malloc(((size_t)n + 1) * sizeof(int32_t));
	if (!*wins) return SET_ERR(ERR_WSFULL);
	for (i = k = 0; i < n; i++)
	{
		d = elem(re, i);
		if (!(d >= -2147483648.0 && d <= 2147483647.0) || d != floor(d)
		    || (d < 0 && d != EV_SEQ))
		{
			free(*wins);
			*wins = NULL;
			return SET_ERR(ERR_DOMAIN);
		}
		if (d == EV_SEQ) *seq = 1;
		else (*wins)[k++] = (int32_t)d;
	}
	if (n == 0)
	{
		free(*wins);
		*wins = NULL;
		return SET_ERR(ERR_LENGTH);
	}
	*nwin = k;
	return 0;
}

short
ev_wait_apl(struct ValEnt *re)
{
	struct evhub_event ev;
	int32_t *wins, res[3 + 4 * EVHUB_MAXNOTES];
	long nwin;
	int seq, r, i;
	short e;

	if ((e = get_sources(re, &wins, &nwin, &seq)) != 0) return e;
	r = evhub_wait(wins, nwin, seq ? seqf_wait_ops() : NULL, &ev);
	free(wins);

	switch (r)
	{
		case EVHUB_NOTHING:	return SET_ERR(ERR_DOMAIN);	/* apl_cli */
		case EVHUB_NOMEM:	return SET_ERR(ERR_WSFULL);
		case EVHUB_INTERNAL:	return SET_ERR(ERR_SYSTEM);
	}

	stack_pop(1);
	switch (r)
	{
		case EVHUB_CLICK:
			return push_val(3, 1, ev.win);
		case EVHUB_SEQ:
			res[0] = EV_SEQ;
			if (ev.seq_kind == EVHUB_SEQ_NOTES)
			{
				/* Phase 7c: ¯1 3 N, then track, pitch, index
				   (adjusted for ⎕IO) and count per note */
				res[1] = EV_NOTES;
				res[2] = ev.nnotes;
				for (i = 0; i < 4 * ev.nnotes; i++)
					res[3 + i] = ev.notes[i];
				for (i = 0; i < ev.nnotes; i++)
					res[3 + 4 * i + 2] += wspace->origin;
				return push_val((short)(3 + 4 * ev.nnotes), 1, res);
			}
			if (ev.seq_kind == EVHUB_SEQ_BEAT)
			{
				res[1] = EV_BEAT;
				res[2] = (ev.seq_value > INT32_MAX) ? INT32_MAX
					 : (int32_t)ev.seq_value;
			}
			else
			{
				res[1] = EV_STOPPED;
				res[2] = seqf_stop_code(ev.seq_value != 0);
			}
			return push_val(3, 1, res);
	}
	return push_val(0, 1, res);	/* EVHUB_EMPTY */
}
