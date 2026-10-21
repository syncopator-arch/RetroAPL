/*
	graphfuncs.c
*/

#define GRAPHFUNCS_C 1

#include "parse.h"
#include "mplmem.h"
#include "funcs.h"
#include "externs.h"
#include "graph_bridge.h"

#include <stdlib.h>	/* realloc() -- see gscratch_ensure() below */
#include <ctype.h>	/* isalpha() -- see is_absolute_path() below */
#include <sys/stat.h>	/* stat() -- see fdialog_folder() below */

short ev_wait_apl();	/* evwait.c: WCLICK and EVWAIT's one wait (Phase 7b) */

/* LSCRATCH's real element capacity as an int32_t buffer -- mirrors the
   exact formula that sizes g_lscratch itself (see parse.h). Computed
   independently here rather than via sizeof(LSCRATCH), since it's not
   certain from this file alone whether the LSCRATCH macro expands to the
   array itself (sizeof would work) or to a pointer expression (sizeof
   would silently give the wrong answer -- pointer size, not buffer
   size). Ties back to LINE_SIZE, the actual source of truth, either way.
   Used to bound WNUMS' call to gw_nums() so it can never write past the
   end of LSCRATCH regardless of how many windows happen to be open.
   POLY/WPOINTS/FPOLY formerly shared this same bound but now use the
   growable g_gscratch buffer below instead (see its comment for why),
   so this cap no longer applies to them. */
#define LSCRATCH_INT32_CAP \
	((LINE_SIZE + (int)sizeof(int32_t) - 1) / (int)sizeof(int32_t))

/* Growable scratch buffer for POLY/WPOINTS/FPOLY specifically -- NOT used
   by WNUMS, which keeps using real LSCRATCH/LSCRATCH_INT32_CAP unchanged,
   since WNUMS' data is bounded by how many windows could plausibly be
   open (not arbitrary user data) and never had the problem this solves.

   High-water-mark growth only, via realloc() -- never shrinks. This
   matters for something like a Conway's-Life-style WPOINTS loop whose
   live-cell count oscillates generation to generation: once the
   session's peak size is hit, every subsequent call at or under that
   size reuses the existing buffer with zero allocation overhead, rather
   than realloc() thrashing near whatever threshold the count happens to
   hover around.

   Deliberately never freed anywhere, including at process exit. This
   was a real design question (see prior discussion), not an oversight:
   currently-open graphics windows already survive both )CLEAR and )LOAD
   untouched, and nothing else in this file's graphics state resets on
   either -- CLEANUP_FUNC (defined in parse.h) is never actually invoked
   for this function table (checked: only filfuncs.c's CLEANUP_FUNC case
   is reachable, which closes open files; grfuncs.c's own case is a
   no-op that nothing calls). So this buffer following the same
   "persists for the process's lifetime" rule keeps graphics state
   behaving as one coherent thing, rather than reintroducing a partial
   reset that would be the actually-surprising choice here. The OS
   reclaims it at process exit regardless -- one bounded high-water-mark
   allocation that stops growing once the peak is reached isn't a
   meaningful leak. If this ever needs to be freed explicitly, WCTRL is
   already a deferred, unimplemented case -- a natural home for that,
   should it ever actually be needed.

   ASSUMPTION, unverified: uses plain realloc()/malloc.h's allocator, not
   whatever mplmem.h may provide -- reasoning is that this is engine-side
   scratch memory outside the interpreter's managed value/workspace pool,
   same category as LSCRATCH itself (a plain static array, not something
   drawn from that pool). Revisit if mplmem.h turns out to manage this
   differently than assumed. */
static int32_t *g_gscratch = 0;
static long g_gscratch_cap = 0;	/* capacity in int32_t elements */

static int
gscratch_ensure(count)
long count;
{
int32_t *newbuf;

	if (count <= g_gscratch_cap) return(1);
	newbuf = (int32_t *)realloc(g_gscratch, (size_t)count * sizeof(int32_t));
	if (!newbuf) return(0);
	g_gscratch = newbuf;
	g_gscratch_cap = count;
	return(1);
}

/* Absolute-path check for WILOAD's $APLHOME/images/ default-directory
   resolution (case 14 below). Deliberately just string inspection, no
   platform macro (#ifdef WINDOWS/UNIX) needed or assumed available in
   this file -- grfuncs.c is compiled by the backend's own Makefile, not
   the frontend's CMakeLists, so the WIN32-vs-not compile definitions
   set there aren't necessarily visible here. Recognizes a leading '/'
   (Unix-style) or a drive-letter prefix like "C:" (Windows-style,
   whether followed by '/' or '\\') -- either is treated as absolute,
   used verbatim with no $APLHOME involvement at all. */
static int
is_absolute_path(path)
char *path;
{
	if (!path || !*path) return(0);
	if (path[0] == '/') return(1);
	if (isalpha((unsigned char)path[0]) && path[1] == ':') return(1);
	return(0);
}

/* Nonzero if ve is a NON-EMPTY argument that isn't character data --
   i.e. where a string argument (title, tag, filename, text, pattern)
   should give ERR_DOMAIN. Mainly there to catch a window handle written
   on the wrong side (wnum ⎕WTAG 'tag' for 'tag' ⎕WTAG wnum), which every
   string-taking case used to accept silently: the number's raw bytes
   were copied as if they were text.

   The type test is the same one sysfuncs.c's UCASE/LCASE use (OR in
   VARIABLE_BIT so a temp and a named variable both compare equal to
   QUOT_CODE). The empty exemption is WFONT's rule (case 21, which already
   checked its face spec this way before this helper existed, and is
   left as it was): an empty argument of any type is let through as the
   empty string, so '' behaves exactly as before even if an empty literal
   doesn't carry QUOT_CODE (not confirmed from source), and ⍳0 keeps
   working wherever '' did. It still catches the wrong-side slip, because
   a window handle is never empty. A scalar (rank 0) counts as one
   element, matching every case's own e = 1 default for rank 0.

   A new small helper rather than the check written out inline, because
   the same test appears in eleven places; it follows is_absolute_path()'s
   style. */
static int
not_char_arg(ve)
struct ValEnt *ve;
{
	if (ve->ve_rank && !ve->ve_rho[0]) return(0);	/* empty: any type */
	return((ve->ve_vtype | VARIABLE_BIT) != QUOT_CODE);
}

/* Nonzero if path names an existing folder. */
static int
is_folder(path)
char *path;
{
	struct stat st;

	return(stat(path,&st) == 0 && S_ISDIR(st.st_mode));
}

/* FDIALOG's (case 20) folder prefix, agreed Oct 21, 2026. Qt reads the
   whole filter as file-name patterns, so a filter like 'MIDI/SONG*.MID'
   matched nothing (no file name contains a '/') and the dialog opened
   wherever Qt chose. Now a filter may start with a folder:

     'MIDI/SONG*.MID'           folder MIDI, filter SONG*.MID
     'MIDI/'                    folder MIDI, no filter
     'MIDI/MIDI files (*.MID)'  folder MIDI, filter MIDI files (*.MID)

   The folder is everything up to the last '/' or '\' (both, on every
   platform) before the first '*', '?', '(' or ';' -- a folder name
   can't hold a wildcard, and a separator inside a labelled group
   ("MIDI files (MIDI/SONG*.MID)") or a later ";;" group isn't a prefix.
   A filter with no separator there is left exactly as before.

   Where the folder is: an absolute path (is_absolute_path()) as given;
   anything else under $APLHOME, the same rule ⎕WILOAD and ⎕MIDIREAD
   use for their own subfolders (get_aplhome_dir() with an empty
   subfolder -- which, like every caller, creates $APLHOME itself if
   it's missing, but never the folder named here, which is only looked
   for). Letter case: tried as typed first, then with A-Z made lower
   case, the same retry seqfuncs.c's midi_load() makes and for the same
   reason (the keyboard types capitals; folder names on Linux are
   usually lower case): the whole folder for one under $APLHOME, only
   the last component of an absolute one. A folder found neither way
   is dropped silently and the dialog opens where Qt chooses, with the
   rest of the filter still applied -- agreed, not an error.

   filter is rewritten in place to the part after the folder. dir
   receives the folder found, or an empty string if there was no
   prefix or it wasn't found. Trailing separators are dropped from the
   folder (Windows' stat() is known to reject "dir\" -- from memory, not
   checked here), except for a root ("/", "C:/"). */
static void
fdialog_folder(filter,dir,dirsize)
char *filter;
char *dir;
size_t dirsize;
{
	char *cp, *sep = 0;
	size_t n, used, lc, i;
	int changed;

	dir[0] = '\0';
	for (cp = filter; *cp; cp++)
	{
		if (*cp == '*' || *cp == '?' || *cp == '(' || *cp == ';')
			break;
		if (*cp == '/' || *cp == '\\') sep = cp;
	}
	if (!sep) return;

	/* the folder as typed, separators at its end dropped */
	n = (size_t)(sep - filter) + 1;
	while (n > 1 && (filter[n-1] == '/' || filter[n-1] == '\\')
	       && !(n == 3 && filter[1] == ':'))
		n--;

	if (is_absolute_path(filter))
	{
		used = 0;
		if (n + 1 > dirsize) n = 0;	/* too long: not found */
	}
	else
	{
		get_aplhome_dir(dir,dirsize,"");	/* "$APLHOME//" */
		used = strlen(dir);
		while (used > 1 && (dir[used-1] == '/' || dir[used-1] == '\\'))
			used--;
		dir[used++] = '/';
		if (used + n + 1 > dirsize) n = 0;	/* too long */
	}
	if (n)
	{
		memcpy(dir + used, filter, n);
		dir[used + n] = '\0';
	}
	else
		dir[0] = '\0';

	/* the filter is what follows the separator */
	memmove(filter, sep + 1, strlen(sep + 1) + 1);
	if (!dir[0] || is_folder(dir)) return;

	/* lower-case retry: under $APLHOME the whole folder as typed, for
	   an absolute path only its last component */
	lc = used;
	if (!used)
		for (lc = strlen(dir); lc > 0; lc--)
			if (dir[lc-1] == '/' || dir[lc-1] == '\\') break;
	changed = 0;
	for (i = lc; dir[i]; i++)
		if (dir[i] >= 'A' && dir[i] <= 'Z')
		{
			dir[i] += 32;
			changed = 1;
		}
	if (!changed || !is_folder(dir)) dir[0] = '\0';
}

UCHAR graph_fnames[] = {
	'W','O','P','E','N',0,
	'W','C','L','O','S','E',0,
	'P','E','N',0,
	'P','O','L','Y',0,
	'W','N','U','M','S',0,
	'W','C','L','E','A','R',0,
	'W','T','I','T','L','E',0,
	'W','P','O','I','N','T','S',0,
	'W','S','I','Z','E',0,
	'W','C','T','R','L',0,
	'F','P','O','L','Y',0,
	'W','C','L','I','C','K',0,
	'W','P','I','X','E','L',0,
	'W','T','E','X','T',0,
	'W','I','L','O','A','D',0,
	'W','T','A','G',0,
	'W','T','A','G','O','F',0,
	'W','F','I','L','L',0,
	'W','I','R','E','L','O','A','D',0,
	'W','I','S','A','V','E',0,
	'F','D','I','A','L','O','G',0,
	'W','F','O','N','T',0,
	'T','W','I','D',0,
	'W','V','I','S',0,
	'W','B','L','I','T',0,
	0,0,0
};

