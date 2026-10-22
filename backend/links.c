// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Keith Doyle

/*
	links.c
*/

#define LINKS_C 1

#include "parse.h"
#include "funcs.h"

/*
	Module slot assignment (the 3-bit "k" selector packed into a compiled
	quad-function reference by platform_translate.c's input_translate(), and unpacked
	by do_sys() as f >> SYSFUNC_SHIFT):

		k=0  SYSFUNCS   (always enabled)
		k=1  FILEFUNCS  (always enabled)
		k=2  GRAPHFUNCS (optional -- disabled by default)
		k=3  SEQFUNCS   (always enabled; seqfuncs.c, the ⎕SEQ MIDI
		                 sequencer functions, Phase 4 Oct 2026)
		k=4..7  reserved / unused

	THESE POSITIONS ARE PERMANENT. A workspace's compiled functions store
	only the raw k value, not a module name -- do_sys() trusts it blindly
	(all_ftypes[k], all_funcs[k]). If a module's position ever changed
	between the build that compiled a function and the build that runs it,
	the function would silently call the wrong module, or crash if the new
	k lands past the end of the real (non-placeholder) entries.

	Consequences:
	  - Never reorder the #ifdef blocks below.
	  - Never remove a slot once it has shipped in any build someone might
	    still have a saved .ws (or an un-)ENTER'd .fn) referencing it --
	    disable it in place (see GRAPHFUNCS below) instead of deleting it.
	  - A brand new module always goes after the last assigned slot, never
	    in a gap.
	  - LEDITFUNCS and DFILEFUNCS were removed outright rather than
	    disabled in place (unlike GRAPHFUNCS below), on two different
	    grounds. LEDITFUNCS has never been enabled in any build since
	    the Linux/Windows port began, so no compiled function from a
	    build this codebase produced can reference it. DFILEFUNCS's
	    source is archived and gone entirely, and it predates the
	    current module ordering, so its historical k value isn't
	    reconstructable from this file with any confidence -- but
	    since no current build can compile it in the first place,
	    that's moot: nothing in this codebase's history can hold a
	    live reference to it either way.
	    Any *future* removal of GRAPHFUNCS should NOT be treated as
	    the same kind of safe outright removal without the same
	    confidence -- disable-in-place is the default; outright slot
	    removal is the exception, justified case by case, not the rule.

	all_fnames[] is scanned (not indexed) by name-lookup code in
	platform_translate.c's input_translate(), via
	"while (cpt = all_fnames[k])" -- a literal 0L there is a loop
	terminator, not a "disabled" marker, and would hide every module after
	it from name lookup. A disabled module must therefore point at
	empty_fnames (a valid pointer to an empty name list) here, never at 0L.
	all_ftypes[]/all_funcs[] are pure indexed lookups (do_sys() checks for
	a null types pointer and fails cleanly), so 0L is the correct
	placeholder in those two arrays.
*/

/* SYSFUNCS and FILEFUNCS are always enabled -- no #ifdef, pinned to k=0/k=1 */
extern UCHAR sys_fnames[];
extern UCHAR file_fnames[];
extern UCHAR sys_ftypes[];
extern short sys_funcs();
extern UCHAR file_ftypes[];
extern short file_funcs();

/* SEQFUNCS is always enabled too, pinned to k=3, after GRAPHFUNCS's
   slot (which stays in place whether or not graphics is built in).
   A build switch was considered and dropped (Oct 5, 2026): leaving
   the module out would have changed which workspaces load and which
   help applies, for no real gain -- an unused sequencer costs nothing
   at run time (its thread starts only at ⎕SEQOPEN). Its independence
   is kept differently: seqfuncs.c is the only interpreter file that
   includes the sequencer's header. */
extern UCHAR seq_fnames[];
extern UCHAR seq_ftypes[];
extern short seq_funcs();

#ifdef GRAPHFUNCS
extern UCHAR graph_fnames[];
extern UCHAR graph_ftypes[];
extern short graph_funcs();
#endif

/* Empty name list -- valid non-null pointer, zero entries. Used to hold a
   disabled module's k slot open for all_fnames[] scanners without hiding
   any module that comes after it. */
UCHAR empty_fnames[] = { 0 };

UCHAR *all_fnames[] = {
	sys_fnames,			/* k=0 */
	file_fnames,			/* k=1 */
#ifdef GRAPHFUNCS
	graph_fnames,			/* k=2 */
#else
	empty_fnames,			/* k=2 (reserved, disabled) */
#endif
	seq_fnames,			/* k=3 */
	0L
};

PTF all_funcs[] = {
	sys_funcs,			/* k=0 */
	file_funcs,			/* k=1 */
#ifdef GRAPHFUNCS
	graph_funcs,			/* k=2 */
#else
	0L,				/* k=2 (reserved, disabled) */
#endif
	seq_funcs,			/* k=3 */
	0L
};

UCHAR *all_ftypes[] = {
	sys_ftypes,			/* k=0 */
	file_ftypes,			/* k=1 */
#ifdef GRAPHFUNCS
	graph_ftypes,			/* k=2 */
#else
	0L,				/* k=2 (reserved, disabled) */
#endif
	seq_ftypes,			/* k=3 */
	0L,0L,0L,0L,0L,0L,0L,0L,0L,0L,0L,0L
};