UCHAR graph_ftypes[] = {
	DYAD,   /* 0  WOPEN */
	MONAD,  /* 1  WCLOSE */
	DYAD,	/* 2  PEN */
	DYAD,	/* 3  POLY */
	MONAD_OPT_NOASSIGN,	/* 4  WNUMS -- was NILAD; now optionally takes a
			   tag pattern (⎕WNUMS alone = all open handles,
			   ⎕WNUMS 'pattern' = handles whose tag matches,
			   trailing '*' = prefix match, no trailing '*' =
			   exact match). MONAD_OPT_NOASSIGN (V77): the
			   pattern isn't a setting, so no ⎕WNUMS← form;
			   ⎕WNUMS 'pattern' works under every -q mode. */
	DYAD,	/* 5  WCLEAR */
	DYAD,	/* 6  WTITLE -- 'title' WTITLE wnum. Flipped from the
			   originally-shipped wnum WTITLE 'title' -- see this
			   case's own comment below for why that was safe to
			   do now rather than a breaking change. */
	DYAD,	/* 7  WPOINTS */
	MONAD_OPT_NOASSIGN,	/* 8  WSIZE -- was NILAD; now optionally takes a
			   window handle (⎕WSIZE alone = desktop size,
			   ⎕WSIZE h = window h's current size).
			   MONAD_OPT_NOASSIGN (V77): h picks the window to
			   report on, it isn't a setting, so no ⎕WSIZE←
			   form; ⎕WSIZE h works under every -q mode. */
	DYAD,	/* 9  WCTRL -- was MONAD (old, deferred, OOB-buggy, never
			   actually shipped working -- see case 9's own comment).
			   wnums WCTRL opcode,params: le is one or more window
			   numbers (a real multi-element list for opcodes 0-3,
			   a new pattern in this file -- every other DYAD here
			   treats le as a single handle), re is opcode,[params]. */
	DYAD,	/* 10 FPOLY */
	MONAD,	/* 11 WCLICK -- wnums WCLICK, no left arg */
	DYAD,	/* 12 WPIXEL -- wnum WPIXEL <x,y point list> */
	DYAD,	/* 13 WTEXT -- 'string' WTEXT wnum,x,y */
	DYAD,	/* 14 WILOAD -- 'filename' WILOAD x,y */
	DYAD,	/* 15 WTAG -- 'tag' WTAG wnum */
	MONAD,	/* 16 WTAGOF -- WTAGOF wnum, reverse of WTAG */
	DYAD,	/* 17 WFILL -- wnum WFILL x,y  or  wnum WFILL tol,x,y.
			   Appended at the true end, after WTAGOF -- the
			   module+index encoding a saved .ws's compiled
			   bytecode relies on means a new slot can only ever
			   be appended here, never inserted earlier or
			   substituted into an existing name's position. */
	DYAD,	/* 18 WIRELOAD -- 'filename' WIRELOAD wnum. Appended after
			   WFILL for the same append-only reason as WFILL's own
			   comment above. */
	DYAD,	/* 19 WISAVE -- 'filename' WISAVE wnum  or
			   'filename' WISAVE wnum,¯1. Appended after WIRELOAD,
			   same append-only reason. */
	DYAD,	/* 20 FDIALOG -- 'title' FDIALOG 'filter'. Appended after
			   WISAVE, same append-only reason. NOT window-scoped --
			   the first DYAD in this table where neither le nor re
			   is a window handle at all, both are strings; see the
			   case body's own comment for why. */
	DYAD,	/* 21 WFONT -- 'face' WFONT wnum[,size]; '' = face
			   unchanged, '?' = face query. Appended after FDIALOG,
			   same append-only reason. */
	DYAD,	/* 22 TWID -- 'string' TWID wnum. Appended after WFONT,
			   same append-only reason. The name was freed up by
			   renaming sysfuncs.c's dead slot 16 (was TWID, now
			   ZTWID) -- all_fnames[] is scanned by name, so two
			   modules can't usefully share one. The only function
			   in this table without a W prefix, kept that way on
			   purpose: TWID is the name the old sysfuncs.c stub
			   already reserved for exactly this job. */
	MONAD,	/* 23 WVIS -- WVIS wnums, one visibility state per
			   handle (0 hidden, 1 shown, 2 minimized, ¯1 not
			   open). Appended after TWID, same append-only
			   reason. Same MONAD shape as WTAGOF/WCLICK -- no
			   left arg. */
	DYAD,	/* 24 WBLIT -- dst WBLIT src[,dx,dy[,sx,sy,sw,sh]],
			   copy a rectangle between windows' backing images.
			   Appended after WVIS, same append-only reason. */
	0
};

short
graph_funcs(f,le,re)
struct ValEnt *le,*re;
short f;
{
long acc;
int32_t iacc;
char *cp;
short i,e;
long count,ncols;
char pathbuf[1024];	/* WILOAD's resolved (possibly $APLHOME/images/-
			   prefixed) path -- see case 14. Plain literal
			   size rather than reusing some other buffer's
			   constant (e.g. LINE_SIZE), since that's sized
			   for a different purpose and its exact value
			   isn't being relied on here; 1024 is just a
			   generous, unremarkable path-length ceiling.
			   FDIALOG (case 20) also uses it, for the folder
			   fdialog_folder() resolves from its filter. */
char titlebuf[256];	/* FDIALOG's (case 20) le-side title string.
			   Same can't-share-LSCRATCH-with-the-result reason
			   as patbuf below -- FDIALOG's result (the chosen
			   path) also lands in LSCRATCH, via push_str(), a
			   few lines after this is extracted. 256 is the
			   same generous, unremarkable ceiling patbuf uses
			   for a comparable short string; a dialog title
			   longer than that is not a real-world case worth
			   sizing for. */
char filterbuf[512];	/* FDIALOG's (case 20) re-side filter string, same
			   reasoning as titlebuf just above -- kept separate
			   from titlebuf too, not just from LSCRATCH, since
			   both are needed at the same time (neither is
			   consumed before the other is extracted). Larger
			   than titlebuf/patbuf's 256: Qt's native filter
			   syntax can chain several named groups
			   ("Images (*.png *.jpg);;Text (*.txt);;All Files
			   (*)"), so this ceiling is deliberately more
			   generous, still just an unremarkable round number,
			   not a measured requirement. */
char facebuf[256];	/* WFONT's (case 21) le-side face spec. Its own
			   buffer, not LSCRATCH: the '?' query writes its
			   result into LSCRATCH while this is still in use for
			   the dispatch, same can't-share-a-buffer reasoning
			   as patbuf below. 256 matches patbuf/titlebuf;
			   longer specs are ERR_LENGTH, not truncated. */
char patbuf[256];	/* WNUMS' (case 4) optional tag-pattern arg,
			   extracted here rather than into LSCRATCH --
			   unlike WTITLE/WTEXT/WTAG's le-side string
			   extraction (which never shares LSCRATCH with an
			   output write in the same case), WNUMS' filtered
			   path also writes its *result* into LSCRATCH via
			   gw_nums_matching()'s buf param moments later.
			   WPIXEL (case 12) gets away with exactly this
			   kind of input/output aliasing in g_gscratch, but
			   only because gw_pixel() is documented to fully
			   consume its input before writing any output --
			   repeating that same fragile pattern a second
			   time, for a much smaller string, isn't worth the
			   risk of it quietly breaking if either function's
			   internals change later. 256 is a generous,
			   unremarkable tag-pattern-length ceiling, same
			   spirit as pathbuf's own sizing above -- and
			   unlike pathbuf/LSCRATCH, this is a small
			   fixed-size local array, so its copy below is
			   explicitly bounds-checked rather than trusted
			   the way WOPEN/WTITLE/WTEXT's existing
			   LSCRATCH-bound copies are. */
long vis;	/* WOPEN's optional 5th / WILOAD's optional 3rd right-arg
		   element: 1 = open shown (the default when the element is
		   absent -- what every existing call already does), 0 = open
		   hidden. Anything else is ERR_DOMAIN, same code WCTRL uses
		   for an unrecognized opcode. Its own named local rather
		   than reusing count/ncols, so what it holds is obvious at
		   the gw_open()/gw_load_image() call sites. */
long j;	/* loop var for every loop over an argument's elements: POLY/
	   WPOINTS/FPOLY/WPIXEL/WVIS's copy loops, and since V99 the
	   bulk loops over a list of window numbers (WCLOSE, PEN, WCTRL,
	   WTAG). NOT short i: past SHRT_MAX it wraps negative, and a
	   loop "while (i < SBOUNDS(re))" over 32768 or more elements
	   never ends. The lists are bounded only by workspace memory
	   (gscratch_ensure() for the copies). get_data() itself took a
	   short index until V99, so j was cut down there anyway. */

	acc = 0L;
	switch (f) 
	{
		case 0:	/* WOPEN , 'title' wnum WOPEN x y w h [vis] */
			/* Optional 5th re element: vis, 1 = shown (default
			   when absent), 0 = open hidden -- registered and
			   drawable, but not shown until wnum WCTRL 6. The
			   length floor stays 4, so every existing 4-element
			   call is unchanged; the 5th element is only read if
			   present -- same extend-without-breaking shape
			   WSIZE's MONAD_OPT extension used. x,y still
			   matters for a hidden open: it's where WCTRL 6 will
			   show the window (¯1 ¯1 = let the window manager
			   place it then). Elements past the 5th are ignored,
			   same as elements past the 4th always were. vis is
			   validated before anything is extracted or popped,
			   same place the length check already returns from.
			   A non-empty title that isn't character data is
			   ERR_DOMAIN (not_char_arg()), checked first so the
			   handle-on-the-wrong-side slip gets the same error
			   whatever re's length is. */
			if (not_char_arg(le)) return(SET_ERR(ERR_DOMAIN));
			if (SBOUNDS(re) < 4) return(SET_ERR(ERR_LENGTH));
			vis = 1L;
			if (SBOUNDS(re) >= 5) vis = get_data(re,4);
			if (vis != 0L && vis != 1L) return(SET_ERR(ERR_DOMAIN));
			acc = 1L;
			if (le->ve_rank) acc = le->ve_rho[0];
			cp = (char *)LSCRATCH;
			if (acc) memcpy(cp,DATA_AD(le),acc);
			cp[acc] = '\0';
			acc = gw_open(cp,get_data(re,0),get_data(re,1),
					get_data(re,2),get_data(re,3),vis);
			stack_pop(2);
			/* gw_open() returns -1 if w/h are unusable (<=0).
			   ERR_DOMAIN is a guess at the right code here --
			   no old equivalent to match against, since the old
			   open_window() had no failure path at all. */
			if (acc < 0) return(SET_ERR(ERR_DOMAIN));
			/* Falls through to this function's shared return at
			   the bottom, which narrows acc through a real
			   int32_t before calling push_val() -- see that
			   comment for why (was a pointer-type mismatch/
			   endianness footgun, now fixed at that one spot). */
			break;	  /* returns window num */
		case 1:	/* WCLOSE */
			/* Old loop condition was "SBOUNDS(re) >= i", which
			   reads one element past the end of re (off-by-one --
			   e.g. SBOUNDS==3 lets i reach 3, a 3-element vector's
			   valid indices are 0..2). Not carrying that forward. */
			j = 0;
			while (j < SBOUNDS(re)) gw_close(get_data(re,j++));
			return(0);	/* leave window list for return */
		case 2:	/* PEN. Dispatch is by re's element count, not
			   arity -- same technique WPOINTS already uses to
			   do different things from one DYAD case. This is
			   the resolved design, not the original attempt:
			   an earlier MONAD_OPT-style arity split (echoing
			   WSIZE) turned out to be invalid monadic syntax
			   *and* to assume a function-type code that doesn't
			   exist -- see apl_graphics_other_ideas_notes_v1.md's
			   PEN section for the full history.

			   le is one or more window handles for every SET
			   case below (re length 1 non-query, 3, or 4) --
			   bulk over le, same pattern WCTRL's opcodes 0-3
			   already established: loop over le, apply the same
			   value to every handle, swapdrop() leaves le (the
			   handle list, unchanged) as the result. Empty le is
			   a no-op for these cases, same as WCLOSE's/WCTRL's
			   own empty-list convention -- not an error.

			   re length 1 is dispatched by VALUE, not just
			   presence, as of today -- the one case in this
			   switch where that's true:
			     re[0] == -1 -> QUERY, single handle only (see
			       below).
			     re[0] != -1 -> set WIDTH only, color unchanged
			       -- the color-only case's mirror image. New
			       capability. -1 reused deliberately as this
			       new case's own dispatch value, not a fresh
			       sentinel invented for it: it's already what
			       width==-1 means one level down in gw_pen()/
			       GraphManager::setPen() (leave color
			       unchanged), and real widths are always >=1
			       so it can't collide with a genuine width
			       request either. gw_pen() gets -1,-1,-1 for
			       r,g,b (the "leave color unchanged" sentinel,
			       see graph_bridge.h) and re[0] itself for
			       width. Fire-and-forget, same as the length-3/
			       4 cases below.

			   QUERY (re[0] == -1) stays SINGLE-HANDLE ONLY, a
			   deliberate choice made in conversation rather than
			   extended to a list: query returns a new per-window
			   value, and a bulk version would need its own
			   return-shape design (fixed rows vs. this case's
			   existing "empty vector means not open" convention
			   don't mix cleanly) -- not pursued. le must be
			   exactly one element here; ERR_LENGTH otherwise,
			   same code WOPEN/WCTRL's move opcode already use
			   for their own wrong-shaped operand. Returns the
			   window's current r,g,b,width as 4 values, or an
			   EMPTY vector if the handle isn't currently open --
			   deliberately not an error here; matches WNUMS'/
			   WCLICK's "nothing to report" shape rather than
			   WSIZE's/WPIXEL's "bad handle fails the call"
			   convention, a real choice for this particular
			   query (unlike WSIZE/WPIXEL, silently asking about
			   a closed/never-opened window's pen is the more
			   natural "nothing there" case here, not a caller
			   error). Needs Qt::BlockingQueuedConnection
			   (gw_pen_query()) since pen state only exists on
			   the GUI-thread object -- same category as
			   gw_size()/gw_pixel(), not the fire-and-forget set
			   paths. Wholly new result, not a reused operand --
			   stack_pop(2), unconditionally, before either
			   push_val() branch, same as WCLICK's
			   stack_pop(1)-then-branch shape.

			   re length 3 (r,g,b) -> set color only, width left
			   unchanged. gw_pen() gets -1 for width, the "leave
			   unchanged" sentinel (see graph_bridge.h) -- real
			   widths are always >=1, so this is unambiguous.

			   re length 4 (r,g,b,width) -> set both.
			   Byte-for-byte the already-shipped single-handle
			   behavior, now applied per-handle across le's list. */
			switch (SBOUNDS(re)) {
				case 1:	/* re[0]==-1: query (single handle
					   only). Anything else: set width
					   only, color unchanged -- bulk
					   over le. */
					acc = get_data(re,0);
					if (acc == -1L) {	/* query */
						if (SBOUNDS(le) != 1)
							return(SET_ERR(ERR_LENGTH));
						acc = get_data(le,0);	/* window # */
						i = (short)gw_pen_query(acc,LSCRATCH);
						stack_pop(2);
						if (!i) return(push_val(0,1,LSCRATCH));
						return(push_val(4,1,LSCRATCH));
					}
					j = 0;		/* set width only, bulk */
					while (j < SBOUNDS(le))
						gw_pen(get_data(le,j++),
							-1L,-1L,-1L,acc);
					swapdrop();
					return(0);
				case 3:	/* set color only, width unchanged --
					   bulk over le */
					j = 0;
					while (j < SBOUNDS(le))
						gw_pen(get_data(le,j++),
							get_data(re,0),
							get_data(re,1),
							get_data(re,2),-1L);
					swapdrop();
					return(0);
				case 4:	/* set color+width, unchanged -- bulk
					   over le */
					j = 0;
					while (j < SBOUNDS(le))
						gw_pen(get_data(le,j++),
							get_data(re,0),
							get_data(re,1),
							get_data(re,2),
							get_data(re,3));
					swapdrop();
					return(0);
				default:
					return(SET_ERR(ERR_LENGTH));
			}
		case 3:	/* POLY, wnum POLY <p,x,y triples>. re is a flat
			   p,x,y stream (multiple of 3). Uses DATA_LEN, not
			   SBOUNDS -- SBOUNDS is ve_rho[0] (the least-
			   significant dimension only), which happens to
			   equal the true element count for a rank<=1 vector
			   but is NOT a safe substitute for "total elements"
			   in general (see WPOINTS below, where this
			   distinction is a real bug fix, not just
			   defensive). */
			if (!SBOUNDS(le)) return(SET_ERR(ERR_LENGTH));
			acc = get_data(le,0);		/* window # */
			count = DATA_LEN(re);
			if (count % 3) return(SET_ERR(ERR_LENGTH));
			if (!gscratch_ensure(count))
				return(SET_ERR(ERR_WSFULL));
			for (j = 0; j < count; j++)
				g_gscratch[j] = (int32_t)get_data(re,j);
			gw_poly(acc,g_gscratch,count);
			swapdrop();
			return(0);
		case 4:	/* WNUMS, ⎕WNUMS or ⎕WNUMS 'pattern'. MONAD_OPT:
			   re == NULL for the niladic call (unchanged --
			   current handle list, creation order). re present
			   -> filtered by tag: a trailing '*' is a prefix
			   match (everything before the '*'), no trailing
			   '*' is an exact match -- see
			   GraphManager::nums(QString)'s own comment for the
			   exact rule, including the empty-pattern/
			   empty-tag edge case. String extraction goes into
			   patbuf (see its declaration above), not LSCRATCH
			   -- this call's result also lands in LSCRATCH a
			   few lines later, so the two can't share a buffer
			   the way WTITLE/WTEXT/WTAG's own extraction safely
			   does. A non-empty pattern that isn't character
			   data is ERR_DOMAIN (not_char_arg()) -- it used to
			   be copied byte-for-byte and silently match
			   nothing. */
			if (re) {
				if (not_char_arg(re)) return(SET_ERR(ERR_DOMAIN));
				e = 1;
				if (re->ve_rank) e = (short)re->ve_rho[0];
				if (e >= (short)sizeof(patbuf))
					e = (short)sizeof(patbuf) - 1;
				if (e) memcpy(patbuf,DATA_AD(re),e);
				patbuf[e] = '\0';
				stack_pop(1);
				acc = gw_nums_matching(patbuf,LSCRATCH,LSCRATCH_INT32_CAP);
			} else {
				acc = gw_nums(LSCRATCH, LSCRATCH_INT32_CAP);
			}
			return(push_val((short)acc,1,LSCRATCH));
		case 5:	/* WCLEAR, wnum WCLEAR color */
			if (!SBOUNDS(le)) return(SET_ERR(ERR_LENGTH));
			if (!SBOUNDS(re)) return(SET_ERR(ERR_LENGTH));
			acc = get_data(le,0);		/* window # */
			gw_clear(acc,get_data(re,0));
			swapdrop();
			return(0);
		case 6:	/* WTITLE, 'title' ⎕WTITLE wnum -- string on le,
			   handle on re. Flipped from the originally-shipped
			   wnum ⎕WTITLE 'title' shape; safe to do since
			   WTITLE has not actually been used yet (see
			   WORKING_PREFERENCES.md's "additive over breaking"
			   note -- that rule protects what's actually
			   shipped-and-used, not merely what's already
			   coded). Brings WTITLE in line with WOPEN/WTEXT/
			   WILOAD's existing char-le/numeric-re shape for
			   every mixed char/numeric graphics DYAD, and puts
			   wnum on the right where ¯1↑⎕WNUMS sits
			   unparenthesized -- the same reason WTAG (case 15,
			   below) is designed this way from the start.
			   String extraction mirrors WTEXT's own le-side
			   pattern (case 13) exactly -- after this flip the
			   two are textually identical, just feeding
			   different bridge calls. Both arguments are still
			   required non-empty, same as the original shape --
			   only which side each check applies to changed;
			   worth reconsidering separately if an empty title
			   should actually be allowed, matching WOPEN's own
			   "may be empty" title. A title that isn't character
			   data is ERR_DOMAIN (not_char_arg()), checked first. */
			if (not_char_arg(le)) return(SET_ERR(ERR_DOMAIN));
			if (!SBOUNDS(le)) return(SET_ERR(ERR_LENGTH));
			if (!SBOUNDS(re)) return(SET_ERR(ERR_LENGTH));
			acc = get_data(re,0);		/* window # */
			e = 1;
			if (le->ve_rank) e = (short)le->ve_rho[0];
			cp = (char *)LSCRATCH;
			memcpy(cp,DATA_AD(le),e);
			cp[e] = '\0';
			gw_title(acc,cp);
			swapdrop();
			return(0);
		case 7:	/* WPOINTS, wnum WPOINTS <point data>. Shape-driven:
			   N x2 -> current pen color, N x3 -> 3rd column is a
			   per-point packed-color override.
			   count MUST be DATA_LEN(re) (true total element
			   count via ve_length), not SBOUNDS(re) -- SBOUNDS is
			   ve_rho[0], the least-significant dimension only.
			   ncols is read from ve_rho[0] too, NOT
			   ve_rho[ve_rank-1] -- ve_rho orders dimensions from
			   least- to most-significant (rho[0] = columns,
			   rho[1] = rows, ...), confirmed via a debug print
			   against a real 3 2⍴... value during this work: for
			   that value, rank=2, rho[0]=2 (columns), rho[1]=3
			   (rows). Longstanding interpreter convention, not
			   graphics-specific -- see parse.h's ve_rho/SBOUNDS
			   comments. get_data(re,i) confirmed (sys.c) to walk
			   the data as a flat buffer with no reference to
			   ve_rho/ve_rank at all, for i in [0,ve_length) --
			   what makes the single linear copy loop below
			   correct regardless of ncols. */
			if (!SBOUNDS(le)) return(SET_ERR(ERR_LENGTH));
			acc = get_data(le,0);		/* window # */
			count = DATA_LEN(re);
			if (re->ve_rank >= 2)
				ncols = re->ve_rho[0];
			else
				ncols = count;		/* single row */
			if (ncols != 2 && ncols != 3)
				return(SET_ERR(ERR_DOMAIN));
			if (!gscratch_ensure(count))
				return(SET_ERR(ERR_WSFULL));
			for (j = 0; j < count; j++)
				g_gscratch[j] = (int32_t)get_data(re,j);
			gw_points(acc,g_gscratch,count,ncols);
			swapdrop();
			return(0);
		case 8:	/* WSIZE. No right arg (re == NULL, MONAD_OPT's
			   do_sys() case when nothing was passed) -> desktop
			   size, passing gw_size() the -1 sentinel -- same
			   convention WOPEN already uses for its own x,y
			   "no explicit value" case. A right arg -> that
			   window's size; 0 is NOT special-cased as desktop
			   here (deliberately -- see graph_bridge.h), so
			   WSIZE 0 falls through to gw_size()'s ordinary
			   "not currently open" failure path, same as any
			   other never-opened handle. ERR_DOMAIN on failure
			   is a guess at the right code, same guess WOPEN
			   already makes for its own failure case -- no old
			   equivalent to match against here either.

			   Result is 4 elements: x,y,width,height -- x,y is
			   the window's (or desktop's) current upper-left
			   position, in the same x,y coordinate system
			   WOPEN's/WILOAD's own placement argument uses (see
			   gw_size()'s header comment). Widened from the
			   original 2-element width,height-only result --
			   a deliberate breaking change to WSIZE's own
			   shape, not the usual new-function-instead
			   pattern (WSIZE is young enough, and few enough
			   places call it, that widening in place was judged
			   worth it here and there are limited slots in
               the function table; existing callers already updated
			   for 4 results as of this change).  the 4
               elements now match WOPEN */
			acc = -1L;
			if (re) {
				if (!SBOUNDS(re)) return(SET_ERR(ERR_LENGTH));
				acc = get_data(re,0);
				/* WSIZE's result (window/desktop size+pos) is
				   a wholly new value, not derived from re the
				   way swapdrop()'s DYAD cases reuse/replace
				   an existing operand -- so the consumed
				   right arg needs to be popped off the stack
				   before pushing that new result, same
				   pattern WOPEN's DYAD case uses
				   (stack_pop() before its own fall-through
				   push_val()). Missing this left the right
				   arg's original value sitting on the stack
				   underneath the newly pushed size result --
				   symptom was a stray extra value trailing
				   the real one (e.g. "600 600" followed by a
				   leftover "1") and assignment picking up
				   the wrong one of the two. Niladic calls
				   (re == NULL) have nothing on the stack to
				   pop here, matching WNUMS' NILAD case, which
				   doesn't pop either. */
				stack_pop(1);
			}
			if (!gw_size(acc,LSCRATCH))
				return(SET_ERR(ERR_DOMAIN));
			
			/* swap size vs position */
			acc = LSCRATCH[0];
			LSCRATCH[0] = LSCRATCH[2];
			LSCRATCH[2] = acc;
			acc = LSCRATCH[1];
			LSCRATCH[1] = LSCRATCH[3];
			LSCRATCH[3] = acc;

			return(push_val(4,1,LSCRATCH));
		case 9:	/* WCTRL, wnums ⎕WCTRL opcode,params -- window-state
			   control. Design settled in conversation (see
			   apl_graphics_other_ideas_notes_v1.md's WCTRL
			   section for the superseded MONAD shape/OOB-bug
			   history this replaces): DYAD, le is one or more
			   window numbers, re is opcode,[params].

			   Opcodes 0-3 (minimize/restore/front/back) take a
			   bare 1-element re and loop over every handle in
			   le -- deliberately mirroring WCLOSE's own loop
			   over its handle list (case 1 above), just on le
			   instead of re here. This is a genuinely new
			   pattern for le in this file: every other DYAD case
			   treats le as a single window handle
			   (PEN/WCLEAR/WPOINTS/etc. all just get_data(le,0));
			   WCTRL's bulk opcodes are the first to actually walk
			   it as a list. Nonexistent handles are silently
			   ignored per-element, same as WCLOSE/setTitle/setTag
			   already do for an unknown handle -- no separate
			   validation pass, gw_ctrl()/GraphManager::ctrl()
			   just no-op on a handle that isn't open. An empty le
			   is a no-op (loop runs zero times), same as an empty
			   WCLOSE list -- not an error.

			   Opcode 4 (move) is the one exception the person
			   called out directly: re is 4,x,y (3 elements,
			   validated exactly, not just "at least"), and le
			   must be exactly one window number -- ERR_LENGTH
			   otherwise, same code WOPEN already uses for its own
			   wrong-shaped re. x,y is the same upper-left
			   placement coordinate system WOPEN's/WSIZE's own x,y
			   already use (GraphManager::ctrl()'s move case calls
			   plain QWidget::move(), the same call
			   registerWindow() already makes for WOPEN's initial
			   placement).

			   Opcodes 5 (hide) and 6 (show) were added with
			   WOPEN/WILOAD's visibility flag. They're bulk
			   opcodes with the same 1-element re and the same loop
			   over le as 0-3. A hidden window keeps its handle and
			   everything drawn on it; 6 shows it at its recorded
			   position (set by WOPEN/WILOAD's x,y or by opcode 4,
			   which works on hidden windows too). 0/1 are no-ops
			   on a hidden window and 6 is a no-op on one that
			   isn't hidden -- that's resolved in
			   GraphManager::ctrl(), not here, same as the
			   unknown-handle no-op.

			   Unrecognized opcode -> ERR_DOMAIN, matching CTRL's
			   (sysfuncs.c) own out-of-range-subfunction handling
			   -- not a silent no-op, since nothing past 6 is
			   designed yet.

			   Side-effect-only, like WCLEAR/WTAG/WTEXT:
			   swapdrop()+return(0), NOT stack_pop()+push_val() --
			   this leaves le (the wnum list, untouched) as the
			   line's implicit result, which is exactly the point
			   for the W←W ⎕WCTRL ... suppression idiom discussed:
			   swapdrop() already returns le as-is, no separate
			   echo/pass-through logic needed here. */
			if (!SBOUNDS(re)) return(SET_ERR(ERR_LENGTH));
			acc = get_data(re,0);		/* opcode */
			switch (acc) {
				case 0:	/* minimize */
				case 1:	/* restore */
				case 2:	/* front (raise) */
				case 3:	/* back (lower) */
				case 5:	/* hide */
				case 6:	/* show */
					if (SBOUNDS(re) != 1)
						return(SET_ERR(ERR_LENGTH));
					j = 0;
					while (j < SBOUNDS(le))
						gw_ctrl(get_data(le,j++),acc,0L,0L);
					break;
				case 4:	/* move, re = 4,x,y -- single window only */
					if (SBOUNDS(re) != 3)
						return(SET_ERR(ERR_LENGTH));
					if (SBOUNDS(le) != 1)
						return(SET_ERR(ERR_LENGTH));
					gw_ctrl(get_data(le,0),acc,
						get_data(re,1),get_data(re,2));
					break;
				default:
					return(SET_ERR(ERR_DOMAIN));
			}
			swapdrop();
			return(0);
		case 10:	/* FPOLY, wnum FPOLY <x,y pairs>. re is a flat
			   x,y stream (multiple of 2) -- accepted as either a
			   plain vector or an N x2 matrix, shape is never
			   consulted. DATA_LEN gives the true element count
			   either way, and get_data() walks the data flat
			   regardless of ve_rho/ve_rank (same reasoning as
			   WPOINTS above) -- simpler here than WPOINTS, since
			   there's no ncols branch: every element is always
			   just an x or y, never a mode/color field the way
			   POLY's p or WPOINTS' 3rd column can be.
			   Filled using the window's current PEN color; no
			   separate outline stroke -- a same-color border
			   would be invisible against the fill anyway. Uses
			   DATA_LEN, not SBOUNDS, for the same reason POLY's
			   comment above explains (SBOUNDS is ve_rho[0] only,
			   not a safe stand-in for total element count in
			   general).
			   Polygon is implicitly closed (last vertex back to
			   first) -- callers do not repeat the first point.
			   Self-intersecting shapes (e.g. a figure-8) are not
			   an error: gw_fpoly() fills under the odd-even rule,
			   which resolves that correctly with no validation
			   needed here. */
			if (!SBOUNDS(le)) return(SET_ERR(ERR_LENGTH));
			acc = get_data(le,0);		/* window # */
			count = DATA_LEN(re);
			if (count % 2) return(SET_ERR(ERR_LENGTH));
			if (count < 6) return(SET_ERR(ERR_LENGTH));
				/* fewer than 3 x,y vertices -- not a polygon */
			if (!gscratch_ensure(count))
				return(SET_ERR(ERR_WSFULL));
			for (j = 0; j < count; j++)
				g_gscratch[j] = (int32_t)get_data(re,j);
			gw_fpoly(acc,g_gscratch,count);
			swapdrop();
			return(0);
		case 11:	/* WCLICK sources. The same wait as EVWAIT since
			   Phase 7b: evwait.c's ev_wait_apl(), which waits in
			   the event hub (evhub.c) for a left click in one of
			   the windows listed, or a sequencer event if ¯1 is
			   listed. Returns handle,x,y on a click (unchanged),
			   ¯1 type details from the sequencer, or empty: none
			   of the list could produce an event (windows not
			   open or hidden, the sequencer not playing), a
			   window waited on closed, Escape, or Ctrl-C (the
			   line runs on and is interrupted after it, as
			   before). Argument rules are EVWAIT's since 7b: a
			   scalar or vector of whole numbers (a matrix is a
			   Rank Error, a fraction or a negative number other
			   than ¯1 a Domain Error, an empty list a Length
			   Error). See evwait.c, evhub.c and
			   MIDI_SEQUENCER_NOTES.md ("Events and waiting");
			   apl_graphics_mouse_input_notes_v1.md for the
			   original click-wait design. */
			return(ev_wait_apl(re));
		case 12:	/* WPIXEL, wnum WPIXEL <x,y point list>. re is
			   a flat x,y stream, N x2 shape (matching WPOINTS'
			   own convention -- confirmed via user decision, not
			   assumed; see apl_graphics_mouse_input_notes_v1.md).
			   count via DATA_LEN, not SBOUNDS, same reasoning as
			   POLY/WPOINTS/FPOLY above. Fully synchronous
			   (gw_pixel(), Qt::BlockingQueuedConnection) -- no
			   waiting on user input, unlike WCLICK. Returns one
			   packed 24-bit color per input point, in order; a
			   point outside the window's current bounds returns
			   ¯1 for that entry rather than failing the whole
			   call (packed colors are always non-negative, so ¯1
			   is an unambiguous per-point sentinel -- user
			   decision, resolves this doc's previously-open
			   out-of-bounds question). A wholly bad handle (not
			   currently open at all) is the one thing that still
			   fails the whole call, same as WSIZE's existing
			   handle-not-open convention.

			   Output goes into g_gscratch itself, NOT LSCRATCH --
			   deliberately. gw_pixel() only ever reads the input
			   points (already fully copied out before it writes
			   anything back), so writing results into the same
			   array afterward is safe, and g_gscratch is already
			   sized to >=count elements from the ensure() call
			   below while the result only ever needs count/2 --
			   always fits with room to spare. LSCRATCH couldn't
			   make that same guarantee: its capacity is a small
			   fixed cap (see LSCRATCH_INT32_CAP), but a point
			   list here is bounded only by workspace memory via
			   gscratch_ensure(), so a large-enough call could
			   silently overrun LSCRATCH the way WNUMS/WCLICK's
			   naturally-small results never risk. */
			if (!SBOUNDS(le)) return(SET_ERR(ERR_LENGTH));
			acc = get_data(le,0);		/* window # */
			count = DATA_LEN(re);
			if (count % 2) return(SET_ERR(ERR_LENGTH));
			if (!gscratch_ensure(count))
				return(SET_ERR(ERR_WSFULL));
			for (j = 0; j < count; j++)
				g_gscratch[j] = (int32_t)get_data(re,j);
			i = gw_pixel(acc,g_gscratch,count,g_gscratch);
			/* stack_pop(2), NOT swapdrop() -- WPIXEL computes a
			   wholly new result (the color array), it doesn't
			   reuse/replace an operand in place the way the
			   swapdrop()-using cases above do (PEN/WCLEAR/
			   WPOINTS/FPOLY etc., which return 0 and leave one
			   existing operand as their implicit result). Same
			   situation WOPEN's own DYAD case is in, and it uses
			   stack_pop(2) for exactly this reason. Using
			   swapdrop() here was the actual bug: it left le
			   (the window number) sitting on the stack
			   unpopped, so after push_val() below added the real
			   result on top, two values were on the stack instead
			   of one -- symptom was the window number surviving
			   as a stray leftover output alongside the real
			   result. */
			stack_pop(2);
			if (!i) return(SET_ERR(ERR_DOMAIN));
			/* push_val()'s count is int32_t since V99; it was
			   short, and a result of more than 32767 pixels
			   reached make_temp() as a negative length. */
			return(push_val((int32_t)(count/2),1,g_gscratch));
		case 13:	/* WTEXT, 'string' WTEXT wnum,x,y. Char le /
			   numeric re, deliberately mirroring WOPEN's
			   'title' WOPEN x,y,w,h shape rather than
			   WPOINTS/POLY's all-numeric-right convention (see
			   context doc) -- keeps the char/numeric split WOPEN
			   already established, reads without parens at the
			   call site. String extraction mirrors WTITLE's own
			   re-side pattern (case 6), just off le here instead
			   -- defaults e to 1 for a rank-0 (scalar) string,
			   same fix WTITLE already applies for its own case.
			   Unlike WOPEN, this produces no new result value --
			   it's a side-effect-only call in the same category
			   as WTITLE/PEN/WCLEAR/POLY/WPOINTS/FPOLY, so
			   swapdrop()+return(0), not WOPEN's
			   stack_pop(2)+push_val() path. A non-empty string
			   that isn't character data is ERR_DOMAIN
			   (not_char_arg()), checked first. */
			if (not_char_arg(le)) return(SET_ERR(ERR_DOMAIN));
			if (SBOUNDS(re) < 3) return(SET_ERR(ERR_LENGTH));
			e = 1;
			if (le->ve_rank) e = (short)le->ve_rho[0];
			cp = (char *)LSCRATCH;
			if (e) memcpy(cp,DATA_AD(le),e);
			cp[e] = '\0';
			gw_text(cp,get_data(re,0),get_data(re,1),
				get_data(re,2));
			swapdrop();
			return(0);
		case 14:	/* WILOAD, 'filename' WILOAD x,y. Loads an
				   image file directly into a brand-new
				   window -- the image *becomes* the window's
				   entire backing bitmap; window size comes
				   from the decoded image, not from the
				   caller (no w,h in re, unlike WOPEN --
				   there's nothing to size, the image
				   dictates it). Window title is just the
				   filename itself, not a separate argument:
				   a DYAD only has room for filename (le) and
				   placement (re), the same trade-off WOPEN's
				   own title/x,y,w,h shape doesn't have to
				   make since it has no filename to carry.
				   Filename extraction mirrors WOPEN's own
				   title-extraction (case 0) exactly, just off
				   le for a different string here.
				   x,y == ¯1 ¯1 is the same system-default-
				   placement sentinel WOPEN uses.
				   An absolute filename (is_absolute_path())
				   is used exactly as given; anything else is
				   resolved against $APLHOME/images/ via
				   get_aplhome_dir() -- same convention already
				   used for ws/fns/bin, not a new mechanism.
				   Decode happens on the engine thread, before
				   any GUI-thread hop (see gw_load_image()'s
				   own comment) -- so a bad path, corrupt
				   file, or unsupported format reports back
				   here directly as ERR_IMGLOAD. Was ERR_DOMAIN
				   originally (matching WOPEN's own "window
				   construction failed" precedent); split out
				   into its own dedicated code once WIRELOAD
				   needed to tell "wouldn't decode" apart from
				   "wrong size for this window" -- see case 18
				   below -- so this got the same more-specific
				   code at the same time, per the decision to
				   keep WILOAD/WIRELOAD's decode-failure
				   reporting matched rather than one staying on
				   the old generic code.

				   Optional 3rd re element: vis, same meaning
				   and same rules as WOPEN's optional 5th (1 =
				   shown, the default when absent; 0 = load
				   hidden; anything else ERR_DOMAIN; length
				   floor unchanged at 2). A hidden load still
				   takes x,y -- it's recorded as where WCTRL 6
				   will show the window, or ¯1 ¯1 to let the
				   window manager place it then -- so the 3rd
				   element is always the flag and never
				   ambiguous with a position.

				   A non-empty filename that isn't character
				   data is ERR_DOMAIN (not_char_arg()), checked
				   first. */
			if (not_char_arg(le)) return(SET_ERR(ERR_DOMAIN));
			if (SBOUNDS(re) < 2) return(SET_ERR(ERR_LENGTH));
			vis = 1L;
			if (SBOUNDS(re) >= 3) vis = get_data(re,2);
			if (vis != 0L && vis != 1L) return(SET_ERR(ERR_DOMAIN));
			acc = 1L;
			if (le->ve_rank) acc = le->ve_rho[0];
			cp = (char *)LSCRATCH;
			if (acc) memcpy(cp,DATA_AD(le),acc);
			cp[acc] = '\0';
			/* cp now holds the raw filename exactly as given.
			   An absolute path is used verbatim; anything else
			   is resolved against $APLHOME/images/ (falls back
			   to $HOME/apl/images/, then ./images/, and creates
			   the directory if it doesn't exist yet -- all
			   get_aplhome_dir()'s existing behavior, same
			   convention already used for ws/fns/bin). Built
			   into pathbuf, a separate buffer from cp/LSCRATCH,
			   since cp is still needed as-is for the check
			   itself -- can't build the resolved path back into
			   the same memory it's reading the filename from. */
			if (is_absolute_path(cp)) {
				strncpy(pathbuf,cp,sizeof(pathbuf)-1);
				pathbuf[sizeof(pathbuf)-1] = '\0';
			} else {
				size_t used;

				get_aplhome_dir(pathbuf,sizeof(pathbuf),"images");
				used = strlen(pathbuf);
				/* a path that doesn't fit would name some other file:
				   clear it, so the call below fails the same way a
				   missing file does */
				if (snprintf(pathbuf+used,sizeof(pathbuf)-used,"%s",cp)
				    >= (int)(sizeof(pathbuf)-used))
					pathbuf[0] = '\0';
			}
			acc = gw_load_image(pathbuf,get_data(re,0),get_data(re,1),vis);
			stack_pop(2);
			if (acc < 0) return(SET_ERR(ERR_IMGLOAD));
			break;	  /* returns window num, same as WOPEN */
		case 15:	/* WTAG, 'tag' ⎕WTAG wnum(s) -- same le-string/
			   re-handle shape as the just-flipped WTITLE (case
			   6) and WOPEN/WTEXT/WILOAD, designed this way from
			   the start so ¯1↑⎕WNUMS sits unparenthesized on the
			   right. re is one or more window handles as of
			   today -- extended to a real bulk list, mirroring
			   WCLOSE's own loop over re (case 1) directly, not
			   just WCTRL's/PEN's le-side pattern -- WTAG's
			   handle(s) sit on re, the opposite side from those.
			   swapdrop() leaves le (the tag string, unchanged) as
			   the result, same as before. Empty re is a no-op,
			   same as WCLOSE's own empty-list convention -- no
			   longer an ERR_LENGTH the way a single required
			   handle was. Unlike WTITLE, an empty tag string is a
			   legitimate value here, not an error -- it's how a
			   window ends up untagged (plain assignment, not a
			   distinct "clear" branch; see GraphManager::
			   setTag()'s own comment). No-op per-handle if that
			   handle isn't currently open (gw_tag()'s own
			   convention, matching gw_title()).

			   A non-empty le that isn't character data is
			   ERR_DOMAIN (not_char_arg()) -- checked first,
			   before anything is copied or any window touched.
			   Catches the easy slip of writing the handle on the
			   wrong side (wnum ⎕WTAG 'tag'), which used to be
			   silently accepted: le's numeric bytes were copied
			   as if they were the tag, and re's character codes
			   were looked up as window handles (almost certainly
			   none open, so every gw_tag() call quietly
			   no-op'd). An empty le of any type still untags. */
			if (not_char_arg(le)) return(SET_ERR(ERR_DOMAIN));
			e = 1;
			if (le->ve_rank) e = (short)le->ve_rho[0];
			cp = (char *)LSCRATCH;
			if (e) memcpy(cp,DATA_AD(le),e);
			cp[e] = '\0';
			j = 0;
			while (j < SBOUNDS(re))
				gw_tag(get_data(re,j++),cp);
			swapdrop();
			return(0);
		case 16:	/* WTAGOF, ⎕WTAGOF wnum -- MONAD, reverse of
			   WTAG. Returns the window's current tag as a
			   character result (empty string if untagged or not
			   currently open -- the two aren't distinguished,
			   same "nothing to report" convention PEN's query/
			   WNUMS/WCLICK already use). Needs
			   Qt::BlockingQueuedConnection (gw_tagof()) since
			   tag state, like pen state, only exists on the
			   GUI-thread GraphWindow object -- same category as
			   gw_size()/gw_pen_query(), not a fire-and-forget
			   set path. Uses push_str(), same as sysfuncs.c's
			   ENV/CD/WSID cases -- the first character-result
			   case in this file, though not the first in the
			   interpreter as a whole. re is popped before
			   push_str(), same reasoning as WSIZE's
			   monadic-with-re case (the tag string is a wholly
			   new result, not derived from re the way a
			   swapdrop()'d DYAD reuses an operand). */
			if (!SBOUNDS(re)) return(SET_ERR(ERR_LENGTH));
			acc = get_data(re,0);		/* window # */
			cp = (char *)LSCRATCH;
			gw_tagof(acc,cp,LINE_SIZE);
			stack_pop(1);
			return(push_str(cp));
		case 17:	/* WFILL, wnum WFILL x,y  or  wnum WFILL
			   tol,x,y -- flood fill ("paint bucket") from a
			   single seed point, replacing every 4-connected
			   pixel matching the seed's starting color with the
			   window's current PEN color. le is a single window
			   handle only -- unlike WCTRL/PEN/WTAG's bulk lists,
			   filling the same seed point across several windows
			   with unrelated pixel content doesn't have an
			   obviously useful meaning, so this wasn't extended
			   that way.

			   re length 2 (x,y) -> tolerance 0 (exact color
			   match only -- correct and sufficient against this
			   module's own drawing primitives, which are all
			   confirmed non-antialiased: POLY/FPOLY/WPOINTS/
			   WCLEAR draw with hard edges, no blended pixels).

			   re length 3 (tol,x,y) -> tolerance is the FIRST
			   element, detected by length, not last -- added
			   specifically for WILOAD'd images, which can be
			   antialiased/photographic and rarely have
			   bit-identical adjacent pixels the way this
			   module's own primitives do. A candidate pixel is
			   included if every one of its r,g,b channels is
			   within tol of the SEED PIXEL's own original color
			   -- always compared against the fixed starting
			   color, never neighbor-to-neighbor, which is the
			   standard bucket-fill definition (matches Photoshop/
			   GIMP/Paint.NET) and avoids a fill "drifting" across
			   a smooth gradient one small step at a time if it
			   compared each new pixel to its immediate neighbor
			   instead. tol is on the same 0-255 per-channel scale
			   every other color value in this module already uses
			   (PEN's r,g,b, WCLEAR's packed color) -- no new
			   normalized/percentage scale introduced. A negative
			   tol is clamped to 0 (same permissive-clamp spirit as
			   PEN's width); tol >=255 harmlessly means "fill the
			   whole 4-connected region regardless of color," no
			   special-casing needed for that either.

			   4-CONNECTED, deliberately, not 8 -- confirmed, not
			   assumed: a real test against an actual POLY-style
			   drawn triangle with a shallow diagonal edge showed
			   4-connected fill stays correctly contained (896
			   pixels, no leak) while 8-connected leaks straight
			   through the diagonal edge's staircase gaps and
			   escapes the shape entirely (3040 pixels, hit the
			   canvas border). A non-antialiased 1px diagonal line
			   is NOT a fully 4-connected barrier on its own (its
			   own pixels can touch only diagonally at each
			   staircase step) -- 4-connected fill can't cross
			   those diagonal-only gaps either, which is exactly
			   why it stays contained; 8-connected fill treats
			   those same diagonal touches as open passages and
			   escapes through them.

			   Out-of-bounds seed point, or window not currently
			   open, is a silent no-op -- same convention every
			   other drawing primitive in this file already uses
			   (POLY/WPOINTS/FPOLY/WCLEAR/WTEXT all no-op on a
			   closed handle; there's only one seed point here, so
			   unlike WPIXEL's per-point bulk convention there's
			   nothing else in the call left to act on if it's
			   out of bounds). Fire-and-forget (Qt::QueuedConnection)
			   -- a drawing op, same category as POLY/FPOLY/
			   WPOINTS/WCLEAR, not a query. Side-effect-only:
			   swapdrop()+return(0), leaving le (the handle)
			   unchanged as the result, same as those four. */
			if (!SBOUNDS(le)) return(SET_ERR(ERR_LENGTH));
			acc = get_data(le,0);		/* window # */
			switch (SBOUNDS(re)) {
				case 2:	/* x,y -- tolerance 0 */
					gw_fill(acc,get_data(re,0),
						get_data(re,1),0L);
					break;
				case 3:	/* tol,x,y */
					gw_fill(acc,get_data(re,1),
						get_data(re,2),
						get_data(re,0));
					break;
				default:
					return(SET_ERR(ERR_LENGTH));
			}
			swapdrop();
			return(0);
		case 18:	/* WIRELOAD, 'filename' WIRELOAD wnum -- refresh
				   an already-open window's backing image in
				   place, same dimensions only (no resize path;
				   see apl_graphics_other_ideas_notes_v1.md's
				   WIRELOAD section for why that's explicitly
				   out of scope). Char le / numeric re, matching
				   WOPEN/WTEXT/WILOAD/WTITLE(post-flip)/WTAG's
				   char-le/numeric-re convention -- string
				   extraction mirrors those exactly, off le here.
				   Path resolution ($APLHOME/images/ unless
				   absolute) is identical to WILOAD's, reused via
				   the same is_absolute_path()/get_aplhome_dir()
				   calls, not reinvented.

				   wnum == ¯1 is a QUERY, a genuinely different
				   dispatch than every other ¯1 in this file --
				   every existing use (WOPEN's/WILOAD's x,y,
				   WSIZE's handle) means "use the system
				   default," a variation on the SAME action.
				   This one means "don't reload anything at all,
				   report the file's size instead" -- a
				   completely different return shape, not a
				   default flavor of the reload. Reports the
				   candidate file's width,height without
				   touching any window or the GUI thread at all
				   (see gw_query_image()'s own header comment)
				   -- cheaper than a real reload, not just a
				   variant of one -- so a caller can check fit
				   before attempting one, or build a filtered
				   list of fit-checked files. Decode failure
				   here returns an EMPTY vector, not
				   SET_ERR(ERR_IMGLOAD) -- mirrors WCLICK's/
				   WNUMS' own push_val(0,1,LSCRATCH)
				   "recoverable, not an error" convention,
				   chosen for consistency with the reload
				   branch's own no-SET_ERR philosophy below
				   rather than treating the two branches of one
				   case differently. Success returns a 2-element
				   width,height vector. Both operands are fully
				   consumed by this point (filename decoded,
				   ¯1 already read out of re) and the result is
				   a wholly new value unrelated to either, so
				   stack_pop(2)+push_val(), the same "new result"
				   pattern WOPEN/WSIZE/WCLICK use, not swapdrop().

				   Ordinary reload (wnum != ¯1): a deliberate
				   RETURN-VALUE EXCEPTION to every other
				   side-effect DYAD in this file. WTITLE/WTAG/
				   WFILL/WCLEAR all swapdrop() and silently no-op
				   on a closed handle -- decided in conversation
				   NOT to follow that here, because unlike a
				   title or a tag, a reload has three genuinely
				   distinct ways to fail and a caller needs an
				   actual signal, not silence -- closer in spirit
				   to WPIXEL's own "a wholly bad handle is a real
				   failure, not a silent no-op" precedent,
				   extended to this call's other two failure
				   modes as well. So: success returns wnum
				   itself (echoed back -- NOT the filename the
				   way swapdrop() would; this is the one case in
				   this file that breaks that reuse-an-operand
				   convention, on purpose). Failure returns one
				   of three negative codes instead of raising
				   SET_ERR at all -- specifically because closing
				   a graphics window via its title bar at the
				   wrong moment is an ordinary user action, not a
				   bug, and aborting the whole running program
				   over it (SET_ERR's usual effect) was judged
				   worse than letting the caller test the result,
				   the same "recoverable condition" philosophy
				   WCLICK already applies to its own closed-
				   mid-wait case:
				     ¯1  decode/format failure (bad path, corrupt
					 file, unsupported format -- same
					 failure WILOAD's own ERR_IMGLOAD case
					 above detects, just not raised as an
					 error here)
				     ¯2  wnum is not currently an open window
				     ¯3  the file decoded fine, but its
					 dimensions don't match the window's
					 current backing image size
				   All three are real runtime conditions a
				   caller is expected to check for (any negative
				   result means it didn't work; the specific
				   value only matters if the caller cares which
				   of the three), not syntax errors -- only a
				   genuinely malformed call (re missing the wnum
				   argument entirely, checked below) still
				   raises ERR_LENGTH. Also a stack_pop(2)+
				   push_val() case, not swapdrop() -- wnum
				   (success) or a negative code (failure) is a
				   wholly new scalar result, not le or re
				   reused/replaced in place.

				   A non-empty filename that isn't character
				   data IS raised, as ERR_DOMAIN
				   (not_char_arg()), checked first: that's a
				   malformed call (usually the handle written
				   on the wrong side), not one of the runtime
				   conditions above. */
			if (not_char_arg(le)) return(SET_ERR(ERR_DOMAIN));
			if (!SBOUNDS(re)) return(SET_ERR(ERR_LENGTH));
			e = 1;
			if (le->ve_rank) e = (short)le->ve_rho[0];
			cp = (char *)LSCRATCH;
			if (e) memcpy(cp,DATA_AD(le),e);
			cp[e] = '\0';
			if (is_absolute_path(cp)) {
				strncpy(pathbuf,cp,sizeof(pathbuf)-1);
				pathbuf[sizeof(pathbuf)-1] = '\0';
			} else {
				size_t used;

				get_aplhome_dir(pathbuf,sizeof(pathbuf),"images");
				used = strlen(pathbuf);
				/* a path that doesn't fit would name some other file:
				   clear it, so the call below fails the same way a
				   missing file does */
				if (snprintf(pathbuf+used,sizeof(pathbuf)-used,"%s",cp)
				    >= (int)(sizeof(pathbuf)-used))
					pathbuf[0] = '\0';
			}
			acc = get_data(re,0);	/* wnum, or ¯1 query sentinel */
			if (acc == -1L) {
				i = (short)gw_query_image(pathbuf,(int32_t *)LSCRATCH);
				stack_pop(2);
				if (!i) return(push_val(0,1,LSCRATCH));
				return(push_val(2,1,LSCRATCH));
			}
			iacc = (int32_t)gw_reload_image(acc,pathbuf);
			stack_pop(2);
			return(push_val(1,0,&iacc));
		case 19:	/* WISAVE, 'filename' WISAVE wnum  or
				   'filename' WISAVE wnum,¯1 -- dump wnum's
				   current backing image to a file. Char le /
				   numeric re, same convention as every other
				   filename+handle DYAD in this file (WOPEN/
				   WTEXT/WILOAD/WTITLE/WTAG/WIRELOAD). Format
				   is inferred from filename's extension by
				   QImage::save() itself -- no separate format
				   argument, same "no room for a third
				   argument" constraint WILOAD/WIRELOAD are
				   already under. Path resolution
				   ($APLHOME/images/ unless absolute) is the
				   same is_absolute_path()/get_aplhome_dir()
				   resolution those two already use, reused
				   here for the same "images live in one
				   default place" reason, just on the write
				   side instead of the read side.

				   re's element count picks the overwrite
				   policy -- same dispatch-by-length technique
				   PEN/WFILL already use in this file, not a
				   new pattern. 1 element (just wnum): FAIL if
				   the target file already exists (the
				   conservative default -- same spirit as
				   WFILL's own omitted-tolerance case defaulting
				   to the stricter behavior, exact match, not
				   the more permissive one). 2 elements
				   (wnum,mode): mode MUST be ¯1 to mean
				   "overwrite silently" -- any other second
				   value, or more than 2 elements, is treated
				   as a malformed call, ERR_LENGTH -- not
				   ERR_DOMAIN, since the problem here is really
				   "wrong number of meaningful arguments" (mode
				   isn't a value in some open range the way an
				   x,y coordinate is, it's an enumerated
				   selector with exactly one legal non-default
				   value) rather than a legal-shaped argument
				   with a bad value in it. Checked at THIS
				   layer, not left to GraphManager -- same
				   reason WCTRL's opcode is validated here
				   rather than left as GUI-thread-only defensive
				   checking.

				   Two genuinely different failure categories
				   here, reported two different ways --
				   deliberately NOT uniform the way WIRELOAD's
				   three failure modes are:
				     - wnum not currently open, or file exists
				       with overwrite not permitted -- both
				       ordinary, checkable runtime conditions
				       (the "window can legitimately disappear
				       mid-program" reasoning WIRELOAD's own ¯2
				       already established, and "caller asked
				       not to overwrite" is exactly that same
				       kind of expected, testable outcome) --
				       reported as a negative return value, ¯2
				       and ¯3 respectively, mirroring
				       WIRELOAD's own numbering for the same
				       underlying meanings where they line up.
				     - the disk write itself actually failing
				       (permissions, disk full, bad path) -- a
				       real environmental failure, not a race
				       with anything this program did, so THIS
				       one raises SET_ERR(ERR_SAVE) -- the
				       existing "Can't write save file" code
				       already used for other file-write
				       failures in this codebase, matching
				       precedent rather than inventing a new
				       code -- instead of returning ¯1 to the
				       caller as a value the way the other two
				       do.

				   A non-empty filename that isn't character
				   data is ERR_DOMAIN (not_char_arg()), checked
				   first -- a malformed call, not a runtime
				   condition. */
			if (not_char_arg(le)) return(SET_ERR(ERR_DOMAIN));
			if (!SBOUNDS(re)) return(SET_ERR(ERR_LENGTH));
			e = 1;
			if (le->ve_rank) e = (short)le->ve_rho[0];
			cp = (char *)LSCRATCH;
			if (e) memcpy(cp,DATA_AD(le),e);
			cp[e] = '\0';
			if (is_absolute_path(cp)) {
				strncpy(pathbuf,cp,sizeof(pathbuf)-1);
				pathbuf[sizeof(pathbuf)-1] = '\0';
			} else {
				size_t used;

				get_aplhome_dir(pathbuf,sizeof(pathbuf),"images");
				used = strlen(pathbuf);
				/* a path that doesn't fit would name some other file:
				   clear it, so the call below fails the same way a
				   missing file does */
				if (snprintf(pathbuf+used,sizeof(pathbuf)-used,"%s",cp)
				    >= (int)(sizeof(pathbuf)-used))
					pathbuf[0] = '\0';
			}
			acc = get_data(re,0);	/* wnum */
			switch (SBOUNDS(re)) {
				case 1:
					count = 0L;	/* fail if file exists
							   (default) -- reusing
							   'count' as this
							   case's overwrite
							   flag, not an element
							   count; nothing else
							   in this case needs
							   it */
					break;
				case 2:
					if (get_data(re,1) != -1L)
						return(SET_ERR(ERR_LENGTH));
					count = 1L;	/* ¯1 -> silent
							   overwrite */
					break;
				default:
					return(SET_ERR(ERR_LENGTH));
			}
			iacc = (int32_t)gw_save_image(acc,pathbuf,count);
			stack_pop(2);
			if (iacc == -1) return(SET_ERR(ERR_SAVE));
			return(push_val(1,0,&iacc));
		case 20:	/* FDIALOG, 'title' FDIALOG 'filter' -- opens a
			   native, modal "open existing file" dialog and
			   returns the chosen path. NOT window-scoped -- no
			   wnum anywhere in this call, deliberately: a file
			   dialog isn't tied to any particular GraphWindow,
			   same "two functions, two shapes" reasoning that
			   already kept sysfuncs.c's CTRL and this module's
			   own WCTRL separate rather than forcing a shared
			   shape. See graph_bridge.h's own comment on
			   gw_file_dialog() for the full threading rationale
			   (confirmed via a standalone spike, not assumed --
			   see GRAPHICS_NOTES.md).

			   le = title (may be empty -- Qt supplies its own
			   default, same "title may be empty" convention
			   WOPEN already establishes for its own title arg).
			   re = filter, in Qt's own native filter-string
			   syntax, optionally starting with a folder to open
			   in ('MIDI/SONG*.MID'; added Oct 21, 2026): that prefix
			   is split off and resolved by fdialog_folder()
			   (see its comment for the rule), and the rest goes
			   to Qt untranslated and unvalidated, as the whole
			   filter always did; empty means no restriction. A
			   filter with no folder prefix behaves exactly as
			   before. Neither argument has a length
			   requirement, unlike most other DYADs in this file
			   -- there's no fixed shape to enforce on either
			   side, just two character strings, so no
			   SET_ERR(ERR_LENGTH) check here.

			   Both extracted into their OWN local buffers
			   (titlebuf/filterbuf, see their declarations above),
			   never LSCRATCH directly -- this call's result also
			   lands in LSCRATCH moments later via push_str(),
			   same can't-share-a-buffer reasoning as WNUMS'
			   patbuf (case 4), not a new pattern. Bounds-checked
			   the same way patbuf's own extraction is, rather
			   than trusted the way WOPEN/WTITLE/WTEXT's
			   LSCRATCH-bound copies are (LSCRATCH is sized for
			   the whole line; these are small fixed local
			   arrays).

			   Single-file, open-existing-file mode only -- no
			   save mode, no directory picker, no multi-select in
			   this version.

			   Returns the selected path as a character result --
			   a wholly new result, not either operand, so
			   stack_pop(2)+push_str(), same shape as WTAGOF (case
			   16), not swapdrop(). Empty string on Cancel/Escape
			   -- deliberately not distinguished from each other,
			   same "nothing to report" convention as WCLICK's own
			   empty-vector result and WTAGOF's empty-string case.
			   No separate error/flag: a real accepted path can
			   never be empty, so the empty string alone is the
			   complete signal.

			   Either argument non-empty and not character data
			   is ERR_DOMAIN (not_char_arg()), checked before
			   either is extracted or the dialog opens. */
			if (not_char_arg(le) || not_char_arg(re))
				return(SET_ERR(ERR_DOMAIN));
			e = 1;
			if (le->ve_rank) e = (short)le->ve_rho[0];
			if (e >= (short)sizeof(titlebuf))
				e = (short)sizeof(titlebuf) - 1;
			if (e) memcpy(titlebuf,DATA_AD(le),e);
			titlebuf[e] = '\0';

			e = 1;
			if (re->ve_rank) e = (short)re->ve_rho[0];
			if (e >= (short)sizeof(filterbuf))
				e = (short)sizeof(filterbuf) - 1;
			if (e) memcpy(filterbuf,DATA_AD(re),e);
			filterbuf[e] = '\0';

			/* folder prefix, if any, into pathbuf (empty: Qt
			   chooses where to open); filterbuf keeps the rest */
			fdialog_folder(filterbuf,pathbuf,sizeof(pathbuf));

			cp = (char *)LSCRATCH;
			gw_file_dialog(titlebuf,pathbuf,filterbuf,cp,LINE_SIZE);
			stack_pop(2);
			return(push_str(cp));
		case 21:	/* WFONT -- per-window text face and size for
			   WTEXT (and what TWID measures with). Char le /
			   numeric re, same shape as WTEXT/WTITLE/WTAG, so the
			   handle sits on the right:
			     '' WFONT wnum             -> current size
			     '' WFONT wnum,size        -> set size
			     'face' WFONT wnum         -> set face, size kept
			     'face' WFONT wnum,size    -> set face and size
			     '?' WFONT wnum            -> resolved face name
			   Every set returns the PRIOR size (same set-returns-
			   prior convention as sysfuncs.c's IO/INTDIV/WI/DIG),
			   so the result is always a number except for the
			   '?' query, which returns a string -- the one mixed-
			   type result in this file, chosen over adding a
			   separate face-query function; the caller picked the
			   query, so it knows which it gets.

			   le (the face spec) is copied into facebuf, bounds-
			   checked (ERR_LENGTH at 256+ bytes, same treatment
			   as FDIALOG's strings, but an error rather than a
			   silent cut -- a truncated family name would quietly
			   select a different font), must be character data
			   if non-empty (ERR_DOMAIN otherwise), and has
			   leading/trailing spaces trimmed. Then:
			     empty  -> leave the face alone. Exactly the
			       pre-face behavior, so existing '' calls are
			       unaffected.
			     "?"    -> face QUERY. re must be exactly one
			       handle (ERR_LENGTH otherwise, same as PEN's
			       query), so it can't be half query, half size
			       change. Returns the family Qt actually resolved
			       (QFontInfo), in Qt's own spelling -- the only
			       way to see a silent substitution for a name
			       that isn't installed, or which entry of a
			       fallback list won. '' if the handle isn't open,
			       same as WTAGOF.
			     anything else -> SET the face. Comma-separated
			       fallback list, applied via QFont::setFamilies()
			       (same mechanism as the Help window's font
			       directive); names are matched ignoring case, so
			       uppercase-only typing works. The whole spec
			       "DEFAULT" (any case) resets to the app's default
			       face. Splitting/trimming and the DEFAULT check
			       happen in GraphManager::setFontFace(), same
			       division of labor as the size's -1 sentinel;
			       only the "names nothing at all" check (a spec
			       of just commas/spaces -> ERR_DOMAIN) is made
			       here, since errors can only be raised here. An
			       unknown name is NOT an error -- Qt substitutes,
			       and '?' shows what you got.
			   '?' and ',' confirmed to arrive as plain ASCII bytes
			   inside a quoted string (user ran ⎕AV⍳', ?*').

			   re, by element count (same technique as PEN):
			     1 (wnum) -> size query, face set, or face query,
			       per le above.
			     2 (wnum,size) -> set size (plus face if le names
			       one). size == -1 resets it to the default; any
			       other size < 1 is ERR_DOMAIN, checked before
			       anything is changed. No upper bound.
			     anything else -> ERR_LENGTH.
			   Size is in PIXELS, not points -- see
			   GraphWindow::fontPixelSize()'s comment for why. A
			   face-and-size set is two blocking calls (face, then
			   size) rather than one combined bridge function with
			   a new "size unchanged" sentinel; the face call's
			   return is already the prior size.

			   Single handle only, unlike PEN's bulk le list: the
			   result is a per-window value, and a bulk version
			   would need its own return-shape design -- same
			   reason PEN's own query stayed single-handle.

			   Handle not currently open: EMPTY vector result for
			   the numeric forms (nothing is changed by a set),
			   same "nothing to report" convention as PEN's query,
			   not WSIZE's error; '' for the face query. Wholly new
			   result either way -- stack_pop(2) before pushing,
			   same as PEN's query. All gw_ calls here are
			   Qt::BlockingQueuedConnection (the sets too, since
			   the prior value has to come back). */
			e = 1;
			if (le->ve_rank) {
				if (le->ve_rho[0] >= (int32_t)sizeof(facebuf))
					return(SET_ERR(ERR_LENGTH));
				e = (short)le->ve_rho[0];
			}
			if (e && (le->ve_vtype | VARIABLE_BIT) != QUOT_CODE)
				return(SET_ERR(ERR_DOMAIN));
			if (e) memcpy(facebuf,DATA_AD(le),e);
			facebuf[e] = '\0';
			cp = facebuf;
			while (*cp == ' ') cp++;
			i = (short)strlen(cp);
			while (i && cp[i-1] == ' ') cp[--i] = '\0';

			if (cp[0] == '?' && !cp[1]) {	/* face query */
				if (SBOUNDS(re) != 1) return(SET_ERR(ERR_LENGTH));
				gw_font_face_query(get_data(re,0),
					(char *)LSCRATCH,LINE_SIZE);
				stack_pop(2);
				return(push_str((char *)LSCRATCH));
			}

			if (*cp) {	/* face given: must name something */
				for (i = 0; cp[i]; i++)
					if (cp[i] != ' ' && cp[i] != ',') break;
				if (!cp[i]) return(SET_ERR(ERR_DOMAIN));
			}

			switch (SBOUNDS(re)) {
				case 1:	/* size query, or face set */
					if (*cp)
						acc = gw_font_face(get_data(re,0),cp);
					else
						acc = gw_font_query(get_data(re,0));
					break;
				case 2:	/* size set (+ face), returns prior */
					count = get_data(re,1);
					if (count < 1L && count != -1L)
						return(SET_ERR(ERR_DOMAIN));
					if (*cp) {
						acc = gw_font_face(get_data(re,0),cp);
						if (acc != -1L)
							gw_font(get_data(re,0),count);
					} else
						acc = gw_font(get_data(re,0),count);
					break;
				default:
					return(SET_ERR(ERR_LENGTH));
			}
			stack_pop(2);
			if (acc == -1L) return(push_val(0,1,LSCRATCH));
			break;		/* scalar result via the shared tail */
		case 22:	/* TWID, 'string' TWID wnum -- width in pixels
			   the string would occupy if drawn by WTEXT in that
			   window right now, using its current WFONT size.
			   Same char le / numeric re shape as WTEXT, and the
			   same le-string extraction (rho[0] is the LAST
			   dimension in this APL, i.e. a vector's length), so
			   TWID measures exactly the bytes WTEXT would draw.
			   gw_text_width() then does the same byte->QString
			   conversion gw_text() does and measures with the
			   same font GraphManager::text() draws with.

			   Horizontal advance (where following text would
			   start), rounded to a whole pixel -- what's needed
			   to place adjacent text or center a label. Scalar
			   integer result; an empty string gives 0.

			   le rank > 1 is ERR_RANK: WTEXT draws a single line,
			   so there's no matching multi-row case to measure.
			   Per-row widths could be added later as a pure
			   extension. A string of LINE_SIZE bytes or more is
			   ERR_LENGTH rather than silently truncated -- a
			   truncated measurement would be a wrong answer, and
			   LSCRATCH (the extraction buffer, same as WTEXT's)
			   holds at most LINE_SIZE bytes. The length check is
			   made on the int32_t rho BEFORE the (short) cast, so
			   a huge string can't wrap e negative.

			   re must be exactly one window handle (ERR_LENGTH
			   otherwise), same as PEN's query. Handle not
			   currently open: EMPTY vector result, same as
			   WFONT's. Wholly new result -- stack_pop(2) before
			   pushing. Blocking (gw_text_width()), so it's
			   ordered correctly after any earlier fire-and-forget
			   calls and any WFONT change.

			   A non-empty string that isn't character data is
			   ERR_DOMAIN (not_char_arg()), checked first. */
			if (not_char_arg(le)) return(SET_ERR(ERR_DOMAIN));
			if (le->ve_rank > 1) return(SET_ERR(ERR_RANK));
			if (SBOUNDS(re) != 1) return(SET_ERR(ERR_LENGTH));
			e = 1;
			if (le->ve_rank) {
				if (le->ve_rho[0] >= (int32_t)LINE_SIZE)
					return(SET_ERR(ERR_LENGTH));
				e = (short)le->ve_rho[0];
			}
			cp = (char *)LSCRATCH;
			if (e) memcpy(cp,DATA_AD(le),e);
			cp[e] = '\0';
			acc = gw_text_width(cp,get_data(re,0));
			stack_pop(2);
			if (acc == -1L) return(push_val(0,1,LSCRATCH));
			break;		/* scalar result via the shared tail */
		case 23:	/* WVIS, ⎕WVIS wnums -- MONAD. One visibility
			   state per handle in re, in order: 0 = hidden,
			   1 = shown, 2 = minimized, ¯1 = not currently open
			   (house "not open" sentinel, per entry -- a bad
			   handle never fails the whole call, same per-entry
			   approach WPIXEL's ¯1 for an out-of-bounds point
			   takes). Hidden wins over minimized: a window hidden
			   while minimized reports 0.

			   Always a vector result, even for one handle --
			   same as WPIXEL, so indexing into the result works
			   the same whether one handle or several was passed.
			   An empty re (e.g. ⎕WVIS ⎕WNUMS with no windows
			   open) gives an empty result, not an error: for a
			   query, "no handles in, no states out" is a
			   legitimate answer -- unlike WCLICK, where an empty
			   list would have nothing to wait on.

			   count via DATA_LEN, same as WCLICK (the closest
			   MONAD handle-list precedent). Input and output use
			   two SEPARATE regions of g_gscratch (input at [0],
			   output at [count]), sized with one
			   gscratch_ensure(2*count) call -- NOT WPIXEL's
			   in-place aliasing. Output here is the same length
			   as input, so it can't go in LSCRATCH (fixed small
			   cap) for a long list; and patbuf's comment above
			   already calls the aliasing pattern fragile enough
			   not to repeat, so the extra count int32_t's is the
			   cheaper choice.

			   Wholly new result, not derived from re -- re is
			   popped before push_val(), same as WCLICK/WTAGOF.
			   Blocking (gw_vis()), so it's ordered after any
			   earlier WCTRL 5/6 or WOPEN in the same program. */
			if (!SBOUNDS(re)) {
				stack_pop(1);
				return(push_val(0,1,LSCRATCH));
			}
			count = DATA_LEN(re);
			if (!gscratch_ensure(2 * count))
				return(SET_ERR(ERR_WSFULL));
			for (j = 0; j < count; j++)
				g_gscratch[j] = (int32_t)get_data(re,j);
			gw_vis(g_gscratch,count,g_gscratch + count);
			stack_pop(1);
			/* push_val()'s count is int32_t since V99 (was
			   short: see WPIXEL). */
			return(push_val((int32_t)count,1,g_gscratch + count));
		case 24:	/* WBLIT, dst ⎕WBLIT src[,dx,dy[,sx,sy,sw,sh]]
			   -- copies a rectangle of window src's backing
			   image into window dst's at dx,dy. Plain opaque
			   copy: destination pixels are replaced, nothing
			   blended (every backing image is RGB32 with no
			   alpha). No transparency/color key -- if that's
			   ever wanted it's a separate function (decided in
			   conversation: a keyed copy works differently
			   underneath, and folding a key into this call's
			   length-based dispatch would make the forms hard
			   to read).

			   le is the destination, a single window handle,
			   same as WFILL (a blit is naturally one-to-one; no
			   bulk form). re is the source handle first, then
			   the rest -- the form is picked by re's length,
			   same length-dispatch idiom as PEN/WFILL, and each
			   longer form only appends to the shorter one:
			     1  src                  whole src at 0,0
			     3  src,dx,dy            whole src at dx,dy
			     7  src,dx,dy,sx,sy,sw,sh  sw x sh rectangle at
			                            sx,sy in src -> dx,dy
			   Any other length is ERR_LENGTH. For forms 1 and 3,
			   gw_blit() gets sx,sy = 0,0 and sw,sh = -1,-1, its
			   internal "whole source image" sentinel.

			   A negative sw or sh in the 7-element form is
			   ERR_DOMAIN -- checkable here for free, before the
			   fire-and-forget hop, and almost certainly a bug
			   in the caller; it also guarantees a real size can
			   never collide with the -1 sentinel above. A zero
			   size just copies nothing. Everything else that can
			   go wrong is NOT an error, matching every other
			   fire-and-forget drawing call: a rectangle partly
			   or wholly outside either image is clipped
			   silently (including negative dx,dy), and a handle
			   that isn't open makes the whole call a no-op.
			   src == dst is allowed (overlap handled in
			   GraphManager::blit()). Shown/hidden doesn't matter
			   for either window.

			   Fire-and-forget (gw_blit(), Qt::QueuedConnection),
			   decided in conversation for animation speed --
			   queued calls still run in posting order, so a
			   frame drawn into a hidden buffer is always complete
			   before a WBLIT posted after it copies it.
			   Side-effect-only: swapdrop()+return(0), leaving le
			   (dst) as the result, same as WFILL/WCLEAR/WTEXT. */
			if (!SBOUNDS(le)) return(SET_ERR(ERR_LENGTH));
			acc = get_data(le,0);		/* destination window # */
			switch (SBOUNDS(re)) {
				case 1:	/* src */
					gw_blit(acc,get_data(re,0),0L,0L,
						0L,0L,-1L,-1L);
					break;
				case 3:	/* src,dx,dy */
					gw_blit(acc,get_data(re,0),
						get_data(re,1),get_data(re,2),
						0L,0L,-1L,-1L);
					break;
				case 7:	/* src,dx,dy,sx,sy,sw,sh */
					if (get_data(re,5) < 0 || get_data(re,6) < 0)
						return(SET_ERR(ERR_DOMAIN));
					gw_blit(acc,get_data(re,0),
						get_data(re,1),get_data(re,2),
						get_data(re,3),get_data(re,4),
						get_data(re,5),get_data(re,6));
					break;
				default:
					return(SET_ERR(ERR_LENGTH));
			}
			swapdrop();
			return(0);
		case CLEANUP_FUNC:
			return(0);
		default:
			return(SET_ERR(ERR_SYSTEM));
	}
	/* Narrow through a real int32_t rather than taking acc's address
	   directly -- push_val() wants int32_t*, acc is long. Fixes the
	   compiler warning, and unlike the pointer-reinterpret this
	   replaces, this is also correct on big-endian: an explicit
	   narrowing conversion always yields the right 32-bit value,
	   where reading raw bytes through a mismatched pointer type only
	   happened to work on little-endian. Scoped to just this shared
	   return -- not attempting the same fix anywhere else in the
	   codebase that may use the same idiom (see prior discussion). */
	iacc = (int32_t)acc;
	return(push_val(1,0,&iacc));
}
